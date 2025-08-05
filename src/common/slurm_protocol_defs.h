/****************************************************************************\
 *  slurm_protocol_defs.h - definitions used for RPCs
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>.
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

#ifndef _SLURM_PROTOCOL_DEFS_H
#define _SLURM_PROTOCOL_DEFS_H

#include <inttypes.h>
#include <sys/wait.h>

#ifdef HAVE_SYSCTLBYNAME
#if defined(__FreeBSD__)
#include <sys/types.h>
#else
#include <sys/param.h>
#endif
# include <sys/sysctl.h>
#endif

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/msg_type.h"
#include "src/common/persist_conn.h"
#include "src/common/part_record.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_step_layout.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/working_cluster.h"
#include "src/common/xassert.h"
#include "src/interfaces/cred.h"

/*
 * This is what the UID and GID accessors return on error.
 * The value is currently RedHat Linux's ID for the user "nobody".
 */
#define SLURM_AUTH_NOBODY 99

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
#define IS_JOB_REVOKED(_X)		\
	(_X->job_state & JOB_REVOKED)
#define IS_JOB_SIGNALING(_X)		\
	(_X->job_state & JOB_SIGNALING)
#define IS_JOB_STAGE_OUT(_X)		\
	(_X->job_state & JOB_STAGE_OUT)

/* DB FLAG state */
#define IS_JOB_IN_DB(_X) \
	(_X->db_flags & SLURMDB_JOB_FLAG_START_R)

/* Defined node states */
#define IS_NODE_UNKNOWN(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_UNKNOWN)
#define IS_NODE_DOWN(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_DOWN)
#define IS_NODE_IDLE(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_IDLE)
#define IS_NODE_ALLOCATED(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_ALLOCATED)
#define IS_NODE_MIXED(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_MIXED)
#define IS_NODE_FUTURE(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_FUTURE)

/* Derived node states */
#define IS_NODE_BLOCKED(_X)		\
	(_X->node_state & NODE_STATE_BLOCKED)
#define IS_NODE_CLOUD(_X)		\
	(_X->node_state & NODE_STATE_CLOUD)
#define IS_NODE_DRAIN(_X)		\
	(_X->node_state & NODE_STATE_DRAIN)
#define IS_NODE_DRAINING(_X)		\
	((_X->node_state & NODE_STATE_DRAIN) \
	 && (IS_NODE_ALLOCATED(_X) || IS_NODE_MIXED(_X)))
#define IS_NODE_DRAINED(_X)		\
	(IS_NODE_DRAIN(_X) && !IS_NODE_DRAINING(_X))
#define IS_NODE_DYNAMIC_FUTURE(_X)		\
	(_X->node_state & NODE_STATE_DYNAMIC_FUTURE)
#define IS_NODE_DYNAMIC_NORM(_X)		\
	(_X->node_state & NODE_STATE_DYNAMIC_NORM)
#define IS_NODE_EXTERNAL(_X)		\
	(_X->node_state & NODE_STATE_EXTERNAL)
#define IS_NODE_COMPLETING(_X)	\
	(_X->node_state & NODE_STATE_COMPLETING)
#define IS_NODE_INVALID_REG(_X)	\
	(_X->node_state & NODE_STATE_INVALID_REG)
#define IS_NODE_PLANNED(_X)		\
	(_X->node_state & NODE_STATE_PLANNED)
#define IS_NODE_POWER_DOWN(_X)		\
	(_X->node_state & NODE_STATE_POWER_DOWN)
#define IS_NODE_POWER_UP(_X)		\
	(_X->node_state & NODE_STATE_POWER_UP)
#define IS_NODE_NO_RESPOND(_X)		\
	(_X->node_state & NODE_STATE_NO_RESPOND)
#define IS_NODE_POWERED_DOWN(_X)		\
	(_X->node_state & NODE_STATE_POWERED_DOWN)
#define IS_NODE_POWERING_DOWN(_X)	\
	(_X->node_state & NODE_STATE_POWERING_DOWN)
#define IS_NODE_FAIL(_X)		\
	(_X->node_state & NODE_STATE_FAIL)
#define IS_NODE_POWERING_UP(_X)		\
	(_X->node_state & NODE_STATE_POWERING_UP)
#define IS_NODE_MAINT(_X)		\
	(_X->node_state & NODE_STATE_MAINT)
#define IS_NODE_REBOOT_REQUESTED(_X)	\
	(_X->node_state & NODE_STATE_REBOOT_REQUESTED)
#define IS_NODE_REBOOT_ISSUED(_X)	\
	(_X->node_state & NODE_STATE_REBOOT_ISSUED)
#define IS_NODE_RUNNING_JOB(_X)		\
	(_X->comp_job_cnt || _X->run_job_cnt || _X->sus_job_cnt)
#define IS_NODE_RES(_X)		\
	(_X->node_state & NODE_STATE_RES)
#define IS_NODE_REBOOT_ASAP(_X) \
	(IS_NODE_REBOOT_REQUESTED(_X) && IS_NODE_DRAIN(_X))

#define THIS_FILE ((strrchr(__FILE__, '/') ?: __FILE__ - 1) + 1)

#define MINUTE_SECONDS 60
#define HOUR_MINUTES 60
#define HOUR_SECONDS (HOUR_MINUTES * MINUTE_SECONDS)
#define DAY_HOURS 24
#define DAY_MINUTES (DAY_HOURS * HOUR_MINUTES)
#define YEAR_DAYS 365
#define YEAR_MINUTES (YEAR_DAYS * DAY_HOURS * HOUR_MINUTES)
#define YEAR_SECONDS (YEAR_DAYS * DAY_HOURS * HOUR_SECONDS)

/* Read as 'how many X are in a Y' */
#define MSEC_IN_SEC 1000
#define USEC_IN_SEC 1000000
#define NSEC_IN_SEC 1000000000
#define NSEC_IN_USEC 1000
#define NSEC_IN_MSEC 1000000

#define SLURMD_REG_FLAG_CONFIGLESS 0x0001
#define SLURMD_REG_FLAG_RESP     0x0002

#define RESV_FREE_STR_USER      SLURM_BIT(0)
#define RESV_FREE_STR_ACCT      SLURM_BIT(1)
#define RESV_FREE_STR_TRES_BB   SLURM_BIT(2)
/* #define SLURM_BIT(3) reusable 2 versions after 23.11 */
#define RESV_FREE_STR_TRES_LIC  SLURM_BIT(4)
/* #define SLURM_BIT(5) reusable 2 versions after 23.11 */
#define RESV_FREE_STR_GROUP     SLURM_BIT(6)
#define RESV_FREE_STR_COMMENT   SLURM_BIT(7)
#define RESV_FREE_STR_NODES     SLURM_BIT(8)
#define RESV_FREE_STR_TRES      SLURM_BIT(9)

#ifndef __job_record_t_defined
#  define __job_record_t_defined
typedef struct job_record job_record_t;
#endif

/*
 * Prototype for conmgr connection w/o including conmgr.h
 */
typedef struct conmgr_fd_s conmgr_fd_t;

/*****************************************************************************\
 * core api configuration struct
\*****************************************************************************/
typedef struct forward {
	slurm_node_alias_addrs_t alias_addrs;
	uint16_t   cnt;		/* number of nodes to forward to */
	uint16_t   init;	/* tell me it has been set (FORWARD_INIT) */
	char      *nodelist;	/* ranged string of who to forward the
				 * message to */
	uint32_t   timeout;	/* original timeout increments */
	uint16_t   tree_width;  /* what the treewidth should be */
	uint16_t   tree_depth;	/* tree depth of this set of nodes */
} forward_t;

#define FORWARD_INITIALIZER \
	{ \
		.init = FORWARD_INIT, \
	}

/*core api protocol message structures */
typedef struct slurm_protocol_header {
	uint16_t version;
	uint16_t flags;
	uint16_t msg_type; /* really slurm_msg_type_t but needs to be
			      uint16_t for packing purposes. */
	uint32_t body_length;
	uint16_t ret_cnt;
	forward_t forward;
	slurm_addr_t orig_addr;
	list_t *ret_list;
} header_t;

typedef struct forward_struct {
	slurm_node_alias_addrs_t *alias_addrs;
	char *buf;
	int buf_len;
	uint16_t fwd_cnt;
	pthread_mutex_t forward_mutex;
	pthread_cond_t notify;
	list_t *ret_list;
	uint32_t timeout;
} forward_struct_t;

