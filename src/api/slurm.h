/*****************************************************************************\
 *  slurm.h - Definitions for all of the SLURM RPCs
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>, 
 *	Joey Ekstrom <ekstrom1@llnl.gov> et. al.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _SLURM_H
#define _SLURM_H

/* FIXME: Need need better way to get configuration info */
#include "config.h"		/* used for possible HAVE_LIBELAN3 */

#include <stdint.h>		/* for uint16_t, uint32_t definitions */
#include <stdio.h>		/* for FILE definitions */
#include <time.h>		/* for time_t definitions */

/* FIXME: Need to park these headers in generally available location */
#include "src/common/slurm_errno.h"
#include "src/common/hostlist.h"

/* Define slurm_addr below to avoid including extraneous slurm headers */
#ifdef	HAVE_SYS_SOCKET_H
#  ifndef __slurm_addr_defined
#    include <netinet/in.h>
#    define  __slurm_addr_defined
     typedef struct sockaddr_in slurm_addr ;
#  endif
#endif

/* Define qsw_jobinfo_t below to avoid including extraneous slurm headers */
#ifdef	HAVE_LIBELAN3
#  ifndef __qsw_jobinfo_t_defined
#    define  __qsw_jobinfo_t_defined
     typedef struct qsw_jobinfo *qsw_jobinfo_t;	/* opaque data type */
#  endif
#endif

/* FIXME: For the slurm library, we need to link
 * api (everything)
 * common/hostlist.c
 * common/slurm_errno.c
 * common/slurm_protocol_*.c
 */

/*****************************************************************************\
 *	DEFINITIONS FOR INPUT VALUES
\*****************************************************************************/

/* INFINITE is used to identify unlimited configurations,  */
/* eg. the maximum count of nodes any job may use in some partition */
#define	INFINITE (0xffffffff)
#define NO_VAL	 (0xfffffffe)
#define MAX_TASKS_PER_NODE 64

/* last entry must be JOB_END
 * NOTE: keep in sync with job_state_string and job_state_string_compact */
enum job_states {
	JOB_PENDING,		/* queued waiting for initiation */
	JOB_RUNNING,		/* allocated resources and executing */
	JOB_COMPLETE,		/* completed execution successfully */
	JOB_FAILED,		/* completed execution unsuccessfully */
	JOB_TIMEOUT,		/* terminated on reaching time limit */
	JOB_NODE_FAIL,		/* terminated on node failure */
	JOB_END			/* last entry in table */
};

/* Possible task distributions across the nodes */
enum task_dist_states {
	SLURM_DIST_CYCLIC,	/* distribute tasks one per node, round robin */
	SLURM_DIST_BLOCK	/* distribute tasks filling node by node */
};

/* last entry must be STATE_END, keep in sync with node_state_string    */
/* 	if a node ceases to respond, its last state is ORed with 	*/
/* 	NODE_STATE_NO_RESPOND	*/
enum node_states {
	NODE_STATE_DOWN,	/* node is not responding */
	NODE_STATE_UNKNOWN,	/* node's initial state, unknown */
	NODE_STATE_IDLE,	/* node idle and available for use */
	NODE_STATE_ALLOCATED,	/* node has been allocated to a job */
	NODE_STATE_DRAINED,	/* node idle and not to be allocated work */
	NODE_STATE_DRAINING,	/* node in use, but not to be allocated work */
	NODE_STATE_END		/* last entry in table */
};
#define NODE_STATE_NO_RESPOND (0x8000)

/* used to define the size of the credential.signature size
 * used to define the key size of the io_stream_header_t
 */
#define SLURM_SSL_SIGNATURE_LENGTH 128

/*****************************************************************************\
 *	PROTOCOL DATA STRUCTURE DEFINITIONS
\*****************************************************************************/

typedef struct {
	uint32_t job_id;	/* job's id */
	uid_t user_id;		/* user who job is running as */
	char *node_list;	/* list of allocated nodes */
	time_t expiration_time;	/* expiration of credential */
	char signature[SLURM_SSL_SIGNATURE_LENGTH];	
} slurm_job_credential_t;

