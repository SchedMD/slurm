/*****************************************************************************\
 *  as_mysql_user.c - functions dealing with users and coordinators.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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
\*****************************************************************************/

#include "as_mysql_assoc.h"
#include "as_mysql_user.h"
#include "as_mysql_wckey.h"

static int _change_user_name(mysql_conn_t *mysql_conn, slurmdb_user_rec_t *user)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	ListIterator itr = NULL;
	char *cluster_name = NULL;

	xassert(user->old_name);
	xassert(user->name);

	slurm_mutex_lock(&as_mysql_cluster_list_lock);
	itr = list_iterator_create(as_mysql_cluster_list);
	while ((cluster_name = list_next(itr))) {
		// Change assoc_tables
		xstrfmtcat(query, "update \"%s_%s\" set user='%s' "
			   "where user='%s';", cluster_name, assoc_table,
			   user->name, user->old_name);
		// Change wckey_tables
		xstrfmtcat(query, "update \"%s_%s\" set user='%s' "
			   "where user='%s';", cluster_name, wckey_table,
			   user->name, user->old_name);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&as_mysql_cluster_list_lock);
	// Change coord_tables
	xstrfmtcat(query, "update %s set user='%s' where user='%s';",
		   acct_coord_table, user->name, user->old_name);

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	if (rc != SLURM_SUCCESS)
		reset_mysql_conn(mysql_conn);

	return rc;
}

static List _get_other_user_names_to_mod(mysql_conn_t *mysql_conn, uint32_t uid,
					 slurmdb_user_cond_t *user_cond)
{
	List tmp_list = NULL;
	List ret_list = NULL;
	ListIterator itr = NULL;

	slurmdb_association_cond_t assoc_cond;
	slurmdb_wckey_cond_t wckey_cond;

	if (!user_cond->def_acct_list || !list_count(user_cond->def_acct_list))
		goto no_assocs;

	/* We have to use a different association_cond here because
	   other things could be set here we don't care about in the
	   user's. (So to be safe just move over the info we care about) */
	memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
	assoc_cond.acct_list = user_cond->def_acct_list;
	if (user_cond->assoc_cond) {
		if (user_cond->assoc_cond->cluster_list)
			assoc_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
		if (user_cond->assoc_cond->user_list)
			assoc_cond.user_list = user_cond->assoc_cond->user_list;
	}
	assoc_cond.only_defs = 1;
	tmp_list = as_mysql_get_assocs(mysql_conn, uid, &assoc_cond);
	if (tmp_list) {
		slurmdb_association_rec_t *object = NULL;
		itr = list_iterator_create(tmp_list);
		while ((object = list_next(itr))) {
			if (!ret_list)
				ret_list = list_create(slurm_destroy_char);
			slurm_addto_char_list(ret_list, object->user);
		}
		list_iterator_destroy(itr);
		list_destroy(tmp_list);
		tmp_list = NULL;
	}

no_assocs:
	if (!user_cond->def_wckey_list
	    || !list_count(user_cond->def_wckey_list))
		goto no_wckeys;

	memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
	if (user_cond->assoc_cond) {
		if (user_cond->assoc_cond->cluster_list)
			wckey_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
		if (user_cond->assoc_cond->user_list)
			wckey_cond.user_list = user_cond->assoc_cond->user_list;
	}
	wckey_cond.name_list = user_cond->def_wckey_list;

	tmp_list = as_mysql_get_wckeys(mysql_conn, uid, &wckey_cond);
	if (tmp_list) {
		slurmdb_wckey_rec_t *object = NULL;
		itr = list_iterator_create(tmp_list);
		while ((object = list_next(itr))) {
			if (!ret_list)
				ret_list = list_create(slurm_destroy_char);
			slurm_addto_char_list(ret_list, object->user);
		}
		list_iterator_destroy(itr);
		list_destroy(tmp_list);
		tmp_list = NULL;
	}

no_wckeys:

	return ret_list;
}

/* Fill in all the accounts this user is coordinator over.  This
 * will fill in all the sub accounts they are coordinator over also.
 */
