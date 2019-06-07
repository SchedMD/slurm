/*****************************************************************************\
 *  proc_req.c - process incoming messages to slurmctld
 *****************************************************************************
 *  Copyright (C) 2010-2017 SchedMD LLC.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
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

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/checkpoint.h"
#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/group_cache.h"
#include "src/common/hostlist.h"
#include "src/common/layouts_mgr.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/slurm_persist_conn.h"
#include "src/common/read_config.h"
#include "src/common/slurm_acct_gather.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_topology.h"
#include "src/common/switch.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/common/xcgroup_read_config.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmctld_plugstack.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"

static pthread_mutex_t rpc_mutex = PTHREAD_MUTEX_INITIALIZER;
static int rpc_type_size = 0;	/* Size of rpc_type_* arrays */
static uint16_t *rpc_type_id = NULL;
static uint32_t *rpc_type_cnt = NULL;
static uint64_t *rpc_type_time = NULL;
static int rpc_user_size = 0;	/* Size of rpc_user_* arrays */
static uint32_t *rpc_user_id = NULL;
static uint32_t *rpc_user_cnt = NULL;
static uint64_t *rpc_user_time = NULL;

static pthread_mutex_t throttle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t throttle_cond = PTHREAD_COND_INITIALIZER;

static void         _create_pack_job_id_set(hostset_t jobid_hostset,
					    uint32_t pack_job_offset,
					    char **pack_job_id_set);
static void         _fill_ctld_conf(slurm_ctl_conf_t * build_ptr);
static void         _kill_job_on_msg_fail(uint32_t job_id);
static int          _is_prolog_finished(uint32_t job_id);
static int          _make_step_cred(struct step_record *step_rec,
				    slurm_cred_t **slurm_cred,
				    uint16_t protocol_version);
inline static void  _proc_multi_msg(uint32_t rpc_uid, slurm_msg_t *msg);
static int          _route_msg_to_origin(slurm_msg_t *msg, char *job_id_str,
					 uint32_t job_id, uid_t uid);
static void         _throttle_fini(int *active_rpc_cnt);
static void         _throttle_start(int *active_rpc_cnt);

inline static void  _slurm_rpc_accounting_first_reg(slurm_msg_t *msg);
inline static void  _slurm_rpc_accounting_register_ctld(slurm_msg_t *msg);
inline static void  _slurm_rpc_accounting_update_msg(slurm_msg_t *msg);
inline static void  _slurm_rpc_allocate_pack(slurm_msg_t * msg);
inline static void  _slurm_rpc_allocate_resources(slurm_msg_t * msg);
inline static void  _slurm_rpc_burst_buffer_info(slurm_msg_t * msg);
inline static void  _slurm_rpc_burst_buffer_status(slurm_msg_t *msg);
inline static void  _slurm_rpc_checkpoint(slurm_msg_t * msg);
inline static void  _slurm_rpc_checkpoint_comp(slurm_msg_t * msg);
inline static void  _slurm_rpc_checkpoint_task_comp(slurm_msg_t * msg);
inline static void  _slurm_rpc_control_status(slurm_msg_t * msg);
inline static void  _slurm_rpc_delete_partition(slurm_msg_t * msg);
inline static void  _slurm_rpc_complete_job_allocation(slurm_msg_t * msg);
inline static void  _slurm_rpc_complete_batch_script(slurm_msg_t * msg,
						     bool *run_scheduler,
						     bool running_composite);
