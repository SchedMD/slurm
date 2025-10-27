/*****************************************************************************\
 *  src/slurmd/slurmd/req.c - slurmd request handling
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
 *  Portions copyright (C) 2015 Mellanox Technologies Inc.
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

#include "config.h"

#define _GNU_SOURCE	/* for setresuid() */

#include <ctype.h>
#include <fcntl.h>
#include <ftw.h>
#include <grp.h>
#ifdef HAVE_NUMA
#undef NUMA_VERSION1_COMPATIBILITY
#include <numa.h>
#endif
#include <poll.h>
#include <pthread.h>
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

#include "src/bcast/file_bcast.h"

#include "src/common/assoc_mgr.h"
#include "src/common/callerid.h"
#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/forward.h"
#include "src/common/group_cache.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/reverse_tree.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_protocol_socket.h"
#include "src/common/spank.h"
#include "src/common/stepd_api.h"
#include "src/common/stepd_proxy.h"
#include "src/common/uid.h"
#include "src/common/util-net.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/conn.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/namespace.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/proctrack.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/task.h"

#include "src/slurmd/common/fname.h"
#include "src/slurmd/common/slurmd_common.h"
#include "src/slurmd/common/slurmstepd_init.h"

#include "src/slurmd/slurmd/cred_context.h"
#include "src/slurmd/slurmd/get_mach_stat.h"
#include "src/slurmd/slurmd/job_mem_limit.h"
#include "src/slurmd/slurmd/launch_state.h"
#include "src/slurmd/slurmd/slurmd.h"

#define RETRY_DELAY 15		/* retry every 15 seconds */
#define MAX_RETRY   240		/* retry 240 times (one hour max) */

#define MAX_NUMA_CNT 128

typedef struct {
	uint32_t uid;
	uint32_t job_id;
	uint32_t step_id;
	char *exe_fname;
	char *directory;
	time_t last_update;
} libdir_rec_t;

typedef struct {
	uint32_t uid;
	uint32_t job_id;
	uint32_t step_id;
	char *new_path;
	char *pos;
} foreach_libdir_args_t;

static void _free_job_env(job_env_t *env_ptr);
static bool _is_batch_job_finished(slurm_step_id_t *step_id);
static int _kill_all_active_steps(slurm_step_id_t *step_id, int sig, int flags,
				  char *details, bool batch, uid_t req_uid);
static int _launch_job_fail(slurm_step_id_t *step_id, uint32_t het_job_id,
			    uint32_t slurm_rc);
static void _note_batch_job_finished(slurm_step_id_t *step_id);
static bool _prolog_is_running(slurm_step_id_t *step_id);
static void _rpc_terminate_job(slurm_msg_t *msg);
static void _file_bcast_cleanup(void);
static int  _file_bcast_register_file(slurm_msg_t *msg,
				      sbcast_cred_arg_t *cred_arg,
				      file_bcast_info_t *key);

static bool _slurm_authorized_user(uid_t uid);
static int _waiter_init(slurm_step_id_t *step_id);
static void _waiter_complete(slurm_step_id_t *step_id);

static bool _steps_completed_now(slurm_step_id_t *step_id);
static sbcast_cred_arg_t *_valid_sbcast_cred(file_bcast_msg_t *req,
					     uid_t req_uid,
					     gid_t req_gid,
					     uint16_t protocol_version);
static void _wait_state_completed(slurm_step_id_t *step_id, int max_delay);
static uid_t _get_job_uid(uint32_t jobid);

static int  _add_starting_step(uint16_t type, void *req);
static int  _remove_starting_step(uint16_t type, void *req);
static int  _wait_for_starting_step(slurm_step_id_t *step_id);
static bool _step_is_starting(slurm_step_id_t *step_id);

static void _add_job_running_prolog(slurm_step_id_t *step_id);
static void _remove_job_running_prolog(slurm_step_id_t *step_id);
static void _wait_for_job_running_prolog(slurm_step_id_t *step_id);
static int _wait_for_request_launch_prolog(slurm_step_id_t *step_id,
					   bool *first_job_run);

/*
 *  List of threads waiting for jobs to complete
 */
static list_t *waiters = NULL;

static time_t startup = 0;		/* daemon startup time */
static time_t last_slurmctld_msg = 0;

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

static pthread_mutex_t prolog_mutex = PTHREAD_MUTEX_INITIALIZER;

#define FILE_BCAST_TIMEOUT 300
static pthread_rwlock_t file_bcast_lock = PTHREAD_RWLOCK_INITIALIZER;
static list_t *file_bcast_list = NULL;
static list_t *bcast_libdir_list = NULL;

static pthread_mutex_t waiter_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef enum {
	RELAY_AUTH_JOB = 0,
	RELAY_AUTH_SLURM_USER,
	RELAY_AUTH_PRIVATE_DATA
} relay_auth_type_t;

static int _stepmgr_connect(slurm_step_id_t *step_id,
			    uint16_t *protocol_version)
{
	int fd = SLURM_ERROR;

	step_id->step_id = SLURM_EXTERN_CONT;
	step_id->step_het_comp = NO_VAL;
	if ((fd = stepd_connect(conf->spooldir, conf->node_name, step_id,
				protocol_version)) == -1) {
		error("%s to %ps failed: %m", __func__, step_id);
	}

	return fd;
}

/*
 * NOTE: reply must be in sync with corresponding rpc handling in slurmstepd.
 */
static void _relay_stepd_msg(slurm_step_id_t *step_id, slurm_msg_t *msg,
			     relay_auth_type_t auth_type, bool reply)
{
	buf_t *resp_buf = NULL;
	int rc = SLURM_SUCCESS;
	uid_t job_uid;
	int stepmgr_fd = -1;
	uint16_t protocol_version;

	step_id->step_het_comp = NO_VAL; /* het jobs aren't supported. */

	job_uid = _get_job_uid(step_id->job_id);
	if (job_uid == INFINITE) {
		error("No stepd for jobid %u from uid %u for rpc %s",
		      step_id->job_id, msg->auth_uid,
		      rpc_num2string(msg->msg_type));
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	switch (auth_type) {
	case RELAY_AUTH_PRIVATE_DATA:
		if ((slurm_conf.private_data & PRIVATE_DATA_JOBS) &&
		    (job_uid != msg->auth_uid) &&
		    !_slurm_authorized_user(msg->auth_uid)) {
			error("Security violation, %s from uid %u",
			      rpc_num2string(msg->msg_type), msg->auth_uid);
			rc = ESLURM_USER_ID_MISSING;
			goto done;
		}
		break;
	case RELAY_AUTH_SLURM_USER:
		if ((job_uid != msg->auth_uid) &&
		    !_slurm_authorized_user(msg->auth_uid)) {
			error("Security violation, %s from uid %u",
			      rpc_num2string(msg->msg_type), msg->auth_uid);
			rc = ESLURM_USER_ID_MISSING;
			goto done;
		}
		break;
	case RELAY_AUTH_JOB:
	default:
		if (job_uid != msg->auth_uid) {
			error("Security violation, %s from uid %u",
			      rpc_num2string(msg->msg_type), msg->auth_uid);
			rc = ESLURM_USER_ID_MISSING;
			goto done;
		}
		break;
	}

	if ((stepmgr_fd = _stepmgr_connect(step_id, &protocol_version)) ==
	    SLURM_ERROR) {
		error("%s: Failed to connect to stepmgr", __func__);
		rc = SLURM_ERROR;
		goto done;
	}

	if (protocol_version < SLURM_25_05_PROTOCOL_VERSION) {
		log_flag(NET, "Relaying message %s to stepd stepmgr for %ps running version %d on fd %d",
			 rpc_num2string(msg->msg_type), step_id,
			 protocol_version, stepmgr_fd);

		if (stepd_relay_msg(stepmgr_fd, msg, protocol_version)) {
			error("%s: Failed to relay message %s to older stepmgr for %ps running version %d on fd %d",
			      __func__, rpc_num2string(msg->msg_type), step_id,
			      protocol_version, stepmgr_fd);
			rc = SLURM_ERROR;
			goto done;
		}
		/* stepd will reply back directly. */
		goto done;
	}

	if (stepd_proxy_send_recv_to_stepd(msg, &resp_buf, step_id, stepmgr_fd,
					   reply)) {
		error("%s: Failed to send/recv message %s to stepmgr for %ps",
		      __func__, rpc_num2string(msg->msg_type), step_id);
		rc = SLURM_ERROR;
		goto done;
	}

	if (!reply) {
		log_flag(NET, "Sent message %s to stepmgr for %ps (this RPC is send only, not waiting for response)",
		      rpc_num2string(msg->msg_type), step_id);
		goto done;
	}

	if (!resp_buf) {
		error("%s: Failed to get response buffer from stepmgr",
		      __func__);
		rc = SLURM_ERROR;
		goto done;
	}

	/* send response from stepd back to original client */
	if (resp_buf && (slurm_msg_sendto(msg->conn, get_buf_data(resp_buf),
					  size_buf(resp_buf)) < 0)) {
		error("%s: Failed to send response bufs", __func__);
		rc = SLURM_ERROR;
		goto done;
	}

	log_flag(NET, "Sent message %s to stepmgr for %ps. Got response buf size %d from stepmgr and forwarded buffer to %pA on fd %d",
		 rpc_num2string(msg->msg_type), step_id, size_buf(resp_buf),
		 &msg->address, stepmgr_fd);
done:
	fd_close(&stepmgr_fd);
	FREE_NULL_BUFFER(resp_buf);

	if (!rc)
		return;

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_job_step_create(slurm_msg_t *msg)
{
	job_step_create_request_msg_t *request = msg->data;

	_relay_stepd_msg(&request->step_id, msg, RELAY_AUTH_JOB, true);
}

static void _slurm_rpc_job_step_get_info(slurm_msg_t *msg)
{
	job_step_info_request_msg_t *request = msg->data;

	_relay_stepd_msg(&request->step_id, msg, RELAY_AUTH_PRIVATE_DATA, true);
}

static void _slurm_rpc_job_step_kill(slurm_msg_t *msg)
{
	job_step_kill_msg_t *request = msg->data;

	_relay_stepd_msg(&request->step_id, msg, RELAY_AUTH_SLURM_USER, true);
}

static void _slurm_rpc_srun_job_complete(slurm_msg_t *msg)
{
	srun_job_complete_msg_t *request = msg->data;
	slurm_step_id_t step_id = *request;

	_relay_stepd_msg(&step_id, msg, RELAY_AUTH_SLURM_USER, false);
}

static void _slurm_rpc_srun_node_fail(slurm_msg_t *msg)
{
	srun_node_fail_msg_t *request = msg->data;

	_relay_stepd_msg(&request->step_id, msg, RELAY_AUTH_SLURM_USER, false);
}

static void _slurm_rpc_srun_timeout(slurm_msg_t *msg)
{
	srun_timeout_msg_t *request = msg->data;

	_relay_stepd_msg(&request->step_id, msg, RELAY_AUTH_SLURM_USER, false);
}

static void _slurm_rpc_update_step(slurm_msg_t *msg)
{
	step_update_request_msg_t *request = msg->data;

	_relay_stepd_msg(&request->step_id, msg, RELAY_AUTH_SLURM_USER, true);
}

static void _slurm_rpc_step_layout(slurm_msg_t *msg)
{
	_relay_stepd_msg(msg->data, msg, RELAY_AUTH_PRIVATE_DATA, true);
}

static void _slurm_rpc_sbcast_cred(slurm_msg_t *msg)
{
	step_alloc_info_msg_t *request = msg->data;

	_relay_stepd_msg(&request->step_id, msg, RELAY_AUTH_SLURM_USER, true);
}

static void _slurm_het_job_alloc_info(slurm_msg_t *msg)
{
	job_alloc_info_msg_t *request = msg->data;

	_relay_stepd_msg(&request->step_id, msg, RELAY_AUTH_PRIVATE_DATA, true);
}

extern int send_slurmd_conf_lite(int fd, slurmd_conf_t *cf)
{
	int len;

	/*
	 * Wait for the registration to come back from the slurmctld so we have
	 * a TRES list to work with.
	 */
	if (!assoc_mgr_tres_list) {
		slurm_mutex_lock(&tres_mutex);
		slurm_cond_wait(&tres_cond, &tres_mutex);
		slurm_mutex_unlock(&tres_mutex);
	}

	slurm_mutex_lock(&cf->config_mutex);

	xassert(cf->buf);
	if (!tres_packed) {
		assoc_mgr_lock_t locks = { .tres = READ_LOCK };
		assoc_mgr_lock(&locks);
		if (assoc_mgr_tres_list) {
			slurm_pack_list(assoc_mgr_tres_list,
					slurmdb_pack_tres_rec, cf->buf,
					SLURM_PROTOCOL_VERSION);
		} else {
			fatal("%s: assoc_mgr_tres_list is NULL when trying to start a slurmstepd. This should never happen.",
			      __func__);
		}
		assoc_mgr_unlock(&locks);
		tres_packed = true;
	}

	len = get_buf_offset(cf->buf);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, get_buf_data(cf->buf), len);

	slurm_mutex_unlock(&cf->config_mutex);

	return (0);

rwfail:
	slurm_mutex_unlock(&cf->config_mutex);
	return (-1);
}

static int
_send_slurmstepd_init(int fd, int type, void *req, slurm_addr_t *cli,
		      hostlist_t *step_hset, uint16_t protocol_version)
{
	int len = 0;
	buf_t *buffer = NULL;
	slurm_msg_t msg;

	int rank;
	int parent_rank, children, depth, max_depth;
	char *parent_alias = NULL;

	slurm_msg_t_init(&msg);

	/* send conf over to slurmstepd */
	if (send_slurmd_conf_lite(fd, conf)) {
		error("%s: send_slurmd_conf_lite(%d) failed: %m", __func__, fd);
		goto fail;
	}

	/* send conf_hashtbl */
	if (read_conf_send_stepd(fd)) {
		error("%s: read_conf_send_stepd(%d) failed: %m", __func__, fd);
		goto fail;
	}

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
			launch_tasks_request_msg_t *launch_req = req;
			if (launch_req->step_id.step_id != SLURM_EXTERN_CONT)
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
	} else {
		int count;
		count = hostlist_count(step_hset);
		rank = hostlist_find(step_hset, conf->node_name);
		reverse_tree_info(rank, count, REVERSE_TREE_WIDTH,
				  &parent_rank, &children,
				  &depth, &max_depth);

		if (children == -1) {
			error("reverse_tree_info: Sanity check fail, can't start job");
			goto fail;
		}
		/*
		 * rank 0 always talks directly to the slurmctld. If
		 * parent_rank = -1, all nodes talk to the slurmctld
		 */
		if (rank > 0 && parent_rank != -1) {
			parent_alias = hostlist_nth(step_hset, parent_rank);
		}
	}
	debug3("slurmstepd rank %d (%s), parent rank %d (%s), "
	       "children %d, depth %d, max_depth %d",
	       rank, conf->node_name,
	       parent_rank, parent_alias ? parent_alias : "NONE",
	       children, depth, max_depth);

	/* send reverse-tree info to the slurmstepd */
	safe_write(fd, &rank, sizeof(int));
	safe_write(fd, &parent_rank, sizeof(int));
	safe_write(fd, &children, sizeof(int));
	safe_write(fd, &depth, sizeof(int));
	safe_write(fd, &max_depth, sizeof(int));
	if (parent_alias) {
		len = strlen(parent_alias);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, parent_alias, len);
		free(parent_alias);
		parent_alias = NULL;
	} else {
		len = 0;
		safe_write(fd, &len, sizeof(int));
	}
	/* send cli address over to slurmstepd */
	buffer = init_buf(0);
	slurm_pack_addr(cli, buffer);
	len = get_buf_offset(buffer);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, get_buf_data(buffer), len);
	FREE_NULL_BUFFER(buffer);

	/* send cpu_frequency info to slurmstepd */
	cpu_freq_send_info(fd);

	/* send req over to slurmstepd */
	switch (type) {
	case LAUNCH_BATCH_JOB:
		msg.msg_type = REQUEST_BATCH_JOB_LAUNCH;
		break;
	case LAUNCH_TASKS:
		msg.msg_type = REQUEST_LAUNCH_TASKS;
		break;
	default:
		error("Was sent a task I didn't understand");
		break;
	}
	buffer = init_buf(0);
	msg.data = req;

	/* always force the RPC format to the latest */
	msg.protocol_version = SLURM_PROTOCOL_VERSION;
	pack_msg(&msg, buffer);
	len = get_buf_offset(buffer);

	/* send the srun protocol_version over, which may be older */
	safe_write(fd, &protocol_version, sizeof(uint16_t));

	safe_write(fd, &len, sizeof(int));
	safe_write(fd, get_buf_data(buffer), len);
	FREE_NULL_BUFFER(buffer);

	/* send cgroup state over to slurmstepd */
	if (cgroup_write_state(fd)) {
		error("%s: cgroup_write_state(%d) failed: %m", __func__, fd);
		goto fail;
	}

	/*
	 * Send all secondary conf files to the stepd.
	 */

	/* send cgroup conf over to slurmstepd */
	if (cgroup_write_conf(fd)) {
		error("%s: cgroup_write_conf(%d) failed: %m",
		      __func__, fd);
		goto fail;
	}

	/* send acct_gather.conf over to slurmstepd */
	if (acct_gather_write_conf(fd)) {
		error("%s: acct_gather_write_conf(%d) failed: %m",
		      __func__, fd);
		goto fail;
	}

	/* Send job_container information to slurmstepd */
	if (namespace_g_send_stepd(fd)) {
		error("%s: namespace_g_send_stepd(%d) failed: %m",
		      __func__, fd);
		goto fail;
	}

	/* Send GRES information to slurmstepd */
	gres_g_send_stepd(fd, &msg);

	/* Send mpi.conf over to slurmstepd */
	if (type == LAUNCH_TASKS) {
		launch_tasks_request_msg_t *job = req;
		if ((job->step_id.step_id != SLURM_EXTERN_CONT) &&
		    (job->step_id.step_id != SLURM_INTERACTIVE_STEP)) {
			if (mpi_conf_send_stepd(fd, job->mpi_plugin_id)) {
				error("%s: mpi_conf_send_stepd(%d, %u) failed: %m",
				      __func__, fd, job->mpi_plugin_id);
				goto fail;
			}
		}
	}

	return 0;

rwfail:
	error("%s: failed: %m", __func__);
fail:
	FREE_NULL_BUFFER(buffer);
	return errno;
}

#if (SLURMSTEPD_MEMCHECK != 1)

static int _send_return_code(const time_t start_time, const int to_stepd,
			     const int forward_rc)
{
	int delta_time = time(NULL) - start_time;
	int cc = SLURM_SUCCESS;

	if (delta_time > 5) {
		warning("slurmstepd startup took %d sec, possible file system problem or full memory",
			delta_time);
	}

	if (forward_rc != SLURM_SUCCESS)
		error("slurmstepd return code %d: %s",
		      forward_rc, slurm_strerror(forward_rc));

	safe_write(to_stepd, &cc, sizeof(cc));
	return SLURM_SUCCESS;
rwfail:
	error("%s: failed to send ack to stepd: %m", __func__);
	return SLURM_ERROR;
}