static int _get_user_coords(mysql_conn_t *mysql_conn, slurmdb_user_rec_t *user)
{
	char *query = NULL;
	slurmdb_coord_rec_t *coord = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	ListIterator itr = NULL, itr2 = NULL;
	char *cluster_name = NULL;

	if (!user) {
		error("We need a user to fill in.");
		return SLURM_ERROR;
	}

	if (!user->coord_accts)
		user->coord_accts = list_create(slurmdb_destroy_coord_rec);

	query = xstrdup_printf(
		"select acct from %s where user='%s' && deleted=0",
		acct_coord_table, user->name);

	if (!(result =
	      mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	while ((row = mysql_fetch_row(result))) {
		coord = xmalloc(sizeof(slurmdb_coord_rec_t));
		list_append(user->coord_accts, coord);
		coord->name = xstrdup(row[0]);
		coord->direct = 1;
	}
	mysql_free_result(result);

	if (!list_count(user->coord_accts))
		return SLURM_SUCCESS;

	slurm_mutex_lock(&as_mysql_cluster_list_lock);
	itr2 = list_iterator_create(as_mysql_cluster_list);
	itr = list_iterator_create(user->coord_accts);
	while ((cluster_name = list_next(itr2))) {
		int set = 0;
		if (query)
			xstrcat(query, " union ");

		while ((coord = list_next(itr))) {
			if (set)
				xstrcat(query, " || ");
			else
				xstrfmtcat(query,
					   "select distinct t1.acct from "
					   "\"%s_%s\" as t1, \"%s_%s\" "
					   "as t2 where t1.deleted=0 && (",
					   cluster_name, assoc_table,
					   cluster_name, assoc_table);
			/* Make sure we don't get the same
			 * account back since we want to keep
			 * track of the sub-accounts.
			 */
			xstrfmtcat(query, "(t2.acct='%s' "
				   "&& t1.lft between t2.lft "
				   "and t2.rgt && t1.user='' "
				   "&& t1.acct!='%s')",
				   coord->name, coord->name);
			set = 1;
		}
		list_iterator_reset(itr);
		if (set)
			xstrcat(query, ")");

	}
	list_iterator_destroy(itr2);
	slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	if (query) {
		debug4("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);

		while ((row = mysql_fetch_row(result))) {
			list_iterator_reset(itr);
			while ((coord = list_next(itr))) {
				if (!strcmp(coord->name, row[0]))
					break;
			}

			if (coord)
				continue;

			coord = xmalloc(sizeof(slurmdb_coord_rec_t));
			list_append(user->coord_accts, coord);
			coord->name = xstrdup(row[0]);
			coord->direct = 0;
		}
		mysql_free_result(result);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern int as_mysql_add_users(mysql_conn_t *mysql_conn, uint32_t uid,
			      List user_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_user_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *query = NULL, *txn_query = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *extra = NULL, *tmp_extra = NULL;
	int affect_rows = 0;
	List assoc_list = list_create(slurmdb_destroy_association_rec);
	List wckey_list = list_create(slurmdb_destroy_wckey_rec);

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(user_list);
	while ((object = list_next(itr))) {
		if (!object->name || !object->name[0]) {
			error("We need a user name and "
			      "default acct to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name");
		xstrfmtcat(vals, "%ld, %ld, '%s'",
			   (long)now, (long)now, object->name);

		if (object->admin_level != SLURMDB_ADMIN_NOTSET) {
			xstrcat(cols, ", admin_level");
			xstrfmtcat(vals, ", %u", object->admin_level);
			xstrfmtcat(extra, ", admin_level=%u",
				   object->admin_level);
		} else
			xstrfmtcat(extra, ", admin_level=%u",
				   SLURMDB_ADMIN_NONE);

		query = xstrdup_printf(
			"insert into %s (%s) values (%s) "
			"on duplicate key update deleted=0, mod_time=%ld %s;",
			user_table, cols, vals,
			(long)now, extra);
		xfree(cols);
		xfree(vals);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add user %s", object->name);
			xfree(extra);
			continue;
		}

		affect_rows = last_affected_rows(mysql_conn);
		if (!affect_rows) {
			debug("nothing changed");
			xfree(extra);
			continue;
		}

		if (addto_update_list(mysql_conn->update_list, SLURMDB_ADD_USER,
				      object) == SLURM_SUCCESS)
			list_remove(itr);

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = slurm_add_slash_to_quotes(extra+2);

		if (txn_query)
			xstrfmtcat(txn_query,
				   ", (%ld, %u, '%s', '%s', '%s')",
				   (long)now, DBD_ADD_USERS, object->name,
				   user_name, tmp_extra);
		else
			xstrfmtcat(txn_query,
				   "insert into %s "
				   "(timestamp, action, name, actor, info) "
				   "values (%ld, %u, '%s', '%s', '%s')",
				   txn_table,
				   (long)now, DBD_ADD_USERS, object->name,
				   user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);

		/* For < 2.2 systems we need to set the is_def flag in
		   the default association/wckey so as to make sure we get
		   it set correctly.
		*/
		if (object->assoc_list) {
			slurmdb_association_rec_t *assoc = NULL;
			ListIterator assoc_itr =
				list_iterator_create(object->assoc_list);
			while ((assoc = list_next(assoc_itr))) {
				/* We need to mark all of the
				   associations with this account
				   since there could be multiple
				   clusters here.
				*/
				if (!strcmp(assoc->acct, object->default_acct))
					assoc->is_def = 1;
			}
			list_iterator_destroy(assoc_itr);
			list_transfer(assoc_list, object->assoc_list);
		}

		if (object->wckey_list) {
			if (object->default_wckey) {
				slurmdb_wckey_rec_t *wckey = NULL;
				ListIterator wckey_itr = list_iterator_create(
					object->wckey_list);
				while ((wckey = list_next(wckey_itr))) {
					/* We need to mark all of the
					   wckeys with this account
					   since there could be multiple
					   clusters here.
					*/
					if (!strcmp(wckey->name,
						    object->default_wckey))
						wckey->is_def = 1;
				}
				list_iterator_destroy(wckey_itr);
			}
			list_transfer(wckey_list, object->wckey_list);
		}
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if (rc != SLURM_ERROR) {
		if (txn_query) {
			xstrcat(txn_query, ";");
			rc = mysql_db_query(mysql_conn,
					    txn_query);
			xfree(txn_query);
			if (rc != SLURM_SUCCESS) {
				error("Couldn't add txn");
				rc = SLURM_SUCCESS;
			}
		}
	} else
		xfree(txn_query);

	if (list_count(assoc_list)) {
		if ((rc = as_mysql_add_assocs(mysql_conn, uid, assoc_list))
		     != SLURM_SUCCESS)
			error("Problem adding user associations");
	}
	list_destroy(assoc_list);

	if (rc == SLURM_SUCCESS && list_count(wckey_list)) {
		if ((rc = as_mysql_add_wckeys(mysql_conn, uid, wckey_list))
		    != SLURM_SUCCESS)
			error("Problem adding user wckeys");
	}
	list_destroy(wckey_list);
	return rc;
}

extern int as_mysql_add_coord(mysql_conn_t *mysql_conn, uint32_t uid,
			      List acct_list, slurmdb_user_cond_t *user_cond)
{
	char *query = NULL, *user = NULL, *acct = NULL;
	char *user_name = NULL, *txn_query = NULL;
	ListIterator itr, itr2;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;
	slurmdb_user_rec_t *user_rec = NULL;

	if (!user_cond || !user_cond->assoc_cond
	    || !user_cond->assoc_cond->user_list
	    || !list_count(user_cond->assoc_cond->user_list)
	    || !acct_list || !list_count(acct_list)) {
		error("we need something to add");
		return SLURM_ERROR;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		slurmdb_user_rec_t user;
		slurmdb_coord_rec_t *coord = NULL;
		char *acct = NULL;

		memset(&user, 0, sizeof(slurmdb_user_rec_t));
		user.uid = uid;

		if (!is_user_any_coord(mysql_conn, &user)) {
			error("Only admins/operators/coordinators "
			      "can add account coordinators");
			return ESLURM_ACCESS_DENIED;
		}

		itr = list_iterator_create(acct_list);
		itr2 = list_iterator_create(user.coord_accts);
		while ((acct = list_next(itr))) {
			while ((coord = list_next(itr2))) {
				if (!strcasecmp(coord->name, acct))
					break;
			}
			if (!coord)
				break;
			list_iterator_reset(itr2);
		}
		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);

		if (!coord)  {
			error("Coordinator %s(%d) tried to add another "
			      "coordinator to an account they aren't "
			      "coordinator over.",
			      user.name, user.uid);
			return ESLURM_ACCESS_DENIED;
		}
	}

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(user_cond->assoc_cond->user_list);
	itr2 = list_iterator_create(acct_list);
	while ((user = list_next(itr))) {
		if (!user[0])
			continue;
		while ((acct = list_next(itr2))) {
			if (!acct[0])
				continue;
			if (query)
				xstrfmtcat(query, ", (%ld, %ld, '%s', '%s')",
					   (long)now, (long)now, acct, user);
			else
				query = xstrdup_printf(
					"insert into %s (creation_time, "
					"mod_time, acct, user) values "
					"(%ld, %ld, '%s', '%s')",
					acct_coord_table,
					(long)now, (long)now, acct, user);

			if (txn_query)
				xstrfmtcat(txn_query,
					   ", (%ld, %u, '%s', '%s', '%s')",
					   (long)now, DBD_ADD_ACCOUNT_COORDS,
					   user, user_name, acct);
			else
				xstrfmtcat(txn_query,
					   "insert into %s "
					   "(timestamp, action, name, "
					   "actor, info) "
					   "values (%ld, %u, '%s', "
					   "'%s', '%s')",
					   txn_table,
					   (long)now, DBD_ADD_ACCOUNT_COORDS,
					   user,
					   user_name, acct);
		}
		list_iterator_reset(itr2);
	}
	xfree(user_name);
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	if (query) {
		xstrfmtcat(query,
			   " on duplicate key update mod_time=%ld, "
			   "deleted=0;%s",
			   (long)now, txn_query);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		xfree(txn_query);

		if (rc != SLURM_SUCCESS) {
			error("Couldn't add cluster hour rollup");
			return rc;
		}
		/* get the update list set */
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while ((user = list_next(itr))) {
			user_rec = xmalloc(sizeof(slurmdb_user_rec_t));
			user_rec->name = xstrdup(user);
			_get_user_coords(mysql_conn, user_rec);
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_ADD_COORD, user_rec)
			    != SLURM_SUCCESS)
				slurmdb_destroy_user_rec(user_rec);
		}
		list_iterator_destroy(itr);
	}

	return SLURM_SUCCESS;
}

extern List as_mysql_modify_users(mysql_conn_t *mysql_conn, uint32_t uid,
				  slurmdb_user_cond_t *user_cond,
				  slurmdb_user_rec_t *user)
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

	if (!user_cond || !user) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (user_cond->assoc_cond && user_cond->assoc_cond->user_list
	    && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (user_cond->admin_level != SLURMDB_ADMIN_NOTSET)
		xstrfmtcat(extra, " && admin_level=%u",
			   user_cond->admin_level);

	ret_list = _get_other_user_names_to_mod(mysql_conn, uid, user_cond);

	if (user->name)
		xstrfmtcat(vals, ", name='%s'", user->name);

	if (user->admin_level != SLURMDB_ADMIN_NOTSET)
		xstrfmtcat(vals, ", admin_level=%u", user->admin_level);

	if ((!extra && !ret_list)
	    || (!vals && !user->default_acct && !user->default_wckey)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	if (!extra) {
		/* means we got a ret_list and don't need to look at
		   the user_table. */
		goto no_user_table;
	}

	query = xstrdup_printf(
		"select distinct name from %s where deleted=0 %s;",
		user_table, extra);
	xfree(extra);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		if (ret_list)
			list_destroy(ret_list);
		return NULL;
	}

	if (!ret_list)
		ret_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		slurmdb_user_rec_t *user_rec = NULL;

		object = row[0];
		slurm_addto_char_list(ret_list, object);
		if (!name_char)
			xstrfmtcat(name_char, "(name='%s'", object);
		else
			xstrfmtcat(name_char, " || name='%s'", object);

		user_rec = xmalloc(sizeof(slurmdb_user_rec_t));

		if (!user->name)
			user_rec->name = xstrdup(object);
		else {
			user_rec->name = xstrdup(user->name);
			user_rec->old_name = xstrdup(object);
			if (_change_user_name(mysql_conn, user_rec)
			    != SLURM_SUCCESS)
				break;
		}

		user_rec->admin_level = user->admin_level;
		if (addto_update_list(mysql_conn->update_list,
				      SLURMDB_MODIFY_USER, user_rec)
		    != SLURM_SUCCESS)
			slurmdb_destroy_user_rec(user_rec);
	}
	mysql_free_result(result);

no_user_table:
	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn,
				 "didn't effect anything\n%s", query);
		xfree(vals);
		xfree(query);
		return ret_list;
	} else if (user->name && (list_count(ret_list) != 1)) {
		errno = ESLURM_ONE_CHANGE;
		xfree(vals);
		xfree(query);
		if (ret_list)
			list_destroy(ret_list);
		return NULL;
	}

	xfree(query);

	if (name_char && vals) {
		xstrcat(name_char, ")");
		user_name = uid_to_string((uid_t) uid);
		rc = modify_common(mysql_conn, DBD_MODIFY_USERS, now,
				   user_name, user_table, name_char,
				   vals, NULL);
		xfree(user_name);
	}

	xfree(name_char);
	xfree(vals);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify users");
		list_destroy(ret_list);
		ret_list = NULL;
	}

	if (user->default_acct) {
		slurmdb_association_cond_t assoc_cond;
		slurmdb_association_rec_t assoc;
		List tmp_list = NULL;

		memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
		slurmdb_init_association_rec(&assoc, 0);
		assoc.is_def = 1;
		assoc_cond.acct_list = list_create(NULL);
		list_append(assoc_cond.acct_list, user->default_acct);
		assoc_cond.user_list = ret_list;
		if (user_cond->assoc_cond
		    && user_cond->assoc_cond->cluster_list)
			assoc_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
		tmp_list = as_mysql_modify_assocs(mysql_conn, uid,
						  &assoc_cond, &assoc);
		list_destroy(assoc_cond.acct_list);

		if (!tmp_list) {
			list_destroy(ret_list);
			ret_list = NULL;
			goto end_it;
		}
		/* char *names = NULL; */
		/* ListIterator itr = list_iterator_create(tmp_list); */
		/* while ((names = list_next(itr))) { */
		/* 	info("%s", names); */
		/* } */
		/* list_iterator_destroy(itr); */
		list_destroy(tmp_list);
	}

	if (user->default_wckey) {
		slurmdb_wckey_cond_t wckey_cond;
		slurmdb_wckey_rec_t wckey;
		List tmp_list = NULL;

		memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
		slurmdb_init_wckey_rec(&wckey, 0);
		wckey.is_def = 1;
		wckey_cond.name_list = list_create(NULL);
		list_append(wckey_cond.name_list, user->default_wckey);
		wckey_cond.user_list = ret_list;
		if (user_cond->assoc_cond
		    && user_cond->assoc_cond->cluster_list)
			wckey_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
		tmp_list = as_mysql_modify_wckeys(mysql_conn, uid,
						  &wckey_cond, &wckey);
		list_destroy(wckey_cond.name_list);

		if (!tmp_list) {
			list_destroy(ret_list);
			ret_list = NULL;
			goto end_it;
		}
		/* char *names = NULL; */
		/* ListIterator itr = list_iterator_create(tmp_list); */
		/* while ((names = list_next(itr))) { */
		/* 	info("%s", names); */
		/* } */
		/* list_iterator_destroy(itr); */
		list_destroy(tmp_list);
	}
