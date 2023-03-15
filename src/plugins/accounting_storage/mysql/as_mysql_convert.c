/*****************************************************************************\
 *  as_mysql_convert.c - functions dealing with converting from tables in
 *                    slurm <= 17.02.
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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
\*****************************************************************************/

#include "as_mysql_convert.h"
#include "as_mysql_tres.h"
#include "src/interfaces/jobacct_gather.h"

/*
 * Any time you have to add to an existing convert update this number.
 * NOTE: 10 was the first version of 21.08.
 * NOTE: 11 was the first version of 22.05.
 * NOTE: 12 was the second version of 22.05.
 * NOTE: 13 was the first version of 23.02.
 */
#define CONVERT_VERSION 13

#define MIN_CONVERT_VERSION 10

#define JOB_CONVERT_LIMIT_CNT 1000

typedef enum {
	MOVE_ENV,
	MOVE_BATCH
} move_large_type_t;

typedef struct {
	uint64_t count;
	uint32_t id;
} local_tres_t;

static uint32_t db_curr_ver = NO_VAL;

static int _rename_clus_res_columns(mysql_conn_t *mysql_conn)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;

	/*
	 * Change the name 'percent_allowed' to be 'allowed'
	 */
	query = xstrdup_printf(
		"alter table %s change percent_allowed allowed "
		"int unsigned default 0;",
		clus_res_table);

	DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
	if ((rc = as_mysql_convert_alter_query(mysql_conn, query)) !=
	    SLURM_SUCCESS)
		error("Can't update %s %m", clus_res_table);
	xfree(query);

	return rc;
}

static int _convert_clus_res_table_pre(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;

	if (db_curr_ver < 13) {
		if ((rc = _rename_clus_res_columns(mysql_conn)) !=
		    SLURM_SUCCESS)
			return rc;
	}

	return rc;
}

static int _rename_usage_columns(mysql_conn_t *mysql_conn, char *table)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;


	/*
	 * Change the names pack_job_id and pack_job_offset to be het_*
	 */
	query = xstrdup_printf(
		"alter table %s change resv_secs plan_secs bigint "
		"unsigned default 0 not null;",
		table);

	DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
	if ((rc = as_mysql_convert_alter_query(mysql_conn, query)) !=
	    SLURM_SUCCESS)
		error("Can't update %s %m", table);
	xfree(query);

	return rc;
}

static int _convert_usage_table_pre(mysql_conn_t *mysql_conn,
				    char *cluster_name)
{
	int rc = SLURM_SUCCESS;

	if (db_curr_ver < 10) {
		char table[200];

		snprintf(table, sizeof(table), "\"%s_%s\"",
			 cluster_name, cluster_day_table);
		if ((rc = _rename_usage_columns(mysql_conn, table))
		    != SLURM_SUCCESS)
			return rc;

		snprintf(table, sizeof(table), "\"%s_%s\"",
			 cluster_name, cluster_hour_table);
		if ((rc = _rename_usage_columns(mysql_conn, table))
		    != SLURM_SUCCESS)
			return rc;

		snprintf(table, sizeof(table), "\"%s_%s\"",
			 cluster_name, cluster_month_table);
		if ((rc = _rename_usage_columns(mysql_conn, table))
		    != SLURM_SUCCESS)
			return rc;
	}

	return rc;
}

