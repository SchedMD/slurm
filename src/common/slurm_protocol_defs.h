/****************************************************************************\
 * slurm_protocol_defs.h - definitions used for RPCs
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
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

/* INFINITE is used to identify unlimited configurations,  */
/* eg. the maximum count of nodes any job may use in some partition */
#define	INFINITE (0xffffffff)
#define NO_VAL	 (0x7f7f7f7f)

/* last entry must be STATE_END, keep in sync with node_state_string    		*/
/* if a node ceases to respond, its last state is ORed with NODE_STATE_NO_RESPOND	*/
enum node_states {
	NODE_STATE_DOWN,	/* node is not responding */
	NODE_STATE_UNKNOWN,	/* node's initial state, unknown */
	NODE_STATE_IDLE,	/* node idle and available for use */
	NODE_STATE_ALLOCATED,	/* node has been allocated, job not currently running */
	NODE_STATE_STAGE_IN,	/* node has been allocated, job is starting execution */
	NODE_STATE_RUNNING,	/* node has been allocated, job currently running */
	NODE_STATE_STAGE_OUT,	/* node has been allocated, job is terminating */
	NODE_STATE_DRAINED,	/* node idle and not to be allocated future work */
	NODE_STATE_DRAINING,	/* node in use, but not to be allocated future work */
	NODE_STATE_END		/* last entry in table */
};
#define NODE_STATE_NO_RESPOND (0x8000)

/* last entry must be JOB_END, keep in sync with job_state_string    	*/
enum job_states {
	JOB_PENDING,		/* queued waiting for initiation */
	JOB_STAGE_IN,		/* allocated resources, not yet running */
	JOB_RUNNING,		/* allocated resources and executing */
	JOB_STAGE_OUT,		/* completed execution, nodes not yet released */
	JOB_COMPLETE,		/* completed execution successfully, nodes released */
	JOB_FAILED,		/* completed execution unsuccessfully, nodes released */
	JOB_TIMEOUT,		/* terminated on reaching time limit, nodes released */
	JOB_END			/* last entry in table */
};

/* keep in sync with job_dist_string    	*/
enum task_dist {
	DIST_BLOCK,		/* fill each node in turn */
	DIST_CYCLE		/* one task each node, round-robin through nodes */
};

enum part_shared {
	SHARED_NO,		/* Nodes never shared in partition */
	SHARED_YES,		/* Nodes possible to share in partition */
	SHARED_FORCE		/* Nodes always shares in partition */
};

#include <src/common/macros.h>
#include <src/common/slurm_protocol_common.h>

/* SLURM Message types */
typedef enum { test1, test2
} SLURM_MSG_TYPE_T ;
	
#define REQUEST_NODE_REGISTRATION_STATUS 	1001
#define MESSAGE_NODE_REGISTRATION_STATUS 	1002	
#define REQUEST_RECONFIGURE			1011
#define RESPONSE_RECONFIGURE			1012 

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
#define RESPONSE_GET_JOB_STEP_INFO		3072

#define REQUEST_UPDATE_JOB			3081
#define REQUEST_UPDATE_NODE			3082
#define REQUEST_UPDATE_PARTITION		3083

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
#define REQUEST_IMMEDIATE_RESOURCE_ALLOCATION	4071
#define RESPONSE_IMMEDIATE_RESOURCE_ALLOCATION	4072
#define REQUEST_JOB_WILL_RUN	 		4081
#define RESPONSE_JOB_WILL_RUN			4082
#define MESSAGE_REVOKE_JOB_CREDENTIAL		4901

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
#define REQUEST_KILL_TASKS			6004

/*DPCS get key to sign submissions*/
#define REQUEST_GET_KEY				8001	
#define RESPONSE_GET_KEY			8002

#define RESPONSE_SLURM_RC			9000	
#define MESSAGE_UPLOAD_ACCOUNTING_INFO		9010	

