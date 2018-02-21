/****************************************************************************\
 *  slurm_protocol_defs.h - definitions used for RPCs
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2014 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>.
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

#ifndef _SLURM_PROTOCOL_DEFS_H
#define _SLURM_PROTOCOL_DEFS_H

#include <inttypes.h>
#include <sys/wait.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"
#include "src/common/bitstring.h"
#include "src/common/job_options.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_persist_conn.h"
#include "src/common/slurm_step_layout.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/working_cluster.h"
#include "src/common/xassert.h"

#define MAX_SLURM_NAME 64
#define FORWARD_INIT 0xfffe

/* Defined job states */
#define IS_JOB_PENDING(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_PENDING)
#define IS_JOB_RUNNING(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_RUNNING)
#define IS_JOB_SUSPENDED(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_SUSPENDED)
#define IS_JOB_COMPLETE(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_COMPLETE)
#define IS_JOB_CANCELLED(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_CANCELLED)
#define IS_JOB_FAILED(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_FAILED)
#define IS_JOB_TIMEOUT(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_TIMEOUT)
#define IS_JOB_NODE_FAILED(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_NODE_FAIL)
#define IS_JOB_DEADLINE(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_DEADLINE)
#define IS_JOB_OOM(_X)		\
	((_X->job_state & JOB_STATE_BASE) == JOB_OOM)
#define IS_JOB_POWER_UP_NODE(_X)	\
	(_X->job_state & JOB_POWER_UP_NODE)

/* Derived job states */
#define IS_JOB_COMPLETING(_X)		\
	(_X->job_state & JOB_COMPLETING)
#define IS_JOB_CONFIGURING(_X)		\
	(_X->job_state & JOB_CONFIGURING)
#define IS_JOB_STARTED(_X)		\
	((_X->job_state & JOB_STATE_BASE) >  JOB_PENDING)
#define IS_JOB_FINISHED(_X)		\
	((_X->job_state & JOB_STATE_BASE) >  JOB_SUSPENDED)
#define IS_JOB_COMPLETED(_X)		\
	(IS_JOB_FINISHED(_X) && ((_X->job_state & JOB_COMPLETING) == 0))
#define IS_JOB_RESIZING(_X)		\
	(_X->job_state & JOB_RESIZING)
#define IS_JOB_REQUEUED(_X)		\
	(_X->job_state & JOB_REQUEUE)
#define IS_JOB_FED_REQUEUED(_X)		\
	(_X->job_state & JOB_REQUEUE_FED)
#define IS_JOB_UPDATE_DB(_X)		\
	(_X->job_state & JOB_UPDATE_DB)
#define IS_JOB_REVOKED(_X)		\
	(_X->job_state & JOB_REVOKED)

/* Defined node states */
#define IS_NODE_UNKNOWN(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_UNKNOWN)
#define IS_NODE_DOWN(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_DOWN)
#define IS_NODE_IDLE(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_IDLE)
#define IS_NODE_ALLOCATED(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_ALLOCATED)
#define IS_NODE_ERROR(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_ERROR)
#define IS_NODE_MIXED(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_MIXED)
#define IS_NODE_FUTURE(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_FUTURE)

/* Derived node states */
#define IS_NODE_CLOUD(_X)		\
	(_X->node_state & NODE_STATE_CLOUD)
#define IS_NODE_DRAIN(_X)		\
	(_X->node_state & NODE_STATE_DRAIN)
#define IS_NODE_DRAINING(_X)		\
	((_X->node_state & NODE_STATE_DRAIN) \
	 && (IS_NODE_ALLOCATED(_X) || IS_NODE_ERROR(_X) || IS_NODE_MIXED(_X)))
#define IS_NODE_DRAINED(_X)		\
	(IS_NODE_DRAIN(_X) && !IS_NODE_DRAINING(_X))
#define IS_NODE_COMPLETING(_X)	\
	(_X->node_state & NODE_STATE_COMPLETING)
#define IS_NODE_NO_RESPOND(_X)		\
	(_X->node_state & NODE_STATE_NO_RESPOND)
#define IS_NODE_POWER_SAVE(_X)		\
	(_X->node_state & NODE_STATE_POWER_SAVE)
#define IS_NODE_FAIL(_X)		\
	(_X->node_state & NODE_STATE_FAIL)
#define IS_NODE_POWER_UP(_X)		\
	(_X->node_state & NODE_STATE_POWER_UP)
#define IS_NODE_MAINT(_X)		\
	(_X->node_state & NODE_STATE_MAINT)
#define IS_NODE_REBOOT(_X)		\
	(_X->node_state & NODE_STATE_REBOOT)

