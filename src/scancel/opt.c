/*****************************************************************************\
 *  opt.c - options processing for scancel
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
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

#if HAVE_POPT_H
#include <popt.h>
#else
#include <src/popt/popt.h>
#endif

#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>		/* strcpy, strncasecmp */
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#include <src/common/log.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/xmalloc.h>
#include <src/common/xstring.h>
#include <src/scancel/scancel.h>

#define __DEBUG 0

#define SIZE(a) (sizeof(a)/sizeof(a[0]))

/*---[ popt definitions ]------------------------------------------------*/

/* generic OPT_ definitions -- mainly for use with env vars 
 * (not to be confused with POPT_* definitions)
 */
#define OPT_NONE	0x00
#define OPT_INT		0x01
#define OPT_STRING	0x02

/* specific options, used with popt (and env var processing) */
#define OPT_INTERACTIVE	0x03
#define OPT_NAME	0x04
#define OPT_PARTITION	0x05
#define OPT_STATE	0x06
#define OPT_USER	0x07
#define OPT_VERBOSE	0x08
#define OPT_VERSION	0x09
#define OPT_SIGNAL	0x0a


#ifndef POPT_TABLEEND
#  define POPT_TABLEEND { NULL, '\0', 0, 0, 0, NULL, NULL }
#endif

struct poptOption options[] = {
	{"interactive", 'i', POPT_ARG_NONE, &opt.interactive, OPT_INTERACTIVE,
	 "confirm each job cancelation", },
	{"name", 'n', POPT_ARG_STRING, NULL, OPT_NAME,
	 "name of job", "name"},
	{"partition", 'p', POPT_ARG_STRING, NULL, OPT_PARTITION,
	 "name of job's partition", "name"},
	{"signal", 's', POPT_ARG_STRING, NULL, OPT_SIGNAL,
	 "signal name or number", "name | integer"},
	{"state", 't', POPT_ARG_STRING, NULL, OPT_STATE,
	 "name of job's state", "PENDING | RUNNING"},
	{"user", 'u', POPT_ARG_STRING, NULL, OPT_USER,
	 "name of job's owner", "name"},
        {"verbose", 'v', 0, 0, OPT_VERBOSE,
	 "verbose operation (multiple -v's increase verbosity)", },
	{"Version", 'V', POPT_ARG_NONE, NULL, OPT_VERSION,
	 "report the current version", },
	POPT_AUTOHELP
	POPT_TABLEEND
};

struct signv {
	char *name;
	uint16_t val;
} sys_signame[ ] = {
	{ "HUP",	SIGHUP },
	{ "INT",	SIGINT },
	{ "QUIT",	SIGQUIT },
	{ "KILL",	SIGKILL },
	{ "ALRM",	SIGALRM },
	{ "TERM",	SIGTERM },
	{ "USR1",	SIGUSR1 },
	{ "USR2",	SIGUSR2 },
	{ "STOP",	SIGSTOP },
	{ "CONT",	SIGCONT }
};

/*---[ end popt definitions ]---------------------------------------------*/

/* forward declarations of static functions 
 *
 */

/* 
 * fill in default options 
 */
static void opt_default(void);

/* set options based upon env vars 
 */
static void opt_env(void);

/* set options based upon commandline args
 */
static void opt_args(int, char **);

/* verify options sanity 
 */
static bool opt_verify(void);

static void print_version (void);

/* translate job state name to number
 */
static enum job_states xlate_state_name(const char *state_name);

/* translate name name to number
 */
static uint16_t xlate_signal_name(const char *signal_name);

/* list known options and their settings 
 */
#if	__DEBUG
static void opt_list(void);
#endif

static void xlate_job_step_ids(const char **rest);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char *argv[])
{
	/* initialize option defaults */
	opt_default();

	/* initialize options with env vars */
	opt_env();

	/* initialize options with argv */
	opt_args(argc, argv);

#if	__DEBUG
	opt_list();
#endif
	return 1;

}

static enum job_states xlate_state_name(const char *state_name)
{
	enum job_states i;
	char *state_names;

	for (i=0; i<JOB_END; i++) {
		if ((strcasecmp(state_name, job_state_string(i)) == 0) ||
		    (strcasecmp(state_name, job_state_string_compact(i)) == 0)) {
			return i;
		}
	}

	fprintf (stderr, "Invalid job state specified: %s", state_name);
	state_names = xstrdup(job_state_string(0));
	for (i=1; i<JOB_END; i++) {
		xstrcat(state_names, ",");
		xstrcat(state_names, job_state_string(i));
	}
	fprintf (stderr, "Valid job states include: %s\n", state_names);
	xfree (state_names);
	exit (1);
}


static uint16_t xlate_signal_name(const char *signal_name)
{
	uint16_t sig_num;
	char *end_ptr, *sig_names;
	int i;

	sig_num = (uint16_t) strtol(signal_name, &end_ptr, 10);
	if ((*end_ptr == '\0') || (sig_num != 0))
		return sig_num;
	
	for (i=0; i<SIZE(sys_signame); i++) {
		if (strcasecmp(sys_signame[i].name, signal_name) == 0) {
			xfree(sig_names);
			return sys_signame[i].val;
		}
		if (i == 0)
			sig_names = xstrdup(sys_signame[i].name);
		else {
			xstrcat(sig_names, ",");
			xstrcat(sig_names, sys_signame[i].name);
		}			
	}
	fprintf (stderr, "Invalid job signal: %s\n", signal_name);
	fprintf (stderr, "Valid signals include: %s\n", sig_names);
	xfree(sig_names);
	exit(1);
}