static int _insert_into_hash_table(mysql_conn_t *mysql_conn, char *cluster_name,
				   move_large_type_t type)
{
	char *query, *hash_inx_col;
	char *hash_col = NULL, *type_col = NULL, *type_table = NULL;
	int rc;

	switch (type) {
	case MOVE_ENV:
		hash_col = "env_hash";
		hash_inx_col = "env_hash_inx";
		type_col = "env_vars";
		type_table = job_env_table;
		break;
	case MOVE_BATCH:
		hash_col = "script_hash";
		hash_inx_col = "script_hash_inx";
		type_col = "batch_script";
		type_table = job_script_table;
		break;
	default:
		return SLURM_ERROR;
		break;
	}

	info("Starting insert from job_table into %s", type_table);
	/*
	 * Do the transfer inside MySQL.  This results in a much quicker
	 * transfer instead of doing a select then an insert after the fact.
	 */
	query = xstrdup_printf(
		"insert into \"%s_%s\" (%s, %s) "
		"select distinct %s, %s from \"%s_%s\" "
		"where %s is not NULL && "
		"(id_array_job=id_job || !id_array_job) "
		"on duplicate key update last_used=UNIX_TIMESTAMP();",
		cluster_name, type_table,
		hash_col, type_col,
		hash_col, type_col,
		cluster_name, job_table,
		type_col);
	DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	if (rc != SLURM_SUCCESS)
		return rc;

	query = xstrdup_printf(
		"update \"%s_%s\" as jobs inner join \"%s_%s\" as hash "
		"on jobs.%s = hash.%s set jobs.%s = hash.hash_inx;",
		cluster_name, job_table,
		cluster_name, type_table,
		hash_col, hash_col,
		hash_inx_col);
	DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	info("Done");
	return rc;
}