/*core api configuration struct */
typedef struct slurm_protocol_config 
{
	slurm_addr primary_controller;
	slurm_addr secondary_controller;
} slurm_protocol_config_t ;

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

typedef struct job_step_id_msg {
	uint32_t job_id;
	uint32_t job_step_id ;
} job_step_id_msg_t ;

typedef struct slurm_update_node_msg
{
	char * node_names ;
	uint16_t node_state ;
}	update_node_msg_t ;

typedef struct slurm_node_registration_status_msg
{
	uint32_t timestamp ;
	char* node_name;
	uint32_t cpus;
	uint32_t real_memory_size ;
 	uint32_t temporary_disk_space ;
} slurm_node_registration_status_msg_t ;

typedef struct launch_tasks_msg
{
	uint32_t job_id ;
	uint32_t job_step_id ;
	uint32_t uid ;
	uint32_t gid ;
	char * credentials ;
	uint32_t tasks_to_launch ;
	uint16_t envc ;
	char ** env ;
	char * cwd ;
	char * cmd_line ;
	slurm_addr * streams;
	uint32_t * global_task_ids;
} launch_tasks_msg_t ;

typedef struct kill_tasks_msg
{
	uint32_t job_id ;
	uint32_t job_step_id ;
} kill_tasks_msg_t ;


typedef struct resource_allocation_response_msg
{
	uint32_t job_id;
	char* node_list;
	int16_t num_cpu_groups;
	int32_t* cpus_per_node;
	int32_t* cpu_count_reps;
} resource_allocation_response_msg_t ;

typedef struct submit_response_msg
{
	uint32_t job_id;
} submit_response_msg_t ;

typedef struct job_desc_msg {   /* Job descriptor for submit, allocate, and update requests */
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
	uint32_t dist;    	/* task distribution pattern, block/cycle */
	uint32_t procs_per_task;   
	uint32_t user_id;       /* set only if different from current UID, default set
				 * to UID by API, can only be set if user is root */
} job_desc_msg_t ;

struct slurm_ctl_conf {
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
} ;
typedef struct slurm_ctl_conf slurm_ctl_conf_t ;
typedef struct slurm_ctl_conf slurm_ctl_conf_info_msg_t ;

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

struct part_table {
        char *name;             /* name of the partition */
        uint32_t max_time;      /* minutes or INFINITE */
        uint32_t max_nodes;     /* per job or INFINITE */
        uint32_t total_nodes;   /* total number of nodes in the partition */
        uint32_t total_cpus;    /* total number of cpus in the partition */
        uint16_t default_part;  /* 1 if this is default partition */
        uint16_t key;           /* 1 if slurm distributed key is required for use  */
        uint16_t shared;        /* 1 if job can share nodes, 2 if job must share nodes */
        uint16_t state_up;      /* 1 if state is up, 0 if down */
        char *nodes;            /* comma delimited list names of nodes in partition */
        int *node_inx;          /* list index pairs into node_table:
                                   start_range_1, end_range_1, start_range_2, .., -1  */
        char *allow_groups;     /* comma delimited list of groups, null indicates all */
} ;
typedef struct part_table partition_desc_t ;
typedef struct part_table partition_desc_msg_t ;
typedef struct part_table partition_table_t ;
typedef struct part_table partition_table_msg_t ;
typedef struct part_table update_part_msg_t ;

typedef struct job_info_msg {
	uint32_t last_update;
	uint32_t record_count;
	job_table_t * job_array;
} job_info_msg_t ;

typedef struct partition_info_msg {
	uint32_t last_update;
	uint32_t record_count;
	partition_table_t * partition_array;
} partition_info_msg_t ;

