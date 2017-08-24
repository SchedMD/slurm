/*****************************************************************************\
 *  as_mysql_fix_runaway_jobs.c - functions dealing with runaway jobs.
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Nathan Yee <nyee32@schedmd.com>
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

#include "as_mysql_fix_runaway_jobs.h"
#include "src/common/list.h"

static int _job_sort_by_start_time(void *void1, void * void2)
{
	time_t start1 = (*(slurmdb_job_rec_t **)void1)->start;
	time_t start2 = (*(slurmdb_job_rec_t **)void2)->start;

	if (start1 < start2)
		return -1;
	else if (start1 > start2)
		return 1;
	else
		return 0;
}

static int _first_job_roll_up(mysql_conn_t *mysql_conn, time_t first_start)
{
	int rc = SLURM_SUCCESS;
	char *query;
	struct tm start_tm;
	time_t month_start;

	/* set up the month period */
	if (!slurm_localtime_r(&first_start, &start_tm)) {
		error("mktime for start failed for rollup\n");
		return SLURM_ERROR;
	}

	// Go to the last day of the previous month for rollup start
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 0;
	start_tm.tm_isdst = -1;
	month_start = slurm_mktime(&start_tm);

	query = xstrdup_printf("UPDATE \"%s_%s\" SET hourly_rollup = %ld, "
			       "daily_rollup = %ld, monthly_rollup = %ld",
			       mysql_conn->cluster_name, last_ran_table,
			       month_start, month_start, month_start);

	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	if (rc != SLURM_SUCCESS)
		error("%s Failed to rollup at the end of previous month",
		      __func__);

	xfree(query);
	return rc;
}

extern int as_mysql_fix_runaway_jobs(mysql_conn_t *mysql_conn, uint32_t uid,
				     List runaway_jobs)
{
	char *query = NULL, *job_ids = NULL;
	slurmdb_job_rec_t *job = NULL;
	ListIterator iter = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_job_rec_t *first_job;

	list_sort(runaway_jobs, _job_sort_by_start_time);
	first_job = list_peek(runaway_jobs);

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		slurmdb_user_rec_t user;

		memset(&user, 0, sizeof(slurmdb_user_rec_t));
		user.uid = uid;

		if (!is_user_any_coord(mysql_conn, &user)) {
			error("Only admins/operators/coordinators "
			      "can fix runaway jobs");
			return ESLURM_ACCESS_DENIED;
		}
	}

	iter = list_iterator_create(runaway_jobs);
	while ((job = list_next(iter))) {
		xstrfmtcat(job_ids, "%s%d", ((job_ids) ? "," : ""), job->jobid);
	}

	query = xstrdup_printf("UPDATE \"%s_%s\" SET time_end="
			       "GREATEST(time_start, time_eligible, time_submit), "
			       "state=%d WHERE id_job IN (%s);",
			       mysql_conn->cluster_name, job_table,
			       JOB_COMPLETE, job_ids);

	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	mysql_db_query(mysql_conn, query);
	xfree(query);
	xfree(job_ids);

	/* Set rollup to the the last day of the previous month of the first
	 * runaway job */
	rc = _first_job_roll_up(mysql_conn, first_job->start);
	if (rc != SLURM_SUCCESS) {
		error("Failed to fix runaway jobs");
		return SLURM_ERROR;
	}

	return rc;
}
