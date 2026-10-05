/*****************************************************************************\
 *  runtime.c - job runtime plugin interface.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <pthread.h>

#include "slurm/slurm_errno.h"

#include "src/common/env.h"
#include "src/common/macros.h"
#include "src/common/optz.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/runtime.h"

#define PLUGIN_TYPE "runtime"
#define MAX_ARGS 24
#define MAX_LINKED_ARGS 5

/*
 * Base for the optvals handed to getopt_long() for runtime arguments.
 *
 * The LONG_OPT_* enum starts at 0x100 and ends well below this, and SPANK
 * counts its own optvals up from 0xfff, so the MAX_ARGS values starting here
 * cannot collide with either. runtime_g_cli_process_arg() maps an optval
 * back to its args[] entry by subtracting this.
 */
#define RUNTIME_OPTVAL_START 0x800

typedef struct {
	int (*init)(const int idx, runtime_context_t context);
	int (*fini)(void);
	int (*setup)(slurmd_conf_t *conf, stepd_step_rec_t *step,
		     slurm_addr_t *cli, slurm_msg_t *msg);
	void (*cleanup)(slurmd_conf_t *conf, stepd_step_rec_t *step);
	void (*task_init)(slurmd_conf_t *conf, stepd_step_rec_t *step,
			  stepd_step_task_info_t *task);
	int (*run)(slurmd_conf_t *conf, stepd_step_rec_t *step,
		   stepd_step_task_info_t *task);
	int (*cli_apply_job_desc)(const uint32_t het_job_inx,
				  runtime_context_t context,
				  job_desc_msg_t *job_desc);
	int (*cli_apply_step_req)(const uint32_t het_job_inx,
				  runtime_context_t context,
				  step_create_req_msg_t *step_req);
	void (*cli_reset_args)(const uint32_t het_job_inx);
	int (*cli_apply_launch_params)(const uint32_t het_job_inx,
				       runtime_context_t context,
				       slurm_step_launch_params_t
					       *launch_params);
	int (*cli_apply_env)(const uint32_t het_job_inx,
			     const uint32_t het_job_offset,
			     runtime_context_t context, bool child,
			     char ***envp);
	int (*cli_apply_interactive_step)(const uint32_t het_job_inx,
					  runtime_context_t context,
					  char **command);
} opts_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for opts_t.
 */
static const char *syms[] = {
	"runtime_p_init",
	"runtime_p_fini",
	"runtime_p_setup",
	"runtime_p_cleanup",
	"runtime_p_task_init",
	"runtime_p_run",
	"runtime_p_cli_apply_job_desc",
	"runtime_p_cli_apply_step_req",
	"runtime_p_cli_reset_args",
	"runtime_p_cli_apply_launch_params",
	"runtime_p_cli_apply_env",
	"runtime_p_cli_apply_interactive_step",
};

/*
 * Plugins are loaded into parallel arrays and referenced by index, so that a
 * job can select a different runtime for each heterogeneous component.
 *
 * Index 0 is a permanently empty placeholder, so a zero-initialized index
 * means "no runtime resolved" without any caller having to say so.
 *
 * The single-context caller (slurmstepd) loads exactly one plugin and keeps the
 * index it was loaded at, with plugin_inited saying whether anything has been
 * loaded yet.
 */
static opts_t *ops = NULL;
static plugin_context_t **g_context = NULL;
/* Fully qualified type of each loaded plugin, e.g. "runtime/oci". */
static char **loaded_types = NULL;
static int g_context_cnt = 0;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	/* Argument from plugin */
	const runtime_arg_t *arg;
	/* Owning plugin ID for argument */
	int idx;
} runtime_arg_link_t;

typedef struct {
	/*
	 * Every runtime that registered this argument, ending at the first
	 * RUNTIME_IDX_INVALID.
	 */
	runtime_arg_link_t args[MAX_LINKED_ARGS];
} runtime_arg_reg_t;

/* Array of all registered args, indexed by optval */
static runtime_arg_reg_t args[MAX_ARGS];
static int args_count = 0;

