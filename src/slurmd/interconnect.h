/*****************************************************************************\
 *  interconnect.h -
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _SLURMD_INTERCONNECT_H_
#define _SLURMD_INTERCONNECT_H_

#include <pthread.h>

/* interconnect_init
 * called by launch_tasks to initialize the interconnect
 * IN launch_msg	- launch_tasks_msg
 * RET int		- return_code
 */
int interconnect_init ( launch_tasks_request_msg_t * launch_msg );

/* fan_out_task_launch
 * called by launch_tasks to do the task fan out
 * IN launch_msg	- launch_tasks_msg
 * RET int		- return_code
 */
int fan_out_task_launch ( launch_tasks_request_msg_t * launch_msg );

/* interconnect_set_capabilities
 * called by fan_out_task_launch to set interconnect capabilities
 * IN task_start	- task_start structure
 * RET int		- return_code
 */
int interconnect_set_capabilities ( task_start_t * task_start ) ;

/*
 * Set environment variables needed.
 */
int interconnect_env(char ***env, uint16_t *envc, int nodeid, int nnodes, 
	             int procid, int nprocs) ;

#endif
