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
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

/* true, false */
#define true -1
#define false 0

#include <src/common/slurm_protocol_common.h>

/* for sendto and recvfrom commands */
#define NO_SEND_RECV_FLAGS 0

#define DEFAULT_LISTEN_BACKLOG 10

/* used in interface methods */
#define SLURM_NOT_IMPLEMENTED -2 


#define SLURM_PROTOCOL_SUCCESS 0
#define SLURM_PROTOCOL_FAILURE -1

#define SLURM_SOCKET_ERROR -1

#define MAX_MESSAGE_BUFFER_SIZE 4096


#define SLURM_PROTOCOL_VERSION 1
#define SLURM_PROTOCOL_NO_FLAGS 0

#define SLURM_PROTOCOL_VERSION_ERROR -100



/* SLURM Message types */
typedef enum { 
	REQUEST_NODE_REGISRATION_STATUS 	= 1,
	MESSAGE_NODE_REGISRATION_STATUS 	= 2,
	REQUEST_RESOURCE_ALLOCATION 		= 3,
	RESPONSE_RESOURCE_ALLOCATION		= 4,
	REQUEST_CANCEL_JOB			= 5,
	REQUEST_RECONFIGURE			= 6,
	RESPONSE_CANCEL_JOB			= 7,
	REQUEST_JOB_INFO			= 8,
	RESPONSE_JOB_INFO			= 9,
	REQUEST_JOB_STEP_INFO			= 10,
	RESPONSE_JOB_STEP_INFO			= 11,
	REQUEST_NODE_INFO			= 12,
	RESPONSE_NODE_INFO			= 13,
	REQUEST_PARTITION_INFO			= 14,
	RESPONSE_PARTITION_INFO			= 15,
	REQUEST_ACCTING_INFO			= 16,
	RESPONSE_ACCOUNTING_INFO		= 17,
	REQUEST_BUILD_INFO			= 18,
	RESPONSE_BUILD_INFO			= 19,
	MESSAGE_UPLOAD_ACCOUNTING_INFO		= 20,
	RESPONSE_RECONFIGURE			= 21,
	REQUEST_SUBMIT_BATCH_JOB		= 22,
	RESPONSE_SUBMIT_BATCH_JOB		= 23,
	REQUEST_CANCEL_JOB_STEP			= 24, 
	RESPONSE_CANCEL_JOB_STEP		= 25,
	REQUEST_SIGNAL_JOB			= 26,
	RESPONSE_SIGNAL_JOB			= 27,
	REQUEST_SIGNAL_JOB_STEP			= 28,
	RESPONSE_SIGNAL_JOB_STEP		= 29,
	REQUEST_BATCH_JOB_LAUNCH		= 30,
	RESPONSE_BATCH_JOB_LAUNCH		= 31,
	MESSAGE_TASK_EXIT			= 32,
	MESSAGE_REVOKE_JOB_CREDENTIAL		= 33,
	REQUEST_LAUNCH_TASKS			= 34,
	REQUEST_CREATE_JOB_STEP			= 35,
	RESPONSE_CREATE_JOB_STEP		= 36,
	REQUEST_RUN_JOB_STEP			= 37,
	RESPONSE_RUN_JOB_STEP			= 38,
	REQUEST_JOB_ATTACH			= 39,
	RESPONSE_JOB_ATTACH			= 40,
	RESPONSE_LAUNCH_TASKS			= 41,
	REQUEST_GET_KEY				= 42,
	RESPONSE_GET_KEY			= 43,
	REQUEST_GET_JOB_STEP_INFO		= 44,
	RESPONSE_GET_JOB_STEP_INFO		= 45,
	REQUEST_JOB_RESOURCE			= 46,
	RESPONSE_JOB_RESOURCE			= 47
	/*
	REQUEST_RUN_JOB_STEP			= 48,
	RESPONSE_RUN_JOB_STEP			= 49
	*/
} SLURM_MSG_TYPE_T ;
typedef uint16_t slurm_msg_type_t ;
	