static int _convert_job_table_pre(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	storage_field_t job_table_fields_21_08[] = {
		{ "job_db_inx", "bigint unsigned not null auto_increment" },
		{ "mod_time", "bigint unsigned default 0 not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "account", "tinytext" },
		{ "admin_comment", "text" },
		{ "array_task_str", "text" },
		{ "array_max_tasks", "int unsigned default 0 not null" },
		{ "array_task_pending", "int unsigned default 0 not null" },
		{ "batch_script", "longtext" },
		{ "constraints", "text default ''" },
		{ "container", "text" },
		{ "cpus_req", "int unsigned not null" },
		{ "derived_ec", "int unsigned default 0 not null" },
		{ "derived_es", "text" },
		{ "env_vars", "longtext" },
		{ "env_hash", "text" },
		{ "env_hash_inx", "bigint unsigned default 0 not null" },
		{ "exit_code", "int unsigned default 0 not null" },
		{ "flags", "int unsigned default 0 not null" },
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
		{ "het_job_id", "int unsigned not null" },
		{ "het_job_offset", "int unsigned not null" },
		{ "kill_requid", "int default null" },
		{ "state_reason_prev", "int unsigned not null" },
		{ "mcs_label", "tinytext default ''" },
		{ "mem_req", "bigint unsigned default 0 not null" },
		{ "nodelist", "text" },
		{ "nodes_alloc", "int unsigned not null" },
		{ "node_inx", "text" },
		{ "partition", "tinytext not null" },
		{ "priority", "int unsigned not null" },
		{ "script_hash", "text" },
		{ "script_hash_inx", "bigint unsigned default 0 not null" },
		{ "state", "int unsigned not null" },
		{ "timelimit", "int unsigned default 0 not null" },
		{ "time_submit", "bigint unsigned default 0 not null" },
		{ "time_eligible", "bigint unsigned default 0 not null" },
		{ "time_start", "bigint unsigned default 0 not null" },
		{ "time_end", "bigint unsigned default 0 not null" },
		{ "time_suspended", "bigint unsigned default 0 not null" },
		{ "gres_used", "text not null default ''" },
		{ "wckey", "tinytext not null default ''" },
		{ "work_dir", "text not null default ''" },
		{ "submit_line", "text" },
		{ "system_comment", "text" },
		{ "track_steps", "tinyint not null" },
		{ "tres_alloc", "text not null default ''" },
		{ "tres_req", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t job_env_table_fields[] = {
		{ "hash_inx", "bigint unsigned not null auto_increment" },
		{ "last_used", "timestamp DEFAULT CURRENT_TIMESTAMP not null" },
		{ "env_hash", "text not null" },
		{ "env_vars", "longtext" },
		{ NULL, NULL}
	};

	storage_field_t job_script_table_fields[] = {
		{ "hash_inx", "bigint unsigned not null auto_increment" },
		{ "last_used", "timestamp DEFAULT CURRENT_TIMESTAMP not null" },
		{ "script_hash", "text not null" },
		{ "batch_script", "longtext" },
		{ NULL, NULL}
	};

	if (db_curr_ver == 10) {
		/* This only needs to happen for 21.08 databases */
		char table_name[200];
		char *query;

		snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
			 cluster_name, job_table);

		if (mysql_db_create_table(
			    mysql_conn, table_name, job_table_fields_21_08,
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
			    "key het_job (het_job_id), "
			    "key reserv (id_resv), "
			    "key sacct_def (id_user, time_start, "
			    "time_end), "
			    "key sacct_def2 (id_user, time_end, "
			    "time_eligible), "
			    "key env_hash_inx (env_hash_inx), "
			    "key script_hash_inx (script_hash_inx))")
		    == SLURM_ERROR)
			return SLURM_ERROR;


		snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
			 cluster_name, job_env_table);
		if (mysql_db_create_table(mysql_conn, table_name,
					  job_env_table_fields,
					  ", primary key (hash_inx), "
					  "unique index env_hash_inx "
					  "(env_hash(66)))")
		    == SLURM_ERROR)
			return SLURM_ERROR;

		snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
			 cluster_name, job_script_table);
		if (mysql_db_create_table(mysql_conn, table_name,
					  job_script_table_fields,
					  ", primary key (hash_inx), "
					  "unique index script_hash_inx "
					  "(script_hash(66)))")
		    == SLURM_ERROR)
			return SLURM_ERROR;

		/*
		 * Using SHA256 here inside MySQL as it will make the conversion
		 * dramatically faster.  This will cause these tables to
		 * potentially be 2x in size as all future things will be K12,
		 * but in theory that shouldn't be as big a deal as this
		 * conversion should save much room more than that.
		 */
		info("Creating env and batch script hashes in the job_table");
		query = xstrdup_printf(
			"update \"%s_%s\" set "
			"env_hash = concat('%d:', SHA2(env_vars, 256)), "
			"script_hash = concat('%d:', SHA2(batch_script, 256));",
			cluster_name, job_table,
			HASH_PLUGIN_SHA256, HASH_PLUGIN_SHA256);
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		info("Done");

		if ((rc = _insert_into_hash_table(
			     mysql_conn, cluster_name,
			     MOVE_ENV)) != SLURM_SUCCESS)
			return rc;

		if ((rc = _insert_into_hash_table(
			     mysql_conn, cluster_name,
			     MOVE_BATCH)) != SLURM_SUCCESS)
			return rc;
	}

	if (db_curr_ver < 12) {
		char *table_name;
		char *query;

		table_name = xstrdup_printf("\"%s_%s\"",
					    cluster_name, job_table);
		/* Update kill_requid to NULL instead of -1 for not set */
		query = xstrdup_printf("alter table %s modify kill_requid "
				       "int default null;", table_name);
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
		if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS) {
			xfree(query);
			return rc;
		}
		xfree(query);
		query = xstrdup_printf("update %s set kill_requid=null where "
				       "kill_requid=-1;", table_name);
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		xfree(table_name);
	}

	return rc;
}

static int _convert_step_table_pre(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;

	if (db_curr_ver < 12) {
		char *table_name;
		char *query;

		table_name = xstrdup_printf("\"%s_%s\"",
					    cluster_name, step_table);
		/* temporarily "not null" from req_uid */
		query = xstrdup_printf("alter table %s modify kill_requid "
				       "int default null;", table_name);
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
		if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS) {
			xfree(query);
			return rc;
		}
		xfree(query);
		/* update kill_requid = -1 to NULL */
		query = xstrdup_printf("update %s set kill_requid=null where "
				       "kill_requid=-1;", table_name);
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		xfree(table_name);
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
	DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
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
		int tmp_ver = CONVERT_VERSION;
		mysql_free_result(result);

		query = xstrdup_printf("insert into %s (version) values (%d);",
				       convert_version_table, tmp_ver);
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			return SLURM_ERROR;
		db_curr_ver = tmp_ver;
	}

	return rc;
}

