/*****************************************************************************\
 *  src/slurmd/slurmd/req.c - slurmd request handling
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2016 SchedMD LLC.
 *  Portions copyright (C) 2015 Mellanox Technologies Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#include <ctype.h>
#include <fcntl.h>
#include <grp.h>
#ifdef HAVE_NUMA
#undef NUMA_VERSION1_COMPATIBILITY
#include <numa.h>
#endif
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "src/common/callerid.h"
#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/msg_aggr.h"
#include "src/common/node_features.h"
#include "src/common/node_select.h"
#include "src/common/plugstack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/stepd_api.h"
#include "src/common/uid.h"
#include "src/common/util-net.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/bcast/file_bcast.h"

#include "src/slurmd/slurmd/get_mach_stat.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/slurmd/common/job_container_plugin.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/run_script.h"
#include "src/slurmd/common/reverse_tree_math.h"
#include "src/slurmd/common/slurmstepd_init.h"
#include "src/slurmd/common/task_plugin.h"

#define _LIMIT_INFO 0

#define RETRY_DELAY 15		/* retry every 15 seconds */
#define MAX_RETRY   240		/* retry 240 times (one hour max) */

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif

#define MAX_CPU_CNT 1024
#define MAX_NUMA_CNT 128

typedef struct {
	int ngids;
	gid_t *gids;
} gids_t;

typedef struct {
	uint32_t job_id;
	uint32_t step_id;
	uint64_t job_mem;
	uint64_t step_mem;
} job_mem_limits_t;

typedef struct {
	uint32_t job_id;
	uint32_t step_id;
} starting_step_t;

typedef struct {
	uint32_t job_id;
	uint16_t msg_timeout;
	bool *prolog_fini;
	pthread_cond_t *timer_cond;
	pthread_mutex_t *timer_mutex;
} timer_struct_t;

typedef struct {
	uint32_t jobid;
	uint32_t step_id;
	char *node_list;
	char *partition;
	char *resv_id;
	char **spank_job_env;
	uint32_t spank_job_env_size;
	uid_t uid;
	char *user_name;
} job_env_t;

static int  _abort_step(uint32_t job_id, uint32_t step_id);
static char **_build_env(job_env_t *job_env);
static void _delay_rpc(int host_inx, int host_cnt, int usec_per_rpc);
static void _destroy_env(char **env);
static bool _is_batch_job_finished(uint32_t job_id);
static void _job_limits_free(void *x);
static int  _job_limits_match(void *x, void *key);
static bool _job_still_running(uint32_t job_id);
static int  _kill_all_active_steps(uint32_t jobid, int sig, bool batch);
static void _launch_complete_add(uint32_t job_id);
static void _launch_complete_log(char *type, uint32_t job_id);
static void _launch_complete_rm(uint32_t job_id);
static void _launch_complete_wait(uint32_t job_id);
static int  _launch_job_fail(uint32_t job_id, uint32_t slurm_rc);
static bool _launch_job_test(uint32_t job_id);
static void _note_batch_job_finished(uint32_t job_id);
static int  _prolog_is_running (uint32_t jobid);
static int  _step_limits_match(void *x, void *key);
static int  _terminate_all_steps(uint32_t jobid, bool batch);
static int  _receive_fd(int socket);
static void _rpc_launch_tasks(slurm_msg_t *);
static void _rpc_abort_job(slurm_msg_t *);
static void _rpc_batch_job(slurm_msg_t *msg, bool new_msg);
static void _rpc_prolog(slurm_msg_t *msg);
static void _rpc_job_notify(slurm_msg_t *);
static void _rpc_signal_tasks(slurm_msg_t *);
static void _rpc_checkpoint_tasks(slurm_msg_t *);
static void _rpc_complete_batch(slurm_msg_t *);
static void _rpc_terminate_tasks(slurm_msg_t *);
static void _rpc_timelimit(slurm_msg_t *);
static void _rpc_reattach_tasks(slurm_msg_t *);
static void _rpc_signal_job(slurm_msg_t *);
static void _rpc_suspend_job(slurm_msg_t *msg);
static void _rpc_terminate_job(slurm_msg_t *);
static void _rpc_update_time(slurm_msg_t *);
static void _rpc_shutdown(slurm_msg_t *msg);
static void _rpc_reconfig(slurm_msg_t *msg);
static void _rpc_reboot(slurm_msg_t *msg);
static void _rpc_pid2jid(slurm_msg_t *msg);
static int  _rpc_file_bcast(slurm_msg_t *msg);
static void _file_bcast_cleanup(void);
static int  _file_bcast_register_file(slurm_msg_t *msg,
				      file_bcast_info_t *key);
static int  _rpc_ping(slurm_msg_t *);
static int  _rpc_health_check(slurm_msg_t *);
static int  _rpc_acct_gather_update(slurm_msg_t *);
static int  _rpc_acct_gather_energy(slurm_msg_t *);
static int  _rpc_step_complete(slurm_msg_t *msg);
static int  _rpc_step_complete_aggr(slurm_msg_t *msg);
static int  _rpc_stat_jobacct(slurm_msg_t *msg);
static int  _rpc_list_pids(slurm_msg_t *msg);
static int  _rpc_daemon_status(slurm_msg_t *msg);
static int  _run_epilog(job_env_t *job_env);
static int  _run_prolog(job_env_t *job_env, slurm_cred_t *cred);
static void _rpc_forward_data(slurm_msg_t *msg);
static int  _rpc_network_callerid(slurm_msg_t *msg);
static void _dealloc_gids(gids_t *p);


static bool _pause_for_job_completion(uint32_t jobid, char *nodes,
		int maxtime);
static bool _slurm_authorized_user(uid_t uid);
static void _sync_messages_kill(kill_job_msg_t *req);
static int  _waiter_init (uint32_t jobid);
static int  _waiter_complete (uint32_t jobid);

static void _send_back_fd(int socket, int fd);
static bool _steps_completed_now(uint32_t jobid);
static int  _valid_sbcast_cred(file_bcast_msg_t *req, uid_t req_uid,
			       uint16_t block_no, uint32_t *job_id);
static void _wait_state_completed(uint32_t jobid, int max_delay);
static uid_t _get_job_uid(uint32_t jobid);

static gids_t *_gids_cache_lookup(char *user, gid_t gid);

static int  _add_starting_step(uint16_t type, void *req);
static int  _remove_starting_step(uint16_t type, void *req);
static int  _compare_starting_steps(void *s0, void *s1);
static int  _wait_for_starting_step(uint32_t job_id, uint32_t step_id);
static bool _step_is_starting(uint32_t job_id, uint32_t step_id);

static void _add_job_running_prolog(uint32_t job_id);
static void _remove_job_running_prolog(uint32_t job_id);
static int  _match_jobid(void *s0, void *s1);
static void _wait_for_job_running_prolog(uint32_t job_id);
static bool _requeue_setup_env_fail(void);

/*
 *  List of threads waiting for jobs to complete
 */
static List waiters;

static pthread_mutex_t launch_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t startup = 0;		/* daemon startup time */
static time_t last_slurmctld_msg = 0;

static pthread_mutex_t job_limits_mutex = PTHREAD_MUTEX_INITIALIZER;
static List job_limits_list = NULL;
static bool job_limits_loaded = false;

/*
 * To be fixed in 17.11 to match the count of cpus on a node instead of a hard
 * code.
 */
#define FINI_JOB_CNT 256
static pthread_mutex_t fini_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t fini_job_id[FINI_JOB_CNT] = {0};
static int next_fini_job_inx = 0;

/* NUM_PARALLEL_SUSP_JOBS controls the number of jobs that can be suspended or
 * resumed at one time. */
#define NUM_PARALLEL_SUSP_JOBS 64
/* NUM_PARALLEL_SUSP_STEPS controls the number of steps per job that can be
 * suspended at one time. */
#define NUM_PARALLEL_SUSP_STEPS 8
static pthread_mutex_t suspend_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t job_suspend_array[NUM_PARALLEL_SUSP_JOBS] = {0};
static int job_suspend_size = 0;

#define JOB_STATE_CNT 64
static pthread_mutex_t job_state_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  job_state_cond    = PTHREAD_COND_INITIALIZER;
static uint32_t active_job_id[JOB_STATE_CNT] = {0};

static pthread_mutex_t prolog_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t prolog_serial_mutex = PTHREAD_MUTEX_INITIALIZER;

#define FILE_BCAST_TIMEOUT 300
static pthread_mutex_t file_bcast_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  file_bcast_cond  = PTHREAD_COND_INITIALIZER;
static int fb_read_lock = 0, fb_write_wait_lock = 0, fb_write_lock = 0;
static List file_bcast_list = NULL;

void
slurmd_req(slurm_msg_t *msg)
{
	int rc;

	if (msg == NULL) {
		if (startup == 0)
			startup = time(NULL);
		FREE_NULL_LIST(waiters);
		slurm_mutex_lock(&job_limits_mutex);
		if (job_limits_list) {
			FREE_NULL_LIST(job_limits_list);
			job_limits_loaded = false;
		}
		slurm_mutex_unlock(&job_limits_mutex);
		return;
	}

	switch (msg->msg_type) {
	case REQUEST_LAUNCH_PROLOG:
		debug2("Processing RPC: REQUEST_LAUNCH_PROLOG");
		_rpc_prolog(msg);
		last_slurmctld_msg = time(NULL);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		debug2("Processing RPC: REQUEST_BATCH_JOB_LAUNCH");
		/* Mutex locking moved into _rpc_batch_job() due to
		 * very slow prolog on Blue Gene system. Only batch
		 * jobs are supported on Blue Gene (no job steps). */
		_rpc_batch_job(msg, true);
		last_slurmctld_msg = time(NULL);
		break;
	case REQUEST_LAUNCH_TASKS:
		debug2("Processing RPC: REQUEST_LAUNCH_TASKS");
		slurm_mutex_lock(&launch_mutex);
		_rpc_launch_tasks(msg);
		slurm_mutex_unlock(&launch_mutex);
		break;
	case REQUEST_SIGNAL_TASKS:
		debug2("Processing RPC: REQUEST_SIGNAL_TASKS");
		_rpc_signal_tasks(msg);
		break;
	case REQUEST_CHECKPOINT_TASKS:
		debug2("Processing RPC: REQUEST_CHECKPOINT_TASKS");
		_rpc_checkpoint_tasks(msg);
		break;
	case REQUEST_TERMINATE_TASKS:
		debug2("Processing RPC: REQUEST_TERMINATE_TASKS");
		_rpc_terminate_tasks(msg);
		break;
	case REQUEST_KILL_PREEMPTED:
		debug2("Processing RPC: REQUEST_KILL_PREEMPTED");
		last_slurmctld_msg = time(NULL);
		_rpc_timelimit(msg);
		break;
	case REQUEST_KILL_TIMELIMIT:
		debug2("Processing RPC: REQUEST_KILL_TIMELIMIT");
		last_slurmctld_msg = time(NULL);
		_rpc_timelimit(msg);
		break;
	case REQUEST_REATTACH_TASKS:
		debug2("Processing RPC: REQUEST_REATTACH_TASKS");
		_rpc_reattach_tasks(msg);
		break;
	case REQUEST_SIGNAL_JOB:
		debug2("Processing RPC: REQUEST_SIGNAL_JOB");
		_rpc_signal_job(msg);
		break;
	case REQUEST_SUSPEND_INT:
		debug2("Processing RPC: REQUEST_SUSPEND_INT");
		_rpc_suspend_job(msg);
		last_slurmctld_msg = time(NULL);
		break;
	case REQUEST_ABORT_JOB:
		debug2("Processing RPC: REQUEST_ABORT_JOB");
		last_slurmctld_msg = time(NULL);
		_rpc_abort_job(msg);
		break;
	case REQUEST_TERMINATE_JOB:
		debug2("Processing RPC: REQUEST_TERMINATE_JOB");
		last_slurmctld_msg = time(NULL);
		_rpc_terminate_job(msg);
		break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		debug2("Processing RPC: REQUEST_COMPLETE_BATCH_SCRIPT");
		_rpc_complete_batch(msg);
		break;
	case REQUEST_UPDATE_JOB_TIME:
		debug2("Processing RPC: REQUEST_UPDATE_JOB_TIME");
		_rpc_update_time(msg);
		last_slurmctld_msg = time(NULL);
		break;
	case REQUEST_SHUTDOWN:
		debug2("Processing RPC: REQUEST_SHUTDOWN");
		_rpc_shutdown(msg);
		break;
	case REQUEST_RECONFIGURE:
		debug2("Processing RPC: REQUEST_RECONFIGURE");
		_rpc_reconfig(msg);
		last_slurmctld_msg = time(NULL);
		break;
	case REQUEST_REBOOT_NODES:
		debug2("Processing RPC: REQUEST_REBOOT_NODES");
		_rpc_reboot(msg);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		debug2("Processing RPC: REQUEST_NODE_REGISTRATION_STATUS");
		/* Treat as ping (for slurmctld agent, just return SUCCESS) */
		rc = _rpc_ping(msg);
		last_slurmctld_msg = time(NULL);
		/* Then initiate a separate node registration */
		if (rc == SLURM_SUCCESS)
			send_registration_msg(SLURM_SUCCESS, true);
		break;
	case REQUEST_PING:
		_rpc_ping(msg);
		last_slurmctld_msg = time(NULL);
		break;
	case REQUEST_HEALTH_CHECK:
		debug2("Processing RPC: REQUEST_HEALTH_CHECK");
		_rpc_health_check(msg);
		last_slurmctld_msg = time(NULL);
		break;
	case REQUEST_ACCT_GATHER_UPDATE:
		debug2("Processing RPC: REQUEST_ACCT_GATHER_UPDATE");
		_rpc_acct_gather_update(msg);
		last_slurmctld_msg = time(NULL);
		break;
	case REQUEST_ACCT_GATHER_ENERGY:
		debug2("Processing RPC: REQUEST_ACCT_GATHER_ENERGY");
		_rpc_acct_gather_energy(msg);
		break;
	case REQUEST_JOB_ID:
		_rpc_pid2jid(msg);
		break;
	case REQUEST_FILE_BCAST:
		rc = _rpc_file_bcast(msg);
		slurm_send_rc_msg(msg, rc);
		break;
	case REQUEST_STEP_COMPLETE:
		(void) _rpc_step_complete(msg);
		break;
	case REQUEST_STEP_COMPLETE_AGGR:
		(void) _rpc_step_complete_aggr(msg);
		break;
	case REQUEST_JOB_STEP_STAT:
		(void) _rpc_stat_jobacct(msg);
		break;
	case REQUEST_JOB_STEP_PIDS:
		(void) _rpc_list_pids(msg);
		break;
	case REQUEST_DAEMON_STATUS:
		_rpc_daemon_status(msg);
		break;
	case REQUEST_JOB_NOTIFY:
		_rpc_job_notify(msg);
		break;
	case REQUEST_FORWARD_DATA:
		_rpc_forward_data(msg);
		break;
	case REQUEST_NETWORK_CALLERID:
		debug2("Processing RPC: REQUEST_NETWORK_CALLERID");
		_rpc_network_callerid(msg);
		break;
	case MESSAGE_COMPOSITE:
		error("Processing RPC: MESSAGE_COMPOSITE: "
		      "This should never happen");
		msg_aggr_add_msg(msg, 0, NULL);
		break;
	case RESPONSE_MESSAGE_COMPOSITE:
		debug2("Processing RPC: RESPONSE_MESSAGE_COMPOSITE");
		msg_aggr_resp(msg);
		break;
	default:
		error("slurmd_req: invalid request msg type %d",
		      msg->msg_type);
		slurm_send_rc_msg(msg, EINVAL);
		break;
	}
	return;
}
static int _send_slurmd_conf_lite (int fd, slurmd_conf_t *cf)
{
	int len;
	Buf buffer = init_buf(0);
	slurm_mutex_lock(&cf->config_mutex);
	pack_slurmd_conf_lite(cf, buffer);
	slurm_mutex_unlock(&cf->config_mutex);
	len = get_buf_offset(buffer);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, get_buf_data(buffer), len);
	FREE_NULL_BUFFER(buffer);
	return (0);

 rwfail:
	FREE_NULL_BUFFER(buffer);
	return (-1);
}

static int
_send_slurmstepd_init(int fd, int type, void *req,
		      slurm_addr_t *cli, slurm_addr_t *self,
		      hostset_t step_hset, uint16_t protocol_version)
{
	int len = 0;
	Buf buffer = NULL;
	slurm_msg_t msg;
	uid_t uid = (uid_t)-1;
	gid_t gid = (uid_t)-1;
	gids_t *gids = NULL;

	int rank, proto;
	int parent_rank, children, depth, max_depth;
	char *parent_alias = NULL;
	char *user_name = NULL;
	slurm_addr_t parent_addr = {0};
	char pwd_buffer[PW_BUF_SIZE];
	struct passwd pwd, *pwd_result;

	slurm_msg_t_init(&msg);
	/* send type over to slurmstepd */
	safe_write(fd, &type, sizeof(int));

	/* step_hset can be NULL for batch scripts OR if the job was submitted
	 * by SlurmUser or root using the --no-allocate/-Z option and the job
	 * job credential validation by _check_job_credential() failed. If the
	 * job credential did not validate, then it did not come from slurmctld
	 * and there is no reason to send step completion messages to slurmctld.
	 */
	if (step_hset == NULL) {
		bool send_error = false;
		if (type == LAUNCH_TASKS) {
			launch_tasks_request_msg_t *launch_req;
			launch_req = (launch_tasks_request_msg_t *) req;
			if (launch_req->job_step_id != SLURM_EXTERN_CONT)
				send_error = true;
		}
		if (send_error) {
			info("task rank unavailable due to invalid job "
			     "credential, step completion RPC impossible");
		}
		rank = -1;
		parent_rank = -1;
		children = 0;
		depth = 0;
		max_depth = 0;
	} else if ((type == LAUNCH_TASKS) &&
		   (((launch_tasks_request_msg_t *)req)->alias_list)) {
		/* In the cloud, each task talks directly to the slurmctld
		 * since node addressing is abnormal */
		rank = 0;
		parent_rank = -1;
		children = 0;
		depth = 0;
		max_depth = 0;
	} else {
#ifndef HAVE_FRONT_END
		int count;
		count = hostset_count(step_hset);
		rank = hostset_find(step_hset, conf->node_name);
		reverse_tree_info(rank, count, REVERSE_TREE_WIDTH,
				  &parent_rank, &children,
				  &depth, &max_depth);
		if (rank > 0) { /* rank 0 talks directly to the slurmctld */
			int rc;
			/* Find the slurm_addr_t of this node's parent slurmd
			 * in the step host list */
			parent_alias = hostset_nth(step_hset, parent_rank);
			rc = slurm_conf_get_addr(parent_alias, &parent_addr);
			if (rc != SLURM_SUCCESS) {
				error("Failed looking up address for "
				      "NodeName %s", parent_alias);
				/* parent_rank = -1; */
			}
		}
#else
		/* In FRONT_END mode, one slurmd pretends to be all
		 * NodeNames, so we can't compare conf->node_name
		 * to the NodeNames in step_hset.  Just send step complete
		 * RPC directly to the controller.
		 */
		rank = 0;
		parent_rank = -1;
		children = 0;
		depth = 0;
		max_depth = 0;
#endif
	}
	debug3("slurmstepd rank %d (%s), parent rank %d (%s), "
	       "children %d, depth %d, max_depth %d",
	       rank, conf->node_name,
	       parent_rank, parent_alias ? parent_alias : "NONE",
	       children, depth, max_depth);
	if (parent_alias)
		free(parent_alias);

	/* send reverse-tree info to the slurmstepd */
	safe_write(fd, &rank, sizeof(int));
	safe_write(fd, &parent_rank, sizeof(int));
	safe_write(fd, &children, sizeof(int));
	safe_write(fd, &depth, sizeof(int));
	safe_write(fd, &max_depth, sizeof(int));
	safe_write(fd, &parent_addr, sizeof(slurm_addr_t));

	/* send conf over to slurmstepd */
	if (_send_slurmd_conf_lite(fd, conf) < 0)
		goto rwfail;

	/* send cli address over to slurmstepd */
	buffer = init_buf(0);
	slurm_pack_slurm_addr(cli, buffer);
	len = get_buf_offset(buffer);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, get_buf_data(buffer), len);
	free_buf(buffer);
	buffer = NULL;

	/* send self address over to slurmstepd */
	if (self) {
		buffer = init_buf(0);
		slurm_pack_slurm_addr(self, buffer);
		len = get_buf_offset(buffer);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, get_buf_data(buffer), len);
		free_buf(buffer);
		buffer = NULL;

	} else {
		len = 0;
		safe_write(fd, &len, sizeof(int));
	}

	/* Send GRES information to slurmstepd */
	gres_plugin_send_stepd(fd);

	/* send cpu_frequency info to slurmstepd */
	cpu_freq_send_info(fd);

	/* send req over to slurmstepd */
	switch(type) {
	case LAUNCH_BATCH_JOB:
		gid = (uid_t)((batch_job_launch_msg_t *)req)->gid;
		uid = (uid_t)((batch_job_launch_msg_t *)req)->uid;
		user_name = ((batch_job_launch_msg_t *)req)->user_name;
		msg.msg_type = REQUEST_BATCH_JOB_LAUNCH;
		break;
	case LAUNCH_TASKS:
		/*
		 * The validity of req->uid was verified against the
		 * auth credential in _rpc_launch_tasks().  req->gid
		 * has NOT yet been checked!
		 */
		gid = (uid_t)((launch_tasks_request_msg_t *)req)->gid;
		uid = (uid_t)((launch_tasks_request_msg_t *)req)->uid;
		user_name = ((launch_tasks_request_msg_t *)req)->user_name;
		msg.msg_type = REQUEST_LAUNCH_TASKS;
		break;
	default:
		error("Was sent a task I didn't understand");
		break;
	}
	buffer = init_buf(0);
	msg.data = req;

	if (protocol_version == (uint16_t)NO_VAL)
		proto = SLURM_PROTOCOL_VERSION;
	else
		proto = protocol_version;

	msg.protocol_version = (uint16_t)proto;
	pack_msg(&msg, buffer);
	len = get_buf_offset(buffer);

	safe_write(fd, &proto, sizeof(int));

	safe_write(fd, &len, sizeof(int));
	safe_write(fd, get_buf_data(buffer), len);
	free_buf(buffer);
	buffer = NULL;

#ifdef HAVE_NATIVE_CRAY
	/* Try to avoid calling this on a system which is a native
	 * cray.  getpwuid_r is slow on the compute nodes and this has
	 * in theory been verified earlier.
	 */
	if (!user_name) {
#endif
		/* send cached group ids array for the relevant uid */
		debug3("_send_slurmstepd_init: call to getpwuid_r");
		if (slurm_getpwuid_r(uid, &pwd, pwd_buffer, PW_BUF_SIZE,
				     &pwd_result) || (pwd_result == NULL)) {
			error("%s: getpwuid_r: %m", __func__);
			len = 0;
			safe_write(fd, &len, sizeof(int));
			errno = ESLURMD_UID_NOT_FOUND;
			return errno;
		}
		debug3("%s: return from getpwuid_r", __func__);
		if (gid != pwd_result->pw_gid) {
			debug("%s: Changing gid from %d to %d",
			      __func__, gid, pwd_result->pw_gid);
		}
		gid = pwd_result->pw_gid;
		if (!user_name)
			user_name = pwd_result->pw_name;
#ifdef HAVE_NATIVE_CRAY
	}
#endif
	if (!user_name) {
		/* Sanity check since gids_cache_lookup will fail
		 * with a NULL. */
		error("%s: No user name for %d: %m", __func__, uid);
		len = 0;
		safe_write(fd, &len, sizeof(int));
		errno = ESLURMD_UID_NOT_FOUND;
		return errno;
	}

	if ((gids = _gids_cache_lookup(user_name, gid))) {
		int i;
		uint32_t tmp32;
		safe_write(fd, &gids->ngids, sizeof(int));
		for (i = 0; i < gids->ngids; i++) {
			tmp32 = (uint32_t)gids->gids[i];
			safe_write(fd, &tmp32, sizeof(uint32_t));
		}
		_dealloc_gids(gids);
	} else {
		len = 0;
		safe_write(fd, &len, sizeof(int));
	}
	return 0;

rwfail:
	if (buffer)
		free_buf(buffer);
	error("_send_slurmstepd_init failed");
	return errno;
}


/*
 * Fork and exec the slurmstepd, then send the slurmstepd its
 * initialization data.  Then wait for slurmstepd to send an "ok"
 * message before returning.  When the "ok" message is received,
 * the slurmstepd has created and begun listening on its unix
 * domain socket.
 *
 * Note that this code forks twice and it is the grandchild that
 * becomes the slurmstepd process, so the slurmstepd's parent process
 * will be init, not slurmd.
 */
