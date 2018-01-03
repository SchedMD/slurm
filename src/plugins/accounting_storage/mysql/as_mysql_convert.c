/*****************************************************************************\
 *  as_mysql_convert.c - functions dealing with converting from tables in
 *                    slurm <= 17.02.
 *****************************************************************************
 *
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#include "as_mysql_convert.h"

List bad_tres_list = NULL;

/* Any time you have to add to an existing convert update this number. */
#define CONVERT_VERSION 5

/*
 * Defined globally because it's used in 2 functions:
 * _convert_resv_table and _update_unused_wall
 */
enum {
	JOIN_REQ_RESV_ID,
	JOIN_REQ_RESV_START,
	JOIN_REQ_RESV_END,
	JOIN_REQ_RESV_TRES,
	JOIN_REQ_JOB_ID,
	JOIN_REQ_JOB_START,
	JOIN_REQ_JOB_END,
	JOIN_REQ_JOB_TRES,
	JOIN_REQ_COUNT
};

/* If this changes, the corresponding enum must change */
const char *join_req_inx[] = {
	"rt.id_resv",
	"rt.time_start",
	"rt.time_end",
	"rt.tres",
	"jt.id_job",
	"jt.time_start",
	"jt.time_end",
	"jt.tres_alloc"
};

typedef struct {
	uint64_t count;
	uint32_t id;
} local_tres_t;

static uint32_t db_curr_ver = NO_VAL;