typedef struct job_descriptor {	/* For submit, allocate, and update requests */
	uint16_t contiguous;	/* 1 if job requires contiguous nodes,
				 * 0 otherwise,default=0 */
	uint16_t kill_on_node_fail; /* 1 if node failure to kill job, 
				 * 0 otherwise,default=1 */
	char **environment;	/* environment variables to set for job, 
				 *  name=value pairs, one per line */
	uint16_t env_size;	/* element count in environment */
	char *features;		/* comma separated list of required features, 
				 * default NONE */
	uint32_t job_id;	/* job ID, default set by SLURM */
	char *name;		/* name of the job, default "" */
	uint32_t min_procs;	/* minimum processors per node, default=0 */
	uint32_t min_memory;	/* minimum real memory per node, default=0 */
	uint32_t min_tmp_disk;	/* minimum temporary disk per node, 
				 * default=0 */
	char *partition;	/* name of requested partition, 
				 * default in SLURM config */
	uint32_t priority;	/* relative priority of the job, explicitly 
				 * set only for user root */
	char *req_nodes;	/* comma separated list of required nodes
				 * default NONE */
	uint16_t shared;	/* 1 if job can share nodes with other jobs,
				 * 0 otherwise */
	uint32_t time_limit;	/* maximum run time in minutes, default is
				 * partition limit */
	uint32_t num_procs;	/* total count of processors required, 
				 * default=0 */
	uint32_t num_nodes;	/* number of nodes required by job, 
				 * default=0 */
	uint32_t num_tasks;	/* number of tasks required by job, 
				 * default=0, used only by 
				 * slurm_allocate_resources_and_run() */
	char *script;		/* the actual job script, default NONE */
	char *err;		/* pathname of stderr */
	char *in;		/* pathname of stdin */
	char *out;		/* pathname of stdout */
	uint32_t user_id;	/* set only if different from current UID, 
				 * can only be explicitly set by user root */
	char *work_dir;		/* pathname of working directory */
} job_desc_msg_t;

typedef struct job_info {
	uint32_t job_id;	/* job ID */
	char *name;		/* name of the job */
	uint32_t user_id;	/* user the job runs as */
	uint16_t job_state;	/* state of the job, see enum job_states */
	uint32_t time_limit;	/* maximum run time in minutes or INFINITE */
	time_t start_time;	/* time execution begins, actual or expected */
	time_t end_time;	/* time of termination, actual or expected */
	uint32_t priority;	/* relative priority of the job */
	char *nodes;		/* list of nodes allocated to job */
	int *node_inx;		/* list index pairs into node_table for *nodes:
				 * start_range_1, end_range_1, 
				 * start_range_2, .., -1  */
	char *partition;	/* name of assigned partition */
	uint32_t num_procs;	/* number of processors required by job */
	uint32_t num_nodes;	/* number of nodes required by job */
	uint16_t shared;	/* 1 if job can share nodes with other jobs */
	uint16_t contiguous;	/* 1 if job requires contiguous nodes */
	uint32_t min_procs;	/* minimum processors required per node */
	uint32_t min_memory;	/* minimum real memory required per node */
	uint32_t min_tmp_disk;	/* minimum temporary disk required per node */
	char *req_nodes;	/* comma separated list of required nodes */
	int *req_node_inx;	/* list index pairs into node_table: 
				 * start_range_1, end_range_1, 
				 * start_range_2, .., -1  */
	char *features;		/* comma separated list of required features */
} job_info_t;

typedef struct job_info_msg {
	time_t last_update;	/* time of latest info */
	uint32_t record_count;	/* number of records */
	job_info_t *job_array;	/* the job records */
} job_info_msg_t;

typedef struct job_step_specs {
	uint32_t job_id;	/* job ID */
	uint32_t user_id;	/* user the job runs as */
	uint32_t node_count;	/* count of required nodes */
	uint32_t cpu_count;	/* count of required processors */
	uint32_t num_tasks;	/* number of tasks required */
	uint16_t relative;	/* first node to use of job's allocation */
	uint16_t task_dist;	/* see enum task_dist_state */
	char *node_list;	/* list of required nodes */
} job_step_create_request_msg_t;