end_it:
	return ret_list;
}

extern List as_mysql_remove_users(mysql_conn_t *mysql_conn, uint32_t uid,
				  slurmdb_user_cond_t *user_cond)
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
	slurmdb_user_cond_t user_coord_cond;
	slurmdb_association_cond_t assoc_cond;
	slurmdb_wckey_cond_t wckey_cond;
	bool jobs_running = 0;

	if (!user_cond) {
		error("we need something to remove");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (user_cond->assoc_cond && user_cond->assoc_cond->user_list
	    && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while ((object = list_next(itr))) {
			if (!object[0])
				continue;
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	ret_list = _get_other_user_names_to_mod(mysql_conn, uid, user_cond);

	if (user_cond->admin_level != SLURMDB_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u", user_cond->admin_level);
	}

	if (!extra && !ret_list) {
		error("Nothing to remove");
		return NULL;
	} else if (!extra) {
		/* means we got a ret_list and don't need to look at
		   the user_table. */
		goto no_user_table;
	}

	/* Only handle this if we need to actually query the
	   user_table.  If a request comes in stating they want to
	   remove all users with default account of whatever then that
	   doesn't deal with the user_table.
	*/
	query = xstrdup_printf("select name from %s where deleted=0 %s;",
			       user_table, extra);
	xfree(extra);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	if (!ret_list)
		ret_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result)))
		slurm_addto_char_list(ret_list, row[0]);
	mysql_free_result(result);

