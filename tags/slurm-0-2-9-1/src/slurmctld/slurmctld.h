/*****************************************************************************\
 *  slurmctld.h - definitions of functions and structures for slurmcltd use
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov> et. al.
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

#ifndef _HAVE_SLURM_H
#define _HAVE_SLURM_H


#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#endif

#include <pthread.h>
/* #include <stdlib.h> */
#include <time.h>
#include <sys/types.h>

#ifdef HAVE_LIBELAN3
#  include "src/common/qsw.h"
#endif				/* HAVE_LIBELAN3 */

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#include <slurm/slurm.h>

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#define FREE_NULL_BITMAP(_X)		\
	do {				\
		if (_X) bit_free (_X);	\
		_X	= NULL; 	\
	} while (0)
#define IS_JOB_FINISHED(_X)		\
	((_X->job_state & (~JOB_COMPLETING)) >  JOB_RUNNING)
#define IS_JOB_PENDING(_X)		\
	((_X->job_state & (~JOB_COMPLETING)) == JOB_PENDING)

/*****************************************************************************\
 *  GENERAL CONFIGURATION parameters and data structures
\*****************************************************************************/
/* Perform full slurmctld's state every PERIODIC_CHECKPOINT seconds */
#define	PERIODIC_CHECKPOINT	300

/* Retry an incomplete RPC agent request every RPC_RETRY_INTERVAL seconds */
#define	RPC_RETRY_INTERVAL	60

/* Attempt to schedule jobs every PERIODIC_SCHEDULE seconds despite 
 * any RPC activity. This will catch any state transisions that may 
 * have otherwise been missed */
#define	PERIODIC_SCHEDULE	30

/* Check for jobs reaching their time limit every PERIODIC_TIMEOUT seconds */
#define	PERIODIC_TIMEOUT	60

/* Pathname of group file record for checking update times */
#define GROUP_FILE	"/etc/group"

/* Check for updates to GROUP_FILE every PERIODIC_GROUP_CHECK seconds, 
 * Update the group uid_t access list as needed */
#define	PERIODIC_GROUP_CHECK	600

/* Default temporary storage for slurm state and user files */
#define DEFAULT_TMP_FS	"/tmp"

/* Don't accept more jobs once there are MAX_JOB_COUNT in the system
 * This should prevent exhausting memory */
#define MAX_JOB_COUNT 2000

/* Purge OK for jobs over MIN_JOB_AGE seconds old (since completion)
 * This should prevent exhausting memory */
#define MIN_JOB_AGE 300

extern slurm_ctl_conf_t slurmctld_conf;

/*****************************************************************************\
 *  NODE parameters and data structures
\*****************************************************************************/
#define MAX_NAME_LEN	32
#define CONFIG_MAGIC 0xc065eded
#define NODE_MAGIC   0x0de575ed

struct config_record {
	uint32_t magic;		/* magic cookie to test data integrity */
	uint32_t cpus;		/* count of cpus running on the node */
	uint32_t real_memory;	/* MB real memory on the node */
	uint32_t tmp_disk;	/* MB total storage in TMP_FS file system */
	uint32_t weight;	/* arbitrary priority of node for 
				 * scheduling work on */
	char *feature;		/* arbitrary list of features associated */
	char *nodes;		/* name of nodes with this configuration */
	bitstr_t *node_bitmap;	/* bitmap of nodes with this configuration */
};

extern List config_list;	/* list of config_record entries */

struct node_record {
	uint32_t magic;			/* magic cookie for data integrity */
	char name[MAX_NAME_LEN];	/* name of the node. NULL==defunct */
	uint16_t node_state;		/* enum node_states, ORed with 
					 * NODE_STATE_NO_RESPOND if not 
					 * responding */
	time_t last_response;		/* last response from the node */
	uint32_t cpus;			/* count of cpus on the node */
	uint32_t real_memory;		/* MB real memory on the node */
	uint32_t tmp_disk;		/* MB total disk in TMP_FS */
	struct config_record *config_ptr;  /* configuration spec ptr */
	struct part_record *partition_ptr; /* partition for this node */
	char comm_name[MAX_NAME_LEN];	/* communications path name to node */
	slurm_addr slurm_addr;		/* network address */
	uint16_t comp_job_cnt;		/* count of jobs completing on node */
	uint16_t run_job_cnt;		/* count of jobs running on node */
};

