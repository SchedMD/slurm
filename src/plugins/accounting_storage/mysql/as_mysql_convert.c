/*****************************************************************************\
 *  as_mysql_convert.c - functions dealing with converting from tables in
 *                    slurm <= 14.11.
 *****************************************************************************
 *
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#include "as_mysql_convert.h"

static int _rename_usage_columns(mysql_conn_t *mysql_conn, char *table)
{
	MYSQL_ROW row;
	MYSQL_RES *result = NULL;
	char *query = NULL;
	int rc = SLURM_SUCCESS;

	query = xstrdup_printf(
		"show columns from %s where field like '%%cpu_%%' "
		"|| field like 'id_assoc' || field like 'id_wckey';",
		table);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	result = mysql_db_query_ret(mysql_conn, query, 0);
	xfree(query);

	if (!result)
		return SLURM_ERROR;

	while ((row = mysql_fetch_row(result))) {
		char *new_char = xstrdup(row[0]);
		xstrsubstitute(new_char, "cpu_", "");
		xstrsubstitute(new_char, "_assoc", "");
		xstrsubstitute(new_char, "_wckey", "");

		if (!query)
			query = xstrdup_printf("alter table %s ", table);
		else
			xstrcat(query, ", ");

		if (!strcmp("id", new_char))
			xstrfmtcat(query, "change %s %s int unsigned not null",
				   row[0], new_char);
		else
			xstrfmtcat(query,
				   "change %s %s bigint unsigned default "
				   "0 not null",
				   row[0], new_char);
		xfree(new_char);
	}
	mysql_free_result(result);

	if (query) {
		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
			error("Can't update %s %m", table);
		xfree(query);
	}

	return rc;
}

static int _update_old_cluster_tables(mysql_conn_t *mysql_conn,
				      char *cluster_name,
				      char *count_col_name)
{
	/* These tables are the 14_11 defs plus things we added in 15.08 */
	storage_field_t assoc_usage_table_fields_14_11[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "id_assoc", "int not null" },
		{ "id_tres", "int default 1 not null" },
		{ "time_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0 not null" },
		{ "consumed_energy", "bigint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t cluster_usage_table_fields_14_11[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "id_tres", "int default 1 not null" },
		{ "time_start", "int unsigned not null" },
		{ count_col_name, "int default 0 not null" },
		{ "alloc_cpu_secs", "bigint default 0 not null" },
		{ "down_cpu_secs", "bigint default 0 not null" },
		{ "pdown_cpu_secs", "bigint default 0 not null" },
		{ "idle_cpu_secs", "bigint default 0 not null" },
		{ "resv_cpu_secs", "bigint default 0 not null" },
		{ "over_cpu_secs", "bigint default 0 not null" },
		{ "consumed_energy", "bigint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t event_table_fields_14_11[] = {
		{ "time_start", "int unsigned not null" },
		{ "time_end", "int unsigned default 0 not null" },
		{ "node_name", "tinytext default '' not null" },
		{ "cluster_nodes", "text not null default ''" },
		{ count_col_name, "int not null" },
		{ "reason", "tinytext not null" },
		{ "reason_uid", "int unsigned default 0xfffffffe not null" },
		{ "state", "smallint unsigned default 0 not null" },
		{ "tres", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t job_table_fields_14_11[] = {
		{ "job_db_inx", "int not null auto_increment" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "account", "tinytext" },
		{ "array_task_str", "text" },
		{ "array_max_tasks", "int unsigned default 0 not null" },
		{ "array_task_pending", "int unsigned default 0 not null" },
		{ "cpus_req", "int unsigned not null" },
		{ "cpus_alloc", "int unsigned not null" },
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
		{ "mem_req", "int unsigned default 0 not null" },
		{ "nodelist", "text" },
		{ "nodes_alloc", "int unsigned not null" },
		{ "node_inx", "text" },
		{ "partition", "tinytext not null" },
		{ "priority", "int unsigned not null" },
		{ "state", "smallint unsigned not null" },
		{ "timelimit", "int unsigned default 0 not null" },
		{ "time_submit", "int unsigned default 0 not null" },
		{ "time_eligible", "int unsigned default 0 not null" },
		{ "time_start", "int unsigned default 0 not null" },
		{ "time_end", "int unsigned default 0 not null" },
		{ "time_suspended", "int unsigned default 0 not null" },
		{ "gres_req", "text not null default ''" },
		{ "gres_alloc", "text not null default ''" },
		{ "gres_used", "text not null default ''" },
		{ "wckey", "tinytext not null default ''" },
		{ "track_steps", "tinyint not null" },
		{ "tres_alloc", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t resv_table_fields_14_11[] = {
		{ "id_resv", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "assoclist", "text not null default ''" },
		{ "cpus", "int unsigned not null" },
		{ "flags", "smallint unsigned default 0 not null" },
		{ "nodelist", "text not null default ''" },
		{ "node_inx", "text not null default ''" },
		{ "resv_name", "text not null" },
		{ "time_start", "int unsigned default 0 not null"},
		{ "time_end", "int unsigned default 0 not null" },
		{ "tres", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields_14_11[] = {
		{ "job_db_inx", "int not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "cpus_alloc", "int unsigned not null" },
		{ "exit_code", "int default 0 not null" },
		{ "id_step", "int not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "nodelist", "text not null" },
		{ "nodes_alloc", "int unsigned not null" },
		{ "node_inx", "text" },
		{ "state", "smallint unsigned not null" },
		{ "step_name", "text not null" },
		{ "task_cnt", "int unsigned not null" },
		{ "task_dist", "smallint default 0 not null" },
		{ "time_start", "int unsigned default 0 not null" },
		{ "time_end", "int unsigned default 0 not null" },
		{ "time_suspended", "int unsigned default 0 not null" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "max_pages", "int unsigned default 0 not null" },
		{ "max_pages_task", "int unsigned default 0 not null" },
		{ "max_pages_node", "int unsigned default 0 not null" },
		{ "ave_pages", "double unsigned default 0.0 not null" },
		{ "max_rss", "bigint unsigned default 0 not null" },
		{ "max_rss_task", "int unsigned default 0 not null" },
		{ "max_rss_node", "int unsigned default 0 not null" },
		{ "ave_rss", "double unsigned default 0.0 not null" },
		{ "max_vsize", "bigint unsigned default 0 not null" },
		{ "max_vsize_task", "int unsigned default 0 not null" },
		{ "max_vsize_node", "int unsigned default 0 not null" },
		{ "ave_vsize", "double unsigned default 0.0 not null" },
		{ "min_cpu", "int unsigned default 0xfffffffe not null" },
		{ "min_cpu_task", "int unsigned default 0 not null" },
		{ "min_cpu_node", "int unsigned default 0 not null" },
		{ "ave_cpu", "double unsigned default 0.0 not null" },
		{ "act_cpufreq", "double unsigned default 0.0 not null" },
		{ "consumed_energy", "double unsigned default 0.0 not null" },
		{ "req_cpufreq", "int unsigned default 0 not null" },
		{ "max_disk_read", "double unsigned default 0.0 not null" },
		{ "max_disk_read_task", "int unsigned default 0 not null" },
		{ "max_disk_read_node", "int unsigned default 0 not null" },
		{ "ave_disk_read", "double unsigned default 0.0 not null" },
		{ "max_disk_write", "double unsigned default 0.0 not null" },
		{ "max_disk_write_task", "int unsigned default 0 not null" },
		{ "max_disk_write_node", "int unsigned default 0 not null" },
		{ "ave_disk_write", "double unsigned default 0.0 not null" },
		{ "tres_alloc", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t wckey_usage_table_fields_14_11[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "id_wckey", "int not null" },
		{ "id_tres", "int default 1 not null" },
		{ "time_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ "consumed_energy", "bigint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	char table_name[200];

	xassert(cluster_name);
	xassert(count_col_name);

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_day_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_usage_table_fields_14_11,
				  ", primary key (id_assoc, "
				  "id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_hour_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_usage_table_fields_14_11,
				  ", primary key (id_assoc, "
				  "id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_month_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_usage_table_fields_14_11,
				  ", primary key (id_assoc, "
				  "id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_day_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields_14_11,
				  ", primary key (id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_hour_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields_14_11,
				  ", primary key (id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_month_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields_14_11,
				  ", primary key (id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, event_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  event_table_fields_14_11,
				  ", primary key (node_name(20), time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, job_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  job_table_fields_14_11,
				  ", primary key (job_db_inx), "
				  "unique index (id_job, "
				  "id_assoc, time_submit), "
				  "key rollup (time_eligible, time_end), "
				  "key wckey (id_wckey), "
				  "key qos (id_qos), "
				  "key association (id_assoc), "
				  "key array_job (id_array_job), "
				  "key reserv (id_resv), "
				  "key sacct_def (id_user, time_start, "
				  "time_end))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, resv_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  resv_table_fields_14_11,
				  ", primary key (id_resv, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, step_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  step_table_fields_14_11,
				  ", primary key (job_db_inx, id_step))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_day_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  wckey_usage_table_fields_14_11,
				  ", primary key (id_wckey, "
				  "id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_hour_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  wckey_usage_table_fields_14_11,
				  ", primary key (id_wckey, "
				  "id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_month_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  wckey_usage_table_fields_14_11,
				  ", primary key (id_wckey, "
				  "id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static int _update2_old_cluster_tables(mysql_conn_t *mysql_conn,
				       char *cluster_name)
{
	/* These tables are the 14_11 defs plus things we added in 15.08 */

	storage_field_t assoc_table_fields_14_11[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "is_def", "tinyint default 0 not null" },
		{ "id_assoc", "int not null auto_increment" },
		{ "user", "tinytext not null default ''" },
		{ "acct", "tinytext not null" },
		{ "partition", "tinytext not null default ''" },
		{ "parent_acct", "tinytext not null default ''" },
		{ "lft", "int not null" },
		{ "rgt", "int not null" },
		{ "shares", "int default 1 not null" },
		{ "max_jobs", "int default NULL" },
		{ "max_submit_jobs", "int default NULL" },
		{ "max_cpus_pj", "int default NULL" },
		{ "max_nodes_pj", "int default NULL" },
		{ "max_tres_pj", "text not null default ''" },
		{ "max_tres_mins_pj", "text not null default ''" },
		{ "max_tres_run_mins", "text not null default ''" },
		{ "max_wall_pj", "int default NULL" },
		{ "max_cpu_mins_pj", "bigint default NULL" },
		{ "max_cpu_run_mins", "bigint default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_cpus", "int default NULL" },
		{ "grp_mem", "int default NULL" },
		{ "grp_nodes", "int default NULL" },
		{ "grp_tres", "text not null default ''" },
		{ "grp_tres_mins", "text not null default ''" },
		{ "grp_tres_run_mins", "text not null default ''" },
		{ "grp_wall", "int default NULL" },
		{ "grp_cpu_mins", "bigint default NULL" },
		{ "grp_cpu_run_mins", "bigint default NULL" },
		{ "def_qos_id", "int default NULL" },
		{ "qos", "blob not null default ''" },
		{ "delta_qos", "blob not null default ''" },
		{ NULL, NULL}
	};

	char table_name[200];

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_table_fields_14_11,
				  ", primary key (id_assoc), "
				  "unique index (user(20), acct(20), "
				  "`partition`(20)), "
				  "key lft (lft), key account (acct(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static int _update2_old_tables(mysql_conn_t *mysql_conn)
{
	/* These tables are the 14_11 defs plus things we added in 15.08 */
	storage_field_t qos_table_fields_14_11[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "name", "tinytext not null" },
		{ "description", "text" },
		{ "flags", "int unsigned default 0" },
		{ "grace_time", "int unsigned default NULL" },
		{ "max_jobs_per_user", "int default NULL" },
		{ "max_submit_jobs_per_user", "int default NULL" },
		{ "max_tres_pj", "text not null default ''" },
		{ "max_tres_pu", "text not null default ''" },
		{ "max_tres_mins_pj", "text not null default ''" },
		{ "max_tres_run_mins_pu", "text not null default ''" },
		{ "min_tres_pj", "text not null default ''" },
		{ "max_cpus_per_job", "int default NULL" },
		{ "max_cpus_per_user", "int default NULL" },
		{ "max_nodes_per_job", "int default NULL" },
		{ "max_nodes_per_user", "int default NULL" },
		{ "max_wall_duration_per_job", "int default NULL" },
		{ "max_cpu_mins_per_job", "bigint default NULL" },
		{ "max_cpu_run_mins_per_user", "bigint default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_tres", "text not null default ''" },
		{ "grp_tres_mins", "text not null default ''" },
		{ "grp_tres_run_mins", "text not null default ''" },
		{ "grp_cpus", "int default NULL" },
		{ "grp_mem", "int default NULL" },
		{ "grp_nodes", "int default NULL" },
		{ "grp_wall", "int default NULL" },
		{ "grp_cpu_mins", "bigint default NULL" },
		{ "grp_cpu_run_mins", "bigint default NULL" },
		{ "preempt", "text not null default ''" },
		{ "preempt_mode", "int default 0" },
		{ "priority", "int unsigned default 0" },
		{ "usage_factor", "double default 1.0 not null" },
		{ "usage_thres", "double default NULL" },
		{ "min_cpus_per_job", "int unsigned default 1 not null" },
		{ NULL, NULL}
	};

	if (mysql_db_create_table(mysql_conn, qos_table,
				  qos_table_fields_14_11,
				  ", primary key (id), "
				  "unique index (name(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static int _convert_assoc_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	char *query = NULL;
	int rc;

	query = xstrdup_printf(
		"update \"%s_%s\" set grp_tres=concat_ws(',', "
		"concat('%d=', grp_cpus), concat('%d=', grp_mem), "
		"concat('%d=', grp_nodes)), "
		"grp_tres_mins=concat_ws(',', concat('%d=', grp_cpu_mins)), "
		"grp_tres_run_mins=concat_ws(',', "
		"concat('%d=', grp_cpu_run_mins)), "
		"max_tres_pj=concat_ws(',', concat('%d=', max_cpus_pj), "
		"concat('%d=', max_nodes_pj)), "
		"max_tres_mins_pj=concat_ws(',', "
		"concat('%d=', max_cpu_mins_pj)), "
		"max_tres_run_mins=concat_ws(',', "
		"concat('%d=', max_cpu_run_mins)); ",
		cluster_name, assoc_table,
		TRES_CPU, TRES_MEM, TRES_NODE, TRES_CPU, TRES_CPU,
		TRES_CPU, TRES_NODE, TRES_CPU, TRES_CPU);
	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert assoc_table for %s: %m", cluster_name);
	xfree(query);

	return rc;
}

static int _convert_qos_table(mysql_conn_t *mysql_conn)
{
	char *query = NULL;
	int rc;

	query = xstrdup_printf(
		"update %s set grp_tres=concat_ws(',', "
		"concat('%d=', grp_cpus), concat('%d=', grp_mem), "
		"concat('%d=', grp_nodes)), "
		"grp_tres_mins=concat_ws(',', concat('%d=', grp_cpu_mins)), "
		"grp_tres_run_mins=concat_ws(',', "
		"concat('%d=', grp_cpu_run_mins)), "
		"max_tres_pj=concat_ws(',', concat('%d=', max_cpus_per_job), "
		"concat('%d=', max_nodes_per_job)), "
		"max_tres_pu=concat_ws(',', concat('%d=', max_cpus_per_user), "
		"concat('%d=', max_nodes_per_user)), "
		"min_tres_pj=concat_ws(',', concat('%d=', min_cpus_per_job)), "
		"max_tres_mins_pj=concat_ws(',', "
		"concat('%d=', max_cpu_mins_per_job)), "
		"max_tres_run_mins_pu=concat_ws(',', "
		"concat('%d=', max_cpu_run_mins_per_user)); ",
		qos_table,
		TRES_CPU, TRES_MEM, TRES_NODE, TRES_CPU, TRES_CPU,
		TRES_CPU, TRES_NODE, TRES_CPU, TRES_NODE, TRES_CPU,
		TRES_CPU, TRES_CPU);
	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert qos_table: %m");
	xfree(query);

	return rc;
}

static int _convert_event_table(mysql_conn_t *mysql_conn, char *cluster_name,
				char *count_col_name)
{
	int rc = SLURM_SUCCESS;
	char *query = xstrdup_printf(
		"update \"%s_%s\" set tres=concat('%d=', %s);",
		cluster_name, event_table, TRES_CPU, count_col_name);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert %s_%s info: %m",
		      cluster_name, event_table);
	xfree(query);

	return rc;
}

static int _convert_cluster_usage_table(mysql_conn_t *mysql_conn,
					char *table)
{
	char *query = NULL;
	int rc;

	if ((rc = _rename_usage_columns(mysql_conn, table)) != SLURM_SUCCESS)
		return rc;

	query = xstrdup_printf("insert into %s (creation_time, mod_time, "
			       "deleted, id_tres, time_start, alloc_secs) "
			       "select creation_time, mod_time, deleted, "
			       "%d, time_start, consumed_energy from %s where "
			       "consumed_energy != 0 on duplicate key update "
			       "mod_time=%ld, alloc_secs=VALUES(alloc_secs);",
			       table, TRES_ENERGY, table, time(NULL));
	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert %s info: %m", table);
	xfree(query);

	return rc;
}

static int _convert_id_usage_table(mysql_conn_t *mysql_conn, char *table)
{
	char *query = NULL;
	int rc;

	if ((rc = _rename_usage_columns(mysql_conn, table)) != SLURM_SUCCESS)
		return rc;

	query = xstrdup_printf("insert into %s (creation_time, mod_time, "
			       "deleted, id, id_tres, time_start, alloc_secs) "
			       "select creation_time, mod_time, deleted, id, "
			       "%d, time_start, consumed_energy from %s where "
			       "consumed_energy != 0 on duplicate key update "
			       "mod_time=%ld, alloc_secs=VALUES(alloc_secs);",
			       table, TRES_ENERGY, table, time(NULL));
	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert %s info: %m", table);
	xfree(query);

	return rc;
}

static int _convert_cluster_usage_tables(mysql_conn_t *mysql_conn,
					 char *cluster_name)
{
	char table[200];
	int rc;

	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, cluster_day_table);
	if ((rc = _convert_cluster_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, cluster_hour_table);
	if ((rc = _convert_cluster_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, cluster_month_table);
	if ((rc = _convert_cluster_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	/* assoc tables */
	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, assoc_day_table);
	if ((rc = _convert_id_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, assoc_hour_table);
	if ((rc = _convert_id_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, assoc_month_table);
	if ((rc = _convert_id_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	/* wckey tables */
	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, wckey_day_table);
	if ((rc = _convert_id_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, wckey_hour_table);
	if ((rc = _convert_id_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	snprintf(table, sizeof(table), "\"%s_%s\"",
		 cluster_name, wckey_month_table);
	if ((rc = _convert_id_usage_table(mysql_conn, table))
	    != SLURM_SUCCESS)
		return rc;

	return rc;
}

static int _convert_job_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = xstrdup_printf("update \"%s_%s\" set tres_alloc="
				     "concat(concat('%d=', cpus_alloc));",
				     cluster_name, job_table, TRES_CPU);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert %s_%s info: %m",
		      cluster_name, job_table);
	xfree(query);

	return rc;
}

static int _convert_step_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = xstrdup_printf(
		"update \"%s_%s\" set tres_alloc=concat('%d=', cpus_alloc);",
		cluster_name, step_table, TRES_CPU);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert %s_%s info: %m",
		      cluster_name, step_table);
	xfree(query);

	return rc;
}

static int _convert_resv_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = xstrdup_printf(
		"update \"%s_%s\" set tres=concat('%d=', cpus);",
		cluster_name, resv_table, TRES_CPU);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert %s_%s info: %m",
		      cluster_name, resv_table);
	xfree(query);

	return rc;
}

static int _convert2_tables(mysql_conn_t *mysql_conn)
{
	char *query;
	MYSQL_RES *result = NULL;
	int i = 0, rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster_name;

	/* no valid clusters, just return */
	if (!(cluster_name = list_peek(as_mysql_total_cluster_list)))
		return SLURM_SUCCESS;

	/* See if the old table exist first.  If already ran here
	   default_acct and default_wckey won't exist.
	*/
	query = xstrdup_printf("show columns from \"%s_%s\" where "
			       "Field='grp_cpus';",
			       cluster_name, assoc_table);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	i = mysql_num_rows(result);
	mysql_free_result(result);
	result = NULL;

	if (!i)
		return 2;

	info("Updating database tables, this may take some time, "
	     "do not stop the process.");

	/* make it up to date */
	itr = list_iterator_create(as_mysql_total_cluster_list);
	while ((cluster_name = list_next(itr))) {
		query = xstrdup_printf("show columns from \"%s_%s\" where "
				       "Field='grp_cpus';",
				       cluster_name, assoc_table);

		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			error("QUERY BAD: No count col name for cluster %s, "
			      "this should never happen", cluster_name);
			continue;
		}
		xfree(query);

		if (!mysql_num_rows(result)) {
			error("No grp_cpus col name in assoc_table "
			      "for cluster %s, this should never happen",
			      cluster_name);
			continue;
		}

		/* make sure old tables are up to date */
		if ((rc = _update2_old_cluster_tables(mysql_conn, cluster_name)
		     != SLURM_SUCCESS)) {
			mysql_free_result(result);
			break;
		}

		/* Convert the event table first */
		info("converting assoc table for %s", cluster_name);
		if ((rc = _convert_assoc_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS)) {
			mysql_free_result(result);
			break;
		}
		mysql_free_result(result);
	}
	list_iterator_destroy(itr);

	/* make sure old non-cluster tables are up to date */
	if ((rc = _update2_old_tables(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if ((rc = _convert_qos_table(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	return rc;
}

extern int as_mysql_convert_tables(mysql_conn_t *mysql_conn)
{
	char *query;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int i = 0, rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster_name;

	xassert(as_mysql_total_cluster_list);

	if ((rc = _convert2_tables(mysql_conn)) == 2) {
		debug2("It appears the table conversions have already "
		       "taken place, hooray!");
		return SLURM_SUCCESS;
	} else if (rc != SLURM_SUCCESS)
		return rc;

	/* no valid clusters, just return */
	if (!(cluster_name = list_peek(as_mysql_total_cluster_list)))
		return SLURM_SUCCESS;


	/* See if the old table exist first.  If already ran here
	   default_acct and default_wckey won't exist.
	*/
	query = xstrdup_printf("show columns from \"%s_%s\" where "
			       "Field='cpu_count' || Field='count';",
			       cluster_name, event_table);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	i = mysql_num_rows(result);
	mysql_free_result(result);
	result = NULL;

	if (!i) {
		info("Conversion done: success!");
		return SLURM_SUCCESS;
	}

	/* make it up to date */
	itr = list_iterator_create(as_mysql_total_cluster_list);
	while ((cluster_name = list_next(itr))) {
		query = xstrdup_printf("show columns from \"%s_%s\" where "
				       "Field='cpu_count' || Field='count';",
				       cluster_name, event_table);

		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			error("QUERY BAD: No count col name for cluster %s, "
			      "this should never happen", cluster_name);
			continue;
		}
		xfree(query);

		if (!(row = mysql_fetch_row(result)) || !row[0] || !row[0][0]) {
			error("No count col name for cluster %s, "
			      "this should never happen", cluster_name);
			continue;
		}

		/* make sure old tables are up to date */
		if ((rc = _update_old_cluster_tables(mysql_conn, cluster_name,
						     row[0])
		     != SLURM_SUCCESS)) {
			mysql_free_result(result);
			break;
		}

		/* Convert the event table first */
		info("converting event table for %s", cluster_name);
		if ((rc = _convert_event_table(mysql_conn, cluster_name, row[0])
		     != SLURM_SUCCESS)) {
			mysql_free_result(result);
			break;
		}
		mysql_free_result(result);


		/* Now convert the cluster usage tables */
		info("converting cluster usage tables for %s", cluster_name);
		if ((rc = _convert_cluster_usage_tables(
			     mysql_conn, cluster_name) != SLURM_SUCCESS))
			break;

		/* Now convert the job tables */
		info("converting job table for %s", cluster_name);
		if ((rc = _convert_job_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;

		/* Now convert the reservation tables */
		info("converting reservation table for %s", cluster_name);
		if ((rc = _convert_resv_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;

		/* Now convert the step tables */
		info("converting step table for %s", cluster_name);
		if ((rc = _convert_step_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;
	}
	list_iterator_destroy(itr);

	if (rc == SLURM_SUCCESS)
		info("Conversion done: success!");

	return rc;
}