static int _convert_job_table_pre(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	storage_field_t job_table_fields_17_02[] = {
		{ "job_db_inx", "bigint unsigned not null auto_increment" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "account", "tinytext" },
		{ "admin_comment", "text" },
		{ "array_task_str", "text" },
		{ "array_max_tasks", "int unsigned default 0 not null" },
		{ "array_task_pending", "int unsigned default 0 not null" },
		{ "cpus_req", "int unsigned not null" },
		{ "derived_ec", "int unsigned default 0 not null" },
		{ "derived_es", "text" },
		{ "exit_code", "int unsigned default 0 not null" },
		{ "job_name", "tinytext not null" },
		{ "id_assoc", "int unsigned not null" },
		{ "id_array_job", "int unsigned default 0 not null" },
		{ "id_array_task", "int unsigned default 0xfffffffe not null" },
		{ "id_block", "tinytext" },
		{ "id_job", "int unsigned not null" },
		{ "id_qos", "int unsigned default 0 not null" },
		{ "id_resv", "int unsigned not null" },
		{ "id_wckey", "int unsigned not null" },
		{ "id_user", "int unsigned not null" },
		{ "id_group", "int unsigned not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "mem_req", "bigint unsigned default 0 not null" },
		{ "nodelist", "text" },
		{ "nodes_alloc", "int unsigned not null" },
		{ "node_inx", "text" },
		{ "partition", "tinytext not null" },
		{ "priority", "int unsigned not null" },
		{ "state", "int unsigned not null" },
		{ "timelimit", "int unsigned default 0 not null" },
		{ "time_submit", "bigint unsigned default 0 not null" },
		{ "time_eligible", "bigint unsigned default 0 not null" },
		{ "time_start", "bigint unsigned default 0 not null" },
		{ "time_end", "bigint unsigned default 0 not null" },
		{ "time_suspended", "bigint unsigned default 0 not null" },
		{ "gres_req", "text not null default ''" },
		{ "gres_alloc", "text not null default ''" },
		{ "gres_used", "text not null default ''" },
		{ "wckey", "tinytext not null default ''" },
		{ "track_steps", "tinyint not null" },
		{ "tres_alloc", "text not null default ''" },
		{ "tres_req", "text not null default ''" },
		{ NULL, NULL}
	};
	char *query;
	char table_name[200];

	if (db_curr_ver < 1) {
		snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
			 cluster_name, job_table);
		/* sacct_def is the index for query's with state as time_tart is
		 * used in these queries. sacct_def2 is for plain sacct
		 * queries. */
		if (mysql_db_create_table(mysql_conn, table_name,
					  job_table_fields_17_02,
					  ", primary key (job_db_inx), "
					  "unique index (id_job, "
					  "id_assoc, time_submit), "
					  "key rollup (time_eligible, time_end), "
					  "key rollup2 (time_end, time_eligible), "
					  "key nodes_alloc (nodes_alloc), "
					  "key wckey (id_wckey), "
					  "key qos (id_qos), "
					  "key association (id_assoc), "
					  "key array_job (id_array_job), "
					  "key reserv (id_resv), "
					  "key sacct_def (id_user, time_start, "
					  "time_end), "
					  "key sacct_def2 (id_user, time_end, "
					  "time_eligible))")
		    == SLURM_ERROR)
			return SLURM_ERROR;
	}

	if (db_curr_ver < 2) {
		query = xstrdup_printf(
			"select id_job, time_submit, count(*) as count from "
			"\"%s_%s\" group by id_job, time_submit having "
			"count >= 2",
			cluster_name, job_table);

		debug4("%d(%s:%d) query\n%s", mysql_conn->conn,
		       THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		while ((row = mysql_fetch_row(result))) {
			query = xstrdup_printf(
				"delete from \"%s_%s\" where id_job=%s and "
				"time_submit=%s and tres_alloc='';",
				cluster_name, job_table, row[0], row[1]);
			debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
			if ((rc = mysql_db_query(mysql_conn, query)) !=
			    SLURM_SUCCESS) {
				error("Can't delete duplicates from %s_%s info: %m",
				      cluster_name, job_table);
				xfree(query);
				break;
			}
			xfree(query);
		}
		mysql_free_result(result);
	}

	return rc;
}

static int _convert_job_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if (db_curr_ver < 1) {
		query = xstrdup_printf("update \"%s_%s\" as job "
				       "left outer join ( select job_db_inx, "
				       "SUM(consumed_energy) 'sum_energy' "
				       "from \"%s_%s\" where id_step >= 0 "
				       "and consumed_energy != %"PRIu64
				       " group by job_db_inx ) step on "
				       "job.job_db_inx=step.job_db_inx "
				       "set job.tres_alloc=concat("
				       "job.tres_alloc, concat(',%d=', "
				       "case when step.sum_energy then "
				       "step.sum_energy else %"PRIu64" END)) "
				       "where job.tres_alloc != '' && "
				       "job.tres_alloc not like '%%,%d=%%';",
				       cluster_name, job_table,
				       cluster_name, step_table,
				       NO_VAL64, TRES_ENERGY, NO_VAL64,
				       TRES_ENERGY);

		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
			error("Can't convert %s_%s info: %m",
			      cluster_name, job_table);
		xfree(query);

		if (rc != SLURM_SUCCESS)
			return rc;
	}

	if (db_curr_ver < 3) {
		query = xstrdup_printf("update \"%s_%s\" set mem_req = 0x8000000000000000 | (mem_req ^ 0x80000000) where (mem_req & 0xffffffff80000000) = 0x80000000;",
				       cluster_name, job_table);

		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
			error("Can't convert %s_%s info: %m",
			      cluster_name, job_table);
		xfree(query);

		if (rc != SLURM_SUCCESS)
			return rc;
	}

	if (db_curr_ver < 5) {
		if (bad_tres_list) {
			slurmdb_tres_rec_t *tres_rec;
			ListIterator itr = list_iterator_create(bad_tres_list);
			while ((tres_rec = list_next(itr))) {
				xstrfmtcat(query,
					   "update \"%s_%s\" set tres_alloc=replace(tres_alloc, ',%u=', ',%u='), tres_req=replace(tres_req, ',%u=', ',%u=');",
					   cluster_name, job_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count);
			}
			list_iterator_destroy(itr);
			if (debug_flags & DEBUG_FLAG_DB_QUERY)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			if ((rc = mysql_db_query(mysql_conn, query))
			    != SLURM_SUCCESS)
				error("%d: Can't convert %s_%s info: %m",
				      __LINE__, cluster_name, job_table);
			xfree(query);
		}
	}

	return rc;
}

