/*****************************************************************************\
 *  mysql_user.c - functions dealing with users and coordinators.
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

#include "mysql_assoc.h"
#include "mysql_user.h"
#include "mysql_wckey.h"

/* Fill in all the accounts this user is coordinator over.  This
 * will fill in all the sub accounts they are coordinator over also.
 */
static int _get_user_coords(mysql_conn_t *mysql_conn, acct_user_rec_t *user)
{
	char *query = NULL;
	acct_coord_rec_t *coord = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	ListIterator itr = NULL;

	if(!user) {
		error("We need a user to fill in.");
		return SLURM_ERROR;
	}

	if(!user->coord_accts)
		user->coord_accts = list_create(destroy_acct_coord_rec);

	query = xstrdup_printf(
		"select acct from %s where user=\"%s\" && deleted=0",
		acct_coord_table, user->name);

	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	while((row = mysql_fetch_row(result))) {
		coord = xmalloc(sizeof(acct_coord_rec_t));
		list_append(user->coord_accts, coord);
		coord->name = xstrdup(row[0]);
		coord->direct = 1;
		if(query)
			xstrcat(query, " || ");
		else
			query = xstrdup_printf(
				"select distinct t1.acct from "
				"%s as t1, %s as t2 where t1.deleted=0 && ",
				assoc_table, assoc_table);
		/* Make sure we don't get the same
		 * account back since we want to keep
		 * track of the sub-accounts.
		 */
		xstrfmtcat(query, "(t2.acct=\"%s\" "
			   "&& t1.lft between t2.lft "
			   "and t2.rgt && t1.user='' "
			   "&& t1.acct!=\"%s\")",
			   coord->name, coord->name);
	}
	mysql_free_result(result);

	if(query) {
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);

		itr = list_iterator_create(user->coord_accts);
		while((row = mysql_fetch_row(result))) {

			while((coord = list_next(itr))) {
				if(!strcmp(coord->name, row[0]))
					break;
			}
			list_iterator_reset(itr);
			if(coord)
				continue;

			coord = xmalloc(sizeof(acct_coord_rec_t));
			list_append(user->coord_accts, coord);
			coord->name = xstrdup(row[0]);
			coord->direct = 0;
		}
		list_iterator_destroy(itr);
		mysql_free_result(result);
	}
	return SLURM_SUCCESS;
}

extern int mysql_add_users(mysql_conn_t *mysql_conn, uint32_t uid,
			   List user_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_user_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *query = NULL, *txn_query = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *extra = NULL, *tmp_extra = NULL;
	int affect_rows = 0;
	List assoc_list = list_create(destroy_acct_association_rec);
	List wckey_list = list_create(destroy_acct_wckey_rec);

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(user_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->name[0]
		   || !object->default_acct || !object->default_acct[0]) {
			error("We need a user name and "
			      "default acct to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name, default_acct");
		xstrfmtcat(vals, "%d, %d, '%s', '%s'",
			   now, now, object->name, object->default_acct);
		xstrfmtcat(extra, ", default_acct='%s'",
			   object->default_acct);

		if(object->admin_level != ACCT_ADMIN_NOTSET) {
			xstrcat(cols, ", admin_level");
			xstrfmtcat(vals, ", %u", object->admin_level);
			xstrfmtcat(extra, ", admin_level=%u",
				   object->admin_level);
		}

		if(object->default_wckey) {
			xstrcat(cols, ", default_wckey");
			xstrfmtcat(vals, ", \"%s\"", object->default_wckey);
			xstrfmtcat(extra, ", default_wckey=\"%s\"",
				   object->default_wckey);
		}

		query = xstrdup_printf(
			"insert into %s (%s) values (%s) "
			"on duplicate key update deleted=0, mod_time=%d %s;",
			user_table, cols, vals,
			now, extra);

		xfree(cols);
		xfree(vals);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add user %s", object->name);
			xfree(extra);
			continue;
		}

		affect_rows = last_affected_rows(mysql_conn->db_conn);
		if(!affect_rows) {
			debug("nothing changed");
			xfree(extra);
			continue;
		}

		if(addto_update_list(mysql_conn->update_list, ACCT_ADD_USER,
				      object) == SLURM_SUCCESS)
			list_remove(itr);

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = fix_double_quotes(extra+2);

		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%d, %u, \"%s\", \"%s\", \"%s\")",
				   now, DBD_ADD_USERS, object->name,
				   user_name, tmp_extra);
		else
			xstrfmtcat(txn_query,
				   "insert into %s "
				   "(timestamp, action, name, actor, info) "
				   "values (%d, %u, \"%s\", \"%s\", \"%s\")",
				   txn_table,
				   now, DBD_ADD_USERS, object->name,
				   user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);

		if(object->assoc_list)
			list_transfer(assoc_list, object->assoc_list);

		if(object->wckey_list)
			list_transfer(wckey_list, object->wckey_list);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(rc != SLURM_ERROR) {
		if(txn_query) {
			xstrcat(txn_query, ";");
			rc = mysql_db_query(mysql_conn->db_conn,
					    txn_query);
			xfree(txn_query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add txn");
				rc = SLURM_SUCCESS;
			}
		}
	} else
		xfree(txn_query);

	if(list_count(assoc_list)) {
		if(mysql_add_assocs(mysql_conn, uid, assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding user associations");
			rc = SLURM_ERROR;
		}
	}
	list_destroy(assoc_list);

	if(list_count(wckey_list)) {
		if(mysql_add_wckeys(mysql_conn, uid, wckey_list)
		   == SLURM_ERROR) {
			error("Problem adding user wckeys");
			rc = SLURM_ERROR;
		}
	}
	list_destroy(wckey_list);

	return rc;
}

