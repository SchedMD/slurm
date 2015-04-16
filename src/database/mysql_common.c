/*****************************************************************************\
 *  mysql_common.c - common functions for the mysql storage plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "mysql_common.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/timers.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/read_config.h"

static char *table_defs_table = "table_defs_table";

typedef struct {
	char *name;
	char *columns;
} db_key_t;

static void _destroy_db_key(void *arg)
{
	db_key_t *db_key = (db_key_t *)arg;

	if (db_key) {
		xfree(db_key->name);
		xfree(db_key->columns);
		xfree(db_key);
	}
}

/* NOTE: Insure that mysql_conn->lock is set on function entry */
static int _clear_results(MYSQL *db_conn)
{
	MYSQL_RES *result = NULL;
	int rc = 0;

	do {
		/* did current statement return data? */
		if ((result = mysql_store_result(db_conn)))
			mysql_free_result(result);

		/* more results? -1 = no, >0 = error, 0 = yes (keep looping) */
		if ((rc = mysql_next_result(db_conn)) > 0)
			error("Could not execute statement %d %s",
			      mysql_errno(db_conn),
			      mysql_error(db_conn));
	} while (rc == 0);

	if (rc > 0) {
		errno = rc;
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/* NOTE: Insure that mysql_conn->lock is set on function entry */
static MYSQL_RES *_get_first_result(MYSQL *db_conn)
{
	MYSQL_RES *result = NULL;
	int rc = 0;
	do {
		/* did current statement return data? */
		if ((result = mysql_store_result(db_conn)))
			return result;

		/* more results? -1 = no, >0 = error, 0 = yes (keep looping) */
		if ((rc = mysql_next_result(db_conn)) > 0)
			debug3("error: Could not execute statement %d", rc);

	} while (rc == 0);

	return NULL;
}

/* NOTE: Insure that mysql_conn->lock is set on function entry */
static MYSQL_RES *_get_last_result(MYSQL *db_conn)
{
	MYSQL_RES *result = NULL;
	MYSQL_RES *last_result = NULL;
	int rc = 0;
	do {
		/* did current statement return data? */
		if ((result = mysql_store_result(db_conn))) {
			if (last_result)
				mysql_free_result(last_result);
			last_result = result;
		}
		/* more results? -1 = no, >0 = error, 0 = yes (keep looping) */
		if ((rc = mysql_next_result(db_conn)) > 0)
			debug3("error: Could not execute statement %d", rc);
	} while (rc == 0);

	return last_result;
}

/* NOTE: Insure that mysql_conn->lock is set on function entry */
static int _mysql_query_internal(MYSQL *db_conn, char *query)
{
	int rc = SLURM_SUCCESS;

	if (!db_conn)
		fatal("You haven't inited this storage yet.");

	/* clear out the old results so we don't get a 2014 error */
	_clear_results(db_conn);
	if (mysql_query(db_conn, query)) {
		const char *err_str = mysql_error(db_conn);
		errno = mysql_errno(db_conn);
		if (errno == ER_NO_SUCH_TABLE) {
			debug4("This could happen often and is expected.\n"
			       "mysql_query failed: %d %s\n%s",
			       errno, err_str, query);
			errno = 0;
			goto end_it;
		}
		error("mysql_query failed: %d %s\n%s", errno, err_str, query);
		if (errno == ER_LOCK_WAIT_TIMEOUT) {
			fatal("mysql gave ER_LOCK_WAIT_TIMEOUT as an error. "
			      "The only way to fix this is restart the "
			      "calling program");
		}
		/* FIXME: If we get ER_LOCK_WAIT_TIMEOUT here we need
		 * to restart the connections, but it appears restarting
		 * the calling program is the only way to handle this.
		 * If anyone in the future figures out a way to handle
		 * this, super.  Until then we will need to restart the
		 * calling program if you ever get this error.
		 */
		rc = SLURM_ERROR;
	}
end_it:
	return rc;
}

/* NOTE: Insure that mysql_conn->lock is NOT set on function entry */
static int _mysql_make_table_current(mysql_conn_t *mysql_conn, char *table_name,
				     storage_field_t *fields, char *ending)
{
	char *query = NULL;
	char *correct_query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int i = 0;
	List columns = NULL;
	ListIterator itr = NULL;
	char *col = NULL;
	int adding = 0;
	int run_update = 0;
	char *primary_key = NULL;
	char *unique_index = NULL;
	int old_primary = 0;
	char *old_index = NULL;
	char *temp = NULL, *temp2 = NULL;
	List keys_list = NULL;
	db_key_t *db_key = NULL;

	DEF_TIMERS;

	/* figure out the unique keys in the table */
	query = xstrdup_printf("show index from %s where non_unique=0",
			       table_name);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	while ((row = mysql_fetch_row(result))) {
		// row[2] is the key name
		if (!strcasecmp(row[2], "PRIMARY"))
			old_primary = 1;
		else if (!old_index)
			old_index = xstrdup(row[2]);
	}
	mysql_free_result(result);

	/* figure out the non-unique keys in the table */
	query = xstrdup_printf("show index from %s where non_unique=1",
			       table_name);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	itr = NULL;
	keys_list = list_create(_destroy_db_key);
	while ((row = mysql_fetch_row(result))) {
		if (!itr)
			itr = list_iterator_create(keys_list);
		else
			list_iterator_reset(itr);
		while ((db_key = list_next(itr))) {
			if (!strcmp(db_key->name, row[2]))
				break;
		}

		if (db_key) {
			xstrfmtcat(db_key->columns, ", %s", row[4]);
		} else {
			db_key = xmalloc(sizeof(db_key_t));
			db_key->name = xstrdup(row[2]); // name
			db_key->columns = xstrdup(row[4]); // column name
			list_append(keys_list, db_key); // don't use list_push
		}
	}
	mysql_free_result(result);

	if (itr) {
		list_iterator_destroy(itr);
		itr = NULL;
	}

	/* figure out the existing columns in the table */
	query = xstrdup_printf("show columns from %s", table_name);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		xfree(old_index);
		FREE_NULL_LIST(keys_list);
		return SLURM_ERROR;
	}
	xfree(query);
	columns = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		col = xstrdup(row[0]); //Field
		list_append(columns, col);
	}
	mysql_free_result(result);


	itr = list_iterator_create(columns);
	query = xstrdup_printf("alter table %s", table_name);
	correct_query = xstrdup_printf("alter table %s", table_name);
	START_TIMER;
	while (fields[i].name) {
		int found = 0;

		list_iterator_reset(itr);
		while ((col = list_next(itr))) {
			if (!strcmp(col, fields[i].name)) {
				xstrfmtcat(query, " modify `%s` %s,",
					   fields[i].name,
					   fields[i].options);
				xstrfmtcat(correct_query, " modify `%s` %s,",
					   fields[i].name,
					   fields[i].options);
				list_delete_item(itr);
				found = 1;
				break;
			}
		}
		if (!found) {
			if (i) {
				info("adding column %s after %s in table %s",
				     fields[i].name,
				     fields[i-1].name,
				     table_name);
				xstrfmtcat(query, " add `%s` %s after %s,",
					   fields[i].name,
					   fields[i].options,
					   fields[i-1].name);
				xstrfmtcat(correct_query, " modify `%s` %s,",
					   fields[i].name,
					   fields[i].options);
			} else {
				info("adding column %s at the beginning "
				     "of table %s",
				     fields[i].name,
				     table_name);
				xstrfmtcat(query, " add `%s` %s first,",
					   fields[i].name,
					   fields[i].options);
				xstrfmtcat(correct_query, " modify `%s` %s,",
					   fields[i].name,
					   fields[i].options);
			}
			adding = 1;
		}

		i++;
	}

	list_iterator_reset(itr);
	while ((col = list_next(itr))) {
		adding = 1;
		info("dropping column %s from table %s", col, table_name);
		xstrfmtcat(query, " drop %s,", col);
	}

	list_iterator_destroy(itr);
	list_destroy(columns);

	if ((temp = strstr(ending, "primary key ("))) {
		int open = 0, close =0;
		int end = 0;
		while (temp[end++]) {
			if (temp[end] == '(')
				open++;
			else if (temp[end] == ')')
				close++;
			else
				continue;
			if (open == close)
				break;
		}
		if (temp[end]) {
			end++;
			primary_key = xstrndup(temp, end);
			if (old_primary) {
				xstrcat(query, " drop primary key,");
				xstrcat(correct_query, " drop primary key,");
			}
			xstrfmtcat(query, " add %s,",  primary_key);
			xstrfmtcat(correct_query, " add %s,",  primary_key);

			xfree(primary_key);
		}
	}

	if ((temp = strstr(ending, "unique index ("))) {
		int open = 0, close =0;
		int end = 0;
		while (temp[end++]) {
			if (temp[end] == '(')
				open++;
			else if (temp[end] == ')')
				close++;
			else
				continue;
			if (open == close)
				break;
		}
		if (temp[end]) {
			end++;
			unique_index = xstrndup(temp, end);
			if (old_index) {
				xstrfmtcat(query, " drop index %s,",
					   old_index);
				xstrfmtcat(correct_query, " drop index %s,",
					   old_index);
			}
			xstrfmtcat(query, " add %s,", unique_index);
			xstrfmtcat(correct_query, " add %s,", unique_index);
			xfree(unique_index);
		}
	}
	xfree(old_index);

	temp2 = ending;
	itr = list_iterator_create(keys_list);
	while ((temp = strstr(temp2, ", key "))) {
		int open = 0, close = 0, name_end = 0;
		int end = 5;
		char *new_key_name = NULL, *new_key = NULL;
		while (temp[end++]) {
			if (!name_end && (temp[end] == ' ')) {
				name_end = end;
				continue;
			} else if (temp[end] == '(') {
				open++;
				if (!name_end)
					name_end = end;
			} else if (temp[end] == ')')
				close++;
			else
				continue;
			if (open == close)
				break;
		}
		if (temp[end]) {
			end++;
			new_key_name = xstrndup(temp+6, name_end-6);
			new_key = xstrndup(temp+2, end-2); // skip ', '
			while ((db_key = list_next(itr))) {
				if (!strcmp(db_key->name, new_key_name)) {
					list_remove(itr);
					break;
				}
			}
			list_iterator_reset(itr);
			if (db_key) {
				xstrfmtcat(query,
					   " drop key %s,", db_key->name);
				xstrfmtcat(correct_query,
					   " drop key %s,", db_key->name);
				_destroy_db_key(db_key);
			} else
				info("adding %s to table %s",
				     new_key, table_name);

			xstrfmtcat(query, " add %s,",  new_key);
			xstrfmtcat(correct_query, " add %s,",  new_key);

			xfree(new_key);
			xfree(new_key_name);
		}
		temp2 = temp + end;
	}

	/* flush extra (old) keys */
	while ((db_key = list_next(itr))) {
		info("dropping key %s from table %s", db_key->name, table_name);
		xstrfmtcat(query, " drop key %s,", db_key->name);
	}
	list_iterator_destroy(itr);

	list_destroy(keys_list);

	query[strlen(query)-1] = ';';
	correct_query[strlen(correct_query)-1] = ';';
	//info("%d query\n%s", __LINE__, query);

	/* see if we have already done this definition */
	if (!adding) {
		char *quoted = slurm_add_slash_to_quotes(query);
		char *query2 = xstrdup_printf("select table_name from "
					      "%s where definition='%s'",
					      table_defs_table, quoted);
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;

		xfree(quoted);
		run_update = 1;
		if ((result = mysql_db_query_ret(mysql_conn, query2, 0))) {
			if ((row = mysql_fetch_row(result)))
				run_update = 0;
			mysql_free_result(result);
		}
		xfree(query2);
		if (run_update) {
			run_update = 2;
			query2 = xstrdup_printf("select table_name from "
						"%s where table_name='%s'",
						table_defs_table, table_name);
			if ((result = mysql_db_query_ret(
				     mysql_conn, query2, 0))) {
				if ((row = mysql_fetch_row(result)))
					run_update = 1;
				mysql_free_result(result);
			}
			xfree(query2);
		}
	}

	/* if something has changed run the alter line */
	if (run_update || adding) {
		time_t now = time(NULL);
		char *query2 = NULL;
		char *quoted = NULL;

		if (run_update == 2)
			debug4("Table %s doesn't exist, adding", table_name);
		else
			debug("Table %s has changed.  Updating...", table_name);
		if (mysql_db_query(mysql_conn, query)) {
			xfree(query);
			return SLURM_ERROR;
		}
		quoted = slurm_add_slash_to_quotes(correct_query);
		query2 = xstrdup_printf("insert into %s (creation_time, "
					"mod_time, table_name, definition) "
					"values (%ld, %ld, '%s', '%s') "
					"on duplicate key update "
					"definition='%s', mod_time=%ld;",
					table_defs_table, now, now,
					table_name, quoted,
					quoted, now);
		xfree(quoted);
		if (mysql_db_query(mysql_conn, query2)) {
			xfree(query2);
			return SLURM_ERROR;
		}
		xfree(query2);
	}

	xfree(query);
	xfree(correct_query);
	query = xstrdup_printf("make table current %s", table_name);
	END_TIMER2(query);
	xfree(query);
	return SLURM_SUCCESS;
}

