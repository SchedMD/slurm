/****************************************************************************\
 *  slurm_protocol_defs.h - definitions used for RPCs
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
#  include <config.h>
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#  ifdef HAVE_LIBELAN3
#    include <src/common/qsw.h>
#  endif
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <src/api/slurm.h>
#include <src/common/macros.h>
#include <src/common/xassert.h>
#include <src/common/slurm_protocol_common.h>


/* used to define the type of the io_stream_header_t.type
 */
enum io_stream_types {
	SLURM_IO_STREAM_INOUT = 0,
	SLURM_IO_STREAM_SIGERR = 1
};

/* used to define flags of the launch_tasks_request_msg_t.task_flags
 */
enum task_flag_vals {
	TASK_TOTALVIEW_DEBUG = 0x1,
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

	REQUEST_UPDATE_JOB = 3001,
	REQUEST_UPDATE_NODE,
	REQUEST_UPDATE_PARTITION,

	REQUEST_RESOURCE_ALLOCATION = 4001,
	RESPONSE_RESOURCE_ALLOCATION,
	REQUEST_SUBMIT_BATCH_JOB,
	RESPONSE_SUBMIT_BATCH_JOB,
	REQUEST_BATCH_JOB_LAUNCH,
	RESPONSE_BATCH_JOB_LAUNCH,
	REQUEST_SIGNAL_JOB,
	RESPONSE_SIGNAL_JOB,
	REQUEST_CANCEL_JOB,
	RESPONSE_CANCEL_JOB,
	REQUEST_JOB_RESOURCE,
	RESPONSE_JOB_RESOURCE,
	REQUEST_JOB_ATTACH,
	RESPONSE_JOB_ATTACH,
	REQUEST_IMMEDIATE_RESOURCE_ALLOCATION,
	RESPONSE_IMMEDIATE_RESOURCE_ALLOCATION,
	REQUEST_JOB_WILL_RUN,
	RESPONSE_JOB_WILL_RUN,
	REQUEST_REVOKE_JOB_CREDENTIAL,
	REQUEST_ALLOCATION_AND_RUN_JOB_STEP,
	RESPONSE_ALLOCATION_AND_RUN_JOB_STEP,
	REQUEST_OLD_JOB_RESOURCE_ALLOCATION,

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

	REQUEST_LAUNCH_TASKS = 6001,
	RESPONSE_LAUNCH_TASKS,
	MESSAGE_TASK_EXIT,
	REQUEST_KILL_TASKS,
	REQUEST_REATTACH_TASKS_STREAMS,
	RESPONSE_REATTACH_TASKS_STREAMS,

	RESPONSE_SLURM_RC = 8001,
	MESSAGE_UPLOAD_ACCOUNTING_INFO,
} slurm_msg_type_t;

typedef enum {
	CREDENTIAL1
} slurm_credential_type_t;

/******************************************************************************
 * core api configuration struct 
 ******************************************************************************/
typedef struct slurm_protocol_config {
	slurm_addr primary_controller;
	slurm_addr secondary_controller;
} slurm_protocol_config_t;

/*core api protocol message structures */
typedef struct slurm_protocol_header {
	uint16_t version;
	uint16_t flags;
	slurm_credential_type_t cred_type;
	uint32_t cred_length;
	slurm_msg_type_t msg_type;
	uint32_t body_length;
} header_t;

typedef struct slurm_io_stream_header {
	uint16_t version;	/*version/magic number */
	char key[SLURM_SSL_SIGNATURE_LENGTH];
	uint32_t task_id;
	uint16_t type;
} slurm_io_stream_header_t;

typedef struct slurm_msg {
	slurm_msg_type_t msg_type;
	slurm_addr address;
	slurm_fd conn_fd;
	slurm_credential_type_t cred_type;
	void *cred;
	uint32_t cred_size;
	void *data;
	uint32_t data_size;
} slurm_msg_t;


/*****************************************************************************
 * Slurm Protocol Data Structures
 *****************************************************************************/
typedef struct job_step_id {
	time_t last_update;
	uint32_t job_id;
	uint32_t job_step_id;
} job_step_id_t;

typedef struct job_id_msg {
	uint32_t job_id;
} job_id_msg_t;

typedef struct job_step_id job_step_id_msg_t;
typedef struct job_step_id job_info_request_msg_t;

typedef struct job_step_info_request_msg {
	time_t last_update;
	uint32_t job_id;
	uint32_t step_id;
} job_step_info_request_msg_t;

