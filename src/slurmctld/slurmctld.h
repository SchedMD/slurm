/*****************************************************************************\
 * slurmctld.h - definitions of functions and structures for slurmcltd use
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

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>

#ifdef HAVE_LIBELAN3
#include <src/common/qsw.h>
#endif

#include <src/api/slurm.h>
#include <src/common/bitstring.h>
#include <src/common/list.h>
#include <src/common/log.h>
#include <src/common/macros.h>
#include <src/common/pack.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/xmalloc.h>

#if DEBUG_MODULE
slurm_ctl_conf_t slurmctld_conf;
#else
extern slurm_ctl_conf_t slurmctld_conf;
#endif

#define BACKUP_INTERVAL		60
#define BACKUP_LOCATION		"/usr/local/slurm/slurm.state"
#define CONTROL_DAEMON  	"/usr/local/slurm/slurmd.control"
#define CONTROLLER_TIMEOUT 	300
#define EPILOG			""
#define FAST_SCHEDULE		1
#define HASH_BASE		10
#define HEARTBEAT_INTERVAL	60
#define INIT_PROGRAM		""
#define FIRST_JOB_ID		(1 << 16)
#define KILL_WAIT		30
#define	PRIORITIZE		""
#define PROLOG			""
#define SERVER_DAEMON   	"/usr/local/slurm/slurmd.server"
#define SERVER_TIMEOUT  	300
#define SLURM_CONF		"../../etc/slurm.conf2"
#define TMP_FS			"/tmp"

extern char *control_machine;	/* name of computer acting as slurm controller */
extern char *backup_controller;	/* name of computer acting as slurm backup controller */

extern slurm_ctl_conf_t slurmctld_config;

#define CONFIG_MAGIC 0xc065eded
#define NODE_MAGIC   0x0de575ed
struct config_record {
	uint32_t magic;		/* magic cookie to test data integrity */
	uint32_t cpus;		/* count of cpus running on the node */
	uint32_t real_memory;	/* megabytes of real memory on the node */
	uint32_t tmp_disk;	/* megabytes of total storage in TMP_FS file system */
	uint32_t weight;	/* arbitrary priority of node for scheduling work on */
	char *feature;		/* arbitrary list of features associated with a node */
	char *nodes;		/* names of nodes in partition configuration record */
	bitstr_t *node_bitmap;	/* bitmap of nodes in configuration record */
};
extern List config_list;	/* list of config_record entries */

extern time_t last_bitmap_update;	/* time of last node creation or deletion */
extern time_t last_node_update;		/* time of last update to node records */
struct node_record {
	uint32_t magic;			/* magic cookie to test data integrity */
	char name[MAX_NAME_LEN];	/* name of the node. a null name indicates defunct node */
	uint16_t node_state;		/* enum node_states, ORed with STATE_NO_RESPOND if down */
	time_t last_response;		/* last response from the node */
	uint32_t cpus;			/* actual count of cpus running on the node */
	uint32_t real_memory;		/* actual megabytes of real memory on the node */
	uint32_t tmp_disk;		/* actual megabytes of total disk in TMP_FS */
	struct config_record *config_ptr;	/* configuration specification for this node */
	struct part_record *partition_ptr;	/* partition for this node */
};
extern struct node_record *node_record_table_ptr;	/* location of the node records */
extern int node_record_count;		/* count of records in the node record table */
extern int *hash_table;			/* table of hashed indicies into node_record */
extern bitstr_t *up_node_bitmap;	/* bitmap of nodes are up */
extern bitstr_t *idle_node_bitmap;	/* bitmap of nodes are idle */
extern struct config_record default_config_record;
extern struct node_record default_node_record;

/* NOTE: change PART_STRUCT_VERSION value whenever the contents of PART_STRUCT_FORMAT change */
#define PART_MAGIC 0xaefe8495
extern time_t last_part_update;	/* time of last update to part records */
struct part_record {
	uint32_t magic;		/* magic cookie to test data integrity */
	char name[MAX_NAME_LEN];/* name of the partition */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint16_t key;		/* 1 if slurm distributed key is required for use of partition */
	uint16_t shared;	/* 1 if >1 job can share a node, 2 if required */
	uint16_t state_up;	/* 1 if state is up, 0 if down */
	char *nodes;		/* comma delimited list names of nodes in partition */
	char *allow_groups;	/* comma delimited list of groups, null indicates all */
	bitstr_t *node_bitmap;	/* bitmap of nodes in partition */
};
extern List part_list;		/* list of part_record entries */
extern struct part_record default_part;	/* default configuration values */
extern char default_part_name[MAX_NAME_LEN];	/* name of default partition */
extern struct part_record *default_part_loc;	/* location of default partition */