#define THIS_FILE ((strrchr(__FILE__, '/') ?: __FILE__ - 1) + 1)
#define INFO_LINE(fmt, ...) \
	info("%s (%s:%d) "fmt, __func__, THIS_FILE, __LINE__, ##__VA_ARGS__);

#define YEAR_MINUTES (365 * 24 * 60)
#define YEAR_SECONDS (365 * 24 * 60 * 60)

/* These defines have to be here to avoid circular dependancy with
 * switch.h
 */
#ifndef __switch_jobinfo_t_defined
#  define __switch_jobinfo_t_defined
   typedef struct switch_jobinfo   switch_jobinfo_t;
#endif
#ifndef __switch_node_info_t_defined
#  define __switch_node_info_t_defined
   typedef struct switch_node_info switch_node_info_t;
#endif

/*
 * SLURM Message types
 *
 * IMPORTANT: ADD NEW MESSAGE TYPES TO THE *END* OF ONE OF THESE NUMBERED
 * SECTIONS. ADDING ONE ELSEWHERE WOULD SHIFT THE VALUES OF EXISTING MESSAGE
 * TYPES IN CURRENT PROGRAMS AND PREVENT BACKWARD COMPATABILITY.
 */
typedef enum {
	REQUEST_NODE_REGISTRATION_STATUS = 1001,
	MESSAGE_NODE_REGISTRATION_STATUS,
	REQUEST_RECONFIGURE,
	RESPONSE_RECONFIGURE,
	REQUEST_SHUTDOWN,
	REQUEST_SHUTDOWN_IMMEDIATE,
	RESPONSE_SHUTDOWN,
	REQUEST_PING,
	REQUEST_CONTROL,
	REQUEST_SET_DEBUG_LEVEL,	/* 1010 */
	REQUEST_HEALTH_CHECK,
	REQUEST_TAKEOVER,
	REQUEST_SET_SCHEDLOG_LEVEL,
	REQUEST_SET_DEBUG_FLAGS,
	REQUEST_REBOOT_NODES,
	RESPONSE_PING_SLURMD,
	REQUEST_ACCT_GATHER_UPDATE,
	RESPONSE_ACCT_GATHER_UPDATE,
	REQUEST_ACCT_GATHER_ENERGY,
	RESPONSE_ACCT_GATHER_ENERGY,	/* 1020 */
	REQUEST_LICENSE_INFO,
	RESPONSE_LICENSE_INFO,
	REQUEST_SET_FS_DAMPENING_FACTOR,
	DBD_MESSAGES_START = 1400, /* We can't replace this with
				    * REQUEST_PERSIST_INIT since DBD_INIT is
				    * packed in a way we can't tell the
				    * protocol_version without unpacking
				    * multiple variables. 2 versions after
				    * 17.02 we can probably replace if someone
				    * remembers */
	PERSIST_RC = 1433, /* To mirror the DBD_RC this is replacing */
	/* Don't make any messages in this range as this is what the DBD uses
	 * unless mirroring */
	DBD_MESSAGES_END   = 2000,
	REQUEST_BUILD_INFO	= 2001,
	RESPONSE_BUILD_INFO,
	REQUEST_JOB_INFO,
	RESPONSE_JOB_INFO,
	REQUEST_JOB_STEP_INFO,
	RESPONSE_JOB_STEP_INFO,
	REQUEST_NODE_INFO,
	RESPONSE_NODE_INFO,
	REQUEST_PARTITION_INFO,
	RESPONSE_PARTITION_INFO,	/* 2010 */
	REQUEST_ACCTING_INFO,
	RESPONSE_ACCOUNTING_INFO,
	REQUEST_JOB_ID,
	RESPONSE_JOB_ID,
	REQUEST_BLOCK_INFO,
	RESPONSE_BLOCK_INFO,
	REQUEST_TRIGGER_SET,
	REQUEST_TRIGGER_GET,
	REQUEST_TRIGGER_CLEAR,
	RESPONSE_TRIGGER_GET,		/* 2020 */
	REQUEST_JOB_INFO_SINGLE,
	REQUEST_SHARE_INFO,
	RESPONSE_SHARE_INFO,
	REQUEST_RESERVATION_INFO,
	RESPONSE_RESERVATION_INFO,
	REQUEST_PRIORITY_FACTORS,
	RESPONSE_PRIORITY_FACTORS,
	REQUEST_TOPO_INFO,
	RESPONSE_TOPO_INFO,
	REQUEST_TRIGGER_PULL,		/* 2030 */
	REQUEST_FRONT_END_INFO,
	RESPONSE_FRONT_END_INFO,
	REQUEST_SPANK_ENVIRONMENT,
	RESPONCE_SPANK_ENVIRONMENT,
	REQUEST_STATS_INFO,
	RESPONSE_STATS_INFO,
	REQUEST_BURST_BUFFER_INFO,
	RESPONSE_BURST_BUFFER_INFO,
	REQUEST_JOB_USER_INFO,
	REQUEST_NODE_INFO_SINGLE,	/* 2040 */
	REQUEST_POWERCAP_INFO,
	RESPONSE_POWERCAP_INFO,
	REQUEST_ASSOC_MGR_INFO,
	RESPONSE_ASSOC_MGR_INFO,
	REQUEST_EVENT_LOG,
	DEFUNCT_RESPONSE_SICP_INFO_DEFUNCT,	/* DEFUNCT */
	REQUEST_LAYOUT_INFO,
	RESPONSE_LAYOUT_INFO,
	REQUEST_FED_INFO,
	RESPONSE_FED_INFO,		/* 2050 */
	REQUEST_BATCH_SCRIPT,
	RESPONSE_BATCH_SCRIPT,

	REQUEST_UPDATE_JOB = 3001,
	REQUEST_UPDATE_NODE,
	REQUEST_CREATE_PARTITION,
	REQUEST_DELETE_PARTITION,
	REQUEST_UPDATE_PARTITION,
	REQUEST_CREATE_RESERVATION,
	RESPONSE_CREATE_RESERVATION,
	REQUEST_DELETE_RESERVATION,
	REQUEST_UPDATE_RESERVATION,
	REQUEST_UPDATE_BLOCK,		/* 3010 */
	REQUEST_UPDATE_FRONT_END,
	REQUEST_UPDATE_LAYOUT,
	REQUEST_UPDATE_POWERCAP,

	REQUEST_RESOURCE_ALLOCATION = 4001,
	RESPONSE_RESOURCE_ALLOCATION,
	REQUEST_SUBMIT_BATCH_JOB,
	RESPONSE_SUBMIT_BATCH_JOB,
	REQUEST_BATCH_JOB_LAUNCH,
	REQUEST_CANCEL_JOB,
	RESPONSE_CANCEL_JOB,
	REQUEST_JOB_RESOURCE,
	RESPONSE_JOB_RESOURCE,
	REQUEST_JOB_ATTACH,		/* 4010 */
	RESPONSE_JOB_ATTACH,
	REQUEST_JOB_WILL_RUN,
	RESPONSE_JOB_WILL_RUN,
	REQUEST_JOB_ALLOCATION_INFO,
	RESPONSE_JOB_ALLOCATION_INFO,
	DEFUNCT_REQUEST_JOB_ALLOCATION_INFO_LITE, /* DEFUNCT, CAN BE REUSED 2
						   * VERSIONS AFTER V17.02 */
	DEFUNCT_RESPONSE_JOB_ALLOCATION_INFO_LITE, /* DEFUNCT, CAN BE REUSED 2
						    * VERSIONS AFTER V17.02 */
	REQUEST_UPDATE_JOB_TIME,
	REQUEST_JOB_READY,
	RESPONSE_JOB_READY,		/* 4020 */
	REQUEST_JOB_END_TIME,
	REQUEST_JOB_NOTIFY,
	REQUEST_JOB_SBCAST_CRED,
	RESPONSE_JOB_SBCAST_CRED,
	REQUEST_JOB_PACK_ALLOCATION,
	RESPONSE_JOB_PACK_ALLOCATION,
	REQUEST_JOB_PACK_ALLOC_INFO,
	REQUEST_SUBMIT_BATCH_JOB_PACK,

	REQUEST_CTLD_MULT_MSG = 4500,
	RESPONSE_CTLD_MULT_MSG,
	REQUEST_SIB_MSG,
	REQUEST_SIB_JOB_LOCK,
	REQUEST_SIB_JOB_UNLOCK,

	REQUEST_JOB_STEP_CREATE = 5001,
	RESPONSE_JOB_STEP_CREATE,
	REQUEST_RUN_JOB_STEP,
	RESPONSE_RUN_JOB_STEP,
	REQUEST_CANCEL_JOB_STEP,
	RESPONSE_CANCEL_JOB_STEP,
	REQUEST_UPDATE_JOB_STEP,
	DEFUNCT_RESPONSE_COMPLETE_JOB_STEP, /* DEFUNCT */
	REQUEST_CHECKPOINT,
	RESPONSE_CHECKPOINT,		/* 5010 */
	REQUEST_CHECKPOINT_COMP,
	REQUEST_CHECKPOINT_TASK_COMP,
	RESPONSE_CHECKPOINT_COMP,
	REQUEST_SUSPEND,
	RESPONSE_SUSPEND,
	REQUEST_STEP_COMPLETE,
	REQUEST_COMPLETE_JOB_ALLOCATION,
	REQUEST_COMPLETE_BATCH_SCRIPT,
	REQUEST_JOB_STEP_STAT,
	RESPONSE_JOB_STEP_STAT,		/* 5020 */
	REQUEST_STEP_LAYOUT,
	RESPONSE_STEP_LAYOUT,
	REQUEST_JOB_REQUEUE,
	REQUEST_DAEMON_STATUS,
	RESPONSE_SLURMD_STATUS,
	RESPONSE_SLURMCTLD_STATUS,
	REQUEST_JOB_STEP_PIDS,
	RESPONSE_JOB_STEP_PIDS,
	REQUEST_FORWARD_DATA,
	REQUEST_COMPLETE_BATCH_JOB,	/* 5030 */
	REQUEST_SUSPEND_INT,
	REQUEST_KILL_JOB,		/* 5032 */
	REQUEST_KILL_JOBSTEP,
	RESPONSE_JOB_ARRAY_ERRORS,
	REQUEST_NETWORK_CALLERID,
	RESPONSE_NETWORK_CALLERID,
	REQUEST_STEP_COMPLETE_AGGR,
	REQUEST_TOP_JOB,		/* 5038 */

	REQUEST_LAUNCH_TASKS = 6001,
	RESPONSE_LAUNCH_TASKS,
	MESSAGE_TASK_EXIT,
	REQUEST_SIGNAL_TASKS,
	REQUEST_CHECKPOINT_TASKS,
	REQUEST_TERMINATE_TASKS,
	REQUEST_REATTACH_TASKS,
	RESPONSE_REATTACH_TASKS,
	REQUEST_KILL_TIMELIMIT,
	DEFUNCT_REQUEST_SIGNAL_JOB,	/* 6010 */ /* DEFUNCT REUSE 2 VERSIONS
						    * AFTER 17.11 */
	REQUEST_TERMINATE_JOB,
	MESSAGE_EPILOG_COMPLETE,
	REQUEST_ABORT_JOB,	/* job shouldn't be running, kill it without
				 * job/step/task complete responses */
	REQUEST_FILE_BCAST,
	TASK_USER_MANAGED_IO_STREAM,
	REQUEST_KILL_PREEMPTED,

	REQUEST_LAUNCH_PROLOG,
	REQUEST_COMPLETE_PROLOG,
	RESPONSE_PROLOG_EXECUTING,	/* 6019 */

	REQUEST_PERSIST_INIT = 6500,

	SRUN_PING = 7001,
	SRUN_TIMEOUT,
	SRUN_NODE_FAIL,
	SRUN_JOB_COMPLETE,
	SRUN_USER_MSG,
	SRUN_EXEC,
	SRUN_STEP_MISSING,
	SRUN_REQUEST_SUSPEND,
	SRUN_STEP_SIGNAL,	/* for launch plugins aprun, poe and runjob,
				 * srun forwards signal to the launch command */

	PMI_KVS_PUT_REQ = 7201,
	PMI_KVS_PUT_RESP,
	PMI_KVS_GET_REQ,
	PMI_KVS_GET_RESP,

	RESPONSE_SLURM_RC = 8001,
	RESPONSE_SLURM_RC_MSG,
	RESPONSE_SLURM_REROUTE_MSG,

	RESPONSE_FORWARD_FAILED = 9001,

	ACCOUNTING_UPDATE_MSG = 10001,
	ACCOUNTING_FIRST_REG,
	ACCOUNTING_REGISTER_CTLD,
	ACCOUNTING_TRES_CHANGE_DB,
	ACCOUNTING_NODES_CHANGE_DB,

	MESSAGE_COMPOSITE = 11001,
	RESPONSE_MESSAGE_COMPOSITE,
} slurm_msg_type_t;

/*****************************************************************************\
 * core api configuration struct
\*****************************************************************************/
typedef struct forward {
	uint16_t   cnt;		/* number of nodes to forward to */
	uint16_t   init;	/* tell me it has been set (FORWARD_INIT) */
	char      *nodelist;	/* ranged string of who to forward the
				 * message to */
	uint32_t   timeout;	/* original timeout increments */
	uint16_t   tree_width;  /* what the treewidth should be */
} forward_t;

/*core api protocol message structures */
typedef struct slurm_protocol_header {
	uint16_t version;
	uint16_t flags;
	uint16_t msg_index;
	uint16_t msg_type; /* really slurm_msg_type_t but needs to be
			      uint16_t for packing purposes. */
	uint32_t body_length;
	uint16_t ret_cnt;
	forward_t forward;
	slurm_addr_t orig_addr;
	List ret_list;
} header_t;

typedef struct forward_struct {
	char *buf;
	int buf_len;
	uint16_t fwd_cnt;
	pthread_mutex_t forward_mutex;
	pthread_cond_t notify;
	List ret_list;
	int timeout;
} forward_struct_t;

typedef struct forward_message {
	forward_struct_t *fwd_struct;
	header_t header;
	int timeout;
} forward_msg_t;

typedef struct slurm_protocol_config {
	slurm_addr_t primary_controller;
	slurm_addr_t secondary_controller;
} slurm_protocol_config_t;

typedef struct slurm_msg {
	slurm_addr_t address;
	void *auth_cred;
	uint32_t body_offset; /* DON'T PACK: offset in buffer where body part of
				 buffer starts. */
	Buf buffer; /* DON't PACK! ptr to buffer that msg was unpacked from. */
	slurm_persist_conn_t *conn; /* DON'T PACK OR FREE! this is here to
				     * distinquish a persistent connection from
				     * a normal connection it should be filled
				     * in with the connection before sending the
				     * message so that it is handled correctly.
				     */
	int conn_fd; /* Only used when the message isn't on a persistent
		      * connection. */
	void *data;
	uint32_t data_size;
	uint16_t flags;
	uint16_t msg_index;
	uint16_t msg_type; /* really a slurm_msg_type_t but needs to be
			    * this way for packing purposes.  message type */
	uint16_t protocol_version; /* DON'T PACK!  Only used if
				    * message comming from non-default
				    * slurm protocol.  Initted to
				    * NO_VAL meaning use the default. */
	/* The following were all added for the forward.c code */
	forward_t forward;
	forward_struct_t *forward_struct;
	slurm_addr_t orig_addr;
	List ret_list;
} slurm_msg_t;

typedef struct ret_data_info {
	uint16_t type; /* really a slurm_msg_type_t but needs to be
			* this way for packing purposes.  message type */
	uint32_t err;
	char *node_name;
	void *data; /* used to hold the return message data (i.e.
		       return_code_msg_t */
} ret_data_info_t;

/*****************************************************************************\
 * Slurm Protocol Data Structures
\*****************************************************************************/
struct kvs_hosts {
	uint32_t	task_id;	/* job step's task id */
	uint16_t	port;		/* communication port */
	char *		hostname;	/* communication host */
};
struct kvs_comm {
	char *		kvs_name;
	uint32_t	kvs_cnt;	/* count of key-pairs */
	char **		kvs_keys;
	char **		kvs_values;
	uint16_t *	kvs_key_sent;
};
typedef struct kvs_comm_set {

	uint16_t	host_cnt;	/* hosts getting this message */
	struct kvs_hosts *kvs_host_ptr;	/* host forwarding info */
 	uint16_t	kvs_comm_recs;	/* count of kvs_comm entries */
	struct kvs_comm **kvs_comm_ptr;	/* pointers to kvs_comm entries */
} kvs_comm_set_t;

typedef struct assoc_shares_object {
	uint32_t assoc_id;	/* association ID */

	char *cluster;          /* cluster name */
	char *name;             /* name */
	char *parent;           /* parent name */
	char *partition;	/* partition */

	double shares_norm;     /* normalized shares */
	uint32_t shares_raw;	/* number of shares allocated */

	uint64_t *tres_run_secs; /* currently running tres-secs
				  * = grp_used_tres_run_secs */
	uint64_t *tres_grp_mins; /* tres-minute limit */

	double usage_efctv;	/* effective, normalized usage */
	double usage_norm;	/* normalized usage */
	uint64_t usage_raw;	/* measure of TRESBillableUnits usage */
	long double *usage_tres_raw; /* measure of each TRES usage */
	double fs_factor;	/* fairshare factor */
	double level_fs;	/* fairshare factor at this level. stored on an
				 * assoc as a long double, but that is not
				 * needed for display in sshare */
	uint16_t user;          /* 1 if user association 0 if account
				 * association */
} assoc_shares_object_t;

typedef struct shares_request_msg {
	List acct_list;
	List user_list;
} shares_request_msg_t;

typedef struct shares_response_msg {
	List assoc_shares_list; /* list of assoc_shares_object_t *'s */
	uint64_t tot_shares;
	uint32_t tres_cnt;
	char **tres_names;
} shares_response_msg_t;

typedef struct priority_factors_request_msg {
	List	 job_id_list;
	char    *partitions;
	List	 uid_list;
} priority_factors_request_msg_t;

typedef struct job_notify_msg {
	uint32_t job_id;
	uint32_t job_step_id;	/* currently not used */
	char *   message;
} job_notify_msg_t;

typedef struct job_id_msg {
	uint32_t job_id;
	uint16_t show_flags;
} job_id_msg_t;

typedef struct job_user_id_msg {
	uint32_t user_id;
	uint16_t show_flags;
} job_user_id_msg_t;

typedef struct job_step_id_msg {
	uint32_t job_id;
	uint32_t step_id;
} job_step_id_msg_t;

typedef struct job_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
	List   job_ids;		/* Optional list of job_ids, otherwise show all
				 * jobs. */
} job_info_request_msg_t;

