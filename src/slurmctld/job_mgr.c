/* 
 * job_mgr.c - manage the job information of slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024

int job_count;				/* job's in the system */
List job_list = NULL;			/* job_record list */
time_t last_job_update;			/* time of last update to job records */
static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;	/* lock for job info */
char *job_state_string[] =
	{ "PENDING", "STATE_IN", "RUNNING", "STAGE_OUT", "COMPLETED", "END" };

int dump_job (struct job_record *dump_job_ptr, char *out_line, int out_line_size, 
	int detail);
void list_delete_job (void *job_entry);
int list_find_job_id (void *job_entry, void *key);
int list_find_job_old (void *job_entry, void *key);
void set_job_id (struct job_record *job_ptr);
void set_job_prio (struct job_record *job_ptr);

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main (int argc, char *argv[]) 
{
	int dump_size, error_code, i;
	time_t update_time = (time_t) NULL;
	struct job_record * job_rec;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	char *dump, tmp_id[50];
	char update_spec[] = "TimeLimit=1234 Priority=123.45";

	printf("initialize the database and create a few jobs\n");
	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	error_code = init_job_conf ();
	if (error_code)
		printf ("ERROR: init_job_conf error %d\n", error_code);

	job_rec = create_job_record(&error_code);
	if ((job_rec == NULL) || error_code) {
		printf("ERROR:create_job_record failure %d\n",error_code);
		exit(1);
	}
	strcpy(job_rec->name, "Name1");
	strcpy(job_rec->partition, "batch");
	job_rec->details->job_script = xmalloc(20);
	strcpy(job_rec->details->job_script, "/bin/hostname");
	job_rec->details->num_nodes = 1;
	job_rec->details->num_procs = 1;
	set_job_id(job_rec);
	set_job_prio(job_rec);
	strcpy(tmp_id, job_rec->job_id);

	for (i=1; i<=4; i++) {
		job_rec = create_job_record(&error_code);
		if ((job_rec == NULL) || error_code) {
			printf("ERROR:create_job_record failure %d\n",error_code);
			exit(1);
		}
		strcpy(job_rec->name, "Name2");
		strcpy(job_rec->partition, "debug");
		job_rec->details->job_script = xmalloc(20);
		strcpy(job_rec->details->job_script, "/bin/hostname");
		job_rec->details->num_nodes = i;
		job_rec->details->num_procs = i;
		set_job_id(job_rec);
		set_job_prio(job_rec);
	}

	printf("\nupdate a job record\n");
	error_code = update_job (tmp_id, update_spec);
	if (error_code)
		printf ("ERROR: update_job error %d\n", error_code);

	error_code = dump_all_job (&dump, &dump_size, &update_time, 1);
	if (error_code)
		printf ("ERROR: dump_all_job error %d\n", error_code);
	else {
		printf("\ndump of job info:\n");
		for (i=0; i<dump_size; ) {
			printf("%s", &dump[i]);
			i += strlen(&dump[i]) + 1;
		}
		printf("\n");
	}
	if (dump)
		xfree(dump);

	job_rec = find_job_record (tmp_id);
	if (job_rec == NULL)
		printf("find_job_record error 1\n");
	else
		printf("found job %s, script=%s\n", 
			job_rec->job_id, job_rec->details->job_script);

	error_code = delete_job_record(tmp_id);
	if (error_code)
		printf ("ERROR: delete_job_record error %d\n", error_code);

	job_rec = find_job_record (tmp_id);
	if (job_rec != NULL)
		printf("find_job_record error 2\n");

	exit (0);
}
#endif


