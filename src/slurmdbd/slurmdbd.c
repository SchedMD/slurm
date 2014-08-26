/*****************************************************************************\
 *  slurmdbd.c - functions for SlurmDBD
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif
#include <grp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_auth.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"

#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"
#include "src/slurmdbd/proc_req.h"
#include "src/slurmdbd/backup.h"

/* Global variables */
time_t shutdown_time = 0;		/* when shutdown request arrived */
List registered_clusters = NULL;
pthread_mutex_t registered_lock = PTHREAD_MUTEX_INITIALIZER;

/* Local variables */
static int    dbd_sigarray[] = {	/* blocked signals for this process */
	SIGINT,  SIGTERM, SIGCHLD, SIGUSR1,
	SIGUSR2, SIGTSTP, SIGXCPU, SIGQUIT,
	SIGPIPE, SIGALRM, SIGABRT, SIGHUP, 0 };
static int    debug_level = 0;		/* incremented for -v on command line */
static int    foreground = 0;		/* run process as a daemon */
static log_options_t log_opts = 	/* Log to stderr & syslog */
	LOG_OPTS_INITIALIZER;
static int	 new_nice = 0;
static pthread_t rpc_handler_thread;	/* thread ID for RPC hander */
static pthread_t signal_handler_thread;	/* thread ID for signal hander */
static pthread_t rollup_handler_thread;	/* thread ID for rollup hander */
static pthread_t commit_handler_thread;	/* thread ID for commit hander */
static pthread_mutex_t rollup_lock = PTHREAD_MUTEX_INITIALIZER;
static bool running_rollup = 0;
static bool running_commit = 0;

