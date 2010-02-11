/*****************************************************************************\
 *  mysql_job.c - functions dealing with jobs and job steps.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include "mysql_job.h"
#include "mysql_usage.h"
#include "mysql_wckey.h"

#include "src/common/jobacct_common.h"

/* Used in job functions for getting the database index based off the
 * submit time, job and assoc id.  0 is returned if none is found
 */
static int _get_db_index(mysql_conn_t *mysql_conn,
			 time_t submit, uint32_t jobid, uint32_t associd)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int db_index = 0;
	char *query = xstrdup_printf("select job_db_inx from %s_%s where "
				     "submit=%d and jobid=%u and associd=%u",
				     mysql_conn->cluster_name, job_table,
				     (int)submit, jobid, associd);

	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return 0;
	}
	xfree(query);

	row = mysql_fetch_row(result);
	if(!row) {
		mysql_free_result(result);
		error("We can't get a db_index for this combo, "
		      "submit=%d and jobid=%u and associd=%u.",
		      (int)submit, jobid, associd);
		return 0;
	}
	db_index = atoi(row[0]);
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
	query = xstrdup_printf("select user from %s_%s where id_assoc=%u",
			       cluster, assoc_table, associd);

	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	if((row = mysql_fetch_row(result)))
		user = xstrdup(row[0]);

	mysql_free_result(result);

	return user;
}

