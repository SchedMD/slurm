/*****************************************************************************\
 * pack.c - pack slurmctld structures into buffers understood by the 
 *          slurm_protocol 
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>, Joseph Ekstrom (ekstrom1@llnl.gov)
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
	char node_list[BUF_SIZE];

	if (step->node_bitmap) 
		bit_fmt (node_list, BUF_SIZE, step->node_bitmap);
	else
		node_list[0] = '\0';

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
}

void
pack_ctld_job_step_info_reponse_msg( List steps, void** buffer_base, int* buffer_size )
{
	ListIterator iterator = list_iterator_create( steps );
	struct step_record* current_step = NULL;		
	int current_size = 0;
	void* current = buffer_base;

	*buffer_size = BUF_SIZE * REALLOC_MULTIPLIER;
	*buffer_base = xmalloc( *buffer_size );


	pack32( (uint32_t)time(NULL), &current, &current_size ); /* FIXME What am I really suppose to put as the time?*/
	pack32( (uint32_t)list_count(steps), &current, &current_size );
	/* Pack the Steps */
	while( ( current_step = (struct step_record*)list_next( iterator ) ) != NULL )
	{
		pack_ctld_job_step_info( current_step, &current, &current_size ); 
		buffer_realloc( buffer_base, &current, buffer_size, &current_size );
	}
	
}


