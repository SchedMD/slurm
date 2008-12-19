/*****************************************************************************\
 *  slurmdbd.c - functions for SlurmDBD
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
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
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"

/* Global variables */
time_t shutdown_time = 0;		/* when shutdown request arrived */

/* Local variables */
static int    dbd_sigarray[] = {	/* blocked signals for this process */
			SIGINT,  SIGTERM, SIGCHLD, SIGUSR1,
			SIGUSR2, SIGTSTP, SIGXCPU, SIGQUIT,
			SIGPIPE, SIGALRM, SIGABRT, SIGHUP, 0 };
static int    debug_level = 0;		/* incremented for -v on command line */
static int    foreground = 0;		/* run process as a daemon */
static log_options_t log_opts = 	/* Log to stderr & syslog */
			LOG_OPTS_INITIALIZER;
static pthread_t rpc_handler_thread;	/* thread ID for RPC hander */
static pthread_t signal_handler_thread;	/* thread ID for signal hander */
static pthread_t rollup_handler_thread;	/* thread ID for rollup hander */
static pthread_mutex_t rollup_lock = PTHREAD_MUTEX_INITIALIZER;
static bool running_rollup = 0;

/* Local functions */
static void  _daemonize(void);
static void  _default_sigaction(int sig);
static void  _init_config(void);
static void  _init_pidfile(void);
static void  _kill_old_slurmdbd(void);
static void  _parse_commandline(int argc, char *argv[]);
static void _rollup_handler_cancel();
static void *_rollup_handler(void *no_data);
static void *_signal_handler(void *no_data);
static void  _update_logging(void);
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
	_update_logging();

	if (gethostname_short(node_name, sizeof(node_name)))
		fatal("getnodename: %m");
	if (slurmdbd_conf->dbd_host &&
	    strcmp(slurmdbd_conf->dbd_host, node_name) &&
	    strcmp(slurmdbd_conf->dbd_host, "localhost")) {
		fatal("This host not configured to run SlurmDBD (%s != %s)",
		      node_name, slurmdbd_conf->dbd_host);
	}
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
	_init_pidfile();
	log_config();

	if (xsignal_block(dbd_sigarray) < 0)
		error("Unable to block signals");

	db_conn = acct_storage_g_get_connection(false, 0, false);
	
	/* Create attached thread for signal handling */
	slurm_attr_init(&thread_attr);
	if (pthread_create(&signal_handler_thread, &thread_attr,
			   _signal_handler, NULL))
		fatal("pthread_create %m");
	slurm_attr_destroy(&thread_attr);

	memset(&assoc_init_arg, 0, sizeof(assoc_init_args_t));

	/* If we are tacking wckey we need to cache associations and
	   wckeys if we aren't only cache the users */
	if(slurmdbd_conf->track_wckey)
		assoc_init_arg.cache_level = 
			ASSOC_MGR_CACHE_USER | ASSOC_MGR_CACHE_WCKEY;
	else
		assoc_init_arg.cache_level = ASSOC_MGR_CACHE_USER;

	if(assoc_mgr_init(db_conn, &assoc_init_arg) == SLURM_ERROR) {
		error("Problem getting cache of data");
		acct_storage_g_close_connection(&db_conn);
		goto end_it;
	}

	if(!shutdown_time) {
		/* Create attached thread to process incoming RPCs */
		slurm_attr_init(&thread_attr);
		if (pthread_create(&rpc_handler_thread, &thread_attr, 
				   rpc_mgr, NULL))
			fatal("pthread_create error %m");
		slurm_attr_destroy(&thread_attr);
	}

	if(!shutdown_time) {
		/* Create attached thread to do usage rollup */
		slurm_attr_init(&thread_attr);
		if (pthread_create(&rollup_handler_thread, &thread_attr,
				   _rollup_handler, db_conn))
			fatal("pthread_create error %m");
		slurm_attr_destroy(&thread_attr);
	}

	/* Daemon is fully operational here */
	info("slurmdbd version %s started", SLURM_VERSION);

	/* Daemon termination handled here */
	if(rollup_handler_thread)
		pthread_join(rollup_handler_thread, NULL);

	if(rpc_handler_thread)
		pthread_join(rpc_handler_thread, NULL);

	if(signal_handler_thread)
		pthread_join(signal_handler_thread, NULL);