/* IN full_type - fully qualified plugin type. init_lock must be held. */
static int _find_loaded(const char *full_type)
{
	for (int i = 1; i < g_context_cnt; i++)
		if (!xstrcmp(loaded_types[i], full_type))
			return i;

	return RUNTIME_IDX_INVALID;
}

/*
 * Load a runtime plugin, or find one already loaded.
 * IN plugin_name - plugin name, or NULL for DefRuntimePlugin
 * IN context - calling context handed to the plugin
 * OUT idx_ptr - index of the plugin, or RUNTIME_IDX_INVALID on failure
 * RET SLURM_SUCCESS or an error
 */
static int _load_runtime(const char *plugin_name, runtime_context_t context,
			 int *idx_ptr)
{
	const char *plugin_type = PLUGIN_TYPE;
	const char *type = plugin_name;
	char *full_type = NULL;
	int idx = RUNTIME_IDX_INVALID;
	int rc = SLURM_SUCCESS;

	if (!type || !type[0])
		type = slurm_conf.def_runtime_plugin;
	if (!type || !type[0])
		type = DEFAULT_RUNTIME_PLUGIN;

	/* Accept both "oci" and the full "runtime/oci" plugin name. */
	if (!xstrncmp(type, "runtime/", 8))
		type += 8;
	full_type = xstrdup_printf("%s/%s", plugin_type, type);

	slurm_mutex_lock(&init_lock);

	if ((idx = _find_loaded(full_type)) != RUNTIME_IDX_INVALID)
		goto done;

	/*
	 * g_context_cnt is one past the highest loaded index, so it is also
	 * the next free slot. Index 0 stays an empty placeholder.
	 */
	if (g_context_cnt)
		idx = g_context_cnt;
	else
		idx = (RUNTIME_IDX_INVALID + 1);

	xrecalloc(ops, (idx + 1), sizeof(*ops));
	xrecalloc(g_context, (idx + 1), sizeof(*g_context));
	xrecalloc(loaded_types, (idx + 1), sizeof(*loaded_types));

	if (!(g_context[idx] = plugin_context_create(plugin_type, full_type,
						     (void **) &ops[idx], syms,
						     sizeof(syms)))) {
		error("%s: cannot create %s context for %s",
		      __func__, plugin_type, full_type);
		rc = ESLURM_PLUGIN_INVALID;
		goto done;
	}

	slurm_mutex_unlock(&init_lock);

	/* A runtime may have registered arguments before failing */
	if ((rc = ops[idx].init(idx, context)))
		fatal("%s: %s init() failed: %s",
		      __func__, full_type, slurm_strerror(rc));

	slurm_mutex_lock(&init_lock);

	SWAP(loaded_types[idx], full_type);
	g_context_cnt = (idx + 1);

done:
	if (!rc)
		plugin_inited = PLUGIN_INITED;

	slurm_mutex_unlock(&init_lock);
	xfree(full_type);

	if (idx_ptr)
		*idx_ptr = (rc ? RUNTIME_IDX_INVALID : idx);

	return rc;
}

extern int runtime_g_init(const char *plugin_name, runtime_context_t context,
			  int *idx_ptr)
{
	/*
	 * _load_runtime() hands back the index of an already loaded plugin
	 * rather than loading it twice, so repeating a name is harmless. It
	 * also sets plugin_inited itself, under init_lock, so a concurrent
	 * reader never observes it flip true before g_context_cnt/ops are
	 * fully published.
	 */
	return _load_runtime(plugin_name, context, idx_ptr);
}

extern void runtime_g_fini(void)
{
	slurm_mutex_lock(&init_lock);

	for (int i = 1; i < g_context_cnt; i++) {
		int rc = EINVAL;

		if (!g_context[i])
			continue;

		ops[i].fini();

		if ((rc = plugin_context_destroy(g_context[i])))
			fatal_abort("%s: plugin_context_destroy() failed: %s",
				__func__, slurm_strerror(rc));

		xfree(loaded_types[i]);
	}

	xfree(ops);
	xfree(g_context);
	xfree(loaded_types);

	memset(args, 0, sizeof(args));
	args_count = 0;
	g_context_cnt = 0;
	plugin_inited = PLUGIN_NOT_INITED;

	slurm_mutex_unlock(&init_lock);
}

