/* 
 * job_step.c - manage the job step information of slurm
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
#include "bitstring.h"
#ifdef HAVE_LIBELAN3
#include "qsw.h"
#endif
#include "slurmctld.h"

#define BUF_SIZE 1024

List step_list = NULL;			/* job_step list */
static pthread_mutex_t step_mutex = PTHREAD_MUTEX_INITIALIZER;	/* lock for step info */
time_t last_step_update = (time_t) NULL;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) 
{
	int error_code, error_count = 0;
	struct step_record *step_ptr;
	char *dump;
	int dump_size;
	time_t update_time;

	printf ("exercise basic job step functions\n");
	error_code = init_step_conf ();
	if (error_code) {
		printf ("init_step_conf error %d\n", error_code);
		error_count++;
	}

	step_ptr = create_step_record (&error_code);
	if (error_code) {
		printf ("create_step_record error %d\n", error_code);
		error_count++;
	}
	step_ptr->step_id = 99;
	step_ptr->dist = 1;
	step_ptr->procs_per_task = 2;

	step_ptr = find_step_record ((uint32_t) 123, (uint16_t) 99);
	if (step_ptr) {
		printf ("step_ptr failure\n");
		error_count++;
	}

	step_lock ();
	step_unlock ();

	error_code = pack_all_step (&dump, &dump_size, &update_time);
	if (error_code) {
		printf ("ERROR: pack_all_step error %d\n", error_code);
		error_count++;
	}
	xfree (dump);

	printf ("tests completed with %d errors\n", error_count);
	exit (error_count);
}
#endif


/* 
 * create_step_record - create an empty step_record.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * input: error_code - location to store error value in
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or NULL if error
 * global: step_list - global step list
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
struct step_record * 
create_step_record (int *error_code) 
{
	struct step_record *step_record_point;

	*error_code = 0;
	step_record_point =
		(struct step_record *) xmalloc (sizeof (struct step_record));

	step_record_point->magic   = STEP_MAGIC;
	if (list_append (step_list, step_record_point) == NULL)
		fatal ("create_step_record: unable to allocate memory");
	return step_record_point;
}


/* 
 * delete_step_record - delete record for job step with specified job_id and step_id
 * input: job_id - job_id of the desired job
 *	step_id - id of the desired job step
 * output: return 0 on success, errno otherwise
 * global: step_list - global step list
 */
int 
delete_step_record (uint32_t job_id, uint16_t step_id) 
{
	ListIterator step_record_iterator;
	struct step_record *step_record_point;
	int error_code;

	error_code = ENOENT;
	step_record_iterator = list_iterator_create (step_list);		

	while ((step_record_point = 
		(struct step_record *) list_next (step_record_iterator))) {
		if (step_record_point->step_id == step_id &&
		    step_record_point->job_ptr->job_id == job_id) {
			if (step_record_point->magic != STEP_MAGIC)
				fatal ("invalid step data\n");
			list_remove (step_record_iterator);
#ifdef HAVE_LIBELAN3
			qsw_free_jobinfo (step_record_point->qsw_job);
#endif
			if (step_record_point->node_bitmap)
				bit_free (step_record_point->node_bitmap);
			xfree (step_record_point);
			error_code = 0;
			break;
		}
	}		

	list_iterator_destroy (step_record_iterator);
	return error_code;
}


/* 
 * find_step_record - return a pointer to the step record with the given job_id and step_id
 * input: job_id - requested job's id
 *	step_id - id of the desired job step
 * output: pointer to the job step's record, NULL on error
 * global: step_list - global step list
 */
