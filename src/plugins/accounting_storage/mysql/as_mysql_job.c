/*****************************************************************************\
 *  as_mysql_job.c - functions dealing with jobs and job steps.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "as_mysql_job.h"
#include "as_mysql_usage.h"
#include "as_mysql_wckey.h"

#include "src/common/gres.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_jobacct_gather.h"

#define BUFFER_SIZE 4096

/* Used in job functions for getting the database index based off the
 * submit time, job and assoc id.  0 is returned if none is found
 */
static int _get_db_index(mysql_conn_t *mysql_conn,
			 time_t submit, uint32_t jobid, uint32_t associd)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int db_index = 0;
	char *query = xstrdup_printf("select job_db_inx from \"%s_%s\" where "
				     "time_submit=%d and id_job=%u "
				     "and id_assoc=%u",
				     mysql_conn->cluster_name, job_table,
				     (int)submit, jobid, associd);

	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return 0;
	}
	xfree(query);

	row = mysql_fetch_row(result);
	if (!row) {
		mysql_free_result(result);
		debug4("We can't get a db_index for this combo, "
		       "time_submit=%d and id_job=%u and id_assoc=%u.  "
		       "We must not have heard about the start yet, "
		       "no big deal, we will get one right after this.",
		       (int)submit, jobid, associd);
		return 0;
	}
	db_index = slurm_atoul(row[0]);
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

	if ((row = mysql_fetch_row(result)))
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
						   1, NULL) != SLURM_SUCCESS) {
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
					    NULL) != SLURM_SUCCESS) {
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
					    NULL) != SLURM_SUCCESS) {
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
						NULL);

			list_destroy(wckey_list);
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
	int rc=SLURM_SUCCESS;
	char *nodes = NULL, *jname = NULL, *node_inx = NULL;
	int track_steps = 0;
	char *block_id = NULL, *partition = NULL,
		*gres_req = NULL, *gres_alloc = NULL;
	char *query = NULL;
	int reinit = 0;
	time_t begin_time, check_time, start_time, submit_time;
	uint32_t wckeyid = 0;
	int job_state, node_cnt = 0;
	uint32_t job_db_inx = job_ptr->db_index;
	job_array_struct_t *array_recs = job_ptr->array_recs;

	if ((!job_ptr->details || !job_ptr->details->submit_time)
	    && !job_ptr->resize_time) {
		error("as_mysql_job_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug2("as_mysql_slurmdb_job_start() called");

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
							  job_ptr->job_id,
							  job_ptr->assoc_id);
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
			      slurm_ctime(&check_time),
			      job_ptr->job_id, mysql_conn->cluster_name);
		else if (begin_time)
			debug("Need to reroll usage from %s Job %u "
			      "from %s became eligible then and we are just "
			      "now hearing about it.",
			      slurm_ctime(&check_time),
			      job_ptr->job_id, mysql_conn->cluster_name);
		else
			debug("Need to reroll usage from %s Job %u "
			      "from %s was submitted then and we are just "
			      "now hearing about it.",
			      slurm_ctime(&check_time),
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
		jname = slurm_add_slash_to_quotes(job_ptr->name);
	else {
		jname = xstrdup("allocation");
		track_steps = 1;
	}

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "None assigned";

	if (job_ptr->batch_flag)
		track_steps = 1;

	if (slurmdbd_conf) {
		block_id = xstrdup(job_ptr->comment);
		node_cnt = job_ptr->total_nodes;
		node_inx = job_ptr->network;
	} else {
		char temp_bit[BUF_SIZE];

		if (job_ptr->node_bitmap) {
			node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					   job_ptr->node_bitmap);
		}
#ifdef HAVE_BG
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_BLOCK_ID,
					    &block_id);
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &node_cnt);
#else
		node_cnt = job_ptr->total_nodes;
