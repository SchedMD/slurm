/*****************************************************************************\
 *  pack.c - pack slurmctld structures into buffers understood by the 
 *          slurm_protocol 
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>, Joseph Ekstrom (ekstrom1@llnl.gov)
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
 *  60 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#include <src/common/bitstring.h>
#include <src/common/list.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024
#define REALLOC_MULTIPLIER 4  


/* buffer_realloc - reallocates the buffer if/when it gets smaller than BUF_SIZE */
inline void buffer_realloc( void** buffer, void** current, int* size, int* len_left )
{
	int current_offset = *current - *buffer;

	if ( *len_left < BUF_SIZE )
	{
		*size += BUF_SIZE * REALLOC_MULTIPLIER ;
		*len_left += BUF_SIZE * REALLOC_MULTIPLIER ;
		*buffer = xrealloc( *buffer, *size );
		*current = buffer + current_offset;
	}
}


void
pack_ctld_job_step_info( struct  step_record* step, void **buf_ptr, int *buf_len)
{
	char *node_list;

	if (step->node_bitmap) 
		node_list = bitmap2node_name (step->node_bitmap);
	else {
		node_list = xmalloc(1);
		node_list[0] = '\0';
	}

	pack_job_step_info_members(
				step->job_ptr->job_id,
				step->step_id,
				step->job_ptr->user_id,
				step->start_time,
				step->job_ptr->partition ,
				node_list,
				buf_ptr,
				buf_len
			);
	xfree (node_list);
}

/* pack_ctld_job_step_info_reponse_msg - packs the message
 * IN - job_id and step_id - zero for all
 * OUT - packed buffer and length NOTE- MUST xfree buffer
 * return - error code
 */
int
pack_ctld_job_step_info_reponse_msg (void** buffer_base, int* buffer_length, uint32_t job_id, uint32_t step_id )
{
	ListIterator job_record_iterator;
	ListIterator step_record_iterator;
	int buffer_size = BUF_SIZE * REALLOC_MULTIPLIER;
	int current_size = buffer_size;
	int error_code = 0, steps_packed = 0;
	void* current = NULL;
	struct step_record* step_ptr;
	struct job_record * job_ptr;

	current = *buffer_base = xmalloc( buffer_size );
	pack32( last_job_update, &current, &current_size );
	pack32( steps_packed , &current, &current_size );	/* steps_packed is placeholder for now */

	if ( job_id == 0 )
	/* Return all steps for all jobs */
	{
		job_record_iterator = list_iterator_create (job_list);		
		while ((job_ptr = (struct job_record *) list_next (job_record_iterator))) {
			step_record_iterator = list_iterator_create (job_ptr->step_list);		
			while ((step_ptr = (struct step_record *) list_next (step_record_iterator))) {
				pack_ctld_job_step_info( step_ptr, &current, &current_size );
				buffer_realloc( buffer_base, &current, &buffer_size, &current_size );
				steps_packed++;
			}
			list_iterator_destroy (step_record_iterator);
		}
		list_iterator_destroy (job_record_iterator);
	}

	else if ( step_id == 0 )
	/* Return all steps for specific job_id */
	{
		job_ptr = find_job_record( job_id );
		if (job_ptr) {
			step_record_iterator = list_iterator_create (job_ptr->step_list);		
			while ((step_ptr = (struct step_record *) list_next (step_record_iterator))) {
				pack_ctld_job_step_info( step_ptr, &current, &current_size );
				buffer_realloc( buffer_base, &current, &buffer_size, &current_size );
				steps_packed++;
			}
			list_iterator_destroy (step_record_iterator);
		}
		else
			error_code = ESLURM_INVALID_JOB_ID;
	}		

	else
	/* Return  step with give step_id/job_id */
	{
		job_ptr = find_job_record( job_id );
		step_ptr =  find_step_record( job_ptr, step_id ); 
		if ( step_ptr ==  NULL ) 
			error_code = ESLURM_INVALID_JOB_ID;
		else {
			pack_ctld_job_step_info( step_ptr, &current, &current_size );
			steps_packed++;
		}
	}

	if( error_code ) {
		xfree( *buffer_base );
		*buffer_base = NULL;
		if( buffer_length )
			*buffer_length = 0;
	}
	else {
		if( buffer_length )
			*buffer_length = buffer_size - current_size;
		current = *buffer_base;
		current_size = buffer_size;
		pack32( last_job_update, &current, &current_size );
		pack32( steps_packed , &current, &current_size );
	}
	return 	error_code;
}


