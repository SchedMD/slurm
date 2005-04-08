/****************************************************************************\
 *  slurm_protocol_defs.h - definitions used for RPCs
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#include "src/common/macros.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/switch.h"
#include "src/common/xassert.h"


/* used to define flags of the launch_tasks_request_msg_t.and
 * spawn task_request_msg_t task_flags
 */
enum task_flag_vals {
	TASK_PARALLEL_DEBUG = 0x1,
	TASK_UNUSED1 = 0x2,
	TASK_UNUSED2 = 0x4
};

enum part_shared {
	SHARED_NO,		/* Nodes never shared in partition */
	SHARED_YES,		/* Nodes possible to share in partition */
	SHARED_FORCE		/* Nodes always shares in partition */
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

	REQUEST_UPDATE_JOB = 3001,
	REQUEST_UPDATE_NODE,
	REQUEST_UPDATE_PARTITION,
	REQUEST_DELETE_PARTITION,

	REQUEST_RESOURCE_ALLOCATION = 4001,
	RESPONSE_RESOURCE_ALLOCATION,
	REQUEST_SUBMIT_BATCH_JOB,
	RESPONSE_SUBMIT_BATCH_JOB,
	REQUEST_BATCH_JOB_LAUNCH,
	REQUEST_SIGNAL_JOB,
	RESPONSE_SIGNAL_JOB,
	REQUEST_CANCEL_JOB,
	RESPONSE_CANCEL_JOB,
	REQUEST_JOB_RESOURCE,
	RESPONSE_JOB_RESOURCE,
	REQUEST_JOB_ATTACH,
	RESPONSE_JOB_ATTACH,
	REQUEST_JOB_WILL_RUN,
	RESPONSE_JOB_WILL_RUN,
	REQUEST_ALLOCATION_AND_RUN_JOB_STEP,
	RESPONSE_ALLOCATION_AND_RUN_JOB_STEP,
	REQUEST_OLD_JOB_RESOURCE_ALLOCATION,
	REQUEST_UPDATE_JOB_TIME,
	REQUEST_JOB_READY,
	RESPONSE_JOB_READY,

	REQUEST_JOB_STEP_CREATE = 5001,
	RESPONSE_JOB_STEP_CREATE,
	REQUEST_RUN_JOB_STEP,
	RESPONSE_RUN_JOB_STEP,
	REQUEST_SIGNAL_JOB_STEP,
	RESPONSE_SIGNAL_JOB_STEP,
	REQUEST_CANCEL_JOB_STEP,
	RESPONSE_CANCEL_JOB_STEP,
	REQUEST_COMPLETE_JOB_STEP,
	RESPONSE_COMPLETE_JOB_STEP,
	REQUEST_CHECKPOINT,
	RESPONSE_CHECKPOINT,

	REQUEST_LAUNCH_TASKS = 6001,
	RESPONSE_LAUNCH_TASKS,
	MESSAGE_TASK_EXIT,
	REQUEST_KILL_TASKS,
	REQUEST_REATTACH_TASKS,
	RESPONSE_REATTACH_TASKS,
	REQUEST_KILL_TIMELIMIT,
	REQUEST_KILL_JOB,
	MESSAGE_EPILOG_COMPLETE,
	REQUEST_SPAWN_TASK,

	SRUN_PING = 7001,
	SRUN_TIMEOUT,
	SRUN_NODE_FAIL,

	RESPONSE_SLURM_RC = 8001,
	MESSAGE_UPLOAD_ACCOUNTING_INFO,
} slurm_msg_type_t;

typedef enum {
	CREDENTIAL1
} slurm_credential_type_t;

/*****************************************************************************\
 * core api configuration struct 
\*****************************************************************************/
typedef struct slurm_protocol_config {
	slurm_addr primary_controller;
	slurm_addr secondary_controller;
} slurm_protocol_config_t;

/*core api protocol message structures */
typedef struct slurm_protocol_header {
	uint16_t version;
	uint16_t flags;
	slurm_msg_type_t msg_type;
	uint32_t body_length;
} header_t;