extern struct node_record *node_record_table_ptr;  /* ptr to node records */
extern time_t last_bitmap_update;	/* time of last node creation or 
					 * deletion */
extern time_t last_node_update;		/* time of last node record update */
extern int node_record_count;		/* count in node_record_table_ptr */
extern int *hash_table;			/* table of hashed indicies into 
					 * node_record_table_ptr */
extern bitstr_t *up_node_bitmap;	/* bitmap of nodes are up */
extern bitstr_t *idle_node_bitmap;	/* bitmap of nodes are idle */
extern struct config_record default_config_record;
extern struct node_record default_node_record;

/*****************************************************************************\
 *  PARTITION parameters and data structures
\*****************************************************************************/
#define PART_MAGIC 0xaefe8495

struct part_record {
	uint32_t magic;		/* magic cookie to test data integrity */
	char name[MAX_NAME_LEN];/* name of the partition */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t min_nodes;	/* per job */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint16_t root_only;	/* 1 if allocate/submit RPC can only be 
				   issued by user root */
	uint16_t shared;	/* 1 if >1 job can share a node,
				   2 if sharingrequired */
	uint16_t state_up;	/* 1 if state is up, 0 if down */
	char *nodes;		/* comma delimited list names of nodes */
	char *allow_groups;	/* comma delimited list of groups, 
				 * NULL indicates all */
	uid_t *allow_uids;	/* zero terminated list of allowed users */
	bitstr_t *node_bitmap;	/* bitmap of nodes in partition */
};

extern List part_list;			/* list of part_record entries */
extern time_t last_part_update;		/* time of last part_list update */
extern struct part_record default_part;	/* default configuration values */
extern char default_part_name[MAX_NAME_LEN];	/* name of default partition */
extern struct part_record *default_part_loc;	/* default partition ptr */

/*****************************************************************************\
 *  JOB parameters and data structures
\*****************************************************************************/
extern time_t last_job_update;	/* time of last update to part records */

#define DETAILS_MAGIC 0xdea84e7
#define JOB_MAGIC 0xf0b7392c
#define STEP_MAGIC 0xce593bc1
#define KILL_ON_STEP_DONE	1

extern int job_count;			/* number of jobs in the system */

/* job_details - specification of a job's constraints, 
 * can be purged after initiation */
struct job_details {
	uint32_t magic;			/* magic cookie for data integrity */
	uint32_t num_procs;		/* minimum number of processors */
	uint32_t min_nodes;		/* minimum number of nodes */
	uint32_t max_nodes;		/* maximum number of nodes */
	char *req_nodes;		/* required nodes */
	char *exc_nodes;		/* excluded nodes */
	bitstr_t *req_node_bitmap;	/* bitmap of required nodes */
	bitstr_t *exc_node_bitmap;	/* bitmap of excluded nodes */
	char *features;			/* required features */
	uint16_t shared;		/* set node can be shared*/
	uint16_t contiguous;		/* set if requires contiguous nodes */
	uint32_t min_procs;		/* minimum processors per node, MB */
	uint32_t min_memory;		/* minimum memory per node, MB */
	uint32_t min_tmp_disk;		/* minimum tempdisk per node, MB */
	char *err;			/* pathname of job's stderr file */
	char *in;			/* pathname of job's stdin file */
	char *out;			/* pathname of job's stdout file */
	uint32_t total_procs;		/* number of allocated processors, 
					   for accounting */
	time_t submit_time;		/* time of submission */
	char *work_dir;			/* pathname of working directory */
};

