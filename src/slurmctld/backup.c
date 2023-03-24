/*****************************************************************************\
 *  backup.c - backup slurm controller
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "slurm/slurm_errno.h"

#include "src/common/daemonize.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"

#include "src/slurmctld/heartbeat.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/trigger_mgr.h"

#define _DEBUG		0
#define SHUTDOWN_WAIT	2	/* Time to wait for primary server shutdown */

static int          _background_process_msg(slurm_msg_t * msg);
static void *       _background_rpc_mgr(void *no_data);
static void *       _background_signal_hand(void *no_data);
static void         _backup_reconfig(void);
static int          _shutdown_primary_controller(int wait_time);
static void *       _trigger_slurmctld_event(void *arg);
inline static void  _update_cred_key(void);

typedef struct ping_struct {
	int backup_inx;
	char *control_addr;
	char *control_machine;
	uint32_t slurmctld_port;
} ping_struct_t;

typedef struct {
	time_t control_time;
	bool responding;
} ctld_ping_t;

/* Local variables */
static ctld_ping_t *	ctld_ping = NULL;
static bool		dump_core = false;
static time_t		last_controller_response;
static pthread_mutex_t	ping_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool	takeover = false;
static pthread_cond_t	shutdown_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t	shutdown_mutex = PTHREAD_MUTEX_INITIALIZER;
static int		shutdown_rc = SLURM_SUCCESS;
static int		shutdown_thread_cnt = 0;
static int		shutdown_timeout = 0;

/*
 * Static list of signals to block in this process
 * *Must be zero-terminated*
 */
static int backup_sigarray[] = {
	SIGINT,  SIGTERM, SIGCHLD, SIGUSR1,
	SIGUSR2, SIGTSTP, SIGXCPU, SIGQUIT,
	SIGPIPE, SIGALRM, SIGABRT, SIGHUP, 0
};

/*
 * run_backup - this is the backup controller, it should run in standby
 *	mode, assuming control when the primary controller stops responding
 */
void run_backup(void)
{
	int i;
	time_t last_ping = 0;
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	info("slurmctld running in background mode");
	takeover = false;
	last_controller_response = time(NULL);

	/* default: don't resume if shutdown */
	slurmctld_config.resume_backup = false;

	/* It is now ok to tell the primary I am done (if I ever had control) */
	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	slurm_cond_broadcast(&slurmctld_config.backup_finish_cond);
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

	if (xsignal_block(backup_sigarray) < 0)
		error("Unable to block signals");

	/*
	 * create attached thread to process RPCs
	 */
	slurm_thread_create(&slurmctld_config.thread_id_rpc,
			    _background_rpc_mgr, NULL);

	/*
	 * create attached thread for signal handling
	 */
	slurm_thread_create(&slurmctld_config.thread_id_sig,
			    _background_signal_hand, NULL);

	slurm_thread_create_detached(NULL, _trigger_slurmctld_event, NULL);

	for (i = 0; ((i < 5) && (slurmctld_config.shutdown_time == 0)); i++) {
		sleep(1);       /* Give the primary slurmctld set-up time */
	}

	/* repeatedly ping ControlMachine */
	while (slurmctld_config.shutdown_time == 0) {
		sleep(1);
		/* Lock of slurm_conf below not important */
		if (slurm_conf.slurmctld_timeout && (takeover == false) &&
		    ((time(NULL) - last_ping) <
		     (slurm_conf.slurmctld_timeout / 3)))
			continue;

		last_ping = time(NULL);
		if (ping_controllers(false) == SLURM_SUCCESS)
			last_controller_response = time(NULL);
		else if (takeover) {
			/*
			 * in takeover mode, take control as soon as
			 * primary no longer respond
			 */
			break;
		} else {
			time_t use_time, last_heartbeat;
			int server_inx = -1;
			last_heartbeat = get_last_heartbeat(&server_inx);
			debug("%s: last_heartbeat %ld from server %d",
			      __func__, last_heartbeat, server_inx);

			use_time = last_controller_response;
			if (server_inx > backup_inx) {
				info("Lower priority slurmctld is currently primary (%d > %d)",
				     server_inx, backup_inx);
			} else if (last_heartbeat > last_controller_response) {
				/* Race condition for time stamps */
				debug("Last message to the controller was at %ld,"
				      " but the last heartbeat was written at %ld,"
				      " trusting the filesystem instead of the network"
				      " and not asserting control at this time.",
				      last_controller_response, last_heartbeat);
				use_time = last_heartbeat;
			}

			if ((time(NULL) - use_time) >
			    slurm_conf.slurmctld_timeout)
				break;
		}
	}

	if (slurmctld_config.shutdown_time != 0) {
		/*
		 * Since pidfile is created as user root (its owner is
		 *   changed to SlurmUser) SlurmUser may not be able to
		 *   remove it, so this is not necessarily an error.
		 * No longer need slurm_conf lock after above join.
		 */
		if (unlink(slurm_conf.slurmctld_pidfile) < 0)
			verbose("Unable to remove pidfile '%s': %m",
			        slurm_conf.slurmctld_pidfile);

		info("BackupController terminating");
		pthread_join(slurmctld_config.thread_id_sig, NULL);
		log_fini();
		if (dump_core)
			abort();
		else
			exit(0);
	}

	lock_slurmctld(config_read_lock);
	error("ControlMachine %s not responding, BackupController%d %s taking over",
	      slurm_conf.control_machine[0], backup_inx,
	      slurmctld_config.node_name_short);
	unlock_slurmctld(config_read_lock);

	backup_slurmctld_restart();
	trigger_primary_ctld_fail();
	trigger_backup_ctld_as_ctrl();

	pthread_kill(slurmctld_config.thread_id_sig, SIGTERM);
	pthread_join(slurmctld_config.thread_id_sig, NULL);
	pthread_join(slurmctld_config.thread_id_rpc, NULL);

	/*
	 * The job list needs to be freed before we run
	 * ctld_assoc_mgr_init, it should be empty here in the first place.
	 */
	lock_slurmctld(config_write_lock);
	job_fini();
	init_job_conf();
	unlock_slurmctld(config_write_lock);

	ctld_assoc_mgr_init();

	/* clear old state and read new state */
	lock_slurmctld(config_write_lock);
	if (switch_g_restore(slurm_conf.state_save_location, true)) {
		error("failed to restore switch state");
		abort();
	}
	if (read_slurm_conf(2, false)) {	/* Recover all state */
		error("Unable to recover slurm state");
		abort();
	}
	slurmctld_config.shutdown_time = (time_t) 0;
	unlock_slurmctld(config_write_lock);
	select_g_select_nodeinfo_set_all();

	return;
}

