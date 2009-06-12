/****************************************************************************\
 *  slurm_protocol_defs.h - definitions used for RPCs
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#if HAVE_CONFIG_H 
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <slurm/slurm.h>
#include <sys/wait.h>

#include "src/common/bitstring.h"
#include "src/common/job_options.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_step_layout.h"
#include "src/common/switch.h"
#include "src/common/xassert.h"
//#include "src/common/slurm_jobacct_common.h"

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
#define IS_NODE_DRAIN(_X)		\
	(_X->node_state & NODE_STATE_DRAIN)
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

/* used to define flags of the launch_tasks_request_msg_t and
 * spawn task_request_msg_t task_flags
 */
enum task_flag_vals {
	TASK_PARALLEL_DEBUG = 0x1,
	TASK_UNUSED1 = 0x2,
	TASK_UNUSED2 = 0x4
};

enum suspend_opts {
	SUSPEND_JOB,		/* Suspend a job now */
	RESUME_JOB		/* Resume a job now */
};

/* SLURM Message types */
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
	REQUEST_SET_DEBUG_LEVEL,
	REQUEST_HEALTH_CHECK,
	REQUEST_TAKEOVER,
	
	REQUEST_BUILD_INFO = 2001,
	RESPONSE_BUILD_INFO,
	REQUEST_JOB_INFO,
	RESPONSE_JOB_INFO,
	REQUEST_JOB_STEP_INFO,
	RESPONSE_JOB_STEP_INFO,
	REQUEST_NODE_INFO,
	RESPONSE_NODE_INFO,
	REQUEST_PARTITION_INFO,
	RESPONSE_PARTITION_INFO,
	REQUEST_ACCTING_INFO,
	RESPONSE_ACCOUNTING_INFO,
	REQUEST_JOB_ID,
	RESPONSE_JOB_ID,
	REQUEST_NODE_SELECT_INFO,
	RESPONSE_NODE_SELECT_INFO,
	REQUEST_TRIGGER_SET,
	REQUEST_TRIGGER_GET,
	REQUEST_TRIGGER_CLEAR,
	RESPONSE_TRIGGER_GET,
	REQUEST_JOB_INFO_SINGLE,
	REQUEST_SHARE_INFO,
	RESPONSE_SHARE_INFO,
	REQUEST_RESERVATION_INFO,
	RESPONSE_RESERVATION_INFO,
	REQUEST_PRIORITY_FACTORS,
	RESPONSE_PRIORITY_FACTORS,
	REQUEST_TOPO_INFO,
	RESPONSE_TOPO_INFO,

	REQUEST_UPDATE_JOB = 3001,
	REQUEST_UPDATE_NODE,
	REQUEST_CREATE_PARTITION,
	REQUEST_DELETE_PARTITION,
	REQUEST_UPDATE_PARTITION,
	REQUEST_CREATE_RESERVATION,
	RESPONSE_CREATE_RESERVATION,
	REQUEST_DELETE_RESERVATION,
	REQUEST_UPDATE_RESERVATION,

	REQUEST_RESOURCE_ALLOCATION = 4001,
	RESPONSE_RESOURCE_ALLOCATION,
	REQUEST_SUBMIT_BATCH_JOB,
	RESPONSE_SUBMIT_BATCH_JOB,
	REQUEST_BATCH_JOB_LAUNCH,
	REQUEST_CANCEL_JOB,
	RESPONSE_CANCEL_JOB,
	REQUEST_JOB_RESOURCE,
	RESPONSE_JOB_RESOURCE,
	REQUEST_JOB_ATTACH,
	RESPONSE_JOB_ATTACH,
	REQUEST_JOB_WILL_RUN,
	RESPONSE_JOB_WILL_RUN,
	REQUEST_JOB_ALLOCATION_INFO,
	RESPONSE_JOB_ALLOCATION_INFO,
	REQUEST_JOB_ALLOCATION_INFO_LITE,
	RESPONSE_JOB_ALLOCATION_INFO_LITE,
	REQUEST_UPDATE_JOB_TIME,
	REQUEST_JOB_READY,
	RESPONSE_JOB_READY,
	REQUEST_JOB_END_TIME,
	REQUEST_JOB_NOTIFY,
	REQUEST_JOB_SBCAST_CRED,
	RESPONSE_JOB_SBCAST_CRED,

	REQUEST_JOB_STEP_CREATE = 5001,
	RESPONSE_JOB_STEP_CREATE,
	REQUEST_RUN_JOB_STEP,
	RESPONSE_RUN_JOB_STEP,
	REQUEST_CANCEL_JOB_STEP,
	RESPONSE_CANCEL_JOB_STEP,
	DEFUNCT_REQUEST_COMPLETE_JOB_STEP, /* DEFUNCT */
	DEFUNCT_RESPONSE_COMPLETE_JOB_STEP, /* DEFUNCT */
	REQUEST_CHECKPOINT,
	RESPONSE_CHECKPOINT,
	REQUEST_CHECKPOINT_COMP,
	REQUEST_CHECKPOINT_TASK_COMP,
	RESPONSE_CHECKPOINT_COMP,
	REQUEST_SUSPEND,
	RESPONSE_SUSPEND,
	REQUEST_STEP_COMPLETE,
	REQUEST_COMPLETE_JOB_ALLOCATION,
	REQUEST_COMPLETE_BATCH_SCRIPT,
	MESSAGE_STAT_JOBACCT,
	REQUEST_STEP_LAYOUT,
	RESPONSE_STEP_LAYOUT,
	REQUEST_JOB_REQUEUE,
	REQUEST_DAEMON_STATUS,
	RESPONSE_SLURMD_STATUS,
	RESPONSE_SLURMCTLD_STATUS,

	REQUEST_LAUNCH_TASKS = 6001,
	RESPONSE_LAUNCH_TASKS,
	MESSAGE_TASK_EXIT,
	REQUEST_SIGNAL_TASKS,
	REQUEST_CHECKPOINT_TASKS,
	REQUEST_TERMINATE_TASKS,
	REQUEST_REATTACH_TASKS,
	RESPONSE_REATTACH_TASKS,
	REQUEST_KILL_TIMELIMIT,
	REQUEST_SIGNAL_JOB,
	REQUEST_TERMINATE_JOB,
	MESSAGE_EPILOG_COMPLETE,
	REQUEST_ABORT_JOB,	/* job shouldn't be running, kill it without
				 * job/step/task complete responses */
	REQUEST_FILE_BCAST,
	TASK_USER_MANAGED_IO_STREAM,

	SRUN_PING = 7001,
	SRUN_TIMEOUT,
	SRUN_NODE_FAIL,
	SRUN_JOB_COMPLETE,
	SRUN_USER_MSG,
	SRUN_EXEC,
	SRUN_STEP_MISSING,

	PMI_KVS_PUT_REQ = 7201,
	PMI_KVS_PUT_RESP,
	PMI_KVS_GET_REQ,
	PMI_KVS_GET_RESP,

	RESPONSE_SLURM_RC = 8001,

	RESPONSE_FORWARD_FAILED = 9001,

	ACCOUNTING_UPDATE_MSG = 10001,
	ACCOUNTING_FIRST_REG,

} slurm_msg_type_t;

