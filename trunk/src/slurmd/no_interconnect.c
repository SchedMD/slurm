/*****************************************************************************\
 *  no_interconnect.c - Manage user task communications without an high-speed
 *	interconnect
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

#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/task_mgr.h>
#include <src/slurmd/interconnect.h>

/* exported module funtion to launch tasks */
/*launch_tasks should really be named launch_job_step*/
int launch_tasks ( launch_tasks_request_msg_t * launch_msg )
{
	pthread_atfork ( NULL , NULL , pthread_fork_child_after ) ;
	return interconnect_init ( launch_msg );
}

/* Contains interconnect specific setup instructions and then calls 
 * fan_out_task_launch */
int interconnect_init ( launch_tasks_request_msg_t * launch_msg )
{
	return fan_out_task_launch ( launch_msg ) ;
}

int interconnect_set_capabilities ( task_start_t * task_start ) 
{
	return SLURM_SUCCESS ;
}

/*
 * Set env variables needed for this interconnect
 */
int interconnect_env(char ***env, uint16_t *envc, int nodeid, int nnodes, 
	             int procid, int nprocs)
{
	return SLURM_SUCCESS ;
}


