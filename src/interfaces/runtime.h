/*****************************************************************************\
 *  runtime.h - job runtime plugin interface.
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

#ifndef _INTERFACES_RUNTIME_H
#define _INTERFACES_RUNTIME_H

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>

#include "src/common/data.h"
#include "src/common/slurm_opt.h"

#include "src/interfaces/data_parser.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

struct option;

/* Forward declaration to avoid pulling slurmd.h into this header. */
typedef struct slurmd_config slurmd_conf_t;

/*
 * Index into the arrays of loaded runtime plugins. Index 0 is a permanently
 * empty placeholder, so a zero-initialized index means no runtime resolved.
 */
#define RUNTIME_IDX_INVALID 0

typedef enum {
	RUNTIME_CTXT_INVALID = 0,
	RUNTIME_CTXT_SLURMD,
	RUNTIME_CTXT_SLURMSTEPD,
	RUNTIME_CTXT_SALLOC,
	RUNTIME_CTXT_SBATCH,
	RUNTIME_CTXT_SCRONTAB,
	RUNTIME_CTXT_SCRUN,
	RUNTIME_CTXT_SLURMRESTD,
	RUNTIME_CTXT_SRUN,
	RUNTIME_CTXT_INVALID_MAX
} runtime_context_t;

/*
 * Initialize the runtime plugin.
 * IN plugin_name - Plugin name or NULL for default
 * IN context - Calling context for plugin
 * OUT idx_ptr - index of the loaded plugin, RUNTIME_IDX_INVALID on failure,
 *	or NULL when the caller has no use for the index
 * RET SLURM_SUCCESS or error. Does not return if the plugin's init() fails.
 */
extern int runtime_g_init(const char *plugin_name, runtime_context_t context,
			  int *idx_ptr);
extern void runtime_g_fini(void);

/*
 * The accessors below read the plugin arrays without holding the interface's
 * lock, while loading a plugin grows them with xrecalloc() under it, which may
 * move them and free the block a reader is walking. Every plugin a caller
 * intends to use must therefore be loaded before the first call to any of
 * them. slurmstepd meets this by loading its one plugin in _init_from_slurmd()
 * before any thread exists; a caller that wants to load plugins concurrently
 * with these calls needs the arrays to stop moving first.
 */

/*
 * Set up the runtime for the step. Runs in slurmstepd.
 * IN idx - index of the loaded plugin to set up
 */
extern int runtime_g_setup(const int idx, slurmd_conf_t *conf,
			   stepd_step_rec_t *step, slurm_addr_t *cli,
			   slurm_msg_t *msg);

/*
 * Clean up the runtime for the step. Runs in slurmstepd.
 * IN idx - index of the loaded plugin to clean up
 */
extern void runtime_g_cleanup(const int idx, slurmd_conf_t *conf,
			      stepd_step_rec_t *step);

/*
 * Prepare the runtime for the specified task. Runs in slurmstepd.
 * IN idx - index of the loaded plugin to prepare
 */
extern void runtime_g_task_init(const int idx, slurmd_conf_t *conf,
				stepd_step_rec_t *step,
				stepd_step_task_info_t *task);

/*
 * Run the specified task within the runtime. Runs in slurmstepd.
 * The plugin execs the task and only returns on failure, or returns
 * ESLURM_NOT_SUPPORTED without exec'ing to have the caller exec the task.
 * IN idx - index of the loaded plugin to run the task within
 * RET ESLURM_NOT_SUPPORTED if the caller must exec the task, otherwise the
 *	errno from the failed exec
 */
extern int runtime_g_run(const int idx, slurmd_conf_t *conf,
			 stepd_step_rec_t *step, stepd_step_task_info_t *task);

typedef struct runtime_arg_s runtime_arg_t;

/*
 * Callback for a registered argument.
 * IN arg - argument being processed
 * IN het_job_inx - heterogeneous component the value belongs to, or
 *	NO_VAL when early_pass leaves it unresolved
 * IN value - argument value, or NULL
 * IN set_by_cli/set_by_env/set_by_data - source of the value
 * IN early_pass - true on a tolerant pass that must not apply the value
 * RET true when the value was accepted
 */
