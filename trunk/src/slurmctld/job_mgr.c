/*****************************************************************************\
 * job_mgr.c - manage the job information of slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "pack.h"
#include "slurmctld.h"
#include "xstring.h"
#include <src/common/slurm_protocol_errno.h>
#include <src/common/macros.h>

#define BUF_SIZE 1024
#define MAX_STR_PACK 128

#define SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 0

/* macros for simple comparisons
 */
#define block_or_cycle(in_string) \
		(strcmp((in_string),"BLOCK")? \
			(strcmp((in_string),"CYCLE")? \
				-1 : DIST_CYCLE ) : DIST_BLOCK ) 

#define yes_or_no(in_string) \
		(( strcmp ((in_string),"YES"))? \
			(strcmp((in_string),"NO")? \
				-1 : 0 ) : 1 ) 

int job_count;				/* job's in the system */
List job_list = NULL;			/* job_record list */
time_t last_job_update;			/* time of last update to job records */
static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;	/* lock for job info */
static struct job_record *job_hash[MAX_JOB_COUNT];
static struct job_record *job_hash_over[MAX_JOB_COUNT];
static int max_hash_over = 0;

void	list_delete_job (void *job_entry);
int	list_find_job_id (void *job_entry, void *key);
int	list_find_job_old (void *job_entry, void *key);
int	top_priority (struct job_record *job_ptr);
int copy_job_desc_to_job_record ( job_desc_msg_t * job_desc , struct job_record ** job_ptr , struct part_record *part_ptr, bitstr_t *req_bitmap) ;
int validate_job_desc ( job_desc_msg_t * job_desc_msg , int allocate ) ;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
	int
main (int argc, char *argv[]) 
{
	int dump_size, error_code, error_count = 0, i;
	time_t update_time = (time_t) NULL;
	struct job_record * job_rec;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	char *dump;
	uint16_t tmp_id;
	char update_spec[] = "TimeLimit=1234 Priority=123";

	printf("initialize the database and create a few jobs\n");
	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	error_code = init_job_conf ();
	if (error_code) {
		printf ("ERROR: init_job_conf error %d\n", error_code);
		error_count++;
	}
	job_rec = create_job_record(&error_code);
	if ((job_rec == NULL) || error_code) {
		printf ("ERROR: create_job_record failure %d\n", error_code);
		error_count++;
		exit(error_count);
	}
	strcpy (job_rec->name, "Name1");
	strcpy (job_rec->partition, "batch");
	job_rec->details->job_script = xmalloc(20);
	strcpy (job_rec->details->job_script, "/bin/hostname");
	job_rec->details->num_nodes = 1;
	job_rec->details->num_procs = 1;
	set_job_id (job_rec);
	set_job_prio (job_rec);
	tmp_id = job_rec->job_id;

	for (i=1; i<=4; i++) {
		job_rec = create_job_record (&error_code);
		if ((job_rec == NULL) || error_code) {
			printf ("ERROR: create_job_record failure %d\n",error_code);
			error_count++;
			exit (error_count);
		}
		strcpy (job_rec->name, "Name2");
		strcpy (job_rec->partition, "debug");
		job_rec->details->job_script = xmalloc(20);
		strcpy (job_rec->details->job_script, "/bin/hostname");
		job_rec->details->num_nodes = i;
		job_rec->details->num_procs = i;
		set_job_id (job_rec);
		set_job_prio (job_rec);
	}

	printf ("\nupdate a job record\n");
	error_code = update_job (tmp_id, update_spec);
	if (error_code) {
		printf ("ERROR: update_job error %d\n", error_code);
		error_count++;
	}

	error_code = pack_all_jobs (&dump, &dump_size, &update_time);
	if (error_code) {
		printf ("ERROR: dump_all_job error %d\n", error_code);
		error_count++;
	}
	if (dump)
		xfree(dump);

	job_rec = find_job_record (tmp_id);
	if (job_rec == NULL) {
		printf("find_job_record error 1\n");
		error_count++;
	}
	else
		printf ("found job %u, script=%s\n", 
				job_rec->job_id, job_rec->details->job_script);

	error_code = delete_job_record (tmp_id);
	if (error_code) {
		printf ("ERROR: delete_job_record error %d\n", error_code);
		error_count++;
	}

	job_rec = find_job_record (tmp_id);
	if (job_rec != NULL) {
		printf ("find_job_record error 2\n");
		error_count++;
	}

	exit (error_count);
}
#endif


/* 
 * create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * input: error_code - location to store error value in
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or NULL if error
 * global: job_list - global job list
 *	job_count - number of jobs in the system
 *	last_job_update - time of last job table update
 * NOTE: allocates memory that should be xfreed with either
 *	delete_job_record or list_delete_job
 */
struct job_record * 
create_job_record (int *error_code) 
{
	struct job_record  *job_record_point;
	struct job_details *job_details_point;

	if (job_count >= MAX_JOB_COUNT) {
		error ("create_job_record: job_count exceeds limit"); 
		*error_code = EAGAIN;
		return NULL;
	}

	*error_code = 0;
	last_job_update = time (NULL);

	job_record_point =
		(struct job_record *)  xmalloc (sizeof (struct job_record));
	job_details_point =
		(struct job_details *) xmalloc (sizeof (struct job_details));

	job_record_point->magic   = JOB_MAGIC;
	job_record_point->details = job_details_point;

	job_details_point->magic  = DETAILS_MAGIC;
	job_details_point->submit_time = time (NULL);
	job_details_point->procs_per_task = 1;

	if (list_append (job_list, job_record_point) == NULL)
		fatal ("create_job_record: unable to allocate memory");

	return job_record_point;
}


/* 
 * delete_job_details - delete a job's detail record and clear it's pointer
 * input: job_entry - pointer to job_record to clear the record of
 */
void 
delete_job_details (struct job_record *job_entry)
{
	if (job_entry->details == NULL) 
		return;

	if (job_entry->details->magic != DETAILS_MAGIC)
		fatal ("list_delete_job: passed invalid job details pointer");
	if (job_entry->details->job_script)
		xfree(job_entry->details->job_script);
	if (job_entry->details->req_nodes)
		xfree(job_entry->details->req_nodes);
	if (job_entry->details->req_node_bitmap)
		bit_free(job_entry->details->req_node_bitmap);
	if (job_entry->details->features)
		xfree(job_entry->details->features);
	xfree(job_entry->details);
	job_entry->details = NULL;
}