typedef struct forward_message {
	forward_struct_t *fwd_struct;
	header_t header;
	int timeout;
} forward_msg_t;

typedef struct slurm_msg {
	slurm_addr_t address;
	void *auth_cred;
	int auth_index;		/* DON'T PACK: zero for normal communication.
				 * index value copied from incoming connection,
				 * so that we'll respond with the same auth
				 * plugin used to connect to us originally.
				 */
	uid_t auth_uid;		/* NEVER PACK. Authenticated uid from auth
				 * credential. Only valid if auth_ids_set is
				 * true. Set to SLURM_AUTH_NOBODY if not set
				 * yet.
				 */
	gid_t auth_gid;		/* NEVER PACK. Authenticated uid from auth
				 * credential. Only valid if auth_ids_set is
				 * true. Set to SLURM_AUTH_NOBODY if not set
				 * yet.
				 */
	bool auth_ids_set;	/* NEVER PACK. True when auth_uid and auth_gid
				 * have been set.
				 * This is a safety measure against handling
				 * a slurm_msg_t that has been xmalloc()'d but
				 * slurm_msg_t_init() was not called since
				 * auth_uid would be root.
				 */
	uid_t restrict_uid;
	bool restrict_uid_set;
	uint32_t body_offset; /* DON'T PACK: offset in buffer where body part of
				 buffer starts. */
	buf_t *buffer;		/* DON'T PACK! ptr to buffer that msg was
				 * unpacked from. */
	persist_conn_t *conn;	/* DON'T PACK OR FREE! this is here to
				 * distinguish a persistent connection from a
				 * normal connection. It should be filled in
				 * with the connection before sending the
				 * message so that it is handled correctly. */
	conmgr_fd_t *conmgr_fd; /* msg originates from conmgr connection. */
	void *data;
	uint16_t flags;
	uint8_t hash_index;	/* DON'T PACK: zero for normal communication.
				 * index value copied from incoming connection,
				 * so that we'll respond with the same hash
				 * plugin used to connect to us originally.
				 */
	char *tls_cert; /* TLS certificate for server. Only needed when server's
			 * cert is not already trusted (i.e. signed by a cert in
			 * our trust store) */
	void *tls_conn; /* TLS connection associated with conn_fd used for
			 * sending this message and receiving a response */

	uint16_t msg_type; /* really a slurm_msg_type_t but needs to be
			    * this way for packing purposes.  message type */
	uint16_t protocol_version; /* DON'T PACK!  Only used if
				    * message coming from non-default
				    * slurm protocol.  Initted to
				    * NO_VAL meaning use the default. */
	/* The following were all added for the forward.c code */
	forward_t forward;
	forward_struct_t *forward_struct;
	slurm_addr_t orig_addr;
	list_t *ret_list;
} slurm_msg_t;

#define SLURM_MSG_INITIALIZER \
	{ \
		.auth_uid = SLURM_AUTH_NOBODY, \
		.auth_gid = SLURM_AUTH_NOBODY, \
		.msg_type = NO_VAL16, \
		.protocol_version = NO_VAL16, \
		.flags = SLURM_PROTOCOL_NO_FLAGS, \
		.forward = FORWARD_INITIALIZER, \
	}

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
	list_t *acct_list;
	list_t *user_list;
} shares_request_msg_t;

typedef struct shares_response_msg {
	list_t *assoc_shares_list; /* list of assoc_shares_object_t *'s */
	uint64_t tot_shares;
	uint32_t tres_cnt;
	char **tres_names;
} shares_response_msg_t;

typedef struct job_notify_msg {
	char *   message;
	slurm_step_id_t step_id;
} job_notify_msg_t;

typedef struct job_id_msg {
	uint32_t job_id;
	uint16_t show_flags;
} job_id_msg_t;

typedef struct job_user_id_msg {
	uint32_t user_id;
	uint16_t show_flags;
} job_user_id_msg_t;

typedef struct job_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
	list_t *job_ids;	/* Optional list of job_ids, otherwise show all
				 * jobs. */
} job_info_request_msg_t;

typedef struct {
	uint32_t count;
	slurm_selected_step_t *job_ids;
} job_state_request_msg_t;

typedef struct {
	uint16_t show_flags;
	char *container_id;
	uint32_t uid; /* optional filter by UID */
} container_id_request_msg_t;

typedef struct {
	list_t *steps; /* list of slurm_step_id_t* */
} container_id_response_msg_t;

typedef struct job_step_info_request_msg {
	time_t last_update;
	slurm_step_id_t step_id;
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

typedef struct part_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
} part_info_request_msg_t;

typedef struct resv_info_request_msg {
        time_t last_update;
} resv_info_request_msg_t;

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
	char *node_name;
	uint32_t prolog_rc;
} complete_prolog_msg_t;

typedef struct step_complete_msg {
	uint32_t range_first;	/* First node rank within job step's alloc */
	uint32_t range_last;	/* Last node rank within job step's alloc */
	slurm_step_id_t step_id;
 	uint32_t step_rc;	/* largest task return code */
	jobacctinfo_t *jobacct;
	bool send_to_stepmgr;
} step_complete_msg_t;

typedef struct signal_tasks_msg {
	uint16_t flags;
	uint16_t signal;
	slurm_step_id_t step_id;
} signal_tasks_msg_t;

typedef struct epilog_complete_msg {
	uint32_t job_id;
	uint32_t return_code;
	char    *node_name;
} epilog_complete_msg_t;

#define REBOOT_FLAGS_ASAP 0x0001	/* Drain to reboot ASAP */
typedef struct reboot_msg {
	char *features;
	uint16_t flags;
	uint32_t next_state;		/* state after reboot */
	char *node_list;
	char *reason;
} reboot_msg_t;

typedef struct shutdown_msg {
	uint16_t options;
} shutdown_msg_t;

typedef enum {
	SLURMCTLD_SHUTDOWN_ALL = 0,	/* all slurm daemons are shutdown */
	/* = 1 can be reused two versions after 23.11 */
	SLURMCTLD_SHUTDOWN_CTLD = 2,	/* slurmctld only (no core file) */
} slurmctld_shutdown_type_t;

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
	char *container; /* OCI container bundle path */
	char *container_id; /* OCI container ID */
	uint32_t cpu_count;	/* count of required processors */
	uint32_t cpu_freq_gov;  /* cpu frequency governor */
	uint32_t cpu_freq_max;  /* Maximum cpu frequency  */
	uint32_t cpu_freq_min;  /* Minimum cpu frequency  */
	char *cpus_per_tres;	/* semicolon delimited list of TRES=# values */
	uint16_t ntasks_per_core; /* number of tasks that can access each cpu */
	uint16_t ntasks_per_tres;/* number of tasks that can access each gpu */
	char *exc_nodes;	/* comma separated list of nodes excluded
				 * from step's allocation, default NONE */
	char *features;		/* required node features, default NONE */
	uint32_t flags;         /* various flags from step_spec_flags_t */
	char *host;		/* host to contact initiating srun */
	uint16_t immediate;	/* 1 if allocate to run or fail immediately,
				 * 0 if to be queued awaiting resources */
	uint64_t pn_min_memory; /* minimum real memory per node OR
				 * real memory per CPU | MEM_PER_CPU,
				 * default=0 (use job limit) */
	char *name;		/* name of the job step, default "" */
	char *network;		/* network use spec */
	uint32_t min_nodes;	/* minimum number of nodes required by job,
				 * default=0 */
	uint32_t max_nodes;	/* maximum number of nodes usable by job,
				 * default=0 */
	char *mem_per_tres;	/* semicolon delimited list of TRES=# values */
	char *node_list;	/* list of required nodes */
	uint32_t num_tasks;	/* number of tasks required */
	uint16_t plane_size;	/* plane size when task_dist =
				   SLURM_DIST_PLANE */
	uint16_t port;		/* port to contact initiating srun */
	uint16_t relative;	/* first node to use of job's allocation */
	uint16_t resv_port_cnt;	/* reserve ports for MPI if set */
	uint32_t step_het_comp_cnt; /* How many het components in the step. Used
				     * for a het step inside a non-het job
				     * allocation. */
	char *step_het_grps;	/* what het groups are used by step */
	slurm_step_id_t step_id;
	uint32_t array_task_id;	/* Array Task Id, or NO_VAL */
	uint32_t srun_pid;	/* PID of srun command, also see host */
	char *cwd;		/* path derived from cwd or --chdir */
	char *std_err;		/* pathname of step stderr */
	char *std_in;		/* pathname of step stdin */
	char *std_out;		/* pathname of step stdout */
	char *submit_line;	/* The command issued with all it's options in a
				 * string */
	uint32_t task_dist;	/* see enum task_dist_state in slurm.h */
	uint32_t time_limit;	/* maximum run time in minutes, default is
				 * partition limit */
	uint16_t threads_per_core; /* step requested threads-per-core */
	char *tres_bind;	/* Task to TRES binding directives */
	char *tres_freq;	/* TRES frequency directives */
	char *tres_per_step;	/* semicolon delimited list of TRES=# values */
	char *tres_per_node;	/* semicolon delimited list of TRES=# values */
	char *tres_per_socket;	/* semicolon delimited list of TRES=# values */
	char *tres_per_task;	/* semicolon delimited list of TRES=# values */
	uint32_t user_id;	/* user the job runs as */
} job_step_create_request_msg_t;