typedef struct job_step_create_response_msg {
	uint32_t job_step_id;	/* assigned job step id */
	char *node_list;	/* list of allocated nodes */
	slurm_job_credential_t *credentials;
#ifdef	HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;	/* Elan3 switch context, opaque data structure */
#endif
} job_step_create_response_msg_t;

typedef struct {
	uint32_t job_id;	/* job ID */
	uint16_t step_id;	/* step ID */
	uint32_t user_id;	/* user the job runs as */
	uint32_t num_tasks;	/* number of tasks */
	time_t start_time;	/* step start time */
	char *partition;	/* name of assigned partition */
	char *nodes;		/* list of nodes allocated to job_step */
} job_step_info_t;

typedef struct job_step_info_response_msg {
	time_t last_update;		/* time of latest info */
	uint32_t job_step_count;	/* number of records */
	job_step_info_t *job_steps;	/* the job step records */
} job_step_info_response_msg_t;

typedef struct node_info {
	char *name;		/* node name */
	uint16_t node_state;	/* see enum node_states */
	uint32_t cpus;		/* configured count of cpus running on 
				 * the node */
	uint32_t real_memory;	/* configured MB of real memory on the node */
	uint32_t tmp_disk;	/* configured MB of total disk in TMP_FS */
	uint32_t weight;	/* arbitrary priority of node for scheduling */
	char *features;		/* arbitrary list of features for node */
	char *partition;	/* name of partition node configured to */
} node_info_t;

typedef struct node_info_msg {
	time_t last_update;		/* time of latest info */
	uint32_t record_count;		/* number of records */
	node_info_t *node_array;	/* the node records */
} node_info_msg_t;

typedef struct old_job_alloc_msg {
	uint32_t job_id;	/* job ID */
	uint32_t uid;		/* user the job runs as */
} old_job_alloc_msg_t;

typedef struct partition_info {
	char *name;		/* name of the partition */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint16_t default_part;	/* 1 if this is default partition */
	uint16_t root_only;	/* 1 if allocate must come for user root */
	uint16_t shared;	/* 1 if job can share nodes, 
				 * 2 if job must share nodes */
	uint16_t state_up;	/* 1 if state is up, 0 if down */
	char *nodes;		/* list names of nodes in partition */
	int *node_inx;		/* list index pairs into node_table:
				 * start_range_1, end_range_1, 
				 * start_range_2, .., -1  */
	char *allow_groups;	/* comma delimited list of groups, 
				 * null indicates all */
} partition_info_t;

typedef struct resource_allocation_response_msg {
	uint32_t job_id;	/* assigned job id */
	char *node_list;	/* assigned list of nodes */
	int16_t num_cpu_groups;	/* elements in below cpu arrays */
	int32_t *cpus_per_node;	/* cpus per node */
	int32_t *cpu_count_reps;/* how many nodes have same cpu count */
	uint16_t node_cnt;	/* count of nodes */
	slurm_addr *node_addr;	/* network addresses */
} resource_allocation_response_msg_t;

typedef struct resource_allocation_and_run_response_msg {
	uint32_t job_id;	/* assigned job id */
	char *node_list;	/* assigned list of nodes */
	int16_t num_cpu_groups;	/* elements in below cpu arrays */
	int32_t *cpus_per_node;	/* cpus per node */
	int32_t *cpu_count_reps;/* how many nodes have same cpu count */
	uint32_t job_step_id;	/* assigned step id */
	uint16_t node_cnt;	/* count of nodes */
	slurm_addr *node_addr;	/* network addresses */
	slurm_job_credential_t *credentials;
#ifdef HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;	/* Elan3 switch context, opaque data structure */
#endif
} resource_allocation_and_run_response_msg_t;

typedef struct partition_info_msg {
	time_t last_update;	/* time of latest info */
	uint32_t record_count;	/* number of records */
	partition_info_t *partition_array; /* the partition records */
} partition_info_msg_t;