static int _convert_step_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if (db_curr_ver < 1) {
		query = xstrdup_printf("update \"%s_%s\" set consumed_energy=%"
				       PRIu64" where consumed_energy=%u;",
				       cluster_name, step_table,
				       NO_VAL64, NO_VAL);

		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
			error("Can't convert %s_%s info: %m",
			      cluster_name, step_table);
		xfree(query);

		if (rc != SLURM_SUCCESS)
			return rc;
	}

	if (db_curr_ver < 5) {
		if (bad_tres_list) {
			slurmdb_tres_rec_t *tres_rec;
			ListIterator itr = list_iterator_create(bad_tres_list);
			while ((tres_rec = list_next(itr))) {
				xstrfmtcat(query,
					   "update \"%s_%s\" set tres_alloc=replace(tres_alloc, ',%u=', ',%u=');",
					   cluster_name, step_table,
					   tres_rec->id, tres_rec->rec_count);
			}
			list_iterator_destroy(itr);
			if (debug_flags & DEBUG_FLAG_DB_QUERY)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			if ((rc = mysql_db_query(mysql_conn, query))
			    != SLURM_SUCCESS)
				error("%d: Can't convert %s_%s info: %m",
				      __LINE__, cluster_name, step_table);
			xfree(query);
		}
	}

	return rc;
}

static int _convert_cluster_tables(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if (db_curr_ver < 5) {
		if (bad_tres_list) {
			slurmdb_tres_rec_t *tres_rec;
			ListIterator itr = list_iterator_create(bad_tres_list);
			while ((tres_rec = list_next(itr))) {
				xstrfmtcat(query,
					   "update \"%s_%s\" set tres=replace(tres, ',%u=', ',%u=');"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;"
					   "update \"%s_%s\" set id_tres=%u where id_tres=%u;",
					   cluster_name, event_table,
					   tres_rec->id, tres_rec->rec_count,
					   cluster_name, assoc_day_table,
					   tres_rec->rec_count, tres_rec->id,
					   cluster_name, assoc_hour_table,
					   tres_rec->rec_count, tres_rec->id,
					   cluster_name, assoc_month_table,
					   tres_rec->rec_count, tres_rec->id,
					   cluster_name, cluster_day_table,
					   tres_rec->rec_count, tres_rec->id,
					   cluster_name, cluster_hour_table,
					   tres_rec->rec_count, tres_rec->id,
					   cluster_name, cluster_month_table,
					   tres_rec->rec_count, tres_rec->id,
					   cluster_name, wckey_day_table,
					   tres_rec->rec_count, tres_rec->id,
					   cluster_name, wckey_hour_table,
					   tres_rec->rec_count, tres_rec->id,
					   cluster_name, wckey_month_table,
					   tres_rec->rec_count, tres_rec->id);
			}
			list_iterator_destroy(itr);
			if (debug_flags & DEBUG_FLAG_DB_QUERY)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			if ((rc = mysql_db_query(mysql_conn, query))
			    != SLURM_SUCCESS)
				error("%d: Can't convert %s's cluster tables: %m",
				      __LINE__, cluster_name);
			xfree(query);
		}
	}

	return rc;
}

static int _convert_assoc_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if (db_curr_ver < 5) {
		if (bad_tres_list) {
			slurmdb_tres_rec_t *tres_rec;
			ListIterator itr = list_iterator_create(bad_tres_list);
			while ((tres_rec = list_next(itr))) {
				xstrfmtcat(query,
					   "update \"%s_%s\" set "
					   "max_tres_pj=replace(max_tres_pj, ',%u=', ',%u='), "
					   "max_tres_pn=replace(max_tres_pn, ',%u=', ',%u='), "
					   "max_tres_mins_pj=replace(max_tres_mins_pj, ',%u=', ',%u='), "
					   "max_tres_run_mins=replace(max_tres_run_mins, ',%u=', ',%u='), "
					   "grp_tres=replace(grp_tres, ',%u=', ',%u='), "
					   "grp_tres_mins=replace(grp_tres_mins, ',%u=', ',%u='), "
					   "grp_tres_run_mins=replace(grp_tres_run_mins, ',%u=', ',%u=');"
					   "update \"%s_%s\" set max_tres_pj=replace(max_tres_pj, '%u=', '%u=') where max_tres_pj like '%u=%%';"
					   "update \"%s_%s\" set max_tres_pn=replace(max_tres_pn, '%u=', '%u=') where max_tres_pn like '%u=%%';"
					   "update \"%s_%s\" set max_tres_mins_pj=replace(max_tres_mins_pj, '%u=', '%u=') where max_tres_mins_pj like '%u=%%';"
					   "update \"%s_%s\" set max_tres_run_mins=replace(max_tres_run_mins, '%u=', '%u=') where max_tres_run_mins like '%u=%%';"
					   "update \"%s_%s\" set grp_tres=replace(grp_tres, '%u=', '%u=') where grp_tres like '%u=%%';"
					   "update \"%s_%s\" set grp_tres_mins=replace(grp_tres_mins, '%u=', '%u=') where grp_tres_mins like '%u=%%';"
					   "update \"%s_%s\" set grp_tres_run_mins=replace(grp_tres_run_mins, '%u=', '%u=') where grp_tres_run_mins like '%u=%%';",
					   cluster_name, assoc_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   cluster_name, assoc_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   cluster_name, assoc_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   cluster_name, assoc_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   cluster_name, assoc_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   cluster_name, assoc_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   cluster_name, assoc_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   cluster_name, assoc_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id);
			}
			list_iterator_destroy(itr);
			if (debug_flags & DEBUG_FLAG_DB_QUERY)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			if ((rc = mysql_db_query(mysql_conn, query))
			    != SLURM_SUCCESS)
				error("%d: Can't convert %s_%s info: %m",
				      __LINE__, cluster_name, assoc_table);
			xfree(query);
		}
	}

	return rc;
}