typedef struct job_step_create_response_msg {
	uint32_t def_cpu_bind_type;	/* Default CPU bind type */
	uint32_t job_id;		/* assigned job id */
	uint32_t job_step_id;		/* assigned job step id */
	char *resv_ports;		/* reserved ports */
	slurm_step_layout_t *step_layout; /* information about how the
                                           * step is laid out */
	char *stepmgr;
	slurm_cred_t *cred;    	  /* slurm job credential */
	dynamic_plugin_data_t *switch_step; /* switch opaque data type
					     * Remove 3 versions after 24.11 */
	uint16_t use_protocol_ver;   /* Lowest protocol version running on
				      * the slurmd's in this step.
				      */
} job_step_create_response_msg_t;

#define LAUNCH_PARALLEL_DEBUG	SLURM_BIT(0)
#define LAUNCH_MULTI_PROG	SLURM_BIT(1)
#define LAUNCH_PTY		SLURM_BIT(2)
#define LAUNCH_BUFFERED_IO	SLURM_BIT(3)
#define LAUNCH_LABEL_IO		SLURM_BIT(4)
#define LAUNCH_EXT_LAUNCHER	SLURM_BIT(5)
#define LAUNCH_NO_ALLOC 	SLURM_BIT(6)
#define LAUNCH_OVERCOMMIT 	SLURM_BIT(7)
#define LAUNCH_NO_SIG_FAIL 	SLURM_BIT(8)
#define LAUNCH_GRES_ALLOW_TASK_SHARING SLURM_BIT(9)
#define LAUNCH_WAIT_FOR_CHILDREN SLURM_BIT(10)
#define LAUNCH_KILL_ON_BAD_EXIT SLURM_BIT(11)

typedef struct launch_tasks_request_msg {
	uint32_t  het_job_node_offset;	/* Hetjob node offset or NO_VAL */
	uint32_t  het_job_id;		/* Hetjob ID or NO_VAL */
	uint32_t  het_job_nnodes;	/* total node count for entire hetjob */
	uint32_t  het_job_ntasks;	/* total task count for entire hetjob */
	uint16_t *het_job_task_cnts;	/* Tasks count on each node in hetjob */
	uint32_t *het_job_step_task_cnts; /* ntasks on each comp of hetjob */
	uint32_t **het_job_tids;	/* Task IDs on each node of hetjob */
	uint32_t *het_job_tid_offsets;	/* map of tasks (by id) to originating
					 * hetjob */
	uint32_t  het_job_offset;	/* Hetjob offset or NO_VAL */
	uint32_t  het_job_step_cnt;	/* number of steps for entire hetjob */
	uint32_t  het_job_task_offset;	/* Hetjob task ID offset or NO_VAL */
	char     *het_job_node_list;	/* Hetjob step node list */
	uint32_t mpi_plugin_id;		/* numeric version of mpi_plugin */
	uint32_t  nnodes;	/* number of nodes in this job step       */
	uint32_t  ntasks;	/* number of tasks in this job step   */
	uint16_t  ntasks_per_board;/* number of tasks to invoke on each board */
	uint16_t  ntasks_per_core; /* number of tasks to invoke on each core */
	uint16_t  ntasks_per_tres; /* number of tasks that can access each gpu */
	uint16_t  ntasks_per_socket;/* number of tasks to invoke on
				     * each socket */
	uint32_t  ngids;
	uint32_t *gids;
	uint64_t  job_mem_lim;	/* MB of memory reserved by job per node OR
				 * real memory per CPU | MEM_PER_CPU,
				 * default=0 (no limit) */
	slurm_step_id_t step_id;
	uint64_t  step_mem_lim;	/* MB of memory reserved by step */
	uint16_t  *tasks_to_launch;
	uint32_t  envc;
	uint32_t  argc;
	uint16_t  node_cpus;
	uint16_t  cpus_per_task;
	uint16_t *cpt_compact_array; /* Compressed per-node cpus_per_task.
				      * Index with slurm_get_rep_count_inx() */
	uint32_t cpt_compact_cnt; /* number of elements in cpt_compact arrays */
	uint32_t *cpt_compact_reps; /* number of consecutive nodes on which a
				     * value in cpt_compact_array is
				     * duplicated */
	uint16_t  threads_per_core;
	char *tres_per_task;	/* semicolon delimited list of TRES=# values */
	char    **env;
	char    **argv;
	char *container;	/* OCI Container Bundle Path */
	char     *cwd;
	uint16_t cpu_bind_type;	/* --cpu-bind=                    */
	char     *cpu_bind;	/* binding map for map/mask_cpu           */
	uint16_t mem_bind_type;	/* --mem-bind=                    */
	char     *mem_bind;	/* binding map for tasks to memory        */
	uint16_t accel_bind_type; /* --accel-bind= */
	char     *tres_bind;	/* task binding to TRES (e.g. GPUs) */
	char     *tres_freq;	/* frequency/power for TRES (e.g. GPUs) */
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
	char     *ofname; /* stdout filename pattern */
	char     *efname; /* stderr filename pattern */
	char     *ifname; /* stdin filename pattern */
	uint16_t  num_io_port;
	uint16_t  *io_port;  /* array of available client IO listen ports */
	/**********  END  "normal" IO only options **********/

	char *alloc_tls_cert; /* TLS certificate for step IO */
	uint32_t profile;
	char     *task_prolog;
	char     *task_epilog;

	uint16_t   slurmd_debug; /* remote slurmd debug level */

	uint16_t cred_version;	/* job credential protocol_version */
	slurm_cred_t *cred;	/* job credential            */
	dynamic_plugin_data_t *switch_step; /* switch credential for the job
					     * Remove 3 versions after 24.11 */
	list_t *options;  /* Arbitrary job options */
	char *complete_nodelist;
	char **spank_job_env;
	uint32_t spank_job_env_size;

	/* only filled out if step is SLURM_EXTERN_CONT */
	uint16_t x11;			/* X11 forwarding setup flags */
	char *x11_alloc_host;		/* host to proxy through */
	uint16_t x11_alloc_port;	/* port to proxy through */
	char *x11_magic_cookie;		/* X11 auth cookie to abuse */
	char *x11_target;		/* X11 target host, or unix socket */
	uint16_t x11_target_port;	/* X11 target port */

	/* To send to stepmgr */
	job_record_t *job_ptr;
	list_t *job_node_array;
	part_record_t *part_ptr;

	char *stepmgr; /* Hostname of stepmgr */
	bool oom_kill_step;
} launch_tasks_request_msg_t;

typedef struct partition_info partition_desc_msg_t;

typedef struct return_code_msg {
	uint32_t return_code;
} return_code_msg_t;
typedef struct return_code2_msg {
	uint32_t return_code;
	char *err_msg;
} return_code2_msg_t;

typedef struct {
	char *stepmgr;
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

typedef struct set_fs_dampening_factor_msg {
	uint16_t dampening_factor;
} set_fs_dampening_factor_msg_t;

typedef struct control_status_msg {
	uint16_t backup_inx;	/* Our BackupController# index,
				 * between 0 and (MAX_CONTROLLERS-1) */
	time_t control_time;	/* Time we became primary slurmctld (or 0) */
} control_status_msg_t;

#define SIG_OOM		253	/* Dummy signal value for out of memory
				 * (OOM) notification. Exit status reported as
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
	slurm_cred_t *cred;
	char *details;
	uint32_t derived_ec;
	uint32_t exit_code;
	uint32_t het_job_id;
	list_t *job_gres_prep;	/* Used to set Epilog environment variables */
	uint32_t job_state;
	uint32_t job_uid;
	uint32_t job_gid;
	char *nodes; /* Used for reliable cleanup on XCPU systems. */
	char **spank_job_env;
	uint32_t spank_job_env_size;
	time_t   start_time;	/* time of job start, track job requeue */
	slurm_step_id_t step_id;
	time_t   time;		/* slurmctld's time of request */
	char *work_dir;
} kill_job_msg_t;

