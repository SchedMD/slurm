/*  launch.c - Define job launch plugin functions.
*****************************************************************************
*  Copyright (C) 2012 SchedMD LLC
*  Written by Danny Auble <da@schedmd.com>
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
#include <fcntl.h>
#include <signal.h>

#include "src/srun/libsrun/launch.h"

#include "src/common/env.h"
#include "src/common/xstring.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/proc_args.h"
#include "src/common/tres_bind.h"
#include "src/common/tres_frequency.h"
#include "src/common/xsignal.h"

typedef struct {
	int (*setup_srun_opt)      (char **rest, slurm_opt_t *opt_local);
	int (*handle_multi_prog)   (int command_pos, slurm_opt_t *opt_local);
	int (*create_job_step)     (srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job,
				    slurm_opt_t *opt_local);
	int (*step_launch)         (srun_job_t *job,
				    slurm_step_io_fds_t *cio_fds,
				    uint32_t *global_rc,
				    slurm_step_launch_callbacks_t *
				    step_callbacks, slurm_opt_t *opt_local);
	int (*step_wait)           (srun_job_t *job, bool got_alloc,
				    slurm_opt_t *opt_local);
	int (*step_terminate)      (void);
	void (*print_status)       (void);
	void (*fwd_signal)         (int signal);
} plugin_ops_t;

/*
 * Must be synchronized with plugin_ops_t above.
 */
static const char *syms[] = {
	"launch_p_setup_srun_opt",
	"launch_p_handle_multi_prog_verify",
	"launch_p_create_job_step",
	"launch_p_step_launch",
	"launch_p_step_wait",
	"launch_p_step_terminate",
	"launch_p_print_status",
	"launch_p_fwd_signal",
};

static plugin_ops_t ops;
static plugin_context_t *plugin_context = NULL;
static pthread_mutex_t plugin_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

static int
_is_local_file (fname_t *fname)
{
	if (fname->name == NULL)
		return 1;

	if (fname->taskid != -1)
		return 1;

	return ((fname->type != IO_PER_TASK) && (fname->type != IO_ONE));
}

/*
 * Initialize context for plugin
 */
extern int launch_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "launch";
	char *type = NULL;

	if (init_run && plugin_context)
		return retval;

	slurm_mutex_lock(&plugin_context_lock);

	if (plugin_context)
		goto done;

	type = slurm_get_launch_type();
	plugin_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!plugin_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

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

	init_run = false;
	rc = plugin_context_destroy(plugin_context);
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
					 sig_atomic_t *destroy_job,
					 slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	int i, j, rc;
	unsigned long step_wait = 0;
	uint16_t base_dist, slurmctld_timeout;
	char *add_tres;
	xassert(srun_opt);

	if (!job) {
		error("launch_common_create_job_step: no job given");
		return SLURM_ERROR;
	}

	slurm_step_ctx_params_t_init(&job->ctx_params);
	job->ctx_params.job_id = job->jobid;
	job->ctx_params.step_id = job->stepid;
	job->ctx_params.uid = opt_local->uid;

	/* Validate minimum and maximum node counts */
	if (opt_local->min_nodes && opt_local->max_nodes &&
	    (opt_local->min_nodes > opt_local->max_nodes)) {
		error ("Minimum node count > maximum node count (%d > %d)",
		       opt_local->min_nodes, opt_local->max_nodes);
		return SLURM_ERROR;
	}
#if !defined HAVE_FRONT_END
	if (opt_local->min_nodes && (opt_local->min_nodes > job->nhosts)) {
		error ("Minimum node count > allocated node count (%d > %d)",
		       opt_local->min_nodes, job->nhosts);
		return SLURM_ERROR;
	}
