/*****************************************************************************\
 *  opt.c - options processing for scancel
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2015 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
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

#define _GNU_SOURCE

#include <getopt.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"
#include "src/common/uid.h"

#include "src/scancel/scancel.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP    0x100
#define OPT_LONG_USAGE   0x101
#define OPT_LONG_CTLD    0x102
#define OPT_LONG_WCKEY   0x103
#define OPT_LONG_SIBLING 0x104
#define OPT_LONG_ME      0x105
#define OPT_LONG_AUTOCOMP 0x106

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

static char **_xlate_job_step_ids(char **rest);

/* translate job state name to number */
static uint32_t _xlate_state_name(const char *state_name, bool env_var);

/* list known options and their settings */
static void _opt_list(void);

static void _usage(void);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char **argv)
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

/*
 * No job filtering options were specified (e.g. by user or state), only the
 * job ids is on the command line.
 */
extern bool has_default_opt(void)
{
	if (opt.account == NULL
	    && opt.batch == false
	    && opt.interactive == false
	    && opt.job_name == NULL
	    && opt.partition == NULL
	    && opt.qos == NULL
	    && opt.reservation == NULL
	    && opt.signal == NO_VAL16
	    && opt.state == JOB_END
	    && opt.user_id == 0
	    && opt.user_name == NULL
	    && opt.wckey == NULL
	    && opt.nodelist == NULL) {
		return true;
	}
	return false;
}

/* Return true if any job step specification given */
extern bool has_job_steps(void)
{
	int i;

	for (i = 0; i < opt.job_cnt; i++) {
		if (opt.step_id[i] != SLURM_BATCH_SCRIPT)
			return true;
	}
	return false;
}

static uint32_t
_xlate_state_name(const char *state_name, bool env_var)
{
	uint32_t i = job_state_num(state_name);

	if (i != NO_VAL)
		return i;

	if (env_var) {
		fprintf(stderr, "Unrecognized SCANCEL_STATE value: %s\n",
			state_name);
	} else {
		fprintf(stderr, "Invalid job state specified: %s\n",
			state_name);
	}
	fprintf(stderr,
		"Valid job states are PENDING, RUNNING, and SUSPENDED\n");
	exit (1);
}

/*
 * opt_default(): used by initialize_and_process_args to set defaults
 */
static void _opt_default(void)
{
	opt.account	= NULL;
	opt.batch	= false;
	opt.clusters    = NULL;
#ifdef HAVE_FRONT_END
	opt.ctld	= true;
#else
	opt.ctld	= false;
#endif
	opt.cron = false;
	opt.full	= false;
	opt.hurry	= false;
	opt.interactive	= false;
	opt.job_cnt	= 0;
	opt.job_list    = NULL;
	opt.job_name	= NULL;
	opt.nodelist	= NULL;
	opt.partition	= NULL;
	opt.qos		= NULL;
	opt.reservation	= NULL;
	opt.sibling     = NULL;
	opt.signal	= NO_VAL16;
	opt.state	= JOB_END;
	opt.user_id	= 0;
	opt.user_name	= NULL;
	opt.verbose	= 0;
	opt.wckey	= NULL;
}

static void _opt_clusters(char *clusters)
{
	opt.ctld = true;
	FREE_NULL_LIST(opt.clusters);
	opt.clusters = slurmdb_get_info_cluster(clusters);
	if (!opt.clusters) {
		print_db_notok(clusters, 0);
		exit(1);
	}
	working_cluster_rec = list_peek(opt.clusters);
}

/*
 * opt_env(): used by initialize_and_process_args to set options via
 *            environment variables. See comments above for how to
 *            extend srun to process different vars
 */
