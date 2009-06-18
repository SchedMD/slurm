/*****************************************************************************\
 *  backup.c - backup slurm controller
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
#include "src/common/switch.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/slurmctld.h"

#ifndef VOLATILE
#if defined(__STDC__) || defined(__cplusplus)
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif

#define SHUTDOWN_WAIT     2	/* Time to wait for primary server shutdown */

static int          _background_process_msg(slurm_msg_t * msg);
static int          _backup_reconfig(void);
static void *       _background_rpc_mgr(void *no_data);
static void *       _background_signal_hand(void *no_data);
static int          _ping_controller(void);
inline static void  _update_cred_key(void);
static int          _shutdown_primary_controller(int wait_time);

/* Local variables */
static bool          dump_core = false;
static VOLATILE bool takeover = false;

/*
 * Static list of signals to block in this process
 * *Must be zero-terminated*
 */
static int backup_sigarray[] = {
	SIGINT,  SIGTERM, SIGCHLD, SIGUSR1,
	SIGUSR2, SIGTSTP, SIGXCPU, SIGQUIT,
	SIGPIPE, SIGALRM, SIGABRT, SIGHUP, 0
};

/* run_backup - this is the backup controller, it should run in standby 
 *	mode, assuming control when the primary controller stops responding */
void run_backup(void)
{
	time_t last_controller_response = time(NULL), last_ping = 0;
	pthread_attr_t thread_attr_sig, thread_attr_rpc;
	slurmctld_lock_t config_read_lock = { 
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	info("slurmctld running in background mode");
	takeover = false;

	/* default: don't resume if shutdown */
	slurmctld_config.resume_backup = false;
	if (xsignal_block(backup_sigarray) < 0)
		error("Unable to block signals");

	/*
	 * create attached thread to process RPCs
	 */
	slurm_attr_init(&thread_attr_rpc);
	while (pthread_create(&slurmctld_config.thread_id_rpc, 
			      &thread_attr_rpc, _background_rpc_mgr, NULL)) {
		error("pthread_create error %m");
		sleep(1);
	}
	slurm_attr_destroy(&thread_attr_rpc);

	/*
	 * create attached thread for signal handling
	 */
	slurm_attr_init(&thread_attr_sig);
	while (pthread_create(&slurmctld_config.thread_id_sig, 
			      &thread_attr_sig, _background_signal_hand, 
			      NULL)) {
		error("pthread_create %m");
		sleep(1);
	}
	slurm_attr_destroy(&thread_attr_sig);

	sleep(5);       /* Give the primary slurmctld set-up time */
	/* repeatedly ping ControlMachine */
	while (slurmctld_config.shutdown_time == 0) {
		sleep(1);
		/* Lock of slurmctld_conf below not important */
		if (slurmctld_conf.slurmctld_timeout &&
		    (takeover == false) &&
		    (difftime(time(NULL), last_ping) <
		     (slurmctld_conf.slurmctld_timeout / 3)))
			continue;

		last_ping = time(NULL);
		if (_ping_controller() == 0)
			last_controller_response = time(NULL);
		else if ( takeover == true ) {
			/* in takeover mode, take control as soon as */
			/* primary no longer respond */
			break;
		} else {
			uint32_t timeout;
			lock_slurmctld(config_read_lock);
			timeout = slurmctld_conf.slurmctld_timeout;
			unlock_slurmctld(config_read_lock);

			if (difftime(time(NULL), last_controller_response) >
			    timeout) {
				break;
			}
		}
	}

	/* Since pidfile is created as user root (its owner is
	 *   changed to SlurmUser) SlurmUser may not be able to 
	 *   remove it, so this is not necessarily an error. 
	 * No longer need slurmctld_conf lock after above join. */
	if (unlink(slurmctld_conf.slurmctld_pidfile) < 0)
		verbose("Unable to remove pidfile '%s': %m",
			slurmctld_conf.slurmctld_pidfile);

	if (slurmctld_config.shutdown_time != 0) {
		info("BackupController terminating");
		pthread_join(slurmctld_config.thread_id_sig, NULL);
		log_fini();
		if (dump_core)
			abort();
		else
			exit(0);
	}

	lock_slurmctld(config_read_lock);
	error("ControlMachine %s not responding, "
		"BackupController %s taking over",
		slurmctld_conf.control_machine,
		slurmctld_conf.backup_controller);
	unlock_slurmctld(config_read_lock);

	pthread_kill(slurmctld_config.thread_id_sig, SIGTERM);
	pthread_join(slurmctld_config.thread_id_sig, NULL);
	pthread_join(slurmctld_config.thread_id_rpc, NULL);

	/* clear old state and read new state */
	job_fini();
	if (switch_restore(slurmctld_conf.state_save_location, true)) {
		error("failed to restore switch state");
		abort();
	}
	if (read_slurm_conf(2)) {	/* Recover all state */
		error("Unable to recover slurm state");
		abort();
	}
	slurmctld_config.shutdown_time = (time_t) 0;
	return;
}

/* _background_signal_hand - Process daemon-wide signals for the 
 *	backup controller */
static void *_background_signal_hand(void *no_data)
{
	int sig, rc;
	sigset_t set;
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = { 
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	/* No need for slurmctld_conf lock yet */
	while ( (create_pidfile(slurmctld_conf.slurmctld_pidfile) < 0) && 
	        (errno == EAGAIN) ) {
		verbose("Retrying create_pidfile: %m");
		sleep(1);
	}

	while (slurmctld_config.shutdown_time == 0) {
		xsignal_sigset_create(backup_sigarray, &set);
		rc = sigwait(&set, &sig);
		if (rc == EINTR)
			continue;
		switch (sig) {
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received");
			slurmctld_config.shutdown_time = time(NULL);
			slurmctld_shutdown();
			return NULL;	/* Normal termination */
			break;
		case SIGHUP:    /* kill -1 */
			info("Reconfigure signal (SIGHUP) received");
			/*
			 * XXX - need to shut down the scheduler
			 * plugin, re-read the configuration, and then
			 * restart the (possibly new) plugin.
			 */
			lock_slurmctld(config_write_lock);
			rc = _backup_reconfig();
			if (rc)
				error("_backup_reconfig: %s",
					slurm_strerror(rc));
			else {
				/* Leave config lock set through this */
				_update_cred_key();
			}
			unlock_slurmctld(config_write_lock);
			break;
		case SIGABRT:   /* abort */
			info("SIGABRT received");
			slurmctld_config.shutdown_time = time(NULL);
			slurmctld_shutdown();
			dump_core = true;
			return NULL;    /* Normal termination */
			break;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}
	return NULL;
}

/* Reset the job credential key based upon configuration parameters.
 * slurmctld_conf is locked on entry. */
static void _update_cred_key(void)
{	
	slurm_cred_ctx_key_update(slurmctld_config.cred_ctx, 
			slurmctld_conf.job_credential_private_key);
}

static void _sig_handler(int signal)
{
}

/* _background_rpc_mgr - Read and process incoming RPCs to the background 
 *	controller (that's us) */
static void *_background_rpc_mgr(void *no_data)
{
	slurm_fd newsockfd;
	slurm_fd sockfd;
	slurm_addr cli_addr;
	slurm_msg_t *msg = NULL;
	int error_code;
	char* node_addr = NULL;

	/* Read configuration only */
	slurmctld_lock_t config_read_lock = { 
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	int sigarray[] = {SIGUSR1, 0};

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_background_rpc_mgr pid = %lu", (unsigned long) getpid());

	/* initialize port for RPCs */
	lock_slurmctld(config_read_lock);

	/* set node_addr to bind to (NULL means any) */
	if ((strcmp(slurmctld_conf.backup_controller,
		    slurmctld_conf.backup_addr) != 0)) {
		node_addr = slurmctld_conf.backup_addr ;
	}

	if ((sockfd =
	     slurm_init_msg_engine_addrname_port(node_addr,
						 slurmctld_conf.slurmctld_port))
	    == SLURM_SOCKET_ERROR)
		fatal("slurm_init_msg_engine_addrname_port error %m");
	unlock_slurmctld(config_read_lock);

	/* Prepare to catch SIGUSR1 to interrupt accept().
	 * This signal is generated by the slurmctld signal
	 * handler thread upon receipt of SIGABRT, SIGINT, 
	 * or SIGTERM. That thread does all processing of  
	 * all signals. */ 
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sigarray);

	/*
	 * Process incoming RPCs indefinitely
	 */
	while (slurmctld_config.shutdown_time == 0) {
		/* accept needed for stream implementation 
		 * is a no-op in message implementation that just passes 
		 * sockfd to newsockfd */
		if ((newsockfd = slurm_accept_msg_conn(sockfd, &cli_addr))
		    == SLURM_SOCKET_ERROR) {
			if (errno != EINTR)
				error("slurm_accept_msg_conn: %m");
			continue;
		}

		msg = xmalloc(sizeof(slurm_msg_t));
		slurm_msg_t_init(msg);
		if(slurm_receive_msg(newsockfd, msg, 0) != 0)
			error("slurm_receive_msg: %m");
		
		error_code = _background_process_msg(msg);
		if ((error_code == SLURM_SUCCESS)
		    &&  (msg->msg_type == REQUEST_SHUTDOWN_IMMEDIATE)
		    &&  (slurmctld_config.shutdown_time == 0))
			slurmctld_config.shutdown_time = time(NULL);
		
		slurm_free_msg(msg);

		/* close should only be called when the socket 
		 * implementation is being used the following call will 
		 * be a no-op in a message/mongo implementation */
		slurm_close_accepted_conn(newsockfd);	/* close new socket */
	}

	debug3("_background_rpc_mgr shutting down");
	slurm_close_accepted_conn(sockfd);	/* close the main socket */
	pthread_exit((void *) 0);
	return NULL;
}

/* _background_process_msg - process an RPC to the backup_controller */
static int _background_process_msg(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;

	if (msg->msg_type != REQUEST_PING) {
		bool super_user = false;
		uid_t uid = g_slurm_auth_get_uid(msg->auth_cred, NULL);
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
			   (msg->msg_type == REQUEST_TAKEOVER)) {
			info("Performing RPC: REQUEST_TAKEOVER");
			_shutdown_primary_controller(SHUTDOWN_WAIT);
			takeover = true ;
			error_code = SLURM_SUCCESS;
		} else if (super_user && 
			   (msg->msg_type == REQUEST_CONTROL)) {
			debug3("Ignoring RPC: REQUEST_CONTROL");
			error_code = ESLURM_DISABLED;
		} else {
			error("Invalid RPC received %d while in standby mode", 
			      msg->msg_type);
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
	/* Locks: Read configuration */
	slurmctld_lock_t config_read_lock = { 
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	/* 
	 *  Set address of controller to ping
	 */
	slurm_msg_t_init(&req);
	lock_slurmctld(config_read_lock);
	debug3("pinging slurmctld at %s", slurmctld_conf.control_addr);
	slurm_set_addr(&req.address, slurmctld_conf.slurmctld_port, 
	               slurmctld_conf.control_addr);
	unlock_slurmctld(config_read_lock);

	req.msg_type = REQUEST_PING;

	if (slurm_send_recv_rc_msg_only_one(&req, &rc, 0) < 0) {
		error("_ping_controller/slurm_send_node_msg error: %m");
		return SLURM_ERROR;
	}

	if (rc) {
		error("_ping_controller/response error %d", rc);
		return SLURM_PROTOCOL_ERROR;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * Reload the slurm.conf parameters without any processing
 * of the node, partition, or state information.
 * Specifically, we don't want to purge batch scripts based 
 * upon old job state information.
 * This is a stripped down version of read_slurm_conf(0).
 */
static int _backup_reconfig(void)
{
	slurm_conf_reinit(NULL);
	update_logging();
	slurmctld_conf.last_update = time(NULL);
	return SLURM_SUCCESS;
}

/*
 * Tell the primary_controller to relinquish control, primary control_machine 
 *	has to suspend operation
 * Based on _shutdown_backup_controller from controller.c
 * wait_time - How long to wait for primary controller to write state, seconds.
 * RET 0 or an error code
 * NOTE: READ lock_slurmctld config before entry (or be single-threaded)
 */
static int _shutdown_primary_controller(int wait_time)
{
	int rc;
	slurm_msg_t req;

	slurm_msg_t_init(&req);
	if ((slurmctld_conf.control_addr == NULL) ||
	    (slurmctld_conf.control_addr[0] == '\0')) {
		error("_shutdown_primary_controller: "
		      "no primary controller to shutdown");
		return SLURM_ERROR;
	}

	slurm_set_addr(&req.address, slurmctld_conf.slurmctld_port,
		       slurmctld_conf.control_addr);

	/* send request message */
	req.msg_type = REQUEST_CONTROL;
	
	if (slurm_send_recv_rc_msg_only_one(&req, &rc, 
				(CONTROL_TIMEOUT * 1000)) < 0) {
		error("_shutdown_primary_controller:send/recv: %m");
		return SLURM_ERROR;
	}
	if (rc == ESLURM_DISABLED)
		debug("primary controller responding");
	else if (rc == 0) {
		debug("primary controller has relinquished control");
	} else {
		error("_shutdown_primary_controller: %s", slurm_strerror(rc));
		return SLURM_ERROR;
	}

	/* FIXME: Ideally the REQUEST_CONTROL RPC does not return until all   
	 * other activity has ceased and the state has been saved. That is   
	 * not presently the case (it returns when no other work is pending,  
	 * so the state save should occur right away). We sleep for a while   
	 * here and give the primary controller time to shutdown */
	if (wait_time)
		sleep(wait_time);

	return SLURM_SUCCESS;
}
