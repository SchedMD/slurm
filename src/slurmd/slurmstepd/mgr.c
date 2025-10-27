/*****************************************************************************\
 *  src/slurmd/slurmstepd/mgr.c - step manager functions for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
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
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#endif

#include "slurm/slurm_errno.h"

#include "src/common/cbuf.h"
#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/reverse_tree.h"
#include "src/common/spank.h"
#include "src/common/strlcpy.h"
#include "src/common/tres_frequency.h"
#include "src/common/util-net.h"
#include "src/common/x11_util.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/acct_gather_profile.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/gpu.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/namespace.h"
#include "src/interfaces/prep.h"
#include "src/interfaces/proctrack.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/task.h"

#include "src/conmgr/conmgr.h"

#include "src/slurmd/common/fname.h"
#include "src/slurmd/common/privileges.h"
#include "src/slurmd/common/set_oomadj.h"
#include "src/slurmd/common/slurmd_cgroup.h"
#include "src/slurmd/common/slurmd_common.h"
#include "src/slurmd/common/xcpuinfo.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/mgr.h"
#include "src/slurmd/slurmstepd/pam_ses.h"
#include "src/slurmd/slurmstepd/pdebug.h"
#include "src/slurmd/slurmstepd/req.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/step_terminate_monitor.h"
#include "src/slurmd/slurmstepd/task.h"
#include "src/slurmd/slurmstepd/ulimits.h"
#include "src/slurmd/slurmstepd/x11_forwarding.h"

#define RETRY_DELAY 15		/* retry every 15 seconds */