#endif
	}

	/* If there is a start_time get the wckeyid.  If the job is
	 * cancelled before the job starts we also want to grab it. */
	if (job_ptr->assoc_id
	    && (job_ptr->start_time || IS_JOB_CANCELLED(job_ptr)))
		wckeyid = _get_wckeyid(mysql_conn, &job_ptr->wckey,
				       job_ptr->user_id,
				       mysql_conn->cluster_name,
				       job_ptr->assoc_id);

	if (!IS_JOB_PENDING(job_ptr) && job_ptr->part_ptr)
		partition = slurm_add_slash_to_quotes(job_ptr->part_ptr->name);
	else if (job_ptr->partition)
		partition = slurm_add_slash_to_quotes(job_ptr->partition);

	if (job_ptr->gres_req)
		gres_req = slurm_add_slash_to_quotes(job_ptr->gres_req);

	if (job_ptr->gres_alloc)
		gres_alloc = slurm_add_slash_to_quotes(job_ptr->gres_alloc);

	if (!job_ptr->db_index) {
		if (!begin_time)
			begin_time = submit_time;
		query = xstrdup_printf(
			"insert into \"%s_%s\" "
			"(id_job, id_array_job, id_array_task, "
			"id_assoc, id_qos, id_wckey, id_user, "
			"id_group, nodelist, id_resv, timelimit, "
			"time_eligible, time_submit, time_start, "
			"job_name, track_steps, state, priority, cpus_req, "
			"cpus_alloc, nodes_alloc, mem_req",
			mysql_conn->cluster_name, job_table);

		if (job_ptr->account)
			xstrcat(query, ", account");
		if (partition)
			xstrcat(query, ", `partition`");
		if (block_id)
			xstrcat(query, ", id_block");
		if (job_ptr->wckey)
			xstrcat(query, ", wckey");
		if (node_inx)
			xstrcat(query, ", node_inx");
		if (gres_req)
			xstrcat(query, ", gres_req");
		if (gres_alloc)
			xstrcat(query, ", gres_alloc");
		if (array_recs && array_recs->task_id_str)
			xstrcat(query, ", array_task_str, array_max_tasks, "
				"array_task_pending");
		else
			xstrcat(query, ", array_task_str, array_task_pending");

		xstrfmtcat(query,
			   ") values (%u, %u, %u, %u, %u, %u, %u, %u, "
			   "'%s', %u, %u, %ld, %ld, %ld, "
			   "'%s', %u, %u, %u, %u, %u, %u, %u",
			   job_ptr->job_id, job_ptr->array_job_id,
			   job_ptr->array_task_id, job_ptr->assoc_id,
			   job_ptr->qos_id, wckeyid,
			   job_ptr->user_id, job_ptr->group_id, nodes,
			   job_ptr->resv_id, job_ptr->time_limit,
			   begin_time, submit_time, start_time,
			   jname, track_steps, job_state,
			   job_ptr->priority, job_ptr->details->min_cpus,
			   job_ptr->total_cpus, node_cnt,
			   job_ptr->details->pn_min_memory);

		if (job_ptr->account)
			xstrfmtcat(query, ", '%s'", job_ptr->account);
		if (partition)
			xstrfmtcat(query, ", '%s'", partition);
		if (block_id)
			xstrfmtcat(query, ", '%s'", block_id);
		if (job_ptr->wckey)
			xstrfmtcat(query, ", '%s'", job_ptr->wckey);
		if (node_inx)
			xstrfmtcat(query, ", '%s'", node_inx);
		if (gres_req)
			xstrfmtcat(query, ", '%s'", gres_req);
		if (gres_alloc)
			xstrfmtcat(query, ", '%s'", gres_alloc);
		if (array_recs && array_recs->task_id_str)
			xstrfmtcat(query, ", '%s', %u, %u",
				   array_recs->task_id_str,
				   array_recs->max_run_tasks,
				   array_recs->task_cnt);
		else
			xstrcat(query, ", NULL, 0");

		xstrfmtcat(query,
			   ") on duplicate key update "
			   "job_db_inx=LAST_INSERT_ID(job_db_inx), "
			   "id_wckey=%u, id_user=%u, id_group=%u, "
			   "nodelist='%s', id_resv=%u, timelimit=%u, "
			   "time_submit=%ld, time_eligible=%ld, "
			   "time_start=%ld, "
			   "job_name='%s', track_steps=%u, id_qos=%u, "
			   "state=greatest(state, %u), priority=%u, "
			   "cpus_req=%u, cpus_alloc=%u, nodes_alloc=%u, "
			   "mem_req=%u, id_array_job=%u, id_array_task=%u",
			   wckeyid, job_ptr->user_id, job_ptr->group_id, nodes,
			   job_ptr->resv_id, job_ptr->time_limit,
			   submit_time, begin_time, start_time,
			   jname, track_steps, job_ptr->qos_id, job_state,
			   job_ptr->priority, job_ptr->details->min_cpus,
			   job_ptr->total_cpus, node_cnt,
			   job_ptr->details->pn_min_memory,
			   job_ptr->array_job_id,
			   job_ptr->array_task_id);

		if (job_ptr->account)
			xstrfmtcat(query, ", account='%s'", job_ptr->account);
		if (partition)
			xstrfmtcat(query, ", `partition`='%s'", partition);
		if (block_id)
			xstrfmtcat(query, ", id_block='%s'", block_id);
		if (job_ptr->wckey)
			xstrfmtcat(query, ", wckey='%s'", job_ptr->wckey);
		if (node_inx)
			xstrfmtcat(query, ", node_inx='%s'", node_inx);
		if (gres_req)
			xstrfmtcat(query, ", gres_req='%s'", gres_req);
		if (gres_alloc)
			xstrfmtcat(query, ", gres_alloc='%s'", gres_alloc);
		if (array_recs && array_recs->task_id_str)
			xstrfmtcat(query, ", array_task_str='%s', "
				   "array_max_tasks=%u, array_task_pending=%u",
				   array_recs->task_id_str,
				   array_recs->max_run_tasks,
				   array_recs->task_cnt);
		else
			xstrfmtcat(query, ", array_task_str=NULL, "
				   "array_task_pending=0");

		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	try_again:
		if (!(job_ptr->db_index = mysql_db_insert_ret_id(
			      mysql_conn, query))) {
			if (!reinit) {
				error("It looks like the storage has gone "
				      "away trying to reconnect");
				mysql_db_close_db_connection(
					mysql_conn);
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

		if (job_ptr->account)
			xstrfmtcat(query, "account='%s', ", job_ptr->account);
		if (partition)
			xstrfmtcat(query, "`partition`='%s', ", partition);
		if (block_id)
			xstrfmtcat(query, "id_block='%s', ", block_id);
		if (job_ptr->wckey)
			xstrfmtcat(query, "wckey='%s', ", job_ptr->wckey);
		if (node_inx)
			xstrfmtcat(query, "node_inx='%s', ", node_inx);
		if (gres_req)
			xstrfmtcat(query, "gres_req='%s', ", gres_req);
		if (gres_alloc)
			xstrfmtcat(query, "gres_alloc='%s', ", gres_alloc);
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

		xstrfmtcat(query, "time_start=%ld, job_name='%s', state=%u, "
			   "cpus_alloc=%u, nodes_alloc=%u, id_qos=%u, "
			   "id_assoc=%u, id_wckey=%u, id_resv=%u, "
			   "timelimit=%u, mem_req=%u, "
			   "id_array_job=%u, id_array_task=%u, "
			   "time_eligible=%ld where job_db_inx=%d",
			   start_time, jname, job_state,
			   job_ptr->total_cpus, node_cnt, job_ptr->qos_id,
			   job_ptr->assoc_id, wckeyid,
			   job_ptr->resv_id, job_ptr->time_limit,
			   job_ptr->details->pn_min_memory,
			   job_ptr->array_job_id,
			   job_ptr->array_task_id,
			   begin_time, job_ptr->db_index);

		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
	}

	xfree(block_id);
	xfree(partition);
	xfree(gres_req);
	xfree(gres_alloc);
	xfree(jname);
	xfree(query);

	/* now we will reset all the steps */
	if (IS_JOB_RESIZING(job_ptr)) {
		/* FIXME : Verify this is still needed */
		if (IS_JOB_SUSPENDED(job_ptr))
			as_mysql_suspend(mysql_conn, job_db_inx, job_ptr);
	}

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

	if (job->derived_es) {
		char *derived_es = slurm_add_slash_to_quotes(job->derived_es);
		xstrfmtcat(vals, ", derived_es='%s'", derived_es);
		xfree(derived_es);
	}
	if (!vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("No change specified for job modification");
		return NULL;
	}

	/* Here we want to get the last job submitted here */
	query = xstrdup_printf("select job_db_inx, id_job, time_submit, "
			       "id_user "
			       "from \"%s_%s\" where deleted=0 "
			       "&& id_job=%u "
			       "order by time_submit desc limit 1;",
			       job_cond->cluster, job_table,
			       job_cond->job_id);

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
		list_destroy(ret_list);
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

	if (!job_ptr->db_index
	    && ((!job_ptr->details || !job_ptr->details->submit_time)
		&& !job_ptr->resize_time)) {
		error("as_mysql_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug2("as_mysql_slurmdb_job_complete() called");

	if (job_ptr->resize_time)
		submit_time = job_ptr->resize_time;
	else
		submit_time = job_ptr->details->submit_time;

	if (IS_JOB_RESIZING(job_ptr)) {
		end_time = job_ptr->resize_time;
		job_state = JOB_RESIZING;
	} else {
		/* If we get an error with this just fall through to avoid an
		 * infinite loop */
		if (job_ptr->end_time == 0) {
			debug("as_mysql_jobacct: job %u never started",
			      job_ptr->job_id);
			return SLURM_SUCCESS;
		}
		end_time = job_ptr->end_time;

		if (IS_JOB_REQUEUED(job_ptr))
			job_state = JOB_REQUEUE;
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
				    job_ptr->job_id,
				    job_ptr->assoc_id))) {
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
			       "time_end=%ld, state=%d",
			       mysql_conn->cluster_name, job_table,
			       end_time, job_state);

	if (job_ptr->derived_ec != NO_VAL)
		xstrfmtcat(query, ", derived_ec=%u", job_ptr->derived_ec);

	if (job_ptr->comment) {
		char *comment = slurm_add_slash_to_quotes(job_ptr->comment);
		xstrfmtcat(query, ", derived_es='%s'", comment);
		xfree(comment);
	}

	exit_code = job_ptr->exit_code;
	if (exit_code == 1) {
		/* This wasn't signalled, it was set by Slurm so don't
		 * treat it like a signal.
		 */
		exit_code = 256;
	}

	xstrfmtcat(query,
		   ", exit_code=%d, kill_requid=%d where job_db_inx=%d;",
		   exit_code, job_ptr->requid,
		   job_ptr->db_index);

	if (debug_flags & DEBUG_FLAG_DB_JOB)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	return rc;
}

extern int as_mysql_step_start(mysql_conn_t *mysql_conn,
			       struct step_record *step_ptr)
{
	int cpus = 0, tasks = 0, nodes = 0, task_dist = 0;
	int rc=SLURM_SUCCESS;
	char node_list[BUFFER_SIZE];
	char *node_inx = NULL, *step_name = NULL;
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
		cpus = step_ptr->cpu_count;
		if (step_ptr->job_ptr->details)
			tasks = step_ptr->job_ptr->details->num_tasks;
		else
			tasks = cpus;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
		nodes = step_ptr->step_layout->node_cnt;
		task_dist = step_ptr->step_layout->task_dist;
		node_inx = step_ptr->network;
	} else if (step_ptr->step_id == SLURM_BATCH_SCRIPT) {
		char temp_bit[BUF_SIZE];

		if (step_ptr->step_node_bitmap) {
			node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					   step_ptr->step_node_bitmap);
		}
		/* We overload gres with the node name of where the
		   script was running.
		*/
		snprintf(node_list, BUFFER_SIZE, "%s", step_ptr->gres);
		nodes = cpus = tasks = 1;
	} else {
		char *ionodes = NULL, *temp_nodes = NULL;
		char temp_bit[BUF_SIZE];

		if (step_ptr->step_node_bitmap) {
			node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					   step_ptr->step_node_bitmap);
		}
