/*****************************************************************************\
 *  backup.c - backup slurm controller
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, Kevin Tew <tew1@llnl.gov>, et. al.
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
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include <slurm/slurm_errno.h>

#include "src/common/daemonize.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_auth.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

static int          _background_process_msg(slurm_msg_t * msg);
static void *       _background_rpc_mgr(void *no_data);
static void *       _background_signal_hand(void *no_data);
static int          _ping_controller(void);


/* run_backup - this is the backup controller, it should run in standby 
 *	mode, assuming control when the primary controller stops responding */
void run_backup(void)
{
	time_t last_controller_response = time(NULL), last_ping = 0;
	pthread_attr_t thread_attr_sig, thread_attr_rpc;

	info("slurmctld running in background mode");
	/* default: don't resume if shutdown */
	slurmctld_config.resume_backup = false;

	/*
	 * create attached thread for signal handling
	 */
	if (pthread_attr_init(&thread_attr_sig))
		fatal("pthread_attr_init error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if (pthread_attr_setscope(&thread_attr_sig, PTHREAD_SCOPE_SYSTEM))
		error("pthread_attr_setscope error %m");
#endif
	if (pthread_create(&slurmctld_config.thread_id_sig,
			   &thread_attr_sig, _background_signal_hand, NULL))
		fatal("pthread_create %m");

	/*
	 * create attached thread to process RPCs
	 */
	if (pthread_attr_init(&thread_attr_rpc))
		fatal("pthread_attr_init error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if (pthread_attr_setscope(&thread_attr_rpc, PTHREAD_SCOPE_SYSTEM))
		error("pthread_attr_setscope error %m");
#endif
	if (pthread_create(&slurmctld_config.thread_id_rpc, 
			&thread_attr_rpc, _background_rpc_mgr, NULL))
		fatal("pthread_create error %m");

	/* repeatedly ping ControlMachine */
	while (slurmctld_config.shutdown_time == 0) {
		sleep(5);	/* Give the primary slurmctld set-up time */
		if (difftime(time(NULL), last_ping) <
		    slurmctld_conf.heartbeat_interval)
			continue;

		last_ping = time(NULL);
		if (_ping_controller() == 0)
			last_controller_response = time(NULL);
		else if (difftime(time(NULL), last_controller_response) >
			 slurmctld_conf.slurmctld_timeout)
			break;
	}
	if (slurmctld_config.shutdown_time != 0) {
		pthread_join(slurmctld_config.thread_id_sig, NULL);
		info("BackupController terminating");
		if (unlink(slurmctld_conf.slurmctld_pidfile) < 0)
			error("Unable to remove pidfile '%s': %m",
		     	 slurmctld_conf.slurmctld_pidfile);
		log_fini();
		exit(0);
	}

	error("ControlMachine %s not responding, "
		"BackupController %s taking over",
		slurmctld_conf.control_machine,
		slurmctld_conf.backup_controller);
	pthread_kill(slurmctld_config.thread_id_sig, SIGTERM);
	pthread_join(slurmctld_config.thread_id_sig, NULL);

	/* clear old state and read new state */
	job_fini();
	if (read_slurm_conf(1))	/* Recover all state */
		fatal("Unable to recover slurm state");
	slurmctld_config.shutdown_time = (time_t) 0;
	return;
}

/* _background_signal_hand - Process daemon-wide signals for the 
 *	backup controller */
static void *_background_signal_hand(void *no_data)
{
	int sig;
	sigset_t set;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	create_pidfile(slurmctld_conf.slurmctld_pidfile);

	if (sigemptyset(&set))
		error("sigemptyset error: %m");
	if (sigaddset(&set, SIGINT))
		error("sigaddset error on SIGINT: %m");
	if (sigaddset(&set, SIGTERM))
		error("sigaddset error on SIGTERM: %m");
	if (sigaddset(&set, SIGABRT))
		error("sigaddset error on SIGABRT: %m");

	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0)
		fatal("sigprocmask error: %m");

	while (1) {
		sigwait(&set, &sig);
		switch (sig) {
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received");
			slurmctld_config.shutdown_time = time(NULL);
			/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
			slurmctld_shutdown();
			pthread_join(slurmctld_config.thread_id_rpc, NULL);

			return NULL;	/* Normal termination */
			break;
		case SIGABRT:	/* abort */
			fatal("SIGABRT received");
			break;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}
}

/* _background_rpc_mgr - Read and process incoming RPCs to the background 
 *	controller (that's us) */
static void *_background_rpc_mgr(void *no_data)
{
	slurm_fd newsockfd;
	slurm_fd sockfd;
	slurm_addr cli_addr;
	slurm_msg_t *msg = NULL;
	bool done_flag = false;
	int error_code;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_background_rpc_mgr pid = %u", getpid());

	/* initialize port for RPCs */
	if ((sockfd =
	     slurm_init_msg_engine_port(slurmctld_conf.slurmctld_port))
	    == SLURM_SOCKET_ERROR) {
		error("slurm_init_msg_engine_port error %m");
		exit(1);
	}

	/*
	 * Procss incoming RPCs indefinitely
	 */
	while (done_flag == false) {
		/* accept needed for stream implementation 
		 * is a no-op in message implementation that just passes 
		 * sockfd to newsockfd */
		if ((newsockfd = slurm_accept_msg_conn(sockfd, &cli_addr))
		    == SLURM_SOCKET_ERROR) {
			error("slurm_accept_msg_conn error %m");
			continue;
		}

		msg = xmalloc(sizeof(slurm_msg_t));
		msg->conn_fd = newsockfd;
		if (slurm_receive_msg(newsockfd, msg, 0) < 0)
			error("slurm_receive_msg error %m");
		else {
			error_code = _background_process_msg(msg);
			if ((error_code == SLURM_SUCCESS) &&
			    (msg->msg_type == REQUEST_SHUTDOWN_IMMEDIATE))
				done_flag = true;
		}
		slurm_free_msg(msg);

		/* close should only be called when the socket 
		 * implementation is being used the following call will 
		 * be a no-op in a message/mongo implementation */
		slurm_close_accepted_conn(newsockfd);	/* close new socket */
	}

	debug3("_background_rpc_mgr shutting down");
	slurm_close_accepted_conn(sockfd);	/* close the main socket */
	pthread_exit((void *) 0);
}

/* _background_process_msg - process an RPC to the backup_controller */
static int _background_process_msg(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;

	if (msg->msg_type != REQUEST_PING) {
		bool super_user = false;
		uid_t uid = g_slurm_auth_get_uid(msg->cred);
		if ((uid == 0) || (uid == getuid()))
			super_user = true;

		if (super_user && 
		    (msg->msg_type == REQUEST_SHUTDOWN_IMMEDIATE)) {
			info("Performing RPC: REQUEST_SHUTDOWN_IMMEDIATE");
		} else if (super_user && 
			   (msg->msg_type == REQUEST_SHUTDOWN)) {
			info("Performing RPC: REQUEST_SHUTDOWN");
			pthread_kill(slurmctld_config.thread_id_sig, SIGTERM);
		} else if (super_user && 
			   (msg->msg_type == REQUEST_CONTROL)) {
			debug3("Ignoring RPC: REQUEST_CONTROL");
		} else {
			error("Invalid RPC received %d from uid %u", 
			      msg->msg_type, uid);
			error_code = ESLURM_IN_STANDBY_MODE;
		}
	}
	if (msg->msg_type != REQUEST_SHUTDOWN_IMMEDIATE)
		slurm_send_rc_msg(msg, error_code);
	return error_code;
}

/* Ping primary ControlMachine
 * RET 0 if no error */
static int _ping_controller(void)
{
	int rc;
	slurm_msg_t req;

	debug3("pinging slurmctld at %s", slurmctld_conf.control_addr);

	/* 
	 *  Set address of controller to ping
	 */
	slurm_set_addr(&req.address, slurmctld_conf.slurmctld_port, 
	               slurmctld_conf.control_addr);


	req.msg_type = REQUEST_PING;

	if (slurm_send_recv_rc_msg(&req, &rc, 0) < 0) {
		error("_ping_controller/slurm_send_node_msg error: %m");
		return SLURM_ERROR;
	}

	if (rc) {
		error("_ping_controller/response error %d", rc);
		return SLURM_PROTOCOL_ERROR;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

