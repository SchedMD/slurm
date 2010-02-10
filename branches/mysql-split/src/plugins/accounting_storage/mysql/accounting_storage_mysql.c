/*****************************************************************************\
 *  accounting_storage_mysql.c - accounting interface to mysql.
 *
 *  $Id: accounting_storage_mysql.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
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
 *****************************************************************************
 * Notes on mysql configuration
 *	Assumes mysql is installed as user root
 *	Assumes SlurmUser is configured as user slurm
 * # mysqladmin create <db_name>
 *	The <db_name> goes into slurmdbd.conf as StorageLoc
 * # mysql --user=root -p
 * mysql> GRANT ALL ON *.* TO 'slurm'@'localhost' IDENTIFIED BY PASSWORD 'pw';
 * mysql> GRANT SELECT, INSERT ON *.* TO 'slurm'@'localhost';
\*****************************************************************************/

#include "accounting_storage_mysql.h"
#include "mysql_acct.h"
#include "mysql_archive.h"
#include "mysql_assoc.h"
#include "mysql_cluster.h"
#include "mysql_convert.h"
#include "mysql_job.h"
#include "mysql_jobacct_process.h"
#include "mysql_problems.h"
#include "mysql_qos.h"
#include "mysql_resv.h"
#include "mysql_rollup.h"
#include "mysql_txn.h"
#include "mysql_usage.h"
#include "mysql_user.h"
#include "mysql_wckey.h"

List mysql_cluster_list = NULL;
pthread_mutex_t mysql_cluster_list_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "accounting_storage" for SLURM job completion
 * logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "accounting_storage/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API
 * matures.
 */
const char plugin_name[] = "Accounting storage MYSQL plugin";
const char plugin_type[] = "accounting_storage/mysql";
const uint32_t plugin_version = 100;

static mysql_db_info_t *mysql_db_info = NULL;
static char *mysql_db_name = NULL;

#define DELETE_SEC_BACK 86400

char *acct_coord_table = "acct_coord_table";
char *acct_table = "acct_table";
char *assoc_day_table = "assoc_day_usage_table";
char *assoc_hour_table = "assoc_hour_usage_table";
char *assoc_month_table = "assoc_month_usage_table";
char *assoc_table = "assoc_table";
char *cluster_day_table = "cluster_day_usage_table";
char *cluster_hour_table = "cluster_hour_usage_table";
char *cluster_month_table = "cluster_month_usage_table";
char *cluster_table = "cluster_table";
char *event_table = "event_table";
char *job_table = "job_table";
char *last_ran_table = "last_ran_table";
char *qos_table = "qos_table";
char *resv_table = "resv_table";
char *step_table = "step_table";
char *txn_table = "txn_table";
char *user_table = "user_table";
char *suspend_table = "suspend_table";
char *wckey_day_table = "wckey_day_usage_table";
char *wckey_hour_table = "wckey_hour_usage_table";
char *wckey_month_table = "wckey_month_usage_table";
char *wckey_table = "wckey_table";

static char *default_qos_str = NULL;

static List _get_cluster_names(MYSQL *db_conn)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	List ret_list = NULL;
	char *query = xstrdup_printf("select name from %s", cluster_table);

	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		return ret_list;
	}
	xfree(query);

	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		if(row[0] && row[0][0])
			list_append(ret_list, xstrdup(row[0]));
	}
	mysql_free_result(result);

	return ret_list;

}

static int _set_qos_cnt(MYSQL *db_conn)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = xstrdup_printf("select MAX(id) from %s", qos_table);

	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(row = mysql_fetch_row(result))) {
		mysql_free_result(result);
		return SLURM_ERROR;
	}

	/* Set the current qos_count on the system for
	   generating bitstr of that length.  Since 0 isn't
	   possible as an id we add 1 to the total to burn 0 and
	   start at the 1 bit.
	*/
	g_qos_count = atoi(row[0]) + 1;
	mysql_free_result(result);

	return SLURM_SUCCESS;
}

/* this function is here to see if any of what we are trying to remove
 * has jobs that are or were once running.  So if we have jobs and the
 * object is less than a day old we don't want to delete it only set
 * the deleted flag.
 */