struct step_record *
find_step_record(uint32_t job_id, uint16_t step_id) 
{
	ListIterator step_record_iterator;
	struct step_record *step_record_point;

	step_record_iterator = list_iterator_create (step_list);		

	while ((step_record_point = 
		(struct step_record *) list_next (step_record_iterator))) {
		if (step_record_point->step_id == step_id &&
		    step_record_point->job_ptr &&
		    step_record_point->job_ptr->job_id == job_id) {
			if (step_record_point->magic != STEP_MAGIC)
				fatal ("invalid step data\n");
			break;
		}
	}		

	list_iterator_destroy (step_record_iterator);
	return step_record_point;
}


/* 
 * init_step_conf - initialize the job step configuration tables and values. 
 *	this should be called before creating any job step entries.
 * output: return value - 0 if no error, otherwise an error code
 * global: step_list - global step list
 */
int 
init_step_conf () 
{
	if (step_list == NULL) {
		step_list = list_create (NULL);
		if (step_list == NULL)
			fatal ("init_step_conf: list_create can not allocate memory");
	}
	last_step_update = time (NULL);
	return 0;
}


/* 
 * pack_all_step - dump all job step information for all steps in 
 *	machine independent form (for network transmission)
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 *         returns 0 if no error, errno otherwise
 * global: step_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change STEP_STRUCT_VERSION in common/slurmlib.h whenever the format changes
 * NOTE: change slurm_load_step() in api/step_info.c whenever the data format changes
 */
int 
pack_all_step (char **buffer_ptr, int *buffer_size, time_t * update_time) 
{
	ListIterator step_record_iterator;
	struct step_record *step_record_point;
	int buf_len, buffer_allocated, buffer_offset = 0, error_code;
	char *buffer;
	void *buf_ptr;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	if (*update_time == last_step_update)
		return 0;

	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	step_record_iterator = list_iterator_create (step_list);		

	/* write haeader: version and time */
	pack32  ((uint32_t) STEP_STRUCT_VERSION, &buf_ptr, &buf_len);
	pack32  ((uint32_t) last_step_update, &buf_ptr, &buf_len);

	/* write individual job step records */
	while ((step_record_point = 
		(struct step_record *) list_next (step_record_iterator))) {
		if (step_record_point->magic != STEP_MAGIC)
			fatal ("pack_all_step: data integrity is bad");

		error_code = pack_step (step_record_point, &buf_ptr, &buf_len);
		if (error_code != 0) continue;
		if (buf_len > BUF_SIZE) 
			continue;
		buffer_allocated += (BUF_SIZE*16);
		buf_len += (BUF_SIZE*16);
		buffer_offset = (char *)buf_ptr - buffer;
		xrealloc(buffer, buffer_allocated);
		buf_ptr = buffer + buffer_offset;
	}			

	list_iterator_destroy (step_record_iterator);
	buffer_offset = (char *)buf_ptr - buffer;
	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_step_update;
	return 0;
}


/* 
 * pack_step - dump state information about a specific job step in 
 *	machine independent form (for network transmission)
 * input:  dump_step_ptr - pointer to step for which information is requested
 *	buf_ptr - buffer for step information 
 *	buf_len - byte size of buffer
 * output: buf_ptr - advanced to end of data written
 *	buf_len - byte size remaining in buffer
 *	return 0 if no error, 1 if buffer too small
 * NOTE: change STEP_STRUCT_VERSION in common/slurmlib.h whenever the format changes
 * NOTE: change slurm_load_step() in api/step_info.c whenever the data format changes
 * NOTE: the caller must insure that the buffer is sufficiently large to hold 
 *	 the data being written (space remaining at least BUF_SIZE)
 */