typedef struct slurm_ctl_conf {
	time_t last_update;	/* last update time of the build parameters */
	char *backup_addr;	/* comm path of slurmctld secondary server */
	char *backup_controller;/* name of slurmctld secondary server */
	char *control_addr;	/* comm path of slurmctld primary server */
	char *control_machine;	/* name of slurmctld primary server */
	char *epilog;		/* pathname of job epilog */
	uint32_t first_job_id;	/* first slurm generated job_id to assign */
	uint16_t fast_schedule;	/* 1 to *not* check configurations by node 
				 * (only check configuration file, faster) */
	uint16_t hash_base;	/* base used for hashing node table */
	uint16_t heartbeat_interval; /* interval between heartbeats, seconds */
	uint16_t inactive_limit;/* seconds of inactivity before a
				 * inactive resource allocation is released */
	uint16_t kill_wait;	/* seconds between SIGXCPU to SIGKILL 
				 * on job termination */
	char *prioritize;	/* pathname of program to set initial job 
				 * priority */
	char *prolog;		/* pathname of job prolog */
	uint16_t ret2service;	/* 1 return DOWN node to service at 
				 * registration */ 
	uint16_t slurm_user_id;	/* uid of slurm_user_name */
	char *slurm_user_name;	/* user that slurmctld runs as */
	char *slurmctld_logfile;/* where slurmctld error log gets written */
	char *slurmctld_pidfile;/* where to put slurmctld pidfile         */
	uint32_t slurmctld_port;/* default communications port to slurmctld */
	uint16_t slurmctld_timeout;/* seconds that backup controller waits 
				 * on non-responding primarly controller */
	char *slurmd_logfile;	/* where slurmd error log gets written */
	uint32_t slurmd_port;	/* default communications port to slurmd */
	char *slurmd_spooldir;	/* where slurmd put temporary state info */
	char *slurmd_pidfile;   /* where to put slurmd pidfile           */
	uint16_t slurmd_timeout;/* how long slurmctld waits for slurmd before 
				 * considering node DOWN */
	char *slurm_conf;	/* pathname of slurm config file */
	char *state_save_location;/* pathname of slurmctld state save
				 * directory */
	char *tmp_fs;		/* pathname of temporary file system */
	char *job_credential_private_key;	/* path to private key */
	char *job_credential_public_certificate;/* path to public certificate*/
} slurm_ctl_conf_t;

typedef struct submit_response_msg {
	uint32_t job_id;	/* job ID */
} submit_response_msg_t;

typedef struct slurm_update_node_msg {
	char *node_names;	/* comma separated list of required nodes */
	uint16_t node_state;	/* see enum node_states */
} update_node_msg_t;

typedef struct partition_info update_part_msg_t;

/*****************************************************************************\
 *	RESOURCE ALLOCATION FUNCTIONS
\*****************************************************************************/

/*
 * slurm_init_job_desc_msg - initialize job descriptor with 
 *	default values 
 * OUT job_desc_msg - user defined job descriptor
 */
extern void slurm_init_job_desc_msg (job_desc_msg_t * job_desc_msg);

/*
 * slurm_allocate_resources - allocate resources for a job request
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * IN immediate - if set then resource allocation must be satisfied 
 *	immediately or fail
 * RET 0 on success or slurm error code
 * NOTE: free the allocated using slurm_free_resource_allocation_response_msg
 */
extern int slurm_allocate_resources (job_desc_msg_t * job_desc_msg , 
		resource_allocation_response_msg_t ** job_alloc_resp_msg, 
		int immediate ) ;

/*
 * slurm_free_resource_allocation_response_msg - free slurm resource
 *	allocation response message
 * IN msg - pointer to allocation response message
 * NOTE: buffer is loaded by slurm_allocate_resources
 */
extern void slurm_free_resource_allocation_response_msg (
		resource_allocation_response_msg_t * msg);

/*
 * slurm_allocate_resources_and_run - allocate resources for a job request and 
 *	initiate a job step
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the response using 
 *	slurm_free_resource_allocation_and_run_response_msg
 */
