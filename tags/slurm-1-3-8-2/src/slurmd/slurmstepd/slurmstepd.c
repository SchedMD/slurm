/*****************************************************************************\
 *  src/slurmd/slurmstepd/slurmstepd.c - SLURM job-step manager.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov> 
 *  and Christopher Morrone <morrone2@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/switch.h"
#include "src/common/stepd_api.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/common/slurmstepd_init.h"
#include "src/slurmd/common/setproctitle.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/mgr.h"
#include "src/slurmd/slurmstepd/req.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

static int _init_from_slurmd(int sock, char **argv, slurm_addr **_cli,
			     slurm_addr **_self, slurm_msg_t **_msg,
			     int *_ngids, gid_t **_gids);

static void _send_ok_to_slurmd(int sock);
static void _send_fail_to_slurmd(int sock);
static slurmd_job_t *_step_setup(slurm_addr *cli, slurm_addr *self,
				 slurm_msg_t *msg);
static void _step_cleanup(slurmd_job_t *job, slurm_msg_t *msg, int rc);

int slurmstepd_blocked_signals[] = {
	SIGPIPE, 0
};

/* global variable */
slurmd_conf_t * conf;

int 
main (int argc, char *argv[])
{
	slurm_addr *cli;
	slurm_addr *self;
	slurm_msg_t *msg;
	slurmd_job_t *job;
	int ngids;
	gid_t *gids;
	int rc = 0;

	xsignal_block(slurmstepd_blocked_signals);
	conf = xmalloc(sizeof(*conf));
	conf->argv = &argv;
	conf->argc = &argc;
	init_setproctitle(argc, argv);
	_init_from_slurmd(STDIN_FILENO, argv, &cli, &self, &msg,
			  &ngids, &gids);

	/* Fancy way of closing stdin that keeps STDIN_FILENO from being
	 * allocated to any random file.  The slurmd already opened /dev/null
	 * on STDERR_FILENO for us. */
	dup2(STDERR_FILENO, STDIN_FILENO);

	job = _step_setup(cli, self, msg);
	job->ngids = ngids;
	job->gids = gids;

	list_install_fork_handlers();
	slurm_conf_install_fork_handlers();
	/* sets job->msg_handle and job->msgid */
	if (msg_thr_create(job) == SLURM_ERROR) {
		_send_fail_to_slurmd(STDOUT_FILENO);
		rc = SLURM_FAILURE;
		goto ending;
	}

	_send_ok_to_slurmd(STDOUT_FILENO);

	/* Fancy way of closing stdout that keeps STDOUT_FILENO from being
	 * allocated to any random file.  The slurmd already opened /dev/null
	 * on STDERR_FILENO for us. */
	dup2(STDERR_FILENO, STDOUT_FILENO);

	rc = job_manager(job); /* blocks until step is complete */

	/* signal the message thread to shutdown, and wait for it */
	eio_signal_shutdown(job->msg_handle);
	pthread_join(job->msgid, NULL);

ending:
	_step_cleanup(job, msg, rc);

	xfree(cli);
	xfree(self);
	xfree(conf->hostname);
	xfree(conf->spooldir);
	xfree(conf->node_name);
	xfree(conf->logfile);
	xfree(conf);
	info("done with job");
	return rc;
}

static void
_send_ok_to_slurmd(int sock)
{
	int ok = SLURM_SUCCESS;
	safe_write(sock, &ok, sizeof(int));
	return;
rwfail:
	error("Unable to send \"ok\" to slurmd");
}

static void
_send_fail_to_slurmd(int sock)
{
	int fail = SLURM_FAILURE;

	if (errno)
		fail = errno;
	safe_write(sock, &fail, sizeof(int));
	return;
rwfail:
	error("Unable to send \"fail\" to slurmd");
}

/*
 *  This function handles the initialization information from slurmd
 *  sent by _send_slurmstepd_init() in src/slurmd/slurmd/req.c.
 */
