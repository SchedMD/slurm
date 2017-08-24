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

#include <sys/stat.h>

#include "src/sacctmgr/sacctmgr.h"
#include <sys/param.h>		/* MAXPATHLEN */
#include "src/common/proc_args.h"
#include "src/common/uid.h"

static char *_string_to_uid( char *name )
{
	uid_t uid;
	if ( uid_from_string( name, &uid ) != 0 ) {
		fprintf(stderr, "Invalid user id: %s\n", name);
		exit(1);
	}
	xfree(name);
	return xstrdup_printf( "%d", (int) uid );
}

/* returns number of objects added to list */
extern int _addto_uid_char_list(List char_list, char *names)
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
		while(names[i]) {
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
					name = _string_to_uid( name );

					while((tmp_char = list_next(itr))) {
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
			name = _string_to_uid( name );

			while((tmp_char = list_next(itr))) {
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

		if (!end && !strncasecmp(argv[i], "where",
					MAX(command_len, 5))) {
			continue;
		} else if (!end && !strncasecmp(argv[i], "events",
					  MAX(command_len, 1))) {
			arch_cond->purge_event |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !strncasecmp(argv[i], "jobs",
					  MAX(command_len, 1))) {
			arch_cond->purge_job |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !strncasecmp(argv[i], "reservations",
					  MAX(command_len, 1))) {
			arch_cond->purge_resv |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !strncasecmp(argv[i], "steps",
					  MAX(command_len, 1))) {
			arch_cond->purge_step |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !strncasecmp(argv[i], "suspend",
					  MAX(command_len, 1))) {
			arch_cond->purge_suspend |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !strncasecmp(argv[i], "txn",
					  MAX(command_len, 1))) {
			arch_cond->purge_txn |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end && !strncasecmp(argv[i], "usage",
					  MAX(command_len, 1))) {
			arch_cond->purge_usage |= SLURMDB_PURGE_ARCHIVE;
			set = 1;
		} else if (!end
			  || !strncasecmp(argv[i], "Clusters",
					  MAX(command_len, 1))) {
			if (!job_cond->cluster_list)
				job_cond->cluster_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->cluster_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Accounts",
					 MAX(command_len, 2))) {
			if (!job_cond->acct_list)
				job_cond->acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->acct_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Associations",
					 MAX(command_len, 2))) {
			if (!job_cond->associd_list)
				job_cond->associd_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->associd_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Directory",
					 MAX(command_len, 2))) {
			arch_cond->archive_dir =
				strip_quotes(argv[i]+end, NULL, 0);
			set = 1;
		} else if (!strncasecmp (argv[i], "End", MAX(command_len, 1))) {
			job_cond->usage_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Gid", MAX(command_len, 2))) {
			if (!job_cond->groupid_list)
				job_cond->groupid_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->groupid_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Jobs",
					 MAX(command_len, 1))) {
			char *end_char = NULL, *start_char = argv[i]+end;
			slurmdb_selected_step_t *selected_step = NULL;
			char *dot = NULL;
			if (!job_cond->step_list)
				job_cond->step_list =
					list_create(slurm_destroy_char);

			while ((end_char = strstr(start_char, ","))
			       && start_char) {
				*end_char = 0;
				while (isspace(*start_char))
					start_char++;  /* discard whitespace */
				if (!(int)*start_char)
					continue;
				selected_step = xmalloc(
					sizeof(slurmdb_selected_step_t));
				list_append(job_cond->step_list, selected_step);

				dot = strstr(start_char, ".");
				if (dot == NULL) {
					debug2("No jobstep requested");
					selected_step->stepid = NO_VAL;
				} else {
					*dot++ = 0;
					selected_step->stepid = atoi(dot);
				}
				selected_step->jobid = atoi(start_char);
				start_char = end_char + 1;
			}

			set = 1;
		} else if (!strncasecmp (argv[i], "Partitions",
					 MAX(command_len, 2))) {
			if (!job_cond->partition_list)
				job_cond->partition_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->partition_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "PurgeEventAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_event |= tmp;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeJobAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_job |= tmp;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeResvAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_resv |= tmp;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeStepAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_step |= tmp;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeSuspendAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_suspend |= tmp;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeTXNAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_txn |= tmp;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeUsageAfter",
					 MAX(command_len, 10))) {
			if ((tmp = slurmdb_parse_purge(argv[i]+end))
			    == NO_VAL) {
				exit_code = 1;
			} else {
				arch_cond->purge_usage |= tmp;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeEventMonths",
					 MAX(command_len, 6))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeEventMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_event |= tmp;
				arch_cond->purge_event |= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeJobMonths",
					 MAX(command_len, 6))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeJobMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_job |= tmp;
				arch_cond->purge_job |= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeResvMonths",
					 MAX(command_len, 6))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeResvMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_resv |= tmp;
				arch_cond->purge_resv |= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeStepMonths",
					 MAX(command_len, 7))) {
			if (get_uint(argv[i]+end, &tmp, "PurgeStepMonths")
			    != SLURM_SUCCESS) {
				exit_code = 1;
			} else {
				arch_cond->purge_step |= tmp;
				arch_cond->purge_step |= SLURMDB_PURGE_MONTHS;
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "PurgeSuspendMonths",
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
		} else if (!strncasecmp (argv[i], "PurgeTXNMonths",
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
		} else if (!strncasecmp (argv[i], "PurgeUsageMonths",
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
		} else if (!strncasecmp (argv[i], "Start",
					 MAX(command_len, 2))) {
			job_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Script",
					 MAX(command_len, 2))) {
			arch_cond->archive_script =
				strip_quotes(argv[i]+end, NULL, 0);
			set = 1;
		} else if (!strncasecmp (argv[i], "Users",
					 MAX(command_len, 1))) {
			if (!job_cond->userid_list)
				job_cond->userid_list =
					list_create(slurm_destroy_char);
			_addto_uid_char_list(job_cond->userid_list,
					     argv[i]+end);
			set = 1;
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
	int rc = SLURM_SUCCESS;
	slurmdb_archive_cond_t *arch_cond =
		xmalloc(sizeof(slurmdb_archive_cond_t));
	int i;
	struct stat st;

	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
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

	rc = jobacct_storage_g_archive(db_conn, arch_cond);
	if (rc == SLURM_SUCCESS) {
		if (commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		exit_code = 1;
		fprintf(stderr, " Problem dumping archive: %s\n",
			slurm_strerror(rc));
		rc = SLURM_ERROR;
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
	struct stat st;

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
		   || !strncasecmp (argv[i], "File", MAX(command_len, 1))) {
			arch_rec->archive_file =
				strip_quotes(argv[i]+end, NULL, 0);
		} else if (!strncasecmp (argv[i], "Insert",
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

	if (arch_rec->archive_file) {
		char *fullpath;
		char cwd[MAXPATHLEN + 1];
		int  mode = F_OK;

		if ((getcwd(cwd, MAXPATHLEN)) == NULL)
			fatal("getcwd failed: %m");

		if ((fullpath = search_path(cwd, arch_rec->archive_file,
					    true, mode, false))) {
			xfree(arch_rec->archive_file);
			arch_rec->archive_file = fullpath;
		}

		if (stat(arch_rec->archive_file, &st) < 0) {
			exit_code = errno;
			fprintf(stderr, " load: Failed to stat %s: %s\n "
				"Note: For archive load, the file must be on "
				"the calling host.\n",
				arch_rec->archive_file, slurm_strerror(errno));
			slurmdb_destroy_archive_rec(arch_rec);
			return SLURM_ERROR;
		}
	}

	rc = jobacct_storage_g_archive_load(db_conn, arch_rec);
	if (rc == SLURM_SUCCESS) {
		if (commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		exit_code = 1;
		fprintf(stderr, " Problem loading archive file: %s\n",
			slurm_strerror(rc));
		rc = SLURM_ERROR;
	}

	slurmdb_destroy_archive_rec(arch_rec);

	return rc;
}