static bool _check_jobs_before_remove(mysql_conn_t *mysql_conn,
				      char *cluster_name,
				      char *assoc_char)
{
	char *query = NULL;
	bool rc = 0;
	MYSQL_RES *result = NULL;

	query = xstrdup_printf("select t0.id_assoc from %s_%s as t0, "
			       "%s_%s as t1, %s_%s as t2 where t1.lft between "
			       "t2.lft and t2.rgt && (%s) "
			       "and t0.id_assoc=t1.id_assoc limit 1;",
			       cluster_name, job_table,
			       cluster_name, assoc_table,
			       cluster_name, assoc_table,
			       assoc_char);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if(mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	mysql_free_result(result);
	return rc;
}

/* Same as above but for associations instead of other tables */
static bool _check_jobs_before_remove_assoc(mysql_conn_t *mysql_conn,
					    char *cluster_name,
					    char *assoc_char)
{
	char *query = NULL;
	bool rc = 0;
	MYSQL_RES *result = NULL;

	query = xstrdup_printf("select t1.associd from %s as t1, "
			       "%s_%s as t2 where (%s_%s) "
			       "and t1.associd=t2.id limit 1;",
			       cluster_name, job_table,
			       cluster_name, assoc_table,
			       assoc_char);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if(mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	mysql_free_result(result);
	return rc;
}

/* Same as above but for things having nothing to do with associations
 * like qos or wckey */
static bool _check_jobs_before_remove_without_assoctable(
	mysql_conn_t *mysql_conn, char *cluster_name, char *where_char)
{
	char *query = NULL;
	bool rc = 0;
	MYSQL_RES *result = NULL;

	query = xstrdup_printf("select associd from %s_%s where (%s) limit 1;",
			       cluster_name, job_table, where_char);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if(mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	mysql_free_result(result);
	return rc;
}

static mysql_db_info_t *_mysql_acct_create_db_info()
{
	mysql_db_info_t *db_info = xmalloc(sizeof(mysql_db_info_t));
	db_info->port = slurm_get_accounting_storage_port();
	if(!db_info->port) {
		db_info->port = DEFAULT_MYSQL_PORT;
		slurm_set_accounting_storage_port(db_info->port);
	}
	db_info->host = slurm_get_accounting_storage_host();
	db_info->backup = slurm_get_accounting_storage_backup_host();

	db_info->user = slurm_get_accounting_storage_user();
	db_info->pass = slurm_get_accounting_storage_pass();
	return db_info;
}

/* Any time a new table is added set it up here */
static int _mysql_acct_check_tables(MYSQL *db_conn)
{
	storage_field_t acct_coord_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "acct", "tinytext not null" },
		{ "user", "tinytext not null" },
		{ NULL, NULL}
	};

	storage_field_t acct_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "description", "text not null" },
		{ "organization", "text not null" },
		{ NULL, NULL}
	};

	storage_field_t assoc_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id_assoc", "int not null auto_increment" },
		{ "user", "tinytext not null default ''" },
		{ "acct", "tinytext not null" },
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

	storage_field_t assoc_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id_assoc", "int not null" },
		{ "time_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	storage_field_t cluster_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "control_host", "tinytext not null default ''" },
		{ "control_port", "int unsigned not null default 0" },
		{ "rpc_version", "smallint unsigned not null default 0" },
		{ "classification", "smallint unsigned default 0" },
		{ NULL, NULL}
	};

	storage_field_t cluster_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "time_start", "int unsigned not null" },
		{ "cpu_count", "int default 0" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "down_cpu_secs", "bigint default 0" },
		{ "pdown_cpu_secs", "bigint default 0" },
		{ "idle_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	storage_field_t event_table_fields[] = {
		{ "time_start", "int unsigned not null" },
		{ "time_end", "int unsigned default 0 not null" },
		{ "node_name", "tinytext default '' not null" },
		{ "cluster_nodes", "text not null default ''" },
		{ "cpu_count", "int not null" },
		{ "reason", "tinytext not null" },
		{ "reason_uid", "int unsigned default 0xfffffffe not null" },
		{ "state", "smallint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t job_table_fields[] = {
		{ "job_db_inx", "int not null auto_increment" },
		{ "deleted", "tinyint default 0" },
		{ "account", "tinytext" },
		{ "cpus_req", "int unsigned not null" },
		{ "cpus_alloc", "int unsigned not null" },
		{ "exit_code", "int default 0 not null" },
		{ "job_name", "tinytext not null" },
		{ "id_assoc", "int unsigned not null" },
		{ "id_block", "tinytext" },
		{ "id_job", "int unsigned not null" },
		{ "id_resv", "int unsigned not null" },
		{ "id_wckey", "int unsigned not null" },
		{ "id_user", "int unsigned not null" },
		{ "id_group", "int unsigned not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "nodelist", "text" },
		{ "nodes_alloc", "int unsigned not null" },
		{ "node_inx", "text" },
		{ "partition", "tinytext not null" },
		{ "priority", "int not null" },
		{ "qos", "smallint default 0" },
		{ "state", "smallint unsigned not null" },
		{ "timelimit", "int unsigned default 0 not null" },
		{ "time_submit", "int unsigned default 0 not null" },
		{ "time_eligible", "int unsigned default 0 not null" },
		{ "time_start", "int unsigned default 0 not null" },
		{ "time_end", "int unsigned default 0 not null" },
		{ "time_suspended", "int unsigned default 0 not null" },
		{ "wckey", "tinytext not null default ''" },
		{ "track_steps", "tinyint not null" },
		{ NULL, NULL}
	};

	storage_field_t last_ran_table_fields[] = {
		{ "hourly_rollup", "int unsigned default 0 not null" },
		{ "daily_rollup", "int unsigned default 0 not null" },
		{ "monthly_rollup", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t qos_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "name", "tinytext not null" },
		{ "description", "text" },
		{ "max_jobs_per_user", "int default NULL" },
		{ "max_submit_jobs_per_user", "int default NULL" },
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
		{ "preempt", "text not null default ''" },
		{ "priority", "int default 0" },
		{ "usage_factor", "double default 1.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t resv_table_fields[] = {
		{ "id_resv", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "assoclist", "text not null default ''" },
		{ "cpus", "int unsigned not null" },
		{ "flags", "smallint unsigned default 0 not null" },
		{ "nodelist", "text not null default ''" },
		{ "node_inx", "text not null default ''" },
		{ "resv_name", "text not null" },
		{ "time_start", "int unsigned default 0 not null"},
		{ "time_end", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields[] = {
		{ "job_db_inx", "int not null" },
		{ "deleted", "tinyint default 0" },
		{ "cpus_alloc", "int unsigned not null" },
		{ "exit_code", "int default 0 not null" },
		{ "id_step", "smallint not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "nodelist", "text not null" },
		{ "nodes_alloc", "int unsigned not null" },
		{ "node_inx", "text" },
		{ "state", "smallint unsigned not null" },
		{ "step_name", "text not null" },
		{ "task_cnt", "int unsigned not null" },
		{ "task_dist", "smallint default 0" },
		{ "time_start", "int unsigned default 0 not null" },
		{ "time_end", "int unsigned default 0 not null" },
		{ "time_suspended", "int unsigned default 0 not null" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "max_pages", "int unsigned default 0 not null" },
		{ "max_pages_task", "smallint unsigned default 0 not null" },
		{ "max_pages_node", "int unsigned default 0 not null" },
		{ "ave_pages", "double unsigned default 0.0 not null" },
		{ "max_rss", "bigint unsigned default 0 not null" },
		{ "max_rss_task", "smallint unsigned default 0 not null" },
		{ "max_rss_node", "int unsigned default 0 not null" },
		{ "ave_rss", "double unsigned default 0.0 not null" },
		{ "max_vsize", "bigint unsigned default 0 not null" },
		{ "max_vsize_task", "smallint unsigned default 0 not null" },
		{ "max_vsize_node", "int unsigned default 0 not null" },
		{ "ave_vsize", "double unsigned default 0.0 not null" },
		{ "min_cpu", "int unsigned default 0 not null" },
		{ "min_cpu_task", "smallint unsigned default 0 not null" },
		{ "min_cpu_node", "int unsigned default 0 not null" },
		{ "ave_cpu", "double unsigned default 0.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t suspend_table_fields[] = {
		{ "job_db_inx", "int not null" },
		{ "id_assoc", "int not null" },
		{ "time_start", "int unsigned default 0 not null" },
		{ "time_end", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t txn_table_fields[] = {
		{ "id", "int not null auto_increment" },
		{ "timestamp", "int unsigned default 0 not null" },
		{ "action", "smallint not null" },
		{ "name", "text not null" },
		{ "actor", "tinytext not null" },
		{ "info", "blob" },
		{ NULL, NULL}
	};

	storage_field_t user_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "default_acct", "tinytext not null" },
		{ "default_wckey", "tinytext not null default ''" },
		{ "admin_level", "smallint default 1 not null" },
		{ NULL, NULL}
	};

	storage_field_t wckey_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id_wckey", "int not null auto_increment" },
		{ "wckey_name", "tinytext not null default ''" },
		{ "user", "tinytext not null" },
		{ NULL, NULL}
	};

	storage_field_t wckey_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id_wckey", "int not null" },
		{ "time_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	char *get_parent_proc =
		"drop procedure if exists get_parent_limits; "
		"create procedure get_parent_limits("
		"my_table text, acct text, cluster text, without_limits int) "
		"begin "
		"set @par_id = NULL; "
		"set @mj = NULL; "
		"set @msj = NULL; "
		"set @mcpj = NULL; "
		"set @mnpj = NULL; "
		"set @mwpj = NULL; "
		"set @mcmpj = NULL; "
		"set @qos = ''; "
		"set @delta_qos = ''; "
		"set @my_acct = acct; "
		"if without_limits then "
		"set @mj = 0; "
		"set @msj = 0; "
		"set @mcpj = 0; "
		"set @mnpj = 0; "
		"set @mwpj = 0; "
		"set @mcmpj = 0; "
		"set @qos = 0; "
		"set @delta_qos = 0; "
		"end if; "
		"REPEAT "
		"set @s = 'select '; "
		"if @par_id is NULL then set @s = CONCAT("
		"@s, '@par_id := id_assoc, '); "
		"end if; "
		"if @mj is NULL then set @s = CONCAT("
		"@s, '@mj := max_jobs, '); "
		"end if; "
		"if @msj is NULL then set @s = CONCAT("
		"@s, '@msj := max_submit_jobs, '); "
		"end if; "
		"if @mcpj is NULL then set @s = CONCAT("
		"@s, '@mcpj := max_cpus_per_job, ') ;"
		"end if; "
		"if @mnpj is NULL then set @s = CONCAT("
		"@s, '@mnpj := max_nodes_per_job, ') ;"
		"end if; "
		"if @mwpj is NULL then set @s = CONCAT("
		"@s, '@mwpj := max_wall_duration_per_job, '); "
		"end if; "
		"if @mcmpj is NULL then set @s = CONCAT("
		"@s, '@mcmpj := max_cpu_mins_per_job, '); "
		"end if; "
		"if @qos = '' then set @s = CONCAT("
		"@s, '@qos := qos, "
		"@delta_qos := CONCAT(delta_qos, @delta_qos), '); "
		"end if; "
		"set @s = concat(@s, ' @my_acct := parent_acct from ', "
		"my_table, ' where acct = \"', @my_acct, '\" && "
		"cluster = \"', cluster, '\" && user=\"\"'); "
		"prepare query from @s; "
		"execute query; "
		"deallocate prepare query; "
		"UNTIL (@mj != -1 && @msj != -1 && @mcpj != -1 "
		"&& @mnpj != -1 && @mwpj != -1 "
		"&& @mcmpj != -1 && @qos != '') || @my_acct = '' END REPEAT; "
		"END;";
	char *query = NULL;
	time_t now = time(NULL);
	char *cluster_name = NULL;
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;

	/* Make the cluster table first since we build other tables
	   built off this one */
	if(mysql_db_create_table(db_conn, cluster_table,
				 cluster_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	slurm_mutex_lock(&mysql_cluster_list_lock);
	if(!(mysql_cluster_list = _get_cluster_names(db_conn))) {
		error("issue getting contents of %s", cluster_table);
		slurm_mutex_unlock(&mysql_cluster_list_lock);
		return SLURM_ERROR;
	}

	/* might as well do all the cluster centric tables inside this
	 * lock */
	itr = list_iterator_create(mysql_cluster_list);
	while((cluster_name = list_next(itr))) {
		char table_name[200];

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, assoc_table);
		if(mysql_db_create_table(db_conn, table_name,
					 assoc_table_fields,
					 ", primary key (id_assoc), "
					 " unique index (user(20), acct(20), "
					 "partition(20)))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, assoc_day_table);
		if(mysql_db_create_table(db_conn, table_name,
					 assoc_usage_table_fields,
					 ", primary key (id_assoc, "
					 "time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, assoc_hour_table);
		if(mysql_db_create_table(db_conn, table_name,
					 assoc_usage_table_fields,
					 ", primary key (id_assoc, "
					 "time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, assoc_month_table);
		if(mysql_db_create_table(db_conn, table_name,
					 assoc_usage_table_fields,
					 ", primary key (id_assoc, "
					 "time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, cluster_day_table);
		if(mysql_db_create_table(db_conn, table_name,
					 cluster_usage_table_fields,
					 ", primary key (time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, cluster_hour_table);
		if(mysql_db_create_table(db_conn, table_name,
					 cluster_usage_table_fields,
					 ", primary key (time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, cluster_month_table);
		if(mysql_db_create_table(db_conn, table_name,
					 cluster_usage_table_fields,
					 ", primary key (time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, event_table);
		if(mysql_db_create_table(db_conn, table_name,
					 event_table_fields,
					 ", primary key (node_name(20), "
					 "time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, job_table);
		if(mysql_db_create_table(db_conn, table_name, job_table_fields,
					 ", primary key (job_db_inx), "
					 "unique index (id_job, "
					 "id_assoc, time_submit))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, resv_table);
		if(mysql_db_create_table(db_conn, table_name,
					 resv_table_fields,
					 ", primary key (id_resv, time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, step_table);
		if(mysql_db_create_table(db_conn, table_name,
					 step_table_fields,
					 ", primary key (job_db_inx, id_step))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, suspend_table);
		if(mysql_db_create_table(db_conn, table_name,
					 suspend_table_fields,
					 ")") == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, wckey_table);
		if(mysql_db_create_table(db_conn, table_name,
					 wckey_table_fields,
					 ", primary key (id_wckey), "
					 " unique index (wckey_name(20), "
					 "user(20)))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, wckey_day_table);
		if(mysql_db_create_table(db_conn, table_name,
					 wckey_usage_table_fields,
					 ", primary key (id_wckey, "
					 "time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, wckey_hour_table);
		if(mysql_db_create_table(db_conn, table_name,
					 wckey_usage_table_fields,
					 ", primary key (id_wckey, "
					 "time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}

		snprintf(table_name, sizeof(table_name), "%s_%s",
			 cluster_name, wckey_month_table);
		if(mysql_db_create_table(db_conn, table_name,
					 wckey_usage_table_fields,
					 ", primary key (id_wckey, "
					 "time_start))")
		   == SLURM_ERROR) {
			rc = SLURM_ERROR;
			break;
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&mysql_cluster_list_lock);
	if(rc != SLURM_SUCCESS)
		return rc;

	if(mysql_convert_tables(db_conn) != SLURM_SUCCESS)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, acct_coord_table,
				 acct_coord_table_fields,
				 ", primary key (acct(20), user(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, acct_table, acct_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, last_ran_table,
				 last_ran_table_fields,
				 ")") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, qos_table,
				 qos_table_fields,
				 ", primary key (id), "
				 "unique index (name(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;
	else {
		int qos_id = 0;
		if(slurmdbd_conf && slurmdbd_conf->default_qos) {
			List char_list = list_create(slurm_destroy_char);
			char *qos = NULL;
			ListIterator itr = NULL;
			slurm_addto_char_list(char_list,
					      slurmdbd_conf->default_qos);
			/* NOTE: you can not use list_pop, or list_push
			   anywhere either, since mysql is
			   exporting something of the same type as a macro,
			   which messes everything up
			   (my_list.h is the bad boy).
			*/
			itr = list_iterator_create(char_list);
			while((qos = list_next(itr))) {
				query = xstrdup_printf(
					"insert into %s "
					"(creation_time, mod_time, name, "
					"description) "
					"values (%d, %d, '%s', "
					"'Added as default') "
					"on duplicate key update "
					"id=LAST_INSERT_ID(id), deleted=0;",
					qos_table, now, now, qos);
				qos_id = mysql_insert_ret_id(db_conn, query);
				if(!qos_id)
					fatal("problem added qos '%s", qos);
				xstrfmtcat(default_qos_str, ",%d", qos_id);
				xfree(query);
			}
			list_iterator_destroy(itr);
			list_destroy(char_list);
		} else {
			query = xstrdup_printf(
				"insert into %s "
				"(creation_time, mod_time, name, description) "
				"values (%d, %d, 'normal', "
				"'Normal QOS default') "
				"on duplicate key update "
				"id=LAST_INSERT_ID(id), deleted=0;",
				qos_table, now, now);
			//debug3("%s", query);
			qos_id = mysql_insert_ret_id(db_conn, query);
			if(!qos_id)
				fatal("problem added qos 'normal");

			xstrfmtcat(default_qos_str, ",%d", qos_id);
			xfree(query);
		}

		if(_set_qos_cnt(db_conn) != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	if(mysql_db_create_table(db_conn, txn_table, txn_table_fields,
				 ", primary key (id))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, user_table, user_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	rc = mysql_db_query(db_conn, get_parent_proc);

	/* Add user root to be a user by default and have this default
	 * account be root.  If already there just update
	 * name='root'.  That way if the admins delete it it will
	 * remained deleted. Creation time will be 0 so it will never
	 * really be deleted.
	 */
	query = xstrdup_printf(
		"insert into %s (creation_time, mod_time, name, default_acct, "
		"admin_level) values (0, %d, 'root', 'root', %u) "
		"on duplicate key update name='root';",
		user_table, now, ACCT_ADMIN_SUPER_USER, now);
	xstrfmtcat(query,
		   "insert into %s (creation_time, mod_time, name, "
		   "description, organization) values (0, %d, 'root', "
		   "'default root account', 'root') on duplicate key "
		   "update name='root';",
		   acct_table, now);

	//debug3("%s", query);
	mysql_db_query(db_conn, query);
	xfree(query);

	return rc;
}

/* This should be added to the beginning of each function to make sure
 * we have a connection to the database before we try to use it.
 */
extern int check_connection(mysql_conn_t *mysql_conn)
{
	if(!mysql_conn) {
		error("We need a connection to run this");
		errno = SLURM_ERROR;
		return SLURM_ERROR;
	} else if(!mysql_conn->db_conn
		  || mysql_db_ping(mysql_conn->db_conn) != 0) {
		if(mysql_get_db_connection(&mysql_conn->db_conn,
					   mysql_db_name, mysql_db_info)
		   != SLURM_SUCCESS) {
			error("unable to re-connect to mysql database");
			errno = ESLURM_DB_CONNECTION;
			return ESLURM_DB_CONNECTION;
		}
	}
	return SLURM_SUCCESS;
}

/* Let me know if the last statement had rows that were affected.
 */
extern int last_affected_rows(MYSQL *mysql_db)
{
	int status=0, rows=0;
	MYSQL_RES *result = NULL;

	do {
		result = mysql_store_result(mysql_db);
		if (result)
			mysql_free_result(result);
		else
			if (mysql_field_count(mysql_db) == 0) {
				status = mysql_affected_rows(mysql_db);
				if(status > 0)
					rows = status;
			}
		if ((status = mysql_next_result(mysql_db)) > 0)
			debug3("Could not execute statement\n");
	} while (status == 0);

	return rows;
}

extern int setup_association_limits(acct_association_rec_t *assoc,
				    char **cols, char **vals,
				    char **extra, qos_level_t qos_level,
				    bool get_fs)
{
	if(!assoc)
		return SLURM_ERROR;

	if((int)assoc->shares_raw >= 0) {
		xstrcat(*cols, ", fairshare");
		xstrfmtcat(*vals, ", %u", assoc->shares_raw);
		xstrfmtcat(*extra, ", fairshare=%u", assoc->shares_raw);
	} else if (((int)assoc->shares_raw == INFINITE) || get_fs) {
		xstrcat(*cols, ", fairshare");
		xstrcat(*vals, ", 1");
		xstrcat(*extra, ", fairshare=1");
		assoc->shares_raw = 1;
	}

	if((int)assoc->grp_cpu_mins >= 0) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrfmtcat(*vals, ", %llu", assoc->grp_cpu_mins);
		xstrfmtcat(*extra, ", grp_cpu_mins=%llu",
			   assoc->grp_cpu_mins);
	} else if((int)assoc->grp_cpu_mins == INFINITE) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpu_mins=NULL");
	}

	if((int)assoc->grp_cpus >= 0) {
		xstrcat(*cols, ", grp_cpus");
		xstrfmtcat(*vals, ", %u", assoc->grp_cpus);
		xstrfmtcat(*extra, ", grp_cpus=%u", assoc->grp_cpus);
	} else if((int)assoc->grp_cpus == INFINITE) {
		xstrcat(*cols, ", grp_cpus");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpus=NULL");
	}

	if((int)assoc->grp_jobs >= 0) {
		xstrcat(*cols, ", grp_jobs");
		xstrfmtcat(*vals, ", %u", assoc->grp_jobs);
		xstrfmtcat(*extra, ", grp_jobs=%u", assoc->grp_jobs);
	} else if((int)assoc->grp_jobs == INFINITE) {
		xstrcat(*cols, ", grp_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_jobs=NULL");
	}

	if((int)assoc->grp_nodes >= 0) {
		xstrcat(*cols, ", grp_nodes");
		xstrfmtcat(*vals, ", %u", assoc->grp_nodes);
		xstrfmtcat(*extra, ", grp_nodes=%u", assoc->grp_nodes);
	} else if((int)assoc->grp_nodes == INFINITE) {
		xstrcat(*cols, ", grp_nodes");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_nodes=NULL");
	}

	if((int)assoc->grp_submit_jobs >= 0) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrfmtcat(*vals, ", %u",
			   assoc->grp_submit_jobs);
		xstrfmtcat(*extra, ", grp_submit_jobs=%u",
			   assoc->grp_submit_jobs);
	} else if((int)assoc->grp_submit_jobs == INFINITE) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_submit_jobs=NULL");
	}

	if((int)assoc->grp_wall >= 0) {
		xstrcat(*cols, ", grp_wall");
		xstrfmtcat(*vals, ", %u", assoc->grp_wall);
		xstrfmtcat(*extra, ", grp_wall=%u",
			   assoc->grp_wall);
	} else if((int)assoc->grp_wall == INFINITE) {
		xstrcat(*cols, ", grp_wall");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_wall=NULL");
	}

	if((int)assoc->max_cpu_mins_pj >= 0) {
		xstrcat(*cols, ", max_cpu_mins_per_job");
		xstrfmtcat(*vals, ", %llu", assoc->max_cpu_mins_pj);
		xstrfmtcat(*extra, ", max_cpu_mins_per_job=%u",
			   assoc->max_cpu_mins_pj);
	} else if((int)assoc->max_cpu_mins_pj == INFINITE) {
		xstrcat(*cols, ", max_cpu_mins_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpu_mins_per_job=NULL");
	}

	if((int)assoc->max_cpus_pj >= 0) {
		xstrcat(*cols, ", max_cpus_per_job");
		xstrfmtcat(*vals, ", %u", assoc->max_cpus_pj);
		xstrfmtcat(*extra, ", max_cpus_per_job=%u",
			   assoc->max_cpus_pj);
	} else if((int)assoc->max_cpus_pj == INFINITE) {
		xstrcat(*cols, ", max_cpus_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpus_per_job=NULL");
	}

	if((int)assoc->max_jobs >= 0) {
		xstrcat(*cols, ", max_jobs");
		xstrfmtcat(*vals, ", %u", assoc->max_jobs);
		xstrfmtcat(*extra, ", max_jobs=%u",
			   assoc->max_jobs);
	} else if((int)assoc->max_jobs == INFINITE) {
		xstrcat(*cols, ", max_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_jobs=NULL");
	}

	if((int)assoc->max_nodes_pj >= 0) {
		xstrcat(*cols, ", max_nodes_per_job");
		xstrfmtcat(*vals, ", %u", assoc->max_nodes_pj);
		xstrfmtcat(*extra, ", max_nodes_per_job=%u",
			   assoc->max_nodes_pj);
	} else if((int)assoc->max_nodes_pj == INFINITE) {
		xstrcat(*cols, ", max_nodes_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_nodes_per_job=NULL");
	}

	if((int)assoc->max_submit_jobs >= 0) {
		xstrcat(*cols, ", max_submit_jobs");
		xstrfmtcat(*vals, ", %u", assoc->max_submit_jobs);
		xstrfmtcat(*extra, ", max_submit_jobs=%u",
			   assoc->max_submit_jobs);
	} else if((int)assoc->max_submit_jobs == INFINITE) {
		xstrcat(*cols, ", max_submit_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_submit_jobs=NULL");
	}

	if((int)assoc->max_wall_pj >= 0) {
		xstrcat(*cols, ", max_wall_duration_per_job");
		xstrfmtcat(*vals, ", %u", assoc->max_wall_pj);
		xstrfmtcat(*extra, ", max_wall_duration_per_job=%u",
			   assoc->max_wall_pj);
	} else if((int)assoc->max_wall_pj == INFINITE) {
		xstrcat(*cols, ", max_wall_duration_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_wall_duration_per_job=NULL");
	}

	/* when modifying the qos it happens in the actual function
	   since we have to wait until we hear about the parent first. */
	if(qos_level == QOS_LEVEL_MODIFY)
		goto end_qos;

	if(assoc->qos_list && list_count(assoc->qos_list)) {
		char *qos_type = "qos";
		char *qos_val = NULL;
		char *tmp_char = NULL;
		int set = 0;
		ListIterator qos_itr =
			list_iterator_create(assoc->qos_list);

		while((tmp_char = list_next(qos_itr))) {
			/* we don't want to include blank names */
			if(!tmp_char[0])
				continue;
			if(!set) {
				if(tmp_char[0] == '+' || tmp_char[0] == '-')
					qos_type = "delta_qos";
				set = 1;
			}
			xstrfmtcat(qos_val, ",%s", tmp_char);
		}

		list_iterator_destroy(qos_itr);
		if(qos_val) {
			xstrfmtcat(*cols, ", %s", qos_type);
			xstrfmtcat(*vals, ", '%s'", qos_val);
			xstrfmtcat(*extra, ", %s=\"%s\"", qos_type, qos_val);
			xfree(qos_val);
		}
	} else if((qos_level == QOS_LEVEL_SET) && default_qos_str) {
		/* Add default qos to the account */
		xstrcat(*cols, ", qos");
		xstrfmtcat(*vals, ", '%s'", default_qos_str);
		xstrfmtcat(*extra, ", qos=\"%s\"", default_qos_str);
		if(!assoc->qos_list)
			assoc->qos_list = list_create(slurm_destroy_char);
		slurm_addto_char_list(assoc->qos_list, default_qos_str);
	} else {
		/* clear the qos */
		xstrcat(*cols, ", qos, delta_qos");
		xstrcat(*vals, ", '', ''");
		xstrcat(*extra, ", qos=\"\", delta_qos=\"\"");
	}
end_qos:

	return SLURM_SUCCESS;

}

/* This is called by most modify functions to alter the table and
 * insert a new line in the transaction table.
 */
extern int modify_common(mysql_conn_t *mysql_conn,
			 uint16_t type,
			 time_t now,
			 char *user_name,
			 char *table,
			 char *cond_char,
			 char *vals)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	char *tmp_cond_char = fix_double_quotes(cond_char);
	char *tmp_vals = NULL;

	if(vals[1])
		tmp_vals = fix_double_quotes(vals+2);

	xstrfmtcat(query,
		   "update %s set mod_time=%d%s "
		   "where deleted=0 && %s;",
		   table, now, vals,
		   cond_char);
	xstrfmtcat(query,
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%d, %d, \"%s\", \"%s\", \"%s\");",
		   txn_table,
		   now, type, tmp_cond_char, user_name, tmp_vals);
	xfree(tmp_cond_char);
	xfree(tmp_vals);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);

		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/* Every option in assoc_char should have a 't1.' infront of it. */
extern int remove_common(mysql_conn_t *mysql_conn,
			 uint16_t type,
			 time_t now,
			 char *user_name,
			 char *table,
			 char *name_char,
			 char *assoc_char,
			 char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	char *loc_assoc_char = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	time_t day_old = now - DELETE_SEC_BACK;
	bool has_jobs = false;
	char *tmp_name_char = fix_double_quotes(name_char);
	bool cluster_centric = true;

	/* figure out which tables we need to append the cluster name to */
	if((table == cluster_table) || (table == acct_coord_table)
	   || (table == acct_table) || (table == qos_table)
	   || (table == txn_table) || (table == user_table))
		cluster_centric = false;

	/* If we have jobs associated with this we do not want to
	 * really delete it for accounting purposes.  This is for
	 * corner cases most of the time this won't matter.
	 */
	if(table == acct_coord_table) {
		/* This doesn't apply for these tables since we are
		 * only looking for association type tables.
		 */
	} else if((table == qos_table) || (table == wckey_table)) {
		has_jobs = _check_jobs_before_remove_without_assoctable(
			mysql_conn, cluster_name, assoc_char);
	} else if(table != assoc_table) {
		has_jobs = _check_jobs_before_remove(
			mysql_conn, cluster_name, assoc_char);
	} else {
		has_jobs = _check_jobs_before_remove_assoc(
			mysql_conn, cluster_name, name_char);
	}
	/* we want to remove completely all that is less than a day old */
	if(!has_jobs && table != assoc_table) {
		if(cluster_centric)
			query = xstrdup_printf("delete from %s_%s where "
					       "creation_time>%d "
					       "&& (%s);"
					       "alter table %s_%s "
					       "AUTO_INCREMENT=0;",
					       cluster_name, table, day_old,
					       name_char, cluster_name, table);
		else
			query = xstrdup_printf("delete from %s where "
					       "creation_time>%d "
					       "&& (%s);"
					       "alter table %s "
					       "AUTO_INCREMENT=0;",
					       table, day_old,
					       name_char, table);
	}

	if(table != assoc_table) {
		if(cluster_centric)
			xstrfmtcat(query,
				   "update %s_%s set mod_time=%d, deleted=1 "
				   "where deleted=0 && (%s);",
				   cluster_name, table, now, name_char);
		else
			xstrfmtcat(query,
				   "update %s set mod_time=%d, deleted=1 "
				   "where deleted=0 && (%s);",
				   table, now, name_char);
	}
	xstrfmtcat(query,
		   "insert into %s (timestamp, action, name, actor) "
		   "values (%d, %d, \"%s\", \"%s\");",
		   txn_table,
		   now, type, tmp_name_char, user_name);
	xfree(tmp_name_char);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);

		return SLURM_ERROR;
	} else if((table == acct_coord_table)
		  || (table == qos_table)
		  || (table == wckey_table))
		return SLURM_SUCCESS;

	/* mark deleted=1 or remove completely the accounting tables
	*/
	if(table != assoc_table) {
		if(!assoc_char) {
			error("no assoc_char");
			if(mysql_conn->rollback) {
				mysql_db_rollback(mysql_conn->db_conn);
			}
			list_flush(mysql_conn->update_list);
			return SLURM_ERROR;
		}

		/* If we are doing this on an assoc_table we have
		   already done this, so don't */
/* 		query = xstrdup_printf("select lft, rgt " */
/* 				       "from %s as t2 where %s order by lft;", */
/* 				       assoc_table, assoc_char); */
		query = xstrdup_printf("select distinct t1.id_assoc "
				       "from %s_%s as t1, %s_%s as t2 "
				       "where (%s) && t1.lft between "
				       "t2.lft and t2.rgt && t1.deleted=0 "
				       " && t2.deleted=0;",
				       cluster_name, assoc_table,
				       cluster_name, assoc_table, assoc_char);

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			if(mysql_conn->rollback) {
				mysql_db_rollback(mysql_conn->db_conn);
			}
			list_flush(mysql_conn->update_list);
			return SLURM_ERROR;
		}
		xfree(query);

		rc = 0;
		loc_assoc_char = NULL;
		while((row = mysql_fetch_row(result))) {
			acct_association_rec_t *rem_assoc = NULL;
			if(loc_assoc_char)
				xstrcat(loc_assoc_char, " || ");
			xstrfmtcat(loc_assoc_char, "id_assoc=%s", row[0]);

			rem_assoc = xmalloc(sizeof(acct_association_rec_t));
			rem_assoc->id = atoi(row[0]);
			if(addto_update_list(mysql_conn->update_list,
					      ACCT_REMOVE_ASSOC,
					      rem_assoc) != SLURM_SUCCESS)
				error("couldn't add to the update list");
		}
		mysql_free_result(result);
	} else
		loc_assoc_char = assoc_char;

	if(!loc_assoc_char) {
		debug2("No associations with object being deleted\n");
		return rc;
	}

	/* We should not have to delete from usage table, only flag since we
	 * only delete things that are typos.
	 */
	xstrfmtcat(query,
		   "update %s_%s set mod_time=%d, deleted=1 where (%s);"
		   "update %s_%s set mod_time=%d, deleted=1 where (%s);"
		   "update %s_%s set mod_time=%d, deleted=1 where (%s);",
		   cluster_name, assoc_day_table, now, loc_assoc_char,
		   cluster_name, assoc_hour_table, now, loc_assoc_char,
		   cluster_name, assoc_month_table, now, loc_assoc_char);

	debug3("%d(%s:%d) query\n%s %d",
	       mysql_conn->conn, __FILE__, __LINE__, query, strlen(query));
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		return SLURM_ERROR;
	}

	/* If we have jobs that have ran don't go through the logic of
	 * removing the associations. Since we may want them for
	 * reports in the future since jobs had ran.
	 */
	if(has_jobs)
		goto just_update;

	/* remove completely all the associations for this added in the last
	 * day, since they are most likely nothing we really wanted in
	 * the first place.
	 */
	query = xstrdup_printf("select id_assoc from %s_%s as t1 where "
			       "creation_time>%d && (%s);",
			       cluster_name, assoc_table,
			       day_old, loc_assoc_char);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		return SLURM_ERROR;
	}
	xfree(query);

	while((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		MYSQL_ROW row2;

		/* we have to do this one at a time since the lft's and rgt's
		   change. If you think you need to remove this make
		   sure your new way can handle changing lft and rgt's
		   in the association. */
		xstrfmtcat(query,
			   "SELECT lft, rgt, (rgt - lft + 1) "
			   "FROM %s_%s WHERE id = %s;",
			   cluster_name, assoc_table, row[0]);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		if(!(result2 = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			rc = SLURM_ERROR;
			break;
		}
		xfree(query);
		if(!(row2 = mysql_fetch_row(result2))) {
			mysql_free_result(result2);
			continue;
		}

		xstrfmtcat(query,
			   "delete quick from %s_%s where "
			   "lft between %s AND %s;",
			   cluster_name, assoc_table, row2[0], row2[1]);

		xstrfmtcat(query,
			   "UPDATE %s_%s SET rgt = rgt - %s WHERE rgt > %s;"
			   "UPDATE %s_%s SET lft = lft - %s WHERE lft > %s;",
			   cluster_name, assoc_table, row2[2], row2[1],
			   cluster_name, assoc_table, row2[2], row2[1]);

		mysql_free_result(result2);

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("couldn't remove assoc");
			break;
		}
	}
	mysql_free_result(result);
	if(rc == SLURM_ERROR) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		return rc;
	}

just_update:
	/* now update the associations themselves that are still
	 * around clearing all the limits since if we add them back
	 * we don't want any residue from past associations lingering
	 * around.
	 */
	query = xstrdup_printf("update %s_%s as t1 set mod_time=%d, deleted=1, "
			       "fairshare=1, max_jobs=NULL, "
			       "max_nodes_per_job=NULL, "
			       "max_wall_duration_per_job=NULL, "
			       "max_cpu_mins_per_job=NULL "
			       "where (%s);"
			       "alter table %s_%s AUTO_INCREMENT=0;",
			       cluster_name, assoc_table, now,
			       loc_assoc_char,
			       cluster_name, assoc_table);

	if(table != assoc_table)
		xfree(loc_assoc_char);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
	}

	return rc;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
	int rc = SLURM_SUCCESS;
	MYSQL *db_conn = NULL;
	char *location = NULL;

	/* since this can be loaded from many different places
	   only tell us once. */
	if(!first)
		return SLURM_SUCCESS;

	first = 0;

	if(!slurmdbd_conf) {
		char *cluster_name = NULL;
		if (!(cluster_name = slurm_get_cluster_name()))
			fatal("%s requires ClusterName in slurm.conf",
			      plugin_name);
		xfree(cluster_name);
	}

	mysql_db_info = _mysql_acct_create_db_info();

	location = slurm_get_accounting_storage_loc();
	if(!location)
		mysql_db_name = xstrdup(DEFAULT_ACCOUNTING_DB);
	else {
		int i = 0;
		while(location[i]) {
			if(location[i] == '.' || location[i] == '/') {
				debug("%s doesn't look like a database "
				      "name using %s",
				      location, DEFAULT_ACCOUNTING_DB);
				break;
			}
			i++;
		}
		if(location[i]) {
			mysql_db_name = xstrdup(DEFAULT_ACCOUNTING_DB);
			xfree(location);
		} else
			mysql_db_name = location;
	}

	debug2("mysql_connect() called for db %s", mysql_db_name);

	if(mysql_get_db_connection(&db_conn, mysql_db_name, mysql_db_info)
	   != SLURM_SUCCESS)
		fatal("The database must be up when starting "
		      "the MYSQL plugin.");

	rc = _mysql_acct_check_tables(db_conn);

	mysql_close_db_connection(&db_conn);

	if(rc == SLURM_SUCCESS)
		verbose("%s loaded", plugin_name);
	else
		verbose("%s failed", plugin_name);

	return rc;
}

extern int fini ( void )
{
	destroy_mysql_db_info(mysql_db_info);
	xfree(mysql_db_name);
	xfree(default_qos_str);
	mysql_cleanup();
	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(bool make_agent, int conn_num,
					   bool rollback)
{
	mysql_conn_t *mysql_conn = xmalloc(sizeof(mysql_conn_t));

	if(!mysql_db_info)
		init();

	debug2("acct_storage_p_get_connection: request new connection");

	mysql_conn->rollback = rollback;
	mysql_conn->conn = conn_num;
	mysql_conn->update_list = list_create(destroy_acct_update_object);

	errno = SLURM_SUCCESS;
	mysql_get_db_connection(&mysql_conn->db_conn,
				mysql_db_name, mysql_db_info);

	if(mysql_conn->db_conn) {
		if(rollback)
			mysql_autocommit(mysql_conn->db_conn, 0);
	}

	return (void *)mysql_conn;
}

extern int acct_storage_p_close_connection(mysql_conn_t **mysql_conn)
{
	if(!mysql_conn || !(*mysql_conn))
		return SLURM_SUCCESS;

	acct_storage_p_commit((*mysql_conn), 0);
	mysql_close_db_connection(&(*mysql_conn)->db_conn);
	list_destroy((*mysql_conn)->update_list);
	xfree((*mysql_conn));

	return SLURM_SUCCESS;
}

extern int acct_storage_p_commit(mysql_conn_t *mysql_conn, bool commit)
{
	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug4("got %d commits", list_count(mysql_conn->update_list));

	if(mysql_conn->rollback) {
		if(!commit) {
			if(mysql_db_rollback(mysql_conn->db_conn))
				error("rollback failed");
		} else {
			if(mysql_db_commit(mysql_conn->db_conn))
				error("commit failed");
		}
	}

	if(commit && list_count(mysql_conn->update_list)) {
		int rc;
		char *query = NULL;
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;
		bool get_qos_count = 0;

		xstrfmtcat(query, "select control_host, control_port, "
			   "name, rpc_version "
			   "from %s where deleted=0 && control_port != 0",
			   cluster_table);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			goto skip;
		}
		xfree(query);
		while((row = mysql_fetch_row(result))) {
			rc = send_accounting_update(mysql_conn->update_list,
						    row[2], row[0],
						    atoi(row[1]), atoi(row[3]));
		}
		mysql_free_result(result);
	skip:
		rc = update_assoc_mgr(mysql_conn->update_list);

		if(get_qos_count)
			_set_qos_cnt(mysql_conn->db_conn);
	}

	list_flush(mysql_conn->update_list);

	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(mysql_conn_t *mysql_conn, uint32_t uid,
				    List user_list)
{
	return mysql_add_users(mysql_conn, uid, user_list);
}

extern int acct_storage_p_add_coord(mysql_conn_t *mysql_conn, uint32_t uid,
				    List acct_list, acct_user_cond_t *user_cond)
{
	return mysql_add_coord(mysql_conn, uid, acct_list, user_cond);
}

extern int acct_storage_p_add_accts(mysql_conn_t *mysql_conn, uint32_t uid,
				    List acct_list)
{
	return mysql_add_accts(mysql_conn, uid, acct_list);
}

extern int acct_storage_p_add_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
				       List cluster_list)
{
	return mysql_add_clusters(mysql_conn, uid, cluster_list);
}

extern int acct_storage_p_add_associations(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   List association_list)
{
	return mysql_add_assocs(mysql_conn, uid, association_list);
}

extern int acct_storage_p_add_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				  List qos_list)
{
	return mysql_add_qos(mysql_conn, uid, qos_list);
}

extern int acct_storage_p_add_wckeys(mysql_conn_t *mysql_conn, uint32_t uid,
				     List wckey_list)
{
	return mysql_add_wckeys(mysql_conn, uid, wckey_list);
}

extern int acct_storage_p_add_reservation(mysql_conn_t *mysql_conn,
					  acct_reservation_rec_t *resv)
{
	return mysql_add_resv(mysql_conn, resv);
}

extern List acct_storage_p_modify_users(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_user_cond_t *user_cond,
					acct_user_rec_t *user)
{
	return mysql_modify_users(mysql_conn, uid, user_cond, user);
}

extern List acct_storage_p_modify_accts(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_account_cond_t *acct_cond,
					acct_account_rec_t *acct)
{
	return mysql_modify_accts(mysql_conn, uid, acct_cond, acct);
}

extern List acct_storage_p_modify_clusters(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   acct_cluster_cond_t *cluster_cond,
					   acct_cluster_rec_t *cluster)
{
	return mysql_modify_clusters(mysql_conn, uid, cluster_cond, cluster);
}

extern List acct_storage_p_modify_associations(
	mysql_conn_t *mysql_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond,
	acct_association_rec_t *assoc)
{
	return mysql_modify_assocs(mysql_conn, uid, assoc_cond, assoc);
}

extern List acct_storage_p_modify_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos)
{
	return mysql_modify_qos(mysql_conn, uid, qos_cond, qos);
}

extern List acct_storage_p_modify_wckeys(mysql_conn_t *mysql_conn,
					 uint32_t uid,
					 acct_wckey_cond_t *wckey_cond,
					 acct_wckey_rec_t *wckey)
{
	return mysql_modify_wckeys(mysql_conn, uid, wckey_cond, wckey);
}

extern int acct_storage_p_modify_reservation(mysql_conn_t *mysql_conn,
					     acct_reservation_rec_t *resv)
{
	return mysql_modify_resv(mysql_conn, resv);
}

extern List acct_storage_p_remove_users(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_user_cond_t *user_cond)
{
	return mysql_remove_users(mysql_conn, uid, user_cond);
}

extern List acct_storage_p_remove_coord(mysql_conn_t *mysql_conn, uint32_t uid,
					List acct_list,
					acct_user_cond_t *user_cond)
{
	return mysql_remove_coord(mysql_conn, uid, acct_list, user_cond);
}

extern List acct_storage_p_remove_accts(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_account_cond_t *acct_cond)
{
	return mysql_remove_accts(mysql_conn, uid, acct_cond);
}

extern List acct_storage_p_remove_clusters(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   acct_cluster_cond_t *cluster_cond)
{
	return mysql_remove_clusters(mysql_conn, uid, cluster_cond);
}

extern List acct_storage_p_remove_associations(
	mysql_conn_t *mysql_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond)
{
	return mysql_remove_assocs(mysql_conn, uid, assoc_cond);
}

extern List acct_storage_p_remove_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond)
{
	return mysql_remove_qos(mysql_conn, uid, qos_cond);
}

extern List acct_storage_p_remove_wckeys(mysql_conn_t *mysql_conn,
					 uint32_t uid,
					 acct_wckey_cond_t *wckey_cond)
{
	return mysql_remove_wckeys(mysql_conn, uid, wckey_cond);
}

extern int acct_storage_p_remove_reservation(mysql_conn_t *mysql_conn,
					     acct_reservation_rec_t *resv)
{
	return mysql_remove_resv(mysql_conn, resv);
}

extern List acct_storage_p_get_users(mysql_conn_t *mysql_conn, uid_t uid,
				     acct_user_cond_t *user_cond)
{
	return mysql_get_users(mysql_conn, uid, user_cond);
}

extern List acct_storage_p_get_accts(mysql_conn_t *mysql_conn, uid_t uid,
				     acct_account_cond_t *acct_cond)
{
	return mysql_get_accts(mysql_conn, uid, acct_cond);
}

extern List acct_storage_p_get_clusters(mysql_conn_t *mysql_conn, uid_t uid,
					acct_cluster_cond_t *cluster_cond)
{
	return mysql_get_clusters(mysql_conn, uid, cluster_cond);
}

extern List acct_storage_p_get_associations(mysql_conn_t *mysql_conn,
					    uid_t uid,
					    acct_association_cond_t *assoc_cond)
{
	return mysql_get_assocs(mysql_conn, uid, assoc_cond);
}

extern List acct_storage_p_get_events(mysql_conn_t *mysql_conn, uint32_t uid,
				      acct_event_cond_t *event_cond)
{
	return mysql_get_cluster_events(mysql_conn, uid, event_cond);
}

extern List acct_storage_p_get_problems(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_association_cond_t *assoc_cond)
{
	List ret_list = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	ret_list = list_create(destroy_acct_association_rec);

	if(mysql_acct_no_assocs(mysql_conn, assoc_cond, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

	if(mysql_acct_no_users(mysql_conn, assoc_cond, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

	if(mysql_user_no_assocs_or_no_uid(mysql_conn, assoc_cond, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

end_it:

	return ret_list;
}

extern List acct_storage_p_get_config(void *db_conn)
{
	return NULL;
}

extern List acct_storage_p_get_qos(mysql_conn_t *mysql_conn, uid_t uid,
				   acct_qos_cond_t *qos_cond)
{
	return mysql_get_qos(mysql_conn, uid, qos_cond);
}

extern List acct_storage_p_get_wckeys(mysql_conn_t *mysql_conn, uid_t uid,
				      acct_wckey_cond_t *wckey_cond)
{
	return mysql_get_wckeys(mysql_conn, uid, wckey_cond);
}

extern List acct_storage_p_get_reservations(mysql_conn_t *mysql_conn, uid_t uid,
					    acct_reservation_cond_t *resv_cond)
{
	return mysql_get_resvs(mysql_conn, uid, resv_cond);
}

extern List acct_storage_p_get_txn(mysql_conn_t *mysql_conn, uid_t uid,
				   acct_txn_cond_t *txn_cond)
{
	return mysql_get_txn(mysql_conn, uid, txn_cond);
}

extern int acct_storage_p_get_usage(mysql_conn_t *mysql_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end)
{
	return mysql_get_usage(mysql_conn, uid, in, type, start, end);
}

extern int acct_storage_p_roll_usage(mysql_conn_t *mysql_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data)
{
	return mysql_roll_usage(mysql_conn, sent_start, sent_end, archive_data);
}

extern int clusteracct_storage_p_node_down(mysql_conn_t *mysql_conn,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	return mysql_node_down(mysql_conn, cluster, node_ptr,
			       event_time, reason, reason_uid);
}

extern int clusteracct_storage_p_node_up(mysql_conn_t *mysql_conn,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	return mysql_node_up(mysql_conn, cluster, node_ptr, event_time);
}

/* This is only called when not running from the slurmdbd so we can
 * assumes some things like rpc_version.
 */
extern int clusteracct_storage_p_register_ctld(mysql_conn_t *mysql_conn,
					       char *cluster,
					       uint16_t port)
{
	return mysql_register_ctld(mysql_conn, cluster, port);
}

extern int clusteracct_storage_p_cluster_cpus(mysql_conn_t *mysql_conn,
					       char *cluster,
					       char *cluster_nodes,
					       uint32_t cpus,
					       time_t event_time)
{
	return mysql_cluster_cpus(mysql_conn, cluster,
				  cluster_nodes, cpus, event_time);
}

extern int clusteracct_storage_p_get_usage(
	mysql_conn_t *mysql_conn, uid_t uid,
	acct_cluster_rec_t *cluster_rec, slurmdbd_msg_type_t type,
	time_t start, time_t end)
{
	return mysql_get_usage(mysql_conn, uid, cluster_rec, type, start, end);
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(mysql_conn_t *mysql_conn,
				       char *cluster_name,
				       struct job_record *job_ptr)
{
	return mysql_job_start(mysql_conn, cluster_name, job_ptr);
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(mysql_conn_t *mysql_conn,
					  struct job_record *job_ptr)
{
	return mysql_job_complete(mysql_conn, job_ptr);
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(mysql_conn_t *mysql_conn,
					struct step_record *step_ptr)
{
	return mysql_step_start(mysql_conn, step_ptr);
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(mysql_conn_t *mysql_conn,
					   struct step_record *step_ptr)
{
	return mysql_step_complete(mysql_conn, step_ptr);
}

/*
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(mysql_conn_t *mysql_conn,
				     struct job_record *job_ptr)
{
	return mysql_suspend(mysql_conn, job_ptr);
}

/*
 * get info from the storage
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(mysql_conn_t *mysql_conn,
					    uid_t uid,
					    acct_job_cond_t *job_cond)
{
	List job_list = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS) {
		return NULL;
	}
	job_list = mysql_jobacct_process_get_jobs(mysql_conn, uid, job_cond);

	return job_list;
}

/*
 * expire old info from the storage
 */
extern int jobacct_storage_p_archive(mysql_conn_t *mysql_conn,
				     acct_archive_cond_t *arch_cond)
{
	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return mysql_jobacct_process_archive(mysql_conn, arch_cond);
}

/*
 * load old info into the storage
 */
extern int jobacct_storage_p_archive_load(mysql_conn_t *mysql_conn,
					  acct_archive_rec_t *arch_rec)
{
	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return mysql_jobacct_process_archive_load(mysql_conn, arch_rec);
}

extern int acct_storage_p_update_shares_used(mysql_conn_t *mysql_conn,
					     List shares_used)
{
	/* No plans to have the database hold the used shares */
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(
	mysql_conn_t *mysql_conn, char *cluster, time_t event_time)
{
	return mysql_flush_jobs_on_cluster(mysql_conn, cluster, event_time);
}