/* NOTE: Insure that mysql_conn->lock is set on function entry */
static int _create_db(char *db_name, mysql_db_info_t *db_info)
{
	char create_line[50];
	MYSQL *mysql_db = NULL;
	int rc = SLURM_ERROR;

	MYSQL *db_ptr = NULL;
	char *db_host = NULL;

	while (rc == SLURM_ERROR) {
		rc = SLURM_SUCCESS;
		if (!(mysql_db = mysql_init(mysql_db)))
			fatal("mysql_init failed: %s", mysql_error(mysql_db));

		db_host = db_info->host;
		db_ptr = mysql_real_connect(mysql_db,
					    db_host, db_info->user,
					    db_info->pass, NULL,
					    db_info->port, NULL, 0);

		if (!db_ptr && db_info->backup) {
			info("Connection failed to host = %s "
			     "user = %s port = %u",
			     db_host, db_info->user,
			     db_info->port);
			db_host = db_info->backup;
			db_ptr = mysql_real_connect(mysql_db, db_host,
						    db_info->user,
						    db_info->pass, NULL,
						    db_info->port, NULL, 0);
		}

		if (db_ptr) {
			snprintf(create_line, sizeof(create_line),
				 "create database %s", db_name);
			if (mysql_query(mysql_db, create_line)) {
				fatal("mysql_real_query failed: %d %s\n%s",
				      mysql_errno(mysql_db),
				      mysql_error(mysql_db), create_line);
			}
			if (mysql_thread_safe())
				mysql_thread_end();
			mysql_close(mysql_db);
		} else {
			info("Connection failed to host = %s "
			     "user = %s port = %u",
			     db_host, db_info->user,
			     db_info->port);
			error("mysql_real_connect failed: %d %s",
			      mysql_errno(mysql_db),
			      mysql_error(mysql_db));
			rc = SLURM_ERROR;
		}
		if (rc == SLURM_ERROR)
			sleep(3);
	}
	return rc;
}

