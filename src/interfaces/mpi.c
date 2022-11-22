/*****************************************************************************\
 *  slurm_mpi.c - Generic MPI selector for Slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondo1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "src/common/slurm_xlator.h"
#include "src/common/env.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/interfaces/mpi.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct slurm_mpi_ops {
	uint32_t (*plugin_id);
	int (*client_fini)(mpi_plugin_client_state_t *state);
	mpi_plugin_client_state_t *(*client_prelaunch)(
		const mpi_step_info_t *mpi_step, char ***env);
	s_p_hashtbl_t *(*conf_get)(void);
	List (*conf_get_printable)(void);
	void (*conf_options)(s_p_options_t **full_options,
			     int *full_options_cnt);
	void (*conf_set)(s_p_hashtbl_t *tbl);
	int (*slurmstepd_prefork)(const stepd_step_rec_t *step, char ***env);
	int (*slurmstepd_task)(const mpi_task_info_t *mpi_task, char ***env);
} slurm_mpi_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_mpi_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"mpi_p_client_fini",
	"mpi_p_client_prelaunch",
	"mpi_p_conf_get",
	"mpi_p_conf_get_printable",
	"mpi_p_conf_options",
	"mpi_p_conf_set",
	"mpi_p_slurmstepd_prefork",
	"mpi_p_slurmstepd_task"
};

/*
 * Can't be "static char *plugin_type". Conflicting declaration: log.h
 * Can't be "#define PLUGIN_TYPE". Previous declaration: plugin.h
 */
static char *mpi_char = "mpi";
static slurm_mpi_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static buf_t **mpi_confs = NULL;
static int g_context_cnt = 0;
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t client_plugin_id = 0;

static void _log_env(char **env)
{
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_MPI) || !env)
		return;

	log_flag(MPI, "ENVIRONMENT");
	log_flag(MPI, "-----------");
	for (int i = 0; env[i]; i++)
		log_flag(MPI, "%s", env[i]);
	log_flag(MPI, "-----------");
}

static void _log_step_rec(const stepd_step_rec_t *step)
{
	int i;

	xassert(step);

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_MPI))
		return;

	log_flag(MPI, "STEPD_STEP_REC");
	log_flag(MPI, "--------------");
	log_flag(MPI, "%ps", &step->step_id);
	log_flag(MPI, "ntasks:%u nnodes:%u node_id:%u", step->ntasks,
		 step->nnodes, step->nodeid);
	log_flag(MPI, "node_tasks:%u", step->node_tasks);
	for (i = 0; i < step->node_tasks; i++)
		log_flag(MPI, "gtid[%d]:%u", i, step->task[i]->gtid);
	for (i = 0; i < step->nnodes; i++)
		log_flag(MPI, "task_cnts[%d]:%u", i, step->task_cnts[i]);

	if ((step->het_job_id != 0) && (step->het_job_id != NO_VAL))
		log_flag(MPI, "het_job_id:%u", step->het_job_id);

	if (step->het_job_offset != NO_VAL) {
		log_flag(MPI, "het_job_ntasks:%u het_job_nnodes:%u",
			 step->het_job_ntasks, step->het_job_nnodes);
		log_flag(MPI, "het_job_node_offset:%u het_job_task_offset:%u",
			 step->het_job_offset, step->het_job_task_offset);
		for (i = 0; i < step->het_job_nnodes; i++)
			log_flag(MPI, "het_job_task_cnts[%d]:%u", i,
				 step->het_job_task_cnts[i]);
		log_flag(MPI, "het_job_node_list:%s", step->het_job_node_list);
	}
	log_flag(MPI, "--------------");
}