/* 
 * delete_job_record - delete record for job with specified job_id
 * input: job_id - job_id of the desired job
 * output: return 0 on success, errno otherwise
 * global: job_list - pointer to global job list
 *	last_job_update - time of last job table update
 * NOTE: Slow as currently constructed due to singly linked list and linear search, 
 *	which is forced by the common/list.c functions. We intend to purge entries 
 *	through purge_old_job() anyway.
 */
int 
delete_job_record (uint32_t job_id) 
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;

	last_job_update = time (NULL);
	job_record_iterator = list_iterator_create (job_list);		

	while ((job_record_point = 
				(struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->job_id != job_id)
			continue;

		delete_job_details (job_record_point);
		xfree (job_record_point);
		list_remove (job_record_iterator);
		break;
	}
	list_iterator_destroy (job_record_iterator);

	if (job_record_point == NULL) {
		error ("delete_job_record: attempt to delete non-existent job %u", 
				job_id);
		return ENOENT;
	} 
	return 0;
}


/* 
 * find_job_record - return a pointer to the job record with the given job_id
 * input: job_id - requested job's id
 * output: pointer to the job's record, NULL on error
 * global: job_list - global job list pointer
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */
struct job_record *
find_job_record(uint32_t job_id) 
{
	int i;

	/* First try to find via hash table */
	if (job_hash[job_id % MAX_JOB_COUNT] &&
			job_hash[job_id % MAX_JOB_COUNT]->job_id == job_id)
		return job_hash[job_id % MAX_JOB_COUNT];

	/* linear search of overflow hash table overflow */
	for (i=0; i<max_hash_over; i++) {
		if (job_hash_over[i] != NULL &&
				job_hash_over[i]->job_id == job_id)
			return job_hash_over[i];
	}

	return NULL;
}


/* dump_job_desc - dump the incoming job submit request message */
void
dump_job_desc(job_desc_msg_t * job_specs)
{
	char * dist = NULL;
	long job_id, min_procs, min_memory, min_tmp_disk, num_procs;
	long num_nodes,  procs_per_task, time_limit, priority, contiguous, shared;

	if (job_specs == NULL) 
		return;

	job_id = (job_specs->job_id != NO_VAL) ? job_specs->job_id : -1 ;
	debug3("JobDesc: user_id=%u job_id=%ld partition=%s, name=%s\n", 
		job_specs->user_id, job_id, 
		job_specs->partition, job_specs->name);

	min_procs = (job_specs->min_procs != NO_VAL) ? job_specs->min_procs : -1 ;
	min_memory = (job_specs->min_memory != NO_VAL) ? job_specs->min_memory : -1 ;
	min_tmp_disk = (job_specs->min_tmp_disk != NO_VAL) ? job_specs->min_tmp_disk : -1 ;
	debug3("   min_procs=%ld min_memory=%ld min_tmp_disk=%ld features=%s", 
		min_procs, min_memory, min_tmp_disk, job_specs->features);

	num_procs = (job_specs->num_procs != NO_VAL) ? job_specs->num_procs : -1 ;
	num_nodes = (job_specs->num_nodes != NO_VAL) ? job_specs->num_nodes : -1 ;
	debug3("   num_procs=%ld num_nodes=%ld req_nodes=%s", 
		num_procs, num_nodes, job_specs->req_nodes);

	time_limit = (job_specs->time_limit != NO_VAL) ? job_specs->time_limit : -1 ;
	priority = (job_specs->priority != NO_VAL) ? job_specs->priority : -1 ;
	contiguous = (job_specs->contiguous != (uint16_t) NO_VAL) ? job_specs->contiguous : -1 ;
	shared = (job_specs->shared != (uint16_t) NO_VAL) ? job_specs->shared : -1 ;
	debug3("   time_limit=%ld priority=%ld contiguous=%ld shared=%ld features=%s", 
		time_limit, priority, contiguous, shared, job_specs->features);

	procs_per_task = (job_specs->procs_per_task != NO_VAL) ? job_specs->procs_per_task : -1 ;
	if (job_specs->dist == DIST_BLOCK)
		dist = "BLOCK";
	else if (job_specs->dist == DIST_CYCLE)
		dist = "CYCLE";
	debug3("   procs_per_task=%ld dist=%s job_script=%s groups=%s", 
		procs_per_task, dist, job_specs->job_script, job_specs->groups);

/*	debug3("   partition_key=%?\n", job_specs->partition_key); */

}


/* 
 * init_job_conf - initialize the job configuration tables and values. 
 *	this should be called after creating node information, but 
 *	before creating any job entries.
 * output: return value - 0 if no error, otherwise an error code
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
int 
init_job_conf () 
{
	if (job_list == NULL) {
		job_count = 0;
		job_list = list_create (&list_delete_job);
		if (job_list == NULL)
			fatal ("init_job_conf: list_create can not allocate memory");
	}
	last_job_update = time (NULL);
	return 0;
}


/*
 * job_allocate - parse the suppied job specification, create job_records for it, 
 *	and allocate nodes for it. if the job can not be immediately allocated 
 *	nodes, EAGAIN will be returned
 * input: job_specs - job specifications
 *	new_job_id - location for storing new job's id
 *	node_list - location for storing new job's allocated nodes
 *	num_cpu_groups - location to store number of cpu groups
 *	cpus_per_node - location to store pointer to array of numbers of cpus on each node allocated
 *	cpu_count_reps - location to store pointer to array of numbers of consecutive nodes having
 *				 same cpu count
 * output: new_job_id - the job's ID
 *	num_cpu_groups - number of cpu groups (elements in cpus_per_node and cpu_count_reps)
 *	cpus_per_node - pointer to array of numbers of cpus on each node allocate
 *	cpu_count_reps - pointer to array of numbers of consecutive nodes having same cpu count
 *	node_list - list of nodes allocated to the job
 *	returns 0 on success, EINVAL if specification is invalid, 
 *		EAGAIN if higher priority jobs exist
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 * NOTE: If allocating nodes lx[0-7] to a job and those nodes have cpu counts of 
 *	 4, 4, 4, 4, 8, 8, 4, 4 then num_cpu_groups=3, cpus_per_node={4,8,4} and
 *	cpu_count_reps={4,2,2}
 */