extern mysql_conn_t *create_mysql_conn(int conn_num, bool rollback,
				       char *cluster_name)
{
	mysql_conn_t *mysql_conn = xmalloc(sizeof(mysql_conn_t));

	mysql_conn->rollback = rollback;
	mysql_conn->conn = conn_num;
	mysql_conn->cluster_name = xstrdup(cluster_name);
	slurm_mutex_init(&mysql_conn->lock);
	mysql_conn->update_list = list_create(slurmdb_destroy_update_object);

	return mysql_conn;
}

extern int destroy_mysql_conn(mysql_conn_t *mysql_conn)
{
	if (mysql_conn) {
		mysql_db_close_db_connection(mysql_conn);
		xfree(mysql_conn->pre_commit_query);
		xfree(mysql_conn->cluster_name);
		slurm_mutex_destroy(&mysql_conn->lock);
		list_destroy(mysql_conn->update_list);
		xfree(mysql_conn);
	}

	return SLURM_SUCCESS;
}

extern mysql_db_info_t *create_mysql_db_info(slurm_mysql_plugin_type_t type)
{
	mysql_db_info_t *db_info = xmalloc(sizeof(mysql_db_info_t));

	switch (type) {
	case SLURM_MYSQL_PLUGIN_AS:
		db_info->port = slurm_get_accounting_storage_port();
		if (!db_info->port) {
			db_info->port = DEFAULT_MYSQL_PORT;
			slurm_set_accounting_storage_port(db_info->port);
		}
		db_info->host = slurm_get_accounting_storage_host();
		db_info->backup = slurm_get_accounting_storage_backup_host();
		db_info->user = slurm_get_accounting_storage_user();
		db_info->pass = slurm_get_accounting_storage_pass();
		break;
	case SLURM_MYSQL_PLUGIN_JC:
		db_info->port = slurm_get_jobcomp_port();
		if (!db_info->port) {
			db_info->port = DEFAULT_MYSQL_PORT;
			slurm_set_jobcomp_port(db_info->port);
		}
		db_info->host = slurm_get_jobcomp_host();
		db_info->user = slurm_get_jobcomp_user();
		db_info->pass = slurm_get_jobcomp_pass();
		break;
	default:
		xfree(db_info);
		fatal("Unknown mysql_db_info %d", type);
	}
	return db_info;
}