#ifdef HAVE_BG_L_P
		/* Only L and P use this code */
		if (step_ptr->job_ptr->details)
			tasks = cpus = step_ptr->job_ptr->details->min_cpus;
		else
			tasks = cpus = step_ptr->job_ptr->cpu_cnt;
		select_g_select_jobinfo_get(step_ptr->job_ptr->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &nodes);
		temp_nodes = step_ptr->job_ptr->nodes;
#else
		if (!step_ptr->step_layout
		    || !step_ptr->step_layout->task_cnt) {
			tasks = cpus = step_ptr->job_ptr->total_cpus;
			nodes = step_ptr->job_ptr->total_nodes;
			temp_nodes = step_ptr->job_ptr->nodes;
		} else {
			cpus = step_ptr->cpu_count;
			tasks = step_ptr->step_layout->task_cnt;
#ifdef HAVE_BGQ
			select_g_select_jobinfo_get(step_ptr->select_jobinfo,
						    SELECT_JOBDATA_NODE_CNT,
						    &nodes);
#else
			nodes = step_ptr->step_layout->node_cnt;
#endif
			task_dist = step_ptr->step_layout->task_dist;
			temp_nodes = step_ptr->step_layout->node_list;
		}
#endif
		select_g_select_jobinfo_get(step_ptr->select_jobinfo,
					    SELECT_JOBDATA_IONODES,
					    &ionodes);
		if (ionodes) {
			snprintf(node_list, BUFFER_SIZE, "%s[%s]",
				 temp_nodes, ionodes);
			xfree(ionodes);
		} else
			snprintf(node_list, BUFFER_SIZE, "%s", temp_nodes);
	}

	if (!step_ptr->job_ptr->db_index) {
		if (!(step_ptr->job_ptr->db_index =
		      _get_db_index(mysql_conn,
				    submit_time,
				    step_ptr->job_ptr->job_id,
				    step_ptr->job_ptr->assoc_id))) {
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

	step_name = slurm_add_slash_to_quotes(step_ptr->name);

	/* we want to print a -1 for the requid so leave it a
	   %d */
	/* The stepid could be -2 so use %d not %u */
	query = xstrdup_printf(
		"insert into \"%s_%s\" (job_db_inx, id_step, time_start, "
		"step_name, state, "
		"cpus_alloc, nodes_alloc, task_cnt, nodelist, "
		"node_inx, task_dist, req_cpufreq) "
		"values (%d, %d, %d, '%s', %d, %d, %d, %d, "
		"'%s', '%s', %d, %u) "
		"on duplicate key update cpus_alloc=%d, nodes_alloc=%d, "
		"task_cnt=%d, time_end=0, state=%d, "
		"nodelist='%s', node_inx='%s', task_dist=%d, req_cpufreq=%u",
		mysql_conn->cluster_name, step_table,
		step_ptr->job_ptr->db_index,
		step_ptr->step_id,
		(int)start_time, step_name,
		JOB_RUNNING, cpus, nodes, tasks, node_list, node_inx, task_dist,
		step_ptr->cpu_freq, cpus, nodes, tasks, JOB_RUNNING,
		node_list, node_inx, task_dist, step_ptr->cpu_freq);
	if (debug_flags & DEBUG_FLAG_DB_STEP)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	xfree(step_name);

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
#ifdef HAVE_BG_L_P
		/* Only L and P use this code */
		tasks = step_ptr->job_ptr->details->min_cpus;
#else
		if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
			tasks = step_ptr->job_ptr->total_cpus;
		else
			tasks = step_ptr->step_layout->task_cnt;
#endif
	}

	exit_code = step_ptr->exit_code;
	comp_status = step_ptr->state;
	if (comp_status < JOB_COMPLETE) {
		if (WIFSIGNALED(exit_code)) {
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
				    step_ptr->job_ptr->job_id,
				    step_ptr->job_ptr->assoc_id))) {
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
		double ave_vsize = NO_VAL, ave_rss = NO_VAL, ave_pages = NO_VAL;
		double ave_disk_read =  (double)NO_VAL;
		double ave_disk_write = (double)NO_VAL;
		double ave_cpu = (double)NO_VAL;
		/* figure out the ave of the totals sent */
		if (tasks > 0) {
			ave_vsize = (double)jobacct->tot_vsize;
			ave_vsize /= (double)tasks;
			ave_rss = (double)jobacct->tot_rss;
			ave_rss /= (double)tasks;
			ave_pages = (double)jobacct->tot_pages;
			ave_pages /= (double)tasks;
			ave_cpu = (double)jobacct->tot_cpu;
			ave_cpu /= (double)tasks;
			ave_disk_read = (double)jobacct->tot_disk_read;
			ave_disk_read /= (double)tasks;
			ave_disk_write = (double)jobacct->tot_disk_write;
			ave_disk_write /= (double)tasks;
		}

		xstrfmtcat(query,
			   ", user_sec=%u, user_usec=%u, "
			   "sys_sec=%u, sys_usec=%u, "
			   "max_disk_read=%f, max_disk_read_task=%u, "
			   "max_disk_read_node=%u, ave_disk_read=%f, "
			   "max_disk_write=%f, max_disk_write_task=%u, "
			   "max_disk_write_node=%u, ave_disk_write=%f, "
			   "max_vsize=%"PRIu64", max_vsize_task=%u, "
			   "max_vsize_node=%u, ave_vsize=%f, "
			   "max_rss=%"PRIu64", max_rss_task=%u, "
			   "max_rss_node=%u, ave_rss=%f, "
			   "max_pages=%"PRIu64", max_pages_task=%u, "
			   "max_pages_node=%u, ave_pages=%f, "
			   "min_cpu=%u, min_cpu_task=%u, "
			   "min_cpu_node=%u, ave_cpu=%f, "
			   "act_cpufreq=%u, consumed_energy=%u",
			   /* user seconds */
			   jobacct->user_cpu_sec,
			   /* user microseconds */
			   jobacct->user_cpu_usec,
			   /* system seconds */
			   jobacct->sys_cpu_sec,
			   /* system microsecs */
			   jobacct->sys_cpu_usec,
			   /* max disk_read */
			   jobacct->max_disk_read,
			   /* max disk_read task */
			   jobacct->max_disk_read_id.taskid,
			   /* max disk_read node */
			   jobacct->max_disk_read_id.nodeid,
			   /* ave disk_read */
			   ave_disk_read,
			   /* max disk_write */
			   jobacct->max_disk_write,
			   /* max disk_write task */
			   jobacct->max_disk_write_id.taskid,
			   /* max disk_write node */
			   jobacct->max_disk_write_id.nodeid,
			   /* ave disk_write */
			   ave_disk_write,
			   jobacct->max_vsize,	/* max vsize */
			   jobacct->max_vsize_id.taskid, /* max vsize task */
			   jobacct->max_vsize_id.nodeid, /* max vsize node */
			   ave_vsize,	/* ave vsize */
			   jobacct->max_rss,	/* max vsize */
			   jobacct->max_rss_id.taskid,	/* max rss task */
			   jobacct->max_rss_id.nodeid,	/* max rss node */
			   ave_rss,	/* ave rss */
			   jobacct->max_pages,	/* max pages */
			   jobacct->max_pages_id.taskid, /* max pages task */
			   jobacct->max_pages_id.nodeid, /* max pages node */
			   ave_pages,	/* ave pages */
			   jobacct->min_cpu,	/* min cpu */
			   jobacct->min_cpu_id.taskid,	/* min cpu task */
			   jobacct->min_cpu_id.nodeid,	/* min cpu node */
			   ave_cpu,	/* ave cpu */
			   jobacct->act_cpufreq,
			   jobacct->energy.consumed_energy);
	}

	xstrfmtcat(query,
		   " where job_db_inx=%d and id_step=%d",
		   step_ptr->job_ptr->db_index, step_ptr->step_id);
	if (debug_flags & DEBUG_FLAG_DB_STEP)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	return rc;
}

