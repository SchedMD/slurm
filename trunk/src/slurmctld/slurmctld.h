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
#include <src/common/slurm_protocol_api.h>
#include <src/common/xmalloc.h>

/* Perform full slurmctld's state every PERIODIC_CHECKPOINT seconds */
#define	PERIODIC_CHECKPOINT	300

/* Attempt to schedule jobs every PERIODIC_SCHEDULE seconds despite any RPC activity 
 * This will catch any state transisions that may have otherwise been missed */
#define	PERIODIC_SCHEDULE	30

/* Check for jobs reaching their time limit every PERIODIC_TIMEOUT seconds */
#define	PERIODIC_TIMEOUT	60

/* Pathname of group file record for checking update times */
#define GROUP_FILE	"/etc/group"

/* Check for updates to GROUP_FILE every PERIODIC_GROUP_CHECK seconds, 
 *	Update the group uid_t access list as needed */
#define	PERIODIC_GROUP_CHECK	600

#define safe_unpack16(valp,bufp,lenp) {			\
        if (*(lenp) < sizeof(*(valp)))			\
		break;					\
	unpack16(valp,bufp,lenp);			\
}

#define safe_unpack32(valp,bufp,lenp) {			\
        if (*(lenp) < sizeof(*(valp)))			\
		break;					\
	unpack32(valp,bufp,lenp);			\
}

#define safe_unpackstr_xmalloc(valp,size_valp,bufp,lenp) { \
       if (*(lenp) < sizeof(uint16_t))			\
		break;					\
	unpackmem_xmalloc(valp,size_valp,bufp,lenp);	\
}

extern slurm_ctl_conf_t slurmctld_conf;

#define MAX_NAME_LEN	32
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
	uint16_t node_state;		/* enum node_states, ORed with 
					   NODE_STATE_NO_RESPOND if not responding */
	time_t last_response;		/* last response from the node */
	uint32_t cpus;			/* actual count of cpus running on the node */
	uint32_t real_memory;		/* actual megabytes of real memory on the node */
	uint32_t tmp_disk;		/* actual megabytes of total disk in TMP_FS */
	struct config_record *config_ptr;	/* configuration specification for this node */
	struct part_record *partition_ptr;	/* partition for this node */
	struct sockaddr_in slurm_addr;	/* network address */
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
	uint16_t root_only;	/* 1 if allocate/submit RPC can only be issued by user root */
	uint16_t shared;	/* 1 if >1 job can share a node, 2 if required */
	uint16_t state_up;	/* 1 if state is up, 0 if down */
	char *nodes;		/* comma delimited list names of nodes in partition */
	char *allow_groups;	/* comma delimited list of groups, null indicates all */
	uid_t *allow_uids;	/* zero terminated list of allowed users */
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
#define MAX_JOB_COUNT 2000
#define MIN_JOB_AGE 300
#define STEP_MAGIC 0xce593bc1

extern int job_count;			/* number of jobs in the system */

/* job_details - specification of a job's constraints, not required after initiation */
struct job_details {
	uint32_t magic;			/* magic cookie to test data integrity */
	uint32_t num_procs;		/* minimum number of processors */
	uint32_t num_nodes;		/* minimum number of nodes */
	char *req_nodes;		/* required nodes */
	bitstr_t *req_node_bitmap;	/* bitmap of required nodes */
	slurm_job_credential_t	credential;	/* job credential */
	char *features;			/* required features */
	uint16_t shared;		/* 1 if more than one job can execute on a node */
	uint16_t contiguous;		/* requires contiguous nodes, 1=true, 0=false */
	uint32_t min_procs;		/* minimum processors per node, MB */
	uint32_t min_memory;		/* minimum memory per node, MB */
	uint32_t min_tmp_disk;		/* minimum temporary disk per node, MB */
	char *stderr;			/* pathname of job's stderr file */
	char *stdin;			/* pathname of job's stdin file */
	char *stdout;			/* pathname of job's stdout file */
	uint32_t total_procs;		/* total number of allocated processors, for accounting */
	time_t submit_time;		/* time of submission */
	char *work_dir;			/* pathname of job's working directory */
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
	struct job_record* job_ptr; 	/* ptr to the job that owns the step */
	uint16_t step_id;		/* step number */
	uint16_t cyclic_alloc;		/* set for cyclic task allocation to nodes */
	time_t start_time;      	/* step allocation time */
	bitstr_t *node_bitmap;		/* bitmap of nodes in allocated to job step */
#ifdef HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;		/* Elan3 switch context, opaque data structure */
#endif
};

typedef struct job_step_specs step_specs; 

extern List job_list;			/* list of job_record entries */

/* allocate_nodes - for a given bitmap, change the state of specified nodes to stage_in
 * this is a simple prototype for testing 
 */
extern void  allocate_nodes (unsigned *bitmap);

