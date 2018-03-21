/*****************************************************************************\
 *  options.c - option functions for sacct
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2017 SchedMD LLC
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "sacct.h"
#include <time.h>

/* getopt_long options, integers but not characters */
#define OPT_LONG_DELIMITER 0x100
#define OPT_LONG_LOCAL     0x101
#define OPT_LONG_NAME      0x102
#define OPT_LONG_NOCONVERT 0x103
#define OPT_LONG_UNITS     0x104
#define OPT_LONG_FEDR      0x105

#define JOB_HASH_SIZE 1000

static void _help_fields_msg(void);
static void _help_msg(void);
static void _init_params(void);
static void _usage(void);

List selected_parts = NULL;
List selected_steps = NULL;
void *acct_db_conn = NULL;

List print_fields_list = NULL;
ListIterator print_fields_itr = NULL;
int field_count = 0;
List g_qos_list = NULL;
List g_tres_list = NULL;

static List _build_cluster_list(slurmdb_federation_rec_t *fed)
{
	slurmdb_cluster_rec_t *cluster;
	ListIterator iter;
	List cluster_list;

	cluster_list = list_create(slurm_destroy_char);
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *) list_next(iter)))
		(void) slurm_addto_char_list(cluster_list, cluster->name);
	list_iterator_destroy(iter);

	return cluster_list;
}

static void _help_fields_msg(void)
{
	int i;

	for (i = 0; fields[i].name; i++) {
		if (i & 3)
			printf(" ");
		else if (i)
			printf("\n");
		printf("%-17s", fields[i].name);
	}
	printf("\n");
	return;
}

static char *_convert_to_id(char *name, bool gid)
{
	if (gid) {
		gid_t gid;
		if ( gid_from_string( name, &gid ) != 0 ) {
			fprintf(stderr, "Invalid group id: %s\n", name);
			exit(1);
		}
		xfree(name);
		name = xstrdup_printf( "%d", (int) gid );
	} else {
		uid_t uid;
		if ( uid_from_string( name, &uid ) != 0 ) {
			fprintf(stderr, "Invalid user id: %s\n", name);
			exit(1);
		}
		xfree(name);
		name = xstrdup_printf( "%d", (int) uid );
	}
	return name;
}