typedef struct reattach_tasks_request_msg {
	char *io_key;
	uint16_t     num_resp_port;
	uint16_t    *resp_port; /* array of available response ports */
	uint16_t     num_io_port;
	uint16_t    *io_port;   /* array of available client IO ports */
	slurm_step_id_t step_id;
	char *tls_cert;
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
	char *alloc_tls_cert;		/* TLS certificate for client that is
					 * getting/has allocation
					 * (srun, salloc,etc.) */
	slurm_cred_t *cred;
	uint32_t gid;
	uint32_t het_job_id;		/* HetJob id or NO_VAL */
	list_t *job_gres_prep;		/* Used to set Prolog env vars */
	uint32_t job_id;		/* slurm job_id */
	uint64_t job_mem_limit;		/* job's memory limit, passed via cred */
	uint32_t nnodes;			/* count of nodes, passed via cred */
	char *nodes;			/* list of nodes allocated to job_step */
	char **spank_job_env;		/* SPANK job environment variables */
	uint32_t spank_job_env_size;	/* size of spank_job_env */
	uint32_t uid;
	char *work_dir;			/* full pathname of working directory */
	uint16_t x11;			/* X11 forwarding setup flags */
	char *x11_alloc_host;		/* srun/salloc host to setup proxy */
	uint16_t x11_alloc_port;	/* srun/salloc port to setup proxy */
	char *x11_magic_cookie;		/* X11 auth cookie to abuse */
	char *x11_target;		/* X11 target host, or unix socket */
	uint16_t x11_target_port;	/* X11 target port */

	/* To send to stepmgr */
	job_record_t *job_ptr;
	buf_t *job_ptr_buf;

	list_t *job_node_array; /* node_record_t array of size
				 * job_ptr->node_cnt for stepmgr. */
	buf_t *job_node_array_buf;

	part_record_t *part_ptr;
	buf_t *part_ptr_buf;
} prolog_launch_msg_t;

typedef struct batch_job_launch_msg {
	char *account;          /* account under which the job is running */
	char *acctg_freq;	/* accounting polling intervals	*/
	uint32_t array_job_id;	/* job array master job ID */
	uint32_t array_task_id;	/* job array ID or NO_VAL */
	char *container;	/* OCI Container Bundle path */
	uint32_t cpu_freq_min;  /* Minimum cpu frequency  */
	uint32_t cpu_freq_max;  /* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;  /* cpu frequency governor */
	uint32_t het_job_id;
	uint32_t job_id;
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
	char *nodes;		/* list of nodes allocated to job_step */
	uint32_t profile;       /* what to profile for the batch step */
	char *script;		/* the actual job script, default NONE */
	buf_t *script_buf;	/* the job script as a mmap buf */
	char *std_err;		/* pathname of stderr */
	char *std_in;		/* pathname of stdin */
	char *qos;              /* qos the job is running under */
	char *std_out;		/* pathname of stdout */
	char *work_dir;		/* full pathname of working directory */

	uint32_t argc;
	char **argv;
	uint32_t envc;		/* element count in environment */
	char **environment;	/* environment variables to set for job,
				 *   name=value pairs, one per line */
	uint16_t cred_version;	/* job credential protocol_version */
	slurm_cred_t *cred;
	uint8_t open_mode;	/* stdout/err append or truncate */
	uint8_t overcommit;	/* if resources being over subscribed */
	char    *partition;	/* partition used to run job */
	uint64_t pn_min_memory;  /* minimum real memory per node OR
				  * real memory per CPU | MEM_PER_CPU,
				  * default=0 (no limit) */
	uint64_t job_mem;	/* memory limit for job		*/
	uint16_t restart_cnt;	/* batch job restart count	*/
	char *resv_name;        /* job's reservation */
	char **spank_job_env;	/* SPANK job environment variables */
	uint32_t spank_job_env_size;	/* size of spank_job_env */
	char *tres_bind;	/* task binding to TRES (e.g. GPUs),
				 * included for possible future use */
	char *tres_freq;	/* frequency/power for TRES (e.g. GPUs) */
	char *tres_per_task;	/* semicolon delimited list of TRES=# values */
	bool oom_kill_step;
} batch_job_launch_msg_t;

typedef struct job_id_request_msg {
	uint32_t job_pid;	/* local process_id of a job */
} job_id_request_msg_t;

typedef struct job_id_response_msg {
	uint32_t job_id;	/* slurm job_id */
	uint32_t return_code;	/* slurm return code */
} job_id_response_msg_t;

typedef enum {
	CONFIG_REQUEST_SLURM_CONF = 0,
	CONFIG_REQUEST_SLURMD,
	CONFIG_REQUEST_SACKD,
} config_request_flags_t;

typedef struct {
	uint32_t flags;		/* see config_request_flags_t */
	uint16_t port; /* sackd port to push conf changes to */
} config_request_msg_t;

typedef struct {
	bool exists;
	bool execute;
	char *file_name;
	char *file_content;

	/* Do not pack - used for local caching for configless clients */
	int memfd_fd;
	char *memfd_path;
} config_file_t;

typedef struct {
	list_t *config_files;
	char *slurmd_spooldir;
} config_response_msg_t;

typedef struct kvs_get_msg {
	uint32_t task_id;	/* job step's task id */
	uint32_t size;		/* count of tasks in job */
	uint16_t port;		/* port to be sent the kvs data */
	char * hostname;	/* hostname to be sent the kvs data */
} kvs_get_msg_t;

enum compress_type {
	COMPRESS_OFF = 0,	/* no compression */
				/* = 1 was zlib */
	COMPRESS_LZ4 = 2,	/* lz4 compression */
};

typedef enum {
	FILE_BCAST_NONE = 0,		/* No flags set */
	FILE_BCAST_FORCE = 1 << 0,	/* replace existing file */
	FILE_BCAST_LAST_BLOCK = 1 << 1,	/* last file block */
	FILE_BCAST_SO = 1 << 2, 	/* shared object */
	FILE_BCAST_EXE = 1 << 3,	/* executable ahead of shared object */
} file_bcast_flags_t;

typedef struct file_bcast_msg {
	char *fname;		/* name of the destination file */
	char *exe_fname;	/* name of the executable file */
	uint32_t block_no;	/* block number of this data */
	uint16_t compress;	/* compress file if set, use compress_type */
	uint16_t flags;		/* flags from file_bcast_flags_t */
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

	uint16_t ntasks_per_board;  /* number of tasks to invoke on each board */
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
	uint32_t job_id;        /* slurm job_id */
	uint16_t op;            /* suspend operation, see enum suspend_opts */
} suspend_int_msg_t;

typedef struct ping_slurmd_resp_msg {
	uint32_t cpu_load;	/* CPU load * 100 */
	uint64_t free_mem;	/* Free memory in MiB */
} ping_slurmd_resp_msg_t;

typedef struct license_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
} license_info_request_msg_t;

typedef struct bb_status_req_msg {
	uint32_t argc;
	char **argv;
} bb_status_req_msg_t;

typedef struct bb_status_resp_msg {
	char *status_resp;
} bb_status_resp_msg_t;

typedef struct {
	uint32_t uid;
} crontab_request_msg_t;

typedef struct {
	char *crontab;
	char *disabled_lines;
} crontab_response_msg_t;

typedef struct {
	char *crontab;
	list_t *jobs;
	uint32_t uid;
	uint32_t gid;
} crontab_update_request_msg_t;

typedef struct {
	char *csr;
	char *node_name;
	char *token;
} tls_cert_request_msg_t;

typedef struct {
	char *signed_cert;
} tls_cert_response_msg_t;

typedef enum {
	DYN_NODE_NONE = 0,
	DYN_NODE_FUTURE,
	DYN_NODE_NORM,
} dynamic_node_type_t;