extern int destroy_mysql_db_info(mysql_db_info_t *db_info)
{
	if (db_info) {
		xfree(db_info->backup);
		xfree(db_info->host);
		xfree(db_info->user);
		xfree(db_info->pass);
		xfree(db_info);
	}
	return SLURM_SUCCESS;
}

extern int mysql_db_get_db_connection(mysql_conn_t *mysql_conn, char *db_name,
				      mysql_db_info_t *db_info)
{
	int rc = SLURM_SUCCESS;
	bool storage_init = false;
	char *db_host = db_info->host;

	xassert(mysql_conn);

	slurm_mutex_lock(&mysql_conn->lock);

	if (!(mysql_conn->db_conn = mysql_init(mysql_conn->db_conn))) {
		slurm_mutex_unlock(&mysql_conn->lock);
		fatal("mysql_init failed: %s",
		      mysql_error(mysql_conn->db_conn));
	} else {
		/* If this ever changes you will need to alter
		 * src/common/slurmdbd_defs.c function _send_init_msg to
		 * handle a different timeout when polling for the
		 * response.
		 */
		unsigned int my_timeout = 30;
#ifdef MYSQL_OPT_RECONNECT
		my_bool reconnect = 1;
		/* make sure reconnect is on */
		mysql_options(mysql_conn->db_conn, MYSQL_OPT_RECONNECT,
			      &reconnect);
#endif
		mysql_options(mysql_conn->db_conn, MYSQL_OPT_CONNECT_TIMEOUT,
			      (char *)&my_timeout);
		while (!storage_init) {
			if (!mysql_real_connect(mysql_conn->db_conn, db_host,
					        db_info->user, db_info->pass,
					        db_name, db_info->port, NULL,
					        CLIENT_MULTI_STATEMENTS)) {
				int err = mysql_errno(mysql_conn->db_conn);
				if (err == ER_BAD_DB_ERROR) {
					debug("Database %s not created.  "
					      "Creating", db_name);
					rc = _create_db(db_name, db_info);
				} else {
					const char *err_str = mysql_error(
						mysql_conn->db_conn);
					error("mysql_real_connect failed: "
					      "%d %s",
					      err, err_str);
					if ((db_host == db_info->host)
					    && db_info->backup) {
						db_host = db_info->backup;
						continue;
					}

					rc = ESLURM_DB_CONNECTION;
					mysql_close(mysql_conn->db_conn);
					mysql_conn->db_conn = NULL;
					break;
				}
			} else {
				storage_init = true;
				if (mysql_conn->rollback)
					mysql_autocommit(
						mysql_conn->db_conn, 0);
				rc = _mysql_query_internal(
					mysql_conn->db_conn,
					"SET session sql_mode='ANSI_QUOTES,"
					"NO_ENGINE_SUBSTITUTION';");
			}
		}
	}
	slurm_mutex_unlock(&mysql_conn->lock);
	errno = rc;
	return rc;
}