typedef bool (*runtime_arg_callback_t)(const runtime_arg_t *arg,
				       const uint32_t het_job_inx,
				       data_t *value, bool set_by_cli,
				       bool set_by_env, bool set_by_data,
				       bool early_pass);

typedef struct runtime_arg_s {
	/*
	 * CLI argument name, registered with getopt_long() as
	 * optional_argument, so a value has to be given as
	 * --argument=value.
	 */
	const char *argument;

	/* CLI environment variable names */
	struct {
		/* Environment specific to salloc */
		const char *salloc;
		/* Environment specific to sbatch */
		const char *sbatch;
		/* Global Slurm name */
		const char *slurm;
		/* Environment specific to srun */
		const char *srun;
	} environment;

	/* Field name to be used in data_parsers */
	const char *field;
	/* Field description */
	const char *field_desc;
	/* Data parser type for field value */
	data_parser_type_t field_type;
	/* One-line description for --help, or NULL to use field_desc */
	const char *help;
	/* Name for the value in --help, or NULL for "value" */
	const char *help_value;
	/* Callback when argument found during processing */
	runtime_arg_callback_t on_arg;
	/* Callback when argument found during processing during early_pass */
	runtime_arg_callback_t on_arg_early_pass;
} runtime_arg_t;

/*
 * Register runtime argument
 * IN idx - Plugin id requesting registration
 * IN arg - Pointer to argument to register
 */
extern void runtime_register_arg(const int idx, const runtime_arg_t *arg);

/*
 * Process a CLI argument that may belong to a runtime plugin.
 * IN idx - index of the plugin the caller is parsing for
 * IN het_job_inx - heterogeneous component the argument belongs to, which is
 *	ignored during early_pass
 * IN optval - argument identifier handed back by getopt_long()
 * IN arg - argument value, or NULL
 * IN set_by_env - true when the value came from the environment
 * IN early_pass - true on a tolerant pass that must not apply the value
 * RET SLURM_SUCCESS when a runtime handled the argument, ENOENT when it is
 *	not a runtime argument, or SLURM_ERROR (logged) when it belongs to a
 *	runtime other than idx or, outside the early pass, its value is rejected
 */
extern int runtime_g_cli_process_arg(const int idx, const uint32_t het_job_inx,
				     int optval, const char *arg,
				     bool set_by_env, bool early_pass);

/*
 * Process the environment variables of every registered runtime argument.
 * IN idx - index of the plugin the caller is parsing for, which is ignored
 *	during early_pass
 * IN het_job_inx - heterogeneous component the variables belong to, which is
 *	ignored during early_pass
 * IN context - calling context, which picks the environment.srun,
 *	environment.sbatch or environment.salloc name
 * IN envp - environment to read, or NULL for the process environment
 * IN early_pass - true to only load the runtime SRUN_RUNTIME, SBATCH_RUNTIME
 *	or SALLOC_RUNTIME picks, ignoring every other variable
 * RET false when a runtime rejected a value or could not be loaded
 */
extern bool runtime_g_cli_process_env(const int idx, const uint32_t het_job_inx,
				      runtime_context_t context, char ***envp,
				      bool early_pass);

/*
 * Copy a getopt() option table, adding every registered runtime argument.
 * IN optz - option table to copy, which the caller still owns
 * RET newly allocated table to be freed with slurm_option_table_destroy(),
 *	or NULL on failure
 */
extern struct option *runtime_cli_add_args(struct option *optz);

/*
 * Print a --help section for each loaded runtime that provides options, as
 * spank_print_options() does for SPANK plugins.
 * IN out - stream to print to
 * IN left_pad - columns before each option
 * IN width - column the option descriptions start at
 */
extern void runtime_g_cli_print_help(FILE *out, int left_pad, int width);

/*
 * Print "[--option=value]" for each option the loaded runtimes provide,
 * wrapped at 80 columns as --usage wraps the built-in options. The value is
 * always named "value", as runtimes registering the same option may name it
 * differently.
 * IN out - stream to print to
 * IN indent - column each line starts at
 */
