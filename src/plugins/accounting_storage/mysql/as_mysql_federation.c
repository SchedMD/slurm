/*****************************************************************************\
 *  as_mysql_federation.c - functions dealing with federations.
 *****************************************************************************
 *
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#include "as_mysql_federation.h"

static int _setup_federation_cond_limits(slurmdb_federation_cond_t *fed_cond,
					 char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;

	if (!fed_cond)
		return 0;

	if (fed_cond->with_deleted)
		xstrcat(*extra, " where (deleted=0 || deleted=1)");
	else
		xstrcat(*extra, " where deleted=0");

	if (fed_cond->federation_list
	    && list_count(fed_cond->federation_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(fed_cond->federation_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	return set;
}

extern int as_mysql_add_federations(mysql_conn_t *mysql_conn, uint32_t uid,
				    List federation_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_federation_rec_t *object = NULL;
	char *extra = NULL,
		*query = NULL, *tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_SUPER_USER))
		return ESLURM_ACCESS_DENIED;

	user_name = uid_to_string((uid_t) uid);

	itr = list_iterator_create(federation_list);
	while ((object = list_next(itr))) {
		xstrfmtcat(extra, ", mod_time=%ld", now);
		xstrfmtcat(query,
			   "insert into %s (creation_time, mod_time, name) "
			   "values (%ld, %ld, '%s') "
			   "on duplicate key update deleted=0, mod_time=%ld, "
			   "flags=0",
			   federation_table,
			   now, now, object->name, now);
		if (debug_flags & DEBUG_FLAG_DB_FEDR)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add federation %s", object->name);
			xfree(extra);
			added = 0;
			break;
		}

		affect_rows = last_affected_rows(mysql_conn);

		if (!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(extra);
			continue;
		}

		/* Add Transaction */
		/* we always have a ', ' as the first 2 chars */
		tmp_extra = slurm_add_slash_to_quotes(extra+2);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%ld, %u, '%s', '%s', '%s');",
			   txn_table, now, DBD_ADD_FEDERATIONS,
			   object->name, user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);
		debug4("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			added++;
		}
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if (!added)
		reset_mysql_conn(mysql_conn);

	return rc;
}

extern List as_mysql_get_federations(mysql_conn_t *mysql_conn, uid_t uid,
				     slurmdb_federation_cond_t *federation_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List federation_list = NULL;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	slurmdb_federation_rec_t *federation = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *federation_req_inx[] = {
		"name",
		"flags",
	};
	enum {
		FEDERATION_REQ_NAME,
		FEDERATION_REQ_FLAGS,
		FEDERATION_REQ_COUNT
	};

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;


	if (!federation_cond) {
		xstrcat(extra, " where deleted=0");
		goto empty;
	}

	_setup_federation_cond_limits(federation_cond, &extra);

empty:

	xfree(tmp);
	i=0;
	xstrfmtcat(tmp, "%s", federation_req_inx[i]);
	for(i=1; i<FEDERATION_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", federation_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s%s",
			       tmp, federation_table, extra);
	xfree(tmp);
	xfree(extra);

	if (debug_flags & DEBUG_FLAG_DB_FEDR)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	federation_list = list_create(slurmdb_destroy_federation_rec);

	while ((row = mysql_fetch_row(result))) {
		federation = xmalloc(sizeof(slurmdb_federation_rec_t));
		list_append(federation_list, federation);

		federation->name = xstrdup(row[FEDERATION_REQ_NAME]);
		federation->flags = slurm_atoul(row[FEDERATION_REQ_FLAGS]);
	}
	mysql_free_result(result);

	return federation_list;
}