extern int as_mysql_suspend(mysql_conn_t *mysql_conn,
			    uint32_t old_db_inx,
			    struct job_record *job_ptr)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	time_t submit_time;
	uint32_t job_db_inx;

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
				    job_ptr->job_id,
				    job_ptr->assoc_id))) {
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
			   "job_db_inx=%u && time_end=0;",
			   mysql_conn->cluster_name, suspend_table,
			   (int)job_ptr->suspend_time, job_db_inx);

	} else
		job_db_inx = job_ptr->db_index;

	/* use job_db_inx for this one since we want to update the
	   supend time of the job before it was resized.
	*/
	xstrfmtcat(query,
		   "update \"%s_%s\" set time_suspended=%d-time_suspended, "
		   "state=%d where job_db_inx=%d;",
		   mysql_conn->cluster_name, job_table,
		   (int)job_ptr->suspend_time,
		   job_ptr->job_state & JOB_STATE_BASE,
		   job_db_inx);
	if (IS_JOB_SUSPENDED(job_ptr))
		xstrfmtcat(query,
			   "insert into \"%s_%s\" (job_db_inx, id_assoc, "
			   "time_start, time_end) values (%u, %u, %d, 0);",
			   mysql_conn->cluster_name, suspend_table,
			   job_ptr->db_index, job_ptr->assoc_id,
			   (int)job_ptr->suspend_time);
	else
		xstrfmtcat(query,
			   "update \"%s_%s\" set time_end=%d where "
			   "job_db_inx=%u && time_end=0;",
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
			   "state=%d where job_db_inx=%u and time_end=0",
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
					   " || job_db_inx=%s", row[0]);
			else
				xstrfmtcat(suspended_char, "job_db_inx=%s",
					   row[0]);
		}

		if (id_char)
			xstrfmtcat(id_char, " || job_db_inx=%s", row[0]);
		else
			xstrfmtcat(id_char, "job_db_inx=%s", row[0]);
	}
	mysql_free_result(result);

	if (suspended_char) {
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