static int
_forkexec_slurmstepd(uint16_t type, void *req,
		     slurm_addr_t *cli, slurm_addr_t *self,
		     const hostset_t step_hset, uint16_t protocol_version)
{
	pid_t pid;
	int to_stepd[2] = {-1, -1};
	int to_slurmd[2] = {-1, -1};

	if (pipe(to_stepd) < 0 || pipe(to_slurmd) < 0) {
		error("_forkexec_slurmstepd pipe failed: %m");
		return SLURM_FAILURE;
	}

	if (_add_starting_step(type, req)) {
		error("_forkexec_slurmstepd failed in _add_starting_step: %m");
		return SLURM_FAILURE;
	}

	if ((pid = fork()) < 0) {
		error("_forkexec_slurmstepd: fork: %m");
		close(to_stepd[0]);
		close(to_stepd[1]);
		close(to_slurmd[0]);
		close(to_slurmd[1]);
		_remove_starting_step(type, req);
		return SLURM_FAILURE;
	} else if (pid > 0) {
		int rc = SLURM_SUCCESS;
#if (SLURMSTEPD_MEMCHECK == 0)
		int i;
		time_t start_time = time(NULL);
#endif
		/*
		 * Parent sends initialization data to the slurmstepd
		 * over the to_stepd pipe, and waits for the return code
		 * reply on the to_slurmd pipe.
		 */
		if (close(to_stepd[0]) < 0)
			error("Unable to close read to_stepd in parent: %m");
		if (close(to_slurmd[1]) < 0)
			error("Unable to close write to_slurmd in parent: %m");

		if ((rc = _send_slurmstepd_init(to_stepd[1], type,
						req, cli, self,
						step_hset,
						protocol_version)) != 0) {
			error("Unable to init slurmstepd");
			goto done;
		}

		/* If running under valgrind/memcheck, this pipe doesn't work
		 * correctly so just skip it. */
#if (SLURMSTEPD_MEMCHECK == 0)
		i = read(to_slurmd[0], &rc, sizeof(int));
		if (i < 0) {
			error("%s: Can not read return code from slurmstepd "
			      "got %d: %m", __func__, i);
			rc = SLURM_FAILURE;
		} else if (i != sizeof(int)) {
			error("%s: slurmstepd failed to send return code "
			      "got %d: %m", __func__, i);
			rc = SLURM_FAILURE;
		} else {
			int delta_time = time(NULL) - start_time;
			int cc;
			if (delta_time > 5) {
				info("Warning: slurmstepd startup took %d sec, "
				     "possible file system problem or full "
				     "memory", delta_time);
			}
			if (rc != SLURM_SUCCESS)
				error("slurmstepd return code %d", rc);

			cc = SLURM_SUCCESS;
			cc = write(to_stepd[1], &cc, sizeof(int));
			if (cc != sizeof(int)) {
				error("%s: failed to send ack to stepd %d: %m",
				      __func__, cc);
			}
		}
#endif
	done:
		if (_remove_starting_step(type, req))
			error("Error cleaning up starting_step list");

		/* Reap child */
		if (waitpid(pid, NULL, 0) < 0)
			error("Unable to reap slurmd child process");
		if (close(to_stepd[1]) < 0)
			error("close write to_stepd in parent: %m");
		if (close(to_slurmd[0]) < 0)
			error("close read to_slurmd in parent: %m");
		return rc;
	} else {
#if (SLURMSTEPD_MEMCHECK == 1)
		/* memcheck test of slurmstepd, option #1 */
		char *const argv[3] = {"memcheck",
				       (char *)conf->stepd_loc, NULL};
#elif (SLURMSTEPD_MEMCHECK == 2)
		/* valgrind test of slurmstepd, option #2 */
		uint32_t job_id = 0, step_id = 0;
		char log_file[256];
		char *const argv[13] = {"valgrind", "--tool=memcheck",
					"--error-limit=no",
					"--leak-check=summary",
					"--show-reachable=yes",
					"--max-stackframe=16777216",
					"--num-callers=20",
					"--child-silent-after-fork=yes",
					"--track-origins=yes",
					log_file, (char *)conf->stepd_loc,
					NULL};
		if (type == LAUNCH_BATCH_JOB) {
			job_id = ((batch_job_launch_msg_t *)req)->job_id;
			step_id = ((batch_job_launch_msg_t *)req)->step_id;
		} else if (type == LAUNCH_TASKS) {
			job_id = ((launch_tasks_request_msg_t *)req)->job_id;
			step_id = ((launch_tasks_request_msg_t *)req)->job_step_id;
		}
		snprintf(log_file, sizeof(log_file),
			 "--log-file=/tmp/slurmstepd_valgrind_%u.%u",
			 job_id, step_id);
#elif (SLURMSTEPD_MEMCHECK == 3)
		/* valgrind/drd test of slurmstepd, option #3 */
		uint32_t job_id = 0, step_id = 0;
		char log_file[256];
		char *const argv[10] = {"valgrind", "--tool=drd",
					"--error-limit=no",
					"--max-stackframe=16777216",
					"--num-callers=20",
					"--child-silent-after-fork=yes",
					log_file, (char *)conf->stepd_loc,
					NULL};
		if (type == LAUNCH_BATCH_JOB) {
			job_id = ((batch_job_launch_msg_t *)req)->job_id;
			step_id = ((batch_job_launch_msg_t *)req)->step_id;
		} else if (type == LAUNCH_TASKS) {
			job_id = ((launch_tasks_request_msg_t *)req)->job_id;
			step_id = ((launch_tasks_request_msg_t *)req)->job_step_id;
		}
		snprintf(log_file, sizeof(log_file),
			 "--log-file=/tmp/slurmstepd_valgrind_%u.%u",
			 job_id, step_id);
#elif (SLURMSTEPD_MEMCHECK == 4)
		/* valgrind/helgrind test of slurmstepd, option #4 */
		uint32_t job_id = 0, step_id = 0;
		char log_file[256];
		char *const argv[10] = {"valgrind", "--tool=helgrind",
					"--error-limit=no",
					"--max-stackframe=16777216",
					"--num-callers=20",
					"--child-silent-after-fork=yes",
					log_file, (char *)conf->stepd_loc,
					NULL};
		if (type == LAUNCH_BATCH_JOB) {
			job_id = ((batch_job_launch_msg_t *)req)->job_id;
			step_id = ((batch_job_launch_msg_t *)req)->step_id;
		} else if (type == LAUNCH_TASKS) {
			job_id = ((launch_tasks_request_msg_t *)req)->job_id;
			step_id = ((launch_tasks_request_msg_t *)req)->job_step_id;
		}
		snprintf(log_file, sizeof(log_file),
			 "--log-file=/tmp/slurmstepd_valgrind_%u.%u",
			 job_id, step_id);
#else
		/* no memory checking, default */
		char *const argv[2] = { (char *)conf->stepd_loc, NULL};
#endif
		int i;
		int failed = 0;
		/* inform slurmstepd about our config */
		setenv("SLURM_CONF", conf->conffile, 1);

		/*
		 * Child forks and exits
		 */
		if (setsid() < 0) {
			error("_forkexec_slurmstepd: setsid: %m");
			failed = 1;
		}
		if ((pid = fork()) < 0) {
			error("_forkexec_slurmstepd: "
			      "Unable to fork grandchild: %m");
			failed = 2;
		} else if (pid > 0) { /* child */
			exit(0);
		}

		/*
		 * Just in case we (or someone we are linking to)
		 * opened a file and didn't do a close on exec.  This
		 * is needed mostly to protect us against libs we link
		 * to that don't set the flag as we should already be
		 * setting it for those that we open.  The number 256
		 * is an arbitrary number based off test7.9.
		 */
		for (i=3; i<256; i++) {
			(void) fcntl(i, F_SETFD, FD_CLOEXEC);
		}

		/*
		 * Grandchild exec's the slurmstepd
		 *
		 * If the slurmd is being shutdown/restarted before
		 * the pipe happens the old conf->lfd could be reused
		 * and if we close it the dup2 below will fail.
		 */
		if ((to_stepd[0] != conf->lfd)
		    && (to_slurmd[1] != conf->lfd))
			slurm_shutdown_msg_engine(conf->lfd);

		if (close(to_stepd[1]) < 0)
			error("close write to_stepd in grandchild: %m");
		if (close(to_slurmd[0]) < 0)
			error("close read to_slurmd in parent: %m");

		(void) close(STDIN_FILENO); /* ignore return */
		if (dup2(to_stepd[0], STDIN_FILENO) == -1) {
			error("dup2 over STDIN_FILENO: %m");
			exit(1);
		}
		fd_set_close_on_exec(to_stepd[0]);
		(void) close(STDOUT_FILENO); /* ignore return */
		if (dup2(to_slurmd[1], STDOUT_FILENO) == -1) {
			error("dup2 over STDOUT_FILENO: %m");
			exit(1);
		}
		fd_set_close_on_exec(to_slurmd[1]);
		(void) close(STDERR_FILENO); /* ignore return */
		if (dup2(devnull, STDERR_FILENO) == -1) {
			error("dup2 /dev/null to STDERR_FILENO: %m");
			exit(1);
		}
		fd_set_noclose_on_exec(STDERR_FILENO);
		log_fini();
		if (!failed) {
			if (conf->chos_loc && !access(conf->chos_loc, X_OK))
				execvp(conf->chos_loc, argv);
			else
				execvp(argv[0], argv);
			error("exec of slurmstepd failed: %m");
		}
		exit(2);
	}
}


/*
 * The job(step) credential is the only place to get a definitive
 * list of the nodes allocated to a job step.  We need to return
 * a hostset_t of the nodes. Validate the incoming RPC, updating
 * job_mem needed.
 */
static int
_check_job_credential(launch_tasks_request_msg_t *req, uid_t uid,
		      int node_id, hostset_t *step_hset,
		      uint16_t protocol_version)
{
	slurm_cred_arg_t arg;
	hostset_t	s_hset = NULL;
	bool		user_ok = _slurm_authorized_user(uid);
	bool		verified = true;
	int		host_index = -1;
	int		rc;
	slurm_cred_t    *cred = req->cred;
	uint32_t	jobid = req->job_id;
	uint32_t	stepid = req->job_step_id;
	int		tasks_to_launch = req->tasks_to_launch[node_id];
	uint32_t	job_cpus = 0, step_cpus = 0;

	/*
	 * First call slurm_cred_verify() so that all valid
	 * credentials are checked
	 */
	rc = slurm_cred_verify(conf->vctx, cred, &arg, protocol_version);
	if (rc < 0) {
		verified = false;
		if ((!user_ok) || (errno != ESLURMD_INVALID_JOB_CREDENTIAL))
			return SLURM_ERROR;
		else {
			debug("_check_job_credential slurm_cred_verify failed:"
			      " %m, but continuing anyway.");
		}
	}

	/* If uid is the SlurmUser or root and the credential is bad,
	 * then do not attempt validating the credential */
	if (!verified) {
		*step_hset = NULL;
		if (rc >= 0) {
			if ((s_hset = hostset_create(arg.step_hostlist)))
				*step_hset = s_hset;
			slurm_cred_free_args(&arg);
		}
		return SLURM_SUCCESS;
	}

	if ((arg.jobid != jobid) || (arg.stepid != stepid)) {
		error("job credential for %u.%u, expected %u.%u",
		      arg.jobid, arg.stepid, jobid, stepid);
		goto fail;
	}

	if (arg.uid != uid) {
		error("job credential created for uid %ld, expected %ld",
		      (long) arg.uid, (long) uid);
		goto fail;
	}

	/*
	 * Check that credential is valid for this host
	 */
	if (!(s_hset = hostset_create(arg.step_hostlist))) {
		error("Unable to parse credential hostlist: `%s'",
		      arg.step_hostlist);
		goto fail;
	}

	if (!hostset_within(s_hset, conf->node_name)) {
		error("Invalid job %u.%u credential for user %u: "
		      "host %s not in hostset %s",
		      arg.jobid, arg.stepid, arg.uid,
		      conf->node_name, arg.step_hostlist);
		goto fail;
	}

	if ((arg.job_nhosts > 0) && (tasks_to_launch > 0)) {
		uint32_t hi, i, i_first_bit=0, i_last_bit=0, j;
		bool cpu_log = slurm_get_debug_flags() & DEBUG_FLAG_CPU_BIND;

#ifdef HAVE_FRONT_END
		host_index = 0;	/* It is always 0 for front end systems */
#else
		hostset_t j_hset;
		/* Determine the CPU count based upon this node's index into
		 * the _job's_ allocation (job's hostlist and core_bitmap) */
		if (!(j_hset = hostset_create(arg.job_hostlist))) {
			error("Unable to parse credential hostlist: `%s'",
			      arg.job_hostlist);
			goto fail;
		}
		host_index = hostset_find(j_hset, conf->node_name);
		hostset_destroy(j_hset);

		if ((host_index < 0) || (host_index >= arg.job_nhosts)) {
			error("job cr credential invalid host_index %d for "
			      "job %u", host_index, arg.jobid);
			goto fail;
		}
#endif

		if (cpu_log) {
			char *per_job = "", *per_step = "";
			uint64_t job_mem  = arg.job_mem_limit;
			uint64_t step_mem = arg.step_mem_limit;
			if (job_mem & MEM_PER_CPU) {
				job_mem &= (~MEM_PER_CPU);
				per_job = "_per_CPU";
			}
			if (step_mem & MEM_PER_CPU) {
				step_mem &= (~MEM_PER_CPU);
				per_step = "_per_CPU";
			}
			info("====================");
			info("step_id:%u.%u job_mem:%"PRIu64"MB%s "
			     "step_mem:%"PRIu64"MB%s",
			     arg.jobid, arg.stepid, job_mem, per_job,
			     step_mem, per_step);
		}

		hi = host_index + 1;	/* change from 0-origin to 1-origin */
		for (i=0; hi; i++) {
			if (hi > arg.sock_core_rep_count[i]) {
				i_first_bit += arg.sockets_per_node[i] *
					       arg.cores_per_socket[i] *
					       arg.sock_core_rep_count[i];
				hi -= arg.sock_core_rep_count[i];
			} else {
				i_first_bit += arg.sockets_per_node[i] *
					       arg.cores_per_socket[i] *
					       (hi - 1);
				i_last_bit = i_first_bit +
					     arg.sockets_per_node[i] *
					     arg.cores_per_socket[i];
				break;
			}
		}
		/* Now count the allocated processors */
		for (i=i_first_bit, j=0; i<i_last_bit; i++, j++) {
			char *who_has = NULL;
			if (bit_test(arg.job_core_bitmap, i)) {
				job_cpus++;
				who_has = "Job";
			}
			if (bit_test(arg.step_core_bitmap, i)) {
				step_cpus++;
				who_has = "Step";
			}
			if (cpu_log && who_has) {
				info("JobNode[%u] CPU[%u] %s alloc",
				     host_index, j, who_has);
			}
		}
		if (cpu_log)
			info("====================");
		if (step_cpus == 0) {
			error("cons_res: zero processors allocated to step");
			step_cpus = 1;
		}
		/* NOTE: step_cpus is the count of allocated resources
		 * (typically cores). Convert to CPU count as needed */
		if (i_last_bit <= i_first_bit)
			error("step credential has no CPUs selected");
		else {
			i = conf->cpus / (i_last_bit - i_first_bit);
			if (i > 1) {
				if (cpu_log)
					info("Scaling CPU count by factor of "
					     "%d (%u/(%u-%u))",
					     i, conf->cpus,
					     i_last_bit, i_first_bit);
				step_cpus *= i;
				job_cpus *= i;
			}
		}
		if (tasks_to_launch > step_cpus) {
			/* This is expected with the --overcommit option
			 * or hyperthreads */
			debug("cons_res: More than one tasks per logical "
			      "processor (%d > %u) on host [%u.%u %ld %s] ",
			      tasks_to_launch, step_cpus, arg.jobid,
			      arg.stepid, (long) arg.uid, arg.step_hostlist);
		}
	} else {
		step_cpus = 1;
		job_cpus  = 1;
	}

	/* Overwrite any memory limits in the RPC with contents of the
	 * memory limit within the credential.
	 * Reset the CPU count on this node to correct value. */
	if (arg.step_mem_limit) {
		if (arg.step_mem_limit & MEM_PER_CPU) {
			req->step_mem_lim  = arg.step_mem_limit &
					     (~MEM_PER_CPU);
			req->step_mem_lim *= step_cpus;
		} else
			req->step_mem_lim  = arg.step_mem_limit;
	} else {
		if (arg.job_mem_limit & MEM_PER_CPU) {
			req->step_mem_lim  = arg.job_mem_limit &
					     (~MEM_PER_CPU);
			req->step_mem_lim *= job_cpus;
		} else
			req->step_mem_lim  = arg.job_mem_limit;
	}
	if (arg.job_mem_limit & MEM_PER_CPU) {
		req->job_mem_lim  = arg.job_mem_limit & (~MEM_PER_CPU);
		req->job_mem_lim *= job_cpus;
	} else
		req->job_mem_lim  = arg.job_mem_limit;
	req->job_core_spec = arg.job_core_spec;
	req->node_cpus = step_cpus;
#if 0
	info("%u.%u node_id:%d mem orig:%"PRIu64" cpus:%u limit:%"PRIu64"",
	     jobid, stepid, node_id, arg.job_mem_limit,
	     step_cpus, req->job_mem_lim);
#endif

	*step_hset = s_hset;
	slurm_cred_free_args(&arg);
	return SLURM_SUCCESS;

    fail:
	if (s_hset)
		hostset_destroy(s_hset);
	*step_hset = NULL;
	slurm_cred_free_args(&arg);
	slurm_seterrno_ret(ESLURMD_INVALID_JOB_CREDENTIAL);
}

static inline int _char_to_val(int c)
{
	int cl;

	cl = tolower(c);
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (cl >= 'a' && cl <= 'f')
		return cl + (10 - 'a');
	else
		return -1;
}

static int _str_to_memset(bitstr_t *mask, char *str)
{
	int len = strlen(str);
	const char *ptr = str + len - 1;
	int base = 0;

	while (ptr >= str) {
		char val = _char_to_val(*ptr);
		if (val == (char) -1)
			return -1;
		if ((val & 1) && (base < MAX_NUMA_CNT))
			bit_set(mask, base);
		base++;
		if ((val & 2) && (base < MAX_NUMA_CNT))
			bit_set(mask, base);
		base++;
		if ((val & 4) && (base < MAX_NUMA_CNT))
			bit_set(mask, base);
		base++;
		if ((val & 8) && (base < MAX_NUMA_CNT))
			bit_set(mask, base);
		base++;
		len--;
		ptr--;
	}

	return 0;
}

static bitstr_t *_build_cpu_bitmap(uint16_t cpu_bind_type, char *cpu_bind,
				   int task_cnt_on_node)
{
	bitstr_t *cpu_bitmap = NULL;
	char *tmp_str, *tok, *save_ptr = NULL;
	int cpu_id;

	if (cpu_bind_type & CPU_BIND_NONE) {
		/* Return NULL bitmap, sort all NUMA */
	} else if ((cpu_bind_type & CPU_BIND_RANK) &&
		   (task_cnt_on_node > 0)) {
		cpu_bitmap = bit_alloc(MAX_CPU_CNT);
		if (task_cnt_on_node >= MAX_CPU_CNT)
			task_cnt_on_node = MAX_CPU_CNT;
		for (cpu_id = 0; cpu_id < task_cnt_on_node; cpu_id++) {
			bit_set(cpu_bitmap, cpu_id);
		}
	} else if (cpu_bind_type & CPU_BIND_MAP) {
		cpu_bitmap = bit_alloc(MAX_CPU_CNT);
		tmp_str = xstrdup(cpu_bind);
		tok = strtok_r(tmp_str, ",", &save_ptr);
		while (tok) {
			if (!xstrncmp(tok, "0x", 2))
				cpu_id = strtoul(tok + 2, NULL, 16);
			else
				cpu_id = strtoul(tok, NULL, 10);
			if (cpu_id < MAX_CPU_CNT)
				bit_set(cpu_bitmap, cpu_id);
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp_str);
	} else if (cpu_bind_type & CPU_BIND_MASK) {
		cpu_bitmap = bit_alloc(MAX_CPU_CNT);
		tmp_str = xstrdup(cpu_bind);
		tok = strtok_r(tmp_str, ",", &save_ptr);
		while (tok) {
			if (!xstrncmp(tok, "0x", 2))
				tok += 2;	/* Skip "0x", always hex */
			(void) _str_to_memset(cpu_bitmap, tok);
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp_str);
	}
	return cpu_bitmap;
}

static bitstr_t *_xlate_cpu_to_numa_bitmap(bitstr_t *cpu_bitmap)
{
	bitstr_t *numa_bitmap = NULL;
#ifdef HAVE_NUMA
	struct bitmask *numa_bitmask = NULL;
	char cpu_str[10240];
	int i, max_numa;

	if (numa_available() != -1) {
		bit_fmt(cpu_str, sizeof(cpu_str), cpu_bitmap);
		numa_bitmask = numa_parse_cpustring(cpu_str);
		if (numa_bitmask) {
			max_numa = numa_max_node();
			numa_bitmap = bit_alloc(MAX_NUMA_CNT);
			for (i = 0; i <= max_numa; i++) {
				if (numa_bitmask_isbitset(numa_bitmask, i))
					bit_set(numa_bitmap, i);
			}
			numa_bitmask_free(numa_bitmask);
		}
	}
#endif
	return numa_bitmap;

}

static bitstr_t *_build_numa_bitmap(uint16_t mem_bind_type, char *mem_bind,
				    uint16_t cpu_bind_type, char *cpu_bind,
				    int task_cnt_on_node)
{
	bitstr_t *cpu_bitmap = NULL, *numa_bitmap = NULL;
	char *tmp_str, *tok, *save_ptr = NULL;
	int numa_id;

	if (mem_bind_type & MEM_BIND_NONE) {
		/* Return NULL bitmap, sort all NUMA */
	} else if ((mem_bind_type & MEM_BIND_RANK) &&
		   (task_cnt_on_node > 0)) {
		numa_bitmap = bit_alloc(MAX_NUMA_CNT);
		if (task_cnt_on_node >= MAX_NUMA_CNT)
			task_cnt_on_node = MAX_NUMA_CNT;
		for (numa_id = 0; numa_id < task_cnt_on_node; numa_id++) {
			bit_set(numa_bitmap, numa_id);
		}
	} else if (mem_bind_type & MEM_BIND_MAP) {
		numa_bitmap = bit_alloc(MAX_NUMA_CNT);
		tmp_str = xstrdup(mem_bind);
		tok = strtok_r(tmp_str, ",", &save_ptr);
		while (tok) {
			if (!xstrncmp(tok, "0x", 2))
				numa_id = strtoul(tok + 2, NULL, 16);
			else
				numa_id = strtoul(tok, NULL, 10);
			if (numa_id < MAX_NUMA_CNT)
				bit_set(numa_bitmap, numa_id);
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp_str);
	} else if (mem_bind_type & MEM_BIND_MASK) {
		numa_bitmap = bit_alloc(MAX_NUMA_CNT);
		tmp_str = xstrdup(mem_bind);
		tok = strtok_r(tmp_str, ",", &save_ptr);
		while (tok) {
			if (!xstrncmp(tok, "0x", 2))
				tok += 2;	/* Skip "0x", always hex */
			(void) _str_to_memset(numa_bitmap, tok);
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp_str);
	} else if (mem_bind_type & MEM_BIND_LOCAL) {
		cpu_bitmap = _build_cpu_bitmap(cpu_bind_type, cpu_bind,
					       task_cnt_on_node);
		if (cpu_bitmap) {
			numa_bitmap = _xlate_cpu_to_numa_bitmap(cpu_bitmap);
			FREE_NULL_BITMAP(cpu_bitmap);
		}
	}

	return numa_bitmap;
}

static void
_rpc_launch_tasks(slurm_msg_t *msg)
{
	int      errnum = SLURM_SUCCESS;
	uint16_t port;
	char     host[MAXHOSTNAMELEN];
	uid_t    req_uid;
	launch_tasks_request_msg_t *req = msg->data;
	bool     super_user = false;
	bool     mem_sort = false;
#ifndef HAVE_FRONT_END
	bool     first_job_run;
#endif
	slurm_addr_t self;
	slurm_addr_t *cli = &msg->orig_addr;
	hostset_t step_hset = NULL;
	job_mem_limits_t *job_limits_ptr;
	int nodeid = 0;
	bitstr_t *numa_bitmap = NULL;

#ifndef HAVE_FRONT_END
	/* It is always 0 for front end systems */
	nodeid = nodelist_find(req->complete_nodelist, conf->node_name);
#endif
	req_uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);
	memcpy(&req->orig_addr, &msg->orig_addr, sizeof(slurm_addr_t));

	super_user = _slurm_authorized_user(req_uid);

	if ((super_user == false) && (req_uid != req->uid)) {
		error("launch task request from uid %u",
		      (unsigned int) req_uid);
		errnum = ESLURM_USER_ID_MISSING;	/* or invalid user */
		goto done;
	}

	slurm_get_ip_str(cli, &port, host, sizeof(host));
	info("launch task %u.%u request from %u.%u@%s (port %hu)", req->job_id,
	     req->job_step_id, req->uid, req->gid, host, port);

	/* this could be set previously and needs to be overwritten by
	 * this call for messages to work correctly for the new call */
	env_array_overwrite(&req->env, "SLURM_SRUN_COMM_HOST", host);
	req->envc = envcount(req->env);

#ifndef HAVE_FRONT_END
	slurm_mutex_lock(&prolog_mutex);
	first_job_run = !slurm_cred_jobid_cached(conf->vctx, req->job_id);
#endif
	if (_check_job_credential(req, req_uid, nodeid, &step_hset,
				  msg->protocol_version) < 0) {
		errnum = errno;
		error("Invalid job credential from %ld@%s: %m",
		      (long) req_uid, host);
#ifndef HAVE_FRONT_END
		slurm_mutex_unlock(&prolog_mutex);
#endif
		goto done;
	}

	/* Must follow _check_job_credential(), which sets some req fields */
	task_g_slurmd_launch_request(req->job_id, req, nodeid);

#ifndef HAVE_FRONT_END
	if (first_job_run) {
		int rc;
		job_env_t job_env;

		slurm_cred_insert_jobid(conf->vctx, req->job_id);
		_add_job_running_prolog(req->job_id);
		slurm_mutex_unlock(&prolog_mutex);

		if (container_g_create(req->job_id))
			error("container_g_create(%u): %m", req->job_id);

		memset(&job_env, 0, sizeof(job_env_t));

		job_env.jobid = req->job_id;
		job_env.step_id = req->job_step_id;
		job_env.node_list = req->complete_nodelist;
		job_env.partition = req->partition;
		job_env.spank_job_env = req->spank_job_env;
		job_env.spank_job_env_size = req->spank_job_env_size;
		job_env.uid = req->uid;
		job_env.user_name = req->user_name;
		rc =  _run_prolog(&job_env, req->cred);
		if (rc) {
			int term_sig, exit_status;
			if (WIFSIGNALED(rc)) {
				exit_status = 0;
				term_sig    = WTERMSIG(rc);
			} else {
				exit_status = WEXITSTATUS(rc);
				term_sig    = 0;
			}
			error("[job %u] prolog failed status=%d:%d",
			      req->job_id, exit_status, term_sig);
			errnum = ESLURMD_PROLOG_FAILED;
			goto done;
		}
		/* Since the job could have been killed while the prolog was
		 * running, test if the credential has since been revoked
		 * and exit as needed. */
		if (slurm_cred_revoked(conf->vctx, req->cred)) {
			info("Job %u already killed, do not launch step %u.%u",
			     req->job_id, req->job_id, req->job_step_id);
			errnum = ESLURMD_CREDENTIAL_REVOKED;
			goto done;
		}
	} else {
		slurm_mutex_unlock(&prolog_mutex);
		_wait_for_job_running_prolog(req->job_id);
	}
#endif

	if (req->mem_bind_type & MEM_BIND_SORT) {
		int task_cnt = -1;
		if (req->tasks_to_launch)
			task_cnt = (int) req->tasks_to_launch[nodeid];
		mem_sort = true;
		numa_bitmap = _build_numa_bitmap(req->mem_bind_type,
						 req->mem_bind,
						 req->cpu_bind_type,
						 req->cpu_bind, task_cnt);
	}
	node_features_g_step_config(mem_sort, numa_bitmap);
	FREE_NULL_BITMAP(numa_bitmap);

	if (req->job_mem_lim || req->step_mem_lim) {
		step_loc_t step_info;
		slurm_mutex_lock(&job_limits_mutex);
		if (!job_limits_list)
			job_limits_list = list_create(_job_limits_free);
		step_info.jobid  = req->job_id;
		step_info.stepid = req->job_step_id;
		job_limits_ptr = list_find_first (job_limits_list,
						  _step_limits_match,
						  &step_info);
		if (!job_limits_ptr) {
			job_limits_ptr = xmalloc(sizeof(job_mem_limits_t));
			job_limits_ptr->job_id   = req->job_id;
			job_limits_ptr->job_mem  = req->job_mem_lim;
			job_limits_ptr->step_id  = req->job_step_id;
			job_limits_ptr->step_mem = req->step_mem_lim;
#if _LIMIT_INFO
			info("AddLim step:%u.%u job_mem:%"PRIu64" "
			     "step_mem:%"PRIu64"",
			     job_limits_ptr->job_id, job_limits_ptr->step_id,
			     job_limits_ptr->job_mem,
			     job_limits_ptr->step_mem);
#endif
			list_append(job_limits_list, job_limits_ptr);
		}
		slurm_mutex_unlock(&job_limits_mutex);
	}

	slurm_get_stream_addr(msg->conn_fd, &self);

	debug3("_rpc_launch_tasks: call to _forkexec_slurmstepd");
	errnum = _forkexec_slurmstepd(LAUNCH_TASKS, (void *)req, cli, &self,
				      step_hset, msg->protocol_version);
	debug3("_rpc_launch_tasks: return from _forkexec_slurmstepd");
	_launch_complete_add(req->job_id);

    done:
	if (step_hset)
		hostset_destroy(step_hset);

	if (slurm_send_rc_msg(msg, errnum) < 0) {
		char addr_str[32];
		slurm_print_slurm_addr(&msg->address, addr_str,
				       sizeof(addr_str));
		error("_rpc_launch_tasks: unable to send return code to "
		      "address:port=%s msg_type=%u: %m",
		      addr_str, msg->msg_type);

		/*
		 * Rewind credential so that srun may perform retry
		 */
		slurm_cred_rewind(conf->vctx, req->cred); /* ignore errors */

	} else if (errnum == SLURM_SUCCESS) {
		save_cred_state(conf->vctx);
		task_g_slurmd_reserve_resources(req->job_id, req, nodeid);
	}

	/*
	 *  If job prolog failed, indicate failure to slurmctld
	 */
	if (errnum == ESLURMD_PROLOG_FAILED)
		send_registration_msg(errnum, false);
}