int 
pack_step (struct step_record *dump_step_ptr, void **buf_ptr, int *buf_len) 
{
	char node_inx_ptr[BUF_SIZE];
	int len;

	if (dump_step_ptr->job_ptr)
		pack32 (dump_step_ptr->job_ptr->job_id, buf_ptr, buf_len);
	else
		pack32 (0, buf_ptr, buf_len);

	pack16  (dump_step_ptr->step_id, buf_ptr, buf_len);
	pack16  (dump_step_ptr->dist, buf_ptr, buf_len);
	pack16  (dump_step_ptr->procs_per_task, buf_ptr, buf_len);

	if (dump_step_ptr->node_bitmap) {
		bit_fmt (node_inx_ptr, BUF_SIZE, dump_step_ptr->node_bitmap);
		packstr (node_inx_ptr, buf_ptr, buf_len);
	}
	else
		packstr ("", buf_ptr, buf_len);

#ifdef HAVE_LIBELAN3
	if (dump_step_ptr->qsw_job) {
		len = qsw_pack_jobinfo (dump_step_ptr->qsw_job, *buf_ptr, *buf_len);
		if (len > 0) {		/* Need to explicitly advance pointer and index here */
			*buf_ptr = (void *) ((char *)*buf_ptr + len);
			*buf_len += len;
		}
	}
	else
		packstr (NULL, buf_ptr, buf_len);
#endif

	return 0;
}


/*
 * step_create - parse the suppied job step specification and create step_records for it
 * input: step_specs - job step specifications
 * output: returns 0 on success, EINVAL if specification is invalid
 * globals: step_list - pointer to global job step list 
 * NOTE: the calling program must xfree the memory pointed to by new_job_id
 */
int
step_create (struct step_specs *step_specs)
{
	int error_code, nprocs;
	struct step_record *step_ptr;
	struct job_record  *job_ptr;
#ifdef HAVE_LIBELAN3
	int first, last, i, node_id;
	bitstr_t *nodeset;
	int node_set_size = QSW_MAX_TASKS; /* overkill but safe */
#endif

	job_ptr = find_job_record (step_specs->job_id);
	if (job_ptr == NULL)
		return ENOENT;;
	if (step_specs->user_id != job_ptr->user_id &&
	    step_specs->user_id != 0)
		return EACCES;

	step_ptr = create_step_record (&error_code);
	if (error_code)
		return error_code;

	step_ptr->job_ptr = job_ptr;
	step_ptr->step_id = (job_ptr->next_step_id)++;
	step_ptr->magic = STEP_MAGIC;
	step_ptr->dist = step_specs->dist;
	step_ptr->procs_per_task = step_specs->procs_per_task;
/* we need to be able to filter out some of the bitmap entries here for partial allocation */
	step_ptr->node_bitmap = bit_copy (job_ptr->node_bitmap);
#ifdef HAVE_LIBELAN3
	if (qsw_alloc_jobinfo (&step_ptr->qsw_job) < 0)
		fatal ("step_create: qsw_alloc_jobinfo error");
	first = bit_ffs (step_ptr->node_bitmap);
	last  = bit_fls (step_ptr->node_bitmap);
	nodeset = bit_alloc (node_set_size);
	if (nodeset == NULL)
		fatal ("step_create: bit_alloc error");
	for (i = first; i <= last; i++) {
		if (bit_test (step_ptr->node_bitmap, i)) {
			node_id = qsw_getnodeid_byhost (node_record_table_ptr[i].name);
			bit_set(nodeset, node_id);
		}
	}
	if (qsw_setup_jobinfo (step_ptr->qsw_job, nprocs, nodeset, step_ptr->dist) < 0)
		fatal ("step_create: qsw_setup_jobinfo error");
	bit_free (nodeset);
#endif
	return 0;
}


/* step_lock - lock the step information 
 * global: step_mutex - semaphore for the step table
 */
void 
step_lock () 
{
	int error_code;
	error_code = pthread_mutex_lock (&step_mutex);
	if (error_code)
		fatal ("step_lock: pthread_mutex_lock error %d", error_code);
	
}


/* step_unlock - unlock the step information 
 * global: step_mutex - semaphore for the step table
 */
void 
step_unlock () 
{
	int error_code;
	error_code = pthread_mutex_unlock (&step_mutex);
	if (error_code)
		fatal ("step_unlock: pthread_mutex_unlock error %d", error_code);
}