static void _opt_env(void)
{
	char *val;

	if ( (val=getenv("SCANCEL_ACCOUNT")) ) {
		opt.account = xstrtolower(xstrdup(val));
	}

	if ( (val=getenv("SCANCEL_BATCH")) ) {
		if (xstrcasecmp(val, "true") == 0)
			opt.batch       = true;
		else if (xstrcasecmp(val, "T") == 0)
			opt.batch       = true;
		else if (xstrcasecmp(val, "false") == 0)
			opt.batch       = false;
		else if (xstrcasecmp(val, "F") == 0)
			opt.batch       = false;
		else
			error ("Unrecognized SCANCEL_BATCH value: %s", val);
	}

	if (getenv("SCANCEL_CTLD"))
		opt.ctld = true;

	if (getenv("SCANCEL_CRON"))
		opt.cron = true;

	if ( (val=getenv("SCANCEL_FULL")) ) {
		if (xstrcasecmp(val, "true") == 0)
			opt.full       = true;
		else if (xstrcasecmp(val, "T") == 0)
			opt.full       = true;
		else if (xstrcasecmp(val, "false") == 0)
			opt.full       = false;
		else if (xstrcasecmp(val, "F") == 0)
			opt.full       = false;
		else
			error ("Unrecognized SCANCEL_FULL value: %s", val);
	}

	if (getenv("SCANCEL_HURRY"))
		opt.hurry = true;

	if ( (val=getenv("SCANCEL_INTERACTIVE")) ) {
		if (xstrcasecmp(val, "true") == 0)
			opt.interactive = true;
		else if (xstrcasecmp(val, "T") == 0)
			opt.interactive = true;
		else if (xstrcasecmp(val, "false") == 0)
			opt.interactive = false;
		else if (xstrcasecmp(val, "F") == 0)
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
		opt.qos = xstrtolower(xstrdup(val));
	}

	if ( (val=getenv("SCANCEL_STATE")) ) {
		opt.state = _xlate_state_name(val, true);
	}

	if ( (val=getenv("SCANCEL_USER")) ) {
		opt.user_name = xstrdup(val);
	}

	if ( (val=getenv("SCANCEL_VERBOSE")) ) {
		if (xstrcasecmp(val, "true") == 0)
			opt.verbose = 1;
		else if (xstrcasecmp(val, "T") == 0)
			opt.verbose = 1;
		else if (xstrcasecmp(val, "false") == 0)
			opt.verbose = 0;
		else if (xstrcasecmp(val, "F") == 0)
			opt.verbose = 0;
		else
			error ("Unrecognized SCANCEL_VERBOSE value: %s",
				val);
	}

	if ( (val=getenv("SCANCEL_WCKEY")) ) {
		opt.wckey = xstrdup(val);
	}

	if ((val = getenv("SLURM_CLUSTERS"))) {
		/*
		 * We must pass in a modifiable string, and we don't want to
		 * modify the environment.
		 */
		char *valdup = xstrdup(val);
		_opt_clusters(valdup);
		xfree(valdup);
	}
}

/*
 * opt_args() : set options via commandline args and getopt_long
 */