extern int mysql_db_close_db_connection(mysql_conn_t *mysql_conn)
{
	slurm_mutex_lock(&mysql_conn->lock);
	if (mysql_conn && mysql_conn->db_conn) {
		if (mysql_thread_safe())
			mysql_thread_end();
		mysql_close(mysql_conn->db_conn);
		mysql_conn->db_conn = NULL;
	}
	slurm_mutex_unlock(&mysql_conn->lock);
	return SLURM_SUCCESS;
}

extern int mysql_db_cleanup()
{
	debug3("starting mysql cleaning up");

#ifdef mysql_library_end
	mysql_library_end();
#else
	mysql_server_end();
#endif
	debug3("finished mysql cleaning up");
	return SLURM_SUCCESS;
}

extern int mysql_db_query(mysql_conn_t *mysql_conn, char *query)
{
	int rc = SLURM_SUCCESS;

	if (!mysql_conn || !mysql_conn->db_conn) {
		fatal("You haven't inited this storage yet.");
		return 0;	/* For CLANG false positive */
	}
	slurm_mutex_lock(&mysql_conn->lock);
	rc = _mysql_query_internal(mysql_conn->db_conn, query);
	slurm_mutex_unlock(&mysql_conn->lock);
	return rc;
}

/*
 * Executes a single delete sql query.
 * Returns the number of deleted rows, <0 for failure.
 */
