/*****************************************************************************\
 *  batch_mgr.c - functions for batch job management (spawn and monitor job)
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
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define EXTREME_DEBUG 1

#include <src/common/log.h>
#include <src/common/slurm_errno.h>
#include <src/common/slurm_protocol_defs.h>

#include <src/slurmd/batch_mgr.h>
#include <src/slurmd/slurmd.h>

void dump_batch_desc (batch_job_launch_msg_t *batch_job_launch_msg);

/* launch_batch_job - establish the environment and launch a batch job script */
int 
launch_batch_job (batch_job_launch_msg_t *batch_job_launch_msg)
{
#if EXTREME_DEBUG
	dump_batch_desc (batch_job_launch_msg);
#endif

	return SLURM_SUCCESS;
}

void
dump_batch_desc (batch_job_launch_msg_t *batch_job_launch_msg)
{
	int i;

	debug3 ("Launching batch job: job_id=%u, user_id=%u, nodes=%s",
		batch_job_launch_msg->job_id, batch_job_launch_msg->user_id, 
		batch_job_launch_msg->nodes);
	debug3 ("    work_dir=%s, stdin=%s",
		batch_job_launch_msg->work_dir, batch_job_launch_msg->stdin);
	debug3 ("    stdout=%s, stderr=%s",
		batch_job_launch_msg->stdout, batch_job_launch_msg->stderr);
	debug3 ("    script=%s", batch_job_launch_msg->script);
	for (i=0; i<batch_job_launch_msg->argc; i++) {
		debug3 ("    argv[%d]=%s", i, batch_job_launch_msg->argv[i]);
	}
	for (i=0; i<batch_job_launch_msg->env_size; i++) {
		debug3 ("    environment[%d]=%s", i, batch_job_launch_msg->environment[i]);
	}
}