/*
 * bitmap2node_name - given a bitmap, build a list of comma separated node names.
 *	names may include regular expressions (e.g. "lx[01-10]")
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
extern char * bitmap2node_name (bitstr_t *bitmap) ;

/* build_node_details - give a node bitmap, return cpu counts for those nodes */
extern void build_node_details (bitstr_t *node_bitmap, 
		uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t **cpu_count_reps);

/* count_cpus - report how many cpus are associated with the identified nodes */
extern int  count_cpus (unsigned *bitmap);

/*
 * create_config_record - create a config_record entry and set is values to the defaults.
 * NOTE: memory allocated will remain in existence until delete_config_record() is called 
 *	to deletet all configuration records
 */
extern struct config_record *create_config_record (void);

/* 
 * create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * NOTE: allocates memory that should be xfreed with list_delete_job
 */
extern struct job_record * create_job_record (int *error_code);

/* 
 * create_node_record - create a node record
 * NOTE: allocates memory that should be freed with delete_part_record
 */
extern struct node_record *create_node_record (struct config_record
					       *config_point,
					       char *node_name);

/* 
 * create_part_record - create a partition record
 * NOTE: allocates memory that should be xfreed with delete_part_record
 */
extern struct part_record *create_part_record (void);

/* 
 * create_step_record - create an empty step_record for the specified job.
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
extern struct step_record * create_step_record (struct job_record *job_ptr);

/* deallocate_nodes - for a given job, deallocate its nodes and make their state IDLE */
extern void deallocate_nodes (struct job_record  * job_ptr);

/* delete_all_step_records - delete all step record for specified job_ptr */
extern void delete_all_step_records (struct job_record *job_ptr);

/* delete_job_details - delete a job's detail record and clear it's pointer */
extern void  delete_job_details (struct job_record *job_entry);

/* delete_node_record - delete record for node with specified name */
extern int delete_node_record (char *name);

/* delete_part_record - delete record for partition with specified name */
extern int delete_part_record (char *name);

/* delete_step_record - delete record for job step for specified job_ptr and step_id */
extern int delete_step_record (struct job_record *job_ptr, uint32_t step_id);

/* dump_all_job_state - save the state of all jobs to file */
extern int dump_all_job_state ( void );

/* dump_all_node_state - save the state of all nodes to file */
extern int dump_all_node_state ( void );

/* dump_all_part_state - save the state of all partitions to file */
extern int dump_all_part_state ( void );

/* dump_job_desc - dump the incoming job submit request message */
extern void dump_job_desc(job_desc_msg_t * job_specs);

/* dump_step_desc - dump the incoming step initiate request message */
extern void dump_step_desc(step_specs *step_spec);

/*  find_job_record - return a pointer to the job record with the given job_id */
extern struct job_record *find_job_record (uint32_t job_id);

/* find_node_record - find a record for node with specified name */
extern struct node_record *find_node_record (char *name);

/* find_part_record - find a record for partition with specified name */
extern struct part_record *find_part_record (char *name);

/* find_step_record - return a pointer to the step record with the given job_id and step_id */
extern struct step_record * find_step_record(struct job_record *job_ptr, uint16_t step_id);

/* 
 * init_job_conf - initialize the job configuration tables and values. 
 *	this should be called after creating node information, but 
 *	before creating any job entries.
 */
extern int init_job_conf ();

/* 
 * init_node_conf - initialize the node configuration values. 
 * this should be called before creating any node or configuration entries.
 */
extern int init_node_conf ();

/* 
 * init_part_conf - initialize the partition configuration values. 
 * this should be called before creating any partition entries.
 */
extern int init_part_conf ();

/* is_key_valid report if the supplied partition key is valid */
extern int  is_key_valid (void * key);

/* job_allocate - allocate resource for the supplied job specifications */
extern int job_allocate (job_desc_msg_t  *job_specs, uint32_t *new_job_id, char **node_list, 
	uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps, 
	int immediate, int will_run, int allocate, uid_t submit_uid);

/* job_cancel - cancel the specified job */
extern int job_cancel (uint32_t job_id, uid_t uid);

/* job_step_cancel - cancel the specified job step */
extern int job_step_cancel (uint32_t job_id, uint32_t job_step_id, uid_t uid );

/* job_complete - note the completion the specified job */
extern int job_complete (uint32_t job_id, uid_t uid);

/* job_step_complete - note the completion the specified job step*/
extern int job_step_complete (uint32_t job_id, uint32_t job_step_id, uid_t uid);

/* job_time_limit - enforce job time limits */
extern void job_time_limit (void);

/* list_append_list - Appends the elements of from list onto the to list */
extern void list_append_list( List to, List from );
	
/* list_compare_config - compare two entry from the config list based upon weight */
extern int list_compare_config (void *config_entry1, void *config_entry2);

/* list_delete_config - delete an entry from the configuration list */
extern void list_delete_config (void *config_entry);

/* list_find_config - find an entry in the configuration list */
extern int list_find_config (void *config_entry, void *key);

