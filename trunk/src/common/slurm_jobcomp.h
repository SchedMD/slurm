/*****************************************************************************\
 *  slurm_jobcomp.h - implementation-independent job completion logging 
 *  API definitions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.com> et. al.
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

#ifndef __SLURM_JOBCOMP_H__
#define __SLURM_JOBCOMP_H__

#if HAVE_STDINT_H
#  include <stdint.h>           /* for uint16_t, uint32_t definitions */
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>         /* for uint16_t, uint32_t definitions */
#endif
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct slurm_jobcomp_context * slurm_jobcomp_context_t;

/* initialization of job completion logging */
extern int g_slurm_jobcomp_init(char *jobcomp_loc);

/* write record of a job's completion */
extern int g_slurm_jobcomp_write(uint32_t job_id, uint32_t user_id, char *job_name, 
		char *job_state, char *partition, uint32_t time_limit,
		time_t start_end, time_t end_time, char *node_list);

/* return error code */
extern int g_slurm_jobcomp_errno(void);

#endif /*__SLURM_JOBCOMP_H__*/