struct job_record {
	uint32_t job_id;		/* job ID */
	uint32_t magic;			/* magic cookie for data integrity */
	char name[MAX_NAME_LEN];	/* name of the job */
	char partition[MAX_NAME_LEN];	/* name of the partition */
	struct part_record *part_ptr;	/* pointer to the partition record */
	uint16_t batch_flag;		/* 1 if batch job (with script) */
	uint32_t user_id;		/* user the job runs as */
	enum job_states job_state;	/* state of the job */
	uint16_t kill_on_node_fail;	/* 1 if job should be killed on 
					 * node failure */
	uint16_t kill_on_step_done;	/* 1 if job should be killed when 
					 * the job step completes, 2 if kill
					 * in progress */
	char *nodes;			/* list of nodes allocated to job */
	bitstr_t *node_bitmap;		/* bitmap of nodes allocated to job */
	uint32_t time_limit;		/* time_limit minutes or INFINITE,
					 * NO_VAL implies partition max_time */
	time_t start_time;		/* time execution begins, 
					 * actual or expected */
	time_t end_time;		/* time of termination, 
					 * actual or expected */
	time_t time_last_active;	/* time of last job activity */
	uint32_t priority;		/* relative priority of the job,
					 * zero == held (don't initiate) */
	struct job_details *details;	/* job details */
	uint16_t num_cpu_groups;	/* record count in cpus_per_node and 
					 * cpu_count_reps */
	uint32_t *cpus_per_node;	/* array of cpus per node allocated */
	uint32_t *cpu_count_reps;	/* array of consecutive nodes with 
					 * same cpu count */
	uint32_t alloc_sid;		/* local sid making resource alloc */
	char    *alloc_node;		/* local node making resource alloc */
	uint16_t next_step_id;		/* next step id to be used */
	uint16_t node_cnt;		/* count of nodes allocated to job */
	slurm_addr *node_addr;		/* addresses of the nodes allocated to 
					 * job */
	List step_list;			/* list of job's steps */
};

struct 	step_record {
	struct job_record* job_ptr; 	/* ptr to the job that owns the step */
	uint16_t step_id;		/* step number */
	uint16_t cyclic_alloc;		/* set for cyclic task allocation 
					   across nodes */
	uint32_t num_tasks;		/* number of tasks required */
	time_t start_time;      	/* step allocation time */
	char *step_node_list;		/* list of nodes allocated to job 
					   step */
	bitstr_t *step_node_bitmap;	/* bitmap of nodes allocated to job 
					   step */
#ifdef HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;		/* Elan3 switch context, opaque */
#endif
};

typedef struct job_step_specs step_specs; 
extern List job_list;			/* list of job_record entries */

/*****************************************************************************\
 *  Global slurmctld functions
\*****************************************************************************/

/*
 * allocate_nodes - change state of specified nodes to NODE_STATE_ALLOCATED
 * IN bitmap - map of nodes to be allocated
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	last_node_update - last update time of node table
 */
extern void  allocate_nodes (unsigned *bitmap);

/*
 * bitmap2node_name - given a bitmap, build a list of comma separated node 
 *	names. names may include regular expressions (e.g. "lx[01-10]")
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error 
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
extern char * bitmap2node_name (bitstr_t *bitmap) ;

/*
 * build_node_details - set cpu counts and addresses for allocated nodes:
 *	cpu_count_reps, cpus_per_node, node_addr, node_cnt, num_cpu_groups
 * IN job_ptr - pointer to a job record
 */
extern void build_node_details (struct job_record *job_ptr);

/*
 * count_cpus - report how many cpus are associated with the identified nodes 
 * IN bitmap - map of nodes to tally
 * RET cpu count
 * globals: node_record_count - number of nodes configured
 *	node_record_table_ptr - pointer to global node table
 */
extern int  count_cpus (unsigned *bitmap);

/*
 * create_config_record - create a config_record entry and set is values to 
 *	the defaults. each config record corresponds to a line in the  
 *	slurm.conf file and typically describes the configuration of a 
 *	large number of nodes
 * RET pointer to the config_record
 * global: default_config_record - default configuration values
 * NOTE: memory allocated will remain in existence until 
 *	_delete_config_record() is called to delete all configuration records
 */
extern struct config_record *create_config_record (void);

/* 
 * create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * IN/OUT error_code - set to zero if no error, errno otherwise
 * RET pointer to the record or NULL if error
 * global: job_list - global job list
 *	job_count - number of jobs in the system
 *	last_job_update - time of last job table update
 * NOTE: allocates memory that should be xfreed with _list_delete_job
 */
extern struct job_record * create_job_record (int *error_code);