/*
 * Open file based upon permissions of a different user
 * IN path_name - name of file to open
 * IN uid - User ID to use for file access check
 * IN gid - Group ID to use for file access check
 * RET -1 on error, file descriptor otherwise
 */
static int _open_as_other(char *path_name, batch_job_launch_msg_t *req)
{
	pid_t child;
	gids_t *gids;
	int pipe[2];
	int fd = -1, rc = 0;

	if (!(gids = _gids_cache_lookup(req->user_name, req->gid))) {
		error("%s: gids_cache_lookup for %s failed",
		      __func__, req->user_name);
		return -1;
	}

	if ((rc = container_g_create(req->job_id))) {
		error("%s: container_g_create(%u): %m", __func__, req->job_id);
		_dealloc_gids(gids);
		return -1;
	}

	/* child process will setuid to the user, register the process
	 * with the container, and open the file for us. */
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pipe) != 0) {
		error("%s: Failed to open pipe: %m", __func__);
		_dealloc_gids(gids);
		return -1;
	}

	child = fork();
	if (child == -1) {
		error("%s: fork failure", __func__);
		_dealloc_gids(gids);
		close(pipe[0]);
		close(pipe[1]);
		return -1;
	} else if (child > 0) {
		close(pipe[0]);
		(void) waitpid(child, &rc, 0);
		_dealloc_gids(gids);
		if (WIFEXITED(rc) && (WEXITSTATUS(rc) == 0))
			fd = _receive_fd(pipe[1]);
		close(pipe[1]);
		return fd;
	}

	/* child process below here */

	close(pipe[1]);

	/* container_g_add_pid needs to be called in the
	 * forked process part of the fork to avoid a race
	 * condition where if this process makes a file or
	 * detacts itself from a child before we add the pid
	 * to the container in the parent of the fork. */
	if (container_g_add_pid(req->job_id, getpid(), req->uid)) {
		error("%s container_g_add_pid(%u): %m", __func__, req->job_id);
		exit(SLURM_ERROR);
	}

	/* The child actually performs the I/O and exits with
	 * a return code, do not return! */

	/*********************************************************************\
	 * NOTE: It would be best to do an exec() immediately after the fork()
	 * in order to help prevent a possible deadlock in the child process
	 * due to locks being set at the time of the fork and being freed by
	 * the parent process, but not freed by the child process. Performing
	 * the work inline is done for simplicity. Note that the logging
	 * performed by error() should be safe due to the use of
	 * atfork_install_handlers() as defined in src/common/log.c.
	 * Change the code below with caution.
	\*********************************************************************/

	if (setgroups(gids->ngids, gids->gids) < 0) {
		error("%s: uid: %u setgroups failed: %m", __func__, req->uid);
		exit(errno);
	}
	_dealloc_gids(gids);

	if (setgid(req->gid) < 0) {
		error("%s: uid:%u setgid(%u): %m", __func__, req->uid,req->gid);
		exit(errno);
	}
	if (setuid(req->uid) < 0) {
		error("%s: getuid(%u): %m", __func__, req->uid);
		exit(errno);
	}

	fd = open(path_name, (O_CREAT|O_APPEND|O_WRONLY), 0644);
	if (fd == -1) {
		error("%s: uid:%u can't open `%s`: %m",
		      __func__, req->uid, path_name);
		exit(errno);
	}
	_send_back_fd(pipe[0], fd);
	close(fd);
	exit(SLURM_SUCCESS);
}

static void
_prolog_error(batch_job_launch_msg_t *req, int rc)
{
	char *err_name = NULL, *path_name = NULL;
	char *fmt_char;
	int fd;

	if (req->std_err || req->std_out) {
		if (req->std_err)
			err_name = xstrdup(req->std_err);
		else
			err_name = xstrdup(req->std_out);
		if ((fmt_char = strchr(err_name, (int) '%')) &&
		    (fmt_char[1] == 'j') && !strchr(fmt_char+1, (int) '%')) {
			char *tmp_name = NULL;
			fmt_char[1] = 'u';
			xstrfmtcat(tmp_name, err_name, req->job_id);
			xfree(err_name);
			err_name = tmp_name;
			//tmp_name = NULL;
		}
	} else {
		xstrfmtcat(err_name, "slurm-%u.out", req->job_id);
	}
	if (err_name[0] == '/')
		xstrfmtcat(path_name, "%s", err_name);
	else if (req->work_dir)
		xstrfmtcat(path_name, "%s/%s", req->work_dir, err_name);
	else
		xstrfmtcat(path_name, "/%s", err_name);
	xfree(err_name);

	if ((fd = _open_as_other(path_name, req)) == -1) {
		error("Unable to open %s: Permission denied", path_name);
		xfree(path_name);
		return;
	}
	xfree(path_name);
	xstrfmtcat(err_name, "Error running slurm prolog: %d\n",
		   WEXITSTATUS(rc));
	safe_write(fd, err_name, strlen(err_name));
	if (fchown(fd, (uid_t) req->uid, (gid_t) req->gid) == -1) {
		error("%s: Couldn't change fd owner to %u:%u: %m",
		      __func__, req->uid, req->gid);
	}
rwfail:
	xfree(err_name);
	close(fd);
}

/* load the user's environment on this machine if requested
 * SLURM_GET_USER_ENV environment variable is set */
static int
_get_user_env(batch_job_launch_msg_t *req)
{
	struct passwd pwd, *pwd_ptr = NULL;
	char pwd_buf[PW_BUF_SIZE];
	char **new_env;
	int i;
	static time_t config_update = 0;
	static bool no_env_cache = false;

	if (config_update != conf->last_update) {
		char *sched_params = slurm_get_sched_params();
		no_env_cache = (sched_params &&
				strstr(sched_params, "no_env_cache"));
		xfree(sched_params);
		config_update = conf->last_update;
	}

	for (i=0; i<req->envc; i++) {
		if (xstrcmp(req->environment[i], "SLURM_GET_USER_ENV=1") == 0)
			break;
	}
	if (i >= req->envc)
		return 0;		/* don't need to load env */

	if (slurm_getpwuid_r(req->uid, &pwd, pwd_buf, PW_BUF_SIZE, &pwd_ptr)
	    || (pwd_ptr == NULL)) {
		error("%s: getpwuid_r(%u):%m", __func__, req->uid);
		return -1;
	}
	verbose("%s: get env for user %s here", __func__, pwd.pw_name);

	/* Permit up to 120 second delay before using cache file */
	new_env = env_array_user_default(pwd.pw_name, 120, 0, no_env_cache);
	if (! new_env) {
		error("%s: Unable to get user's local environment%s",
		      __func__, no_env_cache ?
		      "" : ", running only with passed environment");
		return -1;
	}

	env_array_merge(&new_env,
			(const char **) req->environment);
	env_array_free(req->environment);
	req->environment = new_env;
	req->envc = envcount(new_env);

	return 0;
}

/* The RPC currently contains a memory size limit, but we load the
 * value from the job credential to be certain it has not been
 * altered by the user */
static void
_set_batch_job_limits(slurm_msg_t *msg)
{
	int i;
	uint32_t alloc_lps = 0, last_bit = 0;
	bool cpu_log = slurm_get_debug_flags() & DEBUG_FLAG_CPU_BIND;
	slurm_cred_arg_t arg;
	batch_job_launch_msg_t *req = (batch_job_launch_msg_t *)msg->data;

	if (slurm_cred_get_args(req->cred, &arg) != SLURM_SUCCESS)
		return;
	req->job_core_spec = arg.job_core_spec;	/* Prevent user reset */

	if (cpu_log) {
		char *per_job = "";
		uint64_t job_mem  = arg.job_mem_limit;
		if (job_mem & MEM_PER_CPU) {
			job_mem &= (~MEM_PER_CPU);
			per_job = "_per_CPU";
		}
		info("====================");
		info("batch_job:%u job_mem:%"PRIu64"MB%s", req->job_id,
		     job_mem, per_job);
	}
	if (cpu_log || (arg.job_mem_limit & MEM_PER_CPU)) {
		if (arg.job_nhosts > 0) {
			last_bit = arg.sockets_per_node[0] *
				   arg.cores_per_socket[0];
			for (i=0; i<last_bit; i++) {
				if (!bit_test(arg.job_core_bitmap, i))
					continue;
				if (cpu_log)
					info("JobNode[0] CPU[%u] Job alloc",i);
				alloc_lps++;
			}
		}
		if (cpu_log)
			info("====================");
		if (alloc_lps == 0) {
			error("_set_batch_job_limit: alloc_lps is zero");
			alloc_lps = 1;
		}

		/* NOTE: alloc_lps is the count of allocated resources
		 * (typically cores). Convert to CPU count as needed */
		if (last_bit < 1)
			error("Batch job credential allocates no CPUs");
		else {
			i = conf->cpus / last_bit;
			if (i > 1)
				alloc_lps *= i;
		}
	}

	if (arg.job_mem_limit & MEM_PER_CPU) {
		req->job_mem = arg.job_mem_limit & (~MEM_PER_CPU);
		req->job_mem *= alloc_lps;
	} else
		req->job_mem = arg.job_mem_limit;

	slurm_cred_free_args(&arg);
}

/* These functions prevent a possible race condition if the batch script's
 * complete RPC is processed before it's launch_successful response. This
 *  */
static bool _is_batch_job_finished(uint32_t job_id)
{
	bool found_job = false;
	int i;

	slurm_mutex_lock(&fini_mutex);
	for (i = 0; i < FINI_JOB_CNT; i++) {
		if (fini_job_id[i] == job_id) {
			found_job = true;
			break;
		}
	}
	slurm_mutex_unlock(&fini_mutex);

	return found_job;
}
static void _note_batch_job_finished(uint32_t job_id)
{
	slurm_mutex_lock(&fini_mutex);
	fini_job_id[next_fini_job_inx] = job_id;
	if (++next_fini_job_inx >= FINI_JOB_CNT)
		next_fini_job_inx = 0;
	slurm_mutex_unlock(&fini_mutex);
}

/* Send notification to slurmctld we are finished running the prolog.
 * This is needed on system that don't use srun to launch their tasks.
 */
static void _notify_slurmctld_prolog_fini(
	uint32_t job_id, uint32_t prolog_return_code)
{
	int rc;
	slurm_msg_t req_msg;
	complete_prolog_msg_t req;

	slurm_msg_t_init(&req_msg);
	req.job_id	= job_id;
	req.prolog_rc	= prolog_return_code;

	req_msg.msg_type= REQUEST_COMPLETE_PROLOG;
	req_msg.data	= &req;

	if ((slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0) ||
	    (rc != SLURM_SUCCESS))
		error("Error sending prolog completion notification: %m");
}

/* Convert memory limits from per-CPU to per-node */
static void _convert_job_mem(slurm_msg_t *msg)
{
	prolog_launch_msg_t *req = (prolog_launch_msg_t *)msg->data;
	slurm_cred_arg_t arg;
	hostset_t j_hset = NULL;
	int rc, hi, host_index, job_cpus;
	int i, i_first_bit = 0, i_last_bit = 0;

	rc = slurm_cred_verify(conf->vctx, req->cred, &arg,
			       msg->protocol_version);
	if (rc < 0) {
		error("%s: slurm_cred_verify failed: %m", __func__);
		req->nnodes = 1;	/* best guess */
		return;
	}

	req->nnodes = arg.job_nhosts;

	if (arg.job_mem_limit == 0)
		goto fini;
	if ((arg.job_mem_limit & MEM_PER_CPU) == 0) {
		req->job_mem_limit = arg.job_mem_limit;
		goto fini;
	}

	/* Assume 1 CPU on error */
	req->job_mem_limit = arg.job_mem_limit & (~MEM_PER_CPU);

	if (!(j_hset = hostset_create(arg.job_hostlist))) {
		error("%s: Unable to parse credential hostlist: `%s'",
		      __func__, arg.step_hostlist);
		goto fini;
	}
	host_index = hostset_find(j_hset, conf->node_name);
	hostset_destroy(j_hset);

	hi = host_index + 1;	/* change from 0-origin to 1-origin */
	for (i = 0; hi; i++) {
		if (hi > arg.sock_core_rep_count[i]) {
			i_first_bit += arg.sockets_per_node[i] *
				       arg.cores_per_socket[i] *
				       arg.sock_core_rep_count[i];
			i_last_bit = i_first_bit +
				     arg.sockets_per_node[i] *
				     arg.cores_per_socket[i] *
				     arg.sock_core_rep_count[i];
			hi -= arg.sock_core_rep_count[i];
		} else {
			i_first_bit += arg.sockets_per_node[i] *
				       arg.cores_per_socket[i] * (hi - 1);
			i_last_bit = i_first_bit +
				     arg.sockets_per_node[i] *
				     arg.cores_per_socket[i];
			break;
		}
	}

	/* Now count the allocated processors on this node */
	job_cpus = 0;
	for (i = i_first_bit; i < i_last_bit; i++) {
		if (bit_test(arg.job_core_bitmap, i))
			job_cpus++;
	}

	/* NOTE: alloc_lps is the count of allocated resources
	 * (typically cores). Convert to CPU count as needed */
	if (i_last_bit > i_first_bit) {
		i = conf->cpus / (i_last_bit - i_first_bit);
		if (i > 1)
			job_cpus *= i;
	}

	req->job_mem_limit *= job_cpus;

fini:	slurm_cred_free_args(&arg);
}

static void _make_prolog_mem_container(slurm_msg_t *msg)
{
	prolog_launch_msg_t *req = (prolog_launch_msg_t *)msg->data;
	job_mem_limits_t *job_limits_ptr;
	step_loc_t step_info;

	_convert_job_mem(msg);	/* Convert per-CPU mem limit */
	if (req->job_mem_limit) {
		slurm_mutex_lock(&job_limits_mutex);
		if (!job_limits_list)
			job_limits_list = list_create(_job_limits_free);
		step_info.jobid  = req->job_id;
		step_info.stepid = SLURM_EXTERN_CONT;
		job_limits_ptr = list_find_first (job_limits_list,
						  _step_limits_match,
						  &step_info);
		if (!job_limits_ptr) {
			job_limits_ptr = xmalloc(sizeof(job_mem_limits_t));
			job_limits_ptr->job_id   = req->job_id;
			job_limits_ptr->job_mem  = req->job_mem_limit;
			job_limits_ptr->step_id  = SLURM_EXTERN_CONT;
			job_limits_ptr->step_mem = req->job_mem_limit;
#if _LIMIT_INFO
			info("AddLim step:%u.%u job_mem:%"PRIu64""
			     " step_mem:%"PRIu64"",
			     job_limits_ptr->job_id, job_limits_ptr->step_id,
			     job_limits_ptr->job_mem,
			     job_limits_ptr->step_mem);
#endif
			list_append(job_limits_list, job_limits_ptr);
		}
		slurm_mutex_unlock(&job_limits_mutex);
	}
}

static void _spawn_prolog_stepd(slurm_msg_t *msg)
{
	prolog_launch_msg_t *req = (prolog_launch_msg_t *)msg->data;
	launch_tasks_request_msg_t *launch_req;
	slurm_addr_t self;
	slurm_addr_t *cli = &msg->orig_addr;
	int i;

	launch_req = xmalloc(sizeof(launch_tasks_request_msg_t));
	launch_req->alias_list		= req->alias_list;
	launch_req->complete_nodelist	= req->nodes;
	launch_req->cpus_per_task	= 1;
	launch_req->cred		= req->cred;
	launch_req->cwd			= req->work_dir;
	launch_req->efname		= "/dev/null";
	launch_req->gid			= req->gid;
	launch_req->global_task_ids	= xmalloc(sizeof(uint32_t *)
						  * req->nnodes);
	launch_req->ifname		= "/dev/null";
	launch_req->job_id		= req->job_id;
	launch_req->job_mem_lim		= req->job_mem_limit;
	launch_req->job_step_id		= SLURM_EXTERN_CONT;
	launch_req->nnodes		= req->nnodes;
	launch_req->ntasks		= req->nnodes;
	launch_req->ofname		= "/dev/null";
	launch_req->partition		= req->partition;
	launch_req->spank_job_env_size	= req->spank_job_env_size;
	launch_req->spank_job_env	= req->spank_job_env;
	launch_req->step_mem_lim	= req->job_mem_limit;
	launch_req->tasks_to_launch	= xmalloc(sizeof(uint16_t)
						  * req->nnodes);
	launch_req->uid			= req->uid;

	for (i = 0; i < req->nnodes; i++) {
		uint32_t *tmp32 = xmalloc(sizeof(uint32_t));
		*tmp32 = i;
		launch_req->global_task_ids[i] = tmp32;
		launch_req->tasks_to_launch[i] = 1;
	}

	slurm_get_stream_addr(msg->conn_fd, &self);
	/* Since job could have been killed while the prolog was
	 * running (especially on BlueGene, which can take minutes
	 * for partition booting). Test if the credential has since
	 * been revoked and exit as needed. */
	if (slurm_cred_revoked(conf->vctx, req->cred)) {
		info("Job %u already killed, do not launch extern step",
		     req->job_id);
	} else {
		hostset_t step_hset = hostset_create(req->nodes);

		debug3("%s: call to _forkexec_slurmstepd", __func__);
		(void) _forkexec_slurmstepd(
			LAUNCH_TASKS, (void *)launch_req, cli,
			&self, step_hset, msg->protocol_version);
		debug3("%s: return from _forkexec_slurmstepd", __func__);
		if (step_hset)
			hostset_destroy(step_hset);
	}

	for (i = 0; i < req->nnodes; i++)
		xfree(launch_req->global_task_ids[i]);
	xfree(launch_req->global_task_ids);
	xfree(launch_req->tasks_to_launch);
	xfree(launch_req);
}

static void _rpc_prolog(slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	prolog_launch_msg_t *req = (prolog_launch_msg_t *)msg->data;
	job_env_t job_env;
	bool     first_job_run;
	uid_t    req_uid;

	if (req == NULL)
		return;

	req_uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);
	if (!_slurm_authorized_user(req_uid)) {
		error("REQUEST_LAUNCH_PROLOG request from uid %u",
		      (unsigned int) req_uid);
		return;
	}

	if (slurm_send_rc_msg(msg, rc) < 0) {
		error("Error starting prolog: %m");
	}
	if (rc) {
		int term_sig, exit_status;
		if (WIFSIGNALED(rc)) {
			exit_status = 0;
			term_sig    = WTERMSIG(rc);
		} else {
			exit_status = WEXITSTATUS(rc);
			term_sig    = 0;
		}
		error("[job %u] prolog start failed status=%d:%d",
		      req->job_id, exit_status, term_sig);
		rc = ESLURMD_PROLOG_FAILED;
	}

	slurm_mutex_lock(&prolog_mutex);
	first_job_run = !slurm_cred_jobid_cached(conf->vctx, req->job_id);
	if (first_job_run) {
		if (slurmctld_conf.prolog_flags & PROLOG_FLAG_CONTAIN)
			_make_prolog_mem_container(msg);

		if (container_g_create(req->job_id))
			error("container_g_create(%u): %m", req->job_id);

		slurm_cred_insert_jobid(conf->vctx, req->job_id);
		_add_job_running_prolog(req->job_id);
		slurm_mutex_unlock(&prolog_mutex);

		memset(&job_env, 0, sizeof(job_env_t));

		job_env.jobid = req->job_id;
		job_env.step_id = 0;	/* not available */
		job_env.node_list = req->nodes;
		job_env.partition = req->partition;
		job_env.spank_job_env = req->spank_job_env;
		job_env.spank_job_env_size = req->spank_job_env_size;
		job_env.uid = req->uid;
		job_env.user_name = req->user_name;
#if defined(HAVE_BG)
		select_g_select_jobinfo_get(req->select_jobinfo,
					    SELECT_JOBDATA_BLOCK_ID,
					    &job_env.resv_id);
#elif defined(HAVE_ALPS_CRAY)
		job_env.resv_id = select_g_select_jobinfo_xstrdup(
			req->select_jobinfo, SELECT_PRINT_RESV_ID);
#endif
		rc = _run_prolog(&job_env, req->cred);

		if (rc) {
			int term_sig, exit_status;
			if (WIFSIGNALED(rc)) {
				exit_status = 0;
				term_sig    = WTERMSIG(rc);
			} else {
				exit_status = WEXITSTATUS(rc);
				term_sig    = 0;
			}
			error("[job %u] prolog failed status=%d:%d",
			      req->job_id, exit_status, term_sig);
			rc = ESLURMD_PROLOG_FAILED;
		}
	} else
		slurm_mutex_unlock(&prolog_mutex);

	if (!(slurmctld_conf.prolog_flags & PROLOG_FLAG_NOHOLD))
		_notify_slurmctld_prolog_fini(req->job_id, rc);

	if (rc == SLURM_SUCCESS) {
		if (slurmctld_conf.prolog_flags & PROLOG_FLAG_CONTAIN)
			_spawn_prolog_stepd(msg);
	} else {
		_launch_job_fail(req->job_id, rc);
		send_registration_msg(rc, false);
	}
}

static void
_rpc_batch_job(slurm_msg_t *msg, bool new_msg)
{
	batch_job_launch_msg_t *req = (batch_job_launch_msg_t *)msg->data;
	bool     first_job_run;
	int      rc = SLURM_SUCCESS;
	bool	 replied = false, revoked;
	slurm_addr_t *cli = &msg->orig_addr;

	if (new_msg) {
		uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
						     conf->auth_info);
		if (!_slurm_authorized_user(req_uid)) {
			error("Security violation, batch launch RPC from uid %d",
			      req_uid);
			rc = ESLURM_USER_ID_MISSING;  /* or bad in this case */
			goto done;
		}
	}

	if (_launch_job_test(req->job_id)) {
		error("Job %u already running, do not launch second copy",
		      req->job_id);
		rc = ESLURM_DUPLICATE_JOB_ID;	/* job already running */
		_launch_job_fail(req->job_id, rc);
		goto done;
	}

	slurm_cred_handle_reissue(conf->vctx, req->cred);
	if (slurm_cred_revoked(conf->vctx, req->cred)) {
		error("Job %u already killed, do not launch batch job",
		      req->job_id);
		rc = ESLURMD_CREDENTIAL_REVOKED;	/* job already ran */
		goto done;
	}

	task_g_slurmd_batch_request(req->job_id, req);	/* determine task affinity */

	slurm_mutex_lock(&prolog_mutex);
	first_job_run = !slurm_cred_jobid_cached(conf->vctx, req->job_id);

	/* BlueGene prolog waits for partition boot and is very slow.
	 * On any system we might need to load environment variables
	 * for Moab (see --get-user-env), which could also be slow.
	 * Just reply now and send a separate kill job request if the
	 * prolog or launch fail. */
	replied = true;
	if (new_msg && (slurm_send_rc_msg(msg, rc) < 1)) {
		/* The slurmctld is no longer waiting for a reply.
		 * This typically indicates that the slurmd was
		 * blocked from memory and/or CPUs and the slurmctld
		 * has requeued the batch job request. */
		error("Could not confirm batch launch for job %u, "
		      "aborting request", req->job_id);
		rc = SLURM_COMMUNICATIONS_SEND_ERROR;
		slurm_mutex_unlock(&prolog_mutex);
		goto done;
	}

	/*
	 * Insert jobid into credential context to denote that
	 * we've now "seen" an instance of the job
	 */
	if (first_job_run) {
		job_env_t job_env;
		slurm_cred_insert_jobid(conf->vctx, req->job_id);
		_add_job_running_prolog(req->job_id);
		slurm_mutex_unlock(&prolog_mutex);

		memset(&job_env, 0, sizeof(job_env_t));

		job_env.jobid = req->job_id;
		job_env.step_id = req->step_id;
		job_env.node_list = req->nodes;
		job_env.partition = req->partition;
		job_env.spank_job_env = req->spank_job_env;
		job_env.spank_job_env_size = req->spank_job_env_size;
		job_env.uid = req->uid;
		job_env.user_name = req->user_name;
		/*
	 	 * Run job prolog on this node
	 	 */
#if defined(HAVE_BG)
		select_g_select_jobinfo_get(req->select_jobinfo,
					    SELECT_JOBDATA_BLOCK_ID,
					    &job_env.resv_id);
#elif defined(HAVE_ALPS_CRAY)
		job_env.resv_id = select_g_select_jobinfo_xstrdup(
			req->select_jobinfo, SELECT_PRINT_RESV_ID);
#endif
		if (container_g_create(req->job_id))
			error("container_g_create(%u): %m", req->job_id);
		rc = _run_prolog(&job_env, req->cred);
		xfree(job_env.resv_id);
		if (rc) {
			int term_sig, exit_status;
			if (WIFSIGNALED(rc)) {
				exit_status = 0;
				term_sig    = WTERMSIG(rc);
			} else {
				exit_status = WEXITSTATUS(rc);
				term_sig    = 0;
			}
			error("[job %u] prolog failed status=%d:%d",
			      req->job_id, exit_status, term_sig);
			_prolog_error(req, rc);
			rc = ESLURMD_PROLOG_FAILED;
			goto done;
		}
	} else {
		slurm_mutex_unlock(&prolog_mutex);
		_wait_for_job_running_prolog(req->job_id);
	}

	if (_get_user_env(req) < 0) {
		bool requeue = _requeue_setup_env_fail();
		if (requeue) {
			rc = ESLURMD_SETUP_ENVIRONMENT_ERROR;
			goto done;
		}
	}
	_set_batch_job_limits(msg);

	/* Since job could have been killed while the prolog was
	 * running (especially on BlueGene, which can take minutes
	 * for partition booting). Test if the credential has since
	 * been revoked and exit as needed. */
	if (slurm_cred_revoked(conf->vctx, req->cred)) {
		info("Job %u already killed, do not launch batch job",
		     req->job_id);
		rc = ESLURMD_CREDENTIAL_REVOKED;     /* job already ran */
		goto done;
	}

	slurm_mutex_lock(&launch_mutex);
	if (req->step_id == SLURM_BATCH_SCRIPT)
		info("Launching batch job %u for UID %d",
		     req->job_id, req->uid);
	else
		info("Launching batch job %u.%u for UID %d",
		     req->job_id, req->step_id, req->uid);

	debug3("_rpc_batch_job: call to _forkexec_slurmstepd");
	rc = _forkexec_slurmstepd(LAUNCH_BATCH_JOB, (void *)req, cli, NULL,
				  (hostset_t)NULL, SLURM_PROTOCOL_VERSION);
	debug3("_rpc_batch_job: return from _forkexec_slurmstepd: %d", rc);

	slurm_mutex_unlock(&launch_mutex);
	_launch_complete_add(req->job_id);

	/* On a busy system, slurmstepd may take a while to respond,
	 * if the job was cancelled in the interim, run through the
	 * abort logic below. */
	revoked = slurm_cred_revoked(conf->vctx, req->cred);
	if (revoked)
		_launch_complete_rm(req->job_id);
	if (revoked && _is_batch_job_finished(req->job_id)) {
		/* If configured with select/serial and the batch job already
		 * completed, consider the job sucessfully launched and do
		 * not repeat termination logic below, which in the worst case
		 * just slows things down with another message. */
		revoked = false;
	}
	if (revoked) {
		info("Job %u killed while launch was in progress",
		     req->job_id);
		sleep(1);	/* give slurmstepd time to create
				 * the communication socket */
		_terminate_all_steps(req->job_id, true);
		rc = ESLURMD_CREDENTIAL_REVOKED;
		goto done;
	}