extern int slurm_allocate_resources_and_run (job_desc_msg_t * job_desc_msg,
		resource_allocation_and_run_response_msg_t ** slurm_alloc_msg );

/*
 * slurm_free_resource_allocation_and_run_response_msg - free slurm 
 *	resource allocation and run job step response message
 * IN msg - pointer to allocation and run job step response message
 * NOTE: buffer is loaded by slurm_allocate_resources_and_run
 */
extern void slurm_free_resource_allocation_and_run_response_msg ( 
		resource_allocation_and_run_response_msg_t * msg);

/*
 * slurm_confirm_allocation - confirm an existing resource allocation
 * IN job_desc_msg - description of existing job request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the response using slurm_free_resource_allocation_response_msg
 */
extern int slurm_confirm_allocation (old_job_alloc_msg_t * job_desc_msg , 
		resource_allocation_response_msg_t ** slurm_alloc_msg ) ;

/*
 * slurm_job_step_create - create a job step for a given job id
 * IN slurm_step_alloc_req_msg - description of job step request
 * OUT slurm_step_alloc_resp_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the response using slurm_free_job_step_create_response_msg
 */
extern int slurm_job_step_create (
		job_step_create_request_msg_t * slurm_step_alloc_req_msg, 
		job_step_create_response_msg_t ** slurm_step_alloc_resp_msg );

/*
 * slurm_free_job_step_create_response_msg - free slurm 
 *	job step create response message
 * IN msg - pointer to job step create response message
 * NOTE: buffer is loaded by slurm_job_step_create
 */
extern void slurm_free_job_step_create_response_msg ( 
		job_step_create_response_msg_t *msg);

/*
 * slurm_submit_batch_job - issue RPC to submit a job for later execution
 * NOTE: free the response using slurm_free_submit_response_response_msg
 * IN job_desc_msg - description of batch job request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 */
extern int slurm_submit_batch_job (job_desc_msg_t * job_desc_msg, 
		submit_response_msg_t ** slurm_alloc_msg );

/*
 * slurm_free_submit_response_response_msg - free slurm 
 *	job submit response message
 * IN msg - pointer to job submit response message
 * NOTE: buffer is loaded by slurm_submit_batch_job
 */
extern void slurm_free_submit_response_response_msg (
		submit_response_msg_t *msg);

/*
 * slurm_job_will_run - determine if a job would execute immediately if 
 *	submitted now
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the allocated using slurm_free_resource_allocation_response_msg
 */
extern int slurm_job_will_run (job_desc_msg_t * job_desc_msg , 
		resource_allocation_response_msg_t ** job_alloc_resp_msg );


/*****************************************************************************\
 *	JOB/STEP CANCELATION FUNCTIONS
\*****************************************************************************/

/*
 * slurm_cancel_job - cancel an existing job and all of its steps 
 * IN job_id - the job's id
 * RET 0 on success or slurm error code
 */
extern int slurm_cancel_job (uint32_t job_id);

/*
 * slurm_cancel_job_step - cancel a specific job step
 * IN job_id - the job's id
 * IN step_id - the job step's id
 * RET 0 on success or slurm error code
 */
extern int slurm_cancel_job_step (uint32_t job_id, uint32_t step_id);


/*****************************************************************************\
 *	JOB/STEP COMPLETION FUNCTIONS
\*****************************************************************************/

/*
 * slurm_complete_job - note the completion of a job and all of its steps 
 * IN job_id - the job's id
 * IN job_return_code - the highest exit code of any task of the job
 * IN system_return_code - any slurm/system exit code
 * RET 0 on success or slurm error code
 */
extern int slurm_complete_job (uint32_t job_id, uint32_t job_return_code,
                               uint32_t system_return_code );

/*
 * slurm_complete_job_step - note the completion of a specific job step 
 * IN job_id - the job's id
 * IN step_id - the job step's id or NO_VAL for all of the job's steps
 * IN job_return_code - the highest exit code of any task of the job
 * IN system_return_code - any slurm/system exit code
 * RET 0 on success or slurm error code
 */
