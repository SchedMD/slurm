/*****************************************************************************\
 *  jobacct.c - accounting interface to pgsql - job/step related functions.
 *
 *  $Id: jobacct.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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
#include "common.h"

char *job_table = "job_table";
static storage_field_t job_table_fields[] = {
	{ "id", "SERIAL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "jobid", "INTEGER NOT NULL" },
	{ "associd", "INTEGER NOT NULL" }, /* id in assoc_table is of
					    * type INTEGER */
	{ "wckey", "TEXT DEFAULT '' NOT NULL" },
	{ "wckeyid", "INTEGER NOT NULL" },
	{ "uid", "INTEGER NOT NULL" },
	{ "gid", "INTEGER NOT NULL" },
	{ "cluster", "TEXT NOT NULL" },
	{ "partition", "TEXT NOT NULL" },
	{ "blockid", "TEXT" },
	{ "account", "TEXT" },
	{ "eligible", "INTEGER DEFAULT 0 NOT NULL" },
	{ "submit", "INTEGER DEFAULT 0 NOT NULL" },
	{ "start", "INTEGER DEFAULT 0 NOT NULL" },
	{ "endtime", "INTEGER DEFAULT 0 NOT NULL" }, /* "end" is
						      * reserved
						      * keyword in PG
						      * */
	{ "suspended", "INTEGER DEFAULT 0 NOT NULL" },
	{ "timelimit", "INTEGER DEFAULT 0 NOT NULL" },
	{ "name", "TEXT NOT NULL" },
	{ "track_steps", "INTEGER NOT NULL" },
	{ "state", "INTEGER NOT NULL" },
	{ "comp_code", "INTEGER DEFAULT 0 NOT NULL" },
	{ "priority", "INTEGER NOT NULL" },
	{ "req_cpus", "INTEGER NOT NULL" },
	{ "alloc_cpus", "INTEGER NOT NULL" },
	{ "alloc_nodes", "INTEGER NOT NULL" },
	{ "nodelist", "TEXT" },
	{ "node_inx", "TEXT" },
	{ "kill_requid", "INTEGER DEFAULT -1 NOT NULL" },
	{ "qos", "INTEGER DEFAULT 0" },
	{ "resvid", "INTEGER NOT NULL" },
	{ NULL, NULL}
};
static char *job_table_constraint = ", "
	"PRIMARY KEY (id), "
	"UNIQUE (jobid, associd, submit) "
	")";

char *step_table = "step_table";
static storage_field_t step_table_fields[] = {
	{ "id", "INTEGER NOT NULL" }, /* REFERENCES job_table */
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "stepid", "INTEGER NOT NULL" },
	{ "start", "INTEGER DEFAULT 0 NOT NULL" },
	{ "endtime", "INTEGER DEFAULT 0 NOT NULL" }, /* "end" is
						      * reserved
						      * keyword in PG
						      * */
	{ "suspended", "INTEGER DEFAULT 0 NOT NULL" },
	{ "name", "TEXT NOT NULL" },
	{ "nodelist", "TEXT NOT NULL" },
	{ "node_inx", "TEXT" },
	{ "state", "INTEGER NOT NULL" },
	{ "kill_requid", "INTEGER DEFAULT -1 NOT NULL" },
	{ "comp_code", "INTEGER DEFAULT 0 NOT NULL" },
	{ "nodes", "INTEGER NOT NULL" },
	{ "cpus", "INTEGER NOT NULL" },
	{ "tasks", "INTEGER NOT NULL" },
	{ "task_dist", "INTEGER DEFAULT 0" },
	{ "user_sec", "BIGINT DEFAULT 0 NOT NULL" },
	{ "user_usec", "BIGINT DEFAULT 0 NOT NULL" },
	{ "sys_sec", "BIGINT DEFAULT 0 NOT NULL" },
	{ "sys_usec", "BIGINT DEFAULT 0 NOT NULL" },
	{ "max_vsize", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_vsize_task", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_vsize_node", "INTEGER DEFAULT 0 NOT NULL" },
	 /* use "FLOAT" instead of "DOUBLE PRECISION" since only one
	    identifier supported in make_table_current() */
	{ "ave_vsize", "FLOAT DEFAULT 0.0 NOT NULL" },
	{ "max_rss", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_rss_task", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_rss_node", "INTEGER DEFAULT 0 NOT NULL" },
	{ "ave_rss", "FLOAT DEFAULT 0.0 NOT NULL" },
	{ "max_pages", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_pages_task", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_pages_node", "INTEGER DEFAULT 0 NOT NULL" },
	{ "ave_pages", "FLOAT DEFAULT 0.0 NOT NULL" },
	{ "min_cpu", "INTEGER DEFAULT 0 NOT NULL" },
	{ "min_cpu_task", "INTEGER DEFAULT 0 NOT NULL" },
	{ "min_cpu_node", "INTEGER DEFAULT 0 NOT NULL" },
	{ "ave_cpu", "FLOAT DEFAULT 0.0 NOT NULL" },
	{ NULL, NULL}
};
static char *step_table_constraint = ", "
	"PRIMARY KEY (id, stepid) "
	")";

char *suspend_table = "suspend_table";
static storage_field_t suspend_table_fields[] = {
	{ "id", "INTEGER NOT NULL" }, /* REFERENCES job_table */
	{ "associd", "INTEGER NOT NULL" },
	{ "start", "INTEGER DEFAULT 0 NOT NULL" },
	{ "endtime", "INTEGER DEFAULT 0 NOT NULL" },
	{ NULL, NULL}
};
static char *suspend_table_constraint = ")";

extern time_t global_last_rollup;
extern pthread_mutex_t rollup_lock;

/*
 * _get_db_index - get ID in database of a job
 *
 * IN pg_conn: database connection
 * IN submit: submit time of job
 * IN jobid: jobid of job
 * IN associd: association id of job
 * RET: db id of job, -1 on error
 */
static int
_get_db_index(pgsql_conn_t *pg_conn, time_t submit, uint32_t jobid,
	      uint32_t associd)
{
	PGresult *result = NULL;
	int db_index = -1;
	char *query;

	query = xstrdup_printf("SELECT id FROM %s WHERE submit=%u AND "
			       "  jobid=%u AND associd=%u",
			       job_table, (int)submit, jobid, associd);
	result = DEF_QUERY_RET;
	if(!result)
		return -1;

	if(!PQntuples(result)) {
		error("We can't get a db_index for this combo, "
		      "submit=%u and jobid=%u and associd=%u.",
		      (int)submit, jobid, associd);
	} else {
		db_index = atoi(PG_VAL(0));
	}
	PQclear(result);
	return db_index;
}

/*
 * _check_job_db_index - check job has db index
 *
 * IN pg_conn: database connection
 * IN/OUT job_ptr: the job
 * RET: error code
 */
static int
_check_job_db_index(pgsql_conn_t *pg_conn, struct job_record *job_ptr)
{
	if(!job_ptr->db_index) {
		job_ptr->db_index = _get_db_index(
			pg_conn,
			job_ptr->details->submit_time,
			job_ptr->job_id,
			job_ptr->assoc_id);
		if (! job_ptr->db_index) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(jobacct_storage_p_job_start(pg_conn, job_ptr)
			   == SLURM_ERROR) {
				error("couldn't add job %u ",
				      job_ptr->job_id);
				return SLURM_ERROR;
			}
		}
	}
	return SLURM_SUCCESS;
}