step_complete_t step_complete = {
	PTHREAD_COND_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	-1,
	-1,
	-1,
	(char *)NULL,
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
 * step manager related prototypes
 */
static void _send_launch_failure(launch_tasks_request_msg_t *,
				 slurm_addr_t *, int, uint16_t);
static int _fork_all_tasks(bool *io_initialized);
static int _become_user(struct priv_state *ps);
static void _set_prio_process(void);
static int _setup_normal_io(void);
static void _send_launch_resp(int rc);
static int _slurmd_job_log_init(void);
static void _wait_for_io(void);
static int _send_exit_msg(uint32_t *tid, int n, int status);
static void _wait_for_all_tasks(void);
static int _wait_for_any_task(bool waitflag);

static void _random_sleep(void);
static int _run_script_as_user(const char *name, const char *path, int max_wait,
			       char **env);

/*
 * Batch step management prototypes:
 */
static int _make_batch_dir(void);
static int _make_batch_script(batch_job_launch_msg_t *msg);
static int _send_complete_batch_script_msg(int err, int status);

/*
 * Launch an step step on the current node
 */
extern int mgr_launch_tasks_setup(launch_tasks_request_msg_t *msg,
				  slurm_addr_t *cli, uint16_t protocol_version)
{
	if (stepd_step_rec_create(msg, protocol_version)) {
		_send_launch_failure(msg, cli, errno, protocol_version);
		return SLURM_ERROR;
	}

	step->envtp->cli = cli;
	step->accel_bind_type = msg->accel_bind_type;
	step->tres_bind = xstrdup(msg->tres_bind);
	step->tres_freq = xstrdup(msg->tres_freq);
	step->stepmgr = xstrdup(msg->stepmgr);

	return SLURM_SUCCESS;
}

inline static int
_send_srun_resp_msg(slurm_msg_t *resp_msg, uint32_t nnodes)
{
	int rc = SLURM_ERROR, retry = 1, max_retry = 0;
	unsigned long delay = 100000;

	/* NOTE: Wait until suspended job step is resumed or the RPC
	 * authentication credential from Munge may expire by the time
	 * it is resumed */
	wait_for_resumed(resp_msg->msg_type);
	while (1) {
		if (resp_msg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			int msg_rc = 0;
			msg_rc = slurm_send_recv_rc_msg_only_one(resp_msg,
								 &rc, 0);
			/* Both must be zero for a successful transmission. */
			if (!msg_rc && !rc)
				break;
		} else {
			rc = SLURM_ERROR;
			break;
		}

		if (!max_retry)
			max_retry = (nnodes / 1024) + 5;

		debug("%s: %d/%d failed to send msg type %s: %m",
		      __func__, retry, max_retry,
		      rpc_num2string(resp_msg->msg_type));

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
	int gpumem_pos = -1, gpuutil_pos = -1;

	if (from->pid) {
		gpu_get_tres_pos(&gpumem_pos, &gpuutil_pos);

		/*
		 * Here to make any sense for some variables we need to move the
		 * Max to the total (i.e. Mem, VMem, gpumem, gpuutil) since the
		 * total might be incorrect data, this way the total/ave will be
		 * of the Max values.
		 */
		from->tres_usage_in_tot[TRES_ARRAY_MEM] =
			from->tres_usage_in_max[TRES_ARRAY_MEM];
		from->tres_usage_in_tot[TRES_ARRAY_VMEM] =
			from->tres_usage_in_max[TRES_ARRAY_VMEM];
		if (gpumem_pos != -1)
			from->tres_usage_in_tot[gpumem_pos] =
				from->tres_usage_in_max[gpumem_pos];
		if (gpuutil_pos != -1)
			from->tres_usage_in_tot[gpuutil_pos] =
				from->tres_usage_in_max[gpuutil_pos];
	}

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
static uint32_t _get_exit_code(void)
{
	uint32_t i;
	uint32_t step_rc = NO_VAL;

	/* We are always killing/cancelling the extern_step so don't
	 * report that.
	 */
	if (step->step_id.step_id == SLURM_EXTERN_CONT)
		return 0;

	for (i = 0; i < step->node_tasks; i++) {
		/* if this task was killed by cmd, ignore its
		 * return status as it only reflects the fact
		 * that we killed it
		 */
		if (step->task[i]->killed_by_cmd) {
			debug("get_exit_code task %u killed by cmd", i);
			continue;
		}
		/* if this task called PMI_Abort or PMI2_Abort,
		 * then we let it define the exit status
		 */
		if (step->task[i]->aborted) {
			step_rc = step->task[i]->estatus;
			debug("get_exit_code task %u called abort", i);
			break;
		}
		/* If signaled we need to cycle thru all the
		 * tasks in case one of them called abort
		 */
		if (WIFSIGNALED(step->task[i]->estatus)) {
			info("get_exit_code task %u died by signal: %d",
			     i, WTERMSIG(step->task[i]->estatus));
			step_rc = step->task[i]->estatus;
			break;
		}
		if ((step->task[i]->estatus & 0xff) == SIG_OOM) {
			step_rc = step->task[i]->estatus;
		} else if ((step_rc  & 0xff) != SIG_OOM) {
			step_rc = MAX(step_complete.step_rc,
				      step->task[i]->estatus);
		}
	}
	/* If we killed all the tasks by cmd give at least one return
	   code. */
	if (step_rc == NO_VAL && step->task[0])
		step_rc = step->task[0]->estatus;

	return step_rc;
}

static char *_batch_script_path(void)
{
	return xstrdup_printf("%s/%s", step->batchdir, "slurm_script");
}

/*
 * Send batch exit code to slurmctld. Non-zero rc will DRAIN the node.
 */
extern void batch_finish(int rc)
{
	char *script = _batch_script_path();
	step_complete.step_rc = _get_exit_code();

	if (unlink(script) < 0)
		error("unlink(%s): %m", script);
	xfree(script);

	if (step->aborted) {
		if (step->step_id.step_id != SLURM_BATCH_SCRIPT)
			info("%ps abort completed", &step->step_id);
		else
			info("job %u abort completed", step->step_id.job_id);
	} else if (step->step_id.step_id == SLURM_BATCH_SCRIPT) {
		verbose("job %u completed with slurm_rc = %d, job_rc = %d",
			step->step_id.job_id, rc, step_complete.step_rc);

		/* if launch failed, make sure to tag step as failed too */
		if (!step_complete.step_rc && rc)
			step_complete.step_rc = rc;

		_send_complete_batch_script_msg(rc, step_complete.step_rc);
	} else {
		stepd_wait_for_children_slurmstepd();
		verbose("%ps completed with slurm_rc = %d, job_rc = %d",
			&step->step_id, rc, step_complete.step_rc);
		stepd_send_step_complete_msgs();
	}

	/* Do not purge directory until slurmctld is notified of batch job
	 * completion to avoid race condition with slurmd registering missing
	 * batch job. */
	if (step->batchdir && (rmdir(step->batchdir) < 0))
		error("rmdir(%s): %m",  step->batchdir);
	xfree(step->batchdir);
}

/*
 * Launch a batch job script on the current node
 */
extern int mgr_launch_batch_job_setup(batch_job_launch_msg_t *msg,
				      slurm_addr_t *cli)
{
	if (batch_stepd_step_rec_create(msg)) {
		error("batch_stepd_step_rec_create() failed for %pI on %s: %s",
		      &msg->step_id, conf->hostname, slurm_strerror(errno));
		return SLURM_ERROR;
	}

	if (_make_batch_dir())
		goto cleanup;

	xfree(step->argv[0]);

	if (_make_batch_script(msg))
		goto cleanup;

	env_array_for_batch_job(&step->env, msg, conf->node_name);

	return SLURM_SUCCESS;

cleanup:
	error("batch script setup failed for %pI on %s: %s",
	      &msg->step_id, conf->hostname, slurm_strerror(errno));

	if (step->aborted)
		verbose("%pI abort complete", &step->step_id);

	/* Do not purge directory until slurmctld is notified of batch job
	 * completion to avoid race condition with slurmd registering missing
	 * batch job. */
	if (step->batchdir && (rmdir(step->batchdir) < 0))
		error("rmdir(%s): %m",  step->batchdir);
	xfree(step->batchdir);

	return SLURM_ERROR;
}

static int _setup_normal_io(void)
{
	int rc = 0, ii = 0;
	struct priv_state sprivs;

	debug2("Entering _setup_normal_io");

	/*
	 * Temporarily drop permissions, initialize task stdio file
	 * descriptors (which may be connected to files), then
	 * reclaim privileges.
	 */
	if (drop_privileges(step, true, &sprivs, true) < 0)
		return ESLURMD_SET_UID_OR_GID_ERROR;

	if (io_init_tasks_stdio() != SLURM_SUCCESS) {
		rc = ESLURMD_IO_ERROR;
		goto claim;
	}

	/*
	 * MUST create the initial client object before starting
	 * the IO thread, or we risk losing stdout/err traffic.
	 */
	if (!step->batch) {
		srun_info_t *srun = list_peek(step->sruns);

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
		if (step->flags & LAUNCH_LABEL_IO) {
			slurmd_filename_pattern_t outpattern, errpattern;
			bool same = false;
			int file_flags;

			io_find_filename_pattern(&outpattern, &errpattern,
						 &same);
			file_flags = io_get_file_flags();

			/* Make eio objects to write from the slurmstepd */
			if (outpattern == SLURMD_ALL_UNIQUE) {
				/* Open a separate file per task */
				for (ii = 0; ii < step->node_tasks; ii++) {
					rc = io_create_local_client(
						step->task[ii]->ofname,
						file_flags, 1,
						step->task[ii]->id,
						same ? step->task[ii]->id : -2);
					if (rc != SLURM_SUCCESS) {
						error("Could not open output file %s: %m",
						      step->task[ii]->ofname);
						rc = ESLURMD_IO_ERROR;
						goto claim;
					}
				}
				srun_stdout_tasks = -2;
				if (same)
					srun_stderr_tasks = -2;
			} else if (outpattern == SLURMD_ALL_SAME) {
				/* Open a file for all tasks */
				rc = io_create_local_client(step->task[0]
								    ->ofname,
							    file_flags, 1, -1,
							    same ? -1 : -2);
				if (rc != SLURM_SUCCESS) {
					error("Could not open output file %s: %m",
					      step->task[0]->ofname);
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
					     ii < step->node_tasks; ii++) {
						rc = io_create_local_client(
							step->task[ii]->efname,
							file_flags, 1, -2,
							step->task[ii]->id);
						if (rc != SLURM_SUCCESS) {
							error("Could not open error file %s: %m",
							      step->task[ii]->
							      efname);
							rc = ESLURMD_IO_ERROR;
							goto claim;
						}
					}
					srun_stderr_tasks = -2;
				} else if (errpattern == SLURMD_ALL_SAME) {
					/* Open a file for all tasks */
					rc = io_create_local_client(
						step->task[0]->efname,
						file_flags, 1, -2, -1);
					if (rc != SLURM_SUCCESS) {
						error("Could not open error file %s: %m",
						      step->task[0]->efname);
						rc = ESLURMD_IO_ERROR;
						goto claim;
					}
					srun_stderr_tasks = -2;
				}
			}
		}

		if (io_initial_client_connect(srun, srun_stdout_tasks,
					      srun_stderr_tasks) < 0) {
			rc = ESLURMD_IO_ERROR;
			goto claim;
		}
	}

claim:
	if (reclaim_privileges(&sprivs) < 0) {
		error("sete{u/g}id(%lu/%lu): %m",
		      (u_long) sprivs.saved_uid, (u_long) sprivs.saved_gid);
	}

	if (!rc && !step->batch)
		io_thread_start();

	debug2("Leaving  _setup_normal_io");
	return rc;
}

static void _random_sleep(void)
{
	long int delay = 0;
	long int max = (slurm_conf.tcp_timeout * step->nnodes);

	max = MIN(max, 5000);
	srand48((long int) (step->step_id.job_id + step->nodeid));

	delay = lrand48() % ( max + 1 );
	debug3("delaying %ldms", delay);
	if (poll(NULL, 0, delay) == -1)
		error("%s: poll(): %m", __func__);
}

/*
 * Send task exit message for n tasks. tid is the list of _global_
 * task ids that have exited
 */
static int _send_exit_msg(uint32_t *tid, int n, int status)
{
	slurm_msg_t     resp;
	task_exit_msg_t msg;
	list_itr_t *i = NULL;
	srun_info_t    *srun    = NULL;

	debug3("%s: sending task exit msg for %d tasks (oom:%s exit_status:%s",
	       __func__, n, (step->oom_error ? "true" : "false"),
	       slurm_strerror(status));

	memset(&msg, 0, sizeof(msg));
	msg.task_id_list	= tid;
	msg.num_tasks		= n;
	if (step->oom_error)
		msg.return_code = SIG_OOM;
	else if (WIFSIGNALED(status) && (step->flags & LAUNCH_NO_SIG_FAIL))
		msg.return_code = SLURM_SUCCESS;
	else
		msg.return_code = status;

	memcpy(&msg.step_id, &step->step_id, sizeof(msg.step_id));

	slurm_msg_t_init(&resp);
	resp.data		= &msg;
	resp.msg_type		= MESSAGE_TASK_EXIT;

	/*
	 *  Hack for TCP timeouts on exit of large, synchronized step
	 *  termination. Delay a random amount if step->nnodes > 500
	 */
	if (step->nnodes > 500)
		_random_sleep();

	/*
	 * Notify each srun and sattach.
	 * No message for poe or batch steps
	 */
	i = list_iterator_create(step->sruns);
	while ((srun = list_next(i))) {
		resp.address = srun->resp_addr;
		if (slurm_addr_is_unspec(&resp.address))
			continue;	/* no srun or sattach here */

		/* This should always be set to something else we have a bug. */
		xassert(srun->protocol_version);
		resp.protocol_version = srun->protocol_version;
		resp.tls_cert = xstrdup(srun->tls_cert);

		slurm_msg_set_r_uid(&resp, srun->uid);

		if (_send_srun_resp_msg(&resp, step->nnodes) != SLURM_SUCCESS)
			error("Failed to send MESSAGE_TASK_EXIT: %m");
	}
	list_iterator_destroy(i);

	return SLURM_SUCCESS;
}

extern void stepd_wait_for_children_slurmstepd(void)
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

	step_complete.step_rc = _get_exit_code();
	step_complete.wait_children = false;

	slurm_mutex_unlock(&step_complete.lock);
}

/*
 * Send a single step completion message, which represents a single range
 * of complete job step nodes.
 */
/* caller is holding step_complete.lock */
static void _one_step_complete_msg(int first, int last)
{
	slurm_msg_t req;
	step_complete_msg_t msg;
	int rc = -1;
	int retcode;
	int i;
	static bool acct_sent = false;

	if (step->batch) {	/* Nested batch step anomalies */
		if (first == -1)
			first = 0;
		if (last == -1)
			last = 0;
	}
	memset(&msg, 0, sizeof(msg));

	memcpy(&msg.step_id, &step->step_id, sizeof(msg.step_id));

	msg.range_first = first;
	msg.range_last = last;
	if (step->oom_error)
		msg.step_rc = SIG_OOM;
	else
		msg.step_rc = step_complete.step_rc;
	msg.jobacct = jobacctinfo_create(NULL);
	/************* acct stuff ********************/
	if (!acct_sent) {
		/*
		 * No need to call _local_jobaccinfo_aggregate, step->jobacct
		 * already has the modified total for this node in the step.
		 */
		jobacctinfo_aggregate(step_complete.jobacct, step->jobacct);
		jobacctinfo_getinfo(step_complete.jobacct,
				    JOBACCT_DATA_TOTAL, msg.jobacct,
				    SLURM_PROTOCOL_VERSION);
		acct_sent = true;
	}
	/*********************************************/

	debug2("%s: ranks=%d-%d parent_rank=%d step_rc[0x%x]=%s",
	       __func__, first, last, step_complete.parent_rank, msg.step_rc,
	       slurm_strerror(msg.step_rc));

	slurm_msg_t_init(&req);
	slurm_msg_set_r_uid(&req, slurm_conf.slurmd_user_id);
	req.msg_type = REQUEST_STEP_COMPLETE;
	req.data = &msg;

	/* Do NOT change this check to "step_complete.rank != 0", because
	 * there are odd situations where SlurmUser or root could
	 * craft a launch without a valid credential, and no tree information
	 * can be built with out the hostlist from the credential.
	 */
	if (step_complete.parent_rank != -1) {
		debug3("Rank %d sending complete to rank %d(%s), range %d to %d",
		       step_complete.rank, step_complete.parent_rank,
		       step_complete.parent_name, first, last);
		/* On error, pause then try sending to parent again.
		 * The parent slurmstepd may just not have started yet, because
		 * of the way that the launch message forwarding works.
		 */
		if (slurm_conf_get_addr(step_complete.parent_name, &req.address,
					0)) {
			i = REVERSE_TREE_PARENT_RETRY;
			error("%s: failed getting address for parent NodeName %s (parent rank %d)",
			      __func__, step_complete.parent_name,
			      step_complete.parent_rank);
		} else
			i = 0;

		for (; i < REVERSE_TREE_PARENT_RETRY; i++) {
			if (i)
				sleep(1);
			retcode = slurm_send_recv_rc_msg_only_one(&req, &rc, 0);
			if ((retcode == 0) && (rc == 0))
				goto finished;
		}
		/*
		 * On error AGAIN, send to the slurmctld instead.
		 * This is useful if parent_rank gave up waiting for us
		 * on stepd_wait_for_children_slurmstepd.
		 * If it's just busy handling our prev messages we'll need
		 * to handle duplicated messages in both the parent and
		 * slurmctld.
		 */
		debug3("Rank %d sending complete to slurmctld instead, range "
		       "%d to %d", step_complete.rank, first, last);
	}  else {
		/* this is the base of the tree, its parent is slurmctld */
		debug3("Rank %d sending complete to slurmctld, range %d to %d",
		       step_complete.rank, first, last);
	}

	if (step->stepmgr) {
		slurm_msg_t resp_msg;

		slurm_msg_t_init(&resp_msg);

		slurm_conf_get_addr(step->stepmgr, &req.address,
				    req.flags);
		slurm_msg_set_r_uid(&req, slurm_conf.slurmd_user_id);
		msg.send_to_stepmgr = true;
		debug3("sending complete to step_ctld host:%s",
		       step->stepmgr);
		if (slurm_send_recv_node_msg(&req, &resp_msg, 0))
			return;
		goto finished;
	}

	/* Retry step complete RPC send to slurmctld indefinitely.
	 * Prevent orphan job step if slurmctld is down */
	i = 1;
	while (slurm_send_recv_controller_rc_msg(&req, &rc,
						 working_cluster_rec) < 0) {
		if (i++ == 1) {
			error("Rank %d failed sending step completion message directly to slurmctld, retrying",
			      step_complete.rank);
		}
		sleep(60);
	}
	if (i > 1) {
		info("Rank %d sent step completion message directly to slurmctld",
		     step_complete.rank);
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
extern void stepd_send_step_complete_msgs(void)
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
		_one_step_complete_msg(step_complete.rank, step_complete.rank);
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

		_one_step_complete_msg((first + step_complete.rank + 1),
				       (last + step_complete.rank + 1));
		start = last + 1;
	}

	if (!sent_own_comp_msg) {
		_one_step_complete_msg(step_complete.rank, step_complete.rank);
	}

	slurm_mutex_unlock(&step_complete.lock);
}

extern void set_job_state(slurmstepd_state_t new_state)
{
	slurm_mutex_lock(&step->state_mutex);
	step->state = new_state;
	slurm_cond_signal(&step->state_cond);
	slurm_mutex_unlock(&step->state_mutex);
}

/*
 * Run SPANK functions within the job container.
 * WARNING: This is running as a separate process, but sharing the parent's
 * memory space. Be careful not to leak memory, or free resources that the
 * parent needs to continue processing. Only use _exit() here, otherwise
 * any plugins that registered their own fini() hooks will wreck the parent.
 */
#if defined(__linux__)
static int _spank_user_child(void *ignored)
{
	struct priv_state sprivs;
	int rc = 0;

	if (namespace_g_join(&step->step_id, step->uid, false)) {
		error("namespace_g_join(%u): %m", step->step_id.job_id);
		_exit(-1);
	}

	if (drop_privileges(step, true, &sprivs, true) < 0) {
		error("drop_privileges: %m");
		_exit(-1);
	}

	if (spank_user(step))
		rc = 1;

	/*
	 * This is taken from the end of reclaim_privileges(). The child does
	 * not need to reclaim here since we simply exit, so instead clean up
	 * the structure and the lock so that the parent can continue to
	 * operate.
	 */
	xfree(sprivs.gid_list);
	auth_setuid_unlock();

	_exit(rc);
}

static int _spank_task_post_fork_child(void *arg)
{
	int id = *(int *) arg;

	if (namespace_g_join(&step->step_id, step->uid, false)) {
		error("namespace_g_join(%u): %m", step->step_id.job_id);
		_exit(-1);
	}

	if (spank_task_post_fork(step, id))
		_exit(1);

	_exit(0);
}

static int _spank_task_exit_child(void *arg)
{
	int id = *(int *) arg;

	if (namespace_g_join(&step->step_id, step->uid, false)) {
		error("namespace_g_join(%u): %m", step->step_id.job_id);
		_exit(-1);
	}

	if (spank_task_exit(step, id))
		_exit(1);

	_exit(0);
}
#endif

static int _run_spank_func(step_fn_t spank_func, int id,
			   struct priv_state *sprivs)
{
	int rc = SLURM_SUCCESS;

#if defined(__linux__)
	if (slurm_conf.conf_flags & CONF_FLAG_CONTAIN_SPANK) {
		pid_t pid = -1;
		int flags = CLONE_VM | SIGCHLD;
		char *stack = NULL;
		int status = 0;
		int *arg = NULL;

		/*
		 * To enter the container, the process cannot share CLONE_FS
		 * with another process. However, the spank plugins require
		 * access to global memory that cannot be easily be shipped to
		 * another process.
		 *
		 * clone() + CLONE_VM allows the new process to have distinct
		 * filesystem attribites, but share memory space. By allowing
		 * the current process to continue executing, shared locks can
		 * be released allowing the new process to operate normally.
		 */
		if ((spank_func == SPANK_STEP_TASK_EXIT) &&
		    spank_has_task_exit()) {
			arg = xmalloc(sizeof(*arg));
			*arg = id;
			stack = xmalloc(STACK_SIZE);
			pid = clone(_spank_task_exit_child, stack + STACK_SIZE,
				    flags, arg);
		} else if ((spank_func == SPANK_STEP_TASK_POST_FORK) &&
			   spank_has_task_post_fork()) {
			arg = xmalloc(sizeof(*arg));
			*arg = id;
			stack = xmalloc(STACK_SIZE);
			pid = clone(_spank_task_post_fork_child,
				    stack + STACK_SIZE, flags, arg);
		} else if ((spank_func == SPANK_STEP_USER_INIT) &&
			   spank_has_user_init()) {
			/*
			 * spank_user_init() runs as the user, but setns()
			 * requires CAP_SYS_ADMIN. Reclaim privileges here so
			 * setns() will function.
			 */
			if (reclaim_privileges(sprivs) < 0) {
				error("Unable to reclaim privileges");
				rc = 1;
				goto fail;
			}

			stack = xmalloc(STACK_SIZE);
			pid = clone(_spank_user_child, stack + STACK_SIZE,
				    flags, NULL);
		} else {
			/* no action required */
			return rc;
		}

		if (pid == -1) {
			error("clone failed before spank call: %m");
			rc = SLURM_ERROR;
		} else {
			waitpid(pid, &status, 0);
			if (WEXITSTATUS(status))
				rc = SLURM_ERROR;
		}

		if (spank_func == SPANK_STEP_USER_INIT) {
			if (drop_privileges(step, true, sprivs, true) < 0) {
				error("drop_privileges: %m");
				rc = 2;
			}
		}

fail:
		xfree(arg);
		xfree(stack);
		return rc;
	}
#endif
	/*
	 * Default case is to run these spank functions normally. To allow a
	 * different exit path, set rc = SLURM_ERROR if the plugstack call
	 * fails.
	 */
	if ((spank_func == SPANK_STEP_TASK_EXIT) &&
	    (spank_task_exit(step, id))) {
		rc = SLURM_ERROR;
	} else if ((spank_func == SPANK_STEP_TASK_POST_FORK) &&
		   (spank_task_post_fork(step, id))) {
		rc = SLURM_ERROR;
	} else if ((spank_func == SPANK_STEP_USER_INIT) &&
		   (spank_user(step))) {
		rc = SLURM_ERROR;
	}

	return rc;
}

static bool _need_join_container()
{
	/*
	 * To avoid potential problems with namespace plugins and
	 * home_xauthority, don't join the namespace to create the xauthority
	 * file when it is set.
	 */
	if ((slurm_conf.namespace_plugin) &&
	    (!xstrcasestr(slurm_conf.x11_params, "home_xauthority"))) {
		return true;
	}

	return false;
}

static void _shutdown_x11_forward(void)
{
	struct priv_state sprivs = { 0 };

	if (drop_privileges(step, true, &sprivs, false) < 0) {
		error("%s: Unable to drop privileges", __func__);
		return;
	}

	if (shutdown_x11_forward() != SLURM_SUCCESS)
		error("%s: x11 forward shutdown failed", __func__);

	if (reclaim_privileges(&sprivs) < 0)
		error("%s: Unable to reclaim privileges", __func__);
}

static void _x11_signal_handler(conmgr_callback_args_t conmgr_args, void *ignored)
{
	static bool run_once = false;
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	bool bail = false;
	pid_t cpid, pid;

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED) {
		debug4("%s: cancelled", __func__);
		return;
	}

	/*
	 * Protect against race of SIGTERM and step shutdown causing this
	 * function to run more than once.
	 */
	slurm_mutex_lock(&mutex);
	if (run_once)
		bail = true;
	run_once = true;
	slurm_mutex_unlock(&mutex);

	if (bail) {
		debug4("%s: Already run. bailing.", __func__);
		return;
	}

	debug("Terminate signal (SIGTERM) received");

	if (!_need_join_container()) {
		_shutdown_x11_forward();
		return;
	}
	if ((cpid = fork()) == 0) {
		if (namespace_g_join(&step->step_id, step->uid, false) !=
		    SLURM_SUCCESS) {
			error("%s: cannot join container",
			      __func__);
			_exit(1);
		}
		_shutdown_x11_forward();
		_exit(0);
	} else if (cpid < 0) {
		error("%s: fork: %m", __func__);
	} else {
		int status;

		pid = waitpid(cpid, &status, 0);
		if (pid < 0)
			error("%s: waitpid failed: %m",
			      __func__);
		else if (!WIFEXITED(status))
			error("%s: child terminated abnormally",
			      __func__);
		else if (WEXITSTATUS(status))
			error("%s: child returned non-zero",
			      __func__);
	}
}

