/*****************************************************************************\
 *  src/slurmd/slurmstepd/mgr.c - job manager functions for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2016 SchedMD LLC.
 *  Copyright (C) 2013      Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#define _GNU_SOURCE

#include "config.h"

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <grp.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_PTY_H
#  include <pty.h>
#  ifdef HAVE_UTMP_H
#    include <utmp.h>
#  endif
#endif

#include "slurm/slurm_errno.h"

#include "src/common/cbuf.h"
#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/plugstack.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_mpi.h"
#include "src/common/strlcpy.h"
#include "src/common/switch.h"
#include "src/common/tres_frequency.h"
#include "src/common/util-net.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/slurmd/slurmd/slurmd.h"

#include "src/slurmd/common/core_spec_plugin.h"
#include "src/slurmd/common/fname.h"
#include "src/slurmd/common/job_container_plugin.h"
#include "src/slurmd/common/log_ctld.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/run_script.h"
#include "src/slurmd/common/reverse_tree.h"
#include "src/slurmd/common/set_oomadj.h"
#include "src/slurmd/common/slurmd_cgroup.h"
#include "src/slurmd/common/task_plugin.h"
#include "src/slurmd/common/xcpuinfo.h"

#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/mgr.h"
#include "src/slurmd/slurmstepd/task.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/pdebug.h"
#include "src/slurmd/slurmstepd/req.h"
#include "src/slurmd/slurmstepd/pam_ses.h"
#include "src/slurmd/slurmstepd/ulimits.h"
#include "src/slurmd/slurmstepd/step_terminate_monitor.h"
#include "src/slurmd/slurmstepd/x11_forwarding.h"

#define RETRY_DELAY 15		/* retry every 15 seconds */
#define MAX_RETRY   240		/* retry 240 times (one hour max) */

struct priv_state {
	uid_t	saved_uid;
	gid_t	saved_gid;
	gid_t *	gid_list;
	int	ngids;
	char	saved_cwd [4096];
};

step_complete_t step_complete = {
	PTHREAD_COND_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	-1,
	-1,
	-1,
	{},
	-1,
	-1,
	true,
	(bitstr_t *)NULL,
	0,
	NULL
};

typedef struct kill_thread {
	pthread_t thread_id;
	int       secs;
} kill_thread_t;

/*
 * Prototypes
 */

/*
 * Job manager related prototypes
 */
static bool _access(const char *path, int modes, uid_t uid,
		    int ngids, gid_t *gids);
static void _send_launch_failure(launch_tasks_request_msg_t *,
				 slurm_addr_t *, int, uint16_t);
static int  _fork_all_tasks(stepd_step_rec_t *job, bool *io_initialized);
static int  _become_user(stepd_step_rec_t *job, struct priv_state *ps);
static void  _set_prio_process (stepd_step_rec_t *job);
static int  _setup_normal_io(stepd_step_rec_t *job);
static int  _drop_privileges(stepd_step_rec_t *job, bool do_setuid,
			     struct priv_state *state, bool get_list);
static int  _reclaim_privileges(struct priv_state *state);
static void _send_launch_resp(stepd_step_rec_t *job, int rc);
static int  _slurmd_job_log_init(stepd_step_rec_t *job);
static void _wait_for_io(stepd_step_rec_t *job);
static int  _send_exit_msg(stepd_step_rec_t *job, uint32_t *tid, int n,
			   int status);
static void _set_job_state(stepd_step_rec_t *job, slurmstepd_state_t new_state);
static void _wait_for_all_tasks(stepd_step_rec_t *job);
static int  _wait_for_any_task(stepd_step_rec_t *job, bool waitflag);

static void _random_sleep(stepd_step_rec_t *job);
static int  _run_script_as_user(const char *name, const char *path,
				stepd_step_rec_t *job,
				int max_wait, char **env);
static void _unblock_signals(void);

/*
 * Batch job management prototypes:
 */
static char * _make_batch_dir(stepd_step_rec_t *job);
static char * _make_batch_script(batch_job_launch_msg_t *msg, char *path);
static int    _send_complete_batch_script_msg(stepd_step_rec_t *job,
					      int err, int status);

static stepd_step_rec_t *reattach_job;

/*
 * Launch an job step on the current node
 */
extern stepd_step_rec_t *
mgr_launch_tasks_setup(launch_tasks_request_msg_t *msg, slurm_addr_t *cli,
		       slurm_addr_t *self, uint16_t protocol_version)
{
	stepd_step_rec_t *job = NULL;

	if (!(job = stepd_step_rec_create(msg, protocol_version))) {
		/*
		 * We want to send back to the slurmd the reason we
		 * failed so keep track of it since errno could be
		 * reset in _send_launch_failure.
		 */
		int fail = errno;
		_send_launch_failure(msg, cli, errno, protocol_version);
		errno = fail;
		return NULL;
	}

	job->envtp->cli = cli;
	job->envtp->self = self;
	job->envtp->select_jobinfo = msg->select_jobinfo;
	job->accel_bind_type = msg->accel_bind_type;
	job->tres_bind = xstrdup(msg->tres_bind);
	job->tres_freq = xstrdup(msg->tres_freq);

	return job;
}

inline static int
_send_srun_resp_msg(slurm_msg_t *resp_msg, uint32_t nnodes)
{
	int rc, retry = 0, max_retry = 0;
	unsigned long delay = 100000;

	/* NOTE: Wait until suspended job step is resumed or the RPC
	 * authentication credential from Munge may expire by the time
	 * it is resumed */
	wait_for_resumed(resp_msg->msg_type);
	while (1) {
		if (resp_msg->protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
			int msg_rc = 0;
			msg_rc = slurm_send_recv_rc_msg_only_one(resp_msg,
								 &rc, 0);
			/* Both must be zero for a successful transmission. */
			if (!msg_rc && !rc)
				break;
		} else {
			/*
			 * Old unreliable method. See slurm_send_only_node_msg()
			 * for further details.
			 */
			rc = slurm_send_only_node_msg(resp_msg);
			if (rc == SLURM_SUCCESS)
				break;
		}

		if (!max_retry)
			max_retry = (nnodes / 1024) + 5;

		debug("%s: %d/%d failed to send msg type %u: %m",
		      __func__, retry, max_retry, resp_msg->msg_type);

		if (retry >= max_retry)
			break;

		usleep(delay);
		if (delay < 800000)
			delay *= 2;
		retry++;
	}
	return rc;
}

static void _local_jobacctinfo_aggregate(
	jobacctinfo_t *dest, jobacctinfo_t *from)
{
	/*
	 * Here to make any sense for some variables we need to move the
	 * Max to the total (i.e. Mem VMem) since the total might be
	 * incorrect data, this way the total/ave will be of the Max
	 * values.
	 */
	from->tres_usage_in_tot[TRES_ARRAY_MEM] =
		from->tres_usage_in_max[TRES_ARRAY_MEM];
	from->tres_usage_in_tot[TRES_ARRAY_VMEM] =
		from->tres_usage_in_max[TRES_ARRAY_VMEM];

	/*
	 * Here ave_watts stores the ave of the watts collected so store that
	 * as the last value so the total will be a total of ave instead of just
	 * the last watts collected.
	 */
	from->tres_usage_out_tot[TRES_ARRAY_ENERGY] = from->energy.ave_watts;

	jobacctinfo_aggregate(dest, from);
}

/*
 * Find the maximum task return code
 */
static uint32_t _get_exit_code(stepd_step_rec_t *job)
{
	uint32_t i;
	uint32_t step_rc = NO_VAL;

	/* We are always killing/cancelling the extern_step so don't
	 * report that.
	 */
	if (job->stepid == SLURM_EXTERN_CONT)
		return 0;

	for (i = 0; i < job->node_tasks; i++) {
		/* if this task was killed by cmd, ignore its
		 * return status as it only reflects the fact
		 * that we killed it
		 */
		if (job->task[i]->killed_by_cmd) {
			debug("get_exit_code task %u killed by cmd", i);
			continue;
		}
		/* if this task called PMI_Abort or PMI2_Abort,
		 * then we let it define the exit status
		 */
		if (job->task[i]->aborted) {
			step_rc = job->task[i]->estatus;
			debug("get_exit_code task %u called abort", i);
			break;
		}
		/* If signaled we need to cycle thru all the
		 * tasks in case one of them called abort
		 */
		if (WIFSIGNALED(job->task[i]->estatus)) {
			error("get_exit_code task %u died by signal: %d",
			      i, WTERMSIG(job->task[i]->estatus));
			step_rc = job->task[i]->estatus;
			break;
		}
		if ((job->task[i]->estatus & 0xff) == SIG_OOM) {
			step_rc = job->task[i]->estatus;
		} else if ((step_rc  & 0xff) != SIG_OOM) {
			step_rc = MAX(step_complete.step_rc,
				      job->task[i]->estatus);
		}
	}
	/* If we killed all the tasks by cmd give at least one return
	   code. */
	if (step_rc == NO_VAL && job->task[0])
		step_rc = job->task[0]->estatus;

	return step_rc;
}

/*
 * Send batch exit code to slurmctld. Non-zero rc will DRAIN the node.
 */
extern void
batch_finish(stepd_step_rec_t *job, int rc)
{
	step_complete.step_rc = _get_exit_code(job);

	if (job->argv[0] && (unlink(job->argv[0]) < 0))
		error("unlink(%s): %m", job->argv[0]);

	if (job->aborted) {
		if ((job->stepid == NO_VAL) ||
		    (job->stepid == SLURM_BATCH_SCRIPT)) {
			info("step %u.%u abort completed",
			     job->jobid, job->stepid);
		} else
			info("job %u abort completed", job->jobid);
	} else if ((job->stepid == NO_VAL) ||
		   (job->stepid == SLURM_BATCH_SCRIPT)) {
		verbose("job %u completed with slurm_rc = %d, job_rc = %d",
			job->jobid, rc, step_complete.step_rc);
		_send_complete_batch_script_msg(job, rc, step_complete.step_rc);
	} else {
		stepd_wait_for_children_slurmstepd(job);
		verbose("job %u.%u completed with slurm_rc = %d, job_rc = %d",
			job->jobid, job->stepid, rc, step_complete.step_rc);
		stepd_send_step_complete_msgs(job);
	}

	/* Do not purge directory until slurmctld is notified of batch job
	 * completion to avoid race condition with slurmd registering missing
	 * batch job. */
	if (job->batchdir && (rmdir(job->batchdir) < 0))
		error("rmdir(%s): %m",  job->batchdir);
	xfree(job->batchdir);
}

/*
 * Launch a batch job script on the current node
 */