typedef struct slurm_msg {
	slurm_msg_type_t msg_type;
	slurm_addr address;
	slurm_fd conn_fd;
	void *cred;
	void *data;
	uint32_t data_size;
} slurm_msg_t;


/*****************************************************************************\
 * Slurm Protocol Data Structures
\*****************************************************************************/

typedef struct job_step_kill_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint16_t signal;
	uint16_t batch_flag;
} job_step_kill_msg_t;

typedef struct job_id_msg {
	uint32_t job_id;
} job_id_msg_t;

typedef struct job_step_id job_step_id_msg_t;

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

typedef struct complete_job_step_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint32_t job_rc;
	uint32_t slurm_rc;
	char *node_name;
} complete_job_step_msg_t;

typedef struct kill_tasks_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint32_t signal;
} kill_tasks_msg_t;

typedef struct epilog_complete_msg {
	uint32_t job_id;
	uint32_t return_code;
	char    *node_name;
} epilog_complete_msg_t;

typedef struct shutdown_msg {
	uint16_t core;
} shutdown_msg_t;

typedef struct last_update_msg {
	time_t last_update;
} last_update_msg_t;

typedef struct launch_tasks_request_msg {
	uint32_t  job_id;
	uint32_t  job_step_id;
	uint32_t  nnodes;	/* number of nodes in this job step       */
	uint32_t  nprocs;	/* number of processes in this job step   */
	uint32_t  uid;
	uint32_t  srun_node_id;	/* node id of this node (relative to job) */
	uint32_t  tasks_to_launch;
	uint16_t  envc;
	uint16_t  argc;
	uint16_t  cpus_allocated;
	char    **env;
	char    **argv;
	char     *cwd;
	uint16_t  resp_port;
	uint16_t  io_port;
	uint16_t  task_flags;
	uint32_t *global_task_ids;

	/* stdout/err/in per task filenames */
	char     *ofname;
	char     *efname;
	char     *ifname;

	int32_t   slurmd_debug; /* remote slurmd debug level */

	slurm_cred_t cred;	/* job credential            */
	switch_jobinfo_t switch_job;	/* switch credential for the job */
} launch_tasks_request_msg_t;

typedef struct launch_tasks_response_msg {
	uint32_t return_code;
	char    *node_name;
	uint32_t srun_node_id;
	uint32_t count_of_pids;
	uint32_t *local_pids;
} launch_tasks_response_msg_t;

typedef struct spawn_task_request_msg {
	uint32_t  job_id;
	uint32_t  job_step_id;
	uint32_t  nnodes;	/* number of nodes in this job step       */
	uint32_t  nprocs;	/* number of processes in this job step   */
	uint32_t  uid;
	uint32_t  srun_node_id;	/* node id of this node (relative to job) */
	uint16_t  envc;
	uint16_t  argc;
	uint16_t  cpus_allocated;
	char    **env;
	char    **argv;
	char     *cwd;
	uint16_t  io_port;
	uint16_t  task_flags;
	uint32_t  global_task_id;

	int32_t   slurmd_debug; /* remote slurmd debug level */

	slurm_cred_t cred;	/* job credential            */
	switch_jobinfo_t switch_job;	/* switch credential for the job */
} spawn_task_request_msg_t;

typedef struct task_ext_msg {
	uint32_t num_tasks;
	uint32_t *task_id_list;
	uint32_t return_code;
} task_exit_msg_t;

typedef struct partition_info partition_desc_msg_t;

typedef struct return_code_msg {
	int32_t return_code;
} return_code_msg_t;

/* Note: We include select_jobinfo here in addition to the job launch 
 * RPC in order to insure reliable clean-up of a BlueGene partition in
 * the event of some launch failure or race condition preventing slurmd 
 * from getting the MPIRUN_PARTITION at that time. It is needed for 
 * the job epilog. */
typedef struct kill_job_msg {
	uint32_t job_id;
	uint32_t job_uid;
	select_jobinfo_t select_jobinfo;	/* opaque data type */
} kill_job_msg_t;

typedef struct job_time_msg {
	uint32_t job_id;
	time_t expiration_time;
} job_time_msg_t;