extern int mysql_add_coord(mysql_conn_t *mysql_conn, uint32_t uid,
			   List acct_list, acct_user_cond_t *user_cond)
{
	char *query = NULL, *user = NULL, *acct = NULL;
	char *user_name = NULL, *txn_query = NULL;
	ListIterator itr, itr2;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;
	acct_user_rec_t *user_rec = NULL;

	if(!user_cond || !user_cond->assoc_cond
	   || !user_cond->assoc_cond->user_list
	   || !list_count(user_cond->assoc_cond->user_list)
	   || !acct_list || !list_count(acct_list)) {
		error("we need something to add");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(user_cond->assoc_cond->user_list);
	itr2 = list_iterator_create(acct_list);
	while((user = list_next(itr))) {
		if(!user[0])
			continue;
		while((acct = list_next(itr2))) {
			if(!acct[0])
				continue;
			if(query)
				xstrfmtcat(query, ", (%d, %d, \"%s\", \"%s\")",
					   now, now, acct, user);
			else
				query = xstrdup_printf(
					"insert into %s (creation_time, "
					"mod_time, acct, user) values "
					"(%d, %d, \"%s\", \"%s\")",
					acct_coord_table,
					now, now, acct, user);

			if(txn_query)
				xstrfmtcat(txn_query,
					   ", (%d, %u, \"%s\", \"%s\", \"%s\")",
					   now, DBD_ADD_ACCOUNT_COORDS, user,
					   user_name, acct);
			else
				xstrfmtcat(txn_query,
					   "insert into %s "
					   "(timestamp, action, name, "
					   "actor, info) "
					   "values (%d, %u, \"%s\", "
					   "\"%s\", \"%s\")",
					   txn_table,
					   now, DBD_ADD_ACCOUNT_COORDS, user,
					   user_name, acct);
		}
		list_iterator_reset(itr2);
	}
	xfree(user_name);
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	if(query) {
		xstrfmtcat(query,
			   " on duplicate key update mod_time=%d, deleted=0;%s",
			   now, txn_query);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		xfree(txn_query);

		if(rc != SLURM_SUCCESS) {
			error("Couldn't add cluster hour rollup");
			return rc;
		}
		/* get the update list set */
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((user = list_next(itr))) {
			user_rec = xmalloc(sizeof(acct_user_rec_t));
			user_rec->name = xstrdup(user);
			_get_user_coords(mysql_conn, user_rec);
			addto_update_list(mysql_conn->update_list,
					   ACCT_ADD_COORD, user_rec);
		}
		list_iterator_destroy(itr);
	}

	return SLURM_SUCCESS;
}

extern List mysql_modify_users(mysql_conn_t *mysql_conn, uint32_t uid,
			       acct_user_cond_t *user_cond,
			       acct_user_rec_t *user)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!user_cond || !user) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(user_cond->assoc_cond && user_cond->assoc_cond->user_list
	   && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_acct_list && list_count(user_cond->def_acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_acct_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_wckey_list && list_count(user_cond->def_wckey_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_wckey_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_wckey=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->admin_level != ACCT_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u", user_cond->admin_level);
	}

	if(user->default_acct)
		xstrfmtcat(vals, ", default_acct=\"%s\"", user->default_acct);

	if(user->default_wckey)
		xstrfmtcat(vals, ", default_wckey=\"%s\"", user->default_wckey);

	if(user->admin_level != ACCT_ADMIN_NOTSET)
		xstrfmtcat(vals, ", admin_level=%u", user->admin_level);

	if(!extra || !vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}
	query = xstrdup_printf("select name from %s %s;",
			       user_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		acct_user_rec_t *user_rec = NULL;

		object = xstrdup(row[0]);
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "(name=\"%s\"", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name=\"%s\"", object);
		}
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(object);
		user_rec->default_acct = xstrdup(user->default_acct);
		user_rec->default_wckey = xstrdup(user->default_wckey);
		user_rec->admin_level = user->admin_level;
		addto_update_list(mysql_conn->update_list, ACCT_MODIFY_USER,
				   user_rec);
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(vals);
		xfree(query);
		return ret_list;
	}
	xfree(query);
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = modify_common(mysql_conn, DBD_MODIFY_USERS, now,
			    user_name, user_table, name_char, vals);
	xfree(user_name);
	xfree(name_char);
	xfree(vals);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify users");
		list_destroy(ret_list);
		ret_list = NULL;
	}

	return ret_list;
}