#define REQUEST_NODE_REGISRATION_STATUS 	1
#define MESSAGE_NODE_REGISRATION_STATUS 	2	
#define REQUEST_RESOURCE_ALLOCATION 		3
#define RESPONSE_RESOURCE_ALLOCATION		4
#define REQUEST_CANCEL_JOB			5
#define REQUEST_RECONFIGURE			6
#define RESPONSE_CANCEL_JOB			7
#define REQUEST_JOB_INFO			8
#define RESPONSE_JOB_INFO			9
#define REQUEST_JOB_STEP_INFO			10
#define RESPONSE_JOB_STEP_INFO			11
#define REQUEST_NODE_INFO			12
#define RESPONSE_NODE_INFO			13
#define REQUEST_PARTITION_INFO			14
#define RESPONSE_PATITION_INFO			15
#define REQUEST_ACCTING_INFO			16
#define RESPONSE_ACCOUNTING_INFO		17
#define REQUEST_BUILD_INFO			18
#define RESPONSE_BUILD_INFO			19
#define MESSAGE_UPLOAD_ACCOUNTING_INFO		20
#define RESPONSE_RECONFIGURE			21 
#define REQUEST_SUBMIT_BATCH_JOB		22
#define RESPONSE_SUBMIT_BATCH_JOB		23
#define REQUEST_CANCEL_JOB_STEP			24  
#define RESPONSE_CANCEL_JOB_STEP		25
#define REQUEST_SIGNAL_JOB			26
#define RESPONSE_SIGNAL_JOB			27
#define REQUEST_SIGNAL_JOB_STEP			28
#define RESPONSE_SIGNAL_JOB_STEP		29
#define REQUEST_BATCH_JOB_LAUNCH		30
#define RESPONSE_BATCH_JOB_LAUNCH		31
#define MESSAGE_TASK_EXIT			32
#define MESSAGE_REVOKE_JOB_CREDENTIAL		33
#define REQUEST_LAUNCH_TASKS			34
#define REQUEST_CREATE_JOB_STEP			35
#define RESPONSE_CREATE_JOB_STEP		36
#define REQUEST_RUN_JOB_STEP			37
#define RESPONSE_RUN_JOB_STEP			38
#define REQUEST_JOB_ATTACH			39
#define RESPONSE_JOB_ATTACH			40
#define RESPONSE_LAUNCH_TASKS			41
#define REQUEST_GET_KEY				42
#define RESPONSE_GET_KEY			43
#define REQUEST_GET_JOB_STEP_INFO		44
#define RESPONSE_GET_JOB_STEP_INFO		45
#define REQUEST_JOB_RESOURCE			46
#define RESPONSE_JOB_RESOURCE			47

typedef struct slurm_protocol_header
{
	uint16_t version ;
	uint16_t flags ;
	uint16_t msg_type ;
	uint32_t body_length ;
} header_t ;

typedef struct slurm_node_registration_status_msg
{
	uint32_t timestamp ;
	uint32_t memory_size ;
 	uint32_t temporary_disk_space ;
} node_registration_status_msg_t ;

typedef struct job_desc {    /* Job descriptor for submit, allocate, and update requests */
	uint16_t contiguous;    /* 1 if job requires contiguous nodes, 0 otherwise,
				 * default=0 */
	char *features;         /* comma separated list of required features, default NONE */
	char *groups;           /* comma separated list of groups the user can access,
				 * default set output of "/usr/bin/groups" by API,
				 * can only be set if user is root */
	uint32_t job_id;        /* job ID, default set by SLURM */
	char *name;             /* name of the job, default "" */
	void *partition_key;    /* root key to submit job, format TBD, default NONE */
	uint32_t min_procs;     /* minimum processors required per node, default=0 */
	uint32_t min_memory;    /* minimum real memory required per node, default=0 */
	uint32_t min_tmp_disk;  /* minimum temporary disk required per node, default=0 */
	char *partition;        /* name of requested partition, default in SLURM config */
	uint32_t priority;      /* relative priority of the job, default set by SLURM,
				 * can only be explicitly set if user is root, maximum
				 *                                  * value is #fffffffe */
	char *req_nodes;        /* comma separated list of required nodes, default NONE */
	char *job_script;       /* pathname of required script, default NONE */
	uint16_t shared;        /* 1 if job can share nodes with other jobs, 0 otherwise,
				 * default in SLURM configuration */
	uint32_t time_limit;    /* maximum run time in minutes, default is partition
				 * limit as defined in SLURM configuration, maximum
				 *                                  * value is #fffffffe */
	uint32_t num_procs;     /* number of processors required by job, default=0 */
	uint32_t num_nodes;     /* number of nodes required by job, default=0 */
	uint32_t user_id;       /* set only if different from current UID, default set
				 * to UID by API, can only be set if user is root */
} job_desc_t ;

typedef struct slurm_msg
{
	slurm_msg_type_t msg_type ;
	slurm_addr address ;
	void * msg ;
} slurm_msg_t ;
#endif