static int
_init_from_slurmd(int sock, char **argv,
		  slurm_addr **_cli, slurm_addr **_self, slurm_msg_t **_msg,
		  int *_ngids, gid_t **_gids)
{
	char *incoming_buffer = NULL;
	Buf buffer;
	int step_type;
	int len;
	slurm_addr *cli = NULL;
	slurm_addr *self = NULL;
	slurm_msg_t *msg = NULL;
	int ngids = 0;
	gid_t *gids = NULL;
	uint16_t port;
	char buf[16];

	/* receive job type from slurmd */
	safe_read(sock, &step_type, sizeof(int));
	debug3("step_type = %d", step_type);
	
	/* receive reverse-tree info from slurmd */
	pthread_mutex_lock(&step_complete.lock);
	safe_read(sock, &step_complete.rank, sizeof(int));
	safe_read(sock, &step_complete.parent_rank, sizeof(int));
	safe_read(sock, &step_complete.children, sizeof(int));
	safe_read(sock, &step_complete.depth, sizeof(int));
	safe_read(sock, &step_complete.max_depth, sizeof(int));
	safe_read(sock, &step_complete.parent_addr, sizeof(slurm_addr));
	step_complete.bits = bit_alloc(step_complete.children);
	step_complete.jobacct = jobacct_gather_g_create(NULL);
	pthread_mutex_unlock(&step_complete.lock);

	/* receive conf from slurmd */
	safe_read(sock, &len, sizeof(int));
	incoming_buffer = xmalloc(len);
	safe_read(sock, incoming_buffer, len);
	buffer = create_buf(incoming_buffer,len);
	if(unpack_slurmd_conf_lite_no_alloc(conf, buffer) == SLURM_ERROR) {
		fatal("slurmstepd: problem with unpack of slurmd_conf");
	}
	free_buf(buffer);
				
	debug2("debug level is %d.", conf->debug_level);
	conf->log_opts.stderr_level = conf->debug_level;
	conf->log_opts.logfile_level = conf->debug_level;
	conf->log_opts.syslog_level = conf->debug_level;
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

	log_init(argv[0], conf->log_opts, LOG_DAEMON, conf->logfile);
	/* acct info */
	jobacct_gather_g_startpoll(conf->job_acct_gather_freq);
	
	switch_g_slurmd_step_init();

	slurm_get_ip_str(&step_complete.parent_addr, &port, buf, 16);
	debug3("slurmstepd rank %d, parent address = %s, port = %u",
	       step_complete.rank, buf, port);

	/* receive cli from slurmd */
	safe_read(sock, &len, sizeof(int));
	incoming_buffer = xmalloc(sizeof(char) * len);
	safe_read(sock, incoming_buffer, len);
	buffer = create_buf(incoming_buffer,len);	
	cli = xmalloc(sizeof(slurm_addr));
	if(slurm_unpack_slurm_addr_no_alloc(cli, buffer) == SLURM_ERROR) {
		fatal("slurmstepd: problem with unpack of slurmd_conf");
	}
	free_buf(buffer);

	/* receive self from slurmd */
	safe_read(sock, &len, sizeof(int));
	if(len > 0) {
		/* receive packed self from main slurmd */
		incoming_buffer = xmalloc(sizeof(char) * len);
		safe_read(sock, incoming_buffer, len);
		buffer = create_buf(incoming_buffer,len);
		self = xmalloc(sizeof(slurm_addr));
		if(slurm_unpack_slurm_addr_no_alloc(self, buffer)
		   == SLURM_ERROR) {
			fatal("slurmstepd: problem with unpack of "
			      "slurmd_conf");
		}
		free_buf(buffer);
	}
		
	/* receive req from slurmd */
	safe_read(sock, &len, sizeof(int));
	incoming_buffer = xmalloc(sizeof(char) * len);
	safe_read(sock, incoming_buffer, len);
	buffer = create_buf(incoming_buffer,len);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);

	switch(step_type) {
	case LAUNCH_BATCH_JOB:
		msg->msg_type = REQUEST_BATCH_JOB_LAUNCH;
		break;
	case LAUNCH_TASKS:
		msg->msg_type = REQUEST_LAUNCH_TASKS;
		break;
	default:
		fatal("Unrecognized launch RPC");
		break;
	}
	if(unpack_msg(msg, buffer) == SLURM_ERROR) 
		fatal("slurmstepd: we didn't unpack the request correctly");
	free_buf(buffer);

	/* receive cached group ids array for the relevant uid */
	safe_read(sock, &ngids, sizeof(int));
	if (ngids > 0) {
		int i;
		uint32_t tmp32;

		gids = (gid_t *)xmalloc(sizeof(gid_t) * ngids);
		for (i = 0; i < ngids; i++) {
			safe_read(sock, &tmp32, sizeof(uint32_t));
			gids[i] = (gid_t)tmp32;
			debug2("got gid %d", gids[i]);
		}
	}

	*_cli = cli;
	*_self = self;
	*_msg = msg;
	*_ngids = ngids;
	*_gids = gids;

	return 1;

rwfail:
	fatal("Error reading initialization data from slurmd");
	exit(1);
}

static slurmd_job_t *
_step_setup(slurm_addr *cli, slurm_addr *self, slurm_msg_t *msg)
{
	slurmd_job_t *job = NULL;

	switch(msg->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		debug2("setup for a batch_job");
		job = mgr_launch_batch_job_setup(msg->data, cli);
		break;
	case REQUEST_LAUNCH_TASKS:
		debug2("setup for a launch_task");
		job = mgr_launch_tasks_setup(msg->data, cli, self);
		break;
	default:
		fatal("handle_launch_message: Unrecognized launch RPC");
		break;
	}
	if(!job) {
		fatal("_step_setup: no job returned");
	}
	job->jmgr_pid = getpid();
	job->jobacct = jobacct_gather_g_create(NULL);
	
	return job;
}

static void
_step_cleanup(slurmd_job_t *job, slurm_msg_t *msg, int rc)
{
	jobacct_gather_g_destroy(job->jobacct);
	if (!job->batch)
		job_destroy(job);
	/* 
	 * The message cannot be freed until the jobstep is complete
	 * because the job struct has pointers into the msg, such
	 * as the switch jobinfo pointer.
	 */
	switch(msg->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		slurm_free_job_launch_msg(msg->data);
		break;
	case REQUEST_LAUNCH_TASKS:
		slurm_free_launch_tasks_request_msg(msg->data);
		break;
	default:
		fatal("handle_launch_message: Unrecognized launch RPC");
		break;
	}
	jobacct_gather_g_destroy(step_complete.jobacct);
	
	xfree(msg);
}