/* Local functions */
static void  _become_slurm_user(void);
static void  _daemonize(void);
static void  _default_sigaction(int sig);
static void  _init_config(void);
static void  _init_pidfile(void);
static void  _kill_old_slurmdbd(void);
static void  _parse_commandline(int argc, char *argv[]);
static void  _request_registrations(void *db_conn);
static void  _rollup_handler_cancel();
static void *_rollup_handler(void *no_data);
static void  _commit_handler_cancel();
static void *_commit_handler(void *no_data);
static int   _send_slurmctld_register_req(slurmdb_cluster_rec_t *cluster_rec);
static void  _set_work_dir(void);
static void *_signal_handler(void *no_data);
static void  _update_logging(bool startup);
static void  _update_nice(void);
static void  _usage(char *prog_name);

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char *argv[])
{
	pthread_attr_t thread_attr;
	char node_name[128];
	void *db_conn = NULL;
	assoc_init_args_t assoc_init_arg;

	_init_config();
	log_init(argv[0], log_opts, LOG_DAEMON, NULL);
	if (read_slurmdbd_conf())
		exit(1);
	_parse_commandline(argc, argv);
	_update_logging(true);
	_update_nice();

	if (slurm_auth_init(NULL) != SLURM_SUCCESS) {
		fatal("Unable to initialize %s authentication plugin",
		      slurmdbd_conf->auth_type);
	}
	if (slurm_acct_storage_init(NULL) != SLURM_SUCCESS) {
		fatal("Unable to initialize %s accounting storage plugin",
		      slurmdbd_conf->storage_type);
	}
	_kill_old_slurmdbd();
	if (foreground == 0)
		_daemonize();

	/*
	 * Need to create pidfile here in case we setuid() below
	 * (init_pidfile() exits if it can't initialize pid file).
	 * On Linux we also need to make this setuid job explicitly
	 * able to write a core dump.
	 * This also has to happen after daemon(), which closes all fd's,
	 * so we keep the write lock of the pidfile.
	 */
	_init_pidfile();
	_become_slurm_user();
	if (foreground == 0)
		_set_work_dir();
	log_config();

#ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#endif /* PR_SET_DUMPABLE */

	if (xsignal_block(dbd_sigarray) < 0)
		error("Unable to block signals");

	/* Create attached thread for signal handling */
	slurm_attr_init(&thread_attr);
	if (pthread_create(&signal_handler_thread, &thread_attr,
			   _signal_handler, NULL))
		fatal("pthread_create %m");
	slurm_attr_destroy(&thread_attr);

	registered_clusters = list_create(NULL);

	slurm_attr_init(&thread_attr);
	if (pthread_create(&commit_handler_thread, &thread_attr,
			   _commit_handler, NULL))
		fatal("pthread_create %m");
	slurm_attr_destroy(&thread_attr);

	memset(&assoc_init_arg, 0, sizeof(assoc_init_args_t));

	/* If we are tacking wckey we need to cache
	   wckeys, if we aren't only cache the users, qos */
	assoc_init_arg.cache_level = ASSOC_MGR_CACHE_USER | ASSOC_MGR_CACHE_QOS;
	if (slurmdbd_conf->track_wckey)
		assoc_init_arg.cache_level |= ASSOC_MGR_CACHE_WCKEY;

	db_conn = acct_storage_g_get_connection(NULL, 0, true, NULL);
	if (assoc_mgr_init(db_conn, &assoc_init_arg, errno) == SLURM_ERROR) {
		error("Problem getting cache of data");
		acct_storage_g_close_connection(&db_conn);
		goto end_it;
	}

	if (gethostname_short(node_name, sizeof(node_name)))
		fatal("getnodename: %m");

	while (1) {
		if (slurmdbd_conf->dbd_backup &&
		    (!strcmp(node_name, slurmdbd_conf->dbd_backup) ||
		     !strcmp(slurmdbd_conf->dbd_backup, "localhost"))) {
			info("slurmdbd running in background mode");
			have_control = false;
			backup = true;
			/* make sure any locks are released */
			acct_storage_g_commit(db_conn, 1);
			run_dbd_backup();
			if (!shutdown_time)
				assoc_mgr_refresh_lists(db_conn);
		} else if (slurmdbd_conf->dbd_host &&
			   (!strcmp(slurmdbd_conf->dbd_host, node_name) ||
			    !strcmp(slurmdbd_conf->dbd_host, "localhost"))) {
			backup = false;
			have_control = true;
		} else {
			fatal("This host not configured to run SlurmDBD "
			      "(%s != %s | (backup) %s)",
			      node_name, slurmdbd_conf->dbd_host,
			      slurmdbd_conf->dbd_backup);
		}

		if (!shutdown_time) {
			/* Create attached thread to process incoming RPCs */
			slurm_attr_init(&thread_attr);
			if (pthread_create(&rpc_handler_thread, &thread_attr,
					   rpc_mgr, NULL))
				fatal("pthread_create error %m");
			slurm_attr_destroy(&thread_attr);
		}

		if (!shutdown_time) {
			/* Create attached thread to do usage rollup */
			slurm_attr_init(&thread_attr);
			if (pthread_create(&rollup_handler_thread,
					   &thread_attr,
					   _rollup_handler, db_conn))
				fatal("pthread_create error %m");
			slurm_attr_destroy(&thread_attr);
		}

		/* Daemon is fully operational here */
		if (!shutdown_time || primary_resumed) {
			shutdown_time = 0;
			info("slurmdbd version %s started",
			     SLURM_VERSION_STRING);
			if (backup)
				run_dbd_backup();
		}

		_request_registrations(db_conn);
		acct_storage_g_commit(db_conn, 1);

		/* this is only ran if not backup */
		if (rollup_handler_thread)
			pthread_join(rollup_handler_thread, NULL);
		if (rpc_handler_thread)
			pthread_join(rpc_handler_thread, NULL);

		if (backup && primary_resumed) {
			shutdown_time = 0;
			info("Backup has given up control");
		}

		if (shutdown_time)
			break;
	}
	/* Daemon termination handled here */

end_it:

	if (signal_handler_thread)
		pthread_join(signal_handler_thread, NULL);
	if (commit_handler_thread)
		pthread_join(commit_handler_thread, NULL);

	acct_storage_g_commit(db_conn, 1);
	acct_storage_g_close_connection(&db_conn);

	if (slurmdbd_conf->pid_file &&
	    (unlink(slurmdbd_conf->pid_file) < 0)) {
		verbose("Unable to remove pidfile '%s': %m",
			slurmdbd_conf->pid_file);
	}

	FREE_NULL_LIST(registered_clusters);

	assoc_mgr_fini(NULL);
	slurm_acct_storage_fini();
	slurm_auth_fini();
	log_fini();
	free_slurmdbd_conf();
	exit(0);
}