#endif
	job->ctx_params.min_nodes = job->nhosts;
	if (opt_local->min_nodes &&
	    (opt_local->min_nodes < job->ctx_params.min_nodes))
		job->ctx_params.min_nodes = opt_local->min_nodes;
	job->ctx_params.max_nodes = job->nhosts;
	if (opt_local->max_nodes &&
	    (opt_local->max_nodes < job->ctx_params.max_nodes))
		job->ctx_params.max_nodes = opt_local->max_nodes;

	if (!opt_local->ntasks_set && (opt_local->ntasks_per_node != NO_VAL))
		job->ntasks = opt_local->ntasks = job->nhosts *
						  opt_local->ntasks_per_node;
	job->ctx_params.task_count = opt_local->ntasks;

	if (opt_local->mem_per_cpu > -1)
		job->ctx_params.pn_min_memory = opt_local->mem_per_cpu |
						MEM_PER_CPU;
	else if (opt_local->pn_min_memory != NO_VAL64)
		job->ctx_params.pn_min_memory = opt_local->pn_min_memory;

	if (opt_local->overcommit) {
		if (use_all_cpus)	/* job allocation created by srun */
			job->ctx_params.cpu_count = job->cpu_count;
		else
			job->ctx_params.cpu_count = job->ctx_params.min_nodes;
	} else if (opt_local->cpus_set) {
		job->ctx_params.cpu_count = opt_local->ntasks *
					    opt_local->cpus_per_task;
	} else if (opt_local->ntasks_set) {
		job->ctx_params.cpu_count = opt_local->ntasks;
	} else if (use_all_cpus) {	/* job allocation created by srun */
		job->ctx_params.cpu_count = job->cpu_count;
	} else {
		job->ctx_params.cpu_count = opt_local->ntasks;
	}

	job->ctx_params.cpu_freq_min = opt_local->cpu_freq_min;
	job->ctx_params.cpu_freq_max = opt_local->cpu_freq_max;
	job->ctx_params.cpu_freq_gov = opt_local->cpu_freq_gov;
	job->ctx_params.relative = (uint16_t)srun_opt->relative;
	job->ctx_params.ckpt_interval = (uint16_t)srun_opt->ckpt_interval;
	job->ctx_params.exclusive = (uint16_t)srun_opt->exclusive;
	if (opt_local->immediate == 1)
		job->ctx_params.immediate = (uint16_t)opt_local->immediate;
	if (opt_local->time_limit != NO_VAL)
		job->ctx_params.time_limit = (uint32_t)opt_local->time_limit;
	job->ctx_params.verbose_level = (uint16_t) opt.verbose;
	if (srun_opt->resv_port_cnt != NO_VAL) {
		job->ctx_params.resv_port_cnt = (uint16_t)srun_opt->resv_port_cnt;
	} else {
#if defined(HAVE_NATIVE_CRAY)
		/*
		 * On Cray systems default to reserving one port, or one
		 * more than the number of multi prog commands, for Cray PMI
		 */
		job->ctx_params.resv_port_cnt = (srun_opt->multi_prog ?
					srun_opt->multi_prog_cmds + 1 : 1);
#endif
	}

	switch (opt_local->distribution & SLURM_DIST_NODESOCKMASK) {
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_ARBITRARY:
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_CYCLIC_BLOCK:
	case SLURM_DIST_BLOCK_CYCLIC:
	case SLURM_DIST_BLOCK_BLOCK:
	case SLURM_DIST_CYCLIC_CFULL:
	case SLURM_DIST_BLOCK_CFULL:
		job->ctx_params.task_dist = opt_local->distribution;
		if (opt_local->ntasks_per_node != NO_VAL)
			job->ctx_params.plane_size = opt_local->ntasks_per_node;
		break;
	case SLURM_DIST_PLANE:
		job->ctx_params.task_dist = SLURM_DIST_PLANE;
		job->ctx_params.plane_size = opt_local->plane_size;
		break;
	default:
		/* Leave distribution set to unknown if taskcount <= nodes and
		 * memory is set to 0. step_mgr will handle the 0mem case.
		 * ex. SallocDefaultCommand=srun -n1 -N1 --mem=0 ... */
		if (!opt_local->mem_per_cpu || !opt_local->pn_min_memory)
			base_dist = SLURM_DIST_UNKNOWN;
		else
			base_dist = (job->ctx_params.task_count <=
				     job->ctx_params.min_nodes)
				     ? SLURM_DIST_CYCLIC : SLURM_DIST_BLOCK;
		opt_local->distribution &= SLURM_DIST_STATE_FLAGS;
		opt_local->distribution |= base_dist;
		job->ctx_params.task_dist = opt_local->distribution;
		if (opt_local->ntasks_per_node != NO_VAL)
			job->ctx_params.plane_size = opt_local->ntasks_per_node;
		break;

	}
	job->ctx_params.overcommit = opt_local->overcommit ? 1 : 0;
	job->ctx_params.node_list = opt_local->nodelist;
	job->ctx_params.network = opt_local->network;
	job->ctx_params.no_kill = opt_local->no_kill;
	if (slurm_option_set_by_cli('J'))
		job->ctx_params.name = opt_local->job_name;
	else
		job->ctx_params.name = srun_opt->cmd_name;
	job->ctx_params.features = opt_local->constraint;

	if (opt_local->cpus_per_gpu) {
		xstrfmtcat(job->ctx_params.cpus_per_tres, "gpu:%d",
			   opt_local->cpus_per_gpu);
	}
	xfree(opt_local->tres_bind);	/* Vestigial value from job allocate */
	if (opt_local->gpu_bind)
		xstrfmtcat(opt_local->tres_bind, "gpu:%s", opt_local->gpu_bind);
	if (tres_bind_verify_cmdline(opt_local->tres_bind)) {
		if (tres_bind_err_log) {	/* Log once */
			error("Invalid --tres-bind argument: %s. Ignored",
			      opt_local->tres_bind);
			tres_bind_err_log = false;
		}
		xfree(opt_local->tres_bind);
	}
	job->ctx_params.tres_bind = xstrdup(opt_local->tres_bind);
	xfree(opt_local->tres_freq);	/* Vestigial value from job allocate */
	xfmt_tres_freq(&opt_local->tres_freq, "gpu", opt_local->gpu_freq);
	if (tres_freq_verify_cmdline(opt_local->tres_freq)) {
		if (tres_freq_err_log) {	/* Log once */
			error("Invalid --tres-freq argument: %s. Ignored",
			      opt_local->tres_freq);
			tres_freq_err_log = false;
		}
		xfree(opt_local->tres_freq);
	}
	job->ctx_params.tres_freq = xstrdup(opt_local->tres_freq);
	xfmt_tres(&job->ctx_params.tres_per_step, "gpu", opt_local->gpus);
	xfmt_tres(&job->ctx_params.tres_per_node, "gpu",
		  opt_local->gpus_per_node);
	if (opt_local->gres)
		add_tres = opt_local->gres;
	else
		add_tres = getenv("SLURM_STEP_GRES");
	if (add_tres) {
		if (job->ctx_params.tres_per_node) {
			xstrfmtcat(job->ctx_params.tres_per_node, ",%s",
				   add_tres);
		} else
			job->ctx_params.tres_per_node = xstrdup(add_tres);
	}
	xfmt_tres(&job->ctx_params.tres_per_socket, "gpu",
		  opt_local->gpus_per_socket);
	xfmt_tres(&job->ctx_params.tres_per_task, "gpu",
		  opt_local->gpus_per_task);
	if (opt_local->mem_per_gpu != NO_VAL64) {
		xstrfmtcat(job->ctx_params.mem_per_tres, "gpu:%"PRIu64,
			   opt.mem_per_gpu);
	}

	debug("requesting job %u, user %u, nodes %u including (%s)",
	      job->ctx_params.job_id, job->ctx_params.uid,
	      job->ctx_params.min_nodes, job->ctx_params.node_list);
	debug("cpus %u, tasks %u, name %s, relative %u",
	      job->ctx_params.cpu_count, job->ctx_params.task_count,
	      job->ctx_params.name, job->ctx_params.relative);

	for (i = 0; (!(*destroy_job)); i++) {
		if (srun_opt->no_alloc) {
			job->step_ctx = slurm_step_ctx_create_no_alloc(
				&job->ctx_params, job->stepid);
		} else {
			if (opt_local->immediate) {
				step_wait = MAX(1, opt_local->immediate -
						   difftime(time(NULL),
							    srun_begin_time)) *
					    1000;
			} else {
				slurmctld_timeout = MIN(300, MAX(60,
					slurm_get_slurmctld_timeout()));
				step_wait = ((getpid() % 10) +
					     slurmctld_timeout) * 1000;
			}
			job->step_ctx = slurm_step_ctx_create_timeout(
						&job->ctx_params, step_wait);
		}
		if (job->step_ctx != NULL) {
			if (i > 0) {
				info("Step created for job %u",
				     job->ctx_params.job_id);
			}
			break;
		}
		rc = slurm_get_errno();

		if (((opt_local->immediate != 0) &&
		     ((opt_local->immediate == 1) ||
		      (difftime(time(NULL), srun_begin_time) >=
		       opt_local->immediate))) ||
		    ((rc != ESLURM_PROLOG_RUNNING) &&
		     !slurm_step_retry_errno(rc))) {
			error("Unable to create step for job %u: %m",
			      job->ctx_params.job_id);
			return SLURM_ERROR;
		}

		if (i == 0) {
			if (rc == ESLURM_PROLOG_RUNNING) {
				verbose("Resources allocated for job %u and "
					"being configured, please wait",
					job->ctx_params.job_id);
			} else {
				info("Job %u step creation temporarily disabled, retrying",
				     job->ctx_params.job_id);
			}
			xsignal_unblock(sig_array);
			for (j = 0; sig_array[j]; j++)
				xsignal(sig_array[j], signal_function);
		} else {
			verbose("Job %u step creation still disabled, retrying",
				job->ctx_params.job_id);
		}

		if (*destroy_job) {
			/* cancelled by signal */
			break;
		}
	}
	if (i > 0) {
		xsignal_block(sig_array);
		if (*destroy_job) {
			info("Cancelled pending step for job %u",
			     job->ctx_params.job_id);
			return SLURM_ERROR;
		}
	}

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_STEPID, &job->stepid);
	/*
	 *  Number of hosts in job may not have been initialized yet if
	 *    --jobid was used or only SLURM_JOB_ID was set in user env.
	 *    Reset the value here just in case.
	 */
	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_NUM_HOSTS,
			   &job->nhosts);

	/*
	 * Recreate filenames which may depend upon step id
	 */
	job_update_io_fnames(job, opt_local);

	return SLURM_SUCCESS;
}