stepd_step_rec_t *
mgr_launch_batch_job_setup(batch_job_launch_msg_t *msg, slurm_addr_t *cli)
{
	stepd_step_rec_t *job = NULL;
	char *err_msg = NULL;

	if (!(job = batch_stepd_step_rec_create(msg))) {
		xstrfmtcat(err_msg,
			   "batch_stepd_step_rec_create() failed for job %u.%u on %s: %s",
			   msg->job_id, msg->step_id, conf->hostname,
			   slurm_strerror(errno));
		(void) log_ctld(LOG_LEVEL_ERROR, err_msg);
		error("%s", err_msg);
		xfree(err_msg);
		return NULL;
	}

	if ((job->batchdir = _make_batch_dir(job)) == NULL) {
		goto cleanup;
	}

	xfree(job->argv[0]);

	if ((job->argv[0] = _make_batch_script(msg, job->batchdir)) == NULL) {
		goto cleanup;
	}

	/* this is the new way of setting environment variables */
	env_array_for_batch_job(&job->env, msg, conf->node_name);

	/* this is the old way of setting environment variables (but
	 * needed) */
	job->envtp->overcommit = msg->overcommit;
	job->envtp->select_jobinfo = msg->select_jobinfo;

	return job;

cleanup:
	xstrfmtcat(err_msg,
		   "batch script setup failed for job %u.%u on %s: %s",
		   msg->job_id, msg->step_id, conf->hostname,
		   slurm_strerror(errno));
	(void) log_ctld(LOG_LEVEL_ERROR, err_msg);
	error("%s", err_msg);
	xfree(err_msg);

	if (job->aborted)
		verbose("job %u abort complete", job->jobid);

	/* Do not purge directory until slurmctld is notified of batch job
	 * completion to avoid race condition with slurmd registering missing
	 * batch job. */
	if (job->batchdir && (rmdir(job->batchdir) < 0))
		error("rmdir(%s): %m",  job->batchdir);
	xfree(job->batchdir);

	errno = ESLURMD_CREATE_BATCH_DIR_ERROR;

	return NULL;
}

static int
_setup_normal_io(stepd_step_rec_t *job)
{
	int rc = 0, ii = 0;
	struct priv_state sprivs;

	debug2("Entering _setup_normal_io");

	/*
	 * Temporarily drop permissions, initialize task stdio file
	 * descriptors (which may be connected to files), then
	 * reclaim privileges.
	 */
	if (_drop_privileges(job, true, &sprivs, true) < 0)
		return ESLURMD_SET_UID_OR_GID_ERROR;

	if (io_init_tasks_stdio(job) != SLURM_SUCCESS) {
		rc = ESLURMD_IO_ERROR;
		goto claim;
	}

	/*
	 * MUST create the initial client object before starting
	 * the IO thread, or we risk losing stdout/err traffic.
	 */
	if (!job->batch) {
		srun_info_t *srun = list_peek(job->sruns);

		/* local id of task that sends to srun, -1 for all tasks,
		   any other value for no tasks */
		int srun_stdout_tasks = -1;
		int srun_stderr_tasks = -1;

		xassert(srun != NULL);

		/* If I/O is labelled with task num, and if a separate file is
		   written per node or per task, the I/O needs to be sent
		   back to the stepd, get a label appended, and written from
		   the stepd rather than sent back to srun or written directly
		   from the node.  When a task has ofname or efname == NULL, it
		   means data gets sent back to the client. */
		if (job->flags & LAUNCH_LABEL_IO) {
			slurmd_filename_pattern_t outpattern, errpattern;
			bool same = false;
			int file_flags;

			io_find_filename_pattern(job, &outpattern, &errpattern,
						 &same);
			file_flags = io_get_file_flags(job);

			/* Make eio objects to write from the slurmstepd */
			if (outpattern == SLURMD_ALL_UNIQUE) {
				/* Open a separate file per task */
				for (ii = 0; ii < job->node_tasks; ii++) {
					rc = io_create_local_client(
						job->task[ii]->ofname,
						file_flags, job, 1,
						job->task[ii]->id,
						same ? job->task[ii]->id : -2);
					if (rc != SLURM_SUCCESS) {
						error("Could not open output file %s: %m",
						      job->task[ii]->ofname);
						rc = ESLURMD_IO_ERROR;
						goto claim;
					}
				}
				srun_stdout_tasks = -2;
				if (same)
					srun_stderr_tasks = -2;
			} else if (outpattern == SLURMD_ALL_SAME) {
				/* Open a file for all tasks */
				rc = io_create_local_client(
					job->task[0]->ofname, file_flags,
					job, 1, -1, same ? -1 : -2);
				if (rc != SLURM_SUCCESS) {
					error("Could not open output file %s: %m",
					      job->task[0]->ofname);
					rc = ESLURMD_IO_ERROR;
					goto claim;
				}
				srun_stdout_tasks = -2;
				if (same)
					srun_stderr_tasks = -2;
			}

			if (!same) {
				if (errpattern == SLURMD_ALL_UNIQUE) {
					/* Open a separate file per task */
					for (ii = 0;
					     ii < job->node_tasks; ii++) {
						rc = io_create_local_client(
							job->task[ii]->efname,
							file_flags, job, 1,
							-2, job->task[ii]->id);
						if (rc != SLURM_SUCCESS) {
							error("Could not open error file %s: %m",
							      job->task[ii]->
							      efname);
							rc = ESLURMD_IO_ERROR;
							goto claim;
						}
					}
					srun_stderr_tasks = -2;
				} else if (errpattern == SLURMD_ALL_SAME) {
					/* Open a file for all tasks */
					rc = io_create_local_client(
						job->task[0]->efname,
						file_flags, job, 1, -2, -1);
					if (rc != SLURM_SUCCESS) {
						error("Could not open error file %s: %m",
						      job->task[0]->efname);
						rc = ESLURMD_IO_ERROR;
						goto claim;
					}
					srun_stderr_tasks = -2;
				}
			}
		}

		if (io_initial_client_connect(srun, job, srun_stdout_tasks,
					     srun_stderr_tasks) < 0) {
			rc = ESLURMD_IO_ERROR;
			goto claim;
		}
	}

claim:
	if (_reclaim_privileges(&sprivs) < 0) {
		error("sete{u/g}id(%lu/%lu): %m",
		      (u_long) sprivs.saved_uid, (u_long) sprivs.saved_gid);
	}

	if (!rc && !job->batch)
		io_thread_start(job);

	debug2("Leaving  _setup_normal_io");
	return rc;
}

static int
_setup_user_managed_io(stepd_step_rec_t *job)
{
	srun_info_t *srun;

	if ((srun = list_peek(job->sruns)) == NULL) {
		error("_setup_user_managed_io: no clients!");
		return SLURM_ERROR;
	}

	return user_managed_io_client_connect(job->node_tasks, srun, job->task);
}

static void
_random_sleep(stepd_step_rec_t *job)
{
#if !defined HAVE_FRONT_END
	long int delay = 0;
	long int max   = (slurm_get_tcp_timeout() * job->nnodes);

	max = MIN(max, 5000);
	srand48((long int) (job->jobid + job->nodeid));

	delay = lrand48() % ( max + 1 );
	debug3("delaying %ldms", delay);
	if (poll(NULL, 0, delay) == -1)
		error("%s: poll(): %m", __func__);
#endif
}

/*
 * Send task exit message for n tasks. tid is the list of _global_
 * task ids that have exited
 */
static int
_send_exit_msg(stepd_step_rec_t *job, uint32_t *tid, int n, int status)
{
	slurm_msg_t     resp;
	task_exit_msg_t msg;
	ListIterator    i       = NULL;
	srun_info_t    *srun    = NULL;

	debug3("sending task exit msg for %d tasks status %d oom %d",
	       n, status, job->oom_error);

	memset(&msg, 0, sizeof(msg));
	msg.task_id_list	= tid;
	msg.num_tasks		= n;
	if (job->oom_error)
		msg.return_code = SIG_OOM;
	else
		msg.return_code = status;
	msg.job_id		= job->jobid;
	msg.step_id		= job->stepid;
	slurm_msg_t_init(&resp);
	resp.data		= &msg;
	resp.msg_type		= MESSAGE_TASK_EXIT;

	/*
	 *  Hack for TCP timeouts on exit of large, synchronized job
	 *  termination. Delay a random amount if job->nnodes > 500
	 */
	if (job->nnodes > 500)
		_random_sleep(job);

	/*
	 * Notify each srun and sattach.
	 * No message for poe or batch jobs
	 */
	i = list_iterator_create(job->sruns);
	while ((srun = list_next(i))) {
		resp.address = srun->resp_addr;
		if ((resp.address.sin_family == 0) &&
		    (resp.address.sin_port == 0)   &&
		    (resp.address.sin_addr.s_addr == 0))
			continue;	/* no srun or sattach here */

		/* This should always be set to something else we have a bug. */
		xassert(srun->protocol_version);
		resp.protocol_version = srun->protocol_version;

		if (_send_srun_resp_msg(&resp, job->nnodes) != SLURM_SUCCESS)
			error("Failed to send MESSAGE_TASK_EXIT: %m");
	}
	list_iterator_destroy(i);

	return SLURM_SUCCESS;
}

extern void stepd_wait_for_children_slurmstepd(stepd_step_rec_t *job)
{
	int left = 0;
	int rc;
	struct timespec ts = {0, 0};

	slurm_mutex_lock(&step_complete.lock);

	/* wait an extra 3 seconds for every level of tree below this level */
	if (step_complete.bits && (step_complete.children > 0)) {
		ts.tv_sec += 3 * (step_complete.max_depth-step_complete.depth);
		ts.tv_sec += time(NULL) + REVERSE_TREE_CHILDREN_TIMEOUT;

		while((left = bit_clear_count(step_complete.bits)) > 0) {
			debug3("Rank %d waiting for %d (of %d) children",
			       step_complete.rank, left,
			       step_complete.children);
			rc = pthread_cond_timedwait(&step_complete.cond,
						    &step_complete.lock, &ts);
			if (rc == ETIMEDOUT) {
				debug2("Rank %d timed out waiting for %d"
				       " (of %d) children", step_complete.rank,
				       left, step_complete.children);
				break;
			}
		}
		if (left == 0) {
			debug2("Rank %d got all children completions",
			       step_complete.rank);
		}
	} else {
		debug2("Rank %d has no children slurmstepd",
		       step_complete.rank);
	}

	step_complete.step_rc = _get_exit_code(job);
	step_complete.wait_children = false;

	slurm_mutex_unlock(&step_complete.lock);
}

/*
 * Send a single step completion message, which represents a single range
 * of complete job step nodes.
 */