static void _opt_args(int argc, char **argv)
{
	char **rest = NULL;
	int opt_char, option_index;
	static struct option long_options[] = {
		{"autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP},
		{"account",	required_argument, 0, 'A'},
		{"batch",	no_argument,       0, 'b'},
		{"ctld",	no_argument,	   0, OPT_LONG_CTLD},
		{"cron",	no_argument,	   0, 'c'},
		{"full",	no_argument,       0, 'f'},
		{"help",        no_argument,       0, OPT_LONG_HELP},
		{"hurry",       no_argument,       0, 'H'},
		{"interactive", no_argument,       0, 'i'},
		{"cluster",     required_argument, 0, 'M'},
		{"clusters",    required_argument, 0, 'M'},
		{"jobname",     required_argument, 0, 'n'},
		{"me",		no_argument,       0, OPT_LONG_ME},
		{"name",        required_argument, 0, 'n'},
		{"nodelist",    required_argument, 0, 'w'},
		{"partition",   required_argument, 0, 'p'},
		{"qos",         required_argument, 0, 'q'},
		{"quiet",       no_argument,       0, 'Q'},
		{"reservation", required_argument, 0, 'R'},
		{"sibling",     required_argument, 0, OPT_LONG_SIBLING},
		{"signal",      required_argument, 0, 's'},
		{"state",       required_argument, 0, 't'},
		{"usage",       no_argument,       0, OPT_LONG_USAGE},
		{"user",        required_argument, 0, 'u'},
		{"verbose",     no_argument,       0, 'v'},
		{"version",     no_argument,       0, 'V'},
		{"wckey",       required_argument, 0, OPT_LONG_WCKEY},
		{NULL,          0,                 0, 0}
	};

	while ((opt_char = getopt_long(argc, argv,
				       "A:bcfHiM:n:p:Qq:R:s:t:u:vVw:",
				       long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr,
				"Try \"scancel --help\" for more "
				"information\n");
			exit(1);
			break;
		case (int)'A':
			opt.account = xstrtolower(xstrdup(optarg));
			break;
		case (int)'b':
			opt.batch = true;
			break;
		case OPT_LONG_CTLD:
			opt.ctld = true;
			break;
		case (int)'c':
			opt.cron = true;
			break;
		case (int)'f':
			opt.full = true;
			break;
		case (int)'H':
			opt.hurry = true;
			break;
		case (int)'i':
			opt.interactive = true;
			break;
		case (int)'M':
			_opt_clusters(optarg);
			break;
		case OPT_LONG_ME:
			opt.user_name = xstrdup_printf("%u", getuid());
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
			opt.qos = xstrtolower(xstrdup(optarg));
			break;
		case (int)'R':
			opt.reservation = xstrdup(optarg);
			break;
		case (int)'s':
			opt.signal = sig_name2num(optarg);
			if (!opt.signal) {
				fprintf(stderr, "Unknown job signal: %s\n",
					optarg);
				exit(1);
			}
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
			print_slurm_version ();
			exit(0);
		case (int)'w':
			opt.nodelist = xstrdup(optarg);
			break;
		case OPT_LONG_SIBLING:
			opt.sibling = xstrdup(optarg);
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
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
			break;
		}
	}

	if (optind < argc)
		rest = argv + optind;

	if (rest)
		opt.job_list = _xlate_job_step_ids(rest);

	if (!_opt_verify())
		exit(1);
}

static char **
_xlate_job_step_ids(char **rest)
{
	int buf_size, buf_offset, id_args_size, i, j;
	long job_id, tmp_l;
	char **id_args = NULL, *next_str;

	opt.job_cnt = 0;

	buf_size   = 0xffff;
	buf_offset = 0;
	opt.array_id = xmalloc(buf_size * sizeof(uint32_t));
	opt.job_id   = xmalloc(buf_size * sizeof(uint32_t));
	opt.step_id  = xmalloc(buf_size * sizeof(uint32_t));

	id_args_size = 128;
	id_args = xmalloc(sizeof(char *) * id_args_size);
	for (i = 0; rest[i]; i++) {
		id_args[i] = xstrdup(rest[i]);
		if ((i + 4) >= id_args_size) {
			id_args_size *= 2;
			id_args = xrealloc(id_args, sizeof(char *) *
					   id_args_size);
		}
	}

	for (i = 0; id_args[i] && (buf_offset < buf_size); i++) {
		job_id = strtol(id_args[i], &next_str, 10);
		if (job_id <= 0) {
			error ("Invalid job id %s", id_args[i]);
			exit (1);
		}
		opt.job_id[buf_offset] = job_id;

		if ((next_str[0] == '_') && (next_str[1] == '[')) {
			hostlist_t hl;
			char save_char, *next_elem;
			char *end_char = strchr(next_str + 2, ']');
			if (!end_char || (end_char[1] != '\0')) {
				error ("Invalid job id %s", id_args[i]);
				exit (1);
			}
			save_char = end_char[1];
			end_char[1] = '\0';
			hl = hostlist_create(next_str + 1);
			if (!hl) {
				error ("Invalid job id %s", id_args[i]);
				exit (1);
			}
			while ((next_elem = hostlist_shift(hl))) {
				tmp_l = strtol(next_elem, &next_str, 10);
				if (tmp_l < 0) {
					error ("Invalid job id %s", id_args[i]);
					exit (1);
				}
				opt.job_id[buf_offset]   = job_id;
				opt.array_id[buf_offset] = tmp_l;
				opt.step_id[buf_offset]  = SLURM_BATCH_SCRIPT;
				free(next_elem);
				if (++buf_offset >= buf_size)
					break;
			}
			hostlist_destroy(hl);
			end_char[1] = save_char;
			/* No step ID support for job array range */
			continue;
		} else if ((next_str[0] == '_') && (next_str[1] == '*')) {
			opt.array_id[buf_offset] = INFINITE;
			next_str += 2;
		} else if (next_str[0] == '_') {
			tmp_l = strtol(&next_str[1], &next_str, 10);
			if (tmp_l < 0) {
				error ("Invalid job id %s", id_args[i]);
				exit (1);
			}
			opt.array_id[buf_offset] = tmp_l;
		} else if (next_str[0] == '+') {	/* Hetjob component */
			tmp_l = strtol(&next_str[1], &next_str, 10);
			if (tmp_l < 0) {
				error ("Invalid job id %s", id_args[i]);
				exit (1);
			}
			opt.array_id[buf_offset] = NO_VAL;
		} else {
			opt.array_id[buf_offset] = NO_VAL;
		}


		if (next_str[0] == '.') {
			tmp_l = strtol(&next_str[1], &next_str, 10);
			if (tmp_l < 0) {
				error ("Invalid job id %s", id_args[i]);
				exit (1);
			}
			opt.step_id[buf_offset] = tmp_l;
		} else
			opt.step_id[buf_offset] = SLURM_BATCH_SCRIPT;
		buf_offset++;

		if ((next_str[0] == ',') && (next_str[0] != '\0')) {
			/* Shift args if job IDs are comma separated.
			 * Commas may be embedded in the task IDs, so we can't
			 * simply split the string on commas. */
			if ((i + 4) >= id_args_size) {
				id_args_size *= 2;
				id_args = xrealloc(id_args, sizeof(char *) *
						   id_args_size);
			}
			for (j = id_args_size - 1; j > (i + 1); j--)
				id_args[j] = id_args[j-1];
			next_str[0] = '\0';
			id_args[i+1] = xstrdup(next_str + 1);
		} else if (next_str[0] != '\0') {
			error ("Invalid job id %s", id_args[i]);
			exit (1);
		}
	}
	opt.job_cnt = buf_offset;
	return id_args;
}


/*
 * opt_verify : perform some post option processing verification
 *
 */
static bool
_opt_verify(void)
{
	bool verified = true;

	if (opt.user_name) {	/* translate to user_id */
		if ( uid_from_string( opt.user_name, &opt.user_id ) != 0 ) {
			error("Invalid user name: %s", opt.user_name);
			return false;
		}
	}

	if ((opt.account == 0) &&
	    (opt.job_cnt == 0) &&
	    (opt.job_name == NULL) &&
	    (opt.nodelist == NULL) &&
	    (opt.partition == NULL) &&
	    (opt.qos == NULL) &&
	    (opt.reservation == NULL) &&
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
	info("cron           : %s", tf_(opt.cron));
	info("full           : %s", tf_(opt.full));
	info("hurry          : %s", tf_(opt.hurry));
	info("interactive    : %s", tf_(opt.interactive));
	info("job_name       : %s", opt.job_name);
	info("nodelist       : %s", opt.nodelist);
	info("partition      : %s", opt.partition);
	info("qos            : %s", opt.qos);
	info("reservation    : %s", opt.reservation);
	info("sibling        : %s", opt.sibling);
	if (opt.signal != NO_VAL16)
		info("signal         : %u", opt.signal);
	info("state          : %s", job_state_string(opt.state));
	info("user_id        : %u", opt.user_id);
	info("user_name      : %s", opt.user_name);
	info("verbose        : %d", opt.verbose);
	info("wckey          : %s", opt.wckey);

	for (i = 0; i < opt.job_cnt; i++) {
		if (opt.step_id[i] == SLURM_BATCH_SCRIPT) {
			if (opt.array_id[i] == NO_VAL) {
				info("job_id[%d]      : %u",
				     i, opt.job_id[i]);
			} else if (opt.array_id[i] == INFINITE) {
				info("job_id[%d]      : %u_*",
				     i, opt.job_id[i]);
			} else {
				info("job_id[%d]      : %u_%u",
				     i, opt.job_id[i], opt.array_id[i]);
			}
		} else {
			char tmp_char[23];
			slurm_step_id_t tmp_step_id = {
				.job_id = opt.job_id[i],
				.step_het_comp = NO_VAL,
				.step_id = opt.step_id[i],
			};
			log_build_step_id_str(&tmp_step_id, tmp_char,
					      sizeof(tmp_char),
					      (STEP_ID_FLAG_NO_PREFIX |
					       STEP_ID_FLAG_NO_JOB));
			if (opt.array_id[i] == NO_VAL) {
				info("job_step_id[%d] : %u.%s",
				     i, opt.job_id[i], tmp_char);
			} else if (opt.array_id[i] == INFINITE) {
				info("job_step_id[%d] : %u_*.%s",
				     i, opt.job_id[i], tmp_char);
			} else {
				info("job_step_id[%d] : %u_%u.%s",
				     i, opt.job_id[i], opt.array_id[i],
				     tmp_char);
			}
		}
	}
}

static void _usage(void)
{
	printf("Usage: scancel [-A account] [--batch] [--full] [--interactive] [-n job_name]\n");
	printf("               [-p partition] [-Q] [-q qos] [-R reservation] [-s signal | integer]\n");
	printf("               [-t PENDING | RUNNING | SUSPENDED] [--usage] [-u user_name]\n");
	printf("               [--hurry] [-V] [-v] [-w hosts...] [--wckey=wckey]\n");
	printf("               [job_id[_array_id][.step_id]]\n");
}

static void _help(void)
{
	printf("Usage: scancel [OPTIONS] [job_id[_array_id][.step_id]]\n");
	printf("  -A, --account=account           act only on jobs charging this account\n");
	printf("  -b, --batch                     signal batch shell for specified job\n");
/*	printf("      --ctld                      send request directly to slurmctld\n"); */
	printf("  -c, --cron                      cancel an scrontab job\n");
	printf("  -f, --full                      signal batch shell and all steps for specified job\n");
	printf("  -H, --hurry                     avoid burst buffer stage out\n");
	printf("  -i, --interactive               require response from user for each job\n");
	printf("  -M, --clusters                  clusters to issue commands to.\n");
	printf("                                  NOTE: SlurmDBD must be up.\n");
	printf("  -n, --name=job_name             act only on jobs with this name\n");
	printf("  -p, --partition=partition       act only on jobs in this partition\n");
	printf("  -Q, --quiet                     disable warnings\n");
	printf("  -q, --qos=qos                   act only on jobs with this quality of service\n");
	printf("  -R, --reservation=reservation   act only on jobs with this reservation\n");
	printf("      --sibling=cluster_name      remove an active sibling job from a federated job\n");
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
