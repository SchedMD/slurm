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

/* SLURM Message types */
typedef enum { test1, test2
} SLURM_MSG_TYPE_T ;
	
#define REQUEST_NODE_REGISRATION_STATUS 	1001
#define MESSAGE_NODE_REGISRATION_STATUS 	1002	
#define REQUEST_RECONFIGURE			1011
#define RESPONSE_RECONFIGURE			1012 

#define REQUEST_RESOURCE_ALLOCATION 		4001
#define RESPONSE_RESOURCE_ALLOCATION		4002
#define REQUEST_SUBMIT_BATCH_JOB		4011
#define RESPONSE_SUBMIT_BATCH_JOB		4012
#define REQUEST_BATCH_JOB_LAUNCH		4021
#define RESPONSE_BATCH_JOB_LAUNCH		4022
#define REQUEST_SIGNAL_JOB			4031
#define RESPONSE_SIGNAL_JOB			4032
#define REQUEST_CANCEL_JOB			4041
#define RESPONSE_CANCEL_JOB			4042	
#define REQUEST_JOB_RESOURCE			4051
#define RESPONSE_JOB_RESOURCE			4052
#define REQUEST_JOB_ATTACH			4061
#define RESPONSE_JOB_ATTACH			4062
#define MESSAGE_REVOKE_JOB_CREDENTIAL		4901

#define REQUEST_BUILD_INFO			3011
#define RESPONSE_BUILD_INFO			3012
#define REQUEST_JOB_INFO			3021
#define RESPONSE_JOB_INFO			3022
#define REQUEST_JOB_STEP_INFO			3031
#define RESPONSE_JOB_STEP_INFO			3032
#define REQUEST_NODE_INFO			3041
#define RESPONSE_NODE_INFO			3042
#define REQUEST_PARTITION_INFO			3051
#define RESPONSE_PARTITION_INFO			3052
#define REQUEST_ACCTING_INFO			3061
#define RESPONSE_ACCOUNTING_INFO		3062
#define REQUEST_GET_JOB_STEP_INFO		3071
#define RESPONSE_GET_JOB_STEP_INFO		4072

#define REQUEST_CREATE_JOB_STEP			5001
#define RESPONSE_CREATE_JOB_STEP		5002
#define REQUEST_RUN_JOB_STEP			5011
#define RESPONSE_RUN_JOB_STEP			5012
#define REQUEST_SIGNAL_JOB_STEP			5021
#define RESPONSE_SIGNAL_JOB_STEP		5022
#define REQUEST_CANCEL_JOB_STEP			5031
#define RESPONSE_CANCEL_JOB_STEP		5032

#define REQUEST_LAUNCH_TASKS			6001	
#define RESPONSE_LAUNCH_TASKS			6002
#define MESSAGE_TASK_EXIT		 	6003	

/*DPCS get key to sign submissions*/
#define REQUEST_GET_KEY				8001	
#define RESPONSE_GET_KEY			8002

#define RESPONSE_SLURM_RC			9000	
#define MESSAGE_UPLOAD_ACCOUNTING_INFO		9010	

/*core api protocol message structures */
typedef struct slurm_protocol_header
{
	uint16_t version ;
	uint16_t flags ;
	uint16_t msg_type ;
	uint32_t body_length ;
} header_t ;

typedef struct slurm_msg
{
	slurm_msg_type_t msg_type ;
	slurm_addr address ;
	slurm_fd conn_fd ;
	void * data ;
	uint32_t data_size ;
} slurm_msg_t ;

/* really short messages */
typedef struct last_update_msg {
	uint32_t last_update;
} last_update_msg_t ;

typedef struct return_code_msg {
	int32_t return_code;
} return_code_msg_t ;

typedef struct job_id_msg {
	uint32_t job_id;
} job_id_msg_t ;


typedef struct slurm_node_registration_status_msg
{
	uint32_t timestamp ;
	char* node_name;
	uint32_t cpus;
	uint32_t real_memory_size ;
 	uint32_t temporary_disk_space ;
} node_registration_status_msg_t ;