/*
 * _background_signal_hand - Process daemon-wide signals for the
 *	backup controller
 */
static void *_background_signal_hand(void *no_data)
{
	int sig, rc;
	sigset_t set;
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

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
			_backup_reconfig();
			/* Leave config lock set through this */
			_update_cred_key();
			unlock_slurmctld(config_write_lock);
			break;
		case SIGABRT:   /* abort */
			info("SIGABRT received");
			slurmctld_config.shutdown_time = time(NULL);
			slurmctld_shutdown();
			dump_core = true;
			return NULL;    /* Normal termination */
			break;
		case SIGUSR2:
			info("Logrotate signal (SIGUSR2) received");
			update_logging();
			break;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}
	return NULL;
}

/*
 * Reset the job credential key based upon configuration parameters.
 * slurm_conf is locked on entry.
 */
static void _update_cred_key(void)
{
	slurm_cred_ctx_key_update(slurmctld_config.cred_ctx,
	                          slurm_conf.job_credential_private_key);
}

static void _sig_handler(int signal)
{
}

/*
 * _background_rpc_mgr - Read and process incoming RPCs to the background
 *	controller (that's us)
 */
static void *_background_rpc_mgr(void *no_data)
{
	int newsockfd, sockfd;
	slurm_addr_t cli_addr;
	slurm_msg_t msg;

	/* Read configuration only */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	int sigarray[] = {SIGUSR1, 0};

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_background_rpc_mgr pid = %lu", (unsigned long) getpid());

	/* initialize port for RPCs */
	lock_slurmctld(config_read_lock);

	if ((sockfd = slurm_init_msg_engine_port(slurm_conf.slurmctld_port))
	    == SLURM_ERROR)
		fatal("slurm_init_msg_engine_port error %m");
	unlock_slurmctld(config_read_lock);

	/*
	 * Prepare to catch SIGUSR1 to interrupt accept().  This signal is
	 * generated by the slurmctld signal handler thread upon receipt of
	 * SIGABRT, SIGINT, or SIGTERM. That thread does all processing of
	 * all signals.
	 */
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sigarray);

	/*
	 * Process incoming RPCs indefinitely
	 */
	while (slurmctld_config.shutdown_time == 0) {
		/*
		 * accept needed for stream implementation is a no-op in
		 * message implementation that just passes sockfd to newsockfd
		 */
		if ((newsockfd = slurm_accept_msg_conn(sockfd, &cli_addr))
		    == SLURM_ERROR) {
			if (errno != EINTR)
				error("slurm_accept_msg_conn: %m");
			continue;
		}

		log_flag(PROTOCOL, "%s: accept() connection from %pA",
			 __func__, &cli_addr);

		slurm_msg_t_init(&msg);
		if (slurm_receive_msg(newsockfd, &msg, 0) != 0)
			error("slurm_receive_msg: %m");
		else
			_background_process_msg(&msg);

		slurm_free_msg_members(&msg);

		close(newsockfd);	/* close new socket */
	}

	debug3("_background_rpc_mgr shutting down");
	close(sockfd);	/* close the main socket */
	return NULL;
}