extern int runtime_g_setup(const int idx, slurmd_conf_t *conf,
			   stepd_step_rec_t *step, slurm_addr_t *cli,
			   slurm_msg_t *msg)
{
	xassert(plugin_inited == PLUGIN_INITED);
	xassert(idx > RUNTIME_IDX_INVALID);
	xassert(idx < g_context_cnt);

	return ops[idx].setup(conf, step, cli, msg);
}

extern void runtime_g_cleanup(const int idx, slurmd_conf_t *conf,
			      stepd_step_rec_t *step)
{
	xassert(plugin_inited == PLUGIN_INITED);
	xassert(idx > RUNTIME_IDX_INVALID);
	xassert(idx < g_context_cnt);

	ops[idx].cleanup(conf, step);
}

extern void runtime_g_task_init(const int idx, slurmd_conf_t *conf,
				stepd_step_rec_t *step,
				stepd_step_task_info_t *task)
{
	xassert(plugin_inited == PLUGIN_INITED);
	xassert(idx > RUNTIME_IDX_INVALID);
	xassert(idx < g_context_cnt);

	ops[idx].task_init(conf, step, task);
}

extern int runtime_g_run(const int idx, slurmd_conf_t *conf,
			 stepd_step_rec_t *step, stepd_step_task_info_t *task)
{
	int rc;

	xassert(plugin_inited == PLUGIN_INITED);
	xassert(idx > RUNTIME_IDX_INVALID);
	xassert(idx < g_context_cnt);

	rc = ops[idx].run(conf, step, task);

	/* The plugin execs the task or returns ESLURM_NOT_SUPPORTED. */
	xassert(rc != SLURM_SUCCESS);

	return rc;
}

extern void runtime_register_arg(const int idx, const runtime_arg_t *arg)
{
	runtime_arg_reg_t *reg = NULL;
	int j;

	xassert(arg);
	xassert(arg->argument);

	slurm_mutex_lock(&init_lock);

	/*
	 * getopt_long() can only tell one argument name from another, so a
	 * name already registered joins that optval instead of taking one
	 * of its own.
	 */
	for (int i = 0; i < args_count; i++) {
		if (!xstrcmp(args[i].args[0].arg->argument, arg->argument)) {
			reg = &args[i];
			break;
		}
	}

	if (!reg) {
		if (args_count == ARRAY_SIZE(args))
			fatal("Too many runtime arguments registered");

		reg = &args[args_count++];
	}

	for (j = 0; j < ARRAY_SIZE(reg->args); j++) {
		if (reg->args[j].idx == RUNTIME_IDX_INVALID)
			break;
	}

	if (j == ARRAY_SIZE(reg->args))
		fatal("Too many runtimes registered --%s", arg->argument);

	reg->args[j] = (runtime_arg_link_t) {
		.arg = arg,
		.idx = idx,
	};

	slurm_mutex_unlock(&init_lock);
}

/*
 * Find the registration of args[i] that a value is handed to.
 * IN i - index into args[]
 * IN idx - runtime to find, or RUNTIME_IDX_INVALID for the first to register
 * RET the registration, or NULL when idx did not register the argument
 */
static const runtime_arg_link_t *_find_link(const int i, const int idx)
{
	/* Every runtime that registered this argument shares the same optval */
	for (int j = 0; j < ARRAY_SIZE(args[i].args); j++) {
		if (args[i].args[j].idx == RUNTIME_IDX_INVALID)
			break;

		if ((idx == RUNTIME_IDX_INVALID) ||
		    (args[i].args[j].idx == idx))
			return &args[i].args[j];
	}

	return NULL;
}

