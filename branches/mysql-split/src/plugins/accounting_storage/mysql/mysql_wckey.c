/*****************************************************************************\
 *  mysql_wckey.c - functions dealing with the wckey.
 *****************************************************************************
 *
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
\*****************************************************************************/

#include "mysql_wckey.h"
#include "mysql_usage.h"

/* when doing a select on this all the select should have a prefix of
 * t1. */
static int _setup_wckey_cond_limits(acct_wckey_cond_t *wckey_cond, char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	char *prefix = "t1";
	if(!wckey_cond)
		return 0;

	if(wckey_cond->with_deleted)
		xstrfmtcat(*extra, " where (%s.deleted=0 || %s.deleted=1)",
			prefix, prefix);
	else
		xstrfmtcat(*extra, " where %s.deleted=0", prefix);

	if(wckey_cond->name_list && list_count(wckey_cond->name_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(wckey_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.name=\"%s\"", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(wckey_cond->cluster_list && list_count(wckey_cond->cluster_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(wckey_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.cluster=\"%s\"", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(wckey_cond->id_list && list_count(wckey_cond->id_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(wckey_cond->id_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.id=%s", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(wckey_cond->user_list && list_count(wckey_cond->user_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(wckey_cond->user_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.user=\"%s\"", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	return set;
}

/* extern functions */

extern int mysql_add_wckeys(mysql_conn_t *mysql_conn, uint32_t uid,
			    List wckey_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_wckey_rec_t *object = NULL;
	char *cols = NULL, *extra = NULL, *vals = NULL, *query = NULL,
		*tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(wckey_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->cluster[0]
		   || !object->user || !object->user[0]) {
			error("We need a wckey name, cluster, "
			      "and user to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, cluster, user");
		xstrfmtcat(vals, "%d, %d, \"%s\", \"%s\"",
			   now, now, object->cluster, object->user);
		xstrfmtcat(extra, ", mod_time=%d, cluster=\"%s\", user=\"%s\"",
			   now, object->cluster, object->user);

		if(object->name) {
			xstrcat(cols, ", name");
			xstrfmtcat(vals, ", \"%s\"", object->name);
			xstrfmtcat(extra, ", name=\"%s\"", object->name);
		}

		xstrfmtcat(query,
			   "insert into %s (%s) values (%s) "
			   "on duplicate key update deleted=0, "
			   "id=LAST_INSERT_ID(id)%s;",
			   wckey_table, cols, vals, extra);

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		object->id = mysql_insert_ret_id(mysql_conn->db_conn, query);
		xfree(query);
		if(!object->id) {
			error("Couldn't add wckey %s", object->name);
			added=0;
			xfree(cols);
			xfree(extra);
			xfree(vals);
			break;
		}

		affect_rows = last_affected_rows(mysql_conn->db_conn);

		if(!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(cols);
			xfree(extra);
			xfree(vals);
			continue;
		}

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = fix_double_quotes(extra+2);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %u, '%d', \"%s\", \"%s\");",
			   txn_table,
			   now, DBD_ADD_WCKEYS, object->id, user_name,
			   tmp_extra);

		xfree(tmp_extra);
		xfree(cols);
		xfree(extra);
		xfree(vals);
		debug4("query\n%s",query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if(addto_update_list(mysql_conn->update_list,
					      ACCT_ADD_WCKEY,
					      object) == SLURM_SUCCESS)
				list_remove(itr);
			added++;
		}

	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(!added) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
	}

	return rc;
}

extern List mysql_modify_wckeys(mysql_conn_t *mysql_conn,
				uint32_t uid,
				acct_wckey_cond_t *wckey_cond,
				acct_wckey_rec_t *wckey)
{
	return NULL;
}

extern List mysql_remove_wckeys(mysql_conn_t *mysql_conn,
				uint32_t uid,
				acct_wckey_cond_t *wckey_cond)
{
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!wckey_cond) {
		xstrcat(extra, " where deleted=0");
		goto empty;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	set = _setup_wckey_cond_limits(wckey_cond, &extra);

empty:
	if(!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select t1.id, t1.name from %s as t1%s;",
			       wckey_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	name_char = NULL;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		acct_wckey_rec_t *wckey_rec = NULL;

		list_append(ret_list, xstrdup(row[1]));
		if(!name_char)
			xstrfmtcat(name_char, "id=\"%s\"", row[0]);
		else
			xstrfmtcat(name_char, " || id=\"%s\"", row[0]);
		if(!assoc_char)
			xstrfmtcat(assoc_char, "wckeyid=\"%s\"", row[0]);
		else
			xstrfmtcat(assoc_char, " || wckeyid=\"%s\"", row[0]);

		wckey_rec = xmalloc(sizeof(acct_wckey_rec_t));
		/* we only need id when removing no real need to init */
		wckey_rec->id = atoi(row[0]);
		addto_update_list(mysql_conn->update_list, ACCT_REMOVE_WCKEY,
				  wckey_rec);
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		xfree(assoc_char);
		return ret_list;
	}
	xfree(query);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_WCKEYS, now,
			   user_name, wckey_table, name_char, assoc_char);
	xfree(assoc_char);
	xfree(name_char);
	xfree(user_name);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}

	return ret_list;
}

extern List mysql_get_wckeys(mysql_conn_t *mysql_conn, uid_t uid,
			     acct_wckey_cond_t *wckey_cond)
{
	//DEF_TIMERS;
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List wckey_list = NULL;
	int set = 0;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint16_t private_data = 0;
	acct_user_rec_t user;

	/* needed if we don't have an wckey_cond */
	uint16_t with_usage = 0;

	/* if this changes you will need to edit the corresponding enum */
	char *wckey_req_inx[] = {
		"id",
		"name",
		"user",
		"cluster",
	};

	enum {
		WCKEY_REQ_ID,
		WCKEY_REQ_NAME,
		WCKEY_REQ_USER,
		WCKEY_REQ_CLUSTER,
		WCKEY_REQ_COUNT
	};

	if(!wckey_cond) {
		xstrcat(extra, " where deleted=0");
		goto empty;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USERS) {
		if(!(is_admin = is_user_min_admin_level(
			     mysql_conn, uid, ACCT_ADMIN_OPERATOR)))
			is_user_any_coord(mysql_conn, &user);
	}

	set = _setup_wckey_cond_limits(wckey_cond, &extra);

	with_usage = wckey_cond->with_usage;

empty:
	xfree(tmp);
	xstrfmtcat(tmp, "t1.%s", wckey_req_inx[i]);
	for(i=1; i<WCKEY_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", t1.%s", wckey_req_inx[i]);
	}

	/* this is here to make sure we are looking at only this user
	 * if this flag is set.  We also include any accounts they may be
	 * coordinator of.
	 */
	if(!is_admin && (private_data & PRIVATE_DATA_USERS))
		xstrfmtcat(extra, " && t1.user='%s'", user.name);

	//START_TIMER;
	query = xstrdup_printf("select distinct %s from %s as t1%s "
			       "order by name, cluster, user;",
			       tmp, wckey_table, extra);
	xfree(tmp);
	xfree(extra);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	wckey_list = list_create(destroy_acct_wckey_rec);

	while((row = mysql_fetch_row(result))) {
		acct_wckey_rec_t *wckey = xmalloc(sizeof(acct_wckey_rec_t));
		list_append(wckey_list, wckey);

		wckey->id = atoi(row[WCKEY_REQ_ID]);
		wckey->user = xstrdup(row[WCKEY_REQ_USER]);

		/* we want a blank wckey if the name is null */
		if(row[WCKEY_REQ_NAME])
			wckey->name = xstrdup(row[WCKEY_REQ_NAME]);
		else
			wckey->name = xstrdup("");

		wckey->cluster = xstrdup(row[WCKEY_REQ_CLUSTER]);
	}
	mysql_free_result(result);

	if(with_usage && wckey_list)
		get_usage_for_list(mysql_conn, DBD_GET_WCKEY_USAGE,
				   wckey_list, wckey_cond->usage_start,
				   wckey_cond->usage_end);
	

	//END_TIMER2("get_wckeys");
	return wckey_list;
}