/*
 * _create_function_add_job_start - create a PL/pgSQL function to
 *    add job start record
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_job_start(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_job_start (rec %s) "
		"RETURNS INTEGER AS $$"
		"DECLARE dbid INTEGER; "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s (id, deleted, jobid, associd, wckey, "
		"        wckeyid, uid, gid, cluster, partition, blockid, "
		"        account, eligible, submit, start, endtime, suspended, "
		"        timelimit, name, track_steps, state, comp_code, "
		"        priority, req_cpus, alloc_cpus, alloc_nodes, nodelist, "
		"        node_inx, kill_requid, qos, resvid) "
		"      VALUES (DEFAULT, 0, rec.jobid, "
		"        rec.associd, rec.wckey, rec.wckeyid, rec.uid, "
		"        rec.gid, rec.cluster, rec.partition, rec.blockid, "
		"        rec.account, rec.eligible, rec.submit, rec.start, "
		"        rec.endtime, rec.suspended, rec.timelimit, rec.name, "
		"        rec.track_steps, rec.state, rec.comp_code, "
		"        rec.priority, rec.req_cpus, rec.alloc_cpus, "
		"        rec.alloc_nodes, rec.nodelist, rec.node_inx, "
		"        rec.kill_requid, rec.qos, rec.resvid) "
		"      RETURNING id INTO dbid; "
		"    RETURN dbid;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    \n-- create a new dbid for job?\n "
		"    UPDATE %s SET id=nextval('%s_id_seq'), state=rec.state, "
		"        wckeyid=rec.wckeyid, qos=rec.qos, resvid=rec.resvid, "
		"        timelimit=rec.timelimit, deleted=0, "
		"        account=(CASE WHEN rec.account!='' "
		"          THEN rec.account ELSE account END),"
		"        partition=(CASE WHEN rec.partition!='' "
		"          THEN rec.partition ELSE partition END), "
		"        blockid=(CASE WHEN rec.blockid!='' "
		"          THEN rec.blockid ELSE blockid END), "
		"        wckey=(CASE WHEN rec.wckey!='' "
		"          THEN rec.wckey ELSE wckey END), "
		"        node_inx=(CASE WHEN rec.node_inx!='' "
		"          THEN rec.node_inx ELSE node_inx END)"
		"      WHERE jobid=rec.jobid AND associd=rec.associd AND "
		"            submit=rec.submit"
		"      RETURNING id INTO dbid; "
		"    IF FOUND THEN RETURN dbid; END IF;"
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		job_table, job_table, job_table, job_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_step_start - create a PL/pgSQL function to
 *    add job step record
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_step_start(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_step_start (rec %s) "
		"RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s (id, stepid, start, name, state, "
		"        cpus, nodes, tasks, nodelist, node_inx, task_dist) "
		"      VALUES (rec.id, rec.stepid, rec.start, rec.name,"
		"        rec.state, rec.cpus, rec.nodes, rec.tasks, "
		"        rec.nodelist, rec.node_inx, rec.task_dist);"
		"    RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET cpus=rec.cpus, nodes=rec.nodes, "
		"        tasks=rec.tasks, endtime=0, state=rec.state, "
		"        nodelist=rec.nodelist, node_inx=rec.node_inx, "
		"        task_dist=rec.task_dist, deleted=0 "
		"      WHERE id=rec.id AND stepid=rec.stepid;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		step_table, step_table, step_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_get_job_suspend_time - create a PL/pgSQL function to
 *    get suspended time of given job during specified period
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_get_job_suspend_time(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION get_job_suspend_time "
		"(dbid INTEGER, st INTEGER, et INTEGER) "
		"RETURNS INTEGER AS $$"
		"DECLARE susp INTEGER; "
		"BEGIN "
		"  IF et<=st THEN RETURN 0; END IF;"
		"  SELECT SUM((CASE WHEN (endtime=0 OR endtime>et) THEN et "
		"                   ELSE endtime END) "
		"           - (CASE WHEN start>st THEN start "
		"                     ELSE st END) "
		"            ) FROM %s "
		"    INTO susp"
		"    WHERE (start!=0 AND start<et) AND "
		"          (endtime>=st OR endtime=0) AND id=dbid "
		"    GROUP BY id; "
		"  RETURN susp;"
		"END; $$ LANGUAGE PLPGSQL;",
		suspend_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * check_jobacct_tables - check jobacct related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_jobacct_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, job_table, job_table_fields,
			 job_table_constraint, user);
	rc |= check_table(db_conn, step_table, step_table_fields,
			  step_table_constraint, user);
	rc |= check_table(db_conn, suspend_table, suspend_table_fields,
			  suspend_table_constraint, user);

	rc |= _create_function_add_job_start(db_conn);
	rc |= _create_function_add_step_start(db_conn);
	rc |= _create_function_get_job_suspend_time(db_conn);
	return rc;
}

/*
 * js_p_job_start - load into the storage the start of a job
 *
 * IN pg_conn: database connection
 * IN cluster_name: cluster of the job
 * IN job_ptr: job just started
 * RET: error code
 */
extern int
js_p_job_start(pgsql_conn_t *pg_conn,
	       struct job_record *job_ptr)
{
	int rc=SLURM_SUCCESS, track_steps = 0, reinit = 0;
	char *jname = NULL, *nodes = NULL, *node_inx = NULL;
	char *block_id = NULL, *rec = NULL, *query = NULL;
	time_t check_time;
	int node_cnt = 0;
	uint32_t wckeyid = 0;