typedef struct reattach_tasks_request_msg {
	uint32_t     job_id;
	uint32_t     job_step_id;
	uint32_t     srun_node_id;
	uint16_t     resp_port;
	uint16_t     io_port;
	char        *ofname;
	char        *efname;
	char        *ifname;
	slurm_cred_t cred;
} reattach_tasks_request_msg_t;

typedef struct reattach_tasks_response_msg {
	char     *node_name;
	char     *executable_name;
	uint32_t  return_code;
	uint32_t  srun_node_id;
	uint32_t  ntasks;       /* number of tasks on this node     */
	uint32_t *gids;         /* Global task id assignments       */
	uint32_t *local_pids;   /* list of process ids on this node */
} reattach_tasks_response_msg_t;

typedef struct batch_job_launch_msg {
	uint32_t job_id;
	uint32_t uid;
	uint32_t nprocs;	/* number of tasks in this job         */
	uint16_t num_cpu_groups;/* elements in below cpu arrays */
	uint32_t *cpus_per_node;/* cpus per node */
	uint32_t *cpu_count_reps;/* how many nodes have same cpu count */
	char *nodes;		/* list of nodes allocated to job_step */
	char *script;		/* the actual job script, default NONE */
	char *err;		/* pathname of stderr */
	char *in;		/* pathname of stdin */
	char *out;		/* pathname of stdout */
	char *work_dir;		/* full pathname of working directory */
	uint16_t argc;
	char **argv;
	uint16_t envc;		/* element count in environment */
	char **environment;	/* environment variables to set for job, 
				 *   name=value pairs, one per line */
	select_jobinfo_t select_jobinfo;	/* opaque data type */
} batch_job_launch_msg_t;

typedef struct job_id_request_msg {
	uint32_t job_pid;	/* local process_id of a job */
} job_id_request_msg_t;

typedef struct job_id_response_msg {
	uint32_t job_id;	/* slurm job_id */
} job_id_response_msg_t;

typedef struct srun_ping_msg {
	uint32_t job_id;	/* slurm job_id */
	uint32_t step_id;	/* step_id or NO_VAL */
} srun_ping_msg_t;

typedef struct srun_node_fail_msg {
	uint32_t job_id;	/* slurm job_id */
	uint32_t step_id;	/* step_id or NO_VAL */
	char *nodelist;		/* name of failed node(s) */
} srun_node_fail_msg_t;

typedef struct srun_timeout_msg {
	uint32_t job_id;	/* slurm job_id */
	uint32_t step_id;	/* step_id or NO_VAL */
	time_t   timeout;	/* when job scheduled to be killed */
} srun_timeout_msg_t;

typedef struct checkpoint_msg {
	uint16_t op;		/* checkpoint operation, see enum check_opts */
	uint16_t data;		/* operation specific data */
	uint32_t job_id;	/* slurm job_id */
	uint32_t step_id;	/* slurm step_id */
} checkpoint_msg_t;

typedef struct checkpoint_resp_msg {
	uint16_t ckpt_errno;	/* errno from last checkpoint operation */
	char * ckpt_strerror;	/* string descriptive of ckpt_errno */
} checkpoint_resp_msg_t;

/*****************************************************************************\
 * Slurm API Message Types
\*****************************************************************************/
typedef struct slurm_node_registration_status_msg {
	time_t timestamp;
	char *node_name;
	uint32_t cpus;
	uint32_t real_memory_size;
	uint32_t temporary_disk_space;
	uint32_t job_count;	/* number of associate job_id's */
	uint32_t *job_id;	/* IDs of running job (if any) */
	uint16_t *step_id;	/* IDs of running job steps (if any) */
	uint32_t status;	/* node status code, same as return codes */
	switch_node_info_t switch_nodeinfo;
} slurm_node_registration_status_msg_t;

typedef struct slurm_ctl_conf slurm_ctl_conf_info_msg_t;

/* free message functions */
void inline slurm_free_last_update_msg(last_update_msg_t * msg);
void inline slurm_free_return_code_msg(return_code_msg_t * msg);
void inline slurm_free_old_job_alloc_msg(old_job_alloc_msg_t * msg);
void inline slurm_free_job_info_request_msg(job_info_request_msg_t *msg);
void inline slurm_free_job_step_info_request_msg(
		job_step_info_request_msg_t *msg);