/*
 * _background_process_msg - process an RPC to the backup_controller
 */
static int _background_process_msg(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	bool send_rc = true;

	if (!msg->auth_uid_set) {
		error("%s: received message without previously validated auth",
		      __func__);
		return SLURM_ERROR;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_PROTOCOL) {
		char *p = rpc_num2string(msg->msg_type);
		if (msg->conn) {
			info("%s: received opcode %s from persist conn on (%s)%s uid %u",
			     __func__, p, msg->conn->cluster_name,
			     msg->conn->rem_host, msg->auth_uid);
		} else {
			slurm_addr_t cli_addr;
			(void) slurm_get_peer_addr(msg->conn_fd, &cli_addr);
			info("%s: received opcode %s from %pA uid %u",
			     __func__, p, &cli_addr, msg->auth_uid);
		}
	}

	if (msg->msg_type != REQUEST_PING) {
		bool super_user = false;
		uid_t uid = auth_g_get_uid(msg->auth_cred);

		if (validate_slurm_user(uid))
			super_user = true;

		if (super_user && (msg->msg_type == REQUEST_SHUTDOWN)) {
			info("Performing background RPC: REQUEST_SHUTDOWN");
			pthread_kill(slurmctld_config.thread_id_sig, SIGTERM);
		} else if (super_user &&
			   (msg->msg_type == REQUEST_TAKEOVER)) {
			info("Performing background RPC: REQUEST_TAKEOVER");
			(void) _shutdown_primary_controller(SHUTDOWN_WAIT);
			takeover = true;
			error_code = SLURM_SUCCESS;
		} else if (super_user &&
			   (msg->msg_type == REQUEST_CONTROL)) {
			debug3("Ignoring RPC: REQUEST_CONTROL");
			error_code = ESLURM_DISABLED;
			last_controller_response = time(NULL);
		} else if (msg->msg_type == REQUEST_CONTROL_STATUS) {
			slurm_rpc_control_status(msg);
			send_rc = false;
		} else if (msg->msg_type == REQUEST_CONFIG) {
			/*
			 * Config was asked for from the wrong controller
			 * Assume there was a misconfiguration and redirect
			 * to the correct controller.  This usually indicates a
			 * configuration issue.
			 */
			error("REQUEST_CONFIG received while in standby.");
			error_code = ESLURM_IN_STANDBY_USE_BACKUP;
		} else {
			error("Invalid RPC received %s while in standby mode",
			      rpc_num2string(msg->msg_type));
			error_code = ESLURM_IN_STANDBY_MODE;
		}
	}
	if (send_rc)
		slurm_send_rc_msg(msg, error_code);
	return error_code;
}

static void *_ping_ctld_thread(void *arg)
{
	ping_struct_t *ping = (ping_struct_t *) arg;
	slurm_msg_t req, resp;
	control_status_msg_t *control_msg;
	time_t control_time = (time_t) 0;
	bool responding = false;

	slurm_msg_t_init(&req);
	slurm_set_addr(&req.address, ping->slurmctld_port, ping->control_addr);
	req.msg_type = REQUEST_CONTROL_STATUS;
	slurm_msg_set_r_uid(&req, SLURM_AUTH_UID_ANY);
	if (slurm_send_recv_node_msg(&req, &resp, 0) == SLURM_SUCCESS) {
		switch (resp.msg_type) {
		case RESPONSE_CONTROL_STATUS:
			control_msg = (control_status_msg_t *) resp.data;
			if (ping->backup_inx != control_msg->backup_inx) {
				error("%s: BackupController# index mismatch (%d != %u) from host %s",
				      __func__, ping->backup_inx,
				      control_msg->backup_inx,
				      ping->control_machine);
			}
			control_time  = control_msg->control_time;
			responding = true;
			break;
		default:
			error("%s:, Unknown response message %u from host %s",
			      __func__, resp.msg_type, ping->control_machine);
			break;
		}
		slurm_free_msg_data(resp.msg_type, resp.data);
		if (resp.auth_cred)
			auth_g_destroy(resp.auth_cred);
	}

	slurm_mutex_lock(&ping_mutex);
	if (responding) {
		ctld_ping[ping->backup_inx].control_time = control_time;
		ctld_ping[ping->backup_inx].responding = true;
	}
	slurm_mutex_unlock(&ping_mutex);

	xfree(ping->control_addr);
	xfree(ping->control_machine);
	xfree(ping);

	return NULL;
}

