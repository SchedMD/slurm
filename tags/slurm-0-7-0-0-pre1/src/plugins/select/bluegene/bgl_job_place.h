/*****************************************************************************\
 *  bgl_job_place.h - header for blue gene job placement (e.g. base partition 
 *  selection) functions. 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> et. al.
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

#ifndef _BGL_JOB_PLACE_H_
#define _BGL_JOB_PLACE_H_

#include "src/slurmctld/slurmctld.h"

/*
 * Try to find resources for a given job request
 * IN job_ptr - pointer to job record in slurmctld
 * IN/OUT bitmap - nodes availble for assignment to job, clear those not to 
 *	be used
 * IN min_nodes, max_nodes  - minimum and maximum number of nodes to allocate 
 *	to this job (considers slurm partition limits)
 * RET - SLURM_SUCCESS if job runnable now, error code otherwise 
 */
extern int submit_job(struct job_record *job_ptr, bitstr_t *bitmap,
	       int min_nodes, int max_nodes);

#endif /* _BGL_JOB_PLACE_H_ */
