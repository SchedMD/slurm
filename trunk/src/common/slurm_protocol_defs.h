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
#define RESPONSE_JOB_INFO			15
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
#define REQUEST_RUN_JOB_STEP			48
#define RESPONSE_RUN_JOB_STEP			49

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

typedef struct slurm_msg
{
	uint16_t msg_type ;
	void * msg ;
} slurm_msg_t ;
#endif
