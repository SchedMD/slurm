/*****************************************************************************\
 *  opt.c - options processing for scancel
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
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
 *  to link the code of portions of this program with the OpenSSL library unde
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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/scancel/scancel.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP  0x100
#define OPT_LONG_USAGE 0x101
#define OPT_LONG_CTLD  0x102
#define OPT_LONG_WCKEY 0x103

#define SIZE(a) (sizeof(a)/sizeof(a[0]))

struct signv {
	char *name;
	uint16_t val;
} sig_name_num[ ] = {
	{ "HUP",	SIGHUP  },
	{ "INT",	SIGINT  },
	{ "QUIT",	SIGQUIT },
	{ "ABRT",	SIGABRT },
	{ "KILL",	SIGKILL },
	{ "ALRM",	SIGALRM },
	{ "TERM",	SIGTERM },
	{ "USR1",	SIGUSR1 },
	{ "USR2",	SIGUSR2 },
	{ "CONT",	SIGCONT },
	{ "STOP",	SIGSTOP },
	{ "TSTP",	SIGTSTP },
	{ "TTIN",	SIGTTIN },
	{ "TTOU",	SIGTTOU }
};

/* forward declarations of static functions 
 *
 */

static void _help(void);

/* fill in default options  */
static void _opt_default(void);

/* set options based upon env vars  */
static void _opt_env(void);

/* set options based upon commandline args */
static void _opt_args(int, char **);

/* verify options sanity  */
static bool _opt_verify(void);

static void _print_version (void);

static void _xlate_job_step_ids(char **rest);

/* translate job state name to number */
static uint16_t _xlate_state_name(const char *state_name, bool env_var);

/* translate name name to number */
static uint16_t _xlate_signal_name(const char *signal_name);

/* list known options and their settings */
static void _opt_list(void);

static void _usage(void);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char *argv[])
{
	/* initialize option defaults */
	_opt_default();

	/* initialize options with env vars */
	_opt_env();

	/* initialize options with argv */
	_opt_args(argc, argv);

	if (opt.verbose > 2)
		_opt_list();

	return 1;

}

static uint16_t 
_xlate_state_name(const char *state_name, bool env_var)
{
	enum job_states i;

	for (i=0; i<JOB_END; i++) {
		if ((strcasecmp(state_name,job_state_string(i)) == 0) ||
		    (strcasecmp(state_name,job_state_string_compact(i)) == 0)){
			return i;
		}
	}
	if ((strcasecmp(state_name, 
			job_state_string(JOB_COMPLETING)) == 0) ||
	    (strcasecmp(state_name, 
			job_state_string_compact(JOB_COMPLETING)) == 0)) {
		return JOB_COMPLETING;
	}
	if ((strcasecmp(state_name, 
			job_state_string(JOB_CONFIGURING)) == 0) ||
	    (strcasecmp(state_name, 
			job_state_string_compact(JOB_CONFIGURING)) == 0)) {
		return JOB_CONFIGURING;
	}

	if (env_var)
		fprintf(stderr, "Unrecognized SCANCEL_STATE value: %s\n",
			state_name);
	else
		fprintf(stderr, "Invalid job state specified: %s\n",
			state_name);

	fprintf (stderr, "Valid job states are PENDING, RUNNING, and SUSPENDED\n");
	exit (1);
}


static uint16_t _xlate_signal_name(const char *signal_name)
{
	uint16_t sig_num;
	char *end_ptr, *sig_names = NULL;
	int i;

	sig_num = (uint16_t) strtol(signal_name, &end_ptr, 10);
	if ((*end_ptr == '\0') || (sig_num != 0))
		return sig_num;
	
	for (i=0; i<SIZE(sig_name_num); i++) {
		if (strcasecmp(sig_name_num[i].name, signal_name) == 0) {
			xfree(sig_names);
			return sig_name_num[i].val;
		}
		if (i == 0)
			sig_names = xstrdup(sig_name_num[i].name);
		else {
			xstrcat(sig_names, ",");
			xstrcat(sig_names, sig_name_num[i].name);
		}			
	}
	fprintf (stderr, "Invalid job signal: %s\n", signal_name);
	fprintf (stderr, "Valid signals include: %s\n", sig_names);
	xfree(sig_names);
	exit(1);
}