typedef enum {
	CREDENTIAL1
} slurm_credential_type_t;

/*****************************************************************************\
 * core api configuration struct 
\*****************************************************************************/
typedef struct forward {
	uint16_t   cnt;		/* number of nodes to forward to */
	uint16_t   init;	/* tell me it has been set (FORWARD_INIT) */
	char      *nodelist;	/* ranged string of who to forward the
				 * message to */
	uint32_t   timeout;	/* original timeout increments */
} forward_t;

/*core api protocol message structures */
typedef struct slurm_protocol_header {
	uint16_t version;
	uint16_t flags;
	uint16_t msg_type; /* really slurm_msg_type_t but needs to be
			      uint16_t for packing purposes. */
	uint32_t body_length;
	uint16_t ret_cnt;
	forward_t forward;
	slurm_addr orig_addr;       
	List ret_list;
} header_t;

typedef struct forward_message {
	header_t header;
	char *buf;
	int buf_len;
	int timeout;
	List ret_list;
	pthread_mutex_t *forward_mutex;
	pthread_cond_t *notify;
} forward_msg_t;

typedef struct forward_struct {
	int timeout;
	uint16_t fwd_cnt;
	pthread_mutex_t forward_mutex;
	pthread_cond_t notify;
	forward_msg_t *forward_msg;
	char *buf;
	int buf_len;
	List ret_list;
} forward_struct_t;