/*
 * Ping all higher-priority control nodes.
 * RET SLURM_SUCCESS if a currently active controller is found
 */
extern int ping_controllers(bool active_controller)
{
	int i, ping_target_cnt;
	ping_struct_t *ping;
	pthread_t *ping_tids;
	/* Locks: Read configuration */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	bool active_ctld = false, avail_ctld = false;

	if (active_controller)
		ping_target_cnt = slurm_conf.control_cnt;
	else
		ping_target_cnt = backup_inx;

	ctld_ping = xcalloc(ping_target_cnt, sizeof(ctld_ping_t));
	ping_tids = xcalloc(ping_target_cnt, sizeof(pthread_t));

	for (i = 0; i < ping_target_cnt; i++) {
		ctld_ping[i].control_time  = (time_t) 0;
		ctld_ping[i].responding = false;
	}

	lock_slurmctld(config_read_lock);
	for (i = 0; i < ping_target_cnt; i++) {
		if (i == backup_inx)	/* Avoid pinging ourselves */
			continue;

		ping = xmalloc(sizeof(ping_struct_t));
		ping->backup_inx      = i;
		ping->control_addr = xstrdup(slurm_conf.control_addr[i]);
		ping->control_machine = xstrdup(slurm_conf.control_machine[i]);
		ping->slurmctld_port = slurm_conf.slurmctld_port;
		slurm_thread_create(&ping_tids[i], _ping_ctld_thread, ping);
	}
	unlock_slurmctld(config_read_lock);

	for (i = 0; i < ping_target_cnt; i++) {
		if (i == backup_inx)	/* Avoid pinging ourselves */
			continue;
		pthread_join(ping_tids[i], NULL);
	}
	xfree(ping_tids);

	for (i = 0; i < ping_target_cnt; i++) {
		if (i == backup_inx)	/* Avoid pinging ourselves */
			continue;
		if (ctld_ping[i].control_time) {
			/*
			 * Higher priority slurmctld is already in
			 * primary mode
			 */
			active_ctld = true;
		}
		if (ctld_ping[i].responding) {
			/*
			 * Higher priority slurmctld is available to
			 * enter primary mode
			 */
			avail_ctld = true;
		} else if (active_controller) {
			trigger_backup_ctld_fail(i);
		}
	}

	xfree(ctld_ping);
	if (active_ctld || avail_ctld)
		return SLURM_SUCCESS;
	return SLURM_ERROR;
}

/*
 * Reload the slurm.conf parameters without any processing
 * of the node, partition, or state information.
 * Specifically, we don't want to purge batch scripts based
 * upon old job state information.
 * This is a stripped down version of read_slurm_conf(0).
 */
static void _backup_reconfig(void)
{
	slurm_conf_reinit(NULL);
	update_logging();
	slurm_conf.last_update = time(NULL);
	return;
}