	if (!job_ptr->details || !job_ptr->details->submit_time) {
		error("as/pg: job_start: Not inputing this job, "
		      "it has no submit time.");
		return SLURM_ERROR;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug3("as/pg: job_start() called");

	/* See what we are hearing about here if no start time. If
	 * this job latest time is before the last roll up we will
	 * need to reset it to look at this job. */
	check_time = job_ptr->start_time;
	if(!check_time) {
		check_time = job_ptr->details->begin_time;
		if(!check_time)
			check_time = job_ptr->details->submit_time;
	}

	slurm_mutex_lock(&rollup_lock);
	if(check_time < global_last_rollup) {
		PGresult *result = NULL;
		/* check to see if we are hearing about this time for the
		 * first time.
		 */
		query = xstrdup_printf("SELECT id FROM %s WHERE jobid=%u AND "
				       "submit=%d AND eligible=%d "
				       "AND start=%d;",
				       job_table, job_ptr->job_id,
				       job_ptr->details->submit_time,
				       job_ptr->details->begin_time,
				       job_ptr->start_time);
		result = DEF_QUERY_RET;
		if(!result) {
			slurm_mutex_unlock(&rollup_lock);
			return SLURM_ERROR;
		}
		if (PQntuples(result)) {
			PQclear(result);
			debug4("revieved an update for a "
			       "job (%u) already known about",
			       job_ptr->job_id);
			slurm_mutex_unlock(&rollup_lock);
			goto no_rollup_change;
		}
		PQclear(result);

		if(job_ptr->start_time)
			debug("Need to reroll usage from %sJob %u "
			      "from %s started then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, pg_conn->cluster_name);
		else if(job_ptr->details->begin_time)
			debug("Need to reroll usage from %sJob %u "
			      "from %s became eligible then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, pg_conn->cluster_name);
		else
			debug("Need to reroll usage from %sJob %u "
			      "from %s was submitted then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, pg_conn->cluster_name);

		global_last_rollup = check_time;
		slurm_mutex_unlock(&rollup_lock);

		query = xstrdup_printf("UPDATE %s SET hourly_rollup=%d, "
				       "daily_rollup=%d, monthly_rollup=%d",
				       last_ran_table, check_time,
				       check_time, check_time);
		rc = DEF_QUERY_RET_RC;
	} else
		slurm_mutex_unlock(&rollup_lock);

no_rollup_change:

	if (job_ptr->name && job_ptr->name[0])
		jname = xstrdup(job_ptr->name);
	else {
		jname = xstrdup("allocation");
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
		wckeyid = get_wckeyid(pg_conn, &job_ptr->wckey,
				      job_ptr->user_id, pg_conn->cluster_name,
				      job_ptr->assoc_id);

	/* We need to put a 0 for 'end' incase of funky job state
	 * files from a hot start of the controllers we call
	 * job_start on jobs we may still know about after
	 * job_flush has been called so we need to restart
	 * them by zeroing out the end.
	 */
	if(!job_ptr->db_index) {
		if (!job_ptr->details->begin_time)
			job_ptr->details->begin_time =
				job_ptr->details->submit_time;

		rec = xstrdup_printf(
			"(0, 0, %u, %u, '%s', %u, %u, %u, "
			"'%s', '%s', '%s', '%s', "
			"%u, %u, %u, 0, 0, %d, "
			"'%s', %u, %u, 0, %d, %u, %u, %u, "
			"'%s', '%s', -1, %d, %d)",
			/* id=0, not used */
			/* deleted=0 */
			job_ptr->job_id,
			job_ptr->assoc_id,
			job_ptr->wckey ?: "",
			wckeyid,
			job_ptr->user_id,
			job_ptr->group_id,

			pg_conn->cluster_name ?: "",
			job_ptr->partition ?: "",
			block_id ?: "",
			job_ptr->account ?: "",

			job_ptr->details->begin_time,
			job_ptr->details->submit_time,
			job_ptr->start_time,
			/* endtime=0 */
			/* suspended=0 */
			job_ptr->time_limit,

			jname,
			track_steps,
			job_ptr->job_state & JOB_STATE_BASE,
			/* comp_code=0 */
			job_ptr->priority,
			job_ptr->details->min_cpus,
			job_ptr->total_cpus,
			node_cnt,

			nodes ?: "",
			node_inx ?: "",
			/* kill_requid=-1 */
			job_ptr->qos,
			job_ptr->resv_id);

		query = xstrdup_printf("SELECT add_job_start(%s);", rec);
		xfree(rec);

	try_again:
		DEBUG_QUERY;
		job_ptr->db_index = pgsql_query_ret_id(pg_conn->db_conn,
						       query);
		if (!job_ptr->db_index) {
			if(!reinit) {
				error("It looks like the storage has gone "
				      "away trying to reconnect");
				check_db_connection(pg_conn);
				reinit = 1;
				goto try_again;
			} else
				rc = SLURM_ERROR;
		}
		xfree(query);
	} else {
		query = xstrdup_printf("UPDATE %s SET nodelist='%s', ",
				       job_table, nodes);
		if(job_ptr->account)
			xstrfmtcat(query, "account='%s', ",
				   job_ptr->account);
		if(job_ptr->partition)
			xstrfmtcat(query, "partition='%s', ",
				   job_ptr->partition);
		if(block_id)
			xstrfmtcat(query, "blockid='%s', ", block_id);
		if(job_ptr->wckey)
			xstrfmtcat(query, "wckey='%s', ", job_ptr->wckey);
		if(node_inx)
			xstrfmtcat(query, "node_inx='%s', ", node_inx);

		xstrfmtcat(query, "start=%d, name='%s', state=%u, "
			   "alloc_cpus=%u, alloc_nodes=%u, associd=%u, "
			   "wckeyid=%u, resvid=%u, timelimit=%d WHERE id=%d;",
			   (int)job_ptr->start_time,
			   jname, job_ptr->job_state & JOB_STATE_BASE,
			   job_ptr->total_cpus, node_cnt,
			   job_ptr->assoc_id, wckeyid, job_ptr->resv_id,
			   job_ptr->time_limit, job_ptr->db_index);
		rc = DEF_QUERY_RET_RC;
	}
	xfree(block_id);
	xfree(jname);

	return rc;
}

/*
 * js_p_job_complete - load into the storage the end of a job
 *
 * IN pg_conn: database connection
 * IN job_ptr: job completed
 * RET error code
 */
extern int
js_p_job_complete(pgsql_conn_t *pg_conn,
		  struct job_record *job_ptr)
{
	char *query = NULL, *nodes = NULL;
	int rc=SLURM_SUCCESS;
	time_t start_time = job_ptr->start_time;

	if (!job_ptr->db_index
	    && (!job_ptr->details || !job_ptr->details->submit_time)) {
		error("jobacct_storage_p_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
			return ESLURM_DB_CONNECTION;

	debug2("as/pg: job_complete() called");

	/* If we get an error with this just fall through to avoid an
	 * infinite loop
	 */
	if (job_ptr->end_time == 0) {
		debug("as/pg: job_complete: job %u never started", job_ptr->job_id);
		return SLURM_SUCCESS;
	} else if(start_time > job_ptr->end_time)
		start_time = 0;

	slurm_mutex_lock(&rollup_lock);
	if(job_ptr->end_time < global_last_rollup) {
		global_last_rollup = job_ptr->end_time;
		slurm_mutex_unlock(&rollup_lock);

		query = xstrdup_printf("UPDATE %s SET hourly_rollup=%d, "
				       "daily_rollup=%d, monthly_rollup=%d",
				       last_ran_table, job_ptr->end_time,
				       job_ptr->end_time, job_ptr->end_time);
		rc = DEF_QUERY_RET_RC;
	} else
		slurm_mutex_unlock(&rollup_lock);

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "None assigned";

	/* If we get an error with this just fall
	 * through to avoid an infinite loop
	 */
	if (_check_job_db_index(pg_conn, job_ptr) != SLURM_SUCCESS)
		return SLURM_SUCCESS;

	query = xstrdup_printf("UPDATE %s SET start=%d, endtime=%d, state=%d, "
			       "nodelist='%s', comp_code=%d, "
			       "kill_requid=%d WHERE id=%d",
			       job_table, (int)job_ptr->start_time,
			       (int)job_ptr->end_time,
			       job_ptr->job_state & JOB_STATE_BASE,
			       nodes, job_ptr->exit_code,
			       job_ptr->requid, job_ptr->db_index);
	rc = DEF_QUERY_RET_RC;
	return  rc;
}


/*
 * js_p_step_start - load into the storage the start of a job step
 *
 * IN pg_conn: database connection
 * IN step_ptr: step just started
 * RET: error code
 */
extern int
js_p_step_start(pgsql_conn_t *pg_conn,
		struct step_record *step_ptr)
{
	int cpus = 0, tasks = 0, nodes = 0, task_dist = 0;
	int rc=SLURM_SUCCESS;
	char node_list[BUFFER_SIZE];
	char *node_inx = NULL;
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	char *query = NULL, *rec = NULL;

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("jobacct_storage_p_step_start: "
		      "Not inputing this job step, it has no submit time.");
		return SLURM_ERROR;
	}

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
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

	if (_check_job_db_index(pg_conn, step_ptr->job_ptr)
	    != SLURM_SUCCESS)
		return SLURM_SUCCESS;

	rec = xstrdup_printf("(%u, 0, %u, %u, 0, 0,'%s', '%s', '%s',"
			     "%u, -1, 0, %u, %u, %u, %u,"
			     "0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0)",
			     step_ptr->job_ptr->db_index,
			     /* deleted=0 */
			     step_ptr->step_id,
			     step_ptr->start_time,
			     /* endtime=0 */
			     /* suspended=0 */
			     step_ptr->name ?: "",
			     node_list,
			     node_inx,

			     JOB_RUNNING,
			     /* kill_requid=-1 */
			     /* comp_code=0 */
			     nodes,
			     cpus,
			     tasks,
			     task_dist
			     /* resouce usage all 0 */
		);
	query = xstrdup_printf("SELECT add_step_start(%s)", rec);
	xfree(rec);
	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * js_p_step_complete - load into the storage the end of a job step
 *
 * IN pg_conn: database connection
 * IN step_ptr: step completed
 * RET: error code
 */
extern int
js_p_step_complete(pgsql_conn_t *pg_conn,
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
	uint32_t exit_code;

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("jobacct_storage_p_step_complete: "
		      "Not inputing this job step, it has no submit time.");
		return SLURM_ERROR;
	}

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (jobacct == NULL) {
		/* JobAcctGather=jobacct_gather/none, no data to process */
		memset(&dummy_jobacct, 0, sizeof(dummy_jobacct));
		jobacct = &dummy_jobacct;
	}

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

	if(jobacct->min_cpu != (uint32_t)NO_VAL) {
		ave_cpu2 = (double)jobacct->min_cpu;
		ave_cpu2 /= (double)100;
	}


	if (_check_job_db_index(pg_conn, step_ptr->job_ptr)
	    != SLURM_SUCCESS)
		return SLURM_SUCCESS;

	query = xstrdup_printf(
		"UPDATE %s SET endtime=%u, state=%d, "
		"kill_requid=%d, comp_code=%u, "
		"user_sec=%u, user_usec=%u, "
		"sys_sec=%u, sys_usec=%u, "
		"max_vsize=%u, max_vsize_task=%u, "
		"max_vsize_node=%u, ave_vsize=%.2f, "
		"max_rss=%u, max_rss_task=%u, "
		"max_rss_node=%u, ave_rss=%.2f, "
		"max_pages=%u, max_pages_task=%u, "
		"max_pages_node=%u, ave_pages=%.2f, "
		"min_cpu=%.2f, min_cpu_task=%u, "
		"min_cpu_node=%u, ave_cpu=%.2f "
		"WHERE id=%u and stepid=%u",
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
	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * js_p_suspend - load into the storage a suspention of a job
 *
 * IN pg_conn: database connection
 * IN job_ptr: job suspended
 */
extern int
js_p_suspend(pgsql_conn_t *pg_conn, struct job_record *job_ptr)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	bool suspended = false;

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (_check_job_db_index(pg_conn, job_ptr) != SLURM_SUCCESS)
		return SLURM_SUCCESS;

	if (job_ptr->job_state == JOB_SUSPENDED)
		suspended = true;

	query = xstrdup_printf(
		"UPDATE %s SET suspended=%u-suspended, state=%d "
		"WHERE id=%u",
		job_table, (int)job_ptr->suspend_time,
		job_ptr->job_state & JOB_STATE_BASE,
		job_ptr->db_index);

	if(job_ptr->job_state == JOB_SUSPENDED)
		/* XXX: xstrfmtcat() cannot work on non-xmalloc-ed strings */
		xstrfmtcat(query,
			   "INSERT INTO %s (id, associd, start, end) "
			   "VALUES (%u, %u, %d, 0);",
			   suspend_table, job_ptr->db_index, job_ptr->assoc_id,
			   (int)job_ptr->suspend_time);
	else
		xstrfmtcat(query,
			   "UPDATE %s SET end=%d WHERE id=%u && endtime=0;",
			   suspend_table, (int)job_ptr->suspend_time,
			   job_ptr->db_index);

	rc = DEF_QUERY_RET_RC;
	if(rc != SLURM_ERROR) {
		query = xstrdup_printf(
			"UPDATE %s SET suspended=%u-suspended, "
			"state=%d WHERE id=%u and endtime=0",
			step_table, (int)job_ptr->suspend_time,
			job_ptr->job_state, job_ptr->db_index);
		rc = DEF_QUERY_RET_RC;
	}
	return rc;
}


static void _state_time_string(char **extra, uint32_t state,
			       uint32_t start, uint32_t end)
{
	int base_state = state & JOB_STATE_BASE;

	if(!start && !end) {
		xstrfmtcat(*extra, "t1.state='%u'", state);
		return;
	}

 	switch(base_state) {
	case JOB_PENDING:
		if(start) {
			if(!end) {
				xstrfmtcat(*extra,
					   "(t1.eligible AND (t1.start=0 OR "
					   "(%d BETWEEN "
					   "t1.eligible AND t1.start)))",
					   start);
			} else {
				xstrfmtcat(*extra,
					   "(t1.eligible AND ((%d BETWEEN "
					   "t1.eligible AND t1.start) OR "
					   "(t1.eligible BETWEEN %d AND %d)))",
					   start, start,
					   end);
			}
		} else if (end) {
			xstrfmtcat(*extra, "(t1.eligible AND t1.eligible < %d)",
				   end);
		}
		break;
	case JOB_SUSPENDED:
		/* FIX ME: this should do something with the suspended
		   table, but it doesn't right now. */
	case JOB_RUNNING:
		if(start) {
			if(!end) {
				xstrfmtcat(*extra,
					   "(t1.start AND (t1.endtime=0 OR "
					   "(%d BETWEEN t1.start AND t1.endtime)))",
					   start);
			} else {
				xstrfmtcat(*extra,
					   "(t1.start!=0 AND "
					   "((%d BETWEEN t1.start AND t1.endtime) "
					   "OR (t1.start BETWEEN %d AND %d)))",
					   start, start,
					   end);
			}
		} else if (end) {
			xstrfmtcat(*extra, "(t1.start AND t1.start < %d)", end);
		}
		break;
	case JOB_COMPLETE:
	case JOB_CANCELLED:
	case JOB_FAILED:
	case JOB_TIMEOUT:
	case JOB_NODE_FAIL:
	default:
		xstrfmtcat(*extra, "(t1.state='%u' AND (t1.endtime AND ", state);
		if(start) {
			if(!end) {
				xstrfmtcat(*extra, "(t1.endtime >= %d)))", start);
			} else {
				xstrfmtcat(*extra,
					   "(t1.endtime BETWEEN %d AND %d)))",
					   start, end);
			}
		} else if(end) {
			xstrfmtcat(*extra, "(t1.endtime <= %d)))", end);
		}
		break;
	}

	return;
}

/*
 * _make_job_cond_str - turn job_cond into SQL query condition string
 *
 * IN pg_conn: database connection
 * IN job_cond: job condition
 * OUT extra_table: extra table to select from
 * OUT cond: condition string
 */
static void
_make_job_cond_str(pgsql_conn_t *pg_conn, acct_job_cond_t *job_cond,
		   char **extra_table, char **cond)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	char *table_level = "t2";
	jobacct_selected_step_t *selected_step = NULL;

	xstrcat (*cond, " WHERE TRUE");

	if(!job_cond)
		return;

	/* THIS ASSOCID CHECK ALWAYS NEEDS TO BE FIRST!!!!!!! */
	if(job_cond->associd_list && list_count(job_cond->associd_list)) {
		set = 0;
		xstrfmtcat(*extra_table, ", %s AS t3", assoc_table);
		table_level = "t3";

		/* just incase the association is gone */
		xstrcat(*cond, " AND (t3.id IS NULL");
		itr = list_iterator_create(job_cond->associd_list);
		while((object = list_next(itr))) {
			xstrfmtcat(*cond, " OR t3.id=%s", object);
		}
		list_iterator_destroy(itr);
		xstrfmtcat(*cond, ") AND "
			   "(t2.lft BETWEEN t3.lft AND t3.rgt "
			   "OR t2.lft IS NULL)");
	}

	concat_cond_list(job_cond->acct_list, "t1", "account", cond);
	concat_cond_list(job_cond->userid_list, "t1", "uid", cond);
	concat_cond_list(job_cond->groupid_list, "t1", "gid", cond);
	concat_cond_list(job_cond->partition_list, "t1", "partition", cond);

	/* this must be done before resvid_list since we set
	   resvid_list up here */
	if(job_cond->resv_list && list_count(job_cond->resv_list)) {
		PGresult *result = NULL;
		char *query = xstrdup_printf(
			"SELECT DISTINCT id FROM %s WHERE (", resv_table);
		concat_cond_list(job_cond->cluster_list, NULL,
				 "cluster", &query);
		concat_cond_list(job_cond->resv_list, NULL, "name", &query);
		//xstrcat(query, ";");
		result = DEF_QUERY_RET;
		if(!result) {
			error("as/pg: couldn't get resv id");
			goto no_resv;
		}
		if(!job_cond->resvid_list)
			job_cond->resvid_list = list_create(slurm_destroy_char);
		FOR_EACH_ROW {
			list_append(job_cond->resvid_list, xstrdup(ROW(0)));
		} END_EACH_ROW;
		PQclear(result);
	}
no_resv:
	concat_cond_list(job_cond->resvid_list, "t1", "resvid", cond);

	if(job_cond->step_list && list_count(job_cond->step_list)) {
		set = 0;
		xstrcat(*cond, " AND (");
		itr = list_iterator_create(job_cond->step_list);
		while((selected_step = list_next(itr))) {
			if(set)
				xstrcat(*cond, " OR ");
			xstrfmtcat(*cond, "t1.jobid=%u", selected_step->jobid);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond, ")");
	}

	if(job_cond->state_list && list_count(job_cond->state_list)) {
		set = 0;
		xstrcat(*cond, " AND (");

		itr = list_iterator_create(job_cond->state_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*cond, " OR ");
			_state_time_string(cond, atoi(object),
					   job_cond->usage_start,
					   job_cond->usage_end);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond, ")");
	} else {
		/* Only do this (default of all eligible jobs) if no
		   state is given */
		if(job_cond->usage_start) {
			xstrcat(*cond, " AND (");

			if(!job_cond->usage_end)
				xstrfmtcat(*cond,
					   "(t1.endtime >= %d OR t1.endtime = 0))",
					   job_cond->usage_start);
			else
				xstrfmtcat(*cond,
					   "(t1.eligible < %d "
					   "AND (t1.endtime >= %d OR t1.endtime = 0)))",
					   job_cond->usage_end,
					   job_cond->usage_start);
		} else if(job_cond->usage_end) {
			xstrcat(*cond, " AND (");
			xstrfmtcat(*cond,
				   "(t1.eligible < %d))", job_cond->usage_end);
		}
	}

	concat_cond_list(job_cond->state_list, "t1", "state", cond);

	/* we need to put all the associations (t2) stuff together here */
	if(job_cond->cluster_list && list_count(job_cond->cluster_list)) {
		set = 0;
		xstrcat(*cond, " AND (");
		itr = list_iterator_create(job_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*cond, " OR ");
			xstrfmtcat(*cond,
				   "(t1.cluster='%s' OR %s.cluster='%s')",
				   object, table_level, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond, ")");
	}

	concat_cond_list(job_cond->wckey_list, "t1", "wckey", cond);

	if(job_cond->cpus_min) {
		xstrcat(*cond, " AND (");
		if(job_cond->cpus_max) {
			xstrfmtcat(*cond, "(t1.alloc_cpus BETWEEN %u AND %u))",
				   job_cond->cpus_min, job_cond->cpus_max);

		} else {
			xstrfmtcat(*cond, "(t1.alloc_cpus='%u'))",
				   job_cond->cpus_min);

		}
	}

	if(job_cond->nodes_min) {
		xstrcat(*cond, " AND (");

		if(job_cond->nodes_max) {
			xstrfmtcat(*cond,
				   "(t1.alloc_nodes BETWEEN %u AND %u))",
				   job_cond->nodes_min, job_cond->nodes_max);

		} else {
			xstrfmtcat(*cond, "(t1.alloc_nodes='%u'))",
				   job_cond->nodes_min);

		}
	}

	return;
}


/*
 * js_p_get_jobs_cond - get jobs
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN job_cond: condition of jobs to get
 * RET: list of jobs
 */
extern List
js_p_get_jobs_cond(pgsql_conn_t *pg_conn, uid_t uid,
		   acct_job_cond_t *job_cond)
{

	char *query = NULL, *extra_table = NULL, *tmp = NULL, *cond = NULL,
		*table_level = "t2";
	int set = 0, is_admin = 1, last_id = -1, curr_id = -1;
	jobacct_selected_step_t *selected_step = NULL;
	jobacct_job_rec_t *job = NULL;
	jobacct_step_rec_t *step = NULL;
	time_t now = time(NULL);
	uint16_t private_data = 0;
	acct_user_rec_t user;
	local_cluster_t *curr_cluster = NULL;
	List job_list = list_create(destroy_jobacct_job_rec);
	List local_cluster_list = NULL;
	ListIterator itr = NULL;
	PGresult *result = NULL, *result2 = NULL;
	int i,only_pending = 0;

	/* if this changes you will need to edit the corresponding
	 * enum below also t1 is job_table */
	char *job_req_inx[] = {
		"t1.id",
		"t1.jobid",
		"t1.associd",
		"t1.wckey",
		"t1.wckeyid",
		"t1.uid",
		"t1.gid",
		"t1.resvid",
		"t1.partition",
		"t1.blockid",
		"t1.cluster",
		"t1.account",
		"t1.eligible",
		"t1.submit",
		"t1.start",
		"t1.endtime",
		"t1.suspended",
		"t1.name",
		"t1.track_steps",
		"t1.state",
		"t1.comp_code",
		"t1.priority",
		"t1.req_cpus",
		"t1.alloc_cpus",
		"t1.alloc_nodes",
		"t1.nodelist",
		"t1.node_inx",
		"t1.kill_requid",
		"t1.qos",
		"t2.user_name",
		"t2.cluster",
		"t2.acct",
		"t2.lft"
	};

	enum {
		JOB_REQ_ID,
		JOB_REQ_JOBID,
		JOB_REQ_ASSOCID,
		JOB_REQ_WCKEY,
		JOB_REQ_WCKEYID,
		JOB_REQ_UID,
		JOB_REQ_GID,
		JOB_REQ_RESVID,
		JOB_REQ_PARTITION,
		JOB_REQ_BLOCKID,
		JOB_REQ_CLUSTER1,
		JOB_REQ_ACCOUNT1,
		JOB_REQ_ELIGIBLE,
		JOB_REQ_SUBMIT,
		JOB_REQ_START,
		JOB_REQ_END,
		JOB_REQ_SUSPENDED,
		JOB_REQ_NAME,
		JOB_REQ_TRACKSTEPS,
		JOB_REQ_STATE,
		JOB_REQ_COMP_CODE,
		JOB_REQ_PRIORITY,
		JOB_REQ_REQ_CPUS,
		JOB_REQ_ALLOC_CPUS,
		JOB_REQ_ALLOC_NODES,
		JOB_REQ_NODELIST,
		JOB_REQ_NODE_INX,
		JOB_REQ_KILL_REQUID,
		JOB_REQ_QOS,
		JOB_REQ_USER_NAME,
		JOB_REQ_CLUSTER,
		JOB_REQ_ACCOUNT,
		JOB_REQ_LFT,
		JOB_REQ_COUNT
	};

	/* if this changes you will need to edit the corresponding
	 * enum below also t1 is step_table */
	char *step_req_inx[] = {
		"t1.stepid",
		"t1.start",
		"t1.endtime",
		"t1.suspended",
		"t1.name",
		"t1.nodelist",
		"t1.node_inx",
		"t1.state",
		"t1.kill_requid",
		"t1.comp_code",
		"t1.nodes",
		"t1.cpus",
		"t1.tasks",
		"t1.task_dist",
		"t1.user_sec",
		"t1.user_usec",
		"t1.sys_sec",
		"t1.sys_usec",
		"t1.max_vsize",
		"t1.max_vsize_task",
		"t1.max_vsize_node",
		"t1.ave_vsize",
		"t1.max_rss",
		"t1.max_rss_task",
		"t1.max_rss_node",
		"t1.ave_rss",
		"t1.max_pages",
		"t1.max_pages_task",
		"t1.max_pages_node",
		"t1.ave_pages",
		"t1.min_cpu",
		"t1.min_cpu_task",
		"t1.min_cpu_node",
		"t1.ave_cpu"
	};

	enum {
		STEP_REQ_STEPID,
		STEP_REQ_START,
		STEP_REQ_END,
		STEP_REQ_SUSPENDED,
		STEP_REQ_NAME,
		STEP_REQ_NODELIST,
		STEP_REQ_NODE_INX,
		STEP_REQ_STATE,
		STEP_REQ_KILL_REQUID,
		STEP_REQ_COMP_CODE,
		STEP_REQ_NODES,
		STEP_REQ_CPUS,
		STEP_REQ_TASKS,
		STEP_REQ_TASKDIST,
		STEP_REQ_USER_SEC,
		STEP_REQ_USER_USEC,
		STEP_REQ_SYS_SEC,
		STEP_REQ_SYS_USEC,
		STEP_REQ_MAX_VSIZE,
		STEP_REQ_MAX_VSIZE_TASK,
		STEP_REQ_MAX_VSIZE_NODE,
		STEP_REQ_AVE_VSIZE,
		STEP_REQ_MAX_RSS,
		STEP_REQ_MAX_RSS_TASK,
		STEP_REQ_MAX_RSS_NODE,
		STEP_REQ_AVE_RSS,
		STEP_REQ_MAX_PAGES,
		STEP_REQ_MAX_PAGES_TASK,
		STEP_REQ_MAX_PAGES_NODE,
		STEP_REQ_AVE_PAGES,
		STEP_REQ_MIN_CPU,
		STEP_REQ_MIN_CPU_TASK,
		STEP_REQ_MIN_CPU_NODE,
		STEP_REQ_AVE_CPU,
		STEP_REQ_COUNT
	};

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_JOBS) {
		is_admin = is_user_min_admin_level(
			pg_conn, uid, ACCT_ADMIN_OPERATOR);
		if (!is_admin)
			assoc_mgr_fill_in_user(pg_conn, &user, 1, NULL);
	}

	/* Here we set up environment to check used nodes of jobs.
	   Since we store the bitmap of the entire cluster we can use
	   that to set up a hostlist and set up the bitmap to make
	   things work.  This should go before the setup of conds
	   since we could update the start/end time.
	*/
	if(job_cond && job_cond->used_nodes) {
		local_cluster_list = setup_cluster_list_with_inx(
			pg_conn, job_cond, (void **)&curr_cluster);
		if(!local_cluster_list) {
			list_destroy(job_list);
			return NULL;
		}
	}

	if(job_cond->state_list && (list_count(job_cond->state_list) == 1)
	   && (atoi(list_peek(job_cond->state_list)) == JOB_PENDING))
		only_pending = 1;

	/* TODO: */
	_make_job_cond_str(pg_conn, job_cond, &extra_table, &cond);

	xfree(tmp);
	xstrfmtcat(tmp, "%s", job_req_inx[0]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", job_req_inx[i]);
	}

	/* This is here to make sure we are looking at only this user
	 * if this flag is set.  We also include any accounts they may be
	 * coordinator of.
	 */
	if(!is_admin && (private_data & PRIVATE_DATA_JOBS)) {
		query = xstrdup_printf("SELECT lft FROM %s WHERE user_name='%s'",
				       assoc_table, user.name);
		if(user.coord_accts) {
			acct_coord_rec_t *coord = NULL;
			itr = list_iterator_create(user.coord_accts);
			while((coord = list_next(itr))) {
				xstrfmtcat(query, " OR acct='%s'",
					   coord->name);
			}
			list_iterator_destroy(itr);
		}
		result = DEF_QUERY_RET;
		if(!result) {
			xfree(cond);
			xfree(extra_table);
			list_destroy(job_list);
			if(local_cluster_list)
				list_destroy(local_cluster_list);
			return NULL;
		}

		set = 0;
		FOR_EACH_ROW {
			if(set) {
				xstrfmtcat(cond,
					   " OR (%s BETWEEN %s.lft AND %s.rgt)",
					   ROW(0), table_level, table_level);
			} else {
				set = 1;
				xstrfmtcat(cond,
					   " AND ((%s BETWEEN %s.lft "
					   "AND %s.rgt)",
					   ROW(0), table_level,
					   table_level);
			}
		} END_EACH_ROW;
		if(set)
			xstrcat(cond, ")");
		PQclear(result);
	}

	query = xstrdup_printf("SELECT %s FROM %s AS t1 LEFT JOIN %s AS t2 "
			       "ON t1.associd=t2.id",
			       tmp, job_table, assoc_table);
	xfree(tmp);
	if(extra_table) {
		xstrcat(query, extra_table);
		xfree(extra_table);
	}
	xstrcat(query, cond);
	xfree(cond);

	/* Here we want to order them this way in such a way so it is
	   easy to look for duplicates
	*/
	if(job_cond && !job_cond->duplicates)
		xstrcat(query, " ORDER BY t1.cluster, jobid, submit DESC;");
	else
		xstrcat(query, " ORDER BY t1.cluster, submit DESC;");

	result = DEF_QUERY_RET;
	if(!result) {
		list_destroy(job_list);
		if(local_cluster_list)
			list_destroy(local_cluster_list);
		return NULL;
	}

	FOR_EACH_ROW {
		char *id;
		int submit;
		bool job_ended = 0;

		id = ROW(JOB_REQ_ID);
		submit = atoi(ROW(JOB_REQ_SUBMIT));
		curr_id = atoi(ROW(JOB_REQ_JOBID));

		if(job_cond && !job_cond->duplicates && curr_id == last_id)
			continue;

		last_id = curr_id;

		/* check the bitmap to see if this is one of the jobs
		   we are looking for */
		if(!good_nodes_from_inx(local_cluster_list,
					(void **)&curr_cluster,
					ROW(JOB_REQ_NODE_INX), submit))
			continue;

		debug3("as/pg: get_job_conditions: job %d past node test", curr_id);

		job = create_jobacct_job_rec();
		list_append(job_list, job);

		job->alloc_cpus = atoi(ROW(JOB_REQ_ALLOC_CPUS));
		job->alloc_nodes = atoi(ROW(JOB_REQ_ALLOC_NODES));
		job->associd = atoi(ROW(JOB_REQ_ASSOCID));
		job->resvid = atoi(ROW(JOB_REQ_RESVID));

		/* we want a blank wckey if the name is null */
		if(ROW(JOB_REQ_WCKEY))
			job->wckey = xstrdup(ROW(JOB_REQ_WCKEY));
		else
			job->wckey = xstrdup("");
		job->wckeyid = atoi(ROW(JOB_REQ_WCKEYID));

		if(! ISEMPTY(JOB_REQ_CLUSTER)) /* ISNULL => ISEMPTY */
			job->cluster = xstrdup(ROW(JOB_REQ_CLUSTER));
		else if(! ISEMPTY(JOB_REQ_CLUSTER1))
			job->cluster = xstrdup(ROW(JOB_REQ_CLUSTER1));

		if(! ISNULL(JOB_REQ_USER_NAME))
			job->user = xstrdup(ROW(JOB_REQ_USER_NAME));
		else
			job->uid = atoi(ROW(JOB_REQ_UID));

		if(! ISNULL(JOB_REQ_LFT))
			job->lft = atoi(ROW(JOB_REQ_LFT));

		if(! ISEMPTY(JOB_REQ_ACCOUNT))
			job->account = xstrdup(ROW(JOB_REQ_ACCOUNT));
		else if(! ISEMPTY(JOB_REQ_ACCOUNT1))
			job->account = xstrdup(ROW(JOB_REQ_ACCOUNT1));

		if(! ISNULL(JOB_REQ_BLOCKID))
			job->blockid = xstrdup(ROW(JOB_REQ_BLOCKID));

		job->eligible = atoi(ROW(JOB_REQ_ELIGIBLE));
		job->submit = submit;
		job->start = atoi(ROW(JOB_REQ_START));
		job->end = atoi(ROW(JOB_REQ_END));

		/* since the job->end could be set later end it here */
		if(job->end) {
			job_ended = 1;
			if(!job->start || (job->start > job->end))
				job->start = job->end;
		}

		if(job_cond && !job_cond->without_usage_truncation
		   && job_cond->usage_start) {
			if(job->start && (job->start < job_cond->usage_start))
				job->start = job_cond->usage_start;

			if(!job->end || job->end > job_cond->usage_end)
				job->end = job_cond->usage_end;

			if(!job->start)
				job->start = job->end;

			job->elapsed = job->end - job->start;

			if(ROW(JOB_REQ_SUSPENDED)) {
				int local_start, local_end;

				/* get the suspended time for this job */
				query = xstrdup_printf(
					"SELECT start, endtime FROM %s WHERE "
					"(start < %d AND (endtime >= %d OR "
					"endtime = 0)) AND id=%s "
					"ORDER BY start",
					suspend_table, job_cond->usage_end,
					job_cond->usage_start, id);
				result2 = DEF_QUERY_RET;
				if(!result2) {
					list_destroy(job_list);
					job_list = NULL;
					break;
				}
				FOR_EACH_ROW2 {
					local_start = atoi(ROW2(0));
					local_end =  atoi(ROW2(1));
					if(!local_start)
						continue;

					if(job->start > local_start)
						local_start = job->start;
					if(job->end < local_end)
						local_end = job->end;

					if((local_end - local_start) < 1)
						continue;

					job->elapsed -=
						(local_end - local_start);
					job->suspended +=
						(local_end - local_start);
				} END_EACH_ROW2;
				PQclear(result2);
			}
		} else {
			job->suspended = atoi(ROW(JOB_REQ_SUSPENDED));

			if (job->state == JOB_SUSPENDED)
				job->suspended = now - job->suspended;

			if(!job->start) {
				job->elapsed = 0;
			} else if(!job->end) {
				job->elapsed = now - job->start;
			} else {
				job->elapsed = job->end - job->start;
			}

			job->elapsed -= job->suspended;
		}

		if((int)job->elapsed < 0)
			job->elapsed = 0;

		job->jobid = curr_id;
		job->jobname = xstrdup(ROW(JOB_REQ_NAME));
		job->gid = atoi(ROW(JOB_REQ_GID));
		job->exitcode = atoi(ROW(JOB_REQ_COMP_CODE));

		if(ROW(JOB_REQ_PARTITION))
			job->partition = xstrdup(ROW(JOB_REQ_PARTITION));

		if(ROW(JOB_REQ_NODELIST))
			job->nodes = xstrdup(ROW(JOB_REQ_NODELIST));

		if (!job->nodes || !strcmp(job->nodes, "(null)")) {
			xfree(job->nodes);
			job->nodes = xstrdup("(unknown)");
		}

		job->track_steps = atoi(ROW(JOB_REQ_TRACKSTEPS));
		job->priority = atoi(ROW(JOB_REQ_PRIORITY));
		job->req_cpus = atoi(ROW(JOB_REQ_REQ_CPUS));
		job->requid = atoi(ROW(JOB_REQ_KILL_REQUID));
		job->qos = atoi(ROW(JOB_REQ_QOS));
		job->show_full = 1;

		if(only_pending || (job_cond && job_cond->without_steps))
			goto skip_steps;

		if(job_cond && job_cond->step_list
		   && list_count(job_cond->step_list)) {
			set = 0;
			itr = list_iterator_create(job_cond->step_list);
			while((selected_step = list_next(itr))) {
				if(selected_step->jobid != job->jobid) {
					continue;
				} else if (selected_step->stepid
					   == (uint32_t)NO_VAL) {
					job->show_full = 1;
					break;
				}

				if(set)
					xstrcat(cond, " OR ");
				else
					xstrcat(cond, " AND (");

				xstrfmtcat(cond, "t1.stepid=%u",
					   selected_step->stepid);
				set = 1;
				job->show_full = 0;
			}
			list_iterator_destroy(itr);
			if(set)
				xstrcat(cond, ")");
		}
		for(i=0; i<STEP_REQ_COUNT; i++) {
			if(i)
				xstrcat(tmp, ", ");
			xstrcat(tmp, step_req_inx[i]);
		}
		query =	xstrdup_printf("SELECT %s FROM %s AS t1 WHERE t1.id=%s",
				       tmp, step_table, id);
		xfree(tmp);

		if(cond) {
			xstrcat(query, cond);
			xfree(cond);
		}

		result2 = DEF_QUERY_RET;
		if(!result2) {
			list_destroy(job_list);
			if(local_cluster_list)
				list_destroy(local_cluster_list);
			return NULL;
		}

		/* Querying the steps in the fashion was faster than
		   doing only 1 query and then matching the steps up
		   later with the job.
		*/
		FOR_EACH_ROW2 {
			/* check the bitmap to see if this is one of the steps
			   we are looking for */
			if(!good_nodes_from_inx(local_cluster_list,
						(void **)&curr_cluster,
						ROW2(STEP_REQ_NODE_INX),
						submit))
				continue;

			step = create_jobacct_step_rec();
			step->job_ptr = job;
			if(!job->first_step_ptr)
				job->first_step_ptr = step;
			list_append(job->steps, step);
			step->stepid = atoi(ROW2(STEP_REQ_STEPID));
			/* info("got step %u.%u", */
/* 			     job->header.jobnum, step->stepnum); */
			step->state = atoi(ROW2(STEP_REQ_STATE));
			step->exitcode = atoi(ROW2(STEP_REQ_COMP_CODE));
			step->ncpus = atoi(ROW2(STEP_REQ_CPUS));
			step->nnodes = atoi(ROW2(STEP_REQ_NODES));

			step->ntasks = atoi(ROW2(STEP_REQ_TASKS));
			step->task_dist = atoi(ROW2(STEP_REQ_TASKDIST));
			if(!step->ntasks)
				step->ntasks = step->ncpus;

			step->start = atoi(ROW2(STEP_REQ_START));

			step->end = atoi(ROW2(STEP_REQ_END));
			/* if the job has ended end the step also */
			if(!step->end && job_ended) {
				step->end = job->end;
				step->state = job->state;
			}

			if(job_cond && !job_cond->without_usage_truncation
			   && job_cond->usage_start) {
				if(step->start
				   && (step->start < job_cond->usage_start))
					step->start = job_cond->usage_start;

				if(!step->start && step->end)
					step->start = step->end;

				if(!step->end
				   || (step->end > job_cond->usage_end))
					step->end = job_cond->usage_end;
			}

			/* figure this out by start stop */
			step->suspended = atoi(ROW2(STEP_REQ_SUSPENDED));
			if(!step->end) {
				step->elapsed = now - step->start;
			} else {
				step->elapsed = step->end - step->start;
			}
			step->elapsed -= step->suspended;

			if((int)step->elapsed < 0)
				step->elapsed = 0;

			step->user_cpu_sec = atoi(ROW2(STEP_REQ_USER_SEC));
			step->user_cpu_usec =
				atoi(ROW2(STEP_REQ_USER_USEC));
			step->sys_cpu_sec = atoi(ROW2(STEP_REQ_SYS_SEC));
			step->sys_cpu_usec = atoi(ROW2(STEP_REQ_SYS_USEC));
			job->tot_cpu_sec +=
				step->tot_cpu_sec +=
				step->user_cpu_sec + step->sys_cpu_sec;
			job->tot_cpu_usec +=
				step->tot_cpu_usec +=
				step->user_cpu_usec + step->sys_cpu_usec;
			step->sacct.max_vsize =
				atoi(ROW2(STEP_REQ_MAX_VSIZE));
			step->sacct.max_vsize_id.taskid =
				atoi(ROW2(STEP_REQ_MAX_VSIZE_TASK));
			step->sacct.ave_vsize =
				atof(ROW2(STEP_REQ_AVE_VSIZE));
			step->sacct.max_rss =
				atoi(ROW2(STEP_REQ_MAX_RSS));
			step->sacct.max_rss_id.taskid =
				atoi(ROW2(STEP_REQ_MAX_RSS_TASK));
			step->sacct.ave_rss =
				atof(ROW2(STEP_REQ_AVE_RSS));
			step->sacct.max_pages =
				atoi(ROW2(STEP_REQ_MAX_PAGES));
			step->sacct.max_pages_id.taskid =
				atoi(ROW2(STEP_REQ_MAX_PAGES_TASK));
			step->sacct.ave_pages =
				atof(ROW2(STEP_REQ_AVE_PAGES));
			step->sacct.min_cpu =
				atoi(ROW2(STEP_REQ_MIN_CPU));
			step->sacct.min_cpu_id.taskid =
				atoi(ROW2(STEP_REQ_MIN_CPU_TASK));
			step->sacct.ave_cpu = atof(ROW2(STEP_REQ_AVE_CPU));
			step->stepname = xstrdup(ROW2(STEP_REQ_NAME));
			step->nodes = xstrdup(ROW2(STEP_REQ_NODELIST));
			step->sacct.max_vsize_id.nodeid =
				atoi(ROW2(STEP_REQ_MAX_VSIZE_NODE));
			step->sacct.max_rss_id.nodeid =
				atoi(ROW2(STEP_REQ_MAX_RSS_NODE));
			step->sacct.max_pages_id.nodeid =
				atoi(ROW2(STEP_REQ_MAX_PAGES_NODE));
			step->sacct.min_cpu_id.nodeid =
				atoi(ROW2(STEP_REQ_MIN_CPU_NODE));

			step->requid = atoi(ROW2(STEP_REQ_KILL_REQUID));
		} END_EACH_ROW2;
		PQclear(result2);

		if(!job->track_steps) {
			/* If we don't have track_steps we want to see
			   if we have multiple steps.  If we only have
			   1 step check the job name against the step
			   name in most all cases it will be
			   different.  If it is different print out
			   the step separate.
			*/
			if(list_count(job->steps) > 1)
				job->track_steps = 1;
			else if(step && step->stepname && job->jobname) {
				if(strcmp(step->stepname, job->jobname))
					job->track_steps = 1;
			}
		}
		/* need to reset here to make the above test valid */
	skip_steps:
		step = NULL;
	} END_EACH_ROW;
	PQclear(result);
	if(local_cluster_list)
		list_destroy(local_cluster_list);

	return job_list;

}

/*
 * js_p_archive - expire old job info from the storage
 *
 * IN pg_conn: database connection
 * IN arch_cond: which jobs to expire
 * RET: error code
 */
extern int
js_p_archive(pgsql_conn_t *pg_conn,
	     acct_archive_cond_t *arch_cond)
{
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return ESLURM_NOT_SUPPORTED;
}


/*
 * js_p_archive_load  - load old job info into the storage
 *
 * IN pg_conn: database connection
 * IN arch_rec: old job info
 * RET: error code
 */
extern int
js_p_archive_load(pgsql_conn_t *pg_conn,
		  acct_archive_rec_t *arch_rec)
{
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return ESLURM_NOT_SUPPORTED;
}
