/*****************************************************************************\
 *  as_mysql_tres.c - functions dealing with accounts.
 *****************************************************************************
 *
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

#include "as_mysql_tres.h"
#include "as_mysql_usage.h"
#include "src/common/xstring.h"

extern int as_mysql_add_tres(mysql_conn_t *mysql_conn,
			     uint32_t uid, List tres_list_in)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_tres_rec_t *object = NULL;
	char *cols = NULL, *extra = NULL, *vals = NULL, *query = NULL,
		*tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))
		return ESLURM_ACCESS_DENIED;

	if (!tres_list_in) {
		error("as_mysql_add_tres: Trying to add a blank list");
		return SLURM_ERROR;
	}

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(tres_list_in);
	while ((object = list_next(itr))) {
		if (!object->type || !object->type[0]) {
			error("We need a tres type.");
			rc = SLURM_ERROR;
			continue;
		} else if ((!xstrcasecmp(object->type, "gres") ||
			    !xstrcasecmp(object->type, "bb") ||
			    !xstrcasecmp(object->type, "license") ||
			    !xstrcasecmp(object->type, "fs") ||
			    !xstrcasecmp(object->type, "ic"))) {
			if (!object->name) {
				error("%s type tres "
				      "need to have a name, "
				      "(i.e. Gres/GPU).  You gave none",
				      object->type);
				rc = SLURM_ERROR;
				continue;
			}
		} else /* only the above have a name */
			xfree(object->name);

		xstrcat(cols, "creation_time, type");
		xstrfmtcat(vals, "%ld, '%s'", now, object->type);
		xstrfmtcat(extra, "type='%s'", object->type);
		if (object->name) {
			xstrcat(cols, ", name");
			xstrfmtcat(vals, ", '%s'", object->name);
			xstrfmtcat(extra, ", name='%s'", object->name);
		}

		xstrfmtcat(query,
			   "insert into %s (%s) values (%s) "
			   "on duplicate key update deleted=0;",
			   tres_table, cols, vals);

		if (debug_flags & DEBUG_FLAG_DB_TRES)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		object->id = (uint32_t)mysql_db_insert_ret_id(
			mysql_conn, query);
		xfree(query);
		if (!object->id) {
			error("Couldn't add tres %s%s%s", object->type,
			      object->name ? "/" : "",
			      object->name ? object->name : "");
			xfree(cols);
			xfree(extra);
			xfree(vals);
			break;
		}

		affect_rows = last_affected_rows(mysql_conn);

		if (!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(cols);
			xfree(extra);
			xfree(vals);
			continue;
		}

		tmp_extra = slurm_add_slash_to_quotes(extra);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info, cluster) "
			   "values (%ld, %u, 'id=%d', '%s', '%s', '%s');",
			   txn_table,
			   now, DBD_ADD_TRES, object->id, user_name,
			   tmp_extra, mysql_conn->cluster_name);

		xfree(tmp_extra);
		xfree(cols);
		xfree(extra);
		xfree(vals);
		debug4("query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_ADD_TRES,
					      object) == SLURM_SUCCESS)
				list_remove(itr);
		}

	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if (list_count(mysql_conn->update_list)) {
		/* We only want to update the local cache DBD or ctld */
		assoc_mgr_update(mysql_conn->update_list, 0);
		list_flush(mysql_conn->update_list);
	}

	return rc;
}

extern List as_mysql_get_tres(mysql_conn_t *mysql_conn, uid_t uid,
				slurmdb_tres_cond_t *tres_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List my_tres_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
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

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!tres_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if (tres_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");

	if (tres_cond->id_list
	    && list_count(tres_cond->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(tres_cond->id_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (tres_cond->type_list
	    && list_count(tres_cond->type_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(tres_cond->type_list);
		while ((object = list_next(itr))) {
			char *slash;
			if (set)
				xstrcat(extra, " || ");
			if (!(slash = strchr(object, '/')))
				xstrfmtcat(extra, "type='%s'", object);
			else {
				/* This means we have the name
				 * attached, so split the string and
				 * handle it this way, only on this type.
				 */
				char *name = slash;
				*slash = '\0';
				name++;
				xstrfmtcat(extra, "(type='%s' && name='%s')",
					   object, name);
			}
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (tres_cond->name_list
	    && list_count(tres_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(tres_cond->name_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

	xfree(tmp);
	xstrfmtcat(tmp, "%s", tres_req_inx[i]);
	for(i=1; i<SLURMDB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", tres_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s order by id",
			       tmp, tres_table, extra);
	xfree(tmp);
	xfree(extra);

	if (debug_flags & DEBUG_FLAG_DB_TRES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	my_tres_list = list_create(slurmdb_destroy_tres_rec);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_tres_rec_t *tres =
			xmalloc(sizeof(slurmdb_tres_rec_t));
		list_append(my_tres_list, tres);

		tres->id =  slurm_atoul(row[SLURMDB_REQ_ID]);
		if (row[SLURMDB_REQ_TYPE] && row[SLURMDB_REQ_TYPE][0])
			tres->type = xstrdup(row[SLURMDB_REQ_TYPE]);
		if (row[SLURMDB_REQ_NAME] && row[SLURMDB_REQ_NAME][0])
			tres->name = xstrdup(row[SLURMDB_REQ_NAME]);
	}
	mysql_free_result(result);

	return my_tres_list;
}