static int _convert_qos_table(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if (db_curr_ver < 5) {
		if (bad_tres_list) {
			slurmdb_tres_rec_t *tres_rec;
			ListIterator itr = list_iterator_create(bad_tres_list);
			while ((tres_rec = list_next(itr))) {
				xstrfmtcat(query,
					   "update %s set "
					   "max_tres_pa=replace(max_tres_pa, ',%u=', ',%u='), "
					   "max_tres_pj=replace(max_tres_pj, ',%u=', ',%u='), "
					   "max_tres_pn=replace(max_tres_pn, ',%u=', ',%u='), "
					   "max_tres_pu=replace(max_tres_pu, ',%u=', ',%u='), "
					   "max_tres_mins_pj=replace(max_tres_mins_pj, ',%u=', ',%u='), "
					   "max_tres_run_mins_pa=replace(max_tres_run_mins_pa, ',%u=', ',%u='), "
					   "max_tres_run_mins_pu=replace(max_tres_run_mins_pu, ',%u=', ',%u='), "
					   "min_tres_pj=replace(min_tres_pj, ',%u=', ',%u='), "
					   "grp_tres=replace(grp_tres, ',%u=', ',%u='), "
					   "grp_tres_mins=replace(grp_tres_mins, ',%u=', ',%u='), "
					   "grp_tres_run_mins=replace(grp_tres_run_mins, ',%u=', ',%u=');"
					   "update %s set max_tres_pa=replace(max_tres_pa, '%u=', '%u=') where max_tres_pa like '%u=%%';"
					   "update %s set max_tres_pj=replace(max_tres_pj, '%u=', '%u=') where max_tres_pj like '%u=%%';"
					   "update %s set max_tres_pn=replace(max_tres_pn, '%u=', '%u=') where max_tres_pn like '%u=%%';"
					   "update %s set max_tres_pu=replace(max_tres_pu, '%u=', '%u=') where max_tres_pu like '%u=%%';"
					   "update %s set max_tres_mins_pj=replace(max_tres_mins_pj, '%u=', '%u=') where max_tres_mins_pj like '%u=%%';"
					   "update %s set max_tres_run_mins_pa=replace(max_tres_run_mins_pa, '%u=', '%u=') where max_tres_run_mins_pa like '%u=%%';"
					   "update %s set max_tres_run_mins_pu=replace(max_tres_run_mins_pu, '%u=', '%u=') where max_tres_run_mins_pu like '%u=%%';"
					   "update %s set min_tres_pj=replace(min_tres_pj, '%u=', '%u=') where min_tres_pj like '%u=%%';"
					   "update %s set grp_tres=replace(grp_tres, '%u=', '%u=') where grp_tres like '%u=%%';"
					   "update %s set grp_tres_mins=replace(grp_tres_mins, '%u=', '%u=') where grp_tres_mins like '%u=%%';"
					   "update %s set grp_tres_run_mins=replace(grp_tres_run_mins, '%u=', '%u=') where grp_tres_run_mins like '%u=%%';",
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id, tres_rec->rec_count,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id,
					   qos_table,
					   tres_rec->id, tres_rec->rec_count,
					   tres_rec->id);
			}
			list_iterator_destroy(itr);
			if (debug_flags & DEBUG_FLAG_DB_QUERY)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			if ((rc = mysql_db_query(mysql_conn, query))
			    != SLURM_SUCCESS)
				error("%d: Can't convert %s info: %m",
				      __LINE__, qos_table);
			xfree(query);
		}
	}

	return rc;
}