static int _handle_return_code(int to_slurmd, int to_stepd, int *rc_ptr)
{
	time_t start_time = time(NULL);

	safe_read(to_slurmd, rc_ptr, sizeof(*rc_ptr));
	return _send_return_code(start_time, to_stepd, *rc_ptr);
rwfail:
	error("%s: Can not read return code from slurmstepd: %m", __func__);
	return SLURM_ERROR;
}

#endif /* SLURMSTEPD_MEMCHECK != 1 */

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
static int _forkexec_slurmstepd(uint16_t type, void *req, slurm_addr_t *cli,
				uid_t uid, uint32_t job_id, uint32_t step_id,
				hostlist_t *step_hset,
				uint16_t protocol_version)
{
	pid_t pid;
	int to_stepd[2] = {-1, -1};
	int to_slurmd[2] = {-1, -1};

	if (pipe(to_stepd) < 0 || pipe(to_slurmd) < 0) {
		error("%s: pipe failed: %m", __func__);
		return SLURM_ERROR;
	}

	if (_add_starting_step(type, req)) {
		error("%s: failed in _add_starting_step: %m", __func__);
		return SLURM_ERROR;
	}

	if ((pid = fork()) < 0) {
		error("%s: fork: %m", __func__);
		close(to_stepd[0]);
		close(to_stepd[1]);
		close(to_slurmd[0]);
		close(to_slurmd[1]);
		_remove_starting_step(type, req);
		return SLURM_ERROR;
	} else if (pid > 0) {
		int rc = SLURM_SUCCESS;
#if (SLURMSTEPD_MEMCHECK != 1)
		int rc2;
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
						req, cli, step_hset,
						protocol_version)) != 0) {
			error("Unable to init slurmstepd");
			goto done;
		}

		/*
		 * If running under memcheck, this pipe doesn't work correctly
		 * so just skip it.
		 */
#if (SLURMSTEPD_MEMCHECK != 1)
		if ((rc2 = _handle_return_code(to_slurmd[0],
					       to_stepd[1], &rc))) {
			rc = rc2;
			goto done;
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
		snprintf(log_file, sizeof(log_file),
			 "--log-file=/tmp/slurmstepd_valgrind_%u.%u",
			 job_id, step_id);
#elif (SLURMSTEPD_MEMCHECK == 3)
		/* valgrind/drd test of slurmstepd, option #3 */
		char log_file[256];
		char *const argv[10] = {"valgrind", "--tool=drd",
					"--error-limit=no",
					"--max-stackframe=16777216",
					"--num-callers=20",
					"--child-silent-after-fork=yes",
					log_file, (char *)conf->stepd_loc,
					NULL};
		snprintf(log_file, sizeof(log_file),
			 "--log-file=/tmp/slurmstepd_valgrind_%u.%u",
			 job_id, step_id);
#elif (SLURMSTEPD_MEMCHECK == 4)
		/* valgrind/helgrind test of slurmstepd, option #4 */
		char log_file[256];
		char *const argv[10] = {"valgrind", "--tool=helgrind",
					"--error-limit=no",
					"--max-stackframe=16777216",
					"--num-callers=20",
					"--child-silent-after-fork=yes",
					log_file, (char *)conf->stepd_loc,
					NULL};
		snprintf(log_file, sizeof(log_file),
			 "--log-file=/tmp/slurmstepd_valgrind_%u.%u",
			 job_id, step_id);
#else
		/* no memory checking, default */
		char *const argv[2] = { (char *)conf->stepd_loc, NULL};
#endif
		int i;
		int failed = 0;

		/*
		 * Child forks and exits
		 */
		if (setsid() < 0) {
			error("%s: setsid: %m", __func__);
			failed = 1;
		}

		if (step_id != SLURM_EXTERN_CONT) {
			slurm_step_id_t tmp_step_id = { NO_VAL64, job_id,
							step_id, NO_VAL };
			if (namespace_g_join(&tmp_step_id, uid, true)) {
				error("%s namespace_g_join(%u): %m",
				      __func__, job_id);
				_exit(SLURM_ERROR);
			}
		}

		if ((pid = fork()) < 0) {
			error("%s: Unable to fork grandchild: %m", __func__);
			failed = 2;
		} else if (pid > 0) { /* child */
			_exit(0);
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
			fd_close(&conf->lfd);

		if (close(to_stepd[1]) < 0)
			error("close write to_stepd in grandchild: %m");
		if (close(to_slurmd[0]) < 0)
			error("close read to_slurmd in parent: %m");

		(void) close(STDIN_FILENO); /* ignore return */
		if (dup2(to_stepd[0], STDIN_FILENO) == -1) {
			error("dup2 over STDIN_FILENO: %m");
			_exit(1);
		}
		fd_set_close_on_exec(to_stepd[0]);
		(void) close(STDOUT_FILENO); /* ignore return */
		if (dup2(to_slurmd[1], STDOUT_FILENO) == -1) {
			error("dup2 over STDOUT_FILENO: %m");
			_exit(1);
		}
		fd_set_close_on_exec(to_slurmd[1]);
		(void) close(STDERR_FILENO); /* ignore return */
		if (dup2(devnull, STDERR_FILENO) == -1) {
			error("dup2 /dev/null to STDERR_FILENO: %m");
			_exit(1);
		}
		fd_set_noclose_on_exec(STDERR_FILENO);
		log_fini();
		if (!failed) {
			execvp(argv[0], argv);
			error("exec of slurmstepd failed: %m");
		}
		_exit(2);
	}
}

static void _setup_x11_display(uint32_t job_id, uint32_t step_id_in,
			       char ***env, uint32_t *envc)
{
	int display = 0, fd;
	char *xauthority = NULL;
	uint16_t protocol_version;
	slurm_step_id_t step_id = { .job_id = job_id,
				    .step_id = SLURM_EXTERN_CONT,
				    .step_het_comp = NO_VAL,
	};

	fd = stepd_connect(conf->spooldir, conf->node_name,
			   &step_id,
			   &protocol_version);

	if (fd == -1) {
		error("Cannot connect to slurmstepd. Could not get x11 forwarding display for job %u step %u, x11 forwarding disabled",
		      job_id, step_id_in);
		return;
	}

	display = stepd_get_x11_display(fd, protocol_version, &xauthority);
	close(fd);

	if (!display) {
		error("Didn't get display. Could not get x11 forwarding display for job %u step %u, x11 forwarding disabled",
		      job_id, step_id_in);
		env_array_overwrite(env, "DISPLAY", "SLURM_X11_SETUP_FAILED");
		*envc = envcount(*env);
		return;
	}

	debug2("%s: setting DISPLAY=localhost:%d:0 for job %u step %u",
	       __func__, display, job_id, step_id_in);
	env_array_overwrite_fmt(env, "DISPLAY", "localhost:%d.0", display);

	if (xauthority) {
		env_array_overwrite(env, "XAUTHORITY", xauthority);
		xfree(xauthority);
	}

	*envc = envcount(*env);
}

/*
 * IN cred_hostlist the job credential host_list where to extract this node
 * host_index
 * RET The node host_index in relation to the argument cred_hostlist or -1 in
 * case of error
 */
static int _get_host_index(char *cred_hostlist)
{
	hostlist_t *hl;
	int host_index;
	if (!(hl = hostlist_create(cred_hostlist))) {
		error("Unable to parse credential hostlist: '%s'",
		      cred_hostlist);
		return -1;
	}
	host_index = hostlist_find(hl, conf->node_name);
	hostlist_destroy(hl);

	return host_index;
}

/*
 * IN cred the job credential from where to extract the memory
 * IN host_index used to get the sockets&core from the cred. If -1 is passed,
 * it is searched in the cred->hostlist based on conf->node_name.
 * OUT step_cpus the number of cpus used by the step
 * RET SLURM_SUCCESS on success SLURM_ERROR else
 */
static int _get_ncpus(slurm_cred_arg_t *cred, int host_index,
		      uint32_t *step_cpus)
{
	uint32_t hi, i, j, i_first_bit = 0, i_last_bit = 0;
	bool cpu_log = slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND;

	if (host_index == -1) {
		host_index = _get_host_index(cred->job_hostlist);

		if ((host_index < 0) || (host_index >= cred->job_nhosts)) {
			error("job cr credential invalid host_index %d for %pI",
			      host_index, &cred->step_id);
			return SLURM_ERROR;
		}
	}
	*step_cpus = 0;
	hi = host_index + 1;	/* change from 0-origin to 1-origin */
	for (i = 0; hi; i++) {
		if (hi > cred->sock_core_rep_count[i]) {
			i_first_bit += cred->sockets_per_node[i] *
				cred->cores_per_socket[i] *
				cred->sock_core_rep_count[i];
			hi -= cred->sock_core_rep_count[i];
		} else {
			i_first_bit += cred->sockets_per_node[i] *
				cred->cores_per_socket[i] *
				(hi - 1);
			i_last_bit = i_first_bit +
				cred->sockets_per_node[i] *
				cred->cores_per_socket[i];
			break;
		}
	}
	/* Now count the allocated processors */
	for (i = i_first_bit, j = 0; i < i_last_bit; i++, j++) {
		char *who_has = NULL;
		if (bit_test(cred->job_core_bitmap, i)) {
			who_has = "Job";
		}
		if (bit_test(cred->step_core_bitmap, i)) {
			(*step_cpus)++;
			who_has = "Step";
		}
		if (cpu_log && who_has) {
			log_flag(CPU_BIND, "JobNode[%u] CPU[%u] %s alloc",
				 host_index, j, who_has);
		}
	}
	if (cpu_log)
		log_flag(CPU_BIND, "====================");
	if (*step_cpus == 0) {
		error("Zero processors allocated to step");
		*step_cpus = 1;
	}
	/* NOTE: step_cpus is the count of allocated resources
	 * (typically cores). Convert to CPU count as needed */
	if (i_last_bit <= i_first_bit)
		error("step credential has no CPUs selected");
	else {
		i = conf->cpus / (i_last_bit - i_first_bit);
		if (i > 1) {
			if (cpu_log)
				log_flag(CPU_BIND, "Scaling CPU count by factor of %d (%u/(%u-%u))",
					 i, conf->cpus, i_last_bit,
					 i_first_bit);
			*step_cpus *= i;
		}
	}
	return SLURM_SUCCESS;
}

/*
 * The job(step) credential is the only place to get a definitive
 * list of the nodes allocated to a job step.  We need to return
 * a hostset_t of the nodes. Validate the incoming RPC, updating
 * job_mem needed.
 */
static int _check_job_credential(launch_tasks_request_msg_t *req,
				 uid_t auth_uid, gid_t auth_gid,
				 int node_id, hostlist_t **step_hset,
				 uint16_t protocol_version)
{
	slurm_cred_arg_t *arg;
	hostlist_t *s_hset = NULL;
	int		host_index = -1;
	slurm_cred_t    *cred = req->cred;
	int		tasks_to_launch = req->tasks_to_launch[node_id];
	uint32_t	step_cpus = 0;

	/*
	 * Update the request->cpus_per_task here. It may have been computed
	 * differently than the request if cpus_per_tres was requested instead
	 * of cpus_per_task. Do it here so the task plugin and slurmstepd have
	 * the correct value for cpus_per_task.
	 */
	if (req->cpt_compact_cnt) {
		int inx = slurm_get_rep_count_inx(req->cpt_compact_reps,
						  req->cpt_compact_cnt,
						  node_id);
		req->cpus_per_task = req->cpt_compact_array[inx];
	}

	if (req->flags & LAUNCH_NO_ALLOC) {
		if (_slurm_authorized_user(auth_uid)) {
			/* If we didn't allocate then the cred isn't valid, just
			 * skip checking. Only cool for root or SlurmUser */
			debug("%s: FYI, user %u is an authorized user running outside of an allocation",
			      __func__, auth_uid);
			return SLURM_SUCCESS;
		} else {
			error("%s: User %u is NOT authorized to run a job outside of an allocation",
			      __func__, auth_uid);
			errno = ESLURM_ACCESS_DENIED;
			return SLURM_ERROR;
		}
	}

	/*
	 * First call slurm_cred_verify() so that all credentials are checked
	 */
	if (!(arg = slurm_cred_verify(cred)))
		return SLURM_ERROR;

	/* Check that the credential cache doesn't have any concerns. */
	if (!cred_cache_valid(cred))
		goto fail;

	xassert(arg->job_mem_alloc);

	if ((arg->step_id.job_id != req->step_id.job_id) ||
	    (arg->step_id.step_id != req->step_id.step_id)) {
		error("job credential for %ps, expected %ps",
		      &arg->step_id, &req->step_id);
		goto fail;
	}

	if (arg->uid == SLURM_AUTH_NOBODY) {
		error("%s: rejecting job %u credential for invalid user nobody",
		      __func__, arg->step_id.job_id);
		goto fail;
	}

	if (arg->gid == SLURM_AUTH_NOBODY) {
		error("%s: rejecting job %u credential for invalid group nobody",
		      __func__, arg->step_id.job_id);
		goto fail;
	}

	identity_debug2(arg->id, __func__);

	xfree(req->gids);
	if (arg->id->ngids) {
		req->ngids = arg->id->ngids;
		req->gids = copy_gids(arg->id->ngids, arg->id->gids);
	} else {
		char *user_name = xstrdup(arg->id->pw_name);
		if (!user_name)
			user_name = uid_to_string(arg->uid);
		/*
		 * The gids were not sent in the cred, or dealing with an older
		 * RPC format, so retrieve from cache instead.
		 */
		req->ngids = group_cache_lookup(arg->uid, arg->gid, user_name,
						&req->gids);
		xfree(user_name);
	}

	/*
	 * Check that credential is valid for this host
	 */
	if (!(s_hset = hostlist_create(arg->step_hostlist))) {
		error("Unable to parse credential hostlist: `%s'",
		      arg->step_hostlist);
		goto fail;
	}

	if (hostlist_find(s_hset, conf->node_name) == -1) {
		error("Invalid %ps credential for user %u: host %s not in hostlist %s",
		      &arg->step_id, arg->uid, conf->node_name,
		      arg->step_hostlist);
		goto fail;
	}

	if ((arg->job_nhosts > 0) && (tasks_to_launch > 0)) {
		bool setup_x11 = false;

		host_index = _get_host_index(arg->job_hostlist);
		if ((host_index < 0) || (host_index >= arg->job_nhosts)) {
			error("job cr credential invalid host_index %d for job %u",
			      host_index, arg->step_id.job_id);
			goto fail;
		}

		/*
		 * handle the x11 flag bit here since we have access to the
		 * host_index already.
		 *
		 */
		if (!arg->job_x11)
			setup_x11 = false;
		else if (arg->job_x11 & X11_FORWARD_ALL)
			setup_x11 = true;
		/* assumes that the first node is the batch host */
		else if (((arg->job_x11 & X11_FORWARD_FIRST) ||
			  (arg->job_x11 & X11_FORWARD_BATCH))
			 && (host_index == 0))
			setup_x11 = true;
		else if ((arg->job_x11 & X11_FORWARD_LAST)
			 && (host_index == (req->nnodes - 1)))
			setup_x11 = true;

		/*
		 * Cannot complete x11 forwarding setup until after the prolog
		 * has completed. But we need to make a decision while we
		 * have convenient access to the credential args. So use
		 * the x11 field to signal the remaining setup is needed.
		 */
		if (setup_x11)
			req->x11 = X11_FORWARD_ALL;
		else
			req->x11 = 0;

		if (_get_ncpus(arg, host_index, &step_cpus))
			goto fail;
		if (tasks_to_launch > step_cpus) {
			/* This is expected with the --overcommit option
			 * or hyperthreads */
			debug("More than one tasks per logical processor (%d > %u) on host [%ps %u %s]",
			      tasks_to_launch, step_cpus, &arg->step_id,
			      arg->uid, arg->step_hostlist);
		}
	} else {
		step_cpus = 1;
	}

	/*
	 * Overwrite any memory limits in the RPC with contents of the
	 * memory limit within the credential.
	 */
	slurm_cred_get_mem(cred, conf->node_name, __func__, &req->job_mem_lim,
			   &req->step_mem_lim);

	/* Reset the CPU count on this node to correct value. */
	req->job_core_spec = arg->job_core_spec;
	req->node_cpus = step_cpus;

	*step_hset = s_hset;
	slurm_cred_unlock_args(cred);
	return SLURM_SUCCESS;

fail:
	FREE_NULL_HOSTLIST(s_hset);
	*step_hset = NULL;
	slurm_cred_unlock_args(cred);
	slurm_seterrno_ret(ESLURMD_INVALID_JOB_CREDENTIAL);
}

static int _find_libdir_record(void *x, void *arg)
{
	libdir_rec_t *l = (libdir_rec_t *) x;
	libdir_rec_t *key = (libdir_rec_t *) arg;

	if (l->uid != key->uid)
		return 0;
	if (l->job_id != key->job_id)
		return 0;
	if (l->step_id != key->step_id)
		return 0;
	if (xstrcmp(l->exe_fname, key->exe_fname))
		return 0;

	return 1;
}

static int _foreach_libdir_set_path(void *x, void *arg)
{
	libdir_rec_t *l = x;
	foreach_libdir_args_t *key = arg;

	if (l->uid != key->uid)
		return 1;
	if (l->job_id != key->job_id)
		return 1;
	if (l->step_id != key->step_id)
		return 1;

	if (key->new_path)
		xstrfmtcatat(key->new_path, &key->pos, "%s%s",
			     key->new_path ? ":" : "", l->directory);
	else
		xstrfmtcatat(key->new_path, &key->pos, "%s", l->directory);

	return 1;
}

static void _handle_libdir_fixup(launch_tasks_request_msg_t *req,
				 uid_t auth_uid)
{
	foreach_libdir_args_t arg = {
		.uid = auth_uid,
		.job_id = req->step_id.job_id,
		.step_id = req->step_id.step_id,
		.new_path = NULL,
		.pos = NULL,
	};
	char *orig;

	slurm_rwlock_rdlock(&file_bcast_lock);
	list_for_each_ro(bcast_libdir_list, _foreach_libdir_set_path, &arg);
	slurm_rwlock_unlock(&file_bcast_lock);

	if (!arg.new_path)
		return;

	if ((orig = getenvp(req->env, "LD_LIBRARY_PATH")))
		xstrfmtcatat(arg.new_path, &arg.pos, ":%s", orig);

	env_array_overwrite(&req->env, "LD_LIBRARY_PATH", arg.new_path);
	req->envc = envcount(req->env);
	xfree(arg.new_path);
}