/*****************************************************************************\
 * Slurm API Message Types
\*****************************************************************************/
typedef struct slurm_node_registration_status_msg {
	char *arch;
	uint16_t cores;
	uint16_t cpus;
	uint32_t cpu_load;	/* CPU load * 100 */
	uint8_t dynamic_type;	/* dynamic registration type */
	char *dynamic_conf;	/* dynamic configuration */
	char *dynamic_feature;	/* dynamic registration feature */
	uint16_t flags;	        /* Flags from the slurmd SLURMD_REG_FLAG_* */
	uint64_t free_mem;	/* Free memory in MiB */
	char *cpu_spec_list;	/* list of specialized CPUs */
	acct_gather_energy_t *energy;
	char *extra;		/* arbitrary string */
	char *features_active;	/* Currently active node features */
	char *features_avail;	/* Available node features */
	buf_t *gres_info;	/* generic resource info */
	uint32_t hash_val;      /* hash value of slurm.conf and included files
				 * existing on node */
	char *hostname;         /* hostname of slurmd */
	char *instance_id;	/* cloud instance id */
	char *instance_type;	/* cloud instance type */
	uint32_t job_count;	/* number of associate job_id's */
	uint64_t mem_spec_limit; /* memspec detected by the node */
	char *node_name;
	uint16_t boards;
	char *os;
	uint64_t real_memory;
	time_t slurmd_start_time;
	uint32_t status;	/* node status code, same as return codes */
	slurm_step_id_t *step_id;	/* IDs of running job steps (if any) */
	uint16_t sockets;
	uint16_t threads;
	time_t timestamp;
	uint32_t tmp_disk;
	uint32_t up_time;	/* seconds since reboot */
	char *version;
} slurm_node_registration_status_msg_t;

typedef struct slurm_node_reg_resp_msg {
	char *node_name;
	list_t *tres_list;
} slurm_node_reg_resp_msg_t;

typedef struct requeue_msg {
	uint32_t job_id;	/* slurm job ID (number) */
	char *   job_id_str;	/* slurm job ID (string) */
	uint32_t flags;         /* JobExitRequeue | Hold | JobFailed | etc. */
} requeue_msg_t;

typedef struct {
	uint32_t cluster_id;	/* cluster id of cluster making request */
	void    *data;		/* Unpacked buffer
				 * Only populated on the receiving side. */
	buf_t *data_buffer;	/* Buffer that holds an unpacked data type.
				 * Only populated on the sending side. */
	uint32_t data_offset;	/* DON'T PACK: offset where body part of buffer
				 * starts -- the part that gets sent. */
	uint16_t data_type;	/* date type to unpack */
	uint16_t data_version;	/* Version that data is packed with */
	uint64_t fed_siblings;	/* sibling bitmap of job */
	uint32_t group_id;      /* gid of submitted job */
	uint32_t job_id;	/* job_id of job - set in job_desc on receiving
				 * side */
	uint32_t job_state;     /* state of job */
	uint32_t return_code;   /* return code of job */
	time_t   start_time;    /* time sibling job started */
	char    *resp_host;     /* response host for interactive allocations */
	uint32_t req_uid;       /* uid of user making the request. e.g if a
				   cancel is happening from a user and being
				   passed to a remote then the uid will be the
				   user and not the SlurmUser. */
	uint16_t sib_msg_type; /* fed_job_update_type */
	char    *submit_host;   /* node job was submitted from */
	uint16_t submit_proto_ver; /* protocol version of submission client */
	uint32_t user_id;       /* uid of submitted job */
} sib_msg_t;

typedef struct {
	uint32_t array_job_id;
	uint32_t array_task_id;
	char *dependency;
	bool is_array;
	uint32_t job_id;
	char *job_name;
	uint32_t user_id;
} dep_msg_t;

typedef struct {
	list_t *depend_list;
	uint32_t job_id;
} dep_update_origin_msg_t;

typedef struct {
	list_t *my_list;	/* this list could be of any type as long as it
				 * is handled correctly on both ends */
} ctld_list_msg_t;

/*****************************************************************************\
 *      ACCOUNTING PUSHS
\*****************************************************************************/

typedef struct {
	list_t *update_list; /* of type slurmdb_update_object_t *'s */
	uint16_t rpc_version;
} accounting_update_msg_t;

typedef slurm_conf_t slurm_ctl_conf_info_msg_t;

/*****************************************************************************\
 *      Container RPCs
\*****************************************************************************/

typedef enum {
	CONTAINER_ST_INVALID = 0,
	/*
	 * UNKNOWN: initial state
	 */
	CONTAINER_ST_UNKNOWN = 0xae00,
	/*
	 * CREATING:
	 * Official OCI state.
	 *
	 * waiting for pty allocation
	 * waiting for srun exec process to be ready
	 * waiting for job allocation
	 * waiting for staging plugin push to complete
	 */
	CONTAINER_ST_CREATING,
	/*
	 * CREATED:
	 * Official OCI state.
	 *
	 * job allocated (no steps yet)
	 * staging plugin push done
	 */
	CONTAINER_ST_CREATED,
	/*
	 * STARTING:
	 * waiting for srun to start step
	 */
	CONTAINER_ST_STARTING,
	/*
	 * RUNNING:
	 * Official OCI state.
	 *
	 * job allocated and step started
	 */
	CONTAINER_ST_RUNNING,
	/*
	 * STOPPING:
	 * waiting for step to end
	 * waiting for staging plugin pull to complete
	 * waiting for job to end
	 */
	CONTAINER_ST_STOPPING,
	/*
	 * STOPPED:
	 * Official OCI state
	 *
	 * job and step complete
	 * staging plugin pull complete
	 * anchor exits here
	 */
	CONTAINER_ST_STOPPED,
	CONTAINER_ST_MAX /* place holder */
} container_state_msg_status_t;

extern const char *slurm_container_status_to_str(
	container_state_msg_status_t status);

typedef struct {
	/* every field required by OCI runtime-spec v1.0.2 state query */
	char *oci_version;
	char *id; /* container-id */
	container_state_msg_status_t status; /* current status */
	uint32_t pid; /* pid of anchor process */
	char *bundle; /* path to OCI container bundle */
	list_t *annotations; /* List of config_key_pair_t */
} container_state_msg_t;

/*
 * Create and init new container state message
 * RET ptr to message (must free with slurm_destroy_container_state_msg())
 */
extern container_state_msg_t *slurm_create_container_state_msg(void);
extern void slurm_destroy_container_state_msg(container_state_msg_t *msg);

typedef struct {
	uint32_t signal;
} container_signal_msg_t;

typedef struct {
	bool force;
} container_delete_msg_t;

typedef struct {
	uint32_t rc;
	slurm_step_id_t step;
} container_started_msg_t;

typedef struct {
	char *args;
	char *env;
} container_exec_msg_t;

extern void slurm_destroy_container_exec_msg(container_exec_msg_t *msg);

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

/* here to add \\ to all \" in a string this needs to be xfreed later */
extern char *slurm_add_slash_to_quotes(char *str);
extern list_t *slurm_copy_char_list(list_t *char_list);
extern int slurm_parse_char_list(
	list_t *char_list, char *names, void *args,
	int (*func_ptr)(list_t *char_list, char *name, void *args));
extern int slurm_addto_char_list(list_t *char_list, char *names);
extern int slurm_addto_char_list_with_case(list_t *char_list, char *names,
					   bool lower_case_normalization);
extern int slurm_addto_id_char_list(list_t *char_list, char *names, bool gid);
extern int slurm_addto_mode_char_list(list_t *char_list, char *names, int mode);
extern int slurm_addto_step_list(list_t *step_list, char *names);
extern int slurm_char_list_copy(list_t *dst, list_t *src);
extern char *slurm_char_list_to_xstr(list_t *char_list);
extern void slurm_copy_node_alias_addrs_members(slurm_node_alias_addrs_t *dest,
						slurm_node_alias_addrs_t *src);
extern int slurm_find_char_exact_in_list(void *x, void *key);
extern int slurm_find_char_in_list(void *x, void *key);
extern int slurm_find_ptr_in_list(void *x, void *key);
extern int slurm_find_uint16_in_list(void *x, void *key);
extern int slurm_find_uint32_in_list(void *x, void *key);
extern int slurm_find_uint64_in_list(void *x, void *key);
extern int slurm_find_uint_in_list(void *x, void *key);
extern int slurm_find_int_in_list(void *x, void *key);
extern int slurm_find_int64_in_list(void *x, void *key);
extern void slurm_remove_char_list_from_char_list(list_t *haystack,
						  list_t *needles);

