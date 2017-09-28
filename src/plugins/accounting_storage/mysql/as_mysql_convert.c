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

/* Any time you have to add to an existing convert update this number. */
#define CONVERT_VERSION 3

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
	char *query;

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

	return rc;
}

static int _convert_step_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query;

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
	}

	return rc;
}

static int _set_db_curr_ver(mysql_conn_t *mysql_conn)
{
	char *query;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;

	xassert(as_mysql_total_cluster_list);

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
		if (!list_count(as_mysql_total_cluster_list))
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

extern int as_mysql_convert_tables_pre_create(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster_name;

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
	char *query;
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster_name;

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
	}
	list_iterator_destroy(itr);

	if (rc == SLURM_SUCCESS) {
		info("Conversion done: success!");
		query = xstrdup_printf("update %s set version=%d, "
				       "mod_time=UNIX_TIMESTAMP()",
				       convert_version_table, CONVERT_VERSION);
		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}