static int _find_loc_tres(void *x, void *key)
{
	local_tres_t *loc_tres = (local_tres_t *)x;
	uint32_t tres_id = *(uint32_t *)key;

	if (loc_tres->id == tres_id)
		return 1;
	return 0;
}

/*
 * Job time is a ratio of job tres and resv tres:
 * job time = (job_end - job_start) * (job_tres / resv_tres)
 */
static void _accumulate_job_time(time_t job_start, time_t job_end,
				 List job_tres, List resv_tres,
				 double *total_job_time)
{
	ListIterator resv_itr;
	local_tres_t *loc_tres;
	uint32_t resv_tres_id;
	uint64_t resv_tres_count;
	double tres_ratio = 0.0;

	/* Get TRES counts. Make sure the TRES types match. */
	resv_itr = list_iterator_create(resv_tres);
	while ((loc_tres = list_next(resv_itr))) {
		/* Avoid dividing by zero. */
		if (!loc_tres->count)
			continue;
		resv_tres_id = loc_tres->id;
		resv_tres_count = loc_tres->count;
		if ((loc_tres = list_find_first(job_tres,
						_find_loc_tres,
						&resv_tres_id))) {
			tres_ratio = (double)loc_tres->count /
				(double)resv_tres_count;
			break;
		}
	}
	list_iterator_destroy(resv_itr);

	*total_job_time += (double)(job_end - job_start) * tres_ratio;
}

/*
 * This is almost identical to the function with the same name in
 * as_mysql_rollup.c, but it was much simpler to not try to make that a common
 * function. Maybe it should become a common function in the future?
 */
static void _add_tres_2_list(List tres_list, char *tres_str)
{
	char *tmp_str = tres_str;
	int id;
	uint64_t count;
	local_tres_t *loc_tres;

	xassert(tres_list);

	if (!tres_str || !tres_str[0])
		return;

	while (tmp_str) {
		id = atoi(tmp_str);
		if (id < 1) {
			error("_add_tres_2_list: no id "
			      "found at %s instead", tmp_str);
			break;
		}

		/*
		 * We don't run rollup on a node basis because they are shared
		 * resources on many systems so it will almost always have over
		 * committed resources.
		 */
		if (id != TRES_NODE) {
			if (!(tmp_str = strchr(tmp_str, '='))) {
				error("_add_tres_2_list: no value found");
				xassert(0);
				break;
			}
			count = slurm_atoull(++tmp_str);
			loc_tres = xmalloc(sizeof(local_tres_t));
			loc_tres->id = id;
			loc_tres->count = count;
			list_append(tres_list, loc_tres);
		}

		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}

	return;
}

