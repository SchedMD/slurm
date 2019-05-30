/*****************************************************************************\
 *  as_mysql_job.c - functions dealing with jobs and job steps.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "as_mysql_job.h"
#include "as_mysql_usage.h"
#include "as_mysql_wckey.h"

#include "src/common/assoc_mgr.h"
#include "src/common/gres.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_time.h"

#define BUFFER_SIZE 4096

static char *_average_tres_usage(uint32_t *tres_ids, uint64_t *tres_cnts,
				 int tres_cnt, int tasks)
{
	char *ret_str = NULL;
	int i;

	/*
	 * Don't return NULL here, we need a blank string or we will print
	 * '(null)' in the database which really isn't what we want.
	 */
	if (!tasks)
		return xstrdup("");

	for (i = 0; i < tres_cnt; i++) {
		if (tres_cnts[i] == INFINITE64)
			continue;
		xstrfmtcat(ret_str, "%s%u=%"PRIu64,
			   ret_str ? "," : "",
			   tres_ids[i], tres_cnts[i] / (uint64_t)tasks);
	}

	if (!ret_str)
		ret_str = xstrdup("");
	return ret_str;
}

/* Used in job functions for getting the database index based off the
 * submit time and job.  0 is returned if none is found
 */
static uint64_t _get_db_index(mysql_conn_t *mysql_conn,
			      time_t submit, uint32_t jobid)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint64_t db_index = 0;
	char *query = xstrdup_printf("select job_db_inx from \"%s_%s\" where "
				     "time_submit=%d and id_job=%u",
				     mysql_conn->cluster_name, job_table,
				     (int)submit, jobid);

	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return 0;
	}
	xfree(query);

	row = mysql_fetch_row(result);
	if (!row) {
		mysql_free_result(result);
		debug4("We can't get a db_index for this combo, "
		       "time_submit=%d and id_job=%u.  "
		       "We must not have heard about the start yet, "
		       "no big deal, we will get one right after this.",
		       (int)submit, jobid);
		return 0;
	}
	db_index = slurm_atoull(row[0]);
	mysql_free_result(result);

	return db_index;
}

