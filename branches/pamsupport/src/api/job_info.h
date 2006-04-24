/*****************************************************************************\
 *  job_info.h - get/print the job state information of slurm
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  UCRL-CODE-217948.
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

#ifndef _JOB_INFO_H
#define _JOB_INFO_H

#include <stdint.h>

/*
 * slurm_make_time_str - convert time_t to string "month/date hour:min:sec" 
 * IN time - a time stamp
 * OUT string - pointer user defined buffer
 */
extern void slurm_make_time_str (time_t *time, char *string);

#define READY_JOB_FATAL  -2
#define READY_JOB_ERROR  -1
#define READY_NODE_STATE 0x01
#define READY_JOB_STATE  0x02

/*
 * slurm_job_node_ready - report if nodes are ready for job to execute now
 * IN job_id - slurm job id
 * RET: READY_* values as defined in api/job_info.h
 */
extern int slurm_job_node_ready(uint32_t job_id);

#endif