/* 
 * create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * input: error_code - location to store error value in
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or NULL if error
 * global: job_list - global job list
 *         job_count - number of jobs in the system
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

	memset (job_record_point,  0, sizeof (struct job_record));
	job_record_point->magic   = JOB_MAGIC;
	job_record_point->details = job_details_point;

	memset (job_details_point, 0, sizeof (struct job_details));
	job_details_point->magic  = DETAILS_MAGIC;
	job_details_point->submit_time = time (NULL);
	job_details_point->procs_per_task = 1;

	if (list_append (job_list, job_record_point) == NULL)
		fatal ("create_job_record: unable to allocate memory");

	return job_record_point;
}


/* 
 * delete_job_record - delete record for job with specified job_id
 * input: job_id - job_id of the desired job
 * output: return 0 on success, errno otherwise
 * global: job_list - pointer to global job list
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
 * dump_all_job - dump all partition information to a buffer
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the data buffer is actually allocated by dump_part and the 
 *                     calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if job records updated since time 
 *                       specified, otherwise return empty buffer
 *         detail - report job_detail only if set
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 *         returns 0 if no error, errno otherwise
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 */
int 
dump_all_job (char **buffer_ptr, int *buffer_size, time_t * update_time, 
	int detail) 
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point;
	char *buffer;
	int buffer_offset, buffer_allocated, error_code, i, record_size;
	char out_line[BUF_SIZE];

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	buffer = NULL;
	buffer_offset = 0;
	buffer_allocated = 0;
	if (*update_time == last_job_update)
		return 0;

	job_record_iterator = list_iterator_create (job_list);		

	/* write header, version and time */
	sprintf (out_line, HEAD_FORMAT, (unsigned long) last_job_update,
		 JOB_STRUCT_VERSION);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	/* write individual job records */
	while (job_record_point = (struct job_record *) list_next (job_record_iterator)) {
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("dump_all_job: data integrity is bad");

		error_code = dump_job(job_record_point, out_line, BUF_SIZE, detail);
		if (error_code != 0) continue;

		if (write_buffer
		    (&buffer, &buffer_offset, &buffer_allocated, out_line))
			goto cleanup;
	}			

	list_iterator_destroy (job_record_iterator);
	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_job_update;
	return 0;

      cleanup:
	list_iterator_destroy (job_record_iterator);
	if (buffer)
		xfree (buffer);
	return EINVAL;
}


/* 
 * dump_job - dump all configuration information about a specific job to a buffer
 * input:  dump_job_ptr - pointer to job for which information is requested
 *         out_line - buffer for partition information 
 *         out_line_size - byte size of out_line
 *         detail - report job_detail only if set
 * output: out_line - set to partition information values
 *         return 0 if no error, 1 if out_line buffer too small
 * NOTE: if you make any changes here be sure to increment the value of 
 *       JOB_STRUCT_VERSION and make the corresponding changes to load_part_config 
 *       in api/partition_info.c
 */