static void _log_mpi_rec(const mpi_step_info_t *mpi_step)
{
	slurm_step_layout_t *layout;

	xassert(mpi_step);

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_MPI))
		return;

	log_flag(MPI, "----------------------");
	log_flag(MPI, "MPI_STEP_INFO");
	log_flag(MPI, "%ps", &mpi_step->step_id);
	if ((mpi_step->het_job_id != 0) && (mpi_step->het_job_id != NO_VAL)) {
		log_flag(MPI, "het_job_id:%u", mpi_step->het_job_id);
	}
	if ((layout = mpi_step->step_layout)) {
		log_flag(MPI, "node_cnt:%u task_cnt:%u", layout->node_cnt,
			 layout->task_cnt);
		log_flag(MPI, "node_list:%s", layout->node_list);
		log_flag(MPI, "plane_size:%u task_dist:%u", layout->plane_size,
			 layout->task_dist);
		for (int i = 0; i < layout->node_cnt; i++) {
			log_flag(MPI, "tasks[%d]:%u", i, layout->tasks[i]);
			for (int j = 0; j < layout->tasks[i]; j++) {
				log_flag(MPI, "tids[%d][%d]:%u", i, j,
					 layout->tids[i][j]);
			}
		}
	}
	log_flag(MPI, "----------------------");
}

static void _log_task_rec(const mpi_task_info_t *mpi_task)
{
	xassert(mpi_task);

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_MPI))
		return;

	log_flag(MPI, "MPI_TASK_INFO");
	log_flag(MPI, "--------------------");
	log_flag(MPI, "%ps", &mpi_task->step_id);
	log_flag(MPI, "nnodes:%u node_id:%u",
		 mpi_task->nnodes, mpi_task->nodeid);
	log_flag(MPI, "ntasks:%u local_tasks:%u",
		 mpi_task->ntasks, mpi_task->ltasks);
	log_flag(MPI, "global_task_id:%u local_task_id:%u", mpi_task->gtaskid,
		 mpi_task->ltaskid);
	log_flag(MPI, "--------------------");
}

static int _match_keys(void *x, void *y)
{
	config_key_pair_t *key_pair1 = x, *key_pair2 = y;

	xassert(key_pair1);
	xassert(key_pair2);

	return !xstrcmp(key_pair1->name, key_pair2->name);
}

static char *_plugin_type(int index)
{
	xassert(index > -1);
	xassert(index < g_context_cnt);
	xassert(g_context);

	return &((xstrchr(g_context[index]->type, '/'))[1]);
}

static int _plugin_idx(uint32_t plugin_id)
{
	xassert(g_context_cnt);

	for (int i = 0; i < g_context_cnt; i++)
		if (*(ops[i].plugin_id) == plugin_id)
			return i;
	return -1;
}

static int _load_plugin(void *x, void *arg)
{
	char *plugin_name = x;

	xassert(plugin_name);
	xassert(g_context);

	g_context[g_context_cnt] = plugin_context_create(
		mpi_char, plugin_name, (void **)&ops[g_context_cnt],
		syms, sizeof(syms));

	if (g_context[g_context_cnt])
		g_context_cnt++;
	else
		error("MPI: Cannot create context for %s", plugin_name);

	return SLURM_SUCCESS;
}

static int _mpi_fini_locked(void)
{
	int rc = SLURM_SUCCESS;

	/* Conf cleanup */
	if (mpi_confs) {
		for (int i = 0; i < g_context_cnt; i++)
			FREE_NULL_BUFFER(mpi_confs[i]);

		xfree(mpi_confs);
	}

	/* Plugin cleanup */
	for (int i = 0; i < g_context_cnt; i++)
		if ((rc =
		     plugin_context_destroy(g_context[i])) != SLURM_SUCCESS)
			error("MPI: Unable to destroy context plugin.");

	xfree(g_context);
	xfree(ops);
	g_context_cnt = 0;

	return rc;
}