extern List mysql_remove_users(mysql_conn_t *mysql_conn, uint32_t uid,
			       acct_user_cond_t *user_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	List coord_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	acct_user_cond_t user_coord_cond;
	acct_association_cond_t assoc_cond;
	acct_wckey_cond_t wckey_cond;

	if(!user_cond) {
		error("we need something to remove");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");

	if(user_cond->assoc_cond && user_cond->assoc_cond->user_list
	   && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_acct_list && list_count(user_cond->def_acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_acct_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_wckey_list && list_count(user_cond->def_wckey_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_wckey_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_wckey=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->admin_level != ACCT_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u", user_cond->admin_level);
	}

	if(!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select name from %s %s;", user_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	memset(&user_coord_cond, 0, sizeof(acct_user_cond_t));
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	/* we do not need to free the objects we put in here since
	   they are also placed in a list that will be freed
	*/
	assoc_cond.user_list = list_create(NULL);
	user_coord_cond.assoc_cond = &assoc_cond;

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		acct_user_rec_t *user_rec = NULL;

		list_append(ret_list, object);
		list_append(assoc_cond.user_list, object);

		if(!rc) {
			xstrfmtcat(name_char, "name=\"%s\"", object);
			xstrfmtcat(assoc_char, "t2.user=\"%s\"", object);
			rc = 1;
		} else {
			xstrfmtcat(name_char, " || name=\"%s\"", object);
			xstrfmtcat(assoc_char, " || t2.user=\"%s\"", object);
		}
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(object);
		addto_update_list(mysql_conn->update_list, ACCT_REMOVE_USER,
				   user_rec);

	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		list_destroy(assoc_cond.user_list);
		return ret_list;
	}
	xfree(query);

	/* We need to remove these accounts from the coord's that have it */
	coord_list = mysql_remove_coord(
		mysql_conn, uid, NULL, &user_coord_cond);
	if(coord_list)
		list_destroy(coord_list);

	/* We need to remove these users from the wckey table */
	memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
	wckey_cond.user_list = assoc_cond.user_list;
	coord_list = mysql_remove_wckeys(
		mysql_conn, uid, &wckey_cond);
	if(coord_list)
		list_destroy(coord_list);

	list_destroy(assoc_cond.user_list);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_USERS, now,
			    user_name, user_table, name_char, assoc_char);
	xfree(user_name);
	xfree(name_char);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		xfree(assoc_char);
		return NULL;
	}

	query = xstrdup_printf(
		"update %s as t2 set deleted=1, mod_time=%d where %s",
		acct_coord_table, now, assoc_char);
	xfree(assoc_char);

	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't remove user coordinators");
		list_destroy(ret_list);
		return NULL;
	}

	return ret_list;
}