typedef struct job_step_info_request_msg {
	time_t last_update;
	uint32_t job_id;
	uint32_t step_id;
	uint16_t show_flags;
} job_step_info_request_msg_t;

typedef struct node_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
} node_info_request_msg_t;

typedef struct node_info_single_msg {
	char *node_name;
	uint16_t show_flags;
} node_info_single_msg_t;

typedef struct front_end_info_request_msg {
	time_t last_update;
} front_end_info_request_msg_t;

typedef struct block_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
} block_info_request_msg_t;

typedef struct part_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
} part_info_request_msg_t;

typedef struct resv_info_request_msg {
        time_t last_update;
} resv_info_request_msg_t;

#define LAYOUTS_DUMP_NOLAYOUT 0x00000001
#define LAYOUTS_DUMP_STATE    0x10000000
typedef struct layout_info_request_msg {
	char* layout_type;
	char* entities;
	char* type;
	uint32_t flags;
} layout_info_request_msg_t;

typedef struct complete_job_allocation {
	uint32_t job_id;
	uint32_t job_rc;
} complete_job_allocation_msg_t;

typedef struct complete_batch_script {
	jobacctinfo_t *jobacct;
	uint32_t job_id;
	uint32_t job_rc;
	uint32_t slurm_rc;
	char *node_name;
	uint32_t user_id;	/* user the job runs as */
} complete_batch_script_msg_t;

typedef struct complete_prolog {
	uint32_t job_id;
	uint32_t prolog_rc;
} complete_prolog_msg_t;

typedef struct step_complete_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint32_t range_first;	/* First node rank within job step's alloc */
	uint32_t range_last;	/* Last node rank within job step's alloc */
 	uint32_t step_rc;	/* largest task return code */
	jobacctinfo_t *jobacct;
} step_complete_msg_t;

typedef struct signal_tasks_msg {
	uint16_t flags;
	uint32_t job_id;
	uint32_t job_step_id;
	uint16_t signal;
} signal_tasks_msg_t;

typedef struct checkpoint_tasks_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	time_t timestamp;
	char *image_dir;
} checkpoint_tasks_msg_t;