/* NOTE: change JOB_STRUCT_VERSION value whenever the contents of JOB_STRUCT_FORMAT change */
extern time_t last_job_update;	/* time of last update to part records */
/*
	FIXME: this should be taken out.
	Maybe there should be an update for the step_list in every job.
extern time_t last_step_update;	*//* time of last update to job steps */

/* Don't accept more jobs once there are MAX_JOB_COUNT in the system */
/* Purge OK for jobs over MIN_JOB_AGE seconds old (since completion) */
/* This should prevent exhausting memory */
#define DETAILS_MAGIC 0xdea84e7
#define JOB_MAGIC 0xf0b7392c
#define MAX_JOB_COUNT 1000
#define MIN_JOB_AGE 600
#define STEP_MAGIC 0xce593bc1

extern int job_count;			/* number of jobs in the system */

/* job_details - specification of a job's constraints, not required after initiation */
struct job_details {
	uint32_t magic;			/* magic cookie to test data integrity */
	uint32_t num_procs;		/* minimum number of processors */
	uint32_t num_nodes;		/* minimum number of nodes */
	char *req_nodes;		/* required nodes */
	bitstr_t *req_node_bitmap;	/* bitmap of required nodes */
	char *features;			/* required features */
	uint16_t shared;		/* 1 if more than one job can execute on a node */
	uint16_t contiguous;		/* requires contiguous nodes, 1=true, 0=false */
	uint32_t min_procs;		/* minimum processors per node, MB */
	uint32_t min_memory;		/* minimum memory per node, MB */
	uint32_t min_tmp_disk;		/* minimum temporary disk per node, MB */
	char *job_script;		/* name of job script to execute */
	uint32_t total_procs;		/* total number of allocated processors, for accounting */
	time_t submit_time;		/* time of submission */
};

struct job_record {
	uint32_t job_id;		/* job ID */
	uint32_t magic;			/* magic cookie to test data integrity */
	char name[MAX_NAME_LEN];	/* name of the job */
	char partition[MAX_NAME_LEN];	/* name of the partition */
	struct part_record *part_ptr;	/* pointer to the partition record */
	uint32_t user_id;		/* user the job runs as */
	enum job_states job_state;	/* state of the job */
	char *nodes;			/* comma delimited list of nodes allocated to job */
	bitstr_t *node_bitmap;		/* bitmap of nodes in allocated to job */
	uint32_t time_limit;		/* maximum run time in minutes or INFINITE */
	time_t start_time;		/* time execution begins, actual or expected*/
	time_t end_time;		/* time of termination, actual or expected */
	uint32_t priority;		/* relative priority of the job */
	struct job_details *details;	/* job details (set until job terminates) */
	uint16_t num_cpu_groups;	/* element count in arrays cpus_per_node and cpu_count_reps */
	uint32_t *cpus_per_node;	/* array of cpus per node allocated */
	uint32_t *cpu_count_reps;	/* array of consecutive nodes with same cpu count */
	uint16_t next_step_id;		/* next step id to be used */
	List step_list;			/* list of job's steps */
};

struct 	step_record {
	uint16_t step_id;		/* step number */
	bitstr_t *node_bitmap;		/* bitmap of nodes in allocated to job step */
#ifdef HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;		/* Elan3 switch context, opaque data structure */
#endif
};

typedef struct job_step_create_request_msg step_specs; 

extern List job_list;			/* list of job_record entries */

/* allocate_nodes - for a given bitmap, change the state of specified nodes to stage_in
 * this is a simple prototype for testing 
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
extern void  allocate_nodes (unsigned *bitmap);

/*
 * bitmap2node_name - given a bitmap, build a node name list representation using 
 * 	regular expressions
 * input: bitmap - bitmap pointer
 *        node_list - place to put node list
 * output: node_list - set to node list or null on error 
 * NOTE: consider returning the node list as a regular expression if helpful
 * NOTE: the caller must free memory at node_list when no longer required
 */