/* list_find_part - find an entry in the partition list */
extern int list_find_part (void *part_entry, void *key);

/* load_job_state - load the job state from file, recover from slurmctld restart */
extern int load_job_state ( void );

/* load_node_state - load the node state from file, recover from slurmctld restart */
extern int load_node_state ( void );

/* load_part_uid_allow_list - for every partition reload the allow_uid list */
extern void load_part_uid_allow_list ( int force );

/* load_part_state - load the partition state from file, recover from slurmctld restart */
extern int load_part_state ( void );

/* match_feature - determine if the desired feature (seek) is one of those available */
extern int  match_feature (char *seek, char *available);

/* mkdir2 - issues system calls for mkdir (if root) */
int mkdir2 (char * path, int modes);

/* node_name2bitmap - given a node name regular expression, build a bitmap representation */
extern int node_name2bitmap (char *node_names, bitstr_t **bitmap);

/* 
 * pack_all_jobs - dump all job information for all jobs in 
 *	machine independent form (for network transmission)
 * NOTE: the caller must xfree the buffer at *buffer_ptr when no longer required
 */
extern void pack_all_jobs (char **buffer_ptr, int *buffer_size, 
	time_t * update_time);

/* pack_all_node - dump all configuration and node information for all nodes in 
 *	machine independent form (for network transmission)
 * NOTE: the caller must xfree the buffer at *buffer_ptr when no longer required
 */
extern void pack_all_node (char **buffer_ptr, int *buffer_size, time_t * update_time);

/* pack_ctld_job_step_info_response_msg - packs the message
 * IN - job_id and step_id - zero for all
 * OUT - packed buffer and length NOTE- MUST xfree buffer
 * return - error code
 */
extern int pack_ctld_job_step_info_response_msg (void** buffer_base, int* buffer_length, 
						uint32_t job_id, uint32_t step_id );

/* pack_ctld_job_step_info - packs a job_step_info_t from a step_record
 */
extern void pack_ctld_job_step_info( struct  step_record* step, void **buf_ptr, int *buf_len);
	
/* 
 * pack_all_part - dump all partition information for all partitions in 
 *	machine independent form (for network transmission)
 * NOTE: the caller must xfree the buffer at *buffer_ptr when no longer required
 */
extern void pack_all_part (char **buffer_ptr, int *buffer_size, time_t * update_time);

/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission)
 */
extern void pack_job (struct job_record *dump_job_ptr, void **buf_ptr, int *buf_len);

/* 
 * pack_part - dump all configuration information about a specific partition in 
 *	machine independent form (for network transmission)
 */
extern void pack_part (struct part_record *part_record_point, void **buf_ptr, int *buf_len);

/*
 * purge_old_job - purge old job records. if memory space is needed. 
 *	the jobs must have completed at least MIN_JOB_AGE minutes ago
 */
void purge_old_job (void);

/* read_slurm_conf - load the slurm configuration from the configured file */
extern int  read_slurm_conf (int recover);

/* rehash - build a hash table of the node_record entries */
extern void rehash (void);

/* reset_job_bitmaps - reestablish bitmaps for existing jobs */
extern void reset_job_bitmaps (void);

/* rmdir2 - issues system call to rmdir (if root) */
extern int rmdir2 (char * path);

/* schedule - attempt to schedule all pending jobs */
extern int schedule (void);

/* select_nodes - select and allocate nodes to a specific job */
extern int select_nodes (struct job_record *job_ptr, int test_only);

/* set_job_id - set a default job_id, insure that it is unique */
extern void	set_job_id (struct job_record *job_ptr);

/* set_job_prio - set a default job priority */
extern void	set_job_prio (struct job_record *job_ptr);

/* set_slurmd_addr - establish the slurm_addr for the slurmd on each node */
extern void set_slurmd_addr (void);

/* step_count - return a count of steps associated with a specific job */
extern int step_count (struct job_record *job_ptr);

/* step_create - parse the suppied job step specification and create step_records for it */
extern int step_create ( step_specs *step_specs, struct step_record** );

/* step_lock - lock the step information */
extern void step_lock (void);

/* step_unlock - unlock the step information */
extern void step_unlock (void);

/* sync_nodes_to_jobs - sync the node state to job states on slurmctld restart */
extern int sync_nodes_to_jobs (void);

/* update_job - update a job's parameters per the supplied specification */
extern int update_job (job_desc_msg_t * job_specs, uid_t uid);

/* update_node - update the configuration data for one or more nodes per the supplied specification */
extern int update_node ( update_node_msg_t * update_node_msg )  ;

/* update_part - update a partition's configuration data per the supplied specification */
extern int update_part (update_part_msg_t * part_desc );

/* validate_group - validate that the submit uid is authorized to run in this partition */
extern int validate_group (struct part_record *part_ptr, uid_t submit_uid);

/* validate_node_specs - validate the node's specifications as valid */
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