extern int runtime_g_cli_process_arg(const int idx, const uint32_t het_job_inx,
				     int optval, const char *arg,
				     bool set_by_env, bool early_pass)
{
	const int i = (optval - RUNTIME_OPTVAL_START);
	/* The early pass runs before the runtime and component are resolved */
	const int arg_idx = (early_pass ? RUNTIME_IDX_INVALID : idx);
	const uint32_t arg_het_job_inx = (early_pass ? NO_VAL : het_job_inx);
	const runtime_arg_link_t *link = NULL;
	const runtime_arg_t *rarg = NULL;
	runtime_arg_callback_t callback = NULL;
	data_t *value = NULL;
	int rc = SLURM_SUCCESS;

	/*
	 * runtime_cli_add_args() gave getopt_long() (RUNTIME_OPTVAL_START + i)
	 * for args[i], so the index comes straight back out of the optval.
	 * Short options and the LONG_OPT_* enum sit below the base and SPANK
	 * counts up from above the end of the table, so both land outside
	 * args_count and are left to the caller.
	 */
	if (i < 0)
		return ENOENT;

	if (i >= args_count)
		return ENOENT;

	if ((link = _find_link(i, arg_idx)))
		rarg = link->arg;

	if (!rarg) {
		xassert(arg_idx > RUNTIME_IDX_INVALID);
		xassert(arg_idx < g_context_cnt);

		/*
		 * Ignore environment variables that don't apply to this plugin
		 */
		if (set_by_env) {
			debug2("%s: Ignoring environment --%s for %s",
			       __func__, args[i].args[0].arg->argument,
			       loaded_types[arg_idx]);
			return SLURM_SUCCESS;
		}

		error("--%s is not an option of %s",
		      args[i].args[0].arg->argument, loaded_types[arg_idx]);
		return SLURM_ERROR;
	}

	callback = (early_pass ? rarg->on_arg_early_pass : rarg->on_arg);

	if (arg)
		value = data_set_string(data_new(), arg);

	/*
	 * The argument is ours on both passes. A plugin that registers no
	 * callback for this pass is ignoring the value rather than declining
	 * it, so claim the argument instead of letting SPANK reject it.
	 */
	if (callback && !callback(rarg, arg_het_job_inx, value, !set_by_env,
				  set_by_env, false, early_pass)) {
		error("--%s rejected value: %s", rarg->argument,
		      (arg ? arg : "(null)"));
		/* The early pass may ask a runtime the job will not use */
		if (!early_pass)
			rc = SLURM_ERROR;
	}

	FREE_NULL_DATA(value);
	return rc;
}

/* RET the environment variable an argument is read from in context, or NULL */
static const char *_env_name(const runtime_arg_t *arg,
			     runtime_context_t context)
{
	switch (context) {
	case RUNTIME_CTXT_SRUN:
		return arg->environment.srun;
	case RUNTIME_CTXT_SBATCH:
		return arg->environment.sbatch;
	case RUNTIME_CTXT_SALLOC:
		return arg->environment.salloc;
	case RUNTIME_CTXT_SCRUN:
		return arg->environment.scrun;
	default:
		return NULL;
	}
}

/*
 * Hand args[i] the value of one environment variable, if it is set.
 * IN i - index into args[]
 * IN idx - index of the plugin the caller is parsing for
 * IN het_job_inx - heterogeneous component the value belongs to
 * IN envp - environment to read, or NULL for the process environment
 * IN name - environment variable to read
 * RET false when the runtime rejected the value
 */
static bool _process_env_var(const int i, const int idx,
			     const uint32_t het_job_inx, char ***envp,
			     const char *name)
{
	const char *val = (envp ? getenvp(*envp, name) : getenv(name));

	if (!val)
		return true;

	if (!runtime_g_cli_process_arg(idx, het_job_inx,
				       (RUNTIME_OPTVAL_START + i), val, true,
				       false))
		return true;

	error("unable to process environment variable %s=%s", name, val);
	return false;
}

/* RET the environment variable that picks the runtime in context, or NULL */
static const char *_env_runtime_name(runtime_context_t context)
{
	switch (context) {
	case RUNTIME_CTXT_SRUN:
		return "SRUN_RUNTIME";
	case RUNTIME_CTXT_SBATCH:
		return "SBATCH_RUNTIME";
	case RUNTIME_CTXT_SALLOC:
		return "SALLOC_RUNTIME";
	default:
		return NULL;
	}
}