int 
dump_job (struct job_record *dump_job_ptr, char *out_line, int out_line_size, 
	int detail) 
{
	char *job_id, *name, *partition, *nodes, *req_nodes, *features;
	char *job_script;
	struct job_details *detail_ptr;

	if (dump_job_ptr->job_id)
		job_id = dump_job_ptr->job_id;
	else
		job_id = "NONE";

	if (dump_job_ptr->name)
		name = dump_job_ptr->name;
	else
		name = "NONE";

	if (dump_job_ptr->partition)
		partition = dump_job_ptr->partition;
	else
		partition = "NONE";

	if (dump_job_ptr->nodes)
		nodes = dump_job_ptr->nodes;
	else
		nodes = "NONE";

	if (detail == 0 || (dump_job_ptr->details == NULL)) {
		if ((strlen(JOB_STRUCT_FORMAT1) + strlen(job_id) +  
		     strlen(partition) + strlen(name) + strlen(nodes) + 
		     strlen(job_state_string[dump_job_ptr->job_state]) + 20) > 
		     out_line_size) {
			error ("dump_job: buffer too small for job %s", job_id);
			return 1;
		}

		sprintf (out_line, JOB_STRUCT_FORMAT1,
			 job_id, 
			 partition, 
			 name, 
			 (int) dump_job_ptr->user_id, 
			 nodes, 
			 job_state_string[dump_job_ptr->job_state], 
			 dump_job_ptr->time_limit, 
			 (long) dump_job_ptr->start_time, 
			 (long) dump_job_ptr->end_time, 
			 dump_job_ptr->priority);
	} 
	else {
		detail_ptr = dump_job_ptr->details;
		if (detail_ptr->magic != DETAILS_MAGIC)
			fatal ("dump_job: bad detail pointer for job_id %s", job_id);

		if (detail_ptr->nodes)
			req_nodes = detail_ptr->nodes;
		else
			req_nodes = "NONE";
	
		if (detail_ptr->features)
			features = detail_ptr->features;
		else
			features = "NONE";

		if (detail_ptr->job_script)
			job_script = detail_ptr->job_script;
		else
			job_script = "NONE";
	
		if ((strlen(JOB_STRUCT_FORMAT1) + strlen(job_id) +  
		     strlen(partition) + strlen(name) + strlen(nodes) + 
		     strlen(job_state_string[dump_job_ptr->job_state]) + 
		     strlen(req_nodes) + strlen(features) + 
		     strlen(job_script) + 20) > out_line_size) {
			error ("dump_job: buffer too small for job %s", job_id);
			return 1;
		}

		sprintf (out_line, JOB_STRUCT_FORMAT2,
			 job_id, 
			 partition, 
			 name, 
			 (int) dump_job_ptr->user_id, 
			 nodes, 
			 job_state_string[dump_job_ptr->job_state], 
			 dump_job_ptr->time_limit, 
			 (long) dump_job_ptr->start_time, 
			 (long) dump_job_ptr->end_time, 
			 dump_job_ptr->priority, 
			 detail_ptr->num_procs, 
			 detail_ptr->num_nodes, 
			 req_nodes, 
			 features, 
			 detail_ptr->shared, 
			 detail_ptr->contiguous, 
			 detail_ptr->min_procs, 
			 detail_ptr->min_memory, 
			 detail_ptr->min_tmp_disk, 
			 detail_ptr->dist, 
			 job_script, 
			 detail_ptr->procs_per_task, 
			 detail_ptr->total_procs);
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
 * this should be called before creating any node or configuration entries.
 * output: return value - 0 if no error, otherwise an error code
 * global: last_node_update - time of last node table update
 *	job_list - pointer to global job list
 */
int 
init_job_conf () 
{
	last_job_update = time (NULL);

	if (job_list == NULL) {
		job_count = 0;
		job_list = list_create (&list_delete_job);
	}

	if (job_list == NULL)
		fatal ("init_job_conf: list_create can not allocate memory");
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
 */
int
job_cancel (char * job_id) 
{
	struct job_record *job_ptr;
	bitstr_t *req_bitmap;
	int error_code;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info ("job_cancel: invalid job id %s", job_id);
		return EINVAL;
	}
	if (job_ptr->job_state == JOB_PENDING) {
		job_ptr->job_state = JOB_END;
		job_ptr->start_time = job_ptr->end_time = time(NULL);
		info ("job_cancel of pending job %s", job_id);
		return 0;
	}

	if (job_ptr->job_state == JOB_STAGE_IN) {
		job_ptr->job_state = JOB_END;
		error_code = node_name2bitmap (job_ptr->nodes, &req_bitmap);
		if (error_code == EINVAL)
			fatal ("invalid node list for job %s", job_id);
		deallocate_nodes (req_bitmap);
		bit_free (req_bitmap);
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
	char *req_partition, *script, *out_line, *job_id;
	int contiguous, req_cpus, req_nodes, min_cpus, min_memory;
	int i, min_tmp_disk, time_limit, procs_per_task, user_id;
	int error_code, cpu_tally, dist, node_tally, key, shared;
	struct part_record *part_ptr;
	struct job_record *job_ptr;
	struct job_details *detail_ptr;
	float priority;
	bitstr_t *req_bitmap;

	new_job_id[0] = NULL;
	req_features = req_node_list = job_name = req_group = NULL;
	job_id = req_partition = script = NULL;
	req_bitmap = NULL;
	contiguous = dist = req_cpus = req_nodes = min_cpus = NO_VAL;
	min_memory = min_tmp_disk = time_limit = procs_per_task = NO_VAL;
	key = shared = user_id = NO_VAL;
	priority = (float) NO_VAL;

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
		info ("job_create: JobId specified is too long");
		error_code = EINVAL;
		goto cleanup;
	}
	if (user_id == NO_VAL) {
		info ("job_create: job failed to specify User");
		error_code = EINVAL;
		goto cleanup;
	}	
	if (contiguous == NO_VAL)
		contiguous = 0;		/* default not contiguous */
	if (req_cpus == NO_VAL)
		req_cpus = 1;		/* default cpu count of 1 */
	if (req_nodes == NO_VAL)
		req_nodes = 1;		/* default node count of 1 */
	if (min_cpus == NO_VAL)
		min_cpus = 1;		/* default is 1 processor per node */
	if (min_memory == NO_VAL)
		min_memory = 1;		/* default is 1 MB memory per node */
	if (min_tmp_disk == NO_VAL)
		min_tmp_disk = 1;	/* default is 1 MB disk per node */
	if (shared == NO_VAL)
		shared = 0;		/* default is not shared nodes */
	if (dist == NO_VAL)
		dist = 0;		/* default is block distribution */
	if (procs_per_task == NO_VAL)
		procs_per_task = 1;	/* default is 1 processor per task */


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
		bit_free (req_bitmap);
		req_bitmap = NULL;
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
		strncpy (job_ptr->name, job_name, MAX_NAME_LEN);
		xfree (job_name);
	}
	job_ptr->user_id = (uid_t) user_id;
	job_ptr->job_state = JOB_PENDING;
	job_ptr->time_limit = time_limit;
	if (key && is_key_valid (key) && ((priority - NO_VAL) > 0.01))
		job_ptr->priority = priority;
	else
		set_job_prio (job_ptr);

	detail_ptr = job_ptr->details;
	detail_ptr->num_procs = req_cpus;
	detail_ptr->num_nodes = req_nodes;
	if (req_node_list)
		detail_ptr->nodes = req_node_list;
	if (req_features)
		detail_ptr->features = req_features;
	detail_ptr->shared = shared;
	detail_ptr->contiguous = contiguous;
	detail_ptr->min_procs = min_cpus;
	detail_ptr->min_memory = min_memory;
	detail_ptr->min_tmp_disk = min_tmp_disk;
	detail_ptr->dist = dist;
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
 * input: job_record_point - pointer to job_record to delete
 * output: returns 0 on success, errno otherwise
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

	if (job_record_point->details) {
		if (job_record_point->details->magic != DETAILS_MAGIC)
			fatal ("list_delete_job: passed invalid job details pointer");
		if (job_record_point->details->job_script)
			xfree(job_record_point->details->job_script);
		if (job_record_point->details->nodes)
			xfree(job_record_point->details->nodes);
		if (job_record_point->details->features)
			xfree(job_record_point->details->features);
		xfree(job_record_point->details);
	}

	if (job_record_point->nodes)
		xfree(job_record_point->nodes);
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
		 int *procs_per_task, char **job_id, float *priority, 
		 int *user_id) {
	int bad_index, error_code, i;
	char *temp_specs, *contiguous_str, *dist_str, *shared_str;

	req_features[0] = req_node_list[0] = req_group[0] = NULL;
	req_partition[0] = job_name[0] = script[0] = job_id[0] = NULL;
	contiguous_str = shared_str = dist_str = NULL;
	*contiguous = *req_cpus = *req_nodes = *min_cpus = NO_VAL;
	*min_memory = *min_tmp_disk = *time_limit = NO_VAL;
	*dist = *key = *shared = *procs_per_task = *user_id = NO_VAL;
	*priority = (float) NO_VAL;

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
		"Priority=", 'f', priority, 
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
		i = block_or_cycle (dist_str);
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
	static float default_prio = 1.000;

	if ((job_ptr == NULL) || 
	    (job_ptr->magic != JOB_MAGIC)) 
		fatal ("set_job_prio: invalid job_ptr");
	job_ptr->priority = default_prio;
	default_prio -= 0.00001;
}


/*
 * update_job - update a job's parameters
 * input: job_id - job's id
 *        spec - the updates to the job's specification 
 * output: return - 0 if no error, otherwise an error code
 * global: job_list - global list of job entries
 * NOTE: the contents of spec are overwritten by white space
 * NOTE: only the job's priority and time_limt may be changed
 */
int 
update_job (char *job_id, char *spec) 
{
	int bad_index, error_code, i, time_limit;
	float prio;
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

	prio = (float) NO_VAL;
	error_code = load_float (&prio, "Priority=", spec);
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

	if ((prio - NO_VAL) > 0.01) {	/* avoid reset from round-off */
		job_ptr->priority = prio;
		info ("update_job: setting priority to %f for job_id %s",
			(double) prio, job_id);
	}

	return 0;
}