int
immediate_job_launch (job_desc_msg_t * job_specs, uint32_t *new_job_id, char **node_list, 
		uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps, 
		int immediate , int will_run )
{
	return job_allocate (job_specs, new_job_id, node_list, 
				num_cpu_groups, cpus_per_node, cpu_count_reps, 
				true , false );
}

int 
will_job_run (job_desc_msg_t * job_specs, uint32_t *new_job_id, char **node_list, 
		uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps, 
		int immediate , int will_run )
{
	return job_allocate (job_specs, new_job_id, node_list, 
				num_cpu_groups, cpus_per_node, cpu_count_reps, 
				false , true );
}

int 
job_allocate (job_desc_msg_t  *job_specs, uint32_t *new_job_id, char **node_list, 
	uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t ** cpu_count_reps, 
	int immediate, int will_run)
{
	int error_code;
	struct job_record *job_ptr;

	*num_cpu_groups = 0;
	node_list[0] = NULL;
	cpus_per_node[0] = cpu_count_reps[0] = NULL;

	error_code = job_create (job_specs, new_job_id, 1, will_run, &job_ptr);
	if (error_code || will_run)
		return error_code;
	if (job_ptr == NULL)
		fatal ("job_allocate: allocated job %u lacks record", 
				new_job_id);
	last_job_update = time (NULL);

	if (immediate && top_priority(job_ptr) != 1) {
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time  = 0;
		return ESLURM_NOT_TOP_PRIORITY; 
	}

	error_code = select_nodes(job_ptr, will_run);
	if (error_code == ESLURM_NODES_BUSY) {
		if (immediate) {
			job_ptr->job_state = JOB_FAILED;
			job_ptr->end_time  = 0;
			return ESLURM_NODES_BUSY;
		}
		else 	/* job remains queued */
			return 0;
	}

	if (error_code) {	/* fundamental flaw in job request */
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time  = 0;
		return error_code; 
	}

	if (will_run) {			/* job would run now, flag job record destruction */
		job_ptr->job_state = JOB_FAILED;
		job_ptr->end_time  = 0;
	}

	node_list[0]      = job_ptr->nodes;
	*num_cpu_groups   = job_ptr->num_cpu_groups;
	cpus_per_node[0]  = job_ptr->cpus_per_node;
	cpu_count_reps[0] = job_ptr->cpu_count_reps;
	return 0;
}


/* 
 * job_cancel - cancel the specified job
 * input: job_id - id of the job to be cancelled
 * output: returns 0 on success, EINVAL if specification is invalid
 *	EAGAIN of job available for cancellation now 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int
job_cancel (uint32_t job_id) 
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info ("job_cancel: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->job_state == JOB_FAILED) ||
	    (job_ptr->job_state == JOB_COMPLETE) ||
	    (job_ptr->job_state == JOB_TIMEOUT))
		return ESLURM_ALREADY_DONE;

	if (job_ptr->job_state == JOB_PENDING) {
		last_job_update = time (NULL);
		job_ptr->job_state = JOB_FAILED;
		job_ptr->start_time = job_ptr->end_time = time(NULL);
		delete_job_details(job_ptr);
		info ("job_cancel of pending job %u successful", job_id);
		return 0;
	}

	if (job_ptr->job_state == JOB_STAGE_IN) {
		last_job_update = time (NULL);
		job_ptr->job_state = JOB_FAILED;
		deallocate_nodes (job_ptr->node_bitmap);
		delete_job_details(job_ptr);
		info ("job_cancel of job %u successful", job_id);
		return 0;
	} 

	info ("job_cancel: job %u can't be cancelled from state=%s", 
			job_id, job_state_string(job_ptr->job_state));
	return ESLURM_TRANSISTION_STATE_NO_UPDATE;

}

/*
 * job_create - create job_records with supplied jobs specifications.
 *	this performs only basic tests for request validity (access to partition, 
 *	nodes count in partition, and sufficient processors in partition).
 * input: job_specs - job specifications
 *	new_job_id - location for storing new job's id
 *	allocate - resource allocation request if set rather than job submit
 *	will_run - job is not to be created, test of validity only
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

int
job_create ( job_desc_msg_t *job_desc, uint32_t *new_job_id, int allocate, 
		int will_run, struct job_record **job_rec_ptr)
{
	int error_code, i; 
	struct part_record *part_ptr;
	bitstr_t *req_bitmap = NULL ;

	if ( (error_code = validate_job_desc ( job_desc , allocate ) ) )
		return error_code;

	/* find selected partition */
	if (job_desc->partition) {
		part_ptr = list_find_first (part_list, &list_find_part,
				job_desc->partition);
		if (part_ptr == NULL) {
			info ("job_create: invalid partition specified: %s",
					job_desc->partition);
			error_code = ESLURM_INVALID_PARTITION_SPECIFIED;
			return error_code ;
		}		
	}
	else {
		if (default_part_loc == NULL) {
			error ("job_create: default partition not set.");
			error_code = ESLURM_DEFAULT_PATITION_NOT_SET;
			return error_code ;
		}		
		part_ptr = default_part_loc;
	}
	if (job_desc->time_limit == NO_VAL)	/* Default time_limit is partition maximum */
		job_desc->time_limit = part_ptr->max_time;


	/* can this user access this partition */
	if (part_ptr->key && (is_key_valid (job_desc->partition_key) == 0)) {
		info ("job_create: job lacks key required of partition %s",
				part_ptr->name);
		error_code = ESLURM_JOB_MISSING_PARTITION_KEY ;
		return error_code;
	}			
	if (match_group (part_ptr->allow_groups, job_desc->groups) == 0) {
		info ("job_create: job lacks group required of partition %s",
				part_ptr->name);
		error_code = ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP;
		return error_code;
	}

	/* check if select partition has sufficient resources to satisfy request */
	if (job_desc->req_nodes) {	/* insure that selected nodes are in this partition */
		error_code = node_name2bitmap (job_desc->req_nodes, &req_bitmap);
		if (error_code == EINVAL)
			goto cleanup;
		if (error_code != 0) {
			error_code = EAGAIN;	/* no memory */
			goto cleanup;
		}		
		if (job_desc->contiguous)
			bit_fill_gaps (req_bitmap);
		if (bit_super_set (req_bitmap, part_ptr->node_bitmap) != 1) {
			info ("job_create: requested nodes %s not in partition %s",
					job_desc->req_nodes, part_ptr->name);
			error_code = ESLURM_REQUESTED_NODES_NOT_IN_PARTITION;
			goto cleanup;
		}		
		i = count_cpus (req_bitmap);
		if (i > job_desc->num_procs)
			job_desc->num_procs = i;
		i = bit_set_count (req_bitmap);
		if (i > job_desc->num_nodes)
			job_desc->num_nodes = i;
	}			
	if (job_desc->num_procs > part_ptr->total_cpus) {
		info ("job_create: too many cpus (%d) requested of partition %s(%d)",
				job_desc->num_procs, part_ptr->name, part_ptr->total_cpus);
		error_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
		goto cleanup;
	}			
	if ((job_desc->num_nodes > part_ptr->total_nodes) || 
			(job_desc->num_nodes > part_ptr->max_nodes)) {
		if (part_ptr->total_nodes > part_ptr->max_nodes)
			i = part_ptr->max_nodes;
		else
			i = part_ptr->total_nodes;
		info ("job_create: too many nodes (%d) requested of partition %s(%d)",
				job_desc->req_nodes, part_ptr->name, i);
		error_code = ESLURM_TOO_MANY_REQUESTED_NODES;
		goto cleanup;
	}

	if (will_run) {
		error_code = 0;
		goto cleanup;
	}

	if ( ( error_code = copy_job_desc_to_job_record ( job_desc , job_rec_ptr , part_ptr , 
							req_bitmap ) ) )  {
		error_code = ESLURM_ERROR_ON_DESC_TO_RECORD_COPY ;
		goto cleanup ;
	}

	if (part_ptr->shared == SHARED_FORCE)		/* shared=force */
		(*job_rec_ptr)->details->shared = 1;
	else if (((*job_rec_ptr)->details->shared != 1) || 
	         (part_ptr->shared == SHARED_NO))	/* user or partition want no sharing */
		(*job_rec_ptr)->details->shared = 0;

	*new_job_id = (*job_rec_ptr)->job_id;
	return SLURM_SUCCESS ;

	cleanup:
		if ( req_bitmap )
			bit_free (req_bitmap);
		return error_code ;
}
/* copy_job_desc_to_job_record - copy the job descriptor from the RPC structure 
 *	into the actual slurmctld job record */