static char *_get_user_from_associd(mysql_conn_t *mysql_conn,
				    char *cluster, uint32_t associd)
{
	char *user = NULL;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* Just so we don't have to keep a
	   cache of the associations around we
	   will just query the db for the user
	   name of the association id.  Since
	   this should sort of be a rare case
	   this isn't too bad.
	*/
	query = xstrdup_printf("select user from \"%s_%s\" where id_assoc=%u",
			       cluster, assoc_table, associd);

	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	if (!(result =
	      mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	if ((row = mysql_fetch_row(result)) && row[0][0])
		user = xstrdup(row[0]);

	mysql_free_result(result);

	return user;
}

static uint32_t _get_wckeyid(mysql_conn_t *mysql_conn, char **name,
			     uid_t uid, char *cluster, uint32_t associd)
{
	uint32_t wckeyid = 0;

	if (slurm_get_track_wckey()) {
		/* Here we are looking for the wckeyid if it doesn't
		 * exist we will create one.  We don't need to check
		 * if it is good or not.  Right now this is the only
		 * place things are created. We do this only on a job
		 * start, not on a job submit since we don't want to
		 * slow down getting the db_index back to the
		 * controller.
		 */
		slurmdb_wckey_rec_t wckey_rec;
		char *user = NULL;

		/* since we are unable to rely on uids here (someone could
		   not have there uid in the system yet) we must
		   first get the user name from the associd */
		if (!(user = _get_user_from_associd(
			      mysql_conn, cluster, associd))) {
			error("No user for associd %u", associd);
			goto no_wckeyid;
		}
		/* get the default key */
		if (!*name) {
			slurmdb_user_rec_t user_rec;
			memset(&user_rec, 0, sizeof(slurmdb_user_rec_t));
			user_rec.uid = NO_VAL;
			user_rec.name = user;
			if (assoc_mgr_fill_in_user(mysql_conn, &user_rec,
						   1, NULL, false)
			    != SLURM_SUCCESS) {
				error("No user by name of %s assoc %u",
				      user, associd);
				xfree(user);
				goto no_wckeyid;
			}

			if (user_rec.default_wckey)
				*name = xstrdup_printf("*%s",
						       user_rec.default_wckey);
			else
				*name = xstrdup_printf("*");
		}

		memset(&wckey_rec, 0, sizeof(slurmdb_wckey_rec_t));
		wckey_rec.name = (*name);
		wckey_rec.uid = NO_VAL;
		wckey_rec.user = user;
		wckey_rec.cluster = cluster;
		if (assoc_mgr_fill_in_wckey(mysql_conn, &wckey_rec,
					    ACCOUNTING_ENFORCE_WCKEYS,
					    NULL, false) != SLURM_SUCCESS) {
			List wckey_list = NULL;
			slurmdb_wckey_rec_t *wckey_ptr = NULL;
			/* we have already checked to make
			   sure this was the slurm user before
			   calling this */

			wckey_list = list_create(slurmdb_destroy_wckey_rec);

			wckey_ptr = xmalloc(sizeof(slurmdb_wckey_rec_t));
			wckey_ptr->name = xstrdup((*name));
			wckey_ptr->user = xstrdup(user);
			wckey_ptr->cluster = xstrdup(cluster);
			list_append(wckey_list, wckey_ptr);
			/* info("adding wckey '%s' '%s' '%s'", */
			/* 	     wckey_ptr->name, wckey_ptr->user, */
			/* 	     wckey_ptr->cluster); */

			if (*name[0] == '*') {
				/* make sure the non * wckey has been added */
				wckey_rec.name = (*name)+1;
				if (assoc_mgr_fill_in_wckey(
					    mysql_conn, &wckey_rec,
					    ACCOUNTING_ENFORCE_WCKEYS,
					    NULL, false) != SLURM_SUCCESS) {
					wckey_ptr = xmalloc(
						sizeof(slurmdb_wckey_rec_t));
					wckey_ptr->name =
						xstrdup(wckey_rec.name);
					wckey_ptr->user = xstrdup(user);
					wckey_ptr->cluster = xstrdup(cluster);
					list_prepend(wckey_list, wckey_ptr);
					/* info("adding wckey '%s' '%s' " */
					/*      "'%s'", */
					/*      wckey_ptr->name, */
					/*      wckey_ptr->user, */
					/*      wckey_ptr->cluster); */
				}
				wckey_rec.name = (*name);
			}

			if (as_mysql_add_wckeys(mysql_conn,
						slurm_get_slurm_user_id(),
						wckey_list)
			    == SLURM_SUCCESS)
				acct_storage_p_commit(mysql_conn, 1);
			/* If that worked lets get it */
			assoc_mgr_fill_in_wckey(mysql_conn, &wckey_rec,
						ACCOUNTING_ENFORCE_WCKEYS,
						NULL, false);

			FREE_NULL_LIST(wckey_list);
		}
		xfree(user);
		/* info("got wckeyid of %d", wckey_rec.id); */
		wckeyid = wckey_rec.id;
	}
no_wckeyid:
	return wckeyid;
}

/* extern functions */

extern int as_mysql_job_start(mysql_conn_t *mysql_conn,
			      struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	char *nodes = NULL, *jname = NULL;
	int track_steps = 0;
	char *partition = NULL;
	char *query = NULL;
	int reinit = 0;
	time_t begin_time, check_time, start_time, submit_time;
	uint32_t wckeyid = 0;
	uint32_t job_state;
	uint32_t array_task_id =
		(job_ptr->array_job_id) ? job_ptr->array_task_id : NO_VAL;
	uint64_t job_db_inx = job_ptr->db_index;
	job_array_struct_t *array_recs = job_ptr->array_recs;
	char *tres_alloc_str = NULL;

	if ((!job_ptr->details || !job_ptr->details->submit_time)
	    && !job_ptr->resize_time) {
		error("as_mysql_job_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug2("%s: called", __func__);

	job_state = job_ptr->job_state;

	if (job_ptr->resize_time) {
		begin_time  = job_ptr->resize_time;
		submit_time = job_ptr->resize_time;
		start_time  = job_ptr->resize_time;
	} else {
		begin_time  = job_ptr->details->begin_time;
		submit_time = job_ptr->details->submit_time;
		start_time  = job_ptr->start_time;
	}

	/* If the reason is WAIT_ARRAY_TASK_LIMIT we don't want to
	 * give the pending jobs an eligible time since it will add
	 * time to accounting where as these jobs aren't able to run
	 * until later so mark it as such.
	 */
	if (job_ptr->state_reason == WAIT_ARRAY_TASK_LIMIT)
		begin_time = INFINITE;

	/* Since we need a new db_inx make sure the old db_inx
	 * removed. This is most likely the only time we are going to
	 * be notified of the change also so make the state without
	 * the resize. */
	if (IS_JOB_RESIZING(job_ptr)) {
		/* If we have a db_index lets end the previous record. */
		if (!job_ptr->db_index) {
			error("We don't have a db_index for job %u, "
			      "this should only happen when resizing "
			      "jobs and the database interface was down.",
			      job_ptr->job_id);
			job_ptr->db_index = _get_db_index(mysql_conn,
							  job_ptr->details->
							  submit_time,
							  job_ptr->job_id);
		}

		if (job_ptr->db_index)
			as_mysql_job_complete(mysql_conn, job_ptr);

		job_state &= (~JOB_RESIZING);
		job_ptr->db_index = 0;
	}

	job_state &= JOB_STATE_BASE;

	/* See what we are hearing about here if no start time. If
	 * this job latest time is before the last roll up we will
	 * need to reset it to look at this job. */
	if (start_time)
		check_time = start_time;
	else if (begin_time)
		check_time = begin_time;
	else
		check_time = submit_time;

	slurm_mutex_lock(&rollup_lock);
	if (check_time < global_last_rollup) {
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;

		/* check to see if we are hearing about this time for the
		 * first time.
		 */
		query = xstrdup_printf("select job_db_inx "
				       "from \"%s_%s\" where id_job=%u and "
				       "time_submit=%ld and time_eligible=%ld "
				       "and time_start=%ld;",
				       mysql_conn->cluster_name,
				       job_table, job_ptr->job_id,
				       submit_time, begin_time, start_time);
		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result =
		      mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			slurm_mutex_unlock(&rollup_lock);
			return SLURM_ERROR;
		}
		xfree(query);
		if ((row = mysql_fetch_row(result))) {
			mysql_free_result(result);
			debug4("revieved an update for a "
			       "job (%u) already known about",
			       job_ptr->job_id);
			slurm_mutex_unlock(&rollup_lock);
			goto no_rollup_change;
		}
		mysql_free_result(result);

		if (job_ptr->start_time)
			debug("Need to reroll usage from %s Job %u "
			      "from %s started then and we are just "
			      "now hearing about it.",
			      slurm_ctime2(&check_time),
			      job_ptr->job_id, mysql_conn->cluster_name);
		else if (begin_time)
			debug("Need to reroll usage from %s Job %u "
			      "from %s became eligible then and we are just "
			      "now hearing about it.",
			      slurm_ctime2(&check_time),
			      job_ptr->job_id, mysql_conn->cluster_name);
		else
			debug("Need to reroll usage from %s Job %u "
			      "from %s was submitted then and we are just "
			      "now hearing about it.",
			      slurm_ctime2(&check_time),
			      job_ptr->job_id, mysql_conn->cluster_name);

		global_last_rollup = check_time;
		slurm_mutex_unlock(&rollup_lock);

		/* If the times here are later than the daily_rollup
		   or monthly rollup it isn't a big deal since they
		   are always shrunk down to the beginning of each
		   time period.
		*/
		query = xstrdup_printf("update \"%s_%s\" set "
				       "hourly_rollup=%ld, "
				       "daily_rollup=%ld, monthly_rollup=%ld",
				       mysql_conn->cluster_name,
				       last_ran_table, check_time,
				       check_time, check_time);
		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	} else
		slurm_mutex_unlock(&rollup_lock);

no_rollup_change:

	if (job_ptr->name && job_ptr->name[0])
		jname = job_ptr->name;
	else {
		jname = "allocation";
		track_steps = 1;
	}

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "None assigned";

	if (job_ptr->batch_flag)
		track_steps = 1;

	/* Grab the wckey once to make sure it is placed. */
	if (job_ptr->assoc_id && (!job_ptr->db_index || job_ptr->wckey))
		wckeyid = _get_wckeyid(mysql_conn, &job_ptr->wckey,
				       job_ptr->user_id,
				       mysql_conn->cluster_name,
				       job_ptr->assoc_id);

	if (!IS_JOB_PENDING(job_ptr) && job_ptr->part_ptr)
		partition = job_ptr->part_ptr->name;
	else if (job_ptr->partition)
		partition = job_ptr->partition;

	if (!job_ptr->db_index) {
		query = xstrdup_printf(
			"insert into \"%s_%s\" "
			"(id_job, mod_time, id_array_job, id_array_task, "
			"pack_job_id, pack_job_offset, "
			"id_assoc, id_qos, id_user, "
			"id_group, nodelist, id_resv, timelimit, "
			"time_eligible, time_submit, time_start, "
			"job_name, track_steps, state, priority, cpus_req, "
			"nodes_alloc, mem_req, flags, state_reason_prev",
			mysql_conn->cluster_name, job_table);

		if (wckeyid)
			xstrcat(query, ", id_wckey");
		if (job_ptr->mcs_label)
			xstrcat(query, ", mcs_label");
		if (job_ptr->account)
			xstrcat(query, ", account");
		if (partition)
			xstrcat(query, ", `partition`");
		if (job_ptr->wckey)
			xstrcat(query, ", wckey");
		if (job_ptr->network)
			xstrcat(query, ", node_inx");
		if (job_ptr->gres_req)
			xstrcat(query, ", gres_req");
		if (job_ptr->gres_alloc)
			xstrcat(query, ", gres_alloc");
		if (array_recs && array_recs->task_id_str)
			xstrcat(query, ", array_task_str, array_max_tasks, "
				"array_task_pending");
		else
			xstrcat(query, ", array_task_str, array_task_pending");

		if (job_ptr->tres_alloc_str || tres_alloc_str)
			xstrcat(query, ", tres_alloc");
		if (job_ptr->tres_req_str)
			xstrcat(query, ", tres_req");
		if (job_ptr->details->work_dir)
			xstrcat(query, ", work_dir");
		if (job_ptr->details->features)
			xstrcat(query, ", constraints");

		xstrfmtcat(query,
			   ") values (%u, UNIX_TIMESTAMP(), "
			   "%u, %u, %u, %u, %u, %u, %u, %u, "
			   "'%s', %u, %u, %ld, %ld, %ld, "
			   "'%s', %u, %u, %u, %u, %u, %"PRIu64", %u, %u",
			   job_ptr->job_id,
			   job_ptr->array_job_id, array_task_id,
			   job_ptr->pack_job_id, job_ptr->pack_job_offset,
			   job_ptr->assoc_id, job_ptr->qos_id,
			   job_ptr->user_id, job_ptr->group_id, nodes,
			   job_ptr->resv_id, job_ptr->time_limit,
			   begin_time, submit_time, start_time,
			   jname, track_steps, job_state,
			   job_ptr->priority, job_ptr->details->min_cpus,
			   job_ptr->total_nodes,
			   job_ptr->details->pn_min_memory,
			   job_ptr->db_flags,
			   job_ptr->state_reason_prev_db);

		if (wckeyid)
			xstrfmtcat(query, ", %u", wckeyid);
		if (job_ptr->mcs_label)
			xstrfmtcat(query, ", '%s'", job_ptr->mcs_label);
		if (job_ptr->account)
			xstrfmtcat(query, ", '%s'", job_ptr->account);
		if (partition)
			xstrfmtcat(query, ", '%s'", partition);
		if (job_ptr->wckey)
			xstrfmtcat(query, ", '%s'", job_ptr->wckey);
		if (job_ptr->network)
			xstrfmtcat(query, ", '%s'", job_ptr->network);
		if (job_ptr->gres_req)
			xstrfmtcat(query, ", '%s'", job_ptr->gres_req);
		if (job_ptr->gres_alloc)
			xstrfmtcat(query, ", '%s'", job_ptr->gres_alloc);
		if (array_recs && array_recs->task_id_str)
			xstrfmtcat(query, ", '%s', %u, %u",
				   array_recs->task_id_str,
				   array_recs->max_run_tasks,
				   array_recs->task_cnt);
		else
			xstrcat(query, ", NULL, 0");

		if (tres_alloc_str)
			xstrfmtcat(query, ", '%s'", tres_alloc_str);
		else if (job_ptr->tres_alloc_str)
			xstrfmtcat(query, ", '%s'", job_ptr->tres_alloc_str);
		if (job_ptr->tres_req_str)
			xstrfmtcat(query, ", '%s'", job_ptr->tres_req_str);
		if (job_ptr->details->work_dir)
			xstrfmtcat(query, ", '%s'",
				   job_ptr->details->work_dir);
		if (job_ptr->details->features)
			xstrfmtcat(query, ", '%s'",
				   job_ptr->details->features);

		xstrfmtcat(query,
			   ") on duplicate key update "
			   "job_db_inx=LAST_INSERT_ID(job_db_inx), "
			   "id_assoc=%u, id_user=%u, id_group=%u, "
			   "nodelist='%s', id_resv=%u, timelimit=%u, "
			   "time_submit=%ld, time_eligible=%ld, "
			   "time_start=%ld, mod_time=UNIX_TIMESTAMP(), "
			   "job_name='%s', track_steps=%u, id_qos=%u, "
			   "state=greatest(state, %u), priority=%u, "
			   "cpus_req=%u, nodes_alloc=%u, "
			   "mem_req=%"PRIu64", id_array_job=%u, id_array_task=%u, "
			   "pack_job_id=%u, pack_job_offset=%u, flags=%u, "
			   "state_reason_prev=%u",
			   job_ptr->assoc_id, job_ptr->user_id,
			   job_ptr->group_id, nodes,
			   job_ptr->resv_id, job_ptr->time_limit,
			   submit_time, begin_time, start_time,
			   jname, track_steps, job_ptr->qos_id, job_state,
			   job_ptr->priority, job_ptr->details->min_cpus,
			   job_ptr->total_nodes,
			   job_ptr->details->pn_min_memory,
			   job_ptr->array_job_id, array_task_id,
			   job_ptr->pack_job_id, job_ptr->pack_job_offset,
			   job_ptr->db_flags,
			   job_ptr->state_reason_prev_db);

		if (wckeyid)
			xstrfmtcat(query, ", id_wckey=%u", wckeyid);
		if (job_ptr->mcs_label)
			xstrfmtcat(query, ", mcs_label='%s'",
				   job_ptr->mcs_label);
		if (job_ptr->account)
			xstrfmtcat(query, ", account='%s'", job_ptr->account);
		if (partition)
			xstrfmtcat(query, ", `partition`='%s'", partition);
		if (job_ptr->wckey)
			xstrfmtcat(query, ", wckey='%s'", job_ptr->wckey);
		if (job_ptr->network)
			xstrfmtcat(query, ", node_inx='%s'", job_ptr->network);
		if (job_ptr->gres_req)
			xstrfmtcat(query, ", gres_req='%s'", job_ptr->gres_req);
		if (job_ptr->gres_alloc)
			xstrfmtcat(query, ", gres_alloc='%s'",
				   job_ptr->gres_alloc);
		if (array_recs && array_recs->task_id_str)
			xstrfmtcat(query, ", array_task_str='%s', "
				   "array_max_tasks=%u, array_task_pending=%u",
				   array_recs->task_id_str,
				   array_recs->max_run_tasks,
				   array_recs->task_cnt);
		else
			xstrfmtcat(query, ", array_task_str=NULL, "
				   "array_task_pending=0");

		if (tres_alloc_str)
			xstrfmtcat(query, ", tres_alloc='%s'", tres_alloc_str);
		else if (job_ptr->tres_alloc_str)
			xstrfmtcat(query, ", tres_alloc='%s'",
				   job_ptr->tres_alloc_str);
		if (job_ptr->tres_req_str)
			xstrfmtcat(query, ", tres_req='%s'",
				   job_ptr->tres_req_str);
		if (job_ptr->details->work_dir)
			xstrfmtcat(query, ", work_dir='%s'",
				   job_ptr->details->work_dir);
		if (job_ptr->details->features)
			xstrfmtcat(query, ", constraints='%s'",
				   job_ptr->details->features);

		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	try_again:
		if (!(job_ptr->db_index = mysql_db_insert_ret_id(
			      mysql_conn, query))) {
			if (!reinit) {
				error("It looks like the storage has gone "
				      "away trying to reconnect");
				/* reconnect */
				check_connection(mysql_conn);
				reinit = 1;
				goto try_again;
			} else
				rc = SLURM_ERROR;
		}
	} else {
		query = xstrdup_printf("update \"%s_%s\" set nodelist='%s', ",
				       mysql_conn->cluster_name,
				       job_table, nodes);

		if (wckeyid)
			xstrfmtcat(query, "id_wckey=%u, ", wckeyid);
		if (job_ptr->mcs_label)
			xstrfmtcat(query, "mcs_label='%s', ",
				   job_ptr->mcs_label);
		if (job_ptr->account)
			xstrfmtcat(query, "account='%s', ", job_ptr->account);
		if (partition)
			xstrfmtcat(query, "`partition`='%s', ", partition);
		if (job_ptr->wckey)
			xstrfmtcat(query, "wckey='%s', ", job_ptr->wckey);
		if (job_ptr->network)
			xstrfmtcat(query, "node_inx='%s', ", job_ptr->network);
		if (job_ptr->gres_req)
			xstrfmtcat(query, "gres_req='%s', ",
				   job_ptr->gres_req);
		if (job_ptr->gres_alloc)
			xstrfmtcat(query, "gres_alloc='%s', ",
				   job_ptr->gres_alloc);
		if (array_recs && array_recs->task_id_str)
			xstrfmtcat(query, "array_task_str='%s', "
				   "array_max_tasks=%u, "
				   "array_task_pending=%u, ",
				   array_recs->task_id_str,
				   array_recs->max_run_tasks,
				   array_recs->task_cnt);
		else
			xstrfmtcat(query, "array_task_str=NULL, "
				   "array_task_pending=0, ");

		if (tres_alloc_str)
			xstrfmtcat(query, "tres_alloc='%s', ", tres_alloc_str);
		else if (job_ptr->tres_alloc_str)
			xstrfmtcat(query, "tres_alloc='%s', ",
				   job_ptr->tres_alloc_str);
		if (job_ptr->tres_req_str)
			xstrfmtcat(query, "tres_req='%s', ",
				   job_ptr->tres_req_str);
		if (job_ptr->details->work_dir)
			xstrfmtcat(query, "work_dir='%s', ",
				   job_ptr->details->work_dir);
		if (job_ptr->details->features)
			xstrfmtcat(query, "constraints='%s', ",
				   job_ptr->details->features);

		xstrfmtcat(query, "time_start=%ld, job_name='%s', "
			   "state=greatest(state, %u), "
			   "nodes_alloc=%u, id_qos=%u, "
			   "id_assoc=%u, id_resv=%u, "
			   "timelimit=%u, mem_req=%"PRIu64", "
			   "id_array_job=%u, id_array_task=%u, "
			   "pack_job_id=%u, pack_job_offset=%u, "
			   "flags=%u, state_reason_prev=%u, "
			   "time_eligible=%ld, mod_time=UNIX_TIMESTAMP() "
			   "where job_db_inx=%"PRIu64,
			   start_time, jname, job_state,
			   job_ptr->total_nodes, job_ptr->qos_id,
			   job_ptr->assoc_id,
			   job_ptr->resv_id, job_ptr->time_limit,
			   job_ptr->details->pn_min_memory,
			   job_ptr->array_job_id, array_task_id,
			   job_ptr->pack_job_id, job_ptr->pack_job_offset,
			   job_ptr->db_flags, job_ptr->state_reason_prev_db,
			   begin_time, job_ptr->db_index);

		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
	}

	/* now we will reset all the steps */
	if (IS_JOB_RESIZING(job_ptr)) {
		/* FIXME : Verify this is still needed */
		if (IS_JOB_SUSPENDED(job_ptr))
			as_mysql_suspend(mysql_conn, job_db_inx, job_ptr);
	}

	xfree(tres_alloc_str);
	xfree(query);

	return rc;
}

extern List as_mysql_modify_job(mysql_conn_t *mysql_conn, uint32_t uid,
				slurmdb_job_modify_cond_t *job_cond,
				slurmdb_job_rec_t *job)
{
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *query = NULL, *cond_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if (!job_cond || !job) {
		error("we need something to change");
		return NULL;
	} else if (job_cond->job_id == NO_VAL) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Job ID was not specified for job modification\n");
		return NULL;
	} else if (!job_cond->cluster) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Cluster was not specified for job modification\n");
		return NULL;
	} else if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (job->derived_ec != NO_VAL)
		xstrfmtcat(vals, ", derived_ec=%u", job->derived_ec);

	if (job->derived_es)
		xstrfmtcat(vals, ", derived_es='%s'", job->derived_es);

	if (job->system_comment)
		xstrfmtcat(vals, ", system_comment='%s'",
			   job->system_comment);

	if (!vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("No change specified for job modification");
		return NULL;
	}

	if (job_cond->submit_time)
		xstrfmtcat(cond_char, "&& time_submit=%d ",
			   (int)job_cond->submit_time);

	/* Here we want to get the last job submitted here */
	query = xstrdup_printf("select job_db_inx, id_job, time_submit, "
			       "id_user "
			       "from \"%s_%s\" where deleted=0 "
			       "&& id_job=%u %s"
			       "order by time_submit desc limit 1;",
			       job_cond->cluster, job_table,
			       job_cond->job_id, cond_char ? cond_char : "");
	xfree(cond_char);

	if (debug_flags & DEBUG_FLAG_DB_JOB)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(vals);
		xfree(query);
		return NULL;
	}

	if ((row = mysql_fetch_row(result))) {
		char tmp_char[25];
		time_t time_submit = atol(row[2]);

		if ((uid != atoi(row[3])) &&
		    !is_user_min_admin_level(mysql_conn, uid,
					     SLURMDB_ADMIN_OPERATOR)) {
			errno = ESLURM_ACCESS_DENIED;
			xfree(vals);
			xfree(query);
			mysql_free_result(result);
			return NULL;
		}

		slurm_make_time_str(&time_submit, tmp_char, sizeof(tmp_char));

		xstrfmtcat(cond_char, "job_db_inx=%s", row[0]);
		object = xstrdup_printf("%s submitted at %s", row[1], tmp_char);

		ret_list = list_create(slurm_destroy_char);
		list_append(ret_list, object);
		mysql_free_result(result);
	} else {
		errno = ESLURM_INVALID_JOB_ID;
		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn,
				 "as_mysql_modify_job: Job not found\n%s",
				 query);
		xfree(vals);
		xfree(query);
		mysql_free_result(result);
		return NULL;
	}
	xfree(query);

	user_name = uid_to_string((uid_t) uid);
	rc = modify_common(mysql_conn, DBD_MODIFY_JOB, now, user_name,
			   job_table, cond_char, vals, job_cond->cluster);
	xfree(user_name);
	xfree(cond_char);
	xfree(vals);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify job");
		FREE_NULL_LIST(ret_list);
		ret_list = NULL;
	}

	return ret_list;
}