/* caller is holding step_complete.lock */
static void
_one_step_complete_msg(stepd_step_rec_t *job, int first, int last)
{
	slurm_msg_t req;
	step_complete_msg_t msg;
	int rc = -1;
	int retcode;
	int i;
	uint16_t port = 0;
	char ip_buf[16];
	static bool acct_sent = false;

	debug2("_one_step_complete_msg: first=%d, last=%d", first, last);

	if (job->batch) {	/* Nested batch step anomalies */
		if (first == -1)
			first = 0;
		if (last == -1)
			last = 0;
	}
	memset(&msg, 0, sizeof(msg));
	msg.job_id = job->jobid;
	msg.job_step_id = job->stepid;
	msg.range_first = first;
	msg.range_last = last;
	if (job->oom_error)
		msg.step_rc = SIG_OOM;
	else
		msg.step_rc = step_complete.step_rc;
	msg.jobacct = jobacctinfo_create(NULL);
	/************* acct stuff ********************/
	if (!acct_sent) {
		/*
		 * No need to call _local_jobaccinfo_aggregate, job->jobacct
		 * already has the modified total for this node in the step.
		 */
		jobacctinfo_aggregate(step_complete.jobacct, job->jobacct);
		jobacctinfo_getinfo(step_complete.jobacct,
				    JOBACCT_DATA_TOTAL, msg.jobacct,
				    SLURM_PROTOCOL_VERSION);
		acct_sent = true;
	}
	/*********************************************/
	slurm_msg_t_init(&req);
	req.msg_type = REQUEST_STEP_COMPLETE;
	req.data = &msg;
	req.address = step_complete.parent_addr;

	/* Do NOT change this check to "step_complete.rank != 0", because
	 * there are odd situations where SlurmUser or root could
	 * craft a launch without a valid credential, and no tree information
	 * can be built with out the hostlist from the credential.
	 */
	if (step_complete.parent_rank != -1) {
		debug3("Rank %d sending complete to rank %d, range %d to %d",
		       step_complete.rank, step_complete.parent_rank,
		       first, last);
		/* On error, pause then try sending to parent again.
		 * The parent slurmstepd may just not have started yet, because
		 * of the way that the launch message forwarding works.
		 */
		for (i = 0; i < REVERSE_TREE_PARENT_RETRY; i++) {
			if (i)
				sleep(1);
			retcode = slurm_send_recv_rc_msg_only_one(&req, &rc, 0);
			if ((retcode == 0) && (rc == 0))
				goto finished;
		}
		/* on error AGAIN, send to the slurmctld instead */
		debug3("Rank %d sending complete to slurmctld instead, range "
		       "%d to %d", step_complete.rank, first, last);
	} else if (conf->msg_aggr_window_msgs > 1) {
		/* this is the base of the tree, its parent is slurmctld */
		debug3("Rank %d sending complete to slurmd for message aggr, "
		       "range %d to %d",
		       step_complete.rank, first, last);
		/* this is the base of the tree, but we are doing
		 * message aggr so send it to the slurmd to handle */
		req.msg_type = REQUEST_STEP_COMPLETE_AGGR;
		slurm_set_addr_char(&req.address, conf->port, conf->hostname);
		for (i = 0; i <= REVERSE_TREE_PARENT_RETRY; i++) {
			if (i)
				sleep(1);
			retcode = slurm_send_recv_rc_msg_only_one(&req, &rc, 0);
			if ((retcode == 0) && (rc == 0))
				goto finished;
		}
		req.msg_type = REQUEST_STEP_COMPLETE;
		/* this is the base of the tree, its parent is slurmctld */
		debug3("Rank %d sending complete to slurmctld instead, range "
		       "%d to %d", step_complete.rank, first, last);
	}  else {
		/* this is the base of the tree, its parent is slurmctld */
		debug3("Rank %d sending complete to slurmctld, range %d to %d",
		       step_complete.rank, first, last);
	}

	/* Retry step complete RPC send to slurmctld indefinitely.
	 * Prevent orphan job step if slurmctld is down */
	i = 1;
	while (slurm_send_recv_controller_rc_msg(&req, &rc,
						 working_cluster_rec) < 0) {
		if (i++ == 1) {
			slurm_get_ip_str(&step_complete.parent_addr, &port,
					 ip_buf, sizeof(ip_buf));
			error("Rank %d failed sending step completion message "
			      "directly to slurmctld (%s:%u), retrying",
			      step_complete.rank, ip_buf, port);
		}
		sleep(60);
	}
	if (i > 1) {
		info("Rank %d sent step completion message directly to "
		     "slurmctld (%s:%u)", step_complete.rank, ip_buf, port);
	}

finished:
	jobacctinfo_destroy(msg.jobacct);
}

/*
 * Given a starting bit in the step_complete.bits bitstring, "start",
 * find the next contiguous range of set bits and return the first
 * and last indices of the range in "first" and "last".
 *
 * caller is holding step_complete.lock
 */
static int
_bit_getrange(int start, int size, int *first, int *last)
{
	int i;
	bool found_first = false;

	if (!step_complete.bits)
		return 0;

	for (i = start; i < size; i++) {
		if (bit_test(step_complete.bits, i)) {
			if (found_first) {
				*last = i;
				continue;
			} else {
				found_first = true;
				*first = i;
				*last = i;
			}
		} else {
			if (!found_first) {
				continue;
			} else {
				*last = i - 1;
				break;
			}
		}
	}

	if (found_first)
		return 1;
	else
		return 0;
}

/*
 * Send as many step completion messages as necessary to represent
 * all completed nodes in the job step.  There may be nodes that have
 * not yet signaled their completion, so there will be gaps in the
 * completed node bitmap, requiring that more than one message be sent.
 */
extern void stepd_send_step_complete_msgs(stepd_step_rec_t *job)
{
	int start, size;
	int first = -1, last = -1;
	bool sent_own_comp_msg = false;

	slurm_mutex_lock(&step_complete.lock);
	start = 0;
	if (step_complete.bits)
		size = bit_size(step_complete.bits);
	else
		size = 0;

	/* If no children, send message and return early */
	if (size == 0) {
		_one_step_complete_msg(job, step_complete.rank,
				       step_complete.rank);
		slurm_mutex_unlock(&step_complete.lock);
		return;
	}

	while (_bit_getrange(start, size, &first, &last)) {
		/* THIS node is not in the bit string, so we need to prepend
		 * the local rank */
		if (start == 0 && first == 0) {
			sent_own_comp_msg = true;
			first = -1;
		}

		_one_step_complete_msg(job, (first + step_complete.rank + 1),
	      			       (last + step_complete.rank + 1));
		start = last + 1;
	}

	if (!sent_own_comp_msg) {
		_one_step_complete_msg(job, step_complete.rank,
				       step_complete.rank);
	}

	slurm_mutex_unlock(&step_complete.lock);
}

/* This dummy function is provided so that the checkpoint functions can
 * 	resolve this symbol name (as needed for some of the checkpoint
 *	functions used by slurmctld). */
extern void agent_queue_request(void *dummy)
{
	fatal("Invalid agent_queue_request function call, likely from "
	      "checkpoint plugin");
}

static void _set_job_state(stepd_step_rec_t *job, slurmstepd_state_t new_state)
{
	slurm_mutex_lock(&job->state_mutex);
	job->state = new_state;
	slurm_cond_signal(&job->state_cond);
	slurm_mutex_unlock(&job->state_mutex);
}

static int _spawn_job_container(stepd_step_rec_t *job)
{
	jobacctinfo_t *jobacct = NULL;
	struct rusage rusage;
	jobacct_id_t jobacct_id;
	int status = 0;
	pid_t pid;
	int rc = SLURM_SUCCESS;
	uint32_t jobid;
	int x11_pipe[2] = {0, 0};

	if (job->x11 && (pipe(x11_pipe) < 0)) {
		error("x11 pipe: %m");
		/* let the slurmd know we actually are done with the setup */
		close_slurmd_conn();
		return SLURM_ERROR;
	}

	debug2("%s: Before call to spank_init()", __func__);
	if (spank_init(job) < 0) {
		error("%s: Plugin stack initialization failed.", __func__);
		/* let the slurmd know we actually are done with the setup */
		close_slurmd_conn();
		return SLURM_PLUGIN_NAME_INVALID;
	}
	debug2("%s: After call to spank_init()", __func__);

	set_oom_adj(0);	/* the tasks may be killed by OOM */
	if (task_g_pre_setuid(job)) {
		error("%s: Failed to invoke task plugins: one of "
		      "task_p_pre_setuid functions returned error", __func__);
	}

	acct_gather_profile_g_task_start(0);
	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		setsid();
		acct_gather_profile_g_child_forked();

		_unblock_signals();

		if (job->x11) {
			int display, len = 0;
			char *xauthority;

			close(x11_pipe[0]);

			/* will create several detached threads to process */
			if (setup_x11_forward(job, &display, &xauthority)) {
				/* ssh forwarding setup failed */
				error("x11 port forwarding setup failed");
				exit(127);
			}

			/* send back x11 display number to parent stepd */
			if (write(x11_pipe[1], &display, sizeof(int))
			    != sizeof(int)) {
				error("%s: failed sending display number back: %m",
				      __func__);
			}

			/* send back temporary xauthority file location */
			if (xauthority)
				len = strlen(xauthority) + 1;

			if (write(x11_pipe[1], &len, sizeof(int))
			    != sizeof(int)) {
				error("%s: failed sending XAUTHORITY back: %m",
				      __func__);
			}

			if (write(x11_pipe[1], xauthority, len) != len) {
				error("%s: failed sending XAUTHORITY back: %m",
				      __func__);
			}

			xfree(xauthority);
			close(x11_pipe[1]);

			/*
			 * Have this thread sleep indefinitely to prevent the
			 * process from ending, since all other threads left
			 * are detached.
			 */
			while (true) /* in case of interrupted sleep */
				sleep(100000);

			exit(1);
		} else {
			/*
			 * Need to exec() something for proctrack/linuxproc to
			 * work, it will not keep a process named "slurmstepd"
			 */

			execl(SLEEP_CMD, "sleep", "100000000", NULL);
			error("execl: %m");
			sleep(1);
			exit(0);
		}
	} else if (pid < 0) {
		error("fork: %m");
		_set_job_state(job, SLURMSTEPD_STEP_ENDING);
		rc = SLURM_ERROR;
		/* let the slurmd know we actually are done with the setup */
		close_slurmd_conn();
		goto fail1;
	}

	job->pgid = pid;

	if ((rc = proctrack_g_add(job, pid)) != SLURM_SUCCESS) {
		error("%s: Step %u.%u unable to add pid %d to the proctrack plugin",
		      __func__, job->jobid, job->stepid, pid);
		killpg(pid, SIGKILL);
		kill(pid, SIGKILL);
		/* let the slurmd know we actually are done with the setup */
		close_slurmd_conn();
		goto fail1;
	}

	jobacct_id.nodeid = job->nodeid;
	jobacct_id.taskid = job->nodeid;   /* Treat node ID as global task ID */
	jobacct_id.job    = job;
	jobacct_gather_set_proctrack_container_id(job->cont_id);
	jobacct_gather_add_task(pid, &jobacct_id, 1);
#ifdef HAVE_NATIVE_CRAY
	if (job->pack_jobid && (job->pack_jobid != NO_VAL))
		jobid = job->pack_jobid;
	else
		jobid = job->jobid;
#else
	jobid = job->jobid;