typedef struct epilog_complete_msg {
	uint32_t job_id;
	uint32_t return_code;
	char    *node_name;
} epilog_complete_msg_t;

#define REBOOT_FLAGS_ASAP 0x0001	/* Drain to reboot ASAP */
typedef struct reboot_msg {
	char *features;
	uint16_t flags;
	char *node_list;
} reboot_msg_t;

typedef struct shutdown_msg {
	uint16_t options;
} shutdown_msg_t;

typedef struct last_update_msg {
	time_t last_update;
} last_update_msg_t;

typedef struct set_debug_flags_msg {
	uint64_t debug_flags_minus;
	uint64_t debug_flags_plus;
} set_debug_flags_msg_t;

typedef struct set_debug_level_msg {
	uint32_t debug_level;
} set_debug_level_msg_t;

typedef struct job_step_specs {
	uint16_t ckpt_interval;	/* checkpoint creation interval (minutes) */
	char *ckpt_dir; 	/* path to store checkpoint image files */
	uint32_t cpu_count;	/* count of required processors */
	uint32_t cpu_freq_gov;  /* cpu frequency governor */
	uint32_t cpu_freq_max;  /* Maximum cpu frequency  */
	uint32_t cpu_freq_min;  /* Minimum cpu frequency  */
	uint16_t exclusive;	/* 1 if CPUs not shared with other steps */
	char *features;		/* required node features, default NONE */
	char *gres;		/* generic resources required */
	char *host;		/* host to contact initiating srun */
	uint16_t immediate;	/* 1 if allocate to run or fail immediately,
				 * 0 if to be queued awaiting resources */
	uint32_t job_id;	/* job ID */
	uint64_t pn_min_memory; /* minimum real memory per node OR
				 * real memory per CPU | MEM_PER_CPU,
				 * default=0 (use job limit) */
	char *name;		/* name of the job step, default "" */
	char *network;		/* network use spec */
	uint32_t min_nodes;	/* minimum number of nodes required by job,
				 * default=0 */
	uint32_t max_nodes;	/* maximum number of nodes usable by job,
				 * default=0 */
	uint8_t no_kill;	/* 1 if no kill on node failure */
	char *node_list;	/* list of required nodes */
	uint32_t num_tasks;	/* number of tasks required */
	uint8_t overcommit;     /* flag, 1 to allow overcommit of processors,
				   0 to disallow overcommit. default is 0 */
	uint16_t plane_size;	/* plane size when task_dist =
				   SLURM_DIST_PLANE */
	uint16_t port;		/* port to contact initiating srun */
	uint16_t relative;	/* first node to use of job's allocation */
	uint16_t resv_port_cnt;	/* reserve ports for MPI if set */
	uint32_t step_id;	/* Desired step ID or NO_VAL */
	uint32_t srun_pid;	/* PID of srun command, also see host */
	uint32_t task_dist;	/* see enum task_dist_state in slurm.h */
	uint32_t time_limit;	/* maximum run time in minutes, default is
				 * partition limit */
	uint32_t user_id;	/* user the job runs as */
} job_step_create_request_msg_t;

typedef struct job_step_create_response_msg {
	uint32_t job_step_id;		/* assigned job step id */
	char *resv_ports;		/* reserved ports */
	slurm_step_layout_t *step_layout; /* information about how the
                                           * step is laid out */
	slurm_cred_t *cred;    	  /* slurm job credential */
	dynamic_plugin_data_t *select_jobinfo;	/* select opaque data type */
	dynamic_plugin_data_t *switch_job;	/* switch opaque data type */
	uint16_t use_protocol_ver;   /* Lowest protocol version running on
				      * the slurmd's in this step.
				      */
} job_step_create_response_msg_t;

#define LAUNCH_PARALLEL_DEBUG	0x00000001
#define LAUNCH_MULTI_PROG	0x00000002
#define LAUNCH_PTY		0x00000004
#define LAUNCH_BUFFERED_IO	0x00000008
#define LAUNCH_LABEL_IO		0x00000010
#define LAUNCH_USER_MANAGED_IO	0x00000020
#define LAUNCH_NO_ALLOC 	0x00000040

typedef struct launch_tasks_request_msg {
	uint32_t  job_id;
	uint32_t  job_step_id;
	uint32_t  node_offset;	/* pack job node offset of NO_VAL */
	uint32_t  pack_jobid;	/* pack job ID or NO_VAL */
	uint32_t  pack_nnodes;	/* total task count for entire pack job */
	uint32_t  pack_ntasks;	/* total task count for entire pack job */
	uint16_t *pack_task_cnts; /* Number of tasks on each node in pack job */
	uint32_t  pack_offset;	/* pack job offset of NO_VAL */
	uint32_t  pack_task_offset;/* pack job task ID offset of NO_VAL */
	char     *pack_node_list;  /* Pack step node list */
	uint32_t  nnodes;	/* number of nodes in this job step       */
	uint32_t  ntasks;	/* number of tasks in this job step   */
	uint16_t  ntasks_per_board;/* number of tasks to invoke on each board */
	uint16_t  ntasks_per_core; /* number of tasks to invoke on each core */
	uint16_t  ntasks_per_socket;/* number of tasks to invoke on
				     * each socket */
	uint32_t  uid;
	char     *user_name;
	uint32_t  gid;
	uint32_t  ngids;
	uint32_t *gids;
	uint64_t  job_mem_lim;	/* MB of memory reserved by job per node OR
				 * real memory per CPU | MEM_PER_CPU,
				 * default=0 (no limit) */
	uint64_t  step_mem_lim;	/* MB of memory reserved by step */
	uint16_t  *tasks_to_launch;
	uint32_t  envc;
	uint32_t  argc;
	uint16_t  node_cpus;
	uint16_t  cpus_per_task;
	char    **env;
	char    **argv;
	char     *cwd;
	uint16_t cpu_bind_type;	/* --cpu-bind=                    */
	char     *cpu_bind;	/* binding map for map/mask_cpu           */
	uint16_t mem_bind_type;	/* --mem-bind=                    */
	char     *mem_bind;	/* binding map for tasks to memory        */
	uint16_t accel_bind_type; /* --accel-bind= */
	uint16_t  num_resp_port;
	uint16_t  *resp_port;   /* array of available response ports      */

        /* Distribution at the lowest level of logical processor (lllp) */
	uint32_t task_dist;  /* --distribution=, -m dist	*/
	uint32_t flags;		/* See LAUNCH_* flags defined above */
	uint32_t **global_task_ids;
	slurm_addr_t orig_addr;	  /* where message really came from for io */
	uint8_t open_mode;	/* stdout/err append or truncate */
	char *acctg_freq;	/* accounting polling intervals */
	uint32_t cpu_freq_min;  /* Minimum cpu frequency  */
	uint32_t cpu_freq_max;  /* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;  /* cpu frequency governor */
	uint16_t job_core_spec;	/* Count of specialized cores */

	/********** START "normal" IO only options **********/
	/* These options are ignored if user_managed_io is 1 */
	char     *ofname; /* stdout filename pattern */
	char     *efname; /* stderr filename pattern */
	char     *ifname; /* stdin filename pattern */
	uint16_t  num_io_port;
	uint16_t  *io_port;  /* array of available client IO listen ports */
	/**********  END  "normal" IO only options **********/

	uint32_t profile;
	char     *task_prolog;
	char     *task_epilog;

	uint16_t   slurmd_debug; /* remote slurmd debug level */

	slurm_cred_t *cred;	/* job credential            */
	dynamic_plugin_data_t *switch_job; /* switch credential for the job */
	job_options_t options;  /* Arbitrary job options */
	char *complete_nodelist;
	char *ckpt_dir;		/* checkpoint path */
	char *restart_dir;	/* restart from checkpoint if set */
	char **spank_job_env;
	uint32_t spank_job_env_size;
	dynamic_plugin_data_t *select_jobinfo; /* select context, opaque data */
	char *alias_list;	/* node name/address/hostnamne aliases */
	char *partition;	/* partition that job is running in */

	/* only filled out if step is SLURM_EXTERN_CONT */
	uint16_t x11;			/* X11 forwarding setup flags */
	char *x11_magic_cookie;		/* X11 auth cookie to abuse */
	char *x11_target_host;		/* X11 login node to connect back to */
	uint16_t x11_target_port;	/* X11 target port */
} launch_tasks_request_msg_t;

typedef struct task_user_managed_io_msg {
	uint32_t task_id;
} task_user_managed_io_msg_t;

typedef struct partition_info partition_desc_msg_t;

