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

#include "src/slurmd/slurmd.h"
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

	/* recieve job type from main slurmd */
	if((rc = read(STDIN_FILENO,&step_type,sizeof(int))) == -1) {
		error ("slurmd_step: couldn't read step_type: %m",
		       rc);
		exit(1);
	}
	info("got the number %d",step_type);
	
	/* recieve len of packed conf from main slurmd */
	if((rc = read(STDIN_FILENO, &len, sizeof(int))) == -1) {
		error ("slurmd_step: couldn't read len: %m",
		       rc);
		exit(1);
	}
	
	/* recieve packed conf from main slurmd */
	incoming_buffer = xmalloc(len);
	if((rc = read(STDIN_FILENO, incoming_buffer, 
		      sizeof(char)*len)) == -1) {
		error ("slurmd_step: couldn't read launch_req: %m",
		       rc);
		exit(1);
	}
	buffer = create_buf(incoming_buffer,len);
	conf = xmalloc(sizeof(*conf));
	if(unpack_slurmd_conf_lite_no_alloc(conf, buffer)
	   == SLURM_ERROR) {
		fatal("slurmd_step: problem with unpack of "
		       "slurmd_conf");
	}
	free_buf(buffer);
				
	debug2("running a launch_task %d.", conf->debug_level);
	conf->log_opts.stderr_level = conf->debug_level;
	conf->log_opts.logfile_level = conf->debug_level;
	conf->log_opts.syslog_level = conf->debug_level;
	/* forward the log options to slurmd_step */
	log_alter(conf->log_opts, SYSLOG_FACILITY_DAEMON, conf->logfile);
	g_slurmd_jobacct_init(conf->cf.job_acct_parameters);

	/* recieve len of packed cli from main slurmd */
	if((rc = read(STDIN_FILENO, &len, sizeof(int))) == -1) {
		error ("slurmd_step: couldn't read len: %m",
		       rc);
		exit(1);
	}

	/* recieve packed cli from main slurmd */
	incoming_buffer = xmalloc(len);
	if((rc = read(STDIN_FILENO, incoming_buffer, 
		      sizeof(char)*len)) == -1) {
		error ("slurmd_step: couldn't read launch_req: %m",
		       rc);
		exit(1);
	}

	buffer = create_buf(incoming_buffer,len);	
	cli = xmalloc(sizeof(slurm_addr));
	if(slurm_unpack_slurm_addr_no_alloc(cli, buffer)
	   == SLURM_ERROR) {
		fatal("slurmd_step: problem with unpack of "
		      "slurmd_conf");
	}
	free_buf(buffer);

	/* recieve len of packed self from main slurmd */
	if((rc = read(STDIN_FILENO, &len, sizeof(int))) == -1) {
		error ("slurmd_step: couldn't read len: %m",
		       rc);
		exit(1);
	}
	
	/* recieve packed self from main slurmd */
	incoming_buffer = xmalloc(len);
	if((rc = read(STDIN_FILENO, incoming_buffer, 
		      sizeof(char)*len)) == -1) {
		error ("slurmd_step: couldn't read launch_req: %m",
		       rc);
		exit(1);
	}
	buffer = create_buf(incoming_buffer,len);
		
	self = xmalloc(sizeof(slurm_addr));
	if(slurm_unpack_slurm_addr_no_alloc(self, buffer)
	   == SLURM_ERROR) {
		fatal("slurmd_step: problem with unpack of "
		      "slurmd_conf");
	}
	free_buf(buffer);

	/* recieve len of packed req from main slurmd */
	if((rc = read(STDIN_FILENO, &len, sizeof(int))) == -1) {
		fatal("slurmd_step: couldn't read len: %m",
		      rc);
	}
	
	/* recieve len of packed req from main slurmd */
	incoming_buffer = xmalloc(len);
	if((rc = read(STDIN_FILENO, incoming_buffer, 
		      sizeof(char)*len)) == -1) {
		error ("slurmd_step: couldn't read launch_req: %m",
		       rc);
		exit(1);
	}
	buffer = create_buf(incoming_buffer,len);

	/* determine type and unpack appropriately */
	switch(step_type) {
	case LAUNCH_BATCH_JOB:
		debug2("running a batch_job");
		msg->msg_type = REQUEST_BATCH_JOB_LAUNCH;
		if(unpack_msg(msg, buffer) == SLURM_ERROR) 
			fatal("slurmd_step: we didn't unpack the "
			      "request correctly");
		
		free_buf(buffer);
		exit (mgr_launch_batch_job(msg->data, cli));
		break;
	case LAUNCH_TASKS:
		info("running a launch_task");
		msg->msg_type = REQUEST_LAUNCH_TASKS;
		if(unpack_msg(msg, buffer) == SLURM_ERROR) 
			fatal("slurmd_step: we didn't unpack the "
			      "request correctly");
				
		launch_req = msg->data;
		free_buf(buffer);
		debug2("running a launch_task %d.",launch_req->job_id);
		exit (mgr_launch_tasks(msg->data, cli, self));
		break;
	case SPAWN_TASKS:
		debug2("running a spawn_task");
		msg->msg_type = REQUEST_SPAWN_TASK;
		if(unpack_msg(msg, buffer) == SLURM_ERROR) 
			fatal("slurmd_step: we didn't unpack the "
			      "request correctly");
		free_buf(buffer);
		exit (mgr_spawn_task(msg->data, cli, self));
		break;
	default:
		fatal("Was sent a task I didn't understand");
		break;
	}
	


	return 0;
}