#endif
	container_g_add_cont(jobid, job->cont_id);

	/*
	 * For the X11 forwarding, we need to know what local port number the
	 * forwarding code has started listening on so that the slurmd can
	 * set the correct DISPLAY variable for this node.
	 * This also serves to signal that the forwarding code has started
	 * and is ready to accept connections.
	 * If the port is zero then the forwarding code failed, and the prolog
	 * needs to be marked as having failed as well.
	 */
	if (job->x11) {
		int len;

		close(x11_pipe[1]);
		if (read(x11_pipe[0], &job->x11_display, sizeof(int))
		    != sizeof(int)) {
			error("%s: failed retrieving x11 display value: %m",
			      __func__);
			job->x11_display = 0;
		}

		if (read(x11_pipe[0], &len, sizeof(int)) != sizeof(int)) {
			error("%s: failed retrieving x11 authority value: %m",
			      __func__);
		}

		if (len)
			job->x11_xauthority = xmalloc(len);

		if (read(x11_pipe[0], job->x11_xauthority, len) != len) {
			error("%s: failed retrieving x11 authority value: %m",
			      __func__);
		}

		close(x11_pipe[0]);

		debug("x11 forwarding local display is %d", job->x11_display);
		if (job->x11_xauthority)
			debug("x11 forwarding local xauthority is %s",
			      job->x11_xauthority);
	}

	_set_job_state(job, SLURMSTEPD_STEP_RUNNING);
	if (!conf->job_acct_gather_freq)
		jobacct_gather_stat_task(0);

	if (spank_task_post_fork(job, -1) < 0)
		error("spank extern task post-fork failed");

	/* let the slurmd know we actually are done with the setup */
	close_slurmd_conn();

	while ((wait4(pid, &status, 0, &rusage) < 0) && (errno == EINTR)) {
		;	       /* Wait until above process exits from signal */
	}

	jobacct = jobacct_gather_remove_task(pid);
	if (jobacct) {
		jobacctinfo_setinfo(jobacct,
				    JOBACCT_DATA_RUSAGE, &rusage,
				    SLURM_PROTOCOL_VERSION);
		job->jobacct->energy.consumed_energy = 0;
		_local_jobacctinfo_aggregate(job->jobacct, jobacct);
		jobacctinfo_destroy(jobacct);
	}
	acct_gather_profile_g_task_end(pid);
	step_complete.rank = job->nodeid;
	acct_gather_profile_endpoll();
	acct_gather_profile_g_node_step_end();
	acct_gather_profile_fini();

	/* Call the other plugins to clean up
	 * the cgroup hierarchy.
	 */
	_set_job_state(job, SLURMSTEPD_STEP_ENDING);
	step_terminate_monitor_start(job);
	proctrack_g_signal(job->cont_id, SIGKILL);
	proctrack_g_wait(job->cont_id);
	step_terminate_monitor_stop();

	task_g_post_step(job);

fail1:
	debug2("%s: Before call to spank_fini()", __func__);
	if (spank_fini(job) < 0)
		error("spank_fini failed");
	debug2("%s: After call to spank_fini()", __func__);

	_set_job_state(job, SLURMSTEPD_STEP_ENDING);

	if (step_complete.rank > -1)
		stepd_wait_for_children_slurmstepd(job);
	stepd_send_step_complete_msgs(job);

	return rc;
}

/*
 * Executes the functions of the slurmd job manager process,
 * which runs as root and performs shared memory and interconnect
 * initialization, etc.
 *
 * Returns 0 if job ran and completed successfully.
 * Returns errno if job startup failed. NOTE: This will DRAIN the node.
 */
int
job_manager(stepd_step_rec_t *job)
{
	int  rc = SLURM_SUCCESS;
	bool io_initialized = false;
	char *ckpt_type = slurm_get_checkpoint_type();
	char *err_msg = NULL;

	debug3("Entered job_manager for %u.%u pid=%d",
	       job->jobid, job->stepid, job->jmgr_pid);

#ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#endif /* PR_SET_DUMPABLE */

	/*
	 * Run acct_gather_conf_init() now so we don't drop permissions on any
	 * of the gather plugins.
	 * Preload all plugins afterwards to avoid plugin changes
	 * (i.e. due to a Slurm upgrade) after the process starts.
	 */
	if ((acct_gather_conf_init() != SLURM_SUCCESS)          ||
	    (core_spec_g_init() != SLURM_SUCCESS)		||
	    (switch_init(1) != SLURM_SUCCESS)			||
	    (slurm_proctrack_init() != SLURM_SUCCESS)		||
	    (slurmd_task_init() != SLURM_SUCCESS)		||
	    (checkpoint_init(ckpt_type) != SLURM_SUCCESS)	||
	    (jobacct_gather_init() != SLURM_SUCCESS)		||
	    (acct_gather_profile_init() != SLURM_SUCCESS)	||
	    (slurm_cred_init() != SLURM_SUCCESS)		||
	    (job_container_init() != SLURM_SUCCESS)		||
	    (gres_plugin_init() != SLURM_SUCCESS)) {
		rc = SLURM_PLUGIN_NAME_INVALID;
		goto fail1;
	}
	if (!job->batch && (job->stepid != SLURM_EXTERN_CONT) &&
	    (mpi_hook_slurmstepd_init(&job->env) != SLURM_SUCCESS)) {
		rc = SLURM_MPI_PLUGIN_NAME_INVALID;
		goto fail1;
	}

	if (!job->batch && (job->stepid != SLURM_EXTERN_CONT) &&
	    (switch_g_job_preinit(job->switch_job) < 0)) {
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto fail1;
	}

	if ((job->cont_id == 0) &&
	    (proctrack_g_create(job) != SLURM_SUCCESS)) {
		error("proctrack_g_create: %m");
		rc = ESLURMD_SETUP_ENVIRONMENT_ERROR;
		goto fail1;
	}

	if (job->stepid == SLURM_EXTERN_CONT)
		return _spawn_job_container(job);

	if (!job->batch && (job->accel_bind_type || job->tres_bind ||
	    job->tres_freq)) {
		info("Running gres_plugin_node_config_load()!");
		(void) gres_plugin_node_config_load(conf->cpus, conf->node_name,
						NULL,
						(void *)&xcpuinfo_abs_to_mac,
						(void *)&xcpuinfo_mac_to_abs);
	}

	debug2("Before call to spank_init()");
	if (spank_init (job) < 0) {
		error ("Plugin stack initialization failed.");
		rc = SLURM_PLUGIN_NAME_INVALID;
		goto fail1;
	}
	debug2("After call to spank_init()");

	/* Call switch_g_job_init() before becoming user */
	if (!job->batch && job->argv && (switch_g_job_init(job) < 0)) {
		/* error("switch_g_job_init: %m"); already logged */
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto fail2;
	}

	/* fork necessary threads for checkpoint */
	if (checkpoint_stepd_prefork(job) != SLURM_SUCCESS) {
		error("Failed checkpoint_stepd_prefork");
		rc = SLURM_ERROR;
		xstrfmtcat(err_msg,
			   "checkpoint_stepd_prefork failure for job %u.%u on %s",
			   job->jobid, job->stepid, conf->hostname);
		(void) log_ctld(LOG_LEVEL_ERROR, err_msg);
		xfree(err_msg);
		io_close_task_fds(job);
		goto fail3;
	}

	/* fork necessary threads for MPI */
	if (!job->batch && (job->stepid != SLURM_EXTERN_CONT) &&
	    (mpi_hook_slurmstepd_prefork(job, &job->env) != SLURM_SUCCESS)) {
		error("Failed mpi_hook_slurmstepd_prefork");
		rc = SLURM_ERROR;
		xstrfmtcat(err_msg,
			   "mpi_hook_slurmstepd_prefork failure for job %u.%u on %s",
			   job->jobid, job->stepid, conf->hostname);
		(void) log_ctld(LOG_LEVEL_ERROR, err_msg);
		xfree(err_msg);
		goto fail3;
	}

	if (!job->batch && (job->node_tasks <= 1) &&
	    (job->accel_bind_type || job->tres_bind)) {
		job->accel_bind_type = 0;
		xfree(job->tres_bind);
	}
	if (!job->batch && (job->node_tasks > 1) &&
	    (job->accel_bind_type || job->tres_bind)) {
		uint64_t gpu_cnt, mic_cnt, nic_cnt;
		gpu_cnt = gres_plugin_step_count(job->step_gres_list, "gpu");
		mic_cnt = gres_plugin_step_count(job->step_gres_list, "mic");
		nic_cnt = gres_plugin_step_count(job->step_gres_list, "nic");
		if ((gpu_cnt <= 1) || (gpu_cnt == NO_VAL64))
			job->accel_bind_type &= (~ACCEL_BIND_CLOSEST_GPU);
		if ((mic_cnt <= 1) || (mic_cnt == NO_VAL64))
			job->accel_bind_type &= (~ACCEL_BIND_CLOSEST_MIC);
		if ((nic_cnt <= 1) || (nic_cnt == NO_VAL64))
			job->accel_bind_type &= (~ACCEL_BIND_CLOSEST_NIC);
		if (job->accel_bind_type == ACCEL_BIND_VERBOSE)
			job->accel_bind_type = 0;
	}

	/*
	 * Calls pam_setup() and requires pam_finish() if
	 * successful.  Only check for < 0 here since other slurm
	 * error codes could come that are more descriptive.
	 */
	if ((rc = _fork_all_tasks(job, &io_initialized)) < 0) {
		debug("_fork_all_tasks failed");
		rc = ESLURMD_EXECVE_FAILED;
		goto fail3;
	}

	/*
	 * If IO initialization failed, return SLURM_SUCCESS (on a
	 * batch step) or the node will be drain otherwise.  Regular
	 * srun needs the error sent or it will hang waiting for the
	 * launch to happen.
	 */
	if ((rc != SLURM_SUCCESS) || !io_initialized)
		goto fail3;

	io_close_task_fds(job);

	reattach_job = job;

	/* Attach slurmstepd to system cgroups, if configured */
	attach_system_cgroup_pid(getpid());

	/* if we are not polling then we need to make sure we get some
	 * information here
	 */
	if (!conf->job_acct_gather_freq)
		jobacct_gather_stat_task(0);

	/* Send job launch response with list of pids */
	_send_launch_resp(job, 0);
	_set_job_state(job, SLURMSTEPD_STEP_RUNNING);

#ifdef PR_SET_DUMPABLE
	/* RHEL6 requires setting "dumpable" flag AGAIN; after euid changes */
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#endif /* PR_SET_DUMPABLE */

	_wait_for_all_tasks(job);
	acct_gather_profile_endpoll();
	acct_gather_profile_g_node_step_end();
	acct_gather_profile_fini();

	_set_job_state(job, SLURMSTEPD_STEP_ENDING);

fail3:
	if (!job->batch &&
	    (switch_g_job_fini(job->switch_job) < 0)) {
		error("switch_g_job_fini: %m");
	}

fail2:
	/*
	 * First call switch_g_job_postfini() - In at least one case,
	 * this will clean up any straggling processes. If this call
	 * is moved behind wait_for_io(), we may block waiting for IO
	 * on a hung process.
	 *
	 * Make sure all processes in session are dead. On systems
	 * with an IBM Federation switch, all processes must be
	 * terminated before the switch window can be released by
	 * switch_g_job_postfini().
	 */
	_set_job_state(job, SLURMSTEPD_STEP_ENDING);
	step_terminate_monitor_start(job);
	if (job->cont_id != 0) {
		proctrack_g_signal(job->cont_id, SIGKILL);
		proctrack_g_wait(job->cont_id);
	}
	step_terminate_monitor_stop();
	if (!job->batch) {
		if (switch_g_job_postfini(job) < 0)
			error("switch_g_job_postfini: %m");
	}

	/*
	 * Wait for io thread to complete (if there is one)
	 */
	if (!job->batch && io_initialized &&
	    ((job->flags & LAUNCH_USER_MANAGED_IO) == 0))
		_wait_for_io(job);

	/*
	 * Warn task plugin that the user's step have terminated
	 */
	task_g_post_step(job);

	/*
	 * Reset CPU frequencies if changed
	 */
	if ((job->cpu_freq_min != NO_VAL) || (job->cpu_freq_max != NO_VAL) ||
	    (job->cpu_freq_gov != NO_VAL))
		cpu_freq_reset(job);

	/*
	 * Reset GRES hardware, if needed. This is where GPU frequency is reset.
	 * Make sure stepd is root. If not, emit error.
	 */
	if (!job->batch && job->tres_freq) {
		if (getuid() == (uid_t) 0)
			gres_plugin_step_hardware_fini();
		else
			error("%s: invalid permissions: cannot uninitialize GRES hardware unless Slurmd was started as root",
			      __func__);
	}

	/*
	 * Notify srun of completion AFTER frequency reset to avoid race
	 * condition starting another job on these CPUs.
	 */
	while (stepd_send_pending_exit_msgs(job)) {;}

	/*
	 * This just cleans up all of the PAM state in case rc == 0
	 * which means _fork_all_tasks performs well.
	 * Must be done after IO termination in case of IO operations
	 * require something provided by the PAM (i.e. security token)
	 */
	if (!rc)
		pam_finish();

	debug2("Before call to spank_fini()");
	if (spank_fini (job)  < 0) {
		error ("spank_fini failed");
	}
	debug2("After call to spank_fini()");

fail1:
	/* If interactive job startup was abnormal,
	 * be sure to notify client.
	 */
	_set_job_state(job, SLURMSTEPD_STEP_ENDING);
	if (rc != 0) {
		error("job_manager exiting abnormally, rc = %d", rc);
		_send_launch_resp(job, rc);
	}

	if (!job->batch && (step_complete.rank > -1)) {
		if (job->aborted)
			info("job_manager exiting with aborted job");
		else
			stepd_wait_for_children_slurmstepd(job);
		stepd_send_step_complete_msgs(job);
	}

	if (!job->batch && core_spec_g_clear(job->cont_id))
		error("core_spec_g_clear: %m");

	xfree(ckpt_type);
	return(rc);
}

