/*****************************************************************************\
 * step_mgr.c - manage the job step information of slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>.
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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024

bitstr_t * pick_step_nodes (struct job_record  *job_ptr, int min_nodes, int min_cpus, 
		 char *node_list, char *relative_node_list);

/* 
 * create_step_record - create an empty step_record for the specified job.
 * input: job_ptr - pointer to job table entry to have step record added
 * output: returns a pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
struct step_record * 
create_step_record (struct job_record *job_ptr) 
{
	struct step_record *step_record_point;

	assert (job_ptr);
	step_record_point = (struct step_record *) xmalloc (sizeof (struct step_record));

	step_record_point->step_id = (job_ptr->next_step_id)++;
	if (list_append (job_ptr->step_list, step_record_point) == NULL)
		fatal ("create_step_record: unable to allocate memory");

	return step_record_point;
}


/* 
 * delete_step_record - delete record for job step for specified job_ptr and step_id
 * input: job_ptr - pointer to job table entry to have step record added
 *	step_id - id of the desired job step
 * output: return 0 on success, errno otherwise
 */
int 
delete_step_record (struct job_record *job_ptr, uint32_t step_id) 
{
	ListIterator step_record_iterator;
	struct step_record *step_record_point;
	int error_code;

	assert (job_ptr);
	error_code = ENOENT;
	step_record_iterator = list_iterator_create (job_ptr->step_list);		

	while ((step_record_point = 
		(struct step_record *) list_next (step_record_iterator))) {
		if (step_record_point->step_id == step_id) {
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
 * input: job_ptr - pointer to job table entry to have step record added
 *	step_id - id of the desired job step
 * output: pointer to the job step's record, NULL on error
 * global: step_list - global step list
 */
struct step_record *
find_step_record(struct job_record *job_ptr, uint16_t step_id) 
{
	ListIterator step_record_iterator;
	struct step_record *step_record_point;

	assert (job_ptr);
	step_record_iterator = list_iterator_create (job_ptr->step_list);		

	while ((step_record_point = 
		(struct step_record *) list_next (step_record_iterator))) {
		if (step_record_point->step_id == step_id) {
			break;
		}
	}		

	list_iterator_destroy (step_record_iterator);
	return step_record_point;
}


/* 
 * pack_all_steps - for a specifiied job dump all step information in 
 *	machine independent form (for network transmission)
 * input:  job_ptr - pointer to job for which step info is requested
 *         buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 * global: step_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change unpack_step_desc() in common/slurm_protocol_pack.c whenever the
 *	data format changes
 */
void 
pack_all_steps (struct job_record *job_ptr, char **buffer_ptr, int *buffer_size, time_t * update_time) 
{
	ListIterator step_record_iterator;
	struct step_record *step_record_point;
	int buf_len, buffer_allocated, buffer_offset = 0;
	char *buffer;
	void *buf_ptr;
	uint32_t steps_packed ;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	/* write message body header : size only */
	/* put in a place holder job record count of 0 for now */
	steps_packed = 0 ;
	pack32  ((uint32_t) steps_packed, &buf_ptr, &buf_len);

	/* write individual job step records */
	step_record_iterator = list_iterator_create (job_ptr->step_list);		
	while ((step_record_point = 
		(struct step_record *) list_next (step_record_iterator))) {

		pack_step (step_record_point, &buf_ptr, &buf_len);
		if (buf_len > BUF_SIZE) {
			steps_packed ++ ;
			continue;
		}
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

	/* put the real record count in the message body header */	
	buf_ptr = buffer;
	buf_len = buffer_allocated;
	pack32  ((uint32_t) steps_packed, &buf_ptr, &buf_len);
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
void 
pack_step (struct step_record *dump_step_ptr, void **buf_ptr, int *buf_len) 
{
#ifdef HAVE_LIBELAN3
	int len;
#endif
	char node_inx_ptr[BUF_SIZE];


	pack16  (dump_step_ptr->step_id, buf_ptr, buf_len);

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
}


/* 
 * pick_step_nodes - select nodes for a job step that satify its requirements
 */
bitstr_t *
pick_step_nodes (struct job_record  *job_ptr, int min_nodes, int min_cpus, 
		 char *node_list, char *relative_node_list) {
	if (job_ptr->node_bitmap == NULL)
		return NULL;

	return bit_copy(job_ptr->node_bitmap);
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
	struct step_record *step_ptr;
	struct job_record  *job_ptr;
	bitstr_t *nodeset;
#ifdef HAVE_LIBELAN3
	int first, last, i, node_id, nprocs;
	int node_set_size = QSW_MAX_TASKS; /* overkill but safe */
#endif

	job_ptr = find_job_record (step_specs->job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;
	if (step_specs->user_id != job_ptr->user_id &&
	    step_specs->user_id != 0)
		return ESLURM_ACCESS_DENIED;

	nodeset = pick_step_nodes (job_ptr, step_specs->min_nodes, step_specs->min_cpus, 
		step_specs->node_list, step_specs->relative_node_list);
	if (nodeset == NULL)
		return ESLURM_REQUESTED_NODE_CONFIGURATION_UNAVAILBLE;

	step_ptr = create_step_record (job_ptr);
	if (step_ptr == NULL)
		fatal ("create_step_record failed with no memory");

	step_ptr->step_id = (job_ptr->next_step_id)++;
	step_ptr->node_bitmap = nodeset;

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