int 
copy_job_desc_to_job_record ( job_desc_msg_t * job_desc , 
	struct job_record ** job_rec_ptr , struct part_record *part_ptr, 
	bitstr_t *req_bitmap )
{
	int error_code ;
	struct job_details *detail_ptr;
	struct job_record *job_ptr ;

	job_ptr = create_job_record (&error_code);
	if ( error_code )
		return error_code ;

	strncpy (job_ptr->partition, part_ptr->name, MAX_NAME_LEN);
	job_ptr->part_ptr = part_ptr;
	if (job_desc->job_id != NO_VAL)
		job_ptr->job_id = job_desc->job_id;
	else
		set_job_id(job_ptr);
	if (job_hash[job_ptr->job_id % MAX_JOB_COUNT]) 
		job_hash_over[max_hash_over++] = job_ptr;
	else
		job_hash[job_ptr->job_id % MAX_JOB_COUNT] = job_ptr;

	if (job_desc->name) {
		strncpy (job_ptr->name, job_desc->name , sizeof (job_ptr->name)) ;
	}
	job_ptr->user_id = (uid_t) job_desc->user_id;
	job_ptr->job_state = JOB_PENDING;
	job_ptr->time_limit = job_desc->time_limit;
	if (is_key_valid (job_desc->partition_key) && (job_desc->priority != NO_VAL))
		job_ptr->priority = job_desc->priority;
	else
		set_job_prio (job_ptr);

	detail_ptr = job_ptr->details;
	detail_ptr->num_procs = job_desc->num_procs;
	detail_ptr->num_nodes = job_desc->num_nodes;
	if (job_desc->req_nodes) {
		detail_ptr->req_nodes = xstrdup ( job_desc->req_nodes );
		detail_ptr->req_node_bitmap = req_bitmap;
	}
	if (job_desc->features)
		detail_ptr->features = xstrdup ( job_desc->features );
	if (job_desc->shared != NO_VAL)
		detail_ptr->shared = job_desc->shared;
	if (job_desc->contiguous != NO_VAL)
		detail_ptr->contiguous = job_desc->contiguous;
	if (job_desc->min_procs != NO_VAL)
		detail_ptr->min_procs = job_desc->min_procs;
	if (job_desc->min_memory != NO_VAL)
		detail_ptr->min_memory = job_desc->min_memory;
	if (job_desc->min_tmp_disk != NO_VAL)
		detail_ptr->min_tmp_disk = job_desc->min_tmp_disk;
	if (job_desc->dist != NO_VAL)
		detail_ptr->dist = (enum task_dist) job_desc->dist;
	if (job_desc->procs_per_task != NO_VAL)
		detail_ptr->procs_per_task = job_desc->procs_per_task;
	if (job_desc->job_script)
		detail_ptr->job_script = xstrdup ( job_desc->job_script );
	/* job_ptr->nodes		*leave as NULL pointer for now */
	/* job_ptr->start_time		*leave as NULL pointer for now */
	/* job_ptr->end_time		*leave as NULL pointer for now */
	/* detail_ptr->total_procs	*leave as NULL pointer for now */

	*job_rec_ptr = job_ptr;
	return 0;
}

/* validate_job_desc - validate that a job descriptor for job submit or 
 *	allocate has valid data, set values to defaults as required */