extern void bitmap2node_name (bitstr_t *bitmap, char **node_list);

/*
 * count_cpus - report how many cpus are associated with the identified nodes 
 * input: bitmap - a node bitmap
 * output: returns a cpu count
 * globals: node_record_count - number of nodes configured
 *	node_record_table_ptr - pointer to global node table
 */
extern int  count_cpus (unsigned *bitmap);

/*
 * create_config_record - create a config_record entry and set is values to the defaults.
 * output: returns pointer to the config_record
 * global: default_config_record - default configuration values
 * NOTE: memory allocated will remain in existence until delete_config_record() is called 
 *	to deletet all configuration records
 */
extern struct config_record *create_config_record (void);

/* 
 * create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * input: error_code - location to store error value in
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or NULL if error
 * global: job_list - global job list
 *         job_count - number of jobs in the system
 * NOTE: allocates memory that should be xfreed with list_delete_job
 */
extern struct job_record * create_job_record (int *error_code);

/* 
 * create_node_record - create a node record
 * input: error_code - location to store error value in
 *        config_point - pointer to node's configuration information
 *        node_name - name of the node
 * output: returns a pointer to the record or null if error
 * note the record's values are initialized to those of default_node_record, node_name and 
 *	config_point's cpus, real_memory, and tmp_disk values
 * NOTE: allocates memory that should be freed with delete_part_record
 */
extern struct node_record *create_node_record (struct config_record
					       *config_point,
					       char *node_name);

/* 
 * create_part_record - create a partition record
 * output: returns a pointer to the record or NULL if error
 * global: default_part - default partition parameters
 *         part_list - global partition list
 * NOTE: the record's values are initialized to those of default_part
 * NOTE: allocates memory that should be xfreed with delete_part_record
 */
extern struct part_record *create_part_record (void);

/* 
 * create_step_record - create an empty step_record for the specified job.
 * input: job_ptr - pointer to job table entry to have step record added
 * output: returns a pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
extern struct step_record * create_step_record (struct job_record *job_ptr);

/* deallocate_nodes - for a given bitmap, change the state of specified nodes to idle
 * this is a simple prototype for testing 
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
extern void deallocate_nodes (unsigned *bitmap);

/* 
 * delete_job_details - delete a job's detail record and clear it's pointer
 * input: job_entry - pointer to job_record to clear the record of
 */
extern void  delete_job_details (struct job_record *job_entry);

/* 
 * delete_node_record - delete record for node with specified name
 *   to avoid invalidating the bitmaps and hash table, we just clear the name 
 *   set its state to STATE_DOWN
 * input: name - name of the desired node 
 * output: return 0 on success, errno otherwise
 */
extern int delete_node_record (char *name);

/* 
 * delete_part_record - delete record for partition with specified name
 * input: name - name of the desired node 
 * output: return 0 on success, errno otherwise
 */
extern int delete_part_record (char *name);

/* 
 * delete_step_record - delete record for job step for specified job_ptr and step_id
 * input: job_ptr - pointer to job table entry to have step record added
 *	step_id - id of the desired job step
 * output: return 0 on success, errno otherwise
 */
extern int delete_step_record (struct job_record *job_ptr, uint32_t step_id);

/* dump_job_desc - dump the incoming job submit request message */
void dump_job_desc(job_desc_msg_t * job_specs);

/* 
 * find_job_record - return a pointer to the job record with the given job_id
 * input: job_id - requested job's id
 * output: pointer to the job's record, NULL on error
 * global: job_list - global job list pointer
 */
extern struct job_record *find_job_record (uint32_t job_id);

/* 
 * find_node_record - find a record for node with specified name,
 * input: name - name of the desired node 
 * output: return pointer to node record or null if not found
 */
extern struct node_record *find_node_record (char *name);

/* 
 * find_part_record - find a record for partition with specified name,
 * input: name - name of the desired partition 
 * output: return pointer to node partition or null if not found
 * global: part_list - global partition list
 */
extern struct part_record *find_part_record (char *name);

/* 
 * find_step_record - return a pointer to the step record with the given job_id and step_id
 * input: job_ptr - pointer to job table entry to have step record added
 *	step_id - id of the desired job step
 * output: pointer to the job step's record, NULL on error
 */
extern struct step_record * find_step_record(struct job_record *job_ptr, uint16_t step_id);