typedef struct return_code_msg {
	uint32_t return_code;
} return_code_msg_t;
typedef struct return_code2_msg {
	uint32_t return_code;
	char *err_msg;
} return_code2_msg_t;

typedef struct {
	slurmdb_cluster_rec_t *working_cluster_rec;
} reroute_msg_t;

/* defined in slurm.h
typedef struct network_callerid_msg {
	unsigned char ip_src[16];
	unsigned char ip_dst[16];
	uint32_t port_src;
	uint32_t port_dst;
	int32_t af;	// NOTE: un/packed as uint32_t
} network_callerid_msg_t; */

typedef struct network_callerid_resp {
	uint32_t job_id;
	uint32_t return_code;
	char *node_name;
} network_callerid_resp_t;

typedef struct composite_msg {
	slurm_addr_t sender;	/* address of sending node/port */
	List	 msg_list;
} composite_msg_t;

typedef struct set_fs_dampening_factor_msg {
	uint16_t dampening_factor;
} set_fs_dampening_factor_msg_t;

/* Note: We include the node list here for reliable cleanup on XCPU systems.
 *
 * Note: We include select_jobinfo here in addition to the job launch
 * RPC in order to ensure reliable clean-up of a BlueGene partition in
 * the event of some launch failure or race condition preventing slurmd
 * from getting the MPIRUN_PARTITION at that time. It is needed for
 * the job epilog. */

#define SIG_OOM		253	/* Dummy signal value for out of memory
				 * (OOM) notification. Exist status reported as
				 * 0:125 (0x80 is the signal flag and
				 * 253 - 128 = 125) */
#define SIG_TERM_KILL	991	/* Send SIGCONT + SIGTERM + SIGKILL */
#define SIG_UME		992	/* Dummy signal value for uncorrectable memory
				 * error (UME) notification */
#define SIG_REQUEUED	993	/* Dummy signal value to job requeue */
#define SIG_PREEMPTED	994	/* Dummy signal value for job preemption */
#define SIG_DEBUG_WAKE	995	/* Dummy signal value to wake procs stopped
				 * for debugger */
#define SIG_TIME_LIMIT	996	/* Dummy signal value for time limit reached */
#define SIG_ABORT	997	/* Dummy signal value to abort a job */
#define SIG_NODE_FAIL	998	/* Dummy signal value to signify node failure */
#define SIG_FAILURE	999	/* Dummy signal value to signify sys failure */
typedef struct kill_job_msg {
	uint32_t job_id;
	uint32_t job_state;
	uint32_t job_uid;
	char *nodes;
	dynamic_plugin_data_t *select_jobinfo;	/* opaque data type */
	char **spank_job_env;
	uint32_t spank_job_env_size;
	time_t   start_time;	/* time of job start, track job requeue */
	uint32_t step_id;
	time_t   time;		/* slurmctld's time of request */
} kill_job_msg_t;

typedef struct job_time_msg {
	uint32_t job_id;
	time_t expiration_time;
} job_time_msg_t;

typedef struct reattach_tasks_request_msg {
	uint32_t     job_id;
	uint32_t     job_step_id;
	uint16_t     num_resp_port;
	uint16_t    *resp_port; /* array of available response ports */
	uint16_t     num_io_port;
	uint16_t    *io_port;   /* array of available client IO ports */
	slurm_cred_t *cred;      /* used only a weak authentication mechanism
				   for the slurmstepd to use when connecting
				   back to the client */
} reattach_tasks_request_msg_t;

typedef struct reattach_tasks_response_msg {
	char     *node_name;
	uint32_t  return_code;
	uint32_t  ntasks;       /* number of tasks on this node     */
	uint32_t *gtids;        /* Global task id assignments       */
	uint32_t *local_pids;   /* list of process ids on this node */
	char     **executable_names; /* array of length "ntasks"    */
} reattach_tasks_response_msg_t;

typedef struct prolog_launch_msg {
	char *alias_list;		/* node name/address/hostnamne aliases */
	slurm_cred_t *cred;
	uint32_t gid;
	uint32_t job_id;		/* slurm job_id */
	uint64_t job_mem_limit;		/* job's memory limit, passed via cred */
	uint32_t nnodes;			/* count of nodes, passed via cred */
	char *nodes;			/* list of nodes allocated to job_step */
	char *partition;		/* partition the job is running in */
	dynamic_plugin_data_t *select_jobinfo;	/* opaque data type */
	char **spank_job_env;		/* SPANK job environment variables */
	uint32_t spank_job_env_size;	/* size of spank_job_env */
	char *std_err;			/* pathname of stderr */
	char *std_out;			/* pathname of stdout */
	uint32_t uid;
	char *user_name;		/* job's user name */
	char *work_dir;			/* full pathname of working directory */
	uint16_t x11;			/* X11 forwarding setup flags */
	char *x11_magic_cookie;		/* X11 auth cookie to abuse */
	char *x11_target_host;		/* X11 login node to connect back to */
	uint16_t x11_target_port;	/* X11 target port */
} prolog_launch_msg_t;

typedef struct batch_job_launch_msg {
	char *account;          /* account under which the job is running */
	uint32_t array_job_id;	/* job array master job ID */
	uint32_t array_task_id;	/* job array ID or NO_VAL */
	uint32_t job_id;
	uint32_t step_id;
	uint32_t uid;
	uint32_t gid;
	char    *user_name;
	uint32_t ngids;
	uint32_t *gids;
	uint32_t ntasks;	/* number of tasks in this job         */
	uint32_t num_cpu_groups;/* elements in below cpu arrays */
	uint16_t cpu_bind_type;	/* This currently does not do anything
				 * but here in case we wanted to bind
				 * the batch step differently than
				 * using all the cpus in the
				 * allocation. */
	char     *cpu_bind;	/* This currently does not do anything
				 * but here in case we wanted to bind
				 * the batch step differently than
				 * using all the cpus in the
				 * allocation. */
	uint16_t *cpus_per_node;/* cpus per node */
	uint32_t *cpu_count_reps;/* how many nodes have same cpu count */
	uint16_t cpus_per_task;	/* number of CPUs requested per task */
	uint16_t job_core_spec;	/* Count of specialized cores */
	char *alias_list;	/* node name/address/hostnamne aliases */
	char *nodes;		/* list of nodes allocated to job_step */
	uint32_t profile;       /* what to profile for the batch step */
	char *script;		/* the actual job script, default NONE */
	char *std_err;		/* pathname of stderr */
	char *std_in;		/* pathname of stdin */
	char *qos;              /* qos the job is running under */
	char *std_out;		/* pathname of stdout */
	char *work_dir;		/* full pathname of working directory */
	char *ckpt_dir;		/* location to store checkpoint image */
	char *restart_dir;	/* retart execution from image in this dir */
	uint32_t argc;
	char **argv;
	uint32_t envc;		/* element count in environment */
	char **environment;	/* environment variables to set for job,
				 *   name=value pairs, one per line */
	dynamic_plugin_data_t *select_jobinfo;	/* opaque data type */
	slurm_cred_t *cred;
	uint8_t open_mode;	/* stdout/err append or truncate */
	uint8_t overcommit;	/* if resources being over subscribed */
	char    *partition;	/* partition used to run job */
	uint64_t pn_min_memory;  /* minimum real memory per node OR
				  * real memory per CPU | MEM_PER_CPU,
				  * default=0 (no limit) */
	char *acctg_freq;	/* accounting polling intervals	*/
	uint32_t cpu_freq_min;  /* Minimum cpu frequency  */
	uint32_t cpu_freq_max;  /* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;  /* cpu frequency governor */
	uint64_t job_mem;	/* memory limit for job		*/
	uint16_t restart_cnt;	/* batch job restart count	*/
	char **spank_job_env;	/* SPANK job environment variables */
	uint32_t spank_job_env_size;	/* size of spank_job_env */
	char *resv_name;        /* job's reservation */
} batch_job_launch_msg_t;

typedef struct job_id_request_msg {
	uint32_t job_pid;	/* local process_id of a job */
} job_id_request_msg_t;

typedef struct job_id_response_msg {
	uint32_t job_id;	/* slurm job_id */
	uint32_t return_code;	/* slurm return code */
} job_id_response_msg_t;

typedef struct srun_exec_msg {
	uint32_t job_id;	/* slurm job_id */
	uint32_t step_id;	/* step_id or NO_VAL */
	uint32_t argc;		/* argument count */
	char **  argv;		/* program arguments */
} srun_exec_msg_t;

