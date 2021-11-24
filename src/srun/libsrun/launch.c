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
#include "src/common/net.h"
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

	if (init_run && plugin_context)
		return retval;

	slurm_mutex_lock(&plugin_context_lock);

	if (plugin_context)
		goto done;

	plugin_context = plugin_context_create(plugin_type,
					       slurm_conf.launch_type,
					       (void **) &ops, syms,
					       sizeof(syms));

	if (!plugin_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.launch_type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&plugin_context_lock);

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
	return (!job || !job->step_ctx) ?
		NULL : job->step_ctx->step_resp->step_layout;
}

static job_step_create_request_msg_t *_create_job_step_create_request(
	slurm_opt_t *opt_local, bool use_all_cpus, srun_job_t *job)
{
	char *add_tres = NULL;
	srun_opt_t *srun_opt = opt_local->srun_opt;
	job_step_create_request_msg_t *step_req = xmalloc(sizeof(*step_req));

	xassert(srun_opt);

	step_req->host = xshort_hostname();
	step_req->cpu_freq_min = opt_local->cpu_freq_min;
	step_req->cpu_freq_max = opt_local->cpu_freq_max;
	step_req->cpu_freq_gov = opt_local->cpu_freq_gov;

	if (opt_local->cpus_per_gpu) {
		xstrfmtcat(step_req->cpus_per_tres, "gres:gpu:%d",
			   opt_local->cpus_per_gpu);
	}

	step_req->exc_nodes = xstrdup(opt_local->exclude);
	step_req->features = xstrdup(opt_local->constraint);

	if (srun_opt->exclusive)
		step_req->flags |= SSF_EXCLUSIVE;
	if (opt_local->overcommit)
		step_req->flags |= SSF_OVERCOMMIT;
	if (!srun_opt->exact)
		step_req->flags |= SSF_WHOLE;
	if (opt_local->no_kill)
		step_req->flags |= SSF_NO_KILL;
	if (srun_opt->interactive) {
		debug("interactive step launch request");
		step_req->flags |= SSF_INTERACTIVE;
	}

	if (opt_local->immediate == 1)
		step_req->immediate = opt_local->immediate;

	step_req->max_nodes = job->nhosts;
	if (opt_local->max_nodes &&
	    (opt_local->max_nodes < step_req->max_nodes))
		step_req->max_nodes = opt_local->max_nodes;

	if (opt_local->mem_per_gpu != NO_VAL64)
		xstrfmtcat(step_req->mem_per_tres, "gres:gpu:%"PRIu64,
			   opt.mem_per_gpu);

	step_req->min_nodes = job->nhosts;
	if (opt_local->min_nodes &&
	    (opt_local->min_nodes < step_req->min_nodes))
		step_req->min_nodes = opt_local->min_nodes;

	/*
	 * If the number of CPUs was specified (cpus_set==true), then we need to
	 * set exact = true. Otherwise the step will be allocated the wrong
	 * number of CPUs (and therefore the wrong amount memory if using
	 * mem_per_cpu).
	 */
	if (opt_local->overcommit) {
		if (use_all_cpus)	/* job allocation created by srun */
			step_req->cpu_count = job->cpu_count;
		else
			step_req->cpu_count = step_req->min_nodes;
	} else if (opt_local->cpus_set) {
		step_req->cpu_count =
			opt_local->ntasks * opt_local->cpus_per_task;
		if (!srun_opt->exact)
			verbose("Implicitly setting --exact, because -c/--cpus-per-task given.");
		srun_opt->exact = true;
	} else if (opt_local->gpus_per_task && opt_local->cpus_per_gpu) {
		char *save_ptr = NULL, *tmp_str, *tok, *sep;
		int gpus_per_task = 0;

		tmp_str = xstrdup(opt_local->gpus_per_task);

		tok = strtok_r(tmp_str, ",", &save_ptr);
		while (tok) {
			int tmp;
			sep = xstrchr(tok, ':');
			if (sep)
				tmp =+ atoi(sep + 1);
			else
				tmp =+ atoi(tok);
			if (tmp > 0)
				gpus_per_task =+ tmp;
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp_str);
		step_req->cpu_count = opt_local->ntasks * gpus_per_task *
				      opt_local->cpus_per_gpu;
	} else if (opt_local->ntasks_set) {
		step_req->cpu_count = opt_local->ntasks;
	} else if (use_all_cpus) {	/* job allocation created by srun */
		step_req->cpu_count = job->cpu_count;
	} else {
		step_req->cpu_count = opt_local->ntasks;
	}

	if (slurm_option_set_by_cli(opt_local, 'J'))
		step_req->name = opt_local->job_name;
	else if (srun_opt->cmd_name)
		step_req->name = srun_opt->cmd_name;
	else
		step_req->name = sropt.cmd_name;

	step_req->network = xstrdup(opt_local->network);
	step_req->node_list = xstrdup(opt_local->nodelist);

	if (opt_local->ntasks_per_tres != NO_VAL)
		step_req->ntasks_per_tres = opt_local->ntasks_per_tres;
	else if (opt_local->ntasks_per_gpu != NO_VAL)
		step_req->ntasks_per_tres = opt_local->ntasks_per_gpu;
	else
		step_req->ntasks_per_tres = NO_VAL16;

	step_req->num_tasks = opt_local->ntasks;

	step_req->plane_size = NO_VAL16;
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
		step_req->task_dist = opt_local->distribution;
		if (opt_local->ntasks_per_node != NO_VAL)
			step_req->plane_size = opt_local->ntasks_per_node;
		break;
	case SLURM_DIST_PLANE:
		step_req->task_dist = SLURM_DIST_PLANE;
		step_req->plane_size = opt_local->plane_size;
		break;
	default:
	{
		uint16_t base_dist;
		/* Leave distribution set to unknown if taskcount <= nodes and
		 * memory is set to 0. step_mgr will handle the mem=0 case. */
		if (!opt_local->mem_per_cpu || !opt_local->pn_min_memory ||
		    srun_opt->interactive)
			base_dist = SLURM_DIST_UNKNOWN;
		else
			base_dist = (step_req->num_tasks <=
				     step_req->min_nodes) ?
				SLURM_DIST_CYCLIC : SLURM_DIST_BLOCK;
		opt_local->distribution &= SLURM_DIST_STATE_FLAGS;
		opt_local->distribution |= base_dist;
		step_req->task_dist = opt_local->distribution;
		if (opt_local->ntasks_per_node != NO_VAL)
			step_req->plane_size = opt_local->ntasks_per_node;
		break;
	}
	}

	if (opt_local->mem_per_cpu != NO_VAL64)
		step_req->pn_min_memory = opt_local->mem_per_cpu | MEM_PER_CPU;
	else if (opt_local->pn_min_memory != NO_VAL64)
		step_req->pn_min_memory = opt_local->pn_min_memory;

	step_req->relative = srun_opt->relative;

	if (srun_opt->resv_port_cnt != NO_VAL) {
		step_req->resv_port_cnt = srun_opt->resv_port_cnt;
	} else {
#if defined(HAVE_NATIVE_CRAY)
		/*
		 * On Cray systems default to reserving one port, or one
		 * more than the number of multi prog commands, for Cray PMI
		 */
		step_req->resv_port_cnt = (srun_opt->multi_prog ?
					   srun_opt->multi_prog_cmds + 1 : 1);
#else
		step_req->resv_port_cnt = NO_VAL16;
#endif
	}

	step_req->srun_pid = (uint32_t) getpid();
	step_req->step_het_comp_cnt = opt_local->step_het_comp_cnt;
	step_req->step_het_grps = xstrdup(opt_local->step_het_grps);
	memcpy(&step_req->step_id, &job->step_id, sizeof(step_req->step_id));

	step_req->submit_line = xstrdup(opt_local->submit_line);

	if (opt_local->threads_per_core != NO_VAL) {
		step_req->threads_per_core = opt.threads_per_core;
	} else
		step_req->threads_per_core = NO_VAL16;

	if (!opt_local->tres_bind &&
	    ((opt_local->ntasks_per_tres != NO_VAL) ||
	     (opt_local->ntasks_per_gpu != NO_VAL))) {
		/* Implicit single GPU binding with ntasks-per-tres/gpu */
		if (opt_local->ntasks_per_tres != NO_VAL)
			xstrfmtcat(opt_local->tres_bind, "gpu:single:%d",
				   opt_local->ntasks_per_tres);
		else
			xstrfmtcat(opt_local->tres_bind, "gpu:single:%d",
				   opt_local->ntasks_per_gpu);
	}

	if (!opt_local->tres_bind && opt_local->gpus_per_task) {
		/* Implicit GPU binding with gpus_per_task */
		xstrfmtcat(opt_local->tres_bind, "gpu:per_task:%s",
			   opt_local->gpus_per_task);
	}
	step_req->tres_bind = xstrdup(opt_local->tres_bind);
	step_req->tres_freq = xstrdup(opt_local->tres_freq);

	xstrfmtcat(step_req->tres_per_step, "%scpu:%u",
		   step_req->tres_per_step ? "," : "",
		   step_req->cpu_count);
	xfmt_tres(&step_req->tres_per_step, "gres:gpu", opt_local->gpus);

	xfmt_tres(&step_req->tres_per_node, "gres:gpu",
		  opt_local->gpus_per_node);
	if (opt_local->gres)
		add_tres = opt_local->gres;
	else
		add_tres = getenv("SLURM_STEP_GRES");
	if (add_tres) {
		if (step_req->tres_per_node) {
			xstrfmtcat(step_req->tres_per_node, ",%s", add_tres);
		} else
			step_req->tres_per_node = xstrdup(add_tres);
	}

	xfmt_tres(&step_req->tres_per_socket, "gres:gpu",
		  opt_local->gpus_per_socket);

	if (opt_local->cpus_set)
		xstrfmtcat(step_req->tres_per_task, "%scpu:%u",
			   step_req->tres_per_task ? "," : "",
			   opt_local->cpus_per_task);
	xfmt_tres(&step_req->tres_per_task, "gres:gpu",
		  opt_local->gpus_per_task);

	if (opt_local->time_limit != NO_VAL)
		step_req->time_limit = opt_local->time_limit;

	step_req->user_id = opt_local->uid;

	step_req->container = xstrdup(opt_local->container);

	return step_req;
}