done:
	if (!replied) {
		if (new_msg && (slurm_send_rc_msg(msg, rc) < 1)) {
			/* The slurmctld is no longer waiting for a reply.
			 * This typically indicates that the slurmd was
			 * blocked from memory and/or CPUs and the slurmctld
			 * has requeued the batch job request. */
			error("Could not confirm batch launch for job %u, "
			      "aborting request", req->job_id);
			rc = SLURM_COMMUNICATIONS_SEND_ERROR;
		} else {
			/* No need to initiate separate reply below */
			rc = SLURM_SUCCESS;
		}
	}
	if (rc != SLURM_SUCCESS) {
		/* prolog or job launch failure,
		 * tell slurmctld that the job failed */
		if (req->step_id == SLURM_BATCH_SCRIPT)
			_launch_job_fail(req->job_id, rc);
		else
			_abort_step(req->job_id, req->step_id);
	}

	/*
	 *  If job prolog failed or we could not reply,
	 *  initiate message to slurmctld with current state
	 */
	if ((rc == ESLURMD_PROLOG_FAILED)
	    || (rc == SLURM_COMMUNICATIONS_SEND_ERROR)
	    || (rc == ESLURMD_SETUP_ENVIRONMENT_ERROR)) {
		send_registration_msg(rc, false);
	}
}

/*
 * Send notification message to batch job
 */
static void
_rpc_job_notify(slurm_msg_t *msg)
{
	job_notify_msg_t *req = msg->data;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);
	uid_t job_uid;
	List steps;
	ListIterator i;
	step_loc_t *stepd = NULL;
	int step_cnt  = 0;
	int fd;

	debug("_rpc_job_notify, uid = %d, jobid = %u", req_uid, req->job_id);
	job_uid = _get_job_uid(req->job_id);
	if ((int)job_uid < 0)
		goto no_job;

	/*
	 * check that requesting user ID is the SLURM UID or root
	 */
	if ((req_uid != job_uid) && (!_slurm_authorized_user(req_uid))) {
		error("Security violation: job_notify(%u) from uid %d",
		      req->job_id, req_uid);
		return;
	}

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if ((stepd->jobid  != req->job_id) ||
		    (stepd->stepid != SLURM_BATCH_SCRIPT)) {
			continue;
		}

		step_cnt++;

		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to step %u.%u",
			       stepd->jobid, stepd->stepid);
			continue;
		}

		info("send notification to job %u.%u",
		     stepd->jobid, stepd->stepid);
		if (stepd_notify_job(fd, stepd->protocol_version,
				     req->message) < 0)
			debug("notify jobid=%u failed: %m", stepd->jobid);
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

no_job:
	if (step_cnt == 0) {
		debug2("Can't find jobid %u to send notification message",
		       req->job_id);
	}
}

static int
_launch_job_fail(uint32_t job_id, uint32_t slurm_rc)
{
	complete_batch_script_msg_t comp_msg = {0};
	struct requeue_msg req_msg = {0};
	slurm_msg_t resp_msg;
	int rc = 0, rpc_rc;
	static time_t config_update = 0;
	static bool requeue_no_hold = false;

	if (config_update != conf->last_update) {
		char *sched_params = slurm_get_sched_params();
		requeue_no_hold = (sched_params && strstr(
					   sched_params,
					   "nohold_on_prolog_fail"));
		xfree(sched_params);
		config_update = conf->last_update;
	}

	slurm_msg_t_init(&resp_msg);

	if (slurm_rc == ESLURMD_CREDENTIAL_REVOKED) {
		comp_msg.job_id = job_id;
		comp_msg.job_rc = INFINITE;
		comp_msg.slurm_rc = slurm_rc;
		comp_msg.node_name = conf->node_name;
		comp_msg.jobacct = NULL; /* unused */
		resp_msg.msg_type = REQUEST_COMPLETE_BATCH_SCRIPT;
		resp_msg.data = &comp_msg;
	} else {
		req_msg.job_id = job_id;
		req_msg.job_id_str = NULL;
		if (requeue_no_hold) {
			req_msg.state = JOB_PENDING;
		} else {
			req_msg.state = (JOB_REQUEUE_HOLD|JOB_LAUNCH_FAILED);
		}
		resp_msg.msg_type = REQUEST_JOB_REQUEUE;
		resp_msg.data = &req_msg;
	}

	rpc_rc = slurm_send_recv_controller_rc_msg(&resp_msg, &rc);
	if ((resp_msg.msg_type == REQUEST_JOB_REQUEUE) &&
	    ((rc == ESLURM_DISABLED) || (rc == ESLURM_BATCH_ONLY))) {
		info("Could not launch job %u and not able to requeue it, "
		     "cancelling job", job_id);

		if ((slurm_rc == ESLURMD_PROLOG_FAILED) &&
		    (rc == ESLURM_BATCH_ONLY)) {
			char *buf = NULL;
			xstrfmtcat(buf, "Prolog failure on node %s",
				   conf->node_name);
			slurm_notify_job(job_id, buf);
			xfree(buf);
		}

		comp_msg.job_id = job_id;
		comp_msg.job_rc = INFINITE;
		comp_msg.slurm_rc = slurm_rc;
		comp_msg.node_name = conf->node_name;
		comp_msg.jobacct = NULL; /* unused */
		resp_msg.msg_type = REQUEST_COMPLETE_BATCH_SCRIPT;
		resp_msg.data = &comp_msg;
		rpc_rc = slurm_send_recv_controller_rc_msg(&resp_msg, &rc);
	}

	return rpc_rc;
}

static int
_abort_step(uint32_t job_id, uint32_t step_id)
{
	step_complete_msg_t resp;
	slurm_msg_t resp_msg;
	slurm_msg_t_init(&resp_msg);
	int rc, rc2;

	resp.job_id       = job_id;
	resp.job_step_id  = step_id;
	resp.range_first  = 0;
	resp.range_last   = 0;
	resp.step_rc      = 1;
	resp.jobacct      = jobacctinfo_create(NULL);
	resp_msg.msg_type = REQUEST_STEP_COMPLETE;
	resp_msg.data     = &resp;
	rc2 = slurm_send_recv_controller_rc_msg(&resp_msg, &rc);
	/* Note: we are ignoring the RPC return code */
	jobacctinfo_destroy(resp.jobacct);
	return rc2;
}

static void
_rpc_reconfig(slurm_msg_t *msg)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);

	if (!_slurm_authorized_user(req_uid))
		error("Security violation, reconfig RPC from uid %d",
		      req_uid);
	else
		kill(conf->pid, SIGHUP);
	forward_wait(msg);
	/* Never return a message, slurmctld does not expect one */
}

static void
_rpc_shutdown(slurm_msg_t *msg)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);

	forward_wait(msg);
	if (!_slurm_authorized_user(req_uid))
		error("Security violation, shutdown RPC from uid %d",
		      req_uid);
	else {
		if (kill(conf->pid, SIGTERM) != 0)
			error("kill(%u,SIGTERM): %m", conf->pid);
	}

	/* Never return a message, slurmctld does not expect one */
}

static void
_rpc_reboot(slurm_msg_t *msg)
{
	char *reboot_program, *cmd = NULL, *sp;
	reboot_msg_t *reboot_msg;
	slurm_ctl_conf_t *cfg;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);
	int exit_code;

	if (!_slurm_authorized_user(req_uid))
		error("Security violation, reboot RPC from uid %d",
		      req_uid);
	else {
		cfg = slurm_conf_lock();
		reboot_program = cfg->reboot_program;
		if (reboot_program) {
			sp = strchr(reboot_program, ' ');
			if (sp)
				sp = xstrndup(reboot_program,
					      (sp - reboot_program));
			else
				sp = xstrdup(reboot_program);
			reboot_msg = (reboot_msg_t *) msg->data;
			if (reboot_msg && reboot_msg->features) {
				/*
				 * Run reboot_program with only arguments given
				 * in reboot_msg->features.
				 */
				info("Node reboot request with features %s being processed",
				     reboot_msg->features);
				(void) node_features_g_node_set(
						reboot_msg->features);
				if (reboot_msg->features[0]) {
					xstrfmtcat(cmd, "%s %s",
						   sp, reboot_msg->features);
				} else {
					cmd = xstrdup(sp);
				}
			} else {
				/* Run reboot_program verbatim */
				cmd = xstrdup(reboot_program);
				info("Node reboot request being processed");
			}
			if (access(sp, R_OK | X_OK) < 0)
				error("Cannot run RebootProgram [%s]: %m", sp);
			else if ((exit_code = system(cmd)))
				error("system(%s) returned %d", reboot_program,
				      exit_code);
			xfree(sp);
			xfree(cmd);
		} else
			error("RebootProgram isn't defined in config");
		slurm_conf_unlock();
	}

	/* Never return a message, slurmctld does not expect one */
	/* slurm_send_rc_msg(msg, rc); */
}

static void _job_limits_free(void *x)
{
	xfree(x);
}

static int _job_limits_match(void *x, void *key)
{
	job_mem_limits_t *job_limits_ptr = (job_mem_limits_t *) x;
	uint32_t *job_id = (uint32_t *) key;
	if (job_limits_ptr->job_id == *job_id)
		return 1;
	return 0;
}

static int _step_limits_match(void *x, void *key)
{
	job_mem_limits_t *job_limits_ptr = (job_mem_limits_t *) x;
	step_loc_t *step_ptr = (step_loc_t *) key;

	if ((job_limits_ptr->job_id  == step_ptr->jobid) &&
	    (job_limits_ptr->step_id == step_ptr->stepid))
		return 1;
	return 0;
}

/* Call only with job_limits_mutex locked */
static void
_load_job_limits(void)
{
	List steps;
	ListIterator step_iter;
	step_loc_t *stepd;
	int fd;
	job_mem_limits_t *job_limits_ptr;
	slurmstepd_mem_info_t stepd_mem_info;

	if (!job_limits_list)
		job_limits_list = list_create(_job_limits_free);
	job_limits_loaded = true;

	steps = stepd_available(conf->spooldir, conf->node_name);
	step_iter = list_iterator_create(steps);
	while ((stepd = list_next(step_iter))) {
		job_limits_ptr = list_find_first(job_limits_list,
						 _step_limits_match, stepd);
		if (job_limits_ptr)	/* already processed */
			continue;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1)
			continue;	/* step completed */

		if (stepd_get_mem_limits(fd, stepd->protocol_version,
					  &stepd_mem_info) != SLURM_SUCCESS) {
			error("Error reading step %u.%u memory limits from "
			      "slurmstepd",
			      stepd->jobid, stepd->stepid);
			close(fd);
			continue;
		}


		if ((stepd_mem_info.job_mem_limit
		     || stepd_mem_info.step_mem_limit)) {
			/* create entry for this job */
			job_limits_ptr = xmalloc(sizeof(job_mem_limits_t));
			job_limits_ptr->job_id   = stepd->jobid;
			job_limits_ptr->step_id  = stepd->stepid;
			job_limits_ptr->job_mem  =
				stepd_mem_info.job_mem_limit;
			job_limits_ptr->step_mem =
				stepd_mem_info.step_mem_limit;
#if _LIMIT_INFO
			info("RecLim step:%u.%u job_mem:%"PRIu64""
			     " step_mem:%"PRIu64"",
			     job_limits_ptr->job_id, job_limits_ptr->step_id,
			     job_limits_ptr->job_mem,
			     job_limits_ptr->step_mem);
#endif
			list_append(job_limits_list, job_limits_ptr);
		}
		close(fd);
	}
	list_iterator_destroy(step_iter);
	FREE_NULL_LIST(steps);
}

static void
_cancel_step_mem_limit(uint32_t job_id, uint32_t step_id)
{
	slurm_msg_t msg;
	job_notify_msg_t notify_req;
	job_step_kill_msg_t kill_req;

	/* NOTE: Batch jobs may have no srun to get this message */
	slurm_msg_t_init(&msg);
	notify_req.job_id      = job_id;
	notify_req.job_step_id = step_id;
	notify_req.message     = "Exceeded job memory limit";
	msg.msg_type    = REQUEST_JOB_NOTIFY;
	msg.data        = &notify_req;
	slurm_send_only_controller_msg(&msg);

	memset(&kill_req, 0, sizeof(job_step_kill_msg_t));
	kill_req.job_id      = job_id;
	kill_req.job_step_id = step_id;
	kill_req.signal      = SIGKILL;
	kill_req.flags       = (uint16_t) 0;
	msg.msg_type    = REQUEST_CANCEL_JOB_STEP;
	msg.data        = &kill_req;
	slurm_send_only_controller_msg(&msg);
}

/* Enforce job memory limits here in slurmd. Step memory limits are
 * enforced within slurmstepd (using jobacct_gather plugin). */
static void
_enforce_job_mem_limit(void)
{
	List steps;
	ListIterator step_iter, job_limits_iter;
	job_mem_limits_t *job_limits_ptr;
	step_loc_t *stepd;
	int fd, i, job_inx, job_cnt;
	uint16_t vsize_factor;
	uint64_t step_rss, step_vsize;
	job_step_id_msg_t acct_req;
	job_step_stat_t *resp = NULL;
	struct job_mem_info {
		uint32_t job_id;
		uint64_t mem_limit;	/* MB */
		uint64_t mem_used;	/* MB */
		uint64_t vsize_limit;	/* MB */
		uint64_t vsize_used;	/* MB */
	};
	struct job_mem_info *job_mem_info_ptr = NULL;

	/* If users have configured MemLimitEnforce=no
	 * in their slurm.conf keep going.
	 */
	if (conf->mem_limit_enforce == false)
		return;

	slurm_mutex_lock(&job_limits_mutex);
	if (!job_limits_loaded)
		_load_job_limits();
	if (list_count(job_limits_list) == 0) {
		slurm_mutex_unlock(&job_limits_mutex);
		return;
	}

	/* Build table of job limits, use highest mem limit recorded */
	job_mem_info_ptr = xmalloc((list_count(job_limits_list) + 1) *
				   sizeof(struct job_mem_info));
	job_cnt = 0;
	job_limits_iter = list_iterator_create(job_limits_list);
	while ((job_limits_ptr = list_next(job_limits_iter))) {
		if (job_limits_ptr->job_mem == 0) 	/* no job limit */
			continue;
		for (i=0; i<job_cnt; i++) {
			if (job_mem_info_ptr[i].job_id !=
			    job_limits_ptr->job_id)
				continue;
			job_mem_info_ptr[i].mem_limit = MAX(
				job_mem_info_ptr[i].mem_limit,
				job_limits_ptr->job_mem);
			break;
		}
		if (i < job_cnt)	/* job already found & recorded */
			continue;
		job_mem_info_ptr[job_cnt].job_id    = job_limits_ptr->job_id;
		job_mem_info_ptr[job_cnt].mem_limit = job_limits_ptr->job_mem;
		job_cnt++;
	}
	list_iterator_destroy(job_limits_iter);
	slurm_mutex_unlock(&job_limits_mutex);

	vsize_factor = slurm_get_vsize_factor();
	for (i=0; i<job_cnt; i++) {
		job_mem_info_ptr[i].vsize_limit = job_mem_info_ptr[i].
			mem_limit;
		job_mem_info_ptr[i].vsize_limit *= (vsize_factor / 100.0);
	}

	steps = stepd_available(conf->spooldir, conf->node_name);
	step_iter = list_iterator_create(steps);
	while ((stepd = list_next(step_iter))) {
		for (job_inx=0; job_inx<job_cnt; job_inx++) {
			if (job_mem_info_ptr[job_inx].job_id == stepd->jobid)
				break;
		}
		if (job_inx >= job_cnt)
			continue;	/* job/step not being tracked */

		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1)
			continue;	/* step completed */
		acct_req.job_id  = stepd->jobid;
		acct_req.step_id = stepd->stepid;
		resp = xmalloc(sizeof(job_step_stat_t));

		if ((!stepd_stat_jobacct(
			     fd, stepd->protocol_version,
			     &acct_req, resp)) &&
		    (resp->jobacct)) {
			/* resp->jobacct is NULL if account is disabled */
			jobacctinfo_getinfo((struct jobacctinfo *)
					    resp->jobacct,
					    JOBACCT_DATA_TOT_RSS,
					    &step_rss,
					    stepd->protocol_version);
			jobacctinfo_getinfo((struct jobacctinfo *)
					    resp->jobacct,
					    JOBACCT_DATA_TOT_VSIZE,
					    &step_vsize,
					    stepd->protocol_version);
#if _LIMIT_INFO
			info("Step:%u.%u RSS:%"PRIu64" KB VSIZE:%"PRIu64" KB",
			     stepd->jobid, stepd->stepid,
			     step_rss, step_vsize);
#endif
			step_rss /= 1024;	/* KB to MB */
			step_rss = MAX(step_rss, 1);
			job_mem_info_ptr[job_inx].mem_used += step_rss;
			step_vsize /= 1024;	/* KB to MB */
			step_vsize = MAX(step_vsize, 1);
			job_mem_info_ptr[job_inx].vsize_used += step_vsize;
		}
		slurm_free_job_step_stat(resp);
		close(fd);
	}
	list_iterator_destroy(step_iter);
	FREE_NULL_LIST(steps);

	for (i=0; i<job_cnt; i++) {
		if (job_mem_info_ptr[i].mem_used == 0) {
			/* no steps found,
			 * purge records for all steps of this job */
			slurm_mutex_lock(&job_limits_mutex);
			list_delete_all(job_limits_list, _job_limits_match,
					&job_mem_info_ptr[i].job_id);
			slurm_mutex_unlock(&job_limits_mutex);
			break;
		}

		if ((job_mem_info_ptr[i].mem_limit != 0) &&
		    (job_mem_info_ptr[i].mem_used >
		     job_mem_info_ptr[i].mem_limit)) {
			info("Job %u exceeded memory limit "
			     "(%"PRIu64">%"PRIu64"), cancelling it",
			     job_mem_info_ptr[i].job_id,
			     job_mem_info_ptr[i].mem_used,
			     job_mem_info_ptr[i].mem_limit);
			_cancel_step_mem_limit(job_mem_info_ptr[i].job_id,
					       NO_VAL);
		} else if ((job_mem_info_ptr[i].vsize_limit != 0) &&
			   (job_mem_info_ptr[i].vsize_used >
			    job_mem_info_ptr[i].vsize_limit)) {
			info("Job %u exceeded virtual memory limit "
			     "(%"PRIu64">%"PRIu64"), cancelling it",
			     job_mem_info_ptr[i].job_id,
			     job_mem_info_ptr[i].vsize_used,
			     job_mem_info_ptr[i].vsize_limit);
			_cancel_step_mem_limit(job_mem_info_ptr[i].job_id,
					       NO_VAL);
		}
	}
	xfree(job_mem_info_ptr);
}

static int
_rpc_ping(slurm_msg_t *msg)
{
	int        rc = SLURM_SUCCESS;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);
	static bool first_msg = true;

	if (!_slurm_authorized_user(req_uid)) {
		error("Security violation, ping RPC from uid %d",
		      req_uid);
		if (first_msg) {
			error("Do you have SlurmUser configured as uid %d?",
			      req_uid);
		}
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	}
	first_msg = false;

	if (rc != SLURM_SUCCESS) {
		/* Return result. If the reply can't be sent this indicates
		 * 1. The network is broken OR
		 * 2. slurmctld has died    OR
		 * 3. slurmd was paged out due to full memory
		 * If the reply request fails, we send an registration message
		 * to slurmctld in hopes of avoiding having the node set DOWN
		 * due to slurmd paging and not being able to respond in a
		 * timely fashion. */
		if (slurm_send_rc_msg(msg, rc) < 0) {
			error("Error responding to ping: %m");
			send_registration_msg(SLURM_SUCCESS, false);
		}
	} else {
		slurm_msg_t resp_msg;
		ping_slurmd_resp_msg_t ping_resp;
		get_cpu_load(&ping_resp.cpu_load);
		get_free_mem(&ping_resp.free_mem);
		slurm_msg_t_copy(&resp_msg, msg);
		resp_msg.msg_type = RESPONSE_PING_SLURMD;
		resp_msg.data     = &ping_resp;

		slurm_send_node_msg(msg->conn_fd, &resp_msg);
	}

	/* Take this opportunity to enforce any job memory limits */
	_enforce_job_mem_limit();
	/* Clear up any stalled file transfers as well */
	_file_bcast_cleanup();
	return rc;
}

static int
_rpc_health_check(slurm_msg_t *msg)
{
	int        rc = SLURM_SUCCESS;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);

	if (!_slurm_authorized_user(req_uid)) {
		error("Security violation, health check RPC from uid %d",
		      req_uid);
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	}

	/* Return result. If the reply can't be sent this indicates that
	 * 1. The network is broken OR
	 * 2. slurmctld has died    OR
	 * 3. slurmd was paged out due to full memory
	 * If the reply request fails, we send an registration message to
	 * slurmctld in hopes of avoiding having the node set DOWN due to
	 * slurmd paging and not being able to respond in a timely fashion. */
	if (slurm_send_rc_msg(msg, rc) < 0) {
		error("Error responding to health check: %m");
		send_registration_msg(SLURM_SUCCESS, false);
	}

	if (rc == SLURM_SUCCESS)
		rc = run_script_health_check();

	/* Take this opportunity to enforce any job memory limits */
	_enforce_job_mem_limit();
	/* Clear up any stalled file transfers as well */
	_file_bcast_cleanup();
	return rc;
}


static int
_rpc_acct_gather_update(slurm_msg_t *msg)
{
	int        rc = SLURM_SUCCESS;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);
	static bool first_msg = true;

	if (!_slurm_authorized_user(req_uid)) {
		error("Security violation, acct_gather_update RPC from uid %d",
		      req_uid);
		if (first_msg) {
			error("Do you have SlurmUser configured as uid %d?",
			      req_uid);
		}
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	}
	first_msg = false;

	if (rc != SLURM_SUCCESS) {
		/* Return result. If the reply can't be sent this indicates
		 * 1. The network is broken OR
		 * 2. slurmctld has died    OR
		 * 3. slurmd was paged out due to full memory
		 * If the reply request fails, we send an registration message
		 * to slurmctld in hopes of avoiding having the node set DOWN
		 * due to slurmd paging and not being able to respond in a
		 * timely fashion. */
		if (slurm_send_rc_msg(msg, rc) < 0) {
			error("Error responding to account gather: %m");
			send_registration_msg(SLURM_SUCCESS, false);
		}
	} else {
		slurm_msg_t resp_msg;
		acct_gather_node_resp_msg_t acct_msg;

		/* Update node energy usage data */
		acct_gather_energy_g_update_node_energy();

		memset(&acct_msg, 0, sizeof(acct_gather_node_resp_msg_t));
		acct_msg.node_name = conf->node_name;
		acct_msg.sensor_cnt = 1;
		acct_msg.energy = acct_gather_energy_alloc(acct_msg.sensor_cnt);
		acct_gather_energy_g_get_data(
			ENERGY_DATA_NODE_ENERGY, acct_msg.energy);

		slurm_msg_t_copy(&resp_msg, msg);
		resp_msg.msg_type = RESPONSE_ACCT_GATHER_UPDATE;
		resp_msg.data     = &acct_msg;

		slurm_send_node_msg(msg->conn_fd, &resp_msg);

		acct_gather_energy_destroy(acct_msg.energy);
	}
	return rc;
}

static int
_rpc_acct_gather_energy(slurm_msg_t *msg)
{
	int        rc = SLURM_SUCCESS;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);
	static bool first_msg = true;

	if (!_slurm_authorized_user(req_uid)) {
		error("Security violation, acct_gather_update RPC from uid %d",
		      req_uid);
		if (first_msg) {
			error("Do you have SlurmUser configured as uid %d?",
			      req_uid);
		}
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	}
	first_msg = false;

	if (rc != SLURM_SUCCESS) {
		if (slurm_send_rc_msg(msg, rc) < 0)
			error("Error responding to energy request: %m");
	} else {
		slurm_msg_t resp_msg;
		acct_gather_node_resp_msg_t acct_msg;
		time_t now = time(NULL), last_poll = 0;
		int data_type = ENERGY_DATA_STRUCT;
		uint16_t sensor_cnt;
		acct_gather_energy_req_msg_t *req = msg->data;

		acct_gather_energy_g_get_data(ENERGY_DATA_LAST_POLL,
					      &last_poll);
		acct_gather_energy_g_get_data(ENERGY_DATA_SENSOR_CNT,
					      &sensor_cnt);

		/* If we polled later than delta seconds then force a
		   new poll.
		*/
		if ((now - last_poll) > req->delta)
			data_type = ENERGY_DATA_JOULES_TASK;

		memset(&acct_msg, 0, sizeof(acct_gather_node_resp_msg_t));
		acct_msg.sensor_cnt = sensor_cnt;
		acct_msg.energy = acct_gather_energy_alloc(acct_msg.sensor_cnt);

		acct_gather_energy_g_get_data(data_type, acct_msg.energy);

		slurm_msg_t_copy(&resp_msg, msg);
		resp_msg.msg_type = RESPONSE_ACCT_GATHER_ENERGY;
		resp_msg.data     = &acct_msg;

		slurm_send_node_msg(msg->conn_fd, &resp_msg);

		acct_gather_energy_destroy(acct_msg.energy);
	}
	return rc;
}

