/*****************************************************************************\
 *  mysql_convert.c - functions dealing with converting from tables in
 *                    slurm <= 2.1.
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

#include "mysql_convert.h"

extern int mysql_convert_tables(MYSQL *db_conn)
{
	storage_field_t assoc_table_fields_2_1[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "user", "tinytext not null default ''" },
		{ "acct", "tinytext not null" },
		{ "cluster", "tinytext not null" },
		{ "partition", "tinytext not null default ''" },
		{ "parent_acct", "tinytext not null default ''" },
		{ "lft", "int not null" },
		{ "rgt", "int not null" },
		{ "fairshare", "int default 1 not null" },
		{ "max_jobs", "int default NULL" },
		{ "max_submit_jobs", "int default NULL" },
		{ "max_cpus_per_job", "int default NULL" },
		{ "max_nodes_per_job", "int default NULL" },
		{ "max_wall_duration_per_job", "int default NULL" },
		{ "max_cpu_mins_per_job", "bigint default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_cpus", "int default NULL" },
		{ "grp_nodes", "int default NULL" },
		{ "grp_wall", "int default NULL" },
		{ "grp_cpu_mins", "bigint default NULL" },
		{ "qos", "blob not null default ''" },
		{ "delta_qos", "blob not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t assoc_usage_table_fields_2_1[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null" },
		{ "period_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	storage_field_t cluster_usage_table_fields_2_1[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "cluster", "tinytext not null" },
		{ "period_start", "int unsigned not null" },
		{ "cpu_count", "int default 0" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "down_cpu_secs", "bigint default 0" },
		{ "pdown_cpu_secs", "bigint default 0" },
		{ "idle_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	storage_field_t event_table_fields_2_1[] = {
		{ "node_name", "tinytext default '' not null" },
		{ "cluster", "tinytext not null" },
		{ "cpu_count", "int not null" },
		{ "state", "smallint unsigned default 0 not null" },
		{ "period_start", "int unsigned not null" },
		{ "period_end", "int unsigned default 0 not null" },
		{ "reason", "tinytext not null" },
		{ "reason_uid", "int unsigned default 0xfffffffe not null" },
		{ "cluster_nodes", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t job_table_fields_2_1[] = {
		{ "id", "int not null auto_increment" },
		{ "deleted", "tinyint default 0" },
		{ "jobid", "int unsigned not null" },
		{ "associd", "int unsigned not null" },
		{ "wckey", "tinytext not null default ''" },
		{ "wckeyid", "int unsigned not null" },
		{ "uid", "int unsigned not null" },
		{ "gid", "int unsigned not null" },
		{ "cluster", "tinytext not null" },
		{ "partition", "tinytext not null" },
		{ "blockid", "tinytext" },
		{ "account", "tinytext" },
		{ "eligible", "int unsigned default 0 not null" },
		{ "submit", "int unsigned default 0 not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "timelimit", "int unsigned default 0 not null" },
		{ "name", "tinytext not null" },
		{ "track_steps", "tinyint not null" },
		{ "state", "smallint unsigned not null" },
		{ "comp_code", "int default 0 not null" },
		{ "priority", "int not null" },
		{ "req_cpus", "int unsigned not null" },
		{ "alloc_cpus", "int unsigned not null" },
		{ "alloc_nodes", "int unsigned not null" },
		{ "nodelist", "text" },
		{ "node_inx", "text" },
		{ "kill_requid", "int default -1 not null" },
		{ "qos", "smallint default 0" },
		{ "resvid", "int unsigned not null" },
		{ NULL, NULL}
	};

	storage_field_t resv_table_fields_2_1[] = {
		{ "id", "int unsigned default 0 not null" },
		{ "name", "text not null" },
		{ "cluster", "text not null" },
		{ "deleted", "tinyint default 0" },
		{ "cpus", "int unsigned not null" },
		{ "assoclist", "text not null default ''" },
		{ "nodelist", "text not null default ''" },
		{ "node_inx", "text not null default ''" },
		{ "start", "int unsigned default 0 not null"},
		{ "end", "int unsigned default 0 not null" },
		{ "flags", "smallint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields_2_1[] = {
		{ "id", "int not null" },
		{ "deleted", "tinyint default 0" },
		{ "stepid", "smallint not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "name", "text not null" },
		{ "nodelist", "text not null" },
		{ "node_inx", "text" },
		{ "state", "smallint unsigned not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "comp_code", "int default 0 not null" },
		{ "nodes", "int unsigned not null" },
		{ "cpus", "int unsigned not null" },
		{ "tasks", "int unsigned not null" },
		{ "task_dist", "smallint default 0" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "max_vsize", "bigint unsigned default 0 not null" },
		{ "max_vsize_task", "smallint unsigned default 0 not null" },
		{ "max_vsize_node", "int unsigned default 0 not null" },
		{ "ave_vsize", "double unsigned default 0.0 not null" },
		{ "max_rss", "bigint unsigned default 0 not null" },
		{ "max_rss_task", "smallint unsigned default 0 not null" },
		{ "max_rss_node", "int unsigned default 0 not null" },
		{ "ave_rss", "double unsigned default 0.0 not null" },
		{ "max_pages", "int unsigned default 0 not null" },
		{ "max_pages_task", "smallint unsigned default 0 not null" },
		{ "max_pages_node", "int unsigned default 0 not null" },
		{ "ave_pages", "double unsigned default 0.0 not null" },
		{ "min_cpu", "int unsigned default 0 not null" },
		{ "min_cpu_task", "smallint unsigned default 0 not null" },
		{ "min_cpu_node", "int unsigned default 0 not null" },
		{ "ave_cpu", "double unsigned default 0.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t suspend_table_fields_2_1[] = {
		{ "id", "int not null" },
		{ "associd", "int not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t wckey_table_fields_2_1[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "name", "tinytext not null default ''" },
		{ "cluster", "tinytext not null" },
		{ "user", "tinytext not null" },
		{ NULL, NULL}
	};

	storage_field_t wckey_usage_table_fields_2_1[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null" },
		{ "period_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	ListIterator itr = NULL;
	char *cluster_name;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *id_str = NULL;
	char *query = NULL;
	int rc;
	bool assocs=0, events=0, jobs=0, resvs=0, steps=0,
		suspends=0, usage=0, wckeys=0;

	slurm_mutex_lock(&mysql_cluster_list_lock);

	/* now do associations */
	query = xstrdup_printf("show tables like '%s';", assoc_table);

	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, assoc_table,
					 assoc_table_fields_2_1,
					 ", primary key (id), "
					 " unique index (user(20), acct(20), "
					 "cluster(20), partition(20)))")
		   == SLURM_ERROR)
			goto end_it;
		if(mysql_db_create_table(db_conn, assoc_day_table,
					 assoc_usage_table_fields_2_1,
					 ", primary key (id, period_start))")
		   == SLURM_ERROR)
			goto end_it;

		if(mysql_db_create_table(db_conn, assoc_hour_table,
					 assoc_usage_table_fields_2_1,
					 ", primary key (id, period_start))")
		   == SLURM_ERROR)
			goto end_it;

		if(mysql_db_create_table(db_conn, assoc_month_table,
					 assoc_usage_table_fields_2_1,
					 ", primary key (id, period_start))")
		   == SLURM_ERROR)
			goto end_it;

		assocs = 1;
	}
	mysql_free_result(result);
	result = NULL;

	/* now do events */
	query = xstrdup_printf("show tables like 'cluster_event_table';");
	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, "cluster_event_table",
					 event_table_fields_2_1,
					 ", primary key (node_name(20), "
					 "cluster(20), period_start))")
		   == SLURM_ERROR)
			goto end_it;
		events = 1;
	}
	mysql_free_result(result);
	result = NULL;

	/* now do jobs */
	query = xstrdup_printf("show tables like '%s';", job_table);

	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, job_table,
					 job_table_fields_2_1,
					 ", primary key (id), "
					 "unique index (jobid, "
					 "associd, submit))")
		   == SLURM_ERROR)
			goto end_it;
		jobs = 1;
	}
	mysql_free_result(result);
	result = NULL;

	/* now do reservations */
	query = xstrdup_printf("show tables like '%s';", resv_table);

	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, resv_table,
					 resv_table_fields_2_1,
					 ", primary key (id, start, "
					 "cluster(20)))")
		   == SLURM_ERROR)
			goto end_it;
		resvs = 1;
	}
	mysql_free_result(result);
	result = NULL;

	/* now do steps */
	query = xstrdup_printf("show tables like '%s';", step_table);

	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, step_table,
					 step_table_fields_2_1,
					 ", primary key (id, stepid))")
		   == SLURM_ERROR)
			goto end_it;
		steps = 1;
	}
	mysql_free_result(result);
	result = NULL;

	/* now do suspends */
	query = xstrdup_printf("show tables like '%s';", suspend_table);

	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, suspend_table,
					 suspend_table_fields_2_1,
					 ")")
		   == SLURM_ERROR)
			goto end_it;
		suspends = 1;
	}
	mysql_free_result(result);
	result = NULL;

	/* now do usage */
	query = xstrdup_printf("show tables like '%s';", cluster_hour_table);

	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, cluster_day_table,
					 cluster_usage_table_fields_2_1,
					 ", primary key (cluster(20), "
					 "period_start))")
		   == SLURM_ERROR)
			goto end_it;

		if(mysql_db_create_table(db_conn, cluster_hour_table,
					 cluster_usage_table_fields_2_1,
					 ", primary key (cluster(20), "
					 "period_start))")
		   == SLURM_ERROR)
			goto end_it;

		if(mysql_db_create_table(db_conn, cluster_month_table,
					 cluster_usage_table_fields_2_1,
					 ", primary key (cluster(20), "
					 "period_start))")
		   == SLURM_ERROR)
			goto end_it;

		usage = 1;
	}
	mysql_free_result(result);
	result = NULL;

	/* now do wckeys */
	query = xstrdup_printf("show tables like '%s';", wckey_table);

	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, wckey_table,
					 wckey_table_fields_2_1,
					 ", primary key (id), "
					 " unique index (name(20), user(20), "
					 "cluster(20)))")
		   == SLURM_ERROR)
			goto end_it;
		if(mysql_db_create_table(db_conn, wckey_day_table,
					 wckey_usage_table_fields_2_1,
					 ", primary key (id, period_start))")
		   == SLURM_ERROR)
			goto end_it;

		if(mysql_db_create_table(db_conn, wckey_hour_table,
					 wckey_usage_table_fields_2_1,
					 ", primary key (id, period_start))")
		   == SLURM_ERROR)
			goto end_it;

		if(mysql_db_create_table(db_conn, wckey_month_table,
					 wckey_usage_table_fields_2_1,
					 ", primary key (id, period_start))")
		   == SLURM_ERROR)
			goto end_it;
		wckeys = 1;
	}
	mysql_free_result(result);
	result = NULL;

	/* now convert to new form */
	itr = list_iterator_create(mysql_cluster_list);
	while((cluster_name = list_next(itr))) {
		xfree(id_str);

		if(assocs) {
			//char *assoc_id_str = NULL;
			/*FIXME*/
		}

		if(events) {
			query = xstrdup_printf(
				"insert into %s_%s (node_name, cpu_count, "
				"state, time_start, time_end, reason, "
				"reason_uid, cluster_nodes) "
				"select node_name, cpu_count, state, "
				"period_start, period_end, reason, "
				"reason_uid, cluster_nodes from %s where "
				"cluster='%s' on duplicate key update "
				"time_start=VALUES(time_start), "
				"time_end=VALUES(time_end);",
				cluster_name, event_table,
				"cluster_event_table", cluster_name);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			rc = mysql_db_query(db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't update event table correctly");
				break;
			}
		}

		if(jobs) {
			query = xstrdup_printf(
				"insert into %s_%s (job_db_inx, "
				"deleted, account, "
				"cpus_req, cpus_alloc, exit_code, job_name, "
				"id_assoc, id_block, id_job, id_resv, "
				"id_wckey, id_user, id_group, kill_requid, "
				"nodelist, nodes_alloc, node_inx, "
				"partition, priority, qos, state, timelimit, "
				"time_submit, time_eligible, time_start, "
				"time_end, time_suspended, track_steps, wckey) "
				"select id, deleted, account, req_cpus, "
				"alloc_cpus, comp_code, name, associd, "
				"blockid, jobid, resvid, wckeyid, uid, gid, "
				"kill_requid, nodelist, alloc_nodes, "
				"node_inx, partition, priority, qos, state, "
				"timelimit, submit, eligible, start, end, "
				"suspended, track_steps, wckey from %s where "
				"cluster='%s' on duplicate key update "
				"deleted=VALUES(deleted), "
				"time_start=VALUES(time_start), "
				"time_end=VALUES(time_end);",
				cluster_name, job_table,
				job_table, cluster_name);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			rc = mysql_db_query(db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't update job table correctly");
				break;
			}
		}

		if(steps || suspends) {
			/* Since there isn't a cluster name in the
			   step table we need to get all the ids from
			   the job_table for this cluster and query
			   against that.
			*/
			query = xstrdup_printf("select job_db_inx from %s_%s",
					       cluster_name, job_table);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
				xfree(query);
				break;
			}
			xfree(query);

			if(mysql_num_rows(result)) {
				while((row = mysql_fetch_row(result))) {
					if(id_str)
						xstrcat(id_str, " || ");
					else
						xstrcat(id_str, "(");
					xstrfmtcat(id_str, "(id=%s)",
						   row[0]);
				}
				xstrcat(id_str, ")");
			}
			mysql_free_result(result);
		}

		if(resvs) {
			query = xstrdup_printf(
				"insert into %s_%s (id_resv, "
				"deleted, assoclist, "
				"cpus, flags, nodelist, node_inx, "
				"resv_name, time_start, time_end) "
				"select id, deleted, assoclist, cpus, "
				"flags, nodelist, node_inx, name, start, end "
				"from %s where cluster='%s' "
				"on duplicate key update "
				"deleted=VALUES(deleted), "
				"time_start=VALUES(time_start), "
				"time_end=VALUES(time_end);",
				cluster_name, resv_table,
				resv_table, cluster_name);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			rc = mysql_db_query(db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't update reserve "
				      "table correctly");
				break;
			}
		}

		if(steps && id_str) {
			query = xstrdup_printf(
				"insert into %s_%s (job_db_inx, "
				"deleted, cpus_alloc, "
				"exit_code, id_step, kill_requid, nodelist, "
				"nodes_alloc, node_inx, state, step_name, "
				"task_cnt, task_dist, time_start, time_end, "
				"time_suspended, user_sec, user_usec, "
				"sys_sec, sys_usec, max_pages, "
				"max_pages_task, max_pages_node, ave_pages, "
				"max_rss, max_rss_task, max_rss_node, "
				"ave_rss, max_vsize, max_vsize_task, "
				"max_vsize_node, ave_vsize, min_cpu, "
				"min_cpu_task, min_cpu_node, ave_cpu) "
				"select id, deleted, cpus, "
				"comp_code, stepid, kill_requid, nodelist, "
				"nodes, node_inx, state, name, tasks, "
				"task_dist, start, end, suspended, user_sec, "
				"user_usec, sys_sec, sys_usec, max_pages, "
				"max_pages_task, max_pages_node, ave_pages, "
				"max_rss, max_rss_task, max_rss_node, "
				"ave_rss, max_vsize, max_vsize_task, "
				"max_vsize_node, ave_vsize, min_cpu, "
				"min_cpu_task, min_cpu_node, ave_cpu "
				"from %s where %s on duplicate key update "
				"deleted=VALUES(deleted), "
				"time_start=VALUES(time_start), "
				"time_end=VALUES(time_end);",
				cluster_name, step_table,
				step_table, id_str);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			rc = mysql_db_query(db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't update step table correctly");
				break;
			}
		}

		if(suspends && id_str) {
			query = xstrdup_printf(
				"insert into %s_%s (job_db_inx, id_assoc, "
				"time_start, time_end) "
				"select id, associd, start, end "
				"from %s where %s on duplicate key update "
				"time_start=VALUES(time_start), "
				"time_end=VALUES(time_end);",
				cluster_name, suspend_table,
				suspend_table, id_str);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			rc = mysql_db_query(db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't update suspend "
				      "table correctly");
				goto end_it;
			}
		}

		if(usage) {
			query = xstrdup_printf(
				"insert into %s_%s (creation_time, mod_time, "
				"deleted, time_start, cpu_count, "
				"alloc_cpu_secs, down_cpu_secs, "
				"pdown_cpu_secs, idle_cpu_secs, "
				"resv_cpu_secs, over_cpu_secs) "
				"select creation_time, mod_time, deleted, "
				"period_start, cpu_count, alloc_cpu_secs, "
				"down_cpu_secs, pdown_cpu_secs, "
				"idle_cpu_secs, resv_cpu_secs, over_cpu_secs "
				"from %s where cluster='%s' "
				"on duplicate key update "
				"deleted=VALUES(deleted), "
				"time_start=VALUES(time_start);",
				cluster_name, cluster_day_table,
				cluster_day_table, cluster_name);
			xstrfmtcat(query,
				   "insert into %s_%s (creation_time, "
				   "mod_time, deleted, time_start, cpu_count, "
				   "alloc_cpu_secs, down_cpu_secs, "
				   "pdown_cpu_secs, idle_cpu_secs, "
				   "resv_cpu_secs, over_cpu_secs) "
				   "select creation_time, mod_time, deleted, "
				   "period_start, cpu_count, alloc_cpu_secs, "
				   "down_cpu_secs, pdown_cpu_secs, "
				   "idle_cpu_secs, resv_cpu_secs, "
				   "over_cpu_secs "
				   "from %s where cluster='%s' "
				   "on duplicate key update "
				   "deleted=VALUES(deleted), "
				   "time_start=VALUES(time_start);",
				   cluster_name, cluster_hour_table,
				   cluster_hour_table, cluster_name);
			xstrfmtcat(query,
				   "insert into %s_%s (creation_time, "
				   "mod_time, deleted, time_start, cpu_count, "
				   "alloc_cpu_secs, down_cpu_secs, "
				   "pdown_cpu_secs, idle_cpu_secs, "
				   "resv_cpu_secs, over_cpu_secs) "
				   "select creation_time, mod_time, deleted, "
				   "period_start, cpu_count, alloc_cpu_secs, "
				   "down_cpu_secs, pdown_cpu_secs, "
				   "idle_cpu_secs, resv_cpu_secs, "
				   "over_cpu_secs "
				   "from %s where cluster='%s' "
				   "on duplicate key update "
				   "deleted=VALUES(deleted), "
				   "time_start=VALUES(time_start);",
				   cluster_name, cluster_month_table,
				   cluster_month_table, cluster_name);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			rc = mysql_db_query(db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't update cluster usage "
				      "tables correctly");
				break;
			}
		}

		if(wckeys) {
			char *wckey_ids = NULL;

			xstrfmtcat(query,
				   "insert into %s_%s (creation_time, "
				   "mod_time, deleted, id_wckey, wckey_name, "
				   "user) "
				   "select creation_time, mod_time, deleted, "
				   "id, name, user "
				   "from %s where cluster='%s' "
				   "on duplicate key update "
				   "deleted=VALUES(deleted);",
				   cluster_name, wckey_table,
				   wckey_table, cluster_name);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			rc = mysql_db_query(db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't update wckey table correctly");
				break;
			}

			/* Since there isn't a cluster name in the
			   wckey usage tables we need to get all the ids from
			   the wckey_table for this cluster and query
			   against that.
			*/
			query = xstrdup_printf("select id_wckey from %s_%s",
					       cluster_name, wckey_table);
			debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
				xfree(query);
				break;
			}
			xfree(query);

			if(mysql_num_rows(result)) {
				while((row = mysql_fetch_row(result))) {
					if(id_str)
						xstrcat(id_str, " || ");
					else
						xstrcat(id_str, "(");
					xstrfmtcat(id_str, "(id_wckey=%s)",
						   row[0]);
				}
				xstrcat(id_str, ")");
			}
			mysql_free_result(result);

			if(wckey_ids) {
				xstrfmtcat(query,
					   "insert into %s_%s (creation_time, "
					   "mod_time, deleted, id_wckey, "
					   "time_start alloc_cpu_secs, "
					   "resv_cpu_secs, over_cpu_secs) "
					   "select creation_time, mod_time, "
					   "deleted, id, period_start, "
					   "alloc_cpu_secs, resv_cpu_secs, "
					   "over_cpu_secs "
					   "from %s where %s "
					   "on duplicate key update "
					   "deleted=VALUES(deleted), "
					   "time_start=VALUES(time_start);",
					   cluster_name, wckey_day_table,
					   wckey_day_table, wckey_ids);
				xstrfmtcat(query,
					   "insert into %s_%s (creation_time, "
					   "mod_time, deleted, id_wckey, "
					   "time_start alloc_cpu_secs, "
					   "resv_cpu_secs, over_cpu_secs) "
					   "select creation_time, mod_time, "
					   "deleted, id, period_start, "
					   "alloc_cpu_secs, resv_cpu_secs, "
					   "over_cpu_secs "
					   "from %s where %s "
					   "on duplicate key update "
					   "deleted=VALUES(deleted), "
					   "time_start=VALUES(time_start);",
					   cluster_name, wckey_hour_table,
					   wckey_hour_table, wckey_ids);
				xstrfmtcat(query,
					   "insert into %s_%s (creation_time, "
					   "mod_time, deleted, id_wckey, "
					   "time_start alloc_cpu_secs, "
					   "resv_cpu_secs, over_cpu_secs) "
					   "select creation_time, mod_time, "
					   "deleted, id, period_start, "
					   "alloc_cpu_secs, resv_cpu_secs, "
					   "over_cpu_secs "
					   "from %s where %s "
					   "on duplicate key update "
					   "deleted=VALUES(deleted), "
					   "time_start=VALUES(time_start);",
					   cluster_name, wckey_month_table,
					   wckey_month_table, wckey_ids);
				xfree(wckey_ids);
				debug3("(%s:%d) query\n%s",
				      __FILE__, __LINE__, query);
				rc = mysql_db_query(db_conn, query);
				xfree(query);
				if(rc != SLURM_SUCCESS) {
					error("Couldn't update wckey usage "
					      "table correctly");
					break;
				}
			}
		}
	}

	xfree(id_str);
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&mysql_cluster_list_lock);

	return SLURM_SUCCESS;

end_it:
	if(result)
		mysql_free_result(result);
	slurm_mutex_unlock(&mysql_cluster_list_lock);

	return SLURM_ERROR;
}

