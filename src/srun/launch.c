 /*  launch.c - Define job launch plugin functions.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <stdlib.h>
#include <fcntl.h>

#include "src/srun/launch.h"

#include "src/common/env.h"
#include "src/common/xstring.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/xsignal.h"

typedef struct {
	int (*setup_srun_opt)      (char **rest);
	int (*create_job_step)     (srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job);
	int (*step_launch)         (srun_job_t *job,
				    slurm_step_io_fds_t *cio_fds,
				    uint32_t *global_rc, bool got_alloc,
				    bool *srun_shutdown);
	int (*step_terminate)      (void);
	void (*print_status)       (void);
	void (*fwd_signal)         (int signal);
} plugin_ops_t;

typedef struct {
	char	       *type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		plugin_errno;
	plugin_ops_t    ops;
} plugin_context_t;

static plugin_context_t *plugin_context = NULL;
static pthread_mutex_t	 plugin_context_lock =
	PTHREAD_MUTEX_INITIALIZER;

/*
 * Locate and load the appropriate plugin
 */
static plugin_ops_t * _get_ops(plugin_context_t *c)
{
	/*
	 * Must be synchronized with plugin_ops_t above.
	 */
	static const char *syms[] = {
		"launch_p_setup_srun_opt",
		"launch_p_create_job_step",
		"launch_p_step_launch",
		"launch_p_step_terminate",
		"launch_p_print_status",
		"launch_p_fwd_signal"
	};
	int n_syms = sizeof(syms) / sizeof(char *);
	char *plugin_type = "launch";

	/* Find the correct plugin. */
	c->cur_plugin = plugin_load_and_link(c->type, n_syms, syms,
					     (void **) &c->ops);
	if (c->cur_plugin != PLUGIN_INVALID_HANDLE)
		return &c->ops;

	if (errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      c->type, plugin_strerror(errno));
		return NULL;
	}

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->type);

	/* Get plugin list. */
	if (!c->plugin_list) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if (!c->plugin_list) {
			error("cannot create plugin manager");
			return NULL;
		}
		plugrack_set_major_type(c->plugin_list, plugin_type);
		plugrack_set_paranoia(c->plugin_list,
				      PLUGRACK_PARANOIA_NONE,
				      0);
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir(c->plugin_list, plugin_dir);
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type(c->plugin_list, c->type);
	if (c->cur_plugin == PLUGIN_INVALID_HANDLE) {
		error("cannot find %s plugin for %s", plugin_type, c->type);
		return NULL;
	}

	/* Dereference the API. */
	if (plugin_get_syms(c->cur_plugin, n_syms, syms,
			    (void **) &c->ops) < n_syms ) {
		error("incomplete %s plugin detected", plugin_type);
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a context
 */
static plugin_context_t *_context_create(const char *type)
{
	plugin_context_t *c;

	if (!type) {
		debug3("_context_create: no uler type");
		return NULL;
	}

	c = xmalloc(sizeof(plugin_context_t));
	c->type = xstrdup(type);
	c->plugin_list = NULL;
	c->cur_plugin = PLUGIN_INVALID_HANDLE;
	c->plugin_errno = SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a context
 */
static int _context_destroy(plugin_context_t *c)
{
	int rc = SLURM_SUCCESS;
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if (c->plugin_list) {
		if (plugrack_destroy(c->plugin_list) != SLURM_SUCCESS)
			rc = SLURM_ERROR;
	} else
		plugin_unload(c->cur_plugin);

	xfree(c->type);
	xfree(c);

	return rc;
}

/*
 * Initialize context for plugin
 */
extern int launch_init(void)
{
	int retval = SLURM_SUCCESS;
	char *type = NULL;

	slurm_mutex_lock(&plugin_context_lock);

	if (plugin_context)
		goto done;

	type = slurm_get_launch_type();
	if (!(plugin_context = _context_create(type))) {
		error("cannot create launch context for %s", type);
		retval = SLURM_ERROR;
		goto done;
	}

	if (!_get_ops(plugin_context)) {
		error("cannot resolve location plugin operations");
		_context_destroy(plugin_context);
		plugin_context = NULL;
		retval = SLURM_ERROR;
	}

done:
	slurm_mutex_unlock(&plugin_context_lock);
	xfree(type);

	return retval;
}

extern int location_fini(void)
{
	int rc;

	if (!plugin_context)
		return SLURM_SUCCESS;

	rc = _context_destroy(plugin_context);
	plugin_context = NULL;

	return rc;
}

extern slurm_step_layout_t *launch_common_get_slurm_step_layout(srun_job_t *job)
{
	job_step_create_response_msg_t *resp;

	if (!job || !job->step_ctx)
		return (NULL);

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_RESP, &resp);
	if (!resp)
	    return (NULL);
	return (resp->step_layout);
}

extern int launch_common_create_job_step(srun_job_t *job, bool use_all_cpus,
					 void (*signal_function)(int),
					 sig_atomic_t *destroy_job)
{
	int i, rc;
	unsigned long my_sleep = 0;
	time_t begin_time;

	if (!job) {
		error("launch_common_create_job_step: no job given");
		return SLURM_ERROR;
	}

	slurm_step_ctx_params_t_init(&job->ctx_params);
	job->ctx_params.job_id = job->jobid;
	job->ctx_params.uid = opt.uid;

	/* Validate minimum and maximum node counts */
	if (opt.min_nodes && opt.max_nodes &&
	    (opt.min_nodes > opt.max_nodes)) {
		error ("Minimum node count > maximum node count (%d > %d)",
		       opt.min_nodes, opt.max_nodes);
		return SLURM_ERROR;
	}
#if !defined HAVE_FRONT_END || (defined HAVE_BGQ)
//#if !defined HAVE_FRONT_END || (defined HAVE_BGQ && defined HAVE_BG_FILES)
	if (opt.min_nodes && (opt.min_nodes > job->nhosts)) {
		error ("Minimum node count > allocated node count (%d > %d)",
		       opt.min_nodes, job->nhosts);
		return SLURM_ERROR;
	}
#endif
	job->ctx_params.min_nodes = job->nhosts;
	if (opt.min_nodes && (opt.min_nodes < job->ctx_params.min_nodes))
		job->ctx_params.min_nodes = opt.min_nodes;
	job->ctx_params.max_nodes = job->nhosts;
	if (opt.max_nodes && (opt.max_nodes < job->ctx_params.max_nodes))
		job->ctx_params.max_nodes = opt.max_nodes;

	if (!opt.ntasks_set && (opt.ntasks_per_node != NO_VAL))
		job->ntasks = opt.ntasks = job->nhosts * opt.ntasks_per_node;
	job->ctx_params.task_count = opt.ntasks;

	if (opt.mem_per_cpu != NO_VAL)
		job->ctx_params.mem_per_cpu = opt.mem_per_cpu;
	if (opt.gres)
		job->ctx_params.gres = opt.gres;
	else
		job->ctx_params.gres = getenv("SLURM_STEP_GRES");

	if (use_all_cpus)
		job->ctx_params.cpu_count = job->cpu_count;
	else if (opt.overcommit)
		job->ctx_params.cpu_count = job->ctx_params.min_nodes;
	else if (opt.cpus_set)
		job->ctx_params.cpu_count = opt.ntasks * opt.cpus_per_task;
	else
		job->ctx_params.cpu_count = opt.ntasks;

	job->ctx_params.relative = (uint16_t)opt.relative;
	job->ctx_params.ckpt_interval = (uint16_t)opt.ckpt_interval;
	job->ctx_params.ckpt_dir = opt.ckpt_dir;
	job->ctx_params.exclusive = (uint16_t)opt.exclusive;
	if (opt.immediate == 1)
		job->ctx_params.immediate = (uint16_t)opt.immediate;
	if (opt.time_limit != NO_VAL)
		job->ctx_params.time_limit = (uint32_t)opt.time_limit;
	job->ctx_params.verbose_level = (uint16_t)_verbose;
	if (opt.resv_port_cnt != NO_VAL)
		job->ctx_params.resv_port_cnt = (uint16_t) opt.resv_port_cnt;

	switch (opt.distribution) {
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_ARBITRARY:
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_CYCLIC_BLOCK:
	case SLURM_DIST_BLOCK_CYCLIC:
	case SLURM_DIST_BLOCK_BLOCK:
		job->ctx_params.task_dist = opt.distribution;
		break;
	case SLURM_DIST_PLANE:
		job->ctx_params.task_dist = SLURM_DIST_PLANE;
		job->ctx_params.plane_size = opt.plane_size;
		break;
	default:
		job->ctx_params.task_dist = (job->ctx_params.task_count <=
					     job->ctx_params.min_nodes)
			? SLURM_DIST_CYCLIC : SLURM_DIST_BLOCK;
		opt.distribution = job->ctx_params.task_dist;
		break;

	}
	job->ctx_params.overcommit = opt.overcommit ? 1 : 0;

	job->ctx_params.node_list = opt.nodelist;

	job->ctx_params.network = opt.network;
	job->ctx_params.no_kill = opt.no_kill;
	if (opt.job_name_set_cmd && opt.job_name)
		job->ctx_params.name = opt.job_name;
	else
		job->ctx_params.name = opt.cmd_name;
	job->ctx_params.features = opt.constraints;

	debug("requesting job %u, user %u, nodes %u including (%s)",
	      job->ctx_params.job_id, job->ctx_params.uid,
	      job->ctx_params.min_nodes, job->ctx_params.node_list);
	debug("cpus %u, tasks %u, name %s, relative %u",
	      job->ctx_params.cpu_count, job->ctx_params.task_count,
	      job->ctx_params.name, job->ctx_params.relative);
	begin_time = time(NULL);

	for (i=0; (!(*destroy_job)); i++) {
		if (opt.no_alloc) {
			job->step_ctx = slurm_step_ctx_create_no_alloc(
				&job->ctx_params, job->stepid);
		} else
			job->step_ctx = slurm_step_ctx_create(
				&job->ctx_params);
		if (job->step_ctx != NULL) {
			if (i > 0)
				info("Job step created");

			break;
		}
		rc = slurm_get_errno();

		if (((opt.immediate != 0) &&
		     ((opt.immediate == 1) ||
		      (difftime(time(NULL), begin_time) > opt.immediate))) ||
		    ((rc != ESLURM_NODES_BUSY) && (rc != ESLURM_PORTS_BUSY) &&
		     (rc != ESLURM_PROLOG_RUNNING) &&
		     (rc != SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT) &&
		     (rc != ESLURM_DISABLED))) {
			error ("Unable to create job step: %m");
			return SLURM_ERROR;
		}

		if (i == 0) {
			if (rc == ESLURM_PROLOG_RUNNING) {
				verbose("Resources allocated for job %u and "
					"being configured, please wait",
					job->ctx_params.job_id);
			} else {
				info("Job step creation temporarily disabled, "
				     "retrying");
			}
			xsignal_unblock(sig_array);
			for (i = 0; sig_array[i]; i++)
				xsignal(sig_array[i], signal_function);

			my_sleep = (getpid() % 1000) * 100 + 100000;
		} else {
			verbose("Job step creation still disabled, retrying");
			my_sleep = MIN((my_sleep * 2), 29000000);
		}
		/* sleep 0.1 to 29 secs with exponential back-off */
		usleep(my_sleep);
		if (*destroy_job) {
			/* cancelled by signal */
			break;
		}
	}
	if (i > 0) {
		xsignal_block(sig_array);
		if (*destroy_job) {
			info("Cancelled pending job step");
			return SLURM_ERROR;
		}
	}

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_STEPID, &job->stepid);
	/*  Number of hosts in job may not have been initialized yet if
	 *    --jobid was used or only SLURM_JOB_ID was set in user env.
	 *    Reset the value here just in case.
	 */
	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_NUM_HOSTS,
			   &job->nhosts);

	/*
	 * Recreate filenames which may depend upon step id
	 */
	job_update_io_fnames(job);

	return SLURM_SUCCESS;
}

extern int launch_g_setup_srun_opt(char **rest)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(plugin_context->ops.setup_srun_opt))(rest);
}

extern int launch_g_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(plugin_context->ops.create_job_step))(job, use_all_cpus,
							signal_function,
							destroy_job);
}

extern int launch_g_step_launch(
	srun_job_t *job, slurm_step_io_fds_t *cio_fds,
	uint32_t *global_rc, bool got_alloc, bool *srun_shutdown)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(plugin_context->ops.step_launch))(job, cio_fds, global_rc,
						    got_alloc, srun_shutdown);
}

extern int launch_g_step_terminate(void)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(plugin_context->ops.step_terminate))();
}

extern void launch_g_print_status(void)
{
	if (launch_init() < 0)
		return;

	(*(plugin_context->ops.print_status))();
}

extern void launch_g_fwd_signal(int signal)
{
	if (launch_init() < 0)
		return;

	(*(plugin_context->ops.fwd_signal))(signal);
}