/* 
 * create_node_record - create a node record and set its values to defaults
 * IN config_point - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
 * global: default_node_record - default node values
 * NOTE: the record's values are initialized to those of default_node_record, 
 *	node_name and config_point's cpus, real_memory, and tmp_disk values
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when  
 *	the global node table is no longer required
 */
extern struct node_record *create_node_record (struct config_record
					       *config_point,
					       char *node_name);

/* 
 * create_part_record - create a partition record
 * RET a pointer to the record or NULL if error
 * global: default_part - default partition parameters
 *         part_list - global partition list
 * NOTE: the record's values are initialized to those of default_part
 * NOTE: allocates memory that should be xfreed with delete_part_record
 */
extern struct part_record *create_part_record (void);

/* 
 * create_step_record - create an empty step_record for the specified job.
 * IN job_ptr - pointer to job table entry to have step record added
 * RET a pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
extern struct step_record * create_step_record (struct job_record *job_ptr);

/*
 * deallocate_nodes - for a given job, deallocate its nodes and make 
 *	their state NODE_STATE_COMPLETING
 * IN job_ptr - pointer to terminating job
 * IN timeout - true of job exhausted time limit, send REQUEST_KILL_TIMELIMIT
 *	RPC instead of REQUEST_JOB_KILL
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
extern void deallocate_nodes (struct job_record  * job_ptr, bool timeout);

/* 
 * delete_all_step_records - delete all step record for specified job_ptr
 * IN job_ptr - pointer to job table entry to have step record added
 */
extern void delete_all_step_records (struct job_record *job_ptr);

/* 
 * delete_job_details - delete a job's detail record and clear it's pointer
 *	this information can be deleted as soon as the job is allocated  
 *	resources and running (could need to restart batch job)
 * IN job_entry - pointer to job_record to clear the record of
 */
extern void  delete_job_details (struct job_record *job_entry);

/* 
 * delete_step_record - delete record for job step for specified job_ptr 
 *	and step_id
 * IN job_ptr - pointer to job table entry to have step record removed
 * IN step_id - id of the desired job step
 * RET 0 on success, errno otherwise
 */
extern int delete_step_record (struct job_record *job_ptr, uint32_t step_id);

/* dump_all_job_state - save the state of all jobs to file
 * RET 0 or error code */
extern int dump_all_job_state ( void );

/* dump_all_node_state - save the state of all nodes to file */
extern int dump_all_node_state ( void );

/* dump_all_part_state - save the state of all partitions to file */
extern int dump_all_part_state ( void );

/*
 * dump_job_desc - dump the incoming job submit request message
 * IN job_specs - job specification from RPC
 */
extern void dump_job_desc(job_desc_msg_t * job_specs);

/*
 * dump_step_desc - dump the incoming step initiate request message
 * IN step_spec - job step request specification from RPC
 */
extern void dump_step_desc(step_specs *step_spec);

/* 
 * find_job_record - return a pointer to the job record with the given job_id
 * IN job_id - requested job's id
 * RET pointer to the job's record, NULL on error
 * global: job_list - global job list pointer
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */
extern struct job_record *find_job_record (uint32_t job_id);

/*
 * find_first_node_record - find a record for first node in the bitmap
 * IN node_bitmap
 */
extern struct node_record *find_first_node_record (bitstr_t *node_bitmap);

/* find_node_record - find a record for node with specified name */
extern struct node_record *find_node_record (char *name);

/* 
 * find_part_record - find a record for partition with specified name
 * IN name - name of the desired partition 
 * RET pointer to node partition or NULL if not found
 * global: part_list - global partition list
 */
extern struct part_record *find_part_record (char *name);

/*
 * find_running_job_by_node_name - Given a node name, return a pointer to any 
 *	job currently running on that node
 * IN node_name - name of a node
 * RET pointer to the job's record, NULL if no job on node found
 */
extern struct job_record *find_running_job_by_node_name (char *node_name);

/*
 * get_job_env - return the environment variables and their count for a 
 *	given job
 * IN job_ptr - pointer to job for which data is required
 * OUT env_size - number of elements to read
 * RET point to array of string pointers containing environment variables
 */
extern char **get_job_env (struct job_record *job_ptr, uint16_t *env_size);