end_it:
	acct_storage_g_close_connection(&db_conn);

	if (slurmdbd_conf->pid_file &&
	    (unlink(slurmdbd_conf->pid_file) < 0)) {
		verbose("Unable to remove pidfile '%s': %m",
			slurmdbd_conf->pid_file);
	}

	assoc_mgr_fini(NULL);
	slurm_acct_storage_fini();
	slurm_auth_fini();
	log_fini();
	free_slurmdbd_conf();
	exit(0);
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

	opterr = 0;
	while ((c = getopt(argc, argv, "DhvV")) != -1)
		switch (c) {
		case 'D':
			foreground = 1;
			break;
		case 'h':
			_usage(argv[0]);
			exit(0);
			break;
		case 'v':
			debug_level++;
			break;
		case 'V':
			printf("%s %s\n", PACKAGE, SLURM_VERSION);
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
	fprintf(stderr, "  -v         \t"
			"Verbose mode. Multiple -v's increase verbosity.\n");
	fprintf(stderr, "  -V         \t"
			"Print version information and exit.\n");
}

/* Reset slurmctld logging based upon configuration parameters */
static void _update_logging(void) 
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
	int   fd;

	if (slurmdbd_conf->pid_file == NULL) {
		error("No PidFile configured");
		return;
	}

	if ((fd = create_pidfile(slurmdbd_conf->pid_file)) < 0)
		return;
}

/* Become a daemon (child of init) and 
 * "cd" to the LogFile directory (if one is configured) */
static void _daemonize(void)
{
	if (daemon(1, 1))
		error("daemon(): %m");
	log_alter(log_opts, LOG_DAEMON, slurmdbd_conf->log_file);
		
	if (slurmdbd_conf->log_file &&
	    (slurmdbd_conf->log_file[0] == '/')) {
		char *slash_ptr, *work_dir;
		work_dir = xstrdup(slurmdbd_conf->log_file);
		slash_ptr = strrchr(work_dir, '/');
		if (slash_ptr == work_dir)
			work_dir[1] = '\0';
		else
			slash_ptr[0] = '\0';
		if (chdir(work_dir) < 0)
			fatal("chdir(%s): %m", work_dir);
		xfree(work_dir);
	}
}

static void _rollup_handler_cancel()
{
	if(running_rollup)
		debug("Waiting for rollup thread to finish.");
	slurm_mutex_lock(&rollup_lock);
	if(rollup_handler_thread)
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

	if(!localtime_r(&start_time, &tm)) {
		fatal("Couldn't get localtime for rollup handler %d",
		      start_time);
		return NULL;
	}

	while (1) {
		if(!db_conn)
			break;
		/* run the roll up */
		slurm_mutex_lock(&rollup_lock);
		running_rollup = 1;
		debug2("running rollup at %s", ctime(&start_time));
		acct_storage_g_roll_usage(db_conn, 0);
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
		if(!localtime_r(&start_time, &tm)) {
			fatal("Couldn't get localtime for rollup handler %d",
			      start_time);
			return NULL;
		}
		/* repeat ;) */

	}

	return NULL;
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
			read_slurmdbd_conf();
			_update_logging();
			break;
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received");
			shutdown_time = time(NULL);
			rpc_mgr_wake();
			_rollup_handler_cancel();

			return NULL;	/* Normal termination */
		case SIGABRT:	/* abort */
			info("SIGABRT received");
			abort();	/* Should terminate here */
			shutdown_time = time(NULL);
			rpc_mgr_wake();
			_rollup_handler_cancel();
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