extern void reconfig()
{
	read_slurmdbd_conf();
	assoc_mgr_set_missing_uids();
	acct_storage_g_reconfig(NULL, 0);
	_update_logging(false);
}

extern void shutdown_threads()
{
	shutdown_time = time(NULL);
	/* End commit before rpc_mgr_wake.  It will do the final
	   commit on the connection.
	*/
	_commit_handler_cancel();
	rpc_mgr_wake();
	_rollup_handler_cancel();
}

/* Reset some of the processes resource limits to the hard limits */
static void  _init_config(void)
{
	struct rlimit rlim;

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		(void) setrlimit(RLIMIT_NOFILE, &rlim);
	}
	if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		(void) setrlimit(RLIMIT_CORE, &rlim);
	}
	if (getrlimit(RLIMIT_STACK, &rlim) == 0) {
		/* slurmctld can spawn lots of pthreads.
		 * Set the (per thread) stack size to a
		 * more "reasonable" value to avoid running
		 * out of virtual memory and dying */
		rlim.rlim_cur = rlim.rlim_max;
		(void) setrlimit(RLIMIT_STACK, &rlim);
	}
	if (getrlimit(RLIMIT_DATA, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		(void) setrlimit(RLIMIT_DATA, &rlim);
	}
}

/*
 * _parse_commandline - parse and process any command line arguments
 * IN argc - number of command line arguments
 * IN argv - the command line arguments
 * IN/OUT conf_ptr - pointer to current configuration, update as needed
 */
static void _parse_commandline(int argc, char *argv[])
{
	int c = 0;
	char *tmp_char;

	opterr = 0;
	while ((c = getopt(argc, argv, "Dhn:vV")) != -1)
		switch (c) {
		case 'D':
			foreground = 1;
			break;
		case 'h':
			_usage(argv[0]);
			exit(0);
			break;
		case 'n':
			new_nice = strtol(optarg, &tmp_char, 10);
			if (tmp_char[0] != '\0') {
				error("Invalid option for -n option (nice "
				      "value), ignored");
				new_nice = 0;
			}
			break;
		case 'v':
			debug_level++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		default:
			_usage(argv[0]);
			exit(1);
		}
}

/* _usage - print a message describing the command line arguments of
 *	slurmctld */
static void _usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s [OPTIONS]\n", prog_name);
	fprintf(stderr, "  -D         \t"
		"Run daemon in foreground.\n");
	fprintf(stderr, "  -h         \t"
		"Print this help message.\n");
	fprintf(stderr, "  -n value   \t"
		"Run the daemon at the specified nice value.\n");
	fprintf(stderr, "  -v         \t"
		"Verbose mode. Multiple -v's increase verbosity.\n");
	fprintf(stderr, "  -V         \t"
		"Print version information and exit.\n");
}