/* 
 * get_job_script - return the script for a given job
 * IN job_ptr - pointer to job for which data is required
 * RET point to string containing job script
 */
extern char *get_job_script (struct job_record *job_ptr);

/* 
 * find_step_record - return a pointer to the step record with the given 
 *	job_id and step_id
 * IN job_ptr - pointer to job table entry to have step record added
 * IN step_id - id of the desired job step
 * RET pointer to the job step's record, NULL on error
 */
extern struct step_record * find_step_record(struct job_record *job_ptr, 
					     uint16_t step_id);

/* 
 * init_job_conf - initialize the job configuration tables and values. 
 *	this should be called after creating node information, but 
 *	before creating any job entries.
 * RET 0 if no error, otherwise an error code
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
extern int init_job_conf (void);

/* 
 * init_node_conf - initialize the node configuration tables and values. 
 *	this should be called before creating any node or configuration 
 *	entries.
 * RET 0 if no error, otherwise an error code
 * global: node_record_table_ptr - pointer to global node table
 *         default_node_record - default values for node records
 *         default_config_record - default values for configuration records
 *         hash_table - table of hash indecies
 *         last_node_update - time of last node table update
 */
extern int init_node_conf ();

/* 
 * init_part_conf - initialize the default partition configuration values 
 *	and create a (global) partition list. 
 * this should be called before creating any partition entries.
 * RET 0 if no error, otherwise an error code
 * global: default_part - default partition values
 *         part_list - global partition list
 */
extern int init_part_conf (void);

/*
 * is_node_down - determine if the specified node's state is DOWN
 * IN name - name of the node
 * RET true if node exists and is down, otherwise false 
 */
extern bool is_node_down (char *name);

/*
 * job_allocate - create job_records for the suppied job specification and 
 *	allocate nodes for it.
 * IN job_specs - job specifications
 * IN node_list - location for storing new job's allocated nodes
 * IN immediate - if set then either initiate the job immediately or fail
 * IN will_run - don't initiate the job if set, just test if it could run 
 *	now or later
 * IN allocate - resource allocation request if set, not a full job
 * OUT new_job_id - the new job's ID
 * OUT num_cpu_groups - number of cpu groups (elements in cpus_per_node 
 *	and cpu_count_reps)
 * OUT cpus_per_node - pointer to array of numbers of cpus on each node 
 *	allocate
 * OUT cpu_count_reps - pointer to array of numbers of consecutive nodes 
 *	having same cpu count
 * OUT node_list - list of nodes allocated to the job
 * OUT node_cnt - number of allocated nodes
 * OUT node_addr - slurm_addr's for the allocated nodes
 * RET 0 or an error code
 * NOTE: If allocating nodes lx[0-7] to a job and those nodes have cpu counts  
 *	 of 4, 4, 4, 4, 8, 8, 4, 4 then num_cpu_groups=3, cpus_per_node={4,8,4}
 *	and cpu_count_reps={4,2,2}
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 */
extern int job_allocate(job_desc_msg_t * job_specs, uint32_t * new_job_id,
	     char **node_list, uint16_t * num_cpu_groups,
	     uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps,
	     int immediate, int will_run, int allocate, uid_t submit_uid,
	     uint16_t * node_cnt, slurm_addr ** node_addr);

/* 
 * job_signal - signal the specified job
 * IN job_id - id of the job to be signaled
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN uid - uid of requesting user
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_signal(uint32_t job_id, uint16_t signal, uid_t uid);

/* 
 * job_step_cancel - cancel the specified job step
 * IN job_id - id of the job to be cancelled
 * IN step_id - id of the job step to be cancelled
 * IN uid - user id of user issuing the RPC
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_cancel (uint32_t job_id, uint32_t job_step_id, uid_t uid );

/* 
 * job_complete - note the normal termination the specified job
 * IN job_id - id of the job which completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN job_return_code - job's return code, if set then set state to JOB_FAILED
 * RET - 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_complete (uint32_t job_id, uid_t uid, bool requeue, 
		uint32_t job_return_code);

/* 
 * job_step_complete - note normal completion the specified job step
 * IN job_id - id of the job to be completed
 * IN step_id - id of the job step to be completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN job_return_code - job's return code, if set then set state to JOB_FAILED
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_complete (uint32_t job_id, uint32_t job_step_id, 
			uid_t uid, bool requeue, uint32_t job_return_code);

/* 
 * job_step_signal - signal the specified job step
 * IN job_id - id of the job to be cancelled
 * IN step_id - id of the job step to be cancelled
 * IN signal - user id of user issuing the RPC
 * IN uid - user id of user issuing the RPC
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_signal(uint32_t job_id, uint32_t step_id, 
			   uint16_t signal, uid_t uid);

/* 
 * job_time_limit - terminate jobs which have exceeded their time limit
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern void job_time_limit (void);

/*
 * kill_running_job_by_node_name - Given a node name, deallocate jobs 
 *	from the node or kill them 
 * IN node_name - name of a node
 * IN step_test - if true, only kill the job if a step is running on the node
 * RET number of killed jobs
 */
