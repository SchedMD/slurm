/*****************************************************************************\
 *  accounting_storage_mysql.c - accounting interface to as_mysql.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2011-2018 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com, da@llnl.gov>
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
 *****************************************************************************
 * Notes on as_mysql configuration
 *	Assumes mysql is installed as user root
 *	Assumes SlurmUser is configured as user slurm
 * # mysql --user=root -p
 * mysql> GRANT ALL ON *.* TO 'slurm'@'localhost' IDENTIFIED BY PASSWORD 'pw';
 * mysql> GRANT SELECT, INSERT ON *.* TO 'slurm'@'localhost';
\*****************************************************************************/

#include "accounting_storage_mysql.h"
#include "as_mysql_acct.h"
#include "as_mysql_tres.h"
#include "as_mysql_archive.h"
#include "as_mysql_assoc.h"
#include "as_mysql_cluster.h"
#include "as_mysql_convert.h"
#include "as_mysql_federation.h"
#include "as_mysql_fix_runaway_jobs.h"
#include "as_mysql_job.h"
#include "as_mysql_jobacct_process.h"
#include "as_mysql_problems.h"
#include "as_mysql_qos.h"
#include "as_mysql_resource.h"
#include "as_mysql_resv.h"
#include "as_mysql_rollup.h"
#include "as_mysql_txn.h"
#include "as_mysql_usage.h"
#include "as_mysql_user.h"
#include "as_mysql_wckey.h"

List as_mysql_cluster_list = NULL;
/* This total list is only used for converting things, so no
   need to keep it upto date even though it lives until the
   end of the life of the slurmdbd.
*/
List as_mysql_total_cluster_list = NULL;
pthread_mutex_t as_mysql_cluster_list_lock = PTHREAD_MUTEX_INITIALIZER;

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "accounting_storage" for Slurm job completion
 * logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "accounting_storage/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Accounting storage MYSQL plugin";
const char plugin_type[] = "accounting_storage/as_mysql";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static mysql_db_info_t *mysql_db_info = NULL;
static char *mysql_db_name = NULL;

#define DELETE_SEC_BACK 86400

char *acct_coord_table = "acct_coord_table";
char *acct_table = "acct_table";
char *tres_table = "tres_table";
char *assoc_day_table = "assoc_usage_day_table";
char *assoc_hour_table = "assoc_usage_hour_table";
char *assoc_month_table = "assoc_usage_month_table";
char *assoc_table = "assoc_table";
char *clus_res_table = "clus_res_table";
char *cluster_day_table = "usage_day_table";
char *cluster_hour_table = "usage_hour_table";
char *cluster_month_table = "usage_month_table";
char *cluster_table = "cluster_table";
char *convert_version_table = "convert_version_table";
char *federation_table = "federation_table";
char *event_table = "event_table";
char *job_table = "job_table";
char *last_ran_table = "last_ran_table";
char *qos_table = "qos_table";
char *resv_table = "resv_table";
char *res_table = "res_table";
char *step_table = "step_table";
char *txn_table = "txn_table";
char *user_table = "user_table";
char *suspend_table = "suspend_table";
char *wckey_day_table = "wckey_usage_day_table";
char *wckey_hour_table = "wckey_usage_hour_table";
char *wckey_month_table = "wckey_usage_month_table";
char *wckey_table = "wckey_table";

char *event_view = "event_view";
char *event_ext_view = "event_ext_view";
char *job_view = "job_view";
char *job_ext_view = "job_ext_view";
char *resv_view = "resv_view";
char *resv_ext_view = "resv_ext_view";
char *step_view = "step_view";
char *step_ext_view = "step_ext_view";

uint64_t debug_flags = 0;
bool backup_dbd = 0;

static char *default_qos_str = NULL;

enum {
	JASSOC_JOB,
	JASSOC_ACCT,
	JASSOC_USER,
	JASSOC_PART,
	JASSOC_COUNT
};

extern int acct_storage_p_close_connection(mysql_conn_t **mysql_conn);

static List _get_cluster_names(mysql_conn_t *mysql_conn, bool with_deleted)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	List ret_list = NULL;

	char *query = xstrdup_printf("select name from %s", cluster_table);

	if (!with_deleted)
		xstrcat(query, " where deleted=0");

	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	ret_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		if (row[0] && row[0][0])
			list_append(ret_list, xstrdup(row[0]));
	}
	mysql_free_result(result);

	return ret_list;
}

static int _set_qos_cnt(mysql_conn_t *mysql_conn)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = xstrdup_printf("select MAX(id) from %s", qos_table);
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(row = mysql_fetch_row(result))) {
		mysql_free_result(result);
		return SLURM_ERROR;
	}

	/* Set the current qos_count on the system for
	   generating bitstr of that length.  Since 0 isn't
	   possible as an id we add 1 to the total to burn 0 and
	   start at the 1 bit.
	*/
	assoc_mgr_lock(&locks);
	g_qos_count = slurm_atoul(row[0]) + 1;
	assoc_mgr_unlock(&locks);
	mysql_free_result(result);

	return SLURM_SUCCESS;
}

static void _process_running_jobs_result(char *cluster_name,
					 MYSQL_RES *result, List ret_list)
{
	MYSQL_ROW row;
	char *object;

	while ((row = mysql_fetch_row(result))) {
		if (!row[JASSOC_USER][0]) {
			/* This should never happen */
			error("How did we get a job running on an association "
			      "that isn't a user association job %s cluster "
			      "'%s' acct '%s'?", row[JASSOC_JOB],
			      cluster_name, row[JASSOC_ACCT]);
			continue;
		}
		object = xstrdup_printf(
			"JobID = %-10s C = %-10s A = %-10s U = %-9s",
			row[JASSOC_JOB], cluster_name, row[JASSOC_ACCT],
			row[JASSOC_USER]);
		if (row[JASSOC_PART][0])
			// see if there is a partition name
			xstrfmtcat(object, " P = %s", row[JASSOC_PART]);
		list_append(ret_list, object);
	}
}

/* this function is here to see if any of what we are trying to remove
 * has jobs that are not completed.  If we have jobs and the object is less
 * than a day old we don't want to delete it, only set the deleted flag.
 */
static bool _check_jobs_before_remove(mysql_conn_t *mysql_conn,
				      char *cluster_name,
				      char *assoc_char,
				      List ret_list,
				      bool *already_flushed)
{
	char *query = NULL, *object = NULL;
	bool rc = 0;
	int i;
	MYSQL_RES *result = NULL;

	/* if this changes you will need to edit the corresponding
	 * enum above in the global settings */
	static char *jassoc_req_inx[] = {
		"t0.id_job",
		"t1.acct",
		"t1.user",
		"t1.partition"
	};
	if (ret_list) {
		xstrcat(object, jassoc_req_inx[0]);
		for(i=1; i<JASSOC_COUNT; i++)
			xstrfmtcat(object, ", %s", jassoc_req_inx[i]);

		query = xstrdup_printf(
			"select distinct %s "
			"from \"%s_%s\" as t0, "
			"\"%s_%s\" as t1, \"%s_%s\" as t2 "
			"where t1.lft between "
			"t2.lft and t2.rgt && (%s) "
			"and t0.id_assoc=t1.id_assoc "
			"and t0.time_end=0 && t0.state<%d;",
			object, cluster_name, job_table,
			cluster_name, assoc_table,
			cluster_name, assoc_table,
			assoc_char, JOB_COMPLETE);
		xfree(object);
	} else {
		query = xstrdup_printf(
			"select t0.id_assoc from \"%s_%s\" as t0, "
			"\"%s_%s\" as t1, \"%s_%s\" as t2 "
			"where t1.lft between "
			"t2.lft and t2.rgt && (%s) "
			"and t0.id_assoc=t1.id_assoc limit 1;",
			cluster_name, job_table,
			cluster_name, assoc_table,
			cluster_name, assoc_table,
			assoc_char);
	}

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if (mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
		if (ret_list && !(*already_flushed)) {
			list_flush(ret_list);
			(*already_flushed) = 1;
			reset_mysql_conn(mysql_conn);
		}
		if (ret_list)
			_process_running_jobs_result(cluster_name, result,
						     ret_list);
	}

	mysql_free_result(result);
	return rc;
}

/* Same as above but for associations instead of other tables */
static bool _check_jobs_before_remove_assoc(mysql_conn_t *mysql_conn,
					    char *cluster_name,
					    char *assoc_char,
					    List ret_list,
					    bool *already_flushed)
{
	char *query = NULL, *object = NULL;
	bool rc = 0;
	int i;
	MYSQL_RES *result = NULL;

	/* if this changes you will need to edit the corresponding
	 * enum above in the global settings */
	static char *jassoc_req_inx[] = {
		"t1.id_job",
		"t2.acct",
		"t2.user",
		"t2.partition"
	};

	if (ret_list) {
		xstrcat(object, jassoc_req_inx[0]);
		for(i=1; i<JASSOC_COUNT; i++)
			xstrfmtcat(object, ", %s", jassoc_req_inx[i]);

		query = xstrdup_printf("select %s "
				       "from \"%s_%s\" as t1, \"%s_%s\" as t2 "
				       "where (%s) and t1.id_assoc=t2.id_assoc "
				       "and t1.time_end=0 && t1.state<%d;",
				       object, cluster_name, job_table,
				       cluster_name, assoc_table,
				       assoc_char, JOB_COMPLETE);
		xfree(object);
	} else {
		query = xstrdup_printf(
			"select t1.id_assoc from \"%s_%s\" as t1, "
			"\"%s_%s\" as t2 where (%s) "
			"and t1.id_assoc=t2.id_assoc limit 1;",
			cluster_name, job_table,
			cluster_name, assoc_table,
			assoc_char);
	}

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);

	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if (mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
		if (ret_list && !(*already_flushed)) {
			list_flush(ret_list);
			(*already_flushed) = 1;
			reset_mysql_conn(mysql_conn);
		}
	}

	if (ret_list)
		_process_running_jobs_result(cluster_name, result, ret_list);

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

	query = xstrdup_printf("select id_assoc from \"%s_%s\" "
			       "where (%s) limit 1;",
			       cluster_name, job_table, where_char);

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);

	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if (mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	mysql_free_result(result);
	return rc;
}

/* static int _add_remove_tres_limit(char *tres_limit_str, char *name, */
/* 				  char **cols, char **vals, char **extra) */
/* { */
/* 	int rc = SLURM_SUCCESS; */
/* 	char *tmp_str = tres_limit_str; */
/* 	uint64_t value; */
/* 	bool first = true; */

/* 	if (!tmp_str || !tmp_str[0]) */
/* 		return SLURM_SUCCESS; */

/* 	while (tmp_str) { */
/* 		if (id == atoi(tmp_str)) { */
/* 			if (!(tmp_str = strchr(tmp_str, '='))) { */
/* 				error("_add_remove_tres_limit: no value found"); */
/* 				rc = SLURM_ERROR; */
/* 				break; */
/* 			} */
/* 			if (first) */
/* 				slurm_atoull(++tmp_str); */
/* 		} */

/* 		if (!(tmp_str = strchr(tmp_str, ','))) */
/* 			break; */
/* 		tmp_str++; */
/* 	} */

/* 	return SLURM_SUCCESS; */
/* } */

