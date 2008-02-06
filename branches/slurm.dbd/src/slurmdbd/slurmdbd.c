/*****************************************************************************\
 *  slurmdbd.c - function for SlurmDBD
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
 *  UCRL-CODE-226842.
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
#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */
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
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmdbd/read_config.h"

/* Log to stderr and syslog until becomes a daemon */
log_options_t log_opts = LOG_OPTS_INITIALIZER;
static int debug_level = 0;	/* incremented for -v on command line */
static int foreground = 0;	/* run process as a daemon */
static int cold_start = 0;	/* do not recover any state information */

/* Local functions */
static void  _init_config(void);
static void _kill_old_slurmdbd(void);
static void _parse_commandline(int argc, char *argv[]);
static void _update_logging(void);
static void _update_logging(void);
static void _usage(char *prog_name);

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char *argv[])
{
	_init_config();
	log_init(argv[0], log_opts, LOG_DAEMON, NULL);
	if (read_slurmdbd_conf())
		exit(1);
	_parse_commandline(argc, argv);
	_update_logging();
	_kill_old_slurmdbd();
/* FIXME */

	free_slurmdbd_conf();
	exit(0);
}

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
	while ((c = getopt(argc, argv, "cDhL:vV")) != -1)
		switch (c) {
		case 'c':
			cold_start = 1;
			break;
		case 'D':
			foreground = 1;
			break;
		case 'h':
			_usage(argv[0]);
			exit(0);
			break;
		case 'L':
			xfree(slurmdbd_conf->log_file);
			slurmdbd_conf->log_file = xstrdup(optarg);
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
	fprintf(stderr, "  -c      "
			"\tDo not recover state from last checkpoint.\n");
	fprintf(stderr, "  -D      "
			"\tRun daemon in foreground.\n");
	fprintf(stderr, "  -h      "
			"\tPrint this help message.\n");
	fprintf(stderr, "  -L logfile "
			"\tLog messages to the specified file.\n");
	fprintf(stderr, "  -v      "
			"\tVerbose mode. Multiple -v's increase verbosity.\n");
	fprintf(stderr, "  -V      "
			"\tPrint version information and exit.\n");
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
		info("killing old slurmdbd[%ld]", (long) oldpid);
		kill(oldpid, SIGTERM);

		/* 
		 * Wait for previous daemon to terminate
		 */
		if (fd_get_readw_lock(fd) < 0) 
			fatal("unable to wait for readw lock: %m");
		(void) close(fd); /* Ignore errors */ 
	}
}
