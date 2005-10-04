/*****************************************************************************\
 *  src/slurmd/slurmd_step.c - grandchild for main slurm to avoid glib c fork
 *  issue
 *  $Id:$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <da@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/slurmd/mgr.h"
#include "src/common/xmalloc.h"

int 
main (int argc, char *argv[])
{
	batch_job_launch_msg_t *batch_req = NULL;
	launch_tasks_request_msg_t *launch_req = NULL;
	spawn_task_request_msg_t *spawn_req = NULL;
	int rc;	
	slurm_addr *cli = NULL;
	slurm_addr *self = NULL;
	int step_type;
	char c;
	int len;
	char *incoming_buffer = NULL;
	slurm_msg_t *msg = xmalloc(sizeof(slurm_msg_t));
	Buf buffer = init_buf(0);

	if((rc = read(STDIN_FILENO,&step_type,sizeof(int))) == -1) {
		error ("slurmd_step: couldn't read step_type: %m",
		       rc);
		exit(1);
	}
	info("got the number %d",step_type);
	
	switch(step_type) {
	case LAUNCH_BATCH_JOB:
		if((rc = read(STDIN_FILENO,&batch_req,
			      sizeof(batch_job_launch_msg_t))) == -1) {
			error ("slurmd_step: couldn't read batch_req: %m",
			       rc);
			exit(1);
		}
		if((rc = read(STDIN_FILENO,&cli,
			      sizeof(slurm_addr))) == -1) {
			error ("slurmd_step: couldn't read slurm_addr 1: %m",
			       rc);
			exit(1);
		}
		debug2("running a batch_job");
		exit (mgr_launch_batch_job(batch_req, cli));
		break;
	case LAUNCH_TASKS:
		//launch_req = xmalloc(sizeof(launch_tasks_request_msg_t));
		info("running a launch_task");
		if((rc = read(STDIN_FILENO, &len, sizeof(int))) == -1) {
			error ("slurmd_step: couldn't read len: %m",
			       rc);
			exit(1);
		}
		incoming_buffer = xmalloc(len);
		if((rc = read(STDIN_FILENO, incoming_buffer, 
			      sizeof(char)*len)) == -1) {
			error ("slurmd_step: couldn't read launch_req: %m",
			       rc);
			exit(1);
		}
		buffer = create_buf(incoming_buffer,len);
		msg->msg_type = REQUEST_LAUNCH_TASKS;
		unpack_msg(msg, buffer);
		launch_req = msg->data;
		/* if((rc = read(STDIN_FILENO,&launch_req->uid, */
/* 			      sizeof(uint32_t))) == -1) { */
/* 			error ("slurmd_step: couldn't read launch_req: %m", */
/* 			       rc); */
/* 			exit(1); */
/* 		} */
		info("running a launch_task %d.",launch_req->job_id);
/* 		if((rc = read(STDIN_FILENO,&launch_req->gid, */
/* 			  sizeof(uint32_t))) */
/* 		   == -1) { */
/* 			error ("fork_slurmd: couldn't read " */
/* 			       "gid: %m", rc); */
/* 			exit(1); */
/* 		} */
/* 		if((rc = read(STDIN_FILENO,&launch_req->tasks_to_launch, */
/* 			  sizeof(uint32_t))) */
/* 		   == -1) { */
/* 			error ("fork_slurmd: couldn't read " */
/* 			       "tasks_to_launch: %m", rc); */
/* 			exit(1); */
/* 		} */
/* 		if((rc = read(STDIN_FILENO,&launch_req->nprocs, */
/* 			  sizeof(uint32_t))) */
/* 		   == -1) { */
/* 			error ("fork_slurmd: couldn't read " */
/* 			       "nprocs: %m", rc); */
/* 			exit(1); */
/* 		} */
/* 		if((rc = read(STDIN_FILENO,&launch_req->job_id, */
/* 			  sizeof(uint32_t))) */
/* 		   == -1) { */
/* 			error ("fork_slurmd: couldn't read " */
/* 			       "job_id: %m", rc); */
/* 			exit(1); */
/* 		} */
/* 		if((rc = read(STDIN_FILENO,&launch_req->job_step_id, */
/* 			  sizeof(uint32_t))) */
/* 		   == -1) { */
/* 			error ("fork_slurmd: couldn't read " */
/* 			       "job_step_id: %m", rc); */
/* 			exit(1); */
/* 		} */
		free_buf(buffer);		
		if((rc = read(STDIN_FILENO, &len, sizeof(int))) == -1) {
			error ("slurmd_step: couldn't read len: %m",
			       rc);
			exit(1);
		}
		incoming_buffer = xmalloc(len);
		if((rc = read(STDIN_FILENO, incoming_buffer, 
			      sizeof(char)*len)) == -1) {
			error ("slurmd_step: couldn't read launch_req: %m",
			       rc);
			exit(1);
		}
		buffer = create_buf(incoming_buffer,len);
	
		cli = xmalloc(sizeof(slurm_addr));
		slurm_unpack_slurm_addr_no_alloc(cli, buffer);

		free_buf(buffer);
		if((rc = read(STDIN_FILENO, &len, sizeof(int))) == -1) {
			error ("slurmd_step: couldn't read len: %m",
			       rc);
			exit(1);
		}
		incoming_buffer = xmalloc(len);
		if((rc = read(STDIN_FILENO, incoming_buffer, 
			      sizeof(char)*len)) == -1) {
			error ("slurmd_step: couldn't read launch_req: %m",
			       rc);
			exit(1);
		}
		buffer = create_buf(incoming_buffer,len);
		
		self = xmalloc(sizeof(slurm_addr));
		slurm_unpack_slurm_addr_no_alloc(self, buffer);

		info("running a launch_task");
		
		exit (mgr_launch_tasks(launch_req, cli, self));
		break;
	case SPAWN_TASKS:
		if((rc = read(STDIN_FILENO,&launch_req,
			      sizeof(spawn_task_request_msg_t))) == -1) {
			error ("slurmd_step: couldn't read spawn_req: %m",
			       rc);
			exit(1);
		}
		if((rc = read(STDIN_FILENO,&cli,
			      sizeof(slurm_addr))) == -1) {
			error ("slurmd_step: couldn't read slurm_addr 1: %m",
			       rc);
			exit(1);
		}
		if((rc = read(STDIN_FILENO,&self,
			      sizeof(slurm_addr))) == -1) {
			error ("slurmd_step: couldn't read slurm_addr 2: %m",
			       rc);
			exit(1);
		}
		debug2("running a spawn_task");
		exit (mgr_spawn_task(spawn_req, cli, self));
		break;
	default:
		fatal("Was sent a task I didn't understand");
		break;
	}
	return 0;
}