struct node_table {
	char *name;		/* node name */
	uint16_t node_state;	/* see node_state_string below for translation */
	uint32_t cpus;		/* configured count of cpus running on the node */
	uint32_t real_memory;	/* configured megabytes of real memory on the node */
	uint32_t tmp_disk;	/* configured megabytes of total disk in TMP_FS */
	uint32_t weight;	/* arbitrary priority of node for scheduling work on */
	char *features;		/* arbitrary list of features associated with a node */
	char *partition;	/* name of partition node configured to */
};

typedef struct node_table node_table_t ;
typedef struct node_table node_table_msg_t ;

typedef struct node_info_msg {
	uint32_t last_update;
	uint32_t record_count;
	node_table_t * node_array;
} node_info_msg_t ;

/* the following typedefs follow kevin's communication message naming comvention */

/* free message functions */
void inline slurm_free_last_update_msg ( last_update_msg_t * msg ) ;
void inline slurm_free_return_code_msg ( return_code_msg_t * msg ) ;
void inline slurm_free_job_id_msg ( job_id_msg_t * msg ) ;

void inline slurm_free_ctl_conf ( slurm_ctl_conf_info_msg_t * build_ptr ) ;

void inline slurm_free_job_desc_msg ( job_desc_msg_t * msg ) ;
void inline slurm_free_resource_allocation_response_msg( resource_allocation_response_msg_t * msg );

void inline slurm_free_node_registration_status_msg ( slurm_node_registration_status_msg_t * msg ) ;

void inline slurm_free_job_info ( job_info_msg_t * msg ) ;
void inline slurm_free_job_table ( job_table_t * job ) ;
void inline slurm_free_job_table_msg ( job_table_t * job ) ;

void inline slurm_free_partition_info ( partition_info_msg_t * msg ) ;
void inline slurm_free_partition_table ( partition_table_t * part ) ;
void inline slurm_free_partition_table_msg ( partition_table_t * part ) ;

void inline slurm_free_node_info ( node_info_msg_t * msg ) ;
void inline slurm_free_node_table ( node_table_t * node ) ;
void inline slurm_free_node_table_msg ( node_table_t * node ) ;
void inline slurm_free_update_node_msg ( update_node_msg_t * msg ) ;
void inline slurm_free_launch_tasks_msg ( launch_tasks_msg_t * msg ) ;
void inline slurm_free_kill_tasks_msg ( kill_tasks_msg_t * msg ) ;

extern char *job_dist_string(uint16_t inx);
extern char *job_state_string(uint16_t inx);
extern char *node_state_string(uint16_t inx);

#define SLURM_JOB_DESC_DEFAULT_CONTIGUOUS	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_DIST		NO_VAL	
#define SLURM_JOB_DESC_DEFAULT_FEATURES		NULL
#define SLURM_JOB_DESC_DEFAULT_GROUPS		NULL
#define SLURM_JOB_DESC_DEFAULT_JOB_ID		NO_VAL	
#define SLURM_JOB_DESC_DEFAULT_JOB_NAME 	NULL
#define SLURM_JOB_DESC_DEFAULT_PARITION_KEY	NULL
#define SLURM_JOB_DESC_DEFAULT_MIN_PROCS	NO_VAL	
#define SLURM_JOB_DESC_DEFAULT_MIN_MEMORY	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_MIN_TMP_DISK	NO_VAL	
#define SLURM_JOB_DESC_DEFAULT_PARTITION	NULL
#define SLURM_JOB_DESC_DEFAULT_PRIORITY		NO_VAL
#define SLURM_JOB_DESC_DEFAULT_PROCS_PER_TASK 	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_REQ_NODES	NULL
#define SLURM_JOB_DESC_DEFAULT_JOB_SCRIPT	NULL
#define SLURM_JOB_DESC_DEFAULT_SHARED	 	NO_VAL	
#define SLURM_JOB_DESC_DEFAULT_TIME_LIMIT	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_NUM_PROCS	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_NUM_NODES	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_USER_ID		NO_VAL
void slurm_init_job_desc_msg ( job_desc_msg_t * job_desc_msg ) ;

#endif