static int _pre_task_child_privileged(
	stepd_step_rec_t *job, int taskid, struct priv_state *sp)
{
	if (_reclaim_privileges(sp) < 0)
		return SLURM_ERROR;

	if (spank_task_privileged(job, taskid) < 0)
		return error("spank_task_init_privileged failed");

	/* sp->gid_list should already be initialized */
	return (_drop_privileges(job, true, sp, false));
}

struct exec_wait_info {
	int id;
	pid_t pid;
	int parentfd;
	int childfd;
};

static struct exec_wait_info * _exec_wait_info_create (int i)
{
	int fdpair[2];
	struct exec_wait_info * e;

	if (pipe (fdpair) < 0) {
		error ("_exec_wait_info_create: pipe: %m");
		return NULL;
	}

	fd_set_close_on_exec(fdpair[0]);
	fd_set_close_on_exec(fdpair[1]);

	e = xmalloc (sizeof (*e));
	e->childfd = fdpair[0];
	e->parentfd = fdpair[1];
	e->id = i;
	e->pid = -1;

	return (e);
}

static void _exec_wait_info_destroy (struct exec_wait_info *e)
{
	if (e == NULL)
		return;

	if (e->parentfd >= 0) {
		close (e->parentfd);
		e->parentfd = -1;
	}
	if (e->childfd >= 0) {
		close (e->childfd);
		e->childfd = -1;
	}
	e->id = -1;
	e->pid = -1;
	xfree(e);
}

static pid_t _exec_wait_get_pid (struct exec_wait_info *e)
{
	if (e == NULL)
		return (-1);
	return (e->pid);
}

static struct exec_wait_info * _fork_child_with_wait_info (int id)
{
	struct exec_wait_info *e;

	if (!(e = _exec_wait_info_create (id)))
		return (NULL);

	if ((e->pid = fork ()) < 0) {
		_exec_wait_info_destroy (e);
		return (NULL);
	}
	/*
	 *  Close parentfd in child, and childfd in parent:
	 */
	if (e->pid == 0) {
		close (e->parentfd);
		e->parentfd = -1;
	} else {
		close (e->childfd);
		e->childfd = -1;
	}
	return (e);
}

static int _exec_wait_child_wait_for_parent (struct exec_wait_info *e)
{
	char c;

	if (read (e->childfd, &c, sizeof (c)) != 1)
		return error ("_exec_wait_child_wait_for_parent: failed: %m");

	return (0);
}

static int exec_wait_signal_child (struct exec_wait_info *e)
{
	char c = '\0';

	if (write (e->parentfd, &c, sizeof (c)) != 1)
		return error ("write to unblock task %d failed: %m", e->id);

	return (0);
}

static int exec_wait_signal (struct exec_wait_info *e, stepd_step_rec_t *job)
{
	debug3 ("Unblocking %u.%u task %d, writefd = %d",
		job->jobid, job->stepid, e->id, e->parentfd);
	exec_wait_signal_child (e);
	return (0);
}

/*
 *  Send SIGKILL to child in exec_wait_info 'e'
 *  Returns 0 for success, -1 for failure.
 */
static int exec_wait_kill_child (struct exec_wait_info *e)
{
	if (e->pid < 0)
		return (-1);
	if (kill (e->pid, SIGKILL) < 0)
		return (-1);
	e->pid = -1;
	return (0);
}

/*
 *  Send all children in exec_wait_list SIGKILL.
 *  Returns 0 for success or  < 0 on failure.
 */
static int exec_wait_kill_children (List exec_wait_list)
{
	int rc = 0;
	int count;
	struct exec_wait_info *e;
	ListIterator i;

	if ((count = list_count (exec_wait_list)) == 0)
		return (0);

	verbose ("Killing %d remaining child%s",
		 count, (count > 1 ? "ren" : ""));

	i = list_iterator_create (exec_wait_list);
	if (i == NULL)
		return error ("exec_wait_kill_children: iterator_create: %m");

	while ((e = list_next (i)))
		rc += exec_wait_kill_child (e);
	list_iterator_destroy (i);
	return (rc);
}

static void prepare_stdio (stepd_step_rec_t *job, stepd_step_task_info_t *task)
{
#ifdef HAVE_PTY_H
	if ((job->flags & LAUNCH_PTY) && (task->gtid == 0)) {
		if (login_tty(task->stdin_fd))
			error("login_tty: %m");
		else
			debug3("login_tty good");
		return;
	}
#endif
	io_dup_stdio(task);
	return;
}

static void _unblock_signals (void)
{
	sigset_t set;
	int i;

	for (i = 0; slurmstepd_blocked_signals[i]; i++) {
		/* eliminate pending signals, then set to default */
		xsignal(slurmstepd_blocked_signals[i], SIG_IGN);
		xsignal(slurmstepd_blocked_signals[i], SIG_DFL);
	}
	sigemptyset(&set);
	xsignal_set_mask (&set);
}

/*
 * fork and exec N tasks
 */