static void print_version (void)
{
	printf("%s %s\n", PACKAGE, VERSION);
}

/*
 * opt_default(): used by initialize_and_process_args to set defaults
 */
static void opt_default()
{
	opt.interactive = false;
	opt.job_cnt = 0;
	opt.job_name = NULL;
	opt.partition = NULL;
	opt.signal = SIGKILL;
	opt.state = JOB_END;
	opt.user_name = NULL;
	opt.user_id = 0;
	opt.verbose = false;
}

/*
 * opt_env(): used by initialize_and_process_args to set options via
 *            environment variables. See comments above for how to
 *            extend srun to process different vars
 */
static void opt_env()
{
	char *val;

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

	if ( (val=getenv("SCANCEL_STATE")) ) {
		opt.state = true;
			error ("Unrecognized SCANCEL_STATE value: %s",
				val);
	}

	if ( (val=getenv("SCANCEL_USER")) ) {
		opt.user_name = xstrdup(val);
	}

	if ( (val=getenv("SCANCEL_VERBOSE")) ) {
		if (strcasecmp(val, "true") == 0)
			opt.verbose = true;
		else if (strcasecmp(val, "T") == 0)
			opt.verbose = true;
		else if (strcasecmp(val, "false") == 0)
			opt.verbose = false;
		else if (strcasecmp(val, "F") == 0)
			opt.verbose = false;
		else
			error ("Unrecognized SCANCEL_VERBOSE value: %s",
				val);
	}
}

/*
 * opt_args() : set options via commandline args and popt
 */
static void opt_args(int ac, char **av)
{
	int rc;
	const char **rest;
	const char *arg;
	poptContext optctx;

	optctx = poptGetContext("scancel", ac, (const char **) av, options,
				POPT_CONTEXT_POSIXMEHARDER);

	poptSetOtherOptionHelp(optctx, "[OPTIONS] [job_id.step_id]");

	poptReadDefaultConfig(optctx, 0);

	/* first pass through args to see if attach or allocate mode
	 * are set
	 */
	while ((rc = poptGetNextOpt(optctx)) > 0) {
		arg = poptGetOptArg(optctx);

		switch (rc) {
		case OPT_NAME:
			opt.job_name = xstrdup(arg);
			break;

		case OPT_PARTITION:
			opt.partition = xstrdup(arg);
			break;

		case OPT_SIGNAL:
			opt.signal = xlate_signal_name(arg);
			break;

		case OPT_STATE:
			opt.state = xlate_state_name(arg);
			break;

		case OPT_USER:
			opt.user_name = xstrdup(arg);
			break;

		case OPT_VERBOSE:
			opt.verbose++;
			break;

		case OPT_VERSION:
			print_version();
			exit(0);
			break;

		default:
			break;
			/* do nothing */
		}
	}

	if (rc < -1) {
		const char *bad_opt;
		bad_opt = poptBadOption(optctx, POPT_BADOPTION_NOALIAS);
		error("bad argument %s: %s", bad_opt, poptStrerror(rc));
		error("Try \"scancel --help\" for more information\n");
		exit(1);
	}

	rest = poptGetArgs(optctx);
	xlate_job_step_ids(rest);

	if (!opt_verify()) {
		poptPrintUsage(optctx, stderr, 0);
		exit(1);
	}

	poptFreeContext(optctx);

}

static void
xlate_job_step_ids(const char **rest)
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
			opt.step_id[i] = NO_VAL;

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
opt_verify(void)
{
	bool verified = true;
	struct passwd *passwd_ptr;

	if (opt.user_name) {	/* translate to user_id */
		passwd_ptr = getpwnam (opt.user_name);
		if (passwd_ptr == NULL) {
			error ("Invalid user name: %s", opt.user_name);
			return false;
		} else {
			opt.user_id = passwd_ptr->pw_uid;
		}
	}

	if ((opt.user_id) &&
	    (opt.user_id != getuid()) &&
	    (opt.user_id != geteuid())) {
		error ("You are not authorized to delete jobs of user %u",
			opt.user_id);
		exit (1);
	}

	if ((opt.job_name == NULL) &&
	    (opt.partition == NULL) &&
	    (opt.state == JOB_END) &&
	    (opt.user_name == NULL) &&
	    (opt.job_cnt == 0))
		verified = false;	/* no job specification */

	return verified;
}

#if	__DEBUG

#define tf_(b) (b == true) ? "true" : "false"

static void 
opt_list(void)
{
	int i;

	info("interactive    : %s", tf_(opt.interactive));
	info("job_name       : %s", opt.job_name);
	info("partition      : %s", opt.partition);
	info("signal         : %u", opt.signal);
	info("state          : %s", job_state_string(opt.state));
	info("user_id        : %u", opt.user_id);
	info("user_name      : %s", opt.user_name);
	info("verbose        : %d", opt.verbose);

	for (i=0; i<opt.job_cnt; i++) {
		info("job_steps      : %u.%u ", opt.job_id[i], opt.step_id[i]);
	}
}

#endif				/* __DEBUG */