typedef struct slurm_protocol_config {
	slurm_addr primary_controller;
	slurm_addr secondary_controller;
} slurm_protocol_config_t;

typedef struct slurm_msg {
	uint16_t msg_type; /* really a slurm_msg_type_t but needs to be
			    * this way for packing purposes.  message type */
	uint16_t flags;
	slurm_addr address;       
	slurm_fd conn_fd;
	void *auth_cred;
	void *data;
	uint32_t data_size;

	/* The following were all added for the forward.c code */
	forward_t forward;
	forward_struct_t *forward_struct;
	slurm_addr orig_addr;       
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

typedef struct association_shares_object {
	uint32_t assoc_id;	/* association ID */

	char *cluster;          /* cluster name */
	char *name;             /* name */
	char *parent;           /* parent name */

	double shares_norm;     /* normalized shares */
	uint32_t shares_raw;	/* number of shares allocated */

	double usage_efctv;	/* effective, normalized usage */
	double usage_norm;	/* normalized usage */
	uint64_t usage_raw;	/* measure of resource usage */

	uint16_t user;          /* 1 if user association 0 if account
				 * association */
} association_shares_object_t;

typedef struct shares_request_msg {
	List acct_list;
	List user_list;
} shares_request_msg_t;

typedef struct shares_response_msg {
	List assoc_shares_list; /* list of association_shares_object_t *'s */
	uint64_t tot_shares;
} shares_response_msg_t;

typedef struct priority_factors_object {
	uint32_t job_id;
	uint32_t user_id;

	double	 priority_age;
	double	 priority_fs;
	double	 priority_js;
	double	 priority_part;
	double	 priority_qos;

	uint16_t nice;
} priority_factors_object_t;

typedef struct priority_factors_request_msg {
	List	 job_id_list;
	List	 uid_list;
} priority_factors_request_msg_t;

typedef struct priority_factors_response_msg {
	List	 priority_factors_list;	/* priority_factors_object_t list */
} priority_factors_response_msg_t;

typedef struct job_step_kill_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint16_t signal;
	uint16_t batch_flag;
} job_step_kill_msg_t;

typedef struct job_notify_msg {
	uint32_t job_id;
	uint32_t job_step_id;	/* currently not used */
	char *   message;
} job_notify_msg_t;

typedef struct job_id_msg {
	uint32_t job_id;
} job_id_msg_t;

typedef struct job_step_id_msg {
	uint32_t job_id;
	uint32_t step_id;
} job_step_id_msg_t;

typedef struct job_info_request_msg {
	time_t last_update;
	uint16_t show_flags;
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

typedef struct node_info_select_request_msg {
	time_t last_update;
} node_info_select_request_msg_t;

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
	uint32_t job_id;
	uint32_t job_rc;
	uint32_t slurm_rc;
	char *node_name;
} complete_batch_script_msg_t;

typedef struct step_complete_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint32_t range_first;
	uint32_t range_last;
 	uint32_t step_rc;	/* largest task return code */
	jobacctinfo_t *jobacct;
} step_complete_msg_t;

typedef struct stat_jobacct_msg {
	uint32_t job_id;
	uint32_t return_code;
	uint32_t step_id;
	uint32_t num_tasks;
	jobacctinfo_t *jobacct;
} stat_jobacct_msg_t;

typedef struct kill_tasks_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint32_t signal;
} kill_tasks_msg_t;

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
	switch_node_info_t switch_nodeinfo;
} epilog_complete_msg_t;

typedef struct shutdown_msg {
	uint16_t options;
} shutdown_msg_t;

typedef struct last_update_msg {
	time_t last_update;
} last_update_msg_t;

typedef struct set_debug_level_msg {
	uint32_t debug_level;
} set_debug_level_msg_t;