static int
_fork_all_tasks(stepd_step_rec_t *job, bool *io_initialized)
{
	int rc = SLURM_SUCCESS;
	int i;
	struct priv_state sprivs;
	jobacct_id_t jobacct_id;
	char *oom_value;
	List exec_wait_list = NULL;
	uint32_t jobid;

	DEF_TIMERS;
	START_TIMER;

	xassert(job != NULL);

	set_oom_adj(0);	/* the tasks may be killed by OOM */
	if (task_g_pre_setuid(job)) {
		error("Failed to invoke task plugins: one of task_p_pre_setuid functions returned error");
		return SLURM_ERROR;
	}

	/*
	 * Create hwloc xml file here to avoid threading issues later.
	 * This has to be done after task_g_pre_setuid().
	 */
	xcpuinfo_hwloc_topo_load(NULL, conf->hwloc_xml, false);

	/*
	 * Temporarily drop effective privileges, except for the euid.
	 * We need to wait until after pam_setup() to drop euid.
	 */
	if (_drop_privileges (job, false, &sprivs, true) < 0)
		return ESLURMD_SET_UID_OR_GID_ERROR;

	if (pam_setup(job->user_name, conf->hostname)
	    != SLURM_SUCCESS){
		error ("error in pam_setup");
		rc = SLURM_ERROR;
	}

	/*
	 * Reclaim privileges to do the io setup
	 */
	_reclaim_privileges (&sprivs);
	if (rc)
		goto fail1; /* pam_setup error */

	set_umask(job);		/* set umask for stdout/err files */
	if (job->flags & LAUNCH_USER_MANAGED_IO)
		rc = _setup_user_managed_io(job);
	else
		rc = _setup_normal_io(job);
	/*
	 * Initialize log facility to copy errors back to srun
	 */
	if (!rc)
		rc = _slurmd_job_log_init(job);

	if (rc) {
		error("IO setup failed: %m");
		job->task[0]->estatus = 0x0100;
		step_complete.step_rc = 0x0100;
		if (job->batch)
			rc = SLURM_SUCCESS;	/* drains node otherwise */
		goto fail1;
	} else {
		*io_initialized = true;
	}

	/*
	 * Now that errors will be copied back to srun, set the frequencies of
	 * the GPUs allocated to the step (and eventually other GRES hardware
	 * config options). Make sure stepd is root. If not, emit error.
	 * TODO: generic "settings" parameter rather than tres_freq
	 */
	if (!job->batch && job->tres_freq) {
		if (getuid() == (uid_t) 0) {
			gres_plugin_step_hardware_init(job->step_gres_list,
						       job->nodeid,
						       job->tres_freq);
		} else {
			error("%s: invalid permissions: cannot initialize GRES hardware unless Slurmd was started as root",
			      __func__);
		}
	}

	/*
	 * Temporarily drop effective privileges
	 */
	if (_drop_privileges (job, true, &sprivs, true) < 0) {
		error ("_drop_privileges: %m");
		rc = SLURM_ERROR;
		goto fail2;
	}

	if (chdir(job->cwd) < 0) {
		error("couldn't chdir to `%s': %m: going to /tmp instead",
		      job->cwd);
		if (chdir("/tmp") < 0) {
			error("couldn't chdir to /tmp either. dying.");
			rc = SLURM_ERROR;
			goto fail3;
		}
	}

	if (spank_user (job) < 0) {
		error("spank_user failed.");
		rc = SLURM_ERROR;
		goto fail4;
	}

	exec_wait_list = list_create ((ListDelF) _exec_wait_info_destroy);

	/*
	 * Fork all of the task processes.
	 */
	verbose("starting %u tasks", job->node_tasks);
	for (i = 0; i < job->node_tasks; i++) {
		char time_stamp[256];
		pid_t pid;
		struct exec_wait_info *ei;

		acct_gather_profile_g_task_start(i);
		if ((ei = _fork_child_with_wait_info (i)) == NULL) {
			error("child fork: %m");
			exec_wait_kill_children (exec_wait_list);
			rc = SLURM_ERROR;
			goto fail4;
		} else if ((pid = _exec_wait_get_pid (ei)) == 0)  { /* child */
			/*
			 *  Destroy exec_wait_list in the child.
			 *   Only exec_wait_info for previous tasks have been
			 *   added to the list so far, so everything else
			 *   can be discarded.
			 */
			FREE_NULL_LIST (exec_wait_list);

			/* jobacctinfo_endpoll();
			 * closing jobacct files here causes deadlock */

			if (conf->propagate_prio)
				_set_prio_process(job);

			/*
			 * Reclaim privileges for the child and call any plugin
			 * hooks that may require elevated privs
			 * sprivs.gid_list is already set from the
			 * _drop_privileges call above, no not reinitialize.
			 * NOTE: Only put things in here that are self contained
			 * and belong in the child.
			 */
			if (_pre_task_child_privileged(job, i, &sprivs) < 0)
				exit(1);

 			if (_become_user(job, &sprivs) < 0) {
 				error("_become_user failed: %m");
				/* child process, should not return */
				exit(1);
 			}

			/* log_fini(); */ /* note: moved into exec_task() */

			_unblock_signals();

			/*
			 *  Need to setup stdio before setpgid() is called
			 *   in case we are setting up a tty. (login_tty()
			 *   must be called before setpgid() or it is
			 *   effectively disabled).
			 */
			prepare_stdio (job, job->task[i]);

			/* Close profiling file descriptors */
			acct_gather_profile_g_child_forked();

			/*
			 *  Block until parent notifies us that it is ok to
			 *   proceed. This allows the parent to place all
			 *   children in any process groups or containers
			 *   before they make a call to exec(2).
			 */
			if (_exec_wait_child_wait_for_parent (ei) < 0)
				exit (1);

			exec_task(job, i);
		}

		/*
		 * Parent continues:
		 */

		list_append (exec_wait_list, ei);

		log_timestamp(time_stamp, sizeof(time_stamp));
		verbose("task %lu (%lu) started %s",
			(unsigned long) job->task[i]->gtid,
			(unsigned long) pid, time_stamp);

		job->task[i]->pid = pid;
		if (i == 0)
			job->pgid = pid;
	}

	/*
	 * All tasks are now forked and running as the user, but
	 * will wait for our signal before calling exec.
	 */

	/*
	 * Reclaim privileges
	 */
	if (_reclaim_privileges (&sprivs) < 0) {
		error ("Unable to reclaim privileges");
		/* Don't bother erroring out here */
	}

	if ((oom_value = getenv("SLURMSTEPD_OOM_ADJ"))) {
		int i = atoi(oom_value);
		debug("Setting slurmstepd oom_adj to %d", i);
		set_oom_adj(i);
	}

	if (chdir (sprivs.saved_cwd) < 0) {
		error ("Unable to return to working directory");
	}

	for (i = 0; i < job->node_tasks; i++) {
		/*
		 * Put this task in the step process group
		 * login_tty() must put task zero in its own
		 * session, causing setpgid() to fail, setsid()
		 * has already set its process group as desired
		 */
		if (((job->flags & LAUNCH_PTY) == 0) &&
		    (setpgid (job->task[i]->pid, job->pgid) < 0)) {
			error("Unable to put task %d (pid %d) into pgrp %d: %m",
			      i, job->task[i]->pid, job->pgid);
		}

		if (task_g_pre_launch_priv(job, job->task[i]->pid) < 0) {
			error("task_g_pre_launch_priv: %m");
			rc = SLURM_ERROR;
			goto fail2;
		}

		if (proctrack_g_add(job, job->task[i]->pid)
		    == SLURM_ERROR) {
			error("proctrack_g_add: %m");
			rc = SLURM_ERROR;
			goto fail2;
		}
		jobacct_id.nodeid = job->nodeid;
		jobacct_id.taskid = job->task[i]->gtid;
		jobacct_id.job    = job;
		if (i == (job->node_tasks - 1)) {
			/* start polling on the last task */
			jobacct_gather_set_proctrack_container_id(job->cont_id);
			jobacct_gather_add_task(job->task[i]->pid, &jobacct_id,
						1);
		} else {
			/* don't poll yet */
			jobacct_gather_add_task(job->task[i]->pid, &jobacct_id,
						0);
		}
		if (spank_task_post_fork (job, i) < 0) {
			error ("spank task %d post-fork failed", i);
			rc = SLURM_ERROR;
			goto fail2;
		}
	}
//	jobacct_gather_set_proctrack_container_id(job->cont_id);
#ifdef HAVE_NATIVE_CRAY
	if (job->pack_jobid && (job->pack_jobid != NO_VAL))
		jobid = job->pack_jobid;
	else
		jobid = job->jobid;
#else
	jobid = job->jobid;
#endif
	if (container_g_add_cont(jobid, job->cont_id) != SLURM_SUCCESS)
		error("container_g_add_cont(%u): %m", job->jobid);
	if (!job->batch && core_spec_g_set(job->cont_id, job->job_core_spec) &&
	    (job->stepid == 0))
		error("core_spec_g_set: %m");

	/*
	 * Now it's ok to unblock the tasks, so they may call exec.
	 */
	list_for_each (exec_wait_list, (ListForF) exec_wait_signal, job);
	FREE_NULL_LIST (exec_wait_list);

	for (i = 0; i < job->node_tasks; i++) {
		/*
		 * Prepare process for attach by parallel debugger
		 * (if specified and able)
		 */
		if (pdebug_trace_process(job, job->task[i]->pid)
		    == SLURM_ERROR) {
			rc = SLURM_ERROR;
			goto fail2;
		}
	}
	END_TIMER2(__func__);
	return rc;

fail4:
	if (chdir (sprivs.saved_cwd) < 0) {
		error ("Unable to return to working directory");
	}
fail3:
	_reclaim_privileges (&sprivs);
fail2:
	FREE_NULL_LIST(exec_wait_list);
	io_close_task_fds(job);
fail1:
	pam_finish();
	END_TIMER2(__func__);
	return rc;
}

/*
 * Loop once through tasks looking for all tasks that have exited with
 * the same exit status (and whose statuses have not been sent back to
 * the client) Aggregate these tasks into a single task exit message.
 *
 */
extern int stepd_send_pending_exit_msgs(stepd_step_rec_t *job)
{
	int  i;
	int  nsent  = 0;
	int  status = 0;
	bool set    = false;
	uint32_t *tid;

	/*
	 * Collect all exit codes with the same status into a
	 * single message.
	 */
	tid = xmalloc(sizeof(uint32_t) * job->node_tasks);
	for (i = 0; i < job->node_tasks; i++) {
		stepd_step_task_info_t *t = job->task[i];

		if (!t->exited || t->esent)
			continue;

		if (!set) {
			status = t->estatus;
			set    = true;
		} else if (status != t->estatus)
			continue;

		tid[nsent++] = t->gtid;
		t->esent = true;
	}

	if (nsent) {
		debug2("Aggregated %d task exit messages", nsent);
		_send_exit_msg(job, tid, nsent, status);
	}
	xfree(tid);

	return nsent;
}