extern int mysql_db_delete_affected_rows(mysql_conn_t *mysql_conn, char *query)
{
	int rc = SLURM_SUCCESS;

	if (!mysql_conn || !mysql_conn->db_conn) {
		fatal("You haven't inited this storage yet.");
		return 0;	/* For CLANG false positive */
	}
	slurm_mutex_lock(&mysql_conn->lock);
	if (!(rc = _mysql_query_internal(mysql_conn->db_conn, query)))
			rc = mysql_affected_rows(mysql_conn->db_conn);
	slurm_mutex_unlock(&mysql_conn->lock);
	return rc;
}

extern int mysql_db_ping(mysql_conn_t *mysql_conn)
{
	int rc;

	if (!mysql_conn->db_conn)
		return -1;

	/* clear out the old results so we don't get a 2014 error */
	slurm_mutex_lock(&mysql_conn->lock);
	_clear_results(mysql_conn->db_conn);
	rc = mysql_ping(mysql_conn->db_conn);
	slurm_mutex_unlock(&mysql_conn->lock);
	return rc;
}

extern int mysql_db_commit(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;

	if (!mysql_conn->db_conn)
		return SLURM_ERROR;

	slurm_mutex_lock(&mysql_conn->lock);
	/* clear out the old results so we don't get a 2014 error */
	_clear_results(mysql_conn->db_conn);
	if (mysql_commit(mysql_conn->db_conn)) {
		error("mysql_commit failed: %d %s",
		      mysql_errno(mysql_conn->db_conn),
		      mysql_error(mysql_conn->db_conn));
		errno = mysql_errno(mysql_conn->db_conn);
		rc = SLURM_ERROR;
	}
	slurm_mutex_unlock(&mysql_conn->lock);
	return rc;
}