/*
 * Load the runtime the environment picks, so that its arguments are
 * registered before an option table is built.
 * IN context - calling context, which picks the variable name
 * IN envp - environment to read, or NULL for the process environment
 * RET false when the runtime could not be loaded
 */
static bool _load_env_runtime(runtime_context_t context, char ***envp)
{
	const char *name = _env_runtime_name(context);
	const char *val = NULL;
	int rc = SLURM_SUCCESS;

	if (!name || !(val = (envp ? getenvp(*envp, name) : getenv(name))))
		return true;

	/* "list" names no plugin */
	if (!xstrcasecmp(val, "list"))
		return true;

	if ((rc = runtime_g_init(val, context, NULL))) {
		error("unable to load runtime %s=%s: %s", name, val,
		      slurm_strerror(rc));
		return false;
	}

	return true;
}

extern bool runtime_g_cli_process_env(const int idx, const uint32_t het_job_inx,
				      runtime_context_t context, char ***envp,
				      bool early_pass)
{
	/* The early pass runs before the runtime and component are resolved */
	if (early_pass)
		return _load_env_runtime(context, envp);

	xassert(het_job_inx != NO_VAL);

	for (int i = 0; i < args_count; i++) {
		const runtime_arg_link_t *link = _find_link(i, idx);
		const char *name = NULL;
		char het_name[256];

		/* Variables of a runtime other than idx are ignored */
		if (!link || !(name = _env_name(link->arg, context)))
			continue;

		if (!_process_env_var(i, idx, het_job_inx, envp, name))
			return false;

		/* Each name also has a per-component form */
		snprintf(het_name, sizeof(het_name), "%s_HET_GROUP_%u", name,
			 het_job_inx);

		if (!_process_env_var(i, idx, het_job_inx, envp, het_name))
			return false;
	}

	return true;
}

extern int runtime_g_cli_apply_job_desc(const int idx,
					const uint32_t het_job_inx,
					runtime_context_t context,
					job_desc_msg_t *job_desc)
{
	xassert(job_desc);

	if (idx == RUNTIME_IDX_INVALID)
		return SLURM_SUCCESS;

	xassert(idx < g_context_cnt);
	xassert(ops[idx].cli_apply_job_desc);

	return ops[idx].cli_apply_job_desc(het_job_inx, context, job_desc);
}

extern int runtime_g_cli_apply_step_req(const int idx,
					const uint32_t het_job_inx,
					runtime_context_t context,
					step_create_req_msg_t *step_req)
{
	xassert(step_req);

	if (idx == RUNTIME_IDX_INVALID)
		return SLURM_SUCCESS;

	xassert(idx < g_context_cnt);
	xassert(ops[idx].cli_apply_step_req);

	return ops[idx].cli_apply_step_req(het_job_inx, context, step_req);
}

extern int runtime_g_cli_apply_launch_params(const int idx,
					     const uint32_t het_job_inx,
					     runtime_context_t context,
					     slurm_step_launch_params_t
						     *launch_params)
{
	xassert(launch_params);

	if (idx == RUNTIME_IDX_INVALID)
		return SLURM_SUCCESS;

	xassert(idx < g_context_cnt);
	xassert(ops[idx].cli_apply_launch_params);

	return ops[idx].cli_apply_launch_params(het_job_inx, context,
						launch_params);
}

extern int runtime_g_cli_apply_env(const int idx, const uint32_t het_job_inx,
				   const uint32_t het_job_offset,
				   runtime_context_t context, bool child,
				   char ***envp)
{
	xassert(het_job_inx != NO_VAL);

	if (idx == RUNTIME_IDX_INVALID)
		return SLURM_SUCCESS;

	xassert(idx < g_context_cnt);
	xassert(ops[idx].cli_apply_env);

	return ops[idx].cli_apply_env(het_job_inx, het_job_offset, context,
				      child, envp);
}

