/* 
 * job_mgr.c - manage the job information of slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

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
#include "slurm.h"

#define BUF_SIZE 1024
#define MAX_STR_PACK 128

int job_count;				/* job's in the system */
List job_list = NULL;			/* job_record list */
time_t last_job_update;			/* time of last update to job records */
static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;	/* lock for job info */
char *job_state_string[] =
	{ "PENDING", "STAGE_IN", "RUNNING", "STAGE_OUT", "COMPLETED", "FAILED", "TIME_OUT", "END" };

void list_delete_job (void *job_entry);
int list_find_job_id (void *job_entry, void *key);
int list_find_job_old (void *job_entry, void *key);
void set_job_id (struct job_record *job_ptr);
void set_job_prio (struct job_record *job_ptr);

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) 
{
	int dump_size, error_code, error_count = 0, i;
	time_t update_time = (time_t) NULL;
	struct job_record * job_rec;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	char *dump, tmp_id[50];
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
		printf ("ERROR:create_job_record failure %d\n", error_code);
		error_count++;
		exit(error_count);
	}
	strcpy (job_rec->name, "Name1");
	strcpy (job_rec->partition, "batch");
	job_rec->details->job_script = xmalloc(20);
	strcpy (job_rec->details->job_script, "/bin/hostname");
	job_rec->details->num_nodes = 1;
	job_rec->details->num_procs = 1;
	set_job_id(job_rec);
	set_job_prio(job_rec);
	strcpy (tmp_id, job_rec->job_id);

	for (i=1; i<=4; i++) {
		job_rec = create_job_record (&error_code);
		if ((job_rec == NULL) || error_code) {
			printf ("ERROR:create_job_record failure %d\n",error_code);
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
		printf ("found job %s, script=%s\n", 
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
	if (job_entry->details->node_list)
		xfree(job_entry->details->node_list);
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
 */
int 
delete_job_record (char *job_id) 
{
	int i;

	last_job_update = time (NULL);

	i = list_delete_all (job_list, &list_find_job_id, job_id);
	if (i == 0) {
		error ("delete_job_record: attempt to delete non-existent job %s", 
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
 */
struct job_record *
find_job_record(char *job_id) 
{
	struct job_record *job_ptr;

	job_ptr = list_find_first (job_list, &list_find_job_id, job_id);
	if ((job_ptr != NULL) && (job_ptr->magic != JOB_MAGIC))
		fatal ("job_list invalid");
	return job_ptr;
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
 * output: new_job_id - the job's ID
 *	node_list - list of nodes allocated to the job
 *	returns 0 on success, EINVAL if specification is invalid, 
 *		EAGAIN if higher priority jobs exist
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 *	last_job_update - time of last job table update
 * NOTE: the calling program must xfree the memory pointed to by new_job_id 
 *	and node_list
 */
int
job_allocate (char *job_specs, char **new_job_id, char **node_list)
{
	int error_code, i;
	struct job_record *job_ptr;

	new_job_id[0] = node_list[0] = NULL;

	error_code = job_create (job_specs, new_job_id);
	if (error_code)
		return error_code;
	job_ptr = find_job_record (new_job_id[0]);
	if (job_ptr == NULL)
		fatal ("job_allocate allocated job %s lacks record", 
			new_job_id[0]);

/*	if (top_priority(new_job_id[0]) != 0)
		return EAGAIN; */
	error_code = select_nodes(job_ptr);
	if (error_code)
		return error_code;
	last_job_update = time (NULL);
	i = strlen(job_ptr->nodes) + 1;
	node_list[0] = xmalloc(i);
	strcpy(node_list[0], job_ptr->nodes);
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
job_cancel (char * job_id) 
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info ("job_cancel: invalid job id %s", job_id);
		return EINVAL;
	}
	if (job_ptr->job_state == JOB_PENDING) {
		last_job_update = time (NULL);
		job_ptr->job_state = JOB_FAILED;
		job_ptr->start_time = job_ptr->end_time = time(NULL);
		delete_job_details(job_ptr);
		info ("job_cancel of pending job %s successful", job_id);
		return 0;
	}

	if (job_ptr->job_state == JOB_STAGE_IN) {
		last_job_update = time (NULL);
		job_ptr->job_state = JOB_FAILED;
		deallocate_nodes (job_ptr->node_bitmap);
		delete_job_details(job_ptr);
		info ("job_cancel of job %s successful", job_id);
		return 0;
	} 

	info ("job_cancel: job %s can't be cancelled from state=%s", 
		job_id, job_state_string[job_ptr->job_state]);
	return EAGAIN;

}


/*
 * job_create - parse the suppied job specification and create job_records for it
 * input: job_specs - job specifications
 *	new_job_id - location for storing new job's id
 * output: new_job_id - the job's ID
 *	returns 0 on success, EINVAL if specification is invalid
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 * NOTE: the calling program must xfree the memory pointed to by new_job_id
 */
int
job_create (char *job_specs, char **new_job_id)
{
	char *req_features, *req_node_list, *job_name, *req_group;
	char *req_partition, *script, *job_id;
	int contiguous, req_cpus, req_nodes, min_cpus, min_memory;
	int i, min_tmp_disk, time_limit, procs_per_task, user_id;
	int error_code, dist, key, shared;
	struct part_record *part_ptr;
	struct job_record *job_ptr;
	struct job_details *detail_ptr;
	int priority;
	bitstr_t *req_bitmap;

	new_job_id[0] = NULL;
	req_features = req_node_list = job_name = req_group = NULL;
	job_id = req_partition = script = NULL;
	req_bitmap = NULL;
	contiguous = dist = req_cpus = req_nodes = min_cpus = NO_VAL;
	min_memory = min_tmp_disk = time_limit = procs_per_task = NO_VAL;
	key = shared = user_id = NO_VAL;
	priority = NO_VAL;

	/* setup and basic parsing */
	error_code =
		parse_job_specs (job_specs, &req_features, &req_node_list,
				 &job_name, &req_group, &req_partition,
				 &contiguous, &req_cpus, &req_nodes,
				 &min_cpus, &min_memory, &min_tmp_disk, &key,
				 &shared, &dist, &script, &time_limit, 
				 &procs_per_task, &job_id, &priority, &user_id);
	if (error_code != 0) {
		error_code = EINVAL;	/* permanent error, invalid parsing */
		error ("job_create: parsing failure on %s", job_specs);
		goto cleanup;
	}			
	if ((req_cpus == NO_VAL) && (req_nodes == NO_VAL) && 
	    (req_node_list == NULL)) {
		info ("job_create: job failed to specify ReqNodes, TotalNodes or TotalProcs");
		error_code = EINVAL;
		goto cleanup;
	}
	if (script == NULL) {
		info ("job_create: job failed to specify Script");
		error_code = EINVAL;
		goto cleanup;
	}			
	if (job_id && (strlen(job_id) >= MAX_ID_LEN)) {
		info ("job_create: JobId specified %s is too long", job_id);
		error_code = EINVAL;
		goto cleanup;
	}
	if (user_id == NO_VAL) {
		info ("job_create: job failed to specify User");
		error_code = EINVAL;
		goto cleanup;
	}	
	if (job_name && strlen(job_name) > MAX_NAME_LEN) {
		info ("job_create: job name %s too long", job_name);
		error_code = EINVAL;
		goto cleanup;
	}	
	if (contiguous == NO_VAL)
		contiguous = 0;		/* default not contiguous */
	if (req_cpus == NO_VAL)
		req_cpus = 1;		/* default cpu count of 1 */
	if (req_nodes == NO_VAL)
		req_nodes = 1;		/* default node count of 1 */
	if (min_memory == NO_VAL)
		min_memory = 1;		/* default is 1 MB memory per node */
	if (min_tmp_disk == NO_VAL)
		min_tmp_disk = 1;	/* default is 1 MB disk per node */
	if (shared == NO_VAL)
		shared = 0;		/* default is not shared nodes */
	if (dist == NO_VAL)
		dist = DIST_BLOCK;	/* default is block distribution */
	if (procs_per_task == NO_VAL)
		procs_per_task = 1;	/* default is 1 processor per task */
	else if (procs_per_task <= 0) {
		info ("job_create: Invalid procs_per_task");
		error_code = EINVAL;
		goto cleanup;
	}

	if (min_cpus == NO_VAL)
		min_cpus = 1;		/* default is 1 processor per node */
	if (min_cpus < procs_per_task) {
		info ("job_create: min_cpus < procs_per_task, reset to equal");
		min_cpus = procs_per_task;
	}	


	/* find selected partition */
	if (req_partition) {
		part_ptr = list_find_first (part_list, &list_find_part,
					 req_partition);
		if (part_ptr == NULL) {
			info ("job_create: invalid partition specified: %s",
				 req_partition);
			error_code = EINVAL;
			goto cleanup;
		}		
		xfree (req_partition);
	}
	else {
		if (default_part_loc == NULL) {
			error ("job_create: default partition not set.");
			error_code = EINVAL;
			goto cleanup;
		}		
		part_ptr = default_part_loc;
	}
	if (time_limit == NO_VAL)	/* Default time_limit is partition maximum */
		time_limit = part_ptr->max_time;


	/* can this user access this partition */
	if (part_ptr->key && (is_key_valid (key) == 0)) {
		info ("job_create: job lacks key required of partition %s",
			 part_ptr->name);
		error_code = EINVAL;
		goto cleanup;
	}			
	if (match_group (part_ptr->allow_groups, req_group) == 0) {
		info ("job_create: job lacks group required of partition %s",
			 part_ptr->name);
		error_code = EINVAL;
		goto cleanup;
	}
	if (req_group)
		xfree(req_group);


	/* check if select partition has sufficient resources to satisfy request */
	if (req_node_list) {	/* insure that selected nodes are in this partition */
		error_code = node_name2bitmap (req_node_list, &req_bitmap);
		if (error_code == EINVAL)
			goto cleanup;
		if (error_code != 0) {
			error_code = EAGAIN;	/* no memory */
			goto cleanup;
		}		
		if (contiguous == 1)
			bit_fill_gaps (req_bitmap);
		if (bit_super_set (req_bitmap, part_ptr->node_bitmap) != 1) {
			info ("job_create: requested nodes %s not in partition %s",
				req_node_list, part_ptr->name);
			error_code = EINVAL;
			goto cleanup;
		}		
		i = count_cpus (req_bitmap);
		if (i > req_cpus)
			req_cpus = i;
		i = bit_set_count (req_bitmap);
		if (i > req_nodes)
			req_nodes = i;
	}			
	if (req_cpus > part_ptr->total_cpus) {
		info ("job_create: too many cpus (%d) requested of partition %s(%d)",
			req_cpus, part_ptr->name, part_ptr->total_cpus);
		error_code = EINVAL;
		goto cleanup;
	}			
	if ((req_nodes > part_ptr->total_nodes) || 
	    (req_nodes > part_ptr->max_nodes)) {
		if (part_ptr->total_nodes > part_ptr->max_nodes)
			i = part_ptr->max_nodes;
		else
			i = part_ptr->total_nodes;
		info ("job_create: too many nodes (%d) requested of partition %s(%d)",
			 req_nodes, part_ptr->name, i);
		error_code = EINVAL;
		goto cleanup;
	}			
	if (part_ptr->shared == 2)	/* shared=force */
		shared = 1;
	else if ((shared != 1) || (part_ptr->shared == 0)) /* user or partition want no sharing */
		shared = 0;

	job_ptr = create_job_record (&error_code);
	if ((job_ptr == NULL) || error_code)
		goto cleanup;

	strncpy (job_ptr->partition, part_ptr->name, MAX_NAME_LEN);
	if (job_id) {
		strncpy (job_ptr->job_id, job_id, MAX_ID_LEN);
		xfree (job_id);
		job_id = NULL;
	}
	else
		set_job_id(job_ptr);
	if (job_name) {
		strcpy (job_ptr->name, job_name);
		xfree (job_name);
	}
	job_ptr->user_id = (uid_t) user_id;
	job_ptr->job_state = JOB_PENDING;
	job_ptr->time_limit = time_limit;
	if (key && is_key_valid (key) && (priority != NO_VAL))
		job_ptr->priority = priority;
	else
		set_job_prio (job_ptr);

	detail_ptr = job_ptr->details;
	detail_ptr->num_procs = req_cpus;
	detail_ptr->num_nodes = req_nodes;
	if (req_node_list) {
		detail_ptr->req_nodes = req_node_list;
		detail_ptr->req_node_bitmap = req_bitmap;

	}
	if (req_features)
		detail_ptr->features = req_features;
	detail_ptr->shared = shared;
	detail_ptr->contiguous = contiguous;
	detail_ptr->min_procs = min_cpus;
	detail_ptr->min_memory = min_memory;
	detail_ptr->min_tmp_disk = min_tmp_disk;
	detail_ptr->dist = (enum task_dist) dist;
	detail_ptr->job_script = script;
	detail_ptr->procs_per_task = procs_per_task;
	/* job_ptr->nodes		*leave as NULL pointer for now */
	/* job_ptr->start_time		*leave as NULL pointer for now */
	/* job_ptr->end_time		*leave as NULL pointer for now */
	/* detail_ptr->total_procs	*leave as NULL pointer for now */

	new_job_id[0] = xmalloc(strlen(job_ptr->job_id) + 1);
	strcpy(new_job_id[0], job_ptr->job_id);
	return 0;

      cleanup:
	if (job_id)
		xfree (job_id);
	if (job_name)
		xfree (job_name);
	if (req_bitmap)
		bit_free (req_bitmap);
	if (req_group)
		xfree (req_group);
	if (req_features)
		xfree (req_features);
	if (req_node_list)
		xfree (req_node_list);
	if (req_partition)
		xfree (req_partition);
	if (script)
		xfree (script);
	return error_code;
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
 */
void 
list_delete_job (void *job_entry)
{
	struct job_record *job_record_point;

	job_record_point = (struct job_record *) job_entry;
	if (job_record_point == NULL)
		fatal ("list_delete_job: passed null job pointer");
	if (job_record_point->magic != JOB_MAGIC)
		fatal ("list_delete_job: passed invalid job pointer");

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
	if (strncmp (((struct job_record *) job_entry)->job_id, 
	    (char *) key, MAX_ID_LEN) == 0)
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
 *         returns 0 if no error, errno otherwise
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change JOB_STRUCT_VERSION in common/slurmlib.h whenever the format changes
 * NOTE: change slurm_load_job() in api/job_info.c whenever the data format changes
 */
int 
pack_all_jobs (char **buffer_ptr, int *buffer_size, time_t * update_time) 
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	int buf_len, buffer_allocated, buffer_offset = 0, error_code;
	char *buffer;
	void *buf_ptr;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	if (*update_time == last_part_update)
		return 0;

	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	job_record_iterator = list_iterator_create (job_list);		

	/* write haeader: version and time */
	pack32  ((uint32_t) JOB_STRUCT_VERSION, &buf_ptr, &buf_len);
	pack32  ((uint32_t) last_job_update, &buf_ptr, &buf_len);

	/* write individual job records */
	while ((job_record_point = 
		(struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("dump_all_job: job integrity is bad");

		error_code = pack_job(job_record_point, &buf_ptr, &buf_len);
		if (error_code != 0) continue;
		if (buf_len > BUF_SIZE) 
			continue;
		buffer_allocated += (BUF_SIZE*16);
		buf_len += (BUF_SIZE*16);
		buffer_offset = (char *)buf_ptr - buffer;
		xrealloc(buffer, buffer_allocated);
		buf_ptr = buffer + buffer_offset;
	}		

	list_iterator_destroy (job_record_iterator);
	buffer_offset = (char *)buf_ptr - buffer;
	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_part_update;
	return 0;
}


/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission)
 * input:  dump_job_ptr - pointer to job for which information is requested
 *	buf_ptr - buffer for job information 
 *	buf_len - byte size of buffer
 * output: buf_ptr - advanced to end of data written
 *	buf_len - byte size remaining in buffer
 *	return 0 if no error, 1 if buffer too small
 * NOTE: change JOB_STRUCT_VERSION in common/slurmlib.h whenever the format changes
 * NOTE: change slurm_load_job() in api/job_info.c whenever the data format changes
 * NOTE: the caller must insure that the buffer is sufficiently large to hold 
 *	 the data being written (space remaining at leas BUF_SIZE)
 */
int 
pack_job (struct job_record *dump_job_ptr, void **buf_ptr, int *buf_len) 
{
	char tmp_str[MAX_STR_PACK];
	struct job_details *detail_ptr;

	packstr (dump_job_ptr->job_id, buf_ptr, buf_len);
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

	return 0;
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
		 int *procs_per_task, char **job_id, int *priority, 
		 int *user_id) {
	int bad_index, error_code, i;
	char *temp_specs, *contiguous_str, *dist_str, *shared_str;

	req_features[0] = req_node_list[0] = req_group[0] = NULL;
	req_partition[0] = job_name[0] = script[0] = job_id[0] = NULL;
	contiguous_str = shared_str = dist_str = NULL;
	*contiguous = *req_cpus = *req_nodes = *min_cpus = NO_VAL;
	*min_memory = *min_tmp_disk = *time_limit = NO_VAL;
	*dist = *key = *shared = *procs_per_task = *user_id = NO_VAL;
	*priority = NO_VAL;

	temp_specs = xmalloc (strlen (job_specs) + 1);
	strcpy (temp_specs, job_specs);

	error_code = slurm_parser(temp_specs,
		"Contiguous=", 's', &contiguous_str, 
		"Distribution=", 's', &dist_str, 
		"Features=", 's', req_features, 
		"Groups=", 's', req_group, 
		"JobId=", 's', job_id, 
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
	if (job_id[0])
		xfree (job_id[0]);
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
	job_id[0] = req_partition[0] = job_name[0] = script[0] = NULL;
	return error_code;
}


/*
 * purge_old_job - purge old job records. if memory space is needed. 
 *	the jobs must have completed at least MIN_JOB_AGE minutes ago
 */
void
purge_old_job (void) 
{
	int i;

	if (job_count < (MAX_JOB_COUNT / 2)) return;	/* plenty of room */

	i = list_delete_all (job_list, &list_find_job_old, NULL);
	if (i)
		info ("purge_old_job: purged %d old job records");
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
	return 0;
}


/*
 * set_job_id - set a default job_id: partition name, ".", sequence number
 *	insure that the job_id is unique
 * input: job_ptr - pointer to the job_record
 */
void
set_job_id (struct job_record *job_ptr)
{
	static int id_sequence = 0;
	char new_id[MAX_NAME_LEN+20];

	if ((job_ptr == NULL) || 
	    (job_ptr->magic != JOB_MAGIC)) 
		fatal ("set_job_id: invalid job_ptr");
	if ((job_ptr->partition == NULL) || (strlen(job_ptr->partition) == 0))
		fatal ("set_job_id: partition not set");
	while (1) {
		if (job_ptr->partition)
			sprintf(new_id, "%s.%d", job_ptr->partition, id_sequence++);
		else
			sprintf(new_id, "nopart.%d", id_sequence++);
		if (find_job_record(new_id) == NULL) {
			strncpy(job_ptr->job_id, new_id, MAX_ID_LEN);
			break;
		}
	}
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
 * update_job - update a job's parameters
 * input: job_id - job's id
 *        spec - the updates to the job's specification 
 * output: return - 0 if no error, otherwise an error code
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 * NOTE: the contents of spec are overwritten by white space
 * NOTE: only the job's priority and time_limt may be changed
 */
int 
update_job (char *job_id, char *spec) 
{
	int bad_index, error_code, i, time_limit;
	int prio;
	struct job_record *job_ptr;

	if (strlen (job_id) >= MAX_ID_LEN) {
		error ("update_job: invalid job_id  %s", job_id);
		return EINVAL;
	}			

	job_ptr = list_find_first (job_list, &list_find_job_id, job_id);
	if (job_ptr == NULL) {
		error ("update_job: job_id %s does not exist.", job_id);
		return ENOENT;
	}			

	time_limit = NO_VAL;
	error_code = load_integer (&time_limit, "TimeLimit=", spec);
	if (error_code)
		return error_code;

	prio = NO_VAL;
	error_code = load_integer (&prio, "Priority=", spec);
	if (error_code)
		return error_code;

	bad_index = -1;
	for (i = 0; i < strlen (spec); i++) {
		if (spec[i] == '\n')
			spec[i] = ' ';
		if (isspace ((int) spec[i]))
			continue;
		bad_index = i;
		break;
	}			

	if (bad_index != -1) {
		error ("update_job: ignored job_id %s update specification: %s",
			job_id, &spec[bad_index]);
		return EINVAL;
	}			

	if (time_limit != NO_VAL) {
		job_ptr->time_limit = time_limit;
		info ("update_job: setting time_limit to %d for job_id %s",
			time_limit, job_id);
	}

	if (prio != NO_VAL) {
		job_ptr->priority = prio;
		info ("update_job: setting priority to %f for job_id %s",
			(double) prio, job_id);
	}

	return 0;
}