/* Reset slurmctld logging based upon configuration parameters */
static void _update_logging(bool startup)
{
	/* Preserve execute line arguments (if any) */
	if (debug_level) {
		slurmdbd_conf->debug_level = MIN(
			(LOG_LEVEL_INFO + debug_level),
			(LOG_LEVEL_END - 1));
	}

	log_opts.stderr_level  = slurmdbd_conf->debug_level;
	log_opts.logfile_level = slurmdbd_conf->debug_level;
	log_opts.syslog_level  = slurmdbd_conf->debug_level;

	if (foreground)
		log_opts.syslog_level = LOG_LEVEL_QUIET;
	else {
		log_opts.stderr_level = LOG_LEVEL_QUIET;
		if (slurmdbd_conf->log_file)
			log_opts.syslog_level = LOG_LEVEL_QUIET;
	}

	log_alter(log_opts, SYSLOG_FACILITY_DAEMON, slurmdbd_conf->log_file);
	log_set_timefmt(slurmdbd_conf->log_fmt);
	if (startup && slurmdbd_conf->log_file) {
		int rc;
		gid_t slurm_user_gid;
		slurm_user_gid = gid_from_uid(slurmdbd_conf->slurm_user_id);
		rc = chown(slurmdbd_conf->log_file,
			   slurmdbd_conf->slurm_user_id, slurm_user_gid);
		if (rc) {
			error("chown(%s, %d, %d): %m",
			      slurmdbd_conf->log_file,
			      (int) slurmdbd_conf->slurm_user_id,
			      (int) slurm_user_gid);
		}
	}
}

/* Reset slurmd nice value */
static void _update_nice(void)
{
	int cur_nice;
	id_t pid;

	if (new_nice == 0)	/* No change */
		return;

	pid = getpid();
	cur_nice = getpriority(PRIO_PROCESS, pid);
	if (cur_nice == new_nice)
		return;
	if (setpriority(PRIO_PROCESS, pid, new_nice))
		error("Unable to reset nice value to %d: %m", new_nice);
}

/* Kill the currently running slurmdbd */
static void _kill_old_slurmdbd(void)
{
	int fd;
	pid_t oldpid;

	if (slurmdbd_conf->pid_file == NULL) {
		error("No PidFile configured");
		return;
	}

	oldpid = read_pidfile(slurmdbd_conf->pid_file, &fd);
	if (oldpid != (pid_t) 0) {
		info("Killing old slurmdbd[%ld]", (long) oldpid);
		kill(oldpid, SIGTERM);

		/*
		 * Wait for previous daemon to terminate
		 */
		if (fd_get_readw_lock(fd) < 0)
			fatal("Unable to wait for readw lock: %m");
		(void) close(fd); /* Ignore errors */
	}
}

/* Create the PidFile if one is configured */
static void _init_pidfile(void)
{
	if (slurmdbd_conf->pid_file == NULL) {
		error("No PidFile configured");
		return;
	}

	/* Don't close the fd returned here since we need to keep the
	   fd open to maintain the write lock.
	*/
	create_pidfile(slurmdbd_conf->pid_file, slurmdbd_conf->slurm_user_id);
}

/* Become a daemon (child of init) and
 * "cd" to the LogFile directory (if one is configured) */
static void _daemonize(void)
{
	if (daemon(1, 1))
		error("daemon(): %m");
	log_alter(log_opts, LOG_DAEMON, slurmdbd_conf->log_file);
}

static void _set_work_dir(void)
{
	bool success = false;

	if (slurmdbd_conf->log_file &&
	    (slurmdbd_conf->log_file[0] == '/')) {
		char *slash_ptr, *work_dir;
		work_dir = xstrdup(slurmdbd_conf->log_file);
		slash_ptr = strrchr(work_dir, '/');
		if (slash_ptr == work_dir)
			work_dir[1] = '\0';
		else
			slash_ptr[0] = '\0';
		if ((access(work_dir, W_OK) != 0) || (chdir(work_dir) < 0))
			error("chdir(%s): %m", work_dir);
		else
			success = true;
		xfree(work_dir);
	}

	if (!success) {
		if ((access("/var/tmp", W_OK) != 0) ||
		    (chdir("/var/tmp") < 0)) {
			error("chdir(/var/tmp): %m");
		} else
			info("chdir to /var/tmp");
	}
}