extern int launch_common_create_job_step(srun_job_t *job, bool use_all_cpus,
					 void (*signal_function)(int),
					 sig_atomic_t *destroy_job,
					 slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	int i, j, rc;
	unsigned long step_wait = 0;
	uint16_t slurmctld_timeout;
	slurm_step_layout_t *step_layout;
	job_step_create_request_msg_t *step_req;

	xassert(srun_opt);

	if (!job) {
		error("launch_common_create_job_step: no job given");
		return SLURM_ERROR;
	}

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

	step_req = _create_job_step_create_request(
		opt_local, use_all_cpus, job);

	if (!step_req)
		return SLURM_ERROR;

	debug("requesting job %u, user %u, nodes %u including (%s)",
	      step_req->step_id.job_id, step_req->user_id,
	      step_req->min_nodes, step_req->node_list);
	debug("cpus %u, tasks %u, name %s, relative %u",
	      step_req->cpu_count, step_req->num_tasks,
	      step_req->name, step_req->relative);

	for (i = 0; (!(*destroy_job)); i++) {
		if (srun_opt->no_alloc) {
			job->step_ctx = step_ctx_create_no_alloc(
				step_req, job->step_id.step_id);
		} else {
			if (opt_local->immediate) {
				step_wait = MAX(1, opt_local->immediate -
						   difftime(time(NULL),
							    srun_begin_time)) *
					    1000;
			} else {
				slurmctld_timeout = MIN(300, MAX(60,
					slurm_conf.slurmctld_timeout));
				step_wait = ((getpid() % 10) +
					     slurmctld_timeout) * 1000;
			}
			job->step_ctx = step_ctx_create_timeout(
						step_req, step_wait);
		}
		if (job->step_ctx != NULL) {
			job->step_ctx->verbose_level = opt_local->verbose;
			if (i > 0) {
				info("Step created for job %u",
				     step_req->step_id.job_id);
			}
			break;
		}
		rc = slurm_get_errno();

		if (((opt_local->immediate != 0) &&
		     ((opt_local->immediate == 1) ||
		      (difftime(time(NULL), srun_begin_time) >=
		       opt_local->immediate))) ||
		    ((rc != ESLURM_PROLOG_RUNNING) &&
		     !launch_common_step_retry_errno(rc))) {
			error("Unable to create step for job %u: %m",
			      step_req->step_id.job_id);
			slurm_free_job_step_create_request_msg(step_req);
			return SLURM_ERROR;
		}

		if (i == 0) {
			if (rc == ESLURM_PROLOG_RUNNING) {
				verbose("Resources allocated for job %u and "
					"being configured, please wait",
					step_req->step_id.job_id);
			} else {
				info("Job %u step creation temporarily disabled, retrying (%s)",
				     step_req->step_id.job_id,
				     slurm_strerror(rc));
			}
			xsignal_unblock(sig_array);
			for (j = 0; sig_array[j]; j++)
				xsignal(sig_array[j], signal_function);
		} else {
			if (rc == ESLURM_PROLOG_RUNNING)
				verbose("Job %u step creation still disabled, retrying (%s)",
					step_req->step_id.job_id,
					slurm_strerror(rc));
			else
				info("Job %u step creation still disabled, retrying (%s)",
				     step_req->step_id.job_id,
				     slurm_strerror(rc));
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
			     step_req->step_id.job_id);
			slurm_free_job_step_create_request_msg(step_req);
			return SLURM_ERROR;
		}
	}

	job->step_id.step_id = job->step_ctx->step_req->step_id.step_id;
	/*
	 *  Number of hosts in job may not have been initialized yet if
	 *    --jobid was used or only SLURM_JOB_ID was set in user env.
	 *    Reset the value here just in case.
	 */
	job->nhosts = job->step_ctx->step_resp->step_layout->node_cnt;

	step_layout = launch_common_get_slurm_step_layout(job);
	if (job->ntasks != step_layout->task_cnt)
		job->ntasks = step_layout->task_cnt;
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
	bool err_shares_out = false;
	int file_flags;

	if (opt_local->open_mode == OPEN_MODE_APPEND)
		file_flags = O_CREAT|O_WRONLY|O_APPEND;
	else if (opt_local->open_mode == OPEN_MODE_TRUNCATE)
		file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
	else {
		slurm_conf_t *conf = slurm_conf_lock();
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

/*
 * Return TRUE if the job step create request should be retried later
 * (i.e. the errno set by step_ctx_create_timeout() is recoverable).
 */
extern bool launch_common_step_retry_errno(int rc)
{
	if ((rc == EAGAIN) ||
	    (rc == ESLURM_DISABLED) ||
	    (rc == ESLURM_INTERCONNECT_BUSY) ||
	    (rc == ESLURM_NODES_BUSY) ||
	    (rc == ESLURM_PORTS_BUSY) ||
	    (rc == SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT))
		return true;
	return false;
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