extern int kill_running_job_by_node_name(char *node_name, bool step_test);


/* list_compare_config - compare two entry from the config list based upon 
 *	weight, see common/list.h for documentation */
int list_compare_config (void *config_entry1, void *config_entry2);

/*
 * list_find_part - find an entry in the partition list, see common/list.h 
 *	for documentation
 * IN key - partition name or "universal_key" for all partitions 
 * RET 1 if matches key, 0 otherwise 
 * global- part_list - the global partition list
 */
extern int list_find_part (void *part_entry, void *key);

/*
 * load_all_job_state - load the job state from file, recover from last 
 *	checkpoint. Execute this after loading the configuration file data.
 * RET 0 or error code
 */
extern int load_all_job_state ( void );

/*
 * load_all_node_state - load the node state from file, recover on slurmctld 
 *	restart. execute this after loading the configuration file data.
 *	data goes into common storage
 */
extern int load_all_node_state ( void );

/*
 * load_part_uid_allow_list - reload the allow_uid list of partitions
 *	if required (updated group file or force set)
 * IN force - if set then always reload the allow_uid list
 */
extern void load_part_uid_allow_list ( int force );

/*
 * load_all_part_state - load the partition state from file, recover from 
 *	slurmctld restart. execute this after loading the configuration 
 *	file data.
 */
extern int load_all_part_state ( void );

/* make_node_alloc - flag specified node as allocated to a job */
extern void make_node_alloc(struct node_record *node_ptr);

/* make_node_comp - flag specified node as completing a job */
extern void make_node_comp(struct node_record *node_ptr);

/*
 * make_node_idle - flag specified node as having completed a job
 * IN node_ptr - pointer to node reporting job completion
 * IN job_ptr - pointer to job that just completed
 */
extern void make_node_idle(struct node_record *node_ptr, 
			   struct job_record *job_ptr);

/* msg_to_slurmd - send given msg_type every slurmd, no args */
extern void msg_to_slurmd (slurm_msg_type_t msg_type);

/*
 * node_name2bitmap - given a node name regular expression, build a bitmap 
 *	representation
 * IN node_names - list of nodes
 * OUT bitmap - set to bitmap or NULL on error 
 * RET 0 if no error, otherwise EINVAL or enomem
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree memory at bitmap when no longer required
 */
extern int node_name2bitmap (char *node_names, bitstr_t **bitmap);

/* node_did_resp - record that the specified node is responding
 * IN name - name of the node */
extern void node_did_resp (char *name);

/* 
 * node_not_resp - record that the specified node is not responding
 * IN name - name of the node 
 * IN msg_time - time message was sent
 */
extern void node_not_resp (char *name, time_t msg_time);

/*
 * old_job_info - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_id - ID of job for which info is requested
 * OUT everything else - the job's detains
 */
extern int old_job_info (uint32_t uid, uint32_t job_id, char **node_list, 
	uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, 
	uint32_t ** cpu_count_reps,
	uint16_t * node_cnt, slurm_addr ** node_addr);

/* 
 * pack_all_jobs - dump all job information for all jobs in 
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c 
 *	whenever the data format changes
 */
extern void pack_all_jobs (char **buffer_ptr, int *buffer_size);

/* 
 * pack_all_node - dump all configuration and node information for all nodes  
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 */
extern void pack_all_node (char **buffer_ptr, int *buffer_size);

