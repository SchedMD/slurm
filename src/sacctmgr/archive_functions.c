/*****************************************************************************\
 *  archive_functions.c - functions dealing with archive in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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

#include <sys/stat.h>

#include "src/sacctmgr/sacctmgr.h"
#include <sys/param.h>		/* MAXPATHLEN */
#include "src/common/proc_args.h"
#include "src/common/util-net.h"

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_archive_cond_t *arch_cond)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;
 	uint32_t tmp;
	slurmdb_job_cond_t *job_cond = NULL;

	if (!arch_cond) {
		error("No arch_cond given");
		return -1;
	}
	if (!arch_cond->job_cond)
		arch_cond->job_cond = xmalloc(sizeof(slurmdb_job_cond_t));
	job_cond = arch_cond->job_cond;

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

		if (!end && !xstrncasecmp(argv[i], "where",
					MAX(command_len, 5))) {
			continue;
		} else if (!end && !xstrncasecmp(argv[i], "events",
					  MAX(command_len, 1))) {
			arch_cond->purge_event |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !xstrncasecmp(argv[i], "jobs",
					  MAX(command_len, 1))) {
			arch_cond->purge_job |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !xstrncasecmp(argv[i], "reservations",
					  MAX(command_len, 1))) {
			arch_cond->purge_resv |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !xstrncasecmp(argv[i], "steps",
					  MAX(command_len, 1))) {
			arch_cond->purge_step |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !xstrncasecmp(argv[i], "suspend",
					  MAX(command_len, 1))) {
			arch_cond->purge_suspend |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !xstrncasecmp(argv[i], "txn",
					  MAX(command_len, 1))) {
			arch_cond->purge_txn |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !xstrncasecmp(argv[i], "usage",
					  MAX(command_len, 1))) {
			arch_cond->purge_usage |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Clusters",
					  MAX(command_len, 1))) {
			if (!job_cond->cluster_list)
				job_cond->cluster_list = list_create(xfree_ptr);
			slurm_addto_char_list(job_cond->cluster_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Accounts",
					 MAX(command_len, 2))) {
			if (!job_cond->acct_list)
				job_cond->acct_list = list_create(xfree_ptr);
			slurm_addto_char_list(job_cond->acct_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Associations",
					 MAX(command_len, 2))) {
			if (!job_cond->associd_list)
				job_cond->associd_list = list_create(xfree_ptr);
			slurm_addto_char_list(job_cond->associd_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Directory",
					 MAX(command_len, 2))) {
			arch_cond->archive_dir =
				strip_quotes(argv[i]+end, NULL, 0);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "End", MAX(command_len, 1))) {
			job_cond->usage_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Gid", MAX(command_len, 2))) {
			if (!job_cond->groupid_list)
				job_cond->groupid_list = list_create(xfree_ptr);
			slurm_addto_char_list(job_cond->groupid_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Jobs",
					 MAX(command_len, 1))) {
			char *end_char = NULL, *start_char = argv[i] + end;
			slurm_selected_step_t *selected_step = NULL;
			char *dot = NULL;
			if (!job_cond->step_list)
				job_cond->step_list = list_create(xfree_ptr);

			while ((end_char = strstr(start_char, ","))) {
				end_char[0] = '\0';
				while (isspace(start_char[0]))
					start_char++;  /* discard whitespace */
				if (start_char[0] == '\0')
					continue;
				selected_step = xmalloc(
					sizeof(slurm_selected_step_t));
				selected_step->array_task_id = NO_VAL;
				selected_step->het_job_offset = NO_VAL;
				list_append(job_cond->step_list, selected_step);

				dot = strstr(start_char, ".");
				if (dot == NULL) {
					debug2("No jobstep requested");
					selected_step->step_id.step_id = NO_VAL;
				} else {
					*dot++ = 0;
					selected_step->step_id.step_id =
						atoi(dot);
				}
				selected_step->step_id.step_het_comp = NO_VAL;
				selected_step->step_id.job_id =
					atoi(start_char);
				start_char = end_char + 1;
			}

			set = 1;
		} else if (!xstrncasecmp(argv[i], "Partitions",
					 MAX(command_len, 2))) {
			if (!job_cond->partition_list)
				job_cond->partition_list =
					list_create(xfree_ptr);
			slurm_addto_char_list(job_cond->partition_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "PurgeEventAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_event |= tmp;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeJobAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_job |= tmp;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeResvAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_resv |= tmp;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeStepAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_step |= tmp;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeSuspendAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_suspend |= tmp;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeTXNAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_txn |= tmp;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeUsageAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_usage |= tmp;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeEventMonths",
					 MAX(command_len, 6))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeEventMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_event |= tmp;
				arch_cond->purge_event |= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeJobMonths",
					 MAX(command_len, 6))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeJobMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_job |= tmp;
				arch_cond->purge_job |= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeResvMonths",
					 MAX(command_len, 6))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeResvMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_resv |= tmp;
				arch_cond->purge_resv |= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeStepMonths",
					 MAX(command_len, 7))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeStepMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_step |= tmp;
				arch_cond->purge_step |= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeSuspendMonths",
					 MAX(command_len, 7))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeSuspendMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_suspend |= tmp;
				arch_cond->purge_suspend
					|= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeTXNMonths",
					 MAX(command_len, 6))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeTXNMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_txn |= tmp;
				arch_cond->purge_txn
					|= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "PurgeUsageMonths",
					 MAX(command_len, 6))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeUsageMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_usage |= tmp;
				arch_cond->purge_usage
					|= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 2))) {
			job_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Script",
					 MAX(command_len, 2))) {
			arch_cond->archive_script =
				strip_quotes(argv[i]+end, NULL, 0);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Users",
					 MAX(command_len, 1))) {
			if (!job_cond->userid_list)
				job_cond->userid_list = list_create(xfree_ptr);
			if (slurm_addto_id_char_list(job_cond->userid_list,
						     argv[i]+end, false) > 0)
				set = 1;
			else
				exit_code = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}

	(*start) = i;

	return set;
}