typedef struct checkpoint_msg {
	uint16_t op;		/* checkpoint operation, see enum check_opts */
	uint16_t data;		/* operation specific data */
	uint32_t job_id;	/* slurm job_id */
	uint32_t step_id;	/* slurm step_id */
	char *image_dir;	/* locate to store the context images.
				 * NULL for default */
} checkpoint_msg_t;

typedef struct checkpoint_comp_msg {
	uint32_t job_id;	/* slurm job_id */
	uint32_t step_id;	/* slurm step_id */
	time_t   begin_time;	/* time checkpoint began */
	uint32_t error_code;	/* error code on failure */
	char *   error_msg;	/* error message on failure */
} checkpoint_comp_msg_t;

typedef struct checkpoint_task_comp_msg {
	uint32_t job_id;	/* slurm job_id */
	uint32_t step_id;	/* slurm step_id */
	uint32_t task_id;	/* task id */
	time_t   begin_time;	/* time checkpoint began */
	uint32_t error_code;	/* error code on failure */
	char *   error_msg;	/* error message on failure */
} checkpoint_task_comp_msg_t;

typedef struct checkpoint_resp_msg {
	time_t   event_time;	/* time of checkpoint start/finish */
	uint32_t error_code;	/* error code on failure */
	char   * error_msg;	/* error message on failure */
} checkpoint_resp_msg_t;

typedef struct kvs_get_msg {
	uint32_t task_id;	/* job step's task id */
	uint32_t size;		/* count of tasks in job */
	uint16_t port;		/* port to be sent the kvs data */
	char * hostname;	/* hostname to be sent the kvs data */
} kvs_get_msg_t;

enum compress_type {
	COMPRESS_OFF = 0x0,	/* no compression */
	COMPRESS_ZLIB,		/* zlib (aka gzip) compression */
	COMPRESS_LZ4		/* lz4 compression */
};

typedef struct file_bcast_msg {
	char *fname;		/* name of the destination file */
	uint32_t block_no;	/* block number of this data */
	uint16_t last_block;	/* last block of bcast if set (flag) */
	uint16_t force;		/* replace existing file if set (flag) */
	uint16_t compress;	/* compress file if set, use compress_type */
	uint16_t modes;		/* access rights for destination file */
	uint32_t uid;		/* owner for destination file */
	char *user_name;
	uint32_t gid;		/* group for destination file */
	time_t atime;		/* last access time for destination file */
	time_t mtime;		/* last modification time for dest file */
	sbcast_cred_t *cred;	/* credential for the RPC */
	uint32_t block_len;	/* length of this data block */
	uint64_t block_offset;	/* offset for this data block */
	uint32_t uncomp_len;	/* uncompressed length of this data block */
	char *block;		/* data for this block */
	uint64_t file_size;	/* file size */
} file_bcast_msg_t;

typedef struct multi_core_data {
	uint16_t boards_per_node;	/* boards per node required by job   */
	uint16_t sockets_per_board;	/* sockets per board required by job */
	uint16_t sockets_per_node;	/* sockets per node required by job */
	uint16_t cores_per_socket;	/* cores per cpu required by job */
	uint16_t threads_per_core;	/* threads per core required by job */

	uint16_t ntasks_per_board;  /* number of tasks to invoke on each board*/
	uint16_t ntasks_per_socket; /* number of tasks to invoke on each socket */
	uint16_t ntasks_per_core;   /* number of tasks to invoke on each core */
	uint16_t plane_size;        /* plane size when task_dist = SLURM_DIST_PLANE */
} multi_core_data_t;

typedef struct pty_winsz {
	uint16_t cols;
	uint16_t rows;
} pty_winsz_t;

typedef struct forward_data_msg {
	char *address;
	uint32_t len;
	char *data;
} forward_data_msg_t;

/* suspend_msg_t variant for internal slurm daemon communications */
typedef struct suspend_int_msg {
	uint8_t  indf_susp;     /* non-zero if being suspended indefinitely */
	uint16_t job_core_spec;	/* Count of specialized cores */
	uint32_t job_id;        /* slurm job_id */
	uint16_t op;            /* suspend operation, see enum suspend_opts */
	void *   switch_info;	/* opaque data for switch plugin */
} suspend_int_msg_t;

typedef struct ping_slurmd_resp_msg {
	uint32_t cpu_load;	/* CPU load * 100 */
	uint64_t free_mem;	/* Free memory in MiB */
} ping_slurmd_resp_msg_t;

typedef struct license_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
} license_info_request_msg_t;

/*****************************************************************************\
 * Slurm API Message Types
\*****************************************************************************/
typedef struct slurm_node_registration_status_msg {
	char *arch;
	uint16_t cores;
	uint16_t cpus;
	uint32_t cpu_load;	/* CPU load * 100 */
	uint64_t free_mem;	/* Free memory in MiB */
	char *cpu_spec_list;	/* list of specialized CPUs */
	acct_gather_energy_t *energy;
	char *features_active;	/* Currently active node features */
	char *features_avail;	/* Available node features */
	Buf gres_info;		/* generic resource info */
	uint32_t hash_val;      /* hash value of slurm.conf and included files
				 * existing on node */
	uint32_t job_count;	/* number of associate job_id's */
	uint32_t *job_id;	/* IDs of running job (if any) */
	char *node_name;
	uint16_t boards;
	char *os;
	uint64_t real_memory;
	time_t slurmd_start_time;
	uint32_t status;	/* node status code, same as return codes */
	uint16_t startup;	/* slurmd just restarted */
	uint32_t *step_id;	/* IDs of running job steps (if any) */
	uint16_t sockets;
	switch_node_info_t *switch_nodeinfo;	/* set only if startup != 0 */
	uint16_t threads;
	time_t timestamp;
	uint32_t tmp_disk;
	uint32_t up_time;	/* seconds since reboot */
	char *version;
} slurm_node_registration_status_msg_t;

typedef struct requeue_msg {
	uint32_t job_id;	/* slurm job ID (number) */
	char *   job_id_str;	/* slurm job ID (string) */
	uint32_t state;        /* JobExitRequeue | Hold */
} requeue_msg_t;

typedef struct slurm_event_log_msg {
	uint16_t level;		/* Message level, from log.h */
	char *   string;	/* String for slurmctld to log */
} slurm_event_log_msg_t;

typedef struct {
	uint32_t cluster_id;	/* cluster id of cluster making request */
	void    *data;		/* Unpacked buffer
				 * Only populated on the receiving side. */
	Buf      data_buffer;	/* Buffer that holds an unpacked data type.
				 * Only populated on the sending side. */
	uint32_t data_offset;	/* DON'T PACK: offset where body part of buffer
				 * starts -- the part that gets sent. */
	uint16_t data_type;	/* date type to unpack */
	uint16_t data_version;	/* Version that data is packed with */
	uint64_t fed_siblings;	/* sibling bitmap of job */
	uint32_t job_id;	/* job_id of job - set in job_desc on receiving
				 * side */
	uint32_t return_code;   /* return code of job */
	time_t   start_time;    /* time sibling job started */
	char    *resp_host;     /* response host for interactive allocations */
	uint32_t req_uid;       /* uid of user making the request. e.g if a
				   cancel is happening from a user and being
				   passed to a remote then the uid will be the
				   user and not the SlurmUser. */
	uint16_t sib_msg_type; /* fed_job_update_type */
} sib_msg_t;

typedef struct {
	List my_list;		/* this list could be of any type as long as it
				 * is handled correctly on both ends */
} ctld_list_msg_t;

/*****************************************************************************\
 *      ACCOUNTING PUSHS
\*****************************************************************************/

typedef struct {
	List update_list; /* of type slurmdb_update_object_t *'s */
	uint16_t rpc_version;
} accounting_update_msg_t;

typedef struct {
	uint32_t job_id;	/* ID of job of request */
} spank_env_request_msg_t;

typedef struct {
	uint32_t spank_job_env_size;
	char **spank_job_env;	/* spank environment */
} spank_env_responce_msg_t;

typedef struct slurm_ctl_conf slurm_ctl_conf_info_msg_t;
/*****************************************************************************\
 *	SLURM MESSAGE INITIALIZATION
\*****************************************************************************/

/*
 * slurm_msg_t_init - initialize a slurm message
 * OUT msg - pointer to the slurm_msg_t structure which will be initialized
 */
extern void slurm_msg_t_init (slurm_msg_t *msg);

/*
 * slurm_msg_t_copy - initialize a slurm_msg_t structure "dest" with
 *	values from the "src" slurm_msg_t structure.
 * IN src - Pointer to the initialized message from which "dest" will
 *	be initialized.
 * OUT dest - Pointer to the slurm_msg_t which will be initialized.
 * NOTE: the "dest" structure will contain pointers into the contents of "src".
 */