static int _mpi_init_locked(char **mpi_type)
{
	int count = 0, *opts_cnt;
	List plugin_names;
	s_p_hashtbl_t **all_tbls, *tbl;
	s_p_options_t **opts;
	char *conf_path;
	struct stat buf;

	/* Plugin load */

	/* NULL in the double pointer means load all, otherwise load just one */
	if (mpi_type) {
		debug("MPI: Type: %s", *mpi_type);

		if (!slurm_conf.mpi_default) {
			error("MPI: No default type set.");
			return SLURM_ERROR;
		} else if (!*mpi_type)
			*mpi_type = xstrdup(slurm_conf.mpi_default);
		/*
		 * The openmpi plugin has been equivalent to none for a while.
		 * Translate so we can discard that duplicated no-op plugin.
		 */
		if (!xstrcmp(*mpi_type, "openmpi")) {
			xfree(*mpi_type);
			*mpi_type = xstrdup("none");
		}

		plugin_names = list_create(xfree_ptr);
		list_append(plugin_names,
			    xstrdup_printf("%s/%s", mpi_char, *mpi_type));
	} else {
		debug("MPI: Loading all types");

		plugin_names = plugin_get_plugins_of_type(mpi_char);

		/*
		 * 2 versions after 22.05 this running_in_slurmctld() check can
		 * be removed. Until then we still need to have the double load
		 * of the symlink just in case a 21.08 srun is talking to a
		 * 22.05+ slurmd.
		 */
		if (running_in_slurmctld()) {
			/* Remove the PMIx symlink, so we don't load it twice */
			list_delete_first(plugin_names,
					  slurm_find_char_exact_in_list,
					  "mpi/pmix");
		}
	}

	/* Iterate and load */
	if (plugin_names && (count = list_count(plugin_names))) {
		ops = xcalloc(count, sizeof(*ops));
		g_context = xcalloc(count, sizeof(*g_context));

		list_for_each(plugin_names, _load_plugin, NULL);
	}
	FREE_NULL_LIST(plugin_names);

	if (!g_context_cnt) {
		/* No plugin could load: clean */
		_mpi_fini_locked();
		error("MPI: Unable to load any plugin");
		return SLURM_ERROR;
	} else if (g_context_cnt < count) {
		/* Some could load but not all: shrink */
		xrecalloc(ops, g_context_cnt, sizeof(*ops));
		xrecalloc(g_context, g_context_cnt, sizeof(*g_context));
	} else if (mpi_type)
		setenvf(NULL, "SLURM_MPI_TYPE", "%s", *mpi_type);

	/* Conf load */

	xassert(ops);
	/* Stepd section, else daemons section */
	if (mpi_type) {
		/* Unpack & load the plugin with received config from slurmd */
		if (mpi_confs) {
			xassert(mpi_confs[0]);

			if ((tbl = s_p_unpack_hashtbl(mpi_confs[0]))) {
				(*(ops[0].conf_set))(tbl);
				s_p_hashtbl_destroy(tbl);
			} else {
				s_p_hashtbl_destroy(tbl);
				_mpi_fini_locked();
				error("MPI: Unable to unpack config for %s.",
				      *mpi_type);
				return SLURM_ERROR;
			}
		}
		client_plugin_id = *(ops[0].plugin_id);
		/* If no config, continue with default values */
	} else {
		/* Read config from file and apply to all loaded plugin(s) */
		opts = xcalloc(g_context_cnt, sizeof(*opts));
		opts_cnt = xcalloc(g_context_cnt, sizeof(*opts_cnt));
		all_tbls = xcalloc(g_context_cnt, sizeof(*all_tbls));

		/* Get options from all plugins */
		for (int i = 0; i < g_context_cnt; i++) {
			(*(ops[i].conf_options))(&opts[i], &opts_cnt[i]);
			if (!opts[i])
				continue;

			/*
			 * For the NULL at the end. Just in case the plugin
			 * forgot to add it.
			 */
			xrealloc(opts[i], ((opts_cnt[i] + 1) * sizeof(**opts)));

			all_tbls[i] = s_p_hashtbl_create(opts[i]);
		}

		/* Read mpi.conf and fetch only values from plugins' options */
		if (!(conf_path = get_extra_conf_path("mpi.conf")) ||
		    stat(conf_path, &buf))
			debug2("No mpi.conf file (%s)", conf_path);
		else {
			debug2("Reading mpi.conf file (%s)", conf_path);
			for (int i = 0; i < g_context_cnt; i++) {
				if (!all_tbls[i])
					continue;
				if (s_p_parse_file(all_tbls[i],
						   NULL, conf_path,
						   true, NULL) != SLURM_SUCCESS)
					/*
					 * conf_path can't be freed: It's needed
					 * by fatal and fatal will exit
					 */
					fatal("Could not open/read/parse "
					      "mpi.conf file %s. Many times "
					      "this is because you have "
					      "defined options for plugins "
					      "that are not loaded. Please "
					      "check your slurm.conf file and "
					      "make sure the plugins for the "
					      "options listed are loaded.",
					      conf_path);
			}
		}

		xfree(conf_path);

		mpi_confs = xcalloc(g_context_cnt, sizeof(*mpi_confs));
		count = 0;

		/* Validate and set values for affected options */
		for (int i = 0; i < g_context_cnt; i++) {
			/* Check plugin accepts specified values for configs */
			(*(ops[i].conf_set))(all_tbls[i]);

			/*
			 * Pack the config for later usage. If plugin config
			 * table exists, pack it. If it doesn't exist,
			 * mpi_confs[i] is NULL.
			 */
			if ((tbl = (*(ops[i].conf_get))())) {
				mpi_confs[i] = s_p_pack_hashtbl(tbl, opts[i],
								opts_cnt[i]);
				if (mpi_confs[i]) {
					if (get_buf_offset(mpi_confs[i]))
						count++;
					else
						FREE_NULL_BUFFER(mpi_confs[i]);
				}
				s_p_hashtbl_destroy(tbl);
			}
		}
		/* No plugin has config, clean it */
		if (!count)
			xfree(mpi_confs);

		/* Cleanup for temporal variables */
		for (int i = 0; i < g_context_cnt; i++) {
			for (int j = 0; j < opts_cnt[i]; j++)
				xfree(opts[i][j].key);
			xfree(opts[i]);
			s_p_hashtbl_destroy(all_tbls[i]);
		}
		xfree(opts);
		xfree(opts_cnt);
		xfree(all_tbls);
	}

	return SLURM_SUCCESS;
}

