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
#define NO_VAL   -99

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

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main (int argc, char *argv[]) 
{
	int dump_size, error_code, i;
	time_t update_time;
	struct job_record * job_rec;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	char *dump;
	char update_spec[] = "TimeLimit=1234 Priority=123.45";

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	error_code = init_job_conf ();
	if (error_code)
		printf ("ERROR: init_job_conf error %d\n", error_code);

	job_rec = create_job_record(&error_code);
	if ((job_rec == NULL) || error_code) {
		printf("ERROR:create_job_record failure %d\n",error_code);
		exit(1);
	}
	strcpy(job_rec->job_id, "JobId");
	strcpy(job_rec->name, "Name");
	strcpy(job_rec->partition, "Partition");
	job_rec->details->job_script = xmalloc(20);
	strcpy(job_rec->details->job_script, "/bin/hostname");
	job_rec->details->num_nodes = 4;
	job_rec->details->min_procs = 4;

	error_code = update_job ("JobId", update_spec);
	if (error_code)
		printf ("ERROR: update_job error %d\n", error_code);

	error_code = dump_all_job (&dump, &dump_size, &update_time, 1);
	if (error_code)
		printf ("ERROR: dump_all_job error %d\n", error_code);
	else {
		printf("dump of job info:\n");
		for (i=0; i<dump_size; ) {
			printf("%s", &dump[i]);
			i += strlen(&dump[i]) + 1;
		}
		printf("\n");
	}
	if (dump)
		xfree(dump);

	job_rec = find_job_record ("JobId");
	if (job_rec == NULL)
		printf("find_job_record error 1\n");
	else
		printf("found job JobId, script=%s\n", job_rec->details->job_script);

	error_code = delete_job_record("JobId");
	if (error_code)
		printf ("ERROR: delete_job_record error %d\n", error_code);

	job_rec = find_job_record ("JobId");
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
 * NOTE: allocates memory that should be xfreed with list_delete_job
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
dump_all_job (char **buffer_ptr, int *buffer_size, time_t * update_time, int detail) 
{
	ListIterator job_record_iterator;	/* for iterating through job_record list */
	struct job_record *job_record_point;	/* pointer to job_record */
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
 * NOTE: if you make any changes here be sure to increment the value of JOB_STRUCT_VERSION
 *       and make the corresponding changes to load_part_config in api/partition_info.c
 */
int 
dump_job (struct job_record *dump_job_ptr, char *out_line, int out_line_size, int detail) 
{
	char *job_id, *name, *partition, *nodes, *req_nodes, *features, *groups, *job_script;
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
		     strlen(job_state_string[dump_job_ptr->job_state]) + 20) > out_line_size) {
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
	
		if (detail_ptr->groups)
			groups = detail_ptr->groups;
		else
			groups = "NONE";
	
		if (detail_ptr->job_script)
			job_script = detail_ptr->job_script;
		else
			job_script = "NONE";
	
		if ((strlen(JOB_STRUCT_FORMAT1) + strlen(job_id) +  
		     strlen(partition) + strlen(name) + strlen(nodes) + 
		     strlen(job_state_string[dump_job_ptr->job_state]) + 
		     strlen(req_nodes) + strlen(features) + 
		     strlen(groups) + strlen(job_script) + 20) > out_line_size) {
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
			 dump_job_ptr->priority, 
			 detail_ptr->num_procs, 
			 detail_ptr->num_nodes, 
			 req_nodes, 
			 features, 
			 groups, 
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
		if (job_record_point->details->groups)
			xfree(job_record_point->details->groups);
		xfree(job_record_point->details);
	}

	if (job_record_point->nodes)
		xfree(job_record_point->nodes);
	job_count--;
	xfree(job_record_point);
}


/*
 * list_find_job_id - find an entry in the job list, see common/list.h for documentation, 
 *	key is the job's id 
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
 * list_find_job_old - find an entry in the job list, see common/list.h for documentation, 
 *	key is ignored 
 * global- job_list - the global partition list
 */
int 
list_find_job_old (void *job_entry, void *key) 
{
	time_t min_age;

	min_age = time(NULL) - MIN_JOB_AGE;

	if (((struct job_record *) job_entry)->job_state != JOB_COMPLETE) return 0;
	if (((struct job_record *) job_entry)->end_time  <  min_age)      return 0;
	return 1;
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
 * update_job - update a job's parameters
 * input: job_id - job's id
 *        spec - the updates to the job's specification 
 * output: return - 0 if no error, otherwise an error code
 * global: job_list - global list of job entries
 * NOTE: the contents of spec are overwritten by white space
 * NOTE: only the job's priority and time_limt may be changed once queued
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

	if (time_limit != NO_VAL)
		job_ptr->time_limit = time_limit;

	if ((prio - NO_VAL) > 0.1)
		job_ptr->priority = prio;

	return 0;
}