int 
validate_job_desc ( job_desc_msg_t * job_desc_msg , int allocate )
{
	if ((job_desc_msg->num_procs == NO_VAL) && (job_desc_msg->num_nodes == NO_VAL) && 
			(job_desc_msg->req_nodes == NULL)) {
		info ("job_create: job failed to specify ReqNodes, TotalNodes or TotalProcs");
		return ESLURM_JOB_MISSING_SIZE_SPECIFICATION;
	}
	if (allocate == SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 && 
	    job_desc_msg->job_script == NULL) {
		info ("job_create: job failed to specify Script");
		return ESLURM_JOB_SCRIPT_MISSING;
	}			
	if (job_desc_msg->user_id == NO_VAL) {
		info ("job_create: job failed to specify User");
		return ESLURM_USER_ID_MISSING;
	}	
	if (job_desc_msg->name && strlen(job_desc_msg->name) > MAX_NAME_LEN) {
		info ("job_create: job name %s too long", job_desc_msg->name);
		return ESLURM_JOB_NAME_TOO_LONG;
	}	
	if (job_desc_msg->contiguous == NO_VAL)
		job_desc_msg->contiguous = 0 ;
	if (job_desc_msg->shared == NO_VAL)
		job_desc_msg->shared =  0 ;
	if (job_desc_msg->dist == NO_VAL)
		job_desc_msg->shared = DIST_BLOCK ;

	if (job_desc_msg->job_id != NO_VAL && 
	    find_job_record ((uint32_t) job_desc_msg->job_id))
	{
		info  ("job_create: Duplicate job id %d", job_desc_msg->job_id);
		return ESLURM_DUPLICATE_JOB_ID;
	}
	if (job_desc_msg->num_procs == NO_VAL)
		job_desc_msg->num_procs = 1;		/* default cpu count of 1 */
	if (job_desc_msg->num_nodes == NO_VAL)
		job_desc_msg->num_nodes = 1;		/* default node count of 1 */
	if (job_desc_msg->min_memory == NO_VAL)
		job_desc_msg->min_memory = 1;		/* default is 1 MB memory per node */
	if (job_desc_msg->min_tmp_disk == NO_VAL)
		job_desc_msg->min_tmp_disk = 1;		/* default is 1 MB disk per node */
	if (job_desc_msg->shared == NO_VAL)
		job_desc_msg->shared = 0;		/* default is not shared nodes */
	if (job_desc_msg->dist == NO_VAL)
		job_desc_msg->dist = DIST_BLOCK;	/* default is block distribution */
	if (job_desc_msg->procs_per_task == NO_VAL)
		job_desc_msg->procs_per_task = 1;	/* default is 1 processor per task */
	else if (job_desc_msg->procs_per_task <= 0) {
		info ("job_create: Invalid procs_per_task");
		return ESLURM_INVALID_PROCS_PER_TASK;
	}

	if (job_desc_msg->min_procs == NO_VAL)
		job_desc_msg->min_procs = 1;		/* default is 1 processor per node */
	if (job_desc_msg->min_procs < job_desc_msg->procs_per_task) {
		info ("job_create: min_cpus(%d) < procs_per_task, reset to equal (%d)", 
		       job_desc_msg->min_procs, job_desc_msg->procs_per_task);
		job_desc_msg->min_procs = job_desc_msg->procs_per_task;
	}	
	return SLURM_SUCCESS ;
}

/* job_lock - lock the job information 
 * global: job_mutex - semaphore for the job table
 */
	void 
job_lock () 
{
	int error_code;
	error_code = pthread_mutex_lock (&job_mutex);
	if (error_code)
		fatal ("job_lock: pthread_mutex_lock error %d", error_code);

}


/* job_unlock - unlock the job information 
 * global: part_mutex - semaphore for the job table
 */
	void 
job_unlock () 
{
	int error_code;
	error_code = pthread_mutex_unlock (&job_mutex);
	if (error_code)
		fatal ("job_unlock: pthread_mutex_unlock error %d", error_code);
}

/* 
 * list_delete_job - delete a job record and its corresponding job_details,
 *	see common/list.h for documentation
 * input: job_entry - pointer to job_record to delete
 * global: job_list - pointer to global job list
 *	job_count - count of job list entries
 *	job_hash, job_hash_over, max_hash_over - hash table into job records
 */
void 
list_delete_job (void *job_entry)
{
	struct job_record *job_record_point;
	int i, j;

	job_record_point = (struct job_record *) job_entry;
	if (job_record_point == NULL)
		fatal ("list_delete_job: passed null job pointer");
	if (job_record_point->magic != JOB_MAGIC)
		fatal ("list_delete_job: passed invalid job pointer");

	if (job_hash[job_record_point->job_id] == job_record_point)
		job_hash[job_record_point->job_id] = NULL;
	else {
		for (i=0; i<max_hash_over; i++) {
			if (job_hash_over[i] != job_record_point)
				continue;
			for (j=i+1; j<max_hash_over; j++) {
				job_hash_over[j-1] = job_hash_over[j];
			}
			job_hash_over[--max_hash_over] = NULL;
			break;
		}
	}

	delete_job_details (job_record_point);

	if (job_record_point->nodes)
		xfree(job_record_point->nodes);
	if (job_record_point->node_bitmap)
		bit_free(job_record_point->node_bitmap);
	job_count--;
	xfree(job_record_point);
}


/*
 * list_find_job_id - find an entry in the job list,  
 *	see common/list.h for documentation, key is the job's id 
 * global- job_list - the global partition list
 */
int 
list_find_job_id (void *job_entry, void *key) 
{
	if (((struct job_record *) job_entry)->job_id == *((uint32_t *) key))
		return 1;
	return 0;
}


/*
 * list_find_job_old - find an entry in the job list,  
 *	see common/list.h for documentation, key is ignored 
 * global- job_list - the global partition list
 */
int 
list_find_job_old (void *job_entry, void *key) 
{
	time_t min_age;

	min_age = time(NULL) - MIN_JOB_AGE;

	if (((struct job_record *) job_entry)->job_state != JOB_COMPLETE) 
		return 0;
	if (((struct job_record *) job_entry)->end_time  <  min_age)
		return 0;

	return 1;
}


/* 
 * pack_all_jobs - dump all job information for all jobs in 
 *	machine independent form (for network transmission)
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if job records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change unpack_job_desc() in common/slurm_protocol_pack.c whenever the
 *	data format changes
 */