static int _mpi_init(char **mpi_type)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&context_lock);

	if (!g_context)
		rc = _mpi_init_locked(mpi_type);

	slurm_mutex_unlock(&context_lock);
	return rc;
}

extern int mpi_process_env(char ***env)
{
	int rc = SLURM_SUCCESS;
	char *mpi_type;

	xassert(env);

	if (!(mpi_type = getenvp(*env, "SLURM_MPI_TYPE"))) {
		error("MPI: SLURM_MPI_TYPE environmental variable is not set.");
		rc = SLURM_ERROR;
		goto done;
	}

	log_flag(MPI, "%s: Environment before call:", __func__);
	_log_env(*env);

	/*
	 * Unset env var so that "none" doesn't exist in salloc'ed env, but
	 * still keep it in srun if not none.
	 */
	if (!xstrcmp(mpi_type, "none"))
		unsetenvp(*env, "SLURM_MPI_TYPE");

done:
	return rc;
}

extern int mpi_g_slurmstepd_prefork(const stepd_step_rec_t *step, char ***env)
{
	xassert(step);
	xassert(env);
	xassert(g_context);
	xassert(ops);

	log_flag(MPI, "%s: Details before call:", __func__);
	_log_env(*env);
	_log_step_rec(step);

	return (*(ops[0].slurmstepd_prefork))(step, env);
}

extern int mpi_g_slurmstepd_task(const mpi_task_info_t *mpi_task, char ***env)
{
	xassert(mpi_task);
	xassert(env);
	xassert(g_context);
	xassert(ops);

	log_flag(MPI, "%s: Details before call:", __func__);
	_log_env(*env);
	_log_task_rec(mpi_task);

	return (*(ops[0].slurmstepd_task))(mpi_task, env);
}

extern int mpi_g_client_init(char **mpi_type)
{
	_mpi_init(mpi_type);

	return client_plugin_id;
}

extern mpi_plugin_client_state_t *mpi_g_client_prelaunch(
	const mpi_step_info_t *mpi_step, char ***env)
{
	mpi_plugin_client_state_t *state;

	xassert(mpi_step);
	xassert(env);
	xassert(g_context);
	xassert(ops);

	log_flag(MPI, "%s: Details before call:", __func__);
	_log_env(*env);
	_log_mpi_rec(mpi_step);

	state = (*(ops[0].client_prelaunch))(mpi_step, env);

	log_flag(MPI, "%s: Environment after call:", __func__);
	_log_env(*env);

	return state;
}

extern int mpi_g_client_fini(mpi_plugin_client_state_t *state)
{
	xassert(g_context);
	xassert(ops);

	log_flag(MPI, "%s called", __func__);

	return (*(ops[0].client_fini))(state);
}

