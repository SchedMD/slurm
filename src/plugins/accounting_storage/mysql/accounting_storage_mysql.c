/*****************************************************************************\
 *  accounting_storage_mysql.c - accounting interface to as_mysql.
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
 * Notes on as_mysql configuration
 *	Assumes mysql is installed as user root
 *	Assumes SlurmUser is configured as user slurm
 * # mysql --user=root -p
 * mysql> GRANT ALL ON *.* TO 'slurm'@'localhost' IDENTIFIED BY PASSWORD 'pw';
 * mysql> GRANT SELECT, INSERT ON *.* TO 'slurm'@'localhost';
\*****************************************************************************/

#include "accounting_storage_mysql.h"
#include "as_mysql_acct.h"
#include "as_mysql_archive.h"
#include "as_mysql_assoc.h"
#include "as_mysql_cluster.h"
#include "as_mysql_convert.h"
#include "as_mysql_job.h"
#include "as_mysql_jobacct_process.h"
#include "as_mysql_problems.h"
#include "as_mysql_qos.h"
#include "as_mysql_resv.h"
#include "as_mysql_rollup.h"
#include "as_mysql_txn.h"
#include "as_mysql_usage.h"
#include "as_mysql_user.h"
#include "as_mysql_wckey.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
char *slurmctld_cluster_name  __attribute__((weak_import)) = NULL;
#else
char *slurmctld_cluster_name = NULL;
#endif

List as_mysql_cluster_list = NULL;
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
 * minimum version for their plugins as the job accounting API
 * matures.
 */
const char plugin_name[] = "Accounting storage MYSQL plugin";
const char plugin_type[] = "accounting_storage/as_mysql";
const uint32_t plugin_version = 100;

static mysql_db_info_t *mysql_db_info = NULL;
static char *mysql_db_name = NULL;

#define DELETE_SEC_BACK 86400

char *acct_coord_table = "acct_coord_table";
char *acct_table = "acct_table";
char *assoc_day_table = "assoc_usage_day_table";
char *assoc_hour_table = "assoc_usage_hour_table";
char *assoc_month_table = "assoc_usage_month_table";
char *assoc_table = "assoc_table";
char *cluster_day_table = "usage_day_table";
char *cluster_hour_table = "usage_hour_table";
char *cluster_month_table = "usage_month_table";
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
char *wckey_day_table = "wckey_usage_day_table";
char *wckey_hour_table = "wckey_usage_hour_table";
char *wckey_month_table = "wckey_usage_month_table";
char *wckey_table = "wckey_table";

static char *default_qos_str = NULL;

enum {
	JASSOC_JOB,
	JASSOC_ACCT,
	JASSOC_USER,
	JASSOC_PART,
	JASSOC_COUNT
};

extern int acct_storage_p_close_connection(mysql_conn_t **mysql_conn);

static List _get_cluster_names(mysql_conn_t *mysql_conn)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	List ret_list = NULL;
	char *cluster_name = NULL;
	bool found = 0;

	char *query = xstrdup_printf("select name from %s where deleted=0",
				     cluster_table);

	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return ret_list;
	}
	xfree(query);

	if (!slurmdbd_conf) {
		/* If not running with the slurmdbd we need to make
		   the correct tables for this cluster.  (Since it
		   doesn't have to be added like usual.)
		*/
		cluster_name = slurm_get_cluster_name();
		if (!cluster_name)
			fatal("No cluster name defined in slurm.conf");
	} else
		found = 1;

	ret_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		if (row[0] && row[0][0]) {
			if (cluster_name && !strcmp(cluster_name, row[0]))
				found = 1;
			list_append(ret_list, xstrdup(row[0]));
		}
	}
	mysql_free_result(result);

	if (cluster_name && !found)
		list_append(ret_list, cluster_name);
	else if (cluster_name)
		xfree(cluster_name);

	return ret_list;

}

static int _set_qos_cnt(mysql_conn_t *mysql_conn)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = xstrdup_printf("select MAX(id) from %s", qos_table);

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
	g_qos_count = slurm_atoul(row[0]) + 1;
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
			      "that isn't a user assocation job %s cluster "
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
 * has jobs that are or were once running.  So if we have jobs and the
 * object is less than a day old we don't want to delete it only set
 * the deleted flag.
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
			"and t0.time_end=0 && t0.state=%d;",
			object, cluster_name, job_table,
			cluster_name, assoc_table,
			cluster_name, assoc_table,
			assoc_char, JOB_RUNNING);
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

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
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
	}

	if (ret_list)
		_process_running_jobs_result(cluster_name, result, ret_list);

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
				       "and t1.time_end=0 && t1.state=%d;",
				       object, cluster_name, job_table,
				       cluster_name, assoc_table,
				       assoc_char, JOB_RUNNING);
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
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
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

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
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