static int
_signal_jobstep(uint32_t jobid, uint32_t stepid, uid_t req_uid,
		uint32_t signal)
{
	int               fd, rc = SLURM_SUCCESS;
	uid_t uid;
	uint16_t protocol_version;

	/*  There will be no stepd if the prolog is still running
	 *   Return failure so caller can retry.
	 */
	if (_prolog_is_running (jobid)) {
		info ("signal %d req for %u.%u while prolog is running."
		      " Returning failure.", signal, jobid, stepid);
		return SLURM_FAILURE;
	}

	fd = stepd_connect(conf->spooldir, conf->node_name, jobid, stepid,
			   &protocol_version);
	if (fd == -1) {
		debug("signal for nonexistent %u.%u stepd_connect failed: %m",
		      jobid, stepid);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((int)(uid = stepd_get_uid(fd, protocol_version)) < 0) {
		debug("_signal_jobstep: couldn't read from the step %u.%u: %m",
		      jobid, stepid);
		rc = ESLURM_INVALID_JOB_ID;
		goto done2;
	}

	if ((req_uid != uid) && (!_slurm_authorized_user(req_uid))) {
		debug("kill req from uid %ld for job %u.%u owned by uid %ld",
		      (long) req_uid, jobid, stepid, (long) uid);
		rc = ESLURM_USER_ID_MISSING;     /* or bad in this case */
		goto done2;
	}

	rc = stepd_signal_container(fd, protocol_version, signal);
	if (rc == -1)
		rc = ESLURMD_JOB_NOTRUNNING;

done2:
	close(fd);
	return rc;
}

static void
_rpc_signal_tasks(slurm_msg_t *msg)
{
	int               rc = SLURM_SUCCESS;
	uid_t             req_uid = g_slurm_auth_get_uid(msg->auth_cred,
							 conf->auth_info);
	kill_tasks_msg_t *req = (kill_tasks_msg_t *) msg->data;
	uint32_t flag;
	uint32_t sig;

	flag = req->signal >> 24;
	sig  = req->signal & 0xfff;

	if (flag & KILL_FULL_JOB) {
		debug("%s: sending signal %u to entire job %u flag %u",
		      __func__, sig, req->job_id, flag);
		_kill_all_active_steps(req->job_id, sig, true);
	} else if (flag & KILL_STEPS_ONLY) {
		debug("%s: sending signal %u to all steps job %u flag %u",
		      __func__, sig, req->job_id, flag);
		_kill_all_active_steps(req->job_id, sig, false);
	} else {
		debug("%s: sending signal %u to step %u.%u flag %u", __func__,
		      sig, req->job_id, req->job_step_id, flag);
		rc = _signal_jobstep(req->job_id, req->job_step_id, req_uid,
				     req->signal);
	}
	slurm_send_rc_msg(msg, rc);
}

static void
_rpc_checkpoint_tasks(slurm_msg_t *msg)
{
	int               fd;
	int               rc = SLURM_SUCCESS;
	uid_t             req_uid = g_slurm_auth_get_uid(msg->auth_cred,
							 conf->auth_info);
	checkpoint_tasks_msg_t *req = (checkpoint_tasks_msg_t *) msg->data;
	uint16_t protocol_version;
	uid_t uid;

	fd = stepd_connect(conf->spooldir, conf->node_name,
			   req->job_id, req->job_step_id, &protocol_version);
	if (fd == -1) {
		debug("checkpoint for nonexistent %u.%u stepd_connect "
		      "failed: %m", req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	if ((int)(uid = stepd_get_uid(fd, protocol_version)) < 0) {
		debug("_rpc_checkpoint_tasks: couldn't read from the "
		      "step %u.%u: %m",
		      req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done2;
	}

	if ((req_uid != uid) && (!_slurm_authorized_user(req_uid))) {
		debug("checkpoint req from uid %ld for job %u.%u owned by "
		      "uid %ld", (long) req_uid, req->job_id, req->job_step_id,
		      (long) uid);
		rc = ESLURM_USER_ID_MISSING;     /* or bad in this case */
		goto done2;
	}

	rc = stepd_checkpoint(fd, protocol_version,
			      req->timestamp, req->image_dir);
	if (rc == -1)
		rc = ESLURMD_JOB_NOTRUNNING;

done2:
	close(fd);
done:
	slurm_send_rc_msg(msg, rc);
}

static void
_rpc_terminate_tasks(slurm_msg_t *msg)
{
	kill_tasks_msg_t *req = (kill_tasks_msg_t *) msg->data;
	int               rc = SLURM_SUCCESS;
	int               fd;
	uid_t             req_uid, uid;
	uint16_t protocol_version;

	debug3("Entering _rpc_terminate_tasks");
	fd = stepd_connect(conf->spooldir, conf->node_name,
			   req->job_id, req->job_step_id, &protocol_version);
	if (fd == -1) {
		debug("kill for nonexistent job %u.%u stepd_connect "
		      "failed: %m", req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	if ((int)(uid = stepd_get_uid(fd, protocol_version)) < 0) {
		debug("terminate_tasks couldn't read from the step %u.%u: %m",
		      req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done2;
	}

	req_uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);
	if ((req_uid != uid)
	    && (!_slurm_authorized_user(req_uid))) {
		debug("kill req from uid %ld for job %u.%u owned by uid %ld",
		      (long) req_uid, req->job_id, req->job_step_id,
		      (long) uid);
		rc = ESLURM_USER_ID_MISSING;     /* or bad in this case */
		goto done2;
	}

	rc = stepd_terminate(fd, protocol_version);
	if (rc == -1)
		rc = ESLURMD_JOB_NOTRUNNING;

done2:
	close(fd);
done:
	slurm_send_rc_msg(msg, rc);
}

static int
_rpc_step_complete(slurm_msg_t *msg)
{
	step_complete_msg_t *req = (step_complete_msg_t *)msg->data;
	int               rc = SLURM_SUCCESS;
	int               fd;
	uid_t             req_uid;
	uint16_t protocol_version;

	debug3("Entering _rpc_step_complete");
	fd = stepd_connect(conf->spooldir, conf->node_name,
			   req->job_id, req->job_step_id, &protocol_version);
	if (fd == -1) {
		error("stepd_connect to %u.%u failed: %m",
		      req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	/* step completion messages are only allowed from other slurmstepd,
	   so only root or SlurmUser is allowed here */
	req_uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);
	if (!_slurm_authorized_user(req_uid)) {
		debug("step completion from uid %ld for job %u.%u",
		      (long) req_uid, req->job_id, req->job_step_id);
		rc = ESLURM_USER_ID_MISSING;     /* or bad in this case */
		goto done2;
	}

	rc = stepd_completion(fd, protocol_version, req);
	if (rc == -1)
		rc = ESLURMD_JOB_NOTRUNNING;

done2:
	close(fd);
done:
	slurm_send_rc_msg(msg, rc);

	return rc;
}

static void _setup_step_complete_msg(slurm_msg_t *msg, void *data)
{
	slurm_msg_t_init(msg);
	msg->msg_type = REQUEST_STEP_COMPLETE;
	msg->data = data;
}

/* This step_complete RPC came from slurmstepd because we are using
 * message aggregation configured and we are at the head of the tree.
 * This just adds the message to the list and goes on it's merry way. */
static int
_rpc_step_complete_aggr(slurm_msg_t *msg)
{
	int rc;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);

	if (!_slurm_authorized_user(uid)) {
		error("Security violation: step_complete_aggr from uid %d",
		      uid);
		if (msg->conn_fd >= 0)
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return SLURM_ERROR;
	}

	if (conf->msg_aggr_window_msgs > 1) {
		slurm_msg_t *req = xmalloc_nz(sizeof(slurm_msg_t));
		_setup_step_complete_msg(req, msg->data);
		msg->data = NULL;

		msg_aggr_add_msg(req, 1, NULL);
	} else {
		slurm_msg_t req;
		_setup_step_complete_msg(&req, msg->data);

		while (slurm_send_recv_controller_rc_msg(&req, &rc) < 0) {
			error("Unable to send step complete, "
			      "trying again in a minute: %m");
		}
	}

	/* Finish communication with the stepd, we have to wait for
	 * the message back from the slurmctld or we will cause a race
	 * condition with srun.
	 */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);

	return SLURM_SUCCESS;
}

/* Get list of active jobs and steps, xfree returned value */
static char *
_get_step_list(void)
{
	char tmp[64];
	char *step_list = NULL;
	List steps;
	ListIterator i;
	step_loc_t *stepd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1)
			continue;

		if (stepd_state(fd, stepd->protocol_version)
		    == SLURMSTEPD_NOT_RUNNING) {
			debug("stale domain socket for stepd %u.%u ",
			      stepd->jobid, stepd->stepid);
			close(fd);
			continue;
		}
		close(fd);

		if (step_list)
			xstrcat(step_list, ", ");
		if (stepd->stepid == NO_VAL) {
			snprintf(tmp, sizeof(tmp), "%u",
				 stepd->jobid);
			xstrcat(step_list, tmp);
		} else {
			snprintf(tmp, sizeof(tmp), "%u.%u",
				 stepd->jobid, stepd->stepid);
			xstrcat(step_list, tmp);
		}
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	if (step_list == NULL)
		xstrcat(step_list, "NONE");
	return step_list;
}

static int
_rpc_daemon_status(slurm_msg_t *msg)
{
	slurm_msg_t      resp_msg;
	slurmd_status_t *resp = NULL;

	resp = xmalloc(sizeof(slurmd_status_t));
	resp->actual_cpus        = conf->actual_cpus;
	resp->actual_boards      = conf->actual_boards;
	resp->actual_sockets     = conf->actual_sockets;
	resp->actual_cores       = conf->actual_cores;
	resp->actual_threads     = conf->actual_threads;
	resp->actual_real_mem    = conf->real_memory_size;
	resp->actual_tmp_disk    = conf->tmp_disk_space;
	resp->booted             = startup;
	resp->hostname           = xstrdup(conf->node_name);
	resp->step_list          = _get_step_list();
	resp->last_slurmctld_msg = last_slurmctld_msg;
	resp->pid                = conf->pid;
	resp->slurmd_debug       = conf->debug_level;
	resp->slurmd_logfile     = xstrdup(conf->logfile);
	resp->version            = xstrdup(SLURM_VERSION_STRING);

	slurm_msg_t_copy(&resp_msg, msg);
	resp_msg.msg_type = RESPONSE_SLURMD_STATUS;
	resp_msg.data     = resp;
	slurm_send_node_msg(msg->conn_fd, &resp_msg);
	slurm_free_slurmd_status(resp);
	return SLURM_SUCCESS;
}

static int
_rpc_stat_jobacct(slurm_msg_t *msg)
{
	job_step_id_msg_t *req = (job_step_id_msg_t *)msg->data;
	slurm_msg_t        resp_msg;
	job_step_stat_t *resp = NULL;
	int fd;
	uid_t req_uid, uid;
	uint16_t protocol_version;

	debug3("Entering _rpc_stat_jobacct");
	/* step completion messages are only allowed from other slurmstepd,
	   so only root or SlurmUser is allowed here */
	req_uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);

	fd = stepd_connect(conf->spooldir, conf->node_name,
			   req->job_id, req->step_id, &protocol_version);
	if (fd == -1) {
		error("stepd_connect to %u.%u failed: %m",
		      req->job_id, req->step_id);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		return	ESLURM_INVALID_JOB_ID;
	}

	if ((int)(uid = stepd_get_uid(fd, protocol_version)) < 0) {
		debug("stat_jobacct couldn't read from the step %u.%u: %m",
		      req->job_id, req->step_id);
		close(fd);
		if (msg->conn_fd >= 0)
			slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		return	ESLURM_INVALID_JOB_ID;
	}

	/*
	 * check that requesting user ID is the SLURM UID or root
	 */
	if ((req_uid != uid) && (!_slurm_authorized_user(req_uid))) {
		error("stat_jobacct from uid %ld for job %u "
		      "owned by uid %ld",
		      (long) req_uid, req->job_id, (long) uid);

		if (msg->conn_fd >= 0) {
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
			close(fd);
			return ESLURM_USER_ID_MISSING;/* or bad in this case */
		}
	}

	resp = xmalloc(sizeof(job_step_stat_t));
	resp->step_pids = xmalloc(sizeof(job_step_pids_t));
	resp->step_pids->node_name = xstrdup(conf->node_name);
	slurm_msg_t_copy(&resp_msg, msg);
	resp->return_code = SLURM_SUCCESS;

	if (stepd_stat_jobacct(fd, protocol_version, req, resp)
	    == SLURM_ERROR) {
		debug("accounting for nonexistent job %u.%u requested",
		      req->job_id, req->step_id);
	}

	/* FIX ME: This should probably happen in the
	   stepd_stat_jobacct to get more information about the pids.
	*/
	if (stepd_list_pids(fd, protocol_version, &resp->step_pids->pid,
			    &resp->step_pids->pid_cnt) == SLURM_ERROR) {
		debug("No pids for nonexistent job %u.%u requested",
		      req->job_id, req->step_id);
	}

	close(fd);

	resp_msg.msg_type     = RESPONSE_JOB_STEP_STAT;
	resp_msg.data         = resp;

	slurm_send_node_msg(msg->conn_fd, &resp_msg);
	slurm_free_job_step_stat(resp);
	return SLURM_SUCCESS;
}

static int
_callerid_find_job(callerid_conn_t conn, uint32_t *job_id)
{
	ino_t inode;
	pid_t pid;
	int rc;

	rc = callerid_find_inode_by_conn(conn, &inode);
	if (rc != SLURM_SUCCESS) {
		debug3("network_callerid inode not found");
		return ESLURM_INVALID_JOB_ID;
	}
	debug3("network_callerid found inode %lu", (long unsigned int)inode);

	rc = find_pid_by_inode(&pid, inode);
	if (rc != SLURM_SUCCESS) {
		debug3("network_callerid process not found");
		return ESLURM_INVALID_JOB_ID;
	}
	debug3("network_callerid found process %d", (pid_t)pid);

	rc = slurm_pid2jobid(pid, job_id);
	if (rc != SLURM_SUCCESS) {
		debug3("network_callerid job not found");
		return ESLURM_INVALID_JOB_ID;
	}
	debug3("network_callerid found job %u", *job_id);
	return SLURM_SUCCESS;
}

static int
_rpc_network_callerid(slurm_msg_t *msg)
{
	network_callerid_msg_t *req = (network_callerid_msg_t *)msg->data;
	slurm_msg_t resp_msg;
	network_callerid_resp_t *resp = NULL;

	uid_t req_uid = -1;
	uid_t job_uid = -1;
	uint32_t job_id = (uint32_t)NO_VAL;
	callerid_conn_t conn;
	int rc = ESLURM_INVALID_JOB_ID;
	char ip_src_str[INET6_ADDRSTRLEN];
	char ip_dst_str[INET6_ADDRSTRLEN];

	debug3("Entering _rpc_network_callerid");

	resp = xmalloc(sizeof(network_callerid_resp_t));
	slurm_msg_t_copy(&resp_msg, msg);

	/* Ideally this would be in an if block only when debug3 is enabled */
	inet_ntop(req->af, req->ip_src, ip_src_str, INET6_ADDRSTRLEN);
	inet_ntop(req->af, req->ip_dst, ip_dst_str, INET6_ADDRSTRLEN);
	debug3("network_callerid checking %s:%u => %s:%u",
		ip_src_str, req->port_src, ip_dst_str, req->port_dst);

	/* My remote is the other's source */
	memcpy((void*)&conn.ip_dst, (void*)&req->ip_src, 16);
	memcpy((void*)&conn.ip_src, (void*)&req->ip_dst, 16);
	conn.port_src = req->port_dst;
	conn.port_dst = req->port_src;
	conn.af = req->af;

	/* Find the job id */
	rc = _callerid_find_job(conn, &job_id);
	if (rc == SLURM_SUCCESS) {
		/* We found the job */
		req_uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);
		if (!_slurm_authorized_user(req_uid)) {
			/* Requestor is not root or SlurmUser */
			job_uid = _get_job_uid(job_id);
			if (job_uid != req_uid) {
				/* RPC call sent by non-root user who does not
				 * own this job. Do not send them the job ID. */
				error("Security violation, REQUEST_NETWORK_CALLERID from uid=%d",
				      req_uid);
				job_id = NO_VAL;
				rc = ESLURM_INVALID_JOB_ID;
			}
		}
	}

	resp->job_id = job_id;
	resp->node_name = xstrdup(conf->node_name);

	resp_msg.msg_type = RESPONSE_NETWORK_CALLERID;
	resp_msg.data     = resp;

	slurm_send_node_msg(msg->conn_fd, &resp_msg);
	slurm_free_network_callerid_resp(resp);
	return rc;
}

static int
_rpc_list_pids(slurm_msg_t *msg)
{
	job_step_id_msg_t *req = (job_step_id_msg_t *)msg->data;
	slurm_msg_t        resp_msg;
	job_step_pids_t *resp = NULL;
	int fd;
	uid_t req_uid;
	uid_t job_uid;
	uint16_t protocol_version = 0;

	debug3("Entering _rpc_list_pids");
	/* step completion messages are only allowed from other slurmstepd,
	 * so only root or SlurmUser is allowed here */
	req_uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);

	job_uid = _get_job_uid(req->job_id);

	if ((int)job_uid < 0) {
		error("stat_pid for invalid job_id: %u",
		      req->job_id);
		if (msg->conn_fd >= 0)
			slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		return  ESLURM_INVALID_JOB_ID;
	}

	/*
	 * check that requesting user ID is the SLURM UID or root
	 */
	if ((req_uid != job_uid)
	    && (!_slurm_authorized_user(req_uid))) {
		error("stat_pid from uid %ld for job %u "
		      "owned by uid %ld",
		      (long) req_uid, req->job_id, (long) job_uid);

		if (msg->conn_fd >= 0) {
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
			return ESLURM_USER_ID_MISSING;/* or bad in this case */
		}
	}

	resp = xmalloc(sizeof(job_step_pids_t));
	slurm_msg_t_copy(&resp_msg, msg);
	resp->node_name = xstrdup(conf->node_name);
	resp->pid_cnt = 0;
	resp->pid = NULL;
	fd = stepd_connect(conf->spooldir, conf->node_name,
			   req->job_id, req->step_id, &protocol_version);
	if (fd == -1) {
		error("stepd_connect to %u.%u failed: %m",
		      req->job_id, req->step_id);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		slurm_free_job_step_pids(resp);
		return  ESLURM_INVALID_JOB_ID;

	}

	if (stepd_list_pids(fd, protocol_version,
			    &resp->pid, &resp->pid_cnt) == SLURM_ERROR) {
		debug("No pids for nonexistent job %u.%u requested",
		      req->job_id, req->step_id);
	}

	close(fd);

	resp_msg.msg_type = RESPONSE_JOB_STEP_PIDS;
	resp_msg.data     = resp;

	slurm_send_node_msg(msg->conn_fd, &resp_msg);
	slurm_free_job_step_pids(resp);
	return SLURM_SUCCESS;
}

/*
 *  For the specified job_id: reply to slurmctld,
 *   sleep(configured kill_wait), then send SIGKILL
 */
static void
_rpc_timelimit(slurm_msg_t *msg)
{
	uid_t           uid = g_slurm_auth_get_uid(msg->auth_cred,
						   conf->auth_info);
	kill_job_msg_t *req = msg->data;
	int             nsteps, rc;

	if (!_slurm_authorized_user(uid)) {
		error ("Security violation: rpc_timelimit req from uid %d",
		       uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	/*
	 *  Indicate to slurmctld that we've received the message
	 */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
	slurm_close(msg->conn_fd);
	msg->conn_fd = -1;

	if (req->step_id != NO_VAL) {
		slurm_ctl_conf_t *cf;
		int delay;
		/* A jobstep has timed out:
		 * - send the container a SIG_TIME_LIMIT or SIG_PREEMPTED
		 *   to log the event
		 * - send a SIGCONT to resume any suspended tasks
		 * - send a SIGTERM to begin termination
		 * - sleep KILL_WAIT
		 * - send a SIGKILL to clean up
		 */
		if (msg->msg_type == REQUEST_KILL_TIMELIMIT) {
			rc = _signal_jobstep(req->job_id, req->step_id, uid,
					     SIG_TIME_LIMIT);
		} else {
			rc = _signal_jobstep(req->job_id, req->step_id, uid,
					     SIG_PREEMPTED);
		}
		if (rc != SLURM_SUCCESS)
			return;
		rc = _signal_jobstep(req->job_id, req->step_id, uid, SIGCONT);
		if (rc != SLURM_SUCCESS)
			return;
		rc = _signal_jobstep(req->job_id, req->step_id, uid, SIGTERM);
		if (rc != SLURM_SUCCESS)
			return;
		cf = slurm_conf_lock();
		delay = MAX(cf->kill_wait, 5);
		slurm_conf_unlock();
		sleep(delay);
		_signal_jobstep(req->job_id, req->step_id, uid, SIGKILL);
		return;
	}

	if (msg->msg_type == REQUEST_KILL_TIMELIMIT)
		_kill_all_active_steps(req->job_id, SIG_TIME_LIMIT, true);
	else /* (msg->type == REQUEST_KILL_PREEMPTED) */
		_kill_all_active_steps(req->job_id, SIG_PREEMPTED, true);
	nsteps = _kill_all_active_steps(req->job_id, SIGTERM, false);
	verbose( "Job %u: timeout: sent SIGTERM to %d active steps",
		 req->job_id, nsteps );

	/* Revoke credential, send SIGKILL, run epilog, etc. */
	_rpc_terminate_job(msg);
}

static void  _rpc_pid2jid(slurm_msg_t *msg)
{
	job_id_request_msg_t *req = (job_id_request_msg_t *) msg->data;
	slurm_msg_t           resp_msg;
	job_id_response_msg_t resp;
	bool         found = false;
	List         steps;
	ListIterator i;
	step_loc_t *stepd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1)
			continue;

		if (stepd_pid_in_container(
			    fd, stepd->protocol_version,
			    req->job_pid)
		    || req->job_pid == stepd_daemon_pid(
			    fd, stepd->protocol_version)) {
			slurm_msg_t_copy(&resp_msg, msg);
			resp.job_id = stepd->jobid;
			resp.return_code = SLURM_SUCCESS;
			found = true;
			close(fd);
			break;
		}
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	if (found) {
		debug3("_rpc_pid2jid: pid(%u) found in %u",
		       req->job_pid, resp.job_id);
		resp_msg.address      = msg->address;
		resp_msg.msg_type     = RESPONSE_JOB_ID;
		resp_msg.data         = &resp;

		slurm_send_node_msg(msg->conn_fd, &resp_msg);
	} else {
		debug3("_rpc_pid2jid: pid(%u) not found", req->job_pid);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
	}
}

/* Validate sbcast credential.
 * NOTE: We can only perform the full credential validation once with
 * Munge without generating a credential replay error
 * RET SLURM_SUCCESS or an error code */
static int
_valid_sbcast_cred(file_bcast_msg_t *req, uid_t req_uid, uint16_t block_no,
		   uint32_t *job_id)
{
	int rc = SLURM_SUCCESS;
	char *nodes = NULL;
	hostset_t hset = NULL;

	*job_id = NO_VAL;
	rc = extract_sbcast_cred(conf->vctx, req->cred, block_no,
				 job_id, &nodes);
	if (rc != 0) {
		error("Security violation: Invalid sbcast_cred from uid %d",
		      req_uid);
		return ESLURMD_INVALID_JOB_CREDENTIAL;
	}

	if (!(hset = hostset_create(nodes))) {
		error("Unable to parse sbcast_cred hostlist %s", nodes);
		rc = ESLURMD_INVALID_JOB_CREDENTIAL;
	} else if (!hostset_within(hset, conf->node_name)) {
		error("Security violation: sbcast_cred from %d has "
		      "bad hostset %s", req_uid, nodes);
		rc = ESLURMD_INVALID_JOB_CREDENTIAL;
	}
	if (hset)
		hostset_destroy(hset);
	xfree(nodes);

	/* print_sbcast_cred(req->cred); */

	return rc;
}

static void _fb_rdlock(void)
{
	slurm_mutex_lock(&file_bcast_mutex);
	while (1) {
		if ((fb_write_wait_lock == 0) && (fb_write_lock == 0)) {
			fb_read_lock++;
			break;
		} else {	/* wait for state change and retry */
			slurm_cond_wait(&file_bcast_cond, &file_bcast_mutex);
		}
	}
	slurm_mutex_unlock(&file_bcast_mutex);
}

static void _fb_rdunlock(void)
{
	slurm_mutex_lock(&file_bcast_mutex);
	fb_read_lock--;
	slurm_cond_broadcast(&file_bcast_cond);
	slurm_mutex_unlock(&file_bcast_mutex);
}

static void _fb_wrlock(void)
{
	slurm_mutex_lock(&file_bcast_mutex);
	fb_write_wait_lock++;
	while (1) {
		if ((fb_read_lock == 0) && (fb_write_lock == 0)) {
			fb_write_lock++;
			fb_write_wait_lock--;
			break;
		} else {	/* wait for state change and retry */
			slurm_cond_wait(&file_bcast_cond, &file_bcast_mutex);
		}
	}
	slurm_mutex_unlock(&file_bcast_mutex);
}

static void _fb_wrunlock(void)
{
	slurm_mutex_lock(&file_bcast_mutex);
	fb_write_lock--;
	slurm_cond_broadcast(&file_bcast_cond);
	slurm_mutex_unlock(&file_bcast_mutex);
}

static int _bcast_find_in_list(void *x, void *y)
{
	file_bcast_info_t *info = (file_bcast_info_t *)x;
	file_bcast_info_t *key = (file_bcast_info_t *)y;
	/* uid, job_id, and fname must match */
	return ((info->uid == key->uid)
		&& (info->job_id == key->job_id)
		&& (!xstrcmp(info->fname, key->fname)));
}

/* must have read lock */
static file_bcast_info_t *_bcast_lookup_file(file_bcast_info_t *key)
{
	return list_find_first(file_bcast_list, _bcast_find_in_list, key);
}

/* must not have read lock, will get write lock */
static void _file_bcast_close_file(file_bcast_info_t *key)
{
	_fb_wrlock();
	list_delete_all(file_bcast_list, _bcast_find_in_list, key);
	_fb_wrunlock();
}

static void _free_file_bcast_info_t(file_bcast_info_t *f)
{
	xfree(f->fname);
	if (f->fd)
		close(f->fd);
	xfree(f);
}

static int _bcast_find_in_list_to_remove(void *x, void *y)
{
	file_bcast_info_t *f = (file_bcast_info_t *)x;
	time_t *now = (time_t *) y;

	if (f->last_update + FILE_BCAST_TIMEOUT < *now) {
		error("Removing stalled file_bcast transfer from uid "
		      "%u to file `%s`", f->uid, f->fname);
		return true;
	}

	return false;
}

/* remove transfers that have stalled */
static void _file_bcast_cleanup(void)
{
	time_t now = time(NULL);

	_fb_wrlock();
	list_delete_all(file_bcast_list, _bcast_find_in_list_to_remove, &now);
	_fb_wrunlock();
}

void file_bcast_init(void)
{
	/* skip locks during slurmd init */
	file_bcast_list = list_create((ListDelF) _free_file_bcast_info_t);
}

void file_bcast_purge(void)
{
	_fb_wrlock();
	list_destroy(file_bcast_list);
	/* destroying list before exit, no need to unlock */
}

static int _rpc_file_bcast(slurm_msg_t *msg)
{
	int rc;
	int64_t offset, inx;
	file_bcast_info_t *file_info;
	file_bcast_msg_t *req = msg->data;
	file_bcast_info_t key;

	key.uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);
	key.gid = g_slurm_auth_get_gid(msg->auth_cred, conf->auth_info);
	key.fname = req->fname;

	rc = _valid_sbcast_cred(req, key.uid, req->block_no, &key.job_id);
	if ((rc != SLURM_SUCCESS) && !_slurm_authorized_user(key.uid))
		return rc;

#if 0
	info("last_block=%u force=%u modes=%o",
	     req->last_block, req->force, req->modes);
	info("uid=%u gid=%u atime=%lu mtime=%lu block_len[0]=%u",
	     req->uid, req->gid, req->atime, req->mtime, req->block_len);
#if 0
	/* when the file being transferred is binary, the following line
	 * can break the terminal output for slurmd */
	info("req->block[0]=%s, @ %lu", \
	     req->block[0], (unsigned long) &req->block);
#endif
#endif

	if (req->block_no == 1) {
		info("sbcast req_uid=%u job_id=%u fname=%s block_no=%u",
		     key.uid, key.job_id, key.fname, req->block_no);
	} else {
		debug("sbcast req_uid=%u job_id=%u fname=%s block_no=%u",
		      key.uid, key.job_id, key.fname, req->block_no);
	}

	/* first block must register the file and open fd/mmap */
	if (req->block_no == 1) {
		if ((rc = _file_bcast_register_file(msg, &key)))
			return rc;
	}

	_fb_rdlock();
	if (!(file_info = _bcast_lookup_file(&key))) {
		error("No registered file transfer for uid %u file `%s`.",
		      key.uid, key.fname);
		_fb_rdunlock();
		return SLURM_ERROR;
	}

	/* now decompress file */
	if (bcast_decompress_data(req) < 0) {
		error("sbcast: data decompression error for UID %u, file %s",
		      key.uid, key.fname);
		_fb_rdunlock();
		return SLURM_FAILURE;
	}

	offset = 0;
	while (req->block_len - offset) {
		inx = write(file_info->fd, &req->block[offset],
			    (req->block_len - offset));
		if (inx == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("sbcast: uid:%u can't write `%s`: %m",
			      key.uid, key.fname);
			_fb_rdunlock();
			return SLURM_FAILURE;
		}
		offset += inx;
	}

	file_info->last_update = time(NULL);

	if (req->last_block && fchmod(file_info->fd, (req->modes & 0777))) {
		error("sbcast: uid:%u can't chmod `%s`: %m",
		      key.uid, key.fname);
	}
	if (req->last_block && fchown(file_info->fd, key.uid, key.gid)) {
		error("sbcast: uid:%u gid:%u can't chown `%s`: %m",
		      key.uid, key.gid, key.fname);
	}
	if (req->last_block && req->atime) {
		struct utimbuf time_buf;
		time_buf.actime  = req->atime;
		time_buf.modtime = req->mtime;
		if (utime(key.fname, &time_buf)) {
			error("sbcast: uid:%u can't utime `%s`: %m",
			      key.uid, key.fname);
		}
	}

	_fb_rdunlock();

	if (req->last_block) {
		_file_bcast_close_file(&key);
	}
	return SLURM_SUCCESS;
}

/* pass an open file descriptor back to the parent process */
static void _send_back_fd(int socket, int fd)
{
	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	char buf[CMSG_SPACE(sizeof(fd))];
	memset(buf, '\0', sizeof(buf));

	msg.msg_iov = NULL;
	msg.msg_iovlen = 0;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

	memmove(CMSG_DATA(cmsg), &fd, sizeof(fd));
	msg.msg_controllen = cmsg->cmsg_len;

	if (sendmsg(socket, &msg, 0) < 0)
		error("%s: failed to send fd: %m", __func__);
}

/* receive an open file descriptor from fork()'d child over unix socket */
static int _receive_fd(int socket)
{
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	int fd;
	msg.msg_iov = NULL;
	msg.msg_iovlen = 0;
	char c_buffer[256];
	msg.msg_control = c_buffer;
	msg.msg_controllen = sizeof(c_buffer);

	if (recvmsg(socket, &msg, 0) < 0) {
		error("%s: failed to receive fd: %m", __func__);
		return -1;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	memmove(&fd, CMSG_DATA(cmsg), sizeof(fd));
	return fd;
}

static int _file_bcast_register_file(slurm_msg_t *msg,
				     file_bcast_info_t *key)
{
	file_bcast_msg_t *req = msg->data;
	int fd, flags, rc;
	int pipe[2];
	gids_t *gids;
	pid_t child;
	file_bcast_info_t *file_info;

	if (!(gids = _gids_cache_lookup(req->user_name, key->gid))) {
		error("sbcast: gids_cache_lookup for %s failed", req->user_name);
		return SLURM_ERROR;
	}

	if ((rc = container_g_create(key->job_id))) {
		error("sbcast: container_g_create(%u): %m", key->job_id);
		_dealloc_gids(gids);
		return rc;
	}

	/* child process will setuid to the user, register the process
	 * with the container, and open the file for us. */

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pipe) != 0) {
		error("%s: Failed to open pipe: %m", __func__);
		_dealloc_gids(gids);
		return SLURM_ERROR;
	}

	child = fork();
	if (child == -1) {
		error("sbcast: fork failure");
		_dealloc_gids(gids);
		close(pipe[0]);
		close(pipe[1]);
		return errno;
	} else if (child > 0) {
		/* get fd back from pipe */
		close(pipe[0]);
		waitpid(child, &rc, 0);
		_dealloc_gids(gids);
		if (rc) {
			close(pipe[1]);
			return WEXITSTATUS(rc);
		}

		fd = _receive_fd(pipe[1]);
		close(pipe[1]);

		file_info = xmalloc(sizeof(file_bcast_info_t));
		file_info->fd = fd;
		file_info->fname = xstrdup(req->fname);
		file_info->uid = key->uid;
		file_info->gid = key->gid;
		file_info->job_id = key->job_id;
		file_info->start_time = time(NULL);

		//TODO: mmap the file here
		_fb_wrlock();
		list_append(file_bcast_list, file_info);
		_fb_wrunlock();

		return SLURM_SUCCESS;
	}

	/* child process below here */

	close(pipe[1]);

	/* container_g_add_pid needs to be called in the
	   forked process part of the fork to avoid a race
	   condition where if this process makes a file or
	   detacts itself from a child before we add the pid
	   to the container in the parent of the fork.
	*/
	if (container_g_add_pid(key->job_id, getpid(), key->uid)) {
		error("container_g_add_pid(%u): %m", key->job_id);
		exit(SLURM_ERROR);
	}

	/* The child actually performs the I/O and exits with
	 * a return code, do not return! */

	/*********************************************************************\
	 * NOTE: It would be best to do an exec() immediately after the fork()
	 * in order to help prevent a possible deadlock in the child process
	 * due to locks being set at the time of the fork and being freed by
	 * the parent process, but not freed by the child process. Performing
	 * the work inline is done for simplicity. Note that the logging
	 * performed by error() should be safe due to the use of
	 * atfork_install_handlers() as defined in src/common/log.c.
	 * Change the code below with caution.
	\*********************************************************************/

	if (setgroups(gids->ngids, gids->gids) < 0) {
		error("sbcast: uid: %u setgroups failed: %m", key->uid);
		exit(errno);
	}
	_dealloc_gids(gids);

	if (setgid(key->gid) < 0) {
		error("sbcast: uid:%u setgid(%u): %m", key->uid, key->gid);
		exit(errno);
	}
	if (setuid(key->uid) < 0) {
		error("sbcast: getuid(%u): %m", key->uid);
		exit(errno);
	}

	flags = O_WRONLY | O_CREAT;
	if (req->force)
		flags |= O_TRUNC;
	else
		flags |= O_EXCL;

	fd = open(key->fname, flags, 0700);
	if (fd == -1) {
		error("sbcast: uid:%u can't open `%s`: %m",
		      key->uid, key->fname);
		exit(errno);
	}
	_send_back_fd(pipe[0], fd);
	close(fd);
	exit(SLURM_SUCCESS);
}