/* 
 * init_job_conf - initialize the job configuration tables and values. 
 *	this should be called after creating node information, but 
 *	before creating any job entries.
 * output: return value - 0 if no error, otherwise an error code
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
extern int init_job_conf ();

/* 
 * init_node_conf - initialize the node configuration values. 
 * this should be called before creating any node or configuration entries.
 * output: return value - 0 if no error, otherwise an error code
 */
extern int init_node_conf ();

/* 
 * init_part_conf - initialize the partition configuration values. 
 * this should be called before creating any partition entries.
 * output: return value - 0 if no error, otherwise an error code
 */
extern int init_part_conf ();

/* 
 * init_slurm_conf - initialize or re-initialize the slurm configuration  
 *	values. this should be called before calling read_slurm_conf.  
 * output: return value - 0 if no error, otherwise an error code
 * globals: control_machine - name of primary slurmctld machine
 *	backup_controller - name of backup slurmctld machine
 */
extern int init_slurm_conf ();

extern int  is_key_valid (void * key);

extern int job_allocate (job_desc_msg_t  *job_specs, uint32_t *new_job_id, char **node_list, 
	uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps, 
	int immediate, int will_run);

/* 
 * job_cancel - cancel the specified job
 * input: job_id - id of the job to be cancelled
 * output: returns 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_cancel (uint32_t job_id);

/* 
 * job_step_cancel - cancel the specified job step
 * input: job_id, step_id - id of the job to be cancelled
 * output: returns 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_cancel (uint32_t job_id, uint32_t job_step_id);

/*
 * job_create - parse the suppied job specification and create job_records for it
 * input: job_specs - job specifications
 *	new_job_id - location for storing new job's id
 *	job_rec_ptr - place to park pointer to the job (or NULL)
 * output: new_job_id - the job's ID
 *	returns 0 on success, EINVAL if specification is invalid
 *	allocate - if set, job allocation only (no script required)
 *	will_run - if set then test only, don't create a job entry
 *	job_rec_ptr - pointer to the job (if not passed a NULL)
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */
extern int job_create (job_desc_msg_t * job_specs, uint32_t *new_job_id, int allocate, 
	    int will_run, struct job_record **job_rec_ptr);

/* job_lock - lock the job information */
extern void job_lock ();

/* job_unlock - unlock the job information */
extern void job_unlock ();

/* list_compare_config - compare two entry from the config list based upon weight, 
 * see list.h for documentation */
extern int list_compare_config (void *config_entry1, void *config_entry2);

/* list_delete_config - delete an entry from the configuration list, 
 *see list.h for documentation */
extern void list_delete_config (void *config_entry);

/* list_find_config - find an entry in the configuration list, 
 * see list.h for documentation 
 * key is partition name or "universal_key" for all configuration */
extern int list_find_config (void *config_entry, void *key);

/* list_delete_part - delete an entry from the partition list, 
 * see list.h for documentation */
extern void list_delete_part (void *part_entry);

/* list_find_part - find an entry in the partition list, 
 * see list.h for documentation 
 * key is partition name or "universal_key" for all partitions */
extern int list_find_part (void *part_entry, void *key);

/*
 * load_float - location into which result is stored
 *        keyword - string to search for
 *        in_line - string to search for keyword
 * output: *destination - set to value, no change if value not found
 *         in_line - the keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: in_line is overwritten, do not use a constant
 */
extern int  load_float (float *destination, char *keyword, char *in_line);

/*
 * load_integer - parse a string for a keyword, value pair  
 * input: *destination - location into which result is stored
 *        keyword - string to search for
 *        in_line - string to search for keyword
 * output: *destination - set to value, no change if value not found, 
 *             set to 1 if keyword found without value, 
 *             set to -1 if keyword followed by "unlimited"
 *         in_line - the keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: in_line is overwritten, do not use a constant
 */
extern int load_integer (int *destination, char *keyword, char *in_line);

extern int load_long (long *destination, char *keyword, char *in_line);

/*
 * load_string - parse a string for a keyword, value pair  
 * input: *destination - location into which result is stored
 *        keyword - string to search for
 *        in_line - string to search for keyword
 * output: *destination - set to value, no change if value not found, 
 *	     if *destination had previous value, that memory location is automatically freed
 *         in_line - the keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: destination must be free when no longer required
 * NOTE: if destination is non-null at function call time, it will be freed 
 * NOTE: in_line is overwritten, do not use a constant
 */