static int _update_unused_wall(mysql_conn_t *mysql_conn,
			       char *cluster_name,
			       MYSQL_RES *result)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	MYSQL_ROW row = mysql_fetch_row(result);
	while (row) {
		int next_resv_id, curr_resv_id;
		time_t curr_resv_start, next_resv_start, resv_end;
		time_t total_resv_time = 0;
		double unused_wall = 0;
		double total_job_time = 0;
		List resv_tres_list = list_create(NULL);

		/*
		 * Reservations are uniquely identified by both id and start
		 * time. Accumulate usage for each unique reservation.
		 */
		curr_resv_id = slurm_atoul(row[JOIN_REQ_RESV_ID]);
		curr_resv_start = slurm_atoul(row[JOIN_REQ_RESV_START]);
		resv_end = slurm_atoul(row[JOIN_REQ_RESV_END]);
		_add_tres_2_list(resv_tres_list, row[JOIN_REQ_RESV_TRES]);
		do {

			if (row[JOIN_REQ_JOB_ID]) {
				time_t job_start =
					slurm_atoul(row[JOIN_REQ_JOB_START]);
				time_t job_end =
					slurm_atoul(row[JOIN_REQ_JOB_END]);
				/*
				 * Don't count the job if it started after the
				 * reservation finished (this can happen if a
				 * reservation was updated).
				 */
				if (job_start < resv_end) {
					List job_tres_list = list_create(NULL);
					_add_tres_2_list(
						job_tres_list,
						row[JOIN_REQ_JOB_TRES]);

					/*
					 * Don't count job time outside of the
					 * reservation.
					 */
					if (job_end > resv_end)
						job_end = resv_end;
					if (job_start < curr_resv_start)
						job_start = curr_resv_start;

					_accumulate_job_time(job_start, job_end,
							     job_tres_list,
							     resv_tres_list,
							     &total_job_time);
					list_destroy(job_tres_list);
				}
			}

			if (!(row = mysql_fetch_row(result)))
				break;

			next_resv_id = slurm_atoul(row[JOIN_REQ_RESV_ID]);
			next_resv_start = slurm_atoul(row[JOIN_REQ_RESV_START]);

		} while (next_resv_id == curr_resv_id &&
			 next_resv_start == curr_resv_start);
		list_destroy(resv_tres_list);

		/* Check for potential underflow. */
		total_resv_time = resv_end - curr_resv_start;
		if (total_job_time > total_resv_time) {
			error("%s, total job time %f is greater than total resv time %ld.",
			      __func__, total_job_time, total_resv_time);
			continue;
		}
		unused_wall = total_resv_time - total_job_time;

		/* Update reservation. */
		xstrfmtcat(query, "update \"%s_%s\" set unused_wall=%f where id_resv = %d && time_start = %ld;",
			   cluster_name, resv_table, unused_wall,
			   curr_resv_id, curr_resv_start);
	}

	if (!query)
		return rc;

	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);

	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	return rc;
}

static int _convert_resv_table(mysql_conn_t *mysql_conn, char *cluster_name)
{

	char *query = NULL;
	MYSQL_RES *result = NULL;
	int rc = SLURM_SUCCESS;

	/* 5 needs to happen before 4 since we are moving tres around */
	if (db_curr_ver < 5) {
		if (bad_tres_list) {
			slurmdb_tres_rec_t *tres_rec;
			ListIterator itr = list_iterator_create(bad_tres_list);
			while ((tres_rec = list_next(itr))) {
				xstrfmtcat(query,
					   "update \"%s_%s\" set tres=replace(tres, '%u=', '%u=');",
					   cluster_name, resv_table,
					   tres_rec->id, tres_rec->rec_count);
			}
			list_iterator_destroy(itr);
			if (debug_flags & DEBUG_FLAG_DB_QUERY)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			if ((rc = mysql_db_query(mysql_conn, query))
			    != SLURM_SUCCESS)
				error("%d: Can't convert %s_%s info: %m",
				      __LINE__, cluster_name, resv_table);
			xfree(query);
		}
	}

	/*
	 * Previous to 17.11, the reservation table did not have unused_wall.
	 * Populate the unused_wall field of the reservation records:
	 * unused_wall = (reservation wall time) - (time used by jobs)
	 */
	if (db_curr_ver < 4) {

		char *join_str = NULL;
		int i = 0;
		xstrfmtcat(join_str, "%s", join_req_inx[i]);
		for (i = 1; i < JOIN_REQ_COUNT; i++) {
			xstrfmtcat(join_str, ", %s", join_req_inx[i]);
		}

		/*
		 * Order by the resv id and resv time start so rows with the
		 * same resv are consecutive.
		 */
		query = xstrdup_printf("select %s from \"%s_%s\" as rt left join \"%s_%s\" as jt on (rt.id_resv = jt.id_resv) order by rt.id_resv ASC, rt.time_start ASC;",
				       join_str, cluster_name, resv_table,
				       cluster_name, job_table);
		xfree(join_str);

		if (!query) {
			error("%s, select query is NULL.", __func__);
			return SLURM_ERROR;
		}

		if (debug_flags & DEBUG_FLAG_DB_QUERY)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		result = mysql_db_query_ret(mysql_conn, query, 0);
		xfree(query);
		if (!result) {
			error("%s, Problem getting result of select query.",
			      __func__);
			return SLURM_ERROR;
		}
		xfree(query);

		rc = _update_unused_wall(mysql_conn, cluster_name, result);
		mysql_free_result(result);
	}
	return rc;
}