/* Any time a new table is added set it up here */
static int _as_mysql_acct_check_tables(mysql_conn_t *mysql_conn)
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

	storage_field_t cluster_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "control_host", "tinytext not null default ''" },
		{ "control_port", "int unsigned not null default 0" },
		{ "rpc_version", "smallint unsigned not null default 0" },
		{ "classification", "smallint unsigned default 0" },
		{ "dimensions", "smallint unsigned default 1" },
		{ "plugin_id_select", "smallint unsigned default 0" },
		{ "flags", "int unsigned default 0" },
		{ NULL, NULL}
	};

	storage_field_t qos_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "name", "tinytext not null" },
		{ "description", "text" },
		{ "flags", "int unsigned default 0" },
		{ "max_jobs_per_user", "int default NULL" },
		{ "max_submit_jobs_per_user", "int default NULL" },
		{ "max_cpus_per_job", "int default NULL" },
		{ "max_nodes_per_job", "int default NULL" },
		{ "max_wall_duration_per_job", "int default NULL" },
		{ "max_cpu_mins_per_job", "bigint default NULL" },
		{ "max_cpu_run_mins_per_user", "bigint default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_cpus", "int default NULL" },
		{ "grp_nodes", "int default NULL" },
		{ "grp_wall", "int default NULL" },
		{ "grp_cpu_mins", "bigint default NULL" },
		{ "grp_cpu_run_mins", "bigint default NULL" },
		{ "preempt", "text not null default ''" },
		{ "preempt_mode", "int default 0" },
		{ "priority", "int default 0" },
		{ "usage_factor", "double default 1.0 not null" },
		{ "usage_thres", "double default NULL" },
		{ NULL, NULL}
	};

	storage_field_t txn_table_fields[] = {
		{ "id", "int not null auto_increment" },
		{ "timestamp", "int unsigned default 0 not null" },
		{ "action", "smallint not null" },
		{ "name", "text not null" },
		{ "actor", "tinytext not null" },
		{ "cluster", "tinytext not null default ''" },
		{ "info", "blob" },
		{ NULL, NULL}
	};

	storage_field_t user_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "admin_level", "smallint default 1 not null" },
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
		"set @mcrm = NULL; "
		"set @def_qos_id = NULL; "
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
		"set @mcrm = 0; "
		"set @def_qos_id = 0; "
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
		"@s, '@mcpj := max_cpus_pj, ') ;"
		"end if; "
		"if @mnpj is NULL then set @s = CONCAT("
		"@s, '@mnpj := max_nodes_pj, ') ;"
		"end if; "
		"if @mwpj is NULL then set @s = CONCAT("
		"@s, '@mwpj := max_wall_pj, '); "
		"end if; "
		"if @mcmpj is NULL then set @s = CONCAT("
		"@s, '@mcmpj := max_cpu_mins_pj, '); "
		"end if; "
		"if @mcrm is NULL then set @s = CONCAT("
		"@s, '@mcrm := max_cpu_run_mins, '); "
		"end if; "
		"if @def_qos_id is NULL then set @s = CONCAT("
		"@s, '@def_qos_id := def_qos_id, '); "
		"end if; "
		"if @qos = '' then set @s = CONCAT("
		"@s, '@qos := qos, "
		"@delta_qos := CONCAT(delta_qos, @delta_qos), '); "
		"end if; "
		"set @s = concat(@s, '@my_acct := parent_acct from \"', "
		"cluster, '_', my_table, '\" where "
		"acct = \\\'', @my_acct, '\\\' && user=\\\'\\\''); "
		"prepare query from @s; "
		"execute query; "
		"deallocate prepare query; "
		"UNTIL (@mj != -1 && @msj != -1 && @mcpj != -1 "
		"&& @mnpj != -1 && @mwpj != -1 && @mcmpj != -1 "
		"&& @mcrm != -1 && @def_qos_id != -1 && @qos != '') "
		"|| @my_acct = '' END REPEAT; "
		"END;";
	char *query = NULL;
	time_t now = time(NULL);
	char *cluster_name = NULL;
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;

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

	slurm_mutex_lock(&as_mysql_cluster_list_lock);
	if (!(as_mysql_cluster_list = _get_cluster_names(mysql_conn))) {
		error("issue getting contents of %s", cluster_table);
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
		return SLURM_ERROR;
	}

	/* might as well do all the cluster centric tables inside this
	 * lock */
	itr = list_iterator_create(as_mysql_cluster_list);
	while ((cluster_name = list_next(itr))) {
		if ((rc = create_cluster_tables(mysql_conn, cluster_name))
		    != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	if (rc != SLURM_SUCCESS)
		return rc;
	/* DEF_TIMERS; */
	/* START_TIMER; */
	if (as_mysql_convert_tables(mysql_conn) != SLURM_SUCCESS)
		return SLURM_ERROR;
	/* END_TIMER; */
	/* info("conversion took %s", TIME_STR); */

	if (mysql_db_create_table(mysql_conn, acct_coord_table,
				  acct_coord_table_fields,
				  ", primary key (acct(20), user(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	if (mysql_db_create_table(mysql_conn, acct_table, acct_table_fields,
				  ", primary key (name(20)))") == SLURM_ERROR)
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
				qos_id = mysql_db_insert_ret_id(
					mysql_conn, query);
				if (!qos_id)
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
				"values (%ld, %ld, 'normal', "
				"'Normal QOS default') "
				"on duplicate key update "
				"id=LAST_INSERT_ID(id), deleted=0;",
				qos_table, now, now);
			//debug3("%s", query);
			qos_id = mysql_db_insert_ret_id(mysql_conn, query);
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

	rc = mysql_db_query(mysql_conn, get_parent_proc);

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

	//debug3("%s", query);
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
		errno = SLURM_ERROR;
		return SLURM_ERROR;
	} else if (mysql_db_ping(mysql_conn) != 0) {
		if (mysql_db_get_db_connection(
			    mysql_conn, mysql_db_name, mysql_db_info)
		    != SLURM_SUCCESS) {
			error("unable to re-connect to as_mysql database");
			errno = ESLURM_DB_CONNECTION;
			return ESLURM_DB_CONNECTION;
		} else {
			int rc;
			if (mysql_conn->rollback)
				mysql_autocommit(mysql_conn->db_conn, 0);
			rc = mysql_db_query(mysql_conn,
					    "SET session "
					    "sql_mode='ANSI_QUOTES';");
			if (rc != SLURM_SUCCESS) {
				error("couldn't set sql_mode on reconnect");
				acct_storage_p_close_connection(&mysql_conn);
				errno = ESLURM_DB_CONNECTION;
				return ESLURM_DB_CONNECTION;
			}
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
			debug3("Could not execute statement\n");
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

extern int create_cluster_tables(mysql_conn_t *mysql_conn, char *cluster_name)
{
	storage_field_t assoc_table_fields[] = {
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
		{ "max_wall_pj", "int default NULL" },
		{ "max_cpu_mins_pj", "bigint default NULL" },
		{ "max_cpu_run_mins", "bigint default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_cpus", "int default NULL" },
		{ "grp_nodes", "int default NULL" },
		{ "grp_wall", "int default NULL" },
		{ "grp_cpu_mins", "bigint default NULL" },
		{ "grp_cpu_run_mins", "bigint default NULL" },
		{ "def_qos_id", "int default NULL" },
		{ "qos", "blob not null default ''" },
		{ "delta_qos", "blob not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t assoc_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "id_assoc", "int not null" },
		{ "time_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t cluster_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "time_start", "int unsigned not null" },
		{ "cpu_count", "int default 0 not null" },
		{ "alloc_cpu_secs", "bigint default 0 not null" },
		{ "down_cpu_secs", "bigint default 0 not null" },
		{ "pdown_cpu_secs", "bigint default 0 not null" },
		{ "idle_cpu_secs", "bigint default 0 not null" },
		{ "resv_cpu_secs", "bigint default 0 not null" },
		{ "over_cpu_secs", "bigint default 0 not null" },
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
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "account", "tinytext" },
		{ "cpus_req", "int unsigned not null" },
		{ "cpus_alloc", "int unsigned not null" },
		{ "derived_ec", "int unsigned default 0 not null" },
		{ "derived_es", "text" },
		{ "exit_code", "int unsigned default 0 not null" },
		{ "job_name", "tinytext not null" },
		{ "id_assoc", "int unsigned not null" },
		{ "id_block", "tinytext" },
		{ "id_job", "int unsigned not null" },
		{ "id_qos", "int unsigned default 0 not null" },
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

	storage_field_t resv_table_fields[] = {
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
		{ NULL, NULL}
	};

	storage_field_t step_table_fields[] = {
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

	storage_field_t wckey_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "is_def", "tinyint default 0 not null" },
		{ "id_wckey", "int not null auto_increment" },
		{ "wckey_name", "tinytext not null default ''" },
		{ "user", "tinytext not null" },
		{ NULL, NULL}
	};

	storage_field_t wckey_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "id_wckey", "int not null" },
		{ "time_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	char table_name[200];
	char *query = NULL;
	bool def_exist = 0, user_table_exists = 0;
	MYSQL_RES *result = NULL;

	query = xstrdup_printf("show tables like '%s';", user_table);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	user_table_exists = mysql_num_rows(result);
	mysql_free_result(result);
	result = NULL;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_table);

	/* See if the tables exist (if not new cluster, so no altering
	   has to take place.)  table_name can't be used here since it
	   has the "'s in it which don't work in this query.
	*/
	query = xstrdup_printf("show tables like '%s_%s';",
			       cluster_name, assoc_table);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	/* Here if the tables do exist then set def_exist to 0 so we
	   check for the fields afterwards.
	*/
	def_exist = mysql_num_rows(result) ? 0 : 1;
	mysql_free_result(result);
	result = NULL;
	if (!def_exist) {
		/* need to see if this table already has defaults or not */
		query = xstrdup_printf(
			"show columns from %s where Field='is_def';",
			table_name);
		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		def_exist = mysql_num_rows(result);
		mysql_free_result(result);
		result = NULL;
	}

	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_table_fields,
				  ", primary key (id_assoc), "
				  " unique index (user(20), acct(20), "
				  "partition(20)))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_day_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_usage_table_fields,
				  ", primary key (id_assoc, "
				  "time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_hour_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_usage_table_fields,
				  ", primary key (id_assoc, "
				  "time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, assoc_month_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  assoc_usage_table_fields,
				  ", primary key (id_assoc, "
				  "time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_day_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields,
				  ", primary key (time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_hour_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields,
				  ", primary key (time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, cluster_month_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  cluster_usage_table_fields,
				  ", primary key (time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, event_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  event_table_fields,
				  ", primary key (node_name(20), "
				  "time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, job_table);
	if (mysql_db_create_table(mysql_conn, table_name, job_table_fields,
				  ", primary key (job_db_inx), "
				  "unique index (id_job, "
				  "id_assoc, time_submit))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, last_ran_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  last_ran_table_fields,
				  ")")
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
				  ")") == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_table);
	if (!def_exist) {
		/* need to see if this table already has defaults or not */
		query = xstrdup_printf(
			"show columns from %s where Field='is_def';",
			table_name);
		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		def_exist = mysql_num_rows(result);
		mysql_free_result(result);
		result = NULL;
	}

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
				  wckey_usage_table_fields,
				  ", primary key (id_wckey, "
				  "time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_hour_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  wckey_usage_table_fields,
				  ", primary key (id_wckey, "
				  "time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
		 cluster_name, wckey_month_table);
	if (mysql_db_create_table(mysql_conn, table_name,
				  wckey_usage_table_fields,
				  ", primary key (id_wckey, "
				  "time_start))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	if (!def_exist && user_table_exists)
		/* now set the default for each user since the tables
		 * exist, but the defaults don't. */
		if (as_mysql_convert_user_defs(mysql_conn, cluster_name)
		    != SLURM_SUCCESS)
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
		/* If there were any associations removed fix it up
		   here since the table isn't going to be deleted. */
		xstrfmtcat(mysql_conn->pre_commit_query,
			   "alter table \"%s_%s\" AUTO_INCREMENT=0;",
			   cluster_name, assoc_table);

		debug4("we still have associations, can't remove tables");
		return SLURM_SUCCESS;
	}
	mysql_free_result(result);
	xstrfmtcat(mysql_conn->pre_commit_query,
		   "drop table \"%s_%s\", \"%s_%s\", "
		   "\"%s_%s\", \"%s_%s\", \"%s_%s\", "
		   "\"%s_%s\", \"%s_%s\", \"%s_%s\", \"%s_%s\", "
		   "\"%s_%s\", \"%s_%s\", \"%s_%s\", \"%s_%s\", "
		   "\"%s_%s\", \"%s_%s\", \"%s_%s\", \"%s_%s\";",
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

extern int setup_association_limits(slurmdb_association_rec_t *assoc,
				    char **cols, char **vals,
				    char **extra, qos_level_t qos_level,
				    bool get_fs)
{
	if (!assoc)
		return SLURM_ERROR;

	if ((int32_t)assoc->shares_raw >= 0) {
		xstrcat(*cols, ", shares");
		xstrfmtcat(*vals, ", %u", assoc->shares_raw);
		xstrfmtcat(*extra, ", shares=%u", assoc->shares_raw);
	} else if ((assoc->shares_raw == INFINITE) || get_fs) {
		xstrcat(*cols, ", shares");
		xstrcat(*vals, ", 1");
		xstrcat(*extra, ", shares=1");
		assoc->shares_raw = 1;
	}

	if (assoc->grp_cpu_mins == (uint64_t)INFINITE) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpu_mins=NULL");
	} else if ((assoc->grp_cpu_mins != (uint64_t)NO_VAL)
		   && ((int64_t)assoc->grp_cpu_mins >= 0)) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrfmtcat(*vals, ", %"PRIu64"",
			   assoc->grp_cpu_mins);
		xstrfmtcat(*extra, ", grp_cpu_mins=%"PRIu64"",
			   assoc->grp_cpu_mins);
	}

	if (assoc->grp_cpus == INFINITE) {
		xstrcat(*cols, ", grp_cpus");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpus=NULL");
	} else if ((assoc->grp_cpus != NO_VAL)
		   && ((int32_t)assoc->grp_cpus >= 0)) {
		xstrcat(*cols, ", grp_cpus");
		xstrfmtcat(*vals, ", %u", assoc->grp_cpus);
		xstrfmtcat(*extra, ", grp_cpus=%u", assoc->grp_cpus);
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

	if (assoc->grp_nodes == INFINITE) {
		xstrcat(*cols, ", grp_nodes");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_nodes=NULL");
	} else if ((assoc->grp_nodes != NO_VAL)
		   && ((int32_t)assoc->grp_nodes >= 0)) {
		xstrcat(*cols, ", grp_nodes");
		xstrfmtcat(*vals, ", %u", assoc->grp_nodes);
		xstrfmtcat(*extra, ", grp_nodes=%u", assoc->grp_nodes);
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

	if (assoc->max_cpu_mins_pj == (uint64_t)INFINITE) {
		xstrcat(*cols, ", max_cpu_mins_pj");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpu_mins_pj=NULL");
	} else if ((assoc->max_cpu_mins_pj != (uint64_t)NO_VAL)
		   && ((int64_t)assoc->max_cpu_mins_pj >= 0)) {
		xstrcat(*cols, ", max_cpu_mins_pj");
		xstrfmtcat(*vals, ", %"PRIu64"",
			   assoc->max_cpu_mins_pj);
		xstrfmtcat(*extra, ", max_cpu_mins_pj=%"PRIu64"",
			   assoc->max_cpu_mins_pj);
	}

	if (assoc->max_cpus_pj == INFINITE) {
		xstrcat(*cols, ", max_cpus_pj");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpus_pj=NULL");
	} else if ((assoc->max_cpus_pj != NO_VAL)
		   && ((int32_t)assoc->max_cpus_pj >= 0)) {
		xstrcat(*cols, ", max_cpus_pj");
		xstrfmtcat(*vals, ", %u", assoc->max_cpus_pj);
		xstrfmtcat(*extra, ", max_cpus_pj=%u", assoc->max_cpus_pj);
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

	if (assoc->max_nodes_pj == INFINITE) {
		xstrcat(*cols, ", max_nodes_pj");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_nodes_pj=NULL");
	} else if ((assoc->max_nodes_pj != NO_VAL)
		   && ((int32_t)assoc->max_nodes_pj >= 0)) {
		xstrcat(*cols, ", max_nodes_pj");
		xstrfmtcat(*vals, ", %u", assoc->max_nodes_pj);
		xstrfmtcat(*extra, ", max_nodes_pj=%u", assoc->max_nodes_pj);
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
		/* 0 is the no def_qos_id, so it that way */
		assoc->def_qos_id = 0;
	} else if ((assoc->def_qos_id != NO_VAL)
		   && ((int32_t)assoc->def_qos_id > 0)) {
		xstrcat(*cols, ", def_qos_id");
		xstrfmtcat(*vals, ", %u", assoc->def_qos_id);
		xstrfmtcat(*extra, ", def_qos_id=%u", assoc->def_qos_id);
	}

	/* when modifying the qos it happens in the actual function
	   since we have to wait until we hear about the parent first. */
	if (qos_level == QOS_LEVEL_MODIFY)
		goto end_qos;

	if (assoc->qos_list && list_count(assoc->qos_list)) {
		char *qos_type = "qos";
		char *qos_val = NULL;
		char *tmp_char = NULL;
		int set = 0;
		ListIterator qos_itr =
			list_iterator_create(assoc->qos_list);

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
			xstrfmtcat(*vals, ", '%s'", qos_val);
			xstrfmtcat(*extra, ", %s='%s'", qos_type, qos_val);
			xfree(qos_val);
		}
	} else if ((qos_level == QOS_LEVEL_SET) && default_qos_str) {
		/* Add default qos to the account */
		xstrcat(*cols, ", qos");
		xstrfmtcat(*vals, ", '%s'", default_qos_str);
		xstrfmtcat(*extra, ", qos='%s'", default_qos_str);
		if (!assoc->qos_list)
			assoc->qos_list = list_create(slurm_destroy_char);
		slurm_addto_char_list(assoc->qos_list, default_qos_str);
	} else {
		/* clear the qos */
		xstrcat(*cols, ", qos, delta_qos");
		xstrcat(*vals, ", '', ''");
		xstrcat(*extra, ", qos='', delta_qos=''");
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
	    || (table == txn_table) || (table == user_table))
		cluster_centric = false;

	if (vals[1])
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
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
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
	char *loc_assoc_char = NULL;
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
	    || (table == txn_table) || (table == user_table))
		cluster_centric = false;

	/* If we have jobs associated with this we do not want to
	 * really delete it for accounting purposes.  This is for
	 * corner cases most of the time this won't matter.
	 */
	if (table == acct_coord_table) {
		/* This doesn't apply for these tables since we are
		 * only looking for association type tables.
		 */
	} else if ((table == qos_table) || (table == wckey_table)) {
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
			/* Make sure the next id we get doesn't create holes
			 * in the ids. */
			xstrfmtcat(mysql_conn->pre_commit_query,
				   "alter table \"%s_%s\" AUTO_INCREMENT=0;",
				   cluster_name, table);
		} else {
			query = xstrdup_printf("delete from %s where "
					       "creation_time>%ld && (%s);",
					       table, day_old, name_char);
			/* Make sure the next id we get doesn't create holes
			 * in the ids. */
			xstrfmtcat(mysql_conn->pre_commit_query,
				   "alter table %s AUTO_INCREMENT=0;",
				   table);
		}
	}

	if (table != assoc_table) {
		if (cluster_centric)
			xstrfmtcat(query,
				   "update \"%s_%s\" set mod_time=%ld, "
				   "deleted=1 where deleted=0 && (%s);",
				   cluster_name, table, now, name_char);
		else
			xstrfmtcat(query,
				   "update %s set mod_time=%ld, deleted=1 "
				   "where deleted=0 && (%s);",
				   table, now, name_char);
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

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS) {
		reset_mysql_conn(mysql_conn);
		return SLURM_ERROR;
	} else if ((table == acct_coord_table)
		   || (table == qos_table)
		   || (table == wckey_table))
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

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
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
		loc_assoc_char = NULL;
		while ((row = mysql_fetch_row(result))) {
			slurmdb_association_rec_t *rem_assoc = NULL;
			if (loc_assoc_char)
				xstrcat(loc_assoc_char, " || ");
			xstrfmtcat(loc_assoc_char, "id_assoc=%s", row[0]);

			rem_assoc = xmalloc(sizeof(slurmdb_association_rec_t));
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

	/* We should not have to delete from usage table, only flag since we
	 * only delete things that are typos.
	 */
	xstrfmtcat(query,
		   "update \"%s_%s\" set mod_time=%ld, deleted=1 where (%s);"
		   "update \"%s_%s\" set mod_time=%ld, deleted=1 where (%s);"
		   "update \"%s_%s\" set mod_time=%ld, deleted=1 where (%s);",
		   cluster_name, assoc_day_table, now, loc_assoc_char,
		   cluster_name, assoc_hour_table, now, loc_assoc_char,
		   cluster_name, assoc_month_table, now, loc_assoc_char);

	debug3("%d(%s:%d) query\n%s %zd",
	       mysql_conn->conn, THIS_FILE, __LINE__, query, strlen(query));
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

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
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
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
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

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
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
			       "mod_time=%ld, deleted=1, def_qos_id=NULL, "
			       "shares=1, max_jobs=NULL, "
			       "max_nodes_pj=NULL, "
			       "max_wall_pj=NULL, "
			       "max_cpu_mins_pj=NULL "
			       "where (%s);",
			       cluster_name, assoc_table, now,
			       loc_assoc_char);

	/* if we are removing a cluster table this is handled in
	   remove_cluster_tables if table still exists. */
	if (table != cluster_table) {
		/* Make sure the next id we get doesn't create holes
		 * in the ids. */
		xstrfmtcat(mysql_conn->pre_commit_query,
			   "alter table \"%s_%s\" AUTO_INCREMENT=0;",
			   cluster_name, assoc_table);
	}
	if (table != assoc_table)
		xfree(loc_assoc_char);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS) {
		reset_mysql_conn(mysql_conn);
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
	mysql_conn_t *mysql_conn = NULL;

	/* since this can be loaded from many different places
	   only tell us once. */
	if (!first)
		return SLURM_SUCCESS;

	first = 0;

	if (!slurmdbd_conf) {
		char *cluster_name = NULL;
		if (!(cluster_name = slurm_get_cluster_name()))
			fatal("%s requires ClusterName in slurm.conf",
			      plugin_name);
		xfree(cluster_name);
	}

	mysql_db_info = create_mysql_db_info(SLURM_MYSQL_PLUGIN_AS);
	mysql_db_name = acct_get_db_name();

	debug2("mysql_connect() called for db %s", mysql_db_name);
	mysql_conn = create_mysql_conn(0, 0, NULL);
	if (mysql_db_get_db_connection(mysql_conn, mysql_db_name, mysql_db_info)
	    != SLURM_SUCCESS)
		fatal("The database must be up when starting "
		      "the MYSQL plugin.");

	/* make it so this can be rolled back if failed */
	mysql_autocommit(mysql_conn->db_conn, 0);
	rc = mysql_db_query(mysql_conn,
			    "SET session sql_mode='ANSI_QUOTES';");
	if (rc == SLURM_SUCCESS)
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
	}

	destroy_mysql_conn(mysql_conn);

	return rc;
}