static void
_rpc_reattach_tasks(slurm_msg_t *msg)
{
	reattach_tasks_request_msg_t  *req = msg->data;
	reattach_tasks_response_msg_t *resp =
		xmalloc(sizeof(reattach_tasks_response_msg_t));
	slurm_msg_t                    resp_msg;
	int          rc   = SLURM_SUCCESS;
	uint16_t     port = 0;
	char         host[MAXHOSTNAMELEN];
	slurm_addr_t   ioaddr;
	void        *job_cred_sig;
	uint32_t     len;
	int               fd;
	uid_t             req_uid;
	slurm_addr_t *cli = &msg->orig_addr;
	uint32_t nodeid = (uint32_t)NO_VAL;
	uid_t uid = -1;
	uint16_t protocol_version;

	slurm_msg_t_copy(&resp_msg, msg);
	fd = stepd_connect(conf->spooldir, conf->node_name,
			   req->job_id, req->job_step_id, &protocol_version);
	if (fd == -1) {
		debug("reattach for nonexistent job %u.%u stepd_connect"
		      " failed: %m", req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	if ((int)(uid = stepd_get_uid(fd, protocol_version)) < 0) {
		debug("_rpc_reattach_tasks couldn't read from the "
		      "step %u.%u: %m",
		      req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done2;
	}

	nodeid = stepd_get_nodeid(fd, protocol_version);

	debug2("_rpc_reattach_tasks: nodeid %d in the job step", nodeid);

	req_uid = g_slurm_auth_get_uid(msg->auth_cred, conf->auth_info);
	if ((req_uid != uid) && (!_slurm_authorized_user(req_uid))) {
		error("uid %ld attempt to attach to job %u.%u owned by %ld",
		      (long) req_uid, req->job_id, req->job_step_id,
		      (long) uid);
		rc = EPERM;
		goto done2;
	}

	memset(resp, 0, sizeof(reattach_tasks_response_msg_t));
	slurm_get_ip_str(cli, &port, host, sizeof(host));

	/*
	 * Set response address by resp_port and client address
	 */
	memcpy(&resp_msg.address, cli, sizeof(slurm_addr_t));
	if (req->num_resp_port > 0) {
		port = req->resp_port[nodeid % req->num_resp_port];
		slurm_set_addr(&resp_msg.address, port, NULL);
	}

	/*
	 * Set IO address by io_port and client address
	 */
	memcpy(&ioaddr, cli, sizeof(slurm_addr_t));

	if (req->num_io_port > 0) {
		port = req->io_port[nodeid % req->num_io_port];
		slurm_set_addr(&ioaddr, port, NULL);
	}

	/*
	 * Get the signature of the job credential.  slurmstepd will need
	 * this to prove its identity when it connects back to srun.
	 */
	slurm_cred_get_signature(req->cred, (char **)(&job_cred_sig), &len);
	if (len != SLURM_IO_KEY_SIZE) {
		error("Incorrect slurm cred signature length");
		goto done2;
	}

	resp->gtids = NULL;
	resp->local_pids = NULL;

	 /* NOTE: We need to use the protocol_version from
	  * sattach here since responses will be sent back to it. */
	if (msg->protocol_version < protocol_version)
		protocol_version = msg->protocol_version;

	/* Following call fills in gtids and local_pids when successful. */
	rc = stepd_attach(fd, protocol_version, &ioaddr,
			  &resp_msg.address, job_cred_sig, resp);
	if (rc != SLURM_SUCCESS) {
		debug2("stepd_attach call failed");
		goto done2;
	}

done2:
	close(fd);
done:
	debug2("update step addrs rc = %d", rc);
	resp_msg.data         = resp;
	resp_msg.msg_type     = RESPONSE_REATTACH_TASKS;
	resp->node_name       = xstrdup(conf->node_name);
	resp->return_code     = rc;
	debug2("node %s sending rc = %d", conf->node_name, rc);

	slurm_send_node_msg(msg->conn_fd, &resp_msg);
	slurm_free_reattach_tasks_response_msg(resp);
}

static uid_t _get_job_uid(uint32_t jobid)
{
	List steps;
	ListIterator i;
	step_loc_t *stepd;
	uid_t uid = -1;
	int fd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->jobid != jobid) {
			/* multiple jobs expected on shared nodes */
			continue;
		}
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to step %u.%u",
			       stepd->jobid, stepd->stepid);
			continue;
		}
		uid = stepd_get_uid(fd, stepd->protocol_version);

		close(fd);
		if ((int)uid < 0) {
			debug("stepd_get_uid failed %u.%u: %m",
			      stepd->jobid, stepd->stepid);
			continue;
		}
		break;
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	return uid;
}

/*
 * _kill_all_active_steps - signals the container of all steps of a job
 * jobid IN - id of job to signal
 * sig   IN - signal to send
 * batch IN - if true signal batch script, otherwise skip it
 * RET count of signaled job steps (plus batch script, if applicable)
 */
static int
_kill_all_active_steps(uint32_t jobid, int sig, bool batch)
{
	List steps;
	ListIterator i;
	step_loc_t *stepd;
	int step_cnt  = 0;
	int fd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->jobid != jobid) {
			/* multiple jobs expected on shared nodes */
			debug3("Step from other job: jobid=%u (this jobid=%u)",
			       stepd->jobid, jobid);
			continue;
		}

		if ((stepd->stepid == SLURM_BATCH_SCRIPT) && (!batch))
			continue;

		step_cnt++;

		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to step %u.%u",
			       stepd->jobid, stepd->stepid);
			continue;
		}

		debug2("container signal %d to job %u.%u",
		       sig, stepd->jobid, stepd->stepid);
		if (stepd_signal_container(
			    fd, stepd->protocol_version, sig) < 0)
			debug("kill jobid=%u failed: %m", stepd->jobid);
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);
	if (step_cnt == 0)
		debug2("No steps in jobid %u to send signal %d", jobid, sig);
	return step_cnt;
}

/*
 * ume_notify - Notify all jobs and steps on this node that a Uncorrectable
 *	Memory Error (UME) has occured by sending SIG_UME (to log event in
 *	stderr)
 * RET count of signaled job steps
 */
extern int ume_notify(void)
{
	List steps;
	ListIterator i;
	step_loc_t *stepd;
	int step_cnt  = 0;
	int fd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		step_cnt++;

		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to step %u.%u",
			       stepd->jobid, stepd->stepid);
			continue;
		}

		debug2("container SIG_UME to job %u.%u",
		       stepd->jobid, stepd->stepid);
		if (stepd_signal_container(
			    fd, stepd->protocol_version, SIG_UME) < 0)
			debug("kill jobid=%u failed: %m", stepd->jobid);
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	if (step_cnt == 0)
		debug2("No steps to send SIG_UME");
	return step_cnt;
}
/*
 * _terminate_all_steps - signals the container of all steps of a job
 * jobid IN - id of job to signal
 * batch IN - if true signal batch script, otherwise skip it
 * RET count of signaled job steps (plus batch script, if applicable)
 */
static int
_terminate_all_steps(uint32_t jobid, bool batch)
{
	List steps;
	ListIterator i;
	step_loc_t *stepd;
	int step_cnt  = 0;
	int fd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->jobid != jobid) {
			/* multiple jobs expected on shared nodes */
			debug3("Step from other job: jobid=%u (this jobid=%u)",
			       stepd->jobid, jobid);
			continue;
		}

		if ((stepd->stepid == SLURM_BATCH_SCRIPT) && (!batch))
			continue;

		step_cnt++;

		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to step %u.%u",
			       stepd->jobid, stepd->stepid);
			continue;
		}

		debug2("terminate job step %u.%u", jobid, stepd->stepid);
		if (stepd_terminate(fd, stepd->protocol_version) < 0)
			debug("kill jobid=%u.%u failed: %m", jobid,
			      stepd->stepid);
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);
	if (step_cnt == 0)
		debug2("No steps in job %u to terminate", jobid);
	return step_cnt;
}

static bool
_job_still_running(uint32_t job_id)
{
	bool         retval = false;
	List         steps;
	ListIterator i;
	step_loc_t  *s     = NULL;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((s = list_next(i))) {
		if (s->jobid == job_id) {
			int fd;
			fd = stepd_connect(s->directory, s->nodename,
					   s->jobid, s->stepid,
					   &s->protocol_version);
			if (fd == -1)
				continue;

			if (stepd_state(fd, s->protocol_version)
			    != SLURMSTEPD_NOT_RUNNING) {
				retval = true;
				close(fd);
				break;
			}
			close(fd);
		}
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	return retval;
}

/*
 * Wait until all job steps are in SLURMSTEPD_NOT_RUNNING state.
 * This indicates that switch_g_job_postfini has completed and
 * freed the switch windows (as needed only for Federation switch).
 */
static void
_wait_state_completed(uint32_t jobid, int max_delay)
{
	int i;

	for (i=0; i<max_delay; i++) {
		if (_steps_completed_now(jobid))
			break;
		sleep(1);
	}
	if (i >= max_delay)
		error("timed out waiting for job %u to complete", jobid);
}

static bool
_steps_completed_now(uint32_t jobid)
{
	List steps;
	ListIterator i;
	step_loc_t *stepd;
	bool rc = true;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->jobid == jobid) {
			int fd;
			fd = stepd_connect(stepd->directory, stepd->nodename,
					   stepd->jobid, stepd->stepid,
					   &stepd->protocol_version);
			if (fd == -1)
				continue;

			if (stepd_state(fd, stepd->protocol_version)
			    != SLURMSTEPD_NOT_RUNNING) {
				rc = false;
				close(fd);
				break;
			}
			close(fd);
		}
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	return rc;
}

static void _epilog_complete_msg_setup(
	slurm_msg_t *msg, epilog_complete_msg_t *req, uint32_t jobid, int rc)
{
	slurm_msg_t_init(msg);
	memset(req, 0, sizeof(epilog_complete_msg_t));

	req->job_id      = jobid;
	req->return_code = rc;
	req->node_name   = conf->node_name;

	msg->msg_type    = MESSAGE_EPILOG_COMPLETE;
	msg->data        = req;
}

/*
 *  Send epilog complete message to currently active controller.
 *  If enabled, use message aggregation.
 *   Returns SLURM_SUCCESS if message sent successfully,
 *           SLURM_FAILURE if epilog complete message fails to be sent.
 */
static int
_epilog_complete(uint32_t jobid, int rc)
{
	int ret = SLURM_SUCCESS;

	if (conf->msg_aggr_window_msgs > 1) {
		/* message aggregation is enabled */
		slurm_msg_t *msg = xmalloc(sizeof(slurm_msg_t));
		epilog_complete_msg_t *req =
			xmalloc(sizeof(epilog_complete_msg_t));

		_epilog_complete_msg_setup(msg, req, jobid, rc);

		/* we need to copy this symbol */
		req->node_name   = xstrdup(conf->node_name);

		msg_aggr_add_msg(msg, 0, NULL);
	} else {
		slurm_msg_t msg;
		epilog_complete_msg_t req;

		_epilog_complete_msg_setup(&msg, &req, jobid, rc);

		/* Note: No return code to message, slurmctld will resend
		 * TERMINATE_JOB request if message send fails */
		if (slurm_send_only_controller_msg(&msg) < 0) {
			error("Unable to send epilog complete message: %m");
			ret = SLURM_ERROR;
		} else {
			debug("Job %u: sent epilog complete msg: rc = %d",
			      jobid, rc);
		}
	}
	return ret;
}


/*
 * Send a signal through the appropriate slurmstepds for each job step
 * belonging to a given job allocation.
 */
static void
_rpc_signal_job(slurm_msg_t *msg)
{
	signal_job_msg_t *req = msg->data;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);
	uid_t job_uid;
	List steps;
	ListIterator i;
	step_loc_t *stepd = NULL;
	int step_cnt  = 0;
	int fd;

	debug("_rpc_signal_job, uid = %d, signal = %d", req_uid, req->signal);
	job_uid = _get_job_uid(req->job_id);
	if ((int)job_uid < 0)
		goto no_job;

	/*
	 * check that requesting user ID is the SLURM UID or root
	 */
	if ((req_uid != job_uid) && (!_slurm_authorized_user(req_uid))) {
		error("Security violation: kill_job(%u) from uid %d",
		      req->job_id, req_uid);
		if (msg->conn_fd >= 0) {
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
			if (slurm_close(msg->conn_fd) < 0)
				error ("_rpc_signal_job: close(%d): %m",
				       msg->conn_fd);
			msg->conn_fd = -1;
		}
		return;
	}

	/*
	 * Loop through all job steps for this job and signal the
	 * step's process group through the slurmstepd.
	 */
	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->jobid != req->job_id) {
			/* multiple jobs expected on shared nodes */
			debug3("Step from other job: jobid=%u (this jobid=%u)",
			       stepd->jobid, req->job_id);
			continue;
		}

		if (stepd->stepid == SLURM_BATCH_SCRIPT) {
			debug2("batch script itself not signalled");
			continue;
		}

		step_cnt++;

		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to step %u.%u",
			       stepd->jobid, stepd->stepid);
			continue;
		}

		debug2("  signal %d to job %u.%u",
		       req->signal, stepd->jobid, stepd->stepid);
		if (stepd_signal_container(
			    fd, stepd->protocol_version, req->signal) < 0)
			debug("signal jobid=%u failed: %m", stepd->jobid);
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

no_job:
	if (step_cnt == 0) {
		debug2("No steps in jobid %u to send signal %d",
		       req->job_id, req->signal);
	}

	/*
	 *  At this point, if connection still open, we send controller
	 *   a "success" reply to indicate that we've recvd the msg.
	 */
	if (msg->conn_fd >= 0) {
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		if (slurm_close(msg->conn_fd) < 0)
			error ("_rpc_signal_job: close(%d): %m", msg->conn_fd);
		msg->conn_fd = -1;
	}
}

/* if a lock is granted to the job then return 1; else return 0 if
 * the lock for the job is already taken or there's no more locks */
static int
_get_suspend_job_lock(uint32_t job_id)
{
	static bool logged = false;
	int i, empty_loc = -1, rc = 0;

	slurm_mutex_lock(&suspend_mutex);
	for (i = 0; i < job_suspend_size; i++) {
		if (job_suspend_array[i] == 0) {
			empty_loc = i;
			continue;
		}
		if (job_suspend_array[i] == job_id) {
			/* another thread already a lock for this job ID */
			slurm_mutex_unlock(&suspend_mutex);
			return rc;
		}
	}

	if (empty_loc != -1) {
		/* nobody has the lock and here's an available used lock */
		job_suspend_array[empty_loc] = job_id;
		rc = 1;
	} else if (job_suspend_size < NUM_PARALLEL_SUSP_JOBS) {
		/* a new lock is available */
		job_suspend_array[job_suspend_size++] = job_id;
		rc = 1;
	} else if (!logged) {
		error("Simultaneous job suspend/resume limit reached (%d). "
		      "Configure SchedulerTimeSlice higher.",
		      NUM_PARALLEL_SUSP_JOBS);
		logged = true;
	}
	slurm_mutex_unlock(&suspend_mutex);
	return rc;
}

static void
_unlock_suspend_job(uint32_t job_id)
{
	int i;
	slurm_mutex_lock(&suspend_mutex);
	for (i = 0; i < job_suspend_size; i++) {
		if (job_suspend_array[i] == job_id)
			job_suspend_array[i] = 0;
	}
	slurm_mutex_unlock(&suspend_mutex);
}

/* Add record for every launched job so we know they are ready for suspend */
extern void record_launched_jobs(void)
{
	List steps;
	ListIterator i;
	step_loc_t *stepd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		_launch_complete_add(stepd->jobid);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);
}

/*
 * Send a job suspend/resume request through the appropriate slurmstepds for
 * each job step belonging to a given job allocation.
 */
static void
_rpc_suspend_job(slurm_msg_t *msg)
{
	int time_slice = -1;
	suspend_int_msg_t *req = msg->data;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);
	List steps;
	ListIterator i;
	step_loc_t *stepd;
	int step_cnt  = 0;
	int rc = SLURM_SUCCESS;
	DEF_TIMERS;

	if (time_slice == -1)
		time_slice = slurm_get_time_slice();
	if ((req->op != SUSPEND_JOB) && (req->op != RESUME_JOB)) {
		error("REQUEST_SUSPEND_INT: bad op code %u", req->op);
		rc = ESLURM_NOT_SUPPORTED;
	}

	/*
	 * check that requesting user ID is the SLURM UID or root
	 */
	if (!_slurm_authorized_user(req_uid)) {
		error("Security violation: suspend_job(%u) from uid %d",
		      req->job_id, req_uid);
		rc =  ESLURM_USER_ID_MISSING;
	}

	/* send a response now, which will include any errors
	 * detected with the request */
	if (msg->conn_fd >= 0) {
		slurm_send_rc_msg(msg, rc);
		if (slurm_close(msg->conn_fd) < 0)
			error("_rpc_suspend_job: close(%d): %m",
			      msg->conn_fd);
		msg->conn_fd = -1;
	}
	if (rc != SLURM_SUCCESS)
		return;

	/* now we can focus on performing the requested action,
	 * which could take a few seconds to complete */
	debug("_rpc_suspend_job jobid=%u uid=%d action=%s", req->job_id,
	      req_uid, req->op == SUSPEND_JOB ? "suspend" : "resume");

	/* Try to get a thread lock for this job. If the lock
	 * is not available then sleep and try again */
	while (!_get_suspend_job_lock(req->job_id)) {
		debug3("suspend lock sleep for %u", req->job_id);
		usleep(10000);
	}
	START_TIMER;

	/* Defer suspend until job prolog and launch complete */
	if (req->op == SUSPEND_JOB)
		_launch_complete_wait(req->job_id);

	if ((req->op == SUSPEND_JOB) && (req->indf_susp))
		switch_g_job_suspend(req->switch_info, 5);

	/* Release or reclaim resources bound to these tasks (task affinity) */
	if (req->op == SUSPEND_JOB) {
		(void) task_g_slurmd_suspend_job(req->job_id);
	} else {
		(void) task_g_slurmd_resume_job(req->job_id);
	}

	/*
	 * Loop through all job steps and call stepd_suspend or stepd_resume
	 * as appropriate. Since the "suspend" action may contains a sleep
	 * (if the launch is in progress) suspend multiple jobsteps in parallel.
	 */
	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);

	while (1) {
		int x, fdi, fd[NUM_PARALLEL_SUSP_STEPS];
		uint16_t protocol_version[NUM_PARALLEL_SUSP_STEPS];

		fdi = 0;
		while ((stepd = list_next(i))) {
			if (stepd->jobid != req->job_id) {
				/* multiple jobs expected on shared nodes */
				debug3("Step from other job: jobid=%u "
				       "(this jobid=%u)",
				       stepd->jobid, req->job_id);
				continue;
			}
			step_cnt++;

			fd[fdi] = stepd_connect(stepd->directory,
						stepd->nodename, stepd->jobid,
						stepd->stepid,
						&protocol_version[fdi]);
			if (fd[fdi] == -1) {
				debug3("Unable to connect to step %u.%u",
				       stepd->jobid, stepd->stepid);
				continue;
			}

			fdi++;
			if (fdi >= NUM_PARALLEL_SUSP_STEPS)
				break;
		}
		/* check for open connections */
		if (fdi == 0)
			break;

		if (req->op == SUSPEND_JOB) {
			int susp_fail_count = 0;
			/* The suspend RPCs are processed in parallel for
			 * every step in the job */
			for (x = 0; x < fdi; x++) {
				(void) stepd_suspend(fd[x],
						     protocol_version[x],
						     req, 0);
			}
			for (x = 0; x < fdi; x++) {
				if (stepd_suspend(fd[x],
						  protocol_version[x],
						  req, 1) < 0) {
					susp_fail_count++;
				} else {
					close(fd[x]);
					fd[x] = -1;
				}
			}
			/* Suspend RPCs can fail at step startup, so retry */
			if (susp_fail_count) {
				sleep(1);
				for (x = 0; x < fdi; x++) {
					if (fd[x] == -1)
						continue;
					(void) stepd_suspend(
						fd[x],
						protocol_version[x],
						req, 0);
					if (stepd_suspend(
						    fd[x],
						    protocol_version[x],
						    req, 1) >= 0)
						continue;
					debug("Suspend of job %u failed: %m",
					      req->job_id);
				}
			}
		} else {
			/* The resume RPCs are processed in parallel for
			 * every step in the job */
			for (x = 0; x < fdi; x++) {
				(void) stepd_resume(fd[x],
						    protocol_version[x],
						    req, 0);
			}
			for (x = 0; x < fdi; x++) {
				if (stepd_resume(fd[x],
						 protocol_version[x],
						 req, 1) < 0) {
					debug("Resume of job %u failed: %m",
					      req->job_id);
				}
			}
		}
		for (x = 0; x < fdi; x++) {
			/* fd may have been closed by stepd_suspend */
			if (fd[x] != -1)
				close(fd[x]);
		}

		/* check for no more jobs */
		if (fdi < NUM_PARALLEL_SUSP_STEPS)
			break;
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	if ((req->op == RESUME_JOB) && (req->indf_susp))
		switch_g_job_resume(req->switch_info, 5);

	_unlock_suspend_job(req->job_id);

	END_TIMER;
	if (DELTA_TIMER >= (time_slice * 1000000)) {
		if (req->op == SUSPEND_JOB) {
			info("Suspend time for job_id %u was %s. "
			     "Configure SchedulerTimeSlice higher.",
			     req->job_id, TIME_STR);
		} else {
			info("Resume time for job_id %u was %s. "
			     "Configure SchedulerTimeSlice higher.",
			     req->job_id, TIME_STR);
		}
	}

	if (step_cnt == 0) {
		debug2("No steps in jobid %u to suspend/resume", req->job_id);
	}
}