extern List mysql_remove_coord(mysql_conn_t *mysql_conn, uint32_t uid,
			       List acct_list, acct_user_cond_t *user_cond)
{
	char *query = NULL, *object = NULL, *extra = NULL, *last_user = NULL;
	char *user_name = NULL;
	time_t now = time(NULL);
	int set = 0, is_admin=0, rc;
	ListIterator itr = NULL;
	acct_user_rec_t *user_rec = NULL;
	List ret_list = NULL;
	List user_list = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	acct_user_rec_t user;

	if(!user_cond && !acct_list) {
		error("we need something to remove");
		return NULL;
	} else if(user_cond && user_cond->assoc_cond)
		user_list = user_cond->assoc_cond->user_list;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	/* This only works when running though the slurmdbd.
	 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
	 * SLURMDBD!
	 */
	if(slurmdbd_conf) {
		/* we have to check the authentication here in the
		 * plugin since we don't know what accounts are being
		 * referenced until after the query.  Here we will
		 * set if they are an operator or greater and then
		 * check it below after the query.
		 */
		if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
		   || assoc_mgr_get_admin_level(mysql_conn, uid)
		   >= ACCT_ADMIN_OPERATOR)
			is_admin = 1;
		else {
			if(assoc_mgr_fill_in_user(mysql_conn, &user, 1, NULL)
			   != SLURM_SUCCESS) {
				error("couldn't get information for this user");
				errno = SLURM_ERROR;
				return NULL;
			}
			if(!user.coord_accts || !list_count(user.coord_accts)) {
				error("This user doesn't have any "
				      "coordinator abilities");
				errno = ESLURM_ACCESS_DENIED;
				return NULL;
			}
		}
	} else {
		/* Setting this here just makes it easier down below
		 * since user will not be filled in.
		 */
		is_admin = 1;
	}

	/* Leave it this way since we are using extra below */

	if(user_list && list_count(user_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, "(");

		itr = list_iterator_create(user_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "user=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_list && list_count(acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, "(");

		itr = list_iterator_create(acct_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "acct=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(!extra) {
		errno = SLURM_ERROR;
		debug3("No conditions given");
		return NULL;
	}

	query = xstrdup_printf(
		"select user, acct from %s where deleted=0 && %s order by user",
		acct_coord_table, extra);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		xfree(extra);
		errno = SLURM_ERROR;
		return NULL;
	}
	xfree(query);
	ret_list = list_create(slurm_destroy_char);
	user_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		if(!is_admin) {
			acct_coord_rec_t *coord = NULL;
			if(!user.coord_accts) { // This should never
						// happen
				error("We are here with no coord accts");
				errno = ESLURM_ACCESS_DENIED;
				list_destroy(ret_list);
				list_destroy(user_list);
				xfree(extra);
				mysql_free_result(result);
				return NULL;
			}
			itr = list_iterator_create(user.coord_accts);
			while((coord = list_next(itr))) {
				if(!strcasecmp(coord->name, row[1]))
					break;
			}
			list_iterator_destroy(itr);

			if(!coord) {
				error("User %s(%d) does not have the "
				      "ability to change this account (%s)",
				      user.name, user.uid, row[1]);
				errno = ESLURM_ACCESS_DENIED;
				list_destroy(ret_list);
				list_destroy(user_list);
				xfree(extra);
				mysql_free_result(result);
				return NULL;
			}
		}
		if(!last_user || strcasecmp(last_user, row[0])) {
			list_append(user_list, xstrdup(row[0]));
			last_user = row[0];
		}
		list_append(ret_list, xstrdup_printf("U = %-9s A = %-10s",
						     row[0], row[1]));
	}
	mysql_free_result(result);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_ACCOUNT_COORDS, now,
			    user_name, acct_coord_table, extra, NULL);
	xfree(user_name);
	xfree(extra);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		list_destroy(user_list);
		errno = SLURM_ERROR;
		return NULL;
	}

	/* get the update list set */
	itr = list_iterator_create(user_list);
	while((last_user = list_next(itr))) {
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(last_user);
		_get_user_coords(mysql_conn, user_rec);
		addto_update_list(mysql_conn->update_list,
				   ACCT_REMOVE_COORD, user_rec);
	}
	list_iterator_destroy(itr);
	list_destroy(user_list);

	return ret_list;
}