/* Any time a new table is added set it up here */
static int _as_mysql_acct_check_tables(mysql_conn_t *mysql_conn)
{
	storage_field_t acct_coord_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "acct", "tinytext not null" },
		{ "user", "tinytext not null" },
		{ NULL, NULL}
	};

	storage_field_t acct_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "description", "text not null" },
		{ "organization", "text not null" },
		{ NULL, NULL}
	};

	storage_field_t tres_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "id", "int not null auto_increment" },
		{ "type", "tinytext not null" },
		{ "name", "tinytext not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t cluster_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "control_host", "tinytext not null default ''" },
		{ "control_port", "int unsigned not null default 0" },
		{ "last_port", "int unsigned not null default 0" },
		{ "rpc_version", "smallint unsigned not null default 0" },
		{ "classification", "smallint unsigned default 0" },
		{ "dimensions", "smallint unsigned default 1" },
		{ "plugin_id_select", "smallint unsigned default 0" },
		{ "flags", "int unsigned default 0" },
		{ "federation", "tinytext not null" },
		{ "features", "text not null default ''" },
		{ "fed_id", "int unsigned default 0 not null" },
		{ "fed_state", "smallint unsigned not null" },
		{ NULL, NULL}
	};

	storage_field_t clus_res_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "cluster", "tinytext not null" },
		{ "res_id", "int not null" },
		{ "percent_allowed", "int unsigned default 0" },
		{ NULL, NULL}
	};

	storage_field_t convert_version_table_fields[] = {
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "version", "int default 0" },
		{ NULL, NULL}
	};

	storage_field_t federation_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "flags", "int unsigned default 0" },
		{ NULL, NULL}
	};

	storage_field_t qos_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "name", "tinytext not null" },
		{ "description", "text" },
		{ "flags", "int unsigned default 0" },
		{ "grace_time", "int unsigned default NULL" },
		{ "max_jobs_pa", "int default NULL" },
		{ "max_jobs_per_user", "int default NULL" },
		{ "max_jobs_accrue_pa", "int default NULL" },
		{ "max_jobs_accrue_pu", "int default NULL" },
		{ "min_prio_thresh", "int default NULL" },
		{ "max_submit_jobs_pa", "int default NULL" },
		{ "max_submit_jobs_per_user", "int default NULL" },
		{ "max_tres_pa", "text not null default ''" },
		{ "max_tres_pj", "text not null default ''" },
		{ "max_tres_pn", "text not null default ''" },
		{ "max_tres_pu", "text not null default ''" },
		{ "max_tres_mins_pj", "text not null default ''" },
		{ "max_tres_run_mins_pa", "text not null default ''" },
		{ "max_tres_run_mins_pu", "text not null default ''" },
		{ "min_tres_pj", "text not null default ''" },
		{ "max_wall_duration_per_job", "int default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_jobs_accrue", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_tres", "text not null default ''" },
		{ "grp_tres_mins", "text not null default ''" },
		{ "grp_tres_run_mins", "text not null default ''" },
		{ "grp_wall", "int default NULL" },
		{ "preempt", "text not null default ''" },
		{ "preempt_mode", "int default 0" },
		{ "priority", "int unsigned default 0" },
		{ "usage_factor", "double default 1.0 not null" },
		{ "usage_thres", "double default NULL" },
		{ NULL, NULL}
	};

	storage_field_t res_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "name", "tinytext not null" },
		{ "description", "text default null" },
		{ "manager", "tinytext not null" },
		{ "server", "tinytext not null" },
		{ "count", "int unsigned default 0" },
		{ "type", "int unsigned default 0"},
		{ "flags", "int unsigned default 0"},
		{ NULL, NULL}
	};

	storage_field_t txn_table_fields[] = {
		{ "id", "int not null auto_increment" },
		{ "timestamp", "bigint unsigned default 0 not null" },
		{ "action", "smallint not null" },
		{ "name", "text not null" },
		{ "actor", "tinytext not null" },
		{ "cluster", "tinytext not null default ''" },
		{ "info", "blob" },
		{ NULL, NULL}
	};

	storage_field_t user_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "admin_level", "smallint default 1 not null" },
		{ NULL, NULL}
	};

	/*
	 * If more limits are added here they need to be added to
	 * get_parent_limits_select in as_mysql_assoc.c
	 */
	char *get_parent_proc =
		"drop procedure if exists get_parent_limits; "
		"create procedure get_parent_limits("
		"my_table text, acct text, cluster text, without_limits int) "
		"begin "
		"set @par_id = NULL; "
		"set @mj = NULL; "
		"set @mja = NULL; "
		"set @mpt = NULL; "
		"set @msj = NULL; "
		"set @mwpj = NULL; "
		"set @mtpj = ''; "
		"set @mtpn = ''; "
		"set @mtmpj = ''; "
		"set @mtrm = ''; "
		"set @def_qos_id = NULL; "
		"set @qos = ''; "
		"set @delta_qos = ''; "
		"set @my_acct = acct; "
		"if without_limits then "
		"set @mj = 0; "
		"set @msj = 0; "
		"set @mwpj = 0; "
		"set @def_qos_id = 0; "
		"set @qos = 1; "
		"end if; "
		"REPEAT "
		"set @s = 'select '; "
		"if @par_id is NULL then set @s = CONCAT("
		"@s, '@par_id := id_assoc, '); "
		"end if; "
		"if @mj is NULL then set @s = CONCAT("
		"@s, '@mj := max_jobs, '); "
		"end if; "
		"if @mja is NULL then set @s = CONCAT("
		"@s, '@mja := max_jobs_accrue, '); "
		"end if; "
		"if @mpt is NULL then set @s = CONCAT("
		"@s, '@mpt := min_prio_thresh, '); "
		"end if; "
		"if @msj is NULL then set @s = CONCAT("
		"@s, '@msj := max_submit_jobs, '); "
		"end if; "
		"if @mwpj is NULL then set @s = CONCAT("
		"@s, '@mwpj := max_wall_pj, '); "
		"end if; "
		"if @def_qos_id is NULL then set @s = CONCAT("
		"@s, '@def_qos_id := def_qos_id, '); "
		"end if; "
		"if @qos = '' then set @s = CONCAT("
		"@s, '@qos := qos, "
		"@delta_qos := REPLACE(CONCAT(delta_qos, @delta_qos), "
		"\\\',,\\\', \\\',\\\'), '); "
		"end if; "
		/* "set @s = CONCAT(@s, @mtpj := REPLACE(CONCAT(@mtpj, max_tres_pj), " */
		/* "\\\',,\\\', \\\',\\\'), '); " */
		/* "@mtmpj := REPLACE(CONCAT(@mtmpj, max_tres_mins_pj), " */
		/* "\\\',,\\\', \\\',\\\'), '); " */
		/* "@mtrm := REPLACE(CONCAT(@mtrm, max_tres_run_mins), " */
		/* "\\\',,\\\', \\\',\\\'), '); " */
		"set @s = concat(@s, "
		"'@mtpj := CONCAT(@mtpj, "
		"if (@mtpj != \\\'\\\' && max_tres_pj != \\\'\\\', "
		"\\\',\\\', \\\'\\\'), max_tres_pj), "
		"@mtpn := CONCAT(@mtpn, "
		"if (@mtpn != \\\'\\\' && max_tres_pn != \\\'\\\', "
		"\\\',\\\', \\\'\\\'), max_tres_pn), "
		"@mtmpj := CONCAT(@mtmpj, "
		"if (@mtmpj != \\\'\\\' && max_tres_mins_pj != \\\'\\\', "
		"\\\',\\\', \\\'\\\'), max_tres_mins_pj), "
		"@mtrm := CONCAT(@mtrm, "
		"if (@mtrm != \\\'\\\' && max_tres_run_mins != \\\'\\\', "
		"\\\',\\\', \\\'\\\'), max_tres_run_mins), "
		"@my_acct_new := parent_acct from \"', "
		"cluster, '_', my_table, '\" where "
		"acct = \\\'', @my_acct, '\\\' && user=\\\'\\\''); "
		"prepare query from @s; "
		"execute query; "
		"deallocate prepare query; "
		"set @my_acct = @my_acct_new; "
		"UNTIL without_limits || @my_acct = '' END REPEAT; "
		"END;";
	/* char *get_parent_proc = */
	/* 	"drop procedure if exists get_parent_limits; " */
	/* 	"create procedure get_parent_limits(" */
	/* 	"my_table text, acct text, cluster text, without_limits int) " */
	/* 	"begin " */
	/* 	"set @par_id = NULL; " */
	/* 	"set @mj = NULL; " */
	/* 	"set @msj = NULL; " */
	/* 	"set @mcpj = NULL; " */
	/* 	"set @mnpj = NULL; " */
	/* 	"set @mwpj = NULL; " */
	/* 	"set @mcmpj = NULL; " */
	/* 	"set @mcrm = NULL; " */
	/* 	"set @def_qos_id = NULL; " */
	/* 	"set @qos = ''; " */
	/* 	"set @delta_qos = ''; " */
	/* 	"set @my_acct = acct; " */
	/* 	"if without_limits then " */
	/* 	"set @mj = 0; " */
	/* 	"set @msj = 0; " */
	/* 	"set @mcpj = 0; " */
	/* 	"set @mnpj = 0; " */
	/* 	"set @mwpj = 0; " */
	/* 	"set @mcmpj = 0; " */
	/* 	"set @mcrm = 0; " */
	/* 	"set @def_qos_id = 0; " */
	/* 	"set @qos = 1; " */
	/* 	"end if; " */
	/* 	"REPEAT " */
	/* 	"set @s = 'select '; " */
	/* 	"if @par_id is NULL then set @s = CONCAT(" */
	/* 	"@s, '@par_id := id_assoc, '); " */
	/* 	"end if; " */
	/* 	"if @mj is NULL then set @s = CONCAT(" */
	/* 	"@s, '@mj := max_jobs, '); " */
	/* 	"end if; " */
	/* 	"if @msj is NULL then set @s = CONCAT(" */
	/* 	"@s, '@msj := max_submit_jobs, '); " */
	/* 	"end if; " */
	/* 	"if @mcpj is NULL then set @s = CONCAT(" */
	/* 	"@s, '@mcpj := max_cpus_pj, ') ;" */
	/* 	"end if; " */
	/* 	"if @mnpj is NULL then set @s = CONCAT(" */
	/* 	"@s, '@mnpj := max_nodes_pj, ') ;" */
	/* 	"end if; " */
	/* 	"if @mwpj is NULL then set @s = CONCAT(" */
	/* 	"@s, '@mwpj := max_wall_pj, '); " */
	/* 	"end if; " */
	/* 	"if @mcmpj is NULL then set @s = CONCAT(" */
	/* 	"@s, '@mcmpj := max_cpu_mins_pj, '); " */
	/* 	"end if; " */
	/* 	"if @mcrm is NULL then set @s = CONCAT(" */
	/* 	"@s, '@mcrm := max_cpu_run_mins, '); " */
	/* 	"end if; " */
	/* 	"if @def_qos_id is NULL then set @s = CONCAT(" */
	/* 	"@s, '@def_qos_id := def_qos_id, '); " */
	/* 	"end if; " */
	/* 	"if @qos = '' then set @s = CONCAT(" */
	/* 	"@s, '@qos := qos, " */
	/* 	"@delta_qos := REPLACE(CONCAT(delta_qos, @delta_qos), " */
	/* 	"\\\',,\\\', \\\',\\\'), '); " */
	/* 	"end if; " */
	/* 	"set @s = concat(@s, '@my_acct_new := parent_acct from \"', " */
	/* 	"cluster, '_', my_table, '\" where " */
	/* 	"acct = \\\'', @my_acct, '\\\' && user=\\\'\\\''); " */
	/* 	"prepare query from @s; " */
	/* 	"execute query; " */
	/* 	"deallocate prepare query; " */
	/* 	"set @my_acct = @my_acct_new; " */
	/* 	"UNTIL (@mj != -1 && @msj != -1 && @mcpj != -1 " */
	/* 	"&& @mnpj != -1 && @mwpj != -1 && @mcmpj != -1 " */
	/* 	"&& @mcrm != -1 && @def_qos_id != -1 && @qos != '') " */
	/* 	"|| @my_acct = '' END REPEAT; " */
	/* 	"END;"; */
	char *get_coord_qos =
		"drop procedure if exists get_coord_qos; "
		"create procedure get_coord_qos(my_table text, acct text, "
		"cluster text, coord text) "
		"begin "
		"set @qos = ''; "
		"set @delta_qos = ''; "
		"set @found_coord = NULL; "
		"set @my_acct = acct; "
		"REPEAT "
		"set @s = 'select @qos := t1.qos, "
		"@delta_qos := REPLACE(CONCAT(t1.delta_qos, @delta_qos), "
		"\\\',,\\\', \\\',\\\'), @my_acct_new := parent_acct, "
		"@found_coord_curr := t2.user '; "
		"set @s = concat(@s, 'from \"', cluster, '_', my_table, '\" "
		"as t1 left outer join acct_coord_table as t2 on "
		"t1.acct=t2.acct where t1.acct = @my_acct && t1.user=\\\'\\\' "
		"&& (t2.user=\\\'', coord, '\\\' || t2.user is null)'); "
		"prepare query from @s; "
		"execute query; "
		"deallocate prepare query; "
		"if @found_coord_curr is not NULL then "
		"set @found_coord = @found_coord_curr; "
		"end if; "
		"if @found_coord is NULL then "
		"set @qos = ''; "
		"set @delta_qos = ''; "
		"end if; "
		"set @my_acct = @my_acct_new; "
		"UNTIL @qos != '' || @my_acct = '' END REPEAT; "
		"select REPLACE(CONCAT(@qos, @delta_qos), ',,', ','); "
		"END;";
	char *query = NULL;
	time_t now = time(NULL);
	char *cluster_name = NULL;
	int rc = SLURM_SUCCESS, rc2;
	ListIterator itr = NULL;

	/* Make the convert version table since we will check that going
	 * forward to see if we need to update or not.
	 */

	if (mysql_db_create_table(mysql_conn, convert_version_table,
				  convert_version_table_fields,
				  ", primary key (version))") == SLURM_ERROR)
		return SLURM_ERROR;

	/* Make the cluster table first since we build other tables
	   built off this one */
	if (mysql_db_create_table(mysql_conn, cluster_table,
				  cluster_table_fields,
				  ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	/* This table needs to be made before conversions also since
	   we add a cluster column.
	*/
	if (mysql_db_create_table(mysql_conn, txn_table, txn_table_fields,
				  ", primary key (id))") == SLURM_ERROR)
		return SLURM_ERROR;

	if (mysql_db_create_table(mysql_conn, tres_table,
				  tres_table_fields,
				  ", primary key (id), "
				  "unique index (type(20), name(20))) "
				  "auto_increment=1001")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	/* This has to run on both backup as well as primary */
	if ((rc = as_mysql_convert_get_bad_tres(mysql_conn)) != SLURM_SUCCESS) {
		error("issue getting bad tres");
		return rc;
	} else if (!backup_dbd) {
		/* We always want CPU to be the first one, so create
		   it now.  We also add MEM here, the others tres
		   are site specific and could vary.  None but CPU
		   matter on order though.  CPU always has to be 1.

		   TRES_OFFSET is needed since there's no way to force
		   the number of first automatic id in MySQL. auto_increment
		   value is lost on mysqld restart. Bug 4553.
		*/
		query = xstrdup_printf(
			"insert into %s (creation_time, id, deleted, type) values "
			"(%ld, %d, 0, 'cpu'), "
			"(%ld, %d, 0, 'mem'), "
			"(%ld, %d, 0, 'energy'), "
			"(%ld, %d, 0, 'node'), "
			"(%ld, %d, 0, 'billing'), "
			"(%ld, %d, 0, 'vmem'), "
			"(%ld, %d, 0, 'pages'), "
			"(%ld, %d, 1, 'dynamic_offset') "
			"on duplicate key update deleted=VALUES(deleted), type=VALUES(type), id=VALUES(id);",
			tres_table,
			now, TRES_CPU,
			now, TRES_MEM,
			now, TRES_ENERGY,
			now, TRES_NODE,
			now, TRES_BILLING,
			now, TRES_VMEM,
			now, TRES_PAGES,
			now, TRES_OFFSET);
		if (debug_flags & DEBUG_FLAG_DB_TRES)
			DB_DEBUG(mysql_conn->conn, "%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			fatal("problem adding static tres");

		/* Now insert TRES that have a name */
		query = xstrdup_printf(
			"insert into %s (creation_time, id, deleted, type, name) values "
			"(%ld, %d, 0, 'fs', 'disk') "
			"on duplicate key update deleted=VALUES(deleted), type=VALUES(type), name=VALUES(name), id=VALUES(id);",
			tres_table,
			now, TRES_FS_DISK);
		if (debug_flags & DEBUG_FLAG_DB_TRES)
			DB_DEBUG(mysql_conn->conn, "%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			fatal("problem adding static tres");
	}

	slurm_mutex_lock(&as_mysql_cluster_list_lock);
	if (!(as_mysql_cluster_list = _get_cluster_names(mysql_conn, 0))) {
		error("issue getting contents of %s", cluster_table);
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
		return SLURM_ERROR;
	}

	/* This total list is only used for converting things, so no
	   need to keep it upto date even though it lives until the
	   end of the life of the slurmdbd.
	*/
	if (!(as_mysql_total_cluster_list =
	      _get_cluster_names(mysql_conn, 1))) {
		error("issue getting total contents of %s", cluster_table);
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
		return SLURM_ERROR;
	}

	if ((rc = as_mysql_convert_tables_pre_create(mysql_conn)) !=
	    SLURM_SUCCESS) {
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
		error("issue converting tables before create");
		return rc;
	} else if (backup_dbd) {
		/*
		 * We do not want to create/check the database if we are the
		 * backup (see Bug 3827). This is only handled on the primary.
		 */

		slurm_mutex_unlock(&as_mysql_cluster_list_lock);

		/* We do want to set the QOS count though. */
		if (rc == SLURM_SUCCESS)
			rc = _set_qos_cnt(mysql_conn);

		return rc;
	}

	/* might as well do all the cluster centric tables inside this
	 * lock.  We need to do this on all the clusters deleted or
	 * other wise just to make sure everything is kept up to
	 * date. */
	itr = list_iterator_create(as_mysql_total_cluster_list);
	while ((cluster_name = list_next(itr))) {
		if ((rc = create_cluster_tables(mysql_conn, cluster_name))
		    != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);
	if (rc != SLURM_SUCCESS) {
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
		return rc;
	}

	rc = as_mysql_convert_tables_post_create(mysql_conn);

	slurm_mutex_unlock(&as_mysql_cluster_list_lock);
	if (rc != SLURM_SUCCESS) {
		error("issue converting tables after create");
		return rc;
	}

	if (mysql_db_create_table(mysql_conn, acct_coord_table,
				  acct_coord_table_fields,
				  ", primary key (acct(20), user(20)), "
				  "key user (user(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	if (mysql_db_create_table(mysql_conn, acct_table, acct_table_fields,
				  ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	if (mysql_db_create_table(mysql_conn, res_table,
				  res_table_fields,
				  ", primary key (id), "
				  "unique index (name(20), server(20), type))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	if (mysql_db_create_table(mysql_conn, clus_res_table,
				  clus_res_table_fields,
				  ", primary key (res_id, cluster(20)), "
				  "unique index (res_id, cluster(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;


	if (mysql_db_create_table(mysql_conn, qos_table,
				  qos_table_fields,
				  ", primary key (id), "
				  "unique index (name(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;
	else {
		int qos_id = 0;
		if (slurmdbd_conf && slurmdbd_conf->default_qos) {
			List char_list = list_create(slurm_destroy_char);
			char *qos = NULL;
			ListIterator itr = NULL;
			slurm_addto_char_list(char_list,
					      slurmdbd_conf->default_qos);
			/* NOTE: you can not use list_pop, or list_push
			   anywhere either, since as_mysql is
			   exporting something of the same type as a macro,
			   which messes everything up
			   (my_list.h is the bad boy).
			*/
			itr = list_iterator_create(char_list);
			while ((qos = list_next(itr))) {
				query = xstrdup_printf(
					"insert into %s "
					"(creation_time, mod_time, name, "
					"description) "
					"values (%ld, %ld, '%s', "
					"'Added as default') "
					"on duplicate key update "
					"id=LAST_INSERT_ID(id), deleted=0;",
					qos_table, now, now, qos);
				if (debug_flags & DEBUG_FLAG_DB_QOS)
					DB_DEBUG(mysql_conn->conn, "%s", query);
				qos_id = (int)mysql_db_insert_ret_id(
					mysql_conn, query);
				if (!qos_id)
					fatal("problem added qos '%s", qos);
				xstrfmtcat(default_qos_str, ",%d", qos_id);
				xfree(query);
			}
			list_iterator_destroy(itr);
			FREE_NULL_LIST(char_list);
		} else {
			query = xstrdup_printf(
				"insert into %s "
				"(creation_time, mod_time, name, description) "
				"values (%ld, %ld, 'normal', "
				"'Normal QOS default') "
				"on duplicate key update "
				"id=LAST_INSERT_ID(id), deleted=0;",
				qos_table, now, now);
			if (debug_flags & DEBUG_FLAG_DB_QOS)
				DB_DEBUG(mysql_conn->conn, "%s", query);
			qos_id = (int)mysql_db_insert_ret_id(mysql_conn, query);
			if (!qos_id)
				fatal("problem added qos 'normal");

			xstrfmtcat(default_qos_str, ",%d", qos_id);
			xfree(query);
		}

		if (_set_qos_cnt(mysql_conn) != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	/* This must be ran after create_cluster_tables() */
	if (mysql_db_create_table(mysql_conn, user_table, user_table_fields,
				  ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	if (mysql_db_create_table(mysql_conn, federation_table,
				  federation_table_fields,
				  ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	rc = as_mysql_convert_non_cluster_tables_post_create(mysql_conn);

	if (rc != SLURM_SUCCESS) {
		error("issue converting non-cluster tables after create");
		return rc;
	}

	rc2 = mysql_db_query(mysql_conn, get_parent_proc);
	if (rc2 != SLURM_SUCCESS)
		rc = rc2;
	rc2 = mysql_db_query(mysql_conn, get_coord_qos);
	if (rc2 != SLURM_SUCCESS)
		rc = rc2;

	/* Add user root to be a user by default and have this default
	 * account be root.  If already there just update
	 * name='root'.  That way if the admins delete it it will
	 * remain deleted. Creation time will be 0 so it will never
	 * really be deleted.
	 */
	query = xstrdup_printf(
		"insert into %s (creation_time, mod_time, name, "
		"admin_level) values (%ld, %ld, 'root', %d) "
		"on duplicate key update name='root';",
		user_table, (long)now, (long)now, SLURMDB_ADMIN_SUPER_USER);
	xstrfmtcat(query,
		   "insert into %s (creation_time, mod_time, name, "
		   "description, organization) values (%ld, %ld, 'root', "
		   "'default root account', 'root') on duplicate key "
		   "update name='root';",
		   acct_table, (long)now, (long)now);

	//DB_DEBUG(mysql_conn->conn, "%s", query);
	mysql_db_query(mysql_conn, query);
	xfree(query);

	return rc;
}

/* This should be added to the beginning of each function to make sure
 * we have a connection to the database before we try to use it.
 */
extern int check_connection(mysql_conn_t *mysql_conn)
{
	if (!mysql_conn) {
		error("We need a connection to run this");
		errno = ESLURM_DB_CONNECTION;
		return ESLURM_DB_CONNECTION;
	} else if (mysql_db_ping(mysql_conn) != 0) {
		/* avoid memory leak and end thread */
		mysql_db_close_db_connection(mysql_conn);
		if (mysql_db_get_db_connection(
			    mysql_conn, mysql_db_name, mysql_db_info)
		    != SLURM_SUCCESS) {
			error("unable to re-connect to as_mysql database");
			errno = ESLURM_DB_CONNECTION;
			return ESLURM_DB_CONNECTION;
		}
	}

	if (mysql_conn->cluster_deleted) {
		errno = ESLURM_CLUSTER_DELETED;
		return ESLURM_CLUSTER_DELETED;
	}

	return SLURM_SUCCESS;
}

/* Let me know if the last statement had rows that were affected.
 * This only gets called by a non-threaded connection, so there is no
 * need to worry about locks.
 */
extern int last_affected_rows(mysql_conn_t *mysql_conn)
{
	int status=0, rows=0;
	MYSQL_RES *result = NULL;

	do {
		result = mysql_store_result(mysql_conn->db_conn);
		if (result)
			mysql_free_result(result);
		else
			if (mysql_field_count(mysql_conn->db_conn) == 0) {
				status = mysql_affected_rows(
					mysql_conn->db_conn);
				if (status > 0)
					rows = status;
			}
		if ((status = mysql_next_result(mysql_conn->db_conn)) > 0)
			if (debug_flags & DEBUG_FLAG_DB_ASSOC)
				DB_DEBUG(mysql_conn->conn,
					 "Could not execute statement\n");
	} while (status == 0);

	return rows;
}

extern void reset_mysql_conn(mysql_conn_t *mysql_conn)
{
	if (mysql_conn->rollback)
		mysql_db_rollback(mysql_conn);
	xfree(mysql_conn->pre_commit_query);
	list_flush(mysql_conn->update_list);
}

extern int create_cluster_assoc_table(
	mysql_conn_t *mysql_conn, char *cluster_name)
{
	storage_field_t assoc_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "is_def", "tinyint default 0 not null" },
		{ "id_assoc", "int unsigned not null auto_increment" },
		{ "user", "tinytext not null default ''" },
		{ "acct", "tinytext not null" },
		{ "partition", "tinytext not null default ''" },
		{ "parent_acct", "tinytext not null default ''" },
		{ "lft", "int not null" },
		{ "rgt", "int not null" },
		{ "shares", "int default 1 not null" },
		{ "max_jobs", "int default NULL" },
		{ "max_jobs_accrue", "int default NULL" },
		{ "min_prio_thresh", "int default NULL" },
		{ "max_submit_jobs", "int default NULL" },
		{ "max_tres_pj", "text not null default ''" },
		{ "max_tres_pn", "text not null default ''" },
		{ "max_tres_mins_pj", "text not null default ''" },
		{ "max_tres_run_mins", "text not null default ''" },
		{ "max_wall_pj", "int default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_jobs_accrue", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_tres", "text not null default ''" },
		{ "grp_tres_mins", "text not null default ''" },
		{ "grp_tres_run_mins", "text not null default ''" },
		{ "grp_wall", "int default NULL" },
		{ "def_qos_id", "int default NULL" },
		{ "qos", "blob not null default ''" },
		{ "delta_qos", "blob not null default ''" },
		{ NULL, NULL}
	};

	char table_name[200];

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_table_fields,
				  ", primary key (id_assoc), "
				  "unique index (user(20), acct(20), "
				  "`partition`(20)), "
				  "key lft (lft), key account (acct(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int create_cluster_tables(mysql_conn_t *mysql_conn, char *cluster_name)
{
	storage_field_t cluster_usage_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "id_tres", "int not null" },
		{ "time_start", "bigint unsigned not null" },
		{ "count", "bigint unsigned default 0 not null" },
		{ "alloc_secs", "bigint unsigned default 0 not null" },
		{ "down_secs", "bigint unsigned default 0 not null" },
		{ "pdown_secs", "bigint unsigned default 0 not null" },
		{ "idle_secs", "bigint unsigned default 0 not null" },
		{ "resv_secs", "bigint unsigned default 0 not null" },
		{ "over_secs", "bigint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t event_table_fields[] = {
		{ "time_start", "bigint unsigned not null" },
		{ "time_end", "bigint unsigned default 0 not null" },
		{ "node_name", "tinytext default '' not null" },
		{ "cluster_nodes", "text not null default ''" },
		{ "reason", "tinytext not null" },
		{ "reason_uid", "int unsigned default 0xfffffffe not null" },
		{ "state", "smallint unsigned default 0 not null" },
		{ "tres", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t id_usage_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "id", "int unsigned not null" },
		{ "id_tres", "int default 1 not null" },
		{ "time_start", "bigint unsigned not null" },
		{ "alloc_secs", "bigint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t job_table_fields[] = {
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
		{ "pack_job_id", "int unsigned not null" },
		{ "pack_job_offset", "int unsigned not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "mcs_label", "tinytext default ''" },
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
		{ "work_dir", "text not null default ''" },
		{ "system_comment", "text" },
		{ "track_steps", "tinyint not null" },
		{ "tres_alloc", "text not null default ''" },
		{ "tres_req", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t last_ran_table_fields[] = {
		{ "hourly_rollup", "bigint unsigned default 0 not null" },
		{ "daily_rollup", "bigint unsigned default 0 not null" },
		{ "monthly_rollup", "bigint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t resv_table_fields[] = {
		{ "id_resv", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "assoclist", "text not null default ''" },
		{ "flags", "smallint unsigned default 0 not null" },
		{ "nodelist", "text not null default ''" },
		{ "node_inx", "text not null default ''" },
		{ "resv_name", "text not null" },
		{ "time_start", "bigint unsigned default 0 not null"},
		{ "time_end", "bigint unsigned default 0 not null" },
		{ "tres", "text not null default ''" },
		{ "unused_wall", "double unsigned default 0.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields[] = {
		{ "job_db_inx", "bigint unsigned not null" },
		{ "deleted", "tinyint default 0 not null" },
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
		{ "time_start", "bigint unsigned default 0 not null" },
		{ "time_end", "bigint unsigned default 0 not null" },
		{ "time_suspended", "bigint unsigned default 0 not null" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "act_cpufreq", "double unsigned default 0.0 not null" },
		{ "consumed_energy", "bigint unsigned default 0 not null" },
		{ "req_cpufreq_min", "int unsigned default 0 not null" },
		{ "req_cpufreq", "int unsigned default 0 not null" }, /* max */
		{ "req_cpufreq_gov", "int unsigned default 0 not null" },
		{ "tres_alloc", "text not null default ''" },
		{ "tres_usage_in_ave", "text not null default ''" },
		{ "tres_usage_in_max", "text not null default ''" },
		{ "tres_usage_in_max_taskid", "text not null default ''" },
		{ "tres_usage_in_max_nodeid", "text not null default ''" },
		{ "tres_usage_in_min", "text not null default ''" },
		{ "tres_usage_in_min_taskid", "text not null default ''" },
		{ "tres_usage_in_min_nodeid", "text not null default ''" },
		{ "tres_usage_in_tot", "text not null default ''" },
		{ "tres_usage_out_ave", "text not null default ''" },
		{ "tres_usage_out_max", "text not null default ''" },
		{ "tres_usage_out_max_taskid", "text not null default ''" },
		{ "tres_usage_out_max_nodeid", "text not null default ''" },
		{ "tres_usage_out_min", "text not null default ''" },
		{ "tres_usage_out_min_taskid", "text not null default ''" },
		{ "tres_usage_out_min_nodeid", "text not null default ''" },
		{ "tres_usage_out_tot", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t suspend_table_fields[] = {
		{ "job_db_inx", "bigint unsigned not null" },
		{ "id_assoc", "int not null" },
		{ "time_start", "bigint unsigned default 0 not null" },
		{ "time_end", "bigint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t wckey_table_fields[] = {
		{ "creation_time", "bigint unsigned not null" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "is_def", "tinyint default 0 not null" },
		{ "id_wckey", "int unsigned not null auto_increment" },
		{ "wckey_name", "tinytext not null default ''" },
		{ "user", "tinytext not null" },
		{ NULL, NULL}
	};

	char table_name[200];

	if (create_cluster_assoc_table(mysql_conn, cluster_name)
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_day_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  id_usage_table_fields,
				  ", primary key (id, id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_hour_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  id_usage_table_fields,
				  ", primary key (id, id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_month_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  id_usage_table_fields,
				  ", primary key (id, id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_day_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields,
				  ", primary key (id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_hour_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields,
				  ", primary key (id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_month_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields,
				  ", primary key (id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, event_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  event_table_fields,
				  ", primary key (node_name(20), time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, job_table);
	/*
	 * sacct_def is the index for query's with state as time_start is used
	 * in these queries. sacct_def2 is for plain sacct queries.
	 */
	if (mysql_db_create_table(mysql_conn, table_name, job_table_fields,
				  ", primary key (job_db_inx), "
				  "unique index (id_job, time_submit), "
				  "key old_tuple (id_job, "
				  "id_assoc, time_submit), "
				  "key rollup (time_eligible, time_end), "
				  "key rollup2 (time_end, time_eligible), "
				  "key nodes_alloc (nodes_alloc), "
				  "key wckey (id_wckey), "
				  "key qos (id_qos), "
				  "key association (id_assoc), "
				  "key array_job (id_array_job), "
				  "key pack_job (pack_job_id), "
				  "key reserv (id_resv), "
				  "key sacct_def (id_user, time_start, "
				  "time_end), "
				  "key sacct_def2 (id_user, time_end, "
				  "time_eligible))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, last_ran_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  last_ran_table_fields,
				  ", primary key (hourly_rollup, "
				  "daily_rollup, monthly_rollup))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, resv_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  resv_table_fields,
				  ", primary key (id_resv, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, step_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  step_table_fields,
				  ", primary key (job_db_inx, id_step))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, suspend_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  suspend_table_fields,
				  ", primary key (job_db_inx, time_start), "
				  "key job_db_inx_times (job_db_inx, "
				  "time_start, time_end))") == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  wckey_table_fields,
				  ", primary key (id_wckey), "
				  " unique index (wckey_name(20), "
				  "user(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_day_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  id_usage_table_fields,
				  ", primary key (id, id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_hour_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  id_usage_table_fields,
				  ", primary key (id, id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_month_table);

	if (mysql_db_create_table(mysql_conn, table_name,
				  id_usage_table_fields,
				  ", primary key (id, id_tres, time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int remove_cluster_tables(mysql_conn_t *mysql_conn, char *cluster_name)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	MYSQL_RES *result = NULL;

	query = xstrdup_printf("select id_assoc from \"%s_%s\" limit 1;",
			       cluster_name, assoc_table);
	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		error("no result given when querying cluster %s", cluster_name);
		return SLURM_ERROR;
	}
	xfree(query);

	if (mysql_num_rows(result)) {
		mysql_free_result(result);
		debug4("we still have associations, can't remove tables");
		return SLURM_SUCCESS;
	}
	mysql_free_result(result);
	xstrfmtcat(mysql_conn->pre_commit_query,
		   "drop table \"%s_%s\", \"%s_%s\", \"%s_%s\", "
		   "\"%s_%s\", \"%s_%s\", \"%s_%s\", \"%s_%s\", "
		   "\"%s_%s\", \"%s_%s\", \"%s_%s\", \"%s_%s\", "
		   "\"%s_%s\", \"%s_%s\", \"%s_%s\", \"%s_%s\", "
		   "\"%s_%s\", \"%s_%s\";",
		   cluster_name, assoc_table,
		   cluster_name, assoc_day_table,
		   cluster_name, assoc_hour_table,
		   cluster_name, assoc_month_table,
		   cluster_name, cluster_day_table,
		   cluster_name, cluster_hour_table,
		   cluster_name, cluster_month_table,
		   cluster_name, event_table,
		   cluster_name, job_table,
		   cluster_name, last_ran_table,
		   cluster_name, resv_table,
		   cluster_name, step_table,
		   cluster_name, suspend_table,
		   cluster_name, wckey_table,
		   cluster_name, wckey_day_table,
		   cluster_name, wckey_hour_table,
		   cluster_name, wckey_month_table);
	/* Since we could possibly add this exact cluster after this
	   we will require a commit before doing anything else.  This
	   flag will give us that.
	*/
	mysql_conn->cluster_deleted = 1;
	return rc;
}

extern int setup_assoc_limits(slurmdb_assoc_rec_t *assoc,
			      char **cols, char **vals,
			      char **extra, qos_level_t qos_level,
			      bool for_add)
{
	uint32_t tres_str_flags = TRES_STR_FLAG_REMOVE |
		TRES_STR_FLAG_SORT_ID | TRES_STR_FLAG_SIMPLE |
		TRES_STR_FLAG_NO_NULL;

	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };
	if (!assoc)
		return SLURM_ERROR;

	if (for_add) {
		/* If we are adding we should make sure we don't get
		   old reside sitting around from a former life.
		*/
		if (assoc->shares_raw == NO_VAL)
			assoc->shares_raw = INFINITE;
		if (assoc->grp_jobs == NO_VAL)
			assoc->grp_jobs = INFINITE;
		if (assoc->grp_jobs_accrue == NO_VAL)
			assoc->grp_jobs_accrue = INFINITE;
		if (assoc->grp_submit_jobs == NO_VAL)
			assoc->grp_submit_jobs = INFINITE;
		if (assoc->grp_wall == NO_VAL)
			assoc->grp_wall = INFINITE;
		if (assoc->max_jobs == NO_VAL)
			assoc->max_jobs = INFINITE;
		if (assoc->max_jobs_accrue == NO_VAL)
			assoc->max_jobs_accrue = INFINITE;
		if (assoc->min_prio_thresh == NO_VAL)
			assoc->min_prio_thresh = INFINITE;
		if (assoc->max_submit_jobs == NO_VAL)
			assoc->max_submit_jobs = INFINITE;
		if (assoc->max_wall_pj == NO_VAL)
			assoc->max_wall_pj = INFINITE;
		if (assoc->def_qos_id == NO_VAL)
			assoc->def_qos_id = INFINITE;
	}

	if (assoc->shares_raw == INFINITE) {
		xstrcat(*cols, ", shares");
		xstrcat(*vals, ", 1");
		xstrcat(*extra, ", shares=1");
		assoc->shares_raw = 1;
	} else if ((assoc->shares_raw != NO_VAL)
		   && (int32_t)assoc->shares_raw >= 0) {
		xstrcat(*cols, ", shares");
		xstrfmtcat(*vals, ", %u", assoc->shares_raw);
		xstrfmtcat(*extra, ", shares=%u", assoc->shares_raw);
	}

	if (assoc->grp_jobs == INFINITE) {
		xstrcat(*cols, ", grp_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_jobs=NULL");
	} else if ((assoc->grp_jobs != NO_VAL)
		   && ((int32_t)assoc->grp_jobs >= 0)) {
		xstrcat(*cols, ", grp_jobs");
		xstrfmtcat(*vals, ", %u", assoc->grp_jobs);
		xstrfmtcat(*extra, ", grp_jobs=%u", assoc->grp_jobs);
	}

	if (assoc->grp_jobs_accrue == INFINITE) {
		xstrcat(*cols, ", grp_jobs_accrue");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_jobs_accrue=NULL");
	} else if ((assoc->grp_jobs_accrue != NO_VAL)
		   && ((int32_t)assoc->grp_jobs_accrue >= 0)) {
		xstrcat(*cols, ", grp_jobs_accrue");
		xstrfmtcat(*vals, ", %u", assoc->grp_jobs_accrue);
		xstrfmtcat(*extra, ", grp_jobs_accrue=%u",
			   assoc->grp_jobs_accrue);
	}

	if (assoc->grp_submit_jobs == INFINITE) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_submit_jobs=NULL");
	} else if ((assoc->grp_submit_jobs != NO_VAL)
		   && ((int32_t)assoc->grp_submit_jobs >= 0)) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrfmtcat(*vals, ", %u", assoc->grp_submit_jobs);
		xstrfmtcat(*extra, ", grp_submit_jobs=%u",
			   assoc->grp_submit_jobs);
	}

	if (assoc->grp_wall == INFINITE) {
		xstrcat(*cols, ", grp_wall");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_wall=NULL");
	} else if ((assoc->grp_wall != NO_VAL)
		   && ((int32_t)assoc->grp_wall >= 0)) {
		xstrcat(*cols, ", grp_wall");
		xstrfmtcat(*vals, ", %u", assoc->grp_wall);
		xstrfmtcat(*extra, ", grp_wall=%u", assoc->grp_wall);
	}

	/* this only gets set on a user's association and is_def
	 * could be NO_VAL only 1 is accepted */
	if ((assoc->is_def == 1)
	    && ((qos_level == QOS_LEVEL_MODIFY)
		|| (assoc->user && assoc->cluster && assoc->acct))) {
		xstrcat(*cols, ", is_def");
		xstrcat(*vals, ", 1");
		xstrcat(*extra, ", is_def=1");
	}

	if (assoc->max_jobs == INFINITE) {
		xstrcat(*cols, ", max_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_jobs=NULL");
	} else if ((assoc->max_jobs != NO_VAL)
		   && ((int32_t)assoc->max_jobs >= 0)) {
		xstrcat(*cols, ", max_jobs");
		xstrfmtcat(*vals, ", %u", assoc->max_jobs);
		xstrfmtcat(*extra, ", max_jobs=%u", assoc->max_jobs);
	}

	if (assoc->max_jobs_accrue == INFINITE) {
		xstrcat(*cols, ", max_jobs_accrue");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_jobs_accrue=NULL");
	} else if ((assoc->max_jobs_accrue != NO_VAL)
		   && ((int32_t)assoc->max_jobs_accrue >= 0)) {
		xstrcat(*cols, ", max_jobs_accrue");
		xstrfmtcat(*vals, ", %u", assoc->max_jobs_accrue);
		xstrfmtcat(*extra, ", max_jobs_accrue=%u",
			   assoc->max_jobs_accrue);
	}

	if (assoc->min_prio_thresh == INFINITE) {
		xstrcat(*cols, ", min_prio_thresh");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", min_prio_thresh=NULL");
	} else if ((assoc->min_prio_thresh != NO_VAL)
		   && ((int32_t)assoc->min_prio_thresh >= 0)) {
		xstrcat(*cols, ", min_prio_thresh");
		xstrfmtcat(*vals, ", %u", assoc->min_prio_thresh);
		xstrfmtcat(*extra, ", min_prio_thresh=%u",
			   assoc->min_prio_thresh);
	}

	if (assoc->max_submit_jobs == INFINITE) {
		xstrcat(*cols, ", max_submit_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_submit_jobs=NULL");
	} else if ((assoc->max_submit_jobs != NO_VAL)
		   && ((int32_t)assoc->max_submit_jobs >= 0)) {
		xstrcat(*cols, ", max_submit_jobs");
		xstrfmtcat(*vals, ", %u", assoc->max_submit_jobs);
		xstrfmtcat(*extra, ", max_submit_jobs=%u",
			   assoc->max_submit_jobs);
	}

	if (assoc->max_wall_pj == INFINITE) {
		xstrcat(*cols, ", max_wall_pj");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_wall_pj=NULL");
	} else if ((assoc->max_wall_pj != NO_VAL)
		   && ((int32_t)assoc->max_wall_pj >= 0)) {
		xstrcat(*cols, ", max_wall_pj");
		xstrfmtcat(*vals, ", %u", assoc->max_wall_pj);
		xstrfmtcat(*extra, ", max_wall_pj=%u", assoc->max_wall_pj);
	}

	if (assoc->def_qos_id == INFINITE) {
		xstrcat(*cols, ", def_qos_id");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", def_qos_id=NULL");
	} else if ((assoc->def_qos_id != NO_VAL)
		   && ((int32_t)assoc->def_qos_id > 0)) {
		assoc_mgr_lock(&locks);
		if (!list_find_first(assoc_mgr_qos_list,
		    slurmdb_find_qos_in_list, &(assoc->def_qos_id))) {
			assoc_mgr_unlock(&locks);
			return ESLURM_INVALID_QOS;
		}
		assoc_mgr_unlock(&locks);
		xstrcat(*cols, ", def_qos_id");
		xstrfmtcat(*vals, ", %u", assoc->def_qos_id);
		xstrfmtcat(*extra, ", def_qos_id=%u", assoc->def_qos_id);
	}

	/* When modifying anything below this comment it happens in
	 * the actual function since we have to wait until we hear
	 * about the parent first.
	 * What we do to make it known something needs to be changed
	 * is we cat "" onto extra which will inform the caller
	 * something needs changing.
	 */

	if (assoc->grp_tres) {
		if (qos_level == QOS_LEVEL_MODIFY) {
			xstrcat(*extra, "");
			goto end_modify;
		}
		xstrcat(*cols, ", grp_tres");
		slurmdb_combine_tres_strings(
			&assoc->grp_tres, NULL, tres_str_flags);
		xstrfmtcat(*vals, ", '%s'", assoc->grp_tres);
		xstrfmtcat(*extra, ", grp_tres='%s'", assoc->grp_tres);
	}

	if (assoc->grp_tres_mins) {
		if (qos_level == QOS_LEVEL_MODIFY) {
			xstrcat(*extra, "");
			goto end_modify;
		}
		xstrcat(*cols, ", grp_tres_mins");
		slurmdb_combine_tres_strings(
			&assoc->grp_tres_mins, NULL, tres_str_flags);
		xstrfmtcat(*vals, ", '%s'", assoc->grp_tres_mins);
		xstrfmtcat(*extra, ", grp_tres_mins='%s'",
			   assoc->grp_tres_mins);
	}

	if (assoc->grp_tres_run_mins) {
		if (qos_level == QOS_LEVEL_MODIFY) {
			xstrcat(*extra, "");
			goto end_modify;
		}
		xstrcat(*cols, ", grp_tres_run_mins");
		slurmdb_combine_tres_strings(
			&assoc->grp_tres_run_mins, NULL, tres_str_flags);
		xstrfmtcat(*vals, ", '%s'", assoc->grp_tres_run_mins);
		xstrfmtcat(*extra, ", grp_tres_run_mins='%s'",
			   assoc->grp_tres_run_mins);
	}

	if (assoc->max_tres_pj) {
		if (qos_level == QOS_LEVEL_MODIFY) {
			xstrcat(*extra, "");
			goto end_modify;
		}
		xstrcat(*cols, ", max_tres_pj");
		slurmdb_combine_tres_strings(
			&assoc->max_tres_pj, NULL, tres_str_flags);
		xstrfmtcat(*vals, ", '%s'", assoc->max_tres_pj);
		xstrfmtcat(*extra, ", max_tres_pj='%s'", assoc->max_tres_pj);
	}

	if (assoc->max_tres_pn) {
		if (qos_level == QOS_LEVEL_MODIFY) {
			xstrcat(*extra, "");
			goto end_modify;
		}
		xstrcat(*cols, ", max_tres_pn");
		slurmdb_combine_tres_strings(
			&assoc->max_tres_pn, NULL, tres_str_flags);
		xstrfmtcat(*vals, ", '%s'", assoc->max_tres_pn);
		xstrfmtcat(*extra, ", max_tres_pn='%s'", assoc->max_tres_pn);
	}

	if (assoc->max_tres_mins_pj) {
		if (qos_level == QOS_LEVEL_MODIFY) {
			xstrcat(*extra, "");
			goto end_modify;
		}
		xstrcat(*cols, ", max_tres_mins_pj");
		slurmdb_combine_tres_strings(
			&assoc->max_tres_mins_pj, NULL, tres_str_flags);
		xstrfmtcat(*vals, ", '%s'", assoc->max_tres_mins_pj);
		xstrfmtcat(*extra, ", max_tres_mins_pj='%s'",
			   assoc->max_tres_mins_pj);
	}

	if (assoc->max_tres_run_mins) {
		if (qos_level == QOS_LEVEL_MODIFY) {
			xstrcat(*extra, "");
			goto end_modify;
		}
		xstrcat(*cols, ", max_tres_run_mins");
		slurmdb_combine_tres_strings(
			&assoc->max_tres_run_mins, NULL, tres_str_flags);
		xstrfmtcat(*vals, ", '%s'", assoc->max_tres_run_mins);
		xstrfmtcat(*extra, ", max_tres_run_mins='%s'",
			   assoc->max_tres_run_mins);
	}

	if (assoc->qos_list && list_count(assoc->qos_list)) {
		char *qos_type = "qos";
		char *qos_val = NULL;
		char *tmp_char = NULL;
		int set = 0;
		ListIterator qos_itr;

		if (qos_level == QOS_LEVEL_MODIFY) {
			xstrcat(*extra, "");
			goto end_modify;
		}

		qos_itr = list_iterator_create(assoc->qos_list);
		while ((tmp_char = list_next(qos_itr))) {
			/* we don't want to include blank names */
			if (!tmp_char[0])
				continue;
			if (!set) {
				if (tmp_char[0] == '+' || tmp_char[0] == '-')
					qos_type = "delta_qos";
				set = 1;
			}
			xstrfmtcat(qos_val, ",%s", tmp_char);
		}

		list_iterator_destroy(qos_itr);
		if (qos_val) {
			xstrfmtcat(*cols, ", %s", qos_type);
			xstrfmtcat(*vals, ", '%s,'", qos_val);
			xstrfmtcat(*extra, ", %s='%s,'", qos_type, qos_val);
			xfree(qos_val);
		}
	} else if ((qos_level == QOS_LEVEL_SET) && default_qos_str) {
		/* Add default qos to the account */
		xstrcat(*cols, ", qos");
		xstrfmtcat(*vals, ", '%s,'", default_qos_str);
		xstrfmtcat(*extra, ", qos='%s,'", default_qos_str);
		if (!assoc->qos_list)
			assoc->qos_list = list_create(slurm_destroy_char);
		slurm_addto_char_list(assoc->qos_list, default_qos_str);
	} else if (qos_level != QOS_LEVEL_MODIFY) {
		/* clear the qos */
		xstrcat(*cols, ", qos, delta_qos");
		xstrcat(*vals, ", '', ''");
		xstrcat(*extra, ", qos='', delta_qos=''");
	}
end_modify:

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
			 char *vals,
			 char *cluster_name)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	char *tmp_cond_char = slurm_add_slash_to_quotes(cond_char);
	char *tmp_vals = NULL;
	bool cluster_centric = true;

	/* figure out which tables we need to append the cluster name to */
	if ((table == cluster_table) || (table == acct_coord_table)
	    || (table == acct_table) || (table == qos_table)
	    || (table == txn_table) || (table == user_table)
	    || (table == res_table) || (table == clus_res_table)
	    || (table == federation_table))
		cluster_centric = false;

	if (vals && vals[1])
		tmp_vals = slurm_add_slash_to_quotes(vals+2);

	if (cluster_centric) {
		xassert(cluster_name);
		xstrfmtcat(query,
			   "update \"%s_%s\" set mod_time=%ld%s "
			   "where deleted=0 && %s;",
			   cluster_name, table, now, vals, cond_char);
		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, cluster, actor, info) "
			   "values (%ld, %d, '%s', '%s', '%s', '%s');",
			   txn_table,
			   now, type, tmp_cond_char, cluster_name,
			   user_name, tmp_vals);
	} else {
		xstrfmtcat(query,
			   "update %s set mod_time=%ld%s "
			   "where deleted=0 && %s;",
			   table, now, vals, cond_char);
		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%ld, %d, '%s', '%s', '%s');",
			   txn_table,
			   now, type, tmp_cond_char, user_name, tmp_vals);
	}
	xfree(tmp_cond_char);
	xfree(tmp_vals);
	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	if (rc != SLURM_SUCCESS) {
		reset_mysql_conn(mysql_conn);
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
			 char *cluster_name,
			 List ret_list,
			 bool *jobs_running)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	char *loc_assoc_char = NULL, *loc_usage_id_char = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	time_t day_old = now - DELETE_SEC_BACK;
	bool has_jobs = false;
	char *tmp_name_char = NULL;
	bool cluster_centric = true;
	uint32_t smallest_lft = 0xFFFFFFFF;

	/* figure out which tables we need to append the cluster name to */
	if ((table == cluster_table) || (table == acct_coord_table)
	    || (table == acct_table) || (table == qos_table)
	    || (table == txn_table) || (table == user_table)
	    || (table == res_table) || (table == clus_res_table)
	    || (table == federation_table))
		cluster_centric = false;

	/* If we have jobs associated with this we do not want to
	 * really delete it for accounting purposes.  This is for
	 * corner cases most of the time this won't matter.
	 */
	if ((table == acct_coord_table) || (table == res_table)
	    || (table == clus_res_table) || (table == federation_table)) {
		/* This doesn't apply for these tables since we are
		 * only looking for association type tables.
		 */
	} else if ((table == qos_table) || (table == wckey_table)) {
		if (cluster_name)
			has_jobs = _check_jobs_before_remove_without_assoctable(
				mysql_conn, cluster_name, assoc_char);
	} else if (table != assoc_table) {
		/* first check to see if we are running jobs now */
		if (_check_jobs_before_remove(
			    mysql_conn, cluster_name, assoc_char,
			    ret_list, jobs_running) || (*jobs_running))
			return SLURM_SUCCESS;

		has_jobs = _check_jobs_before_remove(
			mysql_conn, cluster_name, assoc_char, NULL, NULL);
	} else {
		/* first check to see if we are running jobs now */
		if (_check_jobs_before_remove_assoc(
			    mysql_conn, cluster_name, name_char,
			    ret_list, jobs_running) || (*jobs_running))
			return SLURM_SUCCESS;

		/* now check to see if any jobs were ever run. */
		has_jobs = _check_jobs_before_remove_assoc(
			mysql_conn, cluster_name, name_char,
			NULL, NULL);
	}
	/* we want to remove completely all that is less than a day old */
	if (!has_jobs && table != assoc_table) {
		if (cluster_centric) {
			query = xstrdup_printf("delete from \"%s_%s\" where "
					       "creation_time>%ld && (%s);",
					       cluster_name, table, day_old,
					       name_char);
		} else {
			query = xstrdup_printf("delete from %s where "
					       "creation_time>%ld && (%s);",
					       table, day_old, name_char);
		}
	}

	if (table != assoc_table) {
		if (cluster_centric) {
			xstrfmtcat(query,
				   "update \"%s_%s\" set mod_time=%ld, "
				   "deleted=1 where deleted=0 && (%s);",
				   cluster_name, table, now, name_char);
		} else if (table == federation_table) {
			xstrfmtcat(query,
				   "update %s set "
				   "mod_time=%ld, deleted=1, "
				   "flags=DEFAULT "
				   "where deleted=0 && (%s);",
				   federation_table, now,
				   name_char);
		} else if (table == qos_table) {
			xstrfmtcat(query,
				   "update %s set "
				   "mod_time=%ld, deleted=1, "
				   "grace_time=DEFAULT, "
				   "max_jobs_pa=DEFAULT, "
				   "max_jobs_per_user=DEFAULT, "
				   "max_jobs_accrue_pa=DEFAULT, "
				   "max_jobs_accrue_pu=DEFAULT, "
				   "min_prio_thresh=DEFAULT, "
				   "max_submit_jobs_pa=DEFAULT, "
				   "max_submit_jobs_per_user=DEFAULT, "
				   "max_tres_pa=DEFAULT, "
				   "max_tres_pj=DEFAULT, "
				   "max_tres_pn=DEFAULT, "
				   "max_tres_pu=DEFAULT, "
				   "max_tres_mins_pj=DEFAULT, "
				   "max_tres_run_mins_pa=DEFAULT, "
				   "max_tres_run_mins_pu=DEFAULT, "
				   "min_tres_pj=DEFAULT, "
				   "max_wall_duration_per_job=DEFAULT, "
				   "grp_jobs=DEFAULT, grp_submit_jobs=DEFAULT, "
				   "grp_jobs_accrue=DEFAULT, grp_tres=DEFAULT, "
				   "grp_tres_mins=DEFAULT, "
				   "grp_tres_run_mins=DEFAULT, "
				   "grp_wall=DEFAULT, "
				   "preempt=DEFAULT, "
				   "priority=DEFAULT, "
				   "usage_factor=DEFAULT, "
				   "usage_thres=DEFAULT "
				   "where deleted=0 && (%s);",
				   qos_table, now, name_char);
		} else {
			xstrfmtcat(query,
				   "update %s set mod_time=%ld, deleted=1 "
				   "where deleted=0 && (%s);",
				   table, now, name_char);
		}
	}

	/* If we are removing assocs use the assoc_char since the
	   name_char has lft between statements that can change over
	   time.  The assoc_char has the actual ids of the assocs
	   which never change.
	*/
	if (type == DBD_REMOVE_ASSOCS && assoc_char)
		tmp_name_char = slurm_add_slash_to_quotes(assoc_char);
	else
		tmp_name_char = slurm_add_slash_to_quotes(name_char);

	if (cluster_centric)
		xstrfmtcat(query,
			   "insert into %s (timestamp, action, name, "
			   "actor, cluster) values "
			   "(%ld, %d, '%s', '%s', '%s');",
			   txn_table,
			   now, type, tmp_name_char, user_name, cluster_name);
	else
		xstrfmtcat(query,
			   "insert into %s (timestamp, action, name, actor) "
			   "values (%ld, %d, '%s', '%s');",
			   txn_table,
			   now, type, tmp_name_char, user_name);

	xfree(tmp_name_char);

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS) {
		reset_mysql_conn(mysql_conn);
		return SLURM_ERROR;
	} else if ((table == acct_coord_table)
		   || (table == wckey_table)
		   || (table == clus_res_table)
		   || (table == res_table)
		   || (table == federation_table)
		   || (table == qos_table))
		return SLURM_SUCCESS;

	/* mark deleted=1 or remove completely the accounting tables
	 */
	if (table != assoc_table) {
		if (!assoc_char) {
			error("no assoc_char");
			if (mysql_conn->rollback) {
				mysql_db_rollback(mysql_conn);
			}
			list_flush(mysql_conn->update_list);
			return SLURM_ERROR;
		}

		/* If we are doing this on an assoc_table we have
		   already done this, so don't */
		query = xstrdup_printf("select distinct t1.id_assoc "
				       "from \"%s_%s\" as t1, \"%s_%s\" as t2 "
				       "where (%s) && t1.lft between "
				       "t2.lft and t2.rgt && t1.deleted=0 "
				       "&& t2.deleted=0;",
				       cluster_name, assoc_table,
				       cluster_name, assoc_table, assoc_char);

		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			if (mysql_conn->rollback) {
				mysql_db_rollback(mysql_conn);
			}
			list_flush(mysql_conn->update_list);
			return SLURM_ERROR;
		}
		xfree(query);

		rc = 0;
		xfree(loc_assoc_char);
		while ((row = mysql_fetch_row(result))) {
			slurmdb_assoc_rec_t *rem_assoc = NULL;
			if (loc_assoc_char)
				xstrcat(loc_assoc_char, " || ");
			xstrfmtcat(loc_assoc_char, "id_assoc=%s", row[0]);

			rem_assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
			rem_assoc->id = slurm_atoul(row[0]);
			rem_assoc->cluster = xstrdup(cluster_name);
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_REMOVE_ASSOC,
					      rem_assoc) != SLURM_SUCCESS)
				error("couldn't add to the update list");
		}
		mysql_free_result(result);
	} else
		loc_assoc_char = assoc_char;

	if (!loc_assoc_char) {
		debug2("No associations with object being deleted");
		return rc;
	}

	loc_usage_id_char = xstrdup(loc_assoc_char);
	xstrsubstituteall(loc_usage_id_char, "id_assoc", "id");

	/* We should not have to delete from usage table, only flag since we
	 * only delete things that are typos.
	 */
	xstrfmtcat(query,
		   "update \"%s_%s\" set mod_time=%ld, deleted=1 where (%s);"
		   "update \"%s_%s\" set mod_time=%ld, deleted=1 where (%s);"
		   "update \"%s_%s\" set mod_time=%ld, deleted=1 where (%s);",
		   cluster_name, assoc_day_table, now, loc_usage_id_char,
		   cluster_name, assoc_hour_table, now, loc_usage_id_char,
		   cluster_name, assoc_month_table, now, loc_usage_id_char);
	xfree(loc_usage_id_char);

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s %zu",
			 query, strlen(query));
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS) {
		reset_mysql_conn(mysql_conn);
		return SLURM_ERROR;
	}

	/* If we have jobs that have ran don't go through the logic of
	 * removing the associations. Since we may want them for
	 * reports in the future since jobs had ran.
	 */
	if (has_jobs)
		goto just_update;

	/* remove completely all the associations for this added in the last
	 * day, since they are most likely nothing we really wanted in
	 * the first place.
	 */
	query = xstrdup_printf("select id_assoc from \"%s_%s\" as t1 where "
			       "creation_time>%ld && (%s);",
			       cluster_name, assoc_table,
			       day_old, loc_assoc_char);

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		reset_mysql_conn(mysql_conn);
		return SLURM_ERROR;
	}
	xfree(query);

	while ((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		MYSQL_ROW row2;
		uint32_t lft;

		/* we have to do this one at a time since the lft's and rgt's
		   change. If you think you need to remove this make
		   sure your new way can handle changing lft and rgt's
		   in the association. */
		xstrfmtcat(query,
			   "SELECT lft, rgt, (rgt - lft + 1) "
			   "FROM \"%s_%s\" WHERE id_assoc = %s;",
			   cluster_name, assoc_table, row[0]);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result2 = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			rc = SLURM_ERROR;
			break;
		}
		xfree(query);
		if (!(row2 = mysql_fetch_row(result2))) {
			mysql_free_result(result2);
			continue;
		}

		xstrfmtcat(query,
			   "delete quick from \"%s_%s\" where "
			   "lft between %s AND %s;",
			   cluster_name, assoc_table, row2[0], row2[1]);

		xstrfmtcat(query,
			   "UPDATE \"%s_%s\" SET rgt = rgt - %s WHERE rgt > %s;"
			   "UPDATE \"%s_%s\" SET "
			   "lft = lft - %s WHERE lft > %s;",
			   cluster_name, assoc_table, row2[2], row2[1],
			   cluster_name, assoc_table, row2[2], row2[1]);

		lft = slurm_atoul(row2[0]);
		if (lft < smallest_lft)
			smallest_lft = lft;

		mysql_free_result(result2);

		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("couldn't remove assoc");
			break;
		}
	}
	mysql_free_result(result);
	/* This already happened before, but we need to run it again
	   since the first time we ran it we didn't know if we were
	   going to remove the above associations.
	*/
	if (rc == SLURM_SUCCESS)
		rc = as_mysql_get_modified_lfts(mysql_conn,
						cluster_name, smallest_lft);

	if (rc == SLURM_ERROR) {
		reset_mysql_conn(mysql_conn);
		return rc;
	}

just_update:
	/* now update the associations themselves that are still
	 * around clearing all the limits since if we add them back
	 * we don't want any residue from past associations lingering
	 * around.
	 */
	query = xstrdup_printf("update \"%s_%s\" as t1 set "
			       "mod_time=%ld, deleted=1, def_qos_id=DEFAULT, "
			       "shares=DEFAULT, max_jobs=DEFAULT, "
			       "max_jobs_accrue=DEFAULT, "
			       "min_prio_thresh=DEFAULT, "
			       "max_submit_jobs=DEFAULT, "
			       "max_wall_pj=DEFAULT, "
			       "max_tres_pj=DEFAULT, "
			       "max_tres_pn=DEFAULT, "
			       "max_tres_mins_pj=DEFAULT, "
			       "max_tres_run_mins=DEFAULT, "
			       "grp_jobs=DEFAULT, grp_submit_jobs=DEFAULT, "
			       "grp_jobs_accrue=DEFAULT, grp_wall=DEFAULT, "
			       "grp_tres=DEFAULT, "
			       "grp_tres_mins=DEFAULT, "
			       "grp_tres_run_mins=DEFAULT, "
			       "qos=DEFAULT, delta_qos=DEFAULT "
			       "where (%s);",
			       cluster_name, assoc_table, now,
			       loc_assoc_char);

	if (table != assoc_table)
		xfree(loc_assoc_char);

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS) {
		reset_mysql_conn(mysql_conn);
	}

	return rc;
}

extern void mod_tres_str(char **out, char *mod, char *cur,
			 char *cur_par, char *name, char **vals,
			 uint32_t id, bool assoc)
{
	uint32_t tres_str_flags = TRES_STR_FLAG_REMOVE |
		TRES_STR_FLAG_SORT_ID | TRES_STR_FLAG_SIMPLE |
		TRES_STR_FLAG_NO_NULL;

	xassert(out);
	xassert(name);

	if (!mod)
		return;

	/* We have to add strings in waves or we will not be able to
	 * get removes to work correctly.  We want the string returned
	 * after the first slurmdb_combine_tres_strings to be put in
	 * the database.
	 */
	xfree(*out); /* just to make sure */
	*out = xstrdup(mod);
	slurmdb_combine_tres_strings(out, cur, tres_str_flags);

	if (xstrcmp(*out, cur)) {
		if (vals) {
			/* This logic is here because while the change
			 * we are doing on the limit is the same for
			 * each limit the other limits on the
			 * associations might not be.  What this does
			 * is only change the limit on the association
			 * given the id.  I'm hoping someone in the
			 * future comes up with a better way to do
			 * this since this seems like a hack, but it
			 * does do the job.
			 */
			xstrfmtcat(*vals, ", %s = "
				   "if (%s=%u, '%s', %s)",
				   name, assoc ? "id_assoc" : "id", id,
				   *out, name);
			/* xstrfmtcat(*vals, ", %s='%s%s')", */
			/* 	   name, */
			/* 	   *out[0] ? "," : "", */
			/* 	   *out); */
		}
		if (cur_par)
			slurmdb_combine_tres_strings(
				out, cur_par, tres_str_flags);
	} else
		xfree(*out);
}

static int _get_database_variable(mysql_conn_t *mysql_conn,
				  const char *variable_name, uint64_t *value)
{
	MYSQL_ROW row = NULL;
	MYSQL_RES *result = NULL;
	char *err_check = NULL;
	char *query;

	query = xstrdup_printf("show variables like \'%s\';",
			       variable_name);
	result = mysql_db_query_ret(mysql_conn, query, 0);
	if (!result) {
		error("%s: null result from query `%s`", __func__, query);
		xfree(query);
		return SLURM_ERROR;
	}

	if (mysql_num_rows(result) != 1) {
		error("%s: invalid results from query `%s`", __func__, query);
		xfree(query);
		mysql_free_result(result);
		return SLURM_ERROR;
	}

	xfree(query);

	row = mysql_fetch_row(result);
	*value = (uint64_t) strtoll(row[1], &err_check, 10);

	if (*err_check) {
		error("%s: error parsing string to int `%s`", __func__, row[1]);
		mysql_free_result(result);
		return SLURM_ERROR;
	}
	mysql_free_result(result);

	return SLURM_SUCCESS;
}

/*
 * Check the values of innodb global database variables, and print
 * an error if the values are not at least half the recommendation.
 */
static int _check_database_variables(mysql_conn_t *mysql_conn)
{
	const char buffer_var[] = "innodb_buffer_pool_size";
	const uint64_t buffer_size = 1073741824;
	const char logfile_var[] = "innodb_log_file_size";
	const uint64_t logfile_size = 67108864;
	const char lockwait_var[] = "innodb_lock_wait_timeout";
	const uint64_t lockwait_timeout = 900;

	uint64_t value;
	bool recommended_values = true;
	char *error_msg = xstrdup("Database settings not recommended values:");

	if (_get_database_variable(mysql_conn, buffer_var, &value))
		goto error;
	debug2("%s: %"PRIu64, buffer_var, value);
	if (value < (buffer_size / 2)) {
		recommended_values = false;
		xstrfmtcat(error_msg, " %s", buffer_var);
	}

	if (_get_database_variable(mysql_conn, logfile_var, &value))
		goto error;
	debug2("%s: %"PRIu64, logfile_var, value);
	if (value < (logfile_size / 2)) {
		recommended_values = false;
		xstrfmtcat(error_msg, " %s", logfile_var);
	}

	if (_get_database_variable(mysql_conn, lockwait_var, &value))
		goto error;
	debug2("%s: %"PRIu64, lockwait_var, value);
	if (value < (lockwait_timeout / 2)) {
		recommended_values = false;
		xstrfmtcat(error_msg, " %s", lockwait_var);
	}

	if (!recommended_values) {
		error("%s", error_msg);
	}

	xfree(error_msg);
	return SLURM_SUCCESS;

error:
	xfree(error_msg);
	return SLURM_ERROR;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	int rc = SLURM_SUCCESS;
	mysql_conn_t *mysql_conn = NULL;

	debug_flags = slurm_get_debug_flags();

	if (slurmdbd_conf->dbd_backup) {
		char node_name_short[128];
		char node_name_long[128];
		if (gethostname(node_name_long, sizeof(node_name_long)))
			fatal("getnodename: %m");
		if (gethostname_short(node_name_short, sizeof(node_name_short)))
			fatal("getnodename_short: %m");
		if (!xstrcmp(node_name_short, slurmdbd_conf->dbd_backup) ||
		    !xstrcmp(node_name_long, slurmdbd_conf->dbd_backup) ||
		    !xstrcmp(slurmdbd_conf->dbd_backup, "localhost"))
			backup_dbd = true;
	}

	mysql_db_info = create_mysql_db_info(SLURM_MYSQL_PLUGIN_AS);
	mysql_db_name = acct_get_db_name();

	debug2("mysql_connect() called for db %s", mysql_db_name);
	mysql_conn = create_mysql_conn(0, 1, NULL);
	while (mysql_db_get_db_connection(
		       mysql_conn, mysql_db_name, mysql_db_info)
	       != SLURM_SUCCESS) {
		error("The database must be up when starting "
		      "the MYSQL plugin.  Trying again in 5 seconds.");
		sleep(5);
	}

	_check_database_variables(mysql_conn);

	rc = _as_mysql_acct_check_tables(mysql_conn);

	if (rc == SLURM_SUCCESS) {
		if (mysql_db_commit(mysql_conn)) {
			error("commit failed, meaning %s failed", plugin_name);
			rc = SLURM_ERROR;
		} else
			verbose("%s loaded", plugin_name);
	} else {
		verbose("%s failed", plugin_name);
		if (mysql_db_rollback(mysql_conn))
			error("rollback failed");
		/*
		 * Turns out if you create a table after a change you can not
		 * rollback.  This rolls back the potential changes we need to
		 * deal with again on convert failure.
		 */
		if (bad_tres_list) {
			char *query = NULL;
			slurmdb_tres_rec_t *tres_rec;
			ListIterator itr = list_iterator_create(bad_tres_list);

			while ((tres_rec = list_next(itr))) {
				xstrfmtcat(query,
					   "update %s set id=%u where id=%u;",
					   tres_table, tres_rec->id,
					   tres_rec->rec_count);
			}
			list_iterator_destroy(itr);

			/*
			 * Ignore the return of these 2 mysql functions.  We
			 * have already failed here so if these fail we can't do
			 * much about it.  We will just go with whatever the
			 * orignal rc was that got us here.
			 */
			(void)mysql_db_query(mysql_conn, query);
			xfree(query);
			(void)mysql_db_commit(mysql_conn);
		}
	}

	FREE_NULL_LIST(bad_tres_list);
	destroy_mysql_conn(mysql_conn);

	return rc;
}

extern int fini ( void )
{
	slurm_mutex_lock(&as_mysql_cluster_list_lock);
	FREE_NULL_LIST(as_mysql_cluster_list);
	FREE_NULL_LIST(as_mysql_total_cluster_list);
	slurm_mutex_unlock(&as_mysql_cluster_list_lock);
	slurm_mutex_destroy(&as_mysql_cluster_list_lock);
	destroy_mysql_db_info(mysql_db_info);
	xfree(mysql_db_name);
	xfree(default_qos_str);

	mysql_db_cleanup();
	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(
	const slurm_trigger_callbacks_t *cb,
	int conn_num, uint16_t *persist_conn_flags,
	bool rollback, char *cluster_name)
{
	mysql_conn_t *mysql_conn = NULL;

	debug2("acct_storage_p_get_connection: request new connection %d",
	       rollback);

	if (!(mysql_conn = create_mysql_conn(
		      conn_num, rollback, cluster_name))) {
		fatal("couldn't get a mysql_conn");
		return NULL;	/* Fix CLANG false positive error */
	}

	errno = SLURM_SUCCESS;
	mysql_db_get_db_connection(mysql_conn, mysql_db_name, mysql_db_info);

	if (mysql_conn->db_conn)
		errno = SLURM_SUCCESS;

	return (void *)mysql_conn;
}

extern int acct_storage_p_close_connection(mysql_conn_t **mysql_conn)
{
	int rc;

	if (!mysql_conn || !(*mysql_conn))
		return SLURM_SUCCESS;

	acct_storage_p_commit((*mysql_conn), 0);
	rc = destroy_mysql_conn(*mysql_conn);
	*mysql_conn = NULL;

	return rc;
}

extern int acct_storage_p_commit(mysql_conn_t *mysql_conn, bool commit)
{
	int rc = check_connection(mysql_conn);

	/* always reset this here */
	if (mysql_conn)
		mysql_conn->cluster_deleted = 0;

	if ((rc != SLURM_SUCCESS) && (rc != ESLURM_CLUSTER_DELETED))
		return rc;
	/*
	 * We should never get here since check_connection will return
	 * ESLURM_DB_CONNECTION when !mysql_conn, but Coverity doesn't
	 * understand that. CID 44841.
	 */
	xassert(mysql_conn);

	debug4("got %d commits", list_count(mysql_conn->update_list));

	if (mysql_conn->rollback) {
		if (!commit) {
			if (mysql_db_rollback(mysql_conn))
				error("rollback failed");
		} else {
			int rc = SLURM_SUCCESS;
			/*
			 * Handle anything here we were unable to do
			 * because of rollback issues.
			 */
			if (mysql_conn->pre_commit_query) {
				if (debug_flags & DEBUG_FLAG_DB_ASSOC)
					DB_DEBUG(mysql_conn->conn, "query\n%s",
						 mysql_conn->pre_commit_query);
				rc = mysql_db_query(
					mysql_conn,
					mysql_conn->pre_commit_query);
			}

			if (rc != SLURM_SUCCESS) {
				if (mysql_db_rollback(mysql_conn))
					error("rollback failed");
			} else {
				if (mysql_db_commit(mysql_conn))
					error("commit failed");
			}
		}
	}

	if (commit && list_count(mysql_conn->update_list)) {
		char *query = NULL;
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;
		ListIterator itr = NULL, itr2 = NULL, itr3 = NULL;
		char *rem_cluster = NULL, *cluster_name = NULL;
		slurmdb_update_object_t *object = NULL;

		xstrfmtcat(query, "select control_host, control_port, "
			   "name, rpc_version "
			   "from %s where deleted=0 && control_port != 0",
			   cluster_table);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			goto skip;
		}
		xfree(query);
		while ((row = mysql_fetch_row(result))) {
			(void) slurmdb_send_accounting_update(
				mysql_conn->update_list,
				row[2], row[0],
				slurm_atoul(row[1]),
				slurm_atoul(row[3]));
		}
		mysql_free_result(result);
	skip:
		(void) assoc_mgr_update(mysql_conn->update_list, 0);

		slurm_mutex_lock(&as_mysql_cluster_list_lock);
		itr2 = list_iterator_create(as_mysql_cluster_list);
		itr = list_iterator_create(mysql_conn->update_list);
		while ((object = list_next(itr))) {
			if (!object->objects || !list_count(object->objects))
				continue;
			/* We only care about clusters removed here. */
			switch (object->type) {
			case SLURMDB_REMOVE_CLUSTER:
				itr3 = list_iterator_create(object->objects);
				while ((rem_cluster = list_next(itr3))) {
					while ((cluster_name =
						list_next(itr2))) {
						if (!xstrcmp(cluster_name,
							     rem_cluster)) {
							list_delete_item(itr2);
							break;
						}
					}
					list_iterator_reset(itr2);
				}
				list_iterator_destroy(itr3);
				break;
			default:
				break;
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr2);
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
	}
	xfree(mysql_conn->pre_commit_query);
	list_flush(mysql_conn->update_list);

	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(mysql_conn_t *mysql_conn, uint32_t uid,
				    List user_list)
{
	return as_mysql_add_users(mysql_conn, uid, user_list);
}

extern int acct_storage_p_add_coord(mysql_conn_t *mysql_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond)
{
	return as_mysql_add_coord(mysql_conn, uid, acct_list, user_cond);
}

extern int acct_storage_p_add_accts(mysql_conn_t *mysql_conn, uint32_t uid,
				    List acct_list)
{
	return as_mysql_add_accts(mysql_conn, uid, acct_list);
}

extern int acct_storage_p_add_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
				       List cluster_list)
{
	return as_mysql_add_clusters(mysql_conn, uid, cluster_list);
}

extern int acct_storage_p_add_federations(mysql_conn_t *mysql_conn,
					  uint32_t uid, List federation_list)
{
	return as_mysql_add_federations(mysql_conn, uid, federation_list);
}

extern int acct_storage_p_add_tres(mysql_conn_t *mysql_conn,
				   uint32_t uid, List tres_list_in)
{
	return as_mysql_add_tres(mysql_conn, uid, tres_list_in);
}

extern int acct_storage_p_add_assocs(mysql_conn_t *mysql_conn,
				     uint32_t uid,
				     List assoc_list)
{
	return as_mysql_add_assocs(mysql_conn, uid, assoc_list);
}

extern int acct_storage_p_add_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				  List qos_list)
{
	return as_mysql_add_qos(mysql_conn, uid, qos_list);
}

extern int acct_storage_p_add_res(mysql_conn_t *mysql_conn, uint32_t uid,
				  List res_list)
{
	return as_mysql_add_res(mysql_conn, uid, res_list);
}

extern int acct_storage_p_add_wckeys(mysql_conn_t *mysql_conn, uint32_t uid,
				     List wckey_list)
{
	return as_mysql_add_wckeys(mysql_conn, uid, wckey_list);
}

extern int acct_storage_p_add_reservation(mysql_conn_t *mysql_conn,
					  slurmdb_reservation_rec_t *resv)
{
	return as_mysql_add_resv(mysql_conn, resv);
}

extern List acct_storage_p_modify_users(mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond,
					slurmdb_user_rec_t *user)
{
	return as_mysql_modify_users(mysql_conn, uid, user_cond, user);
}

extern List acct_storage_p_modify_accts(mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond,
					slurmdb_account_rec_t *acct)
{
	return as_mysql_modify_accts(mysql_conn, uid, acct_cond, acct);
}

extern List acct_storage_p_modify_clusters(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond,
					   slurmdb_cluster_rec_t *cluster)
{
	return as_mysql_modify_clusters(mysql_conn, uid, cluster_cond, cluster);
}

extern List acct_storage_p_modify_assocs(
	mysql_conn_t *mysql_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond,
	slurmdb_assoc_rec_t *assoc)
{
	return as_mysql_modify_assocs(mysql_conn, uid, assoc_cond, assoc);
}

extern List acct_storage_p_modify_federations(
				mysql_conn_t *mysql_conn, uint32_t uid,
				slurmdb_federation_cond_t *fed_cond,
				slurmdb_federation_rec_t *fed)
{
	return as_mysql_modify_federations(mysql_conn, uid, fed_cond, fed);
}

extern List acct_storage_p_modify_job(mysql_conn_t *mysql_conn, uint32_t uid,
				      slurmdb_job_modify_cond_t *job_cond,
				      slurmdb_job_rec_t *job)
{
	return as_mysql_modify_job(mysql_conn, uid, job_cond, job);
}

extern List acct_storage_p_modify_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos)
{
	return as_mysql_modify_qos(mysql_conn, uid, qos_cond, qos);
}

extern List acct_storage_p_modify_res(mysql_conn_t *mysql_conn,
				      uint32_t uid,
				      slurmdb_res_cond_t *res_cond,
				      slurmdb_res_rec_t *res)
{
	return as_mysql_modify_res(mysql_conn, uid, res_cond, res);
}

extern List acct_storage_p_modify_wckeys(mysql_conn_t *mysql_conn,
					 uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond,
					 slurmdb_wckey_rec_t *wckey)
{
	return as_mysql_modify_wckeys(mysql_conn, uid, wckey_cond, wckey);
}

extern int acct_storage_p_modify_reservation(mysql_conn_t *mysql_conn,
					     slurmdb_reservation_rec_t *resv)
{
	return as_mysql_modify_resv(mysql_conn, resv);
}

extern List acct_storage_p_remove_users(mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond)
{
	return as_mysql_remove_users(mysql_conn, uid, user_cond);
}

extern List acct_storage_p_remove_coord(mysql_conn_t *mysql_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_cond)
{
	return as_mysql_remove_coord(mysql_conn, uid, acct_list, user_cond);
}

extern List acct_storage_p_remove_accts(mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond)
{
	return as_mysql_remove_accts(mysql_conn, uid, acct_cond);
}

extern List acct_storage_p_remove_clusters(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond)
{
	return as_mysql_remove_clusters(mysql_conn, uid, cluster_cond);
}

extern List acct_storage_p_remove_assocs(
	mysql_conn_t *mysql_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond)
{
	return as_mysql_remove_assocs(mysql_conn, uid, assoc_cond);
}

extern List acct_storage_p_remove_federations(
					mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_federation_cond_t *fed_cond)
{
	return as_mysql_remove_federations(mysql_conn, uid, fed_cond);
}

extern List acct_storage_p_remove_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond)
{
	return as_mysql_remove_qos(mysql_conn, uid, qos_cond);
}

extern List acct_storage_p_remove_res(mysql_conn_t *mysql_conn,
				      uint32_t uid,
				      slurmdb_res_cond_t *res_cond)
{
	return as_mysql_remove_res(mysql_conn, uid, res_cond);
}

extern List acct_storage_p_remove_wckeys(mysql_conn_t *mysql_conn,
					 uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond)
{
	return as_mysql_remove_wckeys(mysql_conn, uid, wckey_cond);
}

extern int acct_storage_p_remove_reservation(mysql_conn_t *mysql_conn,
					     slurmdb_reservation_rec_t *resv)
{
	return as_mysql_remove_resv(mysql_conn, resv);
}

extern List acct_storage_p_get_users(mysql_conn_t *mysql_conn, uid_t uid,
				     slurmdb_user_cond_t *user_cond)
{
	return as_mysql_get_users(mysql_conn, uid, user_cond);
}

extern List acct_storage_p_get_accts(mysql_conn_t *mysql_conn, uid_t uid,
				     slurmdb_account_cond_t *acct_cond)
{
	return as_mysql_get_accts(mysql_conn, uid, acct_cond);
}

extern List acct_storage_p_get_clusters(mysql_conn_t *mysql_conn, uid_t uid,
					slurmdb_cluster_cond_t *cluster_cond)
{
	return as_mysql_get_clusters(mysql_conn, uid, cluster_cond);
}

extern List acct_storage_p_get_federations(mysql_conn_t *mysql_conn, uid_t uid,
					   slurmdb_federation_cond_t *fed_cond)
{
	return as_mysql_get_federations(mysql_conn, uid, fed_cond);
}

extern List acct_storage_p_get_tres(
	mysql_conn_t *mysql_conn, uid_t uid,
	slurmdb_tres_cond_t *tres_cond)
{
	return as_mysql_get_tres(mysql_conn, uid, tres_cond);
}

extern List acct_storage_p_get_assocs(
	mysql_conn_t *mysql_conn, uid_t uid,
	slurmdb_assoc_cond_t *assoc_cond)
{
	return as_mysql_get_assocs(mysql_conn, uid, assoc_cond);
}

extern List acct_storage_p_get_events(mysql_conn_t *mysql_conn, uint32_t uid,
				      slurmdb_event_cond_t *event_cond)
{
	return as_mysql_get_cluster_events(mysql_conn, uid, event_cond);
}

extern List acct_storage_p_get_problems(mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_assoc_cond_t *assoc_cond)
{
	List ret_list = NULL;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		errno = ESLURM_ACCESS_DENIED;
		return NULL;
	}

	ret_list = list_create(slurmdb_destroy_assoc_rec);

	if (as_mysql_acct_no_assocs(mysql_conn, assoc_cond, ret_list)
	    != SLURM_SUCCESS)
		goto end_it;

	if (as_mysql_acct_no_users(mysql_conn, assoc_cond, ret_list)
	    != SLURM_SUCCESS)
		goto end_it;

	if (as_mysql_user_no_assocs_or_no_uid(mysql_conn, assoc_cond, ret_list)
	    != SLURM_SUCCESS)
		goto end_it;

end_it:

	return ret_list;
}

extern List acct_storage_p_get_config(void *db_conn, char *config_name)
{
	return NULL;
}

extern List acct_storage_p_get_qos(mysql_conn_t *mysql_conn, uid_t uid,
				   slurmdb_qos_cond_t *qos_cond)
{
	return as_mysql_get_qos(mysql_conn, uid, qos_cond);
}

extern List acct_storage_p_get_res(mysql_conn_t *mysql_conn, uid_t uid,
				   slurmdb_res_cond_t *res_cond)
{
	return as_mysql_get_res(mysql_conn, uid, res_cond);
}

extern List acct_storage_p_get_wckeys(mysql_conn_t *mysql_conn, uid_t uid,
				      slurmdb_wckey_cond_t *wckey_cond)
{
	return as_mysql_get_wckeys(mysql_conn, uid, wckey_cond);
}

extern List acct_storage_p_get_reservations(
	mysql_conn_t *mysql_conn, uid_t uid,
	slurmdb_reservation_cond_t *resv_cond)
{
	return as_mysql_get_resvs(mysql_conn, uid, resv_cond);
}

extern List acct_storage_p_get_txn(mysql_conn_t *mysql_conn, uid_t uid,
				   slurmdb_txn_cond_t *txn_cond)
{
	return as_mysql_get_txn(mysql_conn, uid, txn_cond);
}

extern int acct_storage_p_get_usage(mysql_conn_t *mysql_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end)
{
	return as_mysql_get_usage(mysql_conn, uid, in, type, start, end);
}

extern int acct_storage_p_roll_usage(mysql_conn_t *mysql_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data,
				     rollup_stats_t *rollup_stats)
{
	return as_mysql_roll_usage(mysql_conn, sent_start, sent_end,
				   archive_data, rollup_stats);
}

extern int acct_storage_p_fix_runaway_jobs(void *db_conn, uint32_t uid,
					List jobs)
{
	return as_mysql_fix_runaway_jobs(db_conn, uid, jobs);
}

extern int clusteracct_storage_p_node_down(mysql_conn_t *mysql_conn,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	return as_mysql_node_down(mysql_conn, node_ptr,
				  event_time, reason, reason_uid);
}

extern int clusteracct_storage_p_node_up(mysql_conn_t *mysql_conn,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	return as_mysql_node_up(mysql_conn, node_ptr, event_time);
}

/* This is only called when not running from the slurmdbd so we can
 * assumes some things like rpc_version.
 */
extern int clusteracct_storage_p_register_ctld(mysql_conn_t *mysql_conn,
					       uint16_t port)
{
	return as_mysql_register_ctld(
		mysql_conn, mysql_conn->cluster_name, port);
}

extern uint16_t clusteracct_storage_p_register_disconn_ctld(
	mysql_conn_t *mysql_conn, char *control_host)
{
	uint16_t control_port = 0;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!mysql_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return control_port;
	} else if (!control_host) {
		error("%s:%d no control host for cluster %s",
		      THIS_FILE, __LINE__, mysql_conn->cluster_name);
		return control_port;
	}

	query = xstrdup_printf("select last_port from %s where name='%s';",
			       cluster_table, mysql_conn->cluster_name);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		error("register_disconn_ctld: no result given for cluster %s",
		      mysql_conn->cluster_name);
		return control_port;
	}
	xfree(query);

	if ((row = mysql_fetch_row(result))) {
		control_port = slurm_atoul(row[0]);
		/* If there is ever a network issue talking to the DBD, and
		   both the DBD and the ctrl stay up when the ctld goes to
		   talk to the DBD again it may not re-register (<=2.2).
		   Since the slurmctld didn't go down we can presume the port
		   is still the same and just use the last information as the
		   information we should use and go along our merry way.
		*/
		query = xstrdup_printf(
			"update %s set control_host='%s', "
			"control_port=%u where name='%s';",
			cluster_table, control_host, control_port,
			mysql_conn->cluster_name);
		if (debug_flags & DEBUG_FLAG_DB_EVENT)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (mysql_db_query(mysql_conn, query) != SLURM_SUCCESS)
			control_port = 0;
		xfree(query);
	}
	mysql_free_result(result);

	return control_port;
}

extern int clusteracct_storage_p_fini_ctld(mysql_conn_t *mysql_conn,
					   slurmdb_cluster_rec_t *cluster_rec)
{
	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!cluster_rec || (!mysql_conn->cluster_name && !cluster_rec->name)) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	if (!cluster_rec->name)
		cluster_rec->name = mysql_conn->cluster_name;

	return as_mysql_fini_ctld(mysql_conn, cluster_rec);
}

extern int clusteracct_storage_p_cluster_tres(mysql_conn_t *mysql_conn,
					      char *cluster_nodes,
					      char *tres_str_in,
					      time_t event_time,
					      uint16_t rpc_version)
{
	return as_mysql_cluster_tres(mysql_conn,
				     cluster_nodes, &tres_str_in,
				     event_time, rpc_version);
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(mysql_conn_t *mysql_conn,
				       struct job_record *job_ptr)
{
	return as_mysql_job_start(mysql_conn, job_ptr);
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(mysql_conn_t *mysql_conn,
					  struct job_record *job_ptr)
{
	return as_mysql_job_complete(mysql_conn, job_ptr);
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(mysql_conn_t *mysql_conn,
					struct step_record *step_ptr)
{
	return as_mysql_step_start(mysql_conn, step_ptr);
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(mysql_conn_t *mysql_conn,
					   struct step_record *step_ptr)
{
	return as_mysql_step_complete(mysql_conn, step_ptr);
}

/*
 * load into the storage a suspension of a job
 */
extern int jobacct_storage_p_suspend(mysql_conn_t *mysql_conn,
				     struct job_record *job_ptr)
{
	return as_mysql_suspend(mysql_conn, 0, job_ptr);
}

/*
 * get info from the storage
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(mysql_conn_t *mysql_conn,
					    uid_t uid,
					    slurmdb_job_cond_t *job_cond)
{
	List job_list = NULL;

	if (check_connection(mysql_conn) != SLURM_SUCCESS) {
		return NULL;
	}
	job_list = as_mysql_jobacct_process_get_jobs(mysql_conn, uid, job_cond);

	return job_list;
}

/*
 * expire old info from the storage
 */
extern int jobacct_storage_p_archive(mysql_conn_t *mysql_conn,
				     slurmdb_archive_cond_t *arch_cond)
{
	int rc;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* Make sure only 1 archive is happening at a time. */
	slurm_mutex_lock(&usage_rollup_lock);
	rc = as_mysql_jobacct_process_archive(mysql_conn, arch_cond);
	slurm_mutex_unlock(&usage_rollup_lock);

	return rc;
}

/*
 * load old info into the storage
 */
extern int jobacct_storage_p_archive_load(mysql_conn_t *mysql_conn,
					  slurmdb_archive_rec_t *arch_rec)
{
	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return as_mysql_jobacct_process_archive_load(mysql_conn, arch_rec);
}

extern int acct_storage_p_update_shares_used(mysql_conn_t *mysql_conn,
					     List shares_used)
{
	/* No plans to have the database hold the used shares */
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(
	mysql_conn_t *mysql_conn, time_t event_time)
{
	return as_mysql_flush_jobs_on_cluster(mysql_conn, event_time);
}

extern int acct_storage_p_reconfig(mysql_conn_t *mysql_conn, bool dbd)
{
	debug_flags = slurm_get_debug_flags();
	return SLURM_SUCCESS;
}

extern int acct_storage_p_reset_lft_rgt(mysql_conn_t *mysql_conn, uid_t uid,
					List cluster_list)
{
	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return as_mysql_reset_lft_rgt(mysql_conn, uid, cluster_list);
}

extern int acct_storage_p_get_stats(void *db_conn, bool dbd)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_clear_stats(void *db_conn, bool dbd)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_get_data(void *db_conn, acct_storage_info_t dinfo,
				   void *data)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_shutdown(void *db_conn, bool dbd)
{
	return SLURM_SUCCESS;
}