typedef struct job_step_specs {
	uint16_t ckpt_interval;	/* checkpoint creation interval (minutes) */
	char *ckpt_dir; 	/* path to store checkpoint image files */
	uint32_t cpu_count;	/* count of required processors */
	uint16_t exclusive;	/* 1 if CPUs not shared with other steps */
	char *host;		/* host to contact initiating srun */
	uint16_t immediate;	/* 1 if allocate to run or fail immediately,
				 * 0 if to be queued awaiting resources */
	uint32_t job_id;	/* job ID */
	uint32_t mem_per_task;	/* MB memory required per task, 0=no limit */
	char *name;		/* name of the job step, default "" */
	char *network;		/* network use spec */
	uint32_t node_count;	/* count of required nodes */
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
	uint16_t task_dist;	/* see enum task_dist_state */
	uint32_t time_limit;	/* maximum run time in minutes, default is
				 * partition limit */
	uint32_t user_id;	/* user the job runs as */
} job_step_create_request_msg_t;

typedef struct job_step_create_response_msg {
	uint32_t job_step_id;		/* assigned job step id */
	char *resv_ports;		/* reserved ports */
	slurm_step_layout_t *step_layout; /* information about how the 
                                           * step is laid out */
	slurm_cred_t cred;    	  /* slurm job credential */
	switch_jobinfo_t switch_job;	/* switch context, opaque 
                                         * data structure */
} job_step_create_response_msg_t;

typedef struct launch_tasks_request_msg {
	uint32_t  job_id;
	uint32_t  job_step_id;
	uint32_t  nnodes;	/* number of nodes in this job step       */
	uint32_t  nprocs;	/* number of processes in this job step   */
	uint32_t  uid;
	uint32_t  gid;
	uint32_t  job_mem;	/* MB of memory reserved by job per node OR
				 * real memory per CPU | MEM_PER_CPU,
				 * default=0 (no limit) */
	uint16_t  *tasks_to_launch;
	uint32_t  envc;
	uint32_t  argc;
	uint16_t  multi_prog;
	uint16_t  *cpus_allocated;
	uint16_t  max_sockets;
	uint16_t  max_cores;
	uint16_t  max_threads;
	uint16_t  cpus_per_task;
	char    **env;
	char    **argv;
	char     *cwd;
	uint16_t cpu_bind_type;	/* --cpu_bind=                    */
	char     *cpu_bind;	/* binding map for map/mask_cpu           */
	uint16_t mem_bind_type;	/* --mem_bind=                    */
	char     *mem_bind;	/* binding map for tasks to memory        */
	uint16_t  num_resp_port;
	uint16_t  *resp_port;   /* array of available response ports      */

        /* Distribution at the lowest level of logical processor (lllp) */
	uint16_t task_dist;  /* --distribution=, -m dist	*/
	uint16_t  task_flags;
	uint32_t **global_task_ids;
	slurm_addr orig_addr;	  /* where message really came from for io */ 
	
	uint16_t user_managed_io; /* 0 for "normal" IO,
				     1 for "user manged" IO */
	uint8_t open_mode;	/* stdout/err append or truncate */
	uint8_t pty;		/* use pseudo tty */
	uint16_t acctg_freq;	/* accounting polling interval */

	/********** START "normal" IO only options **********/
	/* These options are ignored if user_managed_io is 1 */
	char     *ofname; /* stdout filename pattern */
	char     *efname; /* stderr filename pattern */
	char     *ifname; /* stdin filename pattern */
	uint8_t   buffered_stdio; /* 1 for line-buffered, 0 for unbuffered */
	uint8_t   labelio;  /* prefix output lines with the task number */
	uint16_t  num_io_port;
	uint16_t  *io_port;  /* array of available client IO listen ports */
	/**********  END  "normal" IO only options **********/

	char     *task_prolog;
	char     *task_epilog;

	uint16_t   slurmd_debug; /* remote slurmd debug level */

	slurm_cred_t cred;	/* job credential            */
	switch_jobinfo_t switch_job;	/* switch credential for the job */
	job_options_t options;  /* Arbitrary job options */
	char *complete_nodelist;
	char *ckpt_dir;		/* checkpoint path */
	char *restart_dir;	/* restart from checkpoint if set */
	char **spank_job_env;
	uint32_t spank_job_env_size;
} launch_tasks_request_msg_t;

typedef struct task_user_managed_io_msg {
	uint32_t task_id;
} task_user_managed_io_msg_t;

typedef struct partition_info partition_desc_msg_t;