void 
pack_all_jobs (char **buffer_ptr, int *buffer_size, time_t * update_time) 
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	int buf_len, buffer_allocated, buffer_offset = 0;
	char *buffer;
	void *buf_ptr;
	int jobs_packed ;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	if (*update_time == last_job_update)
		return;

	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	job_record_iterator = list_iterator_create (job_list);		

	/* write message body header : version and time */
	/* put in a place holder job record count of 0 for now */
	jobs_packed = 0 ;
	pack32  ((uint32_t) jobs_packed, &buf_ptr, &buf_len);
	pack32  ((uint32_t) last_job_update, &buf_ptr, &buf_len);

	/* write individual job records */
	while ((job_record_point = 
				(struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("dump_all_job: job integrity is bad");

		pack_job(job_record_point, &buf_ptr, &buf_len);
		if (buf_len > BUF_SIZE) 
		{
			jobs_packed ++ ;
			continue;
		}
		buffer_allocated += (BUF_SIZE*16);
		buf_len += (BUF_SIZE*16);
		buffer_offset = (char *)buf_ptr - buffer;
		xrealloc(buffer, buffer_allocated);
		buf_ptr = buffer + buffer_offset;
		jobs_packed ++ ;
	}		

	list_iterator_destroy (job_record_iterator);
	buffer_offset = (char *)buf_ptr - buffer;
	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_part_update;

	/* put in the real record count in the message body header */	
	buf_ptr = buffer;
	buf_len = buffer_allocated;
	pack32  ((uint32_t) jobs_packed, &buf_ptr, &buf_len);
}


/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission)
 * input:  dump_job_ptr - pointer to job for which information is requested
 *	buf_ptr - buffer for job information 
 *	buf_len - byte size of buffer
 * output: buf_ptr - advanced to end of data written
 *	buf_len - byte size remaining in buffer
 * NOTE: change unpack_job_desc() in common/slurm_protocol_pack.c whenever the
 *	 data format changes
 * NOTE: the caller must insure that the buffer is sufficiently large to hold 
 *	 the data being written (space remaining at least BUF_SIZE)
 */
	void 
pack_job (struct job_record *dump_job_ptr, void **buf_ptr, int *buf_len) 
{
	char tmp_str[MAX_STR_PACK];
	struct job_details *detail_ptr;

	pack32  (dump_job_ptr->job_id, buf_ptr, buf_len);
	pack32  (dump_job_ptr->user_id, buf_ptr, buf_len);
	pack16  ((uint16_t) dump_job_ptr->job_state, buf_ptr, buf_len);
	pack32  (dump_job_ptr->time_limit, buf_ptr, buf_len);

	pack32  ((uint32_t) dump_job_ptr->start_time, buf_ptr, buf_len);
	pack32  ((uint32_t) dump_job_ptr->end_time, buf_ptr, buf_len);
	pack32  (dump_job_ptr->priority, buf_ptr, buf_len);

	packstr (dump_job_ptr->nodes, buf_ptr, buf_len);
	packstr (dump_job_ptr->partition, buf_ptr, buf_len);
	packstr (dump_job_ptr->name, buf_ptr, buf_len);
	if (dump_job_ptr->node_bitmap) {
		(void) bit_fmt(tmp_str, MAX_STR_PACK, 
			       dump_job_ptr->node_bitmap);
		packstr (tmp_str, buf_ptr, buf_len);
	}
	else 
		packstr (NULL, buf_ptr, buf_len);

	detail_ptr = dump_job_ptr->details;
	if (detail_ptr &&  
			dump_job_ptr->job_state == JOB_PENDING) {
		if (detail_ptr->magic != DETAILS_MAGIC)
			fatal ("dump_all_job: job detail integrity is bad");
		pack32  ((uint32_t) detail_ptr->num_procs, buf_ptr, buf_len);
		pack32  ((uint32_t) detail_ptr->num_nodes, buf_ptr, buf_len);
		pack16  ((uint16_t) detail_ptr->shared, buf_ptr, buf_len);
		pack16  ((uint16_t) detail_ptr->contiguous, buf_ptr, buf_len);

		pack32  ((uint32_t) detail_ptr->min_procs, buf_ptr, buf_len);
		pack32  ((uint32_t) detail_ptr->min_memory, buf_ptr, buf_len);
		pack32  ((uint32_t) detail_ptr->min_tmp_disk, buf_ptr, buf_len);

		if (detail_ptr->req_nodes == NULL ||
				strlen (detail_ptr->req_nodes) < MAX_STR_PACK)
			packstr (detail_ptr->req_nodes, buf_ptr, buf_len);
		else {
			strncpy(tmp_str, detail_ptr->req_nodes, MAX_STR_PACK);
			tmp_str[MAX_STR_PACK-1] = (char) NULL;
			packstr (tmp_str, buf_ptr, buf_len);
		}

		if (detail_ptr->req_node_bitmap) {
			(void) bit_fmt(tmp_str, MAX_STR_PACK, 
				       detail_ptr->req_node_bitmap);
			packstr (tmp_str, buf_ptr, buf_len);
		}
		else 
			packstr (NULL, buf_ptr, buf_len);

		if (detail_ptr->features == NULL ||
				strlen (detail_ptr->features) < MAX_STR_PACK)
			packstr (detail_ptr->features, buf_ptr, buf_len);
		else {
			strncpy(tmp_str, detail_ptr->features, MAX_STR_PACK);
			tmp_str[MAX_STR_PACK-1] = (char) NULL;
			packstr (tmp_str, buf_ptr, buf_len);
		}

		if (detail_ptr->job_script == NULL ||
				strlen (detail_ptr->job_script) < MAX_STR_PACK)
			packstr (detail_ptr->job_script, buf_ptr, buf_len);
		else {
			strncpy(tmp_str, detail_ptr->job_script, MAX_STR_PACK);
			tmp_str[MAX_STR_PACK-1] = (char) NULL;
			packstr (tmp_str, buf_ptr, buf_len);
		}
	}
	else {
		pack32  ((uint32_t) 0, buf_ptr, buf_len);
		pack32  ((uint32_t) 0, buf_ptr, buf_len);
		pack16  ((uint16_t) 0, buf_ptr, buf_len);
		pack16  ((uint16_t) 0, buf_ptr, buf_len);

		pack32  ((uint32_t) 0, buf_ptr, buf_len);
		pack32  ((uint32_t) 0, buf_ptr, buf_len);
		pack32  ((uint32_t) 0, buf_ptr, buf_len);

		packstr (NULL, buf_ptr, buf_len);
		packstr (NULL, buf_ptr, buf_len);
		packstr (NULL, buf_ptr, buf_len);
		packstr (NULL, buf_ptr, buf_len);
	}
}


/* 
 * parse_job_specs - pick the appropriate fields out of a job request specification
 * input: job_specs - string containing the specification
 *        req_features, etc. - pointers to storage for the specifications
 * output: req_features, etc. - the job's specifications
 *         returns 0 if no error, errno otherwise
 * NOTE: the calling function must xfree memory at req_features[0], req_node_list[0],
 *	job_name[0], req_group[0], and req_partition[0]
 */
