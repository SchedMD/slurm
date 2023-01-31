/*****************************************************************************\
 * runaway_jobs_functions.c - functions dealing with runaway/orphan jobs
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Nathan Yee <nyee32@schedmd.com>
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
#include "src/common/assoc_mgr.h"

/* Max runaway jobs per single sql statement. */
#define RUNAWAY_JOBS_PER_PASS 1000

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_job_cond_t *job_cond,
		     List format_list)
{
	int i, end = 0;
	int set = 0;
	int command_len = 0;

	for (i = (*start); i < argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len = strlen(argv[i]);
		else {
			command_len = end-1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!end ||
		    !xstrncasecmp(argv[i], "Cluster", MAX(command_len, 1))) {
			if (!job_cond->cluster_list)
				job_cond->cluster_list = list_create(xfree_ptr);
			if (slurm_addto_char_list(job_cond->cluster_list,
						  argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}

	(*start) = i;

	return set;
}

static void _print_runaway_jobs(List format_list, List jobs)
{
	char outbuf[FORMAT_STRING_SIZE];
	slurmdb_job_rec_t *job = NULL;
	ListIterator itr = NULL;
	ListIterator field_itr = NULL;
	List print_fields_list; /* types are of print_field_t */
	print_field_t *field = NULL;
	int field_count;

	printf("NOTE: Runaway jobs are jobs that don't exist in the "
	       "controller but have a start time and no end time "
	       "in the database\n");

	print_fields_list = sacctmgr_process_format_list(format_list);

	print_fields_header(print_fields_list);
	field_count = list_count(print_fields_list);

	list_sort(jobs, slurmdb_job_sort_by_submit_time);

	itr = list_iterator_create(jobs);
	field_itr = list_iterator_create(print_fields_list);
	while ((job = list_next(itr))) {
		int curr_inx = 1;
		while ((field = list_next(field_itr))) {
			switch(field->type) {
			case PRINT_ID:
				field->print_routine(
					field,
					&job->jobid,
					(curr_inx == field_count));
				break;
			case PRINT_NAME:
				field->print_routine(
					field,
					job->jobname,
					(curr_inx == field_count));
				break;
			case PRINT_PART:
				field->print_routine(
					field,
					job->partition,
					(curr_inx == field_count));
				break;
			case PRINT_CLUSTER:
				field->print_routine(
					field,
					job->cluster,
					(curr_inx == field_count));
				break;
			case PRINT_STATE:
				snprintf(outbuf, FORMAT_STRING_SIZE, "%s",
					 job_state_string(job->state));
				field->print_routine(
					field,
					outbuf,
					(curr_inx == field_count));
				break;
			case PRINT_TIMESTART:
				field->print_routine(
					field,
					&job->start,
					(curr_inx == field_count));
				break;
			case PRINT_TIMEEND:
				field->print_routine(
					field,
					&job->end,
					(curr_inx == field_count));
				break;
			case PRINT_TIMESUBMIT:
				field->print_routine(
					field,
					&job->submit,
					(curr_inx == field_count));
				break;
			case PRINT_TIMEELIGIBLE:
				field->print_routine(
					field,
					&job->eligible,
					(curr_inx == field_count));
				break;
			default:
				break;
			}
			curr_inx++;
		}
		list_iterator_reset(field_itr);
		printf("\n");
	}
	list_iterator_destroy(field_itr);
	list_iterator_destroy(itr);
}

/*
 * Remove job from run away list if it is a currently known job to slurmctld.
 */
static int _purge_known_jobs(void *x, void *key)
{
	job_info_msg_t *clus_jobs = (job_info_msg_t *) key;
	slurmdb_job_rec_t *db_job = (slurmdb_job_rec_t *) x;

	if (clus_jobs->record_count > 0) {
		job_info_t *clus_job  = clus_jobs->job_array;
		for (int i = 0; i < clus_jobs->record_count; i++, clus_job++) {
			if ((db_job->jobid == clus_job->job_id) &&
			    ((db_job->submit == clus_job->submit_time) ||
			     (db_job->submit == clus_job->resize_time))) {
				debug5("%s: matched known JobId=%u SubmitTime=%"PRIu64,
				       __func__, db_job->jobid,
				       (uint64_t)db_job->submit);
				return true;
			}
		}
	}

	debug5("%s: runaway job found JobId=%u SubmitTime=%"PRIu64,
	       __func__, db_job->jobid, (uint64_t)db_job->submit);

	return false;
}

static List _get_runaway_jobs(slurmdb_job_cond_t *job_cond)
{
	List db_jobs_list = NULL;
	job_info_msg_t *clus_jobs = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	List cluster_list = NULL;

	job_cond->db_flags = SLURMDB_JOB_FLAG_NOTSET;
	job_cond->flags |= JOBCOND_FLAG_RUNAWAY | JOBCOND_FLAG_NO_TRUNC;

	if (!job_cond->cluster_list || !list_count(job_cond->cluster_list)) {
		if (!job_cond->cluster_list)
			job_cond->cluster_list = list_create(xfree_ptr);
		slurm_addto_char_list(job_cond->cluster_list,
				      slurm_conf.cluster_name);
	}

	if (list_count(job_cond->cluster_list) != 1) {
		error("You can only fix runaway jobs on "
		      "one cluster at a time.");
		return NULL;
	}

	db_jobs_list = slurmdb_jobs_get(db_conn, job_cond);

	if (!db_jobs_list) {
		if (errno != ESLURM_ACCESS_DENIED)
			error("No job list returned");
		goto cleanup;
	} else if (!list_count(db_jobs_list))
		return db_jobs_list; /* Just return now since we don't
				      * have any run away jobs, no
				      * reason to check the cluster.
				      */
	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = job_cond->cluster_list;
	cluster_list = slurmdb_clusters_get(db_conn,
					    &cluster_cond);
	if (!cluster_list) {
		error("No cluster list returned.");
		goto cleanup;
	} else if (!list_count(cluster_list)) {
		error("Cluster %s is unknown",
		      (char *)list_peek(job_cond->cluster_list));
		goto cleanup;
	} else if (list_count(cluster_list) != 1) {
		error("slurmdb_clusters_get didn't return exactly one cluster (%d)!  This should never happen.",
		      list_count(cluster_list));
		goto cleanup;
	}

	working_cluster_rec = list_peek(cluster_list);
	if (!working_cluster_rec->control_host ||
	    working_cluster_rec->control_host[0] == '\0' ||
	    !working_cluster_rec->control_port) {
		error("Slurmctld running on cluster %s is not up, can't check running jobs",
		      working_cluster_rec->name);
		goto cleanup;
	}
	if (slurm_load_jobs((time_t)NULL, &clus_jobs, SHOW_ALL)) {
		error("Failed to get jobs from requested clusters: %m");
		goto cleanup;
	}

	list_delete_all(db_jobs_list, _purge_known_jobs, clus_jobs);

	return db_jobs_list;

cleanup:
	FREE_NULL_LIST(db_jobs_list);
	FREE_NULL_LIST(cluster_list);

	return NULL;
}

/*
 * List and ask user if they wish to fix the runaway jobs
 */
extern int sacctmgr_list_runaway_jobs(int argc, char **argv)
{
	List runaway_jobs = NULL;
	List process_jobs = list_create(slurmdb_destroy_job_rec);
	int rc = SLURM_SUCCESS;
	int i=0;
	char *cluster_str;
	List format_list = list_create(xfree_ptr);
	slurmdb_job_cond_t *job_cond = xmalloc(sizeof(slurmdb_job_cond_t));
	char *ask_msg = "\nWould you like to fix these runaway jobs?\n"
			"(This will set the end time for each job to the "
			"latest out of the start, eligible, or submit times, "
			"and set the state to completed.\n"
			"Once corrected, this will trigger the rollup to "
			"reroll usage from before the earliest submit time "
			"of all the runaway jobs.)\n\n";


	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, job_cond, format_list);
	}

	runaway_jobs = _get_runaway_jobs(job_cond);
	cluster_str = xstrdup(list_peek(job_cond->cluster_list));

	slurmdb_destroy_job_cond(job_cond);

	if (!runaway_jobs) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (!list_count(runaway_jobs)) {
		printf("Runaway Jobs: No runaway jobs found on cluster %s\n",
		       cluster_str);
		rc = SLURM_SUCCESS;
		goto end_it;
	}

	if (!list_count(format_list))
		slurm_addto_char_list(
			format_list,
			"ID%-12,Name,Part,Cluster,State%10,Submit,Start,End");

	_print_runaway_jobs(format_list, runaway_jobs);

	while (!rc && list_transfer_max(process_jobs, runaway_jobs,
					RUNAWAY_JOBS_PER_PASS)) {
		rc = slurmdb_jobs_fix_runaway(db_conn, process_jobs);
		list_flush(process_jobs);
	}

	if (rc == SLURM_SUCCESS) {
		if (commit_check(ask_msg))
			slurmdb_connection_commit(db_conn, 1);
		else {
			printf("Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	} else
		error("Failed to fix runaway job: %s\n",
		      slurm_strerror(rc));
end_it:
	xfree(cluster_str);
	FREE_NULL_LIST(runaway_jobs);
	FREE_NULL_LIST(process_jobs);
	FREE_NULL_LIST(format_list);

	return rc;
}