extern void slurm_msg_t_copy(slurm_msg_t *dest, slurm_msg_t *src);

extern void slurm_destroy_char(void *object);
extern void slurm_destroy_uint32_ptr(void *object);
/* here to add \\ to all \" in a string this needs to be xfreed later */
extern char *slurm_add_slash_to_quotes(char *str);
extern List slurm_copy_char_list(List char_list);
extern int slurm_addto_char_list(List char_list, char *names);
extern int slurm_addto_mode_char_list(List char_list, char *names, int mode);
extern int slurm_addto_step_list(List step_list, char *names);
extern int slurm_char_list_copy(List dst, List src);
extern char *slurm_char_list_to_xstr(List char_list);
extern int slurm_find_char_in_list(void *x, void *key);
extern int slurm_sort_char_list_asc(void *, void *);
extern int slurm_sort_char_list_desc(void *, void *);

/* free message functions */
extern void slurm_free_checkpoint_tasks_msg(checkpoint_tasks_msg_t * msg);
extern void slurm_free_last_update_msg(last_update_msg_t * msg);
extern void slurm_free_return_code_msg(return_code_msg_t * msg);
extern void slurm_free_reroute_msg(reroute_msg_t *msg);
extern void slurm_free_job_alloc_info_msg(job_alloc_info_msg_t * msg);
extern void slurm_free_job_info_request_msg(job_info_request_msg_t *msg);
extern void slurm_free_job_step_info_request_msg(
		job_step_info_request_msg_t *msg);
extern void slurm_free_front_end_info_request_msg(
		front_end_info_request_msg_t *msg);
extern void slurm_free_node_info_request_msg(node_info_request_msg_t *msg);
extern void slurm_free_node_info_single_msg(node_info_single_msg_t *msg);
extern void slurm_free_part_info_request_msg(part_info_request_msg_t *msg);
extern void slurm_free_sib_msg(sib_msg_t *msg);
extern void slurm_free_stats_info_request_msg(stats_info_request_msg_t *msg);
extern void slurm_free_stats_response_msg(stats_info_response_msg_t *msg);
extern void slurm_free_step_alloc_info_msg(step_alloc_info_msg_t * msg);
extern void slurm_free_resv_info_request_msg(resv_info_request_msg_t *msg);
extern void slurm_free_set_debug_flags_msg(set_debug_flags_msg_t *msg);
extern void slurm_free_set_debug_level_msg(set_debug_level_msg_t *msg);
extern void slurm_destroy_assoc_shares_object(void *object);
extern void slurm_free_shares_request_msg(shares_request_msg_t *msg);
extern void slurm_free_shares_response_msg(shares_response_msg_t *msg);
extern void slurm_destroy_priority_factors_object(void *object);
extern void slurm_copy_priority_factors_object(priority_factors_object_t *dest,
					       priority_factors_object_t *src);
extern void slurm_free_priority_factors_request_msg(
	priority_factors_request_msg_t *msg);
extern void slurm_free_forward_data_msg(forward_data_msg_t *msg);
extern void slurm_free_comp_msg_list(void *x);
extern void slurm_free_composite_msg(composite_msg_t *msg);
extern void slurm_free_ping_slurmd_resp(ping_slurmd_resp_msg_t *msg);

#define	slurm_free_timelimit_msg(msg) \
	slurm_free_kill_job_msg(msg)

extern void slurm_free_reboot_msg(reboot_msg_t * msg);

extern void slurm_free_shutdown_msg(shutdown_msg_t * msg);

extern void slurm_free_job_desc_msg(job_desc_msg_t * msg);
extern void slurm_free_event_log_msg(slurm_event_log_msg_t * msg);

extern void
slurm_free_node_registration_status_msg(slurm_node_registration_status_msg_t *
					msg);

extern void slurm_free_job_info(job_info_t * job);
extern void slurm_free_job_info_members(job_info_t * job);

extern void slurm_free_batch_script_msg(char *msg);
extern void slurm_free_job_id_msg(job_id_msg_t * msg);
extern void slurm_free_job_user_id_msg(job_user_id_msg_t * msg);
extern void slurm_free_job_id_request_msg(job_id_request_msg_t * msg);
extern void slurm_free_job_id_response_msg(job_id_response_msg_t * msg);

extern void slurm_free_job_step_id_msg(job_step_id_msg_t *msg);

extern void slurm_free_job_launch_msg(batch_job_launch_msg_t * msg);

extern void slurm_free_update_front_end_msg(update_front_end_msg_t * msg);
extern void slurm_free_update_node_msg(update_node_msg_t * msg);
extern void slurm_free_update_layout_msg(update_layout_msg_t * msg);
extern void slurm_free_update_part_msg(update_part_msg_t * msg);
extern void slurm_free_delete_part_msg(delete_part_msg_t * msg);
extern void slurm_free_resv_desc_msg(resv_desc_msg_t * msg);
extern void slurm_free_resv_name_msg(reservation_name_msg_t * msg);
extern void slurm_free_resv_info_request_msg(resv_info_request_msg_t * msg);
extern void slurm_free_job_step_create_request_msg(
		job_step_create_request_msg_t * msg);
extern void slurm_free_job_step_create_response_msg(
		job_step_create_response_msg_t *msg);
extern void slurm_free_complete_job_allocation_msg(
		complete_job_allocation_msg_t * msg);
extern void slurm_free_prolog_launch_msg(prolog_launch_msg_t * msg);
extern void slurm_free_complete_batch_script_msg(
		complete_batch_script_msg_t * msg);
extern void slurm_free_complete_prolog_msg(
		complete_prolog_msg_t * msg);
extern void slurm_free_launch_tasks_request_msg(
		launch_tasks_request_msg_t * msg);
extern void slurm_free_launch_tasks_response_msg(
		launch_tasks_response_msg_t * msg);
extern void slurm_free_task_user_managed_io_stream_msg(
		task_user_managed_io_msg_t *msg);
extern void slurm_free_task_exit_msg(task_exit_msg_t * msg);
extern void slurm_free_signal_tasks_msg(signal_tasks_msg_t * msg);
extern void slurm_free_reattach_tasks_request_msg(
		reattach_tasks_request_msg_t * msg);
extern void slurm_free_reattach_tasks_response_msg(
		reattach_tasks_response_msg_t * msg);
extern void slurm_free_kill_job_msg(kill_job_msg_t * msg);
extern void slurm_free_update_job_time_msg(job_time_msg_t * msg);
extern void slurm_free_job_step_kill_msg(job_step_kill_msg_t * msg);
extern void slurm_free_epilog_complete_msg(epilog_complete_msg_t * msg);
extern void slurm_free_srun_job_complete_msg(srun_job_complete_msg_t * msg);
extern void slurm_free_srun_exec_msg(srun_exec_msg_t *msg);
extern void slurm_free_srun_ping_msg(srun_ping_msg_t * msg);
extern void slurm_free_srun_node_fail_msg(srun_node_fail_msg_t * msg);
extern void slurm_free_srun_step_missing_msg(srun_step_missing_msg_t * msg);
extern void slurm_free_srun_timeout_msg(srun_timeout_msg_t * msg);
extern void slurm_free_srun_user_msg(srun_user_msg_t * msg);
extern void slurm_free_checkpoint_msg(checkpoint_msg_t *msg);
extern void slurm_free_checkpoint_comp_msg(checkpoint_comp_msg_t *msg);
extern void slurm_free_checkpoint_task_comp_msg(checkpoint_task_comp_msg_t *msg);
extern void slurm_free_checkpoint_resp_msg(checkpoint_resp_msg_t *msg);
extern void slurm_free_suspend_msg(suspend_msg_t *msg);
extern void slurm_free_suspend_int_msg(suspend_int_msg_t *msg);
extern void slurm_free_top_job_msg(top_job_msg_t *msg);
extern void slurm_free_update_step_msg(step_update_request_msg_t * msg);
extern void slurm_free_resource_allocation_response_msg_members (
	resource_allocation_response_msg_t * msg);
extern void slurm_free_resource_allocation_response_msg (
		resource_allocation_response_msg_t * msg);
extern void slurm_free_job_step_create_response_msg(
		job_step_create_response_msg_t * msg);
extern void slurm_free_submit_response_response_msg(
		submit_response_msg_t * msg);
