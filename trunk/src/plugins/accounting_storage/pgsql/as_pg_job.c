/*****************************************************************************\
 *  as_pg_job.c - accounting interface to pgsql - job/step related functions.
 *
 *  $Id: as_pg_job.c 13061 2008-01-22 21:23:56Z da $
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
#include "as_pg_common.h"

char *job_table = "job_table";
static storage_field_t job_table_fields[] = {
	{ "job_db_inx", "SERIAL" },
	{ "deleted", "INTEGER DEFAULT 0 NOT NULL" },
	{ "account", "TEXT" },
	{ "partition", "TEXT NOT NULL" },
	{ "cpus_req", "INTEGER NOT NULL" },
	{ "cpus_alloc", "INTEGER NOT NULL" },
	{ "exit_code", "INTEGER DEFAULT 0 NOT NULL" },
	{ "job_name", "TEXT NOT NULL" },
	{ "id_assoc", "INTEGER NOT NULL" },
	{ "id_block", "TEXT" },
	{ "id_job", "INTEGER NOT NULL" },
	{ "id_qos", "INTEGER DEFAULT 0" },
	{ "id_resv", "INTEGER NOT NULL" },
	{ "id_wckey", "INTEGER NOT NULL" },
	{ "uid", "INTEGER NOT NULL" },
	{ "gid", "INTEGER NOT NULL" },
	{ "kill_requid", "INTEGER DEFAULT -1 NOT NULL" },
	{ "timelimit", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_submit", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_eligible", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_start", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_end", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_suspended", "INTEGER DEFAULT 0 NOT NULL" },
	{ "nodes_alloc", "INTEGER NOT NULL" },
	{ "nodelist", "TEXT" },
	{ "node_inx", "TEXT" },
	{ "priority", "INTEGER NOT NULL" },
	{ "state", "INTEGER NOT NULL" },
	{ "wckey", "TEXT DEFAULT '' NOT NULL" },
	{ "track_steps", "INTEGER NOT NULL" },
	{ NULL, NULL}
};
static char *job_table_constraint = ", "
	"PRIMARY KEY (job_db_inx), "
	"UNIQUE (id_job, id_assoc, time_submit) "
	")";

char *step_table = "step_table";
static storage_field_t step_table_fields[] = {
	{ "job_db_inx", "INTEGER NOT NULL" }, /* REFERENCES job_table */
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "cpus_alloc", "INTEGER NOT NULL" },
	{ "exit_code", "INTEGER DEFAULT 0 NOT NULL" },
	{ "id_step", "INTEGER NOT NULL" },
	{ "kill_requid", "INTEGER DEFAULT -1 NOT NULL" },
	{ "nodelist", "TEXT NOT NULL" },
	{ "nodes_alloc", "INTEGER NOT NULL" },
	{ "node_inx", "TEXT" },
	{ "state", "INTEGER NOT NULL" },
	{ "step_name", "TEXT NOT NULL" },
	{ "task_cnt", "INTEGER NOT NULL" },
	{ "task_dist", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_start", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_end", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_suspended", "INTEGER DEFAULT 0 NOT NULL" },
	{ "user_sec", "BIGINT DEFAULT 0 NOT NULL" },
	{ "user_usec", "BIGINT DEFAULT 0 NOT NULL" },
	{ "sys_sec", "BIGINT DEFAULT 0 NOT NULL" },
	{ "sys_usec", "BIGINT DEFAULT 0 NOT NULL" },
	{ "max_pages", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_pages_task", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_pages_node", "INTEGER DEFAULT 0 NOT NULL" },
	/* use "FLOAT" instead of "DOUBLE PRECISION" since only one
	   identifier supported in make_table_current() */
	{ "ave_pages", "FLOAT DEFAULT 0.0 NOT NULL" },
	{ "max_rss", "BIGINT DEFAULT 0 NOT NULL" },
	{ "max_rss_task", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_rss_node", "INTEGER DEFAULT 0 NOT NULL" },
	{ "ave_rss", "FLOAT DEFAULT 0.0 NOT NULL" },
	{ "max_vsize", "BIGINT DEFAULT 0 NOT NULL" },
	{ "max_vsize_task", "INTEGER DEFAULT 0 NOT NULL" },
	{ "max_vsize_node", "INTEGER DEFAULT 0 NOT NULL" },
	{ "ave_vsize", "FLOAT DEFAULT 0.0 NOT NULL" },
	{ "min_cpu", "BIGINT DEFAULT 0 NOT NULL" },
	{ "min_cpu_task", "INTEGER DEFAULT 0 NOT NULL" },
	{ "min_cpu_node", "INTEGER DEFAULT 0 NOT NULL" },
	{ "ave_cpu", "FLOAT DEFAULT 0.0 NOT NULL" },
	{ NULL, NULL}
};
static char *step_table_constraint = ", "
	"PRIMARY KEY (job_db_inx, id_step) "
	")";

char *suspend_table = "suspend_table";
static storage_field_t suspend_table_fields[] = {
	{ "job_db_inx", "INTEGER NOT NULL" }, /* REFERENCES job_table */
	{ "id_assoc", "INTEGER NOT NULL" },
	{ "time_start", "INTEGER DEFAULT 0 NOT NULL" },
	{ "time_end", "INTEGER DEFAULT 0 NOT NULL" },
	{ NULL, NULL}
};
static char *suspend_table_constraint = ")";

/*
 * _get_db_index - get ID in database of a job
 *
 * IN pg_conn: database connection
 * IN submit: submit time of job
 * IN jobid: jobid of job
 * IN associd: association id of job
 * RET: db id of job, 0 on error
 */
static int
_get_db_index(pgsql_conn_t *pg_conn, time_t submit, uint32_t jobid,
	      uint32_t associd)
{
	DEF_VARS;
	int db_index = 0;

	query = xstrdup_printf(
		"SELECT job_db_inx FROM %s.%s WHERE time_submit=%ld"
		" AND id_job=%u AND id_assoc=%u", pg_conn->cluster_name,
		job_table, submit, jobid, associd);
	result = DEF_QUERY_RET;
	if(!result)
		return 0;

	if(!PQntuples(result)) {
		debug("We can't get a db_index for this combo, "
		      "time_submit=%ld and id_job=%u and id_assoc=%u."
		      "We must not have heard about the start yet, "
		      "no big deal, we will get one right after this.",
		      submit, jobid, associd);
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
	time_t submit_time;

	if (job_ptr->resize_time)
		submit_time = job_ptr->resize_time;
	else
		submit_time = job_ptr->details->submit_time;

	if(!job_ptr->db_index) {
		job_ptr->db_index = _get_db_index(
			pg_conn,
			submit_time,
			job_ptr->job_id,
			job_ptr->assoc_id);
		if (!job_ptr->db_index) {
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
_create_function_add_job_start(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_job_start (rec %s.%s) "
		"RETURNS INTEGER AS $$"
		"DECLARE dbid INTEGER; "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s (job_db_inx, deleted, id_job, id_assoc, wckey, "
		"        id_wckey, uid, gid, partition, id_block, "
		"        account, time_eligible, time_submit, time_start, time_end, time_suspended, "
		"        timelimit, job_name, track_steps, state, exit_code, "
		"        priority, cpus_req, cpus_alloc, nodes_alloc, nodelist, "
		"        node_inx, kill_requid, id_qos, id_resv) "
		"      VALUES (DEFAULT, 0, rec.id_job, "
		"        rec.id_assoc, rec.wckey, rec.id_wckey, rec.uid, "
		"        rec.gid, rec.partition, rec.id_block, "
		"        rec.account, rec.time_eligible, rec.time_submit, rec.time_start, "
		"        rec.time_end, rec.time_suspended, rec.timelimit, rec.job_name, "
		"        rec.track_steps, rec.state, rec.exit_code, "
		"        rec.priority, rec.cpus_req, rec.cpus_alloc, "
		"        rec.nodes_alloc, rec.nodelist, rec.node_inx, "
		"        rec.kill_requid, rec.id_qos, rec.id_resv) "
		"      RETURNING job_db_inx INTO dbid; "
		"    RETURN dbid;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    \n-- create a new dbid for job?\n "
		"    \n-- id=nextval('%s.%s_id_seq'), \n"
		"    UPDATE %s.%s SET deleted=0, "
		"        wckey=rec.wckey, id_wckey=rec.id_wckey, uid=rec.uid, "
		"        gid=rec.gid, "
		"        partition=(CASE WHEN rec.partition!='' "
		"          THEN rec.partition ELSE partition END), "
		"        id_block=(CASE WHEN rec.id_block!='' "
		"          THEN rec.id_block ELSE id_block END), "
		"        account=(CASE WHEN rec.account!='' "
		"          THEN rec.account ELSE account END),"
		"        time_eligible=rec.time_eligible, time_submit=rec.time_submit,"
		"        time_start=rec.time_start, "
		"        timelimit=rec.timelimit, job_name=rec.job_name, "
		"        track_steps=rec.track_steps,"
		"        state=GREATEST(state, rec.state), "
		"        cpus_req=rec.cpus_req, cpus_alloc=rec.cpus_alloc,"
		"        nodes_alloc=rec.nodes_alloc,"
		"        node_inx=(CASE WHEN rec.node_inx!='' "
		"          THEN rec.node_inx ELSE node_inx END),"
		"        id_qos=rec.id_qos, id_resv=rec.id_resv "
		"      WHERE id_job=rec.id_job AND id_assoc=rec.id_assoc AND "
		"            time_submit=rec.time_submit"
		"      RETURNING job_db_inx INTO dbid; "
		"    IF FOUND THEN RETURN dbid; END IF;"
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster,
		cluster, job_table, cluster, job_table,
		cluster, job_table, cluster, job_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_step_start - create a PL/pgSQL function to
 *    add job step record
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_step_start(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_step_start (rec %s.%s) "
		"RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s (job_db_inx, id_step, time_start, step_name, state, "
		"        cpus_alloc, nodes_alloc, task_cnt, nodelist, node_inx, task_dist) "
		"      VALUES (rec.job_db_inx, rec.id_step, rec.time_start, rec.step_name,"
		"        rec.state, rec.cpus_alloc, rec.nodes_alloc, rec.task_cnt, "
		"        rec.nodelist, rec.node_inx, rec.task_dist);"
		"    RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET cpus_alloc=rec.cpus_alloc, nodes_alloc=rec.nodes_alloc, "
		"        task_cnt=rec.task_cnt, time_end=0, state=rec.state, "
		"        nodelist=rec.nodelist, node_inx=rec.node_inx, "
		"        task_dist=rec.task_dist, deleted=0 "
		"      WHERE job_db_inx=rec.job_db_inx AND id_step=rec.id_step;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster,	cluster,
		step_table, cluster, step_table, cluster, step_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_get_job_suspend_time - create a PL/pgSQL function to
 *    get suspended time of given job during specified period
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_get_job_suspend_time(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.get_job_suspend_time "
		"(dbid INTEGER, st INTEGER, et INTEGER) "
		"RETURNS INTEGER AS $$"
		"DECLARE susp INTEGER; "
		"BEGIN "
		"  IF et<=st THEN RETURN 0; END IF;"
		"  SELECT SUM((CASE WHEN (time_end=0 OR time_end>et) THEN et "
		"                   ELSE time_end END) "
		"           - (CASE WHEN time_start>st THEN time_start "
		"                     ELSE st END) "
		"            ) FROM %s.%s "
		"    INTO susp"
		"    WHERE (time_start!=0 AND time_start<et) AND "
		"          (time_end>=st OR time_end=0) AND job_db_inx=dbid "
		"    GROUP BY job_db_inx; "
		"  RETURN susp;"
		"END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		suspend_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * check_job_tables - check jobacct related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_job_tables(PGconn *db_conn, char *cluster)
{
	int rc;

	rc = check_table(db_conn, cluster, job_table, job_table_fields,
			 job_table_constraint);
	rc |= check_table(db_conn, cluster, step_table, step_table_fields,
			  step_table_constraint);
	rc |= check_table(db_conn, cluster, suspend_table, suspend_table_fields,
			  suspend_table_constraint);

	rc |= _create_function_add_job_start(db_conn, cluster);
	rc |= _create_function_add_step_start(db_conn, cluster);
	rc |= _create_function_get_job_suspend_time(db_conn, cluster);
	return rc;
}

/*
 * js_pg_job_start - load into the storage the start of a job
 *
 * IN pg_conn: database connection
 * IN cluster_name: cluster of the job
 * IN job_ptr: job just started
 * RET: error code
 */
extern int
js_pg_job_start(pgsql_conn_t *pg_conn,
		struct job_record *job_ptr)
{
	int rc=SLURM_SUCCESS, track_steps = 0, reinit = 0;
	char *jname = NULL, *nodes = NULL, *node_inx = NULL;
	char *block_id = NULL, *rec = NULL, *query = NULL;
	time_t begin_time, check_time, start_time, submit_time;
	int job_state, node_cnt = 0;
	uint32_t wckeyid = 0;

	if ((!job_ptr->details || !job_ptr->details->submit_time)
	    && !job_ptr->resize_time) {
		error("as/pg: job_start: Not inputing this job, "
		      "it has no submit time.");
		return SLURM_ERROR;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (! cluster_in_db(pg_conn, pg_conn->cluster_name) ) {
		error("cluster %s not in db", pg_conn->cluster_name);
		return SLURM_ERROR;
	}

	debug3("as/pg: job_start() called");

	job_state = job_ptr->job_state;

	/* Since we need a new db_inx make sure the old db_inx
	 * removed. This is most likely the only time we are going to
	 * be notified of the change also so make the state without
	 * the resize. */
	if(IS_JOB_RESIZING(job_ptr)) {
		/* If we have a db_index lets end the previous record. */
		if(job_ptr->db_index)
			js_pg_job_complete(pg_conn, job_ptr);
		else
			error("We don't have a db_index for job %u, "
			      "this should never happen.", job_ptr->job_id);
		job_state &= (~JOB_RESIZING);
		job_ptr->db_index = 0;
	}

	job_state &= JOB_STATE_BASE;

	if (job_ptr->resize_time) {
		begin_time  = job_ptr->resize_time;
		submit_time = job_ptr->resize_time;
		start_time  = job_ptr->resize_time;
	} else {
		begin_time  = job_ptr->details->begin_time;
		submit_time = job_ptr->details->submit_time;
		start_time  = job_ptr->start_time;
	}

	/* See what we are hearing about here if no start time. If
	 * this job latest time is before the last roll up we will
	 * need to reset it to look at this job. */
	if (start_time)
		check_time = start_time;
	else if (begin_time)
		check_time = begin_time;
	else
		check_time = submit_time;

	slurm_mutex_lock(&usage_rollup_lock);
	if(check_time < global_last_rollup) {
		PGresult *result = NULL;
		/* check to see if we are hearing about this time for the
		 * first time.
		 */
		query = xstrdup_printf(
			"SELECT job_db_inx FROM %s.%s WHERE id_job=%u AND "
			"time_submit=%ld AND time_eligible=%ld AND time_start=%ld",
			pg_conn->cluster_name, job_table, job_ptr->job_id,
			submit_time, begin_time, start_time);
		result = DEF_QUERY_RET;
		if(!result) {
			 slurm_mutex_unlock(&usage_rollup_lock);
			return SLURM_ERROR;
		}
		if (PQntuples(result) != 0) {
			PQclear(result);
			debug4("revieved an update for a "
			       "job (%u) already known about",
			       job_ptr->job_id);
			 slurm_mutex_unlock(&usage_rollup_lock);
			goto no_rollup_change;
		}
		PQclear(result);

		if(job_ptr->start_time)
			debug("Need to reroll usage from %s Job %u "
			      "from %s started then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, pg_conn->cluster_name);
		else if(begin_time)
			debug("Need to reroll usage from %s Job %u "
			      "from %s became eligible then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, pg_conn->cluster_name);
		else
			debug("Need to reroll usage from %s Job %u "
			      "from %s was submitted then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, pg_conn->cluster_name);

		global_last_rollup = check_time;
		slurm_mutex_unlock(&usage_rollup_lock);

		query = xstrdup_printf("UPDATE %s.%s SET hourly_rollup=%ld, "
				       "daily_rollup=%ld, monthly_rollup=%ld",
				       pg_conn->cluster_name, last_ran_table,
				       check_time, check_time, check_time);
		rc = DEF_QUERY_RET_RC;
	} else
		slurm_mutex_unlock(&usage_rollup_lock);

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
		node_cnt = job_ptr->total_nodes;
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
		node_cnt = job_ptr->total_nodes;
#endif
	}

	/* If there is a start_time get the wckeyid.  If the job is
	 * cancelled before the job starts we also want to grab it. */
	if(job_ptr->assoc_id &&
	   (job_ptr->start_time || IS_JOB_CANCELLED(job_ptr)))
		wckeyid = get_wckeyid(pg_conn, &job_ptr->wckey,
				      job_ptr->user_id, pg_conn->cluster_name,
				      job_ptr->assoc_id);

	if(!job_ptr->db_index) {
		if (!begin_time)
			begin_time = submit_time;

		rec = xstrdup_printf(
			"(0, 0, '%s', '%s', %d, %d, 0, '%s', "
			"%d, '%s', %d, %d, %d, %d, %d, %d, 0, "
			"%d, %ld, %ld, %ld, 0, 0, "
			"%d, '%s', '%s', %d, %d, '%s', %d)",
			/* job_db_inx=0, not used */
			/* deleted=0 */
			job_ptr->account ?: "",     /* account */
			job_ptr->partition ?: "",   /* partition */
			(int)job_ptr->details->min_cpus, /* cpus_req */
			(int)job_ptr->total_cpus, /* cpus_alloc */
			/* exit_code=0 */
			jname,  /* job_name */

			(int)job_ptr->assoc_id, /* id_assoc */
			block_id ?: "", /* id_block */
			(int)job_ptr->job_id,   /* id_job */
			(int)job_ptr->qos_id,   /* id_qos */
			(int)job_ptr->resv_id,  /* id_resv */
			(int)wckeyid,           /* id_wckey */
			(int)job_ptr->user_id,  /* uid */
			(int)job_ptr->group_id, /* gid */
			/* kill_requid=0 */

			(int)job_ptr->time_limit, /* timelimit */
			submit_time,         /* time_submit */
			begin_time,          /* time_eligible */
			start_time,          /* time_start */
			/* time_end=0 */
			/* time_suspended=0 */

			(int)node_cnt, /* nodes_alloc */
			nodes ?: "",                    /* nodelist */
			node_inx ?: "",         /* node_inx */
			(int)job_ptr->priority, /* priority */
			(int)job_state,         /* state */
			job_ptr->wckey ?: "",   /* wckey */
			(int)track_steps);

		query = xstrdup_printf("SELECT %s.add_job_start(%s);",
				       pg_conn->cluster_name, rec);
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
		query = xstrdup_printf("UPDATE %s.%s SET nodelist='%s', ",
				       pg_conn->cluster_name, job_table, nodes);
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

		xstrfmtcat(query, "time_start=%ld, job_name='%s', state=%d, "
			   "cpus_alloc=%d, nodes_alloc=%d, id_assoc=%d, "
			   "id_wckey=%d, id_resv=%d, timelimit=%d "
			   "WHERE job_db_inx=%d;",
			   start_time, jname, job_state,
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
 * js_pg_job_complete - load into the storage the end of a job
 *
 * IN pg_conn: database connection
 * IN job_ptr: job completed
 * RET error code
 */
extern int
js_pg_job_complete(pgsql_conn_t *pg_conn,
		   struct job_record *job_ptr)
{
	char *query = NULL, *nodes = NULL;
	int rc = SLURM_SUCCESS, job_state;
	time_t end_time;

	if (!job_ptr->db_index
	    && ((!job_ptr->details || !job_ptr->details->submit_time)
		&& !job_ptr->resize_time)) {
		error("jobacct_storage_p_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (! cluster_in_db(pg_conn, pg_conn->cluster_name) ) {
		error("cluster %s not in db", pg_conn->cluster_name);
		return SLURM_ERROR;
	}

	debug2("as/pg: job_complete() called");

	if (IS_JOB_RESIZING(job_ptr)) {
		end_time = job_ptr->resize_time;
		job_state = JOB_RESIZING;
	} else {
		/* If we get an error with this just fall through to avoid an
		 * infinite loop */
		if (job_ptr->end_time == 0) {
			debug("as/pg: job_complete: job %u never started",
			      job_ptr->job_id);
			return SLURM_SUCCESS;
		}
		end_time = job_ptr->end_time;
		job_state = job_ptr->job_state & JOB_STATE_BASE;
	}

	slurm_mutex_lock(&usage_rollup_lock);
	if(end_time < global_last_rollup) {
		global_last_rollup = job_ptr->end_time;
		slurm_mutex_unlock(&usage_rollup_lock);

		query = xstrdup_printf("UPDATE %s.%s SET hourly_rollup=%ld, "
				       "daily_rollup=%ld, monthly_rollup=%ld",
				       pg_conn->cluster_name, last_ran_table,
				       end_time, end_time, end_time);
		rc = DEF_QUERY_RET_RC;
	} else
		slurm_mutex_unlock(&usage_rollup_lock);

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "None assigned";

	/* If we get an error with this just fall
	 * through to avoid an infinite loop
	 */
	if (_check_job_db_index(pg_conn, job_ptr) != SLURM_SUCCESS)
		return SLURM_SUCCESS;

	query = xstrdup_printf(
		"UPDATE %s.%s SET time_end=%ld, state=%d, nodelist='%s', "
		"exit_code=%d, kill_requid=%d WHERE job_db_inx=%d",
		pg_conn->cluster_name, job_table, end_time, job_state,
		nodes, job_ptr->exit_code,
		job_ptr->requid, job_ptr->db_index);
	rc = DEF_QUERY_RET_RC;
	return  rc;
}


/*
 * js_pg_step_start - load into the storage the start of a job step
 *
 * IN pg_conn: database connection
 * IN step_ptr: step just started
 * RET: error code
 */
extern int
js_pg_step_start(pgsql_conn_t *pg_conn,
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
	time_t start_time, submit_time;

	if (! cluster_in_db(pg_conn, pg_conn->cluster_name) ) {
		error("cluster %s not in db", pg_conn->cluster_name);
		return SLURM_ERROR;
	}

	if (!step_ptr->job_ptr->db_index
	    && ((!step_ptr->job_ptr->details
		 || !step_ptr->job_ptr->details->submit_time)
		&& !step_ptr->job_ptr->resize_time)) {
		error("jobacct_storage_p_step_start: "
		      "Not inputing this job step, it has no submit time.");
		return SLURM_ERROR;
	}

	if (step_ptr->job_ptr->resize_time) {
		submit_time = start_time = step_ptr->job_ptr->resize_time;
		if(step_ptr->start_time > submit_time)
			start_time = step_ptr->start_time;
	} else {
		start_time = step_ptr->start_time;
		submit_time = step_ptr->job_ptr->details->submit_time;
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
			nodes = step_ptr->job_ptr->total_nodes;
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

	rec = xstrdup_printf("(%d, 0, %d, 0, %d, -1, '%s', %d, '%s', %d,"
			     "'%s', %d, %d, %ld, 0, 0, "
			     "0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0)",
			     (int)step_ptr->job_ptr->db_index, /* job_db_inx */
			     /* deleted=0 */
			     (int)cpus, /* cpus_alloc */
			     /* exit_code=0 */
			     (int)step_ptr->step_id, /* id_step */
			     /* kill_requid=-1 */
			     node_list,         /* nodelist */
			     (int)nodes,        /* nodes_alloc */
			     node_inx,          /* node_inx */
			     (int)JOB_RUNNING,  /* state */

			     step_ptr->name ?: "", /* step_name */
			     (int)tasks,           /* task_cnt */
			     (int)task_dist,       /* task_dist */
			     start_time       /* time_start */
			     /* time_end=0 */
			     /* time_suspended=0 */

			     /* resouce usage all 0 */
		);
	query = xstrdup_printf("SELECT %s.add_step_start(%s)",
			       pg_conn->cluster_name, rec);
	xfree(rec);
	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * js_pg_step_complete - load into the storage the end of a job step
 *
 * IN pg_conn: database connection
 * IN step_ptr: step completed
 * RET: error code
 */
extern int
js_pg_step_complete(pgsql_conn_t *pg_conn,
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
	time_t start_time, submit_time;

	if (! cluster_in_db(pg_conn, pg_conn->cluster_name) ) {
		error("cluster %s not in db", pg_conn->cluster_name);
		return SLURM_ERROR;
	}

	if (!step_ptr->job_ptr->db_index
	    && ((!step_ptr->job_ptr->details
		 || !step_ptr->job_ptr->details->submit_time)
		&& !step_ptr->job_ptr->resize_time)) {
		error("jobacct_storage_p_step_complete: "
		      "Not inputing this job step, it has no submit time.");
		return SLURM_ERROR;
	}

	if (step_ptr->job_ptr->resize_time) {
		submit_time = start_time = step_ptr->job_ptr->resize_time;
		if(step_ptr->start_time > submit_time)
			start_time = step_ptr->start_time;
	} else {
		start_time = step_ptr->start_time;
		submit_time = step_ptr->job_ptr->details->submit_time;
	}

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (jobacct == NULL) {
		/* JobAcctGather=slurmdb_gather/none, no data to process */
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

	if ((elapsed = (now - start_time)) < 0)
		elapsed = 0;	/* For *very* short jobs, if clock is wrong */

	exit_code = step_ptr->exit_code;
	if (WIFSIGNALED(exit_code)) {
		comp_status = JOB_CANCELLED;
	} else if (exit_code)
		comp_status = JOB_FAILED;
	else {
		step_ptr->requid = -1;
		comp_status = JOB_COMPLETE;
	}

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
	}

	if(jobacct->min_cpu != (uint32_t)NO_VAL) {
		ave_cpu2 = (double)jobacct->min_cpu;
	}

	if (_check_job_db_index(pg_conn, step_ptr->job_ptr)
	    != SLURM_SUCCESS)
		return SLURM_SUCCESS;

	query = xstrdup_printf(
		"UPDATE %s.%s SET time_end=%ld, state=%d, "
		"kill_requid=%d, exit_code=%d, "
		"user_sec=%d, user_usec=%d, "
		"sys_sec=%d, sys_usec=%d, "
		"max_vsize=%d, max_vsize_task=%d, "
		"max_vsize_node=%d, ave_vsize=%.2f, "
		"max_rss=%d, max_rss_task=%d, "
		"max_rss_node=%d, ave_rss=%.2f, "
		"max_pages=%d, max_pages_task=%d, "
		"max_pages_node=%d, ave_pages=%.2f, "
		"min_cpu=%.2f, min_cpu_task=%d, "
		"min_cpu_node=%d, ave_cpu=%.2f "
		"WHERE job_db_inx=%d and id_step=%d",
		pg_conn->cluster_name,
		step_table, (long)now,
		(int)comp_status,
		(int)step_ptr->requid,
		(int)exit_code,
		/* user seconds */
		(int)jobacct->user_cpu_sec,
		/* user microseconds */
		(int)jobacct->user_cpu_usec,
		/* system seconds */
		(int)jobacct->sys_cpu_sec,
		/* system microsecs */
		(int)jobacct->sys_cpu_usec,
		(int)jobacct->max_vsize,	/* max vsize */
		(int)jobacct->max_vsize_id.taskid,	/* max vsize task */
		(int)jobacct->max_vsize_id.nodeid,	/* max vsize node */
		ave_vsize,	/* ave vsize */
		(int)jobacct->max_rss,	/* max vsize */
		(int)jobacct->max_rss_id.taskid,	/* max rss task */
		(int)jobacct->max_rss_id.nodeid,	/* max rss node */
		ave_rss,	/* ave rss */
		(int)jobacct->max_pages,	/* max pages */
		(int)jobacct->max_pages_id.taskid,	/* max pages task */
		(int)jobacct->max_pages_id.nodeid,	/* max pages node */
		ave_pages,	/* ave pages */
		ave_cpu2,	/* min cpu */
		(int)jobacct->min_cpu_id.taskid,	/* min cpu task */
		(int)jobacct->min_cpu_id.nodeid,	/* min cpu node */
		ave_cpu,	/* ave cpu */
		(int)step_ptr->job_ptr->db_index, (int)step_ptr->step_id);
	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * js_pg_suspend - load into the storage a suspention of a job
 *
 * IN pg_conn: database connection
 * IN job_ptr: job suspended
 */
extern int
js_pg_suspend(pgsql_conn_t *pg_conn, uint32_t old_db_inx,
 	      struct job_record *job_ptr)
{
 	char *query = NULL;
 	int rc = SLURM_SUCCESS;
 	time_t submit_time;
 	uint32_t job_db_inx;

 	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
 		return ESLURM_DB_CONNECTION;

 	if (! cluster_in_db(pg_conn, pg_conn->cluster_name) ) {
 		error("cluster %s not in db", pg_conn->cluster_name);
 		return SLURM_ERROR;
 	}

 	if (job_ptr->resize_time)
 		submit_time = job_ptr->resize_time;
 	else
 		submit_time = job_ptr->details->submit_time;

 	if (_check_job_db_index(pg_conn, job_ptr) != SLURM_SUCCESS)
 		return SLURM_SUCCESS;

 	if(IS_JOB_RESIZING(job_ptr)) {
 		if(!old_db_inx) {
 			error("No old db inx given for job %u cluster %s, "
 			      "can't update suspend table.",
 			      job_ptr->job_id, pg_conn->cluster_name);
 			return SLURM_ERROR;
 		}
 		job_db_inx = old_db_inx;
 		xstrfmtcat(query,
 			   "UPDATE %s.%s SET time_end=%ld WHERE "
 			   "job_db_inx=%u AND time_end=0;",
 			   pg_conn->cluster_name, suspend_table,
 			   job_ptr->suspend_time, job_db_inx);

 	} else
 		job_db_inx = job_ptr->db_index;

 	query = xstrdup_printf(
 		"UPDATE %s.%s SET time_suspended=%d-time_suspended, state=%d "
 		"WHERE job_db_inx=%d", pg_conn->cluster_name, job_table,
 		(int)job_ptr->suspend_time,
 		(int)(job_ptr->job_state & JOB_STATE_BASE),
 		(int)job_ptr->db_index);

 	if(IS_JOB_SUSPENDED(job_ptr))
 		xstrfmtcat(query,
 			   "INSERT INTO %s.%s (job_db_inx, id_assoc, "
 			   "  time_start, time_end) VALUES (%d, %d, %ld, 0);",
 			   pg_conn->cluster_name, suspend_table,
 			   (int)job_ptr->db_index,
 			   (int)job_ptr->assoc_id,
 			   job_ptr->suspend_time);
 	else
 		xstrfmtcat(query,
 			   "UPDATE %s.%s SET time_end=%ld WHERE job_db_inx=%d "
 			   "  AND time_end=0;", pg_conn->cluster_name,
 			   suspend_table, job_ptr->suspend_time,
 			   job_ptr->db_index);

 	rc = DEF_QUERY_RET_RC;
 	if(rc == SLURM_SUCCESS) {
 		query = xstrdup_printf(
 			"UPDATE %s.%s SET time_suspended=%d-time_suspended, "
 			"state=%d WHERE job_db_inx=%d and time_end=0",
 			pg_conn->cluster_name,
 			step_table, (int)job_ptr->suspend_time,
 			(int)job_ptr->job_state, (int)job_ptr->db_index);
 		rc = DEF_QUERY_RET_RC;
 	}
 	return rc;
}

extern int
as_pg_flush_jobs_on_cluster(pgsql_conn_t *pg_conn, time_t event_time)
{
 	DEF_VARS;
 	int rc = SLURM_SUCCESS;
 	/* put end times for a clean start */
 	char *id_char = NULL;
 	char *suspended_char = NULL;

 	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
 		return ESLURM_DB_CONNECTION;

 	if (! cluster_in_db(pg_conn, pg_conn->cluster_name) ) {
 		error("cluster %s not in db", pg_conn->cluster_name);
 		return SLURM_ERROR;
 	}

 	/* First we need to get the job_db_inx's and states so we can clean up
 	 * the suspend table and the step table
 	 */
 	query = xstrdup_printf(
 		"SELECT DISTINCT job_db_inx,state FROM %s.%s "
 		"WHERE time_end=0;", pg_conn->cluster_name, job_table);
 	result = DEF_QUERY_RET;
 	if (!result)
 		return SLURM_ERROR;

 	FOR_EACH_ROW {
 		int state = atoi(ROW(1));
 		if(state == JOB_SUSPENDED) {
 			if(suspended_char)
 				xstrfmtcat(suspended_char,
 					   " OR job_db_inx=%s", ROW(0));
 			else
 				xstrfmtcat(suspended_char, "job_db_inx=%s",
 					   ROW(0));
 		}

 		if(id_char)
 			xstrfmtcat(id_char, " OR job_db_inx=%s", ROW(0));
 		else
 			xstrfmtcat(id_char, "job_db_inx=%s", ROW(0));
 	} END_EACH_ROW;
 	PQclear(result);

 	if(suspended_char) {
 		xstrfmtcat(query,
 			   "UPDATE %s.%s SET time_suspended=%ld-time_suspended "
 			   "WHERE %s;", pg_conn->cluster_name, job_table,
 			   (long)event_time, suspended_char);
 		xstrfmtcat(query,
 			   "UPDATE %s.%s SET time_suspended=%ld-time_suspended "
 			   "WHERE %s;", pg_conn->cluster_name, step_table,
 			   (long)event_time, suspended_char);
 		xstrfmtcat(query,
 			   "UPDATE %s.%s SET time_end=%ld WHERE (%s) "
 			   "AND time_end=0;", pg_conn->cluster_name,
 			   suspend_table, (long)event_time,
 			   suspended_char);
 		xfree(suspended_char);
 	}
 	if(id_char) {
 		xstrfmtcat(query,
 			   "UPDATE %s.%s SET state=%d, time_end=%ld WHERE %s;",
 			   pg_conn->cluster_name, job_table,
 			   (int)JOB_CANCELLED, (long)event_time, id_char);
 		xstrfmtcat(query,
 			   "UPDATE %s.%s SET state=%d, time_end=%ld WHERE %s;",
 			   pg_conn->cluster_name, step_table,
 			   (int)JOB_CANCELLED, (long)event_time, id_char);
 		xfree(id_char);
 	}

 	if(query)
 		rc = DEF_QUERY_RET_RC;

 	return rc;
}


extern int
cluster_has_running_jobs(pgsql_conn_t *pg_conn, char *cluster)
{
 	int rc = 0;
 	PGresult *result;
 	char *query = xstrdup_printf(
 		"SELECT t0.id_assoc FROM %s AS t0, %s AS t1"
 		"  WHERE t0.id_assoc=t1.id_assoc AND t0.state=%u LIMIT 1;",
 		job_table, assoc_table, JOB_RUNNING);
 	result = DEF_QUERY_RET;
 	if (!result) {
 		error("failed to get jobs for cluster %s", cluster);
 		return rc;
 	}
 	rc = PQntuples(result);
 	PQclear(result);
 	return rc;
}