static void
_rpc_launch_tasks(slurm_msg_t *msg)
{
	int      errnum = SLURM_SUCCESS;
	uint16_t port;
	char     host[HOST_NAME_MAX];
	launch_tasks_request_msg_t *req = msg->data;
	bool     first_job_run;
	char *errmsg = NULL;

	slurm_addr_t *cli = &msg->orig_addr;
	hostlist_t *step_hset = NULL;
	int node_id = 0;

	debug("%s: starting for %pI %ps",
	      __func__, &req->step_id, &req->step_id);

	node_id = nodelist_find(req->complete_nodelist, conf->node_name);
	memcpy(&req->orig_addr, &msg->orig_addr, sizeof(slurm_addr_t));

	if ((req->step_id.step_id == SLURM_INTERACTIVE_STEP) ||
	    (req->flags & LAUNCH_EXT_LAUNCHER)) {
		req->cpu_bind_type = CPU_BIND_NONE;
		xfree(req->cpu_bind);
		req->mem_bind_type = MEM_BIND_NONE;
		xfree(req->mem_bind);
	}

	if (node_id < 0) {
		info("%s: Invalid node list (%s not in %s)", __func__,
		     conf->node_name, req->complete_nodelist);
		errnum = ESLURM_INVALID_NODE_NAME;
		goto done;
	}

	slurm_get_ip_str(cli, host, sizeof(host));
	port = slurm_get_port(cli);
	if (req->het_job_id && (req->het_job_id != NO_VAL)) {
		info("launch task %u+%u.%u (%ps) request from UID:%u GID:%u HOST:%s PORT:%hu",
		     req->het_job_id, req->het_job_offset, req->step_id.step_id,
		     &req->step_id, msg->auth_uid, msg->auth_gid, host, port);
	} else {
		info("launch task %ps request from UID:%u GID:%u HOST:%s PORT:%hu",
		     &req->step_id, msg->auth_uid, msg->auth_gid, host, port);
	}

	/*
	 * Handle --send-libs support in srun by injecting the library cache
	 * directory in LD_LIBRARY_PATH.
	 */
	_handle_libdir_fixup(req, msg->auth_uid);

	/* this could be set previously and needs to be overwritten by
	 * this call for messages to work correctly for the new call */
	env_array_overwrite(&req->env, "SLURM_SRUN_COMM_HOST", host);
	req->envc = envcount(req->env);

	slurm_mutex_lock(&prolog_mutex);
	first_job_run = !cred_job_cached(&req->step_id);

	if (!(req->flags & LAUNCH_NO_ALLOC))
		errnum = _wait_for_request_launch_prolog(&req->step_id,
							 &first_job_run);
	if (errnum != SLURM_SUCCESS) {
		slurm_mutex_unlock(&prolog_mutex);
		goto done;
	}

	if (_check_job_credential(req, msg->auth_uid, msg->auth_gid, node_id,
				  &step_hset, msg->protocol_version) < 0) {
		errnum = errno;
		error("Invalid job credential from %u@%s: %m",
		      msg->auth_uid, host);
		slurm_mutex_unlock(&prolog_mutex);
		goto done;
	}

	/* Must follow _check_job_credential(), which sets some req fields */
	if ((errnum = task_g_slurmd_launch_request(req, node_id, &errmsg))) {
		slurm_mutex_unlock(&prolog_mutex);
		goto done;
	}

	if (first_job_run) {
		int rc;
		job_env_t job_env;
		list_t *job_gres_list, *gres_prep_env_list;

		cred_insert_job(&req->step_id);
		_add_job_running_prolog(&req->step_id);
		slurm_mutex_unlock(&prolog_mutex);

		memset(&job_env, 0, sizeof(job_env));
		job_gres_list = slurm_cred_get(req->cred,
					       CRED_DATA_JOB_GRES_LIST);
		gres_prep_env_list = gres_g_prep_build_env(
			job_gres_list, req->complete_nodelist);
		gres_g_prep_set_env(&job_env.gres_job_env,
				    gres_prep_env_list, node_id);
		FREE_NULL_LIST(gres_prep_env_list);

		job_env.step_id = req->step_id;
		job_env.node_list = req->complete_nodelist;
		job_env.het_job_id = req->het_job_id;
		job_env.spank_job_env = req->spank_job_env;
		job_env.spank_job_env_size = req->spank_job_env_size;
		job_env.work_dir = req->cwd;
		job_env.uid = msg->auth_uid;
		job_env.gid = msg->auth_gid;
		rc = run_prolog(&job_env, req->cred);
		_remove_job_running_prolog(&req->step_id);
		_free_job_env(&job_env);
		if (rc) {
			int term_sig = 0, exit_status = 0;
			if (WIFSIGNALED(rc))
				term_sig    = WTERMSIG(rc);
			else if (WIFEXITED(rc))
				exit_status = WEXITSTATUS(rc);
			error("[job %u] prolog failed status=%d:%d",
			      req->step_id.job_id, exit_status, term_sig);
			errnum = ESLURMD_PROLOG_FAILED;
			goto done;
		}
	} else {
		slurm_mutex_unlock(&prolog_mutex);
		_wait_for_job_running_prolog(&req->step_id);

		if (req->x11)
			_setup_x11_display(req->step_id.job_id,
					   req->step_id.step_id,
					   &req->env, &req->envc);
	}

	/*
	 * Since the job could have been killed while the prolog was running,
	 * test if the credential has since been revoked and exit as needed.
	 */
	if (cred_revoked(req->cred)) {
		info("%pI already killed, do not launch %ps",
		     &req->step_id, &req->step_id);
		errnum = SLURM_SUCCESS;
		goto done;
	}

	job_mem_limit_register(req->step_id.job_id, req->job_mem_lim);

	debug3("%s: call to _forkexec_slurmstepd", __func__);
	errnum = _forkexec_slurmstepd(LAUNCH_TASKS, (void *)req, cli, msg->auth_uid,
				      req->step_id.job_id, req->step_id.step_id,
				      step_hset, msg->protocol_version);
	debug3("%s: return from _forkexec_slurmstepd", __func__);

	launch_complete_add(&req->step_id);

done:
	FREE_NULL_HOSTLIST(step_hset);

	if (slurm_send_rc_err_msg(msg, errnum, errmsg) < 0) {
		error("%s: unable to send return code to address:port=%pA msg_type=%s: %m",
		      __func__, &msg->address, rpc_num2string(msg->msg_type));
	} else if (errnum == SLURM_SUCCESS) {
		save_cred_state();
	}

	/*
	 *  If job prolog failed, indicate failure to slurmctld
	 */
	if (errnum == ESLURMD_PROLOG_FAILED) {
		_launch_job_fail(&req->step_id, req->het_job_id, errnum);
		send_registration_msg(errnum);
	}

}

int _rm_file(const char *fpath, const struct stat *sb, int typeflag,
	     struct FTW *ftwbuf)
{
	if (remove(fpath)) {
		switch (typeflag) {
		case FTW_NS:
			error("%s: stat() call failed on path: %s",
			      __func__, fpath);
			break;
		case FTW_DNR:
			error("%s: Directory can't be read: %s",
			      __func__, fpath);
			break;
		}

		error("%s: Could not remove path: %s: %s",
		      __func__, fpath, strerror(errno));
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Open file based upon permissions of a different user
 * IN path_name - name of file to open
 * IN flags - flags to open() call
 * IN mode - mode to open() call
 * IN jobid - (optional) job id
 * IN uid - User ID to use for file access check
 * IN gid - Group ID to use for file access check
 * IN make_dir - if true, create a directory instead of a file
 * IN force - if true and the library directory already exists, replace it
 * OUT fd - File descriptor
 * RET error or SLURM_SUCCESS
 * */
static int _open_as_other(char *path_name, int flags, int mode, uint32_t jobid,
			  uid_t uid, gid_t gid, int ngids, gid_t *gids,
			  bool make_dir, bool force, int *fd)
{
	pid_t child;
	int pipe[2];
	int rc = 0;
	slurm_step_id_t tmp_step_id = { NO_VAL64, jobid, NO_VAL, NO_VAL };

	*fd = -1;

	/* child process will setuid to the user, register the process
	 * with the container, and open the file for us. */
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pipe) != 0) {
		error("%s: Failed to open pipe: %m", __func__);
		return SLURM_ERROR;
	}

	child = fork();
	if (child == -1) {
		error("%s: fork failure", __func__);
		close(pipe[0]);
		close(pipe[1]);
		return SLURM_ERROR;
	} else if (child > 0) {
		int exit_status = -1;
		close(pipe[0]);
		(void) waitpid(child, &rc, 0);
		if (WIFEXITED(rc) && (WEXITSTATUS(rc) == 0) && !make_dir)
			*fd = receive_fd_over_socket(pipe[1]);
		exit_status = WEXITSTATUS(rc);
		close(pipe[1]);
		return exit_status;
	}

	/* child process below here */

	close(pipe[1]);

	/* namespace_g_join needs to be called in the
	 * forked process part of the fork to avoid a race
	 * condition where if this process makes a file or
	 * detacts itself from a child before we add the pid
	 * to the container in the parent of the fork. */
	if (namespace_g_join(&tmp_step_id, uid, false)) {
		error("%s namespace_g_join(%u): %m", __func__, jobid);
		_exit(SLURM_ERROR);
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

	if (setgroups(ngids, gids) < 0) {
		error("%s: uid: %u setgroups failed: %m", __func__, uid);
		_exit(errno);
	}

	if (setgid(gid) < 0) {
		error("%s: uid:%u setgid(%u): %m", __func__, uid, gid);
		_exit(errno);
	}
	if (setresuid(uid, uid, -1) < 0) {
		error("%s: setresuid(%u, %u, %d): %m", __func__, uid, uid, -1);
		_exit(errno);
	}

	if (make_dir) {
		if (force &&
		    (nftw(path_name, _rm_file, 20, FTW_DEPTH | FTW_PHYS) < 0) &&
		    errno != ENOENT) {
			error("%s: uid:%u can't delete dir `%s` code %d: %m",
			      __func__, uid, path_name, errno);
			_exit(errno);
		}
		if (mkdir(path_name, mode) < 0) {
			error("%s: uid:%u can't create dir `%s` code %d: %m",
			      __func__, uid, path_name, errno);
			_exit(errno);
		}
		_exit(SLURM_SUCCESS);
	}

	*fd = open(path_name, flags, mode);
	if (*fd == -1) {
		error("%s: uid:%u can't open `%s` code %d: %m",
		      __func__, uid, path_name, errno);
		_exit(errno);
	}
	send_fd_over_socket(pipe[0], *fd);
	close(*fd);
	_exit(SLURM_SUCCESS);
}

/*
 * Connect to unix socket based upon permissions of a different user
 * IN sock_name - name of socket to open
 * IN uid - User ID to use for file access check
 * IN gid - Group ID to use for file access check
 * OUT fd - File descriptor
 * RET error or SLURM_SUCCESS
 * */
static int _connect_as_other(char *sock_name, uid_t uid, gid_t gid, int *fd)
{
	pid_t child;
	int pipe[2];
	int rc = 0;
	struct sockaddr_un sa;

	*fd = -1;
	if (strlen(sock_name) >= sizeof(sa.sun_path)) {
		error("%s: Unix socket path '%s' is too long. (%ld > %ld)",
		      __func__, sock_name,
		      (long int)(strlen(sock_name) + 1),
		      (long int)sizeof(sa.sun_path));
		return EINVAL;
	}

	/* child process will setuid to the user, register the process
	 * with the container, and open the file for us. */
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pipe) != 0) {
		error("%s: Failed to open pipe: %m", __func__);
		return SLURM_ERROR;
	}

	child = fork();
	if (child == -1) {
		error("%s: fork failure", __func__);
		close(pipe[0]);
		close(pipe[1]);
		return SLURM_ERROR;
	} else if (child > 0) {
		int exit_status = -1;
		close(pipe[0]);
		(void) waitpid(child, &rc, 0);
		if (WIFEXITED(rc) && (WEXITSTATUS(rc) == 0))
			*fd = receive_fd_over_socket(pipe[1]);
		exit_status = WEXITSTATUS(rc);
		close(pipe[1]);
		return exit_status;
	}

	/* child process below here */

	close(pipe[1]);

	if (setgid(gid) < 0) {
		error("%s: uid:%u setgid(%u): %m", __func__, uid, gid);
		_exit(errno);
	}
	if (setuid(uid) < 0) {
		error("%s: getuid(%u): %m", __func__, uid);
		_exit(errno);
	}

	if ((rc = slurm_open_unix_stream(sock_name, 0, fd))) {
		_exit(rc);
	}
	send_fd_over_socket(pipe[0], *fd);
	close(*fd);
	_exit(SLURM_SUCCESS);
}

/* load the user's environment on this machine if requested
 * SLURM_GET_USER_ENV environment variable is set */