extern void as_mysql_convert_possible(mysql_conn_t *mysql_conn)
{
	(void) _set_db_curr_ver(mysql_conn);

	/*
	 * Check to see if conversion is possible.
	 */
	if (db_curr_ver == NO_VAL) {
		/*
		 * Check if the cluster_table exists before deciding if this is
		 * a new database or a database that predates the
		 * convert_version_table.
		 */
		MYSQL_RES *result = NULL;
		char *query = xstrdup_printf("select name from %s limit 1",
					     cluster_table);
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
		if ((result = mysql_db_query_ret(mysql_conn, query, 0))) {
			/*
			 * knowing that the table exists is enough to say this
			 * is an old database.
			 */
			xfree(query);
			mysql_free_result(result);
			fatal("Database schema is too old for this version of Slurm to upgrade.");
		}
		xfree(query);
		debug4("Database is new, conversion is not required");
	} else if (db_curr_ver < MIN_CONVERT_VERSION) {
		fatal("Database schema is too old for this version of Slurm to upgrade.");
	} else if (db_curr_ver > CONVERT_VERSION) {
		char *err_msg = "Database schema is from a newer version of Slurm, downgrading is not possible.";
		/*
		 * If we are configured --enable-debug only make this a
		 * debug statement instead of fatal to allow developers
		 * easier bisects.
		 */
#ifdef NDEBUG
		fatal("%s", err_msg);
#else
		debug("%s", err_msg);
#endif
	}
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
		debug4("No conversion needed, Horray!");
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

	info("pre-converting cluster resource table");
	if ((rc = _convert_clus_res_table_pre(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	/* make it up to date */
	itr = list_iterator_create(as_mysql_total_cluster_list);
	while ((cluster_name = list_next(itr))) {
		/*
		 * When calling alters on tables here please remember to use
		 * as_mysql_convert_alter_query instead of mysql_db_query to be
		 * able to detect a previous failed conversion.
		 */
		info("pre-converting usage table for %s", cluster_name);
		if ((rc = _convert_usage_table_pre(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;
		info("pre-converting job table for %s", cluster_name);
		if ((rc = _convert_job_table_pre(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;
		info("pre-converting step table for %s", cluster_name);
		if ((rc = _convert_step_table_pre(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;
	}
	list_iterator_destroy(itr);

	return rc;
}

extern int as_mysql_convert_tables_post_create(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;
	/* ListIterator itr; */
	/* char *cluster_name; */

	xassert(as_mysql_total_cluster_list);

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if (db_curr_ver == CONVERT_VERSION) {
		debug4("No conversion needed, Horray!");
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
	/* itr = list_iterator_create(as_mysql_total_cluster_list); */
	/* while ((cluster_name = list_next(itr))) { */
	/* } */
	/* list_iterator_destroy(itr); */

	return rc;
}

extern int as_mysql_convert_non_cluster_tables_post_create(
	mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if (db_curr_ver == CONVERT_VERSION) {
		debug4("No conversion needed, Horray!");
		return SLURM_SUCCESS;
	}

	if (rc == SLURM_SUCCESS) {
		char *query = xstrdup_printf(
			"update %s set version=%d, mod_time=UNIX_TIMESTAMP()",
			convert_version_table, CONVERT_VERSION);

		info("Conversion done: success!");

		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}

/*
 * Only use this when running "ALTER TABLE" during an upgrade.  This is to get
 * around that mysql cannot rollback an "ALTER TABLE", but its possible that the
 * rest of the upgrade transaction was aborted.
 *
 * We may not always use this function, but don't delete it just in case we
 * need to alter tables in the future.
 */
extern int as_mysql_convert_alter_query(mysql_conn_t *mysql_conn, char *query)
{
	int rc = SLURM_SUCCESS;

	rc = mysql_db_query(mysql_conn, query);
	if ((rc != SLURM_SUCCESS) && (errno == ER_BAD_FIELD_ERROR)) {
		errno = 0;
		rc = SLURM_SUCCESS;
		info("The database appears to have been altered by a previous upgrade attempt, continuing with upgrade.");
	}

	return rc;
}