int 
parse_job_specs (char *job_specs, char **req_features, char **req_node_list,
		char **job_name, char **req_group, char **req_partition,
		int *contiguous, int *req_cpus, int *req_nodes,
		int *min_cpus, int *min_memory, int *min_tmp_disk, int *key,
		int *shared, int *dist, char **script, int *time_limit, 
		int *procs_per_task, long *job_id, int *priority, 
		int *user_id) {
	int bad_index, error_code, i;
	char *temp_specs, *contiguous_str, *dist_str, *shared_str;

	req_features[0] = req_node_list[0] = req_group[0] = NULL;
	req_partition[0] = job_name[0] = script[0] = NULL;
	contiguous_str = shared_str = dist_str = NULL;
	*contiguous = *req_cpus = *req_nodes = *min_cpus = NO_VAL;
	*min_memory = *min_tmp_disk = *time_limit = NO_VAL;
	*dist = *key = *shared = *procs_per_task = *user_id = NO_VAL;
	*job_id = *priority = NO_VAL;

	temp_specs = xmalloc (strlen (job_specs) + 1);
	strcpy (temp_specs, job_specs);

	error_code = slurm_parser(temp_specs,
			"Contiguous=", 's', &contiguous_str, 
			"Distribution=", 's', &dist_str, 
			"Features=", 's', req_features, 
			"Groups=", 's', req_group, 
			"JobId=", 'l', job_id, 
			"JobName=", 's', job_name, 
			"Key=", 'd', key, 
			"MinProcs=", 'd', min_cpus, 
			"MinRealMemory=", 'd', min_memory, 
			"MinTmpDisk=", 'd', min_tmp_disk, 
			"Partition=", 's', req_partition, 
			"Priority=", 'd', priority, 
			"ProcsPerTask=", 'd', procs_per_task, 
			"ReqNodes=", 's', req_node_list, 
			"Script=", 's', script, 
			"Shared=", 's', shared_str, 
			"TimeLimit=", 'd', time_limit, 
			"TotalNodes=", 'd', req_nodes, 
			"TotalProcs=", 'd', req_cpus, 
			"User=", 'd', user_id,
			"END");

	if (error_code)
		goto cleanup;

	if (contiguous_str) {
		i = yes_or_no (contiguous_str);
		if (i == -1) {
			error ("parse_job_specs: invalid Contiguous value");
			goto cleanup;
		}
		*contiguous = i;
	}

	if (dist_str) {
		i = (int) block_or_cycle (dist_str);
		if (i == -1) {
			error ("parse_job_specs: invalid Distribution value");
			goto cleanup;
		}
		*dist = i;
	}

	if (shared_str) {
		i = yes_or_no (shared_str);
		if (i == -1) {
			error ("parse_job_specs: invalid Shared value");
			goto cleanup;
		}
		*shared = i;
	}

	bad_index = -1;
	for (i = 0; i < strlen (temp_specs); i++) {
		if (isspace ((int) temp_specs[i]) || (temp_specs[i] == '\n'))
			continue;
		bad_index = i;
		break;
	}			

	if (bad_index != -1) {
		error ("parse_job_specs: bad job specification input: %s",
				&temp_specs[bad_index]);
		error_code = EINVAL;
	}			

	if (error_code)
		goto cleanup;

	xfree (temp_specs);
	if (contiguous_str)
		xfree (contiguous_str);
	if (dist_str)
		xfree (dist_str);
	if (shared_str)
		xfree (shared_str);
	return error_code;

cleanup:
	xfree (temp_specs);
	if (contiguous_str)
		xfree (contiguous_str);
	if (job_name[0])
		xfree (job_name[0]);
	if (req_features[0])
		xfree (req_features[0]);
	if (req_node_list[0])
		xfree (req_node_list[0]);
	if (req_group[0])
		xfree (req_group[0]);
	if (req_partition[0])
		xfree (req_partition[0]);
	if (script[0])
		xfree (script[0]);
	if (contiguous_str)
		xfree (contiguous_str);
	if (dist_str)
		xfree (dist_str);
	if (shared_str)
		xfree (shared_str);
	req_features[0] = req_node_list[0] = req_group[0] = NULL;
	req_partition[0] = job_name[0] = script[0] = NULL;
	return error_code;
}


/*
 * purge_old_job - purge old job records. 
 *	the jobs must have completed at least MIN_JOB_AGE minutes ago
 * global: job_list - global job table
 *	last_job_update - time of last job table update
 */
void
purge_old_job (void) 
{
	int i;

	i = list_delete_all (job_list, &list_find_job_old, NULL);
	if (i) {
		info ("purge_old_job: purged %d old job records");
		last_job_update = time (NULL);
	}
}


/* 
 * reset_job_bitmaps - reestablish bitmaps for existing jobs. 
 *	this should be called after rebuilding node information, 
 *	but before using any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
void 
reset_job_bitmaps () 
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;

	if (job_list == NULL)
		fatal ("init_job_conf: list_create can not allocate memory");

	job_record_iterator = list_iterator_create (job_list);		
	while ((job_record_point = 
				(struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("dump_all_job: job integrity is bad");
		if (job_record_point->node_bitmap)
			bit_free(job_record_point->node_bitmap);
		if (job_record_point->nodes)
			node_name2bitmap (job_record_point->nodes,
					&job_record_point->node_bitmap);

		if (job_record_point->details == NULL)
			continue;
		if (job_record_point->details->req_node_bitmap)
			bit_free(job_record_point->details->req_node_bitmap);
		if (job_record_point->details->req_nodes)
			node_name2bitmap (job_record_point->details->req_nodes,
					&job_record_point->details->req_node_bitmap);
	}

	list_iterator_destroy (job_record_iterator);
	last_job_update = time (NULL);
}


/*
 * set_job_id - set a default job_id, insure that it is unique
 * input: job_ptr - pointer to the job_record
 */
void
set_job_id (struct job_record *job_ptr)
{
	static uint32_t id_sequence = FIRST_JOB_ID;
#ifdef HUGE_JOB_ID
	uint32_t new_id;
#endif

	if ((job_ptr == NULL) || 
			(job_ptr->magic != JOB_MAGIC)) 
		fatal ("set_job_id: invalid job_ptr");
	if ((job_ptr->partition == NULL) || (strlen(job_ptr->partition) == 0))
		fatal ("set_job_id: partition not set");

	/* Include below code only if fear of rolling over 32 bit job IDs */
#ifdef HUGE_JOB_ID
	while (1) {
		new_id = id_sequence++;
		if (find_job_record(new_id) == NULL) {
			job_ptr->job_id = new_id;
			break;
		}
	}
#else
	job_ptr->job_id = id_sequence++;
#endif
}


