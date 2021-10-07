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
#include "src/common/slurm_jobacct_gather.h"

/*
 * Any time you have to add to an existing convert update this number.
 * NOTE: 8 was the first version of 20.02.
 * NOTE: 9 was the first version of 20.11.
 * NOTE: 10 was the first version of 21.08.
 */
#define CONVERT_VERSION 10

typedef struct {
	uint64_t count;
	uint32_t id;
} local_tres_t;

static uint32_t db_curr_ver = NO_VAL;

static int _convert_step_table_post(
	mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if (db_curr_ver < 9) {
		/*
		 * Change the names pack_job_id and pack_job_offset to be het_*
		 */
		query = xstrdup_printf(
			"update \"%s_%s\" set id_step = %d where id_step = -2;"
			"update \"%s_%s\" set id_step = %d where id_step = -1;",
			cluster_name, step_table, SLURM_BATCH_SCRIPT,
			cluster_name, step_table, SLURM_EXTERN_CONT);
	}

	if (query) {
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			error("%s: Can't convert %s_%s info: %m",
			      __func__, cluster_name, step_table);
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

static int _convert_job_table_pre(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if (db_curr_ver < 8) {
		/*
		 * Change the names pack_job_id and pack_job_offset to be het_*
		 */
		query = xstrdup_printf(
			"alter table \"%s_%s\" "
			"change pack_job_id het_job_id int unsigned not null, "
			"change pack_job_offset het_job_offset "
			"int unsigned not null;",
			cluster_name, job_table);
	}

	if (query) {
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);

		rc = as_mysql_convert_alter_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			error("%s: Can't convert %s_%s info: %m",
			      __func__, cluster_name, job_table);
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
		info("post-converting step table for %s", cluster_name);
		if ((rc = _convert_step_table_post(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;
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
