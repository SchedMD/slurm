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
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024


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
pack_job_step_info_reponse_msg( List steps )
{
	
}