extern void launch_common_set_stdio_fds(srun_job_t *job,
					slurm_step_io_fds_t *cio_fds,
					slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	bool err_shares_out = false;
	int file_flags;
	xassert(srun_opt);

	if (srun_opt->open_mode == OPEN_MODE_APPEND)
		file_flags = O_CREAT|O_WRONLY|O_APPEND;
	else if (srun_opt->open_mode == OPEN_MODE_TRUNCATE)
		file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
	else {
		slurm_ctl_conf_t *conf;
		conf = slurm_conf_lock();
		if (conf->job_file_append)
			file_flags = O_CREAT|O_WRONLY|O_APPEND;
		else
			file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
		slurm_conf_unlock();
	}

	/*
	 * create stdin file descriptor
	 */
	if (_is_local_file(job->ifname)) {
		if ((job->ifname->name == NULL) ||
		    (job->ifname->taskid != -1)) {
			cio_fds->input.fd = STDIN_FILENO;
		} else {
			cio_fds->input.fd = open(job->ifname->name, O_RDONLY);
			if (cio_fds->input.fd == -1) {
				error("Could not open stdin file: %m");
				exit(error_exit);
			}
		}
		if (job->ifname->type == IO_ONE) {
			cio_fds->input.taskid = job->ifname->taskid;
			cio_fds->input.nodeid = slurm_step_layout_host_id(
				launch_common_get_slurm_step_layout(job),
				job->ifname->taskid);
		}
	}

	/*
	 * create stdout file descriptor
	 */
	if (_is_local_file(job->ofname)) {
		if ((job->ofname->name == NULL) ||
		    (job->ofname->taskid != -1)) {
			cio_fds->out.fd = STDOUT_FILENO;
		} else {
			cio_fds->out.fd = open(job->ofname->name,
					       file_flags, 0644);
			if (cio_fds->out.fd == -1) {
				error("Could not open stdout file: %m");
				exit(error_exit);
			}
		}
		if (job->ofname->name != NULL
		    && job->efname->name != NULL
		    && !xstrcmp(job->ofname->name, job->efname->name)) {
			err_shares_out = true;
		}
	}

	/*
	 * create seperate stderr file descriptor only if stderr is not sharing
	 * the stdout file descriptor
	 */
	if (err_shares_out) {
		debug3("stdout and stderr sharing a file");
		cio_fds->err.fd = cio_fds->out.fd;
		cio_fds->err.taskid = cio_fds->out.taskid;
	} else if (_is_local_file(job->efname)) {
		if ((job->efname->name == NULL) ||
		    (job->efname->taskid != -1)) {
			cio_fds->err.fd = STDERR_FILENO;
		} else {
			cio_fds->err.fd = open(job->efname->name,
					       file_flags, 0644);
			if (cio_fds->err.fd == -1) {
				error("Could not open stderr file: %m");
				exit(error_exit);
			}
		}
	}
}