static uint32_t _get_wckeyid(mysql_conn_t *mysql_conn, char **name,
			     uid_t uid, char *cluster, uint32_t associd)
{
	uint32_t wckeyid = 0;

	if(slurm_get_track_wckey()) {
		/* Here we are looking for the wckeyid if it doesn't
		 * exist we will create one.  We don't need to check
		 * if it is good or not.  Right now this is the only
		 * place things are created. We do this only on a job
		 * start, not on a job submit since we don't want to
		 * slow down getting the db_index back to the
		 * controller.
		 */
		acct_wckey_rec_t wckey_rec;
		char *user = NULL;

		/* since we are unable to rely on uids here (someone could
		   not have there uid in the system yet) we must
		   first get the user name from the associd */
		if(!(user = _get_user_from_associd(
			     mysql_conn, cluster, associd))) {
			error("No user for associd %u", associd);
			goto no_wckeyid;
		}
		/* get the default key */
		if(!*name) {
			acct_user_rec_t user_rec;
			memset(&user_rec, 0, sizeof(acct_user_rec_t));
			user_rec.uid = NO_VAL;
			user_rec.name = user;
			if(assoc_mgr_fill_in_user(mysql_conn, &user_rec,
						  1, NULL) != SLURM_SUCCESS) {
				error("No user by name of %s assoc %u",
				      user, associd);
				xfree(user);
				goto no_wckeyid;
			}

			if(user_rec.default_wckey)
				*name = xstrdup_printf("*%s",
						       user_rec.default_wckey);
			else
				*name = xstrdup_printf("*");
		}

		memset(&wckey_rec, 0, sizeof(acct_wckey_rec_t));
		wckey_rec.name = (*name);
		wckey_rec.uid = NO_VAL;
		wckey_rec.user = user;
		wckey_rec.cluster = cluster;
		if(assoc_mgr_fill_in_wckey(mysql_conn, &wckey_rec,
					   ACCOUNTING_ENFORCE_WCKEYS,
					   NULL) != SLURM_SUCCESS) {
			List wckey_list = NULL;
			acct_wckey_rec_t *wckey_ptr = NULL;

			wckey_list = list_create(destroy_acct_wckey_rec);

			wckey_ptr = xmalloc(sizeof(acct_wckey_rec_t));
			wckey_ptr->name = xstrdup((*name));
			wckey_ptr->user = xstrdup(user);
			wckey_ptr->cluster = xstrdup(cluster);
			list_append(wckey_list, wckey_ptr);
			/* info("adding wckey '%s' '%s' '%s'", */
			/* 	     wckey_ptr->name, wckey_ptr->user, */
			/* 	     wckey_ptr->cluster); */
			/* we have already checked to make
			   sure this was the slurm user before
			   calling this */
			if(mysql_add_wckeys(mysql_conn,
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

extern int mysql_job_start(mysql_conn_t *mysql_conn,
			   struct job_record *job_ptr)
{
	int rc=SLURM_SUCCESS;
	char *nodes = NULL, *jname = NULL, *node_inx = NULL;
	int track_steps = 0;
	char *block_id = NULL;
	char *query = NULL;
	int reinit = 0;
	time_t check_time = job_ptr->start_time;
	uint32_t wckeyid = 0;
	int node_cnt = 0;

	if (!job_ptr->details || !job_ptr->details->submit_time) {
		error("mysql_job_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug2("mysql_jobacct_job_start() called");

	/* See what we are hearing about here if no start time. If
	 * this job latest time is before the last roll up we will
	 * need to reset it to look at this job. */
	if(!check_time) {
		check_time = job_ptr->details->begin_time;

		if(!check_time)
			check_time = job_ptr->details->submit_time;
	}

	slurm_mutex_lock(&rollup_lock);
	if(check_time < global_last_rollup) {
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;

		/* check to see if we are hearing about this time for the
		 * first time.
		 */
		query = xstrdup_printf("select job_db_inx "
				       "from %s_%s where id_job=%u and "
				       "time_submit=%d and time_eligible=%d "
				       "and time_start=%d;",
				       mysql_conn->cluster_name,
				       job_table, job_ptr->job_id,
				       job_ptr->details->submit_time,
				       job_ptr->details->begin_time,
				       job_ptr->start_time);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		if(!(result =
		     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
			xfree(query);
			slurm_mutex_unlock(&rollup_lock);
			return SLURM_ERROR;
		}
		xfree(query);
		if((row = mysql_fetch_row(result))) {
			mysql_free_result(result);
			debug4("revieved an update for a "
			       "job (%u) already known about",
			       job_ptr->job_id);
			slurm_mutex_unlock(&rollup_lock);
			goto no_rollup_change;
		}
		mysql_free_result(result);

		if(job_ptr->start_time)
			debug("Need to reroll usage from %sJob %u "
			      "from %s started then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, mysql_conn->cluster_name);
		else if(job_ptr->details->begin_time)
			debug("Need to reroll usage from %sJob %u "
			      "from %s became eligible then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, mysql_conn->cluster_name);
		else
			debug("Need to reroll usage from %sJob %u "
			      "from %s was submitted then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, mysql_conn->cluster_name);

		global_last_rollup = check_time;
		slurm_mutex_unlock(&rollup_lock);

		query = xstrdup_printf("update %s set hourly_rollup=%d, "
				       "daily_rollup=%d, monthly_rollup=%d",
				       last_ran_table, check_time,
				       check_time, check_time);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
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

	if(job_ptr->batch_flag)
		track_steps = 1;

	if(slurmdbd_conf) {
		block_id = xstrdup(job_ptr->comment);
		node_cnt = job_ptr->node_cnt;
		node_inx = job_ptr->network;
	} else {
		char temp_bit[BUF_SIZE];

		if(job_ptr->node_bitmap) {
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
		node_cnt = job_ptr->node_cnt;
#endif
	}

	/* If there is a start_time get the wckeyid.  If the job is
	 * cancelled before the job starts we also want to grab it. */
	if(job_ptr->assoc_id
	   && (job_ptr->start_time || IS_JOB_CANCELLED(job_ptr)))
		wckeyid = _get_wckeyid(mysql_conn, &job_ptr->wckey,
				       job_ptr->user_id,
				       mysql_conn->cluster_name,
				       job_ptr->assoc_id);


	/* We need to put a 0 for 'end' incase of funky job state
	 * files from a hot start of the controllers we call
	 * job_start on jobs we may still know about after
	 * job_flush has been called so we need to restart
	 * them by zeroing out the end.
	 */
	if(!job_ptr->db_index) {
		if(!job_ptr->details->begin_time)
			job_ptr->details->begin_time =
				job_ptr->details->submit_time;
		query = xstrdup_printf(
			"insert into %s_%s "
			"(id_job, id_assoc, id_wckey, id_user, "
			"id_user, nodelist, id_resv, timelimit, ",
			mysql_conn->cluster_name, job_table);

		if(job_ptr->account)
			xstrcat(query, "account, ");
		if(job_ptr->partition)
			xstrcat(query, "partition, ");
		if(block_id)
			xstrcat(query, "id_block, ");
		if(job_ptr->wckey)
			xstrcat(query, "wckey, ");
		if(node_inx)
			xstrcat(query, "node_inx, ");

		xstrfmtcat(query,
			   "time_eligible, time_submit, time_start, "
			   "job_name, track_steps, "
			   "state, priority, cpus_req, "
			   "cpus_alloc, nodes_alloc) "
			   "values (%u, %u, %u, %u, %u, \"%s\", %u, %u, ",
			   job_ptr->job_id, job_ptr->assoc_id, wckeyid,
			   job_ptr->user_id, job_ptr->group_id, nodes,
			   job_ptr->resv_id, job_ptr->time_limit);

		if(job_ptr->account)
			xstrfmtcat(query, "\"%s\", ", job_ptr->account);
		if(job_ptr->partition)
			xstrfmtcat(query, "\"%s\", ", job_ptr->partition);
		if(block_id)
			xstrfmtcat(query, "\"%s\", ", block_id);
		if(job_ptr->wckey)
			xstrfmtcat(query, "\"%s\", ", job_ptr->wckey);
		if(node_inx)
			xstrfmtcat(query, "\"%s\", ", node_inx);

		xstrfmtcat(query,
			   "%d, %d, %d, \"%s\", %u, %u, %u, %u, %u, %u) "
			   "on duplicate key update "
			   "job_db_inx=LAST_INSERT_ID(job_db_inx), state=%u, "
			   "id_assoc=%u, id_wckey=%u, id_resv=%u, timelimit=%u",
			   (int)job_ptr->details->begin_time,
			   (int)job_ptr->details->submit_time,
			   (int)job_ptr->start_time,
			   jname, track_steps,
			   job_ptr->job_state & JOB_STATE_BASE,
			   job_ptr->priority, job_ptr->details->min_cpus,
			   job_ptr->total_cpus, node_cnt,
			   job_ptr->job_state & JOB_STATE_BASE,
			   job_ptr->assoc_id, wckeyid, job_ptr->resv_id,
			   job_ptr->time_limit);

		if(job_ptr->account)
			xstrfmtcat(query, ", account=\"%s\"", job_ptr->account);
		if(job_ptr->partition)
			xstrfmtcat(query, ", partition=\"%s\"",
				   job_ptr->partition);
		if(block_id)
			xstrfmtcat(query, ", id_block=\"%s\"", block_id);
		if(job_ptr->wckey)
			xstrfmtcat(query, ", wckey=\"%s\"", job_ptr->wckey);
		if(node_inx)
			xstrfmtcat(query, ", node_inx=\"%s\"", node_inx);

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
	try_again:
		if(!(job_ptr->db_index = mysql_insert_ret_id(
			     mysql_conn->db_conn, query))) {
			if(!reinit) {
				error("It looks like the storage has gone "
				      "away trying to reconnect");
				mysql_close_db_connection(
					&mysql_conn->db_conn);
				/* reconnect */
				check_connection(mysql_conn);
				reinit = 1;
				goto try_again;
			} else
				rc = SLURM_ERROR;
		}
	} else {
		query = xstrdup_printf("update %s_%s set nodelist=\"%s\", ",
				       mysql_conn->cluster_name,
				       job_table, nodes);

		if(job_ptr->account)
			xstrfmtcat(query, "account=\"%s\", ", job_ptr->account);
		if(job_ptr->partition)
			xstrfmtcat(query, "partition=\"%s\", ",
				   job_ptr->partition);
		if(block_id)
			xstrfmtcat(query, "blockid=\"%s\", ", block_id);
		if(job_ptr->wckey)
			xstrfmtcat(query, "wckey=\"%s\", ", job_ptr->wckey);
		if(node_inx)
			xstrfmtcat(query, "node_inx=\"%s\", ", node_inx);

		xstrfmtcat(query, "start=%d, name=\"%s\", state=%u, "
			   "alloc_cpus=%u, alloc_nodes=%u, "
			   "associd=%u, wckeyid=%u, resvid=%u, timelimit=%u "
			   "where job_db_inx=%d",
			   (int)job_ptr->start_time,
			   jname, job_ptr->job_state & JOB_STATE_BASE,
			   job_ptr->total_cpus, node_cnt,
			   job_ptr->assoc_id, wckeyid,
			   job_ptr->resv_id, job_ptr->time_limit,
			   job_ptr->db_index);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
	}

	xfree(block_id);
	xfree(query);

	return rc;
}

extern int mysql_job_complete(mysql_conn_t *mysql_conn,
			      struct job_record *job_ptr)
{
	char *query = NULL, *nodes = NULL;
	int rc=SLURM_SUCCESS;
	time_t start_time = job_ptr->start_time;

	if (!job_ptr->db_index
	    && (!job_ptr->details || !job_ptr->details->submit_time)) {
		error("mysql_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;
	debug2("mysql_jobacct_job_complete() called");

	/* If we get an error with this just fall through to avoid an
	 * infinite loop
	 */
	if (job_ptr->end_time == 0) {
		debug("mysql_jobacct: job %u never started", job_ptr->job_id);
		return SLURM_SUCCESS;
	} else if(start_time > job_ptr->end_time)
		start_time = 0;

	slurm_mutex_lock(&rollup_lock);
	if(job_ptr->end_time < global_last_rollup) {
		global_last_rollup = job_ptr->end_time;
		slurm_mutex_unlock(&rollup_lock);

		query = xstrdup_printf("update %s set hourly_rollup=%d, "
				       "daily_rollup=%d, monthly_rollup=%d",
				       last_ran_table, job_ptr->end_time,
				       job_ptr->end_time, job_ptr->end_time);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	} else
		slurm_mutex_unlock(&rollup_lock);

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "None assigned";

	if(!job_ptr->db_index) {
		if(!(job_ptr->db_index =
		     _get_db_index(mysql_conn,
				   job_ptr->details->submit_time,
				   job_ptr->job_id,
				   job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(mysql_job_start(
				   mysql_conn, job_ptr) == SLURM_ERROR) {
				error("couldn't add job %u at job completion",
				      job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	query = xstrdup_printf("update %s set start=%d, end=%d, state=%d, "
			       "nodelist=\"%s\", comp_code=%d, "
			       "kill_requid=%d where job_db_inx=%d",
			       job_table, (int)start_time,
			       (int)job_ptr->end_time,
			       job_ptr->job_state & JOB_STATE_BASE,
			       nodes, job_ptr->exit_code,
			       job_ptr->requid, job_ptr->db_index);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	return rc;
}

extern int mysql_step_start(mysql_conn_t *mysql_conn,
			    struct step_record *step_ptr)
{
	int cpus = 0, tasks = 0, nodes = 0, task_dist = 0;
	int rc=SLURM_SUCCESS;
	char node_list[BUFFER_SIZE];
	char *node_inx = NULL;
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	char *query = NULL;

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("mysql_step_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;
	if(slurmdbd_conf) {
		tasks = step_ptr->job_ptr->details->num_tasks;
		cpus = step_ptr->cpu_count;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
		nodes = step_ptr->step_layout->node_cnt;
		task_dist = step_ptr->step_layout->task_dist;
		node_inx = step_ptr->network;
	} else {
		char temp_bit[BUF_SIZE];

		if(step_ptr->step_node_bitmap) {
			node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					   step_ptr->step_node_bitmap);
		}
#ifdef HAVE_BG
		tasks = cpus = step_ptr->job_ptr->details->min_cpus;
		select_g_select_jobinfo_get(step_ptr->job_ptr->select_jobinfo,
				     SELECT_JOBDATA_IONODES,
				     &ionodes);
		if(ionodes) {
			snprintf(node_list, BUFFER_SIZE,
				 "%s[%s]", step_ptr->job_ptr->nodes, ionodes);
			xfree(ionodes);
		} else
			snprintf(node_list, BUFFER_SIZE, "%s",
				 step_ptr->job_ptr->nodes);
		select_g_select_jobinfo_get(step_ptr->job_ptr->select_jobinfo,
				     SELECT_JOBDATA_NODE_CNT,
				     &nodes);
#else
		if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
			tasks = cpus = step_ptr->job_ptr->total_cpus;
			snprintf(node_list, BUFFER_SIZE, "%s",
				 step_ptr->job_ptr->nodes);
			nodes = step_ptr->job_ptr->node_cnt;
		} else {
			cpus = step_ptr->cpu_count;
			tasks = step_ptr->step_layout->task_cnt;
			nodes = step_ptr->step_layout->node_cnt;
			task_dist = step_ptr->step_layout->task_dist;
			snprintf(node_list, BUFFER_SIZE, "%s",
				 step_ptr->step_layout->node_list);
		}
#endif
	}

	if(!step_ptr->job_ptr->db_index) {
		if(!(step_ptr->job_ptr->db_index =
		     _get_db_index(mysql_conn,
				   step_ptr->job_ptr->details->submit_time,
				   step_ptr->job_ptr->job_id,
				   step_ptr->job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(mysql_job_start(mysql_conn, step_ptr->job_ptr)
			   == SLURM_ERROR) {
				error("couldn't add job %u at step start",
				      step_ptr->job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	/* we want to print a -1 for the requid so leave it a
	   %d */
	query = xstrdup_printf(
		"insert into %s (id, stepid, start, name, state, "
		"cpus, nodes, tasks, nodelist, node_inx, task_dist) "
		"values (%d, %d, %d, \"%s\", %d, %d, %d, %d, "
		"\"%s\", \"%s\", %d) "
		"on duplicate key update cpus=%d, nodes=%d, "
		"tasks=%d, end=0, state=%d, "
		"nodelist=\"%s\", node_inx=\"%s\", task_dist=%d",
		step_table, step_ptr->job_ptr->db_index,
		step_ptr->step_id,
		(int)step_ptr->start_time, step_ptr->name,
		JOB_RUNNING, cpus, nodes, tasks, node_list, node_inx, task_dist,
		cpus, nodes, tasks, JOB_RUNNING,
		node_list, node_inx, task_dist);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	return rc;
}

extern int mysql_step_complete(mysql_conn_t *mysql_conn,
			       struct step_record *step_ptr)
{
	time_t now;
	int elapsed;
	int comp_status;
	int cpus = 0, tasks = 0;
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	struct jobacctinfo dummy_jobacct;
	double ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	double ave_cpu = 0, ave_cpu2 = 0;
	char *query = NULL;
	int rc =SLURM_SUCCESS;
	uint32_t exit_code = 0;

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("mysql_step_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (jobacct == NULL) {
		/* JobAcctGather=jobacct_gather/none, no data to process */
		memset(&dummy_jobacct, 0, sizeof(dummy_jobacct));
		jobacct = &dummy_jobacct;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if(slurmdbd_conf) {
		now = step_ptr->job_ptr->end_time;
		tasks = step_ptr->job_ptr->details->num_tasks;
		cpus = step_ptr->cpu_count;
	} else {
		now = time(NULL);
#ifdef HAVE_BG
		tasks = cpus = step_ptr->job_ptr->details->min_cpus;

#else
		if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
			tasks = cpus = step_ptr->job_ptr->total_cpus;
		else {
			cpus = step_ptr->cpu_count;
			tasks = step_ptr->step_layout->task_cnt;
		}
#endif
	}

	if ((elapsed=now-step_ptr->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */

	exit_code = step_ptr->exit_code;
	if (exit_code == NO_VAL) {
		comp_status = JOB_CANCELLED;
		exit_code = 0;
	} else if (exit_code)
		comp_status = JOB_FAILED;
	else
		comp_status = JOB_COMPLETE;

	/* figure out the ave of the totals sent */
	if(cpus > 0) {
		ave_vsize = (double)jobacct->tot_vsize;
		ave_vsize /= (double)cpus;
		ave_rss = (double)jobacct->tot_rss;
		ave_rss /= (double)cpus;
		ave_pages = (double)jobacct->tot_pages;
		ave_pages /= (double)cpus;
		ave_cpu = (double)jobacct->tot_cpu;
		ave_cpu /= (double)cpus;
		ave_cpu /= (double)100;
	}

	if(jobacct->min_cpu != NO_VAL) {
		ave_cpu2 = (double)jobacct->min_cpu;
		ave_cpu2 /= (double)100;
	}

	if(!step_ptr->job_ptr->db_index) {
		if(!(step_ptr->job_ptr->db_index =
		     _get_db_index(mysql_conn,
				   step_ptr->job_ptr->details->submit_time,
				   step_ptr->job_ptr->job_id,
				   step_ptr->job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(mysql_job_start(mysql_conn, step_ptr->job_ptr)
			   == SLURM_ERROR) {
				error("couldn't add job %u "
				      "at step completion",
				      step_ptr->job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	query = xstrdup_printf(
		"update %s set end=%d, state=%d, "
		"kill_requid=%d, comp_code=%d, "
		"user_sec=%u, user_usec=%u, "
		"sys_sec=%u, sys_usec=%u, "
		"max_vsize=%u, max_vsize_task=%u, "
		"max_vsize_node=%u, ave_vsize=%f, "
		"max_rss=%u, max_rss_task=%u, "
		"max_rss_node=%u, ave_rss=%f, "
		"max_pages=%u, max_pages_task=%u, "
		"max_pages_node=%u, ave_pages=%f, "
		"min_cpu=%f, min_cpu_task=%u, "
		"min_cpu_node=%u, ave_cpu=%f "
		"where job_db_inx=%d and stepid=%u",
		step_table, (int)now,
		comp_status,
		step_ptr->requid,
		exit_code,
		/* user seconds */
		jobacct->user_cpu_sec,
		/* user microseconds */
		jobacct->user_cpu_usec,
		/* system seconds */
		jobacct->sys_cpu_sec,
		/* system microsecs */
		jobacct->sys_cpu_usec,
		jobacct->max_vsize,	/* max vsize */
		jobacct->max_vsize_id.taskid,	/* max vsize task */
		jobacct->max_vsize_id.nodeid,	/* max vsize node */
		ave_vsize,	/* ave vsize */
		jobacct->max_rss,	/* max vsize */
		jobacct->max_rss_id.taskid,	/* max rss task */
		jobacct->max_rss_id.nodeid,	/* max rss node */
		ave_rss,	/* ave rss */
		jobacct->max_pages,	/* max pages */
		jobacct->max_pages_id.taskid,	/* max pages task */
		jobacct->max_pages_id.nodeid,	/* max pages node */
		ave_pages,	/* ave pages */
		ave_cpu2,	/* min cpu */
		jobacct->min_cpu_id.taskid,	/* min cpu task */
		jobacct->min_cpu_id.nodeid,	/* min cpu node */
		ave_cpu,	/* ave cpu */
		step_ptr->job_ptr->db_index, step_ptr->step_id);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	return rc;
}

extern int mysql_suspend(mysql_conn_t *mysql_conn, struct job_record *job_ptr)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	bool suspended = false;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;
	if(!job_ptr->db_index) {
		if(!(job_ptr->db_index =
		     _get_db_index(mysql_conn,
				   job_ptr->details->submit_time,
				   job_ptr->job_id,
				   job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(mysql_job_start(
				   mysql_conn, job_ptr) == SLURM_ERROR) {
				error("couldn't suspend job %u",
				      job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	if (job_ptr->job_state == JOB_SUSPENDED)
		suspended = true;

	xstrfmtcat(query,
		   "update %s set suspended=%d-suspended, state=%d "
		   "where job_db_inx=%d;",
		   job_table, (int)job_ptr->suspend_time,
		   job_ptr->job_state & JOB_STATE_BASE,
		   job_ptr->db_index);
	if(suspended)
		xstrfmtcat(query,
			   "insert into %s (id, associd, start, end) "
			   "values (%u, %u, %d, 0);",
			   suspend_table, job_ptr->db_index, job_ptr->assoc_id,
			   (int)job_ptr->suspend_time);
	else
		xstrfmtcat(query,
			   "update %s set end=%d where job_db_inx=%u && end=0;",
			   suspend_table, (int)job_ptr->suspend_time,
			   job_ptr->db_index);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);

	rc = mysql_db_query(mysql_conn->db_conn, query);

	xfree(query);
	if(rc != SLURM_ERROR) {
		xstrfmtcat(query,
			   "update %s set suspended=%u-suspended, "
			   "state=%d where job_db_inx=%u and end=0",
			   step_table, (int)job_ptr->suspend_time,
			   job_ptr->job_state, job_ptr->db_index);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	}

	return rc;
}

extern int mysql_flush_jobs_on_cluster(
	mysql_conn_t *mysql_conn, char *cluster, time_t event_time)
{
	int rc = SLURM_SUCCESS;
	/* put end times for a clean start */
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	char *id_char = NULL;
	char *suspended_char = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* First we need to get the job_db_inx's and states so we can clean up
	 * the suspend table and the step table
	 */
	query = xstrdup_printf(
		"select distinct t1.job_db_inx, t1.state from %s as t1 where "
		"t1.cluster=\"%s\" && t1.end=0;",
		job_table, cluster);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	while((row = mysql_fetch_row(result))) {
		int state = atoi(row[1]);
		if(state == JOB_SUSPENDED) {
			if(suspended_char)
				xstrfmtcat(suspended_char,
					   " || job_db_inx=%s", row[0]);
			else
				xstrfmtcat(suspended_char, "job_db_inx=%s",
					   row[0]);
		}

		if(id_char)
			xstrfmtcat(id_char, " || job_db_inx=%s", row[0]);
		else
			xstrfmtcat(id_char, "job_db_inx=%s", row[0]);
	}
	mysql_free_result(result);

	if(suspended_char) {
		xstrfmtcat(query,
			   "update %s set suspended=%d-suspended where %s;",
			   job_table, event_time, suspended_char);
		xstrfmtcat(query,
			   "update %s set suspended=%d-suspended where %s;",
			   step_table, event_time, suspended_char);
		xstrfmtcat(query,
			   "update %s set end=%d where (%s) && end=0;",
			   suspend_table, event_time, suspended_char);
		xfree(suspended_char);
	}
	if(id_char) {
		xstrfmtcat(query,
			   "update %s set state=%d, end=%u where %s;",
			   job_table, JOB_CANCELLED, event_time, id_char);
		xstrfmtcat(query,
			   "update %s set state=%d, end=%u where %s;",
			   step_table, JOB_CANCELLED, event_time, id_char);
		xfree(id_char);
	}
/* 	query = xstrdup_printf("update %s as t1, %s as t2 set " */
/* 			       "t1.state=%u, t1.end=%u where " */
/* 			       "t2.id=t1.associd and t2.cluster=\"%s\" " */
/* 			       "&& t1.end=0;", */
/* 			       job_table, assoc_table, JOB_CANCELLED,  */
/* 			       event_time, cluster); */
	if(query) {
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);

		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	}

	return rc;
}