extern int slurm_sort_time_list_asc(const void *, const void *);
extern int slurm_sort_time_list_desc(const void *, const void *);
extern int slurm_sort_uint16_list_asc(const void *, const void *);
extern int slurm_sort_uint16_list_desc(const void *, const void *);
extern int slurm_sort_uint32_list_asc(const void *, const void *);
extern int slurm_sort_uint32_list_desc(const void *, const void *);
extern int slurm_sort_uint64_list_asc(const void *, const void *);
extern int slurm_sort_uint64_list_desc(const void *, const void *);
extern int slurm_sort_int_list_asc(const void *, const void *);
extern int slurm_sort_int_list_desc(const void *, const void *);
extern int slurm_sort_int64_list_asc(const void *, const void *);
extern int slurm_sort_int64_list_desc(const void *, const void *);

extern int slurm_sort_char_list_asc(void *, void *);
extern int slurm_sort_char_list_desc(void *, void *);

extern char **slurm_char_array_copy(int n, char **src);

/*
 * Sort an unordered node_list string and remove duplicate node names.
 *
 * Returns an xmalloc'd node_list that is sorted.
 * Caller must xfree() return value.
 */
extern char *slurm_sort_node_list_str(char *node_list);

/*
 * For each token in a comma delimited job array expression set the matching
 * bitmap entry.
 *
 * IN tok - An array expression. For example: "[1,3-5,8]"
 * IN array_bitmap - Matching entries in tok are set in this bitmap.
 * IN max - maximum size of the job array.
 * RET true if tok is a valid array expression, v
 */
extern bool slurm_parse_array_tok(char *tok, bitstr_t *array_bitmap,
				  uint32_t max);
/*
 * Take a string representation of an array range or comma separated values
 * and translate that into a bitmap.
 *
 * IN str - An array expression. For example: "[1,3-5,8]"
 * IN max_array_size - maximum size of the job array.
 * OUT i_last_p - (optional) if not NULL, set the value pointed to by i_last_p
 *                to the last bit set in the array bitmap. This is only changed
 *                if the string was successfully parsed.
 * RET array_bitmap if successful, NULL otherwise.
 */
extern bitstr_t *slurm_array_str2bitmap(char *str, uint32_t max_array_size,
					int32_t *i_last_p);

/*
 * Take a string identifying any part of a job and parses it into an id
 *
 * Formats parsed:
 *      0000 - JobId
 *      0000+0000 - HetJob
 *      0000+0000.0000 - HetJob Step
 *      0000_0000 - Array Job
 *      0000_0000.0000 - Array Job Step
 *      0000.0000 - Job Step
 *      0000.0000+0000 - Job HetStep
 *
 * Rejected formats:
 *      0000_0000+0000 Array HetJob (not permitted)
 *      0000+0000.0000+0000 HetJob with HetStep (not permitted)
 *
 * IN src - identifier string
 * IN/OUT id - ptr to id to be populated.
 * 	All values are always set to NO_VAL and then populated as parsed.
 *      (Errors during parsing may result in partially populated ID.)
 * IN max_array_size - Maximum size of a job array. May be 0 or NO_VAL if
 *                     job array expressions are not expected.
 * RET SLURM_SUCCESS or error
 */
extern int unfmt_job_id_string(const char *src, slurm_selected_step_t *id,
			       uint32_t max_array_size);
/*
 * Dump id into string identifying a part of a job.
 * Dumps same formats as unfmt_job_id_string() parsed.
 * IN id - job identifier to dump
 * IN/OUT dst - ptr to string to populate.
 * 	*dst must always be NULL when called.
 * 	Caller must xfree(*dst).
 * RET SLURM_SUCCESS or error
 */
extern int fmt_job_id_string(slurm_selected_step_t *id, char **dst);

extern slurm_selected_step_t *slurm_parse_step_str(char *name);

extern resource_allocation_response_msg_t *
slurm_copy_resource_allocation_response_msg(
	resource_allocation_response_msg_t *msg);

/* free message functions */
extern void slurm_free_dep_msg(dep_msg_t *msg);
extern void slurm_free_dep_update_origin_msg(dep_update_origin_msg_t *msg);
extern void slurm_free_last_update_msg(last_update_msg_t * msg);
extern void slurm_free_return_code_msg(return_code_msg_t * msg);
extern void slurm_free_return_code2_msg(return_code2_msg_t *msg);
extern void slurm_free_reroute_msg(reroute_msg_t *msg);
extern void slurm_free_job_alloc_info_msg(job_alloc_info_msg_t * msg);
extern void slurm_free_container_id_request_msg(
	container_id_request_msg_t *msg);
extern void slurm_free_container_id_response_msg(
	container_id_response_msg_t *msg);
extern void slurm_free_job_info_request_msg(job_info_request_msg_t *msg);
extern void slurm_free_job_state_request_msg(job_state_request_msg_t *msg);
extern void slurm_free_job_step_info_request_msg(
		job_step_info_request_msg_t *msg);
extern void slurm_free_node_info_request_msg(node_info_request_msg_t *msg);
extern void slurm_free_node_info_single_msg(node_info_single_msg_t *msg);
extern void slurm_free_part_info_request_msg(part_info_request_msg_t *msg);
extern void slurm_free_sib_msg(sib_msg_t *msg);
extern void slurm_free_stats_info_request_msg(stats_info_request_msg_t *msg);
extern void slurm_free_stats_response_msg(stats_info_response_msg_t *msg);
extern void slurm_free_resv_info_request_msg(resv_info_request_msg_t *msg);
extern void slurm_free_set_debug_flags_msg(set_debug_flags_msg_t *msg);
extern void slurm_free_set_debug_level_msg(set_debug_level_msg_t *msg);
extern void slurm_destroy_assoc_shares_object(void *object);
extern void slurm_free_shares_request_msg(shares_request_msg_t *msg);
extern void slurm_free_shares_response_msg(shares_response_msg_t *msg);
extern void slurm_destroy_priority_factors(void *object);
extern void slurm_destroy_priority_factors_object(void *object);
extern void slurm_copy_priority_factors(priority_factors_t *dest,
					priority_factors_t *src);
extern void slurm_free_forward_data_msg(forward_data_msg_t *msg);
extern void slurm_free_ping_slurmd_resp(ping_slurmd_resp_msg_t *msg);

#define	slurm_free_timelimit_msg(msg) \
	slurm_free_kill_job_msg(msg)

extern void slurm_init_reboot_msg(reboot_msg_t * msg, bool clear);
extern void slurm_free_reboot_msg(reboot_msg_t * msg);

extern void slurm_free_shutdown_msg(shutdown_msg_t * msg);

extern void slurm_free_job_desc_msg(job_desc_msg_t * msg);

extern void
slurm_free_node_registration_status_msg(slurm_node_registration_status_msg_t *
					msg);
extern void slurm_free_node_reg_resp_msg(
	slurm_node_reg_resp_msg_t *msg);

extern void slurm_free_job_info(job_info_t * job);
extern void slurm_free_job_info_members(job_info_t * job);

extern void slurm_free_batch_script_msg(char *msg);
extern void slurm_free_job_id_msg(job_id_msg_t * msg);
extern void slurm_free_job_user_id_msg(job_user_id_msg_t * msg);
extern void slurm_free_job_id_request_msg(job_id_request_msg_t * msg);
extern void slurm_free_job_id_response_msg(job_id_response_msg_t * msg);
extern void slurm_free_config_request_msg(config_request_msg_t *msg);
extern void slurm_free_config_response_msg(config_response_msg_t *msg);

extern void slurm_free_step_id(slurm_step_id_t *msg);

extern void slurm_free_job_launch_msg(batch_job_launch_msg_t * msg);

extern void slurm_free_update_node_msg(update_node_msg_t * msg);
extern void slurm_free_update_part_msg(update_part_msg_t * msg);
extern void slurm_free_delete_part_msg(delete_part_msg_t * msg);
extern void slurm_free_resv_desc_msg_part(resv_desc_msg_t *msg,
					  uint32_t res_free_flags);
extern void slurm_free_resv_desc_members(resv_desc_msg_t *msg);
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
extern void slurm_free_task_exit_msg(task_exit_msg_t * msg);
extern void slurm_free_signal_tasks_msg(signal_tasks_msg_t * msg);
extern void slurm_free_reattach_tasks_request_msg(
		reattach_tasks_request_msg_t * msg);
extern void slurm_free_reattach_tasks_response_msg(
		reattach_tasks_response_msg_t * msg);