extern int launch_g_setup_srun_opt(char **rest, slurm_opt_t *opt_local)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(ops.setup_srun_opt))(rest, opt_local);
}

extern int launch_g_handle_multi_prog_verify(int command_pos,
					     slurm_opt_t *opt_local)
{
	if (launch_init() < 0)
		return 0;

	return (*(ops.handle_multi_prog))(command_pos, opt_local);
}

extern int launch_g_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job,
				    slurm_opt_t *opt_local)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(ops.create_job_step))(job, use_all_cpus, signal_function,
					destroy_job, opt_local);
}

extern int launch_g_step_launch(srun_job_t *job, slurm_step_io_fds_t *cio_fds,
				uint32_t *global_rc,
				slurm_step_launch_callbacks_t *step_callbacks,
				slurm_opt_t *opt_local)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(ops.step_launch))(job, cio_fds, global_rc, step_callbacks,
				    opt_local);
}

extern int launch_g_step_wait(srun_job_t *job, bool got_alloc,
			      slurm_opt_t *opt_local)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(ops.step_wait))(job, got_alloc, opt_local);
}

extern int launch_g_step_terminate(void)
{
	if (launch_init() < 0)
		return SLURM_ERROR;

	return (*(ops.step_terminate))();
}

extern void launch_g_print_status(void)
{
	if (launch_init() < 0)
		return;

	(*(ops.print_status))();
}

extern void launch_g_fwd_signal(int signal)
{
	if (launch_init() < 0)
		return;

	(*(ops.fwd_signal))(signal);
}