no_user_table:

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn,
				 "didn't effect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	memset(&user_coord_cond, 0, sizeof(slurmdb_user_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
	/* we do not need to free the objects we put in here since
	   they are also placed in a list that will be freed
	*/
	assoc_cond.user_list = list_create(NULL);
	user_coord_cond.assoc_cond = &assoc_cond;

	itr = list_iterator_create(ret_list);
	while ((object = list_next(itr))) {
		slurmdb_user_rec_t *user_rec =
			xmalloc(sizeof(slurmdb_user_rec_t));
		list_append(assoc_cond.user_list, object);

		if (name_char) {
			xstrfmtcat(name_char, " || name='%s'", object);
			xstrfmtcat(assoc_char, " || t2.user='%s'", object);
		} else {
			xstrfmtcat(name_char, "name='%s'", object);
			xstrfmtcat(assoc_char, "t2.user='%s'", object);
		}

		user_rec->name = xstrdup(object);
		if (addto_update_list(mysql_conn->update_list,
				      SLURMDB_REMOVE_USER, user_rec)
		    != SLURM_SUCCESS)
			slurmdb_destroy_user_rec(user_rec);
	}
	list_iterator_destroy(itr);

	/* We need to remove these accounts from the coord's that have it */
	coord_list = as_mysql_remove_coord(
		mysql_conn, uid, NULL, &user_coord_cond);
	if (coord_list)
		list_destroy(coord_list);

	/* We need to remove these users from the wckey table */
	memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
	wckey_cond.user_list = assoc_cond.user_list;
	coord_list = as_mysql_remove_wckeys(mysql_conn, uid, &wckey_cond);
	if (coord_list)
		list_destroy(coord_list);

	list_destroy(assoc_cond.user_list);

	user_name = uid_to_string((uid_t) uid);
	slurm_mutex_lock(&as_mysql_cluster_list_lock);
	itr = list_iterator_create(as_mysql_cluster_list);
	while ((object = list_next(itr))) {
		if ((rc = remove_common(mysql_conn, DBD_REMOVE_USERS, now,
					user_name, user_table, name_char,
					assoc_char, object, ret_list,
					&jobs_running))
		    != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	xfree(user_name);
	xfree(name_char);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		xfree(assoc_char);
		return NULL;
	}

	query = xstrdup_printf(
		"update %s as t2 set deleted=1, mod_time=%ld where %s",
		acct_coord_table, (long)now, assoc_char);
	xfree(assoc_char);

	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS) {
		error("Couldn't remove user coordinators");
		list_destroy(ret_list);
		return NULL;
	}

	if (jobs_running)
		errno = ESLURM_JOBS_RUNNING_ON_ASSOC;
	else
		errno = SLURM_SUCCESS;
	return ret_list;
}