extern int runtime_g_cli_apply_interactive_step(const int idx,
						const uint32_t het_job_inx,
						runtime_context_t context,
						char **command)
{
	if (idx == RUNTIME_IDX_INVALID)
		return SLURM_SUCCESS;

	xassert(command && *command);

	xassert(idx < g_context_cnt);
	xassert(ops[idx].cli_apply_interactive_step);

	return ops[idx].cli_apply_interactive_step(het_job_inx, context,
						   command);
}

extern void runtime_g_cli_reset_args(const uint32_t het_job_inx)
{
	/* Every loaded plugin holds its own arguments. */
	for (int i = 1; i < g_context_cnt; i++) {
		if (g_context[i])
			ops[i].cli_reset_args(het_job_inx);
	}
}

extern struct option *runtime_cli_add_args(struct option *optz)
{
	struct option *runtime_optz = optz_create();

	if (optz && (optz_append(&runtime_optz, optz) < 0)) {
		error("%s: unable to copy option table", __func__);
		optz_destroy(runtime_optz);
		return NULL;
	}

	slurm_mutex_lock(&init_lock);

	for (int i = 0; i < args_count; i++) {
		/*
		 * Runtimes loaded together share one entry per name, so only
		 * optional serves a flag and a value alike.
		 */
		struct option opt = {
			.name = args[i].args[0].arg->argument,
			.has_arg = optional_argument,
			.flag = NULL,
			.val = (RUNTIME_OPTVAL_START + i),
		};

		/*
		 * A clash leaves the argument out of this table rather than
		 * failing the whole thing, matching how SPANK handles one.
		 */
		if (optz_add(&runtime_optz, &opt) < 0) {
			if (errno == EEXIST)
				debug("%s: ignoring runtime argument --%s that conflicts with an existing option",
				      __func__, opt.name);
			else
				debug("%s: unable to add runtime argument --%s",
				      __func__, opt.name);
		}
	}

	slurm_mutex_unlock(&init_lock);

	return runtime_optz;
}

/* RET the --help description of arg */
static const char *_arg_help(const runtime_arg_t *arg)
{
	if (arg->help)
		return arg->help;

	return (arg->field_desc ? arg->field_desc : "");
}

/* RET the name --help gives the value of arg */
static const char *_arg_value(const runtime_arg_t *arg)
{
	return (arg->help_value ? arg->help_value : "value");
}

extern void runtime_g_cli_print_help(FILE *out, int left_pad, int width)
{
	slurm_mutex_lock(&init_lock);

	for (int idx = (RUNTIME_IDX_INVALID + 1); idx < g_context_cnt; idx++) {
		bool header = false;

		for (int i = 0; i < args_count; i++) {
			const runtime_arg_link_t *link = _find_link(i, idx);
			char *opt = NULL;

			if (!link)
				continue;

			if (!header) {
				fprintf(out, "\nOptions provided by %s:\n",
					loaded_types[idx]);
				header = true;
			}

			opt = xstrdup_printf("%*s--%s=%s", left_pad, "",
					     link->arg->argument,
					     _arg_value(link->arg));

			/* A too-wide option gets its own line */
			if (strlen(opt) < (size_t) width)
				fprintf(out, "%-*s%s\n", width, opt,
					_arg_help(link->arg));
			else
				fprintf(out, "%s\n%*s%s\n", opt, width, "",
					_arg_help(link->arg));

			xfree(opt);
		}
	}

	slurm_mutex_unlock(&init_lock);
}

extern void runtime_g_cli_print_usage(FILE *out, int indent)
{
	int col = 0;

	slurm_mutex_lock(&init_lock);

	/* Each name once, however many runtimes registered it */
	for (int i = 0; i < args_count; i++) {
		const runtime_arg_t *arg = args[i].args[0].arg;
		char *item = xstrdup_printf("[--%s=value]", arg->argument);
		const int len = strlen(item);

		if (col && ((col + 1 + len) > 80)) {
			fprintf(out, "\n");
			col = 0;
		}

		if (!col)
			col = fprintf(out, "%*s%s", indent, "", item);
		else
			col += fprintf(out, " %s", item);

		xfree(item);
	}

	if (col)
		fprintf(out, "\n");

	slurm_mutex_unlock(&init_lock);
}