static int _set_db_curr_ver(mysql_conn_t *mysql_conn)
{
	char *query;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;

	if (db_curr_ver != NO_VAL)
		return SLURM_SUCCESS;

	query = xstrdup_printf("select version from %s", convert_version_table);
	debug4("%d(%s:%d) query\n%s", mysql_conn->conn,
	       THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	row = mysql_fetch_row(result);

	if (row) {
		db_curr_ver = slurm_atoul(row[0]);
		mysql_free_result(result);
	} else {
		int tmp_ver = 0;
		mysql_free_result(result);

		/* no valid clusters, just return */
		if (as_mysql_total_cluster_list &&
		    !list_count(as_mysql_total_cluster_list))
			tmp_ver = CONVERT_VERSION;

		query = xstrdup_printf("insert into %s (version) values (%d);",
				       convert_version_table, tmp_ver);
		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			return SLURM_ERROR;
		db_curr_ver = tmp_ver;
	}

	return rc;
}

extern int as_mysql_convert_get_bad_tres(mysql_conn_t *mysql_conn)
{
	char *query = NULL;
	char *tmp = NULL;
	int rc = SLURM_SUCCESS;
	int i=0, auto_inc = TRES_OFFSET;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *tres_req_inx[] = {
		"id",
		"type",
		"name"
	};
	enum {
		SLURMDB_REQ_ID,
		SLURMDB_REQ_TYPE,
		SLURMDB_REQ_NAME,
		SLURMDB_REQ_COUNT
	};

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if (db_curr_ver == CONVERT_VERSION) {
		debug4("%s: No conversion needed, Horray!", __func__);
		return SLURM_SUCCESS;
	} else if (backup_dbd) {
		/*
		 * We do not want to create/check the database if we are the
		 * backup (see Bug 3827). This is only handled on the primary.
		 *
		 * To avoid situations where someone might upgrade the database
		 * through the backup we want to fatal so they know what
		 * happened instead of potentially starting with the older
		 * database.
		 */
		fatal("Backup DBD can not convert database, please start the primary DBD before starting the backup.");
		return SLURM_ERROR;
	}

	/*
	 * Check to see if we have a bad one to start with.
	 * Any bad one will be in id=5 and will also have a name.  If we don't
	 * have this then we are ok.  Otherwise fatal since it may involve
	 * manually altering the database.
	 */
	query = xstrdup_printf(
		"select id from %s where id=%d && type='billing' && name!=''",
		tres_table, TRES_BILLING);

	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if ((row = mysql_fetch_row(result))) {
		fatal("%s: There is a known bug dealing with MySQL and auto_increment numbers, unfortunately your system has hit this bug.  To temporarily resolve the issue please revert back to your last version of SlurmDBD.  Fixing this issue correctly will require manual intervention with the database.  SchedMD can assist with this.  Supported sites please open a ticket at https://bugs.schedmd.com/.  Non-supported sites please contact SchedMD at sales@schedmd.com if you would like to discuss commercial support options.",
		      __func__);
		return SLURM_ERROR;
	}
	mysql_free_result(result);

	/*
	 * Get the largest id in the tres table.
	 */
	query = xstrdup_printf("select MAX(id) from %s;", tres_table);
	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(row = mysql_fetch_row(result))) {
		fatal("%s: Couldn't get auto_increment for some reason",
		      __func__);
		return SLURM_ERROR;
	}

	/*
	 * Make sure it is at least TRES_OFFSET (blank/new databases will return
	 * NULL.
	 */
	if (row[0] && row[0][0]) {
		uint32_t max_id = slurm_atoul(row[0]);
		auto_inc = MAX(auto_inc, max_id);
	}

	/*
	 * Now get all the ones that need to me moved.
	 */
	xfree(tmp);
	xstrfmtcat(tmp, "%s", tres_req_inx[i]);
	for (i = 1; i < SLURMDB_REQ_COUNT; i++)
		xstrfmtcat(tmp, ", %s", tres_req_inx[i]);

	query = xstrdup_printf("select %s from %s where (id between 5 and 999) && type!='billing'",
			       tmp, tres_table);
	xfree(tmp);

	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_tres_rec_t *tres;

		if (!bad_tres_list)
			bad_tres_list = list_create(slurmdb_destroy_tres_rec);

		tres = xmalloc(sizeof(slurmdb_tres_rec_t));
		list_append(bad_tres_list, tres);

		tres->id = slurm_atoul(row[SLURMDB_REQ_ID]);
		/* use this to say where we are moving it to */
		tres->rec_count = ++auto_inc;
		if (row[SLURMDB_REQ_TYPE] && row[SLURMDB_REQ_TYPE][0])
			tres->type = xstrdup(row[SLURMDB_REQ_TYPE]);
		if (row[SLURMDB_REQ_NAME] && row[SLURMDB_REQ_NAME][0])
			tres->name = xstrdup(row[SLURMDB_REQ_NAME]);
		xstrfmtcat(query,
			   "update %s set id=%u where id=%u;",
			   tres_table, tres->rec_count, tres->id);
	}
	mysql_free_result(result);

	if (query) {
		if (debug_flags & DEBUG_FLAG_DB_QUERY)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}