/* Job shouldn't even be running here, abort it immediately */
static void
_rpc_abort_job(slurm_msg_t *msg)
{
	kill_job_msg_t *req    = msg->data;
	uid_t           uid    = g_slurm_auth_get_uid(msg->auth_cred,
						      conf->auth_info);
	job_env_t       job_env;

	debug("_rpc_abort_job, uid = %d", uid);
	/*
	 * check that requesting user ID is the SLURM UID
	 */
	if (!_slurm_authorized_user(uid)) {
		error("Security violation: abort_job(%u) from uid %d",
		      req->job_id, uid);
		if (msg->conn_fd >= 0)
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	task_g_slurmd_release_resources(req->job_id);

	/*
	 * "revoke" all future credentials for this jobid
	 */
	if (slurm_cred_revoke(conf->vctx, req->job_id, req->time,
			      req->start_time) < 0) {
		debug("revoking cred for job %u: %m", req->job_id);
	} else {
		save_cred_state(conf->vctx);
		debug("credential for job %u revoked", req->job_id);
	}

	/*
	 *  At this point, if connection still open, we send controller
	 *   a "success" reply to indicate that we've recvd the msg.
	 */
	if (msg->conn_fd >= 0) {
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		if (slurm_close(msg->conn_fd) < 0)
			error ("rpc_abort_job: close(%d): %m", msg->conn_fd);
		msg->conn_fd = -1;
	}

	if (_kill_all_active_steps(req->job_id, SIG_ABORT, true)) {
		/*
		 *  Block until all user processes are complete.
		 */
		_pause_for_job_completion (req->job_id, req->nodes, 0);
	}

	/*
	 *  Begin expiration period for cached information about job.
	 *   If expiration period has already begun, then do not run
	 *   the epilog again, as that script has already been executed
	 *   for this job.
	 */
	if (slurm_cred_begin_expiration(conf->vctx, req->job_id) < 0) {
		debug("Not running epilog for jobid %d: %m", req->job_id);
		return;
	}

	save_cred_state(conf->vctx);

	memset(&job_env, 0, sizeof(job_env_t));

	job_env.jobid = req->job_id;
	job_env.node_list = req->nodes;
	job_env.spank_job_env = req->spank_job_env;
	job_env.spank_job_env_size = req->spank_job_env_size;
	job_env.uid = req->job_uid;

#if defined(HAVE_BG)
	select_g_select_jobinfo_get(req->select_jobinfo,
				    SELECT_JOBDATA_BLOCK_ID,
				    &job_env.resv_id);
#elif defined(HAVE_ALPS_CRAY)
	job_env.resv_id = select_g_select_jobinfo_xstrdup(req->select_jobinfo,
							  SELECT_PRINT_RESV_ID);
#endif

	_run_epilog(&job_env);

	if (container_g_delete(req->job_id))
		error("container_g_delete(%u): %m", req->job_id);
	_launch_complete_rm(req->job_id);

	xfree(job_env.resv_id);
}

/* This is a variant of _rpc_terminate_job for use with select/serial */
static void
_rpc_terminate_batch_job(uint32_t job_id, uint32_t user_id, char *node_name)
{
	int             rc     = SLURM_SUCCESS;
	int             nsteps = 0;
	int		delay;
	time_t		now = time(NULL);
	slurm_ctl_conf_t *cf;
	job_env_t job_env;

	task_g_slurmd_release_resources(job_id);

	if (_waiter_init(job_id) == SLURM_ERROR)
		return;

	/*
	 * "revoke" all future credentials for this jobid
	 */
	_note_batch_job_finished(job_id);
	if (slurm_cred_revoke(conf->vctx, job_id, now, now) < 0) {
		debug("revoking cred for job %u: %m", job_id);
	} else {
		save_cred_state(conf->vctx);
		debug("credential for job %u revoked", job_id);
	}

	/*
	 * Tasks might be stopped (possibly by a debugger)
	 * so send SIGCONT first.
	 */
	_kill_all_active_steps(job_id, SIGCONT, true);
	if (errno == ESLURMD_STEP_SUSPENDED) {
		/*
		 * If the job step is currently suspended, we don't
		 * bother with a "nice" termination.
		 */
		debug2("Job is currently suspended, terminating");
		nsteps = _terminate_all_steps(job_id, true);
	} else {
		nsteps = _kill_all_active_steps(job_id, SIGTERM, true);
	}

	if ((nsteps == 0) && !conf->epilog) {
		slurm_cred_begin_expiration(conf->vctx, job_id);
		save_cred_state(conf->vctx);
		_waiter_complete(job_id);
		if (container_g_delete(job_id))
			error("container_g_delete(%u): %m", job_id);
		_launch_complete_rm(job_id);
		return;
	}

	/*
	 *  Check for corpses
	 */
	cf = slurm_conf_lock();
	delay = MAX(cf->kill_wait, 5);
	slurm_conf_unlock();
	if (!_pause_for_job_completion(job_id, NULL, delay) &&
	     _terminate_all_steps(job_id, true) ) {
		/*
		 *  Block until all user processes are complete.
		 */
		_pause_for_job_completion(job_id, NULL, 0);
	}

	/*
	 *  Begin expiration period for cached information about job.
	 *   If expiration period has already begun, then do not run
	 *   the epilog again, as that script has already been executed
	 *   for this job.
	 */
	if (slurm_cred_begin_expiration(conf->vctx, job_id) < 0) {
		debug("Not running epilog for jobid %d: %m", job_id);
		goto done;
	}

	save_cred_state(conf->vctx);

	memset(&job_env, 0, sizeof(job_env_t));

	job_env.jobid = job_id;
	job_env.node_list = node_name;
	job_env.uid = (uid_t)user_id;
	/* NOTE: We lack the job's SPANK environment variables */
	rc = _run_epilog(&job_env);
	if (rc) {
		int term_sig, exit_status;
		if (WIFSIGNALED(rc)) {
			exit_status = 0;
			term_sig    = WTERMSIG(rc);
		} else {
			exit_status = WEXITSTATUS(rc);
			term_sig    = 0;
		}
		error("[job %u] epilog failed status=%d:%d",
		      job_id, exit_status, term_sig);
	} else
		debug("completed epilog for jobid %u", job_id);
	if (container_g_delete(job_id))
		error("container_g_delete(%u): %m", job_id);
	_launch_complete_rm(job_id);

    done:
	_wait_state_completed(job_id, 5);
	_waiter_complete(job_id);
}

static void _handle_old_batch_job_launch(slurm_msg_t *msg)
{
	if (msg->msg_type != REQUEST_BATCH_JOB_LAUNCH) {
		error("_handle_batch_job_launch: "
		      "Invalid response msg_type (%u)", msg->msg_type);
		return;
	}

	/* (resp_msg.msg_type == REQUEST_BATCH_JOB_LAUNCH) */
	debug2("Processing RPC: REQUEST_BATCH_JOB_LAUNCH");
	last_slurmctld_msg = time(NULL);
	_rpc_batch_job(msg, false);
	slurm_free_job_launch_msg(msg->data);
	msg->data = NULL;

}

/* This complete batch RPC came from slurmstepd because we have select/serial
 * configured. Terminate the job here. Forward the batch completion RPC to
 * slurmctld and possible get a new batch launch RPC in response. */
static void
_rpc_complete_batch(slurm_msg_t *msg)
{
	int		i, rc, msg_rc;
	slurm_msg_t	resp_msg;
	uid_t           uid    = g_slurm_auth_get_uid(msg->auth_cred,
						      conf->auth_info);
	complete_batch_script_msg_t *req = msg->data;
	static int	running_serial = -1;
	uint16_t msg_type;

	if (running_serial == -1) {
		char *select_type = slurm_get_select_type();
		if (!xstrcmp(select_type, "select/serial"))
			running_serial = 1;
		else
			running_serial = 0;
		xfree(select_type);
	}

	if (!_slurm_authorized_user(uid)) {
		error("Security violation: complete_batch(%u) from uid %d",
		      req->job_id, uid);
		if (msg->conn_fd >= 0)
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);

	if (running_serial) {
		_rpc_terminate_batch_job(
			req->job_id, req->user_id, req->node_name);
		msg_type = REQUEST_COMPLETE_BATCH_JOB;
	} else
		msg_type = msg->msg_type;

	for (i = 0; i <= MAX_RETRY; i++) {
		if (conf->msg_aggr_window_msgs > 1) {
			slurm_msg_t *req_msg =
				xmalloc_nz(sizeof(slurm_msg_t));
			slurm_msg_t_init(req_msg);
			req_msg->msg_type = msg_type;
			req_msg->data = msg->data;
			msg->data = NULL;

			msg_aggr_add_msg(req_msg, 1,
					 _handle_old_batch_job_launch);
			return;
		} else {
			slurm_msg_t req_msg;
			slurm_msg_t_init(&req_msg);
			req_msg.msg_type = msg_type;
			req_msg.data	 = msg->data;
			msg_rc = slurm_send_recv_controller_msg(
				&req_msg, &resp_msg);

			if (msg_rc == SLURM_SUCCESS)
				break;
		}
		info("Retrying job complete RPC for job %u", req->job_id);
		sleep(RETRY_DELAY);
	}
	if (i > MAX_RETRY) {
		error("Unable to send job complete message: %m");
		return;
	}

	if (resp_msg.msg_type == RESPONSE_SLURM_RC) {
		last_slurmctld_msg = time(NULL);
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc) {
			error("complete_batch for job %u: %s", req->job_id,
			      slurm_strerror(rc));
		}
		return;
	}

	_handle_old_batch_job_launch(&resp_msg);
}

static void
_rpc_terminate_job(slurm_msg_t *msg)
{
	bool		have_spank = false;
	int             rc     = SLURM_SUCCESS;
	kill_job_msg_t *req    = msg->data;
	uid_t           uid    = g_slurm_auth_get_uid(msg->auth_cred,
						      conf->auth_info);
	int             nsteps = 0;
	int		delay;
//	slurm_ctl_conf_t *cf;
//	struct stat	stat_buf;
	job_env_t       job_env;

	debug("_rpc_terminate_job, uid = %d", uid);
	/*
	 * check that requesting user ID is the SLURM UID
	 */
	if (!_slurm_authorized_user(uid)) {
		error("Security violation: kill_job(%u) from uid %d",
		      req->job_id, uid);
		if (msg->conn_fd >= 0)
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	task_g_slurmd_release_resources(req->job_id);

	/*
	 *  Initialize a "waiter" thread for this jobid. If another
	 *   thread is already waiting on termination of this job,
	 *   _waiter_init() will return SLURM_ERROR. In this case, just
	 *   notify slurmctld that we recvd the message successfully,
	 *   then exit this thread.
	 */
	if (_waiter_init(req->job_id) == SLURM_ERROR) {
		if (msg->conn_fd >= 0) {
			/* No matter if the step hasn't started yet or
			 * not just send a success to let the
			 * controller know we got this request.
			 */
			slurm_send_rc_msg (msg, SLURM_SUCCESS);
		}
		return;
	}

	/*
	 * Note the job is finishing to avoid a race condition for batch jobs
	 * that finish before the slurmd knows it finished launching.
	 */
	_note_batch_job_finished(req->job_id);
	/*
	 * "revoke" all future credentials for this jobid
	 */
	if (slurm_cred_revoke(conf->vctx, req->job_id, req->time,
			      req->start_time) < 0) {
		debug("revoking cred for job %u: %m", req->job_id);
	} else {
		save_cred_state(conf->vctx);
		debug("credential for job %u revoked", req->job_id);
	}

	/*
	 * Before signalling steps, if the job has any steps that are still
	 * in the process of fork/exec/check in with slurmd, wait on a condition
	 * var for the start.  Otherwise a slow-starting step can miss the
	 * job termination message and run indefinitely.
	 */
	if (_step_is_starting(req->job_id, NO_VAL)) {
		if (msg->conn_fd >= 0) {
			/* If the step hasn't started yet just send a
			 * success to let the controller know we got
			 * this request.
			 */
			debug("sent SUCCESS, waiting for step to start");
			slurm_send_rc_msg (msg, SLURM_SUCCESS);
			if (slurm_close(msg->conn_fd) < 0)
				error ( "rpc_kill_job: close(%d): %m",
					msg->conn_fd);
			msg->conn_fd = -1;
		}
		if (_wait_for_starting_step(req->job_id, NO_VAL)) {
			/*
			 * There's currently no case in which we enter this
			 * error condition.  If there was, it's hard to say
			 * whether to to proceed with the job termination.
			 */
			error("Error in _wait_for_starting_step");
		}
	}
	if (IS_JOB_NODE_FAILED(req))
		_kill_all_active_steps(req->job_id, SIG_NODE_FAIL, true);
	if (IS_JOB_PENDING(req))
		_kill_all_active_steps(req->job_id, SIG_REQUEUED, true);
	else if (IS_JOB_FAILED(req))
		_kill_all_active_steps(req->job_id, SIG_FAILURE, true);

	/*
	 * Tasks might be stopped (possibly by a debugger)
	 * so send SIGCONT first.
	 */
	_kill_all_active_steps(req->job_id, SIGCONT, true);
	if (errno == ESLURMD_STEP_SUSPENDED) {
		/*
		 * If the job step is currently suspended, we don't
		 * bother with a "nice" termination.
		 */
		debug2("Job is currently suspended, terminating");
		nsteps = _terminate_all_steps(req->job_id, true);
	} else {
		nsteps = _kill_all_active_steps(req->job_id, SIGTERM, true);
	}

	if ((nsteps == 0) && !conf->epilog) {
		struct stat stat_buf;
		if (conf->plugstack && (stat(conf->plugstack, &stat_buf) == 0))
			have_spank = true;
	}
	/*
	 *  If there are currently no active job steps and no
	 *    configured epilog to run, bypass asynchronous reply and
	 *    notify slurmctld that we have already completed this
	 *    request. We need to send current switch state on AIX
	 *    systems, so this bypass can not be used.
	 */
	if ((nsteps == 0) && !conf->epilog && !have_spank) {
		debug4("sent ALREADY_COMPLETE");
		if (msg->conn_fd >= 0) {
			slurm_send_rc_msg(msg,
					  ESLURMD_KILL_JOB_ALREADY_COMPLETE);
		}
		slurm_cred_begin_expiration(conf->vctx, req->job_id);
		save_cred_state(conf->vctx);
		_waiter_complete(req->job_id);

		/*
		 * The controller needs to get MESSAGE_EPILOG_COMPLETE to bring
		 * the job out of "completing" state.  Otherwise, the job
		 * could remain "completing" unnecessarily, until the request
		 * to terminate is resent.
		 */
		_sync_messages_kill(req);
		if (msg->conn_fd < 0) {
			/* The epilog complete message processing on
			 * slurmctld is equivalent to that of a
			 * ESLURMD_KILL_JOB_ALREADY_COMPLETE reply above */
			_epilog_complete(req->job_id, rc);
		}
		if (container_g_delete(req->job_id))
			error("container_g_delete(%u): %m", req->job_id);
		_launch_complete_rm(req->job_id);
		return;
	}

	/*
	 *  At this point, if connection still open, we send controller
	 *   a "success" reply to indicate that we've recvd the msg.
	 */
	if (msg->conn_fd >= 0) {
		debug4("sent SUCCESS");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		if (slurm_close(msg->conn_fd) < 0)
			error ("rpc_kill_job: close(%d): %m", msg->conn_fd);
		msg->conn_fd = -1;
	}

	/*
	 *  Check for corpses
	 */
	delay = MAX(conf->kill_wait, 5);
	if ( !_pause_for_job_completion (req->job_id, req->nodes, delay) &&
	     _terminate_all_steps(req->job_id, true) ) {
		/*
		 *  Block until all user processes are complete.
		 */
		_pause_for_job_completion (req->job_id, req->nodes, 0);
	}

	/*
	 *  Begin expiration period for cached information about job.
	 *   If expiration period has already begun, then do not run
	 *   the epilog again, as that script has already been executed
	 *   for this job.
	 */
	if (slurm_cred_begin_expiration(conf->vctx, req->job_id) < 0) {
		debug("Not running epilog for jobid %d: %m", req->job_id);
		goto done;
	}

	save_cred_state(conf->vctx);

	memset(&job_env, 0, sizeof(job_env_t));

	job_env.jobid = req->job_id;
	job_env.node_list = req->nodes;
	job_env.spank_job_env = req->spank_job_env;
	job_env.spank_job_env_size = req->spank_job_env_size;
	job_env.uid = req->job_uid;

#if defined(HAVE_BG)
	select_g_select_jobinfo_get(req->select_jobinfo,
				    SELECT_JOBDATA_BLOCK_ID,
				    &job_env.resv_id);
#elif defined(HAVE_ALPS_CRAY)
	job_env.resv_id = select_g_select_jobinfo_xstrdup(
		req->select_jobinfo, SELECT_PRINT_RESV_ID);
#endif
	rc = _run_epilog(&job_env);
	xfree(job_env.resv_id);

	if (rc) {
		int term_sig, exit_status;
		if (WIFSIGNALED(rc)) {
			exit_status = 0;
			term_sig    = WTERMSIG(rc);
		} else {
			exit_status = WEXITSTATUS(rc);
			term_sig    = 0;
		}
		error("[job %u] epilog failed status=%d:%d",
		      req->job_id, exit_status, term_sig);
		rc = ESLURMD_EPILOG_FAILED;
	} else
		debug("completed epilog for jobid %u", req->job_id);
	if (container_g_delete(req->job_id))
		error("container_g_delete(%u): %m", req->job_id);
	_launch_complete_rm(req->job_id);

    done:
	_wait_state_completed(req->job_id, 5);
	_waiter_complete(req->job_id);
	_sync_messages_kill(req);

	_epilog_complete(req->job_id, rc);
}

/* On a parallel job, every slurmd may send the EPILOG_COMPLETE
 * message to the slurmctld at the same time, resulting in lost
 * messages. We add a delay here to spead out the message traffic
 * assuming synchronized clocks across the cluster.
 * Allow 10 msec processing time in slurmctld for each RPC. */
static void _sync_messages_kill(kill_job_msg_t *req)
{
	int host_cnt, host_inx;
	char *host;
	hostset_t hosts;
	int epilog_msg_time;

	hosts = hostset_create(req->nodes);
	host_cnt = hostset_count(hosts);
	if (host_cnt <= 64)
		goto fini;
	if (conf->hostname == NULL)
		goto fini;	/* should never happen */

	for (host_inx=0; host_inx<host_cnt; host_inx++) {
		host = hostset_shift(hosts);
		if (host == NULL)
			break;
		if (xstrcmp(host, conf->node_name) == 0) {
			free(host);
			break;
		}
		free(host);
	}
	epilog_msg_time = slurm_get_epilog_msg_time();
	_delay_rpc(host_inx, host_cnt, epilog_msg_time);

 fini:	hostset_destroy(hosts);
}

/* Delay a message based upon the host index, total host count and RPC_TIME.
 * This logic depends upon synchronized clocks across the cluster. */
static void _delay_rpc(int host_inx, int host_cnt, int usec_per_rpc)
{
	struct timeval tv1;
	uint32_t cur_time;	/* current time in usec (just 9 digits) */
	uint32_t tot_time;	/* total time expected for all RPCs */
	uint32_t offset_time;	/* relative time within tot_time */
	uint32_t target_time;	/* desired time to issue the RPC */
	uint32_t delta_time;

again:	if (gettimeofday(&tv1, NULL)) {
		usleep(host_inx * usec_per_rpc);
		return;
	}

	cur_time = ((tv1.tv_sec % 1000) * 1000000) + tv1.tv_usec;
	tot_time = host_cnt * usec_per_rpc;
	offset_time = cur_time % tot_time;
	target_time = host_inx * usec_per_rpc;
	if (target_time < offset_time)
		delta_time = target_time - offset_time + tot_time;
	else
		delta_time = target_time - offset_time;
	if (usleep(delta_time)) {
		if (errno == EINVAL) /* usleep for more than 1 sec */
			usleep(900000);
		/* errno == EINTR */
		goto again;
	}
}

/*
 *  Returns true if "uid" is a "slurm authorized user" - i.e. uid == 0
 *   or uid == slurm user id at this time.
 */
static bool
_slurm_authorized_user(uid_t uid)
{
	return ((uid == (uid_t) 0) || (uid == conf->slurm_user_id));
}


struct waiter {
	uint32_t jobid;
	pthread_t thd;
};


static struct waiter *
_waiter_create(uint32_t jobid)
{
	struct waiter *wp = xmalloc(sizeof(struct waiter));

	wp->jobid = jobid;
	wp->thd   = pthread_self();

	return wp;
}

static int _find_waiter(struct waiter *w, uint32_t *jp)
{
	return (w->jobid == *jp);
}

static void _waiter_destroy(struct waiter *wp)
{
	xfree(wp);
}

static int _waiter_init (uint32_t jobid)
{
	if (!waiters)
		waiters = list_create((ListDelF) _waiter_destroy);

	/*
	 *  Exit this thread if another thread is waiting on job
	 */
	if (list_find_first (waiters, (ListFindF) _find_waiter, &jobid))
		return SLURM_ERROR;
	else
		list_append(waiters, _waiter_create(jobid));

	return (SLURM_SUCCESS);
}

static int _waiter_complete (uint32_t jobid)
{
	return (list_delete_all (waiters, (ListFindF) _find_waiter, &jobid));
}

/*
 *  Like _wait_for_procs(), but only wait for up to max_time seconds
 *  if max_time == 0, send SIGKILL to tasks repeatedly
 *
 *  Returns true if all job processes are gone
 */
static bool
_pause_for_job_completion (uint32_t job_id, char *nodes, int max_time)
{
	int sec = 0;
	int pause = 1;
	bool rc = false;

	while ((sec < max_time) || (max_time == 0)) {
		rc = _job_still_running (job_id);
		if (!rc)
			break;
		if ((max_time == 0) && (sec > 1)) {
			_terminate_all_steps(job_id, true);
		}
		if (sec > 10) {
			/* Reduce logging frequency about unkillable tasks */
			if (max_time)
				pause = MIN((max_time - sec), 10);
			else
				pause = 10;
		}
		sleep(pause);
		sec += pause;
	}

	/*
	 * Return true if job is NOT running
	 */
	return (!rc);
}

/*
 * Does nothing and returns SLURM_SUCCESS (if uid authenticates).
 *
 * Timelimit is not currently used in the slurmd or slurmstepd.
 */
static void
_rpc_update_time(slurm_msg_t *msg)
{
	int   rc      = SLURM_SUCCESS;
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred,
					     conf->auth_info);

	if ((req_uid != conf->slurm_user_id) && (req_uid != 0)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, uid %d can't update time limit",
		      req_uid);
		goto done;
	}

/* 	if (shm_update_job_timelimit(req->job_id, req->expiration_time) < 0) { */
/* 		error("updating lifetime for job %u: %m", req->job_id); */
/* 		rc = ESLURM_INVALID_JOB_ID; */
/* 	} else */
/* 		debug("reset job %u lifetime", req->job_id); */

    done:
	slurm_send_rc_msg(msg, rc);
}

/* NOTE: call _destroy_env() to free returned value */
static char **
_build_env(job_env_t *job_env)
{
	char **env = xmalloc(sizeof(char *));
	bool user_name_set = 0;

	env[0]  = NULL;
	if (!valid_spank_job_env(job_env->spank_job_env,
				 job_env->spank_job_env_size,
				 job_env->uid)) {
		/* If SPANK job environment is bad, log it and do not use */
		job_env->spank_job_env_size = 0;
		job_env->spank_job_env = (char **) NULL;
	}
	if (job_env->spank_job_env_size) {
		env_array_merge_spank(&env,
				      (const char **) job_env->spank_job_env);
	}

	slurm_mutex_lock(&conf->config_mutex);
	setenvf(&env, "SLURMD_NODENAME", "%s", conf->node_name);
	setenvf(&env, "SLURM_CONF", conf->conffile);
	slurm_mutex_unlock(&conf->config_mutex);

	setenvf(&env, "SLURM_CLUSTER_NAME", "%s", conf->cluster_name);
	setenvf(&env, "SLURM_JOB_ID", "%u", job_env->jobid);
	setenvf(&env, "SLURM_JOB_UID",   "%u", job_env->uid);

#ifndef HAVE_NATIVE_CRAY
	/* uid_to_string on a cray is a heavy call, so try to avoid it */
	if (!job_env->user_name) {
		job_env->user_name = uid_to_string(job_env->uid);
		user_name_set = 1;
	}
#endif

	setenvf(&env, "SLURM_JOB_USER", "%s", job_env->user_name);
	if (user_name_set)
		xfree(job_env->user_name);

	setenvf(&env, "SLURM_JOBID", "%u", job_env->jobid);
	setenvf(&env, "SLURM_UID",   "%u", job_env->uid);
	if (job_env->node_list)
		setenvf(&env, "SLURM_NODELIST", "%s", job_env->node_list);

	if (job_env->partition)
		setenvf(&env, "SLURM_JOB_PARTITION", "%s", job_env->partition);

	if (job_env->resv_id) {
#if defined(HAVE_BG)
		setenvf(&env, "MPIRUN_PARTITION", "%s", job_env->resv_id);
#elif defined(HAVE_ALPS_CRAY)
		setenvf(&env, "BASIL_RESERVATION_ID", "%s", job_env->resv_id);
#endif
	}
	return env;
}

static void
_destroy_env(char **env)
{
	int i=0;

	if (env) {
		for(i=0; env[i]; i++) {
			xfree(env[i]);
		}
		xfree(env);
	}

	return;
}

/* Trigger srun of spank prolog or epilog in slurmstepd */
static int
_run_spank_job_script (const char *mode, char **env, uint32_t job_id, uid_t uid)
{
	pid_t cpid;
	int status = 0, timeout;
	int pfds[2];

	if (pipe (pfds) < 0) {
		error ("_run_spank_job_script: pipe: %m");
		return (-1);
	}

	fd_set_close_on_exec (pfds[1]);

	debug ("Calling %s spank %s", conf->stepd_loc, mode);
	if ((cpid = fork ()) < 0) {
		error ("executing spank %s: %m", mode);
		return (-1);
	}
	if (cpid == 0) {
		/* Run slurmstepd spank [prolog|epilog] */
		char *argv[4] = {
			(char *) conf->stepd_loc,
			"spank",
			(char *) mode,
			NULL };

		/* container_g_add_pid needs to be called in the
		   forked process part of the fork to avoid a race
		   condition where if this process makes a file or
		   detacts itself from a child before we add the pid
		   to the container in the parent of the fork.
		*/
		if (container_g_add_pid(job_id, getpid(), getuid())
		    != SLURM_SUCCESS)
			error("container_g_add_pid(%u): %m", job_id);

		if (dup2 (pfds[0], STDIN_FILENO) < 0)
			fatal ("dup2: %m");
		setpgid(0, 0);
		if (conf->chos_loc && !access(conf->chos_loc, X_OK))
			execve(conf->chos_loc, argv, env);
		else
			execve(argv[0], argv, env);
		error ("execve(%s): %m", argv[0]);
		exit (127);
	}

	close (pfds[0]);

	if (_send_slurmd_conf_lite (pfds[1], conf) < 0)
		error ("Failed to send slurmd conf to slurmstepd\n");
	close (pfds[1]);

	timeout = MAX(slurm_get_prolog_timeout(), 120); /* 120 secs in v15.08 */
	if (waitpid_timeout (mode, cpid, &status, timeout) < 0) {
		error ("spank/%s timed out after %u secs", mode, timeout);
		return (-1);
	}

	if (status)
		error ("spank/%s returned status 0x%04x", mode, status);

	/*
	 *  No longer need SPANK option env vars in environment
	 */
	spank_clear_remote_options_env (env);

	return (status);
}