extern int mysql_db_rollback(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;

	if (!mysql_conn->db_conn)
		return SLURM_ERROR;

	slurm_mutex_lock(&mysql_conn->lock);
	/* clear out the old results so we don't get a 2014 error */
	_clear_results(mysql_conn->db_conn);
	if (mysql_rollback(mysql_conn->db_conn)) {
		error("mysql_commit failed: %d %s",
		      mysql_errno(mysql_conn->db_conn),
		      mysql_error(mysql_conn->db_conn));
		errno = mysql_errno(mysql_conn->db_conn);
		rc = SLURM_ERROR;
	}
	slurm_mutex_unlock(&mysql_conn->lock);
	return rc;

}

extern MYSQL_RES *mysql_db_query_ret(mysql_conn_t *mysql_conn,
				     char *query, bool last)
{
	MYSQL_RES *result = NULL;

	slurm_mutex_lock(&mysql_conn->lock);
	if (_mysql_query_internal(mysql_conn->db_conn, query) != SLURM_ERROR)  {
		if (mysql_errno(mysql_conn->db_conn) == ER_NO_SUCH_TABLE)
			goto fini;
		else if (last)
			result = _get_last_result(mysql_conn->db_conn);
		else
			result = _get_first_result(mysql_conn->db_conn);
		if (!result && mysql_field_count(mysql_conn->db_conn)) {
			/* should have returned data */
			error("We should have gotten a result: '%m' '%s'",
			      mysql_error(mysql_conn->db_conn));
		}
	}

fini:
	slurm_mutex_unlock(&mysql_conn->lock);
	return result;
}

extern int mysql_db_query_check_after(mysql_conn_t *mysql_conn, char *query)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&mysql_conn->lock);
	if ((rc = _mysql_query_internal(
		     mysql_conn->db_conn, query)) != SLURM_ERROR)
		rc = _clear_results(mysql_conn->db_conn);
	slurm_mutex_unlock(&mysql_conn->lock);
	return rc;
}

extern int mysql_db_insert_ret_id(mysql_conn_t *mysql_conn, char *query)
{
	int new_id = 0;

	slurm_mutex_lock(&mysql_conn->lock);
	if (_mysql_query_internal(mysql_conn->db_conn, query) != SLURM_ERROR)  {
		new_id = mysql_insert_id(mysql_conn->db_conn);
		if (!new_id) {
			/* should have new id */
			error("We should have gotten a new id: %s",
			      mysql_error(mysql_conn->db_conn));
		}
	}
	slurm_mutex_unlock(&mysql_conn->lock);
	return new_id;

}

extern int mysql_db_create_table(mysql_conn_t *mysql_conn, char *table_name,
				 storage_field_t *fields, char *ending)
{
	char *query = NULL;
	int i = 0, rc;
	storage_field_t *first_field = fields;

	if (!fields || !fields->name) {
		error("Not creating an empty table");
		return SLURM_ERROR;
	}

	/* We have an internal table called table_defs_table which
	 * contains the definition of each table in the database.  To
	 * speed things up we just check against that to see if
	 * anything has changed.
	 */
	query = xstrdup_printf("create table if not exists %s "
			       "(creation_time int unsigned not null, "
			       "mod_time int unsigned default 0 not null, "
			       "table_name text not null, "
			       "definition text not null, "
			       "primary key (table_name(50))) engine='innodb'",
			       table_defs_table);
	if (mysql_db_query(mysql_conn, query) == SLURM_ERROR) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	query = xstrdup_printf("create table if not exists %s (`%s` %s",
			       table_name, fields->name, fields->options);
	i = 1;
	fields++;

	while (fields && fields->name) {
		xstrfmtcat(query, ", `%s` %s", fields->name, fields->options);
		fields++;
		i++;
	}
	xstrcat(query, ending);

	/* make sure we can do a rollback */
	xstrcat(query, " engine='innodb'");

	if (mysql_db_query(mysql_conn, query) == SLURM_ERROR) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	rc = _mysql_make_table_current(
		mysql_conn, table_name, first_field, ending);
	return rc;
}