typedef struct kill_tasks_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint32_t signal;
} kill_tasks_msg_t;

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
	char    **env;
	char    **argv;
	char     *cwd;
	uint16_t  resp_port;
	uint16_t  io_port;
	uint16_t  task_flags;
	uint32_t *global_task_ids;

	slurm_job_credential_t *credential;	/* job credential            */

#ifdef HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;	/* Elan3 switch context */
#endif
} launch_tasks_request_msg_t;

typedef struct launch_tasks_response_msg {
	uint32_t return_code;
	char *node_name;
	uint32_t srun_node_id;
	uint32_t local_pid;
} launch_tasks_response_msg_t;

typedef struct task_ext_msg {
	uint32_t task_id;
	uint32_t return_code;
} task_exit_msg_t;

typedef struct partition_info partition_desc_msg_t;

typedef struct return_code_msg {
	int32_t return_code;
} return_code_msg_t;

typedef struct revoke_credential_msg {
	uint32_t job_id;
	time_t expiration_time;
	char signature[SLURM_SSL_SIGNATURE_LENGTH];
} revoke_credential_msg_t;

typedef struct reattach_tasks_streams_msg {
	uint32_t job_id;
	uint32_t job_step_id;
	uint32_t uid;
	slurm_job_credential_t *credential;
	uint32_t tasks_to_reattach;
	slurm_addr streams;
	uint32_t *global_task_ids;
} reattach_tasks_streams_msg_t;

typedef struct batch_job_launch_msg {
	uint32_t job_id;
	uint32_t user_id;
	char *nodes;		/* comma delimited list of nodes allocated to job_step */
	char *script;		/* the actual job script, default NONE */
	char *err;		/* pathname of stderr */
	char *in;		/* pathname of stdin */
	char *out;		/* pathname of stdout */
	char *work_dir;		/* fully qualified pathname of working directory */
	uint16_t argc;
	char **argv;
	uint16_t env_size;	/* element count in environment */
	char **environment;	/* environment variables to set for job, 
				   *   name=value pairs, one per line */
} batch_job_launch_msg_t;

/****************************************************************************
 * Slurm API Message Types
 ****************************************************************************/
typedef struct slurm_node_registration_status_msg {
	uint32_t timestamp;
	char *node_name;
	uint32_t cpus;
	uint32_t real_memory_size;
	uint32_t temporary_disk_space;
	uint32_t job_count;	/* number of associate job_id's */
	uint32_t *job_id;	/* IDs of running job (if any) */
	uint16_t *step_id;	/* IDs of running job steps (if any) */
} slurm_node_registration_status_msg_t;

typedef struct slurm_ctl_conf slurm_ctl_conf_info_msg_t;

/* free message functions */
void inline slurm_free_last_update_msg(last_update_msg_t * msg);
void inline slurm_free_return_code_msg(return_code_msg_t * msg);
void inline slurm_free_job_step_id(job_step_id_t * msg);

#define slurm_free_job_step_id_msg(msg) slurm_free_job_step_id((job_step_id_t*)(msg))
#define slurm_free_job_step_info_request_msg(msg) slurm_free_job_step_id(msg)
#define slurm_free_job_info_request_msg(msg) slurm_free_job_step_id(msg)

void inline slurm_free_shutdown_msg (shutdown_msg_t * msg);

void inline slurm_free_job_desc_msg(job_desc_msg_t * msg);

void inline
slurm_free_node_registration_status_msg (slurm_node_registration_status_msg_t * msg);

void inline slurm_free_job_info(job_info_t * job);
void inline slurm_free_job_info_members(job_info_t * job);

void inline slurm_free_job_launch_msg(batch_job_launch_msg_t * msg);

void inline slurm_free_update_node_msg(update_node_msg_t * msg);
void inline slurm_free_update_part_msg(update_part_msg_t * msg);
void inline
slurm_free_job_step_create_request_msg(job_step_create_request_msg_t *
				       msg);
void inline slurm_free_launch_tasks_request_msg(launch_tasks_request_msg_t
						* msg);
void inline
slurm_free_launch_tasks_response_msg(launch_tasks_response_msg_t * msg);
void inline slurm_free_task_exit_msg(task_exit_msg_t * msg);
void inline slurm_free_kill_tasks_msg(kill_tasks_msg_t * msg);
void inline
slurm_free_reattach_tasks_streams_msg(reattach_tasks_streams_msg_t * msg);
void inline slurm_free_revoke_credential_msg(revoke_credential_msg_t * msg);

extern char *job_dist_string(uint16_t inx);
extern char *job_state_string(enum job_states inx);
extern char *job_state_string_compact(enum job_states inx);
extern char *node_state_string(enum node_states inx);
extern char *node_state_string_compact(enum node_states inx);

#endif