extern int slurm_complete_job_step (uint32_t job_id, uint32_t step_id, 
                                    uint32_t job_return_code,
                                    uint32_t system_return_code );


/*****************************************************************************\
 *	SLURM CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * make_time_str - convert time_t to string with "month/date hour:min:sec" 
 * IN time - a time stamp
 * OUT string - pointer user defined buffer
 */
extern void make_time_str (time_t *time, char *string);

/*
 * slurm_load_ctl_conf - issue RPC to get slurm control configuration  
 *	information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN slurm_ctl_conf_ptr - place to store slurm control configuration 
 *	pointer
 * RET 0 on success or slurm error code
 * NOTE: free the response using slurm_free_ctl_conf
 */
extern int slurm_load_ctl_conf (time_t update_time, 
		slurm_ctl_conf_t  **slurm_ctl_conf_ptr);

/*
 * slurm_free_ctl_conf - free slurm control information response message
 * IN msg - pointer to slurm control information response message
 * NOTE: buffer is loaded by slurm_load_jobs
 */
extern void slurm_free_ctl_conf (slurm_ctl_conf_t* slurm_ctl_conf_ptr);

/*
 * slurm_print_ctl_conf - output the contents of slurm control configuration 
 *	message as loaded using slurm_load_ctl_conf
 * IN out - file to write to
 * IN slurm_ctl_conf_ptr - slurm control configuration pointer
 */
extern void slurm_print_ctl_conf ( FILE * out, 
		slurm_ctl_conf_t* slurm_ctl_conf ) ;


/*****************************************************************************\
 *	SLURM JOB CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_jobs - issue RPC to get slurm all job configuration  
 *	information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_jobs (time_t update_time, 
		job_info_msg_t **job_info_msg_pptr);

/*
 * slurm_free_job_info - free the job information response message
 * IN msg - pointer to job information response message
 * NOTE: buffer is loaded by slurm_load_job.
 */
extern void slurm_free_job_info_msg (job_info_msg_t * job_buffer_ptr);

/*
 * slurm_print_job_info_msg - output information about all Slurm 
 *	jobs based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_info_msg_ptr - job information message pointer
 */
extern void slurm_print_job_info_msg ( FILE * out, 
		job_info_msg_t * job_info_msg_ptr ) ;

/*
 * slurm_print_job_info - output information about a specific Slurm 
 *	job based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_ptr - an individual job information record pointer
 */
extern void slurm_print_job_info ( FILE*, job_info_t * job_ptr );

/*
 * slurm_update_job - issue RPC to a job's configuration per request, 
 *	only usable by user root or (for some parameters) the job's owner
 * IN job_msg - description of job updates
 * RET 0 on success or slurm error code
 */
extern int slurm_update_job ( job_desc_msg_t * job_msg ) ;


/*****************************************************************************\
 *	SLURM JOB STEP CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_get_job_steps - issue RPC to get specific slurm job step   
 *	configuration information if changed since update_time.
 *	a job_id value of zero implies all jobs, a step_id value of 
 *	zero implies all steps
 * IN update_time - time of current configuration data
 * IN job_id - get information for specific job id, zero for all jobs
 * IN step_id - get information for specific job step id, zero for all 
 *	job steps
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_job_step_info_response_msg
 */
extern int slurm_get_job_steps (time_t update_time, uint32_t job_id, 
		uint32_t step_id, 
		job_step_info_response_msg_t **step_response_pptr);

/*
 * slurm_free_job_step_info_response_msg - free the job step 
 *	information response message
 * IN msg - pointer to job step information response message
 * NOTE: buffer is loaded by slurm_get_job_steps.
 */
extern void slurm_free_job_step_info_response_msg (
		job_step_info_response_msg_t * msg);

/*
 * slurm_print_job_step_info_msg - output information about all Slurm 
 *	job steps based upon message as loaded using slurm_get_job_steps
 * IN out - file to write to
 * IN job_step_info_msg_ptr - job step information message pointer
 */
extern void slurm_print_job_step_info_msg ( FILE * out, 
		job_step_info_response_msg_t * job_step_info_msg_ptr );