/* 
 * pack_ctld_job_step_info_response_msg - packs job step info
 * IN - job_id and step_id - zero for all
 * OUT buffer - location to store data, pointers automatically advanced 
 * RET - 0 or error code
 * NOTE: MUST free_buf buffer
 */
extern int pack_ctld_job_step_info_response_msg ( uint32_t job_id, 
					uint32_t step_id, Buf buffer );

/* 
 * pack_all_part - dump all partition information for all partitions in 
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * global: part_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change slurm_load_part() in api/part_info.c if data format changes
 */
extern void pack_all_part (char **buffer_ptr, int *buffer_size);

/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission)
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	  whenever the data format changes
 */
extern void pack_job (struct job_record *dump_job_ptr, Buf buffer);

/* 
 * pack_part - dump all configuration information about a specific partition 
 *	in machine independent form (for network transmission)
 * IN dump_part_ptr - pointer to partition for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * global: default_part_loc - pointer to the default partition
 * NOTE: if you make any changes here be sure to make the corresponding 
 *	changes to load_part_config in api/partition_info.c
 */
extern void pack_part (struct part_record *part_record_point, Buf buffer);

/* ping_nodes - check that all nodes and daemons are alive,  
 *	get nodes in UNKNOWN state to register */
extern void ping_nodes (void);

/*
 * purge_old_job - purge old job records. 
 *	the jobs must have completed at least MIN_JOB_AGE minutes ago
 * global: job_list - global job table
 *	last_job_update - time of last job table update
 */
void purge_old_job (void);

/*
 * read_slurm_conf - load the slurm configuration from the configured file. 
 * read_slurm_conf can be called more than once if so desired.
 * IN recover - set to use state saved from last slurmctld shutdown
 * RET 0 if no error, otherwise an error code
 * Note: Operates on common variables only
 */
extern int  read_slurm_conf (int recover);

/* 
 * rehash - build a hash table of the node_record entries. this is a large 
 *	hash table to permit the immediate finding of a record based only 
 *	upon its name without regards to their number. there should be no 
 *	need for a search. 
 * global: node_record_table_ptr - pointer to global node table
 *         hash_table - table of hash indecies
 * NOTE: manages memory for hash_table
 */
extern void rehash (void);

/* update first assigned job id as needed on reconfigure */
extern void reset_first_job_id(void);

/* 
 * reset_job_bitmaps - reestablish bitmaps for existing jobs. 
 *	this should be called after rebuilding node information, 
 *	but before using any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
extern void reset_job_bitmaps (void);

/* 
 * schedule - attempt to schedule all pending jobs
 *	pending jobs for each partition will be scheduled in priority  
 *	order until a request fails
 * RET count of jobs scheduled
 * global: job_list - global list of job records
 *	last_job_update - time of last update to job table
 * Note: We re-build the queue every time. Jobs can not only be added 
 *	or removed from the queue, but have their priority or partition 
 *	changed with the update_job RPC. In general nodes will be in priority 
 *	order (by submit time), so the sorting should be pretty fast.
 */
extern int schedule (void);

/*
 * select_nodes - select and allocate nodes to a specific job
 * IN job_ptr - pointer to the job record
 * IN test_only - if set do not allocate nodes, just confirm they  
 *	could be allocated now
 * RET 0 on success, ESLURM code from slurm_errno.h otherwise
 * globals: list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 *	config_list - global list of node configuration info
 * Notes: The algorithm is
 *	1) Build a table (node_set_ptr) of nodes with the requisite 
 *	   configuration. Each table entry includes their weight, 
 *	   node_list, features, etc.
 *	2) Call _pick_best_nodes() to select those nodes best satisfying 
 *	   the request, (e.g. best-fit or other criterion)
 *	3) Call allocate_nodes() to perform the actual allocation
 */
extern int select_nodes (struct job_record *job_ptr, bool test_only);

/* set_node_down - make the specified node's state DOWN 
 * IN name - name of the node */
extern void set_node_down (char *name);

/* set_slurmd_addr - establish the slurm_addr for the slurmd on each node
 *	Uses common data structures. */
extern void set_slurmd_addr (void);