typedef struct return_code_msg {
	uint32_t return_code;
} return_code_msg_t;

/* Note: We include the node list here for reliable cleanup on XCPU systems.
 *
 * Note: We include select_jobinfo here in addition to the job launch 
 * RPC in order to insure reliable clean-up of a BlueGene partition in
 * the event of some launch failure or race condition preventing slurmd 
 * from getting the MPIRUN_PARTITION at that time. It is needed for 
 * the job epilog. */
#define SIG_TIME_LIMIT	996	/* Dummy signal value i time limit reached */
#define SIG_ABORT	997	/* Dummy signal value to abort a job */
#define SIG_NODE_FAIL	998	/* Dummy signal value to signify node failure */
#define SIG_FAILURE	999	/* Dummy signal value to signify sys failure */
typedef struct kill_job_msg {
	uint32_t job_id;
	uint32_t step_id;
	uint16_t job_state;
	uint32_t job_uid;
	time_t   time;		/* slurmctld's time of request */
	char *nodes;
	select_jobinfo_t *select_jobinfo;	/* opaque data type */
	char **spank_job_env;
	uint32_t spank_job_env_size;
} kill_job_msg_t;

typedef struct signal_job_msg {
	uint32_t job_id;
	uint32_t signal;
} signal_job_msg_t;

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
	slurm_cred_t cred;      /* used only a weak authentication mechanism
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

typedef struct batch_job_launch_msg {
	uint32_t job_id;
	uint32_t step_id;
	uint32_t uid;
	uint32_t gid;
	uint32_t nprocs;	/* number of tasks in this job         */
	uint32_t num_cpu_groups;/* elements in below cpu arrays */
	uint16_t cpu_bind_type;	/* Internal for slurmd/task_affinity   */
	char     *cpu_bind;	/* Internal for slurmd/task_affinity   */
	uint16_t *cpus_per_node;/* cpus per node */
	uint32_t *cpu_count_reps;/* how many nodes have same cpu count */
	uint16_t cpus_per_task;	/* number of CPUs requested per task */
	char *nodes;		/* list of nodes allocated to job_step */
	char *script;		/* the actual job script, default NONE */
	char *err;		/* pathname of stderr */
	char *in;		/* pathname of stdin */
	char *out;		/* pathname of stdout */
	char *work_dir;		/* full pathname of working directory */
	char *ckpt_dir;		/* location to store checkpoint image */
	char *restart_dir;	/* retart execution from image in this dir */
	uint32_t argc;
	char **argv;
	uint32_t envc;		/* element count in environment */
	char **environment;	/* environment variables to set for job, 
				 *   name=value pairs, one per line */
	select_jobinfo_t *select_jobinfo;	/* opaque data type */
	slurm_cred_t cred;
	uint8_t open_mode;	/* stdout/err append or truncate */
	uint8_t overcommit;	/* if resources being over subscribed */
	uint16_t acctg_freq;	/* accounting polling interval	*/
	uint32_t job_mem;	/* memory limit for job		*/
	uint16_t restart_cnt;	/* batch job restart count	*/
	char **spank_job_env;	/* SPANK job environment variables */
	uint32_t spank_job_env_size;	/* size of spank_job_env */
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

typedef struct suspend_msg {
	uint16_t op;            /* suspend operation, see enum suspend_opts */
	uint32_t job_id;        /* slurm job_id */
} suspend_msg_t;

typedef struct kvs_get_msg {
	uint16_t task_id;	/* job step's task id */
	uint16_t size;		/* count of tasks in job */
	uint16_t port;		/* port to be sent the kvs data */
	char * hostname;	/* hostname to be sent the kvs data */
} kvs_get_msg_t;

typedef struct file_bcast_msg {
	char *fname;		/* name of the destination file */
	uint16_t block_no;	/* block number of this data */
	uint16_t last_block;	/* last block of bcast if set */
	uint16_t force;		/* replace existing file if set */
	uint16_t modes;		/* access rights for destination file */
	uint32_t uid;		/* owner for destination file */
	uint32_t gid;		/* group for destination file */
	time_t atime;		/* last access time for destination file */
	time_t mtime;		/* last modification time for dest file */
	sbcast_cred_t cred;	/* credential for the RPC */
	uint32_t block_len;	/* length of this data block */
	char *block;		/* data for this block */
} file_bcast_msg_t; 

