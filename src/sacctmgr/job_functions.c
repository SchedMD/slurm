/*****************************************************************************\
 *  job_functions.c - functions dealing with jobs in the accounting system.
 *****************************************************************************
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/sacctmgr/sacctmgr.h"

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_job_cond_t *job_cond)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;

	if (!job_cond) {
		error("No job_cond given");
		return -1;
	}

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
/* 				option = (int)argv[i][end-1]; */
				end++;
			}
		}

		if (!xstrncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "where",
						 MAX(command_len, 5))) {
			continue;
		} else if (!xstrncasecmp(argv[i], "Accounts",
					 MAX(command_len, 1)) ||
			   !xstrncasecmp(argv[i], "Accts",
					 MAX(command_len, 1))) {
			if (!job_cond->acct_list)
				job_cond->acct_list = list_create(xfree_ptr);
			slurm_addto_char_list(job_cond->acct_list, argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Cluster",
					 MAX(command_len, 1))) {
			if (!job_cond->cluster_list)
				job_cond->cluster_list = list_create(xfree_ptr);
			slurm_addto_char_list(job_cond->cluster_list,
					      argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "JobID",
					 MAX(command_len, 1))) {
			if (!job_cond->step_list)
				job_cond->step_list = list_create(
					slurm_destroy_selected_step);
			slurm_addto_step_list(job_cond->step_list, argv[i]+end);
			if (!list_count(job_cond->step_list))
				FREE_NULL_LIST(job_cond->step_list);
			if (!list_count(job_cond->step_list)) {
				fprintf(stderr, "Invalid job id(s) %s specified\n",
					argv[i]+end);
				exit_code = 1;
			} else
				set = 1;
		} else if (!xstrncasecmp(argv[i], "EndTime",
					 MAX(command_len, 1))) {
			job_cond->usage_end = parse_time(argv[i]+end, 1);
			if (errno == ESLURM_INVALID_TIME_VALUE)
				exit(1);
		} else if (!xstrncasecmp(argv[i], "StartTime",
					 MAX(command_len, 1))) {
			job_cond->usage_start = parse_time(argv[i]+end, 1);
			if (errno == ESLURM_INVALID_TIME_VALUE)
				exit(1);
		} else if (!xstrncasecmp(argv[i], "WCkeys",
					 MAX(command_len, 1))) {
			if (!job_cond->wckey_list)
				job_cond->wckey_list = list_create(xfree_ptr);
			slurm_addto_char_list(job_cond->wckey_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Users",
					 MAX(command_len, 1))) {
			if (!job_cond->userid_list)
				job_cond->userid_list = list_create(xfree_ptr);
			if (slurm_addto_id_char_list(job_cond->userid_list,
						     argv[i]+end, 0) < 1)
				exit(1);
			set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
		}
	}

	if (!job_cond->cluster_list) {
		job_cond->cluster_list = list_create(xfree_ptr);
		list_push(job_cond->cluster_list,
			  xstrdup(slurm_conf.cluster_name));
	}

	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char **argv,
		    slurmdb_job_rec_t *job)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!xstrncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "set",
						 MAX(command_len, 3))) {
			continue;
		} else if (!end) {
			exit_code=1;
			fprintf(stderr,
				" Bad format on %s: End your option with "
				"an '=' sign\n", argv[i]);
		} else if ((!xstrncasecmp(argv[i], "DerivedExitCode",
					  MAX(command_len, 12))) ||
			   (!xstrncasecmp(argv[i], "DerivedEC",
					  MAX(command_len, 9)))) {
			if (get_uint(argv[i]+end, &job->derived_ec,
				     "DerivedExitCode") == SLURM_SUCCESS) {
				set = 1;
			}
		} else if ((!xstrncasecmp(argv[i], "Comment",
					  MAX(command_len, 7))) ||
			   (!xstrncasecmp(argv[i], "DerivedExitString",
					  MAX(command_len, 12))) ||
			   (!xstrncasecmp(argv[i], "DerivedES",
					  MAX(command_len, 9)))) {
			xfree(job->derived_es);
			job->derived_es =
				strip_quotes(argv[i] + end, NULL, false);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Extra",
					 MAX(command_len, 5))) {
			xfree(job->extra);
			job->extra = strip_quotes(argv[i] + end, NULL, false);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "AdminComment",
					 MAX(command_len, 12))) {
			xfree(job->admin_comment);
			job->admin_comment =
				strip_quotes(argv[i] + end, NULL, false);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "SystemComment",
					 MAX(command_len, 13))) {
			xfree(job->system_comment);
			job->system_comment =
				strip_quotes(argv[i] + end, NULL, false);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "NewWCKey",
					 MAX(command_len, 1))) {
			xfree(job->wckey);
			job->wckey = strip_quotes(argv[i]+end, NULL, 1);
			set = 1;
		} else {
			printf(" Unknown option: %s\n"
			       " Use keyword 'where' to modify condition\n",
			       argv[i]);
		}
	}

	(*start) = i;

	return set;
}

extern int sacctmgr_modify_job(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_job_cond_t *job_cond = xmalloc(sizeof(slurmdb_job_cond_t));
	slurmdb_job_rec_t *job = slurmdb_create_job_rec();
	int i=0;
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	job_cond->db_flags = SLURMDB_JOB_FLAG_NOTSET;
	job_cond->flags = JOBCOND_FLAG_NO_DEFAULT_USAGE;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			cond_set += _set_cond(&i, argc, argv, job_cond);

		} else if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			rec_set += _set_rec(&i, argc, argv, job);
		} else {
			cond_set += _set_cond(&i, argc, argv, job_cond);
		}
	}

	if (exit_code) {
		slurmdb_destroy_job_cond(job_cond);
		slurmdb_destroy_job_rec(job);
		return SLURM_ERROR;
	} else if (!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		slurmdb_destroy_job_cond(job_cond);
		slurmdb_destroy_job_rec(job);
		return SLURM_ERROR;
	} else if (!cond_set) {
		if (!commit_check("You didn't set any conditions with 'WHERE'."
				  "\nAre you sure you want to continue?")) {
			printf("Aborted\n");
			slurmdb_destroy_job_cond(job_cond);
			slurmdb_destroy_job_rec(job);
			return SLURM_SUCCESS;
		}
	}

	notice_thread_init();

	ret_list = slurmdb_job_modify(db_conn, job_cond, job);
	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified jobs...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		set = 1;
	} else if (ret_list) {
		printf(" Nothing modified\n");
		rc = SLURM_ERROR;
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(ret_list);

	notice_thread_fini();

	if (set) {
		if (commit_check("Would you like to commit changes?"))
			slurmdb_connection_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	}

	slurmdb_destroy_job_cond(job_cond);
	slurmdb_destroy_job_rec(job);

	return rc;
}