extern void slurm_free_kill_job_msg(kill_job_msg_t * msg);
extern void slurm_free_job_step_kill_msg(job_step_kill_msg_t * msg);
extern void slurm_free_kill_jobs_msg(kill_jobs_msg_t *msg);
extern void slurm_free_kill_jobs_resp_job_t(kill_jobs_resp_job_t *job_resp);
extern void slurm_free_kill_jobs_response_msg(kill_jobs_resp_msg_t *msg);
extern void slurm_free_epilog_complete_msg(epilog_complete_msg_t * msg);
extern void slurm_free_srun_job_complete_msg(srun_job_complete_msg_t * msg);
extern void slurm_free_srun_ping_msg(srun_ping_msg_t * msg);
extern void slurm_free_net_forward_msg(net_forward_msg_t *msg);
extern void slurm_free_srun_node_fail_msg(srun_node_fail_msg_t * msg);
extern void slurm_free_srun_step_missing_msg(srun_step_missing_msg_t * msg);
extern void slurm_free_srun_timeout_msg(srun_timeout_msg_t * msg);
extern void slurm_free_srun_user_msg(srun_user_msg_t * msg);
extern void slurm_free_suspend_msg(suspend_msg_t *msg);
extern void slurm_free_suspend_int_msg(suspend_int_msg_t *msg);
extern void slurm_free_top_job_msg(top_job_msg_t *msg);
extern void slurm_free_token_request_msg(token_request_msg_t *msg);
extern void slurm_free_token_response_msg(token_response_msg_t *msg);
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
extern void slurm_free_node_info_msg(node_info_msg_t * msg);
extern void slurm_init_node_info_t(node_info_t * msg, bool clear);
extern void slurm_free_node_info_members(node_info_t * node);
extern void slurm_free_partition_info_msg(partition_info_msg_t * msg);
extern void slurm_free_partition_info_members(partition_info_t * part);
extern void slurm_free_reservation_info_msg(reserve_info_msg_t * msg);
extern void slurm_free_get_kvs_msg(kvs_get_msg_t *msg);
extern void slurm_free_kvs_comm_set(kvs_comm_set_t *msg);
extern void slurm_free_will_run_response_msg(void *data);
extern void slurm_free_reserve_info_members(reserve_info_t * resv);
extern void slurm_free_topo_info_msg(topo_info_response_msg_t *msg);
extern void slurm_free_topo_config_msg(topo_config_response_msg_t *msg);
extern void slurm_free_topo_request_msg(topo_info_request_msg_t *msg);
extern void slurm_free_file_bcast_msg(file_bcast_msg_t *msg);
extern void slurm_free_step_complete_msg(step_complete_msg_t *msg);
extern void slurm_free_job_step_stat(void *object);
extern void slurm_free_job_step_pids(void *object);
extern void slurm_free_acct_gather_node_resp_msg(
	acct_gather_node_resp_msg_t *msg);
extern void slurm_free_acct_gather_energy_req_msg(
	acct_gather_energy_req_msg_t *msg);
extern void slurm_free_job_notify_msg(job_notify_msg_t * msg);
extern void slurm_free_ctld_multi_msg(ctld_list_msg_t *msg);

extern void slurm_free_accounting_update_msg(accounting_update_msg_t *msg);
extern void slurm_free_requeue_msg(requeue_msg_t *);
extern int slurm_free_msg_data(slurm_msg_type_t type, void *data);
extern void slurm_free_license_info_request_msg(license_info_request_msg_t *msg);
extern uint32_t slurm_get_return_code(slurm_msg_type_t type, void *data);
extern void slurm_free_network_callerid_msg(network_callerid_msg_t *mesg);
extern void slurm_free_network_callerid_resp(network_callerid_resp_t *resp);
extern void slurm_free_node_alias_addrs_members(slurm_node_alias_addrs_t *msg);
extern void slurm_free_node_alias_addrs(slurm_node_alias_addrs_t *msg);
extern void slurm_free_set_fs_dampening_factor_msg(
	set_fs_dampening_factor_msg_t *msg);
extern void slurm_free_control_status_msg(control_status_msg_t *msg);

extern void slurm_free_bb_status_req_msg(bb_status_req_msg_t *msg);
extern void slurm_free_bb_status_resp_msg(bb_status_resp_msg_t *msg);

extern void slurm_free_crontab_request_msg(crontab_request_msg_t *msg);
extern void slurm_free_crontab_response_msg(crontab_response_msg_t *msg);
extern void slurm_free_crontab_update_request_msg(
	crontab_update_request_msg_t *msg);
extern void slurm_free_crontab_update_response_msg(
	crontab_update_response_msg_t *msg);
extern void slurm_free_tls_cert_request_msg(tls_cert_request_msg_t *msg);
extern void slurm_free_tls_cert_response_msg(tls_cert_response_msg_t *msg);
extern void slurm_free_suspend_exc_update_msg(suspend_exc_update_msg_t *msg);
extern void slurm_free_sbcast_cred_req_msg(sbcast_cred_req_msg_t *msg);

extern const char *preempt_mode_string(uint16_t preempt_mode);
extern uint16_t preempt_mode_num(const char *preempt_mode);

extern char *log_num2string(uint16_t inx);
extern uint16_t log_string2num(const char *name);

/* Translate a burst buffer numeric value to its equivalent state string */
extern char *bb_state_string(uint16_t state);
/* Translate a burst buffer state string to its equivalent numeric value */
extern uint16_t bb_state_num(char *tok);

/* Convert HealthCheckNodeState numeric value to a string.
 * Caller must xfree() the return value */
extern char *health_check_node_state_str(uint32_t node_state);

extern char *job_share_string(uint16_t shared);
extern char *job_state_string(uint32_t inx);
extern char *job_state_string_compact(uint32_t inx);
/* Caller must xfree() the return value */
extern char *job_state_string_complete(uint32_t state);
extern uint32_t job_state_num(const char *state_name);
/*
 * Returns true is the node's base state is a known base state.
 */
extern bool valid_base_state(uint32_t state);
/*
 * Return the string representing a given node base state.
 */
extern const char *node_state_base_string(uint32_t state);
/*
 * Return the string representing a single node state flag.
 *
 * Clears the flag bit in the passed state variable.
 */
extern const char *node_state_flag_string_single(uint32_t *state);
/*
 * Return + separated string of node state flags.
 *
 * Caller must xfree() the return value.
 */
extern char *node_state_flag_string(uint32_t state);
extern char *node_state_string(uint32_t inx);
extern char *node_state_string_compact(uint32_t inx);
/*
 * Return node base state + flags strings.
 *
 * Caller must xfree() the return value.
 */
extern char *node_state_string_complete(uint32_t inx);

/*
 * Return first node state flag that matches the string
 */
extern uint32_t parse_node_state_flag(char *flag_str);

extern void  private_data_string(uint16_t private_data, char *str, int str_len);
extern void  accounting_enforce_string(uint16_t enforce,
				       char *str, int str_len);

/* Validate SPANK specified job environment does not contain any invalid
 * names. Log failures using info() */
extern bool valid_spank_job_env(char **spank_job_env,
			        uint32_t spank_job_env_size, uid_t uid);

extern char *trigger_res_type(uint16_t res_type);
extern char *trigger_type(uint32_t trig_type);

/* user needs to xfree return value */
extern char *priority_flags_string(uint16_t priority_flags);

/* user needs to xfree return value */
extern char *reservation_flags_string(reserve_info_t * resv_ptr);

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
extern void print_multi_line_string(char *user_msg, int inx,
				    log_level_t loglevel);

/*
 * Given a numeric suffix, return the equivalent multiplier for the numeric
 * portion. For example: "k" returns 1024, "KB" returns 1000, etc.
 * The return value for an invalid suffix is NO_VAL64.
 */
extern uint64_t suffix_mult(char *suffix);
/*
 * See if the step_id 'key' coming in matches enough of the step_id 'object'
 */
extern bool verify_step_id(slurm_step_id_t *object, slurm_step_id_t *key);

/* OUT: job_id_str - filled in with the id of the job/array
 * RET: job_id_str */
extern char *slurm_get_selected_step_id(
	char *job_id_str, int len,
	slurm_selected_step_t *selected_step);

/*
 * Translate bitmap representation of array from hex to decimal format,
 * replacing array_task_str and store the bitmap in array_bitmap.
 *
 * IN/OUT array_task_str - job's array task string
 * IN array_max_tasks - job's array_max_tasks
 * OUT array_bitmap - job's array_bitmap
 */