static int _run_job_script(const char *name, const char *path,
			   uint32_t jobid, int timeout, char **env, uid_t uid)
{
	struct stat stat_buf;
	int status = 0, rc;

	/*
	 *  Always run both spank prolog/epilog and real prolog/epilog script,
	 *   even if spank plugins fail. (May want to alter this in the future)
	 *   If both "script" mechanisms fail, prefer to return the "real"
	 *   prolog/epilog status.
	 */
	if (conf->plugstack && (stat(conf->plugstack, &stat_buf) == 0))
		status = _run_spank_job_script(name, env, jobid, uid);
	if ((rc = run_script(name, path, jobid, timeout, env, uid)))
		status = rc;
	return (status);
}

#ifdef HAVE_BG
/* a slow prolog is expected on bluegene systems */
static int
_run_prolog(job_env_t *job_env, slurm_cred_t *cred)
{
	int rc;
	char *my_prolog;
	char **my_env;

	my_env = _build_env(job_env);
	setenvf(&my_env, "SLURM_STEP_ID", "%u", job_env->step_id);

	slurm_mutex_lock(&conf->config_mutex);
	my_prolog = xstrdup(conf->prolog);
	slurm_mutex_unlock(&conf->config_mutex);

	rc = _run_job_script("prolog", my_prolog, job_env->jobid,
			     -1, my_env, job_env->uid);
	_remove_job_running_prolog(job_env->jobid);
	xfree(my_prolog);
	_destroy_env(my_env);

	return rc;
}
#else
static void *_prolog_timer(void *x)
{
	int delay_time, rc = SLURM_SUCCESS;
	struct timespec ts;
	struct timeval now;
	slurm_msg_t msg;
	job_notify_msg_t notify_req;
	char srun_msg[128];
	timer_struct_t *timer_struct = (timer_struct_t *) x;

	delay_time = MAX(2, (timer_struct->msg_timeout - 2));
	gettimeofday(&now, NULL);
	ts.tv_sec = now.tv_sec + delay_time;
	ts.tv_nsec = now.tv_usec * 1000;
	slurm_mutex_lock(timer_struct->timer_mutex);
	if (!timer_struct->prolog_fini) {
		rc = pthread_cond_timedwait(timer_struct->timer_cond,
					    timer_struct->timer_mutex, &ts);
	}
	slurm_mutex_unlock(timer_struct->timer_mutex);

	if (rc != ETIMEDOUT)
		return NULL;

	slurm_msg_t_init(&msg);
	snprintf(srun_msg, sizeof(srun_msg), "Prolog hung on node %s",
		 conf->node_name);
	notify_req.job_id	= timer_struct->job_id;
	notify_req.job_step_id	= NO_VAL;
	notify_req.message	= srun_msg;
	msg.msg_type	= REQUEST_JOB_NOTIFY;
	msg.data	= &notify_req;
	slurm_send_only_controller_msg(&msg);
	return NULL;
}

static int _get_node_inx(char *hostlist)
{
	char *host;
	int node_inx = -1;
	hostset_t hset;

	if (!conf->node_name)
		return node_inx;

	if ((hset = hostset_create(hostlist))) {
		int inx = 0;
		while ((host = hostset_shift(hset))) {
			if (!strcmp(host, conf->node_name)) {
				node_inx = inx;
				free(host);
				break;
			}
			inx++;
			free(host);
		}
		hostset_destroy(hset);
	}
	return node_inx;
}

static int
_run_prolog(job_env_t *job_env, slurm_cred_t *cred)
{
	DEF_TIMERS;
	int rc, diff_time;
	char *my_prolog;
	time_t start_time = time(NULL);
	static uint16_t msg_timeout = 0;
	static uint16_t timeout;
	pthread_t       timer_id;
	pthread_attr_t  timer_attr;
	pthread_cond_t  timer_cond  = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
	timer_struct_t  timer_struct;
	bool prolog_fini = false;
	bool script_lock = false;
	char **my_env;

	my_env = _build_env(job_env);
	setenvf(&my_env, "SLURM_STEP_ID", "%u", job_env->step_id);
	if (cred) {
		slurm_cred_arg_t cred_arg;
		slurm_cred_get_args(cred, &cred_arg);
		setenvf(&my_env, "SLURM_JOB_CONSTRAINTS", "%s",
			cred_arg.job_constraints);
		gres_plugin_job_set_env(&my_env, cred_arg.job_gres_list,
					_get_node_inx(cred_arg.step_hostlist));
		slurm_cred_free_args(&cred_arg);
	}

	if (msg_timeout == 0)
		msg_timeout = slurm_get_msg_timeout();

	if (timeout == 0)
		timeout = slurm_get_prolog_timeout();

	slurm_mutex_lock(&conf->config_mutex);
	my_prolog = xstrdup(conf->prolog);
	slurm_mutex_unlock(&conf->config_mutex);

	if (slurmctld_conf.prolog_flags & PROLOG_FLAG_SERIAL) {
		slurm_mutex_lock(&prolog_serial_mutex);
		script_lock = true;
	}

	slurm_attr_init(&timer_attr);
	timer_struct.job_id      = job_env->jobid;
	timer_struct.msg_timeout = msg_timeout;
	timer_struct.prolog_fini = &prolog_fini;
	timer_struct.timer_cond  = &timer_cond;
	timer_struct.timer_mutex = &timer_mutex;
	pthread_create(&timer_id, &timer_attr, &_prolog_timer, &timer_struct);
	START_TIMER;

	if (timeout == (uint16_t)NO_VAL)
		rc = _run_job_script("prolog", my_prolog, job_env->jobid,
				     -1, my_env, job_env->uid);
	else
		rc = _run_job_script("prolog", my_prolog, job_env->jobid,
				     timeout, my_env, job_env->uid);

	END_TIMER;
	info("%s: run job script took %s", __func__, TIME_STR);
	slurm_mutex_lock(&timer_mutex);
	prolog_fini = true;
	slurm_cond_broadcast(&timer_cond);
	slurm_mutex_unlock(&timer_mutex);

	diff_time = difftime(time(NULL), start_time);
	info("%s: prolog with lock for job %u ran for %d seconds",
	     __func__, job_env->jobid, diff_time);
	if (diff_time >= (msg_timeout / 2)) {
		info("prolog for job %u ran for %d seconds",
		     job_env->jobid, diff_time);
	}

	_remove_job_running_prolog(job_env->jobid);
	xfree(my_prolog);
	_destroy_env(my_env);

	pthread_join(timer_id, NULL);
	if (script_lock)
		slurm_mutex_unlock(&prolog_serial_mutex);

	return rc;
}
#endif

static int
_run_epilog(job_env_t *job_env)
{
	time_t start_time = time(NULL);
	static uint16_t msg_timeout = 0;
	static uint16_t timeout;
	int error_code, diff_time;
	char *my_epilog;
	char **my_env = _build_env(job_env);
	bool script_lock = false;

	if (msg_timeout == 0)
		msg_timeout = slurm_get_msg_timeout();

	if (timeout == 0)
		timeout = slurm_get_prolog_timeout();

	slurm_mutex_lock(&conf->config_mutex);
	my_epilog = xstrdup(conf->epilog);
	slurm_mutex_unlock(&conf->config_mutex);

	_wait_for_job_running_prolog(job_env->jobid);

	if (slurmctld_conf.prolog_flags & PROLOG_FLAG_SERIAL) {
		slurm_mutex_lock(&prolog_serial_mutex);
		script_lock = true;
	}

	if (timeout == (uint16_t)NO_VAL)
		error_code = _run_job_script("epilog", my_epilog, job_env->jobid,
					     -1, my_env, job_env->uid);
	else
		error_code = _run_job_script("epilog", my_epilog, job_env->jobid,
					     timeout, my_env, job_env->uid);

	xfree(my_epilog);
	_destroy_env(my_env);

	diff_time = difftime(time(NULL), start_time);
	if (diff_time >= (msg_timeout / 2)) {
		info("epilog for job %u ran for %d seconds",
		     job_env->jobid, diff_time);
	}

	if (script_lock)
		slurm_mutex_unlock(&prolog_serial_mutex);

	return error_code;
}


/**********************************************************************/
/* Because calling initgroups(2)/getgrouplist(3) can be expensive and */
/* is not cached by sssd or nscd, we cache the group access list.     */
/**********************************************************************/

typedef struct gid_cache_s {
	char *user;
	time_t timestamp;
	gid_t gid;
	gids_t *gids;
	struct gid_cache_s *next;
} gids_cache_t;

#define GIDS_HASH_LEN 64
static gids_cache_t *gids_hashtbl[GIDS_HASH_LEN] = {NULL};
static pthread_mutex_t gids_mutex = PTHREAD_MUTEX_INITIALIZER;

static gids_t *
_alloc_gids(int n, gid_t *gids)
{
	gids_t *new;

	new = (gids_t *)xmalloc(sizeof(gids_t));
	new->ngids = n;
	new->gids = gids;
	return new;
}

static void
_dealloc_gids(gids_t *p)
{
	xfree(p->gids);
	xfree(p);
}

/* Duplicate a gids_t struct.  */
static gids_t *
_gids_dup(gids_t *g)
{
	int buf_size;
	gids_t *n = xmalloc(sizeof(gids_t));
	n->ngids = g->ngids;
	buf_size = g->ngids * sizeof(gid_t);
	n->gids = xmalloc(buf_size);
	memcpy(n->gids, g->gids, buf_size);
	return n;
}

static gids_cache_t *
_alloc_gids_cache(char *user, gid_t gid, gids_t *gids, gids_cache_t *next)
{
	gids_cache_t *p;

	p = (gids_cache_t *)xmalloc(sizeof(gids_cache_t));
	p->user = xstrdup(user);
	p->timestamp = time(NULL);
	p->gid = gid;
	p->gids = gids;
	p->next = next;
	return p;
}

static void
_dealloc_gids_cache(gids_cache_t *p)
{
	xfree(p->user);
	_dealloc_gids(p->gids);
	xfree(p);
}

static size_t
_gids_hashtbl_idx(const char *user)
{
	uint64_t x = 0;
	int i = 0;
	/* copied from _get_hash_idx in slurmctld */
	/* step through string, multiply value times position
	 * to get a bit of entropy back */
	while (*user) {
		x += i++ * (int) *user++;
	}

	return x % GIDS_HASH_LEN;
}

void
gids_cache_purge(void)
{
	int i;
	gids_cache_t *p, *q;

	slurm_mutex_lock(&gids_mutex);
	for (i=0; i<GIDS_HASH_LEN; i++) {
		p = gids_hashtbl[i];
		while (p) {
			q = p->next;
			_dealloc_gids_cache(p);
			p = q;
		}
		gids_hashtbl[i] = NULL;
	}
	slurm_mutex_unlock(&gids_mutex);
}

static void
_gids_cache_register(char *user, gid_t gid, gids_t *gids)
{
	size_t idx;
	gids_cache_t *p, *q;

	idx = _gids_hashtbl_idx(user);
	q = gids_hashtbl[idx];
	p = _alloc_gids_cache(user, gid, gids, q);
	gids_hashtbl[idx] = p;
	debug2("Cached group access list for %s/%d", user, gid);
}

/* how many groups to use by default to avoid repeated calls to getgrouplist */
#define NGROUPS_START 64

static gids_t *_gids_cache_lookup(char *user, gid_t gid)
{
	size_t idx;
	gids_cache_t *p;
	bool found_but_old = false;
	time_t now = 0;
	int ngroups = NGROUPS_START;
	gid_t *groups;
	gids_t *ret_gids = NULL;

	idx = _gids_hashtbl_idx(user);
	slurm_mutex_lock(&gids_mutex);
	p = gids_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->user, user) == 0 && p->gid == gid) {
			slurm_ctl_conf_t *cf = slurm_conf_lock();
			int group_ttl = cf->group_info & GROUP_TIME_MASK;
			slurm_conf_unlock();
			if (!group_ttl) {
				ret_gids = _gids_dup(p->gids);
				goto done;
			}
			now = time(NULL);
			if (difftime(now, p->timestamp) < group_ttl) {
				ret_gids = _gids_dup(p->gids);
				goto done;
			} else {
				found_but_old = true;
				break;
			}
		}
		p = p->next;
	}
	/* Cache lookup failed or cached value was too old, fetch new
	 * value and insert it into cache.  */
	groups = xmalloc(ngroups * sizeof(gid_t));
	while (getgrouplist(user, gid, groups, &ngroups) == -1) {
		/* group list larger than array, resize array to fit */
		groups = xrealloc(groups, ngroups * sizeof(gid_t));
	}
	if (found_but_old) {
		xfree(p->gids->gids);
		p->gids->gids = groups;
		p->gids->ngids = ngroups;
		p->timestamp = now;
		ret_gids = _gids_dup(p->gids);
	} else {
		gids_t *gids = _alloc_gids(ngroups, groups);
		_gids_cache_register(user, gid, gids);
		ret_gids = _gids_dup(gids);
	}
done:
	slurm_mutex_unlock(&gids_mutex);
	return ret_gids;
}


extern void
destroy_starting_step(void *x)
{
	xfree(x);
}


static int
_add_starting_step(uint16_t type, void *req)
{
	starting_step_t *starting_step;
	int rc = SLURM_SUCCESS;

	/* Add the step info to a list of starting processes that
	   cannot reliably be contacted. */

	starting_step = xmalloc(sizeof(starting_step_t));
	if (!starting_step) {
		error("%s failed to allocate memory", __func__);
		rc = SLURM_FAILURE;
		goto fail;
	}
	switch (type) {
	case LAUNCH_BATCH_JOB:
		starting_step->job_id =
			((batch_job_launch_msg_t *)req)->job_id;
		starting_step->step_id =
			((batch_job_launch_msg_t *)req)->step_id;
		break;
	case LAUNCH_TASKS:
		starting_step->job_id =
			((launch_tasks_request_msg_t *)req)->job_id;
		starting_step->step_id =
			((launch_tasks_request_msg_t *)req)->job_step_id;
		break;
	case REQUEST_LAUNCH_PROLOG:
		starting_step->job_id  = ((prolog_launch_msg_t *)req)->job_id;
		starting_step->step_id = SLURM_EXTERN_CONT;
		break;
	default:
		error("%s called with an invalid type: %u", __func__, type);
		rc = SLURM_FAILURE;
		xfree(starting_step);
		goto fail;
	}

	if (!list_append(conf->starting_steps, starting_step)) {
		error("%s failed to allocate memory for list", __func__);
		rc = SLURM_FAILURE;
		xfree(starting_step);
		goto fail;
	}

fail:
	return rc;
}


static int
_remove_starting_step(uint16_t type, void *req)
{
	starting_step_t starting_step;
	int rc = SLURM_SUCCESS;

	switch(type) {
	case LAUNCH_BATCH_JOB:
		starting_step.job_id =
			((batch_job_launch_msg_t *)req)->job_id;
		starting_step.step_id =
			((batch_job_launch_msg_t *)req)->step_id;
		break;
	case LAUNCH_TASKS:
		starting_step.job_id =
			((launch_tasks_request_msg_t *)req)->job_id;
		starting_step.step_id =
			((launch_tasks_request_msg_t *)req)->job_step_id;
		break;
	default:
		error("%s called with an invalid type: %u", __func__, type);
		rc = SLURM_FAILURE;
		goto fail;
	}

	if (!list_delete_all(conf->starting_steps,
			     _compare_starting_steps,
			     &starting_step)) {
		error("%s: step %u.%u not found", __func__,
		      starting_step.job_id,
		      starting_step.step_id);
		rc = SLURM_FAILURE;
	}
	slurm_cond_broadcast(&conf->starting_steps_cond);
fail:
	return rc;
}



static int _compare_starting_steps(void *listentry, void *key)
{
	starting_step_t *step0 = (starting_step_t *)listentry;
	starting_step_t *step1 = (starting_step_t *)key;

	if (step1->step_id != NO_VAL)
		return (step0->job_id  == step1->job_id &&
			step0->step_id == step1->step_id);
	else
		return (step0->job_id  == step1->job_id);
}


/* Wait for a step to get far enough in the launch process to have
   a socket open, ready to handle RPC calls.  Pass step_id = NO_VAL
   to wait on any step for the given job. */

static int _wait_for_starting_step(uint32_t job_id, uint32_t step_id)
{
	static pthread_mutex_t dummy_lock = PTHREAD_MUTEX_INITIALIZER;
	struct timespec ts = {0, 0};
	struct timeval now;

	starting_step_t starting_step;
	int num_passes = 0;

	starting_step.job_id  = job_id;
	starting_step.step_id = step_id;

	while (list_find_first( conf->starting_steps,
				&_compare_starting_steps,
				&starting_step )) {
		if (num_passes == 0) {
			if (step_id != NO_VAL)
				debug( "Blocked waiting for step %d.%d",
					job_id, step_id);
			else
				debug( "Blocked waiting for job %d, all steps",
					job_id);
		}
		num_passes++;

		gettimeofday(&now, NULL);
		ts.tv_sec = now.tv_sec+1;
		ts.tv_nsec = now.tv_usec * 1000;

		slurm_mutex_lock(&dummy_lock);
		slurm_cond_timedwait(&conf->starting_steps_cond,
				     &dummy_lock, &ts);
		slurm_mutex_unlock(&dummy_lock);
	}
	if (num_passes > 0) {
		if (step_id != NO_VAL)
			debug( "Finished wait for step %d.%d",
				job_id, step_id);
		else
			debug( "Finished wait for job %d, all steps",
				job_id);
	}

	return SLURM_SUCCESS;
}


/* Return true if the step has not yet confirmed that its socket to
   handle RPC calls has been created.  Pass step_id = NO_VAL
   to return true if any of the job's steps are still starting. */
static bool _step_is_starting(uint32_t job_id, uint32_t step_id)
{
	starting_step_t  starting_step;
	starting_step.job_id  = job_id;
	starting_step.step_id = step_id;
	bool ret = false;

	if (list_find_first( conf->starting_steps,
			     &_compare_starting_steps,
			     &starting_step )) {
		ret = true;
	}

	return ret;
}

/* Add this job to the list of jobs currently running their prolog */
static void _add_job_running_prolog(uint32_t job_id)
{
	uint32_t *job_running_prolog;

	/* Add the job to a list of jobs whose prologs are running */
	job_running_prolog = xmalloc(sizeof(uint32_t));
	if (!job_running_prolog) {
		error("_add_job_running_prolog failed to allocate memory");
		goto fail;
	}

	*job_running_prolog = job_id;
	if (!list_append(conf->prolog_running_jobs, job_running_prolog)) {
		error("_add_job_running_prolog failed to append job to list");
		xfree(job_running_prolog);
	}

fail:
	return;
}

/* Remove this job from the list of jobs currently running their prolog */
static void _remove_job_running_prolog(uint32_t job_id)
{
	if (!list_delete_all(conf->prolog_running_jobs,
			     _match_jobid, &job_id))
		error("_remove_job_running_prolog: job not found");
	slurm_cond_broadcast(&conf->prolog_running_cond);
}

static int _match_jobid(void *listentry, void *key)
{
	uint32_t *job0 = (uint32_t *)listentry;
	uint32_t *job1 = (uint32_t *)key;

	return (*job0 == *job1);
}

static int _prolog_is_running (uint32_t jobid)
{
	int rc = 0;
	if (list_find_first (conf->prolog_running_jobs,
	                     (ListFindF) _match_jobid, &jobid))
		rc = 1;
	return (rc);
}

/* Wait for the job's prolog to complete */
static void _wait_for_job_running_prolog(uint32_t job_id)
{
	static pthread_mutex_t dummy_lock = PTHREAD_MUTEX_INITIALIZER;
	struct timespec ts = {0, 0};
	struct timeval now;

	debug( "Waiting for job %d's prolog to complete", job_id);

	while (_prolog_is_running (job_id)) {

		gettimeofday(&now, NULL);
		ts.tv_sec = now.tv_sec+1;
		ts.tv_nsec = now.tv_usec * 1000;

		slurm_mutex_lock(&dummy_lock);
		slurm_cond_timedwait(&conf->prolog_running_cond,
				     &dummy_lock, &ts);
		slurm_mutex_unlock(&dummy_lock);
	}

	debug( "Finished wait for job %d's prolog to complete", job_id);
}


static void
_rpc_forward_data(slurm_msg_t *msg)
{
	forward_data_msg_t *req = (forward_data_msg_t *)msg->data;
	uint32_t req_uid;
	struct sockaddr_un sa;
	int fd = -1, rc = 0;

	/* Make sure we adjust for the spool dir coming in on the address to
	 * point to the right spot.
	 */
	xstrsubstitute(req->address, "%n", conf->node_name);
	xstrsubstitute(req->address, "%h", conf->node_name);
	debug3("Entering _rpc_forward_data, address: %s, len: %u",
	       req->address, req->len);

	/* sanity check */
	if (strlen(req->address) > sizeof(sa.sun_path) - 1) {
		slurm_seterrno(EINVAL);
		rc = errno;
		goto done;
	}

	/* connect to specified address */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		rc = errno;
		error("failed creating UNIX domain socket: %m");
		goto done;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, req->address);
	while (((rc = connect(fd, (struct sockaddr *)&sa, SUN_LEN(&sa))) < 0) &&
	       (errno == EINTR));
	if (rc < 0) {
		rc = errno;
		debug2("failed connecting to specified socket '%s': %m",
		       req->address);
		goto done;
	}

	req_uid = (uint32_t)g_slurm_auth_get_uid(msg->auth_cred,
						 conf->auth_info);
	/*
	 * although always in localhost, we still convert it to network
	 * byte order, to make it consistent with pack/unpack.
	 */
	req_uid = htonl(req_uid);
	safe_write(fd, &req_uid, sizeof(uint32_t));
	req_uid = htonl(req->len);
	safe_write(fd, &req_uid, sizeof(uint32_t));
	safe_write(fd, req->data, req->len);

rwfail:
done:
	if (fd >= 0){
		close(fd);
	}
	slurm_send_rc_msg(msg, rc);
}

static void _launch_complete_add(uint32_t job_id)
{
	int j, empty;

	slurm_mutex_lock(&job_state_mutex);
	empty = -1;
	for (j = 0; j < JOB_STATE_CNT; j++) {
		if (job_id == active_job_id[j])
			break;
		if ((active_job_id[j] == 0) && (empty == -1))
			empty = j;
	}
	if (j >= JOB_STATE_CNT || job_id != active_job_id[j]) {
		if (empty == -1)	/* Discard oldest job */
			empty = 0;
		for (j = empty + 1; j < JOB_STATE_CNT; j++) {
			active_job_id[j - 1] = active_job_id[j];
		}
		active_job_id[JOB_STATE_CNT - 1] = 0;
		for (j = 0; j < JOB_STATE_CNT; j++) {
			if (active_job_id[j] == 0) {
				active_job_id[j] = job_id;
				break;
			}
		}
	}
	slurm_cond_signal(&job_state_cond);
	slurm_mutex_unlock(&job_state_mutex);
	_launch_complete_log("job add", job_id);
}

static void _launch_complete_log(char *type, uint32_t job_id)
{
#if 0
	int j;

	info("active %s %u", type, job_id);
	slurm_mutex_lock(&job_state_mutex);
	for (j = 0; j < JOB_STATE_CNT; j++) {
		if (active_job_id[j] != 0) {
			info("active_job_id[%d]=%u", j, active_job_id[j]);
		}
	}
	slurm_mutex_unlock(&job_state_mutex);
#endif
}

/* Test if we have a specific job ID still running */
static bool _launch_job_test(uint32_t job_id)
{
	bool found = false;
	int j;

	slurm_mutex_lock(&job_state_mutex);
	for (j = 0; j < JOB_STATE_CNT; j++) {
		if (job_id == active_job_id[j]) {
			found = true;
			break;
		}
	}
	slurm_mutex_unlock(&job_state_mutex);
	return found;
}


static void _launch_complete_rm(uint32_t job_id)
{
	int j;

	slurm_mutex_lock(&job_state_mutex);
	for (j = 0; j < JOB_STATE_CNT; j++) {
		if (job_id == active_job_id[j])
			break;
	}
	if (j < JOB_STATE_CNT && job_id == active_job_id[j]) {
		for (j = j + 1; j < JOB_STATE_CNT; j++) {
			active_job_id[j - 1] = active_job_id[j];
		}
		active_job_id[JOB_STATE_CNT - 1] = 0;
	}
	slurm_mutex_unlock(&job_state_mutex);
	_launch_complete_log("job remove", job_id);
}

static void _launch_complete_wait(uint32_t job_id)
{
	int i, j, empty;
	time_t start = time(NULL);
	struct timeval now;
	struct timespec timeout;

	slurm_mutex_lock(&job_state_mutex);
	for (i = 0; ; i++) {
		empty = -1;
		for (j = 0; j < JOB_STATE_CNT; j++) {
			if (job_id == active_job_id[j])
				break;
			if ((active_job_id[j] == 0) && (empty == -1))
				empty = j;
		}
		if (j < JOB_STATE_CNT)	/* Found job, ready to return */
			break;
		if (difftime(time(NULL), start) <= 9) {  /* Retry for 9 secs */
			debug2("wait for launch of job %u before suspending it",
			       job_id);
			gettimeofday(&now, NULL);
			timeout.tv_sec  = now.tv_sec + 1;
			timeout.tv_nsec = now.tv_usec * 1000;
			slurm_cond_timedwait(&job_state_cond,&job_state_mutex,
					     &timeout);
			continue;
		}
		if (empty == -1)	/* Discard oldest job */
			empty = 0;
		for (j = empty + 1; j < JOB_STATE_CNT; j++) {
			active_job_id[j - 1] = active_job_id[j];
		}
		active_job_id[JOB_STATE_CNT - 1] = 0;
		for (j = 0; j < JOB_STATE_CNT; j++) {
			if (active_job_id[j] == 0) {
				active_job_id[j] = job_id;
				break;
			}
		}
		break;
	}
	slurm_mutex_unlock(&job_state_mutex);
	_launch_complete_log("job wait", job_id);
}

static bool
_requeue_setup_env_fail(void)
{
	static time_t config_update = 0;
	static bool requeue = false;

	if (config_update != conf->last_update) {
		char *sched_params = slurm_get_sched_params();
		requeue = (sched_params &&
			   (strstr(sched_params, "no_env_cache") ||
			    strstr(sched_params, "requeue_setup_env_fail")));
		xfree(sched_params);
		config_update = conf->last_update;
	}

	return requeue;
}