extern List mysql_get_users(mysql_conn_t *mysql_conn, uid_t uid,
			    acct_user_cond_t *user_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List user_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint16_t private_data = 0;
	acct_user_rec_t user;

	/* if this changes you will need to edit the corresponding enum */
	char *user_req_inx[] = {
		"name",
		"default_acct",
		"default_wckey",
		"admin_level"
	};
	enum {
		USER_REQ_NAME,
		USER_REQ_DA,
		USER_REQ_DW,
		USER_REQ_AL,
		USER_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USERS) {
		/* This only works when running though the slurmdbd.
		 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
		 * SLURMDBD!
		 */
		if(slurmdbd_conf) {
			is_admin = 0;
			/* we have to check the authentication here in the
			 * plugin since we don't know what accounts are being
			 * referenced until after the query.  Here we will
			 * set if they are an operator or greater and then
			 * check it below after the query.
			 */
			if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
			   || assoc_mgr_get_admin_level(mysql_conn, uid)
			   >= ACCT_ADMIN_OPERATOR)
				is_admin = 1;
			else {
				assoc_mgr_fill_in_user(mysql_conn, &user, 1,
						       NULL);
			}
		}
	}

	if(!user_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if(user_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");


	if(user_cond->assoc_cond &&
	   user_cond->assoc_cond->user_list
	   && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_acct_list && list_count(user_cond->def_acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_acct_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_wckey_list && list_count(user_cond->def_wckey_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_wckey_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_wckey=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->admin_level != ACCT_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u",
			   user_cond->admin_level);
	}
empty:
	/* This is here to make sure we are looking at only this user
	 * if this flag is set.
	 */
	if(!is_admin && (private_data & PRIVATE_DATA_USERS)) {
		xstrfmtcat(extra, " && name=\"%s\"", user.name);
	}

	xfree(tmp);
	xstrfmtcat(tmp, "%s", user_req_inx[i]);
	for(i=1; i<USER_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", user_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s", tmp, user_table, extra);
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

	user_list = list_create(destroy_acct_user_rec);

	while((row = mysql_fetch_row(result))) {
		acct_user_rec_t *user = xmalloc(sizeof(acct_user_rec_t));
/* 		uid_t pw_uid; */
		list_append(user_list, user);

		user->name =  xstrdup(row[USER_REQ_NAME]);
		user->default_acct = xstrdup(row[USER_REQ_DA]);
		if(row[USER_REQ_DW])
			user->default_wckey = xstrdup(row[USER_REQ_DW]);
		else
			user->default_wckey = xstrdup("");

		user->admin_level = atoi(row[USER_REQ_AL]);

		/* user id will be set on the client since this could be on a
		 * different machine where this user may not exist or
		 * may have a different uid
		 */
/* 		if (uid_from_string (user->name, &pw_uid) < 0)  */
/* 			user->uid = (uint32_t)NO_VAL; */
/* 		else */
/* 			user->uid = passwd_ptr->pw_uid; */

		if(user_cond && user_cond->with_coords)
			_get_user_coords(mysql_conn, user);
	}
	mysql_free_result(result);

	if(user_cond && user_cond->with_assocs) {
		ListIterator assoc_itr = NULL;
		acct_user_rec_t *user = NULL;
		acct_association_rec_t *assoc = NULL;
		List assoc_list = NULL;

		/* Make sure we don't get any non-user associations
		 * this is done by at least having a user_list
		 * defined */
		if(!user_cond->assoc_cond)
			user_cond->assoc_cond =
				xmalloc(sizeof(acct_association_cond_t));

		if(!user_cond->assoc_cond->user_list)
			user_cond->assoc_cond->user_list = list_create(NULL);

		assoc_list = mysql_get_assocs(
			mysql_conn, uid, user_cond->assoc_cond);

		if(!assoc_list) {
			error("no associations");
			goto get_wckeys;
		}

		itr = list_iterator_create(user_list);
		assoc_itr = list_iterator_create(assoc_list);
		while((user = list_next(itr))) {
			while((assoc = list_next(assoc_itr))) {
				if(strcmp(assoc->user, user->name))
					continue;

				if(!user->assoc_list)
					user->assoc_list = list_create(
						destroy_acct_association_rec);
				list_append(user->assoc_list, assoc);
				list_remove(assoc_itr);
			}
			list_iterator_reset(assoc_itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(assoc_itr);

		list_destroy(assoc_list);
	}

get_wckeys:
	if(user_cond && user_cond->with_wckeys) {
		ListIterator wckey_itr = NULL;
		acct_user_rec_t *user = NULL;
		acct_wckey_rec_t *wckey = NULL;
		List wckey_list = NULL;
		acct_wckey_cond_t wckey_cond;

		memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
		if(user_cond->assoc_cond) {
			wckey_cond.user_list =
				user_cond->assoc_cond->user_list;
			wckey_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
		}
		wckey_list = mysql_get_wckeys(
			mysql_conn, uid, &wckey_cond);

		if(!wckey_list) {
			error("no wckeys");
			return user_list;
		}

		itr = list_iterator_create(user_list);
		wckey_itr = list_iterator_create(wckey_list);
		while((user = list_next(itr))) {
			while((wckey = list_next(wckey_itr))) {
				if(strcmp(wckey->user, user->name))
					continue;

				if(!user->wckey_list)
					user->wckey_list = list_create(
						destroy_acct_wckey_rec);
				list_append(user->wckey_list, wckey);
				list_remove(wckey_itr);
			}
			list_iterator_reset(wckey_itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(wckey_itr);

		list_destroy(wckey_list);
	}

	return user_list;
}