static void _print_version (void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

/*
 * opt_default(): used by initialize_and_process_args to set defaults
 */
static void _opt_default()
{
	opt.account	= NULL;
	opt.batch	= false;
	opt.ctld	= false;
	opt.interactive	= false;
	opt.job_cnt	= 0;
	opt.job_name	= NULL;
	opt.nodelist	= NULL;
	opt.partition	= NULL;
	opt.qos		= NULL;
	opt.signal	= (uint16_t)-1; /* no signal specified */
	opt.state	= JOB_END;
	opt.user_id	= 0;
	opt.user_name	= NULL;
	opt.verbose	= 0;
	opt.wckey	= NULL;
}

/*
 * opt_env(): used by initialize_and_process_args to set options via
 *            environment variables. See comments above for how to
 *            extend srun to process different vars
 */
static void _opt_env()
{
	char *val;

	if ( (val=getenv("SCANCEL_ACCOUNT")) ) {
		opt.account = xstrdup(val);
	}

	if ( (val=getenv("SCANCEL_BATCH")) ) {
		if (strcasecmp(val, "true") == 0)
			opt.batch       = true;
		else if (strcasecmp(val, "T") == 0)
			opt.batch       = true;
		else if (strcasecmp(val, "false") == 0)
			opt.batch       = false;
		else if (strcasecmp(val, "F") == 0)
			opt.batch       = false;
		else
			error ("Unrecognized SCANCEL_BATCH value: %s",
				val);
	}

	if (getenv("SCANCEL_CTLD"))
		opt.ctld = true;

	if ( (val=getenv("SCANCEL_INTERACTIVE")) ) {
		if (strcasecmp(val, "true") == 0)
			opt.interactive = true;
		else if (strcasecmp(val, "T") == 0)
			opt.interactive = true;
		else if (strcasecmp(val, "false") == 0)
			opt.interactive = false;
		else if (strcasecmp(val, "F") == 0)
			opt.interactive = false;
		else
			error ("Unrecognized SCANCEL_INTERACTIVE value: %s",
				val);
	}

	if ( (val=getenv("SCANCEL_NAME")) ) {
		opt.job_name = xstrdup(val);
	}

	if ( (val=getenv("SCANCEL_PARTITION")) ) {
		opt.partition = xstrdup(val);
	}

	if ( (val=getenv("SCANCEL_QOS")) ) {
		opt.qos = xstrdup(val);
	}

	if ( (val=getenv("SCANCEL_STATE")) ) {
		opt.state = _xlate_state_name(val, true);
	}

	if ( (val=getenv("SCANCEL_USER")) ) {
		opt.user_name = xstrdup(val);
	}

	if ( (val=getenv("SCANCEL_VERBOSE")) ) {
		if (strcasecmp(val, "true") == 0)
			opt.verbose = 1;
		else if (strcasecmp(val, "T") == 0)
			opt.verbose = 1;
		else if (strcasecmp(val, "false") == 0)
			opt.verbose = 0;
		else if (strcasecmp(val, "F") == 0)
			opt.verbose = 0;
		else
			error ("Unrecognized SCANCEL_VERBOSE value: %s",
				val);
	}

	if ( (val=getenv("SCANCEL_WCKEY")) ) {
		opt.wckey = xstrdup(val);
	}
}

/*
 * opt_args() : set options via commandline args and getopt_long
 */
static void _opt_args(int argc, char **argv)
{
	int opt_char;
	int option_index;
	static struct option long_options[] = { 
		{"account",	required_argument, 0, 'A'},
		{"batch",	no_argument,       0, 'b'},
		{"ctld",	no_argument,	   0, OPT_LONG_CTLD},
		{"help",        no_argument,       0, OPT_LONG_HELP},
		{"interactive", no_argument,       0, 'i'},
		{"name",        required_argument, 0, 'n'},
		{"nodelist",    required_argument, 0, 'w'},
		{"partition",   required_argument, 0, 'p'},
		{"qos",         required_argument, 0, 'q'},
		{"quiet",       no_argument,       0, 'Q'},
		{"signal",      required_argument, 0, 's'},
		{"state",       required_argument, 0, 't'},
		{"usage",       no_argument,       0, OPT_LONG_USAGE},
		{"user",        required_argument, 0, 'u'},
		{"verbose",     no_argument,       0, 'v'},
		{"version",     no_argument,       0, 'V'},
		{"wckey",       required_argument, 0, OPT_LONG_WCKEY},
		{NULL,          0,                 0, 0}
	};

	while((opt_char = getopt_long(argc, argv, "A:bin:p:Qq:s:t:u:vVw:",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
			case (int)'?':
				fprintf(stderr, 
					"Try \"scancel --help\" for more "
					"information\n");
				exit(1);
				break;
			case (int)'A':
				opt.account = xstrdup(optarg);
				break;
			case (int)'b':
				opt.batch = true;
				break;
			case OPT_LONG_CTLD:
				opt.ctld = true;
				break;
			case (int)'i':
				opt.interactive = true;
				break;
			case (int)'n':
				opt.job_name = xstrdup(optarg);
				break;
			case (int)'p':
				opt.partition = xstrdup(optarg);
				break;
			case (int)'Q':
				opt.verbose = -1;
				break;
			case (int)'q':
				opt.qos = xstrdup(optarg);
				break;
			case (int)'s':
				opt.signal = _xlate_signal_name(optarg);
				break;
			case (int)'t':
				opt.state = _xlate_state_name(optarg, false);
				break;
			case (int)'u':
				opt.user_name = xstrdup(optarg);
				break;
			case (int)'v':
				opt.verbose++;
				break;
			case (int)'V':
				_print_version();
				exit(0);
			case (int)'w':
				opt.nodelist = xstrdup(optarg);
				break;
			case OPT_LONG_WCKEY:
				opt.wckey = xstrdup(optarg);
				break;
			case OPT_LONG_HELP:
				_help();
				exit(0);
			case OPT_LONG_USAGE:
				_usage();
				exit(0);
		}
	}

	if (optind < argc) {
		char **rest = argv + optind;
		_xlate_job_step_ids(rest);
	}

	if (!_opt_verify())
		exit(1);
}

static void
_xlate_job_step_ids(char **rest)
{
	int i;
	long tmp_l;
	char *next_str;

	opt.job_cnt = 0;

	if (rest != NULL) {
		while (rest[opt.job_cnt] != NULL)
			opt.job_cnt++;
	}

	opt.job_id  = xmalloc(opt.job_cnt * sizeof(uint32_t));
	opt.step_id = xmalloc(opt.job_cnt * sizeof(uint32_t));

	for (i=0; i<opt.job_cnt; i++) {
		tmp_l = strtol(rest[i], &next_str, 10);
		if (tmp_l <= 0) {
			error ("Invalid job_id %s", rest[i]);
			exit (1);
		}
		opt.job_id[i] = tmp_l;

		if (next_str[0] == '.') {
			tmp_l = strtol(&next_str[1], &next_str, 10);
			if (tmp_l < 0) {
				error ("Invalid job id %s", rest[i]);
				exit (1);
			}
			opt.step_id[i] = tmp_l;
		} else 
			opt.step_id[i] = SLURM_BATCH_SCRIPT;

		if (next_str[0] != '\0') {
			error ("Invalid job ID %s", rest[i]);
			exit (1);
		}
	}
}


/* 
 * opt_verify : perform some post option processing verification
 *
 */
static bool
_opt_verify(void)
{
	bool verified = true;
	struct passwd *passwd_ptr;

	if (opt.user_name) {	/* translate to user_id */
		passwd_ptr = getpwnam (opt.user_name);
		if (passwd_ptr == NULL) {
			error("Invalid user name: %s", opt.user_name);
			return false;
		} else {
			opt.user_id = passwd_ptr->pw_uid;
		}
	}

	if ((opt.account == 0) &&
	    (opt.job_cnt == 0) &&
	    (opt.job_name == NULL) &&
	    (opt.nodelist == NULL) &&
	    (opt.partition == NULL) &&
	    (opt.qos == NULL) &&
	    (opt.state == JOB_END) &&
	    (opt.user_name == NULL) &&
	    (opt.wckey == NULL)) {
		error("No job identification provided");
		verified = false;	/* no job specification */
	}

	return verified;
}

#define tf_(b) (b == true) ? "true" : "false"

static void _opt_list(void)
{
	int i;

	info("account        : %s", opt.account);
	info("batch          : %s", tf_(opt.batch));
	info("ctld           : %s", tf_(opt.ctld));
	info("interactive    : %s", tf_(opt.interactive));
	info("job_name       : %s", opt.job_name);
	info("nodelist       : %s", opt.nodelist);
	info("partition      : %s", opt.partition);
	info("qos            : %s", opt.qos);
	info("signal         : %u", opt.signal);
	info("state          : %s", job_state_string(opt.state));
	info("user_id        : %u", opt.user_id);
	info("user_name      : %s", opt.user_name);
	info("verbose        : %d", opt.verbose);
	info("wckey          : %s", opt.wckey);

	for (i=0; i<opt.job_cnt; i++) {
		info("job_steps      : %u.%u ", opt.job_id[i], opt.step_id[i]);
	}
}

static void _usage(void)
{
	printf("Usage: scancel [-A account] [--batch] [--interactive] [-n job_name]\n");
	printf("               [-p partition] [-Q] [-q qos] [-s signal | integer]\n");
	printf("               [-t PENDING | RUNNING | SUSPENDED] [--usage] [-u user_name]\n");
	printf("               [-V] [-v] [-w hosts...] [--wckey=wckey] [job_id[.step_id]]\n");
}

static void _help(void)
{
	printf("Usage: scancel [OPTIONS] [job_id[.step_id]]\n");
	printf("  -A, --account=account           act only on jobs charging this account\n");
	printf("  -b, --batch                     signal batch shell for specified job\n");
/*	printf("      --ctld                      route request through slurmctld\n"); */
	printf("  -i, --interactive               require response from user for each job\n");
	printf("  -n, --name=job_name             act only on jobs with this name\n");
	printf("  -p, --partition=partition       act only on jobs in this partition\n");
	printf("  -Q, --quiet                     disable warnings\n");
	printf("  -q, --qos=qos                   act only on jobs with this quality of service\n");
	printf("  -s, --signal=name | integer     signal to send to job, default is SIGKILL\n");
	printf("  -t, --state=states              act only on jobs in this state.  Valid job\n");
	printf("                                  states are PENDING, RUNNING and SUSPENDED\n");
	printf("  -u, --user=user_name            act only on jobs of this user\n");
	printf("  -V, --version                   output version information and exit\n");
	printf("  -v, --verbose                   verbosity level\n");
	printf("  -w, --nodelist                  act only on jobs on these nodes\n");
	printf("      --wckey=wckey               act only on jobs with this workload\n");
	printf("                                  charactization key\n");
	printf("\nHelp options:\n");
	printf("  --help                          show this help message\n");
	printf("  --usage                         display brief usage message\n");
}