typedef struct multi_core_data {
	uint16_t job_min_sockets;  /* minimum sockets per node, default=0 */
	uint16_t job_min_cores;    /* minimum cores per processor, default=0 */
	uint16_t job_min_threads;  /* minimum threads per core, default=0 */

	uint16_t min_sockets;	/* minimum number of sockets per node required
				 * by job, default=0 */
	uint16_t max_sockets;	/* maximum number of sockets per node usable 
				 * by job, default=unlimited (NO_VAL) */
	uint16_t min_cores;	/* minimum number of cores per cpu required
				 * by job, default=0 */
	uint16_t max_cores;	/* maximum number of cores per cpu usable
				 * by job, default=unlimited (NO_VAL) */
	uint16_t min_threads;	/* minimum number of threads per core required
				 * by job, default=0 */
	uint16_t max_threads;	/* maximum number of threads per core usable
				 * by job, default=unlimited (NO_VAL) */

	uint16_t ntasks_per_socket; /* number of tasks to invoke on each socket */
	uint16_t ntasks_per_core;   /* number of tasks to invoke on each core */
	uint16_t plane_size;        /* plane size when task_dist = SLURM_DIST_PLANE */
} multi_core_data_t;

typedef struct pty_winsz {
	uint16_t cols;
	uint16_t rows;
} pty_winsz_t;

typedef struct will_run_response_msg {
	uint32_t job_id;
	uint32_t proc_cnt;
	time_t start_time;
	char *node_list;
} will_run_response_msg_t;

/*****************************************************************************\
 * Slurm API Message Types
\*****************************************************************************/
typedef struct slurm_node_registration_status_msg {
	time_t timestamp;
	char *node_name;
	char *arch;
	char *os;
	uint16_t cpus;
	uint16_t sockets;
	uint16_t cores;
	uint16_t threads;
	uint32_t real_memory;
	uint32_t tmp_disk;
	uint32_t job_count;	/* number of associate job_id's */
	uint32_t *job_id;	/* IDs of running job (if any) */
	uint32_t *step_id;	/* IDs of running job steps (if any) */
	uint32_t status;	/* node status code, same as return codes */
	uint16_t startup;	/* slurmd just restarted */
	uint32_t up_time;	/* seconds since reboot */
	switch_node_info_t switch_nodeinfo;	/* set only if startup != 0 */
} slurm_node_registration_status_msg_t;


/*****************************************************************************\
 *      ACCOUNTING PUSHS
\*****************************************************************************/

typedef struct {
	List update_list; /* of type acct_update_object_t *'s */
	uint16_t rpc_version;
} accounting_update_msg_t;

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
 * OUT dest - Pointer to the slurm_msg_t which will be intialized.
 * NOTE: the "dest" structure will contain pointers into the contents of "src".
 */
extern void slurm_msg_t_copy(slurm_msg_t *dest, slurm_msg_t *src);

extern void slurm_destroy_char(void *object);
extern void slurm_destroy_uint32_ptr(void *object);
extern int slurm_addto_char_list(List char_list, char *names);
extern int slurm_sort_char_list_asc(char *name_a, char *name_b);
extern int slurm_sort_char_list_desc(char *name_a, char *name_b);

/* free message functions */
void inline slurm_free_checkpoint_tasks_msg(checkpoint_tasks_msg_t * msg);
void inline slurm_free_last_update_msg(last_update_msg_t * msg);
void inline slurm_free_return_code_msg(return_code_msg_t * msg);
void inline slurm_free_job_alloc_info_msg(job_alloc_info_msg_t * msg);
void inline slurm_free_job_info_request_msg(job_info_request_msg_t *msg);
void inline slurm_free_job_step_info_request_msg(
		job_step_info_request_msg_t *msg);
void inline slurm_free_node_info_request_msg(node_info_request_msg_t *msg);
void inline slurm_free_part_info_request_msg(part_info_request_msg_t *msg);
void inline slurm_free_resv_info_request_msg(resv_info_request_msg_t *msg);
void inline slurm_free_set_debug_level_msg(set_debug_level_msg_t *msg);
void inline slurm_destroy_association_shares_object(void *object);
void inline slurm_free_shares_request_msg(shares_request_msg_t *msg);
void inline slurm_free_shares_response_msg(shares_response_msg_t *msg);
void inline slurm_destroy_priority_factors_object(void *object);
void inline slurm_free_priority_factors_request_msg(
	priority_factors_request_msg_t *msg);