extern int fini ( void )
{
	slurm_mutex_lock(&as_mysql_cluster_list_lock);
	if (as_mysql_cluster_list) {
		list_destroy(as_mysql_cluster_list);
		as_mysql_cluster_list = NULL;
	}
	slurm_mutex_unlock(&as_mysql_cluster_list_lock);
	slurm_mutex_destroy(&as_mysql_cluster_list_lock);
	destroy_mysql_db_info(mysql_db_info);
	xfree(mysql_db_name);
	xfree(default_qos_str);
	mysql_db_cleanup();
	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(const slurm_trigger_callbacks_t *cb,
                                           int conn_num, bool rollback,
                                           char *cluster_name)
{
	mysql_conn_t *mysql_conn = NULL;

	if (!mysql_db_info)
		init();

	debug2("acct_storage_p_get_connection: request new connection %d",
	       rollback);

	if (!(mysql_conn = create_mysql_conn(
		      conn_num, rollback, cluster_name)))
		fatal("couldn't get a mysql_conn");

	errno = SLURM_SUCCESS;
	mysql_db_get_db_connection(mysql_conn, mysql_db_name, mysql_db_info);

       	if (mysql_conn->db_conn) {
		int rc;
		if (rollback)
			mysql_autocommit(mysql_conn->db_conn, 0);
		rc = mysql_db_query(mysql_conn,
				    "SET session sql_mode='ANSI_QUOTES';");
		if (rc != SLURM_SUCCESS) {
			error("couldn't set sql_mode");
			acct_storage_p_close_connection(&mysql_conn);
			errno = rc;
		}
	}

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
	mysql_conn->cluster_deleted = 0;
	if ((rc != SLURM_SUCCESS) && (rc != ESLURM_CLUSTER_DELETED))
		return rc;

	debug4("got %d commits", list_count(mysql_conn->update_list));

	if (mysql_conn->rollback) {
		if (!commit) {
			if (mysql_db_rollback(mysql_conn))
				error("rollback failed");
		} else {
			int rc = SLURM_SUCCESS;
			/* Handle anything here we were unable to do
			   because of rollback issues.  i.e. Since any
			   use of altering a tables
			   AUTO_INCREMENT will make it so you can't
			   rollback, save it until right at the end.
			*/
			if (mysql_conn->pre_commit_query) {
				debug3("%d(%d) query\n%s",
				       mysql_conn->conn, __LINE__,
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
		int rc;
		char *query = NULL;
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;
		bool get_qos_count = 0;
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
			rc = slurmdb_send_accounting_update(
				mysql_conn->update_list,
				row[2], row[0],
				slurm_atoul(row[1]),
				slurm_atoul(row[3]));
		}
		mysql_free_result(result);
	skip:
		rc = assoc_mgr_update(mysql_conn->update_list);

		slurm_mutex_lock(&as_mysql_cluster_list_lock);
		itr2 = list_iterator_create(as_mysql_cluster_list);
		itr = list_iterator_create(mysql_conn->update_list);
		while ((object = list_next(itr))) {
			if (!object->objects || !list_count(object->objects))
				continue;
			/* We only care about clusters removed here. */
			switch(object->type) {
			case SLURMDB_REMOVE_CLUSTER:
				itr3 = list_iterator_create(object->objects);
				while ((rem_cluster = list_next(itr3))) {
					while ((cluster_name =
						list_next(itr2))) {
						if (!strcmp(cluster_name,
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

		if (get_qos_count)
			_set_qos_cnt(mysql_conn);
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

extern int acct_storage_p_add_associations(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   List association_list)
{
	return as_mysql_add_assocs(mysql_conn, uid, association_list);
}

extern int acct_storage_p_add_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				  List qos_list)
{
	return as_mysql_add_qos(mysql_conn, uid, qos_list);
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

extern List acct_storage_p_modify_associations(
	mysql_conn_t *mysql_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond,
	slurmdb_association_rec_t *assoc)
{
	return as_mysql_modify_assocs(mysql_conn, uid, assoc_cond, assoc);
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

extern List acct_storage_p_remove_associations(
	mysql_conn_t *mysql_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond)
{
	return as_mysql_remove_assocs(mysql_conn, uid, assoc_cond);
}

extern List acct_storage_p_remove_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond)
{
	return as_mysql_remove_qos(mysql_conn, uid, qos_cond);
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

extern List acct_storage_p_get_associations(
	mysql_conn_t *mysql_conn, uid_t uid,
	slurmdb_association_cond_t *assoc_cond)
{
	return as_mysql_get_assocs(mysql_conn, uid, assoc_cond);
}

extern List acct_storage_p_get_events(mysql_conn_t *mysql_conn, uint32_t uid,
				      slurmdb_event_cond_t *event_cond)
{
	return as_mysql_get_cluster_events(mysql_conn, uid, event_cond);
}

extern List acct_storage_p_get_problems(mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_association_cond_t *assoc_cond)
{
	List ret_list = NULL;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	ret_list = list_create(slurmdb_destroy_association_rec);

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

extern List acct_storage_p_get_config(void *db_conn)
{
	return NULL;
}

extern List acct_storage_p_get_qos(mysql_conn_t *mysql_conn, uid_t uid,
				   slurmdb_qos_cond_t *qos_cond)
{
	return as_mysql_get_qos(mysql_conn, uid, qos_cond);
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
				     uint16_t archive_data)
{
	return as_mysql_roll_usage(mysql_conn, sent_start,
				   sent_end, archive_data);
}

extern int clusteracct_storage_p_node_down(mysql_conn_t *mysql_conn,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	if (!mysql_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return as_mysql_node_down(mysql_conn, node_ptr,
				  event_time, reason, reason_uid);
}

extern int clusteracct_storage_p_node_up(mysql_conn_t *mysql_conn,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	if (!mysql_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return as_mysql_node_up(mysql_conn, node_ptr, event_time);
}

/* This is only called when not running from the slurmdbd so we can
 * assumes some things like rpc_version.
 */
extern int clusteracct_storage_p_register_ctld(mysql_conn_t *mysql_conn,
					       uint16_t port)
{
	if (!mysql_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return as_mysql_register_ctld(
		mysql_conn, mysql_conn->cluster_name, port);
}

extern int clusteracct_storage_p_cluster_cpus(mysql_conn_t *mysql_conn,
					      char *cluster_nodes,
					      uint32_t cpus,
					      time_t event_time)
{
	if (!mysql_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return as_mysql_cluster_cpus(mysql_conn,
				     cluster_nodes, cpus, event_time);
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
 * load into the storage a suspention of a job
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
	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return as_mysql_jobacct_process_archive(mysql_conn, arch_cond);
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