static int _set_xauthority(void)
{
	struct priv_state sprivs = { 0 };
	int rc = SLURM_SUCCESS;

	if (drop_privileges(step, true, &sprivs, false) < 0) {
		error("%s: Unable to drop privileges before xauth", __func__);
		return SLURM_ERROR;
	}

	if (!xstrcasestr(slurm_conf.x11_params, "home_xauthority")) {
		int fd;
		/* protect against weak file permissions in old glibc */
		umask(0077);
		if ((fd = mkstemp(step->x11_xauthority)) == -1) {
			error("%s: failed to create temporary XAUTHORITY file: %m",
			      __func__);
			rc = SLURM_ERROR;
			goto endit;
		}
		close(fd);
	}

	if (x11_set_xauth(step->x11_xauthority, step->x11_magic_cookie,
			  step->x11_display)) {
		error("%s: failed to run xauth", __func__);
		rc =  SLURM_ERROR;
	}
endit:
	if (reclaim_privileges(&sprivs) < 0) {
		error("%s: Unable to reclaim privileges after xauth", __func__);
		return SLURM_ERROR;
	}

	return rc;
}

static int _run_prolog_epilog(bool is_epilog)
{
	int rc = SLURM_SUCCESS;
	job_env_t job_env;
	list_t *tmp_list;

	memset(&job_env, 0, sizeof(job_env));

	tmp_list = gres_g_prep_build_env(step->job_gres_list, step->node_list);
	/*
	 * When ran in the stepd we only have gres information from the cred
	 * about this node so all index's should be 0
	 */
	gres_g_prep_set_env(&job_env.gres_job_env, tmp_list, 0);
	FREE_NULL_LIST(tmp_list);

	job_env.step_id = step->step_id;
	job_env.node_list = step->node_list;
	job_env.het_job_id = step->het_job_id;
	job_env.partition = step->msg->cred->arg->job_partition;
	job_env.spank_job_env = step->msg->spank_job_env;
	job_env.spank_job_env_size = step->msg->spank_job_env_size;
	job_env.work_dir = step->cwd;
	job_env.uid = step->uid;
	job_env.gid = step->gid;

	if (!is_epilog)
		rc = run_prolog(&job_env, step->msg->cred);
	else
		rc = run_epilog(&job_env, step->msg->cred);

	if (job_env.gres_job_env) {
		for (int i = 0; job_env.gres_job_env[i]; i++)
			xfree(job_env.gres_job_env[i]);
		xfree(job_env.gres_job_env);
	}

	if (rc) {
		int term_sig = 0, exit_status = 0;
		if (WIFSIGNALED(rc))
			term_sig = WTERMSIG(rc);
		else if (WIFEXITED(rc))
			exit_status = WEXITSTATUS(rc);
		error("[job %u] %s failed status=%d:%d", step->step_id.job_id,
		      is_epilog ? "epilog" : "prolog", exit_status, term_sig);
		rc = is_epilog ? ESLURMD_EPILOG_FAILED : ESLURMD_PROLOG_FAILED;
	}

	return rc;
}