inline static void  _slurm_rpc_complete_prolog(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_batch_script(slurm_msg_t *msg);
inline static void  _slurm_rpc_dump_conf(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_front_end(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_jobs(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_jobs_user(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_job_single(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_licenses(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_nodes(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_node_single(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_partitions(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_spank(slurm_msg_t * msg);
inline static void  _slurm_rpc_dump_stats(slurm_msg_t * msg);
inline static void  _slurm_rpc_end_time(slurm_msg_t * msg);
inline static void  _slurm_rpc_event_log(slurm_msg_t * msg);
inline static void  _slurm_rpc_epilog_complete(slurm_msg_t * msg,
					       bool *run_scheduler,
					       bool running_composite);
inline static void  _slurm_rpc_get_fed(slurm_msg_t *msg);
inline static void  _slurm_rpc_get_shares(slurm_msg_t *msg);
inline static void  _slurm_rpc_get_topo(slurm_msg_t * msg);
inline static void  _slurm_rpc_get_powercap(slurm_msg_t * msg);
inline static void  _slurm_rpc_get_priority_factors(slurm_msg_t *msg);
inline static void  _slurm_rpc_job_notify(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_ready(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_sbcast_cred(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_step_kill(uint32_t uid, slurm_msg_t * msg);
inline static void  _slurm_rpc_job_step_create(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_step_get_info(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_will_run(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_alloc_info(slurm_msg_t * msg);
inline static void  _slurm_rpc_job_pack_alloc_info(slurm_msg_t * msg);
inline static void  _slurm_rpc_kill_job(slurm_msg_t *msg);
inline static void  _slurm_rpc_node_registration(slurm_msg_t *msg,
						 bool running_composite);
inline static void  _slurm_rpc_ping(slurm_msg_t * msg);
inline static void  _slurm_rpc_reboot_nodes(slurm_msg_t * msg);
inline static void  _slurm_rpc_reconfigure_controller(slurm_msg_t * msg);
inline static void  _slurm_rpc_resv_create(slurm_msg_t * msg);
inline static void  _slurm_rpc_resv_update(slurm_msg_t * msg);
inline static void  _slurm_rpc_resv_delete(slurm_msg_t * msg);
inline static void  _slurm_rpc_resv_show(slurm_msg_t * msg);
inline static void  _slurm_rpc_layout_show(slurm_msg_t * msg);
inline static void  _slurm_rpc_requeue(slurm_msg_t * msg);
inline static void  _slurm_rpc_takeover(slurm_msg_t * msg);
inline static void  _slurm_rpc_set_debug_flags(slurm_msg_t *msg);
inline static void  _slurm_rpc_set_debug_level(slurm_msg_t *msg);
inline static void  _slurm_rpc_set_schedlog_level(slurm_msg_t *msg);
inline static void  _slurm_rpc_shutdown_controller(slurm_msg_t * msg);
inline static void  _slurm_rpc_shutdown_controller_immediate(slurm_msg_t *msg);
inline static void  _slurm_rpc_set_fs_dampening_factor(slurm_msg_t *msg);

inline static void _slurm_rpc_sib_job_lock(uint32_t uid, slurm_msg_t *msg);
inline static void _slurm_rpc_sib_job_unlock(uint32_t uid, slurm_msg_t *msg);
inline static void _slurm_rpc_sib_msg(uint32_t uid, slurm_msg_t *msg);

inline static void  _slurm_rpc_step_complete(slurm_msg_t * msg,
					     bool running_composite);
inline static void  _slurm_rpc_step_layout(slurm_msg_t * msg);
inline static void  _slurm_rpc_step_update(slurm_msg_t * msg);
inline static void  _slurm_rpc_submit_batch_job(slurm_msg_t * msg);
inline static void  _slurm_rpc_submit_batch_pack_job(slurm_msg_t * msg);
inline static void  _slurm_rpc_suspend(slurm_msg_t * msg);
inline static void  _slurm_rpc_top_job(slurm_msg_t * msg);
inline static void  _slurm_rpc_trigger_clear(slurm_msg_t * msg);
inline static void  _slurm_rpc_trigger_get(slurm_msg_t * msg);
inline static void  _slurm_rpc_trigger_set(slurm_msg_t * msg);
inline static void  _slurm_rpc_trigger_pull(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_front_end(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_job(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_node(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_layout(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_partition(slurm_msg_t * msg);
inline static void  _slurm_rpc_update_powercap(slurm_msg_t * msg);
inline static void  _update_cred_key(void);

static void  _slurm_rpc_composite_msg(slurm_msg_t *msg);
static void  _slurm_rpc_comp_msg_list(composite_msg_t * comp_msg,
				      bool *run_scheduler,
				      List msg_list_in,
				      struct timeval *start_tv,
				      int timeout);
static void  _slurm_rpc_assoc_mgr_info(slurm_msg_t * msg);
static void  _slurm_rpc_persist_init(slurm_msg_t *msg, connection_arg_t *arg);

extern diag_stats_t slurmctld_diag_stats;

#ifndef NDEBUG
/*
 * Used alongside the testsuite to signal that the RPC should be processed
 * as an untrusted user, rather than the "real" account. (Which in a lot of
 * testing is likely SlurmUser, and thus allowed to bypass many security
 * checks.
 *
 * Implemented with a thread-local variable to apply only to the current
 * RPC handling thread. Set by SLURM_DROP_PRIV bit in the slurm_msg_t flags.
 */
static __thread bool drop_priv = false;
#endif

/*
 * slurmctld_req  - Process an individual RPC request
 * IN/OUT msg - the request message, data associated with the message is freed
 */
void slurmctld_req(slurm_msg_t *msg, connection_arg_t *arg)
{
	DEF_TIMERS;
	int i, rpc_type_index = -1, rpc_user_index = -1;
	uint32_t rpc_uid;

	if (arg && (arg->newsockfd >= 0))
		fd_set_nonblocking(arg->newsockfd);

#ifndef NDEBUG
	if ((msg->flags & SLURM_DROP_PRIV))
		drop_priv = true;
#endif

	/* Validate the credential */
	if (g_slurm_auth_verify(msg->auth_cred, slurmctld_config.auth_info)) {
		error("Bad authentication: %m");
		return;
	}
	rpc_uid = (uint32_t) g_slurm_auth_get_uid(msg->auth_cred);

	slurm_mutex_lock(&rpc_mutex);
	if (rpc_type_size == 0) {
		rpc_type_size = 100;  /* Capture info for first 100 RPC types */
		rpc_type_id   = xmalloc(sizeof(uint16_t) * rpc_type_size);
		rpc_type_cnt  = xmalloc(sizeof(uint32_t) * rpc_type_size);
		rpc_type_time = xmalloc(sizeof(uint64_t) * rpc_type_size);
	}
	for (i = 0; i < rpc_type_size; i++) {
		if (rpc_type_id[i] == 0)
			rpc_type_id[i] = msg->msg_type;
		else if (rpc_type_id[i] != msg->msg_type)
			continue;
		rpc_type_index = i;
		break;
	}
	if (rpc_user_size == 0) {
		rpc_user_size = 200;  /* Capture info for first 200 RPC users */
		rpc_user_id   = xmalloc(sizeof(uint32_t) * rpc_user_size);
		rpc_user_cnt  = xmalloc(sizeof(uint32_t) * rpc_user_size);
		rpc_user_time = xmalloc(sizeof(uint64_t) * rpc_user_size);
	}
	for (i = 0; i < rpc_user_size; i++) {
		if ((rpc_user_id[i] == 0) && (i != 0))
			rpc_user_id[i] = rpc_uid;
		else if (rpc_user_id[i] != rpc_uid)
			continue;
		rpc_user_index = i;
		break;
	}
	slurm_mutex_unlock(&rpc_mutex);

	/* Debug the protocol layer.
	 */
	START_TIMER;
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_PROTOCOL) {
		char *p = rpc_num2string(msg->msg_type);
		if (msg->conn) {
			info("%s: received opcode %s from persist conn on (%s)%s",
			     __func__, p, msg->conn->cluster_name,
			     msg->conn->rem_host);
		} else if (arg) {
			char inetbuf[64];
			slurm_print_slurm_addr(&arg->cli_addr,
					       inetbuf,
					       sizeof(inetbuf));
			info("%s: received opcode %s from %s",
			     __func__, p, inetbuf);
		} else {
			error("%s: No arg given and this doesn't appear to be a persistent connection, this should never happen", __func__);
		}
	}

	switch (msg->msg_type) {
	case REQUEST_RESOURCE_ALLOCATION:
		_slurm_rpc_allocate_resources(msg);
		break;
	case REQUEST_JOB_PACK_ALLOCATION:
		_slurm_rpc_allocate_pack(msg);
		break;
	case REQUEST_BUILD_INFO:
		_slurm_rpc_dump_conf(msg);
		break;
	case REQUEST_JOB_INFO:
		_slurm_rpc_dump_jobs(msg);
		break;
	case REQUEST_JOB_USER_INFO:
		_slurm_rpc_dump_jobs_user(msg);
		break;
	case REQUEST_JOB_INFO_SINGLE:
		_slurm_rpc_dump_job_single(msg);
		break;
	case REQUEST_BATCH_SCRIPT:
		_slurm_rpc_dump_batch_script(msg);
		break;
	case REQUEST_SHARE_INFO:
		_slurm_rpc_get_shares(msg);
		break;
	case REQUEST_PRIORITY_FACTORS:
		_slurm_rpc_get_priority_factors(msg);
		break;
	case REQUEST_JOB_END_TIME:
		_slurm_rpc_end_time(msg);
		break;
	case REQUEST_FED_INFO:
		_slurm_rpc_get_fed(msg);
		break;
	case REQUEST_FRONT_END_INFO:
		_slurm_rpc_dump_front_end(msg);
		break;
	case REQUEST_NODE_INFO:
		_slurm_rpc_dump_nodes(msg);
		break;
	case REQUEST_NODE_INFO_SINGLE:
		_slurm_rpc_dump_node_single(msg);
		break;
	case REQUEST_PARTITION_INFO:
		_slurm_rpc_dump_partitions(msg);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		i = 0;
		_slurm_rpc_epilog_complete(msg, (bool *)&i, 0);
		break;
	case REQUEST_CANCEL_JOB_STEP:
		_slurm_rpc_job_step_kill(rpc_uid, msg);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		_slurm_rpc_complete_job_allocation(msg);
		break;
	case REQUEST_COMPLETE_PROLOG:
		_slurm_rpc_complete_prolog(msg);
		break;
	case REQUEST_COMPLETE_BATCH_JOB:
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		i = 0;
		_slurm_rpc_complete_batch_script(msg, (bool *)&i, 0);
		break;
	case REQUEST_JOB_STEP_CREATE:
		_slurm_rpc_job_step_create(msg);
		break;
	case REQUEST_JOB_STEP_INFO:
		_slurm_rpc_job_step_get_info(msg);
		break;
	case REQUEST_JOB_WILL_RUN:
		_slurm_rpc_job_will_run(msg);
		break;
	case REQUEST_SIB_JOB_LOCK:
		_slurm_rpc_sib_job_lock(rpc_uid, msg);
		break;
	case REQUEST_SIB_JOB_UNLOCK:
		_slurm_rpc_sib_job_unlock(rpc_uid, msg);
		break;
	case REQUEST_CTLD_MULT_MSG:
		_proc_multi_msg(rpc_uid, msg);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		_slurm_rpc_node_registration(msg, 0);
		break;
	case REQUEST_JOB_ALLOCATION_INFO:
		_slurm_rpc_job_alloc_info(msg);
		break;
	case REQUEST_JOB_PACK_ALLOC_INFO:
		_slurm_rpc_job_pack_alloc_info(msg);
		break;
	case REQUEST_JOB_SBCAST_CRED:
		_slurm_rpc_job_sbcast_cred(msg);
		break;
	case REQUEST_PING:
		_slurm_rpc_ping(msg);
		break;
	case REQUEST_RECONFIGURE:
		_slurm_rpc_reconfigure_controller(msg);
		break;
	case REQUEST_CONTROL:
		_slurm_rpc_shutdown_controller(msg);
		break;
	case REQUEST_TAKEOVER:
		_slurm_rpc_takeover(msg);
		break;
	case REQUEST_SHUTDOWN:
		_slurm_rpc_shutdown_controller(msg);
		break;
	case REQUEST_SHUTDOWN_IMMEDIATE:
		_slurm_rpc_shutdown_controller_immediate(msg);
		break;
	case REQUEST_SUBMIT_BATCH_JOB:
		_slurm_rpc_submit_batch_job(msg);
		break;
	case REQUEST_SUBMIT_BATCH_JOB_PACK:
		_slurm_rpc_submit_batch_pack_job(msg);
		break;
	case REQUEST_UPDATE_FRONT_END:
		_slurm_rpc_update_front_end(msg);
		break;
	case REQUEST_UPDATE_JOB:
		_slurm_rpc_update_job(msg);
		break;
	case REQUEST_UPDATE_NODE:
		_slurm_rpc_update_node(msg);
		break;
	case REQUEST_UPDATE_LAYOUT:
		_slurm_rpc_update_layout(msg);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		_slurm_rpc_update_partition(msg);
		break;
	case REQUEST_UPDATE_POWERCAP:
		_slurm_rpc_update_powercap(msg);
		break;
	case REQUEST_DELETE_PARTITION:
		_slurm_rpc_delete_partition(msg);
		break;
	case REQUEST_CREATE_RESERVATION:
		_slurm_rpc_resv_create(msg);
		break;
	case REQUEST_UPDATE_RESERVATION:
		_slurm_rpc_resv_update(msg);
		break;
	case REQUEST_DELETE_RESERVATION:
		_slurm_rpc_resv_delete(msg);
		break;
	case REQUEST_RESERVATION_INFO:
		_slurm_rpc_resv_show(msg);
		break;
	case REQUEST_LAYOUT_INFO:
		_slurm_rpc_layout_show(msg);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		error("slurmctld is talking with itself. "
		      "SlurmctldPort == SlurmdPort");
		slurm_send_rc_msg(msg, EINVAL);
		break;
	case REQUEST_CHECKPOINT:
		_slurm_rpc_checkpoint(msg);
		break;
	case REQUEST_CHECKPOINT_COMP:
		_slurm_rpc_checkpoint_comp(msg);
		break;
	case REQUEST_CHECKPOINT_TASK_COMP:
		_slurm_rpc_checkpoint_task_comp(msg);
		break;
	case REQUEST_SUSPEND:
		_slurm_rpc_suspend(msg);
		break;
	case REQUEST_TOP_JOB:
		_slurm_rpc_top_job(msg);
		break;
	case REQUEST_JOB_REQUEUE:
		_slurm_rpc_requeue(msg);
		break;
	case REQUEST_JOB_READY:
		_slurm_rpc_job_ready(msg);
		break;
	case REQUEST_BURST_BUFFER_INFO:
		_slurm_rpc_burst_buffer_info(msg);
		break;
	case REQUEST_STEP_COMPLETE:
		_slurm_rpc_step_complete(msg, 0);
		break;
	case REQUEST_STEP_LAYOUT:
		_slurm_rpc_step_layout(msg);
		break;
	case REQUEST_UPDATE_JOB_STEP:
		_slurm_rpc_step_update(msg);
		break;
	case REQUEST_TRIGGER_SET:
		_slurm_rpc_trigger_set(msg);
		break;
	case REQUEST_TRIGGER_GET:
		_slurm_rpc_trigger_get(msg);
		break;
	case REQUEST_TRIGGER_CLEAR:
		_slurm_rpc_trigger_clear(msg);
		break;
	case REQUEST_TRIGGER_PULL:
		_slurm_rpc_trigger_pull(msg);
		break;
	case REQUEST_JOB_NOTIFY:
		_slurm_rpc_job_notify(msg);
		break;
	case REQUEST_SET_DEBUG_FLAGS:
		_slurm_rpc_set_debug_flags(msg);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
		_slurm_rpc_set_debug_level(msg);
		break;
	case REQUEST_SET_SCHEDLOG_LEVEL:
		_slurm_rpc_set_schedlog_level(msg);
		break;
	case ACCOUNTING_UPDATE_MSG:
		_slurm_rpc_accounting_update_msg(msg);
		break;
	case ACCOUNTING_FIRST_REG:
		_slurm_rpc_accounting_first_reg(msg);
		break;
	case ACCOUNTING_REGISTER_CTLD:
		_slurm_rpc_accounting_register_ctld(msg);
		break;
	case REQUEST_TOPO_INFO:
		_slurm_rpc_get_topo(msg);
		break;
	case REQUEST_POWERCAP_INFO:
		_slurm_rpc_get_powercap(msg);
		break;
	case REQUEST_SPANK_ENVIRONMENT:
		_slurm_rpc_dump_spank(msg);
		break;
	case REQUEST_REBOOT_NODES:
		_slurm_rpc_reboot_nodes(msg);
		break;
	case REQUEST_STATS_INFO:
		_slurm_rpc_dump_stats(msg);
		break;
	case REQUEST_LICENSE_INFO:
		_slurm_rpc_dump_licenses(msg);
		break;
	case REQUEST_KILL_JOB:
		_slurm_rpc_kill_job(msg);
		break;
	case MESSAGE_COMPOSITE:
		_slurm_rpc_composite_msg(msg);
		break;
	case REQUEST_ASSOC_MGR_INFO:
		_slurm_rpc_assoc_mgr_info(msg);
		break;
	case REQUEST_PERSIST_INIT:
		if (msg->conn)
			error("We already have a persistent connect, this should never happen");
		_slurm_rpc_persist_init(msg, arg);
		break;
	case REQUEST_EVENT_LOG:
		_slurm_rpc_event_log(msg);
		break;
	case REQUEST_SET_FS_DAMPENING_FACTOR:
		_slurm_rpc_set_fs_dampening_factor(msg);
		break;
	case REQUEST_CONTROL_STATUS:
		_slurm_rpc_control_status(msg);
		break;
	case REQUEST_BURST_BUFFER_STATUS:
		_slurm_rpc_burst_buffer_status(msg);
		break;
	default:
		error("invalid RPC msg_type=%u", msg->msg_type);
		slurm_send_rc_msg(msg, EINVAL);
		break;
	}

	END_TIMER;
	slurm_mutex_lock(&rpc_mutex);
	if (rpc_type_index >= 0) {
		rpc_type_cnt[rpc_type_index]++;
		rpc_type_time[rpc_type_index] += DELTA_TIMER;
	}
	if (rpc_user_index >= 0) {
		rpc_user_cnt[rpc_user_index]++;
		rpc_user_time[rpc_user_index] += DELTA_TIMER;
	}
	slurm_mutex_unlock(&rpc_mutex);
}

/* These functions prevent certain RPCs from keeping the slurmctld write locks
 * constantly set, which can prevent other RPCs and system functions from being
 * processed. For example, a steady stream of batch submissions can prevent
 * squeue from responding or jobs from being scheduled. */
static void _throttle_start(int *active_rpc_cnt)
{
	slurm_mutex_lock(&throttle_mutex);
	while (1) {
		if (*active_rpc_cnt == 0) {
			(*active_rpc_cnt)++;
			break;
		}
#if 1
		slurm_cond_wait(&throttle_cond, &throttle_mutex);
#else
		/* While an RPC is being throttled due to a running RPC of the
		 * same type, do not count that thread against the daemon's
		 * thread limit. In extreme environments, this logic can result
		 * in the slurmctld spawning so many pthreads that it exhausts
		 * system resources and fails. */
		server_thread_decr();
		slurm_cond_wait(&throttle_cond, &throttle_mutex);
		server_thread_incr();
#endif
	}
	slurm_mutex_unlock(&throttle_mutex);
	if (LOTS_OF_AGENTS)
		usleep(1000);
	else
		usleep(1);
}
static void _throttle_fini(int *active_rpc_cnt)
{
	slurm_mutex_lock(&throttle_mutex);
	(*active_rpc_cnt)--;
	slurm_cond_broadcast(&throttle_cond);
	slurm_mutex_unlock(&throttle_mutex);
}

/*
 * Initialize a response slurm_msg_t to an inbound msg,
 * first by calling slurm_msg_t_init(), then by copying
 * fields needed to communicate with the remote correctly.
 */
extern void response_init(slurm_msg_t *resp, slurm_msg_t *msg)
{
	slurm_msg_t_init(resp);
	resp->address = msg->address;
	resp->auth_index = msg->auth_index;
	resp->conn = msg->conn;
	resp->flags = msg->flags;
	resp->protocol_version = msg->protocol_version;
}

/*
 * _fill_ctld_conf - make a copy of current slurm configuration
 *	this is done with locks set so the data can change at other times
 * OUT conf_ptr - place to copy configuration to
 */
static void _fill_ctld_conf(slurm_ctl_conf_t * conf_ptr)
{
	slurm_ctl_conf_t *conf = &slurmctld_conf;
	char *licenses_used;
	uint32_t next_job_id;
	int i;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	licenses_used = get_licenses_used();

	next_job_id   = get_next_job_id(true);

	memset(conf_ptr, 0, sizeof(slurm_ctl_conf_t));

	conf_ptr->last_update         = time(NULL);
	conf_ptr->accounting_storage_enforce =
		conf->accounting_storage_enforce;
	conf_ptr->accounting_storage_host =
		xstrdup(conf->accounting_storage_host);
	conf_ptr->accounting_storage_backup_host =
		xstrdup(conf->accounting_storage_backup_host);
	conf_ptr->accounting_storage_loc =
		xstrdup(conf->accounting_storage_loc);
	conf_ptr->accounting_storage_port = conf->accounting_storage_port;
	conf_ptr->accounting_storage_tres =
		xstrdup(conf->accounting_storage_tres);
	conf_ptr->accounting_storage_type =
		xstrdup(conf->accounting_storage_type);
	conf_ptr->accounting_storage_user =
		xstrdup(conf->accounting_storage_user);
	conf_ptr->acctng_store_job_comment = conf->acctng_store_job_comment;

	conf_ptr->acct_gather_conf = acct_gather_conf_values();
	conf_ptr->acct_gather_energy_type =
		xstrdup(conf->acct_gather_energy_type);
	conf_ptr->acct_gather_filesystem_type =
		xstrdup(conf->acct_gather_filesystem_type);
	conf_ptr->acct_gather_interconnect_type =
		xstrdup(conf->acct_gather_interconnect_type);
	conf_ptr->acct_gather_profile_type =
		xstrdup(conf->acct_gather_profile_type);
	conf_ptr->acct_gather_node_freq = conf->acct_gather_node_freq;

	conf_ptr->authinfo            = xstrdup(conf->authinfo);
	conf_ptr->authtype            = xstrdup(conf->authtype);

	conf_ptr->batch_start_timeout = conf->batch_start_timeout;
	conf_ptr->boot_time           = slurmctld_config.boot_time;
	conf_ptr->bb_type             = xstrdup(conf->bb_type);

	if (strstr(conf->job_acct_gather_type, "cgroup") ||
	    strstr(conf->proctrack_type, "cgroup") ||
	    strstr(conf->task_plugin, "cgroup"))
		conf_ptr->cgroup_conf = xcgroup_get_conf_list();

	conf_ptr->checkpoint_type     = xstrdup(conf->checkpoint_type);
	conf_ptr->cli_filter_plugins  = xstrdup(conf->cli_filter_plugins);
	conf_ptr->cluster_name        = xstrdup(conf->cluster_name);
	conf_ptr->comm_params         = xstrdup(conf->comm_params);
	conf_ptr->complete_wait       = conf->complete_wait;
	conf_ptr->control_cnt         = conf->control_cnt;
	conf_ptr->control_addr    = xmalloc(sizeof(char *) * conf->control_cnt);
	conf_ptr->control_machine = xmalloc(sizeof(char *) * conf->control_cnt);
	for (i = 0; i < conf_ptr->control_cnt; i++) {
		conf_ptr->control_addr[i] = xstrdup(conf->control_addr[i]);
		conf_ptr->control_machine[i] =
			xstrdup(conf->control_machine[i]);
	}
	conf_ptr->core_spec_plugin    = xstrdup(conf->core_spec_plugin);
	conf_ptr->cpu_freq_def        = conf->cpu_freq_def;
	conf_ptr->cpu_freq_govs       = conf->cpu_freq_govs;
	conf_ptr->cred_type           = xstrdup(conf->cred_type);

	conf_ptr->def_mem_per_cpu     = conf->def_mem_per_cpu;
	conf_ptr->debug_flags         = conf->debug_flags;
	conf_ptr->disable_root_jobs   = conf->disable_root_jobs;

	conf_ptr->eio_timeout         = conf->eio_timeout;
	conf_ptr->enforce_part_limits = conf->enforce_part_limits;
	conf_ptr->epilog              = xstrdup(conf->epilog);
	conf_ptr->epilog_msg_time     = conf->epilog_msg_time;
	conf_ptr->epilog_slurmctld    = xstrdup(conf->epilog_slurmctld);
	ext_sensors_g_get_config(&conf_ptr->ext_sensors_conf);
	conf_ptr->ext_sensors_type    = xstrdup(conf->ext_sensors_type);
	conf_ptr->ext_sensors_freq    = conf->ext_sensors_freq;

	conf_ptr->fast_schedule       = conf->fast_schedule;
	conf_ptr->fed_params          = xstrdup(conf->fed_params);
	conf_ptr->first_job_id        = conf->first_job_id;
	conf_ptr->fs_dampening_factor = conf->fs_dampening_factor;

	conf_ptr->gres_plugins        = xstrdup(conf->gres_plugins);
	conf_ptr->group_time          = conf->group_time;
	conf_ptr->group_force         = conf->group_force;
	conf_ptr->gpu_freq_def        = xstrdup(conf->gpu_freq_def);

	conf_ptr->inactive_limit      = conf->inactive_limit;

	conf_ptr->hash_val            = conf->hash_val;
	conf_ptr->health_check_interval = conf->health_check_interval;
	conf_ptr->health_check_node_state = conf->health_check_node_state;
	conf_ptr->health_check_program = xstrdup(conf->health_check_program);

	conf_ptr->job_acct_gather_freq  = xstrdup(conf->job_acct_gather_freq);
	conf_ptr->job_acct_gather_type  = xstrdup(conf->job_acct_gather_type);
	conf_ptr->job_acct_gather_params= xstrdup(conf->job_acct_gather_params);
	conf_ptr->job_acct_oom_kill    = conf->job_acct_oom_kill;

	conf_ptr->job_ckpt_dir        = xstrdup(conf->job_ckpt_dir);
	conf_ptr->job_comp_host       = xstrdup(conf->job_comp_host);
	conf_ptr->job_comp_loc        = xstrdup(conf->job_comp_loc);
	conf_ptr->job_comp_port       = conf->job_comp_port;
	conf_ptr->job_comp_type       = xstrdup(conf->job_comp_type);
	conf_ptr->job_comp_user       = xstrdup(conf->job_comp_user);
	conf_ptr->job_container_plugin = xstrdup(conf->job_container_plugin);

	conf_ptr->job_credential_private_key =
		xstrdup(conf->job_credential_private_key);
	conf_ptr->job_credential_public_certificate =
		xstrdup(conf->job_credential_public_certificate);
	conf_ptr->job_defaults_list   =
		job_defaults_copy(conf->job_defaults_list);
	conf_ptr->job_file_append     = conf->job_file_append;
	conf_ptr->job_requeue         = conf->job_requeue;
	conf_ptr->job_submit_plugins  = xstrdup(conf->job_submit_plugins);

	conf_ptr->get_env_timeout     = conf->get_env_timeout;

	conf_ptr->keep_alive_time     = conf->keep_alive_time;
	conf_ptr->kill_wait           = conf->kill_wait;
	conf_ptr->kill_on_bad_exit    = conf->kill_on_bad_exit;

	conf_ptr->launch_params       = xstrdup(conf->launch_params);
	conf_ptr->launch_type         = xstrdup(conf->launch_type);
	conf_ptr->layouts             = xstrdup(conf->layouts);
	conf_ptr->licenses            = xstrdup(conf->licenses);
	conf_ptr->licenses_used       = licenses_used;
	conf_ptr->log_fmt             = conf->log_fmt;

	conf_ptr->mail_domain         = xstrdup(conf->mail_domain);
	conf_ptr->mail_prog           = xstrdup(conf->mail_prog);
	conf_ptr->max_array_sz        = conf->max_array_sz;
	conf_ptr->max_job_cnt         = conf->max_job_cnt;
	conf_ptr->max_job_id          = conf->max_job_id;
	conf_ptr->max_mem_per_cpu     = conf->max_mem_per_cpu;
	conf_ptr->max_step_cnt        = conf->max_step_cnt;
	conf_ptr->max_tasks_per_node  = conf->max_tasks_per_node;
	conf_ptr->mcs_plugin          = xstrdup(conf->mcs_plugin);
	conf_ptr->mcs_plugin_params   = xstrdup(conf->mcs_plugin_params);
	conf_ptr->min_job_age         = conf->min_job_age;
	conf_ptr->mpi_default         = xstrdup(conf->mpi_default);
	conf_ptr->mpi_params          = xstrdup(conf->mpi_params);
	conf_ptr->msg_aggr_params     = xstrdup(conf->msg_aggr_params);
	conf_ptr->msg_timeout         = conf->msg_timeout;

	conf_ptr->next_job_id         = next_job_id;
	conf_ptr->node_features_conf  = node_features_g_get_config();
	conf_ptr->node_features_plugins = xstrdup(conf->node_features_plugins);
	conf_ptr->node_prefix         = xstrdup(conf->node_prefix);

	conf_ptr->over_time_limit     = conf->over_time_limit;

	conf_ptr->plugindir           = xstrdup(conf->plugindir);
	conf_ptr->plugstack           = xstrdup(conf->plugstack);
	conf_ptr->power_parameters    = xstrdup(conf->power_parameters);
	conf_ptr->power_plugin        = xstrdup(conf->power_plugin);

	conf_ptr->preempt_mode        = conf->preempt_mode;
	conf_ptr->preempt_type        = xstrdup(conf->preempt_type);
	conf_ptr->preempt_exempt_time = conf->preempt_exempt_time;
	conf_ptr->priority_decay_hl   = conf->priority_decay_hl;
	conf_ptr->priority_calc_period = conf->priority_calc_period;
	conf_ptr->priority_favor_small= conf->priority_favor_small;
	conf_ptr->priority_flags      = conf->priority_flags;
	conf_ptr->priority_max_age    = conf->priority_max_age;
	conf_ptr->priority_params     = xstrdup(conf->priority_params);
	conf_ptr->priority_reset_period = conf->priority_reset_period;
	conf_ptr->priority_type       = xstrdup(conf->priority_type);
	conf_ptr->priority_weight_age = conf->priority_weight_age;
	conf_ptr->priority_weight_assoc = conf->priority_weight_assoc;
	conf_ptr->priority_weight_fs  = conf->priority_weight_fs;
	conf_ptr->priority_weight_js  = conf->priority_weight_js;
	conf_ptr->priority_weight_part= conf->priority_weight_part;
	conf_ptr->priority_weight_qos = conf->priority_weight_qos;
	conf_ptr->priority_weight_tres = xstrdup(conf->priority_weight_tres);

	conf_ptr->private_data        = conf->private_data;
	conf_ptr->proctrack_type      = xstrdup(conf->proctrack_type);
	conf_ptr->prolog              = xstrdup(conf->prolog);
	conf_ptr->prolog_epilog_timeout = conf->prolog_epilog_timeout;
	conf_ptr->prolog_slurmctld    = xstrdup(conf->prolog_slurmctld);
	conf_ptr->prolog_flags        = conf->prolog_flags;
	conf_ptr->propagate_prio_process =
		slurmctld_conf.propagate_prio_process;
	conf_ptr->propagate_rlimits   = xstrdup(conf->propagate_rlimits);
	conf_ptr->propagate_rlimits_except = xstrdup(conf->
						     propagate_rlimits_except);

	conf_ptr->reboot_program      = xstrdup(conf->reboot_program);
	conf_ptr->reconfig_flags      = conf->reconfig_flags;
	conf_ptr->requeue_exit        = xstrdup(conf->requeue_exit);
	conf_ptr->requeue_exit_hold   = xstrdup(conf->requeue_exit_hold);
	conf_ptr->resume_fail_program = xstrdup(conf->resume_fail_program);
	conf_ptr->resume_program      = xstrdup(conf->resume_program);
	conf_ptr->resume_rate         = conf->resume_rate;
	conf_ptr->resume_timeout      = conf->resume_timeout;
	conf_ptr->resv_epilog         = xstrdup(conf->resv_epilog);
	conf_ptr->resv_over_run       = conf->resv_over_run;
	conf_ptr->resv_prolog         = xstrdup(conf->resv_prolog);
	conf_ptr->ret2service         = conf->ret2service;
	conf_ptr->route_plugin        = xstrdup(conf->route_plugin);

	conf_ptr->salloc_default_command =xstrdup(conf->salloc_default_command);
	conf_ptr->sbcast_parameters   = xstrdup(conf->sbcast_parameters);

	conf_ptr->sched_params        = xstrdup(conf->sched_params);
	conf_ptr->sched_logfile       = xstrdup(conf->sched_logfile);
	conf_ptr->sched_log_level     = conf->sched_log_level;
	conf_ptr->sched_time_slice    = conf->sched_time_slice;
	conf_ptr->schedtype           = xstrdup(conf->schedtype);
	conf_ptr->select_type         = xstrdup(conf->select_type);
	select_g_get_info_from_plugin(SELECT_CONFIG_INFO, NULL,
				      &conf_ptr->select_conf_key_pairs);
	conf_ptr->select_type_param   = conf->select_type_param;
	conf_ptr->site_factor_params  = xstrdup(conf->site_factor_params);
	conf_ptr->site_factor_plugin  = xstrdup(conf->site_factor_plugin);
	conf_ptr->slurm_user_id       = conf->slurm_user_id;
	conf_ptr->slurm_user_name     = xstrdup(conf->slurm_user_name);
	conf_ptr->slurmctld_addr      = xstrdup(conf->slurmctld_addr);
	conf_ptr->slurmctld_debug     = conf->slurmctld_debug;
	conf_ptr->slurmctld_logfile   = xstrdup(conf->slurmctld_logfile);
	conf_ptr->slurmctld_params    = xstrdup(conf->slurmctld_params);
	conf_ptr->slurmctld_pidfile   = xstrdup(conf->slurmctld_pidfile);
	conf_ptr->slurmctld_plugstack = xstrdup(conf->slurmctld_plugstack);
	conf_ptr->slurmctld_plugstack_conf = slurmctld_plugstack_g_get_config();
	conf_ptr->slurmctld_port      = conf->slurmctld_port;
	conf_ptr->slurmctld_port_count = conf->slurmctld_port_count;
	conf_ptr->slurmctld_primary_off_prog  =
		xstrdup(conf->slurmctld_primary_off_prog);
	conf_ptr->slurmctld_primary_on_prog  =
		xstrdup(conf->slurmctld_primary_on_prog);
	conf_ptr->slurmctld_syslog_debug = conf->slurmctld_syslog_debug;
	conf_ptr->slurmctld_timeout   = conf->slurmctld_timeout;
	conf_ptr->slurmctld_params    = xstrdup(conf->slurmctld_params);
	conf_ptr->slurmd_debug        = conf->slurmd_debug;
	conf_ptr->slurmd_logfile      = xstrdup(conf->slurmd_logfile);
	conf_ptr->slurmd_params	      = xstrdup(conf->slurmd_params);
	conf_ptr->slurmd_pidfile      = xstrdup(conf->slurmd_pidfile);
	conf_ptr->slurmd_port         = conf->slurmd_port;
	conf_ptr->slurmd_spooldir     = xstrdup(conf->slurmd_spooldir);
	conf_ptr->slurmd_syslog_debug = conf->slurmd_syslog_debug;
	conf_ptr->slurmd_timeout      = conf->slurmd_timeout;
	conf_ptr->slurmd_user_id      = conf->slurmd_user_id;
	conf_ptr->slurmd_user_name    = xstrdup(conf->slurmd_user_name);
	conf_ptr->slurm_conf          = xstrdup(conf->slurm_conf);
	conf_ptr->srun_epilog         = xstrdup(conf->srun_epilog);

	conf_ptr->srun_port_range = xmalloc(2 * sizeof(uint16_t));
	if (conf->srun_port_range) {
		conf_ptr->srun_port_range[0] = conf->srun_port_range[0];
		conf_ptr->srun_port_range[1] = conf->srun_port_range[1];
	} else {
		conf_ptr->srun_port_range[0] = 0;
		conf_ptr->srun_port_range[1] = 0;
	}

	conf_ptr->srun_prolog         = xstrdup(conf->srun_prolog);
	conf_ptr->state_save_location = xstrdup(conf->state_save_location);
	conf_ptr->suspend_exc_nodes   = xstrdup(conf->suspend_exc_nodes);
	conf_ptr->suspend_exc_parts   = xstrdup(conf->suspend_exc_parts);
	conf_ptr->suspend_program     = xstrdup(conf->suspend_program);
	conf_ptr->suspend_rate        = conf->suspend_rate;
	conf_ptr->suspend_time        = conf->suspend_time;
	conf_ptr->suspend_timeout     = conf->suspend_timeout;
	conf_ptr->switch_type         = xstrdup(conf->switch_type);

	conf_ptr->task_epilog         = xstrdup(conf->task_epilog);
	conf_ptr->task_prolog         = xstrdup(conf->task_prolog);
	conf_ptr->task_plugin         = xstrdup(conf->task_plugin);
	conf_ptr->task_plugin_param   = conf->task_plugin_param;
	conf_ptr->tcp_timeout         = conf->tcp_timeout;
	conf_ptr->tmp_fs              = xstrdup(conf->tmp_fs);
	conf_ptr->topology_param      = xstrdup(conf->topology_param);
	conf_ptr->topology_plugin     = xstrdup(conf->topology_plugin);
	conf_ptr->track_wckey         = conf->track_wckey;
	conf_ptr->tree_width          = conf->tree_width;

	conf_ptr->wait_time           = conf->wait_time;

	conf_ptr->use_pam             = conf->use_pam;
	conf_ptr->use_spec_resources  = conf->use_spec_resources;
	conf_ptr->unkillable_program  = xstrdup(conf->unkillable_program);
	conf_ptr->unkillable_timeout  = conf->unkillable_timeout;
	conf_ptr->version             = xstrdup(SLURM_VERSION_STRING);
	conf_ptr->vsize_factor        = conf->vsize_factor;
	conf_ptr->x11_params          = xstrdup(conf->x11_params);
}

/*
 * validate_slurm_user - validate that the uid is authorized to see
 *      privileged data (either user root or SlurmUser)
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_slurm_user(uid_t uid)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurmctld_conf.slurm_user_id))
		return true;
	else
		return false;
}

/*
 * validate_super_user - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_SUPER_USER level
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_super_user(uid_t uid)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurmctld_conf.slurm_user_id) ||
	    assoc_mgr_get_admin_level(acct_db_conn, uid) >=
	    SLURMDB_ADMIN_SUPER_USER)
		return true;
	else
		return false;
}

/*
 * validate_operator - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_OPERATOR level
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_operator(uid_t uid)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurmctld_conf.slurm_user_id) ||
	    assoc_mgr_get_admin_level(acct_db_conn, uid) >=
	    SLURMDB_ADMIN_OPERATOR)
		return true;
	else
		return false;
}

static int _valid_id(char *caller, job_desc_msg_t *msg, uid_t uid, gid_t gid)
{
	if (validate_slurm_user(uid))
		return SLURM_SUCCESS;

	if (uid != msg->user_id) {
		error("%s: Requested UID=%u doesn't match user UID=%u.",
		      caller, msg->user_id, uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (gid != msg->group_id) {
		error("%s: Requested GID=%u doesn't match user GID=%u.",
		      caller, msg->group_id, gid);
		return ESLURM_GROUP_ID_MISSING;
	}

	return SLURM_SUCCESS;
}

/* _kill_job_on_msg_fail - The request to create a job record successed,
 *	but the reply message to srun failed. We kill the job to avoid
 *	leaving it orphaned */
static void _kill_job_on_msg_fail(uint32_t job_id)
{
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };

	error("Job allocate response msg send failure, killing JobId=%u",
	      job_id);
	lock_slurmctld(job_write_lock);
	job_complete(job_id, slurmctld_conf.slurm_user_id, false, false,
		     SIGTERM);
	unlock_slurmctld(job_write_lock);
}

/* create a credential for a given job step, return error code */
static int _make_step_cred(struct step_record *step_ptr,
			   slurm_cred_t **slurm_cred, uint16_t protocol_version)
{
	slurm_cred_arg_t cred_arg;
	struct job_record* job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;

	xassert(job_resrcs_ptr && job_resrcs_ptr->cpus);

	memset(&cred_arg, 0, sizeof(cred_arg));

	cred_arg.jobid    = job_ptr->job_id;
	cred_arg.stepid   = step_ptr->step_id;
	cred_arg.uid      = job_ptr->user_id;
	cred_arg.gid      = job_ptr->group_id;
	cred_arg.x11             = job_ptr->details->x11;
	cred_arg.job_constraints = job_ptr->details->features;
	cred_arg.job_core_bitmap = job_resrcs_ptr->core_bitmap;
	cred_arg.job_core_spec   = job_ptr->details->core_spec;
	cred_arg.job_hostlist    = job_resrcs_ptr->nodes;
	cred_arg.job_mem_limit   = job_ptr->details->pn_min_memory;
	cred_arg.job_nhosts      = job_resrcs_ptr->nhosts;
	cred_arg.job_gres_list   = job_ptr->gres_list;
	cred_arg.step_gres_list  = step_ptr->gres_list;

	cred_arg.step_core_bitmap = step_ptr->core_bitmap_job;
#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	cred_arg.step_hostlist   = job_ptr->batch_host;
#else
	cred_arg.step_hostlist   = step_ptr->step_layout->node_list;
#endif
	if (step_ptr->pn_min_memory)
		cred_arg.step_mem_limit  = step_ptr->pn_min_memory;

	cred_arg.cores_per_socket    = job_resrcs_ptr->cores_per_socket;
	cred_arg.sockets_per_node    = job_resrcs_ptr->sockets_per_node;
	cred_arg.sock_core_rep_count = job_resrcs_ptr->sock_core_rep_count;

	*slurm_cred = slurm_cred_create(slurmctld_config.cred_ctx, &cred_arg,
					protocol_version);

	if (*slurm_cred == NULL) {
		error("slurm_cred_create error");
		return ESLURM_INVALID_JOB_CREDENTIAL;
	}

	return SLURM_SUCCESS;
}

static int _pack_job_cancel(void *x, void *arg)
{
	struct job_record *job_ptr = (struct job_record *) x;
	time_t now = time(NULL);

	info("Cancelling aborted pack job submit: %pJ", job_ptr);
	job_ptr->job_state	= JOB_CANCELLED;
	job_ptr->start_time	= now;
	job_ptr->end_time	= now;
	job_ptr->exit_code	= 1;
	job_completion_logger(job_ptr, false);
	fed_mgr_job_complete(job_ptr, 0, now);

	return 0;
}

/*
 * build_alloc_msg - Fill in resource_allocation_response_msg_t off job_record.
 * job_ptr IN - job_record to copy members off.
 * error_code IN - error code used for the response.
 * job_submit_user_msg IN - user message from job submit plugin.
 * RET resource_allocation_response_msg_t filled in.
 */
extern resource_allocation_response_msg_t *build_alloc_msg(
	struct job_record *job_ptr, int error_code, char *job_submit_user_msg)
{
	int i;
	resource_allocation_response_msg_t *alloc_msg =
		xmalloc(sizeof(resource_allocation_response_msg_t));

	/* send job_ID and node_name_ptr */
	if (job_ptr->job_resrcs && job_ptr->job_resrcs->cpu_array_cnt) {
		alloc_msg->num_cpu_groups = job_ptr->job_resrcs->cpu_array_cnt;
		alloc_msg->cpu_count_reps = xmalloc(sizeof(uint32_t) *
						    job_ptr->job_resrcs->
						    cpu_array_cnt);
		memcpy(alloc_msg->cpu_count_reps,
		       job_ptr->job_resrcs->cpu_array_reps,
		       (sizeof(uint32_t) * job_ptr->job_resrcs->cpu_array_cnt));
		alloc_msg->cpus_per_node  = xmalloc(sizeof(uint16_t) *
						    job_ptr->job_resrcs->
						    cpu_array_cnt);
		memcpy(alloc_msg->cpus_per_node,
		       job_ptr->job_resrcs->cpu_array_value,
		       (sizeof(uint16_t) * job_ptr->job_resrcs->cpu_array_cnt));
	}

	alloc_msg->error_code     = error_code;
	alloc_msg->job_submit_user_msg = xstrdup(job_submit_user_msg);
	alloc_msg->job_id         = job_ptr->job_id;
	alloc_msg->node_cnt       = job_ptr->node_cnt;
	alloc_msg->node_list      = xstrdup(job_ptr->nodes);
	alloc_msg->partition      = xstrdup(job_ptr->partition);
	alloc_msg->alias_list     = xstrdup(job_ptr->alias_list);
	alloc_msg->select_jobinfo =
		select_g_select_jobinfo_copy(job_ptr->select_jobinfo);
	if (job_ptr->details) {
		if (job_ptr->bit_flags & JOB_MEM_SET) {
			alloc_msg->pn_min_memory =
				job_ptr->details->pn_min_memory;
		}
		alloc_msg->cpu_freq_min = job_ptr->details->cpu_freq_min;
		alloc_msg->cpu_freq_max = job_ptr->details->cpu_freq_max;
		alloc_msg->cpu_freq_gov = job_ptr->details->cpu_freq_gov;
		if (job_ptr->details->mc_ptr) {
			alloc_msg->ntasks_per_board =
				job_ptr->details->mc_ptr->ntasks_per_board;
			alloc_msg->ntasks_per_core =
				job_ptr->details->mc_ptr->ntasks_per_core;
			alloc_msg->ntasks_per_socket =
				job_ptr->details->mc_ptr->ntasks_per_socket;
		}

		if (job_ptr->details->env_cnt) {
			alloc_msg->env_size = job_ptr->details->env_cnt;
			alloc_msg->environment =
				xmalloc(sizeof(char *) * alloc_msg->env_size);
			for (i = 0; i < alloc_msg->env_size; i++) {
				alloc_msg->environment[i] =
					xstrdup(job_ptr->details->env_sup[i]);
			}
		}
	} else {
		/* alloc_msg->pn_min_memory = 0; */
		alloc_msg->ntasks_per_board  = NO_VAL16;
		alloc_msg->ntasks_per_core   = NO_VAL16;
		alloc_msg->ntasks_per_socket = NO_VAL16;
	}
	if (job_ptr->account)
		alloc_msg->account = xstrdup(job_ptr->account);
	if (job_ptr->qos_ptr) {
		slurmdb_qos_rec_t *qos;
		qos = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;
		alloc_msg->qos = xstrdup(qos->name);
	}
	if (job_ptr->resv_name)
		alloc_msg->resv_name = xstrdup(job_ptr->resv_name);

	set_remote_working_response(alloc_msg, job_ptr,
				    job_ptr->origin_cluster);

	return alloc_msg;
}

static void _del_alloc_pack_msg(void *x)
{
	resource_allocation_response_msg_t *alloc_msg;

	alloc_msg = (resource_allocation_response_msg_t *) x;
	/* NULL out working_cluster_rec since it's pointing to global memory */
	alloc_msg->working_cluster_rec = NULL;
	slurm_free_resource_allocation_response_msg(alloc_msg);
}

static bool _sched_backfill(void)
{
	static int backfill = -1;

	if (backfill == -1) {
		char *sched_type = slurm_get_sched_type();
		if (!xstrcmp(sched_type, "sched/backfill"))
			backfill = 1;
		else
			backfill = 0;
		xfree(sched_type);
	}

	if (backfill)
		return true;
	return false;
}

/*
 * If any job component has required nodes, those nodes must be excluded
 * from all other components to avoid scheduling deadlock
*/
static void _exclude_pack_nodes(List job_req_list)
{
	job_desc_msg_t *job_desc_msg;
	ListIterator iter;
	int pack_cnt, req_cnt = 0, i;
	char **req_nodes, *sep;

	pack_cnt = list_count(job_req_list);
	req_nodes = xmalloc(sizeof(char *) * pack_cnt);
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = (job_desc_msg_t *) list_next(iter))) {
		if (!job_desc_msg->req_nodes || !job_desc_msg->req_nodes[0])
			continue;
		req_nodes[req_cnt++] = job_desc_msg->req_nodes;
	}
	if (req_cnt) {
		list_iterator_reset(iter);
		while ((job_desc_msg = (job_desc_msg_t *) list_next(iter))) {
			for (i = 0; i < req_cnt; i++) {
				if (req_nodes[i] == job_desc_msg->req_nodes)
					continue;     /* required by this job */
				if (job_desc_msg->exc_nodes &&
				    job_desc_msg->exc_nodes[0])
					sep = ",";
				else
					sep = "";
				xstrfmtcat(job_desc_msg->exc_nodes, "%s%s",
					   sep, req_nodes[i]);
			}
		}
	}
	list_iterator_destroy(iter);
	xfree(req_nodes);
}

/*
 * _create_pack_job_id_set - Obtain the pack_job_id_set
 * pack_job_id_set OUT - allocated in the function and must be xfreed
 *                       be the caller.
 */
static void _create_pack_job_id_set(hostset_t jobid_hostset,
				    uint32_t pack_job_offset,
				    char **pack_job_id_set)
{
	int buf_size = pack_job_offset * 16;
	char *tmp_str = xmalloc(buf_size);
	char *tmp_offset = tmp_str;

	if (!jobid_hostset)
		return;

	hostset_ranged_string(jobid_hostset, buf_size, tmp_str);
	if (tmp_str[0] == '[') {
		tmp_offset = strchr(tmp_str, ']');
		if (tmp_offset)
			tmp_offset[0] = '\0';
		tmp_offset = tmp_str + 1;
	}

	*pack_job_id_set = xstrdup(tmp_offset);
	xfree(tmp_str);
}

/* _slurm_rpc_allocate_pack: process RPC to allocate a pack job resources */
static void _slurm_rpc_allocate_pack(slurm_msg_t * msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS, inx, pack_cnt = -1;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg;
	List job_req_list = (List) msg->data;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	gid_t gid = g_slurm_auth_get_gid(msg->auth_cred);
	char *hostname = g_slurm_auth_get_host(msg->auth_cred);
	uint32_t job_uid = NO_VAL;
	struct job_record *job_ptr, *first_job_ptr = NULL;
	char *err_msg = NULL, **job_submit_user_msg = NULL;
	ListIterator iter;
	List submit_job_list = NULL;
	uint32_t pack_job_id = 0, pack_job_offset = 0;
	hostset_t jobid_hostset = NULL;
	char tmp_str[32];
	List resp = NULL;
	slurm_addr_t resp_addr;
	char resp_host[16];
	uint16_t port;	/* dummy value */
	char *pack_job_id_set = NULL;

	START_TIMER;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		goto send_msg;
	}
	if (!_sched_backfill()) {
		error_code = ESLURM_NOT_SUPPORTED;
		goto send_msg;
	}
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_JOB_PACK_ALLOCATION from uid=%d with empty job list",
		     uid);
		error_code = SLURM_ERROR;
		goto send_msg;
	}
	if (slurm_get_peer_addr(msg->conn_fd, &resp_addr) == 0) {
		slurm_get_ip_str(&resp_addr, &port,resp_host,sizeof(resp_host));
	} else {
		info("REQUEST_JOB_PACK_ALLOCATION from uid=%d , can't get peer addr",
		     uid);
		error_code = SLURM_ERROR;
		goto send_msg;
	}

	sched_debug2("Processing RPC: REQUEST_JOB_PACK_ALLOCATION from uid=%d",
		     uid);

	/*
	 * If any job component has required nodes, those nodes must be excluded
	 * from all other components to avoid scheduling deadlock
	 */
	_exclude_pack_nodes(job_req_list);

	pack_cnt = list_count(job_req_list);
	job_submit_user_msg = xmalloc(sizeof(char *) * pack_cnt);
	submit_job_list = list_create(NULL);
	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	inx = 0;
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = (job_desc_msg_t *) list_next(iter))) {
		if (job_uid == NO_VAL)
			job_uid = job_desc_msg->user_id;

		if ((error_code = _valid_id("REQUEST_JOB_PACK_ALLOCATION",
					    job_desc_msg, uid, gid))) {
			break;
		}

		/*
		 * Old versions of Slurm don't work with het jobs and Cray
		 * This can be removed 2 versions after 19.05.
		 */
		if ((msg->protocol_version < SLURM_19_05_PROTOCOL_VERSION) &&
		    ((job_desc_msg->select_jobinfo->plugin_id ==
		      SELECT_PLUGIN_CRAY_LINEAR) ||
		     (job_desc_msg->select_jobinfo->plugin_id ==
		      SELECT_PLUGIN_CRAY_CONS_RES) ||
		     (job_desc_msg->select_jobinfo->plugin_id ==
		      SELECT_PLUGIN_CRAY_CONS_TRES))) {
			err_msg = xstrdup("Het jobs from old Slurm versions are not possible");
			error_code = SLURM_PROTOCOL_VERSION_ERROR;
			break;
		}

		/* use the credential to validate where we came from */
		if (hostname) {
			xfree(job_desc_msg->alloc_node);
			job_desc_msg->alloc_node = xstrdup(hostname);
		}

		if ((job_desc_msg->alloc_node == NULL) ||
		    (job_desc_msg->alloc_node[0] == '\0')) {
			error_code = ESLURM_INVALID_NODE_NAME;
			error("REQUEST_JOB_PACK_ALLOCATION lacks alloc_node from uid=%d",
			      uid);
			break;
		}

		if (job_desc_msg->array_inx) {
			error_code = ESLURM_INVALID_ARRAY;
			break;
		}

		if (job_desc_msg->immediate) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			break;
		}

		/* Locks are for job_submit plugin use */
		job_desc_msg->pack_job_offset = pack_job_offset;
		error_code = validate_job_create_req(job_desc_msg, uid,
						     &job_submit_user_msg[inx++]);
		if (error_code)
			break;

		dump_job_desc(job_desc_msg);

		job_ptr = NULL;
		if (!job_desc_msg->resp_host)
			job_desc_msg->resp_host = xstrdup(resp_host);
		if (pack_job_offset) {
			/* Email notifications disable except for pack leader */
			job_desc_msg->mail_type = 0;
			xfree(job_desc_msg->mail_user);
		}
		job_desc_msg->pack_job_offset = pack_job_offset;
		error_code = job_allocate(job_desc_msg, false, false, NULL,
					  true, uid, &job_ptr, &err_msg,
					  msg->protocol_version);
		if (!job_ptr) {
			if (error_code == SLURM_SUCCESS)
				error_code = SLURM_ERROR;
			break;
		}
		if (error_code && (job_ptr->job_state == JOB_FAILED))
			break;
		error_code = SLURM_SUCCESS;	/* Non-fatal error */
		if (pack_job_id == 0) {
			pack_job_id = job_ptr->job_id;
			first_job_ptr = job_ptr;
		}
		snprintf(tmp_str, sizeof(tmp_str), "%u", job_ptr->job_id);
		if (jobid_hostset)
			hostset_insert(jobid_hostset, tmp_str);
		else
			jobid_hostset = hostset_create(tmp_str);
		job_ptr->pack_job_id     = pack_job_id;
		job_ptr->pack_job_offset = pack_job_offset++;
		list_append(submit_job_list, job_ptr);
	}
	list_iterator_destroy(iter);
	xfree(hostname);

	if ((error_code == 0) && (!first_job_ptr)) {
		error("%s: No error, but no pack_job_id", __func__);
		error_code = SLURM_ERROR;
	}

	/* Validate limits on pack-job as a whole */
	if ((error_code == SLURM_SUCCESS) &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    !acct_policy_validate_pack(submit_job_list)) {
		info("Pack job %u exceeded association/QOS limit for user %u",
		     pack_job_id, job_uid);
		error_code = ESLURM_ACCOUNTING_POLICY;
        }

	/* Set the pack_job_id_set */
	_create_pack_job_id_set(jobid_hostset, pack_job_offset,
				&pack_job_id_set);

	if (first_job_ptr)
		first_job_ptr->pack_job_list = submit_job_list;
	iter = list_iterator_create(submit_job_list);
	while ((job_ptr = (struct job_record *) list_next(iter))) {
		job_ptr->pack_job_id_set = xstrdup(pack_job_id_set);
	}
	list_iterator_destroy(iter);
	xfree(pack_job_id_set);

	if (error_code) {
		/* Cancel remaining job records */
		(void) list_for_each(submit_job_list, _pack_job_cancel, NULL);
		if (!first_job_ptr)
			FREE_NULL_LIST(submit_job_list);
	} else {
		ListIterator iter;
		inx = 0;
		iter = list_iterator_create(submit_job_list);
		while ((job_ptr = (struct job_record *) list_next(iter))) {
			if (!resp)
				resp = list_create(_del_alloc_pack_msg);
			list_append(resp,
				    build_alloc_msg(
					    job_ptr, error_code,
					    job_submit_user_msg[inx++]));
			if (slurmctld_conf.debug_flags &
			    DEBUG_FLAG_HETERO_JOBS) {
				info("Submit %pJ", job_ptr);
			}
		}
		list_iterator_destroy(iter);
	}
	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);
	END_TIMER2("_slurm_rpc_allocate_pack");

	if (resp) {
		slurm_msg_t response_msg;
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_JOB_PACK_ALLOCATION;
		response_msg.data = resp;

		if (slurm_send_node_msg(msg->conn_fd, &response_msg) < 0)
			_kill_job_on_msg_fail(pack_job_id);
		list_destroy(resp);
	} else {
		char *aggregate_user_msg;

send_msg:	info("%s: %s ", __func__, slurm_strerror(error_code));
		aggregate_user_msg = NULL;

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		for (inx = 0; inx < pack_cnt; inx++) {
			char *line = NULL, *last = NULL;

			if (!job_submit_user_msg[inx])
				continue;

			/*
			 * Break apart any combined sentences and tag with index
			 */
			line = strtok_r(job_submit_user_msg[inx], "\n", &last);
			while (line) {
				xstrfmtcat(aggregate_user_msg, "%s%d: %s",
					   (aggregate_user_msg ? "\n" : ""),
					    inx, line);

				line = strtok_r(NULL, "\n", &last);
			}
		}
		if (aggregate_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = aggregate_user_msg;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	}
	xfree(err_msg);
	for (inx = 0; inx < pack_cnt; inx++)
		xfree(job_submit_user_msg[inx]);
	xfree(job_submit_user_msg);

	if (jobid_hostset)
		hostset_destroy(jobid_hostset);

	schedule_job_save();	/* has own locks */
}

/* _slurm_rpc_allocate_resources:  process RPC to allocate resources for
 *	a job
 */
static void _slurm_rpc_allocate_resources(slurm_msg_t * msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	resource_allocation_response_msg_t *alloc_msg = NULL;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	gid_t gid = g_slurm_auth_get_gid(msg->auth_cred);
	char *hostname = g_slurm_auth_get_host(msg->auth_cred);
	int immediate = job_desc_msg->immediate;
	bool do_unlock = false;
	bool reject_job = false;
	struct job_record *job_ptr = NULL;
	uint16_t port;	/* dummy value */
	slurm_addr_t resp_addr;
	char *err_msg = NULL, *job_submit_user_msg = NULL;

	START_TIMER;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}

	if ((error_code = _valid_id("REQUEST_RESOURCE_ALLOCATION",
				    job_desc_msg, uid, gid))) {
		reject_job = true;
		goto send_msg;
	}

	sched_debug2("Processing RPC: REQUEST_RESOURCE_ALLOCATION from uid=%d",
		     uid);

	/* use the credential to validate where we came from */
	if (hostname) {
		xfree(job_desc_msg->alloc_node);
		job_desc_msg->alloc_node = hostname;
		hostname = NULL;
	}

	/* do RPC call */
	if ((job_desc_msg->alloc_node == NULL) ||
	    (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_RESOURCE_ALLOCATE lacks alloc_node from uid=%d",
		      uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		lock_slurmctld(job_read_lock);
		job_desc_msg->pack_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,uid,&err_msg);
		unlock_slurmctld(job_read_lock);
	}

	/*
	 * In validate_job_create_req(), err_msg is currently only modified in
	 * the call to job_submit_plugin_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to fed_mgr_job_allocate and/or job_allocate, and we
	 * need the job submit plugin value to build the resource allocation
	 * response in the call to build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	if (error_code) {
		reject_job = true;
	} else if (!slurm_get_peer_addr(msg->conn_fd, &resp_addr)) {
		/* resp_host could already be set from a federated cluster */
		if (!job_desc_msg->resp_host) {
			job_desc_msg->resp_host = xmalloc(16);
			slurm_get_ip_str(&resp_addr, &port,
					 job_desc_msg->resp_host, 16);
		}
		dump_job_desc(job_desc_msg);
		do_unlock = true;
		_throttle_start(&active_rpc_cnt);

		lock_slurmctld(job_write_lock);
		if (fed_mgr_fed_rec) {
			uint32_t job_id;
			if (fed_mgr_job_allocate(msg, job_desc_msg, true, uid,
						 msg->protocol_version, &job_id,
						 &error_code, &err_msg)) {
				reject_job = true;
			} else if (!(job_ptr = find_job_record(job_id))) {
				error("%s: can't find fed job that was just created. this should never happen",
				      __func__);
				reject_job = true;
				error_code = SLURM_ERROR;
			}
		} else {
			job_desc_msg->pack_job_offset = NO_VAL;
			error_code = job_allocate(job_desc_msg, immediate,
						  false, NULL, true, uid,
						  &job_ptr, &err_msg,
						  msg->protocol_version);
			/* unlock after finished using the job structure
			 * data */

			/* return result */
			if (!job_ptr ||
			    (error_code && job_ptr->job_state == JOB_FAILED))
				reject_job = true;
		}
		END_TIMER2("_slurm_rpc_allocate_resources");
	} else {
		reject_job = true;
		if (errno)
			error_code = errno;
		else
			error_code = SLURM_ERROR;
	}

send_msg:

	if (!reject_job) {
		xassert(job_ptr);
		sched_info("%s %pJ NodeList=%s %s",
			   __func__, job_ptr, job_ptr->nodes, TIME_STR);

		alloc_msg = build_alloc_msg(job_ptr, error_code,
					    job_submit_user_msg);

		/*
		 * This check really isn't needed, but just doing it
		 * to be more complete.
		 */
		if (do_unlock) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}

		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_RESOURCE_ALLOCATION;
		response_msg.data = alloc_msg;

		if (slurm_send_node_msg(msg->conn_fd, &response_msg) < 0)
			_kill_job_on_msg_fail(job_ptr->job_id);

		schedule_job_save();	/* has own locks */
		schedule_node_save();	/* has own locks */

		if (!alloc_msg->node_cnt) /* didn't get an allocation */
			queue_job_scheduler();

		/* NULL out working_cluster_rec since it's pointing to global
		 * memory */
		alloc_msg->working_cluster_rec = NULL;
		slurm_free_resource_allocation_response_msg(alloc_msg);
	} else {	/* allocate error */
		if (do_unlock) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		info("%s: %s ", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_dump_conf - process RPC for Slurm configuration information */
static void _slurm_rpc_dump_conf(slurm_msg_t * msg)
{
	DEF_TIMERS;
	slurm_msg_t response_msg;
	last_update_msg_t *last_time_msg = (last_update_msg_t *) msg->data;
	slurm_ctl_conf_info_msg_t config_tbl;
	/* Locks: Read config, job, partition, fed */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_BUILD_INFO from uid=%d",
	       uid);
	lock_slurmctld(config_read_lock);

	/* check to see if configuration data has changed */
	if ((last_time_msg->last_update - 1) >= slurmctld_conf.last_update) {
		unlock_slurmctld(config_read_lock);
		debug2("_slurm_rpc_dump_conf, no change");
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		_fill_ctld_conf(&config_tbl);
		unlock_slurmctld(config_read_lock);
		END_TIMER2("_slurm_rpc_dump_conf");

		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_BUILD_INFO;
		response_msg.data = &config_tbl;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		free_slurm_conf(&config_tbl, false);
	}
}

/* _slurm_rpc_dump_jobs - process RPC for job state information */
static void _slurm_rpc_dump_jobs(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	job_info_request_msg_t *job_info_request_msg =
		(job_info_request_msg_t *) msg->data;
	/* Locks: Read config job part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug3("Processing RPC: REQUEST_JOB_INFO from uid=%d", uid);
	lock_slurmctld(job_read_lock);

	if ((job_info_request_msg->last_update - 1) >= last_job_update) {
		unlock_slurmctld(job_read_lock);
		debug3("_slurm_rpc_dump_jobs, no change");
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		if (job_info_request_msg->job_ids) {
			pack_spec_jobs(&dump, &dump_size,
				       job_info_request_msg->job_ids,
				       job_info_request_msg->show_flags, uid,
				       NO_VAL, msg->protocol_version);
		} else {
			pack_all_jobs(&dump, &dump_size,
				      job_info_request_msg->show_flags, uid,
				      NO_VAL, msg->protocol_version);
		}
		unlock_slurmctld(job_read_lock);
		END_TIMER2("_slurm_rpc_dump_jobs");
#if 0
		info("_slurm_rpc_dump_jobs, size=%d %s", dump_size, TIME_STR);
#endif

		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_JOB_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_dump_jobs - process RPC for job state information */
static void _slurm_rpc_dump_jobs_user(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	job_user_id_msg_t *job_info_request_msg =
		(job_user_id_msg_t *) msg->data;
	/* Locks: Read config job part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug3("Processing RPC: REQUEST_JOB_USER_INFO from uid=%d", uid);
	lock_slurmctld(job_read_lock);
	pack_all_jobs(&dump, &dump_size, job_info_request_msg->show_flags, uid,
		      job_info_request_msg->user_id, msg->protocol_version);
	unlock_slurmctld(job_read_lock);
	END_TIMER2("_slurm_rpc_dump_job_user");
#if 0
	info("_slurm_rpc_dump_user_jobs, size=%d %s", dump_size, TIME_STR);
#endif

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_JOB_INFO;
	response_msg.data = dump;
	response_msg.data_size = dump_size;

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* _slurm_rpc_dump_job_single - process RPC for one job's state information */
static void _slurm_rpc_dump_job_single(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump = NULL;
	int dump_size, rc;
	slurm_msg_t response_msg;
	job_id_msg_t *job_id_msg = (job_id_msg_t *) msg->data;
	/* Locks: Read config, job, and node info */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug3("Processing RPC: REQUEST_JOB_INFO_SINGLE from uid=%d", uid);
	lock_slurmctld(job_read_lock);

	rc = pack_one_job(&dump, &dump_size, job_id_msg->job_id,
			  job_id_msg->show_flags, uid, msg->protocol_version);
	unlock_slurmctld(job_read_lock);
	END_TIMER2("_slurm_rpc_dump_job_single");
#if 0
	info("_slurm_rpc_dump_job_single, size=%d %s", dump_size, TIME_STR);
#endif

	/* init response_msg structure */
	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_JOB_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
	}
	xfree(dump);
}

static void  _slurm_rpc_get_shares(slurm_msg_t *msg)
{
	DEF_TIMERS;
	shares_request_msg_t *req_msg = (shares_request_msg_t *) msg->data;
	shares_response_msg_t resp_msg;
	slurm_msg_t response_msg;

	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_SHARE_INFO from uid=%d", uid);

	memset(&resp_msg, 0, sizeof(resp_msg));
	assoc_mgr_get_shares(acct_db_conn, uid, req_msg, &resp_msg);

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_SHARE_INFO;
	response_msg.data     = &resp_msg;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	FREE_NULL_LIST(resp_msg.assoc_shares_list);
	/* don't free the resp_msg.tres_names */
	END_TIMER2("_slurm_rpc_get_share");
	debug2("_slurm_rpc_get_shares %s", TIME_STR);
}

static void  _slurm_rpc_get_priority_factors(slurm_msg_t *msg)
{
	DEF_TIMERS;
	priority_factors_request_msg_t *req_msg =
		(priority_factors_request_msg_t *) msg->data;
	priority_factors_response_msg_t resp_msg;
	slurm_msg_t response_msg;

	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_PRIORITY_FACTORS from uid=%d", uid);
	resp_msg.priority_factors_list = priority_g_get_priority_factors_list(
		req_msg, uid);
	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_PRIORITY_FACTORS;
	response_msg.data     = &resp_msg;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	FREE_NULL_LIST(resp_msg.priority_factors_list);
	END_TIMER2("_slurm_rpc_get_priority_factors");
	debug2("_slurm_rpc_get_priority_factors %s", TIME_STR);
}

/* _slurm_rpc_end_time - Process RPC for job end time */
static void _slurm_rpc_end_time(slurm_msg_t * msg)
{
	DEF_TIMERS;
	job_alloc_info_msg_t *time_req_msg =
		(job_alloc_info_msg_t *) msg->data;
	srun_timeout_msg_t timeout_msg;
	slurm_msg_t response_msg;
	int rc;
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_JOB_END_TIME from uid=%d", uid);
	lock_slurmctld(job_read_lock);
	rc = job_end_time(time_req_msg, &timeout_msg);
	unlock_slurmctld(job_read_lock);
	END_TIMER2("_slurm_rpc_end_time");

	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		response_init(&response_msg, msg);
		response_msg.msg_type = SRUN_TIMEOUT;
		response_msg.data     = &timeout_msg;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
	}
	debug2("_slurm_rpc_end_time JobId=%u %s",
	       time_req_msg->job_id, TIME_STR);
}

/* _slurm_rpc_get_fd - process RPC for federation state information */
static void _slurm_rpc_get_fed(slurm_msg_t * msg)
{
	DEF_TIMERS;
	slurm_msg_t response_msg;
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_FED_INFO from uid=%d", uid);

	lock_slurmctld(fed_read_lock);

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_FED_INFO;
	response_msg.data = fed_mgr_fed_rec;

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	unlock_slurmctld(fed_read_lock);

	END_TIMER2("_slurm_rpc_get_fed");
	debug2("%s %s", __func__, TIME_STR);
}

/* _slurm_rpc_dump_front_end - process RPC for front_end state information */
static void _slurm_rpc_dump_front_end(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump = NULL;
	int dump_size = 0;
	slurm_msg_t response_msg;
	front_end_info_request_msg_t *front_end_req_msg =
		(front_end_info_request_msg_t *) msg->data;
	/* Locks: Read config, read node */
	slurmctld_lock_t node_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug3("Processing RPC: REQUEST_FRONT_END_INFO from uid=%d", uid);
	lock_slurmctld(node_read_lock);

	if ((front_end_req_msg->last_update - 1) >= last_front_end_update) {
		unlock_slurmctld(node_read_lock);
		debug3("_slurm_rpc_dump_front_end, no change");
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_front_end(&dump, &dump_size, uid,
				   msg->protocol_version);
		unlock_slurmctld(node_read_lock);
		END_TIMER2("_slurm_rpc_dump_front_end");
		debug2("_slurm_rpc_dump_front_end, size=%d %s",
		       dump_size, TIME_STR);

		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_FRONT_END_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_dump_nodes - dump RPC for node state information */
static void _slurm_rpc_dump_nodes(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	node_info_request_msg_t *node_req_msg =
		(node_info_request_msg_t *) msg->data;
	/* Locks: Read config, write node (reset allocated CPU count in some
	 * select plugins), read part (for part_is_visible) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug3("Processing RPC: REQUEST_NODE_INFO from uid=%d", uid);

	if ((slurmctld_conf.private_data & PRIVATE_DATA_NODES) &&
	    (!validate_operator(uid))) {
		error("Security violation, REQUEST_NODE_INFO RPC from uid=%d",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	lock_slurmctld(node_write_lock);

	select_g_select_nodeinfo_set_all();

	if ((node_req_msg->last_update - 1) >= last_node_update) {
		unlock_slurmctld(node_write_lock);
		debug3("_slurm_rpc_dump_nodes, no change");
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_node(&dump, &dump_size, node_req_msg->show_flags,
			      uid, msg->protocol_version);
		unlock_slurmctld(node_write_lock);
		END_TIMER2("_slurm_rpc_dump_nodes");
#if 0
		info("_slurm_rpc_dump_nodes, size=%d %s", dump_size, TIME_STR);
#endif

		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_NODE_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_dump_node_single - done RPC state information for one node */
static void _slurm_rpc_dump_node_single(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	node_info_single_msg_t *node_req_msg =
		(node_info_single_msg_t *) msg->data;
	/* Locks: Read config, read node, read part (for part_is_visible) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug3("Processing RPC: REQUEST_NODE_INFO_SINGLE from uid=%d", uid);

	if ((slurmctld_conf.private_data & PRIVATE_DATA_NODES) &&
	    (!validate_operator(uid))) {
		error("Security violation, REQUEST_NODE_INFO_SINGLE RPC from "
		      "uid=%d", uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	lock_slurmctld(node_write_lock);

#if 0
	/* This function updates each node's alloc_cpus count and too slow for
	 * our use here. Node write lock is needed if this function is used */
	select_g_select_nodeinfo_set_all();
#endif
	pack_one_node(&dump, &dump_size, node_req_msg->show_flags,
		      uid, node_req_msg->node_name, msg->protocol_version);
	unlock_slurmctld(node_write_lock);
	END_TIMER2("_slurm_rpc_dump_node_single");
#if 0
	info("_slurm_rpc_dump_node_single, name=%s size=%d %s",
	     node_req_msg->node_name, dump_size, TIME_STR);
#endif

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_NODE_INFO;
	response_msg.data = dump;
	response_msg.data_size = dump_size;

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* _slurm_rpc_dump_partitions - process RPC for partition state information */
static void _slurm_rpc_dump_partitions(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	part_info_request_msg_t  *part_req_msg;

	/* Locks: Read configuration and partition */
	slurmctld_lock_t part_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_PARTITION_INFO uid=%d", uid);
	part_req_msg = (part_info_request_msg_t  *) msg->data;
	lock_slurmctld(part_read_lock);

	if ((slurmctld_conf.private_data & PRIVATE_DATA_PARTITIONS) &&
	    !validate_operator(uid)) {
		unlock_slurmctld(part_read_lock);
		debug2("Security violation, PARTITION_INFO RPC from uid=%d",
		       uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
	} else if ((part_req_msg->last_update - 1) >= last_part_update) {
		unlock_slurmctld(part_read_lock);
		debug2("_slurm_rpc_dump_partitions, no change");
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_part(&dump, &dump_size, part_req_msg->show_flags,
			      uid, msg->protocol_version);
		unlock_slurmctld(part_read_lock);
		END_TIMER2("_slurm_rpc_dump_partitions");
		debug2("_slurm_rpc_dump_partitions, size=%d %s",
		       dump_size, TIME_STR);

		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_PARTITION_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_epilog_complete - process RPC noting the completion of
 * the epilog denoting the completion of a job it its entirety */
static void  _slurm_rpc_epilog_complete(slurm_msg_t *msg,
					bool *run_scheduler,
					bool running_composite)
{
	static int active_rpc_cnt = 0;
	static time_t config_update = 0;
	static bool defer_sched = false;
	DEF_TIMERS;
	/* Locks: Read configuration, write job, write node */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	epilog_complete_msg_t *epilog_msg =
		(epilog_complete_msg_t *) msg->data;
	struct job_record  *job_ptr;

	START_TIMER;
	debug2("Processing RPC: MESSAGE_EPILOG_COMPLETE uid=%d", uid);
	if (!validate_slurm_user(uid)) {
		error("Security violation, EPILOG_COMPLETE RPC from uid=%d",
		      uid);
		return;
	}

	/* Only throttle on none composite messages, the lock should
	 * already be set earlier. */
	if (!running_composite) {
		if (config_update != slurmctld_conf.last_update) {
			char *sched_params = slurm_get_sched_params();
			defer_sched = (xstrcasestr(sched_params, "defer"));
			xfree(sched_params);
			config_update = slurmctld_conf.last_update;
		}

		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_ROUTE)
		info("_slurm_rpc_epilog_complete: "
		     "node_name = %s, JobId=%u", epilog_msg->node_name,
		     epilog_msg->job_id);

	if (job_epilog_complete(epilog_msg->job_id, epilog_msg->node_name,
				epilog_msg->return_code))
		*run_scheduler = true;

	job_ptr = find_job_record(epilog_msg->job_id);

	if (epilog_msg->return_code)
		error("%s: epilog error %pJ Node=%s Err=%s %s",
		      __func__, job_ptr, epilog_msg->node_name,
		      slurm_strerror(epilog_msg->return_code), TIME_STR);
	else
		debug2("%s: %pJ Node=%s %s",
		       __func__, job_ptr, epilog_msg->node_name, TIME_STR);

	if (!running_composite) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

	END_TIMER2("_slurm_rpc_epilog_complete");

	/* Functions below provide their own locking */
	if (!running_composite && *run_scheduler) {
		/*
		 * In defer mode, avoid triggering the scheduler logic
		 * for every epilog complete message.
		 * As one epilog message is sent from every node of each
		 * job at termination, the number of simultaneous schedule
		 * calls can be very high for large machine or large number
		 * of managed jobs.
		 */
		if (!LOTS_OF_AGENTS && !defer_sched)
			(void) schedule(0);	/* Has own locking */
		schedule_node_save();		/* Has own locking */
		schedule_job_save();		/* Has own locking */
	}

	/* NOTE: RPC has no response */
}

/* _slurm_rpc_job_step_kill - process RPC to cancel an entire job or
 * an individual job step */
static void _slurm_rpc_job_step_kill(uint32_t uid, slurm_msg_t * msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	job_step_kill_msg_t *job_step_kill_msg =
		(job_step_kill_msg_t *) msg->data;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS) {
		info("Processing RPC: REQUEST_CANCEL_JOB_STEP JobId=%u StepId=%u uid=%u",
		     job_step_kill_msg->job_id, job_step_kill_msg->job_step_id,
		     uid);
	}
	_throttle_start(&active_rpc_cnt);

	error_code = kill_job_step(job_step_kill_msg, uid);

	_throttle_fini(&active_rpc_cnt);

	slurm_send_rc_msg(msg, error_code);
}

/* _slurm_rpc_complete_job_allocation - process RPC to note the
 *	completion of a job allocation */
static void _slurm_rpc_complete_job_allocation(slurm_msg_t * msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	complete_job_allocation_msg_t *comp_msg =
		(complete_job_allocation_msg_t *) msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	struct job_record *job_ptr;

	/* init */
	START_TIMER;
	debug2("Processing RPC: REQUEST_COMPLETE_JOB_ALLOCATION from "
	       "uid=%u, JobId=%u rc=%d",
	       uid, comp_msg->job_id, comp_msg->job_rc);

	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(comp_msg->job_id);
	trace_job(job_ptr, __func__, "enter");

	/* Mark job and/or job step complete */
	error_code = job_complete(comp_msg->job_id, uid,
				  false, false, comp_msg->job_rc);
	if (error_code) {
		info("%s: %pJ error %s",
		     __func__, job_ptr, slurm_strerror(error_code));
	} else {
		debug2("%s: %pJ %s", __func__, job_ptr, TIME_STR);
	}

	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);
	END_TIMER2("_slurm_rpc_complete_job_allocation");

	/* synchronize power layouts key/values */
	if ((powercap_get_cluster_current_cap() != 0) &&
	    (which_power_layout() == 2)) {
		layouts_entity_pull_kv("power", "Cluster", "CurrentSumPower");
	}

	/* return result */
	if (error_code) {
		slurm_send_rc_msg(msg, error_code);
	} else {
		slurmctld_diag_stats.jobs_completed++;
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		(void) schedule_job_save();	/* Has own locking */
		(void) schedule_node_save();	/* Has own locking */
	}

	trace_job(job_ptr, __func__, "return");
}

/* _slurm_rpc_complete_prolog - process RPC to note the
 *	completion of a prolog */
static void _slurm_rpc_complete_prolog(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	complete_prolog_msg_t *comp_msg =
		(complete_prolog_msg_t *) msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	/* init */
	START_TIMER;
	debug2("Processing RPC: REQUEST_COMPLETE_PROLOG from JobId=%u",
	       comp_msg->job_id);

	lock_slurmctld(job_write_lock);
	error_code = prolog_complete(comp_msg->job_id, comp_msg->prolog_rc);
	unlock_slurmctld(job_write_lock);

	END_TIMER2("_slurm_rpc_complete_prolog");

	/* return result */
	if (error_code) {
		info("_slurm_rpc_complete_prolog JobId=%u: %s ",
		     comp_msg->job_id, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_complete_prolog JobId=%u %s",
		       comp_msg->job_id, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

/* _slurm_rpc_complete_batch - process RPC from slurmstepd to note the
 *	completion of a batch script */
static void _slurm_rpc_complete_batch_script(slurm_msg_t *msg,
					     bool *run_scheduler,
					     bool running_composite)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS, i;
	DEF_TIMERS;
	complete_batch_script_msg_t *comp_msg =
		(complete_batch_script_msg_t *) msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	bool job_requeue = false;
	bool dump_job = false, dump_node = false;
	struct job_record *job_ptr = NULL;
	char *msg_title = "node(s)";
	char *nodes = comp_msg->node_name;

	/* init */
	START_TIMER;
	debug2("Processing RPC: REQUEST_COMPLETE_BATCH_SCRIPT from "
	       "uid=%u JobId=%u",
	       uid, comp_msg->job_id);

	if (!validate_slurm_user(uid)) {
		error("A non superuser %u tried to complete batch JobId=%u",
		      uid, comp_msg->job_id);
		/* Only the slurmstepd can complete a batch script */
		END_TIMER2("_slurm_rpc_complete_batch_script");
		return;
	}

	if (!running_composite) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	job_ptr = find_job_record(comp_msg->job_id);

	if (job_ptr && job_ptr->batch_host && comp_msg->node_name &&
	    xstrcmp(job_ptr->batch_host, comp_msg->node_name)) {
		/* This can be the result of the slurmd on the batch_host
		 * failing, but the slurmstepd continuing to run. Then the
		 * batch job is requeued and started on a different node.
		 * The end result is one batch complete RPC from each node. */
		error("Batch completion for JobId=%u sent from wrong node (%s rather than %s). Was the job requeued due to node failure?",
		      comp_msg->job_id,
		      comp_msg->node_name, job_ptr->batch_host);
		if (!running_composite) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	/*
	 * Send batch step info to accounting, only if the job is
	 * still completing.
	 *
	 * When a job is requeued because of node failure, and there is no
	 * epilog, both EPILOG_COMPLETE and COMPLETE_BATCH_SCRIPT_COMPLETE
	 * messages are sent at the same time and receieved on different
	 * threads. EPILOG_COMPLETE will grab a new db_index for the job. So if
	 * COMPLETE_BATCH_SCRIPT happens after EPILOG_COMPLETE, then adding the
	 * batch step would happen on the new db instance -- which is incorrect.
	 * Rather than try to ensure that COMPLETE_BATCH_SCRIPT happens after
	 * EPILOG_COMPLETE, just throw away the batch step for node failures.
	 *
	 * NOTE: Do not use IS_JOB_PENDING since that doesn't take
	 * into account the COMPLETING FLAG which is valid, but not
	 * always set yet when the step exits normally.
	 */
	if (association_based_accounting && job_ptr &&
	    (job_ptr->job_state != JOB_PENDING)) {
		/* This logic was taken from _slurm_rpc_step_complete() */
		struct step_record *step_ptr =
			find_step_record(job_ptr, SLURM_BATCH_SCRIPT);
		if (!step_ptr) {
			if (msg->protocol_version >=
			    SLURM_19_05_PROTOCOL_VERSION) {
				error("%s: Could not find batch step for %pJ, this should never happen",
				      __func__, job_ptr);
			} else {
				debug2("%s: Batch step complete from old version of Slurm recieved for job %pJ",
				       __func__, job_ptr);
			}
			step_ptr = build_batch_step(job_ptr);
		}

		if (step_ptr->step_id != SLURM_BATCH_SCRIPT) {
			error("%s: %pJ Didn't find batch step, found step %u. This should never happen.",
			      __func__, job_ptr, step_ptr->step_id);
		} else {
			step_ptr->exit_code = comp_msg->job_rc;
			jobacctinfo_destroy(step_ptr->jobacct);
			step_ptr->jobacct = comp_msg->jobacct;
			comp_msg->jobacct = NULL;
			step_ptr->state |= JOB_COMPLETING;
			jobacct_storage_g_step_complete(acct_db_conn, step_ptr);
			delete_step_record(job_ptr, step_ptr->step_id);
		}
	}

#ifdef HAVE_FRONT_END
	if (job_ptr && job_ptr->front_end_ptr)
		nodes = job_ptr->front_end_ptr->name;
	msg_title = "front_end";
#endif

	/* do RPC call */
	/* First set node DOWN if fatal error */
	if ((comp_msg->slurm_rc == ESLURMD_JOB_NOTRUNNING) ||
	    (comp_msg->slurm_rc == ESLURM_ALREADY_DONE) ||
	    (comp_msg->slurm_rc == ESLURMD_CREDENTIAL_REVOKED)) {
		/* race condition on job termination, not a real error */
		info("slurmd error running JobId=%u from %s=%s: %s",
		     comp_msg->job_id,
		     msg_title, nodes,
		     slurm_strerror(comp_msg->slurm_rc));
		comp_msg->slurm_rc = SLURM_SUCCESS;
	} else if ((comp_msg->slurm_rc == SLURM_COMMUNICATIONS_SEND_ERROR) ||
		   (comp_msg->slurm_rc == ESLURM_USER_ID_MISSING) ||
		   (comp_msg->slurm_rc == ESLURMD_UID_NOT_FOUND)  ||
		   (comp_msg->slurm_rc == ESLURMD_GID_NOT_FOUND)  ||
		   (comp_msg->slurm_rc == ESLURMD_INVALID_ACCT_FREQ)) {
		/* Handle non-fatal errors here. All others drain the node. */
		error("Slurmd error running JobId=%u on %s=%s: %s",
		      comp_msg->job_id, msg_title, nodes,
		      slurm_strerror(comp_msg->slurm_rc));
	} else if (comp_msg->slurm_rc != SLURM_SUCCESS) {
		error("slurmd error running JobId=%u on %s=%s: %s",
		      comp_msg->job_id,
		      msg_title, nodes,
		      slurm_strerror(comp_msg->slurm_rc));
		slurmctld_diag_stats.jobs_failed++;
		if (error_code == SLURM_SUCCESS) {
#ifdef HAVE_FRONT_END
			if (job_ptr && job_ptr->front_end_ptr) {
				update_front_end_msg_t update_node_msg;
				memset(&update_node_msg, 0,
				       sizeof(update_node_msg));
				update_node_msg.name = job_ptr->front_end_ptr->
						       name;
				update_node_msg.node_state = NODE_STATE_DRAIN;
				update_node_msg.reason =
					"batch job complete failure";
				error_code = update_front_end(&update_node_msg);
			}
#else
			error_code = drain_nodes(comp_msg->node_name,
						 "batch job complete failure",
						 slurmctld_conf.slurm_user_id);
#endif	/* !HAVE_FRONT_END */
			if ((comp_msg->job_rc != SLURM_SUCCESS) && job_ptr &&
			    job_ptr->details && job_ptr->details->requeue)
				job_requeue = true;
			dump_job = true;
			dump_node = true;
		}
	}

	/* Mark job allocation complete */
	if (msg->msg_type == REQUEST_COMPLETE_BATCH_JOB)
		job_epilog_complete(comp_msg->job_id, comp_msg->node_name, 0);
	i = job_complete(comp_msg->job_id, uid, job_requeue, false,
			 comp_msg->job_rc);
	error_code = MAX(error_code, i);
	if (!running_composite) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

	/* this has to be done after the job_complete */

	END_TIMER2("_slurm_rpc_complete_batch_script");

	/* synchronize power layouts key/values */
	if ((powercap_get_cluster_current_cap() != 0) &&
	    (which_power_layout() == 2)) {
		layouts_entity_pull_kv("power", "Cluster", "CurrentSumPower");
	}

	/* return result */
	if (error_code) {
		debug2("_slurm_rpc_complete_batch_script JobId=%u: %s ",
		       comp_msg->job_id,
		       slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_complete_batch_script JobId=%u %s",
		       comp_msg->job_id, TIME_STR);
		slurmctld_diag_stats.jobs_completed++;
		dump_job = true;
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

	/* If running composite lets not call this to avoid deadlock */
	if (!running_composite && *run_scheduler)
		(void) schedule(0);		/* Has own locking */
	if (dump_job)
		(void) schedule_job_save();	/* Has own locking */
	if (dump_node)
		(void) schedule_node_save();	/* Has own locking */
}

static void  _slurm_rpc_dump_batch_script(slurm_msg_t *msg)
{
	DEF_TIMERS;
	int rc = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	struct job_record *job_ptr;
	Buf script;
	job_id_msg_t *job_id_msg = (job_id_msg_t *) msg->data;
	/* Locks: Read config, job, and node info */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug3("Processing RPC: REQUEST_BATCH_SCRIPT from uid=%d for JobId=%u",
	       uid, job_id_msg->job_id);
	lock_slurmctld(job_read_lock);

	if ((job_ptr = find_job_record(job_id_msg->job_id))) {
		if (!validate_operator(uid) && job_ptr->user_id != uid) {
			rc = ESLURM_USER_ID_MISSING;
		} else {
			script = get_job_script(job_ptr);
			if (!script)
				rc = ESLURM_JOB_SCRIPT_MISSING;
		}
	} else {
		rc = ESLURM_INVALID_JOB_ID;
	}

	unlock_slurmctld(job_read_lock);
	END_TIMER2("_slurm_rpc_dump_batch_script");

	/* init response_msg structure */
	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_BATCH_SCRIPT;
		response_msg.data = script;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		free_buf(script);
	}
}

/* _slurm_rpc_job_step_create - process RPC to create/register a job step
 *	with the step_mgr */
static void _slurm_rpc_job_step_create(slurm_msg_t * msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	slurm_msg_t resp;
	struct step_record *step_rec;
	job_step_create_response_msg_t job_step_resp;
	job_step_create_request_msg_t *req_step_msg =
		(job_step_create_request_msg_t *) msg->data;
	slurm_cred_t *slurm_cred = (slurm_cred_t *) NULL;
	/* Locks: Write jobs, read nodes */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
		info("Processing RPC: REQUEST_JOB_STEP_CREATE from uid=%d",
				uid);

	dump_step_desc(req_step_msg);
	if (uid && (uid != req_step_msg->user_id)) {
		error("Security violation, JOB_STEP_CREATE RPC from uid=%d "
		      "to run as uid %u",
		      uid, req_step_msg->user_id);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

#if defined HAVE_FRONT_END
	/* Limited job step support */
	/* Non-super users not permitted to run job steps on front-end.
	 * A single slurmd can not handle a heavy load. */
	if (!validate_slurm_user(uid)) {
		info("Attempt to execute job step by uid=%d", uid);
		slurm_send_rc_msg(msg, ESLURM_NO_STEPS);
		return;
	}
#endif

#if defined HAVE_NATIVE_CRAY
	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	if (LOTS_OF_AGENTS || (slurmctld_config.server_thread_count >= 128)) {
		/*
		 * Don't start more steps if system is very busy right now.
		 * Getting cray network switch cookie is slow and happens
		 * with job write lock set.
		 */
		slurm_send_rc_msg(msg, EAGAIN);
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
		return;
	}
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
#endif

	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);

	error_code = step_create(req_step_msg, &step_rec,
				 msg->protocol_version);

	if (error_code == SLURM_SUCCESS) {
		error_code = _make_step_cred(step_rec, &slurm_cred,
					     step_rec->start_protocol_ver);
		ext_sensors_g_get_stepstartdata(step_rec);
	}
	END_TIMER2("_slurm_rpc_job_step_create");

	/* return result */
	if (error_code) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS) {
			if ((error_code == ESLURM_PROLOG_RUNNING) ||
			    (error_code == ESLURM_DISABLED)) { /*job suspended*/
				debug("%s for suspended JobId=%u: %s",
				      __func__,
				      req_step_msg->job_id,
				      slurm_strerror(error_code));
			} else {
				info("%s for JobId=%u: %s",
				     __func__,
				     req_step_msg->job_id,
				     slurm_strerror(error_code));
			}
		}
		slurm_send_rc_msg(msg, error_code);
	} else {
		slurm_step_layout_t *layout = step_rec->step_layout;

		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
			sched_info("%s: %pS %s %s",
				   __func__, step_rec, req_step_msg->node_list,
				   TIME_STR);

		memset(&job_step_resp, 0, sizeof(job_step_resp));
		job_step_resp.job_step_id = step_rec->step_id;
		job_step_resp.resv_ports  = step_rec->resv_ports;
		job_step_resp.step_layout = layout;
#ifdef HAVE_FRONT_END
		if (step_rec->job_ptr->batch_host) {
			job_step_resp.step_layout->front_end =
				xstrdup(step_rec->job_ptr->batch_host);
		}
#endif
		if (step_rec->job_ptr && step_rec->job_ptr->details &&
		    (step_rec->job_ptr->details->cpu_bind_type != NO_VAL16)) {
			job_step_resp.def_cpu_bind_type =
				step_rec->job_ptr->details->cpu_bind_type;
		}
		job_step_resp.cred           = slurm_cred;
		job_step_resp.use_protocol_ver = step_rec->start_protocol_ver;
		job_step_resp.select_jobinfo = step_rec->select_jobinfo;
		job_step_resp.switch_job     = step_rec->switch_job;

		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
		response_init(&resp, msg);
		resp.msg_type = RESPONSE_JOB_STEP_CREATE;
		resp.data = &job_step_resp;

		slurm_send_node_msg(msg->conn_fd, &resp);
		slurm_cred_destroy(slurm_cred);
		schedule_job_save();	/* Sets own locks */
	}
}

/* _slurm_rpc_job_step_get_info - process request for job step info */
static void _slurm_rpc_job_step_get_info(slurm_msg_t * msg)
{
	DEF_TIMERS;
	void *resp_buffer = NULL;
	int resp_buffer_size = 0;
	int error_code = SLURM_SUCCESS;
	job_step_info_request_msg_t *request =
		(job_step_info_request_msg_t *) msg->data;
	/* Locks: Read config, job, part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
		debug("Processing RPC: REQUEST_JOB_STEP_INFO from uid=%d", uid);

	lock_slurmctld(job_read_lock);

	if ((request->last_update - 1) >= last_job_update) {
		unlock_slurmctld(job_read_lock);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
			debug("%s, no change", __func__);
		error_code = SLURM_NO_CHANGE_IN_DATA;
	} else {
		Buf buffer = init_buf(BUF_SIZE);
		error_code = pack_ctld_job_step_info_response_msg(
			request->job_id, request->step_id,
			uid, request->show_flags, buffer,
			msg->protocol_version);
		unlock_slurmctld(job_read_lock);
		END_TIMER2("_slurm_rpc_job_step_get_info");
		if (error_code) {
			/* job_id:step_id not found or otherwise *\
			\* error message is printed elsewhere    */
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
				debug("%s: %s",
					__func__, slurm_strerror(error_code));
			free_buf(buffer);
		} else {
			resp_buffer_size = get_buf_offset(buffer);
			resp_buffer = xfer_buf_data(buffer);
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
				debug("%s size=%d %s",
					__func__, resp_buffer_size, TIME_STR);
		}
	}

	if (error_code)
		slurm_send_rc_msg(msg, error_code);
	else {
		slurm_msg_t response_msg;
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_JOB_STEP_INFO;
		response_msg.data = resp_buffer;
		response_msg.data_size = resp_buffer_size;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(resp_buffer);
	}
}

/* _slurm_rpc_job_will_run - process RPC to determine if job with given
 *	configuration can be initiated */
static void _slurm_rpc_job_will_run(slurm_msg_t * msg)
{
	/* init */
	DEF_TIMERS;
	int error_code = SLURM_SUCCESS;
	struct job_record *job_ptr = NULL;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition, read fed*/
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	gid_t gid = g_slurm_auth_get_gid(msg->auth_cred);
	char *hostname = g_slurm_auth_get_host(msg->auth_cred);
	uint16_t port;	/* dummy value */
	slurm_addr_t resp_addr;
	will_run_response_msg_t *resp = NULL;
	char *err_msg = NULL, *job_submit_user_msg = NULL;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		goto send_reply;
	}

	START_TIMER;
	debug2("Processing RPC: REQUEST_JOB_WILL_RUN from uid=%d", uid);

	/* do RPC call */
	if ((error_code = _valid_id("REQUEST_JOB_WILL_RUN",
				    job_desc_msg, uid, gid)))
		goto send_reply;


	/* use the credential to validate where we came from */
	if (hostname) {
		xfree(job_desc_msg->alloc_node);
		job_desc_msg->alloc_node = hostname;
		hostname = NULL;
	}

	if ((job_desc_msg->alloc_node == NULL)
	    ||  (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_JOB_WILL_RUN lacks alloc_node from uid=%d", uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		lock_slurmctld(job_read_lock);
		job_desc_msg->pack_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,uid,&err_msg);
		unlock_slurmctld(job_read_lock);
	}

	if (err_msg)
		job_submit_user_msg = xstrdup(err_msg);

	if (!slurm_get_peer_addr(msg->conn_fd, &resp_addr)) {
		job_desc_msg->resp_host = xmalloc(16);
		slurm_get_ip_str(&resp_addr, &port,
				 job_desc_msg->resp_host, 16);
		dump_job_desc(job_desc_msg);
		if (error_code == SLURM_SUCCESS) {
			lock_slurmctld(job_write_lock);
			if (job_desc_msg->job_id == NO_VAL) {
				job_desc_msg->pack_job_offset = NO_VAL;
				error_code = job_allocate(job_desc_msg, false,
							  true, &resp, true,
							  uid, &job_ptr,
							  &err_msg,
							  msg->protocol_version);
			} else {	/* existing job test */
				error_code = job_start_data(job_desc_msg,
							    &resp);
			}
			unlock_slurmctld(job_write_lock);
			END_TIMER2("_slurm_rpc_job_will_run");
		}
	} else if (errno)
		error_code = errno;
	else
		error_code = SLURM_ERROR;

send_reply:

	/* return result */
	if (error_code) {
		debug2("_slurm_rpc_job_will_run: %s",
		       slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else if (resp) {
		slurm_msg_t response_msg;
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_JOB_WILL_RUN;
		response_msg.data = resp;
		resp->job_submit_user_msg = job_submit_user_msg;
		job_submit_user_msg = NULL;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		slurm_free_will_run_response_msg(resp);
		debug2("_slurm_rpc_job_will_run success %s", TIME_STR);
	} else {
		debug2("_slurm_rpc_job_will_run success %s", TIME_STR);
		if (job_desc_msg->job_id == NO_VAL)
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

static void _slurm_rpc_event_log(slurm_msg_t * msg)
{
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	slurm_event_log_msg_t *event_log_msg;
	int error_code = SLURM_SUCCESS;

	event_log_msg = (slurm_event_log_msg_t *) msg->data;
	if (!validate_slurm_user(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_EVENT_LOG from uid=%d", uid);
	} else if (event_log_msg->level == LOG_LEVEL_ERROR) {
		error("%s", event_log_msg->string);
	} else if (event_log_msg->level == LOG_LEVEL_INFO) {
		info("%s", event_log_msg->string);
	} else if (event_log_msg->level == LOG_LEVEL_VERBOSE) {
		verbose("%s", event_log_msg->string);
	} else if (event_log_msg->level == LOG_LEVEL_DEBUG) {
		debug("%s", event_log_msg->string);
	} else if (event_log_msg->level == LOG_LEVEL_DEBUG2) {
		debug2("%s", event_log_msg->string);
	} else if (event_log_msg->level == LOG_LEVEL_DEBUG3) {
		debug3("%s", event_log_msg->string);
	} else if (event_log_msg->level == LOG_LEVEL_DEBUG4) {
		debug4("%s", event_log_msg->string);
	} else if (event_log_msg->level == LOG_LEVEL_DEBUG5) {
		debug5("%s", event_log_msg->string);
	} else {
		error_code = EINVAL;
		error("Invalid message level: %u", event_log_msg->level);
		error("%s", event_log_msg->string);
	}
	slurm_send_rc_msg(msg, error_code);
}

/* _slurm_rpc_node_registration - process RPC to determine if a node's
 *	actual configuration satisfies the configured specification */
static void _slurm_rpc_node_registration(slurm_msg_t * msg,
					 bool running_composite)
{
	/* init */
	DEF_TIMERS;
	int error_code = SLURM_SUCCESS;
	bool newly_up = false;
	slurm_node_registration_status_msg_t *node_reg_stat_msg =
		(slurm_node_registration_status_msg_t *) msg->data;
	/* Locks: Read config, write job, write node */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: MESSAGE_NODE_REGISTRATION_STATUS from uid=%d",
	       uid);
	if (!validate_slurm_user(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, NODE_REGISTER RPC from uid=%d", uid);
	}

	if (msg->protocol_version != SLURM_PROTOCOL_VERSION)
		info("Node %s appears to have a different version "
		     "of Slurm than ours.  Please update at your earliest "
		     "convenience.", node_reg_stat_msg->node_name);

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		if (!(slurmctld_conf.debug_flags & DEBUG_FLAG_NO_CONF_HASH) &&
		    (node_reg_stat_msg->hash_val != NO_VAL) &&
		    (node_reg_stat_msg->hash_val != slurm_get_hash_val())) {
			error("Node %s appears to have a different slurm.conf "
			      "than the slurmctld.  This could cause issues "
			      "with communication and functionality.  "
			      "Please review both files and make sure they "
			      "are the same.  If this is expected ignore, and "
			      "set DebugFlags=NO_CONF_HASH in your slurm.conf.",
			      node_reg_stat_msg->node_name);
		}
		if (!running_composite)
			lock_slurmctld(job_write_lock);
#ifdef HAVE_FRONT_END		/* Operates only on front-end */
		error_code = validate_nodes_via_front_end(node_reg_stat_msg,
							  msg->protocol_version,
							  &newly_up);
#else
		validate_jobs_on_node(node_reg_stat_msg);
		error_code = validate_node_specs(node_reg_stat_msg,
						 msg->protocol_version,
						 &newly_up);
#endif
		if (!running_composite)
			unlock_slurmctld(job_write_lock);
		END_TIMER2("_slurm_rpc_node_registration");
		if (newly_up) {
			queue_job_scheduler();
		}
	}

	/* return result */
	if (error_code) {
		error("_slurm_rpc_node_registration node=%s: %s",
		      node_reg_stat_msg->node_name,
		      slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_node_registration complete for %s %s",
		       node_reg_stat_msg->node_name, TIME_STR);
		/* If the slurmd is requesting a response send it */
		if (node_reg_stat_msg->flags & SLURMD_REG_FLAG_RESP) {
			slurm_node_reg_resp_msg_t *resp = NULL, tmp_resp;
			if (msg->msg_index && msg->ret_list) {
				/*
				 * If this is the case then the resp must be
				 * xmalloced and will be freed when dealt with
				 * later.
				 */
				resp = xmalloc(
					sizeof(slurm_node_reg_resp_msg_t));
			} else {
				memset(&tmp_resp, 0, sizeof(tmp_resp));
				resp = &tmp_resp;
			}

			/*
			 * Don't add the assoc_mgr_tres_list here as it could
			 * get freed later if you do.  The pack functions grab
			 * it for us if it isn't here.
			 */
			//resp->tres_list = assoc_mgr_tres_list;

			slurm_send_msg(msg, RESPONSE_NODE_REGISTRATION, resp);
		} else
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

/* _slurm_rpc_job_alloc_info - process RPC to get details on existing job */
static void _slurm_rpc_job_alloc_info(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	struct job_record *job_ptr;
	DEF_TIMERS;
	job_alloc_info_msg_t *job_info_msg = (job_alloc_info_msg_t *) msg->data;
	resource_allocation_response_msg_t *job_info_resp_msg;
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_JOB_ALLOCATION_INFO from uid=%d", uid);

	/* do RPC call */
	lock_slurmctld(job_read_lock);
	error_code = job_alloc_info(uid, job_info_msg->job_id, &job_ptr);
	END_TIMER2("_slurm_rpc_job_alloc_info");

	/* return result */
	if (error_code || (job_ptr == NULL) || (job_ptr->job_resrcs == NULL)) {
		unlock_slurmctld(job_read_lock);
		debug2("%s: JobId=%u, uid=%u: %s", __func__,
		       job_info_msg->job_id, uid, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug("%s: JobId=%u NodeList=%s %s", __func__,
		      job_info_msg->job_id, job_ptr->nodes, TIME_STR);

		job_info_resp_msg = build_job_info_resp(job_ptr);
		set_remote_working_response(job_info_resp_msg, job_ptr,
					    job_info_msg->req_cluster);
		unlock_slurmctld(job_read_lock);

		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_JOB_ALLOCATION_INFO;
		response_msg.data     = job_info_resp_msg;

		slurm_send_node_msg(msg->conn_fd, &response_msg);

		/* NULL out msg->working_cluster_rec because it's pointing to
		 * the global memory */
		job_info_resp_msg->working_cluster_rec = NULL;
		slurm_free_resource_allocation_response_msg(job_info_resp_msg);
	}
}

static void _pack_alloc_list_del(void *x)
{
	resource_allocation_response_msg_t *job_info_resp_msg;

	job_info_resp_msg = (resource_allocation_response_msg_t *) x;
	/* NULL out msg->working_cluster_rec because it's pointing to
	 * the global memory */
	job_info_resp_msg->working_cluster_rec = NULL;
	slurm_free_resource_allocation_response_msg(job_info_resp_msg);
}

/*
 * _slurm_rpc_job_pack_alloc_info - process RPC to get details on existing pack
 * job
 */
static void _slurm_rpc_job_pack_alloc_info(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	struct job_record *job_ptr, *pack_job;
	ListIterator iter;
	void *working_cluster_rec = NULL;
	List resp;
	DEF_TIMERS;
	job_alloc_info_msg_t *job_info_msg = (job_alloc_info_msg_t *) msg->data;
	resource_allocation_response_msg_t *job_info_resp_msg;
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_JOB_PACK_ALLOC_INFO from uid=%d", uid);

	/* do RPC call */
	lock_slurmctld(job_read_lock);
	error_code = job_alloc_info(uid, job_info_msg->job_id, &job_ptr);
	END_TIMER2("_slurm_rpc_job_pack_alloc_info");

	/* return result */
	if ((error_code == SLURM_SUCCESS) && job_ptr &&
	    (job_ptr->pack_job_id && !job_ptr->pack_job_list))
		error_code = ESLURM_NOT_PACK_JOB_LEADER;
	if (error_code || (job_ptr == NULL) || (job_ptr->job_resrcs == NULL)) {
		unlock_slurmctld(job_read_lock);
		debug2("%s: JobId=%u, uid=%u: %s", __func__,
		       job_info_msg->job_id, uid, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	debug("%s: JobId=%u NodeList=%s %s", __func__,
	      job_info_msg->job_id, job_ptr->nodes, TIME_STR);

	if (!job_ptr->pack_job_list) {
		resp = list_create(_pack_alloc_list_del);
		job_info_resp_msg = build_job_info_resp(job_ptr);
		set_remote_working_response(job_info_resp_msg, job_ptr,
					    job_info_msg->req_cluster);
		list_append(resp, job_info_resp_msg);
	} else {
		resp = list_create(_pack_alloc_list_del);
		iter = list_iterator_create(job_ptr->pack_job_list);
		while ((pack_job = (struct job_record *) list_next(iter))) {
			if (job_ptr->pack_job_id != pack_job->pack_job_id) {
				error("%s: Bad pack_job_list for %pJ",
				      __func__, job_ptr);
				continue;
			}
			if (pack_job->job_id != job_info_msg->job_id)
				(void) job_alloc_info_ptr(uid, pack_job);
			job_info_resp_msg = build_job_info_resp(pack_job);
			if (working_cluster_rec) {
				job_info_resp_msg->working_cluster_rec =
					working_cluster_rec;
			} else {
				set_remote_working_response(job_info_resp_msg,
						pack_job,
						job_info_msg->req_cluster);
				working_cluster_rec =
					job_info_resp_msg->working_cluster_rec;
			}
			list_append(resp, job_info_resp_msg);
		}
		list_iterator_destroy(iter);
	}
	unlock_slurmctld(job_read_lock);

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_JOB_PACK_ALLOCATION;
	response_msg.data     = resp;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	FREE_NULL_LIST(resp);
}

#ifndef HAVE_FRONT_END
static slurm_addr_t *_build_node_addr(char *node_list, uint32_t node_cnt,
				      uint32_t pack_job_id)
{
	hostlist_t host_list = NULL;
	struct node_record *node_ptr;
	slurm_addr_t *node_addr;
	char *this_node_name;
	int error_code = SLURM_SUCCESS, node_inx = 0;

	if ((host_list = hostlist_create(node_list)) == NULL) {
		error("%s hostlist_create error for pack JobId=%u (%s): %m",
		      __func__, pack_job_id, node_list);
		return NULL;
	}


	node_addr = xmalloc(sizeof(slurm_addr_t) * node_cnt);
	while ((this_node_name = hostlist_shift(host_list))) {
		if ((node_ptr = find_node_record(this_node_name))) {
			memcpy(node_addr + node_inx,
			       &node_ptr->slurm_addr, sizeof(slurm_addr_t));
			node_inx++;
		} else {
			error("%s: Invalid node %s in pack JobId=%u",
			      __func__, this_node_name, pack_job_id);
			error_code = SLURM_ERROR;
		}
		free(this_node_name);
		if (error_code != SLURM_SUCCESS)
			break;
	}
	hostlist_destroy(host_list);

	if (error_code != SLURM_SUCCESS)
		xfree(node_addr);
	return node_addr;
}
#endif

/* _slurm_rpc_job_sbcast_cred - process RPC to get details on existing job
 *	plus sbcast credential */
static void _slurm_rpc_job_sbcast_cred(slurm_msg_t * msg)
{
#ifdef HAVE_FRONT_END
	slurm_send_rc_msg(msg, ESLURM_NOT_SUPPORTED);
#else
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	struct job_record *job_ptr = NULL, *job_pack_ptr;
	struct step_record *step_ptr;
	char *local_node_list = NULL, *node_list = NULL;
	struct node_record *node_ptr;
	slurm_addr_t *node_addr = NULL;
	hostlist_t host_list = NULL;
	char *this_node_name;
	int node_inx = 0;
	uint32_t node_cnt = 0;
	DEF_TIMERS;
	step_alloc_info_msg_t *job_info_msg =
		(step_alloc_info_msg_t *) msg->data;
	job_sbcast_cred_msg_t job_info_resp_msg;
	sbcast_cred_arg_t sbcast_arg;
	sbcast_cred_t *sbcast_cred;
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_JOB_SBCAST_CRED from uid=%d", uid);

	/* do RPC call */
	lock_slurmctld(job_read_lock);
	if (job_info_msg->pack_job_offset == NO_VAL) {
		bitstr_t *node_bitmap = NULL;
		ListIterator iter;
		error_code = job_alloc_info(uid, job_info_msg->job_id,
					    &job_ptr);
		if (job_ptr && job_ptr->pack_job_list) {  /* Do full pack job */
			job_info_msg->step_id = NO_VAL;
			iter = list_iterator_create(job_ptr->pack_job_list);
			while ((job_pack_ptr =
			        (struct job_record *) list_next(iter))) {
				error_code = job_alloc_info_ptr(uid,
							        job_pack_ptr);
				if (error_code)
					break;
				if (!job_pack_ptr->node_bitmap) {
					debug("%s: %pJ lacks node bitmap",
					      __func__, job_pack_ptr);
				} else if (!node_bitmap) {
					node_bitmap = bit_copy(
						     job_pack_ptr->node_bitmap);
				} else {
					bit_or(node_bitmap,
					       job_pack_ptr->node_bitmap);
				}
			}
			list_iterator_destroy(iter);
			if (!error_code) {
				node_cnt = bit_set_count(node_bitmap);
				local_node_list = bitmap2node_name(node_bitmap);
				node_list = local_node_list;
				node_addr = _build_node_addr(node_list,
							     node_cnt,
							     job_ptr->job_id);
				if (!node_addr)
					error_code = SLURM_ERROR;
			}
			FREE_NULL_BITMAP(node_bitmap);
		}
	} else {
		job_ptr = find_job_pack_record(job_info_msg->job_id,
					       job_info_msg->pack_job_offset);
		if (job_ptr) {
			job_info_msg->job_id = job_ptr->job_id;
			error_code = job_alloc_info(uid, job_info_msg->job_id,
						    &job_ptr);
		} else {
			error_code = ESLURM_INVALID_JOB_ID;
		}
	}

	if (job_ptr && !validate_operator(uid) && job_ptr->user_id != uid)
		error_code = ESLURM_USER_ID_MISSING;

	if ((error_code == SLURM_SUCCESS) && job_ptr
	    && (job_info_msg->step_id != NO_VAL)) {
		step_ptr = find_step_record(job_ptr, job_info_msg->step_id);
		if (!step_ptr) {
			job_ptr = NULL;
			error_code = ESLURM_INVALID_JOB_ID;
		} else if (step_ptr->step_layout &&
			   (step_ptr->step_layout->node_cnt !=
			    job_ptr->node_cnt)) {
			node_cnt  = step_ptr->step_layout->node_cnt;
			node_list = step_ptr->step_layout->node_list;
			if ((host_list = hostlist_create(node_list)) == NULL) {
				fatal("hostlist_create error for %s: %m",
				      node_list);
				return;	/* Avoid CLANG false positive */
			}
			node_addr = xmalloc(sizeof(slurm_addr_t) * node_cnt);
			while ((this_node_name = hostlist_shift(host_list))) {
				if ((node_ptr =
				     find_node_record(this_node_name))) {
					memcpy(&node_addr[node_inx++],
					       &node_ptr->slurm_addr,
					       sizeof(slurm_addr_t));
				} else {
					error("Invalid node %s in %pS",
					      this_node_name, step_ptr);
				}
				free(this_node_name);
			}
			hostlist_destroy(host_list);
		}
	}
	if ((error_code == SLURM_SUCCESS) && job_ptr && !node_addr) {
		node_addr = job_ptr->node_addr;
		node_cnt  = job_ptr->node_cnt;
		node_list = job_ptr->nodes;
		node_addr = xmalloc(sizeof(slurm_addr_t) * node_cnt);
		memcpy(node_addr, job_ptr->node_addr,
		       (sizeof(slurm_addr_t) * node_cnt));
	}
	END_TIMER2("_slurm_rpc_job_sbcast_cred");

	/* return result */
	if (error_code || (job_ptr == NULL)) {
		unlock_slurmctld(job_read_lock);
		debug2("_slurm_rpc_job_sbcast_cred: JobId=%u, uid=%u: %s",
		       job_info_msg->job_id, uid,
		       slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
		xfree(local_node_list);
		xfree(node_addr);
		return ;
	}

	/*
	 * Note - using pointers to other xmalloc'd elements owned by other
	 * structures to avoid copy overhead. Do not free them!
	 */
	memset(&sbcast_arg, 0, sizeof(sbcast_arg));
	sbcast_arg.job_id = job_ptr->job_id;
	sbcast_arg.pack_jobid = job_ptr->pack_job_id;
	sbcast_arg.uid = job_ptr->user_id;
	sbcast_arg.gid = job_ptr->group_id;
	sbcast_arg.nodes = node_list; /* avoid extra copy */
	sbcast_arg.expiration = job_ptr->end_time;

	if ((sbcast_cred =
		    create_sbcast_cred(slurmctld_config.cred_ctx,
				       &sbcast_arg,
				       msg->protocol_version)) == NULL) {
		unlock_slurmctld(job_read_lock);
		error("_slurm_rpc_job_sbcast_cred %pJ cred create error",
		      job_ptr);
		slurm_send_rc_msg(msg, SLURM_ERROR);
	} else {
		if (job_ptr && (job_info_msg->step_id != NO_VAL)) {
			info("_slurm_rpc_job_sbcast_cred %pJ NodeList=%s %s",
			     job_ptr, node_list, TIME_STR);
		} else {
			info("_slurm_rpc_job_sbcast_cred %pJ Step=%u NodeList=%s %s",
			     job_ptr, job_info_msg->step_id, node_list,
			     TIME_STR);
		}

		memset(&job_info_resp_msg, 0, sizeof(job_info_resp_msg));
		job_info_resp_msg.job_id         = job_ptr->job_id;
		job_info_resp_msg.node_addr      = node_addr;
		job_info_resp_msg.node_cnt       = node_cnt;
		job_info_resp_msg.node_list      = xstrdup(node_list);
		job_info_resp_msg.sbcast_cred    = sbcast_cred;
		unlock_slurmctld(job_read_lock);

		response_init(&response_msg, msg);
		response_msg.msg_type    = RESPONSE_JOB_SBCAST_CRED;
		response_msg.data        = &job_info_resp_msg;

		slurm_send_node_msg(msg->conn_fd, &response_msg);
		/* job_info_resp_msg.node_addr is pointer,
		 * xfree(node_addr) is below */
		xfree(job_info_resp_msg.node_list);
		delete_sbcast_cred(sbcast_cred);
	}
	xfree(local_node_list);
	xfree(node_addr);
#endif
}

/* _slurm_rpc_ping - process ping RPC */
static void _slurm_rpc_ping(slurm_msg_t * msg)
{
	/* We could authenticate here, if desired */

	/* return result */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}


/* _slurm_rpc_reconfigure_controller - process RPC to re-initialize
 *	slurmctld from configuration file
 * Anything you add to this function must be added to the
 * slurm_reconfigure function inside controller.c try
 * to keep these in sync.
 */
static void _slurm_rpc_reconfigure_controller(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	static bool in_progress = false;
	DEF_TIMERS;
	/* Locks: Write configuration, job, node and partition */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	info("Processing RPC: REQUEST_RECONFIGURE from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error("Security violation, RECONFIGURE RPC from uid=%d", uid);
		error_code = ESLURM_USER_ID_MISSING;
	}
	if (in_progress || slurmctld_config.shutdown_time)
		error_code = EINPROGRESS;

	/* do RPC call */
	if (error_code == SLURM_SUCCESS) {
		sched_debug("begin reconfiguration");
		lock_slurmctld(config_write_lock);
		in_progress = true;
		error_code = read_slurm_conf(1, true);
		if (error_code == SLURM_SUCCESS) {
			_update_cred_key();
			set_slurmctld_state_loc();
			msg_to_slurmd(REQUEST_RECONFIGURE);
		}
		in_progress = false;
		gs_reconfig();
		unlock_slurmctld(config_write_lock);
		xcgroup_reconfig_slurm_cgroup_conf();
		assoc_mgr_set_missing_uids();
		start_power_mgr(&slurmctld_config.thread_id_power);
		trigger_reconfig();
	}
	END_TIMER2("_slurm_rpc_reconfigure_controller");

	/* return result */
	if (error_code) {
		error("_slurm_rpc_reconfigure_controller: %s",
		      slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("_slurm_rpc_reconfigure_controller: completed %s",
		     TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		acct_storage_g_reconfig(acct_db_conn, 0);
		priority_g_reconfig(false);	/* notify priority plugin too */
		save_all_state();		/* has its own locks */
		queue_job_scheduler();
	}
}

/* _slurm_rpc_takeover - process takeover RPC */
static void _slurm_rpc_takeover(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	/* We could authenticate here, if desired */
	if (!validate_super_user(uid)) {
		error("Security violation, TAKEOVER RPC from uid=%d", uid);
		error_code = ESLURM_USER_ID_MISSING;
	} else {
		/* takeover is not possible in controller mode */
		/* return success */
		info("Performing RPC: REQUEST_TAKEOVER : "
		     "already in controller mode - skipping");
	}

	slurm_send_rc_msg(msg, error_code);

}

/* _slurm_rpc_shutdown_controller - process RPC to shutdown slurmctld */
static void _slurm_rpc_shutdown_controller(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	uint16_t options = 0;
	time_t now = time(NULL);
	shutdown_msg_t *shutdown_msg = (shutdown_msg_t *) msg->data;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	/* Locks: Read node */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	if (!validate_super_user(uid)) {
		error("Security violation, SHUTDOWN RPC from uid=%d", uid);
		error_code = ESLURM_USER_ID_MISSING;
	}
	if (error_code);
	else if (msg->msg_type == REQUEST_CONTROL) {
		info("Performing RPC: REQUEST_CONTROL");
		/* resume backup mode */
		slurmctld_config.resume_backup = true;
	} else {
		info("Performing RPC: REQUEST_SHUTDOWN");
		options = shutdown_msg->options;
	}

	/* do RPC call */
	if (error_code)
		;
	else if (options == 1)
		info("performing immeditate shutdown without state save");
	else if (slurmctld_config.shutdown_time)
		debug2("shutdown RPC issued when already in progress");
	else {
		if ((msg->msg_type == REQUEST_SHUTDOWN) &&
		    (options == 0)) {
			/* This means (msg->msg_type != REQUEST_CONTROL) */
			lock_slurmctld(node_read_lock);
			msg_to_slurmd(REQUEST_SHUTDOWN);
			unlock_slurmctld(node_read_lock);
		}
		if (slurmctld_config.thread_id_sig)	/* signal clean-up */
			pthread_kill(slurmctld_config.thread_id_sig, SIGTERM);
		else {
			error("thread_id_sig undefined, hard shutdown");
			slurmctld_config.shutdown_time = now;
			/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
			slurmctld_shutdown();
		}
	}

	if (msg->msg_type == REQUEST_CONTROL) {
		struct timespec ts = {0, 0};

		/* save_all_state();	performed by _slurmctld_background */

		/*
		 * jobcomp/elasticsearch saves/loads the state to/from file
		 * elasticsearch_state. Since the jobcomp API isn't designed
		 * with save/load state operations, the jobcomp/elasticsearch
		 * _save_state() is highly coupled to its fini() function. This
		 * state doesn't follow the same execution path as the rest of
		 * Slurm states, where in save_all_sate() they are all indepen-
		 * dently scheduled. So we save it manually here.
		 */
		(void) g_slurm_jobcomp_fini();

		/*
		 * Wait for the backup to dump state and finish up everything.
		 * This should happen in _slurmctld_background and then release
		 * once we know for sure we are in backup mode in run_backup().
		 * Here we will wait CONTROL_TIMEOUT - 1 before we reply.
		 */
		ts.tv_sec = now + CONTROL_TIMEOUT - 1;

		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		slurm_cond_timedwait(&slurmctld_config.backup_finish_cond,
				     &slurmctld_config.thread_count_lock,
				     &ts);
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

		if (slurmctld_config.resume_backup)
			error("%s: REQUEST_CONTROL reply but backup not completely done relinquishing control.  Old state possible", __func__);
	}

	slurm_send_rc_msg(msg, error_code);
	if ((error_code == SLURM_SUCCESS) && (options == 1) &&
	    (slurmctld_config.thread_id_sig))
		pthread_kill(slurmctld_config.thread_id_sig, SIGABRT);
}

/* _slurm_rpc_shutdown_controller_immediate - process RPC to shutdown
 *	slurmctld */
static void _slurm_rpc_shutdown_controller_immediate(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	if (!validate_super_user(uid)) {
		error("Security violation, SHUTDOWN_IMMEDIATE RPC from uid=%d",
		      uid);
		error_code = ESLURM_USER_ID_MISSING;
	}

	/* do RPC call */
	/* No op: just used to knock loose accept RPC thread */
	if (error_code == SLURM_SUCCESS)
		debug("Performing RPC: REQUEST_SHUTDOWN_IMMEDIATE");
}

/* _slurm_rpc_step_complete - process step completion RPC to note the
 *      completion of a job step on at least some nodes.
 *	If the job step is complete, it may
 *	represent the termination of an entire job step */
static void _slurm_rpc_step_complete(slurm_msg_t *msg, bool running_composite)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS, rc, rem;
	uint32_t step_rc;
	DEF_TIMERS;
	step_complete_msg_t *req = (step_complete_msg_t *)msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	bool dump_job = false;

	/* init */
	START_TIMER;
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
		info("Processing RPC: REQUEST_STEP_COMPLETE for JobId=%u StepId=%u nodes %u-%u rc=%u uid=%d",
		     req->job_id, req->job_step_id, req->range_first,
		     req->range_last, req->step_rc, uid);

	if (!running_composite) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	rc = step_partial_comp(req, uid, &rem, &step_rc);

	if (rc || rem) {	/* some error or not totally done */
		/* Note: Error printed within step_partial_comp */
		if (!running_composite) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		slurm_send_rc_msg(msg, rc);
		if (!rc)	/* partition completion */
			schedule_job_save();	/* Has own locking */
		return;
	}

	error_code = job_step_complete(req->job_id, req->job_step_id,
				       uid, false, step_rc);
	if (!running_composite) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}
	END_TIMER2("_slurm_rpc_step_complete");

	/* return result */
	if (error_code) {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
			info("%s 1 JobId=%u StepId=%u %s",
			     __func__, req->job_id, req->job_step_id,
			     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
			sched_info("%s JobId=%u StepId=%u %s",
				   __func__, req->job_id,
				   req->job_step_id,
				   TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		dump_job = true;
	}

	if (dump_job)
		(void) schedule_job_save();	/* Has own locking */
}

/* _slurm_rpc_step_layout - return the step layout structure for
 *      a job step, if it currently exists
 */
static void _slurm_rpc_step_layout(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	DEF_TIMERS;
	job_step_id_msg_t *req = (job_step_id_msg_t *)msg->data;
	slurm_step_layout_t *step_layout = NULL;
	/* Locks: Read config job, write node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	struct job_record *job_ptr = NULL;
	struct step_record *step_ptr = NULL;

	START_TIMER;
	debug2("Processing RPC: REQUEST_STEP_LAYOUT, from uid=%d", uid);

	lock_slurmctld(job_read_lock);
	error_code = job_alloc_info(uid, req->job_id, &job_ptr);
	END_TIMER2("_slurm_rpc_step_layout");
	/* return result */
	if (error_code || (job_ptr == NULL)) {
		unlock_slurmctld(job_read_lock);
		if (error_code == ESLURM_ACCESS_DENIED) {
			error("Security vioation, REQUEST_STEP_LAYOUT for "
			      "JobId=%u from uid=%u", req->job_id, uid);
		} else {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
				info("%s: JobId=%u, uid=%u: %s", __func__,
				     req->job_id, uid,
				     slurm_strerror(error_code));
		}
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	step_ptr = find_step_record(job_ptr, req->step_id);
	if (!step_ptr) {
		unlock_slurmctld(job_read_lock);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
			info("%s: %pJ StepId=%u Not Found",
			     __func__, job_ptr, req->step_id);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		return;
	}
	step_layout = slurm_step_layout_copy(step_ptr->step_layout);
#ifdef HAVE_FRONT_END
	if (job_ptr->batch_host)
		step_layout->front_end = xstrdup(job_ptr->batch_host);
#endif
	unlock_slurmctld(job_read_lock);

	response_init(&response_msg, msg);
	response_msg.msg_type    = RESPONSE_STEP_LAYOUT;
	response_msg.data        = step_layout;

	slurm_send_node_msg(msg->conn_fd, &response_msg);
	slurm_step_layout_destroy(step_layout);
}

/* _slurm_rpc_step_update - update a job step
 */
static void _slurm_rpc_step_update(slurm_msg_t *msg)
{
	DEF_TIMERS;
	step_update_request_msg_t *req =
		(step_update_request_msg_t *) msg->data;
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	int rc;

	START_TIMER;
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
		info("Processing RPC: REQUEST_STEP_UPDATE, from uid=%d", uid);

	lock_slurmctld(job_write_lock);
	rc = update_step(req, uid);
	unlock_slurmctld(job_write_lock);
	END_TIMER2("_slurm_rpc_step_update");

	slurm_send_rc_msg(msg, rc);
}

/* _slurm_rpc_submit_batch_job - process RPC to submit a batch job */
static void _slurm_rpc_submit_batch_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	uint32_t job_id = 0, priority = 0;
	struct job_record *job_ptr = NULL;
	slurm_msg_t response_msg;
	submit_response_msg_t submit_msg;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition, read
	 * federation */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	gid_t gid = g_slurm_auth_get_gid(msg->auth_cred);
	char *hostname = g_slurm_auth_get_host(msg->auth_cred);
	char *err_msg = NULL, *job_submit_user_msg = NULL;
	bool reject_job = false;

	START_TIMER;
	debug2("Processing RPC: REQUEST_SUBMIT_BATCH_JOB from uid=%d", uid);
	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}

	if ((error_code = _valid_id("REQUEST_SUBMIT_BATCH_JOB",
				    job_desc_msg, uid, gid))) {
		reject_job = true;
		goto send_msg;
	}

	/* use the credential to validate where we came from */
	if (hostname) {
		xfree(job_desc_msg->alloc_node);
		job_desc_msg->alloc_node = hostname;
		hostname = NULL;
	}

	if ((job_desc_msg->alloc_node == NULL) ||
	    (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_SUBMIT_BATCH_JOB lacks alloc_node from uid=%d",
		      uid);
	}

	dump_job_desc(job_desc_msg);

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		lock_slurmctld(job_read_lock);
		job_desc_msg->pack_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,uid,&err_msg);
		unlock_slurmctld(job_read_lock);
	}

	/*
	 * In validate_job_create_req(), err_msg is currently only modified in
	 * the call to job_submit_plugin_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to fed_mgr_job_allocate and/or job_allocate, and we
	 * need the job submit plugin value to build the resource allocation
	 * response in the call to build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	if (error_code) {
		reject_job = true;
		goto send_msg;
	}

	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	START_TIMER;	/* Restart after we have locks */

	if (fed_mgr_fed_rec) {
		if (fed_mgr_job_allocate(msg, job_desc_msg, false, uid,
					 msg->protocol_version, &job_id,
					 &error_code, &err_msg))
			reject_job = true;
	} else {
		/* Create new job allocation */
		job_desc_msg->pack_job_offset = NO_VAL;
		error_code = job_allocate(job_desc_msg,
					  job_desc_msg->immediate,
					  false, NULL, 0, uid, &job_ptr,
					  &err_msg,
					  msg->protocol_version);
		if (!job_ptr ||
		    (error_code && job_ptr->job_state == JOB_FAILED))
			reject_job = true;
		else {
			job_id = job_ptr->job_id;
			priority = job_ptr->priority;
		}

		if (job_desc_msg->immediate &&
		    (error_code != SLURM_SUCCESS)) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			reject_job = true;
		}
	}
	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);

send_msg:
	END_TIMER2("_slurm_rpc_submit_batch_job");

	if (reject_job) {
		info("%s: %s", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		info("%s: JobId=%u InitPrio=%u %s",
		     __func__, job_id, priority, TIME_STR);
		/* send job_ID */
		memset(&submit_msg, 0, sizeof(submit_msg));
		submit_msg.job_id     = job_id;
		submit_msg.step_id    = SLURM_BATCH_SCRIPT;
		submit_msg.error_code = error_code;
		submit_msg.job_submit_user_msg = job_submit_user_msg;
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_SUBMIT_BATCH_JOB;
		response_msg.data = &submit_msg;
		slurm_send_node_msg(msg->conn_fd, &response_msg);

		schedule_job_save();	/* Has own locks */
		schedule_node_save();	/* Has own locks */
		queue_job_scheduler();
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_submit_batch_pack_job - process RPC to submit a batch pack job */
static void _slurm_rpc_submit_batch_pack_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	ListIterator iter;
	int error_code = SLURM_SUCCESS, alloc_only = 0;
	DEF_TIMERS;
	uint32_t pack_job_id = 0, pack_job_offset = 0;
	struct job_record *job_ptr = NULL, *first_job_ptr = NULL;
	slurm_msg_t response_msg;
	submit_response_msg_t submit_msg;
	job_desc_msg_t *job_desc_msg;
	char *script = NULL;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node, read partition, read fed */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	List job_req_list = (List) msg->data;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	gid_t gid = g_slurm_auth_get_gid(msg->auth_cred);
	char *hostname = g_slurm_auth_get_host(msg->auth_cred);
	uint32_t job_uid = NO_VAL;
	char *err_msg = NULL, *job_submit_user_msg = NULL;
	bool reject_job = false;
	List submit_job_list = NULL;
	hostset_t jobid_hostset = NULL;
	char tmp_str[32];
	char *pack_job_id_set = NULL;

	START_TIMER;
	debug2("Processing RPC: REQUEST_SUBMIT_BATCH_PACK_JOB from uid=%d",
	       uid);
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_SUBMIT_BATCH_PACK_JOB from uid=%d with empty job list",
		     uid);
		error_code = SLURM_ERROR;
		reject_job = true;
		goto send_msg;
	}
	if (!_sched_backfill()) {
		error_code = ESLURM_NOT_SUPPORTED;
		reject_job = true;
		goto send_msg;
	}
	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_SUBMIT_BATCH_PACK_JOB from uid=%d with empty job list",
		     uid);
		error_code = SLURM_ERROR;
		reject_job = true;
		goto send_msg;
	}

	/*
	 * If any job component has required nodes, those nodes must be excluded
	 * from all other components to avoid scheduling deadlock
	 */
	_exclude_pack_nodes(job_req_list);

	/* Validate the individual request */
	lock_slurmctld(job_read_lock);     /* Locks for job_submit plugin use */
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = (job_desc_msg_t *) list_next(iter))) {
		if (job_uid == NO_VAL)
			job_uid = job_desc_msg->user_id;

		if ((error_code = _valid_id("REQUEST_SUBMIT_BATCH_JOB",
					    job_desc_msg, uid, gid))) {
			reject_job = true;
			break;
		}

		/* use the credential to validate where we came from */
		if (hostname) {
			xfree(job_desc_msg->alloc_node);
			job_desc_msg->alloc_node = xstrdup(hostname);
		}

		if ((job_desc_msg->alloc_node == NULL) ||
		    (job_desc_msg->alloc_node[0] == '\0')) {
			error("REQUEST_SUBMIT_BATCH_PACK_JOB lacks alloc_node from uid=%d",
			      uid);
			error_code = ESLURM_INVALID_NODE_NAME;
			break;
		}

		dump_job_desc(job_desc_msg);

		job_desc_msg->pack_job_offset = pack_job_offset;
		error_code = validate_job_create_req(job_desc_msg, uid,
						     &err_msg);
		if (err_msg) {
			char *save_ptr = NULL, *tok;
			tok = strtok_r(err_msg, "\n", &save_ptr);
			while (tok) {
				char *sep = "";
				if (job_submit_user_msg)
					sep = "\n";
				xstrfmtcat(job_submit_user_msg, "%s%d: %s",
					   sep, pack_job_offset, tok);
				tok = strtok_r(NULL, "\n", &save_ptr);
			}
			xfree(err_msg);
		}

		if (error_code != SLURM_SUCCESS) {
			reject_job = true;
			break;
		}

		pack_job_offset++;
	}
	list_iterator_destroy(iter);
	xfree(hostname);
	unlock_slurmctld(job_read_lock);
	if (error_code != SLURM_SUCCESS)
		goto send_msg;

	/*
	 * In validate_job_create_req, err_msg is currently only modified in
	 * the call to job_submit_plugin_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to job_allocate, and we need the job submit plugin value
	 * to build the resource allocation response in the call to
	 * build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	/* Create new job allocations */
	submit_job_list = list_create(NULL);
	pack_job_offset = 0;
	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	START_TIMER;	/* Restart after we have locks */
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = (job_desc_msg_t *) list_next(iter))) {
		if (!script)
			script = xstrdup(job_desc_msg->script);
		if (pack_job_offset && job_desc_msg->script) {
			info("%s: Pack job %u offset %u has script, being ignored",
			     __func__, pack_job_id, pack_job_offset);
			xfree(job_desc_msg->script);

		}
		if (pack_job_offset) {
			/* Email notifications disable except for pack leader */
			job_desc_msg->mail_type = 0;
			xfree(job_desc_msg->mail_user);
		}
		if (!job_desc_msg->burst_buffer) {
			xfree(job_desc_msg->script);
			if (!(job_desc_msg->script = bb_g_build_pack_script(
				      script, pack_job_offset))) {
				error_code =
					ESLURM_INVALID_BURST_BUFFER_REQUEST;
				reject_job = true;
				break;
			}
		}
		job_desc_msg->pack_job_offset = pack_job_offset;
		error_code = job_allocate(job_desc_msg,
					  job_desc_msg->immediate, false,
					  NULL, alloc_only, uid, &job_ptr,
					  &err_msg, msg->protocol_version);
		if (!job_ptr ||
		    (error_code && job_ptr->job_state == JOB_FAILED)) {
			reject_job = true;
		} else {
			if (pack_job_id == 0) {
				pack_job_id = job_ptr->job_id;
				first_job_ptr = job_ptr;
				alloc_only = 1;
			}
			snprintf(tmp_str, sizeof(tmp_str), "%u",
				 job_ptr->job_id);
			if (jobid_hostset)
				hostset_insert(jobid_hostset, tmp_str);
			else
				jobid_hostset = hostset_create(tmp_str);
			job_ptr->pack_job_id     = pack_job_id;
			job_ptr->pack_job_offset = pack_job_offset++;
			job_ptr->batch_flag      = 1;
			list_append(submit_job_list, job_ptr);
		}

		if (job_desc_msg->immediate &&
		    (error_code != SLURM_SUCCESS)) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			reject_job = true;
		}
		if (reject_job)
			break;
	}
	list_iterator_destroy(iter);
	xfree(script);

	if ((pack_job_id == 0) && !reject_job) {
		info("%s: No error, but no pack_job_id", __func__);
		error_code = SLURM_ERROR;
		reject_job = true;
	}

	/* Validate limits on pack-job as a whole */
	if (!reject_job &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    !acct_policy_validate_pack(submit_job_list)) {
		info("Pack JobId=%u exceeded association/QOS limit for user %u",
		     pack_job_id, job_uid);
		error_code = ESLURM_ACCOUNTING_POLICY;
		reject_job = true;
	}

	_create_pack_job_id_set(jobid_hostset, pack_job_offset,
				&pack_job_id_set);
	if (first_job_ptr)
		first_job_ptr->pack_job_list = submit_job_list;

	iter = list_iterator_create(submit_job_list);
	while ((job_ptr = (struct job_record *) list_next(iter))) {
		job_ptr->pack_job_id_set = xstrdup(pack_job_id_set);
		if ((error_code == SLURM_SUCCESS) &&
		    (slurmctld_conf.debug_flags & DEBUG_FLAG_HETERO_JOBS)) {
			info("Submit %pJ", job_ptr);
		}
	}
	list_iterator_destroy(iter);
	xfree(pack_job_id_set);

	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);

send_msg:
	END_TIMER2("_slurm_rpc_submit_batch_pack_job");
	if (reject_job) {
		info("%s: %s", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
		if (submit_job_list) {
			(void) list_for_each(submit_job_list, _pack_job_cancel,
					     NULL);
			if (first_job_ptr)
				first_job_ptr->pack_job_list = submit_job_list;
			else
				FREE_NULL_LIST(submit_job_list);
		}
	} else {
		info("%s: JobId=%u %s", __func__, pack_job_id, TIME_STR);
		/* send job_ID */
		memset(&submit_msg, 0, sizeof(submit_msg));
		submit_msg.job_id     = pack_job_id;
		submit_msg.step_id    = SLURM_BATCH_SCRIPT;
		submit_msg.error_code = error_code;
		submit_msg.job_submit_user_msg = job_submit_user_msg;
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_SUBMIT_BATCH_JOB;
		response_msg.data = &submit_msg;
		slurm_send_node_msg(msg->conn_fd, &response_msg);

		schedule_job_save();	/* Has own locks */
	}
	if (jobid_hostset)
		hostset_destroy(jobid_hostset);
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_update_job - process RPC to update the configuration of a
 * job (e.g. priority)
 */
static void _slurm_rpc_update_job(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;
	/* Locks: Read config, write job, write node, read partition, read fed*/
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	lock_slurmctld(fed_read_lock);
	if (!_route_msg_to_origin(msg, job_desc_msg->job_id_str,
				  job_desc_msg->job_id, uid)) {
		unlock_slurmctld(fed_read_lock);
		return;
	}
	unlock_slurmctld(fed_read_lock);

	START_TIMER;
	debug2("Processing RPC: REQUEST_UPDATE_JOB from uid=%d", uid);
	/* job_desc_msg->user_id is set when the uid has been overriden with
	 * -u <uid> or --uid=<uid>. NO_VAL is default. Verify the request has
	 * come from an admin */
	if (job_desc_msg->user_id != NO_VAL) {
		if (!validate_super_user(uid)) {
			error_code = ESLURM_USER_ID_MISSING;
			error("Security violation, REQUEST_UPDATE_JOB RPC from uid=%d",
			      uid);
			/* Send back the error message for this case because
			 * update_job also sends back an error message */
			slurm_send_rc_msg(msg, error_code);
		} else {
			/* override uid allowed */
			uid = job_desc_msg->user_id;
		}
	}

	if (error_code == SLURM_SUCCESS) {
		int db_inx_max_cnt = 5, i=0;
		/* do RPC call */
		dump_job_desc(job_desc_msg);
		/* Ensure everything that may be written to database is lower
		 * case */
		xstrtolower(job_desc_msg->account);
		xstrtolower(job_desc_msg->wckey);
		error_code = ESLURM_JOB_SETTING_DB_INX;
		while (error_code == ESLURM_JOB_SETTING_DB_INX) {
			lock_slurmctld(job_write_lock);
			/* Use UID provided by scontrol. May be overridden with
			 * -u <uid>  or --uid=<uid> */
			if (job_desc_msg->job_id_str)
				error_code = update_job_str(msg, uid);
			else
				error_code = update_job(msg, uid, true);
			unlock_slurmctld(job_write_lock);
			if (error_code == ESLURM_JOB_SETTING_DB_INX) {
				if (i >= db_inx_max_cnt) {
					if (job_desc_msg->job_id_str) {
						info("%s: can't update job, waited %d seconds for JobId=%s to get a db_index, but it hasn't happened yet. Giving up and informing the user",
						      __func__, db_inx_max_cnt,
						      job_desc_msg->job_id_str);
					} else {
						info("%s: can't update job, waited %d seconds for JobId=%u to get a db_index, but it hasn't happened yet. Giving up and informing the user",
						      __func__, db_inx_max_cnt,
						      job_desc_msg->job_id);
					}
					slurm_send_rc_msg(msg, error_code);
					break;
				}
				i++;
				if (job_desc_msg->job_id_str) {
					debug("%s: We cannot update JobId=%s at the moment, we are setting the db index, waiting",
					      __func__,
					      job_desc_msg->job_id_str);
				} else {
					debug("%s: We cannot update JobId=%u at the moment, we are setting the db index, waiting",
					      __func__, job_desc_msg->job_id);
				}
				sleep(1);
			}
		}
	}
	END_TIMER2("_slurm_rpc_update_job");

	/* return result */
	if (error_code) {
		if (job_desc_msg->job_id_str) {
			info("%s: JobId=%s uid=%d: %s", __func__,
			     job_desc_msg->job_id_str, uid,
			     slurm_strerror(error_code));
		} else {
			info("%s: JobId=%u uid=%d: %s", __func__,
			     job_desc_msg->job_id, uid,
			     slurm_strerror(error_code));
		}
	} else {
		if (job_desc_msg->job_id_str) {
			info("%s: complete JobId=%s uid=%d %s", __func__,
			     job_desc_msg->job_id_str, uid, TIME_STR);
		} else {
			info("%s: complete JobId=%u uid=%d %s", __func__,
			     job_desc_msg->job_id, uid, TIME_STR);
		}
		/* Below functions provide their own locking */
		schedule_job_save();
		schedule_node_save();
		queue_job_scheduler();
	}
}

/*
 * slurm_drain_nodes - process a request to drain a list of nodes,
 *	no-op for nodes already drained or draining
 * node_list IN - list of nodes to drain
 * reason IN - reason to drain the nodes
 * reason_uid IN - who set the reason
 * RET SLURM_SUCCESS or error code
 * NOTE: This is utilzed by plugins and not via RPC and it sets its
 *	own locks.
 */
extern int slurm_drain_nodes(char *node_list, char *reason, uint32_t reason_uid)
{
	int error_code;
	DEF_TIMERS;
	/* Locks: Write  node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(node_write_lock);
	error_code = drain_nodes(node_list, reason, reason_uid);
	unlock_slurmctld(node_write_lock);
	END_TIMER2("slurm_drain_nodes");

	return error_code;
}

/*
 * _slurm_rpc_update_front_end - process RPC to update the configuration of a
 *	front_end node (e.g. UP/DOWN)
 */
static void _slurm_rpc_update_front_end(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_front_end_msg_t *update_front_end_msg_ptr =
		(update_front_end_msg_t *) msg->data;
	/* Locks: write node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_UPDATE_FRONT_END from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_FRONT_END RPC from uid=%d",
		      uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = update_front_end(update_front_end_msg_ptr);
		unlock_slurmctld(node_write_lock);
		END_TIMER2("_slurm_rpc_update_front_end");
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_update_front_end for %s: %s",
		     update_front_end_msg_ptr->name,
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_update_front_end complete for %s %s",
		       update_front_end_msg_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

/*
 * _slurm_rpc_update_node - process RPC to update the configuration of a
 *	node (e.g. UP/DOWN)
 */
static void _slurm_rpc_update_node(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_node_msg_t *update_node_msg_ptr =
		(update_node_msg_t *) msg->data;
	/* Locks: Write job, partition and node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_UPDATE_NODE from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_NODE RPC from uid=%d", uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = update_node(update_node_msg_ptr);
		unlock_slurmctld(node_write_lock);
		END_TIMER2("_slurm_rpc_update_node");
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_update_node for %s: %s",
		     update_node_msg_ptr->node_names,
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_update_node complete for %s %s",
		       update_node_msg_ptr->node_names, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

	/* Below functions provide their own locks */
	schedule_node_save();
	queue_job_scheduler();
	trigger_reconfig();
}

/*
 * _slurm_rpc_update_layout - process RPC to update the configuration of a
 *	layout (e.g. params of entities)
 */
static void _slurm_rpc_update_layout(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	Buf buffer;
	DEF_TIMERS;
	update_layout_msg_t *msg_ptr = (update_layout_msg_t *) msg->data;
	int shrink_size;

	/* Locks: Write job and write node */
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_UPDATE_LAYOUT from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_LAYOUT RPC from uid=%d", uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		buffer = init_buf(BUF_SIZE);
		packstr(msg_ptr->arg, buffer);
		shrink_size = (int)get_buf_offset(buffer) - size_buf(buffer);
		set_buf_offset(buffer, 0);
		grow_buf(buffer, shrink_size);	/* Shrink actually */
		error_code = layouts_update_layout(msg_ptr->layout, buffer);
		free_buf(buffer);
		END_TIMER2("_slurm_rpc_update_node");
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_update_layout for %s: %s",
		     msg_ptr->layout, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_update_layout complete for %s %s",
		       msg_ptr->layout, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

/* _slurm_rpc_update_partition - process RPC to update the configuration
 *	of a partition (e.g. UP/DOWN) */
static void _slurm_rpc_update_partition(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_part_msg_t *part_desc_ptr = (update_part_msg_t *) msg->data;
	/* Locks: Read config, write job, read node, write partition
	 * NOTE: job write lock due to gang scheduler support */
	slurmctld_lock_t part_write_lock = {
		READ_LOCK, WRITE_LOCK, READ_LOCK, WRITE_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_UPDATE_PARTITION from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_PARTITION RPC from uid=%d",
		      uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		if (msg->msg_type == REQUEST_CREATE_PARTITION) {
			lock_slurmctld(part_write_lock);
			error_code = update_part(part_desc_ptr, true);
			unlock_slurmctld(part_write_lock);
		} else {
			lock_slurmctld(part_write_lock);
			error_code = update_part(part_desc_ptr, false);
			unlock_slurmctld(part_write_lock);
		}
		END_TIMER2("_slurm_rpc_update_partition");
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_update_partition partition=%s: %s",
		     part_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_update_partition complete for %s %s",
		       part_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		schedule_part_save();		/* Has its locking */
		queue_job_scheduler();
	}
}

/* _slurm_rpc_update_powercap - process RPC to update the powercap */
static void _slurm_rpc_update_powercap(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	bool valid_cap = false;
	uint32_t min, max, orig_cap;
	update_powercap_msg_t *ptr = (update_powercap_msg_t *) msg->data;

	/* Locks: write configuration, read node */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_UPDATE_POWERCAP from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_POWERCAP RPC from uid=%d",
		      uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(config_write_lock);
		if (ptr->power_cap == 0 ||
		    ptr->power_cap == INFINITE) {
			valid_cap = true;
		} else if (!power_layout_ready()) {
			/* Not using layouts/power framework */
			valid_cap = true;
		} else {
			/* we need to set a cap if 
			 * the current value is 0 in order to
			 * enable the capping system and get 
			 * the min and max values */
			orig_cap = powercap_get_cluster_current_cap();
			powercap_set_cluster_cap(INFINITE);
			min = powercap_get_cluster_min_watts();
			max = powercap_get_cluster_max_watts();
			if (min <= ptr->power_cap && max >= ptr->power_cap)
				valid_cap = true;
			else
				powercap_set_cluster_cap(orig_cap);
		}
		if (valid_cap)
			powercap_set_cluster_cap(ptr->power_cap);
		else
			error_code = ESLURM_INVALID_POWERCAP;
		unlock_slurmctld(config_write_lock);
		END_TIMER2("_slurm_rpc_update_powercap");
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_update_powercap: %s",
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_update_powercap complete %s", TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		/* NOTE: These functions provide their own locks */
		if (!LOTS_OF_AGENTS)
			schedule(0);	/* Has own locking */
		save_all_state();	/* Has own locking */
	}
}

/* _slurm_rpc_delete_partition - process RPC to delete a partition */
static void _slurm_rpc_delete_partition(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	delete_part_msg_t *part_desc_ptr = (delete_part_msg_t *) msg->data;
	/* Locks: write job, read node, write partition */
	slurmctld_lock_t part_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, WRITE_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_DELETE_PARTITION from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_PARTITION RPC from uid=%d",
		      uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(part_write_lock);
		error_code = delete_partition(part_desc_ptr);
		unlock_slurmctld(part_write_lock);
		END_TIMER2("_slurm_rpc_delete_partition");
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_delete_partition partition=%s: %s",
		     part_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("_slurm_rpc_delete_partition complete for %s %s",
		     part_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		save_all_state();	/* Has own locking */
		queue_job_scheduler();
	}
}

/* _slurm_rpc_resv_create - process RPC to create a reservation */
static void _slurm_rpc_resv_create(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	resv_desc_msg_t *resv_desc_ptr = (resv_desc_msg_t *)
		msg->data;
	/* Locks: read config, read job, write node, read partition */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_CREATE_RESERVATION from uid=%d", uid);
	if (!validate_operator(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, CREATE_RESERVATION RPC from uid=%d",
		      uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = create_resv(resv_desc_ptr);
		unlock_slurmctld(node_write_lock);
		END_TIMER2("_slurm_rpc_resv_create");
	}

	/* return result */
	if (error_code) {
		if (resv_desc_ptr->name) {
			info("_slurm_rpc_resv_create reservation=%s: %s",
			     resv_desc_ptr->name, slurm_strerror(error_code));
		} else {
			info("_slurm_rpc_resv_create: %s",
			     slurm_strerror(error_code));
		}
		slurm_send_rc_msg(msg, error_code);
	} else {
		slurm_msg_t response_msg;
		reservation_name_msg_t resv_resp_msg;

		debug2("_slurm_rpc_resv_create complete for %s %s",
		       resv_desc_ptr->name, TIME_STR);
		/* send reservation name */
		response_init(&response_msg, msg);
		memset(&resv_resp_msg, 0, sizeof(resv_resp_msg));
		resv_resp_msg.name    = resv_desc_ptr->name;
		response_msg.msg_type = RESPONSE_CREATE_RESERVATION;
		response_msg.data     = &resv_resp_msg;
		slurm_send_node_msg(msg->conn_fd, &response_msg);

		queue_job_scheduler();
	}
}

/* _slurm_rpc_resv_update - process RPC to update a reservation */
static void _slurm_rpc_resv_update(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	resv_desc_msg_t *resv_desc_ptr = (resv_desc_msg_t *)
		msg->data;
	/* Locks: read config, read job, write node, read partition */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_UPDATE_RESERVATION from uid=%d", uid);
	if (!validate_operator(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_RESERVATION RPC from uid=%d",
		      uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = update_resv(resv_desc_ptr);
		unlock_slurmctld(node_write_lock);
		END_TIMER2("_slurm_rpc_resv_update");
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_resv_update reservation=%s: %s",
		     resv_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_resv_update complete for %s %s",
		       resv_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		queue_job_scheduler();
	}
}

/* _slurm_rpc_resv_delete - process RPC to delete a reservation */
static void _slurm_rpc_resv_delete(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	reservation_name_msg_t *resv_desc_ptr = (reservation_name_msg_t *)
		msg->data;
	/* Locks: write job, write node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_DELETE_RESERVATION from uid=%d", uid);
	if (!validate_operator(uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_RESERVATION RPC from uid=%d",
		      uid);
	} else if (!resv_desc_ptr->name) {
		error_code = ESLURM_INVALID_PARTITION_NAME;
		error("Invalid DELETE_RESERVATION RPC from uid=%d, name is null",
		      uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = delete_resv(resv_desc_ptr);
		unlock_slurmctld(node_write_lock);
		END_TIMER2("_slurm_rpc_resv_delete");
	}

	/* return result */
	if (error_code) {
		info("_slurm_rpc_delete_reservation reservation=%s: %s",
		     resv_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("_slurm_rpc_delete_reservation complete for %s %s",
		     resv_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		queue_job_scheduler();
	}
}

/* _slurm_rpc_resv_show - process RPC to dump reservation info */
static void _slurm_rpc_resv_show(slurm_msg_t * msg)
{
	resv_info_request_msg_t *resv_req_msg = (resv_info_request_msg_t *)
		msg->data;
	DEF_TIMERS;
	/* Locks: read node */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	slurm_msg_t response_msg;
	char *dump;
	int dump_size;

	START_TIMER;
	debug2("Processing RPC: REQUEST_RESERVATION_INFO from uid=%d", uid);
	if ((resv_req_msg->last_update - 1) >= last_resv_update) {
		debug2("_slurm_rpc_resv_show, no change");
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		lock_slurmctld(node_read_lock);
		show_resv(&dump, &dump_size, uid, msg->protocol_version);
		unlock_slurmctld(node_read_lock);
		END_TIMER2("_slurm_rpc_resv_show");

		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_RESERVATION_INFO;
		response_msg.data = dump;
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_layout_show - process RPC to dump layout info */
static void _slurm_rpc_layout_show(slurm_msg_t * msg)
{
	layout_info_request_msg_t *layout_req_msg = (layout_info_request_msg_t *)
		msg->data;
	DEF_TIMERS;
	slurm_msg_t response_msg;
	char *dump;
	int dump_size;
	static int high_buffer_size = (1024 * 1024);
	Buf buffer = init_buf(high_buffer_size);

	START_TIMER;
	debug2("Processing RPC: REQUEST_LAYOUT_INFO");
	if (layout_req_msg->layout_type == NULL) {
		dump = slurm_get_layouts();
		pack32((uint32_t) 2, buffer);	/* 1 record + trailing \n */
		packstr(dump, buffer);
		packstr("\n", buffer); /* to be consistent with
					* layouts internal prints */
		xfree(dump);
	} else {
		if ( layouts_pack_layout(layout_req_msg->layout_type,
					 layout_req_msg->entities,
					 layout_req_msg->type,
					 layout_req_msg->flags,
					 buffer) != SLURM_SUCCESS) {
			debug2("%s: unable to get layout[%s]",
			       __func__, layout_req_msg->layout_type);
			slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
			free_buf(buffer);
			return;
		}
	}

	dump_size = get_buf_offset(buffer);
	high_buffer_size = MAX(high_buffer_size, dump_size);
	dump = xfer_buf_data(buffer);
	END_TIMER2("_slurm_rpc_resv_show");

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_LAYOUT_INFO;
	response_msg.data = dump;
	response_msg.data_size = dump_size;

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* determine of nodes are ready for the job */
static void _slurm_rpc_job_ready(slurm_msg_t * msg)
{
	int error_code, result;
	job_id_msg_t *id_msg = (job_id_msg_t *) msg->data;
	DEF_TIMERS;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	slurm_msg_t response_msg;
	return_code_msg_t rc_msg;

	START_TIMER;
	lock_slurmctld(job_read_lock);
	error_code = job_node_ready(id_msg->job_id, &result);
	unlock_slurmctld(job_read_lock);
	END_TIMER2("_slurm_rpc_job_ready");

	if (error_code) {
		debug2("_slurm_rpc_job_ready: %s",
		       slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("_slurm_rpc_job_ready(%u)=%d %s", id_msg->job_id,
		       result, TIME_STR);
		response_init(&response_msg, msg);
		memset(&rc_msg, 0, sizeof(rc_msg));
		rc_msg.return_code = result;
		response_msg.data = &rc_msg;
		if(_is_prolog_finished(id_msg->job_id)) {
			response_msg.msg_type = RESPONSE_JOB_READY;
		} else {
			response_msg.msg_type = RESPONSE_PROLOG_EXECUTING;
		}
		slurm_send_node_msg(msg->conn_fd, &response_msg);
	}
}

/* Check if prolog has already finished */
static int _is_prolog_finished(uint32_t job_id) {
	int is_running = 0;
	struct job_record  *job_ptr;

	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	lock_slurmctld(job_read_lock);
	job_ptr = find_job_record(job_id);
	if (job_ptr) {
		is_running = (job_ptr->state_reason != WAIT_PROLOG);
	}
	unlock_slurmctld(job_read_lock);
	return is_running;
}

/* get node select info plugin */
static void  _slurm_rpc_burst_buffer_info(slurm_msg_t * msg)
{
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	void *resp_buffer = NULL;
	int resp_buffer_size = 0;
	int error_code = SLURM_SUCCESS;
	Buf buffer;
	DEF_TIMERS;

	START_TIMER;
	debug2("Processing RPC: REQUEST_BURST_BUFFER_INFO from uid=%d", uid);

	buffer = init_buf(BUF_SIZE);
	if (validate_super_user(uid))
		uid = 0;
	error_code = bb_g_state_pack(uid, buffer, msg->protocol_version);
	END_TIMER2(__func__);

	if (error_code) {
		debug("_slurm_rpc_burst_buffer_info: %s",
		       slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		slurm_msg_t response_msg;

		resp_buffer_size = get_buf_offset(buffer);
		resp_buffer = xfer_buf_data(buffer);
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONSE_BURST_BUFFER_INFO;
		response_msg.data = resp_buffer;
		response_msg.data_size = resp_buffer_size;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(resp_buffer);
	}
}

/* Reset the job credential key based upon configuration parameters.
 * NOTE: READ lock_slurmctld config before entry */
static void _update_cred_key(void)
{
	slurm_cred_ctx_key_update(slurmctld_config.cred_ctx,
				  slurmctld_conf.job_credential_private_key);
}

inline static void _slurm_rpc_suspend(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	suspend_msg_t *sus_ptr = (suspend_msg_t *) msg->data;
	/* Locks: write job and node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	struct job_record *job_ptr;
	char *op;

	START_TIMER;
	switch (sus_ptr->op) {
	case SUSPEND_JOB:
		op = "suspend";
		break;
	case RESUME_JOB:
		op = "resume";
		break;
	default:
		op = "unknown";
	}
	info("Processing RPC: REQUEST_SUSPEND(%s) from uid=%u",
	     op, (unsigned int) uid);

	/* Get the job id part of the jobid. It could be an array id. Currently
	 * in a federation, job arrays only run on the origin cluster so we just
	 * want to find if the array, not a specific task, is on the origin
	 * cluster. */
	if ((sus_ptr->job_id == NO_VAL) && sus_ptr->job_id_str)
		sus_ptr->job_id = strtol(sus_ptr->job_id_str, NULL, 10);

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(sus_ptr->job_id);

	/* If job is found on the cluster, it could be pending, the origin
	 * cluster, or running on the sibling cluster. If it's not there then
	 * route it to the origin, otherwise try to suspend the job. If it's
	 * pending an error should be returned. If it's running then it should
	 * suspend the job. */
	if (!job_ptr &&
	    !_route_msg_to_origin(msg, NULL, sus_ptr->job_id, uid)) {
		unlock_slurmctld(job_write_lock);
		return;
	}
	if (!job_ptr)
		error_code = ESLURM_INVALID_JOB_ID;
	else if (fed_mgr_cluster_rec && job_ptr->fed_details &&
		 fed_mgr_is_origin_job(job_ptr) &&
		 IS_JOB_REVOKED(job_ptr) &&
		 job_ptr->fed_details->cluster_lock &&
		 (job_ptr->fed_details->cluster_lock !=
		  fed_mgr_cluster_rec->fed.id)) {

		/* Route to the cluster that is running the job. */
		slurmdb_cluster_rec_t *dst =
			fed_mgr_get_cluster_by_id(
					job_ptr->fed_details->cluster_lock);
		if (dst) {
			slurm_send_reroute_msg(msg, dst);
			info("%s: %s %pJ uid %d routed to %s",
			     __func__, rpc_num2string(msg->msg_type),
			     job_ptr, uid, dst->name);

			unlock_slurmctld(job_write_lock);
			END_TIMER2("_slurm_rpc_suspend");
			return;
		}

		error("couldn't find cluster by cluster id %d",
		      job_ptr->fed_details->cluster_lock);
		error_code = ESLURM_INVALID_CLUSTER_NAME;
	} else if (sus_ptr->job_id_str) {
		error_code = job_suspend2(sus_ptr, uid, msg->conn_fd, true,
					  msg->protocol_version);
	} else {
		error_code = job_suspend(sus_ptr, uid, msg->conn_fd, true,
					 msg->protocol_version);
	}
	unlock_slurmctld(job_write_lock);
	END_TIMER2("_slurm_rpc_suspend");

	if (!sus_ptr->job_id_str)
		xstrfmtcat(sus_ptr->job_id_str, "%u", sus_ptr->job_id);

	if (error_code) {
		info("_slurm_rpc_suspend(%s) for %s %s", op,
		     sus_ptr->job_id_str, slurm_strerror(error_code));
	} else {
		info("_slurm_rpc_suspend(%s) for %s %s", op,
		     sus_ptr->job_id_str, TIME_STR);

		schedule_job_save();	/* Has own locking */
		if (sus_ptr->op == SUSPEND_JOB)
			queue_job_scheduler();
	}
}

inline static void _slurm_rpc_top_job(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	top_job_msg_t *top_ptr = (top_job_msg_t *) msg->data;
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	debug("Processing RPC: REQUEST_TOP_JOB from uid=%u", (unsigned int)uid);

	START_TIMER;
	lock_slurmctld(job_write_lock);
	error_code = job_set_top(top_ptr, uid, msg->conn_fd,
				 msg->protocol_version);
	unlock_slurmctld(job_write_lock);
	END_TIMER2("_slurm_rpc_top");

	if (error_code) {
		info("%s for %s %s",
		     __func__, top_ptr->job_id_str, slurm_strerror(error_code));
	} else {
		info("%s for %s %s",
		     __func__, top_ptr->job_id_str, TIME_STR);
	}
}

inline static void _slurm_rpc_requeue(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	requeue_msg_t *req_ptr = (requeue_msg_t *)msg->data;
	/* Locks: write job and node */
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	lock_slurmctld(fed_read_lock);
	if (!_route_msg_to_origin(msg, req_ptr->job_id_str, req_ptr->job_id,
				  uid)) {
		unlock_slurmctld(fed_read_lock);
		return;
	}
	unlock_slurmctld(fed_read_lock);

	START_TIMER;

	info("%s: Processing RPC: REQUEST_JOB_REQUEUE from uid=%d", __func__,
	     uid);

	lock_slurmctld(job_write_lock);
	if (req_ptr->job_id_str) {
		error_code = job_requeue2(uid, req_ptr, msg, false);
	} else {
		error_code = job_requeue(uid, req_ptr->job_id, msg, false,
					 req_ptr->flags);
	}
	unlock_slurmctld(job_write_lock);
	END_TIMER2("_slurm_rpc_requeue");

	if (error_code) {
		if (!req_ptr->job_id_str)
			xstrfmtcat(req_ptr->job_id_str, "%u", req_ptr->job_id);

		info("%s: %s: %s", __func__, req_ptr->job_id_str,
		     slurm_strerror(error_code));
	}

	/* Functions below provide their own locking
	 */
	schedule_job_save();
}

/* Assorted checkpoint operations */
inline static void  _slurm_rpc_checkpoint(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	checkpoint_msg_t *ckpt_ptr = (checkpoint_msg_t *) msg->data;
	/* Locks: write job lock, read node lock */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	char *op;

	START_TIMER;
	switch (ckpt_ptr->op) {
	case CHECK_ABLE:
		op = "able";
		break;
	case CHECK_CREATE:
		op = "create";
		break;
	case CHECK_DISABLE:
		op = "disable";
		break;
	case CHECK_ENABLE:
		op = "enable";
		break;
	case CHECK_ERROR:
		op = "error";
		break;
	case CHECK_REQUEUE:
		op = "requeue";
		break;
	case CHECK_RESTART:
		op = "restart";
		break;
	case CHECK_VACATE:
		op = "vacate";
		break;
	default:
		op = "unknown";
	}
	debug2("Processing RPC: REQUEST_CHECKPOINT(%s) from uid=%u",
	       op, (unsigned int) uid);

	/* do RPC call and send reply */
	lock_slurmctld(job_write_lock);
	if (ckpt_ptr->op == CHECK_RESTART) {
		error_code = job_restart(ckpt_ptr, uid, msg->conn_fd,
					 msg->protocol_version);
	} else if (ckpt_ptr->step_id == SLURM_BATCH_SCRIPT) {
		error_code = job_checkpoint(ckpt_ptr, uid, msg->conn_fd,
					    msg->protocol_version);
	} else {
		error_code = job_step_checkpoint(ckpt_ptr, uid, msg->conn_fd,
						 msg->protocol_version);
	}
	unlock_slurmctld(job_write_lock);
	END_TIMER2("_slurm_rpc_checkpoint");

	if (error_code) {
		if (ckpt_ptr->step_id == SLURM_BATCH_SCRIPT) {
			info("_slurm_rpc_checkpoint %s %u: %s", op,
			     ckpt_ptr->job_id, slurm_strerror(error_code));
		} else {
			info("_slurm_rpc_checkpoint %s JobId=%u StepId=%u: %s",
			     op, ckpt_ptr->job_id, ckpt_ptr->step_id,
			     slurm_strerror(error_code));
		}
	} else {
		if (ckpt_ptr->step_id == SLURM_BATCH_SCRIPT) {
			info("_slurm_rpc_checkpoint %s for JobId=%u %s",
			     op, ckpt_ptr->job_id, TIME_STR);
		} else {
			info("_slurm_rpc_checkpoint %s for JobId=%u StepId=%u %s",
			     op, ckpt_ptr->job_id, ckpt_ptr->step_id, TIME_STR);
		}
		if ((ckpt_ptr->op != CHECK_ABLE) &&
		    (ckpt_ptr->op != CHECK_ERROR)) {
			/* job state changed, save it */
			/* NOTE: This function provides it own locks */
			schedule_job_save();
		}
	}
}

inline static void  _slurm_rpc_checkpoint_comp(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	checkpoint_comp_msg_t *ckpt_ptr = (checkpoint_comp_msg_t *) msg->data;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("Processing RPC: REQUEST_CHECKPOINT_COMP from uid=%d", uid);

	/* do RPC call and send reply */
	lock_slurmctld(job_read_lock);
	error_code = job_step_checkpoint_comp(ckpt_ptr, uid, msg->conn_fd,
					      msg->protocol_version);
	unlock_slurmctld(job_read_lock);
	END_TIMER2("_slurm_rpc_checkpoint_comp");

	if (error_code) {
		info("_slurm_rpc_checkpoint_comp JobId=%u StepId=%u: %s",
		     ckpt_ptr->job_id, ckpt_ptr->step_id,
		     slurm_strerror(error_code));
	} else {
		info("_slurm_rpc_checkpoint_comp JobId=%u StepId=%u %s",
		     ckpt_ptr->job_id, ckpt_ptr->step_id, TIME_STR);
	}
}

inline static void  _slurm_rpc_checkpoint_task_comp(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	checkpoint_task_comp_msg_t *ckpt_ptr;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	ckpt_ptr = (checkpoint_task_comp_msg_t *) msg->data;
	START_TIMER;
	debug2("Processing RPC: REQUEST_CHECKPOINT_TASK_COMP from uid=%d", uid);

	/* do RPC call and send reply */
	lock_slurmctld(job_read_lock);
	error_code = job_step_checkpoint_task_comp(ckpt_ptr, uid, msg->conn_fd,
						   msg->protocol_version);
	unlock_slurmctld(job_read_lock);
	END_TIMER2("_slurm_rpc_checkpoint_task_comp");

	if (error_code) {
		info("_slurm_rpc_checkpoint_task_comp JobId=%u StepId=%u: %s",
		     ckpt_ptr->job_id, ckpt_ptr->step_id,
		     slurm_strerror(error_code));
	} else {
		info("_slurm_rpc_checkpoint_task_comp JobId=%u StepId=%u %s",
		     ckpt_ptr->job_id, ckpt_ptr->step_id, TIME_STR);
	}
}

/* Copy an array of type char **, xmalloc() the array and xstrdup() the
 * strings in the array */
extern char **
xduparray(uint32_t size, char ** array)
{
	int i;
	char ** result;

	if (size == 0)
		return (char **)NULL;

	result = xcalloc(size, sizeof(char *));
	for (i=0; i<size; i++)
		result[i] = xstrdup(array[i]);

	return result;
}

inline static void  _slurm_rpc_trigger_clear(slurm_msg_t * msg)
{
	int rc;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	trigger_info_msg_t * trigger_ptr = (trigger_info_msg_t *) msg->data;
	DEF_TIMERS;

	START_TIMER;
	debug("Processing RPC: REQUEST_TRIGGER_CLEAR from uid=%d", uid);

	rc = trigger_clear(uid, trigger_ptr);
	END_TIMER2("_slurm_rpc_trigger_clear");

	slurm_send_rc_msg(msg, rc);
}

inline static void  _slurm_rpc_trigger_get(slurm_msg_t * msg)
{
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	trigger_info_msg_t *resp_data;
	trigger_info_msg_t * trigger_ptr = (trigger_info_msg_t *) msg->data;
	slurm_msg_t response_msg;
	DEF_TIMERS;

	START_TIMER;
	debug("Processing RPC: REQUEST_TRIGGER_GET from uid=%d", uid);

	resp_data = trigger_get(uid, trigger_ptr);
	END_TIMER2("_slurm_rpc_trigger_get");

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_TRIGGER_GET;
	response_msg.data     = resp_data;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	slurm_free_trigger_msg(resp_data);
}

inline static void  _slurm_rpc_trigger_set(slurm_msg_t * msg)
{
	int rc;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	gid_t gid = g_slurm_auth_get_gid(msg->auth_cred);
	trigger_info_msg_t * trigger_ptr = (trigger_info_msg_t *) msg->data;
	bool allow_user_triggers = xstrcasestr(slurmctld_conf.slurmctld_params,
					       "allow_user_triggers");
	DEF_TIMERS;

	START_TIMER;
	debug("Processing RPC: REQUEST_TRIGGER_SET from uid=%d", uid);
	if (validate_slurm_user(uid) || allow_user_triggers) {
		rc = trigger_set(uid, gid, trigger_ptr);
	} else {
		rc = ESLURM_ACCESS_DENIED;
		error("Security violation, REQUEST_TRIGGER_SET RPC from uid=%d",
		      uid);
	}
	END_TIMER2("_slurm_rpc_trigger_set");

	slurm_send_rc_msg(msg, rc);
}

inline static void  _slurm_rpc_trigger_pull(slurm_msg_t * msg)
{
	int rc;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	trigger_info_msg_t * trigger_ptr = (trigger_info_msg_t *) msg->data;
	DEF_TIMERS;

	START_TIMER;

	/* NOTE: No locking required here, trigger_pull only needs to lock
	 * it's own internal trigger structure */
	debug("Processing RPC: REQUEST_TRIGGER_PULL from uid=%u",
	      (unsigned int) uid);
	if (!validate_slurm_user(uid)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_TRIGGER_PULL RPC "
		      "from uid=%d",
		      uid);
	} else
		rc = trigger_pull(trigger_ptr);
	END_TIMER2("_slurm_rpc_trigger_pull");

	slurm_send_rc_msg(msg, rc);
}

inline static void  _slurm_rpc_get_topo(slurm_msg_t * msg)
{
	topo_info_response_msg_t *topo_resp_msg;
	slurm_msg_t response_msg;
	int i;
	/* Locks: read node lock */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	DEF_TIMERS;

	START_TIMER;
	lock_slurmctld(node_read_lock);
	topo_resp_msg = xmalloc(sizeof(topo_info_response_msg_t));
	topo_resp_msg->record_count = switch_record_cnt;
	topo_resp_msg->topo_array = xmalloc(sizeof(topo_info_t) *
					    topo_resp_msg->record_count);
	for (i=0; i<topo_resp_msg->record_count; i++) {
		topo_resp_msg->topo_array[i].level      =
			switch_record_table[i].level;
		topo_resp_msg->topo_array[i].link_speed =
			switch_record_table[i].link_speed;
		topo_resp_msg->topo_array[i].name       =
			xstrdup(switch_record_table[i].name);
		topo_resp_msg->topo_array[i].nodes      =
			xstrdup(switch_record_table[i].nodes);
		topo_resp_msg->topo_array[i].switches   =
			xstrdup(switch_record_table[i].switches);
	}
	unlock_slurmctld(node_read_lock);
	END_TIMER2("_slurm_rpc_get_topo");

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_TOPO_INFO;
	response_msg.data     = topo_resp_msg;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	slurm_free_topo_info_msg(topo_resp_msg);
}

inline static void  _slurm_rpc_get_powercap(slurm_msg_t * msg)
{
	powercap_info_msg_t *powercap_resp_msg, *ptr;
	slurm_msg_t response_msg;
	/* Locks: read config lock */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	DEF_TIMERS;

	START_TIMER;
	lock_slurmctld(config_read_lock);
	powercap_resp_msg = xmalloc(sizeof(powercap_info_msg_t));
	ptr = powercap_resp_msg;
	ptr->power_cap = powercap_get_cluster_current_cap();
	ptr->min_watts = powercap_get_cluster_min_watts();
	ptr->cur_max_watts = powercap_get_cluster_current_max_watts();
	ptr->adj_max_watts = powercap_get_cluster_adjusted_max_watts();
	ptr->max_watts = powercap_get_cluster_max_watts();
	unlock_slurmctld(config_read_lock);
	END_TIMER2("_slurm_rpc_get_powercap");

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_POWERCAP_INFO;
	response_msg.data     = powercap_resp_msg;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	slurm_free_powercap_info_msg(powercap_resp_msg);
}

inline static void  _slurm_rpc_job_notify(slurm_msg_t * msg)
{
	int error_code;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	job_notify_msg_t * notify_msg = (job_notify_msg_t *) msg->data;
	struct job_record *job_ptr;
	DEF_TIMERS;

	START_TIMER;
	debug("Processing RPC: REQUEST_JOB_NOTIFY from uid=%d", uid);

	/* do RPC call */
	lock_slurmctld(job_read_lock);
	job_ptr = find_job_record(notify_msg->job_id);

	/* If job is found on the cluster, it could be pending, the origin
	 * cluster, or running on the sibling cluster. If it's not there then
	 * route it to the origin. */
	if (!job_ptr &&
	    !_route_msg_to_origin(msg, NULL, notify_msg->job_id, uid)) {
		unlock_slurmctld(job_read_lock);
		return;
	}

	if (!job_ptr)
		error_code = ESLURM_INVALID_JOB_ID;
	else if (job_ptr->batch_flag &&
		 fed_mgr_cluster_rec && job_ptr->fed_details &&
		 fed_mgr_is_origin_job(job_ptr) &&
		 IS_JOB_REVOKED(job_ptr) &&
		 job_ptr->fed_details->cluster_lock &&
		 (job_ptr->fed_details->cluster_lock !=
		  fed_mgr_cluster_rec->fed.id)) {

		/* Route to the cluster that is running the batch job. srun jobs
		 * don't need to be routed to the running cluster since the
		 * origin cluster knows how to contact the listening srun. */
		slurmdb_cluster_rec_t *dst =
			fed_mgr_get_cluster_by_id(
					job_ptr->fed_details->cluster_lock);
		if (dst) {
			slurm_send_reroute_msg(msg, dst);
			info("%s: %s %pJ uid %d routed to %s",
			     __func__, rpc_num2string(msg->msg_type),
			     job_ptr, uid, dst->name);

			unlock_slurmctld(job_read_lock);
			END_TIMER2("_slurm_rpc_job_notify");
			return;
		}

		error("couldn't find cluster by cluster id %d",
		      job_ptr->fed_details->cluster_lock);
		error_code = ESLURM_INVALID_CLUSTER_NAME;

	} else if ((job_ptr->user_id == uid) || validate_slurm_user(uid))
		error_code = srun_user_message(job_ptr, notify_msg->message);
	else {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_JOB_NOTIFY RPC "
		      "from uid=%d for %pJ owner %d",
		      uid, job_ptr, job_ptr->user_id);
	}
	unlock_slurmctld(job_read_lock);

	END_TIMER2("_slurm_rpc_job_notify");
	slurm_send_rc_msg(msg, error_code);
}

inline static void  _slurm_rpc_set_debug_flags(slurm_msg_t *msg)
{
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	set_debug_flags_msg_t *request_msg =
		(set_debug_flags_msg_t *) msg->data;
	uint64_t debug_flags;
	char *flag_string;

	debug2("Processing RPC: REQUEST_SET_DEBUG_FLAGS from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error("set debug flags request from non-super user uid=%d",
		      uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	lock_slurmctld (config_write_lock);
	debug_flags  = slurmctld_conf.debug_flags;
	debug_flags &= (~request_msg->debug_flags_minus);
	debug_flags |= request_msg->debug_flags_plus;
	slurm_set_debug_flags(debug_flags);
	slurmctld_conf.last_update = time(NULL);

	/* Reset cached debug_flags values */
	log_set_debug_flags();
	gs_reconfig();
	gres_plugin_reconfig();
	acct_storage_g_reconfig(acct_db_conn, 0);
	priority_g_reconfig(false);
	select_g_reconfigure();
	(void) slurm_sched_g_reconfig();
	(void) switch_g_reconfig();

	unlock_slurmctld (config_write_lock);
	flag_string = debug_flags2str(debug_flags);
	info("Set DebugFlags to %s", flag_string ? flag_string : "none");
	xfree(flag_string);
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

inline static void  _slurm_rpc_set_debug_level(slurm_msg_t *msg)
{
	int debug_level;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	set_debug_level_msg_t *request_msg =
		(set_debug_level_msg_t *) msg->data;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	debug2("Processing RPC: REQUEST_SET_DEBUG_LEVEL from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error("set debug level request from non-super user uid=%d",
		      uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	/* NOTE: not offset by LOG_LEVEL_INFO, since it's inconvenient
	 * to provide negative values for scontrol */
	debug_level = MIN (request_msg->debug_level, (LOG_LEVEL_END - 1));
	debug_level = MAX (debug_level, LOG_LEVEL_QUIET);

	lock_slurmctld (config_write_lock);
	if (slurmctld_config.daemonize) {
		log_opts.stderr_level = LOG_LEVEL_QUIET;
		if (slurmctld_conf.slurmctld_logfile) {
			log_opts.logfile_level = debug_level;
			log_opts.syslog_level = LOG_LEVEL_QUIET;
		} else {
			log_opts.syslog_level = debug_level;
			log_opts.logfile_level = LOG_LEVEL_QUIET;
		}
	} else {
		log_opts.syslog_level = LOG_LEVEL_QUIET;
		log_opts.stderr_level = debug_level;
		if (slurmctld_conf.slurmctld_logfile)
			log_opts.logfile_level = debug_level;
		else
			log_opts.logfile_level = LOG_LEVEL_QUIET;
	}
	log_alter(log_opts, LOG_DAEMON, slurmctld_conf.slurmctld_logfile);
	unlock_slurmctld (config_write_lock);

	if (debug_level != slurmctld_conf.slurmctld_debug)
		info("Set debug level to %d", debug_level);

	slurmctld_conf.slurmctld_debug = debug_level;
	slurmctld_conf.last_update = time(NULL);

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

inline static void  _slurm_rpc_set_schedlog_level(slurm_msg_t *msg)
{
	int schedlog_level;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	slurmctld_lock_t config_read_lock =
		{ READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	set_debug_level_msg_t *request_msg =
		(set_debug_level_msg_t *) msg->data;
	log_options_t log_opts = SCHEDLOG_OPTS_INITIALIZER;

	debug2("Processing RPC: REQUEST_SET_SCHEDLOG_LEVEL from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error("set scheduler log level request from non-super user "
		      "uid=%d", uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	/*
	 * If slurmctld_conf.sched_logfile is NULL, then this operation
	 *  will fail, since there is no sched logfile for which to alter
	 *  the log level. (Calling sched_log_alter with a NULL filename
	 *  is likely to cause a segfault at the next sched log call)
	 *  So just give up and return "Operation Disabled"
	 */
	if (slurmctld_conf.sched_logfile == NULL) {
		error("set scheduler log level failed: no log file!");
		slurm_send_rc_msg (msg, ESLURM_DISABLED);
		return;
	}

	schedlog_level = MIN (request_msg->debug_level, (LOG_LEVEL_QUIET + 1));
	schedlog_level = MAX (schedlog_level, LOG_LEVEL_QUIET);

	lock_slurmctld (config_read_lock);
	log_opts.logfile_level = schedlog_level;
	sched_log_alter(log_opts, LOG_DAEMON, slurmctld_conf.sched_logfile);
	unlock_slurmctld (config_read_lock);

	if (schedlog_level != slurmctld_conf.sched_log_level)
		sched_info("Set scheduler log level to %d", schedlog_level);

	slurmctld_conf.sched_log_level = schedlog_level;
	slurmctld_conf.last_update = time(NULL);

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static int _find_update_object_in_list(void *x, void *key)
{
	slurmdb_update_object_t *object = (slurmdb_update_object_t *)x;
	slurmdb_update_type_t type = *(slurmdb_update_type_t *)key;

	if (object->type == type)
		return 1;

	return 0;
}

inline static void  _slurm_rpc_accounting_update_msg(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int rc = SLURM_SUCCESS;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	accounting_update_msg_t *update_ptr =
		(accounting_update_msg_t *) msg->data;
	DEF_TIMERS;

	START_TIMER;
	debug2("Processing RPC: ACCOUNTING_UPDATE_MSG from uid=%d", uid);

	if (!validate_super_user(uid)) {
		error("Update Association request from non-super user uid=%d",
		      uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	/* Send message back to the caller letting him know we got it.
	   There is no need to wait since the end result would be the
	   same if we wait or not since the update has already
	   happened in the database.
	*/

	slurm_send_rc_msg(msg, rc);

	/*
	 * We only want one of these running at a time or we could get some
	 * interesting locking scenarios.  Meaning we could get into a situation
	 * if multiple were running to have both the assoc_mgr locks locked in
	 * one threads as well as the slurmctld locks locked and then waiting of
	 * the assoc_mgr locks in another.  Usually this wouldn't be an issue
	 * though some systems looking up user names could be fairly heavy.  If
	 * you are looking for hundreds you could make the slurmctld
	 * unresponsive in the mean time.  Throttling this to only 1 update at a
	 * time should minimize this situation.
	 */
	_throttle_start(&active_rpc_cnt);
	if (update_ptr->update_list && list_count(update_ptr->update_list)) {
		slurmdb_update_object_t *object;

		slurmdb_update_type_t fed_type = SLURMDB_UPDATE_FEDS;
		if ((object = list_find_first(update_ptr->update_list,
					      _find_update_object_in_list,
					      &fed_type))) {
#if HAVE_SYS_PRCTL_H
			if (prctl(PR_SET_NAME, "fedmgr", NULL, NULL, NULL) < 0){
				error("%s: cannot set my name to %s %m",
				      __func__, "fedmgr");
			}
#endif
			fed_mgr_update_feds(object);
		}

		rc = assoc_mgr_update(update_ptr->update_list, 0);
	}
	_throttle_fini(&active_rpc_cnt);

	END_TIMER2("_slurm_rpc_accounting_update_msg");

	if (rc != SLURM_SUCCESS)
		error("assoc_mgr_update gave error: %s",
		      slurm_strerror(rc));
}

/* _slurm_rpc_reboot_nodes - process RPC to schedule nodes reboot */
inline static void _slurm_rpc_reboot_nodes(slurm_msg_t * msg)
{
	int rc;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
#ifndef HAVE_FRONT_END
	int i;
	struct node_record *node_ptr;
	reboot_msg_t *reboot_msg = (reboot_msg_t *)msg->data;
	char *nodelist = NULL;
	bitstr_t *bitmap = NULL;
	/* Locks: write node lock */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	time_t now = time(NULL);
#endif
	DEF_TIMERS;

	START_TIMER;
	debug2("Processing RPC: REQUEST_REBOOT_NODES from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error("Security violation, REBOOT_NODES RPC from uid=%d", uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}
#ifdef HAVE_FRONT_END
	rc = ESLURM_NOT_SUPPORTED;
#else
	/* do RPC call */
	if (reboot_msg)
		nodelist = reboot_msg->node_list;
	if (!nodelist || !xstrcasecmp(nodelist, "ALL")) {
		bitmap = bit_alloc(node_record_count);
		bit_nset(bitmap, 0, (node_record_count - 1));
	} else if (node_name2bitmap(nodelist, false, &bitmap) != 0) {
		FREE_NULL_BITMAP(bitmap);
		error("Bad node list in REBOOT_NODES request: %s", nodelist);
		slurm_send_rc_msg(msg, ESLURM_INVALID_NODE_NAME);
		return;
	}

	lock_slurmctld(node_write_lock);
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (!bit_test(bitmap, i))
			continue;
		if (IS_NODE_FUTURE(node_ptr) ||
		    (IS_NODE_CLOUD(node_ptr) && IS_NODE_POWER_SAVE(node_ptr))) {
			bit_clear(bitmap, i);
			continue;
		}
		node_ptr->node_state |= NODE_STATE_REBOOT;
		node_ptr->boot_req_time = (time_t) 0;
		node_ptr->last_response = now + slurmctld_conf.resume_timeout;
		if (reboot_msg) {
			node_ptr->next_state = reboot_msg->next_state;
			if (node_ptr->next_state == NODE_RESUME)
				bit_set(rs_node_bitmap, i);

			if (reboot_msg->reason) {
				xfree(node_ptr->reason);
				node_ptr->reason = xstrdup(reboot_msg->reason);
				node_ptr->reason_time = now;
				node_ptr->reason_uid = uid;
			}
			if (reboot_msg->flags & REBOOT_FLAGS_ASAP) {
				node_ptr->node_state |= NODE_STATE_DRAIN;
				bit_clear(avail_node_bitmap, i);
				if (node_ptr->reason == NULL) {
					node_ptr->reason =
						xstrdup("Reboot ASAP");
					node_ptr->reason_time = now;
					node_ptr->reason_uid = uid;
				}
			}
		}
		want_nodes_reboot = true;
	}

	if (want_nodes_reboot == true)
		schedule_node_save();
	unlock_slurmctld(node_write_lock);
	if (want_nodes_reboot == true) {
		nodelist = bitmap2node_name(bitmap);
		info("reboot request queued for nodes %s", nodelist);
		xfree(nodelist);
	}
	FREE_NULL_BITMAP(bitmap);
	rc = SLURM_SUCCESS;
#endif
	END_TIMER2("_slurm_rpc_reboot_nodes");
	slurm_send_rc_msg(msg, rc);
}

inline static void  _slurm_rpc_accounting_first_reg(slurm_msg_t *msg)
{
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	time_t event_time = time(NULL);

	DEF_TIMERS;

	START_TIMER;
	debug2("Processing RPC: ACCOUNTING_FIRST_REG from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error("First Registration request from non-super user uid=%d",
		      uid);
		return;
	}

	send_all_to_accounting(event_time, ACCOUNTING_FIRST_REG);

	END_TIMER2("_slurm_rpc_accounting_first_reg");
}

inline static void  _slurm_rpc_accounting_register_ctld(slurm_msg_t *msg)
{
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	DEF_TIMERS;

	START_TIMER;
	debug2("Processing RPC: ACCOUNTING_REGISTER_CTLD from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error("Registration request from non-super user uid=%d",
		      uid);
		return;
	}

	clusteracct_storage_g_register_ctld(acct_db_conn,
					    slurmctld_conf.slurmctld_port);

	END_TIMER2("_slurm_rpc_accounting_register_ctld");
}

inline static void _slurm_rpc_dump_spank(slurm_msg_t * msg)
{
	int rc = SLURM_SUCCESS;
	spank_env_request_msg_t *spank_req_msg = (spank_env_request_msg_t *)
						 msg->data;
	spank_env_responce_msg_t *spank_resp_msg = NULL;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	slurm_msg_t response_msg;
	DEF_TIMERS;

	START_TIMER;
	debug("Processing RPC: REQUEST_SPANK_ENVIRONMENT from uid=%d JobId=%u",
	      uid, spank_req_msg->job_id);
	if (!validate_slurm_user(uid)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_SPANK_ENVIRONMENT RPC "
		      "from uid=%d", uid);
	}

	if (rc == SLURM_SUCCESS) {
		/* do RPC call */
		struct job_record *job_ptr;
		uint32_t i;

		lock_slurmctld(job_read_lock);
		job_ptr = find_job_record(spank_req_msg->job_id);
		if (job_ptr) {
			spank_resp_msg =
				xmalloc(sizeof(spank_env_responce_msg_t));
			spank_resp_msg->spank_job_env_size =
				job_ptr->spank_job_env_size;
			spank_resp_msg->spank_job_env = xmalloc(
				spank_resp_msg->spank_job_env_size *
				sizeof(char *));
			for (i = 0; i < spank_resp_msg->spank_job_env_size; i++)
				spank_resp_msg->spank_job_env[i] = xstrdup(
					job_ptr->spank_job_env[i]);
		} else {
			rc = ESLURM_INVALID_JOB_ID;
		}
		unlock_slurmctld(job_read_lock);
	}
	END_TIMER2("_slurm_rpc_dump_spank");

	if (rc == SLURM_SUCCESS) {
		response_init(&response_msg, msg);
		response_msg.msg_type = RESPONCE_SPANK_ENVIRONMENT;
		response_msg.data     = spank_resp_msg;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		slurm_free_spank_env_responce_msg(spank_resp_msg);
	} else {
		slurm_send_rc_msg(msg, rc);
	}
}

static void _clear_rpc_stats(void)
{
	int i;

	slurm_mutex_lock(&rpc_mutex);
	for (i = 0; i < rpc_type_size; i++) {
		rpc_type_cnt[i] = 0;
		rpc_type_id[i] = 0;
		rpc_type_time[i] = 0;
	}
	for (i = 0; i < rpc_user_size; i++) {
		rpc_user_cnt[i] = 0;
		rpc_user_id[i] = 0;
		rpc_user_time[i] = 0;
	}
	slurm_mutex_unlock(&rpc_mutex);
}

static void _pack_rpc_stats(int resp, char **buffer_ptr, int *buffer_size,
			    uint16_t protocol_version)
{
	uint32_t i;
	Buf buffer;

	slurm_mutex_lock(&rpc_mutex);
	buffer = create_buf(*buffer_ptr, *buffer_size);
	set_buf_offset(buffer, *buffer_size);

	if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
		for (i = 0; i < rpc_type_size; i++) {
			if (rpc_type_id[i] == 0)
				break;
		}
		pack32(i, buffer);
		pack16_array(rpc_type_id,   i, buffer);
		pack32_array(rpc_type_cnt,  i, buffer);
		pack64_array(rpc_type_time, i, buffer);

		for (i = 1; i < rpc_user_size; i++) {
			if (rpc_user_id[i] == 0)
				break;
		}
		pack32(i, buffer);
		pack32_array(rpc_user_id,   i, buffer);
		pack32_array(rpc_user_cnt,  i, buffer);
		pack64_array(rpc_user_time, i, buffer);

		agent_pack_pending_rpc_stats(buffer);

	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		for (i = 0; i < rpc_type_size; i++) {
			if (rpc_type_id[i] == 0)
				break;
		}
		pack32(i, buffer);
		pack16_array(rpc_type_id,   i, buffer);
		pack32_array(rpc_type_cnt,  i, buffer);
		pack64_array(rpc_type_time, i, buffer);

		for (i = 1; i < rpc_user_size; i++) {
			if (rpc_user_id[i] == 0)
				break;
		}
		pack32(i, buffer);
		pack32_array(rpc_user_id,   i, buffer);
		pack32_array(rpc_user_cnt,  i, buffer);
		pack64_array(rpc_user_time, i, buffer);
	}
	slurm_mutex_unlock(&rpc_mutex);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}

inline static void _slurm_rpc_burst_buffer_status(slurm_msg_t *msg)
{
	slurm_msg_t response_msg;
	bb_status_resp_msg_t status_resp_msg;
	bb_status_req_msg_t *status_req_msg = (bb_status_req_msg_t *)msg->data;

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_BURST_BUFFER_STATUS;
	memset(&status_resp_msg, 0, sizeof(status_resp_msg));
	response_msg.data = &status_resp_msg;
	status_resp_msg.status_resp = bb_g_get_status(status_req_msg->argc,
						      status_req_msg->argv);
	if (status_resp_msg.status_resp)
		response_msg.data_size =
			strlen(status_resp_msg.status_resp) + 1;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(status_resp_msg.status_resp);
}

inline static void _slurm_rpc_control_status(slurm_msg_t * msg)
{
	slurm_msg_t response_msg;
	control_status_msg_t data;

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_CONTROL_STATUS;
	response_msg.data = &data;
	response_msg.data_size = sizeof(control_status_msg_t);
	data.backup_inx = backup_inx;
	data.control_time = control_time;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
}

/* _slurm_rpc_dump_stats - process RPC for statistics information */
inline static void _slurm_rpc_dump_stats(slurm_msg_t * msg)
{
	char *dump;
	int dump_size;
	stats_info_request_msg_t *request_msg;
	slurm_msg_t response_msg;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	request_msg = (stats_info_request_msg_t *)msg->data;

	if ((request_msg->command_id == STAT_COMMAND_RESET) &&
	    !validate_operator(uid)) {
		error("Security violation: REQUEST_STATS_INFO reset "
		      "from uid=%d", uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	debug2("Processing RPC: REQUEST_STATS_INFO (command: %u)",
	       request_msg->command_id);

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_STATS_INFO;

	if (request_msg->command_id == STAT_COMMAND_RESET) {
		reset_stats(1);
		_clear_rpc_stats();
		pack_all_stat(0, &dump, &dump_size, msg->protocol_version);
		_pack_rpc_stats(0, &dump, &dump_size, msg->protocol_version);
		response_msg.data = dump;
		response_msg.data_size = dump_size;
	} else {
		pack_all_stat(1, &dump, &dump_size, msg->protocol_version);
		_pack_rpc_stats(1, &dump, &dump_size, msg->protocol_version);
		response_msg.data = dump;
		response_msg.data_size = dump_size;
	}

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* _slurm_rpc_dump_licenses()
 *
 * Pack the io buffer and send it back to the library.
 */
inline static void
_slurm_rpc_dump_licenses(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	license_info_request_msg_t  *lic_req_msg;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("%s: Processing RPC: REQUEST_LICENSE_INFO uid=%d",
	       __func__, uid);
	lic_req_msg = (license_info_request_msg_t *)msg->data;

	if ((lic_req_msg->last_update - 1) >= last_license_update) {
		/* Dont send unnecessary data
		 */
		debug2("%s: no change SLURM_NO_CHANGE_IN_DATA", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);

		return;
	}

	get_all_license_info(&dump, &dump_size, uid, msg->protocol_version);

	END_TIMER2("_slurm_rpc_dump_licenses");
	debug2("%s: size=%d %s", __func__, dump_size, TIME_STR);

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_LICENSE_INFO;
	response_msg.data = dump;
	response_msg.data_size = dump_size;

	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* Free memory used to track RPC usage by type and user */
extern void free_rpc_stats(void)
{
	slurm_mutex_lock(&rpc_mutex);
	xfree(rpc_type_cnt);
	xfree(rpc_type_id);
	xfree(rpc_type_time);
	rpc_type_size = 0;

	xfree(rpc_user_cnt);
	xfree(rpc_user_id);
	xfree(rpc_user_time);
	rpc_user_size = 0;
	slurm_mutex_unlock(&rpc_mutex);
}

/*
 * _slurm_rpc_kill_job()
 */
inline static void
_slurm_rpc_kill_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	DEF_TIMERS;
	job_step_kill_msg_t *kill;
	slurmctld_lock_t fed_job_read_lock =
		{NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t lock = {READ_LOCK, WRITE_LOCK,
				 WRITE_LOCK, NO_LOCK, READ_LOCK };
	uid_t uid;
	int cc;

	kill =	(job_step_kill_msg_t *)msg->data;
	uid = g_slurm_auth_get_uid(msg->auth_cred);

	/*
	 * If the cluster is part of a federation and it isn't the origin of the
	 * job then if it doesn't know about the federated job, then route the
	 * request to the origin cluster via the client. If the cluster does
	 * know about the job and it owns the job, the this cluster will cancel
	 * the job and it will report the cancel back to the origin.
	 */
	lock_slurmctld(fed_job_read_lock);
	if (fed_mgr_fed_rec) {
		uint32_t job_id, origin_id;
		struct job_record *job_ptr;
		slurmdb_cluster_rec_t *origin;

		job_id    = strtol(kill->sjob_id, NULL, 10);
		origin_id = fed_mgr_get_cluster_id(job_id);
		origin    = fed_mgr_get_cluster_by_id(origin_id);

		/*
		 * only reroute to the origin if the connection is up. If it
		 * isn't then _signal_job will signal the sibling jobs
		 */
		if (origin && origin->fed.send &&
		    (((slurm_persist_conn_t *)origin->fed.send)->fd != -1) &&
		    (origin != fed_mgr_cluster_rec) &&
		    (!(job_ptr = find_job_record(job_id)) ||
		     (job_ptr && job_ptr->fed_details &&
		      (job_ptr->fed_details->cluster_lock !=
		       fed_mgr_cluster_rec->fed.id)))) {

			slurmdb_cluster_rec_t *dst =
				fed_mgr_get_cluster_by_id(origin_id);
			if (!dst) {
				error("couldn't find cluster by cluster id %d",
				      origin_id);
				slurm_send_rc_msg(msg, SLURM_ERROR);
			} else {
				slurm_send_reroute_msg(msg, dst);
				info("%s: REQUEST_KILL_JOB JobId=%s uid %d routed to %s",
				     __func__, kill->sjob_id, uid, dst->name);
			}

			unlock_slurmctld(fed_job_read_lock);
			return;
		}
	}
	unlock_slurmctld(fed_job_read_lock);

	START_TIMER;
	info("%s: REQUEST_KILL_JOB JobId=%s uid %d",
	     __func__, kill->sjob_id, uid);

	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(lock);
	if (kill->sibling) {
		uint32_t job_id = strtol(kill->sjob_id, NULL, 10);
		cc = fed_mgr_remove_active_sibling(job_id, kill->sibling);
	} else {
		cc = job_str_signal(kill->sjob_id,
				    kill->signal,
				    kill->flags,
				    uid,
				    0);
	}
	unlock_slurmctld(lock);
	_throttle_fini(&active_rpc_cnt);

	if (cc == ESLURM_ALREADY_DONE) {
		debug2("%s: job_str_signal() JobId=%s sig %d returned %s",
		       __func__, kill->sjob_id,
		       kill->signal, slurm_strerror(cc));
	} else if (cc != SLURM_SUCCESS) {
		info("%s: job_str_signal() JobId=%s sig %d returned %s",
		     __func__, kill->sjob_id,
		     kill->signal, slurm_strerror(cc));
	} else {
		slurmctld_diag_stats.jobs_canceled++;
	}

	slurm_send_rc_msg(msg, cc);

	END_TIMER2("_slurm_rpc_kill_job");
}

/* The batch messages when made for the comp_msg need to be freed
 * differently than the normal free, so do that here.
 */
static void _slurmctld_free_comp_msg_list(void *x)
{
	slurm_msg_t *msg = (slurm_msg_t*)x;
	if (msg) {
		if (msg->msg_type == REQUEST_BATCH_JOB_LAUNCH) {
			slurm_free_job_launch_msg(msg->data);
			msg->data = NULL;
		}

		slurm_free_comp_msg_list(msg);
	}
}


static void  _slurm_rpc_composite_msg(slurm_msg_t *msg)
{
	static time_t config_update = 0;
	static bool defer_sched = false;
	static int sched_timeout = 0;
	static int active_rpc_cnt = 0;
	struct timeval start_tv;
	bool run_scheduler = false;
	composite_msg_t *comp_msg, comp_resp_msg;
	/* Locks: Read configuration, write job, write node, read federation */
	/* Must match locks in _slurm_rpc_comp_msg_list */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };

	memset(&comp_resp_msg, 0, sizeof(comp_resp_msg));
	comp_resp_msg.msg_list = list_create(_slurmctld_free_comp_msg_list);

	comp_msg = (composite_msg_t *) msg->data;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_ROUTE)
		info("Processing RPC: MESSAGE_COMPOSITE msg with %d messages",
		     comp_msg->msg_list ? list_count(comp_msg->msg_list) : 0);

	if (config_update != slurmctld_conf.last_update) {
		char *sched_params = slurm_get_sched_params();
		int time_limit;
		char *tmp_ptr;

		defer_sched = (xstrcasestr(sched_params, "defer"));

		time_limit = slurm_get_msg_timeout() / 2;
		if ((tmp_ptr = xstrcasestr(sched_params, "max_sched_time="))) {
			sched_timeout = atoi(tmp_ptr + 15);
			if ((sched_timeout <= 0) ||
			    (sched_timeout > time_limit)) {
				error("Invalid max_sched_time: %d",
				      sched_timeout);
				sched_timeout = 0;
			}
		}

		if (sched_timeout == 0) {
			sched_timeout = MAX(time_limit, 1);
			sched_timeout = MIN(sched_timeout, 2);
			sched_timeout *= 1000000;
		}
		xfree(sched_params);
		config_update = slurmctld_conf.last_update;
	}

	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	gettimeofday(&start_tv, NULL);
	_slurm_rpc_comp_msg_list(comp_msg, &run_scheduler,
				 comp_resp_msg.msg_list, &start_tv,
				 sched_timeout);
	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);

	if (list_count(comp_resp_msg.msg_list)) {
		slurm_msg_t resp_msg;
		slurm_msg_t_init(&resp_msg);
		resp_msg.flags    = msg->flags;
		resp_msg.protocol_version = msg->protocol_version;
		memcpy(&resp_msg.address, &comp_msg->sender,
		       sizeof(slurm_addr_t));
		resp_msg.msg_type = RESPONSE_MESSAGE_COMPOSITE;
		resp_msg.data     = &comp_resp_msg;
		slurm_send_only_node_msg(&resp_msg);
	}
	FREE_NULL_LIST(comp_resp_msg.msg_list);

	/* Functions below provide their own locking */
	if (run_scheduler) {
		/*
		 * In defer mode, avoid triggering the scheduler logic
		 * for every epilog complete message.
		 * As one epilog message is sent from every node of each
		 * job at termination, the number of simultaneous schedule
		 * calls can be very high for large machine or large number
		 * of managed jobs.
		 */
		if (!LOTS_OF_AGENTS && !defer_sched)
			(void) schedule(0);	/* Has own locking */
		schedule_node_save();		/* Has own locking */
		schedule_job_save();		/* Has own locking */
	}
}

static void  _slurm_rpc_comp_msg_list(composite_msg_t * comp_msg,
				      bool *run_scheduler,
				      List msg_list_in,
				      struct timeval *start_tv,
				      int timeout)
{
	ListIterator itr;
	slurm_msg_t *next_msg;
	composite_msg_t *ncomp_msg;
	composite_msg_t *comp_resp_msg;
	/* Locks: Read configuration, write job, write node, read federation */
	/* Must match locks in _slurm_rpc_composite_msg */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	DEF_TIMERS;

	START_TIMER;

	itr = list_iterator_create(comp_msg->msg_list);
	while ((next_msg = list_next(itr))) {
		if (slurm_delta_tv(start_tv) >= timeout) {
			END_TIMER;
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_ROUTE)
				info("composite message processing "
				     "yielding locks after running for %s",
				     TIME_STR);
			unlock_slurmctld(job_write_lock);
			usleep(10);
			lock_slurmctld(job_write_lock);
			gettimeofday(start_tv, NULL);
			START_TIMER;
		}
		/* The ret_list is used by slurm_send_rc_msg to house
		   replys going back to the nodes.
		*/
		FREE_NULL_LIST(next_msg->ret_list);
		next_msg->ret_list = msg_list_in;
		switch (next_msg->msg_type) {
		case MESSAGE_COMPOSITE:
			comp_resp_msg = xmalloc(sizeof(composite_msg_t));
			comp_resp_msg->msg_list =
				list_create(_slurmctld_free_comp_msg_list);

			ncomp_msg = (composite_msg_t *) next_msg->data;
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_ROUTE)
				info("Processing embedded MESSAGE_COMPOSITE "
				     "msg with %d direct "
				     "messages", ncomp_msg->msg_list ?
				     list_count(ncomp_msg->msg_list) : 0);
			_slurm_rpc_comp_msg_list(ncomp_msg, run_scheduler,
						 comp_resp_msg->msg_list,
						 start_tv, timeout);
			if (list_count(comp_resp_msg->msg_list)) {
				slurm_msg_t *resp_msg =
					xmalloc_nz(sizeof(slurm_msg_t));
				slurm_msg_t_init(resp_msg);
				resp_msg->msg_index = next_msg->msg_index;
				resp_msg->flags = next_msg->flags;
				resp_msg->protocol_version =
					next_msg->protocol_version;
				resp_msg->msg_type = RESPONSE_MESSAGE_COMPOSITE;
				/* You can't just set the
				 * resp_msg->address here, it won't
				 * make it to where it needs to be
				 * used, set the sender and let it be
				 * handled on the tree node.
				 */
				memcpy(&comp_resp_msg->sender,
				       &ncomp_msg->sender,
				       sizeof(slurm_addr_t));

				resp_msg->data = comp_resp_msg;

				list_append(msg_list_in, resp_msg);
			} else
				slurm_free_composite_msg(comp_resp_msg);
			break;
		case REQUEST_COMPLETE_BATCH_SCRIPT:
		case REQUEST_COMPLETE_BATCH_JOB:
			_slurm_rpc_complete_batch_script(next_msg,
							 run_scheduler, 1);
			break;
		case REQUEST_STEP_COMPLETE:
			_slurm_rpc_step_complete(next_msg, 1);
			break;
		case MESSAGE_EPILOG_COMPLETE:
			_slurm_rpc_epilog_complete(next_msg, run_scheduler, 1);
			break;
		case MESSAGE_NODE_REGISTRATION_STATUS:
			_slurm_rpc_node_registration(next_msg, 1);
			break;
		default:
			error("_slurm_rpc_comp_msg_list: invalid msg type");
			break;
		}
		next_msg->ret_list = NULL;
	}
	list_iterator_destroy(itr);
	END_TIMER;
	/* NOTE: RPC has no response */
}

/* _slurm_rpc_assoc_mgr_info()
 *
 * Pack the assoc_mgr lists and return it back to the caller.
 */
static void _slurm_rpc_assoc_mgr_info(slurm_msg_t * msg)
{
	DEF_TIMERS;
	char *dump = NULL;
	int dump_size = 0;
	slurm_msg_t response_msg;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	START_TIMER;
	debug2("%s: Processing RPC: REQUEST_ASSOC_MGR_INFO uid=%d",
	       __func__, uid);

	/* do RPC call */
	/* Security is handled in the assoc_mgr */
	assoc_mgr_info_get_pack_msg(&dump, &dump_size,
				     (assoc_mgr_info_request_msg_t *)msg->data,
				     uid, acct_db_conn,
				     msg->protocol_version);

	END_TIMER2("_slurm_rpc_assoc_mgr_info");
	debug2("%s: size=%d %s", __func__, dump_size, TIME_STR);

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_ASSOC_MGR_INFO;
	response_msg.data = dump;
	response_msg.data_size = dump_size;

	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* Take a persist_msg_t and handle it like a normal slurm_msg_t */
static int _process_persist_conn(void *arg,
				 persist_msg_t *persist_msg,
				 Buf *out_buffer, uint32_t *uid)
{
	slurm_msg_t msg;
	slurm_persist_conn_t *persist_conn = arg;

	if (*uid == NO_VAL)
		*uid = g_slurm_auth_get_uid(persist_conn->auth_cred);

	*out_buffer = NULL;

	slurm_msg_t_init(&msg);

	msg.auth_cred = persist_conn->auth_cred;
	msg.conn = persist_conn;
	msg.conn_fd = persist_conn->fd;

	msg.msg_type = persist_msg->msg_type;
	msg.data = persist_msg->data;

	slurmctld_req(&msg, NULL);

	return SLURM_SUCCESS;
}

static void _slurm_rpc_persist_init(slurm_msg_t *msg, connection_arg_t *arg)
{
	DEF_TIMERS;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;
	uint16_t port;
	Buf ret_buf;
	slurm_persist_conn_t *persist_conn = NULL, p_tmp;
	persist_init_req_msg_t *persist_init = msg->data;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);

	xassert(arg);

	START_TIMER;

	if (persist_init->version > SLURM_PROTOCOL_VERSION)
		persist_init->version = SLURM_PROTOCOL_VERSION;

	if (!validate_slurm_user(uid)) {
		memset(&p_tmp, 0, sizeof(p_tmp));
		p_tmp.fd = arg->newsockfd;
		p_tmp.cluster_name = persist_init->cluster_name;
		p_tmp.version = persist_init->version;
		p_tmp.shutdown = &slurmctld_config.shutdown_time;

		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_PERSIST_INIT RPC "
		      "from uid=%d", uid);
		goto end_it;
	}

	persist_conn = xmalloc(sizeof(slurm_persist_conn_t));

	persist_conn->auth_cred = msg->auth_cred;
	msg->auth_cred = NULL;

	persist_conn->cluster_name = persist_init->cluster_name;
	persist_init->cluster_name = NULL;

	persist_conn->fd = arg->newsockfd;
	arg->newsockfd = -1;

	persist_conn->callback_proc = _process_persist_conn;

	persist_conn->persist_type = persist_init->persist_type;
	persist_conn->rem_port = persist_init->port;
	persist_conn->rem_host = xmalloc_nz(16);

	slurm_get_ip_str(&arg->cli_addr, &port,
			 persist_conn->rem_host, 16);
	/* info("got it from %d %s %s(%u)", persist_conn->fd, */
	/*      persist_conn->cluster_name, */
	/*      persist_conn->rem_host, persist_conn->rem_port); */
	persist_conn->shutdown = &slurmctld_config.shutdown_time;
	//persist_conn->timeout = 0; /* we want this to be 0 */

	persist_conn->version = persist_init->version;
	memcpy(&p_tmp, persist_conn, sizeof(slurm_persist_conn_t));

	if (persist_init->persist_type == PERSIST_TYPE_FED)
		rc = fed_mgr_add_sibling_conn(persist_conn, &comment);
	else
		rc = SLURM_ERROR;
end_it:

	/* If people are really hammering the fed_mgr we could get into trouble
	 * with the persist_conn we sent in, so use the copy instead
	 */
	ret_buf = slurm_persist_make_rc_msg(&p_tmp, rc, comment, p_tmp.version);
	if (slurm_persist_send_msg(&p_tmp, ret_buf) != SLURM_SUCCESS) {
		debug("Problem sending response to connection %d uid(%d)",
		      p_tmp.fd, uid);
	}

	if (rc && persist_conn) {
		/* Free AFTER message has been sent back to remote */
		persist_conn->fd = -1;
		slurm_persist_conn_destroy(persist_conn);
	}
	xfree(comment);
	free_buf(ret_buf);
	END_TIMER;

	/* Don't free this here, it will be done elsewhere */
	//slurm_persist_conn_destroy(persist_conn);
}

static void _slurm_rpc_sib_job_lock(uint32_t uid, slurm_msg_t *msg)
{
	int rc;
	sib_msg_t *sib_msg = msg->data;

	if (!msg->conn) {
		error("Security violation, SIB_JOB_LOCK RPC from uid=%d",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	rc = fed_mgr_job_lock_set(sib_msg->job_id, sib_msg->cluster_id);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_sib_job_unlock(uint32_t uid, slurm_msg_t *msg)
{
	int rc;
	sib_msg_t *sib_msg = msg->data;

	if (!msg->conn) {
		error("Security violation, SIB_JOB_UNLOCK RPC from uid=%d",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	rc = fed_mgr_job_lock_unset(sib_msg->job_id, sib_msg->cluster_id);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_sib_msg(uint32_t uid, slurm_msg_t *msg) {
	if (!msg->conn) {
		error("Security violation, SIB_SUBMISSION RPC from uid=%d",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	fed_mgr_q_sib_msg(msg, uid);
}

static Buf _build_rc_buf(int rc, uint16_t rpc_version)
{
	Buf buf = NULL;
	slurm_msg_t msg;
	return_code_msg_t data;

	memset(&data, 0, sizeof(data));
	data.return_code = rc;
	slurm_msg_t_init(&msg);
	msg.msg_type = RESPONSE_SLURM_RC;
	msg.data = &data;
	buf = init_buf(128);
	pack16(msg.msg_type, buf);
	if (pack_msg(&msg, buf) != SLURM_SUCCESS)
		FREE_NULL_BUFFER(buf);

	return buf;
}

/* Free Buf record from a list */
static void _ctld_free_list_msg(void *x)
{
	FREE_NULL_BUFFER(x);
}

static void _proc_multi_msg(uint32_t rpc_uid, slurm_msg_t *msg)
{
	slurm_msg_t sub_msg, response_msg;
	ctld_list_msg_t *ctld_req_msg, ctld_resp_msg;
	List full_resp_list = NULL;
	Buf single_req_buf = NULL;
	Buf ret_buf, resp_buf = NULL;
	ListIterator iter = NULL;
	int rc;

	if (!msg->conn) {
		error("Security violation, REQUEST_CTLD_MULT_MSG RPC from uid=%d",
		      rpc_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	ctld_req_msg = (ctld_list_msg_t *) msg->data;
	full_resp_list = list_create(_ctld_free_list_msg);
	iter = list_iterator_create(ctld_req_msg->my_list);
	while ((single_req_buf = list_next(iter))) {
		slurm_msg_t_init(&sub_msg);
		if (unpack16(&sub_msg.msg_type, single_req_buf) ||
		    unpack_msg(&sub_msg, single_req_buf)) {
			error("Sub-message unpack error for REQUEST_CTLD_MULT_MSG %u RPC",
			      sub_msg.msg_type);
			ret_buf = _build_rc_buf(SLURM_ERROR,
						msg->protocol_version);
			list_append(full_resp_list, ret_buf);
			continue;
		}
		sub_msg.conn = msg->conn;
		sub_msg.auth_cred = msg->auth_cred;
		ret_buf = NULL;

		if (slurmctld_conf.debug_flags & DEBUG_FLAG_PROTOCOL) {
			char *p = rpc_num2string(sub_msg.msg_type);
			info("%s: received opcode %s", __func__, p);
		}

		switch (sub_msg.msg_type) {
		case REQUEST_PING:
			rc = SLURM_SUCCESS;
			ret_buf = _build_rc_buf(rc, msg->protocol_version);
			break;
		case REQUEST_SIB_MSG:
			_slurm_rpc_sib_msg(rpc_uid, &sub_msg);
			ret_buf = _build_rc_buf(SLURM_SUCCESS,
						msg->protocol_version);
			break;
		default:
			error("%s: Unsupported Message Type:%s",
			      __func__, rpc_num2string(sub_msg.msg_type));
		}
		(void) slurm_free_msg_data(sub_msg.msg_type, sub_msg.data);

		if (!ret_buf) {
			ret_buf = _build_rc_buf(SLURM_ERROR,
						msg->protocol_version);
		}
		list_append(full_resp_list, ret_buf);
	}
	list_iterator_destroy(iter);

	ctld_resp_msg.my_list = full_resp_list;

	response_init(&response_msg, msg);
	response_msg.msg_type = RESPONSE_CTLD_MULT_MSG;
	response_msg.data = &ctld_resp_msg;

	/* Send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	FREE_NULL_LIST(full_resp_list);
	free_buf(resp_buf);
	return;
}

/* Route msg to federated job's origin.
 * RET returns SLURM_SUCCESS if the msg was routed.
 */
static int _route_msg_to_origin(slurm_msg_t *msg, char *src_job_id_str,
				uint32_t src_job_id, uid_t uid)
{
	xassert(msg);

	/* route msg to origin cluster if a federated job */
	if (!msg->conn && fed_mgr_fed_rec) {
		/* Don't send reroute if coming from a federated cluster (aka
		 * has a msg->conn). */
		uint32_t job_id, origin_id;

		if (src_job_id_str)
			job_id = strtol(src_job_id_str, NULL, 10);
		else
			job_id = src_job_id;
		origin_id = fed_mgr_get_cluster_id(job_id);

		if (origin_id && (origin_id != fed_mgr_cluster_rec->fed.id)) {
			slurmdb_cluster_rec_t *dst =
				fed_mgr_get_cluster_by_id(origin_id);
			if (!dst) {
				error("couldn't find cluster by cluster id %d",
				      origin_id);
				slurm_send_rc_msg(msg, SLURM_ERROR);
			} else {
				slurm_send_reroute_msg(msg, dst);
				info("%s: %s JobId=%u uid %d routed to %s",
				     __func__, rpc_num2string(msg->msg_type),
				     job_id, uid, dst->name);
			}

			return SLURM_SUCCESS;
		}
	}

	return SLURM_ERROR;
}

inline static void  _slurm_rpc_set_fs_dampening_factor(slurm_msg_t *msg)
{
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred);
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	set_fs_dampening_factor_msg_t *request_msg =
		(set_fs_dampening_factor_msg_t *) msg->data;
	uint16_t factor;

	debug2("Processing RPC: REQUEST_SET_FS_DAMPENING_FACTOR from uid=%d", uid);
	if (!validate_super_user(uid)) {
		error("set FairShareDampeningFactor request from non-super user uid=%d",
		      uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}
	factor = request_msg->dampening_factor;

	lock_slurmctld(config_write_lock);
	slurm_set_fs_dampening_factor(factor);
	slurmctld_conf.last_update = time(NULL);
	priority_g_reconfig(false);
	unlock_slurmctld(config_write_lock);

	info("Set FairShareDampeningFactor to %u", factor);
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}