extern int as_mysql_job_complete(mysql_conn_t *mysql_conn,
				 struct job_record *job_ptr)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS, job_state;
	time_t submit_time, end_time;
	uint32_t exit_code = 0;
	char *tres_alloc_str = NULL;

	if (!job_ptr->db_index
	    && ((!job_ptr->details || !job_ptr->details->submit_time)
		&& !job_ptr->resize_time)) {
		error("as_mysql_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug2("%s() called", __func__);

	if (job_ptr->resize_time)
		submit_time = job_ptr->resize_time;
	else
		submit_time = job_ptr->details->submit_time;

	if (IS_JOB_RESIZING(job_ptr)) {
		end_time = job_ptr->resize_time;
		job_state = JOB_RESIZING;
	} else {
		if (job_ptr->end_time == 0) {
			if (job_ptr->start_time) {
				error("%s: We are trying to end a job (%u) with no end time, setting it to the start time (%ld) of the job.",
				      __func__,
				      job_ptr->job_id, job_ptr->start_time);
				job_ptr->end_time = job_ptr->start_time;
			} else {
				error("%s: job %u never started",
				      __func__, job_ptr->job_id);

				/* If we get an error with this just
				 * fall through to avoid an infinite loop */
				return SLURM_SUCCESS;
			}
		}
		end_time = job_ptr->end_time;

		if (IS_JOB_REQUEUED(job_ptr))
			job_state = JOB_REQUEUE;
		else if (IS_JOB_REVOKED(job_ptr))
			job_state = JOB_REVOKED;
		else
			job_state = job_ptr->job_state & JOB_STATE_BASE;
	}

	slurm_mutex_lock(&rollup_lock);
	if (end_time < global_last_rollup) {
		global_last_rollup = job_ptr->end_time;
		slurm_mutex_unlock(&rollup_lock);

		query = xstrdup_printf("update \"%s_%s\" set "
				       "hourly_rollup=%ld, "
				       "daily_rollup=%ld, monthly_rollup=%ld",
				       mysql_conn->cluster_name,
				       last_ran_table, end_time,
				       end_time, end_time);
		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		(void) mysql_db_query(mysql_conn, query);
		xfree(query);
	} else
		slurm_mutex_unlock(&rollup_lock);

	if (!job_ptr->db_index) {
		if (!(job_ptr->db_index =
		      _get_db_index(mysql_conn,
				    submit_time,
				    job_ptr->job_id))) {
			/* Comment is overloaded in job_start to be
			   the block_id, so we will need to store this
			   for later.
			*/
			char *comment = job_ptr->comment;
			job_ptr->comment = NULL;
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if (as_mysql_job_start(
				    mysql_conn, job_ptr) == SLURM_ERROR) {
				job_ptr->comment = comment;
				error("couldn't add job %u at job completion",
				      job_ptr->job_id);
				return SLURM_SUCCESS;
			}
			job_ptr->comment = comment;
		}
	}

	/*
	 * make sure we handle any quotes that may be in the comment
	 */

	query = xstrdup_printf("update \"%s_%s\" set "
			       "mod_time=UNIX_TIMESTAMP(), "
			       "time_end=%ld, state=%d",
			       mysql_conn->cluster_name, job_table,
			       end_time, job_state);

	if (job_ptr->derived_ec != NO_VAL)
		xstrfmtcat(query, ", derived_ec=%u", job_ptr->derived_ec);

	if (tres_alloc_str)
		xstrfmtcat(query, ", tres_alloc='%s'", tres_alloc_str);
	else if (job_ptr->tres_alloc_str)
		xstrfmtcat(query, ", tres_alloc='%s'", job_ptr->tres_alloc_str);

	if (job_ptr->comment)
		xstrfmtcat(query, ", derived_es='%s'", job_ptr->comment);

	if (job_ptr->admin_comment)
		xstrfmtcat(query, ", admin_comment='%s'",
			   job_ptr->admin_comment);

	if (job_ptr->system_comment)
		xstrfmtcat(query, ", system_comment='%s'",
			   job_ptr->system_comment);

	exit_code = job_ptr->exit_code;
	if (exit_code == 1) {
		/* This wasn't signaled, it was set by Slurm so don't
		 * treat it like a signal.
		 */
		exit_code = 256;
	}

	xstrfmtcat(query,
		   ", exit_code=%d, kill_requid=%d where job_db_inx=%"PRIu64";",
		   exit_code, job_ptr->requid,
		   job_ptr->db_index);

	if (debug_flags & DEBUG_FLAG_DB_JOB)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	xfree(tres_alloc_str);
	return rc;
}

extern int as_mysql_step_start(mysql_conn_t *mysql_conn,
			       struct step_record *step_ptr)
{
	int tasks = 0, nodes = 0, task_dist = 0;
	int rc = SLURM_SUCCESS;
	char temp_bit[BUF_SIZE];
	char node_list[BUFFER_SIZE];
	char *node_inx = NULL;
	time_t start_time, submit_time;
	char *query = NULL;

	if (!step_ptr->job_ptr->db_index
	    && ((!step_ptr->job_ptr->details
		 || !step_ptr->job_ptr->details->submit_time)
		&& !step_ptr->job_ptr->resize_time)) {
		error("as_mysql_step_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (step_ptr->job_ptr->resize_time) {
		submit_time = start_time = step_ptr->job_ptr->resize_time;
		if (step_ptr->start_time > submit_time)
			start_time = step_ptr->start_time;
	} else {
		start_time = step_ptr->start_time;
		submit_time = step_ptr->job_ptr->details->submit_time;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;
	if (slurmdbd_conf) {
		if (step_ptr->job_ptr->details)
			tasks = step_ptr->job_ptr->details->num_tasks;
		else
			tasks = step_ptr->cpu_count;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
		nodes = step_ptr->step_layout->node_cnt;
		task_dist = step_ptr->step_layout->task_dist;
		node_inx = step_ptr->network;
	} else if (step_ptr->step_id == SLURM_BATCH_SCRIPT) {
		if (step_ptr->step_node_bitmap) {
			node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					   step_ptr->step_node_bitmap);
		}
		/*
		 * We overload tres_per_node with the node name of where the
		 * script was running.
		 */
		snprintf(node_list, BUFFER_SIZE, "%s", step_ptr->tres_per_node);
		nodes = tasks = 1;
		if (!step_ptr->tres_alloc_str)
			xstrfmtcat(step_ptr->tres_alloc_str,
				   "%s%u=%u,%u=%u",
				   step_ptr->tres_alloc_str ? "," : "",
				   TRES_CPU, 1,
				   TRES_NODE, 1);
	} else {
		char *temp_nodes = NULL;

		if (step_ptr->step_node_bitmap) {
			node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					   step_ptr->step_node_bitmap);
		}

		if (!step_ptr->step_layout
		    || !step_ptr->step_layout->task_cnt) {
			if (step_ptr->cpu_count)
				tasks = step_ptr->cpu_count;
			else {
				if ((tasks = slurmdb_find_tres_count_in_string(
					     step_ptr->tres_alloc_str,
					     TRES_CPU)) == INFINITE64) {
					if ((tasks =
					     slurmdb_find_tres_count_in_string(
						     step_ptr->job_ptr->
						     tres_alloc_str,
						     TRES_CPU)) == INFINITE64)
						tasks = step_ptr->job_ptr->
							total_nodes;
				}
			}

			nodes = step_ptr->job_ptr->total_nodes;
			temp_nodes = step_ptr->job_ptr->nodes;
		} else {
			tasks = step_ptr->step_layout->task_cnt;
			nodes = step_ptr->step_layout->node_cnt;
			task_dist = step_ptr->step_layout->task_dist;
			temp_nodes = step_ptr->step_layout->node_list;
		}

		snprintf(node_list, BUFFER_SIZE, "%s", temp_nodes);
	}

	if (!step_ptr->job_ptr->db_index) {
		if (!(step_ptr->job_ptr->db_index =
		      _get_db_index(mysql_conn,
				    submit_time,
				    step_ptr->job_ptr->job_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if (as_mysql_job_start(mysql_conn, step_ptr->job_ptr)
			    == SLURM_ERROR) {
				error("couldn't add job %u at step start",
				      step_ptr->job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	/* we want to print a -1 for the requid so leave it a
	   %d */
	/* The stepid could be -2 so use %d not %u */
	query = xstrdup_printf(
		"insert into \"%s_%s\" (job_db_inx, id_step, time_start, "
		"step_name, state, tres_alloc, "
		"nodes_alloc, task_cnt, nodelist, node_inx, "
		"task_dist, req_cpufreq, req_cpufreq_min, req_cpufreq_gov) "
		"values (%"PRIu64", %d, %d, '%s', %d, '%s', %d, %d, "
		"'%s', '%s', %d, %u, %u, %u) "
		"on duplicate key update "
		"nodes_alloc=%d, task_cnt=%d, time_end=0, state=%d, "
		"nodelist='%s', node_inx='%s', task_dist=%d, "
		"req_cpufreq=%u, req_cpufreq_min=%u, req_cpufreq_gov=%u,"
		"tres_alloc='%s';",
		mysql_conn->cluster_name, step_table,
		step_ptr->job_ptr->db_index,
		step_ptr->step_id,
		(int)start_time, step_ptr->name,
		JOB_RUNNING, step_ptr->tres_alloc_str,
		nodes, tasks, node_list, node_inx, task_dist,
		step_ptr->cpu_freq_max, step_ptr->cpu_freq_min,
		step_ptr->cpu_freq_gov, nodes, tasks, JOB_RUNNING,
		node_list, node_inx, task_dist, step_ptr->cpu_freq_max,
		step_ptr->cpu_freq_min, step_ptr->cpu_freq_gov,
		step_ptr->tres_alloc_str);
	if (debug_flags & DEBUG_FLAG_DB_STEP)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	return rc;
}

extern int as_mysql_step_complete(mysql_conn_t *mysql_conn,
				  struct step_record *step_ptr)
{
	time_t now;
	uint16_t comp_status;
	int tasks = 0;
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	uint32_t exit_code = 0;
	time_t submit_time;

	if (!step_ptr->job_ptr->db_index
	    && ((!step_ptr->job_ptr->details
		 || !step_ptr->job_ptr->details->submit_time)
		&& !step_ptr->job_ptr->resize_time)) {
		error("as_mysql_step_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (step_ptr->job_ptr->resize_time)
		submit_time = step_ptr->job_ptr->resize_time;
	else
		submit_time = step_ptr->job_ptr->details->submit_time;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (slurmdbd_conf) {
		now = step_ptr->job_ptr->end_time;
		if (step_ptr->job_ptr->details)
			tasks = step_ptr->job_ptr->details->num_tasks;
		else
			tasks = step_ptr->cpu_count;
	} else if (step_ptr->step_id == SLURM_BATCH_SCRIPT) {
		now = time(NULL);
		tasks = 1;
	} else {
		now = time(NULL);
		if (!step_ptr->step_layout
		    || !step_ptr->step_layout->task_cnt) {
			if (step_ptr->cpu_count)
				tasks = step_ptr->cpu_count;
			else {
				if ((tasks = slurmdb_find_tres_count_in_string(
					     step_ptr->tres_alloc_str,
					     TRES_CPU)) == INFINITE64) {
					if ((tasks =
					     slurmdb_find_tres_count_in_string(
						     step_ptr->job_ptr->
						     tres_alloc_str,
						     TRES_CPU)) == INFINITE64)
						tasks = step_ptr->job_ptr->
							total_nodes;
				}
			}
		} else
			tasks = step_ptr->step_layout->task_cnt;
	}

	exit_code = step_ptr->exit_code;
	comp_status = step_ptr->state & JOB_STATE_BASE;
	if (comp_status < JOB_COMPLETE) {
		if (exit_code == SIG_OOM) {
			comp_status = JOB_OOM;
		} else if (WIFSIGNALED(exit_code)) {
			comp_status = JOB_CANCELLED;
		} else if (exit_code)
			comp_status = JOB_FAILED;
		else {
			step_ptr->requid = -1;
			comp_status = JOB_COMPLETE;
		}
	}

	if (!step_ptr->job_ptr->db_index) {
		if (!(step_ptr->job_ptr->db_index =
		      _get_db_index(mysql_conn,
				    submit_time,
				    step_ptr->job_ptr->job_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if (as_mysql_job_start(mysql_conn, step_ptr->job_ptr)
			    == SLURM_ERROR) {
				error("couldn't add job %u "
				      "at step completion",
				      step_ptr->job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	/* The stepid could be -2 so use %d not %u */
	query = xstrdup_printf(
		"update \"%s_%s\" set time_end=%d, state=%u, "
		"kill_requid=%d, exit_code=%d",
		mysql_conn->cluster_name, step_table, (int)now,
		comp_status,
		step_ptr->requid,
		exit_code);


	if (jobacct) {
		slurmdb_stats_t stats;

		memset(&stats, 0, sizeof(slurmdb_stats_t));

		/* figure out the ave of the totals sent */
		if (tasks > 0) {
			stats.tres_usage_in_ave =
				_average_tres_usage(jobacct->tres_ids,
						    jobacct->tres_usage_in_tot,
						    jobacct->tres_count,
						    tasks);
			stats.tres_usage_out_ave =
				_average_tres_usage(jobacct->tres_ids,
						    jobacct->tres_usage_out_tot,
						    jobacct->tres_count,
						    tasks);
		}

		/*
		 * We can't trust the assoc_mgr here as the tres may have
		 * changed, we have to go off what was sent us.  We can just use
		 * the _average_tres_usage to do this by dividing by 1.
		 */
		stats.tres_usage_in_max = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_in_max,
			jobacct->tres_count, 1);
		stats.tres_usage_in_max_nodeid = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_in_max_nodeid,
			jobacct->tres_count, 1);
		stats.tres_usage_in_max_taskid = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_in_max_taskid,
			jobacct->tres_count, 1);
		stats.tres_usage_in_min = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_in_min,
			jobacct->tres_count, 1);
		stats.tres_usage_in_min_nodeid = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_in_min_nodeid,
			jobacct->tres_count, 1);
		stats.tres_usage_in_min_taskid = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_in_min_taskid,
			jobacct->tres_count, 1);
		stats.tres_usage_in_tot = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_in_tot,
			jobacct->tres_count, 1);
		stats.tres_usage_out_max = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_out_max,
			jobacct->tres_count, 1);
		stats.tres_usage_out_max_nodeid = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_out_max_nodeid,
			jobacct->tres_count, 1);
		stats.tres_usage_out_max_taskid = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_out_max_taskid,
			jobacct->tres_count, 1);
		stats.tres_usage_out_min = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_out_min,
			jobacct->tres_count, 1);
		stats.tres_usage_out_min_nodeid = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_out_min_nodeid,
			jobacct->tres_count, 1);
		stats.tres_usage_out_min_taskid = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_out_min_taskid,
			jobacct->tres_count, 1);
		stats.tres_usage_out_tot = _average_tres_usage(
			jobacct->tres_ids,
			jobacct->tres_usage_out_tot,
			jobacct->tres_count, 1);

		xstrfmtcat(query,
			   ", user_sec=%u, user_usec=%u, "
			   "sys_sec=%u, sys_usec=%u, "
			   "act_cpufreq=%u, consumed_energy=%"PRIu64", "
			   "tres_usage_in_ave='%s', "
			   "tres_usage_out_ave='%s', "
			   "tres_usage_in_max='%s', "
			   "tres_usage_in_max_taskid='%s', "
			   "tres_usage_in_max_nodeid='%s', "
			   "tres_usage_in_min='%s', "
			   "tres_usage_in_min_taskid='%s', "
			   "tres_usage_in_min_nodeid='%s', "
			   "tres_usage_in_tot='%s', "
			   "tres_usage_out_max='%s', "
			   "tres_usage_out_max_taskid='%s', "
			   "tres_usage_out_max_nodeid='%s', "
			   "tres_usage_out_min='%s', "
			   "tres_usage_out_min_taskid='%s', "
			   "tres_usage_out_min_nodeid='%s', "
			   "tres_usage_out_tot='%s'",
			   /* user seconds */
			   jobacct->user_cpu_sec,
			   /* user microseconds */
			   jobacct->user_cpu_usec,
			   /* system seconds */
			   jobacct->sys_cpu_sec,
			   /* system microsecs */
			   jobacct->sys_cpu_usec,
			   jobacct->act_cpufreq,
			   jobacct->energy.consumed_energy,
			   stats.tres_usage_in_ave,
			   stats.tres_usage_out_ave,
			   stats.tres_usage_in_max,
			   stats.tres_usage_in_max_taskid,
			   stats.tres_usage_in_max_nodeid,
			   stats.tres_usage_in_min,
			   stats.tres_usage_in_min_taskid,
			   stats.tres_usage_in_min_nodeid,
			   stats.tres_usage_in_tot,
			   stats.tres_usage_out_max,
			   stats.tres_usage_out_max_taskid,
			   stats.tres_usage_out_max_nodeid,
			   stats.tres_usage_out_min,
			   stats.tres_usage_out_min_taskid,
			   stats.tres_usage_out_min_nodeid,
			   stats.tres_usage_out_tot);

		slurmdb_free_slurmdb_stats_members(&stats);
	}

	/* id_step has to be %d here to handle the -2 -1 for the batch and
	   extern steps.  Don't change it to a %u.
	*/
	xstrfmtcat(query,
		   " where job_db_inx=%"PRIu64" and id_step=%d",
		   step_ptr->job_ptr->db_index, step_ptr->step_id);
	if (debug_flags & DEBUG_FLAG_DB_STEP)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	/* set the energy for the entire job. */
	if (step_ptr->job_ptr->tres_alloc_str) {
		query = xstrdup_printf(
			"update \"%s_%s\" set tres_alloc='%s' where "
			"job_db_inx=%"PRIu64,
			mysql_conn->cluster_name, job_table,
			step_ptr->job_ptr->tres_alloc_str,
			step_ptr->job_ptr->db_index);
		if (debug_flags & DEBUG_FLAG_DB_STEP)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}

extern int as_mysql_suspend(mysql_conn_t *mysql_conn,
			    uint64_t old_db_inx,
			    struct job_record *job_ptr)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	time_t submit_time;
	uint64_t job_db_inx;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (job_ptr->resize_time)
		submit_time = job_ptr->resize_time;
	else
		submit_time = job_ptr->details->submit_time;

	if (!job_ptr->db_index) {
		if (!(job_ptr->db_index =
		      _get_db_index(mysql_conn,
				    submit_time,
				    job_ptr->job_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if (as_mysql_job_start(
				    mysql_conn, job_ptr) == SLURM_ERROR) {
				error("couldn't suspend job %u",
				      job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	if (IS_JOB_RESIZING(job_ptr)) {
		if (!old_db_inx) {
			error("No old db inx given for job %u cluster %s, "
			      "can't update suspend table.",
			      job_ptr->job_id, mysql_conn->cluster_name);
			return SLURM_ERROR;
		}
		job_db_inx = old_db_inx;
		xstrfmtcat(query,
			   "update \"%s_%s\" set time_end=%d where "
			   "job_db_inx=%"PRIu64" && time_end=0;",
			   mysql_conn->cluster_name, suspend_table,
			   (int)job_ptr->suspend_time, job_db_inx);

	} else
		job_db_inx = job_ptr->db_index;

	/* use job_db_inx for this one since we want to update the
	   supend time of the job before it was resized.
	*/
	xstrfmtcat(query,
		   "update \"%s_%s\" set time_suspended=%d-time_suspended, "
		   "state=%d where job_db_inx=%"PRIu64";",
		   mysql_conn->cluster_name, job_table,
		   (int)job_ptr->suspend_time,
		   job_ptr->job_state & JOB_STATE_BASE,
		   job_db_inx);
	if (IS_JOB_SUSPENDED(job_ptr))
		xstrfmtcat(query,
			   "insert into \"%s_%s\" (job_db_inx, id_assoc, "
			   "time_start, time_end) "
			   "values (%"PRIu64", %u, %d, 0);",
			   mysql_conn->cluster_name, suspend_table,
			   job_ptr->db_index, job_ptr->assoc_id,
			   (int)job_ptr->suspend_time);
	else
		xstrfmtcat(query,
			   "update \"%s_%s\" set time_end=%d where "
			   "job_db_inx=%"PRIu64" && time_end=0;",
			   mysql_conn->cluster_name, suspend_table,
			   (int)job_ptr->suspend_time, job_ptr->db_index);
	if (debug_flags & DEBUG_FLAG_DB_JOB)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);

	rc = mysql_db_query(mysql_conn, query);

	xfree(query);
	if (rc != SLURM_ERROR) {
		xstrfmtcat(query,
			   "update \"%s_%s\" set "
			   "time_suspended=%u-time_suspended, "
			   "state=%d where job_db_inx=%"PRIu64" and time_end=0",
			   mysql_conn->cluster_name, step_table,
			   (int)job_ptr->suspend_time,
			   job_ptr->job_state, job_ptr->db_index);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}

extern int as_mysql_flush_jobs_on_cluster(
	mysql_conn_t *mysql_conn, time_t event_time)
{
	int rc = SLURM_SUCCESS;
	/* put end times for a clean start */
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	char *id_char = NULL;
	char *suspended_char = NULL;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* First we need to get the job_db_inx's and states so we can clean up
	 * the suspend table and the step table
	 */
	query = xstrdup_printf(
		"select distinct t1.job_db_inx, t1.state from \"%s_%s\" "
		"as t1 where t1.time_end=0;",
		mysql_conn->cluster_name, job_table);
	if (debug_flags & DEBUG_FLAG_DB_JOB)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result =
	      mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	while ((row = mysql_fetch_row(result))) {
		int state = slurm_atoul(row[1]);
		if (state == JOB_SUSPENDED) {
			if (suspended_char)
				xstrfmtcat(suspended_char,
					   ", %s", row[0]);
			else
				xstrfmtcat(suspended_char, "job_db_inx in (%s",
					   row[0]);
		}

		if (id_char)
			xstrfmtcat(id_char, ", %s", row[0]);
		else
			xstrfmtcat(id_char, "job_db_inx in (%s", row[0]);
	}
	mysql_free_result(result);

	if (suspended_char) {
		xstrfmtcat(suspended_char, ")");
		xstrfmtcat(query,
			   "update \"%s_%s\" set "
			   "time_suspended=%ld-time_suspended "
			   "where %s;",
			   mysql_conn->cluster_name, job_table,
			   event_time, suspended_char);
		xstrfmtcat(query,
			   "update \"%s_%s\" set "
			   "time_suspended=%ld-time_suspended "
			   "where %s;",
			   mysql_conn->cluster_name, step_table,
			   event_time, suspended_char);
		xstrfmtcat(query,
			   "update \"%s_%s\" set time_end=%ld where (%s) "
			   "&& time_end=0;",
			   mysql_conn->cluster_name, suspend_table,
			   event_time, suspended_char);
		xfree(suspended_char);
	}
	if (id_char) {
		xstrfmtcat(id_char, ")");
		xstrfmtcat(query,
			   "update \"%s_%s\" set state=%d, "
			   "time_end=%ld where %s;",
			   mysql_conn->cluster_name, job_table,
			   JOB_CANCELLED, event_time, id_char);
		xstrfmtcat(query,
			   "update \"%s_%s\" set state=%d, "
			   "time_end=%ld where %s;",
			   mysql_conn->cluster_name, step_table,
			   JOB_CANCELLED, event_time, id_char);
		xfree(id_char);
	}

	if (query) {
		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}