extern void xlate_array_task_str(char **array_task_str,
				 uint32_t array_max_tasks,
				 bitstr_t **array_bitmap);

/*
 * slurm_array<size>_to_value_reps - Compress array into an array that
 *				     represents the number of repeated values
 *				     compressed.
 *
 * IN array - Array of values.
 * IN array_cnt - Count of elements in 'array'.
 * OUT values - Array of values compressed.
 * OUT values_reps - How many each corresponding element in 'values' there are.
 * OUT values_cnt - Count of elements in 'values' and 'values_reps'.
 */
extern void slurm_array64_to_value_reps(uint64_t *array, uint32_t array_cnt,
					uint64_t **values,
					uint32_t **values_reps,
					uint32_t *values_cnt);
extern void slurm_array16_to_value_reps(uint16_t *array, uint32_t array_cnt,
					uint16_t **values,
					uint32_t **values_reps,
					uint32_t *values_cnt);

/*
 * slurm_get_rep_count_inx - given a compressed array of counts and the actual
 *                           index you are looking for if the array was
 *                           uncompressed return the matching index for the
 *                           compressed array.
 * IN rep_count - compressed array
 * IN rep_count_size - size of compressed_array
 * IN inx - uncompressed index
 *
 * RET - compressed index or -1 on failure.
 */
extern int slurm_get_rep_count_inx(
	uint32_t *rep_count, uint32_t rep_count_size, int inx);

/*
 * slurm_format_tres_string - given a TRES type and a tres-per-* string,
 *			      will modify the string from the original
 *			      colon-separated tres request format to the new
 *			      '/' separating the TRES type from the tres
 *			      request. This will work even if the request name
 *			      or grestype with the TRES type.
 * IN s - tres-per-* tres request string. This will be modified to fit the new
 *	  format.
 * IN tres_type - the type of tres to replace in the tres request string
 *
 * If *s is non-NULL, it must be an xmalloc'd string.
 *
 * Input:
 * license:testing_license:3
 * Output:
 * license/testing_license:3
 *
 * This function will not modify correctly formatted tres request strings
 * Input:
 * license/testing_license:3
 * Output:
 * license/testing_license:3
 *
 * Input:
 * gres:gres1:type1:3,gres:gres2:type2:6
 * Output:
 * gres:gres1/type1:3,gres/gres2:type2:6
 *
 * Input
 * gres:gres1_gres:type1_gres:3,gres:gres2_gres:type2_gres:6
 * Output:
 * gres/gres1_gres:type1_gres:3,gres/gres2_gres:type2_gres:6
 */
extern void slurm_format_tres_string(char **s, char *tres_type);

/*
 * Reentrant TRES specification parse logic
 * tres_type IN/OUT - type of tres we are looking for, If *tres_type is NULL we
 *                    will fill it in
 * in_val IN - initial input string
 * name OUT -  must be xfreed by caller
 * type OUT -  must be xfreed by caller
 * cnt OUT - count of values
 * save_ptr IN/OUT - NULL on initial call, otherwise value from previous call
 * RET rc - error code
 */
extern int slurm_get_next_tres(
	char **tres_type, char *in_val, char **name_ptr, char **type_ptr,
	uint64_t *cnt, char **save_ptr);

/*
 * Return a sub-string from full_tres_str for a specific TRES.
 * full_tres_str IN - complete list of TRES.
 * tres_type IN - type of tres we are looking for (NULL) for all
 * num_tasks IN - number of tasks to multiply tres by (tres-per-task)
 * include_tres_type IN - include the tres_type in the sub-string
 * include_type IN - if a TRES has a name and type (GRES) include it in the
 *                   sub-string.
 * RET char * of tres_type we are looking for (xfreed by caller).
 */
extern char *slurm_get_tres_sub_string(
	char *full_tres_str, char *tres_type, uint32_t num_tasks,
	bool include_tres_type, bool include_type);

extern char *schedule_exit2string(uint16_t opcode);

extern char *bf_exit2string(uint16_t opcode);

/*
 * Parse reservation request option Watts
 * IN watts_str - value to parse
 * IN/OUT resv_msg_ptr - msg where resv_watts member is modified
 * OUT err_msg - set to an explanation of failure, if any. Don't set if NULL
 */
extern uint32_t slurm_watts_str_to_int(char *watts_str, char **err_msg);

typedef struct {
	uint32_t	node_count;	/* number of nodes to communicate
					 * with */
	uint16_t	retry;		/* if set, keep trying */
	uid_t r_uid;			/* receiver UID */
	bool r_uid_set;			/* true if receiver UID set */
	slurm_addr_t    *addr;          /* if set will send to this
					   addr not hostlist */
	hostlist_t *hostlist;		/* hostlist containing the
					 * nodes we are sending to */
	uint16_t        protocol_version; /* protocol version to use */
	slurm_msg_type_t msg_type;	/* RPC to be issued */
	void		*msg_args;	/* RPC data to be transmitted */
	uint16_t msg_flags;		/* Flags to be added to msg */
	char *tls_cert;
} agent_arg_t;

/* Set r_uid of agent_arg */
extern void set_agent_arg_r_uid(agent_arg_t *agent_arg_ptr, uid_t r_uid);
extern void purge_agent_args(agent_arg_t *agent_arg_ptr);

/*
 * validate_slurm_user - validate that the uid is authorized to see
 *      privileged data (either user root or SlurmUser)
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_slurm_user(uid_t uid);

/*
 * validate_slurmd_user - validate that the uid is authorized to see
 *      privileged data (either user root or SlurmUser)
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_slurmd_user(uid_t uid);

/*
 * Return the job's sharing value from job or partition value.
 */
extern uint16_t get_job_share_value(job_record_t *job_ptr);

/*
 * Free stepmgr_job_info_t
 */
extern void slurm_free_stepmgr_job_info(stepmgr_job_info_t *object);

/* Resv creation msg client validation. On error err_msg is set */
extern int validate_resv_create_desc(resv_desc_msg_t *resv_msg, char **err_msg,
				     uint32_t *res_free_flags);

/*
 * Get configured DefCpuPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t slurm_get_def_cpu_per_gpu(list_t *job_defaults_list);

/*
 * Get configured DefMemPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t slurm_get_def_mem_per_gpu(list_t *job_defaults_list);

#define safe_read(fd, buf, size) do {					\
		size_t remaining = size;				\
		char *ptr = (char *) buf;				\
		int rc;							\
		while (remaining > 0) {					\
			rc = read(fd, ptr, remaining);			\
			if ((rc == 0) && (remaining == size)) {		\
				debug("%s:%d: %s: safe_read EOF",	\
				      __FILE__, __LINE__, __func__); \
				errno = EIO;				\
				goto rwfail;				\
			} else if (rc == 0) {				\
				debug("%s:%d: %s: safe_read (%zu of %d) EOF", \
				      __FILE__, __LINE__, __func__, \
				      remaining, (int)size);		\
				errno = EIO;				\
				goto rwfail;				\
			} else if (rc < 0) {				\
				if ((errno == EAGAIN) ||		\
				    (errno == EINTR) ||			\
				    (errno == EWOULDBLOCK))		\
					continue;			\
				debug("%s:%d: %s: safe_read (%zu of %d) failed: %m", \
				      __FILE__, __LINE__, __func__, \
				      remaining, (int)size);		\
				goto rwfail;				\
			} else {					\
				ptr += rc;				\
				remaining -= rc;			\
				if (remaining > 0)			\
					debug3("%s:%d: %s: safe_read (%zu of %d) partial read", \
					       __FILE__, __LINE__, __func__, \
					       remaining, (int)size);	\
			}						\
		}							\
	} while (0)

#define safe_write(fd, buf, size) do {					\
		size_t remaining = size;				\
		char *ptr = (char *) buf;				\
		int rc;							\
		while(remaining > 0) {					\
			rc = write(fd, ptr, remaining);			\
 			if (rc < 0) {					\
				if ((errno == EAGAIN) || (errno == EINTR))\
					continue;			\
				debug("%s:%d: %s: safe_write (%zu of %d) failed: %m", \
				      __FILE__, __LINE__, __func__, \
				      remaining, (int)size);		\
				goto rwfail;				\
			} else {					\
				ptr += rc;				\
				remaining -= rc;			\
				if (remaining > 0)			\
					debug3("%s:%d: %s: safe_write (%zu of %d) partial write", \
					       __FILE__, __LINE__, __func__, \
					       remaining, (int)size);	\
			}						\
		}							\
	} while (0)

#endif