extern void slurm_free_ctl_conf(slurm_ctl_conf_info_msg_t * config_ptr);
extern void slurm_free_job_info_msg(job_info_msg_t * job_buffer_ptr);
extern void slurm_free_job_step_info_response_msg(
		job_step_info_response_msg_t * msg);
extern void slurm_free_job_step_info_members (job_step_info_t * msg);
extern void slurm_free_front_end_info_msg (front_end_info_msg_t * msg);
extern void slurm_free_front_end_info_members(front_end_info_t * front_end);
extern void slurm_free_node_info_msg(node_info_msg_t * msg);
extern void slurm_free_node_info_members(node_info_t * node);
extern void slurm_free_partition_info_msg(partition_info_msg_t * msg);
extern void slurm_free_partition_info_members(partition_info_t * part);
extern void slurm_free_layout_info_msg(layout_info_msg_t * msg);
extern void slurm_free_layout_info_request_msg(layout_info_request_msg_t * msg);
extern void slurm_free_reservation_info_msg(reserve_info_msg_t * msg);
extern void slurm_free_get_kvs_msg(kvs_get_msg_t *msg);
extern void slurm_free_kvs_comm_set(kvs_comm_set_t *msg);
extern void slurm_free_will_run_response_msg(will_run_response_msg_t *msg);
extern void slurm_free_reserve_info_members(reserve_info_t * resv);
extern void slurm_free_topo_info_msg(topo_info_response_msg_t *msg);
extern void slurm_free_file_bcast_msg(file_bcast_msg_t *msg);
extern void slurm_free_step_complete_msg(step_complete_msg_t *msg);
extern void slurm_free_job_step_stat(void *object);
extern void slurm_free_job_step_pids(void *object);
extern void slurm_free_block_job_info(void *object);
extern void slurm_free_block_info_members(block_info_t *block_info);
extern void slurm_free_block_info(block_info_t *block_info);
extern void slurm_free_block_info_msg(block_info_msg_t *block_info_msg);
extern void slurm_free_block_info_request_msg(
		block_info_request_msg_t *msg);
extern void slurm_free_acct_gather_node_resp_msg(
	acct_gather_node_resp_msg_t *msg);
extern void slurm_free_acct_gather_energy_req_msg(
	acct_gather_energy_req_msg_t *msg);
extern void slurm_free_job_notify_msg(job_notify_msg_t * msg);
extern void slurm_free_ctld_multi_msg(ctld_list_msg_t *msg);

extern void slurm_free_accounting_update_msg(accounting_update_msg_t *msg);
extern void slurm_free_spank_env_request_msg(spank_env_request_msg_t *msg);
extern void slurm_free_spank_env_responce_msg(spank_env_responce_msg_t *msg);
extern void slurm_free_requeue_msg(requeue_msg_t *);
extern int slurm_free_msg_data(slurm_msg_type_t type, void *data);
extern void slurm_free_license_info_request_msg(license_info_request_msg_t *msg);
extern uint32_t slurm_get_return_code(slurm_msg_type_t type, void *data);
extern void slurm_free_network_callerid_msg(network_callerid_msg_t *mesg);
extern void slurm_free_network_callerid_resp(network_callerid_resp_t *resp);
extern void slurm_free_set_fs_dampening_factor_msg(
	set_fs_dampening_factor_msg_t *msg);

extern const char *preempt_mode_string(uint16_t preempt_mode);
extern uint16_t preempt_mode_num(const char *preempt_mode);

extern char *log_num2string(uint16_t inx);
extern uint16_t log_string2num(char *name);

/* Translate a burst buffer numeric value to its equivalent state string */
extern char *bb_state_string(uint16_t state);
/* Translate a burst buffer state string to its equivalent numeric value */
extern uint16_t bb_state_num(char *tok);

/* Convert HealthCheckNodeState numeric value to a string.
 * Caller must xfree() the return value */
extern char *health_check_node_state_str(uint32_t node_state);

extern char *job_reason_string(enum job_state_reason inx);
extern bool job_state_qos_grp_limit(enum job_state_reason state_reason);
extern char *job_share_string(uint16_t shared);
extern char *job_state_string(uint32_t inx);
extern char *job_state_string_compact(uint32_t inx);
extern uint32_t job_state_num(const char *state_name);
extern char *node_state_string(uint32_t inx);
extern char *node_state_string_compact(uint32_t inx);

extern uint16_t power_flags_id(char *power_flags);
extern char    *power_flags_str(uint16_t power_flags);

extern void  private_data_string(uint16_t private_data, char *str, int str_len);
extern void  accounting_enforce_string(uint16_t enforce,
				       char *str, int str_len);
extern char *conn_type_string(enum connection_type conn_type);
extern char *conn_type_string_full(uint16_t *conn_type);
extern char *node_use_string(enum node_use_type node_use);
/* Translate a state enum to a readable string */
extern char *bg_block_state_string(uint16_t state);

/* Translate a Slurm nodelist to a char * of numbers
 * nid000[36-37] -> 36-37
 * IN - hl_in - if NULL will be made from nodelist
 * IN - nodelist - generate hl from list if hl is NULL
 * RET - nid list, needs to be xfreed.
 */
extern char *cray_nodelist2nids(hostlist_t hl_in, char *nodelist);

/* Validate SPANK specified job environment does not contain any invalid
 * names. Log failures using info() */
extern bool valid_spank_job_env(char **spank_job_env,
			        uint32_t spank_job_env_size, uid_t uid);

extern char *trigger_res_type(uint16_t res_type);
extern char *trigger_type(uint32_t trig_type);

/* user needs to xfree return value */
extern char *priority_flags_string(uint16_t priority_flags);

/* user needs to xfree return value */
extern char *reservation_flags_string(uint32_t flags);

/* Functions to convert burst buffer flags between strings and numbers */
extern char *   slurm_bb_flags2str(uint32_t bb_flags);
extern uint32_t slurm_bb_str2flags(char *bb_str);

/* Function to convert enforce type flags between strings and numbers */
extern int parse_part_enforce_type(char *enforce_part_type, uint16_t *param);
extern char * parse_part_enforce_type_2str (uint16_t type);

/* Return true if this cluster_name is in a federation */
extern bool cluster_in_federation(void *ptr, char *cluster_name);

/* Find where cluster_name nodes start in the node_array */
extern int get_cluster_node_offset(char *cluster_name,
				   node_info_msg_t *node_info_ptr);

/*
 * Print the char* given.
 *
 * Each \n will result in a new line.
 * If inx is != -1 it is prepended to the string.
 */
extern void print_multi_line_string(char *user_msg, int inx);

/* Given a protocol opcode return its string
 * description mapping the slurm_msg_type_t
 * to its name.
 */
extern char *rpc_num2string(uint16_t opcode);

#define safe_read(fd, buf, size) do {					\
		int remaining = size;					\
		char *ptr = (char *) buf;				\
		int rc;							\
		while (remaining > 0) {					\
			rc = read(fd, ptr, remaining);			\
			if ((rc == 0) && (remaining == size)) {		\
				debug("%s:%d: %s: safe_read EOF",	\
				      __FILE__, __LINE__, __func__); \
				goto rwfail;				\
			} else if (rc == 0) {				\
				debug("%s:%d: %s: safe_read (%d of %d) EOF", \
				      __FILE__, __LINE__, __func__, \
				      remaining, (int)size);		\
				goto rwfail;				\
			} else if (rc < 0) {				\
				debug("%s:%d: %s: safe_read (%d of %d) failed: %m", \
				      __FILE__, __LINE__, __func__, \
				      remaining, (int)size);		\
				goto rwfail;				\
			} else {					\
				ptr += rc;				\
				remaining -= rc;			\
				if (remaining > 0)			\
					debug3("%s:%d: %s: safe_read (%d of %d) partial read", \
					       __FILE__, __LINE__, __func__, \
					       remaining, (int)size);	\
			}						\
		}							\
	} while (0)

#define safe_write(fd, buf, size) do {					\
		int remaining = size;					\
		char *ptr = (char *) buf;				\
		int rc;							\
		while(remaining > 0) {					\
			rc = write(fd, ptr, remaining);			\
 			if (rc < 0) {					\
				debug("%s:%d: %s: safe_write (%d of %d) failed: %m", \
				      __FILE__, __LINE__, __func__, \
				      remaining, (int)size);		\
				goto rwfail;				\
			} else {					\
				ptr += rc;				\
				remaining -= rc;			\
				if (remaining > 0)			\
					debug3("%s:%d: %s: safe_write (%d of %d) partial write", \
					       __FILE__, __LINE__, __func__, \
					       remaining, (int)size);	\
			}						\
		}							\
	} while (0)

#endif