extern int load_string (char **destination, char *keyword, char *in_line);

/*
 * match_feature - determine if the desired feature (seek) is one of those available
 * input: seek - desired feature
 *        available - comma separated list of features
 * output: returns 1 if found, 0 otherwise
 */
extern int  match_feature (char *seek, char *available);

/*
 * match_group - determine if the user is a member of any groups permitted to use this partition
 * input: allow_groups - comma delimited list of groups permitted to use the partition, 
 *			NULL is for all groups
 *        user_groups - comma delimited list of groups the user belongs to
 * output: returns 1 if user is member, 0 otherwise
 */
extern int match_group (char *allow_groups, char *user_groups);

/* node_lock - lock the node and configuration information */
extern void node_lock ();

/* node_unlock - unlock the node and configuration information */
extern void node_unlock ();

/*
 * node_name2bitmap - given a node name regular expression, build a bitmap representation
 * input: node_names - list of nodes
 *        bitmap - place to put bitmap pointer
 * output: bitmap - set to bitmap or null on error 
 *         returns 0 if no error, otherwise EINVAL or ENOMEM
 * NOTE: the caller must free memory at bitmap when no longer required
 */
extern int node_name2bitmap (char *node_names, bitstr_t **bitmap);

/* 
 * pack_all_jobs - dump all job information for all jobs in 
 *	machine independent form (for network transmission) */
extern void pack_all_jobs (char **buffer_ptr, int *buffer_size, 
	time_t * update_time);

/* pack_all_node - dump all configuration and node information for all nodes in 
 *	machine independent form (for network transmission)
 * NOTE: the caller must xfree the buffer at *buffer_ptr when no longer required
 */
extern void pack_all_node (char **buffer_ptr, int *buffer_size, time_t * update_time);

/* 
 * pack_all_part - dump all partition information for all partitions in 
 *	machine independent form (for network transmission) */
extern void pack_all_part (char **buffer_ptr, int *buffer_size, time_t * update_time);

/* 
 * pack_all_step - dump all job step information for all steps in 
 *	machine independent form (for network transmission) */
extern void pack_all_step (char **buffer_ptr, int *buffer_size, time_t * update_time);

/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission) */
extern void pack_job (struct job_record *dump_job_ptr, void **buf_ptr, int *buf_len);

/* pack_node - dump all configuration information about a specific node in 
 *	machine independent form (for network transmission) */
extern void pack_node (struct node_record *dump_node_ptr, void **buf_ptr, int *buf_len); 

/* 
 * pack_part - dump all configuration information about a specific partition in 
 *	machine independent form (for network transmission) */
extern void pack_part (struct part_record *part_record_point, void **buf_ptr, int *buf_len);

/* 
 * pack_step - dump state information about a specific job step in 
 *	machine independent form (for network transmission) */
extern void pack_step (struct step_record *dump_step_ptr, void **buf_ptr, int *buf_len);

/* part_lock - lock the partition information */
extern void part_lock ();

/* part_unlock - unlock the partition information */
extern void part_unlock ();

/*
 * purge_old_job - purge old job records. if memory space is needed. 
 *	the jobs must have completed at least MIN_JOB_AGE minutes ago
 */
void purge_old_job (void);

/*
 * read_slurm_conf - load the slurm configuration from the specified file. 
 * read_slurm_conf can be called more than once if so desired.
 * input: file_name - name of the file containing overall slurm configuration information
 * output: return - 0 if no error, otherwise an error code
 * global: control_machine - primary machine on which slurmctld runs
 * 	backup_controller - backup machine on which slurmctld runs
 *	default_part_loc - pointer to default partition
 * NOTE: call init_slurm_conf before ever calling read_slurm_conf.  
 */
extern int  read_slurm_conf (char *file_name);

/* 
 * rehash - build a hash table of the node_record entries. this is a large hash table 
 * to permit the immediate finding of a record based only upon its name without regards 
 * to the number. there should be no need for a search. the algorithm is optimized for 
 * node names with a base-ten sequence number suffix. if you have a large cluster and 
 * use a different naming convention, this function and/or the hash_index function 
 * should be re-written.
 * global: node_record_table_ptr - pointer to global node table
 *         hash_table - table of hash indecies
 * NOTE: allocates memory for hash_table
 */