/*
 * slurm_print_job_step_info - output information about a specific Slurm 
 *	job step based upon message as loaded using slurm_get_job_steps
 * IN out - file to write to
 * IN job_ptr - an individual job step information record pointer
 */
extern void slurm_print_job_step_info ( FILE * out, 
		job_step_info_t * step_ptr );


/*****************************************************************************\
 *	SLURM NODE CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_node - issue RPC to get slurm all node configuration information 
 *	if changed since update_time 
 * IN update_time - time of current configuration data
 * IN node_info_msg_pptr - place to store a node configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node (time_t update_time, 
		node_info_msg_t **node_info_msg_pptr);

/*
 * slurm_free_node_info - free the node information response message
 * IN msg - pointer to node information response message
 * NOTE: buffer is loaded by slurm_load_node.
 */
extern void slurm_free_node_info_msg (node_info_msg_t * node_buffer_ptr);

/*
 * slurm_print_node_info_msg - output information about all Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_info_msg_ptr - node information message pointer
 */
extern void slurm_print_node_info_msg ( FILE * out, 
		node_info_msg_t * node_info_msg_ptr ) ;

/*
 * slurm_print_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_ptr - an individual node information record pointer
 */
extern void slurm_print_node_table ( FILE * out, node_info_t * node_ptr );

/*
 * slurm_update_node - issue RPC to a node's configuration per request, 
 *	only usable by user root
 * IN node_msg - description of node updates
 * RET 0 on success or slurm error code
 */
extern int slurm_update_node ( update_node_msg_t * node_msg ) ;


/*****************************************************************************\
 *	SLURM PARTITION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/* 
 * slurm_init_part_desc_msg - initialize partition descriptor with 
 *	default values 
 * OUT job_desc_msg - user defined partition descriptor
 */
void slurm_init_part_desc_msg (update_part_msg_t * update_part_msg);

/*
 * slurm_load_partitions - issue RPC to get slurm all partition configuration  
 *	information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN partition_info_msg_pptr - place to store a partition configuration 
 *	pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_partition_info_msg
 */
extern int slurm_load_partitions (time_t update_time, 
		partition_info_msg_t **part_buffer_ptr);

/*
 * slurm_free_partition_info_msg - free the partition information 
 *	response message
 * IN msg - pointer to partition information response message
 * NOTE: buffer is loaded by slurm_load_partitions
 */
extern void slurm_free_partition_info_msg ( partition_info_msg_t * part_info_ptr);

/*
 * slurm_print_partition_info_msg - output information about all Slurm 
 *	partitions based upon message as loaded using slurm_load_partitions
 * IN out - file to write to
 * IN part_info_ptr - partitions information message pointer
 */
extern void slurm_print_partition_info_msg ( FILE * out, 
		partition_info_msg_t * part_info_ptr ) ;

/*
 * slurm_print_partition_info - output information about a specific Slurm 
 *	partition based upon message as loaded using slurm_load_partitions
 * IN out - file to write to
 * IN part_ptr - an individual partition information record pointer
 */
extern void slurm_print_partition_info ( FILE *out , 
		partition_info_t * part_ptr ) ;

/*
 * slurm_update_partition - issue RPC to a partition's configuration per  
 *	request, only usable by user root
 * IN part_msg - description of partition updates
 * RET 0 on success or slurm error code
 */
extern int slurm_update_partition ( update_part_msg_t * part_msg ) ;


/*****************************************************************************\
 *	SLURM RECONFIGURE/SHUTDOWN FUNCTIONS
\*****************************************************************************/

/*
 * slurm_reconfigure - issue RPC to have Slurm controller (slurmctld)
 *	reload its configuration file 
 * RET 0 or a slurm error code
 */
extern int slurm_reconfigure ( void );

/*
 * slurm_shutdown - issue RPC to have Slurm controller (slurmctld)
 *	cease operations, both the primary and backup controller 
 *	are shutdown.
 * IN core - controller generates a core file if set
 * RET 0 or a slurm error code
 */
extern int slurm_shutdown (uint16_t core);

#endif