/*
 * Tell the backup_controller to relinquish control, primary control_machine 
 *	has resumed operation
 * RET 0 or an error code 
 */
extern int shutdown_backup_controller(void);

/*
 * signal_step_tasks - send specific signal to specific job step
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 */
extern void signal_step_tasks(struct step_record *step_ptr, uint16_t signal);

/*
 * step_create - creates a step_record in step_specs->job_id, sets up the
 *	accoding to the step_specs.
 * IN step_specs - job step specifications
 * OUT new_step_record - pointer to the new step_record (NULL on error)
 * IN kill_job_when_step_done - if set kill the job on step completion
 * RET - 0 or error code
 * NOTE: don't free the returned step_record because that is managed through
 * 	the job.
 */
extern int step_create ( step_specs *step_specs, 
			 struct step_record** new_step_record,
			 bool kill_job_when_step_done );

/* 
 * step_on_node - determine if the specified job has any job steps allocated to 
 * 	the specified node 
 * IN job_ptr - pointer to an active job record
 * IN node_ptr - pointer to a node record
 * RET true of job has step on the node, false otherwise 
 */
extern bool step_on_node(struct job_record  *job_ptr, 
			 struct node_record *node_ptr);

/*
 * switch_state_fini - save switch state and shutdown switch
 * RET 0 if no error, otherwise an error code
 */ 
extern int switch_state_fini(void);

/*
 * switch_state_begin - Recover or initialize switch state
 * IN recover - If set, recover switch state as previously saved
 * RET 0 if no error, otherwise an error code
 */ 
extern int switch_state_begin(int recover);

/*
 * Synchronize the batch job in the system with their files.
 * All pending batch jobs must have script and environment files
 * No other jobs should have such files
 */
extern int sync_job_files(void);

/*
 * update_job - update a job's parameters per the supplied specifications
 * IN job_specs - a job's specification
 * IN uid - uid of user issuing RPC
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job (job_desc_msg_t * job_specs, uid_t uid);


/* Reset slurmctld logging based upon configuration parameters
 * uses common slurmctld_conf data structure */
extern void update_logging(void);

/* 
 * update_node - update the configuration data for one or more nodes
 * IN update_node_msg - update node request
 * RET 0 or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int update_node ( update_node_msg_t * update_node_msg )  ;

/* 
 * update_part - update a partition's configuration data
 * IN part_desc - description of partition changes
 * RET 0 or an error code
 * global: part_list - list of partition entries
 *	last_part_update - update time of partition records
 */
extern int update_part (update_part_msg_t * part_desc );

/*
 * validate_group - validate that the submit uid is authorized to run in 
 *	this partition
 * IN part_ptr - pointer to a partition
 * IN submit_uid - user submitting the job
 * RET 1 if permitted to run, 0 otherwise
 */
extern int validate_group (struct part_record *part_ptr, uid_t submit_uid);

/*
 * validate_jobs_on_node - validate that any jobs that should be on the node 
 *	are actually running, if not clean up the job records and/or node 
 *	records, call this function after validate_node_specs() sets the node 
 *	state properly 
 * IN node_name - node which should have jobs running
 * IN/OUT job_count - number of jobs which should be running on specified node
 * IN job_id_ptr - pointer to array of job_ids that should be on this node
 * IN step_id_ptr - pointer to array of job step ids that should be on node
 */
extern void validate_jobs_on_node ( char *node_name, uint32_t *job_count, 
			uint32_t *job_id_ptr, uint16_t *step_id_ptr);

/*
 * validate_node_specs - validate the node's specifications as valid, 
 *   if not set state to down, in any case update last_response
 * IN node_name - name of the node
 * IN cpus - number of cpus measured
 * IN real_memory - mega_bytes of real_memory measured
 * IN tmp_disk - mega_bytes of tmp_disk measured
 * IN job_count - number of jobs allocated to this node
 * IN status - node status code
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 * global: node_record_table_ptr - pointer to global node table
 */
extern int validate_node_specs (char *node_name,
				uint32_t cpus, uint32_t real_memory, 
				uint32_t tmp_disk, uint32_t job_count,
				uint32_t status);

#endif /* !_HAVE_SLURM_H */