static void _request_registrations(void *db_conn)
{
	List cluster_list = acct_storage_g_get_clusters(
		db_conn, getuid(), NULL);
	ListIterator itr;
	slurmdb_cluster_rec_t *cluster_rec = NULL;

	if (!cluster_list)
		return;
	itr = list_iterator_create(cluster_list);
	while ((cluster_rec = list_next(itr))) {
		if (!cluster_rec->control_port
		    || (cluster_rec->rpc_version < 9))
			continue;
		if (_send_slurmctld_register_req(cluster_rec) != SLURM_SUCCESS)
			/* mark this cluster as unresponsive */
			clusteracct_storage_g_fini_ctld(db_conn, cluster_rec);
	}
	list_iterator_destroy(itr);
	list_destroy(cluster_list);
}

static void _rollup_handler_cancel()
{
	if (running_rollup)
		debug("Waiting for rollup thread to finish.");
	slurm_mutex_lock(&rollup_lock);
	if (rollup_handler_thread)
		pthread_cancel(rollup_handler_thread);
	slurm_mutex_unlock(&rollup_lock);
}

/* _rollup_handler - Process rollup duties */
static void *_rollup_handler(void *db_conn)
{
	time_t start_time = time(NULL);
	time_t next_time;
/* 	int sigarray[] = {SIGUSR1, 0}; */
	struct tm tm;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if (!localtime_r(&start_time, &tm)) {
		fatal("Couldn't get localtime for rollup handler %ld",
		      (long)start_time);
		return NULL;
	}

	while (1) {
		if (!db_conn)
			break;
		/* run the roll up */
		slurm_mutex_lock(&rollup_lock);
		running_rollup = 1;
		debug2("running rollup at %s", slurm_ctime(&start_time));
		acct_storage_g_roll_usage(db_conn, 0, 0, 1);
		acct_storage_g_commit(db_conn, 1);
		running_rollup = 0;
		slurm_mutex_unlock(&rollup_lock);

		/* sleep for an hour */
		tm.tm_sec = 0;
		tm.tm_min = 0;
		tm.tm_hour++;
		tm.tm_isdst = -1;
		next_time = mktime(&tm);

		/* get the time now we have rolled usage */
		start_time = time(NULL);

		sleep((next_time-start_time));

		start_time = time(NULL);
		if (!localtime_r(&start_time, &tm)) {
			fatal("Couldn't get localtime for rollup handler %ld",
			      (long)start_time);
			return NULL;
		}
		/* Just in case some new uids were added to the system
		   pick them up here. */
		assoc_mgr_set_missing_uids();
		/* repeat ;) */

	}

	return NULL;
}

static void _commit_handler_cancel()
{
	if (running_commit)
		debug("Waiting for commit thread to finish.");
	slurm_mutex_lock(&registered_lock);
	if (commit_handler_thread)
		pthread_cancel(commit_handler_thread);
	slurm_mutex_unlock(&registered_lock);
}

/* _commit_handler - Process commit's of registered clusters */
static void *_commit_handler(void *db_conn)
{
	ListIterator itr;
	slurmdbd_conn_t *slurmdbd_conn;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (!shutdown_time) {
		/* Commit each slurmctld's info */
		if (slurmdbd_conf->commit_delay) {
			slurm_mutex_lock(&registered_lock);
			running_commit = 1;
			itr = list_iterator_create(registered_clusters);
			while ((slurmdbd_conn = list_next(itr))) {
				debug4("running commit for %s",
				       slurmdbd_conn->cluster_name);
				acct_storage_g_commit(
					slurmdbd_conn->db_conn, 1);
			}
			list_iterator_destroy(itr);
			running_commit = 0;
			slurm_mutex_unlock(&registered_lock);
		}

		/* This really doesn't need to be synconized so just
		 * sleep for a bit and do it again.
		 */
		sleep(slurmdbd_conf->commit_delay ?
		      slurmdbd_conf->commit_delay : 5);
	}

	return NULL;
}

/*
 * send_slurmctld_register_req - request register from slurmctld
 * IN host: control host of cluster
 * IN port: control port of cluster
 * IN rpc_version: rpc version of cluster
 * RET:  error code
 */