typedef struct job_desc_msg {    /* Job descriptor for submit, allocate, and update requests */
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
} job_desc_msg_t ;

struct build_table {
	uint32_t last_update;   /* last update time of the build parameters*/
	uint16_t backup_interval;/* slurmctld save state interval, seconds */
	char *backup_location;	/* pathname of state save directory */
	char *backup_machine;	/* name of slurmctld secondary server */
	char *control_daemon;	/* pathname of slurmctld */
	char *control_machine;	/* name of slurmctld primary server */
	uint16_t controller_timeout; /* seconds for secondary slurmctld to take over */
	char *epilog;		/* pathname of job epilog */
	uint16_t fast_schedule;	/* 1 to *not* check configurations by node 
				 * (only check configuration file, faster) */
	uint16_t hash_base;	/* base used for hashing node table */
	uint16_t heartbeat_interval; /* interval between node heartbeats, seconds */
	char *init_program;	/* pathname of program to complete with exit 0 
 				 * before slurmctld or slurmd start on that node */
	uint16_t kill_wait;	/* seconds from SIGXCPU to SIGKILL on job termination */
	char *prioritize;	/* pathname of program to set initial job priority */
	char *prolog;		/* pathname of job prolog */
	char *server_daemon;	/* pathame of slurmd */
	uint16_t server_timeout;/* how long slurmctld waits for setting node DOWN */
	char *slurm_conf;	/* pathname of slurm config file */
	char *tmp_fs;		/* pathname of temporary file system */
};
typedef struct build_table build_info_msg_t ;

struct job_table {
	uint32_t job_id;	/* job ID */
	char *name;		/* name of the job */
	uint32_t user_id;	/* user the job runs as */
	uint16_t job_state;	/* state of the job, see enum job_states */
	uint32_t time_limit;	/* maximum run time in minutes or INFINITE */
	time_t start_time;	/* time execution begins, actual or expected*/
	time_t end_time;	/* time of termination, actual or expected */
	uint32_t priority;	/* relative priority of the job */
	char *nodes;		/* comma delimited list of nodes allocated to job */
	int *node_inx;		/* list index pairs into node_table for *nodes:
				   start_range_1, end_range_1, start_range_2, .., -1  */
	char *partition;	/* name of assigned partition */
	uint32_t num_procs;	/* number of processors required by job */
	uint32_t num_nodes;	/* number of nodes required by job */
	uint16_t shared;	/* 1 if job can share nodes with other jobs */
	uint16_t contiguous;	/* 1 if job requires contiguous nodes */
	uint32_t min_procs;	/* minimum processors required per node */
	uint32_t min_memory;	/* minimum real memory required per node */
	uint32_t min_tmp_disk;	/* minimum temporary disk required per node */
	char *req_nodes;	/* comma separated list of required nodes */
	int *req_node_inx;	/* list index pairs into node_table for *req_nodes:
				   start_range_1, end_range_1, start_range_2, .., -1  */
	char *features;		/* comma separated list of required features */
	char *job_script;	/* pathname of required script */
};
typedef struct job_table job_table_t ;
typedef struct job_table job_table_msg_t ;

typedef struct job_info_msg {
	uint32_t last_update;
	uint32_t record_count;
	job_table_t * job_array;
} job_info_msg_t ;

/* the following typedefs follow kevin's communication message naming comvention */

/* free message functions */
void inline slurm_free_last_update_msg ( last_update_msg_t * msg ) ;
void inline slurm_free_return_code_msg ( return_code_msg_t * msg ) ;
void inline slurm_free_job_id_msg ( job_id_msg_t * msg ) ;

void inline slurm_free_build_info ( build_info_msg_t * build_ptr ) ;

void inline slurm_free_job_info ( job_info_msg_t * msg ) ;
void inline slurm_free_job_table ( job_table_t * job ) ;

void inline slurm_free_job_desc_msg ( job_desc_msg_t * msg ) ;
#endif