extern void rehash ();

/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line.
 * input: in_line - what is left of the configuration input line.
 *        line_num - line number of the configuration file.
 * output: none
 */
/* extern void report_leftover (char *in_line, int line_num); */


/* 
 * reset_job_bitmaps - reestablish bitmaps for existing jobs. 
 *	this should be called after rebuilding node information, 
 *	but before using any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
extern void reset_job_bitmaps ();

/* 
 * schedule - attempt to schedule all pending jobs
 *	pending jobs for each partition will be scheduled in priority  
 *	order until a request fails
 * global: job_list - global list of job records
 *	last_job_update - time of last update to job table
 */
void schedule();

/*
 * select_nodes - select and allocate nodes to a specific job
 * input: job_ptr - pointer to the job record
 *	test_only - do not allocate nodes, just confirm they could be allocated now
 * output: returns 0 on success, EINVAL if not possible to satisfy request, 
 *		or EAGAIN if resources are presently busy
 *	job_ptr->nodes is set to the node list (on success)
 * globals: list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 *	config_list - global list of node configuration info
 */
extern int select_nodes (struct job_record *job_ptr, int test_only);

/* set_job_id - set a default job_id, insure that it is unique */
extern void	set_job_id (struct job_record *job_ptr);

/* set_job_prio - set a default job priority */
extern void	set_job_prio (struct job_record *job_ptr);

/* 
 * slurm_parser - parse the supplied specification into keyword/value pairs
 *	only the keywords supplied will be searched for. the supplied specification
 *	is altered, overwriting the keyword and value pairs with spaces.
 * input: spec - pointer to the string of specifications
 *	sets of three values (as many sets as required): keyword, type, value 
 *	keyword - string with the keyword to search for including equal sign 
 *		(e.g. "name=")
 *	type - char with value 'd' for int, 'f' for float, 's' for string
 *	value - pointer to storage location for value (char **) for type 's'
 * output: spec - everything read is overwritten by speces
 *	value - set to read value (unchanged if keyword not found)
 *	return - 0 if no error, otherwise errno code
 * NOTE: terminate with a keyword value of "END"
 * NOTE: values of type (char *) are xfreed if non-NULL. caller must xfree any 
 *	returned value
 */
extern int slurm_parser (char *spec, ...);

/*
 * step_create - parse the suppied job step specification and create step_records for it
 * input: step_specs - job step specifications
 * output: returns 0 on success, EINVAL if specification is invalid
 * NOTE: the calling program must xfree the memory pointed to by new_job_id
 */
extern int step_create ( step_specs *step_specs, struct step_record** );

/* step_lock - lock the step information 
 * global: step_mutex - semaphore for the step table
 */
extern void step_lock ();

/* step_unlock - unlock the step information 
 * global: step_mutex - semaphore for the step table
 */
extern void step_unlock ();

/*
 * update_job - update a job's parameters per the supplied specifications
 * output: returns 0 on success, otherwise an error code from common/slurm_protocol_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job (job_desc_msg_t * job_specs);

/* 
 * update_node - update the configuration data for one or more nodes
 * input: node_names - node names, may contain regular expression
 *        spec - the updates to the node's specification 
 * output:  return - 0 if no error, otherwise an error code
 */
extern int update_node ( update_node_msg_t * update_node_msg )  ;

/* 
 * update_part - update a partition's configuration data
 * global: part_list - list of partition entries
 *	last_part_update - update time of partition records
 */
extern int update_part (update_part_msg_t * part_desc );


/*
 * validate_node_specs - validate the node's specifications as valid, 
 *   if not set state to down, in any case update last_response
 * input: node_name - name of the node
 *        cpus - number of cpus measured
 *        real_memory - mega_bytes of real_memory measured
 *        tmp_disk - mega_bytes of tmp_disk measured
 * output: returns 0 if no error, enoent if no such node, einval if values too low
 */
extern int validate_node_specs (char *node_name,
				uint32_t cpus, uint32_t real_memory, 
				uint32_t tmp_disk);

/*
 * yes_or_no - map string into integer
 * input: in_string: pointer to string containing "YES" or "NO"
 * output: returns 1 for "YES", 0 for "NO", -1 otherwise
 */
extern int yes_or_no (char *in_string);

#endif /* !_HAVE_SLURM_H */