static int _get_user_env(batch_job_launch_msg_t *req, char *user_name)
{
	char **new_env;
	int i;

	for (i=0; i<req->envc; i++) {
		if (xstrcmp(req->environment[i], "SLURM_GET_USER_ENV=1") == 0)
			break;
	}
	if (i >= req->envc)
		return 0;		/* don't need to load env */

	verbose("%s: get env for user %s here", __func__, user_name);

	/* Permit delay before failing env retrieval */
	new_env = env_array_user_default(user_name);
	if (! new_env) {
		error("%s: Unable to get user's local environment",
		      __func__);
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
static void _set_batch_job_limits(batch_job_launch_msg_t *req)
{
	slurm_cred_arg_t *arg = slurm_cred_get_args(req->cred);

	req->job_core_spec = arg->job_core_spec; /* Prevent user reset */

	slurm_cred_get_mem(req->cred, conf->node_name, __func__, &req->job_mem,
			   NULL);

	/*
	 * handle x11 settings here since this is the only access to the cred
	 * on the batch step.
	 */
	if ((arg->job_x11 & X11_FORWARD_ALL) ||
	    (arg->job_x11 & X11_FORWARD_BATCH))
		_setup_x11_display(req->step_id.job_id, SLURM_BATCH_SCRIPT,
				   &req->environment, &req->envc);

	slurm_cred_unlock_args(req->cred);
}

/* These functions prevent a possible race condition if the batch script's
 * complete RPC is processed before it's launch_successful response. This
 *  */
static bool _is_batch_job_finished(slurm_step_id_t *step_id)
{
	bool found_job = false;
	int i;

	slurm_mutex_lock(&fini_job_mutex);
	for (i = 0; i < fini_job_cnt; i++) {
		if (fini_job_id[i] == step_id->job_id) {
			found_job = true;
			break;
		}
	}
	slurm_mutex_unlock(&fini_job_mutex);

	return found_job;
}

static void _note_batch_job_finished(slurm_step_id_t *step_id)
{
	slurm_mutex_lock(&fini_job_mutex);
	fini_job_id[next_fini_job_inx] = step_id->job_id;
	if (++next_fini_job_inx >= fini_job_cnt)
		next_fini_job_inx = 0;
	slurm_mutex_unlock(&fini_job_mutex);
}

/* Send notification to slurmctld we are finished running the prolog.
 * This is needed on system that don't use srun to launch their tasks.
 */
static int _notify_slurmctld_prolog_fini(slurm_step_id_t *step_id,
					 uint32_t prolog_return_code)
{
	int rc, ret_c;
	slurm_msg_t req_msg;
	prolog_complete_msg_t req;

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.node_name	= conf->node_name;
	req.prolog_rc	= prolog_return_code;
	req.step_id = *step_id;

	req_msg.msg_type = REQUEST_COMPLETE_PROLOG;
	req_msg.data	= &req;

	/*
	 * Here we only care about the return code of
	 * slurm_send_recv_controller_rc_msg since it means there was a
	 * communication failure and we may need to try again.
	 */
	if ((ret_c = slurm_send_recv_controller_rc_msg(
		     &req_msg, &rc, working_cluster_rec)))
		error("Error sending prolog completion notification: %m");

	return ret_c;
}

/* Convert memory limits from per-CPU to per-node */
static int _convert_job_mem(slurm_msg_t *msg)
{
	prolog_launch_msg_t *req = msg->data;
	slurm_cred_arg_t *arg = slurm_cred_get_args(req->cred);

	if (req->nnodes > arg->job_nhosts) {
		error("%s: request node count:%u is larger than cred job node count:%u",
		      __func__, req->nnodes, arg->job_nhosts);
		return ESLURM_INVALID_NODE_COUNT;
	}

	req->nnodes = arg->job_nhosts;

	slurm_cred_get_mem(req->cred, conf->node_name, __func__,
			   &req->job_mem_limit, NULL);

	slurm_cred_unlock_args(req->cred);
	return SLURM_SUCCESS;
}

static int _make_prolog_mem_container(slurm_msg_t *msg)
{
	prolog_launch_msg_t *req = msg->data;
	int rc = SLURM_SUCCESS;

	/* Convert per-CPU mem limit */
	if ((rc = _convert_job_mem(msg)) != SLURM_SUCCESS)
		return rc;

	job_mem_limit_register(req->step_id.job_id, req->job_mem_limit);

	return rc;
}

static int _spawn_prolog_stepd(slurm_msg_t *msg)
{
	prolog_launch_msg_t *req = msg->data;
	launch_tasks_request_msg_t *launch_req;
	slurm_addr_t *cli = &msg->orig_addr;
	int rc = SLURM_SUCCESS;
	int i;

	launch_req = xmalloc(sizeof(launch_tasks_request_msg_t));
	launch_req->step_id = req->step_id;
	launch_req->complete_nodelist	= req->nodes;
	launch_req->cpus_per_task	= 1;
	launch_req->cred_version = msg->protocol_version;
	launch_req->cred		= req->cred;
	launch_req->cwd			= req->work_dir;
	launch_req->efname		= "/dev/null";
	launch_req->global_task_ids	= xcalloc(req->nnodes,
						  sizeof(uint32_t *));
	launch_req->ifname		= "/dev/null";
	launch_req->job_mem_lim		= req->job_mem_limit;
	launch_req->nnodes		= req->nnodes;
	launch_req->ntasks		= req->nnodes;
	launch_req->ofname		= "/dev/null";

	launch_req->het_job_id		= req->het_job_id;
	launch_req->het_job_nnodes	= NO_VAL;

	launch_req->spank_job_env_size	= req->spank_job_env_size;
	launch_req->spank_job_env	= req->spank_job_env;
	launch_req->step_mem_lim	= req->job_mem_limit;
	launch_req->tasks_to_launch	= xcalloc(req->nnodes,
						  sizeof(uint16_t));
	launch_req->alloc_tls_cert = req->alloc_tls_cert;

	launch_req->job_ptr = req->job_ptr;
	launch_req->job_node_array = req->job_node_array;
	launch_req->part_ptr = req->part_ptr;

	/*
	 * determine which node this is in the allocation and if
	 * it should setup the x11 forwarding or not
	 */
	if (req->x11) {
		bool setup_x11 = false;
		int host_index = -1;
		hostlist_t *j_hset;
		/*
		 * Determine need to setup X11 based upon this node's index into
		 * the _job's_ allocation
		 */
		if (req->x11 & X11_FORWARD_ALL) {
			;	/* Don't need host_index */
		} else if (!(j_hset = hostlist_create(req->nodes))) {
			error("Unable to parse hostlist: `%s'", req->nodes);
		} else {
			host_index = hostlist_find(j_hset, conf->node_name);
			hostlist_destroy(j_hset);
		}

		if (req->x11 & X11_FORWARD_ALL)
			setup_x11 = true;
		/* assumes that the first node is the batch host */
		else if (((req->x11 & X11_FORWARD_FIRST) ||
			  (req->x11 & X11_FORWARD_BATCH))
			 && (host_index == 0))
			setup_x11 = true;
		else if ((req->x11 & X11_FORWARD_LAST)
			 && (host_index == (req->nnodes - 1)))
			setup_x11 = true;

		if (setup_x11) {
			launch_req->x11 = req->x11;
			launch_req->x11_alloc_host = req->x11_alloc_host;
			launch_req->x11_alloc_port = req->x11_alloc_port;
			launch_req->x11_magic_cookie = req->x11_magic_cookie;
			launch_req->x11_target = req->x11_target;
			launch_req->x11_target_port = req->x11_target_port;
		}
	}

	for (i = 0; i < req->nnodes; i++) {
		uint32_t *tmp32 = xmalloc(sizeof(uint32_t));
		*tmp32 = i;
		launch_req->global_task_ids[i] = tmp32;
		launch_req->tasks_to_launch[i] = 1;
	}

	/*
	 * Since job could have been killed while the prolog was
	 * running (especially on BlueGene, which can take minutes
	 * for partition booting). Test if the credential has since
	 * been revoked and exit as needed.
	 */
	if (cred_revoked(req->cred)) {
		info("%pI already killed, do not launch extern step",
		     &req->step_id);
		/*
		 * Don't set the rc to SLURM_ERROR at this point.
		 * The job's already been killed, and returning a prolog
		 * failure will just add more confusion. Better to just
		 * silently terminate.
		 */
	} else {
		hostlist_t *step_hset = hostlist_create(req->nodes);
		int forkexec_rc;

		debug3("%s: call to _forkexec_slurmstepd", __func__);
		forkexec_rc = _forkexec_slurmstepd(LAUNCH_TASKS,
						   (void *) launch_req, cli,
						   req->uid,
						   req->step_id.job_id,
						   SLURM_EXTERN_CONT,
						   step_hset,
						   msg->protocol_version);
		debug3("%s: return from _forkexec_slurmstepd %d",
		       __func__, forkexec_rc);

		if (forkexec_rc != SLURM_SUCCESS) {
			_launch_job_fail(&req->step_id, req->het_job_id,
					 forkexec_rc);

			if (forkexec_rc == ESLURMD_PROLOG_FAILED)
				rc = forkexec_rc;
		}

		FREE_NULL_HOSTLIST(step_hset);
	}

	for (i = 0; i < req->nnodes; i++)
		xfree(launch_req->global_task_ids[i]);
	xfree(launch_req->global_task_ids);
	xfree(launch_req->tasks_to_launch);
	xfree(launch_req);

	return rc;
}

static void _notify_result_rpc_prolog(prolog_launch_msg_t *req, int rc)
{
	int alt_rc = SLURM_ERROR;

	/*
	 * We need the slurmctld to know we are done or we can get into a
	 * situation where nothing from the job will ever launch because the
	 * prolog will never appear to stop running.
	 */
	while (alt_rc != SLURM_SUCCESS) {
		if (!(slurm_conf.prolog_flags & PROLOG_FLAG_NOHOLD))
			alt_rc = _notify_slurmctld_prolog_fini(&req->step_id, rc);
		else
			alt_rc = SLURM_SUCCESS;

		if (rc != SLURM_SUCCESS) {
			alt_rc = _launch_job_fail(&req->step_id,
						  req->het_job_id, rc);
			send_registration_msg(rc);
		}

		if (alt_rc != SLURM_SUCCESS) {
			info("%s: Retrying prolog complete RPC for %pI [sleeping %us]",
			     __func__, &req->step_id, RETRY_DELAY);
			sleep(RETRY_DELAY);
		}
	}
}

static void _rpc_prolog(slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	prolog_launch_msg_t *req = msg->data;

	if (req == NULL)
		return;

	debug("%s: starting for %pI %ps",
	      __func__, &req->step_id, &req->step_id);

	/*
	 * Send message back to the slurmctld so it knows we got the rpc.  A
	 * prolog could easily run way longer than a MessageTimeout or we would
	 * just wait.
	 */
	if (slurm_send_rc_msg(msg, rc) < 0) {
		error("%s: Error talking to slurmctld: %m", __func__);
	}

	cred_handle_reissue(req->cred, false);

	slurm_mutex_lock(&prolog_mutex);

	if (cred_job_cached(&req->step_id)) {
		/* prolog has already run */
		slurm_mutex_unlock(&prolog_mutex);
		_notify_result_rpc_prolog(req, rc);
		return;
	}

	if (slurm_conf.prolog_flags & PROLOG_FLAG_CONTAIN &&
	    ((rc = _make_prolog_mem_container(msg)) != SLURM_SUCCESS)) {
		error("%s: aborting prolog due to _make_prolog_mem_container failure: %s. Consider increasing cred_expire window if job prologs take large amount of time.",
		      __func__, slurm_strerror(rc));
		slurm_mutex_unlock(&prolog_mutex);
		_notify_result_rpc_prolog(req, rc);
		return;
	}

	cred_insert_job(&req->step_id);
	_add_job_running_prolog(&req->step_id);
	/* signal just in case the batch rpc got here before we did */
	slurm_cond_broadcast(&conf->prolog_running_cond);
	slurm_mutex_unlock(&prolog_mutex);

	if (!(slurm_conf.prolog_flags & PROLOG_FLAG_RUN_IN_JOB)) {
		job_env_t job_env;
		int node_id = nodelist_find(req->nodes, conf->node_name);

		memset(&job_env, 0, sizeof(job_env));
		gres_g_prep_set_env(&job_env.gres_job_env, req->job_gres_prep,
				    node_id);

		job_env.step_id = req->step_id;
		job_env.node_list = req->nodes;
		job_env.het_job_id = req->het_job_id;
		job_env.spank_job_env = req->spank_job_env;
		job_env.spank_job_env_size = req->spank_job_env_size;
		job_env.work_dir = req->work_dir;
		job_env.uid = req->uid;
		job_env.gid = req->gid;

		rc = run_prolog(&job_env, req->cred);
		_free_job_env(&job_env);
		if (rc) {
			int term_sig = 0, exit_status = 0;
			if (WIFSIGNALED(rc))
				term_sig = WTERMSIG(rc);
			else if (WIFEXITED(rc))
				exit_status = WEXITSTATUS(rc);
			error("[job %u] prolog failed status=%d:%d",
			      req->step_id.job_id, exit_status, term_sig);
			rc = ESLURMD_PROLOG_FAILED;
		}
	}

	if ((rc == SLURM_SUCCESS) &&
	    (slurm_conf.prolog_flags & PROLOG_FLAG_CONTAIN))
		rc = _spawn_prolog_stepd(msg);

	/*
	 * Revoke cred so that the slurmd won't launch tasks if the prolog
	 * failed. The slurmd waits for the prolog to finish, but can't check
	 * the return code.
	 */
	if (rc)
		cred_revoke(&req->step_id, time(NULL), time(NULL));

	_remove_job_running_prolog(&req->step_id);

	_notify_result_rpc_prolog(req, rc);
}

static void _rpc_batch_job(slurm_msg_t *msg)
{
	slurm_cred_arg_t *cred_arg;
	batch_job_launch_msg_t *req = msg->data;
	char *user_name = NULL;
	bool     first_job_run;
	int      rc = SLURM_SUCCESS, node_id = 0;
	bool	 replied = false, revoked;
	slurm_addr_t *cli = &msg->orig_addr;
	uid_t batch_uid = SLURM_AUTH_NOBODY;
	gid_t batch_gid = SLURM_AUTH_NOBODY;

	debug("%s: starting for %pI %ps",
	      __func__, &req->step_id, &req->step_id);

	if (launch_job_test(&req->step_id)) {
		error("%pI already running, do not launch second copy",
		      &req->step_id);
		rc = ESLURM_DUPLICATE_JOB_ID;	/* job already running */
		_launch_job_fail(&req->step_id, req->het_job_id, rc);
		goto done;
	}

	cred_handle_reissue(req->cred, false);
	if (cred_revoked(req->cred)) {
		error("%pI already killed, do not launch batch job",
		      &req->step_id);
		rc = ESLURMD_CREDENTIAL_REVOKED;	/* job already ran */
		goto done;
	}

	cred_arg = slurm_cred_get_args(req->cred);
	batch_uid = cred_arg->uid;
	batch_gid = cred_arg->gid;
	/* If available, use the cred to fill in username. */
	if (cred_arg->id->pw_name)
		user_name = xstrdup(cred_arg->id->pw_name);
	else
		user_name = uid_to_string(batch_uid);

	xfree(req->gids); /* Never sent by slurmctld */
	/* If available, use the cred to fill in groups */
	if (cred_arg->id->ngids) {
		req->ngids = cred_arg->id->ngids;
		req->gids = copy_gids(cred_arg->id->ngids, cred_arg->id->gids);
	} else
		req->ngids = group_cache_lookup(batch_uid, batch_gid,
						user_name, &req->gids);
	slurm_cred_unlock_args(req->cred);

	task_g_slurmd_batch_request(req);	/* determine task affinity */

	slurm_mutex_lock(&prolog_mutex);
	first_job_run = !cred_job_cached(&req->step_id);

	/* BlueGene prolog waits for partition boot and is very slow.
	 * On any system we might need to load environment variables
	 * for Moab (see --get-user-env), which could also be slow.
	 * Just reply now and send a separate kill job request if the
	 * prolog or launch fail. */
	replied = true;
	if (slurm_send_rc_msg(msg, rc)) {
		/* The slurmctld is no longer waiting for a reply.
		 * This typically indicates that the slurmd was
		 * blocked from memory and/or CPUs and the slurmctld
		 * has requeued the batch job request. */
		error("Could not confirm batch launch for %pI, aborting request",
		      &req->step_id);
		rc = SLURM_COMMUNICATIONS_SEND_ERROR;
		slurm_mutex_unlock(&prolog_mutex);
		goto done;
	}

	rc = _wait_for_request_launch_prolog(&req->step_id, &first_job_run);
	if (rc != SLURM_SUCCESS) {
		slurm_mutex_unlock(&prolog_mutex);
		goto done;
	}

	/*
	 * Insert jobid into credential context to denote that
	 * we've now "seen" an instance of the job
	 */
	if (first_job_run) {
		job_env_t job_env;
		list_t *job_gres_list, *gres_prep_env_list;

		cred_insert_job(&req->step_id);
		_add_job_running_prolog(&req->step_id);
		slurm_mutex_unlock(&prolog_mutex);

		node_id = nodelist_find(req->nodes, conf->node_name);
		memset(&job_env, 0, sizeof(job_env));
		job_gres_list = slurm_cred_get(req->cred,
					       CRED_DATA_JOB_GRES_LIST);
		gres_prep_env_list = gres_g_prep_build_env(job_gres_list,
							   req->nodes);
		gres_g_prep_set_env(&job_env.gres_job_env,
				    gres_prep_env_list, node_id);
		FREE_NULL_LIST(gres_prep_env_list);
		job_env.step_id = req->step_id;
		job_env.node_list = req->nodes;
		job_env.het_job_id = req->het_job_id;
		job_env.partition = req->partition;
		job_env.spank_job_env = req->spank_job_env;
		job_env.spank_job_env_size = req->spank_job_env_size;
		job_env.work_dir = req->work_dir;
		job_env.uid = batch_uid;
		job_env.gid = batch_gid;
		/*
	 	 * Run job prolog on this node
	 	 */

		rc = run_prolog(&job_env, req->cred);
		_remove_job_running_prolog(&req->step_id);
		_free_job_env(&job_env);
		if (rc) {
			int term_sig = 0, exit_status = 0;
			if (WIFSIGNALED(rc))
				term_sig    = WTERMSIG(rc);
			else if (WIFEXITED(rc))
				exit_status = WEXITSTATUS(rc);
			error("%pI prolog failed status=%d:%d",
			      &req->step_id, exit_status, term_sig);
			rc = ESLURMD_PROLOG_FAILED;
			goto done;
		}
	} else {
		slurm_mutex_unlock(&prolog_mutex);
		_wait_for_job_running_prolog(&req->step_id);
	}

	if (_get_user_env(req, user_name) < 0) {
		rc = ESLURMD_SETUP_ENVIRONMENT_ERROR;
		goto done;
	}
	_set_batch_job_limits(msg->data);

	/* Since job could have been killed while the prolog was
	 * running (especially on BlueGene, which can take minutes
	 * for partition booting). Test if the credential has since
	 * been revoked and exit as needed. */
	if (cred_revoked(req->cred)) {
		info("%pI already killed, do not launch batch job",
		     &req->step_id);
		rc = SLURM_SUCCESS;     /* job already ran */
		goto done;
	}

	info("Launching batch %pI for UID %u", &req->step_id, batch_uid);

	debug3("%s: call to _forkexec_slurmstepd", __func__);
	rc = _forkexec_slurmstepd(LAUNCH_BATCH_JOB, (void *)req, cli, batch_uid,
				  req->step_id.job_id, SLURM_BATCH_SCRIPT,
				  NULL, SLURM_PROTOCOL_VERSION);
	debug3("%s: return from _forkexec_slurmstepd: %d", __func__, rc);

	launch_complete_add(&req->step_id);

	/* On a busy system, slurmstepd may take a while to respond,
	 * if the job was cancelled in the interim, run through the
	 * abort logic below. */
	revoked = cred_revoked(req->cred);
	if (revoked)
		launch_complete_rm(&req->step_id);
	if (revoked && _is_batch_job_finished(&req->step_id)) {
		/* If configured with select/serial and the batch job already
		 * completed, consider the job successfully launched and do
		 * not repeat termination logic below, which in the worst case
		 * just slows things down with another message. */
		revoked = false;
	}
	if (revoked) {
		info("%pI killed while launch was in progress",
		     &req->step_id);
		sleep(1);	/* give slurmstepd time to create
				 * the communication socket */
		terminate_all_steps(req->step_id.job_id, true,
				    !(slurm_conf.prolog_flags &
				      PROLOG_FLAG_RUN_IN_JOB));
		rc = ESLURMD_CREDENTIAL_REVOKED;
		goto done;
	}

done:
	if (!replied) {
		if (slurm_send_rc_msg(msg, rc)) {
			/* The slurmctld is no longer waiting for a reply.
			 * This typically indicates that the slurmd was
			 * blocked from memory and/or CPUs and the slurmctld
			 * has requeued the batch job request. */
			error("Could not confirm batch launch for %pI, aborting request",
			      &req->step_id);
			rc = SLURM_COMMUNICATIONS_SEND_ERROR;
		} else {
			/* No need to initiate separate reply below */
			rc = SLURM_SUCCESS;
		}
	}
	if (rc != SLURM_SUCCESS) {
		/* prolog or job launch failure,
		 * tell slurmctld that the job failed */
		_launch_job_fail(&req->step_id, req->het_job_id, rc);
	}

	/*
	 *  If job prolog failed or we could not reply,
	 *  initiate message to slurmctld with current state
	 */
	if ((rc == ESLURMD_PROLOG_FAILED) ||
	    (rc == SLURM_COMMUNICATIONS_SEND_ERROR)) {
		send_registration_msg(rc);
	}
	xfree(user_name);
}

/*
 * Send notification message to batch job
 */
static void
_rpc_job_notify(slurm_msg_t *msg)
{
	job_notify_msg_t *req = msg->data;
	uid_t job_uid;
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd = NULL;
	int step_cnt  = 0;
	int fd;

	debug("%s: uid = %u, %ps", __func__, msg->auth_uid, &req->step_id);
	job_uid = _get_job_uid(req->step_id.job_id);
	if (job_uid == INFINITE)
		goto no_job;

	/*
	 * check that requesting user ID is the Slurm UID or root
	 */
	if ((msg->auth_uid != job_uid) &&
	    !_slurm_authorized_user(msg->auth_uid)) {
		error("Security violation: job_notify(%u) from uid %u",
		      req->step_id.job_id, msg->auth_uid);
		return;
	}

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if ((stepd->step_id.job_id  != req->step_id.job_id) ||
		    (stepd->step_id.step_id != SLURM_BATCH_SCRIPT)) {
			continue;
		}

		step_cnt++;

		fd = stepd_connect(stepd->directory, stepd->nodename,
				   &stepd->step_id, &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to %ps", &stepd->step_id);
			continue;
		}

		info("send notification to %ps", &stepd->step_id);
		if (stepd_notify_job(fd, stepd->protocol_version,
				     req->message) < 0)
			debug("notify jobid=%u failed: %m",
			      stepd->step_id.job_id);
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

no_job:
	if (step_cnt == 0) {
		debug2("No steps running for jobid %u to send notification message",
		       req->step_id.job_id);
	}
}

/* Wrapper for slurm_kill_job() */
static uint32_t _kill_job(uint32_t job_id)
{
	/*
	 * If we have a meta array job, kill only it and not the whole array
	 * job.
	 */
	return slurm_kill_job(job_id, SIGKILL, KILL_ARRAY_TASK);
}

/* Wrapper for slurm_kill_job() */
static uint32_t _kill_fail_job(uint32_t job_id)
{
	/*
	 * If we have a meta array job, kill only it and not the whole array
	 * job.
	 */
	return slurm_kill_job(job_id, SIGKILL, KILL_ARRAY_TASK | KILL_FAIL_JOB);
}

static int _launch_job_fail(slurm_step_id_t *step_id, uint32_t het_job_id,
			    uint32_t slurm_rc)
{
	requeue_msg_t req_msg = { { 0 } };
	slurm_msg_t resp_msg;
	int rc = 0, rpc_rc;
	uint32_t job_id = step_id->job_id;

	if (het_job_id && (het_job_id != NO_VAL))
		job_id = het_job_id;

	slurm_msg_t_init(&resp_msg);

	if (slurm_rc == ESLURMD_CREDENTIAL_REVOKED)
		return _kill_job(job_id);
	if (slurm_rc == ESPANK_JOB_FAILURE)
		return _kill_fail_job(job_id);

	/* Try to requeue the job. If that doesn't work, kill the job. */
	req_msg.step_id = *step_id;
	req_msg.step_id.job_id = job_id;
	req_msg.job_id_str = NULL;
	req_msg.flags = JOB_LAUNCH_FAILED;
	if (slurm_rc == ESLURMD_SETUP_ENVIRONMENT_ERROR)
		req_msg.flags |= JOB_GETENV_FAILED;
	resp_msg.msg_type = REQUEST_JOB_REQUEUE;
	resp_msg.data = &req_msg;
	rpc_rc = slurm_send_recv_controller_rc_msg(&resp_msg, &rc,
						   working_cluster_rec);

	if ((rc == ESLURM_DISABLED) || (rc == ESLURM_BATCH_ONLY)) {
		info("Could not launch job %u and not able to requeue it, "
		     "cancelling job", job_id);

		if (slurm_rc == ESLURMD_PROLOG_FAILED) {
			/*
			 * Send the job's stdout a message, whether or not it's
			 * a batch job. ESLURM_DISABLED can take priority over
			 * ESLURM_BATCH_ONLY so we have no way to tell if it's
			 * a batch job or not.
			 */
			char *buf = NULL;
			xstrfmtcat(buf, "Prolog failure on node %s",
				   conf->node_name);
			slurm_notify_job(job_id, buf);
			xfree(buf);
		}
		rpc_rc = _kill_job(job_id);
	}

	return rpc_rc;
}

static void _rpc_set_slurmd_debug_flags(slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;

	if (!_slurm_authorized_user(msg->auth_uid)) {
		error("Security violation, %s from uid %u",
		      rpc_num2string(msg->msg_type), msg->auth_uid);
		rc = ESLURM_USER_ID_MISSING;
	} else {
		char *flag_string = NULL;
		slurm_conf_t *cf = NULL;
		set_debug_flags_msg_t *request_msg = msg->data;

		cf = slurm_conf_lock();
		cf->debug_flags &= (~request_msg->debug_flags_minus);
		cf->debug_flags |= request_msg->debug_flags_plus;
		flag_string = debug_flags2str(cf->debug_flags);
		build_conf_buf();
		slurm_conf_unlock();
		info("Set DebugFlags to %s",
		     flag_string ? flag_string : "none");
		xfree(flag_string);
	}

	forward_wait(msg);
	slurm_send_rc_msg(msg, rc);
}

static void _rpc_set_slurmd_debug(slurm_msg_t *msg)
{
	set_debug_level_msg_t *request_msg = msg->data;
	int rc = SLURM_SUCCESS;

	if (!_slurm_authorized_user(msg->auth_uid)) {
		error("Security violation, %s from uid %u",
		      rpc_num2string(msg->msg_type), msg->auth_uid);
		rc = ESLURM_USER_ID_MISSING;
	} else {
		update_slurmd_logging(request_msg->debug_level);
		update_stepd_logging(false);
		build_conf_buf();
	}

	forward_wait(msg);
	slurm_send_rc_msg(msg, rc);
}

static void _rpc_reconfig(slurm_msg_t *msg)
{
	if ((msg->msg_type == REQUEST_RECONFIGURE_WITH_CONFIG) &&
	    conf->conf_cache) {
		config_response_msg_t *configs = msg->data;
		/*
		 * Running in "configless" mode as indicated by the
		 * cache directory's existence. Update those so
		 * our reconfigure picks up the changes, and so
		 * client commands see the changes as well.
		 */
		write_configs_to_conf_cache(configs, conf->conf_cache);
	}

	kill(conf->pid, SIGHUP);
	forward_wait(msg);
	/* Never return a message, slurmctld does not expect one */
}

static void
_rpc_shutdown(slurm_msg_t *msg)
{
	forward_wait(msg);

	if (kill(conf->pid, SIGTERM) != 0)
		error("kill(%u,SIGTERM): %m", conf->pid);

	/* Never return a message, slurmctld does not expect one */
}

static void
_rpc_reboot(slurm_msg_t *msg)
{
	char *reboot_program, *cmd = NULL, *sp;
	reboot_msg_t *reboot_msg;
	slurm_conf_t *cfg;
	int exit_code;
	bool need_reboot = true;

	cfg = slurm_conf_lock();
	reboot_program = cfg->reboot_program;
	reboot_msg = msg->data;

	if (reboot_msg && reboot_msg->features) {
		/*
		 * Run node_features_g_node_set first to check
		 * if reboot will be required.
		 */
		char *new_features = xstrdup(reboot_msg->features);
		info("Node features change request %s being processed",
		     reboot_msg->features);
		if (node_features_g_node_set(reboot_msg->features,
					     &need_reboot)) {
			error("Failed to set features: '%s'.",
			      new_features);
			update_node_msg_t update_node_msg;
			slurm_init_update_node_msg(&update_node_msg);
			update_node_msg.node_names = conf->node_name;
			update_node_msg.node_state = NODE_STATE_DOWN;
			xstrfmtcat(update_node_msg.reason,
				   "Failed to set node feature(s): '%s'",
				   new_features);
			slurm_conf_unlock();

			/*
			 * Send updated registration to clear booting
			 * state on controller and then down the node
			 * with the failure reason so it's the last
			 * reason displayed.
			 */
			conf->boot_time = time(NULL);
			send_registration_msg(SLURM_SUCCESS);
			slurm_update_node(&update_node_msg);

			xfree(update_node_msg.reason);
			xfree(new_features);
			return;
		}
		xfree(new_features);
		log_flag(NODE_FEATURES, "Features on node updated successfully");
	}
		if (!need_reboot) {
			log_flag(NODE_FEATURES, "Reboot not required - sending registration message");
			conf->boot_time = time(NULL);
			slurm_mutex_lock(&cached_features_mutex);
			refresh_cached_features = true;
			slurm_mutex_unlock(&cached_features_mutex);
			slurm_conf_unlock();
			send_registration_msg(SLURM_SUCCESS);
			return;
		} else if (need_reboot && reboot_program) {
			sp = strchr(reboot_program, ' ');
			if (sp)
				sp = xstrndup(reboot_program,
					      (sp - reboot_program));
			else
				sp = xstrdup(reboot_program);
			if (reboot_msg && reboot_msg->features) {
				/*
				 * Run reboot_program with only arguments given
				 * in reboot_msg->features.
				 */
				info("Node reboot request with features %s being processed",
				     reboot_msg->features);
				if (reboot_msg->features[0]) {
					xstrfmtcat(cmd, "%s '%s'",
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

			/*
			 * Explicitly shutdown the slurmd. This is usually
			 * taken care of by calling reboot_program, but in
			 * case that fails to shut things down this will at
			 * least offline this node until someone intervenes.
			 */
			if (cfg->conf_flags & CONF_FLAG_SHR) {
				slurmd_shutdown();
			}
			slurm_conf_unlock();
		} else {
			error("RebootProgram isn't defined in config");
			slurm_conf_unlock();
		}

	/* Never return a message, slurmctld does not expect one */
	/* slurm_send_rc_msg(msg, rc); */
}

static int _find_step_loc(void *x, void *key)
{
	step_loc_t *step_loc = (step_loc_t *) x;
	slurm_step_id_t *step_id = (slurm_step_id_t *) key;

	return verify_step_id(&step_loc->step_id, step_id);
}

static void _rpc_ping(slurm_msg_t *msg)
{
	slurm_msg_t resp_msg;
	ping_slurmd_resp_msg_t ping_resp;
	get_cpu_load(&ping_resp.cpu_load);
	get_free_mem(&ping_resp.free_mem);
	slurm_msg_t_copy(&resp_msg, msg);
	resp_msg.msg_type = RESPONSE_PING_SLURMD;
	resp_msg.data = &ping_resp;

	slurm_send_node_msg(msg->conn, &resp_msg);

	/* Take this opportunity to enforce any job memory limits */
	job_mem_limit_enforce();
	/* Clear up any stalled file transfers as well */
	_file_bcast_cleanup();

	if (msg->msg_type == REQUEST_NODE_REGISTRATION_STATUS) {
		get_reg_resp = true;
		send_registration_msg(SLURM_SUCCESS);
	}
}

static void _rpc_health_check(slurm_msg_t *msg)
{
	/* If the reply can't be sent this indicates that
	 * 1. The network is broken OR
	 * 2. slurmctld has died    OR
	 * 3. slurmd was paged out due to full memory
	 * If the reply request fails, we send an registration message to
	 * slurmctld in hopes of avoiding having the node set DOWN due to
	 * slurmd paging and not being able to respond in a timely fashion. */
	if (slurm_send_rc_msg(msg, SLURM_SUCCESS) < 0) {
		error("Error responding to health check: %m");
		send_registration_msg(SLURM_SUCCESS);
	}

	run_script_health_check();

	/* Take this opportunity to enforce any job memory limits */
	job_mem_limit_enforce();
	/* Clear up any stalled file transfers as well */
	_file_bcast_cleanup();
}


static void _rpc_acct_gather_update(slurm_msg_t *msg)
{
	slurm_msg_t resp_msg;
	acct_gather_node_resp_msg_t acct_msg;

	/* Update node energy usage data */
	acct_gather_energy_g_update_node_energy();

	memset(&acct_msg, 0, sizeof(acct_msg));
	acct_msg.node_name = conf->node_name;
	acct_msg.sensor_cnt = 1;
	acct_msg.energy = acct_gather_energy_alloc(acct_msg.sensor_cnt);
	(void) acct_gather_energy_g_get_sum(ENERGY_DATA_NODE_ENERGY,
					    acct_msg.energy);

	slurm_msg_t_copy(&resp_msg, msg);
	resp_msg.msg_type = RESPONSE_ACCT_GATHER_UPDATE;
	resp_msg.data = &acct_msg;

	slurm_send_node_msg(msg->conn, &resp_msg);

	acct_gather_energy_destroy(acct_msg.energy);
}

static void _rpc_acct_gather_energy(slurm_msg_t *msg)
{
	int        rc = SLURM_SUCCESS;
	static bool first_msg = true;
	static bool first_error = true;
	static uint32_t req_cnt = 0;
	static pthread_mutex_t req_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_mutex_t last_poll_mutex = PTHREAD_MUTEX_INITIALIZER;
	bool req_added = false;

	if (!_slurm_authorized_user(msg->auth_uid)) {
		error("Security violation, acct_gather_update RPC from uid %u",
		      msg->auth_uid);
		if (first_msg) {
			error("Do you have SlurmUser configured as uid %u?",
			      msg->auth_uid);
		}
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	}
	first_msg = false;

	/*
	 * Avoid tying up too many slurmd threads if the IPMI (or similar)
	 * interface is locked up. The request would likely eventually fail
	 * anyways, so dying early isn't much worse here.
	 */
	slurm_mutex_lock(&req_cnt_mutex);
	if (req_cnt < 10) {
		req_cnt++;
		req_added = true;
	} else {
		if (first_error) {
			error("%s: Too many pending requests", __func__);
			first_error = false;
		} else {
			debug("%s: Too many pending requests", __func__);
		}

		rc = ESLURMD_TOO_MANY_RPCS;
	}
	slurm_mutex_unlock(&req_cnt_mutex);

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

		if (req->context_id == NO_VAL16) {
			rc = SLURM_PROTOCOL_VERSION_ERROR;
			if (slurm_send_rc_msg(msg, rc) < 0)
				error("Error responding to energy request: %m");
			goto end;
		}

		acct_gather_energy_g_get_data(req->context_id,
					      ENERGY_DATA_SENSOR_CNT,
					      &sensor_cnt);

		memset(&acct_msg, 0, sizeof(acct_msg));
		if (!sensor_cnt) {
			error("Can't get energy data. No power sensors are available. Try later.");
		} else {
			slurm_mutex_lock(&last_poll_mutex);
			acct_gather_energy_g_get_data(req->context_id,
						      ENERGY_DATA_LAST_POLL,
						      &last_poll);
			/*
			 * If we polled later than delta seconds then force a
			 * new poll.
			 */
			if ((now - last_poll) > req->delta)
				data_type = ENERGY_DATA_JOULES_TASK;
			else
				slurm_mutex_unlock(&last_poll_mutex);

			acct_msg.sensor_cnt = sensor_cnt;
			acct_msg.energy =
				acct_gather_energy_alloc(acct_msg.sensor_cnt);

			acct_gather_energy_g_get_data(req->context_id,
						      data_type,
						      acct_msg.energy);
			if (data_type == ENERGY_DATA_JOULES_TASK)
				slurm_mutex_unlock(&last_poll_mutex);
		}

		slurm_msg_t_copy(&resp_msg, msg);
		resp_msg.msg_type = RESPONSE_ACCT_GATHER_ENERGY;
		resp_msg.data     = &acct_msg;

		slurm_send_node_msg(msg->conn, &resp_msg);

		acct_gather_energy_destroy(acct_msg.energy);
	}
end:
	if (req_added) {
		slurm_mutex_lock(&req_cnt_mutex);
		req_cnt--;
		first_error = true;
		slurm_mutex_unlock(&req_cnt_mutex);
	}
}

static int _signal_jobstep(slurm_step_id_t *step_id, uint16_t signal,
			   uint16_t flags, char *details, uid_t req_uid)
{
	int fd, rc = SLURM_SUCCESS;
	uint16_t protocol_version;

	/*
	 * There will be no stepd if the prolog is still running
	 * Return failure so caller can retry.
	 */
	if (_prolog_is_running(step_id)) {
		info("signal %d req for %ps while prolog is running. Returning failure.",
		     signal, step_id);
		return ESLURM_TRANSITION_STATE_NO_UPDATE;
	}

	fd = stepd_connect(conf->spooldir, conf->node_name,
			   step_id, &protocol_version);
	if (fd == -1) {
		debug("signal for nonexistent %ps stepd_connect failed: %m",
		      step_id);
		return ESLURM_INVALID_JOB_ID;
	}

	debug2("container signal %d to %ps flags=0x%x", signal, step_id, flags);
	rc = stepd_signal_container(fd, protocol_version, signal, flags,
				    details, req_uid);
	if (rc == -1)
		rc = ESLURMD_STEP_NOTRUNNING;

	close(fd);
	return rc;
}

static void
_rpc_signal_tasks(slurm_msg_t *msg)
{
	int               rc = SLURM_SUCCESS;
	signal_tasks_msg_t *req = msg->data;
	uid_t job_uid;

	job_uid = _get_job_uid(req->step_id.job_id);
	if (job_uid == INFINITE) {
		debug("%s: failed to get job_uid for job %u",
		      __func__, req->step_id.job_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	if ((msg->auth_uid != job_uid) &&
	    !_slurm_authorized_user(msg->auth_uid)) {
		debug("%s: from uid %u for job %u owned by uid %u",
		      __func__, msg->auth_uid, req->step_id.job_id, job_uid);
		rc = ESLURM_USER_ID_MISSING;     /* or bad in this case */
		goto done;
	}

	/* security is handled when communicating with the stepd */
	if ((req->flags & KILL_FULL_JOB) || (req->flags & KILL_JOB_BATCH)) {
		debug("%s: sending signal %u to entire job %u flag %u",
		      __func__, req->signal, req->step_id.job_id, req->flags);
		_kill_all_active_steps(&req->step_id, req->signal, req->flags,
				       NULL, true, msg->auth_uid);
	} else if (req->flags & KILL_STEPS_ONLY) {
		debug("%s: sending signal %u to all steps job %u flag %u",
		      __func__, req->signal, req->step_id.job_id, req->flags);
		_kill_all_active_steps(&req->step_id, req->signal, req->flags,
				       NULL, false, msg->auth_uid);
	} else {
		debug("%s: sending signal %u to %ps flag %u", __func__,
		      req->signal, &req->step_id, req->flags);
		rc = _signal_jobstep(&req->step_id, req->signal, req->flags,
				     NULL, msg->auth_uid);
	}
done:
	slurm_send_rc_msg(msg, rc);
}

static void
_rpc_terminate_tasks(slurm_msg_t *msg)
{
	signal_tasks_msg_t *req = msg->data;
	int               rc = SLURM_SUCCESS;
	int               fd;
	uint16_t protocol_version;
	uid_t uid;

	debug3("Entering _rpc_terminate_tasks");
	fd = stepd_connect(conf->spooldir, conf->node_name,
			   &req->step_id, &protocol_version);
	if (fd == -1) {
		debug("kill for nonexistent %ps stepd_connect "
		      "failed: %m", &req->step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	if ((uid = stepd_get_uid(fd, protocol_version)) == INFINITE) {
		debug("terminate_tasks couldn't read from the %ps: %m",
		      &req->step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done2;
	}

	if ((msg->auth_uid != uid)
	    && !_slurm_authorized_user(msg->auth_uid)) {
		debug("kill req from uid %u for %ps owned by uid %u",
		      msg->auth_uid, &req->step_id, uid);
		rc = ESLURM_USER_ID_MISSING;     /* or bad in this case */
		goto done2;
	}

	rc = stepd_terminate(fd, protocol_version);
	if (rc == -1)
		rc = ESLURMD_STEP_NOTRUNNING;

done2:
	close(fd);
done:
	slurm_send_rc_msg(msg, rc);
}

static void _rpc_step_complete(slurm_msg_t *msg)
{
	step_complete_msg_t *req = msg->data;
	int               rc = SLURM_SUCCESS;
	int               fd;
	uint16_t protocol_version;
	slurm_step_id_t *tmp_step_id;
	slurm_step_id_t step_id = {
		.job_id = req->step_id.job_id,
		.step_het_comp = NO_VAL,
		.step_id = SLURM_EXTERN_CONT,
	};

	if (req->send_to_stepmgr)
		tmp_step_id = &step_id;
	else
		tmp_step_id = &req->step_id;

	debug3("Entering _rpc_step_complete");
	fd = stepd_connect(conf->spooldir, conf->node_name,
			   tmp_step_id, &protocol_version);
	if (fd == -1) {
		error("stepd_connect to %ps failed: %m", &req->step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	/* step completion messages are only allowed from other slurmstepd,
	   so only root or SlurmUser is allowed here */
	if (!_slurm_authorized_user(msg->auth_uid)) {
		debug("step completion from uid %u for %ps",
		      msg->auth_uid, &req->step_id);
		rc = ESLURM_USER_ID_MISSING;     /* or bad in this case */
		goto done2;
	}

	rc = stepd_completion(fd, protocol_version, req);
	if (rc == -1)
		rc = ESLURMD_STEP_NOTRUNNING;

done2:
	close(fd);
done:
	slurm_send_rc_msg(msg, rc);
}

/* Get list of active jobs and steps, xfree returned value */
static char *
_get_step_list(void)
{
	char tmp[64];
	char *step_list = NULL;
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   &stepd->step_id, &stepd->protocol_version);
		if (fd == -1)
			continue;

		if (stepd_state(fd, stepd->protocol_version)
		    == SLURMSTEPD_NOT_RUNNING) {
			debug("stale domain socket for %ps", &stepd->step_id);
			close(fd);
			continue;
		}
		close(fd);

		if (step_list)
			xstrcat(step_list, ", ");
		if (stepd->step_id.step_id == SLURM_BATCH_SCRIPT) {
			snprintf(tmp, sizeof(tmp), "%u",
				 stepd->step_id.job_id);
			xstrcat(step_list, tmp);
		} else {
			xstrcat(step_list,
				log_build_step_id_str(&stepd->step_id,
						      tmp,
						      sizeof(tmp),
						      STEP_ID_FLAG_NO_PREFIX));
		}
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	if (step_list == NULL)
		xstrcat(step_list, "NONE");
	return step_list;
}

static void _rpc_daemon_status(slurm_msg_t *msg)
{
	slurm_msg_t      resp_msg;
	slurmd_status_t *resp = NULL;

	resp = xmalloc(sizeof(slurmd_status_t));
	resp->actual_cpus        = conf->actual_cpus;
	resp->actual_boards      = conf->actual_boards;
	resp->actual_sockets     = conf->actual_sockets;
	resp->actual_cores       = conf->actual_cores;
	resp->actual_threads     = conf->actual_threads;
	resp->actual_real_mem    = conf->physical_memory_size;
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
	slurm_send_node_msg(msg->conn, &resp_msg);
	slurm_free_slurmd_status(resp);
}

static void _rpc_stat_jobacct(slurm_msg_t *msg)
{
	slurm_step_id_t *req = msg->data;
	slurm_msg_t        resp_msg;
	job_step_stat_t *resp = NULL;
	int fd;
	uint16_t protocol_version;
	uid_t uid;

	debug3("Entering _rpc_stat_jobacct for %ps", req);
	/* step completion messages are only allowed from other slurmstepd,
	   so only root or SlurmUser is allowed here */

	fd = stepd_connect(conf->spooldir, conf->node_name,
			   req, &protocol_version);
	if (fd == -1) {
		error("stepd_connect to %ps failed: %m", req);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		return;
	}

	if ((uid = stepd_get_uid(fd, protocol_version)) == INFINITE) {
		debug("stat_jobacct couldn't read from %ps: %m", req);
		close(fd);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		return;
	}

	/*
	 * check that requesting user ID is the Slurm UID or root
	 */
	if ((msg->auth_uid != uid) &&
	    !_slurm_authorized_user(msg->auth_uid)) {
		error("stat_jobacct from uid %u for %pI owned by uid %u",
		      msg->auth_uid, &req->step_id, uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		/* or bad in this case */
		return;
	}

	resp = xmalloc(sizeof(job_step_stat_t));
	resp->step_pids = xmalloc(sizeof(job_step_pids_t));
	resp->step_pids->node_name = xstrdup(conf->node_name);
	slurm_msg_t_copy(&resp_msg, msg);
	resp->return_code = SLURM_SUCCESS;

	if (stepd_stat_jobacct(fd, protocol_version, req, resp)
	    == SLURM_ERROR) {
		debug("accounting for nonexistent %ps requested", req);
	}

	/* FIX ME: This should probably happen in the
	   stepd_stat_jobacct to get more information about the pids.
	*/
	if (stepd_list_pids(fd, protocol_version, &resp->step_pids->pid,
			    &resp->step_pids->pid_cnt) == SLURM_ERROR) {
		debug("No pids for nonexistent %ps requested", req);
	}

	close(fd);

	resp_msg.msg_type     = RESPONSE_JOB_STEP_STAT;
	resp_msg.data         = resp;

	slurm_send_node_msg(msg->conn, &resp_msg);
	slurm_free_job_step_stat(resp);
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

static void _rpc_network_callerid(slurm_msg_t *msg)
{
	network_callerid_msg_t *req = msg->data;
	slurm_msg_t resp_msg;
	network_callerid_resp_t *resp = NULL;

	uid_t job_uid = -1;
	uint32_t job_id = NO_VAL;
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
		if (!_slurm_authorized_user(msg->auth_uid)) {
			/* Requester is not root or SlurmUser */
			job_uid = _get_job_uid(job_id);
			if (job_uid != msg->auth_uid) {
				/* RPC call sent by non-root user who does not
				 * own this job. Do not send them the job ID. */
				error("Security violation, REQUEST_NETWORK_CALLERID from uid=%u",
				      msg->auth_uid);
				job_id = NO_VAL;
				rc = ESLURM_INVALID_JOB_ID;
			}
		}
	}

	resp->step_id.job_id = job_id;
	resp->node_name = xstrdup(conf->node_name);

	resp_msg.msg_type = RESPONSE_NETWORK_CALLERID;
	resp_msg.data     = resp;

	slurm_send_node_msg(msg->conn, &resp_msg);
	slurm_free_network_callerid_resp(resp);
}

static void _rpc_list_pids(slurm_msg_t *msg)
{
	slurm_step_id_t *req = msg->data;
	slurm_msg_t        resp_msg;
	job_step_pids_t *resp = NULL;
	int fd;
	uint16_t protocol_version = 0;
	uid_t job_uid;

	debug3("Entering _rpc_list_pids");

	job_uid = _get_job_uid(req->job_id);

	if (job_uid == INFINITE) {
		error("stat_pid for invalid job_id: %u",
		      req->job_id);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		return;
	}

	/*
	 * check that requesting user ID is the Slurm UID or root
	 */
	if ((msg->auth_uid != job_uid)
	    && (!_slurm_authorized_user(msg->auth_uid))) {
		error("stat_pid from uid %u for job %u owned by uid %u",
		      msg->auth_uid, req->job_id, job_uid);

		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		/* or bad in this case */
		return;
	}

	resp = xmalloc(sizeof(job_step_pids_t));
	slurm_msg_t_copy(&resp_msg, msg);
	resp->node_name = xstrdup(conf->node_name);
	resp->pid_cnt = 0;
	resp->pid = NULL;
	fd = stepd_connect(conf->spooldir, conf->node_name,
			   req, &protocol_version);
	if (fd == -1) {
		error("stepd_connect to %ps failed: %m", req);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		slurm_free_job_step_pids(resp);
		return;
	}

	if (stepd_list_pids(fd, protocol_version,
			    &resp->pid, &resp->pid_cnt) == SLURM_ERROR) {
		debug("No pids for nonexistent %ps requested", req);
	}

	close(fd);

	resp_msg.msg_type = RESPONSE_JOB_STEP_PIDS;
	resp_msg.data     = resp;

	slurm_send_node_msg(msg->conn, &resp_msg);
	slurm_free_job_step_pids(resp);
}

/*
 *  For the specified job_id: reply to slurmctld,
 *   sleep(configured kill_wait), then send SIGKILL
 */
static void
_rpc_timelimit(slurm_msg_t *msg)
{
	kill_job_msg_t *req = msg->data;
	int             nsteps, rc;

	/*
	 *  Indicate to slurmctld that we've received the message
	 */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);

	if (req->step_id.step_id != NO_VAL) {
		slurm_conf_t *cf;
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
			rc = _signal_jobstep(&req->step_id, SIG_TIME_LIMIT, 0,
					     req->details, msg->auth_uid);
		} else {
			rc = _signal_jobstep(&req->step_id, SIG_PREEMPTED, 0,
					     req->details, msg->auth_uid);
		}
		if (rc != SLURM_SUCCESS)
			return;
		rc = _signal_jobstep(&req->step_id, SIGCONT, 0, req->details,
				     msg->auth_uid);
		if (rc != SLURM_SUCCESS)
			return;
		rc = _signal_jobstep(&req->step_id, SIGTERM, 0, req->details,
				     msg->auth_uid);
		if (rc != SLURM_SUCCESS)
			return;
		cf = slurm_conf_lock();
		delay = MAX(cf->kill_wait, 5);
		slurm_conf_unlock();
		sleep(delay);
		_signal_jobstep(&req->step_id, SIGKILL, 0, req->details,
				msg->auth_uid);
		return;
	}

	if (msg->msg_type == REQUEST_KILL_TIMELIMIT)
		_kill_all_active_steps(&req->step_id, SIG_TIME_LIMIT, 0,
				       req->details, true, msg->auth_uid);
	else /* (msg->type == REQUEST_KILL_PREEMPTED) */
		_kill_all_active_steps(&req->step_id, SIG_PREEMPTED, 0,
				       req->details, true, msg->auth_uid);
	nsteps = _kill_all_active_steps(&req->step_id, SIGTERM, 0, req->details,
					false, msg->auth_uid);
	verbose("Job %u: timeout: sent SIGTERM to %d active steps",
		req->step_id.job_id, nsteps);

	/* Revoke credential, send SIGKILL, run epilog, etc. */
	_rpc_terminate_job(msg);
}

static void  _rpc_pid2jid(slurm_msg_t *msg)
{
	job_id_request_msg_t *req = msg->data;
	slurm_msg_t           resp_msg;
	job_id_response_msg_t resp;
	bool         found = false;
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   &stepd->step_id, &stepd->protocol_version);
		if (fd == -1)
			continue;

		if (stepd_pid_in_container(
			    fd, stepd->protocol_version,
			    req->job_pid)
		    || req->job_pid == stepd_daemon_pid(
			    fd, stepd->protocol_version)) {
			slurm_msg_t_copy(&resp_msg, msg);
			resp.step_id = stepd->step_id;
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
		debug3("%s: pid(%u) found in %pI",
		       __func__, req->job_pid, &resp.step_id);
		resp_msg.address      = msg->address;
		resp_msg.msg_type     = RESPONSE_JOB_ID;
		resp_msg.data         = &resp;

		slurm_send_node_msg(msg->conn, &resp_msg);
	} else {
		debug3("_rpc_pid2jid: pid(%u) not found", req->job_pid);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
	}
}

/* Validate sbcast credential.
 * NOTE: We can only perform the full credential validation once with
 * Munge without generating a credential replay error
 * RET an sbcast credential or NULL on error.
 */
static sbcast_cred_arg_t *_valid_sbcast_cred(file_bcast_msg_t *req,
					     uid_t req_uid,
					     gid_t req_gid,
					     uint16_t protocol_version)
{
	sbcast_cred_arg_t *arg = &req->cred->arg;
	hostset_t *hset = NULL;

	if (!(hset = hostset_create(arg->nodes))) {
		error("Unable to parse sbcast_cred hostlist %s", arg->nodes);
		return NULL;
	} else if (!hostset_within(hset, conf->node_name)) {
		error("Security violation: sbcast_cred from %u has "
		      "bad hostset %s", req_uid, arg->nodes);
		hostset_destroy(hset);
		return NULL;
	}
	hostset_destroy(hset);

	if ((arg->id->uid != req_uid) || (arg->id->gid != req_gid)) {
		error("Security violation: sbcast cred from %u/%u but rpc from %u/%u",
		      arg->id->uid, arg->id->gid, req_uid, req_gid);
		return NULL;
	}

	/*
	 * NOTE: user_name, ngids, gids may still be NULL, 0, NULL at this point.
	 *       we skip filling them in here to avoid excessive lookup calls
	 *       as this must run once per block (and there may be thousands of
	 *       blocks), and is only currently needed by the first block.
	 */
	/* print_sbcast_cred(req->cred); */

	return arg;
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
	slurm_rwlock_wrlock(&file_bcast_lock);
	list_delete_all(file_bcast_list, _bcast_find_in_list, key);
	slurm_rwlock_unlock(&file_bcast_lock);
}

static void _free_file_bcast_info_t(void *arg)
{
	file_bcast_info_t *f = (file_bcast_info_t *)arg;

	if (!f)
		return;

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

static void _free_libdir_rec_t(void *x)
{
	libdir_rec_t *l = (libdir_rec_t *) x;

	if (!l)
		return;

	xfree(l->directory);
	xfree(l->exe_fname);
	xfree(l);
}

static int _libdir_find_in_list_to_remove(void *x, void *y)
{
	libdir_rec_t *l = (libdir_rec_t *) x;
	time_t *now = (time_t *) y;

	if (l->last_update + FILE_BCAST_TIMEOUT < *now) {
		debug("Removing stale library directory reference for uid %u for `%s`",
		      l->uid, l->directory);
		return true;
	}

	return false;
}

/* remove transfers that have stalled */
static void _file_bcast_cleanup(void)
{
	time_t now = time(NULL);

	slurm_rwlock_wrlock(&file_bcast_lock);
	list_delete_all(file_bcast_list, _bcast_find_in_list_to_remove, &now);
	list_delete_all(bcast_libdir_list, _libdir_find_in_list_to_remove, &now);
	slurm_rwlock_unlock(&file_bcast_lock);
}

static int _bcast_find_by_job(void *x, void *y)
{
	file_bcast_info_t *f = x;
	uint32_t *job_id = y;

	if (f->job_id == *job_id) {
		debug("Removing file_bcast transfer from JobId=%u to file `%s`",
		       f->job_id, f->fname);
		return 1;
	}

	return 0;
}

static int _libdir_find_by_job(void *x, void *y)
{
	libdir_rec_t *l = x;
	uint32_t *job_id = y;

	if (l->job_id == *job_id) {
		debug("Removing library directory reference for JobId=%u for `%s`",
		      l->job_id, l->directory);
		return 1;
	}

	return 0;

}

static void _file_bcast_job_cleanup(uint32_t job_id)
{
	slurm_rwlock_wrlock(&file_bcast_lock);
	list_delete_all(file_bcast_list, _bcast_find_by_job, &job_id);
	list_delete_all(bcast_libdir_list, _libdir_find_by_job, &job_id);
	slurm_rwlock_unlock(&file_bcast_lock);
}

void file_bcast_init(void)
{
	/* skip locks during slurmd init */
	file_bcast_list = list_create(_free_file_bcast_info_t);
	bcast_libdir_list = list_create(_free_libdir_rec_t);

}

void file_bcast_purge(void)
{
	slurm_rwlock_wrlock(&file_bcast_lock);
	FREE_NULL_LIST(file_bcast_list);
	FREE_NULL_LIST(bcast_libdir_list);
	/* destroying list before exit, no need to unlock */
}

static void _rpc_file_bcast(slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	int64_t offset, inx;
	sbcast_cred_arg_t *cred_arg;
	file_bcast_info_t *file_info;
	file_bcast_msg_t *req = msg->data;
	file_bcast_info_t key;

	key.uid = msg->auth_uid;
	key.gid = msg->auth_gid;

	cred_arg = _valid_sbcast_cred(req, key.uid, key.gid,
				      msg->protocol_version);
	if (!cred_arg) {
		rc = ESLURMD_INVALID_JOB_CREDENTIAL;
		goto done;
	}

	key.job_id = cred_arg->step_id.job_id;
	key.step_id = cred_arg->step_id.step_id;

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

	if (req->flags & FILE_BCAST_SO) {
		libdir_rec_t *libdir;
		libdir_rec_t libdir_args = {
			.uid = key.uid,
			.job_id = key.job_id,
			.step_id = key.step_id,
			.exe_fname = xstrdup(req->exe_fname),
		};
		char *fname = NULL;
		size_t exe_fname_len = strlen(libdir_args.exe_fname);

		if (libdir_args.exe_fname[exe_fname_len - 1] == '/')
			/*
			 * Append the default filename to the executable path in
			 * the search key so this shared object is associated
			 * with the correct libdir entry.
			 */
			xstrfmtcat(libdir_args.exe_fname, BCAST_FILE_FMT,
				   cred_arg->step_id.job_id,
				   cred_arg->step_id.step_id, conf->node_name);

		slurm_rwlock_rdlock(&file_bcast_lock);
		libdir = list_find_first(bcast_libdir_list, _find_libdir_record,
					 &libdir_args);
		xfree(libdir_args.exe_fname);
		if (!libdir) {
			error("Could not find library directory for transfer from uid %u",
			      key.uid);
			slurm_rwlock_unlock(&file_bcast_lock);
			rc = SLURM_ERROR;
			goto done;
		}

		libdir->last_update = time(NULL);
		xstrfmtcat(fname, "%s/%s", libdir->directory, req->fname);
		xfree(req->fname);
		req->fname = fname;
		slurm_rwlock_unlock(&file_bcast_lock);
	} else if (req->fname[strlen(req->fname) - 1] == '/') {
		/*
		 * "srun --bcast" was called with a target directory instead of
		 * a filename, and we have to append the default filename to
		 * req->fname. This same file name has to be recreated by
		 * exec_task().
		 */
		xstrfmtcat(req->fname, BCAST_FILE_FMT, cred_arg->step_id.job_id,
			   cred_arg->step_id.step_id, conf->node_name);
	}
	key.fname = req->fname;

	if (req->block_no == 1) {
		info("sbcast req_uid=%u job_id=%u fname=%s block_no=%u",
		     key.uid, key.job_id, key.fname, req->block_no);
	} else {
		debug("sbcast req_uid=%u job_id=%u fname=%s block_no=%u",
		      key.uid, key.job_id, key.fname, req->block_no);
	}

	/* first block must register the file and open fd/mmap */
	if (req->block_no == 1) {
		if ((rc = _file_bcast_register_file(msg, cred_arg, &key))) {
			goto done;
		}
	}

	slurm_rwlock_rdlock(&file_bcast_lock);
	if (!(file_info = _bcast_lookup_file(&key))) {
		error("No registered file transfer for uid %u file `%s`.",
		      key.uid, key.fname);
		slurm_rwlock_unlock(&file_bcast_lock);
		rc = SLURM_ERROR;
		goto done;
	}

	/* now decompress file */
	if (bcast_decompress_data(req) < 0) {
		error("sbcast: data decompression error for UID %u, file %s",
		      key.uid, key.fname);
		slurm_rwlock_unlock(&file_bcast_lock);
		rc = SLURM_ERROR;
		goto done;
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
			slurm_rwlock_unlock(&file_bcast_lock);
			rc = SLURM_ERROR;
			goto done;
		}
		offset += inx;
	}

	file_info->last_update = time(NULL);

	if ((req->flags & FILE_BCAST_LAST_BLOCK) &&
	    fchmod(file_info->fd, (req->modes & 0777))) {
		error("sbcast: uid:%u can't chmod `%s`: %m",
		      key.uid, key.fname);
	}
	if ((req->flags & FILE_BCAST_LAST_BLOCK) &&
	    fchown(file_info->fd, key.uid, key.gid)) {
		error("sbcast: uid:%u gid:%u can't chown `%s`: %m",
		      key.uid, key.gid, key.fname);
	}
	if ((req->flags & FILE_BCAST_LAST_BLOCK) && req->atime) {
		struct timespec time_buf[2];
		time_buf[0].tv_sec = req->atime;
		time_buf[0].tv_nsec = 0;
		time_buf[1].tv_sec = req->mtime;
		time_buf[1].tv_nsec = 0;
		if (futimens(file_info->fd, time_buf)) {
			error("sbcast: uid:%u can't futimens `%s`: %m",
			      key.uid, key.fname);
		}
	}

	slurm_rwlock_unlock(&file_bcast_lock);

	if (req->flags & FILE_BCAST_LAST_BLOCK) {
		_file_bcast_close_file(&key);
	}

done:
	slurm_send_rc_msg(msg, rc);
}

static int _file_bcast_register_file(slurm_msg_t *msg,
				     sbcast_cred_arg_t *cred_arg,
				     file_bcast_info_t *key)
{
	file_bcast_msg_t *req = msg->data;
	int fd = -1, flags, rc;
	file_bcast_info_t *file_info;
	libdir_rec_t *libdir = NULL;
	bool force_opt = false;

	force_opt = req->flags & FILE_BCAST_FORCE;

	flags = O_WRONLY | O_CREAT;
	if (force_opt)
		flags |= O_TRUNC;
	else
		flags |= O_EXCL;

	rc = _open_as_other(req->fname, flags, 0700, key->job_id, key->uid,
			    key->gid, cred_arg->id->ngids, cred_arg->id->gids,
			    false, false, &fd);
	if (rc != SLURM_SUCCESS) {
		error("Unable to open %s: %s", req->fname, strerror(rc));
		return rc;
	}

	if (req->flags & FILE_BCAST_EXE) {
		int fd_dir;
		char *directory = xstrdup_printf("%s_libs", key->fname);
		rc = _open_as_other(directory, 0, 0700, key->job_id, key->uid,
				    key->gid, cred_arg->id->ngids,
				    cred_arg->id->gids, true, force_opt,
				    &fd_dir);
		if (rc != SLURM_SUCCESS) {
			error("Unable to create directory %s: %s",
			      directory, strerror(rc));
			/*
			 * fd might be opened from the previous call to
			 * _open_as_other() for the file that is being
			 * transmitted and won't be cleaned up otherwise, so
			 * close it here.
			 */
			if (fd > 0)
				close(fd);
			return rc;
		}

		libdir = xmalloc(sizeof(*libdir));
		libdir->uid = key->uid;
		libdir->job_id = key->job_id;
		libdir->step_id = key->step_id;
		libdir->directory = directory;
		libdir->exe_fname = xstrdup(key->fname);
		libdir->last_update = time(NULL);
	}


	file_info = xmalloc(sizeof(file_bcast_info_t));
	file_info->fd = fd;
	file_info->fname = xstrdup(req->fname);
	file_info->uid = key->uid;
	file_info->gid = key->gid;
	file_info->job_id = key->job_id;
	file_info->last_update = file_info->start_time = time(NULL);

	//TODO: mmap the file here
	slurm_rwlock_wrlock(&file_bcast_lock);
	list_append(file_bcast_list, file_info);
	if (libdir)
		list_append(bcast_libdir_list, libdir);
	slurm_rwlock_unlock(&file_bcast_lock);

	return SLURM_SUCCESS;
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
	slurm_addr_t   ioaddr;
	int               fd;
	slurm_addr_t *cli = &msg->orig_addr;
	uint32_t nodeid = NO_VAL;
	uid_t uid = -1;
	uint16_t protocol_version;
	list_t *steps = stepd_available(conf->spooldir, conf->node_name);;
	step_loc_t *stepd = NULL;

	slurm_msg_t_copy(&resp_msg, msg);

	/*
	 * At the time of writing only 1 stepd could be running for a step
	 * (het step) on a node at a time.  If this ever is resolved this will
	 * need to be altered.
	 */
	stepd = list_find_first(steps, _find_step_loc, &req->step_id);

	if (!stepd) {
		debug("%s: Couldn't find %ps: %m",
		      __func__, &req->step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	fd = stepd_connect(conf->spooldir, conf->node_name,
			   &stepd->step_id, &protocol_version);
	if (fd == -1) {
		debug("reattach for nonexistent %ps stepd_connect failed: %m",
		      &req->step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	}

	if ((uid = stepd_get_uid(fd, protocol_version)) == INFINITE) {
		debug("_rpc_reattach_tasks couldn't read from the %ps: %m",
		      &req->step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done2;
	}

	nodeid = stepd_get_nodeid(fd, protocol_version);

	debug2("_rpc_reattach_tasks: nodeid %d in the job step", nodeid);

	if ((msg->auth_uid != uid) &&
	    !_slurm_authorized_user(msg->auth_uid)) {
		error("uid %u attempt to attach to %ps owned by %u",
		      msg->auth_uid, &req->step_id, uid);
		rc = EPERM;
		goto done2;
	}

	memset(resp, 0, sizeof(reattach_tasks_response_msg_t));
	port = slurm_get_port(cli);

	/*
	 * Set response address by resp_port and client address
	 */
	memcpy(&resp_msg.address, cli, sizeof(slurm_addr_t));
	if (req->num_resp_port > 0) {
		port = req->resp_port[nodeid % req->num_resp_port];
		slurm_set_port(&resp_msg.address, port);
	}

	/*
	 * Set IO address by io_port and client address
	 */
	memcpy(&ioaddr, cli, sizeof(slurm_addr_t));

	if (req->num_io_port > 0) {
		port = req->io_port[nodeid % req->num_io_port];
		slurm_set_port(&ioaddr, port);
	}

	resp->gtids = NULL;
	resp->local_pids = NULL;

	/* NOTE: We need to use the protocol_version from
	 * sattach here since responses will be sent back to it. */
	if (msg->protocol_version < protocol_version)
		protocol_version = msg->protocol_version;

	/* Following call fills in gtids and local_pids when successful. */
	rc = stepd_attach(fd, protocol_version, &ioaddr, &resp_msg.address,
			  req->tls_cert, req->io_key, msg->auth_uid, resp);
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

	slurm_send_node_msg(msg->conn, &resp_msg);
	slurm_free_reattach_tasks_response_msg(resp);
	FREE_NULL_LIST(steps);
}

static uid_t _get_job_uid(uint32_t jobid)
{
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;
	uid_t uid = -1;
	int fd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->step_id.job_id != jobid) {
			/* multiple jobs expected on shared nodes */
			continue;
		}
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   &stepd->step_id, &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to %ps", &stepd->step_id);
			continue;
		}
		uid = stepd_get_uid(fd, stepd->protocol_version);

		close(fd);
		if (uid == INFINITE) {
			debug("stepd_get_uid failed %ps: %m",
			      &stepd->step_id);
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
 * step_id IN - id of job to signal
 * sig   IN - signal to send
 * flags IN - to decide if batch step must be signaled, if its children too, etc
 * batch IN - if true signal batch script, otherwise skip it
 * RET count of signaled job steps (plus batch script, if applicable)
 */
static int _kill_all_active_steps(slurm_step_id_t *step_id, int sig, int flags,
				  char *details, bool batch, uid_t req_uid)
{
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;
	int step_cnt  = 0;
	int rc = SLURM_SUCCESS;

	bool sig_all_steps = true;
	bool sig_batch_step = false;

	if ((flags & KILL_JOB_BATCH) || (flags & KILL_FULL_JOB)) {
		sig_all_steps = false;
		sig_batch_step = true;
	} else if (batch)
		sig_batch_step = true;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->step_id.job_id != step_id->job_id) {
			/* multiple jobs expected on shared nodes */
			debug3("%s: Looking for %pI, found step from %pI",
			       __func__, step_id, &stepd->step_id);
			continue;
		}
		if ((sig_all_steps &&
		     (stepd->step_id.step_id != SLURM_BATCH_SCRIPT)) ||
		    (sig_batch_step &&
		     (stepd->step_id.step_id == SLURM_BATCH_SCRIPT))) {
			if (_signal_jobstep(&stepd->step_id, sig, flags,
					    details,
					    req_uid) != SLURM_SUCCESS) {
				rc = SLURM_ERROR;
				continue;
			}
			step_cnt++;
		} else {
			debug3("%s: No signaling. Job: %u, Step: %u. Flags: %u",
			       __func__, stepd->step_id.job_id,
			       stepd->step_id.step_id, flags);
		}
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	if (step_cnt == 0)
		debug2("No steps in %pI %s %d",
		       step_id, (rc == SLURM_SUCCESS) ?
		       "to send signal" : "were able to be signaled with",
		       sig);

	return step_cnt;
}

/*
 * Wait until all job steps are in SLURMSTEPD_NOT_RUNNING state.
 * This indicates that switch_g_job_postfini has completed and
 * freed the switch windows (as needed only for Federation switch).
 */
static void _wait_state_completed(slurm_step_id_t *step_id, int max_delay)
{
	int i;

	for (i=0; i<max_delay; i++) {
		if (_steps_completed_now(step_id))
			break;
		sleep(1);
	}
	if (i >= max_delay)
		error("%s: timed out waiting for %pI to complete",
		      __func__, step_id);
}

static bool _steps_completed_now(slurm_step_id_t *step_id)
{
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;
	bool rc = true;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->step_id.job_id == step_id->job_id) {
			int fd;
			fd = stepd_connect(stepd->directory, stepd->nodename,
					   &stepd->step_id,
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
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		int fd;
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   &stepd->step_id, &stepd->protocol_version);
		if (fd == -1)
			continue; /* step gone */
		close(fd);
		launch_complete_add(&stepd->step_id);
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
	suspend_int_msg_t *req = msg->data;
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;
	int step_cnt  = 0;
	int rc = SLURM_SUCCESS;
	DEF_TIMERS;

	if ((req->op != SUSPEND_JOB) && (req->op != RESUME_JOB)) {
		error("REQUEST_SUSPEND_INT: bad op code %u", req->op);
		rc = ESLURM_NOT_SUPPORTED;
	}

	/* send a response now, which will include any errors
	 * detected with the request */
	slurm_send_rc_msg(msg, rc);
	if (rc != SLURM_SUCCESS)
		return;

	conn_g_destroy(msg->conn, true);
	msg->conn = NULL;

	/* now we can focus on performing the requested action,
	 * which could take a few seconds to complete */
	debug("%s: %pI uid=%u action=%s",
	      __func__, &req->step_id, msg->auth_uid,
	      (req->op == SUSPEND_JOB ? "suspend" : "resume"));

	/* Try to get a thread lock for this job. If the lock
	 * is not available then sleep and try again */
	while (!_get_suspend_job_lock(req->step_id.job_id)) {
		debug3("suspend lock sleep for %pI", &req->step_id);
		usleep(10000);
	}
	START_TIMER;

	/* Defer suspend until job prolog and launch complete */
	if (req->op == SUSPEND_JOB)
		launch_complete_wait(&req->step_id);

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
			if (stepd->step_id.job_id != req->step_id.job_id) {
				/* multiple jobs expected on shared nodes */
				debug3("Step from other job: jobid=%pI (this jobid=%pI)",
				       &stepd->step_id, &req->step_id);
				continue;
			}
			step_cnt++;

			fd[fdi] = stepd_connect(stepd->directory,
						stepd->nodename,
						&stepd->step_id,
						&protocol_version[fdi]);
			if (fd[fdi] == -1) {
				debug3("Unable to connect to %ps",
				       &stepd->step_id);
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
					debug("Suspend of %pI failed: %m",
					      &req);
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
					debug("Resume of %pI failed: %m",
					      &req->step_id);
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

	_unlock_suspend_job(req->step_id.job_id);

	END_TIMER;
	if (DELTA_TIMER >= (long)(slurm_conf.sched_time_slice * USEC_IN_SEC)) {
		if (req->op == SUSPEND_JOB) {
			info("Suspend time for %pI was %s. Configure SchedulerTimeSlice higher.",
			     &req->step_id, TIME_STR);
		} else {
			info("Resume time for %pI was %s. Configure SchedulerTimeSlice higher.",
			     &req->step_id, TIME_STR);
		}
	}

	if (step_cnt == 0) {
		debug2("No steps in %pI to suspend/resume", &req->step_id);
	}
}

/* Job shouldn't even be running here, abort it immediately */
static void
_rpc_abort_job(slurm_msg_t *msg)
{
	kill_job_msg_t *req    = msg->data;

	/*
	 * "revoke" all future credentials for this jobid
	 */
	if (cred_revoke(&req->step_id, req->time, req->start_time) < 0) {
		debug("revoking cred for job %u: %m", req->step_id.job_id);
	} else {
		save_cred_state();
		debug("credential for job %u revoked", req->step_id.job_id);
	}

	/*
	 *  At this point, if connection still open, we send controller
	 *   a "success" reply to indicate that we've recvd the msg.
	 */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
	conn_g_destroy(msg->conn, true);
	msg->conn = NULL;

	if (_kill_all_active_steps(&req->step_id, SIG_ABORT, 0, req->details,
				   true, msg->auth_uid)) {
		/*
		 *  Block until all user processes are complete.
		 */
		pause_for_job_completion(&req->step_id, 0,
					 (slurm_conf.prolog_flags &
					  PROLOG_FLAG_RUN_IN_JOB));
	}

	/*
	 *  Begin expiration period for cached information about job.
	 *   If expiration period has already begun, then do not run
	 *   the epilog again, as that script has already been executed
	 *   for this job.
	 */
	if (cred_begin_expiration(&req->step_id) < 0) {
		debug("Not running epilog for jobid %d: %m", req->step_id.job_id);
		return;
	}

	save_cred_state();

	_file_bcast_job_cleanup(req->step_id.job_id);

	if (!(slurm_conf.prolog_flags & PROLOG_FLAG_RUN_IN_JOB)) {
		job_env_t job_env;
		int node_id = nodelist_find(req->nodes, conf->node_name);

		memset(&job_env, 0, sizeof(job_env));
		gres_g_prep_set_env(&job_env.gres_job_env, req->job_gres_prep,
				    node_id);
		job_env.step_id = req->step_id;
		job_env.derived_ec = req->derived_ec;
		job_env.exit_code = req->exit_code;
		job_env.node_list = req->nodes;
		job_env.het_job_id = req->het_job_id;
		job_env.spank_job_env = req->spank_job_env;
		job_env.spank_job_env_size = req->spank_job_env_size;
		job_env.work_dir = req->work_dir;
		job_env.uid = req->job_uid;
		job_env.gid = req->job_gid;
		_wait_for_job_running_prolog(&req->step_id);
		run_epilog(&job_env, req->cred);
		_free_job_env(&job_env);
	}

	launch_complete_rm(&req->step_id);
}

static void _rpc_terminate_job(slurm_msg_t *msg)
{
	int             rc     = SLURM_SUCCESS;
	kill_job_msg_t *req    = msg->data;
	int             nsteps = 0;
	int		delay;
	bool send_response = true;

	debug("%s: starting for %pI %ps",
	      __func__, &req->step_id, &req->step_id);

	/*
	 * This function is also used within _rpc_timelimit() which does not
	 * need us to send a response here.
	 */
	if (msg->msg_type != REQUEST_TERMINATE_JOB)
		send_response = false;

	/*
	 *  Initialize a "waiter" thread for this jobid. If another
	 *   thread is already waiting on termination of this job,
	 *   _waiter_init() will return SLURM_ERROR. In this case, just
	 *   notify slurmctld that we recvd the message successfully,
	 *   then exit this thread.
	 */
	if (_waiter_init(&req->step_id) == SLURM_ERROR) {
		if (send_response) {
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
	_note_batch_job_finished(&req->step_id);

	/*
	 * "revoke" all future credentials for this jobid
	 */
	if (cred_revoke(&req->step_id, req->time, req->start_time) < 0) {
		debug("revoking cred for job %u: %m", req->step_id.job_id);
	} else {
		save_cred_state();
		debug("credential for job %u revoked", req->step_id.job_id);
	}

	if (_prolog_is_running(&req->step_id)) {
		if (send_response) {
			/* If the step hasn't finished running the prolog
			 * (or finished starting the extern step) yet just send
			 * a success to let the controller know we got
			 * this request.
			 */
			debug("%s: sent SUCCESS for %u, waiting for prolog to finish",
			      __func__, req->step_id.job_id);
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			send_response = false;
		}
		_wait_for_job_running_prolog(&req->step_id);
	}

	/*
	 * Before signaling steps, if the job has any steps that are still
	 * in the process of fork/exec/check in with slurmd, wait on a condition
	 * var for the start.  Otherwise a slow-starting step can miss the
	 * job termination message and run indefinitely.
	 */
	if (_step_is_starting(&req->step_id)) {
		if (send_response) {
			/* If the step hasn't started yet just send a
			 * success to let the controller know we got
			 * this request.
			 */
			debug("sent SUCCESS, waiting for step to start");
			slurm_send_rc_msg (msg, SLURM_SUCCESS);
			send_response = false;
		}
		if (_wait_for_starting_step(&req->step_id)) {
			/*
			 * There's currently no case in which we enter this
			 * error condition.  If there was, it's hard to say
			 * whether to proceed with the job termination.
			 */
			error("Error in _wait_for_starting_step");
		}
	}

	if (IS_JOB_NODE_FAILED(req))
		_kill_all_active_steps(&req->step_id, SIG_NODE_FAIL, 0,
				       req->details, true, msg->auth_uid);
	if (IS_JOB_PENDING(req))
		_kill_all_active_steps(&req->step_id, SIG_REQUEUED, 0,
				       req->details, true, msg->auth_uid);
	else if (IS_JOB_FAILED(req))
		_kill_all_active_steps(&req->step_id, SIG_FAILURE, 0,
				       req->details, true, msg->auth_uid);

	/*
	 * Tasks might be stopped (possibly by a debugger)
	 * so send SIGCONT first.
	 */
	_kill_all_active_steps(&req->step_id, SIGCONT, 0, req->details, true,
			       msg->auth_uid);
	if (errno == ESLURMD_STEP_SUSPENDED) {
		/*
		 * If the job step is currently suspended, we don't
		 * bother with a "nice" termination.
		 */
		debug2("Job is currently suspended, terminating");
		nsteps = terminate_all_steps(req->step_id.job_id, true,
					     !(slurm_conf.prolog_flags &
					       PROLOG_FLAG_RUN_IN_JOB));
	} else {
		nsteps = _kill_all_active_steps(&req->step_id, SIGTERM, 0,
						req->details, true,
						msg->auth_uid);
	}

	/*
	 *  If there are currently no active job steps and no
	 *    configured epilog to run, bypass asynchronous reply and
	 *    notify slurmctld that we have already completed this
	 *    request. We need to send current switch state on AIX
	 *    systems, so this bypass can not be used.
	 */
	if ((nsteps == 0) && !slurm_conf.epilog && !spank_has_epilog()) {
		debug4("sent ALREADY_COMPLETE");
		if (send_response) {
			slurm_send_rc_msg(msg,
					  ESLURMD_KILL_JOB_ALREADY_COMPLETE);
		}
		cred_begin_expiration(&req->step_id);
		save_cred_state();
		_waiter_complete(&req->step_id);

		/*
		 * The controller needs to get MESSAGE_EPILOG_COMPLETE to bring
		 * the job out of "completing" state.  Otherwise, the job
		 * could remain "completing" unnecessarily, until the request
		 * to terminate is resent.
		 */
		if (!send_response) {
			/* The epilog complete message processing on
			 * slurmctld is equivalent to that of a
			 * ESLURMD_KILL_JOB_ALREADY_COMPLETE reply above */
			epilog_complete(&req->step_id, req->nodes, rc);
		}

		launch_complete_rm(&req->step_id);
		return;
	}

	/*
	 *  At this point, if connection still open, we send controller
	 *   a "success" reply to indicate that we've recvd the msg.
	 */
	if (send_response) {
		debug4("sent SUCCESS");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		send_response = false;
	}

	/*
	 *  Check for corpses
	 */
	delay = MAX(slurm_conf.kill_wait, 5);
	if (!pause_for_job_completion(&req->step_id, delay,
				      (slurm_conf.prolog_flags &
				       PROLOG_FLAG_RUN_IN_JOB)) &&
	    terminate_all_steps(req->step_id.job_id, true,
				!(slurm_conf.prolog_flags &
				  PROLOG_FLAG_RUN_IN_JOB))) {
		/*
		 *  Block until all user processes are complete.
		 */
		pause_for_job_completion(&req->step_id, 0,
					 (slurm_conf.prolog_flags &
					  PROLOG_FLAG_RUN_IN_JOB));
	}

	/*
	 *  Begin expiration period for cached information about job.
	 *   If expiration period has already begun, then do not run
	 *   the epilog again, as that script has already been executed
	 *   for this job.
	 */
	if (cred_begin_expiration(&req->step_id) < 0) {
		debug("Not running epilog for jobid %d: %m", req->step_id.job_id);
		goto done;
	}

	save_cred_state();

	_file_bcast_job_cleanup(req->step_id.job_id);

	if (!(slurm_conf.prolog_flags & PROLOG_FLAG_RUN_IN_JOB)) {
		job_env_t job_env;
		int node_id = nodelist_find(req->nodes, conf->node_name);

		memset(&job_env, 0, sizeof(job_env));
		gres_g_prep_set_env(&job_env.gres_job_env, req->job_gres_prep,
				    node_id);

		job_env.step_id = req->step_id;
		job_env.derived_ec = req->derived_ec;
		job_env.exit_code = req->exit_code;
		job_env.node_list = req->nodes;
		job_env.het_job_id = req->het_job_id;
		job_env.spank_job_env = req->spank_job_env;
		job_env.spank_job_env_size = req->spank_job_env_size;
		job_env.work_dir = req->work_dir;
		job_env.uid = req->job_uid;
		job_env.gid = req->job_gid;

		_wait_for_job_running_prolog(&req->step_id);
		rc = run_epilog(&job_env, req->cred);
		_free_job_env(&job_env);
		if (rc) {
			int term_sig = 0, exit_status = 0;
			if (WIFSIGNALED(rc))
				term_sig = WTERMSIG(rc);
			else if (WIFEXITED(rc))
				exit_status = WEXITSTATUS(rc);
			error("[job %u] epilog failed status=%d:%d",
			req->step_id.job_id, exit_status, term_sig);
			rc = ESLURMD_EPILOG_FAILED;
		} else
			debug("completed epilog for jobid %u",
			      req->step_id.job_id);
	}
	launch_complete_rm(&req->step_id);

done:
	_wait_state_completed(&req->step_id, 5);
	_waiter_complete(&req->step_id);

	if (!(slurm_conf.prolog_flags & PROLOG_FLAG_RUN_IN_JOB))
		epilog_complete(&req->step_id, req->nodes, rc);
}

/*
 *  Returns true if "uid" is a "slurm authorized user" - i.e. uid == 0
 *   or uid == slurm user id at this time.
 */
static bool
_slurm_authorized_user(uid_t uid)
{
	return ((uid == (uid_t) 0) || (uid == slurm_conf.slurm_user_id));
}

static uint32_t *_waiter_create(uint32_t jobid)
{
	uint32_t *wp = xmalloc(sizeof(*wp));

	*wp = jobid;

	return wp;
}

static int _find_waiter(void *x, void *y)
{
	uint32_t *w = x;
	uint32_t *jp = (uint32_t *)y;

	return (*w == *jp);
}

static int _waiter_init(slurm_step_id_t *step_id)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&waiter_mutex);
	if (!waiters)
		waiters = list_create(xfree_ptr);

	/*
	 *  Exit this thread if another thread is waiting on job
	 */
	if (list_find_first(waiters, _find_waiter, &step_id->job_id))
		rc = SLURM_ERROR;
	else
		list_append(waiters, _waiter_create(step_id->job_id));

	slurm_mutex_unlock(&waiter_mutex);

	return rc;
}

static void _waiter_complete(slurm_step_id_t *step_id)
{
	slurm_mutex_lock(&waiter_mutex);
	if (waiters)
		list_delete_all(waiters, _find_waiter, &step_id->job_id);
	slurm_mutex_unlock(&waiter_mutex);
}

static void _free_job_env(job_env_t *env_ptr)
{
	int i;

	if (env_ptr->gres_job_env) {
		for (i = 0; env_ptr->gres_job_env[i]; i++)
			xfree(env_ptr->gres_job_env[i]);
		xfree(env_ptr->gres_job_env);
	}
	/* NOTE: spank_job_env is just a pointer without allocated memory */
}

static int
_add_starting_step(uint16_t type, void *req)
{
	slurm_step_id_t *starting_step;

	/* Add the step info to a list of starting processes that
	   cannot reliably be contacted. */
	starting_step = xmalloc(sizeof(slurm_step_id_t));

	switch (type) {
	case LAUNCH_BATCH_JOB:
		*starting_step = ((batch_job_launch_msg_t *) req)->step_id;
		break;
	case LAUNCH_TASKS:
		memcpy(starting_step,
		       &((launch_tasks_request_msg_t *)req)->step_id,
		       sizeof(*starting_step));
		break;
	case REQUEST_LAUNCH_PROLOG:
		*starting_step = ((prolog_launch_msg_t *) req)->step_id;
		break;
	default:
		error("%s called with an invalid type: %u", __func__, type);
		xfree(starting_step);
		return SLURM_ERROR;
	}

	list_append(conf->starting_steps, starting_step);

	return SLURM_SUCCESS;
}


static int
_remove_starting_step(uint16_t type, void *req)
{
	slurm_step_id_t starting_step = { 0 };
	int rc = SLURM_SUCCESS;

	switch(type) {
	case LAUNCH_BATCH_JOB:
		starting_step = ((batch_job_launch_msg_t *) req)->step_id;
		break;
	case LAUNCH_TASKS:
		memcpy(&starting_step,
		       &((launch_tasks_request_msg_t *)req)->step_id,
		       sizeof(starting_step));
		break;
	default:
		error("%s called with an invalid type: %u", __func__, type);
		rc = SLURM_ERROR;
		goto fail;
	}

	if (!list_delete_all(conf->starting_steps,
			     (ListCmpF) verify_step_id,
			     &starting_step)) {
		error("%s: %ps not found", __func__, &starting_step);
		rc = SLURM_ERROR;
	}
	slurm_cond_broadcast(&conf->starting_steps_cond);
fail:
	return rc;
}

/* Wait for a step to get far enough in the launch process to have
   a socket open, ready to handle RPC calls.  Pass step_id = NO_VAL
   to wait on any step for the given job. */

static int _wait_for_starting_step(slurm_step_id_t *step_id)
{
	static pthread_mutex_t dummy_lock = PTHREAD_MUTEX_INITIALIZER;
	struct timespec ts = {0, 0};
	struct timeval now;

	int num_passes = 0;

	while (list_find_first(conf->starting_steps,
			       (ListCmpF) verify_step_id,
			       step_id)) {
		if (num_passes == 0) {
			if (step_id->step_id != NO_VAL)
				debug("Blocked waiting for %ps", step_id);
			else
				debug("Blocked waiting for %ps, all steps",
				      step_id);
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
		if (step_id->step_id != NO_VAL)
			debug("Finished wait for step %ps", step_id);
		else
			debug("Finished wait for %ps, all steps",
			      step_id);
	}

	return SLURM_SUCCESS;
}


/* Return true if the step has not yet confirmed that its socket to
   handle RPC calls has been created.  Pass step_id = NO_VAL
   to return true if any of the job's steps are still starting. */
static bool _step_is_starting(slurm_step_id_t *step_id)
{
	return list_find_first(conf->starting_steps,
			       (ListCmpF) verify_step_id,
			       step_id);
}

static int _match_job(void *x, void *key)
{
	slurm_step_id_t *step1 = x, *step2 = key;

	/* Only compare if both have sluid set */
	if (step1->sluid && step2->sluid)
		return (step1->sluid == step2->sluid);

	/* Otherwise fall back to the job_id */
	return (step1->job_id == step2->job_id);
}

/* Add this job to the list of jobs currently running their prolog */
static void _add_job_running_prolog(slurm_step_id_t *step_id)
{
	slurm_step_id_t *new_id = xmalloc(sizeof(*step_id));

	*new_id = *step_id;

	list_append(conf->prolog_running_jobs, new_id);
}

/* Remove this job from the list of jobs currently running their prolog */
static void _remove_job_running_prolog(slurm_step_id_t *step_id)
{
	if (!list_delete_all(conf->prolog_running_jobs, _match_job, step_id))
		error("%s: %pI not found", __func__, step_id);
	slurm_cond_broadcast(&conf->prolog_running_cond);
}

static bool _prolog_is_running(slurm_step_id_t *step_id)
{
	if (conf->prolog_running_jobs &&
	    list_find_first(conf->prolog_running_jobs, _match_job, step_id))
		return true;
	return false;
}

/* Wait for the job's prolog to complete */
static void _wait_for_job_running_prolog(slurm_step_id_t *step_id)
{
	static pthread_mutex_t dummy_lock = PTHREAD_MUTEX_INITIALIZER;
	struct timespec ts = {0, 0};
	struct timeval now;

	debug("Waiting for %pI prolog to complete", step_id);

	while (_prolog_is_running(step_id)) {
		gettimeofday(&now, NULL);
		ts.tv_sec = now.tv_sec+1;
		ts.tv_nsec = now.tv_usec * 1000;

		slurm_mutex_lock(&dummy_lock);
		slurm_cond_timedwait(&conf->prolog_running_cond,
				     &dummy_lock, &ts);
		slurm_mutex_unlock(&dummy_lock);
	}

	debug("Finished wait for %pI prolog to complete", step_id);
}

/* Wait for the job's prolog launch request */
static int _wait_for_request_launch_prolog(slurm_step_id_t *step_id,
					   bool *first_job_run)
{
	struct timespec ts = {0, 0};
	struct timeval now;
	struct timeval timeout;

	if (!(slurm_conf.prolog_flags & PROLOG_FLAG_ALLOC) || !(*first_job_run))
		return SLURM_SUCCESS;

	/*
	 * We want to wait until the rpc_prolog is ran before
	 * continuing. Since we are already locked on prolog_mutex here
	 * we don't have to unlock to wait on the
	 * conf->prolog_running_cond.
	 */
	debug("Waiting for %pI prolog launch request", step_id);
	gettimeofday(&timeout, NULL);
	timeout.tv_sec += slurm_conf.msg_timeout * 2;
	while (*first_job_run) {
		/*
		 * This race should only happen for at most a second as
		 * we are only waiting for the other rpc to get here.
		 * We should wait here for msg_timeout * 2, in case of
		 * REQUEST_LAUNCH_PROLOG lost in forwarding tree the
		 * direct retry from slurmctld will happen after
		 * MessageTimeout.
		 */
		gettimeofday(&now, NULL);
		ts.tv_sec = now.tv_sec + 1;
		ts.tv_nsec = now.tv_usec * 1000;
		if (now.tv_sec > timeout.tv_sec) {
			error("Waiting for %pI REQUEST_LAUNCH_PROLOG notification failed, giving up after %u sec",
			      step_id, slurm_conf.msg_timeout * 2);
			return ESLURMD_PROLOG_FAILED;
		}

		slurm_cond_timedwait(&conf->prolog_running_cond,
				     &prolog_mutex, &ts);
		*first_job_run = !cred_job_cached(step_id);
	}
	debug("Finished wait for %pI prolog launch request", step_id);

	return SLURM_SUCCESS;
}

static void
_rpc_forward_data(slurm_msg_t *msg)
{
	forward_data_msg_t *req = msg->data;
	uint32_t req_uid = msg->auth_uid;
	char *tmp_addr = req->address;
	int fd = -1, rc = 0;

	/*
	 * Make sure we adjust for the spool dir coming in on the address to
	 * point to the right spot. Use conf->node_name for both nodename and
	 * hostname as that is what happens on the other side.
	 */
	req->address = slurm_conf_expand_slurmd_path(tmp_addr,
						     conf->node_name,
						     conf->node_name);
	xfree(tmp_addr);
	debug3("Entering _rpc_forward_data, address: %s, len: %u",
	       req->address, req->len);

	errno = 0;
	rc = _connect_as_other(req->address, req_uid, msg->auth_gid, &fd);

	if ((rc < 0) || (fd < 0)) {
		if (errno)
			rc = errno;
		debug2("failed connecting to specified socket '%s': %m",
		       req->address);
		goto rwfail;
	}

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
	if (fd >= 0)
		close(fd);
	slurm_send_rc_msg(msg, rc);
}

typedef struct {
	uint16_t msg_type;
	bool from_slurmctld;
	void (*func)(slurm_msg_t *msg);
} slurmd_rpc_t;

slurmd_rpc_t slurmd_rpcs[] =
{
	{
		.msg_type = REQUEST_LAUNCH_PROLOG,
		.from_slurmctld = true,
		.func = _rpc_prolog,
	},{
		.msg_type = REQUEST_BATCH_JOB_LAUNCH,
		.from_slurmctld = true,
		.func = _rpc_batch_job,
	},{
		.msg_type = REQUEST_LAUNCH_TASKS,
		.func = _rpc_launch_tasks,
	},{
		.msg_type = REQUEST_SIGNAL_TASKS,
		.func = _rpc_signal_tasks,
	},{
		.msg_type = REQUEST_TERMINATE_TASKS,
		.func = _rpc_terminate_tasks,
	},{
		.msg_type = REQUEST_KILL_PREEMPTED,
		.from_slurmctld = true,
		.func = _rpc_timelimit,
	},{
		.msg_type = REQUEST_KILL_TIMELIMIT,
		.from_slurmctld = true,
		.func = _rpc_timelimit,
	},{
		.msg_type = REQUEST_REATTACH_TASKS,
		.func = _rpc_reattach_tasks,
	},{
		.msg_type = REQUEST_SUSPEND_INT,
		.from_slurmctld = true,
		.func = _rpc_suspend_job,
	},{
		.msg_type = REQUEST_ABORT_JOB,
		.from_slurmctld = true,
		.func = _rpc_abort_job,
	},{
		.msg_type = REQUEST_TERMINATE_JOB,
		.from_slurmctld = true,
		.func = _rpc_terminate_job,
	},{
		.msg_type = REQUEST_SHUTDOWN,
		.from_slurmctld = true,
		.func = _rpc_shutdown,
	},{
		.msg_type = REQUEST_RECONFIGURE,
		.from_slurmctld = true,
		.func = _rpc_reconfig,
	},{
		.msg_type = REQUEST_SET_DEBUG_FLAGS,
		.func = _rpc_set_slurmd_debug_flags,
	},{
		.msg_type = REQUEST_SET_DEBUG_LEVEL,
		.func = _rpc_set_slurmd_debug,
	},{
		.msg_type = REQUEST_RECONFIGURE_WITH_CONFIG,
		.from_slurmctld = true,
		.func = _rpc_reconfig,
	},{
		.msg_type = REQUEST_REBOOT_NODES,
		.from_slurmctld = true,
		.func = _rpc_reboot,
	},{
		/* Treat as ping (for slurmctld agent, just return SUCCESS) */
		.msg_type = REQUEST_NODE_REGISTRATION_STATUS,
		.from_slurmctld = true,
		.func = _rpc_ping,
	},{
		.msg_type = REQUEST_PING,
		.from_slurmctld = true,
		.func = _rpc_ping,
	},{
		.msg_type = REQUEST_HEALTH_CHECK,
		.from_slurmctld = true,
		.func = _rpc_health_check,
	},{
		.msg_type = REQUEST_ACCT_GATHER_UPDATE,
		.from_slurmctld = true,
		.func = _rpc_acct_gather_update,
	},{
		.msg_type = REQUEST_ACCT_GATHER_ENERGY,
		.func = _rpc_acct_gather_energy,
	},{
		.msg_type = REQUEST_JOB_ID,
		.func = _rpc_pid2jid,
	},{
		.msg_type = REQUEST_FILE_BCAST,
		.func = _rpc_file_bcast,
	},{
		.msg_type = REQUEST_STEP_COMPLETE,
		.func = _rpc_step_complete,
	},{
		.msg_type = REQUEST_JOB_STEP_CREATE,
		.func = _slurm_rpc_job_step_create,
	},{
		.msg_type = REQUEST_JOB_STEP_STAT,
		.func = _rpc_stat_jobacct,
	},{
		.msg_type = REQUEST_JOB_STEP_PIDS,
		.func = _rpc_list_pids,
	},{
		.msg_type = REQUEST_JOB_STEP_INFO,
		.func = _slurm_rpc_job_step_get_info,
	},{
		.msg_type = REQUEST_DAEMON_STATUS,
		.func = _rpc_daemon_status,
	},{
		.msg_type = REQUEST_JOB_NOTIFY,
		.func = _rpc_job_notify,
	},{
		.msg_type = REQUEST_FORWARD_DATA,
		.func = _rpc_forward_data,
	},{
		.msg_type = REQUEST_NETWORK_CALLERID,
		.func = _rpc_network_callerid,
	},{
		.msg_type = REQUEST_CANCEL_JOB_STEP,
		.func = _slurm_rpc_job_step_kill,
	},{
		.msg_type = SRUN_JOB_COMPLETE,
		.func = _slurm_rpc_srun_job_complete,
	},{
		.msg_type = SRUN_NODE_FAIL,
		.func = _slurm_rpc_srun_node_fail,
	},{
		.msg_type = SRUN_TIMEOUT,
		.func = _slurm_rpc_srun_timeout,
	},{
		.msg_type = REQUEST_UPDATE_JOB_STEP,
		.func = _slurm_rpc_update_step,
	},{
		.msg_type = REQUEST_STEP_LAYOUT,
		.func = _slurm_rpc_step_layout,
	},{
		.msg_type = REQUEST_JOB_SBCAST_CRED,
		.func = _slurm_rpc_sbcast_cred,
	},{
		.msg_type = REQUEST_HET_JOB_ALLOC_INFO,
		.func = _slurm_het_job_alloc_info,
	},{
		/* terminate the array. this must be last. */
		.msg_type = 0,
		.func = NULL,
	}
};

extern void slurmd_req(slurm_msg_t *msg)
{
	slurmd_rpc_t *this_rpc = NULL;

	if (msg == NULL) {
		if (startup == 0)
			startup = time(NULL);
		slurm_mutex_lock(&waiter_mutex);
		FREE_NULL_LIST(waiters);
		slurm_mutex_unlock(&waiter_mutex);
		return;
	}

	if (!msg->auth_ids_set) {
		error("%s: received message without previously validated auth",
		      __func__);
		return;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_PROTOCOL) {
		const char *p = rpc_num2string(msg->msg_type);
		info("%s: received opcode %s from %pA uid %u",
		     __func__, p, &msg->address, msg->auth_uid);
	}

	debug2("Processing RPC: %s", rpc_num2string(msg->msg_type));

	for (this_rpc = slurmd_rpcs; this_rpc->msg_type; this_rpc++) {
		if (this_rpc->msg_type == msg->msg_type)
			break;
	}

	if (!this_rpc->msg_type) {
		error("%s: invalid request for msg_type %u",
		      __func__, msg->msg_type);
		slurm_send_rc_msg(msg, EINVAL);
		return;
	}

	if (this_rpc->from_slurmctld) {
		/*
		 * Consistently handle authentication for slurmctld -> slurmd
		 * connections, rather than deferring to each rpc handler.
		 */
		if (!_slurm_authorized_user(msg->auth_uid)) {
			error("Security violation: %s req from uid %u",
			      rpc_num2string(msg->msg_type), msg->auth_uid);
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
			return;
		}

		last_slurmctld_msg = time(NULL);
	}

	this_rpc->func(msg);
}