static inline void
_log_task_exit(unsigned long taskid, unsigned long pid, int status)
{
	/*
	 *  Print a nice message to the log describing the task exit status.
	 *
	 *  The final else is there just in case there is ever an exit status
	 *   that isn't WIFEXITED || WIFSIGNALED. We'll probably never reach
	 *   that code, but it is better than dropping a potentially useful
	 *   exit status.
	 */
	if ((status & 0xff) == SIG_OOM) {
		verbose("task %lu (%lu) Out Of Memory (OOM)",
			taskid, pid);
	} else if (WIFEXITED(status)) {
		verbose("task %lu (%lu) exited with exit code %d.",
			taskid, pid, WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		/* WCOREDUMP isn't available on AIX */
		verbose("task %lu (%lu) exited. Killed by signal %d%s.",
			taskid, pid, WTERMSIG(status),
#ifdef WCOREDUMP
			WCOREDUMP(status) ? " (core dumped)" : ""
#else
			""
#endif
			);
	} else {
		verbose("task %lu (%lu) exited with status 0x%04x.",
			taskid, pid, status);
	}
}

/*
 * If waitflag is true, perform a blocking wait for a single process
 * and then return.
 *
 * If waitflag is false, do repeated non-blocking waits until
 * there are no more processes to reap (waitpid returns 0).
 *
 * Returns the number of tasks for which a wait3() was successfully
 * performed, or -1 if there are no child tasks.
 */
static int
_wait_for_any_task(stepd_step_rec_t *job, bool waitflag)
{
	stepd_step_task_info_t *t = NULL;
	int rc, status = 0;
	pid_t pid;
	int completed = 0;
	jobacctinfo_t *jobacct = NULL;
	struct rusage rusage;
	char **tmp_env;
	uint32_t task_offset = 0;

	if (job->pack_task_offset != NO_VAL)
		task_offset = job->pack_task_offset;
	do {
		pid = wait3(&status, waitflag ? 0 : WNOHANG, &rusage);
		if (pid == -1) {
			if (errno == ECHILD) {
				debug("No child processes");
				if (completed == 0)
					completed = -1;
				break;
			} else if (errno == EINTR) {
				debug("wait3 was interrupted");
				continue;
			} else {
				debug("Unknown errno %d", errno);
				continue;
			}
		} else if (pid == 0) { /* WNOHANG and no pids available */
			break;
		}

		/************* acct stuff ********************/
		jobacct = jobacct_gather_remove_task(pid);
		if (jobacct) {
			jobacctinfo_setinfo(jobacct,
					    JOBACCT_DATA_RUSAGE, &rusage,
					    SLURM_PROTOCOL_VERSION);
			/* Since we currently don't track energy
			   usage per task (only per step).  We take
			   into account only the last poll of the last task.
			   Odds are it is the only one with
			   information anyway, but just to be safe we
			   will zero out the previous value since this
			   one will over ride it.
			   If this ever changes in the future this logic
			   will need to change.
			*/
			if (jobacct->energy.consumed_energy)
				job->jobacct->energy.consumed_energy = 0;
			_local_jobacctinfo_aggregate(job->jobacct, jobacct);
			jobacctinfo_destroy(jobacct);
		}
		acct_gather_profile_g_task_end(pid);
		/*********************************************/

		if ((t = job_task_info_by_pid(job, pid))) {
			completed++;
			_log_task_exit(t->gtid, pid, status);
			t->exited  = true;
			t->estatus = status;
			job->envtp->procid = t->gtid + task_offset;
			job->envtp->localid = t->id;
			job->envtp->distribution = -1;
			job->envtp->batch_flag = job->batch;

			/*
			 * Modify copy of job's environment. Do not alter in
			 * place or concurrent searches of the environment can
			 * generate invalid memory references.
			 */
			job->envtp->env =
				env_array_copy((const char **) job->env);
			setup_env(job->envtp, false);
			tmp_env = job->env;
			job->env = job->envtp->env;
			env_array_free(tmp_env);

			setenvf(&job->env, "SLURM_SCRIPT_CONTEXT",
				"epilog_task");
			if (job->task_epilog) {
				_run_script_as_user("user task_epilog",
						    job->task_epilog,
						    job, 5, job->env);
			}
			if (conf->task_epilog) {
				char *my_epilog;
				slurm_mutex_lock(&conf->config_mutex);
				my_epilog = xstrdup(conf->task_epilog);
				slurm_mutex_unlock(&conf->config_mutex);
				_run_script_as_user("slurm task_epilog",
						    my_epilog,
						    job, -1, job->env);
				xfree(my_epilog);
			}

			if (spank_task_exit (job, t->id) < 0) {
				error ("Unable to spank task %d at exit",
				       t->id);
			}
			rc = task_g_post_term(job, t);
			if (rc == ENOMEM)
				job->oom_error = true;
		}

	} while ((pid > 0) && !waitflag);

	return completed;
}

static void
_wait_for_all_tasks(stepd_step_rec_t *job)
{
	int tasks_left = 0;
	int i;

	for (i = 0; i < job->node_tasks; i++) {
		if (job->task[i]->state < STEPD_STEP_TASK_COMPLETE) {
			tasks_left++;
		}
	}
	if (tasks_left < job->node_tasks)
		verbose("Only %d of %d requested tasks successfully launched",
			tasks_left, job->node_tasks);

	for (i = 0; i < tasks_left; ) {
		int rc;
		rc = _wait_for_any_task(job, true);
		if (rc != -1) {
			i += rc;
			if (i < tasks_left) {
				/* To limit the amount of traffic back
				 * we will sleep a bit to make sure we
				 * have most if not all the tasks
				 * completed before we return */
				usleep(100000);	/* 100 msec */
				rc = _wait_for_any_task(job, false);
				if (rc != -1)
					i += rc;
			}
		}

		if (i < tasks_left) {
			/* Send partial completion message only.
			 * The full completion message can only be sent
			 * after resetting CPU frequencies */
			while (stepd_send_pending_exit_msgs(job)) {;}
		}
	}
}

static void *_kill_thr(void *args)
{
	kill_thread_t *kt = ( kill_thread_t *) args;
	unsigned int pause = kt->secs;
	do {
		pause = sleep(pause);
	} while (pause > 0);
	pthread_cancel(kt->thread_id);
	xfree(kt);
	return NULL;
}

static void _delay_kill_thread(pthread_t thread_id, int secs)
{
	kill_thread_t *kt = xmalloc(sizeof(kill_thread_t));

	kt->thread_id = thread_id;
	kt->secs = secs;

	slurm_thread_create_detached(NULL, _kill_thr, kt);
}

/*
 * Wait for IO
 */
static void
_wait_for_io(stepd_step_rec_t *job)
{
	debug("Waiting for IO");
	io_close_all(job);

	/*
	 * Wait until IO thread exits or kill it after 300 seconds
	 */
	if (job->ioid) {
		_delay_kill_thread(job->ioid, 300);
		pthread_join(job->ioid, NULL);
	} else
		info("_wait_for_io: ioid==0");

	/* Close any files for stdout/stderr opened by the stepd */
	io_close_local_fds(job);

	return;
}


static char *
_make_batch_dir(stepd_step_rec_t *job)
{
	char path[MAXPATHLEN];

	if (job->stepid == NO_VAL)
		snprintf(path, sizeof(path), "%s/job%05u",
			 conf->spooldir, job->jobid);
	else {
		snprintf(path, sizeof(path), "%s/job%05u.%05u",
			 conf->spooldir, job->jobid, job->stepid);
	}

	if ((mkdir(path, 0750) < 0) && (errno != EEXIST)) {
		error("mkdir(%s): %m", path);
		if (errno == ENOSPC)
			stepd_drain_node("SlurmdSpoolDir is full");
		goto error;
	}

	if (chown(path, (uid_t) -1, (gid_t) job->gid) < 0) {
		error("chown(%s): %m", path);
		goto error;
	}

	if (chmod(path, 0750) < 0) {
		error("chmod(%s, 750): %m", path);
		goto error;
	}

	return xstrdup(path);

error:
	return NULL;
}

static char *_make_batch_script(batch_job_launch_msg_t *msg, char *path)
{
	int flags = O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC;
	int fd, length;
	char *script = NULL;
	char *output;

	if (msg->script == NULL) {
		error("%s: called with NULL script", __func__);
		return NULL;
	}

	/* note: should replace this with a length as part of msg */
	if ((length = strlen(msg->script)) < 1) {
		error("%s: called with empty script", __func__);
		return NULL;
	}

	xstrfmtcat(script, "%s/%s", path, "slurm_script");

	if ((fd = open(script, flags, S_IRWXU)) < 0) {
		error("couldn't open `%s': %m", script);
		goto error;
	}

	/*
	 * lseek() plus the following write() ensure the file is created
	 * as the appropriate length.
	 */
	if (lseek(fd, length - 1, SEEK_SET) == -1) {
		error("%s: lseek to %d failed on `%s`: %m",
		      __func__, length, script);
		close(fd);
		goto error;
	}

	if (write(fd, "", 1) == -1) {
		error("%s: write failed", __func__);
		if (errno == ENOSPC)
			stepd_drain_node("SlurmdSpoolDir is full");
		close(fd);
		goto error;
	}

	output = mmap(0, length, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (output == MAP_FAILED) {
		error("%s: mmap failed", __func__);
		close(fd);
		goto error;
	}

	(void) close(fd);

	memcpy(output, msg->script, length);

	munmap(output, length);

	if (chown(script, (uid_t) msg->uid, (gid_t) -1) < 0) {
		error("chown(%s): %m", path);
		goto error;
	}

	return script;

error:
	(void) unlink(script);
	xfree(script);
	return NULL;

}

extern int stepd_drain_node(char *reason)
{
	slurm_msg_t req_msg;
	update_node_msg_t update_node_msg;

	memset(&update_node_msg, 0, sizeof(update_node_msg));
	update_node_msg.node_names = conf->node_name;
	update_node_msg.node_state = NODE_STATE_DRAIN;
	update_node_msg.reason = reason;
	update_node_msg.reason_uid = getuid();
	update_node_msg.weight = NO_VAL;
	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = REQUEST_UPDATE_NODE;
	req_msg.data = &update_node_msg;

	if (slurm_send_only_controller_msg(&req_msg, working_cluster_rec) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static void
_send_launch_failure(launch_tasks_request_msg_t *msg, slurm_addr_t *cli, int rc,
		     uint16_t protocol_version)
{
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t resp;
	int nodeid;
	char *name = NULL;

	/*
	 * The extern step can get here if something goes wrong starting the
	 * step.  If this does happen we don't have to contact the srun since
	 * there isn't one, just return.
	 */
	if ((msg->job_step_id == SLURM_EXTERN_CONT) ||
	    !msg->resp_port || !msg->num_resp_port) {
		debug2("%s: The extern step has nothing to send a launch failure to",
		       __func__);
		return;
	}

#ifndef HAVE_FRONT_END
	nodeid = nodelist_find(msg->complete_nodelist, conf->node_name);
	name = xstrdup(conf->node_name);
#else
	nodeid = 0;
	name = xstrdup(msg->complete_nodelist);
#endif
	debug ("sending launch failure message: %s", slurm_strerror (rc));

	slurm_msg_t_init(&resp_msg);
	memcpy(&resp_msg.address, cli, sizeof(slurm_addr_t));
	slurm_set_addr(&resp_msg.address,
		       msg->resp_port[nodeid % msg->num_resp_port],
		       NULL);
	resp_msg.data = &resp;
	resp_msg.msg_type = RESPONSE_LAUNCH_TASKS;
	resp_msg.protocol_version = protocol_version;

	resp.job_id        = msg->job_id;
	resp.step_id       = msg->job_step_id;
	resp.node_name     = name;
	resp.return_code   = rc ? rc : -1;
	resp.count_of_pids = 0;

	if (_send_srun_resp_msg(&resp_msg, msg->nnodes) != SLURM_SUCCESS)
		error("%s: Failed to send RESPONSE_LAUNCH_TASKS: %m", __func__);
	xfree(name);
	return;
}

static void
_send_launch_resp(stepd_step_rec_t *job, int rc)
{
	int i;
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t resp;
	srun_info_t *srun = list_peek(job->sruns);

	if (job->batch)
		return;

	debug("Sending launch resp rc=%d", rc);

	slurm_msg_t_init(&resp_msg);
	resp_msg.address	= srun->resp_addr;
	resp_msg.protocol_version = srun->protocol_version;
	resp_msg.data		= &resp;
	resp_msg.msg_type	= RESPONSE_LAUNCH_TASKS;

	resp.job_id		= job->jobid;
	resp.step_id		= job->stepid;
	resp.node_name		= xstrdup(job->node_name);
	resp.return_code	= rc;
	resp.count_of_pids	= job->node_tasks;

	resp.local_pids = xmalloc(job->node_tasks * sizeof(*resp.local_pids));
	resp.task_ids = xmalloc(job->node_tasks * sizeof(*resp.task_ids));
	for (i = 0; i < job->node_tasks; i++) {
		resp.local_pids[i] = job->task[i]->pid;
		resp.task_ids[i] = job->task[i]->gtid;
	}

	if (_send_srun_resp_msg(&resp_msg, job->nnodes) != SLURM_SUCCESS)
		error("%s: Failed to send RESPONSE_LAUNCH_TASKS: %m", __func__);

	xfree(resp.local_pids);
	xfree(resp.task_ids);
	xfree(resp.node_name);
}


static int
_send_complete_batch_script_msg(stepd_step_rec_t *job, int err, int status)
{
	int		rc, i, msg_rc;
	slurm_msg_t	req_msg;
	complete_batch_script_msg_t req;
	bool msg_to_ctld = true;

	if (conf->msg_aggr_window_msgs > 1)
		msg_to_ctld = false;

	memset(&req, 0, sizeof(req));
	req.job_id	= job->jobid;
	if (job->oom_error)
		req.job_rc = SIG_OOM;
	else
		req.job_rc = status;
	req.jobacct	= job->jobacct;
	req.node_name	= job->node_name;
	req.slurm_rc	= err;
	req.user_id	= (uint32_t) job->uid;
	slurm_msg_t_init(&req_msg);
	req_msg.msg_type= REQUEST_COMPLETE_BATCH_SCRIPT;
	req_msg.data	= &req;

	info("sending REQUEST_COMPLETE_BATCH_SCRIPT, error:%d status:%d",
	     err, status);

	/* Note: these log messages don't go to slurmd.log from here */
	for (i = 0; i <= MAX_RETRY; i++) {
		if (msg_to_ctld) {
			msg_rc = slurm_send_recv_controller_rc_msg(&req_msg, &rc,
							working_cluster_rec);
		} else {
			/* Send msg to slurmd, which forwards to slurmctld and
			 * may get a new job to launch */
			if (i == 0) {
				slurm_set_addr_char(&req_msg.address,
						    conf->port, conf->hostname);
			}
			msg_rc = slurm_send_recv_rc_msg_only_one(&req_msg,
								 &rc, 0);
		}
		if (msg_rc == SLURM_SUCCESS)
			break;
		info("Retrying job complete RPC for %u.%u",
		     job->jobid, job->stepid);
		sleep(RETRY_DELAY);
	}
	if (i > MAX_RETRY) {
		error("Unable to send job complete message: %m");
		return SLURM_ERROR;
	}

	if ((rc == ESLURM_ALREADY_DONE) || (rc == ESLURM_INVALID_JOB_ID))
		rc = SLURM_SUCCESS;
	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}

/* If get_list is false make sure ps->gid_list is initialized before
 * hand to prevent xfree.
 */
static int
_drop_privileges(stepd_step_rec_t *job, bool do_setuid,
		 struct priv_state *ps, bool get_list)
{
	ps->saved_uid = getuid();
	ps->saved_gid = getgid();

	if (!getcwd (ps->saved_cwd, sizeof (ps->saved_cwd))) {
		error ("Unable to get current working directory: %m");
		strlcpy(ps->saved_cwd, "/tmp", sizeof(ps->saved_cwd));
	}

	ps->ngids = getgroups(0, NULL);
	if (ps->ngids == -1) {
		error("%s: getgroups(): %m", __func__);
		return -1;
	}
	if (get_list) {
		ps->gid_list = (gid_t *) xmalloc(ps->ngids * sizeof(gid_t));

		if (getgroups(ps->ngids, ps->gid_list) == -1) {
			error("%s: couldn't get %d groups: %m",
			      __func__, ps->ngids);
			xfree(ps->gid_list);
			return -1;
		}
	}

	/*
	 * No need to drop privileges if we're not running as root
	 */
	if (getuid() != (uid_t) 0)
		return SLURM_SUCCESS;

	if (setegid(job->gid) < 0) {
		error("setegid: %m");
		return -1;
	}

	if (setgroups(job->ngids, job->gids) < 0) {
		error("getgroups: %m");
		return -1;
	}

	if (do_setuid && seteuid(job->uid) < 0) {
		error("seteuid: %m");
		return -1;
	}

	return SLURM_SUCCESS;
}

static int
_reclaim_privileges(struct priv_state *ps)
{
	int rc = SLURM_SUCCESS;

	/*
	 * No need to reclaim privileges if our uid == job->uid
	 */
	if (geteuid() == ps->saved_uid)
		goto done;
	else if (seteuid(ps->saved_uid) < 0) {
		error("seteuid: %m");
		rc = -1;
	} else if (setegid(ps->saved_gid) < 0) {
		error("setegid: %m");
		rc = -1;
	} else if (setgroups(ps->ngids, ps->gid_list) < 0) {
		error("setgroups: %m");
		rc = -1;
	}

done:
	xfree(ps->gid_list);

	return rc;
}


static int
_slurmd_job_log_init(stepd_step_rec_t *job)
{
	char argv0[64];

	conf->log_opts.buffered = 1;

	/*
	 * Reset stderr logging to user requested level
	 * (Logfile and syslog levels remain the same)
	 *
	 * The maximum stderr log level is LOG_LEVEL_DEBUG3 because
	 * some higher level debug messages are generated in the
	 * stdio code, which would otherwise create more stderr traffic
	 * to srun and therefore more debug messages in an endless loop.
	 */
	conf->log_opts.stderr_level = LOG_LEVEL_ERROR + job->debug;
	if (conf->log_opts.stderr_level > LOG_LEVEL_DEBUG3)
		conf->log_opts.stderr_level = LOG_LEVEL_DEBUG3;

#if defined(MULTIPLE_SLURMD)
	snprintf(argv0, sizeof(argv0), "slurmstepd-%s", conf->node_name);
#else
	snprintf(argv0, sizeof(argv0), "slurmstepd");
#endif
	/*
	 * reinitialize log
	 */

	log_alter(conf->log_opts, 0, NULL);
	log_set_argv0(argv0);

	/*  Connect slurmd stderr to stderr of job, unless we are using
	 *   user_managed_io or a pty.
	 *
	 *  user_managed_io directly connects the client (e.g. poe) to the tasks
	 *   over a TCP connection, and we fully leave it up to the client
	 *   to manage the stream with no buffering on slurm's part.
	 *   We also promise that we will not insert any foreign data into
	 *   the stream, so here we need to avoid connecting slurmstepd's
	 *   STDERR_FILENO to the tasks's stderr.
	 *
	 *  When pty terminal emulation is used, the pts can potentially
	 *   cause IO to block, so we need to avoid connecting slurmstepd's
	 *   STDERR_FILENO to the task's pts on stderr to avoid hangs in
	 *   the slurmstepd.
	 */
	if (((job->flags & LAUNCH_USER_MANAGED_IO) == 0) &&
	    ((job->flags & LAUNCH_PTY) == 0) &&
	    (job->task != NULL)) {
		if (dup2(job->task[0]->stderr_fd, STDERR_FILENO) < 0) {
			error("job_log_init: dup2(stderr): %m");
			return ESLURMD_IO_ERROR;
		}
	}
	verbose("debug level = %d", conf->log_opts.stderr_level);
	return SLURM_SUCCESS;
}

/*
 * Set the priority of the job to be the same as the priority of
 * the process that launched the job on the submit node.
 * In support of the "PropagatePrioProcess" config keyword.
 */
static void _set_prio_process (stepd_step_rec_t *job)
{
	char *env_name = "SLURM_PRIO_PROCESS";
	char *env_val;
	int prio_daemon, prio_process;

	if (!(env_val = getenvp( job->env, env_name ))) {
		error( "Couldn't find %s in environment", env_name );
		prio_process = 0;
	} else {
		/* Users shouldn't get this in their environment */
		unsetenvp( job->env, env_name );
		prio_process = atoi( env_val );
	}

	if (conf->propagate_prio == PROP_PRIO_NICER) {
		prio_daemon = getpriority( PRIO_PROCESS, 0 );
		prio_process = MAX( prio_process, (prio_daemon + 1) );
	}

	if (setpriority( PRIO_PROCESS, 0, prio_process ))
		error( "setpriority(PRIO_PROCESS, %d): %m", prio_process );
	else {
		debug2( "_set_prio_process: setpriority %d succeeded",
			prio_process);
	}
}

static int
_become_user(stepd_step_rec_t *job, struct priv_state *ps)
{
	/*
	 * First reclaim the effective uid and gid
	 */
	if (geteuid() == ps->saved_uid)
		return SLURM_SUCCESS;

	if (seteuid(ps->saved_uid) < 0) {
		error("_become_user seteuid: %m");
		return SLURM_ERROR;
	}

	if (setegid(ps->saved_gid) < 0) {
		error("_become_user setegid: %m");
		return SLURM_ERROR;
	}

	/*
	 * Now drop real, effective, and saved uid/gid
	 */
	if (setregid(job->gid, job->gid) < 0) {
		error("setregid: %m");
		return SLURM_ERROR;
	}

	if (setreuid(job->uid, job->uid) < 0) {
		error("setreuid: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Check this user's access rights to a file
 * path IN: pathname of file to test
 * modes IN: desired access
 * uid IN: user ID to access the file
 * gid IN: group ID to access the file
 * RET true on success, false on failure
 */
static bool _access(const char *path, int modes, uid_t uid,
		    int ngids, gid_t *gids)
{
	struct stat buf;
	int f_mode, i;

	if (!gids)
		return false;

	if (stat(path, &buf) != 0)
		return false;

	if (buf.st_uid == uid)
		f_mode = (buf.st_mode >> 6) & 07;
	else {
		for (i=0; i < ngids; i++)
			if (buf.st_gid == gids[i])
				break;
		if (i < ngids)	/* one of the gids matched */
			f_mode = (buf.st_mode >> 3) & 07;
		else		/* uid and gid failed, test against all */
			f_mode = buf.st_mode & 07;
	}

	if ((f_mode & modes) == modes)
		return true;

	return false;
}

/*
 * Run a script as a specific user, with the specified uid, gid, and
 * extended groups.
 *
 * name IN: class of program (task prolog, task epilog, etc.),
 * path IN: pathname of program to run
 * job IN: slurd job structue, used to get uid, gid, and groups
 * max_wait IN: maximum time to wait in seconds, -1 for no limit
 * env IN: environment variables to use on exec, sets minimal environment
 *	if NULL
 *
 * RET 0 on success, -1 on failure.
 */
int
_run_script_as_user(const char *name, const char *path, stepd_step_rec_t *job,
		    int max_wait, char **env)
{
	int status, rc, opt;
	pid_t cpid;
	struct exec_wait_info *ei;

	xassert(env);
	if (path == NULL || path[0] == '\0')
		return 0;

	debug("[job %u] attempting to run %s [%s]", job->jobid, name, path);

	if (!_access(path, 5, job->uid, job->ngids, job->gids)) {
		error("Could not run %s [%s]: access denied", name, path);
		return -1;
	}

	if ((ei = _fork_child_with_wait_info(0)) == NULL) {
		error ("executing %s: fork: %m", name);
		return -1;
	}
	if ((cpid = _exec_wait_get_pid (ei)) == 0) {
		struct priv_state sprivs;
		char *argv[2];
		uint32_t jobid;

#ifdef HAVE_NATIVE_CRAY
		if (job->pack_jobid && (job->pack_jobid != NO_VAL))
			jobid = job->pack_jobid;
		else
			jobid = job->jobid;
#else
		jobid = job->jobid;
#endif
		/* container_g_join needs to be called in the
		   forked process part of the fork to avoid a race
		   condition where if this process makes a file or
		   detacts itself from a child before we add the pid
		   to the container in the parent of the fork.
		*/
		if ((jobid != 0) &&	/* Ignore system processes */
		    (container_g_join(jobid, job->uid) != SLURM_SUCCESS))
			error("container_g_join(%u): %m", job->jobid);

		argv[0] = (char *)xstrdup(path);
		argv[1] = NULL;

		sprivs.gid_list = NULL;	/* initialize to prevent xfree */
		if (_drop_privileges(job, true, &sprivs, false) < 0) {
			error("run_script_as_user _drop_privileges: %m");
			/* child process, should not return */
			exit(127);
		}

		if (_become_user(job, &sprivs) < 0) {
			error("run_script_as_user _become_user failed: %m");
			/* child process, should not return */
			exit(127);
		}

		if (chdir(job->cwd) == -1)
			error("run_script_as_user: couldn't "
			      "change working dir to %s: %m", job->cwd);
		setpgid(0, 0);
		/*
		 *  Wait for signal from parent
		 */
		_exec_wait_child_wait_for_parent (ei);

		while (1) {
			execve(path, argv, env);
			error("execve(%s): %m", path);
			if ((errno == ENFILE) || (errno == ENOMEM)) {
				/* System limit on open files or memory reached,
				 * retry after short delay */
				sleep(1);
			} else {
				break;
			}
		}
		exit(127);
	}

	if (exec_wait_signal_child (ei) < 0)
		error ("run_script_as_user: Failed to wakeup %s", name);
	_exec_wait_info_destroy (ei);

	if (max_wait < 0)
		opt = 0;
	else
		opt = WNOHANG;

	while (1) {
		rc = waitpid(cpid, &status, opt);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			error("waidpid: %m");
			status = 0;
			break;
		} else if (rc == 0) {
			sleep(1);
			if ((--max_wait) <= 0) {
				killpg(cpid, SIGKILL);
				opt = 0;
			}
		} else  {
			/* spawned process exited */
			break;
		}
	}
	/* Ensure that all child processes get killed, one last time */
	killpg(cpid, SIGKILL);

	return status;
}