static void *_shutdown_controller(void *arg)
{
	int shutdown_inx, rc = SLURM_SUCCESS, rc2 = SLURM_SUCCESS;
	slurm_msg_t req;
	bool do_shutdown = false;
	shutdown_arg_t *shutdown_arg;
	shutdown_msg_t shutdown_msg;

	shutdown_arg = (shutdown_arg_t *)arg;
	shutdown_inx = shutdown_arg->index;
	do_shutdown = shutdown_arg->shutdown;
	xfree(arg);

	slurm_msg_t_init(&req);
	slurm_msg_set_r_uid(&req, slurm_conf.slurm_user_id);
	slurm_set_addr(&req.address, slurm_conf.slurmctld_port,
	               slurm_conf.control_addr[shutdown_inx]);
	if (do_shutdown) {
		req.msg_type = REQUEST_SHUTDOWN;
		shutdown_msg.options = SLURMCTLD_SHUTDOWN_CTLD;
		req.data = &shutdown_msg;
	} else {
		req.msg_type = REQUEST_CONTROL;
	}
	if (slurm_send_recv_rc_msg_only_one(&req, &rc2, shutdown_timeout) < 0) {
		error("%s: send/recv(%s): %m",
		      __func__, slurm_conf.control_machine[shutdown_inx]);
		rc = SLURM_ERROR;
	} else if (rc2 == ESLURM_DISABLED) {
		debug("primary controller responding");
	} else if (rc2 == SLURM_SUCCESS) {
		debug("primary controller has relinquished control");
	} else {
		error("%s(%s): %s",
		      __func__, slurm_conf.control_machine[shutdown_inx],
		      slurm_strerror(rc2));
		rc = SLURM_ERROR;
	}

	slurm_mutex_lock(&shutdown_mutex);
	if (rc != SLURM_SUCCESS)
		shutdown_rc = rc;
	shutdown_thread_cnt--;
	slurm_cond_signal(&shutdown_cond);
	slurm_mutex_unlock(&shutdown_mutex);
	return NULL;
}

/*
 * Tell the primary controller and all other possible controller daemons to
 *	relinquish control, primary control_machine has to suspend operation
 * Based on _shutdown_backup_controller from controller.c
 * wait_time - How long to wait for primary controller to write state, seconds.
 * RET 0 or an error code
 * NOTE: READ lock_slurmctld config before entry (or be single-threaded)
 */
static int _shutdown_primary_controller(int wait_time)
{
	int i;
	shutdown_arg_t *shutdown_arg;

	if (shutdown_timeout == 0) {
		shutdown_timeout = slurm_conf.msg_timeout / 2;
		shutdown_timeout = MAX(shutdown_timeout, 2);	/* 2 sec min */
		shutdown_timeout = MIN(shutdown_timeout, CONTROL_TIMEOUT);
		shutdown_timeout *= 1000;	/* sec to msec */
	}

	if ((slurm_conf.control_addr[0] == NULL) ||
	    (slurm_conf.control_addr[0][0] == '\0')) {
		error("%s: no primary controller to shutdown", __func__);
		return SLURM_ERROR;
	}

	shutdown_rc = SLURM_SUCCESS;
	for (i = 0; i < slurm_conf.control_cnt; i++) {
		if (i == backup_inx)
			continue;	/* No message to self */

		shutdown_arg = xmalloc(sizeof(*shutdown_arg));
		shutdown_arg->index = i;
		/*
		 * need to send actual REQUEST_SHUTDOWN to non-primary ctlds
		 * in order to have them properly shutdown and not contend
		 * for primary position, otherwise "takeover" results in
		 * contention among backups for primary position.
		 */
		if (i < backup_inx)
			shutdown_arg->shutdown = true;
		slurm_thread_create_detached(NULL, _shutdown_controller,
					     shutdown_arg);
		slurm_mutex_lock(&shutdown_mutex);
		shutdown_thread_cnt++;
		slurm_mutex_unlock(&shutdown_mutex);
	}

	slurm_mutex_lock(&shutdown_mutex);
	while (shutdown_thread_cnt != 0) {
		slurm_cond_wait(&shutdown_cond, &shutdown_mutex);
	}
	slurm_mutex_unlock(&shutdown_mutex);

	/*
	 * FIXME: Ideally the REQUEST_CONTROL RPC does not return until all
	 * other activity has ceased and the state has been saved. That is
	 * not presently the case (it returns when no other work is pending,
	 * so the state save should occur right away). We sleep for a while
	 * here and give the primary controller time to shutdown
	 */
	if (wait_time)
		sleep(wait_time);

	return shutdown_rc;
}

static void *_trigger_slurmctld_event(void *arg)
{
	trigger_info_t ti;

	memset(&ti, 0, sizeof(ti));
	ti.res_id = "*";
	ti.res_type = TRIGGER_RES_TYPE_SLURMCTLD;
	ti.trig_type = TRIGGER_TYPE_BU_CTLD_RES_OP;
	ti.control_inx = backup_inx;
	if (slurm_pull_trigger(&ti)) {
		error("%s: TRIGGER_TYPE_BU_CTLD_RES_OP send failure: %m",
		      __func__);
	} else {
		verbose("%s: TRIGGER_TYPE_BU_CTLD_RES_OP sent", __func__);
	}
	return NULL;
}
