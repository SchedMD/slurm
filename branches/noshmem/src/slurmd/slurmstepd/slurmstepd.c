/*****************************************************************************\
 *  src/slurmd/slurmstepd/slurmstepd.c - SLURM job-step manager.
 *  $Id: $
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

#include <unistd.h>
#include <stdlib.h>

#include "src/common/xmalloc.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/common/slurmstepd_init.h"
#include "src/slurmd/slurmstepd/mgr.h"

static int init_from_slurmd(char **argv, slurm_addr **_cli,
			    slurm_addr **_self, slurm_msg_t **_msg);
static int handle_launch_message(slurm_addr *cli, slurm_addr *self,
				 slurm_msg_t *msg);

int 
main (int argc, char *argv[])
{
	slurm_addr *cli;
	slurm_addr *self;
	slurm_msg_t *msg;

	conf = xmalloc(sizeof(*conf));
	conf->argv = &argv;
	conf->argc = &argc;
	conf->task_prolog = slurm_get_task_prolog();
	conf->task_epilog = slurm_get_task_epilog();
	init_setproctitle(argc, argv);
	if (slurm_proctrack_init() != SLURM_SUCCESS)
		return SLURM_FAILURE;

	init_from_slurmd(argv, &cli, &self, &msg);

	handle_launch_message(cli, self, msg);

	xfree(cli);
	xfree(self);
	xfree(conf->hostname);
	xfree(conf->spooldir);
	xfree(conf->node_name);
	xfree(conf->logfile);
	xfree(conf->cf.job_acct_parameters);
	xfree(conf);
	slurm_free_msg(msg);

	return 0;
}

static int
init_from_slurmd(char **argv,
		 slurm_addr **_cli, slurm_addr **_self, slurm_msg_t **_msg)
{
	int sock = STDIN_FILENO;
	char *incoming_buffer = NULL;
	Buf buffer;
	int step_type;
	int len;
	int rc;	
	char c;
	slurm_addr *cli = NULL;
	slurm_addr *self = NULL;
	slurm_msg_t *msg = NULL;

	/* receive job type from main slurmd */
	if((rc = read(sock,&step_type,sizeof(int))) == -1) {
		error ("slurmd_step: couldn't read step_type: %m",
		       rc);
		exit(1);
	}
	debug3("got the number %d",step_type);
	
	/* receive len of packed conf from main slurmd */
	if((rc = read(sock, &len, sizeof(int))) == -1) {
		fatal("slurmd_step: couldn't read len: %m",
		      rc);
	}
	
	/* receive packed conf from main slurmd */
	incoming_buffer = xmalloc(len);
	if((rc = read(sock, incoming_buffer, 
		      sizeof(char)*len)) == -1) {
		fatal("slurmd_step: couldn't read launch_req: %m",
		      rc);
	}
	buffer = create_buf(incoming_buffer,len);
	if(unpack_slurmd_conf_lite_no_alloc(conf, buffer)
	   == SLURM_ERROR) {
		fatal("slurmd_step: problem with unpack of "
		       "slurmd_conf");
	}
	free_buf(buffer);
				
	debug2("debug level is %d.", conf->debug_level);
	conf->log_opts.stderr_level = conf->debug_level;
	conf->log_opts.logfile_level = conf->debug_level;
	conf->log_opts.syslog_level = conf->debug_level;
	/* forward the log options to slurmd_step */
	//log_alter(conf->log_opts, 0, NULL);
	/*
	 * If daemonizing, turn off stderr logging -- also, if
	 * logging to a file, turn off syslog.
	 *
	 * Otherwise, if remaining in foreground, turn off logging
	 * to syslog (but keep logfile level)
	 */
	if (conf->daemonize) {
		conf->log_opts.stderr_level = LOG_LEVEL_QUIET;
		if (conf->logfile)
			conf->log_opts.syslog_level = LOG_LEVEL_QUIET;
	} else 
		conf->log_opts.syslog_level  = LOG_LEVEL_QUIET;

	log_init(argv[0],conf->log_opts, LOG_DAEMON, conf->logfile);
	g_slurmd_jobacct_init(conf->cf.job_acct_parameters);
	switch_g_slurmd_step_init();

	/* receive len of packed cli from main slurmd */
	if((rc = read(sock, &len, sizeof(int))) == -1) {
		error ("slurmd_step: couldn't read len: %m",
		       rc);
		exit(1);
	}

	/* receive packed cli from main slurmd */
	incoming_buffer = xmalloc(len);
	if((rc = read(sock, incoming_buffer, 
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

	/* receive len of packed self from main slurmd */
	if((rc = read(sock, &len, sizeof(int))) == -1) {
		error ("slurmd_step: couldn't read len: %m",
		       rc);
		exit(1);
	}
	
	if(len > 0) {
		/* receive packed self from main slurmd */
		incoming_buffer = xmalloc(len);
		if((rc = read(sock, incoming_buffer, 
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
	}
		
	/* receive len of packed req from main slurmd */
	if((rc = read(sock, &len, sizeof(int))) == -1) {
		fatal("slurmd_step: couldn't read len: %m",
		      rc);
	}

	/* receive len of packed req from main slurmd */
	incoming_buffer = xmalloc(len);
	if((rc = read(sock, incoming_buffer, 
		      sizeof(char)*len)) == -1) {
		error ("slurmd_step: couldn't read launch_req: %m",
		       rc);
		exit(1);
	}
	buffer = create_buf(incoming_buffer,len);

	msg = xmalloc(sizeof(slurm_msg_t));
	switch(step_type) {
	case LAUNCH_BATCH_JOB:
		msg->msg_type = REQUEST_BATCH_JOB_LAUNCH;
		break;
	case LAUNCH_TASKS:
		msg->msg_type = REQUEST_LAUNCH_TASKS;
		break;
	case SPAWN_TASKS:
		msg->msg_type = REQUEST_SPAWN_TASK;
		break;
	default:
		fatal("Unrecognized launch/spawn RPC");
		break;
	}
	if(unpack_msg(msg, buffer) == SLURM_ERROR) 
		fatal("slurmd_step: we didn't unpack the request correctly");
	free_buf(buffer);

	*_cli = cli;
	*_self = self;
	*_msg = msg;
}

static int
handle_launch_message(slurm_addr *cli, slurm_addr *self, slurm_msg_t *msg)
{
	int rc;

	switch(msg->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		debug2("running a batch_job");
		rc = mgr_launch_batch_job(msg->data, cli);
		slurm_free_job_launch_msg(msg->data);
		break;
	case REQUEST_LAUNCH_TASKS:
		debug2("running a launch_task");
		rc = mgr_launch_tasks(msg->data, cli, self);
		slurm_free_launch_tasks_request_msg(msg->data);
		break;
	case REQUEST_SPAWN_TASK:
		debug2("running a spawn_task");
		rc = mgr_spawn_task(msg->data, cli, self);
		slurm_free_spawn_task_request_msg(msg->data);
		break;
	default:
		fatal("handle_launch_message: Unrecognized launch/spawn RPC");
		break;
	}
}