void inline slurm_free_priority_factors_response_msg(
	priority_factors_response_msg_t *msg);

#define	slurm_free_timelimit_msg(msg) \
	slurm_free_kill_job_msg(msg)

void inline slurm_free_shutdown_msg(shutdown_msg_t * msg);

void inline slurm_free_job_desc_msg(job_desc_msg_t * msg);

void inline
slurm_free_node_registration_status_msg(slurm_node_registration_status_msg_t *
					msg);

void inline slurm_free_job_info(job_info_t * job);
void inline slurm_free_job_info_members(job_info_t * job);

void inline slurm_free_job_id_msg(job_id_msg_t * msg);
void inline slurm_free_job_id_request_msg(job_id_request_msg_t * msg);
void inline slurm_free_job_id_response_msg(job_id_response_msg_t * msg);

void inline slurm_free_job_step_id_msg(job_step_id_msg_t *msg);

void inline slurm_free_job_launch_msg(batch_job_launch_msg_t * msg);

void inline slurm_free_update_node_msg(update_node_msg_t * msg);
void inline slurm_free_update_part_msg(update_part_msg_t * msg);
void inline slurm_free_delete_part_msg(delete_part_msg_t * msg);
void inline slurm_free_resv_desc_msg(resv_desc_msg_t * msg);
void inline slurm_free_resv_name_msg(reservation_name_msg_t * msg);
void inline slurm_free_resv_info_request_msg(resv_info_request_msg_t * msg);
void inline
slurm_free_job_step_create_request_msg(job_step_create_request_msg_t * msg);
void inline
slurm_free_job_step_create_response_msg(job_step_create_response_msg_t *msg);
void inline 
slurm_free_complete_job_allocation_msg(complete_job_allocation_msg_t * msg);
void inline
slurm_free_complete_batch_script_msg(complete_batch_script_msg_t * msg);
void inline 
slurm_free_launch_tasks_request_msg(launch_tasks_request_msg_t * msg);
void inline 
slurm_free_launch_tasks_response_msg(launch_tasks_response_msg_t * msg);
void inline slurm_free_task_user_managed_io_stream_msg(
	task_user_managed_io_msg_t *msg);
void inline slurm_free_task_exit_msg(task_exit_msg_t * msg);
void inline slurm_free_kill_tasks_msg(kill_tasks_msg_t * msg);
void inline 
slurm_free_reattach_tasks_request_msg(reattach_tasks_request_msg_t * msg);
void inline
slurm_free_reattach_tasks_response_msg(reattach_tasks_response_msg_t * msg);
void inline slurm_free_kill_job_msg(kill_job_msg_t * msg);
void inline slurm_free_signal_job_msg(signal_job_msg_t * msg);
void inline slurm_free_update_job_time_msg(job_time_msg_t * msg);
void inline slurm_free_job_step_kill_msg(job_step_kill_msg_t * msg);
void inline slurm_free_epilog_complete_msg(epilog_complete_msg_t * msg);
void inline slurm_free_srun_job_complete_msg(srun_job_complete_msg_t * msg);
void inline slurm_free_srun_exec_msg(srun_exec_msg_t *msg);
void inline slurm_free_srun_ping_msg(srun_ping_msg_t * msg);
void inline slurm_free_srun_node_fail_msg(srun_node_fail_msg_t * msg);
void inline slurm_free_srun_step_missing_msg(srun_step_missing_msg_t * msg);
void inline slurm_free_srun_timeout_msg(srun_timeout_msg_t * msg);
void inline slurm_free_srun_user_msg(srun_user_msg_t * msg);
void inline slurm_free_checkpoint_msg(checkpoint_msg_t *msg);
void inline slurm_free_checkpoint_comp_msg(checkpoint_comp_msg_t *msg);
void inline slurm_free_checkpoint_task_comp_msg(checkpoint_task_comp_msg_t *msg);
void inline slurm_free_checkpoint_resp_msg(checkpoint_resp_msg_t *msg);
void inline slurm_free_suspend_msg(suspend_msg_t *msg);
void slurm_free_resource_allocation_response_msg (
		resource_allocation_response_msg_t * msg);