extern int as_mysql_convert_tables_pre_create(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster_name;

	xassert(as_mysql_total_cluster_list);

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if (db_curr_ver == CONVERT_VERSION) {
		debug4("%s: No conversion needed, Horray!", __func__);
		return SLURM_SUCCESS;
	} else if (backup_dbd) {
		/*
		 * We do not want to create/check the database if we are the
		 * backup (see Bug 3827). This is only handled on the primary.
		 *
		 * To avoid situations where someone might upgrade the database
		 * through the backup we want to fatal so they know what
		 * happened instead of potentially starting with the older
		 * database.
		 */
		fatal("Backup DBD can not convert database, please start the primary DBD before starting the backup.");
		return SLURM_ERROR;
	}


	/* make it up to date */
	itr = list_iterator_create(as_mysql_total_cluster_list);
	while ((cluster_name = list_next(itr))) {
		info("pre-converting job table for %s", cluster_name);
		if ((rc = _convert_job_table_pre(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;
	}
	list_iterator_destroy(itr);

	return rc;
}

extern int as_mysql_convert_tables_post_create(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster_name;

	xassert(as_mysql_total_cluster_list);

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if (db_curr_ver == CONVERT_VERSION) {
		debug4("%s: No conversion needed, Horray!", __func__);
		return SLURM_SUCCESS;
	}

	/* make it up to date */
	itr = list_iterator_create(as_mysql_total_cluster_list);
	while ((cluster_name = list_next(itr))) {
		/* Convert the step tables */
		info("converting step table for %s", cluster_name);
		if ((rc = _convert_step_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;

		/* Now convert the job tables */
		info("converting job table for %s", cluster_name);
		if ((rc = _convert_job_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;

		/* Convert the reservation table */
		info("converting resv table for %s", cluster_name);
		if ((rc = _convert_resv_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;

		/* Convert the cluster tables */
		info("converting cluster tables for %s", cluster_name);
		if ((rc = _convert_cluster_tables(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			return rc;

		/* Convert the assoc table */
		info("converting assoc table for %s", cluster_name);
		if ((rc = _convert_assoc_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			return rc;
	}
	list_iterator_destroy(itr);

	return rc;
}

extern int as_mysql_convert_non_cluster_tables_post_create(
	mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if (db_curr_ver == CONVERT_VERSION) {
		debug4("%s: No conversion needed, Horray!", __func__);
		return SLURM_SUCCESS;
	}

	/* make it up to date */
	/* Convert the QOS table */
	info("converting QOS table");
	if ((rc = _convert_qos_table(mysql_conn)
	     != SLURM_SUCCESS))
		return rc;

	if (rc == SLURM_SUCCESS) {
		char *query = xstrdup_printf(
			"update %s set version=%d, mod_time=UNIX_TIMESTAMP()",
			convert_version_table, CONVERT_VERSION);

		info("Conversion done: success!");

		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}