void inline slurm_free_node_info_request_msg(
		node_info_request_msg_t *msg);
void inline slurm_free_part_info_request_msg(
		part_info_request_msg_t *msg);

#define	slurm_free_timelimit_msg(msg) \
	slurm_free_kill_job_msg(msg)

void inline slurm_free_shutdown_msg(shutdown_msg_t * msg);

void inline slurm_free_job_desc_msg(job_desc_msg_t * msg);
void inline slurm_free_job_complete_msg(complete_job_step_msg_t * msg);

void inline
slurm_free_node_registration_status_msg(slurm_node_registration_status_msg_t *
					msg);

void inline slurm_free_job_info(job_info_t * job);
void inline slurm_free_job_info_members(job_info_t * job);

void inline slurm_free_job_id_msg(job_id_msg_t * msg);
void inline slurm_free_job_id_request_msg(job_id_request_msg_t * msg);
void inline slurm_free_job_id_response_msg(job_id_response_msg_t * msg);

void inline slurm_free_job_launch_msg(batch_job_launch_msg_t * msg);

void inline slurm_free_update_node_msg(update_node_msg_t * msg);
void inline slurm_free_update_part_msg(update_part_msg_t * msg);
void inline slurm_free_delete_part_msg(delete_part_msg_t * msg);
void inline
slurm_free_job_step_create_request_msg(job_step_create_request_msg_t * msg);
void inline slurm_free_launch_tasks_request_msg(launch_tasks_request_msg_t *
						msg);
void inline slurm_free_launch_tasks_response_msg(launch_tasks_response_msg_t *
						 msg);
void inline slurm_free_spawn_task_request_msg(spawn_task_request_msg_t *
						msg);
void inline slurm_free_task_exit_msg(task_exit_msg_t * msg);
void inline slurm_free_kill_tasks_msg(kill_tasks_msg_t * msg);

void inline 
slurm_free_reattach_tasks_request_msg(reattach_tasks_request_msg_t * msg);
void inline
slurm_free_reattach_tasks_response_msg(reattach_tasks_response_msg_t * msg);

void inline slurm_free_kill_job_msg(kill_job_msg_t * msg);
void inline slurm_free_update_job_time_msg(job_time_msg_t * msg);

void inline slurm_free_job_step_kill_msg(job_step_kill_msg_t * msg);

void inline slurm_free_epilog_complete_msg(epilog_complete_msg_t * msg);

void inline slurm_free_srun_ping_msg(srun_ping_msg_t * msg);
void inline slurm_free_srun_node_fail_msg(srun_node_fail_msg_t * msg);
void inline slurm_free_srun_timeout_msg(srun_timeout_msg_t * msg);
void inline slurm_free_checkpoint_msg(checkpoint_msg_t *msg);
void inline slurm_free_checkpoint_resp_msg(checkpoint_resp_msg_t *msg);

void slurm_free_resource_allocation_response_msg (
		resource_allocation_response_msg_t * msg);
void slurm_free_resource_allocation_and_run_response_msg (
		resource_allocation_and_run_response_msg_t * msg);
void slurm_free_job_step_create_response_msg(
		job_step_create_response_msg_t * msg);
void slurm_free_submit_response_response_msg(submit_response_msg_t * msg);
void slurm_free_ctl_conf(slurm_ctl_conf_info_msg_t * config_ptr);
void slurm_free_job_info_msg(job_info_msg_t * job_buffer_ptr);
void slurm_free_job_step_info_response_msg(
		job_step_info_response_msg_t * msg);
void slurm_free_node_info_msg(node_info_msg_t * msg);
void slurm_free_partition_info_msg(partition_info_msg_t * msg);

extern char *job_reason_string(enum job_wait_reason inx);
extern char *job_state_string(enum job_states inx);
extern char *job_state_string_compact(enum job_states inx);
extern char *node_state_string(enum node_states inx);
extern char *node_state_string_compact(enum node_states inx);

#endif