extern void runtime_g_cli_print_usage(FILE *out, int indent);

/*
 * Apply the loaded runtime to a job description.
 * IN idx - index of the plugin to apply, or RUNTIME_IDX_INVALID for none
 * IN het_job_inx - heterogeneous component being applied
 * IN context - calling context for the plugin
 * IN/OUT job_desc - job description to apply the runtime to
 * RET SLURM_SUCCESS, or an error from the plugin
 *
 * A plugin exports environment variables through runtime_g_cli_apply_env()
 * instead: sbatch rebuilds job_desc->environment after this call.
 */
extern int runtime_g_cli_apply_job_desc(const int idx,
					const uint32_t het_job_inx,
					runtime_context_t context,
					job_desc_msg_t *job_desc);

/*
 * Apply the loaded runtime to a step request.
 * IN idx - index of the plugin to apply, or RUNTIME_IDX_INVALID for none
 * IN het_job_inx - heterogeneous component being applied
 * IN context - calling context for the plugin
 * IN/OUT step_req - step request to apply the runtime to
 * RET SLURM_SUCCESS, or an error from the plugin
 */
extern int runtime_g_cli_apply_step_req(const int idx,
					const uint32_t het_job_inx,
					runtime_context_t context,
					step_create_req_msg_t *step_req);

/*
 * Apply the loaded runtime to the step launch parameters.
 * IN idx - index of the plugin to apply, or RUNTIME_IDX_INVALID for none
 * IN het_job_inx - heterogeneous component being applied
 * IN context - calling context for the plugin
 * IN/OUT launch_params - launch parameters to apply the runtime to
 * RET SLURM_SUCCESS, or an error from the plugin
 *
 * Pointer fields are aliases the caller does not free, so a plugin hands
 * back storage it keeps owning.
 */
extern int runtime_g_cli_apply_launch_params(const int idx,
					     const uint32_t het_job_inx,
					     runtime_context_t context,
					     slurm_step_launch_params_t
						     *launch_params);

/*
 * Apply the loaded runtime to an environment.
 * IN idx - index of the plugin to apply, or RUNTIME_IDX_INVALID for none
 * IN het_job_inx - heterogeneous component whose arguments are exported
 * IN het_job_offset - NO_VAL to export the plain names, or the component to
 *	export <name>_HET_GROUP_<het_job_offset> for, in an environment that
 *	every component of a heterogeneous job shares
 * IN context - calling context for the plugin
 * IN child - false to export the names context reads itself (e.g.
 *	environment.srun), true to export the names of what context starts
 *	(e.g. environment.slurm for the tasks of srun). Nothing is exported
 *	when context has no such name.
 * IN/OUT envp - environment to apply the runtime to, or NULL for the
 *	process environment
 * RET SLURM_SUCCESS, or an error from the plugin
 */
extern int runtime_g_cli_apply_env(const int idx, const uint32_t het_job_inx,
				   const uint32_t het_job_offset,
				   runtime_context_t context, bool child,
				   char ***envp);

/*
 * Apply the loaded runtime to the shell command that starts salloc's
 * interactive step (LaunchParameters=use_interactive_step). Only that srun
 * sees what the plugin adds, unlike what runtime_g_cli_apply_env() exports.
 * IN idx - index of the plugin to apply, or RUNTIME_IDX_INVALID for none
 * IN het_job_inx - heterogeneous component being applied, or NO_VAL when the
 *	job is not heterogeneous
 * IN context - calling context for the plugin
 * IN/OUT command - xmalloc()ed command line, which a plugin may xfree() and
 *	replace, e.g. to put an environment assignment in front of the srun
 * RET SLURM_SUCCESS, or an error from the plugin
 */
extern int runtime_g_cli_apply_interactive_step(const int idx,
						const uint32_t het_job_inx,
						runtime_context_t context,
						char **command);

/*
 * Drop the arguments every loaded runtime holds for a component, so they do
 * not outlive the parsing pass that set them.
 * IN het_job_inx - heterogeneous component being parsed
 */
extern void runtime_g_cli_reset_args(const uint32_t het_job_inx);

#endif