void slurm_free_job_alloc_info_response_msg (
		job_alloc_info_response_msg_t * msg);
void slurm_free_job_step_create_response_msg(
		job_step_create_response_msg_t * msg);
void slurm_free_submit_response_response_msg(submit_response_msg_t * msg);
void slurm_free_ctl_conf(slurm_ctl_conf_info_msg_t * config_ptr);
void slurm_free_job_info_msg(job_info_msg_t * job_buffer_ptr);
void slurm_free_job_step_info_response_msg(
		job_step_info_response_msg_t * msg);
void slurm_free_node_info_msg(node_info_msg_t * msg);
void slurm_free_partition_info_msg(partition_info_msg_t * msg);
void slurm_free_get_kvs_msg(kvs_get_msg_t *msg);
void slurm_free_will_run_response_msg(will_run_response_msg_t *msg);
void inline slurm_free_file_bcast_msg(file_bcast_msg_t *msg);
void inline slurm_free_step_complete_msg(step_complete_msg_t *msg);
void inline slurm_free_stat_jobacct_msg(stat_jobacct_msg_t *msg);
void inline slurm_free_node_select_msg(
		node_info_select_request_msg_t *msg);
void inline slurm_free_job_notify_msg(job_notify_msg_t * msg);

void inline slurm_free_accounting_update_msg(accounting_update_msg_t *msg);

extern int slurm_free_msg_data(slurm_msg_type_t type, void *data);
extern uint32_t slurm_get_return_code(slurm_msg_type_t type, void *data);

extern char *job_reason_string(enum job_state_reason inx);
extern char *job_state_string(uint16_t inx);
extern char *job_state_string_compact(uint16_t inx);
extern char *node_state_string(uint16_t inx);
extern char *node_state_string_compact(uint16_t inx);
extern void  private_data_string(uint16_t private_data, char *str, int str_len);
extern void  accounting_enforce_string(uint16_t enforce,
				       char *str, int str_len);
extern char *conn_type_string(enum connection_type conn_type);
#ifdef HAVE_BGL
extern char *node_use_string(enum node_use_type node_use);
#endif
/* Translate a state enum to a readable string */
extern char *bg_block_state_string(uint16_t state);


/* Validate SPANK specified job environment does not contain any invalid
 * names. Log failures using info() */
extern bool valid_spank_job_env(char **spank_job_env, 
			        uint32_t spank_job_env_size, uid_t uid);

/* user needs to xfree after */
extern char *reservation_flags_string(uint16_t flags);

#define safe_read(fd, buf, size) do {					\
		int remaining = size;					\
		char *ptr = (char *) buf;				\
		int rc;							\
		while (remaining > 0) {					\
			rc = read(fd, ptr, remaining);			\
			if ((rc == 0) && (remaining == size))		\
				goto rwfail;				\
			else if (rc == 0) {				\
				debug("%s:%d: %s: safe_read (%d of %d) EOF", \
				      __FILE__, __LINE__, __CURRENT_FUNC__, \
				      remaining, (int)size);		\
				goto rwfail;				\
			} else if (rc < 0) {				\
				debug("%s:%d: %s: safe_read (%d of %d) failed: %m", \
				      __FILE__, __LINE__, __CURRENT_FUNC__, \
				      remaining, (int)size);		\
				goto rwfail;				\
			} else {					\
				ptr += rc;				\
				remaining -= rc;			\
				if (remaining > 0)			\
					debug3("%s:%d: %s: safe_read (%d of %d) partial read", \
					       __FILE__, __LINE__, __CURRENT_FUNC__, \
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
				      __FILE__, __LINE__, __CURRENT_FUNC__, \
				      remaining, (int)size);		\
				goto rwfail;				\
			} else {					\
				ptr += rc;				\
				remaining -= rc;			\
				if (remaining > 0)			\
					debug3("%s:%d: %s: safe_write (%d of %d) partial write", \
					       __FILE__, __LINE__, __CURRENT_FUNC__, \
					       remaining, (int)size);	\
			}						\
		}							\
	} while (0)

#endif