/*
 * set_job_prio - set a default job priority
 * input: job_ptr - pointer to the job_record
 * NOTE: this is a simple prototype, we need to re-establish value on restart
 */
void
set_job_prio (struct job_record *job_ptr)
{
	static int default_prio = 100000;

	if ((job_ptr == NULL) || 
			(job_ptr->magic != JOB_MAGIC)) 
		fatal ("set_job_prio: invalid job_ptr");
	job_ptr->priority = default_prio--;
}


/* 
 * top_priority - determine if any other job for this partition has a higher priority
 *	than specified job
 * input: job_ptr - pointer to selected partition
 * output: returns 1 if selected job has highest priority, 0 otherwise
 */
int
top_priority (struct job_record *job_ptr) {
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	int top;

	top = 1;	/* assume top priority until found otherwise */
	job_record_iterator = list_iterator_create (job_list);		
	while ((job_record_point = 
				(struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("top_priority: job integrity is bad");
		if (job_record_point == job_ptr)
			continue;
		if (job_record_point->job_state != JOB_PENDING)
			continue;
		if (job_record_point->priority >  job_ptr->priority &&
				job_record_point->part_ptr == job_ptr->part_ptr) {
			top = 0;
			break;
		}
	}		

	list_iterator_destroy (job_record_iterator);
	return top;
}


/*
 * update_job - update a job's parameters per the supplied specifications
 * output: returns 0 on success, otherwise an error code from common/slurm_protocol_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
int 
update_job (job_desc_msg_t * job_specs) 
{
	int error_code;
	struct job_record *job_ptr;
	struct job_details *detail_ptr;
	struct part_record *tmp_part_ptr;
	bitstr_t *req_bitmap = NULL ;

	job_ptr = find_job_record (job_specs -> job_id);
	if (job_ptr == NULL) {
		error ("update_job: job_id %u does not exist.", job_specs -> job_id);
		return ESLURM_INVALID_JOB_ID;
	}			
	detail_ptr = job_ptr->details;
	last_job_update = time (NULL);

	if (job_specs -> time_limit != NO_VAL) {
		job_ptr -> time_limit = job_specs -> time_limit;
		info ("update_job: setting time_limit to %u for job_id %u",
				job_specs -> time_limit, job_specs -> job_id);
	}

	if (job_specs -> priority != NO_VAL) {
		job_ptr -> priority = job_specs -> priority;
		info ("update_job: setting priority to %u for job_id %u",
				job_specs -> priority, job_specs -> job_id);
	}

	if (job_specs -> min_procs != NO_VAL && detail_ptr) {
		detail_ptr -> min_procs = job_specs -> min_procs;
		info ("update_job: setting min_procs to %u for job_id %u",
				job_specs -> min_procs, job_specs -> job_id);
	}

	if (job_specs -> min_memory != NO_VAL && detail_ptr) {
		detail_ptr -> min_memory = job_specs -> min_memory;
		info ("update_job: setting min_memory to %u for job_id %u",
				job_specs -> min_memory, job_specs -> job_id);
	}

	if (job_specs -> min_tmp_disk != NO_VAL && detail_ptr) {
		detail_ptr -> min_tmp_disk = job_specs -> min_tmp_disk;
		info ("update_job: setting min_tmp_disk to %u for job_id %u",
				job_specs -> min_tmp_disk, job_specs -> job_id);
	}

	if (job_specs -> num_procs != NO_VAL && detail_ptr) {
		detail_ptr -> num_procs = job_specs -> num_procs;
		info ("update_job: setting num_procs to %u for job_id %u",
				job_specs -> num_procs, job_specs -> job_id);
	}

	if (job_specs -> num_nodes != NO_VAL && detail_ptr) {
		detail_ptr -> num_nodes = job_specs -> num_nodes;
		info ("update_job: setting num_nodes to %u for job_id %u",
				job_specs -> num_nodes, job_specs -> job_id);
	}

	if (job_specs -> shared != (uint16_t) NO_VAL && detail_ptr) {
		detail_ptr -> shared = job_specs -> shared;
		info ("update_job: setting shared to %u for job_id %u",
				job_specs -> shared, job_specs -> job_id);
	}

	if (job_specs -> contiguous != (uint16_t) NO_VAL && detail_ptr) {
		detail_ptr -> contiguous = job_specs -> contiguous;
		info ("update_job: setting contiguous to %u for job_id %u",
				job_specs -> contiguous, job_specs -> job_id);
	}

	if (job_specs -> features && detail_ptr) {
		if (detail_ptr -> features)
			xfree (detail_ptr -> features);
		detail_ptr -> features = job_specs -> features;
		info ("update_job: setting features to %s for job_id %u",
				job_specs -> features, job_specs -> job_id);
		job_specs -> features = NULL;
	}

	if (job_specs -> name) {
		strncpy(job_ptr -> name, job_specs -> name, MAX_NAME_LEN);
		info ("update_job: setting name to %s for job_id %u",
				job_specs -> name, job_specs -> job_id);
		job_specs -> name = NULL;
	}

	if (job_specs -> partition) {
		tmp_part_ptr = find_part_record (job_specs -> partition);
		if (tmp_part_ptr == NULL)
			return ESLURM_INVALID_PARTITION;
		strncpy(job_ptr -> partition, job_specs -> partition, MAX_NAME_LEN);
		job_ptr -> part_ptr = tmp_part_ptr;
		info ("update_job: setting partition to %s for job_id %u",
				job_specs -> partition, job_specs -> job_id);
		job_specs -> partition = NULL;
	}

	if (job_specs -> req_nodes && detail_ptr) {
		error_code = node_name2bitmap (job_specs->req_nodes, &req_bitmap);
		if (error_code == EINVAL) {
			if ( req_bitmap )
				bit_free (req_bitmap);
			return ESLURM_INVALID_NODE_NAMES;
		}

		if (detail_ptr -> req_nodes)
			xfree (detail_ptr -> req_nodes);
		detail_ptr -> req_nodes = job_specs -> req_nodes;
		if (detail_ptr->req_node_bitmap)
			bit_free (detail_ptr->req_node_bitmap);
		detail_ptr->req_node_bitmap = req_bitmap;
		info ("update_job: setting req_nodes to %s for job_id %u",
				job_specs -> req_nodes, job_specs -> job_id);
		job_specs -> req_nodes = NULL;
	}

	return SLURM_PROTOCOL_SUCCESS;
}