extern int mpi_g_daemon_init(void)
{
	return _mpi_init(NULL);
}

extern int mpi_g_daemon_reconfig(void)
{
	int rc;

	slurm_mutex_lock(&context_lock);

	if (g_context)
		_mpi_fini_locked();

	rc = _mpi_init_locked(NULL);

	slurm_mutex_unlock(&context_lock);
	return rc;
}

extern List mpi_g_conf_get_printable(void)
{
	List opts_list, opts;

	slurm_mutex_lock(&context_lock);

	xassert(g_context_cnt);
	xassert(g_context);
	xassert(ops);

	opts_list = list_create(destroy_config_key_pair);

	for (int i = 0; i < g_context_cnt; i++) {
		opts = (*(ops[i].conf_get_printable))();

		if (opts) {
			list_transfer_unique(opts_list, _match_keys, opts);
			FREE_NULL_LIST(opts);
		}
	}

	if (!list_count(opts_list))
		FREE_NULL_LIST(opts_list);
	else
		list_sort(opts_list, sort_key_pairs);

	slurm_mutex_unlock(&context_lock);
	return opts_list;
}

extern int mpi_conf_send_stepd(int fd, uint32_t plugin_id)
{
	int index;
	bool have_conf;
	uint32_t len = 0, ns;
	char *mpi_type;

	/* 0 shouldn't ever happen here. */
	xassert(plugin_id);

	slurm_mutex_lock(&context_lock);

	if ((index = _plugin_idx(plugin_id)) < 0)
		goto rwfail;

	/* send the type over */
	mpi_type = _plugin_type(index);
	len = strlen(mpi_type);
	safe_write(fd, &len, sizeof(len));
	safe_write(fd, mpi_type, len);

	if ((have_conf = (mpi_confs && mpi_confs[index])))
		len = get_buf_offset(mpi_confs[index]);
	else
		len = 0;

	ns = htonl(len);
	safe_write(fd, &ns, sizeof(ns));
	if (have_conf)
		safe_write(fd, get_buf_data(mpi_confs[index]), len);

	slurm_mutex_unlock(&context_lock);
	return SLURM_SUCCESS;
rwfail:
	slurm_mutex_unlock(&context_lock);
	return SLURM_ERROR;
}

extern int mpi_conf_recv_stepd(int fd)
{
	uint32_t len;
	char *mpi_type = NULL;
	buf_t *buf = NULL;
	int rc;

	safe_read(fd, &len, sizeof(len));
	xassert(len);
	mpi_type = xmalloc(len+1);
	safe_read(fd, mpi_type, len);

	safe_read(fd, &len, sizeof(len));
	len = ntohl(len);

	/* We have no conf for this specific plugin. Sender sent empty buffer */
	if (len) {
		buf = init_buf(len);
		safe_read(fd, get_buf_data(buf), len);

		slurm_mutex_lock(&context_lock);

		/*
		 * As we are in the stepd, only 1 plugin is loaded, and we
		 * always receive this conf before the plugin gets loaded
		 */
		mpi_confs = xcalloc(1, sizeof(*mpi_confs));
		mpi_confs[0] = buf;

		rc = _mpi_init_locked(&mpi_type);

		slurm_mutex_unlock(&context_lock);

		if (rc != SLURM_SUCCESS)
			goto rwfail;
	} else if (_mpi_init(&mpi_type) != SLURM_SUCCESS)
		goto rwfail;

	xfree(mpi_type);

	return SLURM_SUCCESS;
rwfail:
	xfree(mpi_type);
	FREE_NULL_BUFFER(buf);
	return SLURM_ERROR;
}

extern int mpi_id_from_plugin_type(char *mpi_type)
{
	int id = -1;
	xassert(g_context_cnt);

	slurm_mutex_lock(&context_lock);
	for (int i = 0; i < g_context_cnt; i++) {
		if (!xstrcmp(_plugin_type(i), mpi_type)) {
			id = *(ops[i].plugin_id);
			break;
		}
	}
	slurm_mutex_unlock(&context_lock);

	return id;
}

extern int mpi_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&context_lock);

	if (g_context)
		rc = _mpi_fini_locked();

	slurm_mutex_unlock(&context_lock);
	return rc;
}