static void _setup_x11_child(int to_parent[2])
{
	uint32_t len = 0;

	if (namespace_g_join(&step->step_id, step->uid, false) !=
	    SLURM_SUCCESS) {
		safe_write(to_parent[1], &len, sizeof(len));
		_exit(1);
	}

	if (_set_xauthority() != SLURM_SUCCESS) {
		safe_write(to_parent[1], &len, sizeof(len));
		_exit(1);
	}

	len = strlen(step->x11_xauthority);
	safe_write(to_parent[1], &len, sizeof(len));
	safe_write(to_parent[1], step->x11_xauthority, len);

	_exit(0);

rwfail:
	error("%s: failed to write to parent: %m", __func__);
	_exit(1);
}

static int _setup_x11_parent(int to_parent[2], pid_t pid, char **tmp)
{
	uint32_t len = 0;
	int status = 0;

	safe_read(to_parent[0], &len, sizeof(len));

	if (len) {
		*tmp = xcalloc(len, sizeof(char));
		safe_read(to_parent[0], *tmp, len);
	}

	if ((waitpid(pid, &status, 0) != pid) || WEXITSTATUS(status)) {
		error("%s: Xauthority setup failed", __func__);
		xfree(*tmp);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;

rwfail:
	error("%s: failed to read from child: %m", __func__);
	xfree(*tmp);
	waitpid(pid, &status, 0);
	debug2("%s: status from child %d", __func__, status);

	return SLURM_ERROR;
}

static int _spawn_job_container(void)
{
	jobacctinfo_t *jobacct = NULL;
	struct rusage rusage;
	jobacct_id_t jobacct_id;
	int rc = SLURM_SUCCESS;
	uint32_t jobid = step->step_id.job_id;

	if (namespace_g_stepd_create(step)) {
		error("%s: namespace_g_stepd_create(%u): %m", __func__, jobid);
		return SLURM_ERROR;
	}

	debug2("%s: Before call to spank_init()", __func__);
	if ((rc = spank_init(step))) {
		error("%s: Plugin stack initialization failed.", __func__);
		/* let the slurmd know we actually are done with the setup */
		close_slurmd_conn(rc);
		return rc;
	}
	debug2("%s: After call to spank_init()", __func__);

	if (task_g_pre_setuid(step)) {
		error("%s: Failed to invoke task plugins: one of "
		      "task_p_pre_setuid functions returned error", __func__);
		return SLURM_ERROR;
	}

	acct_gather_profile_g_task_start(0);

	if (step->x11) {
		struct priv_state sprivs = { 0 };

		if (drop_privileges(step, true, &sprivs, false) < 0) {
			error ("Unable to drop privileges");
			return SLURM_ERROR;
		}
		if (setup_x11_forward() != SLURM_SUCCESS) {
			/* ssh forwarding setup failed */
			error("x11 port forwarding setup failed");
			_exit(127);
		}
		if (reclaim_privileges(&sprivs) < 0) {
			error ("Unable to reclaim privileges");
			return SLURM_ERROR;
		}

		conmgr_add_work_signal(SIGTERM, _x11_signal_handler, NULL);

		/*
		 * When using job_container/tmpfs we need to get into
		 * the correct namespace or .Xauthority won't be visible
		 * in /tmp from inside the job.
		 */
		if (_need_join_container()) {
			pid_t pid;
			int to_parent[2] = {-1, -1};

			if (pipe(to_parent) < 0) {
				error("%s: pipe failed: %m", __func__);
				rc = SLURM_ERROR;
				goto x11_fail;
			}
			/*
			 * The fork is necessary because we cannot join a
			 * namespace if we are multithreaded. Also we need to
			 * wait for the child to end before proceeding or there
			 * can be a timing race with srun starting X11 apps very
			 * fast.
			 */
			pid = fork();
			if (pid == 0) {
				_setup_x11_child(to_parent);
			} else if (pid > 0) {
				char *tmp = NULL;
				rc = _setup_x11_parent(to_parent, pid, &tmp);
				xfree(step->x11_xauthority);
				if (tmp)
					step->x11_xauthority = tmp;
			} else {
				error("fork: %m");
				rc = SLURM_ERROR;
			}
			close(to_parent[0]);
			close(to_parent[1]);
		} else {
			rc = _set_xauthority();
		}
x11_fail:
		if (rc != SLURM_SUCCESS) {
			set_job_state(SLURMSTEPD_STEP_ENDING);
			close_slurmd_conn(rc);
			goto fail1;
		}

		debug("x11 forwarding local display is %d", step->x11_display);
		debug("x11 forwarding local xauthority is %s",
		      step->x11_xauthority);
	}

	jobacct_id.nodeid = step->nodeid;
	jobacct_id.taskid = step->nodeid; /* Treat node ID as global task ID */
	jobacct_id.step = step;
	jobacct_gather_set_proctrack_container_id(step->cont_id);
	jobacct_gather_add_task(0, &jobacct_id, 1);

	set_job_state(SLURMSTEPD_STEP_RUNNING);
	if (!slurm_conf.job_acct_gather_freq)
		jobacct_gather_stat_task(0, true);

	if (_run_spank_func(SPANK_STEP_TASK_POST_FORK, -1, NULL) < 0) {
		error("spank extern task post-fork failed");
		rc = SLURM_ERROR;
	} else if (slurm_conf.prolog_flags & PROLOG_FLAG_RUN_IN_JOB) {
		rc = _run_prolog_epilog(false);
	}

	if (rc != SLURM_SUCCESS) {
		/*
		 * Failure before the tasks have even started, so we will need
		 * to mark all of them as failed unless there is already an
		 * error present to avoid slurmctld from thinking this was a
		 * slurmd issue and the step just landed on an unhealthy node.
		 */
		slurm_mutex_lock(&step_complete.lock);
		if (!step_complete.step_rc)
			step_complete.step_rc = rc;
		slurm_mutex_unlock(&step_complete.lock);

		for (uint32_t i = 0; i < step->node_tasks; i++)
			if (step->task[i]->estatus <= 0)
				step->task[i]->estatus = W_EXITCODE(1, 0);
	}

	/*
	 * Tell slurmd the setup status; slurmd will handle a failure and
	 * cleanup the sleep task launched above, so we do not need to do
	 * anything special here to handle a setup failure.
	 */
	close_slurmd_conn(rc);

	slurm_mutex_lock(&step->state_mutex);
	while ((step->state < SLURMSTEPD_STEP_CANCELLED)) {
		slurm_cond_wait(&step->state_cond, &step->state_mutex);
	}
	join_extern_threads();
	slurm_mutex_unlock(&step->state_mutex);
	/* Wait for all steps other than extern (this one) to complete */
	if (!pause_for_job_completion(&step->step_id,
				      MAX(slurm_conf.kill_wait, 5), true)) {
		warning("steps did not complete quickly");
	}

	/* remove all tracked tasks */
	while ((jobacct = jobacct_gather_remove_task(0))) {
		if (jobacct->pid)
			jobacctinfo_setinfo(jobacct, JOBACCT_DATA_RUSAGE,
					    &rusage, SLURM_PROTOCOL_VERSION);
		step->jobacct->energy.consumed_energy = 0;
		_local_jobacctinfo_aggregate(step->jobacct, jobacct);
		jobacctinfo_destroy(jobacct);
	}
	step_complete.rank = step->nodeid;
	acct_gather_profile_endpoll();
	acct_gather_profile_g_node_step_end();

	/* Call the other plugins to clean up
	 * the cgroup hierarchy.
	 */
	set_job_state(SLURMSTEPD_STEP_ENDING);
	step_terminate_monitor_start();
	proctrack_g_signal(step->cont_id, SIGKILL);
	proctrack_g_wait(step->cont_id);
	step_terminate_monitor_stop();

	/*
	 * When an event is registered using the cgroups notification API and
	 * memory is constrained using task/cgroup, the following check needs to
	 * happen before any memory cgroup hierarchy removal.
	 *
	 * The eventfd will be woken up by control file implementation *or*
	 * when the cgroup is removed. Thus, for the second case (cgroup
	 * removal) we could be notified with false positive oom events.
	 *
	 * acct_gather_profile_fini() and task_g_post_step() can remove the
	 * cgroup hierarchy if the cgroup implementation of these plugins are
	 * configured.
	 */
	for (uint32_t i = 0; i < step->node_tasks; i++)
		if (task_g_post_term(step, step->task[i]) == ENOMEM)
			step->oom_error = true;

	/* Lock to not collide with the _x11_signal_handler thread. */
	auth_setuid_lock();

	/*
	 * This function below calls jobacct_gather_fini(). For the case of
	 * jobacct_gather/cgroup, it ends up doing the cgroup hierarchy cleanup
	 * in here, and it should happen after the SIGKILL above so that all
	 * children processes from the step are gone.
	 */
	acct_gather_profile_fini();
	task_g_post_step(step);

	auth_setuid_unlock();

fail1:
	conmgr_add_work_fifo(_x11_signal_handler, NULL);

	debug2("%s: Before call to spank_fini()", __func__);
	if (spank_fini(step))
		error("spank_fini failed");
	debug2("%s: After call to spank_fini()", __func__);

	set_job_state(SLURMSTEPD_STEP_ENDING);

	if (step_complete.rank > -1)
		stepd_wait_for_children_slurmstepd();

	/*
	 * Step failed outside of the exec()ed tasks, make sure to tell
	 * slurmctld about it to avoid the user not knowing about a
	 * failure.
	 */
	if (rc && !step_complete.step_rc)
		step_complete.step_rc = rc;

	stepd_send_step_complete_msgs();

	switch_g_extern_step_fini(jobid);

	if (slurm_conf.prolog_flags & PROLOG_FLAG_RUN_IN_JOB) {
		/* Force all other steps to end before epilog starts */
		pause_for_job_completion(&step->step_id, 0, true);

		int epilog_rc = _run_prolog_epilog(true);
		epilog_complete(&step->step_id, step->node_list, epilog_rc);
	}

	return rc;
}

/*
 * Executes the functions of the slurmstepd job manager process,
 * which runs as root and performs shared memory and interconnect
 * initialization, etc.
 *
 * Returns 0 if job ran and completed successfully.
 * Returns errno if job startup failed. NOTE: This will DRAIN the node.
 */
extern int job_manager(void)
{
	int  rc = SLURM_SUCCESS;
	bool io_initialized = false;
	char *oom_val_str;

	debug3("Entered job_manager for %ps pid=%d",
	       &step->step_id, step->jmgr_pid);

#ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#endif /* PR_SET_DUMPABLE */

	/*
	 * Set oom_score_adj of this slurmstepd to the minimum to avoid OOM
	 * killing us before the user processes. If we were killed at this point
	 * due to other steps OOMing, no cleanup would happen, leaving for
	 * example cgroup stray directories if cgroup plugins were initialized.
	 */
	set_oom_adj(STEPD_OOM_ADJ);
	debug("Setting slurmstepd(%d) oom_score_adj to %d", getpid(),
	      STEPD_OOM_ADJ);

	/*
	 * Readjust this slurmstepd oom_score_adj now that we've loaded the
	 * task plugin. If the environment variable SLURMSTEPD_OOM_ADJ is set
	 * and is a valid number (from -1000 to 1000) set the score to that
	 * value.
	 */
	if ((oom_val_str = getenv("SLURMSTEPD_OOM_ADJ"))) {
		int oom_val = atoi(oom_val_str);
		if ((oom_val >= -1000) && (oom_val <= 1000)) {
			debug("Setting slurmstepd oom_score_adj from env to %d",
			      oom_val);
			set_oom_adj(oom_val);
		}
	}

	if (!step->batch && (step->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (step->step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    (mpi_process_env(&step->env) != SLURM_SUCCESS)) {
		rc = SLURM_MPI_PLUGIN_NAME_INVALID;
		goto fail1;
	}

	if (!step->batch && (step->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (step->step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    (switch_g_job_preinit(step) < 0)) {
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto fail1;
	}

	if ((step->cont_id == 0) &&
	    (proctrack_g_create(step) != SLURM_SUCCESS)) {
		error("proctrack_g_create: %m");
		rc = ESLURMD_SETUP_ENVIRONMENT_ERROR;
		goto fail1;
	}

	if (step->step_id.step_id == SLURM_EXTERN_CONT)
		return _spawn_job_container();

	debug2("Before call to spank_init()");
	if ((rc = spank_init(step))) {
		error ("Plugin stack initialization failed.");
		goto fail1;
	}
	debug2("After call to spank_init()");

	/* Call switch_g_job_init() before becoming user */
	if (((!step->batch &&
	      (step->step_id.step_id != SLURM_INTERACTIVE_STEP)) ||
	     switch_g_setup_special_steps()) &&
	    step->argv && (switch_g_job_init(step) < 0)) {
		/* error("switch_g_job_init: %m"); already logged */
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto fail2;
	}

	/* fork necessary threads for MPI */
	if (!step->batch && (step->step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    (mpi_g_slurmstepd_prefork(step, &step->env) != SLURM_SUCCESS)) {
		error("Failed mpi_g_slurmstepd_prefork");
		rc = SLURM_ERROR;
		goto fail2;
	}

	if (!step->batch && (step->step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    (step->accel_bind_type || step->tres_bind)) {
		uint64_t gpu_cnt, nic_cnt;
		gpu_cnt = gres_step_count(step->step_gres_list, "gpu");
		nic_cnt = gres_step_count(step->step_gres_list, "nic");
		if ((gpu_cnt <= 1) || (gpu_cnt == NO_VAL64))
			step->accel_bind_type &= (~ACCEL_BIND_CLOSEST_GPU);
		if ((nic_cnt <= 1) || (nic_cnt == NO_VAL64))
			step->accel_bind_type &= (~ACCEL_BIND_CLOSEST_NIC);
		if (step->accel_bind_type == ACCEL_BIND_VERBOSE)
			step->accel_bind_type = 0;
	}

	/*
	 * Calls pam_setup() and requires pam_finish() if
	 * successful.  Only check for < 0 here since other slurm
	 * error codes could come that are more descriptive.
	 */
	if ((rc = _fork_all_tasks(&io_initialized)) < 0) {
		debug("_fork_all_tasks failed");
		rc = ESLURMD_EXECVE_FAILED;
		goto fail2;
	}

	/*
	 * If IO initialization failed, return SLURM_SUCCESS (on a
	 * batch step) or the node will be drain otherwise.  Regular
	 * srun needs the error sent or it will hang waiting for the
	 * launch to happen.
	 */
	if ((rc != SLURM_SUCCESS) || !io_initialized)
		goto fail2;

	io_close_task_fds();

	/* Attach slurmstepd to system cgroups, if configured */
	attach_system_cgroup_pid(getpid());

	/* if we are not polling then we need to make sure we get some
	 * information here
	 */
	if (!slurm_conf.job_acct_gather_freq)
		jobacct_gather_stat_task(0, true);

	/* Send step launch response with list of pids */
	_send_launch_resp(0);
	set_job_state(SLURMSTEPD_STEP_RUNNING);

#ifdef PR_SET_DUMPABLE
	/* RHEL6 requires setting "dumpable" flag AGAIN; after euid changes */
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#endif /* PR_SET_DUMPABLE */

	/*
	 * task_g_post_term() needs to be called before
	 * acct_gather_profile_fini() and task_g_post_step().
	 */
	_wait_for_all_tasks();
	acct_gather_profile_endpoll();
	acct_gather_profile_g_node_step_end();
	set_job_state(SLURMSTEPD_STEP_ENDING);

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
	set_job_state(SLURMSTEPD_STEP_ENDING);
	step_terminate_monitor_start();
	if (step->cont_id != 0) {
		proctrack_g_signal(step->cont_id, SIGKILL);
		proctrack_g_wait(step->cont_id);
	}
	step_terminate_monitor_stop();
	if (!step->batch && (step->step_id.step_id != SLURM_INTERACTIVE_STEP)) {
		/* This sends a SIGKILL to the pgid */
		if (switch_g_job_postfini(step) < 0) {
                        error("switch_g_job_postfini: %m");
			/*
			 * Drain the node since resources might still be kept.
			 * (E.g, cxi_service for switch/hpe_slingshot.)
			 */
			stepd_drain_node("switch_g_job_postfini failed");
		}
	}

	/*
	 * This function below calls jobacct_gather_fini(). For the case of
	 * jobacct_gather/cgroup, it ends up doing the cgroup hierarchy cleanup
	 * in here, and it should happen after the SIGKILL above so that all
	 * children processes from the step are gone.
	 */
	acct_gather_profile_fini();

	/*
	 * Wait for io thread to complete (if there is one)
	 */
	if (!step->batch && io_initialized)
		_wait_for_io();

	/*
	 * Warn task plugin that the user's step have terminated
	 */
	task_g_post_step(step);

	/*
	 * Reset CPU frequencies if changed
	 */
	if ((step->cpu_freq_min != NO_VAL) || (step->cpu_freq_max != NO_VAL) ||
	    (step->cpu_freq_gov != NO_VAL))
		cpu_freq_reset(step);

	/*
	 * Reset GRES hardware, if needed. This is where GPU frequency is reset.
	 * Make sure stepd is root. If not, emit error.
	 */
	if (!step->batch && (step->step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    step->tres_freq) {
		if (getuid() == (uid_t) 0)
			gres_g_step_hardware_fini();
		else
			error("%s: invalid permissions: cannot uninitialize GRES hardware unless Slurmd was started as root",
			      __func__);
	}

	/*
	 * Notify srun of completion AFTER frequency reset to avoid race
	 * condition starting another job on these CPUs.
	 */
	while (stepd_send_pending_exit_msgs()) {
		;
	}

	debug2("Before call to spank_fini()");
	if (spank_fini(step))
		error("spank_fini failed");
	debug2("After call to spank_fini()");

	/*
	 * This just cleans up all of the PAM state in case rc == 0
	 * which means _fork_all_tasks performs well.
	 * Must be done after IO termination in case of IO operations
	 * require something provided by the PAM (i.e. security token)
	 */
	if (!rc)
		pam_finish();

fail1:
	/* If interactive job startup was abnormal,
	 * be sure to notify client.
	 */
	set_job_state(SLURMSTEPD_STEP_ENDING);
	if (rc != 0) {
		error("%s: exiting abnormally: %s",
		      __func__, slurm_strerror(rc));
		_send_launch_resp(rc);
	}

	if (!step->batch && (step_complete.rank > -1)) {
		if (step->aborted)
			info("job_manager exiting with aborted job");
		else
			stepd_wait_for_children_slurmstepd();

		/*
		 * Step failed outside of the exec()ed tasks, make sure to tell
		 * slurmctld about it to avoid the user not knowing about a
		 * failure.
		 */
		if (rc && !step_complete.step_rc)
			step_complete.step_rc = rc;

		stepd_send_step_complete_msgs();
	}

	return(rc);
}

static int _pre_task_child_privileged(int taskid, struct priv_state *sp)
{
	int setwd = 0; /* set working dir */
	int rc = 0;

	if (reclaim_privileges(sp) < 0)
		return SLURM_ERROR;

	set_oom_adj(0); /* the tasks may be killed by OOM */

	if (!(step->flags & LAUNCH_NO_ALLOC)) {
		/* Add job's pid to job container, if a normal job */
		if (namespace_g_join(&step->step_id, step->uid, false)) {
			error("namespace_g_join failed: %u",
			      step->step_id.job_id);
			exit(1);
		}

		/*
		 * tmpfs job container plugin changes the working directory
		 * back to root working directory, so change it back to users
		 * but after dropping privillege
		 */
		setwd = 1;
	}

	if (spank_task_privileged(step, taskid))
		return error("spank_task_init_privileged failed");

	/* sp->gid_list should already be initialized */
	rc = drop_privileges(step, true, sp, false);
	if (rc) {
		error ("drop_privileges: %m");
		return rc;
	}

	if (step->container) {
		/* Container jobs must start in the correct directory */
		if (chdir(step->cwd) < 0) {
			error("couldn't chdir to `%s': %m", step->cwd);
			return errno;
		}
		debug2("%s: chdir(%s) success", __func__, step->cwd);
	} else if (setwd) {
		if (chdir(step->cwd) < 0) {
			error("couldn't chdir to `%s': %m: going to /tmp instead",
			      step->cwd);
			if (chdir("/tmp") < 0) {
				error("couldn't chdir to /tmp either. dying.");
				return SLURM_ERROR;
			}
		}
	}

	return rc;

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

	if (pipe2(fdpair, O_CLOEXEC) < 0) {
		error ("_exec_wait_info_create: pipe: %m");
		return NULL;
	}

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

static int _exec_wait_signal_child(struct exec_wait_info *e)
{
	char c = '\0';

	safe_write(e->parentfd, &c, sizeof(c));

	return SLURM_SUCCESS;
rwfail:
	error("%s: write(fd:%d) to unblock task %d failed",
		      __func__, e->parentfd, e->id);

	return SLURM_ERROR;
}

static int _exec_wait_signal(void *x, void *arg)
{
	struct exec_wait_info *e = x;
	debug3 ("Unblocking %ps task %d, writefd = %d",
		&step->step_id, e->id, e->parentfd);

	if (_exec_wait_signal_child(e) != SLURM_SUCCESS) {
		/*
		 * can't unblock the task so it must have errored out already
		 */
		if (!step->task[e->id]->estatus)
			step->task[e->id]->estatus = W_EXITCODE(1, 0);
		step->task[e->id]->exited = true;
	}

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
static int exec_wait_kill_children(list_t *exec_wait_list)
{
	int rc = 0;
	int count;
	struct exec_wait_info *e;
	list_itr_t *i;

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

static void _prepare_stdio(stepd_step_task_info_t *task)
{
#ifdef HAVE_PTY_H
	if ((step->flags & LAUNCH_PTY) && (task->gtid == 0)) {
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

/*
 * fork and exec N tasks
 */
static int _fork_all_tasks(bool *io_initialized)
{
	int rc = SLURM_SUCCESS;
	int i;
	struct priv_state sprivs;
	jobacct_id_t jobacct_id;
	list_t *exec_wait_list = NULL;
	uint32_t node_offset = 0, task_offset = 0;
	char saved_cwd[PATH_MAX];

	if (step->het_job_node_offset != NO_VAL)
		node_offset = step->het_job_node_offset;
	if (step->het_job_task_offset != NO_VAL)
		task_offset = step->het_job_task_offset;

	DEF_TIMERS;
	START_TIMER;

	xassert(step != NULL);

	if (!getcwd(saved_cwd, sizeof(saved_cwd))) {
		error ("Unable to get current working directory: %m");
		strlcpy(saved_cwd, "/tmp", sizeof(saved_cwd));
	}

	if (task_g_pre_setuid(step)) {
		error("Failed to invoke task plugins: one of task_p_pre_setuid functions returned error");
		return SLURM_ERROR;
	}

	/*
	 * Temporarily drop effective privileges, except for the euid.
	 * We need to wait until after pam_setup() to drop euid.
	 */
	if (drop_privileges (step, false, &sprivs, true) < 0)
		return ESLURMD_SET_UID_OR_GID_ERROR;

	if (pam_setup(step->user_name, conf->hostname)
	    != SLURM_SUCCESS){
		error ("error in pam_setup");
		rc = SLURM_ERROR;
	}

	/*
	 * Reclaim privileges to do the io setup
	 */
	if (reclaim_privileges(&sprivs) < 0) {
		error("Unable to reclaim privileges");
		/* Don't bother erroring out here */
	}
	if (rc)
		goto fail1; /* pam_setup error */

	set_umask(); /* set umask for stdout/err files */
	rc = _setup_normal_io();
	/*
	 * Initialize log facility to copy errors back to srun
	 */
	if (!rc)
		rc = _slurmd_job_log_init();

	if (rc) {
		error("%s: IO setup failed: %s", __func__, slurm_strerror(rc));

		step->task[0]->estatus = W_EXITCODE(1, 0);
		slurm_mutex_lock(&step_complete.lock);
		step_complete.step_rc = rc;
		slurm_mutex_unlock(&step_complete.lock);

		if (step->batch)
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
	if (!step->batch && (step->step_id.step_id != SLURM_INTERACTIVE_STEP)) {
		/* Handle GpuFreqDef option */
		if (!step->tres_freq && slurm_conf.gpu_freq_def) {
			debug("Setting GPU to GpuFreqDef=%s",
			      slurm_conf.gpu_freq_def);
			xstrfmtcat(step->tres_freq, "gpu:%s",
				   slurm_conf.gpu_freq_def);
		}

		if (step->tres_freq && (getuid() == (uid_t) 0)) {
			gres_g_step_hardware_init(step->step_gres_list,
						  step->tres_freq);
		} else if (step->tres_freq) {
			error("%s: invalid permissions: cannot initialize GRES hardware unless Slurmd was started as root",
			      __func__);
		}
	}

	/*
	 * Temporarily drop effective privileges
	 */
	if (drop_privileges (step, true, &sprivs, true) < 0) {
		error ("drop_privileges: %m");
		rc = SLURM_ERROR;
		goto fail2;
	}

	if (chdir(step->cwd) < 0) {
		error("couldn't chdir to `%s': %m: going to /tmp instead",
		      step->cwd);
		if (chdir("/tmp") < 0) {
			error("couldn't chdir to /tmp either. dying.");
			rc = SLURM_ERROR;
			goto fail3;
		}
	}

	if ((rc = _run_spank_func(SPANK_STEP_USER_INIT, -1, &sprivs))) {
		if (rc < 0) {
			error("spank_user failed.");
			rc = SLURM_ERROR;

			step->task[0]->estatus = W_EXITCODE(1, 0);
			step->task[0]->exited = true;
			slurm_mutex_lock(&step_complete.lock);
			if (!step_complete.step_rc)
				step_complete.step_rc = rc;
			slurm_mutex_unlock(&step_complete.lock);
			goto fail4;
		} else {
			/*
			 * A drop_privileges() or reclaim_privileges() failed,
			 * In this case, bail out skipping the redundant
			 * reclaim.
			 */
			goto fail2;
		}
	}

	exec_wait_list = list_create ((ListDelF) _exec_wait_info_destroy);

	/*
	 * Fork all of the task processes.
	 */
	verbose("starting %u tasks", step->node_tasks);
	for (i = 0; i < step->node_tasks; i++) {
		char time_stamp[256];
		pid_t pid;
		struct exec_wait_info *ei;

		acct_gather_profile_g_task_start(i);
		if ((ei = _fork_child_with_wait_info(i)) == NULL) {
			error("child fork: %m");
			exec_wait_kill_children(exec_wait_list);
			rc = SLURM_ERROR;
			goto fail4;
		} else if ((pid = _exec_wait_get_pid(ei)) == 0) { /* child */
			int rc;

			/*
			 *  Destroy exec_wait_list in the child.
			 *   Only exec_wait_info for previous tasks have been
			 *   added to the list so far, so everything else
			 *   can be discarded.
			 */
			FREE_NULL_LIST(exec_wait_list);

			/* jobacctinfo_endpoll();
			 * closing jobacct files here causes deadlock */

			if (slurm_conf.propagate_prio_process)
				_set_prio_process();

			/*
			 * Reclaim privileges for the child and call any plugin
			 * hooks that may require elevated privs
			 * sprivs.gid_list is already set from the
			 * drop_privileges call above, no not reinitialize.
			 * NOTE: Only put things in here that are self contained
			 * and belong in the child.
			 */
			if ((rc = _pre_task_child_privileged(i, &sprivs)))
				fatal("%s: _pre_task_child_privileged() failed: %s",
				      __func__, slurm_strerror(rc));

			if (_become_user(&sprivs) < 0) {
				error("_become_user failed: %m");
				/* child process, should not return */
				_exit(1);
			}

			/* log_fini(); */ /* note: moved into exec_task() */

			/*
			 *  Need to setup stdio before setpgid() is called
			 *   in case we are setting up a tty. (login_tty()
			 *   must be called before setpgid() or it is
			 *   effectively disabled).
			 */
			_prepare_stdio(step->task[i]);

			/* Close profiling file descriptors */
			acct_gather_profile_g_child_forked();

			/*
			 *  Block until parent notifies us that it is ok to
			 *   proceed. This allows the parent to place all
			 *   children in any process groups or containers
			 *   before they make a call to exec(2).
			 */
			if (_exec_wait_child_wait_for_parent(ei) < 0)
				_exit(1);

			exec_task(i);
		}

		/*
		 * Parent continues:
		 */

		list_append(exec_wait_list, ei);

		log_timestamp(time_stamp, sizeof(time_stamp));
		verbose("task %lu (%lu) started %s",
			(unsigned long) step->task[i]->gtid + task_offset,
			(unsigned long) pid, time_stamp);

		step->task[i]->pid = pid;
		if (i == 0)
			step->pgid = pid;
	}

	/*
	 * All tasks are now forked and running as the user, but
	 * will wait for our signal before calling exec.
	 */

	/*
	 * Reclaim privileges
	 */
	if (reclaim_privileges(&sprivs) < 0) {
		error ("Unable to reclaim privileges");
		/* Don't bother erroring out here */
	}

	if (chdir(saved_cwd) < 0) {
		error ("Unable to return to working directory");
	}

	for (i = 0; i < step->node_tasks; i++) {
		/*
		 * Put this task in the step process group
		 * login_tty() must put task zero in its own
		 * session, causing setpgid() to fail, setsid()
		 * has already set its process group as desired
		 */
		if (((step->flags & LAUNCH_PTY) == 0) &&
		    (setpgid (step->task[i]->pid, step->pgid) < 0)) {
			error("Unable to put task %d (pid %d) into pgrp %d: %m",
			      i, step->task[i]->pid, step->pgid);
		}

		if (proctrack_g_add(step, step->task[i]->pid)
		    == SLURM_ERROR) {
			error("proctrack_g_add: %m");
			rc = SLURM_ERROR;
			goto fail2;
		}
		jobacct_id.nodeid = step->nodeid + node_offset;
		jobacct_id.taskid = step->task[i]->gtid + task_offset;
		jobacct_id.step = step;
		if (i == (step->node_tasks - 1)) {
			/* start polling on the last task */
			jobacct_gather_set_proctrack_container_id(
				step->cont_id);
			jobacct_gather_add_task(step->task[i]->pid, &jobacct_id,
						1);
		} else {
			/* don't poll yet */
			jobacct_gather_add_task(step->task[i]->pid, &jobacct_id,
						0);
		}

		/*
		 * Affinity must be set after cgroup is set, or moving pids from
		 * one cgroup to another will reset affinity.
		 */
		if (task_g_pre_launch_priv(step, i, jobacct_id.taskid) < 0) {
			error("task_g_set_affinity: %m");
			rc = SLURM_ERROR;
			goto fail2;
		}

		if (_run_spank_func(SPANK_STEP_TASK_POST_FORK, i, NULL) < 0) {
			error ("spank task %d post-fork failed", i);
			rc = SLURM_ERROR;

			/*
			 * Failure before the tasks have even started, so we
			 * will need to mark all of them as failed unless there
			 * is already an error present to avoid slurmctld from
			 * thinking this was a slurmd issue and the step just
			 * landed on an unhealthy node.
			 */
			slurm_mutex_lock(&step_complete.lock);
			if (!step_complete.step_rc)
				step_complete.step_rc = rc;
			slurm_mutex_unlock(&step_complete.lock);

			if (step->task[i]->estatus <= 0)
				step->task[i]->estatus = W_EXITCODE(1, 0);
			step->task[i]->exited = true;

			goto fail2;
		}
	}
//	jobacct_gather_set_proctrack_container_id(step->cont_id);

	/*
	 * Now it's ok to unblock the tasks, so they may call exec.
	 */
	list_for_each(exec_wait_list, _exec_wait_signal, NULL);
	FREE_NULL_LIST(exec_wait_list);

	for (i = 0; i < step->node_tasks; i++) {
		/*
		 * Prepare process for attach by parallel debugger
		 * (if specified and able)
		 */
		if (pdebug_trace_process(step->task[i]->pid) == SLURM_ERROR) {
			rc = SLURM_ERROR;
			goto fail2;
		}
	}
	END_TIMER2(__func__);
	return rc;

fail4:
	if (chdir(saved_cwd) < 0) {
		error ("Unable to return to working directory");
	}
fail3:
	if (reclaim_privileges(&sprivs) < 0) {
		error("Unable to reclaim privileges");
		/* Don't bother erroring out here */
	}
fail2:
	FREE_NULL_LIST(exec_wait_list);
	io_close_task_fds();
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
extern int stepd_send_pending_exit_msgs(void)
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
	tid = xmalloc(sizeof(uint32_t) * step->node_tasks);
	for (i = 0; i < step->node_tasks; i++) {
		stepd_step_task_info_t *t = step->task[i];

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
		debug2("%s: aggregated %d task exit messages (rc=[0x%x]:%s)",
		       __func__, nsent, status, slurm_strerror(status));
		_send_exit_msg(tid, nsent, status);
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
static int _wait_for_any_task(bool waitflag)
{
	pid_t pid;
	int completed = 0;
	uint32_t task_offset = 0;

	if (step->het_job_task_offset != NO_VAL)
		task_offset = step->het_job_task_offset;
	do {
		stepd_step_task_info_t *t = NULL;
		int rc = 0;
		jobacctinfo_t *jobacct = NULL;
		char **tmp_env;

		pid = proctrack_g_wait_for_any_task(step, &t, waitflag);
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
					    JOBACCT_DATA_RUSAGE, &t->rusage,
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
				step->jobacct->energy.consumed_energy = 0;
			_local_jobacctinfo_aggregate(step->jobacct, jobacct);
			jobacctinfo_destroy(jobacct);
		}
		acct_gather_profile_g_task_end(pid);
		/*********************************************/

		if (t) {
			debug2("%s: found ended task with primary pid %d ",
			       __func__, t->pid);
			completed++;
			_log_task_exit(t->gtid + task_offset, pid, t->estatus);
			t->exited  = true;
			step->envtp->procid = t->gtid + task_offset;
			step->envtp->localid = t->id;
			step->envtp->distribution = -1;
			step->envtp->batch_flag = step->batch;
			step->envtp->uid = step->uid;
			step->envtp->user_name = xstrdup(step->user_name);
			step->envtp->nodeid = step->nodeid;
			step->envtp->oom_kill_step = step->oom_kill_step ? 1 : 0;

			/*
			 * Modify copy of job's environment. Do not alter in
			 * place or concurrent searches of the environment can
			 * generate invalid memory references.
			 */
			step->envtp->env =
				env_array_copy((const char **) step->env);
			setup_env(step->envtp, false);
			tmp_env = step->env;
			step->env = step->envtp->env;
			env_array_free(tmp_env);

			setenvf(&step->env, "SLURM_SCRIPT_CONTEXT",
				"epilog_task");
			setenvf(&step->env, "SLURMD_NODENAME", "%s",
				conf->node_name);

			if (step->task_epilog) {
				rc = _run_script_as_user("user task_epilog",
							 step->task_epilog, 5,
							 step->env);
				if (rc)
					error("TaskEpilog failed status=%d",
					      rc);
			}
			if (slurm_conf.task_epilog) {
				rc = _run_script_as_user("slurm task_epilog",
							 slurm_conf.task_epilog,
							 -1, step->env);
				if (rc)
					error("--task-epilog failed status=%d",
					      rc);
			}
			if (_run_spank_func(SPANK_STEP_TASK_EXIT, t->id, NULL) <
			    0)
				error ("Unable to spank task %d at exit",
				       t->id);
			rc = task_g_post_term(step, t);
			if (rc == ENOMEM)
				step->oom_error = true;
			else if (rc && !t->estatus)
				t->estatus = rc;

			if (t->estatus) {
				slurm_mutex_lock(&step_complete.lock);
				if (!step_complete.step_rc)
					step_complete.step_rc = t->estatus;
				slurm_mutex_unlock(&step_complete.lock);
			}
		}

	} while ((pid > 0) && !waitflag);

	return completed;
}

static void _wait_for_all_tasks(void)
{
	int tasks_left = 0;
	int i;

	for (i = 0; i < step->node_tasks; i++) {
		if (step->task[i]->state < STEPD_STEP_TASK_COMPLETE) {
			tasks_left++;
		}
	}
	if (tasks_left < step->node_tasks)
		verbose("Only %d of %d requested tasks successfully launched",
			tasks_left, step->node_tasks);

	for (i = 0; i < tasks_left; ) {
		int rc;
		if ((rc = _wait_for_any_task(true)) == -1) {
			error("%s: No child processes. node_tasks:%u, expected:%d, reaped:%d",
			      __func__, step->node_tasks, tasks_left, i);
			break;
		}

		i += rc;
		if (i < tasks_left) {
			/* To limit the amount of traffic back
			 * we will sleep a bit to make sure we
			 * have most if not all the tasks
			 * completed before we return */
			usleep(100000);	/* 100 msec */
			rc = _wait_for_any_task(false);
			if (rc != -1)
				i += rc;
		}

		if (i < tasks_left) {
			/* Send partial completion message only.
			 * The full completion message can only be sent
			 * after resetting CPU frequencies */
			while (stepd_send_pending_exit_msgs()) {
				;
			}
		}
	}
}

static void _wait_for_io(void)
{
	debug("Waiting for IO");
	io_close_all();

	slurm_mutex_lock(&step->io_mutex);
	if (step->io_running) {
		/*
		 * Give the I/O thread up to 300 seconds to cleanup before
		 * continuing with shutdown. Note that it is *not* safe to
		 * try to kill that thread if it's still running - it could
		 * be holding some internal locks which could lead to deadlock
		 * on step teardown, which is infinitely worse than letting
		 * that thread attempt to continue as we quickly head towards
		 * the process exiting anyways.
		 */
		struct timespec ts = { 0, 0 };
		ts.tv_sec = time(NULL) + 300;
		slurm_cond_timedwait(&step->io_cond, &step->io_mutex, &ts);
	}
	slurm_mutex_unlock(&step->io_mutex);

	/* Close any files for stdout/stderr opened by the stepd */
	io_close_local_fds();

	return;
}

static int _make_batch_dir(void)
{
	char *path = NULL;

	if (step->step_id.step_id == SLURM_BATCH_SCRIPT) {
		xstrfmtcat(path, "%s/job%05u",
			   conf->spooldir, step->step_id.job_id);
	} else {
		xstrfmtcat(path, "%s/job%05u.%05u",
			   conf->spooldir, step->step_id.job_id,
			   step->step_id.step_id);
	}

	if ((mkdir(path, 0750) < 0) && (errno != EEXIST)) {
		error("mkdir(%s): %m", path);
		if (errno == ENOSPC)
			stepd_drain_node("SlurmdSpoolDir is full");
		goto error;
	}

	if (chown(path, (uid_t) -1, (gid_t) step->gid) < 0) {
		error("chown(%s): %m", path);
		goto error;
	}

	if (chmod(path, 0750) < 0) {
		error("chmod(%s, 750): %m", path);
		goto error;
	}

	step->batchdir = path;
	return SLURM_SUCCESS;

error:
	xfree(path);
	return SLURM_ERROR;
}

static int _make_batch_script(batch_job_launch_msg_t *msg)
{
	int flags = O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC;
	int fd, length;
	char *script = NULL;
	char *output;

	if (msg->script == NULL) {
		error("%s: called with NULL script", __func__);
		return SLURM_ERROR;
	}

	/* note: should replace this with a length as part of msg */
	if ((length = strlen(msg->script)) < 1) {
		error("%s: called with empty script", __func__);
		return SLURM_ERROR;
	}

	script = _batch_script_path();

	if ((fd = open(script, flags, S_IRWXU)) < 0) {
		error("couldn't open `%s': %m", script);
		goto error;
	}

	if (ftruncate(fd, length) == -1) {
		error("%s: ftruncate to %d failed on `%s`: %m",
		      __func__, length, script);
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

	if (chown(script, step->uid, (gid_t) -1) < 0) {
		error("chown(%s): %m", script);
		goto error;
	}

	step->argv[0] = script;
	return SLURM_SUCCESS;

error:
	(void) unlink(script);
	xfree(script);
	return SLURM_ERROR;
}

extern void stepd_drain_node(char *reason)
{
	update_node_msg_t update_node_msg;

	slurm_init_update_node_msg(&update_node_msg);
	update_node_msg.node_names = conf->node_name;
	update_node_msg.node_state = NODE_STATE_DRAIN;
	update_node_msg.reason = reason;

	(void) slurm_update_node(&update_node_msg);
}

static void
_send_launch_failure(launch_tasks_request_msg_t *msg, slurm_addr_t *cli, int rc,
		     uint16_t protocol_version)
{
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t resp;
	int nodeid;
	char *name = NULL;
	slurm_cred_arg_t *cred;
	uid_t launch_uid = SLURM_AUTH_NOBODY;

	/*
	 * The extern step can get here if something goes wrong starting the
	 * step.  If this does happen we don't have to contact the srun since
	 * there isn't one, just return.
	 */
	if ((msg->step_id.step_id == SLURM_EXTERN_CONT) ||
	    !msg->resp_port || !msg->num_resp_port) {
		debug2("%s: The extern step has nothing to send a launch failure to",
		       __func__);
		return;
	}

	nodeid = nodelist_find(msg->complete_nodelist, conf->node_name);
	name = xstrdup(conf->node_name);

	/* Need to fetch the step uid to restrict the response appropriately */
	cred = slurm_cred_get_args(msg->cred);
	launch_uid = cred->uid;
	slurm_cred_unlock_args(msg->cred);

	debug ("sending launch failure message: %s", slurm_strerror (rc));

	slurm_msg_t_init(&resp_msg);
	memcpy(&resp_msg.address, cli, sizeof(slurm_addr_t));
	slurm_set_port(&resp_msg.address,
		       msg->resp_port[nodeid % msg->num_resp_port]);
	resp_msg.data = &resp;
	resp_msg.msg_type = RESPONSE_LAUNCH_TASKS;
	resp_msg.protocol_version = protocol_version;
	resp_msg.tls_cert = xstrdup(msg->alloc_tls_cert);
	slurm_msg_set_r_uid(&resp_msg, launch_uid);

	memcpy(&resp.step_id, &msg->step_id, sizeof(resp.step_id));

	resp.node_name     = name;
	resp.return_code   = rc ? rc : -1;
	resp.count_of_pids = 0;

	if (_send_srun_resp_msg(&resp_msg, msg->nnodes) != SLURM_SUCCESS)
		error("%s: Failed to send RESPONSE_LAUNCH_TASKS: %m", __func__);
	xfree(name);
	return;
}

static void _send_launch_resp(int rc)
{
	int i;
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t resp;
	srun_info_t *srun = list_peek(step->sruns);

	if (step->batch)
		return;

	debug("Sending launch resp rc=%d", rc);

	slurm_msg_t_init(&resp_msg);
	resp_msg.address	= srun->resp_addr;
	slurm_msg_set_r_uid(&resp_msg, srun->uid);
	resp_msg.protocol_version = srun->protocol_version;
	resp_msg.data		= &resp;
	resp_msg.msg_type	= RESPONSE_LAUNCH_TASKS;
	resp_msg.tls_cert = xstrdup(srun->tls_cert);

	memcpy(&resp.step_id, &step->step_id, sizeof(resp.step_id));

	resp.node_name		= xstrdup(step->node_name);
	resp.return_code	= rc;
	resp.count_of_pids	= step->node_tasks;

	resp.local_pids = xmalloc(step->node_tasks * sizeof(*resp.local_pids));
	resp.task_ids = xmalloc(step->node_tasks * sizeof(*resp.task_ids));
	for (i = 0; i < step->node_tasks; i++) {
		resp.local_pids[i] = step->task[i]->pid;
		/*
		 * Don't add offset here, this represents a bit on the other
		 * side.
		 */
		resp.task_ids[i] = step->task[i]->gtid;
	}

	if (_send_srun_resp_msg(&resp_msg, step->nnodes) != SLURM_SUCCESS)
		error("%s: Failed to send RESPONSE_LAUNCH_TASKS: %m", __func__);

	xfree(resp.local_pids);
	xfree(resp.task_ids);
	xfree(resp.node_name);
}

static int _send_complete_batch_script_msg(int err, int status)
{
	int rc;
	slurm_msg_t	req_msg;
	complete_batch_script_msg_t req;

	memset(&req, 0, sizeof(req));
	if (step->oom_error)
		req.job_rc = SIG_OOM;
	else
		req.job_rc = status;
	req.jobacct	= step->jobacct;
	req.node_name	= step->node_name;
	req.slurm_rc	= err;
	req.step_id = step->step_id;
	req.user_id	= (uint32_t) step->uid;
	slurm_msg_t_init(&req_msg);
	req_msg.msg_type= REQUEST_COMPLETE_BATCH_SCRIPT;
	req_msg.data	= &req;

	log_flag(PROTOCOL, "sending REQUEST_COMPLETE_BATCH_SCRIPT slurm_rc:%s job_rc:%d",
		 slurm_strerror(err), status);

	/* Note: these log messages don't go to slurmd.log from here */

	/*
	 * Retry batch complete RPC, send to slurmctld indefinitely.
	 */
	while (slurm_send_recv_controller_rc_msg(&req_msg, &rc,
						 working_cluster_rec)) {
		info("Retrying job complete RPC for %ps [sleeping %us]",
		     &step->step_id, RETRY_DELAY);
		sleep(RETRY_DELAY);
	}

	if ((rc == ESLURM_ALREADY_DONE) || (rc == ESLURM_INVALID_JOB_ID))
		rc = SLURM_SUCCESS;
	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}

static int _slurmd_job_log_init(void)
{
	char argv0[64];

	conf->log_opts.buffered = 1;

	/*
	 * Reset stderr logging to user requested level
	 * (Logfile and syslog levels remain the same)
	 *
	 * The maximum stderr log level is LOG_LEVEL_DEBUG2 because
	 * some higher level debug messages are generated in the
	 * stdio code, which would otherwise create more stderr traffic
	 * to srun and therefore more debug messages in an endless loop.
	 */
	conf->log_opts.stderr_level = LOG_LEVEL_ERROR;
	if (step->debug > LOG_LEVEL_ERROR) {
		if ((step->uid == 0) || (step->uid == slurm_conf.slurm_user_id))
			conf->log_opts.stderr_level = step->debug;
		else
			error("Use of --slurmd-debug is allowed only for root and SlurmUser(%s), ignoring it",
			      slurm_conf.slurm_user_name);
	}
	if (conf->log_opts.stderr_level > LOG_LEVEL_DEBUG2)
		conf->log_opts.stderr_level = LOG_LEVEL_DEBUG2;

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

	/*
	 *  Connect slurmd stderr to stderr of job
	 */
	if (step->flags & LAUNCH_PTY)
		fd_set_nonblocking(STDERR_FILENO);
	if (step->task != NULL) {
		if (dup2(step->task[0]->stderr_fd, STDERR_FILENO) < 0) {
			error("job_log_init: dup2(stderr): %m");
			return ESLURMD_IO_ERROR;
		}
	}

	verbose("debug levels are stderr='%s', logfile='%s', syslog='%s'",
		log_num2string(conf->log_opts.stderr_level),
		log_num2string(conf->log_opts.logfile_level),
		log_num2string(conf->log_opts.syslog_level));

	return SLURM_SUCCESS;
}

/*
 * Set the priority of the job to be the same as the priority of
 * the process that launched the job on the submit node.
 * In support of the "PropagatePrioProcess" config keyword.
 */
static void _set_prio_process(void)
{
	char *env_name = "SLURM_PRIO_PROCESS";
	char *env_val;
	int prio_daemon, prio_process;

	if (!(env_val = getenvp( step->env, env_name ))) {
		error( "Couldn't find %s in environment", env_name );
		prio_process = 0;
	} else {
		/* Users shouldn't get this in their environment */
		unsetenvp( step->env, env_name );
		prio_process = atoi( env_val );
	}

	if (slurm_conf.propagate_prio_process == PROP_PRIO_NICER) {
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

static int _become_user(struct priv_state *ps)
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
	if (setregid(step->gid, step->gid) < 0) {
		error("setregid: %m");
		return SLURM_ERROR;
	}

	if (setreuid(step->uid, step->uid) < 0) {
		error("setreuid: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Run a script as a specific user, with the specified uid, gid, and
 * extended groups.
 *
 * name IN: class of program (task prolog, task epilog, etc.),
 * path IN: pathname of program to run
 * job IN: slurd job structure, used to get uid, gid, and groups
 * max_wait IN: maximum time to wait in seconds, -1 for no limit
 * env IN: environment variables to use on exec, sets minimal environment
 *	if NULL
 *
 * RET 0 on success, -1 on early failure, or the return from execve().
 */
static int _run_script_as_user(const char *name, const char *path, int max_wait,
			       char **env)
{
	int status, rc, opt;
	pid_t cpid;
	struct exec_wait_info *ei;

	xassert(env);
	if (path == NULL || path[0] == '\0')
		return 0;

	debug("[job %u] attempting to run %s [%s]",
	      step->step_id.job_id, name, path);

	if ((ei = _fork_child_with_wait_info(0)) == NULL) {
		error ("executing %s: fork: %m", name);
		return -1;
	}
	if ((cpid = _exec_wait_get_pid (ei)) == 0) {
		struct priv_state sprivs;
		char *argv[2];

		/* namespace_g_join needs to be called in the
		   forked process part of the fork to avoid a race
		   condition where if this process makes a file or
		   detacts itself from a child before we add the pid
		   to the container in the parent of the fork.
		*/
		/* Ignore system processes */
		if ((step->step_id.job_id != 0) &&
		    !(step->flags & LAUNCH_NO_ALLOC) &&
		    (namespace_g_join(&step->step_id, step->uid, false)))
			error("namespace_g_join(%u): %m", step->step_id.job_id);

		argv[0] = (char *)xstrdup(path);
		argv[1] = NULL;

#ifdef WITH_SELINUX
		if (setexeccon(step->selinux_context)) {
			error("Failed to set SELinux context to '%s': %m",
			      step->selinux_context);
			_exit(127);
		}
#else
		if (step->selinux_context) {
			error("Built without SELinux support but context was specified");
			_exit(127);
		}
#endif

		sprivs.gid_list = NULL;	/* initialize to prevent xfree */
		if (drop_privileges(step, true, &sprivs, false) < 0) {
			error("run_script_as_user drop_privileges: %m");
			/* child process, should not return */
			_exit(127);
		}

		if (_become_user(&sprivs) < 0) {
			error("run_script_as_user _become_user failed: %m");
			/* child process, should not return */
			_exit(127);
		}

		if (chdir(step->cwd) == -1)
			error("run_script_as_user: couldn't "
			      "change working dir to %s: %m", step->cwd);
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
			} else if (errno == EACCES) {
				error("Could not run %s [%s]: access denied",
				      name, path);
				break;
			} else {
				break;
			}
		}
		_exit(127);
	}

	if (_exec_wait_signal_child(ei) != SLURM_SUCCESS)
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
			error("waitpid: %m");
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

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return status;
}