extern List as_mysql_remove_coord(mysql_conn_t *mysql_conn, uint32_t uid,
				  List acct_list,
				  slurmdb_user_cond_t *user_cond)
{
	char *query = NULL, *object = NULL, *extra = NULL, *last_user = NULL;
	char *user_name = NULL;
	time_t now = time(NULL);
	int set = 0, is_admin=0, rc = SLURM_SUCCESS;
	ListIterator itr = NULL;
	slurmdb_user_rec_t *user_rec = NULL;
	List ret_list = NULL;
	List user_list = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	slurmdb_user_rec_t user;

	if (!user_cond && !acct_list) {
		error("we need something to remove");
		return NULL;
	} else if (user_cond && user_cond->assoc_cond)
		user_list = user_cond->assoc_cond->user_list;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (!(is_admin = is_user_min_admin_level(
		      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
		if (!is_user_any_coord(mysql_conn, &user)) {
			error("Only admins/coordinators can "
			      "remove coordinators");
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}
	}

	/* Leave it this way since we are using extra below */

	if (user_list && list_count(user_list)) {
		set = 0;
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, "(");

		itr = list_iterator_create(user_list);
		while ((object = list_next(itr))) {
			if (!object[0])
				continue;
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "user='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (acct_list && list_count(acct_list)) {
		set = 0;
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, "(");

		itr = list_iterator_create(acct_list);
		while ((object = list_next(itr))) {
			if (!object[0])
				continue;
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (!extra) {
		errno = SLURM_ERROR;
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "No conditions given");
		return NULL;
	}

	query = xstrdup_printf(
		"select user, acct from %s where deleted=0 && %s order by user",
		acct_coord_table, extra);

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result =
	      mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		xfree(extra);
		errno = SLURM_ERROR;
		return NULL;
	}
	xfree(query);
	ret_list = list_create(slurm_destroy_char);
	user_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		if (!is_admin) {
			slurmdb_coord_rec_t *coord = NULL;
			if (!user.coord_accts) { // This should never
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
			while ((coord = list_next(itr))) {
				if (!strcasecmp(coord->name, row[1]))
					break;
			}
			list_iterator_destroy(itr);

			if (!coord) {
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
		if (!last_user || strcasecmp(last_user, row[0])) {
			list_append(user_list, xstrdup(row[0]));
			last_user = row[0];
		}
		list_append(ret_list, xstrdup_printf("U = %-9s A = %-10s",
						     row[0], row[1]));
	}
	mysql_free_result(result);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_ACCOUNT_COORDS,
			   now, user_name, acct_coord_table,
			   extra, NULL, NULL, NULL, NULL);
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
	while ((last_user = list_next(itr))) {
		user_rec = xmalloc(sizeof(slurmdb_user_rec_t));
		user_rec->name = xstrdup(last_user);
		_get_user_coords(mysql_conn, user_rec);
		if (addto_update_list(mysql_conn->update_list,
				      SLURMDB_REMOVE_COORD, user_rec)
		    != SLURM_SUCCESS)
			slurmdb_destroy_user_rec(user_rec);
	}
	list_iterator_destroy(itr);
	list_destroy(user_list);

	return ret_list;
}

extern List as_mysql_get_users(mysql_conn_t *mysql_conn, uid_t uid,
			       slurmdb_user_cond_t *user_cond)
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
	slurmdb_user_rec_t user;

	/* if this changes you will need to edit the corresponding enum */
	char *user_req_inx[] = {
		"name",
		"admin_level"
	};
	enum {
		USER_REQ_NAME,
		USER_REQ_AL,
		USER_REQ_COUNT
	};

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USERS) {
		if (!(is_admin = is_user_min_admin_level(
			      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
			assoc_mgr_fill_in_user(
				mysql_conn, &user, 1, NULL);
		}
		if (!is_admin && !user.name) {
			debug("User %u has no assocations, and is not admin, "
			      "so not returning any users.", user.uid);
			return NULL;
		}
	}

	if (!user_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if (user_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");


	user_list = _get_other_user_names_to_mod(mysql_conn, uid, user_cond);
	if (user_list) {
		if (!user_cond->assoc_cond)
			user_cond->assoc_cond =
				xmalloc(sizeof(slurmdb_association_rec_t));

		if (!user_cond->assoc_cond->user_list)
			user_cond->assoc_cond->user_list = user_list;
		else {
			list_transfer(user_cond->assoc_cond->user_list,
				      user_list);
			list_destroy(user_list);
		}
		user_list = NULL;
	} else if ((user_cond->def_acct_list
		    && list_count(user_cond->def_acct_list))
		   || (user_cond->def_wckey_list
		       && list_count(user_cond->def_wckey_list)))
		return NULL;

	if (user_cond->assoc_cond &&
	    user_cond->assoc_cond->user_list
	    && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (user_cond->admin_level != SLURMDB_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u",
			   user_cond->admin_level);
	}

empty:
	/* This is here to make sure we are looking at only this user
	 * if this flag is set.
	 */
	if (!is_admin && (private_data & PRIVATE_DATA_USERS)) {
		xstrfmtcat(extra, " && name='%s'", user.name);
	}

	xfree(tmp);
	xstrfmtcat(tmp, "%s", user_req_inx[0]);
	for(i=1; i<USER_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", user_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s", tmp, user_table, extra);
	xfree(tmp);
	xfree(extra);

	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	user_list = list_create(slurmdb_destroy_user_rec);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_user_rec_t *user = xmalloc(sizeof(slurmdb_user_rec_t));

		list_append(user_list, user);

		user->name =  xstrdup(row[USER_REQ_NAME]);
		user->admin_level = slurm_atoul(row[USER_REQ_AL]);

		if (user_cond && user_cond->with_coords)
			_get_user_coords(mysql_conn, user);
	}
	mysql_free_result(result);

	if (user_cond && (user_cond->with_assocs
			  || (user_cond->assoc_cond
			      && user_cond->assoc_cond->only_defs))) {
		ListIterator assoc_itr = NULL;
		slurmdb_user_rec_t *user = NULL;
		slurmdb_association_rec_t *assoc = NULL;
		List assoc_list = NULL;

		/* Make sure we don't get any non-user associations
		 * this is done by at least having a user_list
		 * defined */
		if (!user_cond->assoc_cond)
			user_cond->assoc_cond =
				xmalloc(sizeof(slurmdb_association_cond_t));

		if (!user_cond->assoc_cond->user_list)
			user_cond->assoc_cond->user_list = list_create(NULL);

		user_cond->assoc_cond->with_deleted = user_cond->with_deleted;

		assoc_list = as_mysql_get_assocs(
			mysql_conn, uid, user_cond->assoc_cond);

		if (!assoc_list) {
			error("no associations");
			goto get_wckeys;
		}

		itr = list_iterator_create(user_list);
		assoc_itr = list_iterator_create(assoc_list);
		while ((user = list_next(itr))) {
			while ((assoc = list_next(assoc_itr))) {
				if (strcmp(assoc->user, user->name))
					continue;
				/* Set up the default.  This is needed
				 * for older versions primarily that
				 * don't have the notion of default
				 * account per cluster. */
				if (!user->default_acct && (assoc->is_def == 1))
					user->default_acct =
						xstrdup(assoc->acct);

				if (!user_cond->with_assocs) {
					/* We just got the default so no
					   reason to hang around if we aren't
					   getting the associations.
					*/
					if (user->default_acct)
						break;
					else
						continue;
				}

				if (!user->assoc_list)
					user->assoc_list = list_create(
						slurmdb_destroy_association_rec);
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
	if (user_cond && (user_cond->with_wckeys
			  || (user_cond->assoc_cond
			      && user_cond->assoc_cond->only_defs))) {
		ListIterator wckey_itr = NULL;
		slurmdb_user_rec_t *user = NULL;
		slurmdb_wckey_rec_t *wckey = NULL;
		List wckey_list = NULL;
		slurmdb_wckey_cond_t wckey_cond;

		memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
		if (user_cond->assoc_cond) {
			wckey_cond.user_list =
				user_cond->assoc_cond->user_list;
			wckey_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
			wckey_cond.only_defs =
				user_cond->assoc_cond->only_defs;
		}
		wckey_list = as_mysql_get_wckeys(mysql_conn, uid, &wckey_cond);

		if (!wckey_list)
			return user_list;

		itr = list_iterator_create(user_list);
		wckey_itr = list_iterator_create(wckey_list);
		while ((user = list_next(itr))) {
			while ((wckey = list_next(wckey_itr))) {
				if (strcmp(wckey->user, user->name))
					continue;

				/* Set up the default.  This is needed
				 * for older versions primarily that
				 * don't have the notion of default
				 * wckey per cluster. */
				if (!user->default_wckey
				    && (wckey->is_def == 1))
					user->default_wckey =
						xstrdup(wckey->name);

				/* We just got the default so no
				   reason to hang around if we aren't
				   getting the wckeys.
				*/
				if (!user_cond->with_wckeys) {
					/* We just got the default so no
					   reason to hang around if we aren't
					   getting the wckeys.
					*/
					if (user->default_wckey)
						break;
					else
						continue;
				}

				if (!user->wckey_list)
					user->wckey_list = list_create(
						slurmdb_destroy_wckey_rec);
				list_append(user->wckey_list, wckey);
				list_remove(wckey_itr);
			}
			list_iterator_reset(wckey_itr);
			/* If a user doesn't have a default wckey (they
			   might not of had track_wckeys on), set it now.
			*/
			if (!user->default_wckey)
				user->default_wckey = xstrdup("");
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(wckey_itr);

		list_destroy(wckey_list);
	}

	return user_list;
}