extern int sacctmgr_archive_dump(int argc, char **argv)
{
	char *warning = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_archive_cond_t *arch_cond =
		xmalloc(sizeof(slurmdb_archive_cond_t));
	int i;
	struct stat st;

	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, arch_cond);
	}

	if (!arch_cond->purge_event)
		arch_cond->purge_event = NO_VAL;
	if (!arch_cond->purge_job)
		arch_cond->purge_job = NO_VAL;
	if (!arch_cond->purge_resv)
		arch_cond->purge_resv = NO_VAL;
	if (!arch_cond->purge_step)
		arch_cond->purge_step = NO_VAL;
	if (!arch_cond->purge_suspend)
		arch_cond->purge_suspend = NO_VAL;
	if (!arch_cond->purge_txn)
		arch_cond->purge_txn = NO_VAL;
	if (!arch_cond->purge_usage)
		arch_cond->purge_usage = NO_VAL;

	if (exit_code) {
		slurmdb_destroy_archive_cond(arch_cond);
		return SLURM_ERROR;
	}

	if (arch_cond->archive_dir) {
		if (stat(arch_cond->archive_dir, &st) < 0) {
			exit_code = errno;
			fprintf(stderr, " dump: Failed to stat %s: %s\n "
				"Note: For archive dump, "
				"the directory must be on "
				"the calling host.\n",
				arch_cond->archive_dir, slurm_strerror(errno));
			slurmdb_destroy_archive_cond(arch_cond);
			return SLURM_ERROR;
		}

		if (!(st.st_mode & S_IFDIR)) {
			errno = EACCES;
			fprintf(stderr, " dump: "
				"archive dir %s isn't a directory\n",
				arch_cond->archive_dir);
			slurmdb_destroy_archive_cond(arch_cond);
			return SLURM_ERROR;
		}

		if (access(arch_cond->archive_dir, W_OK) < 0) {
			errno = EACCES;
			fprintf(stderr, " dump: "
				"archive dir %s is not writable\n",
				arch_cond->archive_dir);
			slurmdb_destroy_archive_cond(arch_cond);
			return SLURM_ERROR;
		}
	}

	if (arch_cond->archive_script) {
		if (stat(arch_cond->archive_script, &st) < 0) {
			exit_code = errno;
			fprintf(stderr, " dump: Failed to stat %s: %s\n "
				"Note: For archive dump, the script must be on "
				"the calling host.\n",
				arch_cond->archive_script,
				slurm_strerror(errno));
			slurmdb_destroy_archive_cond(arch_cond);
			return SLURM_ERROR;
		}
		if (!(st.st_mode & S_IFREG)) {
			errno = EACCES;
			fprintf(stderr, " dump: "
				"archive script %s isn't a regular file\n",
				arch_cond->archive_script);
			slurmdb_destroy_archive_cond(arch_cond);
			return SLURM_ERROR;
		}

		if (access(arch_cond->archive_script, X_OK) < 0) {
			errno = EACCES;
			fprintf(stderr, " dump: "
				"archive script %s is not executable\n",
				arch_cond->archive_script);
			slurmdb_destroy_archive_cond(arch_cond);
			return SLURM_ERROR;
		}
	}

	warning = "This may result in loss of accounting database records (if Purge* options enabled).\nAre you sure you want to continue?";
	if (commit_check(warning)) {
		rc = slurmdb_archive(db_conn, arch_cond);
		if (rc != SLURM_SUCCESS) {
			exit_code = 1;
			fprintf(stderr, " Problem dumping archive: %s\n",
				slurm_strerror(rc));
			rc = SLURM_ERROR;
		}
	} else {
		printf(" Changes Discarded\n");
	}

	slurmdb_destroy_archive_cond(arch_cond);

	return rc;
}

extern int sacctmgr_archive_load(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_archive_rec_t *arch_rec =
		xmalloc(sizeof(slurmdb_archive_rec_t));
	int i, command_len = 0;

	for (i = 0; i < argc; i++) {
		int end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!end
		   || !xstrncasecmp(argv[i], "File", MAX(command_len, 1))) {
			arch_rec->archive_file =
				strip_quotes(argv[i]+end, NULL, 0);
			if (!is_full_path(arch_rec->archive_file)) {
				char *file = arch_rec->archive_file;
				arch_rec->archive_file =
					make_full_path(arch_rec->archive_file);
				xfree(file);
			}
		} else if (!xstrncasecmp(argv[i], "Insert",
					 MAX(command_len, 2))) {
			arch_rec->insert = strip_quotes(argv[i]+end, NULL, 1);
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown option: %s\n", argv[i]);
		}
	}

	if (exit_code) {
		slurmdb_destroy_archive_rec(arch_rec);
		return SLURM_ERROR;
	}

	rc = slurmdb_archive_load(db_conn, arch_rec);
	if (rc == SLURM_SUCCESS) {
		if (commit_check("Would you like to commit changes?")) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	} else {
		exit_code = 1;
		fprintf(stderr, " Problem loading archive file: %s\n",
			slurm_strerror(rc));

		if (rc == EACCES || rc == EISDIR || rc == ENOENT)
			fprintf(stderr, " Note: For archive load, the file must be accessible on the slurmdbd host.\n");

		rc = SLURM_ERROR;
	}

	slurmdb_destroy_archive_rec(arch_rec);

	return rc;
}