/* returns number of objects added to list */
static int _addto_id_char_list(List char_list, char *names, bool gid)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if (names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while (names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if (quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if (names[i] == ',') {
				if ((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					//info("got %s %d", name, i-start);
					name = _convert_to_id( name, gid );

					while ((tmp_char = list_next(itr))) {
						if (!xstrcasecmp(tmp_char,
								 name))
							break;
					}

					if (!tmp_char) {
						list_append(char_list, name);
						count++;
					} else
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
				if (!names[i]) {
					info("There is a problem with "
					     "your request.  It appears you "
					     "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if ((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			name = _convert_to_id(name, gid);

			while ((tmp_char = list_next(itr))) {
				if (!xstrcasecmp(tmp_char, name))
					break;
			}

			if (!tmp_char) {
				list_append(char_list, name);
				count++;
			} else
				xfree(name);
		}
	}
	list_iterator_destroy(itr);
	return count;
}

/* returns number of objects added to list */
static int _addto_state_char_list(List char_list, char *names)
{
	int i = 0, start = 0;
	uint32_t c;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if (names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while (names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if (quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if (names[i] == ',') {
				if ((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					c = job_state_num(name);
					if (c == NO_VAL)
						fatal("unrecognized job state value %s", name);
					xfree(name);
					name = xstrdup_printf("%d", c);

					while ((tmp_char = list_next(itr))) {
						if (!xstrcasecmp(tmp_char,
								 name))
							break;
					}

					if (!tmp_char) {
						list_append(char_list, name);
						count++;
					} else
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
				if (!names[i]) {
					info("There is a problem with "
					     "your request.  It appears you "
					     "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if ((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			c = job_state_num(name);
			if (c == NO_VAL)
				fatal("unrecognized job state value '%s'",
				      name);
			xfree(name);
			name = xstrdup_printf("%d", c);

			while ((tmp_char = list_next(itr))) {
				if (!xstrcasecmp(tmp_char, name))
					break;
			}

			if (!tmp_char) {
				list_append(char_list, name);
				count++;
			} else
				xfree(name);
		}
	}
	list_iterator_destroy(itr);
	return count;
}

static void _help_msg(void)
{
    printf("\
sacct [<OPTION>]                                                            \n \
    Valid <OPTION> values are:                                              \n\
     -a, --allusers:                                                        \n\
	           Display jobs for all users. By default, only the         \n\
                   current user's jobs are displayed.  If ran by user root  \n\
                   this is the default.                                     \n\
     -A, --accounts:                                                        \n\
	           Use this comma separated list of accounts to select jobs \n\
                   to display.  By default, all accounts are selected.      \n\
     -b, --brief:                                                           \n\
	           Equivalent to '--format=jobstep,state,error'.            \n\
     -c, --completion: Use job completion instead of accounting data.       \n\
         --delimiter:                                                       \n\
	           ASCII characters used to separate the fields when        \n\
	           specifying the  -p  or  -P options. The default delimiter\n\
	           is a '|'. This options is ignored if -p or -P options    \n\
	           are not specified.                                       \n\
     -D, --duplicates:                                                      \n\
	           If SLURM job ids are reset, some job numbers may         \n\
	           appear more than once referring to different jobs.       \n\
	           Without this option only the most recent jobs will be    \n\
                   displayed.                                               \n\
     -e, --helpformat:                                                      \n\
	           Print a list of fields that can be specified with the    \n\
	           '--format' option                                        \n\
     -E, --endtime:                                                         \n\
                   Select jobs eligible before this time.  If states are    \n\
                   given with the -s option return jobs in this state before\n\
                   this period.                                             \n\
         --federation: Report jobs from federation if a member of a one.    \n\
     -f, --file=file:                                                       \n\
	           Read data from the specified file, rather than SLURM's   \n\
                   current accounting log file. (Only appliciable when      \n\
                   running the filetxt plugin.)                             \n\
     -g, --gid, --group:                                                    \n\
	           Use this comma separated list of gids or group names     \n\
                   to select jobs to display.  By default, all groups are   \n\
                   selected.                                                \n\
     -h, --help:   Print this description of use.                           \n\
     -i, --nnodes=N:                                                        \n\
                   Return jobs which ran on this many nodes (N = min[-max]) \n\
     -I, --ncpus=N:                                                         \n\
                   Return jobs which ran on this many cpus (N = min[-max])  \n\
     -j, --jobs:                                                            \n\
	           Format is <job(.step)>. Display information about this   \n\
                   job or comma-separated list of jobs. The default is all  \n\
                   jobs. Adding .step will display the specific job step of \n\
                   that job. (A step id of 'batch' will display the         \n\
                   information about the batch step.)                       \n\
     -k, --timelimit-min:                                                   \n\
                   Only send data about jobs with this timelimit.           \n\
                   If used with timelimit_max this will be the minimum      \n\
                   timelimit of the range.  Default is no restriction.      \n\
     -K, --timelimit-max:                                                   \n\
                   Ignored by itself, but if timelimit_min is set this will \n\
                   be the maximum timelimit of the range.  Default is no    \n\
                   restriction.                                             \n\
         --local   Report information only about jobs on the local cluster. \n\
	           Overrides --federation.                                  \n\
     -l, --long:                                                            \n\
	           Equivalent to specifying                                 \n\
	           '--format=jobid,jobname,partition,maxvmsize,maxvmsizenode,\n\
                             maxvmsizetask,avevmsize,maxrss,maxrssnode,     \n\
                             maxrsstask,averss,maxpages,maxpagesnode,       \n\
                             maxpagestask,avepages,mincpu,mincpunode,       \n\
                             mincputask,avecpu,ntasks,alloccpus,elapsed,    \n\
                             state,exitcode,avecpufreq,reqcpufreqmin,       \n\
                             reqcpufreqmax,reqcpufreqgov,consumedenergy,    \n\
                             maxdiskread,maxdiskreadnode,maxdiskreadtask,   \n\
                             avediskread,maxdiskwrite,maxdiskwritenode,     \n\
                             maxdiskwritetask,avediskread,allocgres,reqgres \n\
     -L, --allclusters:                                                     \n\
	           Display jobs ran on all clusters. By default, only jobs  \n\
                   ran on the cluster from where sacct is called are        \n\
                   displayed.                                               \n\
     -M, --clusters:                                                        \n\
                   Only send data about these clusters. Use \"all\" for all \n\
                   clusters.\n\
     -n, --noheader:                                                        \n\
	           No header will be added to the beginning of output.      \n\
                   The default is to print a header.                        \n\
     --noconvert:                                                           \n\
		   Don't convert units from their original type             \n\
		   (e.g. 2048M won't be converted to 2G).                   \n\
     -N, --nodelist:                                                        \n\
                   Display jobs that ran on any of these nodes,             \n\
                   can be one or more using a ranged string.                \n\
     --name:                                                                \n\
                   Display jobs that have any of these name(s).             \n\
     -o, --format:                                                          \n\
	           Comma separated list of fields. (use \"--helpformat\"    \n\
                   for a list of available fields).                         \n\
     -p, --parsable: output will be '|' delimited with a '|' at the end     \n\
     -P, --parsable2: output will be '|' delimited without a '|' at the end \n\
     -q, --qos:                                                             \n\
                   Only send data about jobs using these qos.  Default is all.\n\
     -r, --partition:                                                       \n\
	           Comma separated list of partitions to select jobs and    \n\
                   job steps from. The default is all partitions.           \n\
     -s, --state:                                                           \n\
	           Select jobs based on their current state or the state    \n\
                   they were in during the time period given: running (r),  \n\
                   completed (cd), failed (f), timeout (to), resizing (rs), \n\
                   deadline (dl) and node_fail (nf).                        \n\
     -S, --starttime:                                                       \n\
                   Select jobs eligible after this time.  Default is        \n\
                   00:00:00 of the current day, unless '-s' is set then     \n\
                   the default is 'now'.                                    \n\
     -T, --truncate:                                                        \n\
                   Truncate time.  So if a job started before --starttime   \n\
                   the start time would be truncated to --starttime.        \n\
                   The same for end time and --endtime.                     \n\
     -u, --uid, --user:                                                     \n\
	           Use this comma separated list of uids or user names      \n\
                   to select jobs to display.  By default, the running      \n\
                   user's uid is used.                                      \n\
     --units=[KMGTP]:                                                       \n\
                   Display values in specified unit type. Takes precedence  \n\
		   over --noconvert option.                                 \n\
     --usage:      Display brief usage message.                             \n\
     -v, --verbose:                                                         \n\
	           Primarily for debugging purposes, report the state of    \n\
                   various variables during processing.                     \n\
     -V, --version: Print version.                                          \n\
     -W, --wckeys:                                                          \n\
                   Only send data about these wckeys.  Default is all.      \n\
     -x, --associations:                                                    \n\
                   Only send data about these association id.  Default is all.\n\
     -X, --allocations:                                                     \n\
	           Only show statistics relevant to the job allocation      \n\
	           itself, not taking steps into consideration.             \n\
	                                                                    \n\
     Note, valid start/end time formats are...                              \n\
	           HH:MM[:SS] [AM|PM]                                       \n\
	           MMDD[YY] or MM/DD[/YY] or MM.DD[.YY]                     \n\
	           MM/DD[/YY]-HH:MM[:SS]                                    \n\
	           YYYY-MM-DD[THH:MM[:SS]]                                  \n\
\n");

	return;
}

static void _usage(void)
{
	printf("Usage: sacct [options]\n\tUse --help for help\n");
}

static void _init_params(void)
{
	memset(&params, 0, sizeof(sacct_parameters_t));
	params.job_cond = xmalloc(sizeof(slurmdb_job_cond_t));
	params.job_cond->without_usage_truncation = 1;
	params.convert_flags = CONVERT_NUM_UNIT_EXACT;
	params.units = NO_VAL;
}

static int _sort_desc_submit_time(void *x, void *y)
{
	slurmdb_job_rec_t *j1 = *(slurmdb_job_rec_t **)x;
	slurmdb_job_rec_t *j2 = *(slurmdb_job_rec_t **)y;

	if (j1->submit < j2->submit)
		return -1;
	else if (j1->submit > j2->submit)
		return 1;

	return 0;
}

static int _sort_asc_submit_time(void *x, void *y)
{
	slurmdb_job_rec_t *j1 = *(slurmdb_job_rec_t **)x;
	slurmdb_job_rec_t *j2 = *(slurmdb_job_rec_t **)y;

	if (j1->submit < j2->submit)
		return 1;
	else if (j1->submit > j2->submit)
		return -1;

	return 0;
}

static void _remove_duplicate_fed_jobs(List jobs)
{
	int i, j;
	uint32_t hash_inx;
	bool found = false;
	uint32_t *hash_tbl_size = NULL;
	slurmdb_job_rec_t ***hash_job = NULL;
	slurmdb_job_rec_t *job = NULL;
	ListIterator itr = NULL;

	xassert(jobs);

	hash_tbl_size = xmalloc(sizeof(uint32_t) * JOB_HASH_SIZE);
	hash_job = xmalloc(sizeof(slurmdb_job_rec_t **) * JOB_HASH_SIZE);

	for (i = 0; i < JOB_HASH_SIZE; i++) {
		hash_tbl_size[i] = 100;
		hash_job[i] = xmalloc(sizeof(slurmdb_job_rec_t *) *
				      hash_tbl_size[i]);
	}

	/* Put newest jobs at the front so that the later jobs can be removed
	 * easily */
	list_sort(jobs, _sort_asc_submit_time);

	itr = list_iterator_create(jobs);
	while ((job = list_next(itr))) {
		found = false;

		hash_inx = job->jobid % JOB_HASH_SIZE;
		for (j = 0; (j < hash_tbl_size[hash_inx] &&
			     hash_job[hash_inx][j]); j++) {
			if (job->jobid == hash_job[hash_inx][j]->jobid) {
				found = true;
				break;
			}
		}
		if (found) {
			/* Show sibling jobs that are related. e.g. when a
			 * pending sibling job is cancelled all siblings have
			 * the state as cancelled. Since jobids won't roll in a
			 * day -- unless the system is amazing -- just remove
			 * jobs that are older than a day. */
			if (hash_job[hash_inx][j]->submit > (job->submit +
							     86400))
				list_delete_item(itr);
		} else {
			if (j >= hash_tbl_size[hash_inx]) {
				hash_tbl_size[hash_inx] *= 2;
				xrealloc(hash_job[hash_inx],
					 sizeof(slurmdb_job_rec_t *) *
					 hash_tbl_size[hash_inx]);
			}
			hash_job[hash_inx][j] = job;
		}
	}
	list_iterator_destroy(itr);

	/* Put jobs back in desc order */
	list_sort(jobs, _sort_desc_submit_time);

	for (i = 0; i < JOB_HASH_SIZE; i++)
		xfree(hash_job[i]);
	xfree(hash_tbl_size);
	xfree(hash_job);
}

extern int get_data(void)
{
	slurmdb_job_rec_t *job = NULL;
	slurmdb_step_rec_t *step = NULL;
	ListIterator itr = NULL;
	ListIterator itr_step = NULL;
	slurmdb_job_cond_t *job_cond = params.job_cond;

	if (params.opt_completion) {
		jobs = slurmdb_jobcomp_jobs_get(job_cond);
		return SLURM_SUCCESS;
	} else {
		jobs = slurmdb_jobs_get(acct_db_conn, job_cond);
	}

	if (!jobs)
		return SLURM_ERROR;

	/* Remove duplicate federated jobs. The db will remove duplicates for
	 * one cluster but not when jobs for multiple clusters are requested.
	 * Remove the current job if there were jobs with the same id submitted
	 * in the future. */
	if (params.cluster_name && !params.opt_dup)
	    _remove_duplicate_fed_jobs(jobs);

	itr = list_iterator_create(jobs);
	while ((job = list_next(itr))) {

		if (job->user) {
			struct	passwd *pw = NULL;
			if ((pw=getpwnam(job->user)))
				job->uid = pw->pw_uid;
		}

		if (!job->steps || !list_count(job->steps))
			continue;

		itr_step = list_iterator_create(job->steps);
		while ((step = list_next(itr_step)) != NULL) {
			/* now aggregate the aggregatable */

			if (step->state < JOB_COMPLETE)
				continue;
			job->tot_cpu_sec += step->tot_cpu_sec;
			job->tot_cpu_usec += step->tot_cpu_usec;
			job->user_cpu_sec +=
				step->user_cpu_sec;
			job->user_cpu_usec +=
				step->user_cpu_usec;
			job->sys_cpu_sec +=
				step->sys_cpu_sec;
			job->sys_cpu_usec +=
				step->sys_cpu_usec;

			/* get the max for all the sacct_t struct */
			aggregate_stats(&job->stats, &step->stats);
		}
		list_iterator_destroy(itr_step);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern void parse_command_line(int argc, char **argv)
{
	extern int optind;
	int c, i, optionIndex = 0;
	char *end = NULL, *start = NULL, *acct_type = NULL;
	slurmdb_selected_step_t *selected_step = NULL;
	ListIterator itr = NULL;
	struct stat stat_buf;
	char *dot = NULL;
	char *env_val = NULL;
	bool brief_output = false, long_output = false;
	bool all_users = false;
	bool all_clusters = false;
	slurmdb_job_cond_t *job_cond = params.job_cond;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	int verbosity;		/* count of -v options */
	bool set;

	static struct option long_options[] = {
                {"allusers",       no_argument,       0,    'a'},
                {"accounts",       required_argument, 0,    'A'},
                {"allocations",    no_argument,       0,    'X'},
                {"brief",          no_argument,       0,    'b'},
                {"completion",     no_argument,       0,    'c'},
                {"delimiter",      required_argument, 0,    OPT_LONG_DELIMITER},
                {"duplicates",     no_argument,       0,    'D'},
                {"federation",     no_argument,       0,    OPT_LONG_FEDR},
                {"helpformat",     no_argument,       0,    'e'},
                {"help-fields",    no_argument,       0,    'e'},
                {"endtime",        required_argument, 0,    'E'},
                {"file",           required_argument, 0,    'f'},
                {"gid",            required_argument, 0,    'g'},
                {"group",          required_argument, 0,    'g'},
                {"help",           no_argument,       0,    'h'},
                {"local",          no_argument,       0,    OPT_LONG_LOCAL},
                {"name",           required_argument, 0,    OPT_LONG_NAME},
                {"nnodes",         required_argument, 0,    'i'},
                {"ncpus",          required_argument, 0,    'I'},
                {"jobs",           required_argument, 0,    'j'},
                {"timelimit-min",  required_argument, 0,    'k'},
                {"timelimit-max",  required_argument, 0,    'K'},
                {"long",           no_argument,       0,    'l'},
                {"allclusters",    no_argument,       0,    'L'},
                {"cluster",        required_argument, 0,    'M'},
                {"clusters",       required_argument, 0,    'M'},
                {"nodelist",       required_argument, 0,    'N'},
                {"noconvert",      no_argument,       0,    OPT_LONG_NOCONVERT},
                {"units",          required_argument, 0,    OPT_LONG_UNITS},
                {"noheader",       no_argument,       0,    'n'},
                {"fields",         required_argument, 0,    'o'},
                {"format",         required_argument, 0,    'o'},
                {"parsable",       no_argument,       0,    'p'},
                {"parsable2",      no_argument,       0,    'P'},
                {"qos",            required_argument, 0,    'q'},
                {"partition",      required_argument, 0,    'r'},
                {"state",          required_argument, 0,    's'},
                {"starttime",      required_argument, 0,    'S'},
                {"truncate",       no_argument,       0,    'T'},
                {"uid",            required_argument, 0,    'u'},
                {"usage",          no_argument,       0,    'U'},
                {"user",           required_argument, 0,    'u'},
                {"verbose",        no_argument,       0,    'v'},
                {"version",        no_argument,       0,    'V'},
                {"wckeys",         required_argument, 0,    'W'},
                {"associations",   required_argument, 0,    'x'},
                {0,                0,		      0,    0}};

	params.opt_uid = getuid();
	params.opt_gid = getgid();

	verbosity         = 0;
	log_init("sacct", opts, SYSLOG_FACILITY_DAEMON, NULL);
	opterr = 1;		/* Let getopt report problems to the user */

	if (slurmctld_conf.fed_params &&
	    strstr(slurmctld_conf.fed_params, "fed_display"))
		params.opt_federation = true;

	if (getenv("SACCT_FEDERATION"))
		params.opt_federation = true;
	if (getenv("SACCT_LOCAL"))
		params.opt_local = true;

	while (1) {		/* now cycle through the command line */
		c = getopt_long(argc, argv,
				"aA:bcC:DeE:f:g:hi:I:j:k:K:lLM:nN:o:pPq:r:s:S:Ttu:UvVW:x:X",
				long_options, &optionIndex);
		if (c == -1)
			break;
		switch (c) {
		case 'a':
			all_users = true;
			break;
		case 'A':
			if (!job_cond->acct_list)
				job_cond->acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->acct_list, optarg);
			break;
		case 'b':
			brief_output = true;
			break;
		case 'c':
			params.opt_completion = 1;
			break;
		case OPT_LONG_DELIMITER:
			fields_delimiter = optarg;
			break;
		case 'C':
			/* 'C' is deprecated since 'M' is cluster on
			   everything else.
			*/
		case 'M':
			if (!xstrcasecmp(optarg, "all") ||
			    !xstrcasecmp(optarg, "-1")) {	/* vestigial */
				all_clusters = true;
				break;
			}
			all_clusters = false;
			params.opt_local = true;
			if (!job_cond->cluster_list)
				job_cond->cluster_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->cluster_list, optarg);
			break;
		case 'D':
			params.opt_dup = 1;
			break;
		case 'e':
			params.opt_help = 2;
			break;
		case 'E':
			job_cond->usage_end = parse_time(optarg, 1);
			if (errno == ESLURM_INVALID_TIME_VALUE)
				exit(1);
			break;
		case 'f':
			xfree(params.opt_filein);
			params.opt_filein = xstrdup(optarg);
			break;
		case 'g':
			if (!job_cond->groupid_list)
				job_cond->groupid_list =
					list_create(slurm_destroy_char);
			_addto_id_char_list(job_cond->groupid_list, optarg, 1);
			break;
		case 'h':
			params.opt_help = 1;
			break;
		case 'i':
			set = get_resource_arg_range(
				optarg,
				"requested node range",
				(int *)&job_cond->nodes_min,
				(int *)&job_cond->nodes_max,
				true);

			if (set == false) {
				error("invalid node range -i '%s'",
				      optarg);
				exit(1);
			}
			break;
		case 'I':
			set = get_resource_arg_range(
				optarg,
				"requested cpu range",
				(int *)&job_cond->cpus_min,
				(int *)&job_cond->cpus_max,
				true);

			if (set == false) {
				error("invalid cpu range -i '%s'",
				      optarg);
				exit(1);
			}
			break;
		case 'j':
			if (!job_cond->step_list)
				job_cond->step_list = list_create(
					slurmdb_destroy_selected_step);
			slurm_addto_step_list(job_cond->step_list, optarg);
			if (!list_count(job_cond->step_list))
				FREE_NULL_LIST(job_cond->step_list);
			break;
		case 'k':
			job_cond->timelimit_min = time_str2mins(optarg);
			if (((int32_t)job_cond->timelimit_min <= 0)
			    && (job_cond->timelimit_min != INFINITE))
				fatal("Invalid time limit specification");
			break;
		case 'K':
			job_cond->timelimit_max = time_str2mins(optarg);
			if (((int32_t)job_cond->timelimit_max <= 0)
			    && (job_cond->timelimit_max != INFINITE))
				fatal("Invalid time limit specification");
			break;
		case 'L':
			all_clusters = true;
			break;
		case 'l':
			long_output = true;
			break;
		case OPT_LONG_FEDR:
			params.opt_federation = true;
			all_clusters = false;
			break;
		case OPT_LONG_LOCAL:
			params.opt_local = true;
			all_clusters = false;
			break;
		case OPT_LONG_NOCONVERT:
			params.convert_flags |= CONVERT_NUM_UNIT_NO;
			break;
		case OPT_LONG_UNITS:
		{
			int type = get_unit_type(*optarg);
			if (type == SLURM_ERROR)
				fatal("Invalid unit type");
			params.units = type;
		}
			break;
		case 'n':
			print_fields_have_header = 0;
			break;
		case 'N':
			if (job_cond->used_nodes) {
				error("Aleady asked for nodes '%s'",
				      job_cond->used_nodes);
				break;
			}
			job_cond->used_nodes = xstrdup(optarg);
			break;
		case OPT_LONG_NAME:
			if (!job_cond->jobname_list)
				job_cond->jobname_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->jobname_list, optarg);
			break;
		case 'o':
			xstrfmtcat(params.opt_field_list, "%s,", optarg);
			break;
		case 'p':
			print_fields_parsable_print =
				PRINT_FIELDS_PARSABLE_ENDING;
			break;
		case 'P':
			print_fields_parsable_print =
				PRINT_FIELDS_PARSABLE_NO_ENDING;
			break;
		case 'q':
			if (!g_qos_list) {
				slurmdb_qos_cond_t qos_cond;
				memset(&qos_cond, 0,
				       sizeof(slurmdb_qos_cond_t));
				qos_cond.with_deleted = 1;
				g_qos_list = slurmdb_qos_get(
					acct_db_conn, &qos_cond);
			}

			if (!job_cond->qos_list)
				job_cond->qos_list =
					list_create(slurm_destroy_char);

			if (!slurmdb_addto_qos_char_list(job_cond->qos_list,
							g_qos_list, optarg, 0))
				fatal("problem processing qos list");
			break;
		case 'r':
			if (!job_cond->partition_list)
				job_cond->partition_list =
					list_create(slurm_destroy_char);

			slurm_addto_char_list(job_cond->partition_list,
					      optarg);
			break;
		case 's':
			if (!job_cond->state_list)
				job_cond->state_list =
					list_create(slurm_destroy_char);

			_addto_state_char_list(job_cond->state_list, optarg);
			break;
		case 'S':
			job_cond->usage_start = parse_time(optarg, 1);
			if (errno == ESLURM_INVALID_TIME_VALUE)
				exit(1);
			break;
		case 'T':
			job_cond->without_usage_truncation = 0;
			break;
		case 'U':
			params.opt_help = 3;
			break;
		case 'u':
			if (!xstrcmp(optarg, "-1")) {
				all_users = true;
				break;
			}
			all_users = false;
			if (!job_cond->userid_list)
				job_cond->userid_list =
					list_create(slurm_destroy_char);
			_addto_id_char_list(job_cond->userid_list, optarg, 0);
			break;
		case 'v':
			/* Handle -vvv thusly...
			 */
			verbosity++;
			break;
		case 'W':
			if (!job_cond->wckey_list)
				job_cond->wckey_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->wckey_list, optarg);
			break;
		case 'V':
			print_slurm_version();
			exit(0);
		case 'x':
			if (!job_cond->associd_list)
				job_cond->associd_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->associd_list, optarg);
			break;
		case 't':
			/* 't' is deprecated and was replaced with 'X'.	*/
		case 'X':
			params.opt_allocs = 1;
			break;
		case ':':
		case '?':	/* getopt() has explained it */
			exit(1);
		}
	}

	if (verbosity) {
		opts.stderr_level += verbosity;
		opts.prefix_level = 1;
		log_alter(opts, 0, NULL);
	}


	/* Now set params.opt_dup, unless they've already done so */
	if (params.opt_dup < 0)	/* not already set explicitly */
		params.opt_dup = 0;

	job_cond->duplicates = params.opt_dup;
	job_cond->without_steps = params.opt_allocs;

	if (!job_cond->usage_start && !job_cond->step_list) {
		struct tm start_tm;
		job_cond->usage_start = time(NULL);
		/* If we are looking for job states default to now.
		   If not default to midnight of the current day.
		*/
		if (!job_cond->state_list
		    || !list_count(job_cond->state_list)) {
			if (!slurm_localtime_r(&job_cond->usage_start,
					       &start_tm)) {
				error("Couldn't get localtime from %ld",
				      (long)job_cond->usage_start);
				return;
			}
			start_tm.tm_sec = 0;
			start_tm.tm_min = 0;
			start_tm.tm_hour = 0;
			start_tm.tm_isdst = -1;
			job_cond->usage_start = slurm_mktime(&start_tm);
		}
	}

	if (verbosity > 0) {
		char start_char[25], end_char[25];

		slurm_ctime2_r(&job_cond->usage_start, start_char);
		if (job_cond->usage_end)
			slurm_ctime2_r(&job_cond->usage_end, end_char);
		else
			sprintf(end_char, "Now");
		info("Jobs eligible from %s - %s", start_char, end_char);
	}

	debug("Options selected:\n"
	      "\topt_completion=%d\n"
	      "\topt_dup=%d\n"
	      "\topt_field_list=%s\n"
	      "\topt_help=%d\n"
	      "\topt_allocs=%d",
	      params.opt_completion,
	      params.opt_dup,
	      params.opt_field_list,
	      params.opt_help,
	      params.opt_allocs);

	if (params.opt_completion) {
		slurmdb_jobcomp_init(params.opt_filein);

		acct_type = slurm_get_jobcomp_type();
		if ((xstrcmp(acct_type, "jobcomp/none") == 0)
		    &&  (stat(params.opt_filein, &stat_buf) != 0)) {
			fprintf(stderr, "SLURM job completion is disabled\n");
			exit(1);
		}
		xfree(acct_type);
	} else {
		if (slurm_acct_storage_init(params.opt_filein) !=
		    SLURM_SUCCESS) {
			fprintf(stderr, "SLURM unable to initialize storage plugin\n");
			exit(1);
		}
		acct_type = slurm_get_accounting_storage_type();
		if ((xstrcmp(acct_type, "accounting_storage/none") == 0)
		    &&  (stat(params.opt_filein, &stat_buf) != 0)) {
			fprintf(stderr,
				"SLURM accounting storage is disabled\n");
			exit(1);
		}
		xfree(acct_type);
		acct_db_conn = slurmdb_connection_get();
		if (errno != SLURM_SUCCESS) {
			error("Problem talking to the database: %m");
			exit(1);
		}
	}

	/* specific clusters requested? */
	if (params.opt_federation && !all_clusters && !job_cond->cluster_list &&
	    !params.opt_local) {
		/* Test if in federated cluster and if so, get information from
		 * all clusters in that federation */
		slurmdb_federation_rec_t *fed = NULL;
		slurmdb_federation_cond_t fed_cond;
		List fed_list = NULL;
		List cluster_list = list_create(NULL);

		params.cluster_name = slurm_get_cluster_name();

		list_append(cluster_list, params.cluster_name);
		slurmdb_init_federation_cond(&fed_cond, 0);
		fed_cond.cluster_list = cluster_list;

		if ((fed_list = slurmdb_federations_get(
			     acct_db_conn, &fed_cond)) &&
		    list_count(fed_list) == 1) {
			fed = list_peek(fed_list);
			job_cond->cluster_list = _build_cluster_list(fed);
			/* Leave cluster_name to identify remote only jobs */
			// xfree(params.cluster_name);
		} else
			xfree(params.cluster_name);
		FREE_NULL_LIST(cluster_list);
		FREE_NULL_LIST(fed_list);
	}
	if (all_clusters) {
		if (job_cond->cluster_list
		   && list_count(job_cond->cluster_list)) {
			FREE_NULL_LIST(job_cond->cluster_list);
		}
		debug2("Clusters requested:\tall");
	} else if (job_cond->cluster_list
		   && list_count(job_cond->cluster_list)) {
		debug2( "Clusters requested:");
		itr = list_iterator_create(job_cond->cluster_list);
		while ((start = list_next(itr)))
			debug2("\t: %s", start);
		list_iterator_destroy(itr);
	} else if (!job_cond->cluster_list
		  || !list_count(job_cond->cluster_list)) {
		if (!job_cond->cluster_list)
			job_cond->cluster_list =
				list_create(slurm_destroy_char);
		if ((start = slurm_get_cluster_name())) {
			list_append(job_cond->cluster_list, start);
			debug2("Clusters requested:\t%s", start);
		}
	}

	/* if any jobs or nodes are specified set to look for all users if none
	   are set */
	if (!job_cond->userid_list || !list_count(job_cond->userid_list))
		if ((job_cond->step_list && list_count(job_cond->step_list))
		   || job_cond->used_nodes)
			all_users = true;

	/* set all_users for user root if not requesting any */
	if (!job_cond->userid_list && !params.opt_uid)
		all_users = true;

	if (all_users) {
		if (job_cond->userid_list &&
		    list_count(job_cond->userid_list)) {
			FREE_NULL_LIST(job_cond->userid_list);
		}
		debug2("Userids requested:\tall");
	} else if (job_cond->userid_list && list_count(job_cond->userid_list)) {
		debug2("Userids requested:");
		itr = list_iterator_create(job_cond->userid_list);
		while ((start = list_next(itr)))
			debug2("\t: %s", start);
		list_iterator_destroy(itr);
	} else if (!job_cond->userid_list
		  || !list_count(job_cond->userid_list)) {
		if (!job_cond->userid_list)
			job_cond->userid_list = list_create(slurm_destroy_char);
		start = xstrdup_printf("%u", params.opt_uid);
		list_append(job_cond->userid_list, start);
		debug2("Userid requested\t: %s", start);
	}

	if (job_cond->groupid_list && list_count(job_cond->groupid_list)) {
		debug2("Groupids requested:");
		itr = list_iterator_create(job_cond->groupid_list);
		while ((start = list_next(itr)))
			debug2("\t: %s", start);
		list_iterator_destroy(itr);
	}

	/* specific partitions requested? */
	if (job_cond->partition_list && list_count(job_cond->partition_list)) {
		debug2("Partitions requested:");
		itr = list_iterator_create(job_cond->partition_list);
		while ((start = list_next(itr)))
			debug2("\t: %s", start);
		list_iterator_destroy(itr);
	}

	/* specific qos' requested? */
	if (job_cond->qos_list && list_count(job_cond->qos_list)) {
		start = get_qos_complete_str(g_qos_list, job_cond->qos_list);
		debug2("QOS requested\t: %s\n", start);
		xfree(start);
	}

	/* specific jobs requested? */
	if (job_cond->step_list && list_count(job_cond->step_list)) {
		debug2("Jobs requested:");
		itr = list_iterator_create(job_cond->step_list);
		while ((selected_step = list_next(itr))) {
			char id[FORMAT_STRING_SIZE];

			debug2("\t: %s", slurmdb_get_selected_step_id(
				       id, sizeof(id), selected_step));
		}
		list_iterator_destroy(itr);
	}

	/* specific states (completion state) requested? */
	if (job_cond->state_list && list_count(job_cond->state_list)) {
		debug2("States requested:");
		itr = list_iterator_create(job_cond->state_list);
		while ((start = list_next(itr))) {
			debug2("\t: %s",
				job_state_string(atoi(start)));
		}
		list_iterator_destroy(itr);
	}

	if (job_cond->wckey_list && list_count(job_cond->wckey_list)) {
		debug2("Wckeys requested:");
		itr = list_iterator_create(job_cond->wckey_list);
		while ((start = list_next(itr)))
			debug2("\t: %s\n", start);
		list_iterator_destroy(itr);
	}

	if (job_cond->timelimit_min) {
		char time_str[128], tmp1[32], tmp2[32];
		mins2time_str(job_cond->timelimit_min, tmp1, sizeof(tmp1));
		sprintf(time_str, "%s", tmp1);
		if (job_cond->timelimit_max) {
			int len = strlen(tmp1);
			mins2time_str(job_cond->timelimit_max,
				      tmp2, sizeof(tmp2));
			sprintf(time_str+len, " - %s", tmp2);
		}
		debug2("Timelimit requested\t: %s", time_str);
	}

	/* specific jobnames requested? */
	if (job_cond->jobname_list && list_count(job_cond->jobname_list)) {
		debug2("Jobnames requested:");
		itr = list_iterator_create(job_cond->jobname_list);
		while ((start = list_next(itr))) {
			debug2("\t: %s", start);
		}
		list_iterator_destroy(itr);
	}

	/* select the output fields */
	if (brief_output) {
		if (params.opt_completion)
			dot = BRIEF_COMP_FIELDS;
		else
			dot = BRIEF_FIELDS;

		xstrfmtcat(params.opt_field_list, "%s,", dot);
	}

	if (long_output) {
		if (params.opt_completion)
			dot = LONG_COMP_FIELDS;
		else
			dot = LONG_FIELDS;

		xstrfmtcat(params.opt_field_list, "%s,", dot);
	}

	if (params.opt_field_list == NULL) {
		if (params.opt_completion)
			dot = DEFAULT_COMP_FIELDS;
		else if ( ( env_val = getenv("SACCT_FORMAT") ) )
			dot = xstrdup(env_val);
		else
			dot = DEFAULT_FIELDS;

		xstrfmtcat(params.opt_field_list, "%s,", dot);
	}

	start = params.opt_field_list;
	while ((end = strstr(start, ","))) {
		char *tmp_char = NULL;
		int command_len = 0;
		int newlen = 0;
		bool newlen_set = false;

		*end = 0;
		while (isspace(*start))
			start++;	/* discard whitespace */
		if (!(int)*start)
			continue;

		if ((tmp_char = strstr(start, "\%"))) {
			newlen_set = true;
			newlen = atoi(tmp_char+1);
			tmp_char[0] = '\0';
		}

		command_len = strlen(start);

		if (!xstrncasecmp("ALL", start, command_len)) {
			for (i = 0; fields[i].name; i++) {
				if (newlen_set)
					fields[i].len = newlen;
				list_append(print_fields_list, &fields[i]);
				start = end + 1;
			}
			start = end + 1;
			continue;
		}

		for (i = 0; fields[i].name; i++) {
			if (!xstrncasecmp(fields[i].name, start, command_len))
				goto foundfield;
		}
		error("Invalid field requested: \"%s\"", start);
		exit(1);
	foundfield:
		if (newlen_set)
			fields[i].len = newlen;
		list_append(print_fields_list, &fields[i]);
		start = end + 1;
	}
	field_count = list_count(print_fields_list);

	if (optind < argc) {
		debug2("Error: Unknown arguments:");
		for (i=optind; i<argc; i++)
			debug2(" %s", argv[i]);
		debug2("\n");
		exit(1);
	}
	return;
}

extern void do_help(void)
{
	switch (params.opt_help) {
	case 1:
		_help_msg();
		break;
	case 2:
		_help_fields_msg();
		break;
	case 3:
		_usage();
		break;
	default:
		debug2("sacct bug: params.opt_help=%d",
			params.opt_help);
	}
}

/* Return true if the specified job id is local to a cluster
 * (not a federated job) */
static inline bool _test_local_job(uint32_t job_id)
{
	if ((job_id & (~MAX_JOB_ID)) == 0)
		return true;
	return false;
}

/* do_list() -- List the assembled data
 *
 * In:	Nothing explicit.
 * Out:	void.
 *
 * At this point, we have already selected the desired data,
 * so we just need to print it for the user.
 */
extern void do_list(void)
{
	ListIterator itr = NULL;
	ListIterator itr_step = NULL;
	slurmdb_job_rec_t *job = NULL;
	slurmdb_step_rec_t *step = NULL;

	if (!jobs)
		return;

	itr = list_iterator_create(jobs);
	while ((job = list_next(itr))) {
		if ((params.cluster_name) &&
		    _test_local_job(job->jobid) &&
		    xstrcmp(params.cluster_name, job->cluster))
			continue;


		if (list_count(job->steps)) {
			int cnt = list_count(job->steps);
			job->stats.cpu_ave /= (double)cnt;
			job->stats.rss_ave /= (double)cnt;
			job->stats.vsize_ave /= (double)cnt;
			job->stats.pages_ave /= (double)cnt;
			job->stats.disk_read_ave /= (double)cnt;
			job->stats.disk_write_ave /= (double)cnt;
		}

		if (job->show_full)
			print_fields(JOB, job);

		if (!params.opt_allocs
		    && (job->track_steps || !job->show_full)) {
			itr_step = list_iterator_create(job->steps);
			while ((step = list_next(itr_step))) {
				if (step->end == 0)
					step->end = job->end;
				print_fields(JOBSTEP, step);
			}
			list_iterator_destroy(itr_step);
		}
	}
	list_iterator_destroy(itr);
}

/* do_list_completion() -- List the assembled data
 *
 * In:	Nothing explicit.
 * Out:	void.
 *
 * NOTE: This data is from the job completion data and not federation compliant.
 * At this point, we have already selected the desired data,
 * so we just need to print it for the user.
 */
extern void do_list_completion(void)
{
	ListIterator itr = NULL;
	jobcomp_job_rec_t *job = NULL;

	if (!jobs)
		return;

	itr = list_iterator_create(jobs);
	while ((job = list_next(itr))) {
		print_fields(JOBCOMP, job);
	}
	list_iterator_destroy(itr);
}

extern void sacct_init(void)
{
	_init_params();
	print_fields_list = list_create(NULL);
	print_fields_itr = list_iterator_create(print_fields_list);
}

extern void sacct_fini(void)
{
	if (print_fields_itr)
		list_iterator_destroy(print_fields_itr);
	FREE_NULL_LIST(print_fields_list);
	FREE_NULL_LIST(jobs);
	FREE_NULL_LIST(g_qos_list);
	FREE_NULL_LIST(g_tres_list);

	if (params.opt_completion)
		slurmdb_jobcomp_fini();
	else {
		slurmdb_connection_close(&acct_db_conn);
		slurm_acct_storage_fini();
	}
	xfree(params.opt_field_list);
	xfree(params.opt_filein);
	slurmdb_destroy_job_cond(params.job_cond);
}