static int _send_slurmctld_register_req(slurmdb_cluster_rec_t *cluster_rec)
{
	slurm_addr_t ctld_address;
	slurm_fd_t fd;
	int rc = SLURM_SUCCESS;

	slurm_set_addr_char(&ctld_address, cluster_rec->control_port,
			    cluster_rec->control_host);
	fd = slurm_open_msg_conn(&ctld_address);
	if (fd < 0) {
		rc = SLURM_ERROR;
	} else {
		slurm_msg_t out_msg;
		slurm_msg_t_init(&out_msg);
		out_msg.msg_type = ACCOUNTING_REGISTER_CTLD;
		out_msg.flags = SLURM_GLOBAL_AUTH_KEY;
		out_msg.protocol_version
			= slurmdbd_translate_rpc(cluster_rec->rpc_version);
		slurm_send_node_msg(fd, &out_msg);
		/* We probably need to add matching recv_msg function
		 * for an arbitray fd or should these be fire
		 * and forget?  For this, that we can probably
		 * forget about it */
		slurm_close_stream(fd);
	}
	return rc;
}

/* _signal_handler - Process daemon-wide signals */
static void *_signal_handler(void *no_data)
{
	int rc, sig;
	int sig_array[] = {SIGINT, SIGTERM, SIGHUP, SIGABRT, 0};
	sigset_t set;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	/* Make sure no required signals are ignored (possibly inherited) */
	_default_sigaction(SIGINT);
	_default_sigaction(SIGTERM);
	_default_sigaction(SIGHUP);
	_default_sigaction(SIGABRT);

	while (1) {
		xsignal_sigset_create(sig_array, &set);
		rc = sigwait(&set, &sig);
		if (rc == EINTR)
			continue;
		switch (sig) {
		case SIGHUP:	/* kill -1 */
			info("Reconfigure signal (SIGHUP) received");
			reconfig();
			break;
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received");
			shutdown_threads();
			return NULL;	/* Normal termination */
		case SIGABRT:	/* abort */
			info("SIGABRT received");
			abort();	/* Should terminate here */
			shutdown_threads();
			return NULL;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}

}

/* Reset some signals to their default state to clear any
 * inherited signal states */
static void _default_sigaction(int sig)
{
	struct sigaction act;

	if (sigaction(sig, NULL, &act)) {
		error("sigaction(%d): %m", sig);
		return;
	}
	if (act.sa_handler != SIG_IGN)
		return;

	act.sa_handler = SIG_DFL;
	if (sigaction(sig, &act, NULL))
		error("sigaction(%d): %m", sig);
}

static void _become_slurm_user(void)
{
	gid_t slurm_user_gid;

	/* Determine SlurmUser gid */
	slurm_user_gid = gid_from_uid(slurmdbd_conf->slurm_user_id);
	if (slurm_user_gid == (gid_t) -1) {
		fatal("Failed to determine gid of SlurmUser(%u)",
		      slurmdbd_conf->slurm_user_id);
	}

	/* Initialize supplementary groups ID list for SlurmUser */
	if (getuid() == 0) {
		/* root does not need supplementary groups */
		if ((slurmdbd_conf->slurm_user_id == 0) &&
		    (setgroups(0, NULL) != 0)) {
			fatal("Failed to drop supplementary groups, "
			      "setgroups: %m");
		} else if ((slurmdbd_conf->slurm_user_id != getuid()) &&
			   initgroups(slurmdbd_conf->slurm_user_name,
				      slurm_user_gid)) {
			fatal("Failed to set supplementary groups, "
			      "initgroups: %m");
		}
	} else {
		info("Not running as root. Can't drop supplementary groups");
	}

	/* Set GID to GID of SlurmUser */
	if ((slurm_user_gid != getegid()) &&
	    (setgid(slurm_user_gid))) {
		fatal("Failed to set GID to %d", slurm_user_gid);
	}

	/* Set UID to UID of SlurmUser */
	if ((slurmdbd_conf->slurm_user_id != getuid()) &&
	    (setuid(slurmdbd_conf->slurm_user_id))) {
		fatal("Can not set uid to SlurmUser(%u): %m",
		      slurmdbd_conf->slurm_user_id);
	}
}
