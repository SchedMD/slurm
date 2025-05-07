/*****************************************************************************\
 *  as_mysql_user.c - functions dealing with users and coordinators.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "as_mysql_assoc.h"
#include "as_mysql_user.h"
#include "as_mysql_wckey.h"

typedef struct {
	list_t *acct_list; /* for coords, a list of just char * instead of
			    * slurmdb_coord_rec_t */
	char *coord_query;
	char *coord_query_pos;
	mysql_conn_t *mysql_conn;
	time_t now;
	int rc;
	bool ret_str_err;
	char *ret_str;
	char *ret_str_pos;
	char *txn_query;
	char *txn_query_pos;
	slurmdb_user_rec_t *user_in;
	char *user_name;
} add_user_cond_t;

static int _change_user_name(mysql_conn_t *mysql_conn, slurmdb_user_rec_t *user)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	list_itr_t *itr = NULL;
	char *cluster_name = NULL;

	xassert(user->old_name);
	xassert(user->name);

	slurm_rwlock_rdlock(&as_mysql_cluster_list_lock);
	itr = list_iterator_create(as_mysql_cluster_list);
	while ((cluster_name = list_next(itr))) {
		// Change assoc_tables
		xstrfmtcat(query, "update \"%s_%s\" set user='%s', "
			   "lineage=replace(lineage, '0-%s', '0-%s') "
			   "where user='%s';", cluster_name, assoc_table,
			   user->name, user->old_name,
			   user->name, user->old_name);
		// Change wckey_tables
		xstrfmtcat(query, "update \"%s_%s\" set user='%s' "
			   "where user='%s';", cluster_name, wckey_table,
			   user->name, user->old_name);
	}
	list_iterator_destroy(itr);
	slurm_rwlock_unlock(&as_mysql_cluster_list_lock);
	// Change coord_tables
	xstrfmtcat(query, "update %s set user='%s' where user='%s';",
		   acct_coord_table, user->name, user->old_name);

	DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	if (rc != SLURM_SUCCESS)
		reset_mysql_conn(mysql_conn);

	return rc;
}

static list_t *_get_other_user_names_to_mod(mysql_conn_t *mysql_conn,
					    uint32_t uid,
					    slurmdb_user_cond_t *user_cond)
{
	list_t *tmp_list = NULL;
	list_t *ret_list = NULL;
	list_itr_t *itr = NULL;

	slurmdb_assoc_cond_t assoc_cond;
	slurmdb_wckey_cond_t wckey_cond;

	if (!user_cond->def_acct_list || !list_count(user_cond->def_acct_list))
		goto no_assocs;

	/* We have to use a different association_cond here because
	   other things could be set here we don't care about in the
	   user's. (So to be safe just move over the info we care about) */
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.acct_list = user_cond->def_acct_list;
	if (user_cond->assoc_cond) {
		if (user_cond->assoc_cond->cluster_list)
			assoc_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
		if (user_cond->assoc_cond->user_list)
			assoc_cond.user_list = user_cond->assoc_cond->user_list;
	}
	assoc_cond.flags |= ASSOC_COND_FLAG_ONLY_DEFS;
	tmp_list = as_mysql_get_assocs(mysql_conn, uid, &assoc_cond);
	if (tmp_list) {
		slurmdb_assoc_rec_t *object = NULL;
		itr = list_iterator_create(tmp_list);
		while ((object = list_next(itr))) {
			if (!ret_list)
				ret_list = list_create(xfree_ptr);
			slurm_addto_char_list(ret_list, object->user);
		}
		list_iterator_destroy(itr);
		FREE_NULL_LIST(tmp_list);
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
	wckey_cond.only_defs = 1;

	tmp_list = as_mysql_get_wckeys(mysql_conn, uid, &wckey_cond);
	if (tmp_list) {
		slurmdb_wckey_rec_t *object = NULL;
		itr = list_iterator_create(tmp_list);
		while ((object = list_next(itr))) {
			if (!ret_list)
				ret_list = list_create(xfree_ptr);
			slurm_addto_char_list(ret_list, object->user);
		}
		list_iterator_destroy(itr);
		FREE_NULL_LIST(tmp_list);
	}

no_wckeys:

	return ret_list;
}

/* Fill in all the accounts this user is coordinator over.  This
 * will fill in all the sub accounts they are coordinator over also.
 */
static int _get_user_coords(mysql_conn_t *mysql_conn, slurmdb_user_rec_t *user)
{
	char *query = NULL, *query_pos = NULL;
	char *meat_query = NULL, *meat_query_pos = NULL;
	slurmdb_coord_rec_t *coord = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	list_itr_t *itr = NULL;
	char *cluster_name = NULL;

	if (!user) {
		error("We need a user to fill in.");
		return SLURM_ERROR;
	}

	if (!user->coord_accts)
		user->coord_accts = list_create(slurmdb_destroy_coord_rec);

	/*
	 * Get explicit account coordinators
	 */
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
		if (assoc_mgr_is_user_acct_coord_user_rec(user, row[0]))
			continue;

		coord = xmalloc(sizeof(slurmdb_coord_rec_t));
		list_append(user->coord_accts, coord);
		coord->name = xstrdup(row[0]);
		coord->direct = 1;
	}
	mysql_free_result(result);

	/*
	 * Get implicit account coordinators
	 */
	query_pos = NULL;
	slurm_rwlock_rdlock(&as_mysql_cluster_list_lock);
	itr = list_iterator_create(as_mysql_cluster_list);
	while ((cluster_name = list_next(itr))) {
		xstrfmtcatat(query, &query_pos,
			     "%sselect distinct t2.acct from \"%s_%s\" as t1, "
			     "\"%s_%s\" as t2 where t1.deleted=0 && "
			     "t2.deleted=0 && "
			     "(t1.flags & %u) && t2.lineage like "
			     "concat('%%/', t1.acct, '/%%0-%s/%%')",
			     query ? " union " : "", cluster_name, assoc_table,
			     cluster_name, assoc_table,
			     ASSOC_FLAG_USER_COORD, user->name);
	}
	list_iterator_destroy(itr);
	slurm_rwlock_unlock(&as_mysql_cluster_list_lock);

	if (query) {
		query_pos = NULL;

		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);

		if (!(result =
		      mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);

		while ((row = mysql_fetch_row(result))) {
			if (assoc_mgr_is_user_acct_coord_user_rec(user, row[0]))
				continue;

			coord = xmalloc(sizeof(slurmdb_coord_rec_t));
			debug2("adding %s to coord_accts for user %s",
			       row[0], user->name);
			list_append(user->coord_accts, coord);
			coord->name = xstrdup(row[0]);
		}
		mysql_free_result(result);
	}

	if (!list_count(user->coord_accts))
		return SLURM_SUCCESS;

	itr = list_iterator_create(user->coord_accts);
	while ((coord = list_next(itr))) {
		/*
		 * Make sure we don't get the same account back since we want to
		 * keep track of the sub-accounts.
		 */
		xstrfmtcatat(meat_query, &meat_query_pos,
			     "%s(lineage like '%%/%s/%%' && user='' && acct!='%s')",
			     meat_query ? " || " : "",
			     coord->name, coord->name);

	}
	list_iterator_destroy(itr);

	if (!meat_query)
		return SLURM_SUCCESS;

	slurm_rwlock_rdlock(&as_mysql_cluster_list_lock);
	itr = list_iterator_create(as_mysql_cluster_list);
	while ((cluster_name = list_next(itr))) {
		xstrfmtcatat(query, &query_pos,
			     "%sselect distinct acct from \"%s_%s\" where deleted=0 && (%s)",
			     query ? " union " : "",
			     cluster_name, assoc_table, meat_query);
	}
	xfree(meat_query);
	list_iterator_destroy(itr);
	slurm_rwlock_unlock(&as_mysql_cluster_list_lock);

	if (!query)
		return SLURM_SUCCESS;

	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	result = mysql_db_query_ret(mysql_conn, query, 0);
	xfree(query);

	if (!result)
		return SLURM_ERROR;

	while ((row = mysql_fetch_row(result))) {
		if (assoc_mgr_is_user_acct_coord_user_rec(user, row[0]))
			continue;

		coord = xmalloc(sizeof(slurmdb_coord_rec_t));
		list_append(user->coord_accts, coord);
		coord->name = xstrdup(row[0]);
		coord->direct = 0;
	}
	mysql_free_result(result);
	return SLURM_SUCCESS;
}

static int _foreach_add_coord(void *x, void *arg)
{
	slurmdb_coord_rec_t *coord = x;
	add_user_cond_t *add_user_cond = arg;

	if (!add_user_cond->coord_query)
		xstrfmtcatat(add_user_cond->coord_query,
			     &add_user_cond->coord_query_pos,
			     "insert into %s (creation_time, mod_time, acct, user) values ",
			     acct_coord_table);
	else
		xstrcatat(add_user_cond->coord_query,
			  &add_user_cond->coord_query_pos,
			  ", ");

	xstrfmtcatat(add_user_cond->coord_query,
		     &add_user_cond->coord_query_pos,
		     "(%ld, %ld, '%s', '%s')",
		     add_user_cond->now, add_user_cond->now, coord->name,
		     add_user_cond->user_in->name);

	if (!add_user_cond->txn_query)
		xstrfmtcatat(add_user_cond->txn_query,
			     &add_user_cond->txn_query_pos,
			     "insert into %s (timestamp, action, name, actor, info) values ",
			     txn_table);
	else
		xstrcatat(add_user_cond->txn_query,
			  &add_user_cond->txn_query_pos,
			  ", ");

	xstrfmtcatat(add_user_cond->txn_query,
		     &add_user_cond->txn_query_pos,
		     "(%ld, %u, '%s', '%s', '%s')",
		     add_user_cond->now, DBD_ADD_ACCOUNT_COORDS,
		     add_user_cond->user_in->name,
		     add_user_cond->user_name, coord->name);

	return 0;
}

static int _foreach_add_acct(void *x, void *arg)
{
	char *acct = x;
	list_t *coord_accts = arg;
	slurmdb_coord_rec_t *coord = xmalloc(sizeof(*coord));

	coord->name = xstrdup(acct);
	coord->direct = 1;
	list_append(coord_accts, coord);

	return 0;
}

static int _add_coords(add_user_cond_t *add_user_cond)
{
	xassert(add_user_cond);
	xassert(add_user_cond->mysql_conn);
	xassert(add_user_cond->user_in);

	if (add_user_cond->acct_list && list_count(add_user_cond->acct_list)) {
		if (add_user_cond->user_in->coord_accts)
			list_flush(add_user_cond->user_in->coord_accts);
		else
			add_user_cond->user_in->coord_accts =
				list_create(slurmdb_destroy_coord_rec);
		(void) list_for_each(add_user_cond->acct_list,
				     _foreach_add_acct,
				     add_user_cond->user_in->coord_accts);
	}

	if (add_user_cond->user_in->coord_accts &&
	    list_count(add_user_cond->user_in->coord_accts))
		(void) list_for_each(add_user_cond->user_in->coord_accts,
				     _foreach_add_coord,
				     add_user_cond);

	if (add_user_cond->coord_query) {
		int rc = SLURM_SUCCESS;
		xstrfmtcat(add_user_cond->coord_query,
			   " on duplicate key update mod_time=%ld, deleted=0, user=VALUES(user);",
			   add_user_cond->now);
		DB_DEBUG(DB_ASSOC, add_user_cond->mysql_conn->conn, "query\n%s",
			 add_user_cond->coord_query);
		rc = mysql_db_query(add_user_cond->mysql_conn,
				    add_user_cond->coord_query);
		xfree(add_user_cond->coord_query);
		add_user_cond->coord_query_pos = NULL;

		if (rc != SLURM_SUCCESS) {
			error("Couldn't add coords");
			return ESLURM_BAD_SQL;
		}
	}

	_get_user_coords(add_user_cond->mysql_conn, add_user_cond->user_in);

	return SLURM_SUCCESS;
}

static int _foreach_add_user(void *x, void *arg)
{
	char *name = x;
	add_user_cond_t *add_user_cond = arg;
	slurmdb_user_rec_t *object, check_object;
	char *extra, *tmp_extra;
	int rc;
	char *query;

	/* Check to see if it is already in the assoc_mgr */
	memset(&check_object, 0, sizeof(check_object));
	check_object.name = x;
	check_object.uid = NO_VAL;

	rc = assoc_mgr_fill_in_user(add_user_cond->mysql_conn,
				    &check_object,
				    ACCOUNTING_ENFORCE_ASSOCS, NULL, false);
	if (rc == SLURM_SUCCESS) {
		debug2("User %s is already here, not adding again.",
		       check_object.name);
		return 0;
	}

	/* Else, add it */
	object = xmalloc(sizeof(*object));
	object->name = xstrdup(x);
	object->admin_level = add_user_cond->user_in->admin_level;
	object->coord_accts = slurmdb_list_copy_coord(
		add_user_cond->user_in->coord_accts);

	query = xstrdup_printf(
		"insert into %s (creation_time, mod_time, name, admin_level) values (%ld, %ld, '%s', %u) on duplicate key update deleted=0, mod_time=VALUES(mod_time), admin_level=VALUES(admin_level);",
		user_table, add_user_cond->now, add_user_cond->now,
		object->name, object->admin_level);

	DB_DEBUG(DB_ASSOC, add_user_cond->mysql_conn->conn, "query:\n%s",
		 query);
	add_user_cond->rc = mysql_db_query(add_user_cond->mysql_conn, query);
	xfree(query);
	if (add_user_cond->rc != SLURM_SUCCESS) {
		add_user_cond->rc = ESLURM_BAD_SQL;
		add_user_cond->ret_str_err = true;
		xfree(add_user_cond->ret_str);
		add_user_cond->ret_str = xstrdup_printf(
			"Couldn't add user %s: %s",
			object->name, slurm_strerror(add_user_cond->rc));
		slurmdb_destroy_user_rec(object);
		error("%s", add_user_cond->ret_str);
		return -1;
	}

	if (object->coord_accts) {
		slurmdb_user_rec_t *user_rec = add_user_cond->user_in;
		add_user_cond->user_in = object;
		add_user_cond->rc = _add_coords(add_user_cond);
		add_user_cond->user_in = user_rec;
	} else {
		add_user_cond->rc =
			_get_user_coords(add_user_cond->mysql_conn, object);
	}

	if (add_user_cond->rc != SLURM_SUCCESS) {
		slurmdb_destroy_user_rec(object);
		return -1;
	}

	extra = xstrdup_printf("admin_level=%u", object->admin_level);
	tmp_extra = slurm_add_slash_to_quotes(extra);

	if (!add_user_cond->txn_query)
		xstrfmtcatat(add_user_cond->txn_query,
			     &add_user_cond->txn_query_pos,
			     "insert into %s (timestamp, action, name, actor, info) values ",
			     txn_table);
	else
		xstrcatat(add_user_cond->txn_query,
			  &add_user_cond->txn_query_pos,
			  ", ");

	xstrfmtcatat(add_user_cond->txn_query,
		     &add_user_cond->txn_query_pos,
		     "(%ld, %u, '%s', '%s', '%s')",
		     add_user_cond->now, DBD_ADD_USERS, name,
		     add_user_cond->user_name, tmp_extra);
	xfree(tmp_extra);
	xfree(extra);

	if (addto_update_list(add_user_cond->mysql_conn->update_list,
			      SLURMDB_ADD_USER,
			      object) == SLURM_SUCCESS) {
		if (!add_user_cond->ret_str)
			xstrcatat(add_user_cond->ret_str,
				  &add_user_cond->ret_str_pos,
				  " Adding User(s)\n");

		xstrfmtcatat(add_user_cond->ret_str,
			     &add_user_cond->ret_str_pos,
			     "  %s\n", object->name);
		object = NULL;
	}

	slurmdb_destroy_user_rec(object);

	return 0;
}

extern int as_mysql_add_users(mysql_conn_t *mysql_conn, uint32_t uid,
			      list_t *user_list)
{
	list_itr_t *itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_user_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *query = NULL, *txn_query = NULL;
	char *txn_query_pos = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *extra = NULL, *tmp_extra = NULL;
	int affect_rows = 0;
	list_t *assoc_list;
	list_t *wckey_list;
	bool is_admin = false;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		slurmdb_user_rec_t user;

		if (slurmdbd_conf->flags & DBD_CONF_FLAG_DISABLE_COORD_DBD) {
			error("Coordinator privilege revoked with DisableCoordDBD, only admins/operators can add accounts.");
			return ESLURM_ACCESS_DENIED;
		}

		memset(&user, 0, sizeof(slurmdb_user_rec_t));
		user.uid = uid;

		if (!is_user_any_coord(mysql_conn, &user)) {
			error("Only admins/operators/coordinators "
			      "can add accounts");
			return ESLURM_ACCESS_DENIED;
		}
		/* If the user is a coord of any acct they can add
		 * accounts they are only able to make associations to
		 * these accounts if they are coordinators of the
		 * parent they are trying to add to
		 */
	} else {
		is_admin = true;
	}

	if (!user_list || !list_count(user_list)) {
		error("%s: Trying to add empty user list", __func__);
		return ESLURM_EMPTY_LIST;
	}

	assoc_list = list_create(slurmdb_destroy_assoc_rec);
	wckey_list = list_create(slurmdb_destroy_wckey_rec);

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
			if (!is_admin) {
				error("Only admins/operators can add an admin/operator");
				rc = ESLURM_ACCESS_DENIED;
				break;
			}
			xstrcat(cols, ", admin_level");
			xstrfmtcat(vals, ", %u", object->admin_level);
			xstrfmtcat(extra, ", admin_level=%u",
				   object->admin_level);
		} else
			xstrfmtcat(extra, ", admin_level=%u",
				   SLURMDB_ADMIN_NONE);

		query = xstrdup_printf(
			"insert into %s (%s) values (%s) "
			"on duplicate key update name=VALUES(name), deleted=0, mod_time=%ld %s;",
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

		if (object->coord_accts) {
			add_user_cond_t add_user_cond;
			memset(&add_user_cond, 0, sizeof(add_user_cond));
			add_user_cond.user_in = object;
			add_user_cond.mysql_conn = mysql_conn;
			add_user_cond.user_name = user_name;
			add_user_cond.now = now;
			add_user_cond.txn_query = txn_query;
			add_user_cond.txn_query_pos = txn_query_pos;
			rc = _add_coords(&add_user_cond);
			txn_query = add_user_cond.txn_query;
			txn_query_pos = add_user_cond.txn_query_pos;
		} else {
			rc = _get_user_coords(mysql_conn, object);
		}

		if (rc != SLURM_SUCCESS)
			continue;

		if (addto_update_list(mysql_conn->update_list, SLURMDB_ADD_USER,
				      object) == SLURM_SUCCESS)
			list_remove(itr);

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = slurm_add_slash_to_quotes(extra+2);

		if (txn_query)
			xstrfmtcatat(txn_query, &txn_query_pos,
				     ", (%ld, %u, '%s', '%s', '%s')",
				     (long)now, DBD_ADD_USERS, object->name,
				     user_name, tmp_extra);
		else
			xstrfmtcatat(txn_query, &txn_query_pos,
				     "insert into %s "
				     "(timestamp, action, name, actor, info) "
				     "values (%ld, %u, '%s', '%s', '%s')",
				     txn_table,
				     (long)now, DBD_ADD_USERS, object->name,
				     user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);
		if (object->assoc_list)
			list_transfer(assoc_list, object->assoc_list);

		if (object->wckey_list)
			list_transfer(wckey_list, object->wckey_list);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if (rc == SLURM_SUCCESS) {
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
		if ((rc = as_mysql_add_assocs(mysql_conn, uid, assoc_list)) !=
		    SLURM_SUCCESS)
			error("Problem adding user associations");
	}
	FREE_NULL_LIST(assoc_list);

	if (rc == SLURM_SUCCESS && list_count(wckey_list)) {
		if ((rc = as_mysql_add_wckeys(mysql_conn, uid, wckey_list)) !=
		    SLURM_SUCCESS)
			error("Problem adding user wckeys");
	}
	FREE_NULL_LIST(wckey_list);
	return rc;
}

extern char *as_mysql_add_users_cond(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_add_assoc_cond_t *add_assoc,
				     slurmdb_user_rec_t *user)
{
	add_user_cond_t add_user_cond;
	char *ret_str = NULL;
	bool admin_set = false;
	int rc;

	if (check_connection(mysql_conn) != SLURM_SUCCESS) {
		errno = ESLURM_DB_CONNECTION;
		return NULL;
	}

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		slurmdb_user_rec_t user_coord = {
			.uid = uid,
		};

		if (user->admin_level != SLURMDB_ADMIN_NOTSET) {
			ret_str = xstrdup("Only admins/operators can add an admin/operator");
			error("%s", ret_str);
			errno = ESLURM_ACCESS_DENIED;
			return ret_str;
		}

		if (slurmdbd_conf->flags & DBD_CONF_FLAG_DISABLE_COORD_DBD) {
			ret_str = xstrdup("Coordinator privilege revoked with DisableCoordDBD, only admins/operators can add accounts.");
			error("%s", ret_str);
			errno = ESLURM_ACCESS_DENIED;
			return ret_str;
		}

		if (!is_user_any_coord(mysql_conn, &user_coord)) {
			ret_str = xstrdup("Only admins/operators/coordinators can add accounts");
			error("%s", ret_str);
			errno = ESLURM_ACCESS_DENIED;
			return ret_str;
		}
		/*
		 * If the user is a coord of any acct they can add
		 * accounts they are only able to make associations to
		 * these accounts if they are coordinators of the
		 * parent they are trying to add to
		 */
	}

	if (user->admin_level == SLURMDB_ADMIN_NOTSET)
		user->admin_level = SLURMDB_ADMIN_NONE;
	else
		admin_set = true;

	memset(&add_user_cond, 0, sizeof(add_user_cond));
	add_user_cond.user_in = user;
	add_user_cond.mysql_conn = mysql_conn;
	add_user_cond.now = time(NULL);
	add_user_cond.user_name = uid_to_string((uid_t) uid);

	/* First add the accounts to the user_table. */
	if (list_for_each_ro(add_assoc->user_list, _foreach_add_user,
			     &add_user_cond) < 0) {
		xfree(add_user_cond.ret_str);
		xfree(add_user_cond.txn_query);
		xfree(add_user_cond.user_name);
		errno = add_user_cond.rc;
		return NULL;
	}

	if (add_user_cond.txn_query) {
		/* Success means we add the defaults to the string */
		xstrcatat(add_user_cond.ret_str,
			  &add_user_cond.ret_str_pos,
			  " Settings\n");
		if (user->default_acct)
			xstrfmtcatat(add_user_cond.ret_str,
				     &add_user_cond.ret_str_pos,
				     "  Default Account = %s\n",
				     user->default_acct);
		if (user->default_wckey)
			xstrfmtcatat(add_user_cond.ret_str,
				     &add_user_cond.ret_str_pos,
				     "  Default WCKey   = %s\n",
				     user->default_wckey);
		if (admin_set)
			xstrfmtcatat(add_user_cond.ret_str,
				     &add_user_cond.ret_str_pos,
				     "  Admin Level     = %s\n",
				     slurmdb_admin_level_str(
					     user->admin_level));

		xstrcatat(add_user_cond.txn_query,
			  &add_user_cond.txn_query_pos,
			  ";");
		rc = mysql_db_query(mysql_conn, add_user_cond.txn_query);
		xfree(add_user_cond.txn_query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
			rc = SLURM_SUCCESS;
		}
	}

	if (add_assoc->acct_list) {
		/* Now add the associations */
		add_assoc->default_acct = user->default_acct;
		ret_str = as_mysql_add_assocs_cond(mysql_conn, uid, add_assoc);
		rc = errno;
		add_assoc->default_acct = NULL;

		if (rc != SLURM_SUCCESS) {
			reset_mysql_conn(mysql_conn);
			if (!add_user_cond.ret_str_err)
				xfree(add_user_cond.ret_str);
			else
				xfree(ret_str);
			xfree(add_user_cond.txn_query);
			xfree(add_user_cond.user_name);
			errno = rc;
			return add_user_cond.ret_str ?
				add_user_cond.ret_str : ret_str;
		}

		if (ret_str) {
			xstrcatat(add_user_cond.ret_str,
				  &add_user_cond.ret_str_pos,
				  ret_str);
			xfree(ret_str);
		}
	}

	if (add_assoc->wckey_list) {
		ret_str = as_mysql_add_wckeys_cond(
			mysql_conn, uid, add_assoc, user);
		rc = errno;

		if (rc != SLURM_SUCCESS) {
			reset_mysql_conn(mysql_conn);
			if (!add_user_cond.ret_str_err)
				xfree(add_user_cond.ret_str);
			else
				xfree(ret_str);
			xfree(add_user_cond.txn_query);
			xfree(add_user_cond.user_name);
			errno = rc;
			return add_user_cond.ret_str ?
				add_user_cond.ret_str : ret_str;
		}

		if (ret_str) {
			xstrcatat(add_user_cond.ret_str,
				  &add_user_cond.ret_str_pos,
				  ret_str);
			xfree(ret_str);
		}
	}

	xfree(add_user_cond.txn_query);
	xfree(add_user_cond.user_name);

	if (!add_user_cond.ret_str) {
		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "didn't affect anything");
		errno = SLURM_NO_CHANGE_IN_DATA;
		return NULL;
	}

	errno = SLURM_SUCCESS;
	return add_user_cond.ret_str;
}

extern int as_mysql_add_coord(mysql_conn_t *mysql_conn, uint32_t uid,
			      list_t *acct_list, slurmdb_user_cond_t *user_cond)
{
	char *user = NULL;
	list_itr_t *itr;
	int rc = SLURM_SUCCESS;
	add_user_cond_t add_user_cond;

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
		list_itr_t *itr2;

		if (slurmdbd_conf->flags & DBD_CONF_FLAG_DISABLE_COORD_DBD) {
			error("Coordinator privilege revoked with DisableCoordDBD, only admins/operators can add account coordinators.");
			return ESLURM_ACCESS_DENIED;
		}

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
				if (!xstrcasecmp(coord->name, acct))
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

	memset(&add_user_cond, 0, sizeof(add_user_cond));
	add_user_cond.acct_list = acct_list;
	add_user_cond.mysql_conn = mysql_conn;
	add_user_cond.user_name = uid_to_string((uid_t) uid);
	add_user_cond.now = time(NULL);
	itr = list_iterator_create(user_cond->assoc_cond->user_list);
	while ((user = list_next(itr))) {
		if (!user[0])
			continue;
		add_user_cond.user_in = xmalloc(sizeof(slurmdb_user_rec_t));
		add_user_cond.user_in->name = xstrdup(user);

		if ((rc = _add_coords(&add_user_cond)) != SLURM_SUCCESS) {
			slurmdb_destroy_user_rec(add_user_cond.user_in);
			xfree(add_user_cond.txn_query);
			break;
		}

		if ((rc = addto_update_list(mysql_conn->update_list,
					    SLURMDB_ADD_COORD,
					    add_user_cond.user_in)) !=
		    SLURM_SUCCESS) {
			slurmdb_destroy_user_rec(add_user_cond.user_in);
			xfree(add_user_cond.txn_query);
			break;
		}
		add_user_cond.user_in = NULL;
	}
	list_iterator_destroy(itr);

	xfree(add_user_cond.user_name);

	if (add_user_cond.txn_query) {
		xstrcatat(add_user_cond.txn_query,
			  &add_user_cond.txn_query_pos,
			  ";");
		rc = mysql_db_query(mysql_conn, add_user_cond.txn_query);
		xfree(add_user_cond.txn_query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
			rc = SLURM_SUCCESS;
		}
	}

	return rc;
}

extern list_t *as_mysql_modify_users(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_user_cond_t *user_cond,
				     slurmdb_user_rec_t *user)
{
	list_itr_t *itr = NULL;
	list_t *ret_list = NULL;
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
		FREE_NULL_LIST(ret_list);
		return NULL;
	}

	if (!ret_list)
		ret_list = list_create(xfree_ptr);
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
		DB_DEBUG(DB_ASSOC, mysql_conn->conn,
		         "didn't affect anything\n%s", query);
		xfree(vals);
		xfree(query);
		return ret_list;
	} else if (user->name && (list_count(ret_list) != 1)) {
		errno = ESLURM_ONE_CHANGE;
		xfree(vals);
		xfree(query);
		FREE_NULL_LIST(ret_list);
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
		FREE_NULL_LIST(ret_list);
	}

	if (user->default_acct && user->default_acct[0]) {
		slurmdb_assoc_cond_t assoc_cond;
		slurmdb_assoc_rec_t assoc;
		list_t *tmp_list = NULL;
		memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
		slurmdb_init_assoc_rec(&assoc, 0);
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
		FREE_NULL_LIST(assoc_cond.acct_list);

		if (!tmp_list) {
			FREE_NULL_LIST(ret_list);
			goto end_it;
		}
		/* char *names = NULL; */
		/* list_itr_t *itr = list_iterator_create(tmp_list); */
		/* while ((names = list_next(itr))) { */
		/* 	info("%s", names); */
		/* } */
		/* list_iterator_destroy(itr); */
		FREE_NULL_LIST(tmp_list);
	} else if (user->default_acct) {
		list_t *cluster_list = NULL;
		if (user_cond->assoc_cond
		    && user_cond->assoc_cond->cluster_list)
			cluster_list = user_cond->assoc_cond->cluster_list;

		rc = as_mysql_assoc_remove_default(
			mysql_conn, ret_list, cluster_list);
		if (rc != SLURM_SUCCESS) {
			FREE_NULL_LIST(ret_list);
			errno = rc;
			goto end_it;
		}
	}

	if (user->default_wckey) {
		slurmdb_wckey_cond_t wckey_cond;
		slurmdb_wckey_rec_t wckey;
		list_t *tmp_list = NULL;

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
		FREE_NULL_LIST(wckey_cond.name_list);

		if (!tmp_list) {
			FREE_NULL_LIST(ret_list);
			goto end_it;
		}
		/* char *names = NULL; */
		/* list_itr_t *itr = list_iterator_create(tmp_list); */
		/* while ((names = list_next(itr))) { */
		/* 	info("%s", names); */
		/* } */
		/* list_iterator_destroy(itr); */
		FREE_NULL_LIST(tmp_list);
	}
end_it:
	errno = rc;
	return ret_list;
}

/*
 * If the coordinator has permissions to modify every account
 * belonging to each user, return true. Otherwise return false.
 */
static bool _is_coord_over_all_accts(mysql_conn_t *mysql_conn,
				     char *cluster_name, char *user_char,
				     slurmdb_user_rec_t *coord)
{
	bool has_access;
	char *query = NULL, *sep_str = "";
	MYSQL_RES *result;
	list_itr_t *itr;
	slurmdb_coord_rec_t *coord_acct;

	if (!coord->coord_accts || !list_count(coord->coord_accts)) {
		/* This should never happen */
		error("%s: We are here with no coord accts", __func__);
		return false;
	}

	query = xstrdup_printf("select distinct acct from \"%s_%s\" where deleted=0 && (%s) && (",
			       cluster_name, assoc_table, user_char);

	/*
	 * Add the accounts we are coordinator of.  If anything is returned
	 * outside of this list we will know there are accounts in the request
	 * that we are not coordinator over.
	 */
	itr = list_iterator_create(coord->coord_accts);
	while ((coord_acct = (list_next(itr)))) {
		xstrfmtcat(query, "%sacct != '%s'", sep_str, coord_acct->name);
		sep_str = " && ";
	}
	list_iterator_destroy(itr);
	xstrcat(query, ");");

	DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return false;
	}
	xfree(query);

	/*
	 * If nothing was returned we are coordinator over all these accounts
	 * and users.
	 */
	has_access = !mysql_num_rows(result);

	mysql_free_result(result);
	return has_access;
}

extern list_t *as_mysql_remove_users(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_user_cond_t *user_cond)
{
	list_itr_t *itr = NULL;
	list_t *ret_list = NULL;
	list_t *coord_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL, *user_char = NULL;
	char *name_char_pos = NULL, *assoc_char_pos = NULL,
		*user_char_pos = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	slurmdb_user_cond_t user_coord_cond;
	slurmdb_user_rec_t user;
	slurmdb_assoc_cond_t assoc_cond;
	slurmdb_wckey_cond_t wckey_cond;
	bool jobs_running = 0;
	bool is_coord = false;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (!user_cond) {
		error("we need something to remove");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		if (slurmdbd_conf->flags & DBD_CONF_FLAG_DISABLE_COORD_DBD) {
			error("Coordinator privilege revoked with DisableCoordDBD, only admins/operators can remove users.");
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}

		/*
		 * Allow coordinators to delete users from accounts that
		 * they coordinate. After we have gotten every association that
		 * the users belong to, check that the coordinator has access
		 * to modify every affected account.
		 */
		is_coord = is_user_any_coord(mysql_conn, &user);
		if (!is_coord) {
			error("Only admins/coordinators can remove users");
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}
	}

	if (user_cond->assoc_cond && user_cond->assoc_cond->user_list
	    && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while ((object = list_next(itr))) {
			if (!object[0])
				continue;
			if (set)
				xstrcat(extra, " || ");
			else
				xstrcat(extra, " && (");

			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		if (extra)
			xstrcat(extra, ")");
	}

	ret_list = _get_other_user_names_to_mod(mysql_conn, uid, user_cond);

	if (user_cond->admin_level != SLURMDB_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u", user_cond->admin_level);
	}

	if (!extra && !ret_list) {
		errno = SLURM_NO_CHANGE_IN_DATA;
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
		ret_list = list_create(xfree_ptr);
	while ((row = mysql_fetch_row(result)))
		slurm_addto_char_list(ret_list, row[0]);
	mysql_free_result(result);

no_user_table:

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		DB_DEBUG(DB_ASSOC, mysql_conn->conn,
		         "didn't affect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	memset(&user_coord_cond, 0, sizeof(slurmdb_user_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	/* we do not need to free the objects we put in here since
	   they are also placed in a list that will be freed
	*/
	assoc_cond.user_list = list_create(NULL);
	user_coord_cond.assoc_cond = &assoc_cond;

	itr = list_iterator_create(ret_list);
	while ((object = list_next(itr))) {
		slurmdb_user_rec_t *user_rec;

		/*
		 * Skip empty names or else will select account associations
		 * and remove all associations.
		 */
		if (!object[0]) {
			list_delete_item(itr);
			continue;
		}

		user_rec = xmalloc(sizeof(slurmdb_user_rec_t));
		list_append(assoc_cond.user_list, object);

		if (name_char) {
			xstrfmtcatat(name_char, &name_char_pos, ",'%s'",
				     object);
			xstrfmtcatat(user_char, &user_char_pos, ",'%s'",
				     object);
		} else {
			xstrfmtcatat(name_char, &name_char_pos, "name in('%s'",
				     object);
			xstrfmtcatat(user_char, &user_char_pos, "user in('%s'",
				     object);
		}
		xstrfmtcatat(assoc_char, &assoc_char_pos,
			     "%st2.lineage like '%%/0-%s/%%'",
			     assoc_char ? " || " : "", object);

		user_rec->name = xstrdup(object);
		if (addto_update_list(mysql_conn->update_list,
				      SLURMDB_REMOVE_USER, user_rec)
		    != SLURM_SUCCESS)
			slurmdb_destroy_user_rec(user_rec);
	}
	list_iterator_destroy(itr);
	if (name_char) {
		xstrcatat(name_char, &name_char_pos, ")");
		xstrcatat(user_char, &user_char_pos, ")");
	}
	/* We need to remove these accounts from the coord's that have it */
	coord_list = as_mysql_remove_coord(
		mysql_conn, uid, NULL, &user_coord_cond);
	FREE_NULL_LIST(coord_list);

	/* We need to remove these users from the wckey table */
	memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
	wckey_cond.user_list = assoc_cond.user_list;
	coord_list = as_mysql_remove_wckeys(mysql_conn, uid, &wckey_cond);
	FREE_NULL_LIST(coord_list);

	FREE_NULL_LIST(assoc_cond.user_list);

	user_name = uid_to_string((uid_t) uid);
	slurm_rwlock_rdlock(&as_mysql_cluster_list_lock);
	itr = list_iterator_create(as_mysql_cluster_list);
	while ((object = list_next(itr))) {

		if (is_coord) {
			if (!_is_coord_over_all_accts(mysql_conn, object,
						      user_char, &user)) {
				errno = ESLURM_ACCESS_DENIED;
				rc = SLURM_ERROR;
				break;
			}
		}

		if ((rc = remove_common(mysql_conn, DBD_REMOVE_USERS, now,
					user_name, user_table, name_char,
					assoc_char, object, ret_list,
					&jobs_running, NULL))
		    != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);
	slurm_rwlock_unlock(&as_mysql_cluster_list_lock);

	xfree(user_name);
	xfree(name_char);
	xfree(user_char);
	if (rc == SLURM_ERROR) {
		FREE_NULL_LIST(ret_list);
		xfree(assoc_char);
		return NULL;
	}

	query = xstrdup_printf(
		"update %s set deleted=1, mod_time=%ld where %s",
		acct_coord_table, (long)now, user_char);
	xfree(assoc_char);

	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS) {
		error("Couldn't remove user coordinators");
		FREE_NULL_LIST(ret_list);
		return NULL;
	}

	if (jobs_running)
		errno = ESLURM_JOBS_RUNNING_ON_ASSOC;
	else
		errno = SLURM_SUCCESS;
	return ret_list;
}

extern list_t *as_mysql_remove_coord(mysql_conn_t *mysql_conn, uint32_t uid,
				     list_t *acct_list,
				     slurmdb_user_cond_t *user_cond)
{
	char *query = NULL, *object = NULL, *extra = NULL, *last_user = NULL;
	char *user_name = NULL;
	time_t now = time(NULL);
	int set = 0, is_admin=0, rc = SLURM_SUCCESS;
	list_itr_t *itr = NULL;
	slurmdb_user_rec_t *user_rec = NULL;
	list_t *ret_list = NULL;
	list_t *user_list = NULL;
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
		if (slurmdbd_conf->flags & DBD_CONF_FLAG_DISABLE_COORD_DBD) {
			error("Coordinator privilege revoked with DisableCoordDBD, only admins/operators can remove coordinators.");
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}
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
		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "No conditions given");
		return NULL;
	}

	query = xstrdup_printf(
		"select user, acct from %s where deleted=0 && %s order by user",
		acct_coord_table, extra);

	DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
	if (!(result =
	      mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		xfree(extra);
		errno = SLURM_ERROR;
		return NULL;
	}
	xfree(query);
	ret_list = list_create(xfree_ptr);
	user_list = list_create(xfree_ptr);
	while ((row = mysql_fetch_row(result))) {
		if (!is_admin) {
			slurmdb_coord_rec_t *coord = NULL;
			if (!user.coord_accts) { // This should never
				// happen
				error("We are here with no coord accts");
				errno = ESLURM_ACCESS_DENIED;
				FREE_NULL_LIST(ret_list);
				FREE_NULL_LIST(user_list);
				xfree(extra);
				mysql_free_result(result);
				return NULL;
			}
			itr = list_iterator_create(user.coord_accts);
			while ((coord = list_next(itr))) {
				if (!xstrcasecmp(coord->name, row[1]))
					break;
			}
			list_iterator_destroy(itr);

			if (!coord) {
				error("User %s(%d) does not have the "
				      "ability to change this account (%s)",
				      user.name, user.uid, row[1]);
				errno = ESLURM_ACCESS_DENIED;
				FREE_NULL_LIST(ret_list);
				FREE_NULL_LIST(user_list);
				xfree(extra);
				mysql_free_result(result);
				return NULL;
			}
		}
		if (!last_user || xstrcasecmp(last_user, row[0])) {
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
			   extra, NULL, NULL, NULL, NULL, NULL);
	xfree(user_name);
	xfree(extra);
	if (rc == SLURM_ERROR) {
		FREE_NULL_LIST(ret_list);
		FREE_NULL_LIST(user_list);
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
	FREE_NULL_LIST(user_list);

	return ret_list;
}

extern list_t *as_mysql_get_users(mysql_conn_t *mysql_conn, uid_t uid,
				  slurmdb_user_cond_t *user_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	list_t *user_list = NULL;
	list_itr_t *itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	slurmdb_user_rec_t user;

	/* if this changes you will need to edit the corresponding enum */
	char *user_req_inx[] = {
		"name",
		"admin_level",
		"deleted",
	};
	enum {
		USER_REQ_NAME,
		USER_REQ_AL,
		USER_REQ_DELETED,
		USER_REQ_COUNT
	};

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (slurm_conf.private_data & PRIVATE_DATA_USERS) {
		if (!(is_admin = is_user_min_admin_level(
			      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
			assoc_mgr_fill_in_user(
				mysql_conn, &user, 1, NULL, false);
		}
		if (!is_admin && !user.name) {
			debug("User %u has no associations, and is not admin, "
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
				xmalloc(sizeof(slurmdb_assoc_rec_t));

		if (!user_cond->assoc_cond->user_list)
			user_cond->assoc_cond->user_list = user_list;
		else {
			list_transfer(user_cond->assoc_cond->user_list,
				      user_list);
			FREE_NULL_LIST(user_list);
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
	if (!is_admin && (slurm_conf.private_data & PRIVATE_DATA_USERS)) {
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

	DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
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

		if (slurm_atoul(row[USER_REQ_DELETED]))
			user->flags |= SLURMDB_USER_FLAG_DELETED;

		if (user_cond && user_cond->with_coords) {
			/*
			 * On start up the coord list doesn't exist so get it
			 * the SQL way.
			 */
			if (!assoc_mgr_coord_list)
				_get_user_coords(mysql_conn, user);
			else
				user->coord_accts =
					assoc_mgr_user_acct_coords(
						mysql_conn, user->name);
		}
	}
	mysql_free_result(result);

	if (user_cond && (user_cond->with_assocs ||
			  (user_cond->assoc_cond &&
			   (user_cond->assoc_cond->flags &
			    ASSOC_COND_FLAG_ONLY_DEFS)))) {
		list_itr_t *assoc_itr = NULL;
		slurmdb_user_rec_t *user = NULL;
		slurmdb_assoc_rec_t *assoc = NULL;
		list_t *assoc_list = NULL;

		/* Make sure we don't get any non-user associations
		 * this is done by at least having a user_list
		 * defined */
		if (!user_cond->assoc_cond)
			user_cond->assoc_cond =
				xmalloc(sizeof(slurmdb_assoc_cond_t));

		if (!user_cond->assoc_cond->user_list)
			user_cond->assoc_cond->user_list = list_create(NULL);

		if (user_cond->with_deleted)
			user_cond->assoc_cond->flags |=
				ASSOC_COND_FLAG_WITH_DELETED;

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
				if (xstrcmp(assoc->user, user->name))
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
						slurmdb_destroy_assoc_rec);
				list_append(user->assoc_list, assoc);
				list_remove(assoc_itr);
			}
			list_iterator_reset(assoc_itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(assoc_itr);
		FREE_NULL_LIST(assoc_list);
	}

get_wckeys:
	if (user_cond && (user_cond->with_wckeys ||
			  (user_cond->assoc_cond &&
			   (user_cond->assoc_cond->flags &
			    ASSOC_COND_FLAG_ONLY_DEFS)))) {
		list_itr_t *wckey_itr = NULL;
		slurmdb_user_rec_t *user = NULL;
		slurmdb_wckey_rec_t *wckey = NULL;
		list_t *wckey_list = NULL;
		slurmdb_wckey_cond_t wckey_cond;

		memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
		if (user_cond->assoc_cond) {
			wckey_cond.user_list =
				user_cond->assoc_cond->user_list;
			wckey_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
			wckey_cond.only_defs =
				user_cond->assoc_cond->flags &
				ASSOC_COND_FLAG_ONLY_DEFS;
		}
		wckey_list = as_mysql_get_wckeys(mysql_conn, uid, &wckey_cond);

		if (!wckey_list)
			return user_list;

		itr = list_iterator_create(user_list);
		wckey_itr = list_iterator_create(wckey_list);
		while ((user = list_next(itr))) {
			while ((wckey = list_next(wckey_itr))) {
				if (xstrcmp(wckey->user, user->name))
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

		FREE_NULL_LIST(wckey_list);
	}

	return user_list;
}

static int _find_user(void *x, void *arg)
{
	slurmdb_user_rec_t *user_rec = x;
	char *name = arg;

	return slurm_find_char_exact_in_list(user_rec->name, name);
}

static slurmdb_user_rec_t *_make_user_rec_with_coords(
	mysql_conn_t *mysql_conn, char *user, bool locked)
{
	slurmdb_user_rec_t *user_rec = NULL;
	/*
	 * We can't use user_rec just yet since we get that filled up
	 * with variables that we don't own. We will eventually free it
	 * later which causes issues memory wise.
	 */
	slurmdb_user_rec_t user_tmp = {
		.name = user,
		.uid = NO_VAL,
	};

	assoc_mgr_lock_t locks = {
		.user = READ_LOCK
	};

	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(USER_LOCK, READ_LOCK));

	/* Grab the current coord_accts if user exists already */
	(void) assoc_mgr_fill_in_user(mysql_conn, &user_tmp,
				      ACCOUNTING_ENFORCE_ASSOCS,
				      NULL, true);

	/*
	 * The association manager expects the dbd to do all the lifting
	 * here, so we get a full list and then remove from it.
	 */
	user_rec = xmalloc(sizeof(slurmdb_user_rec_t));
	user_rec->name = xstrdup(user_tmp.name);
	user_rec->uid = NO_VAL;
	user_rec->coord_accts = slurmdb_list_copy_coord(
		user_tmp.coord_accts);

	/*
	 * This is needed if the user is being added for the first time right
	 * now as they will not be in the assoc mgr just yet.
	 */
	if (!user_rec->coord_accts)
		user_rec->coord_accts =
			list_create(slurmdb_destroy_coord_rec);

	if (!locked)
		assoc_mgr_unlock(&locks);
	return user_rec;
}

extern slurmdb_user_rec_t *as_mysql_user_add_coord_update(
	mysql_conn_t *mysql_conn, list_t **user_list, char *user, bool locked)
{
	slurmdb_user_rec_t *user_rec;

	xassert(user_list);
	xassert(user);

	if (!*user_list) {
		/* the mysql_conn->update_list will free the contents */
		*user_list = list_create(NULL);
	}

	/* See if we have already added it. */
	if ((user_rec = list_find_first(*user_list, _find_user, user)))
		return user_rec;

	user_rec = _make_user_rec_with_coords(mysql_conn, user, locked);

	if (!user_rec)
		return NULL;

	list_append(*user_list, user_rec);

	/*
	 * NOTE: REMOVE|ADD do the same thing, they both expect the full list so
	 * we can use either one to do the same thing.
	 */
	if (addto_update_list(mysql_conn->update_list,
			      SLURMDB_REMOVE_COORD, user_rec) !=
	    SLURM_SUCCESS) {
		error("Couldn't add removal of coord, this should never happen.");
		slurmdb_destroy_user_rec(user_rec);
		return NULL;
	}

	return user_rec;
}

extern void as_mysql_user_handle_user_coord_flag(slurmdb_user_rec_t *user_rec,
						 slurmdb_assoc_flags_t flags,
						 char *acct)
{
	xassert(user_rec);
	xassert(user_rec->coord_accts);
	xassert(acct);

	if (flags & ASSOC_FLAG_USER_COORD_NO) {
		(void) list_delete_first(user_rec->coord_accts,
					 assoc_mgr_find_nondirect_coord_by_name,
					 acct);
		debug2("Removing user %s from being a coordinator of account %s",
		       user_rec->name, acct);
	} else if ((flags & ASSOC_FLAG_USER_COORD) &&
		   !list_find_first(user_rec->coord_accts,
				    assoc_mgr_find_coord_in_user,
				    acct)) {
		slurmdb_coord_rec_t *coord = xmalloc(sizeof(*coord));

		coord->name = xstrdup(acct);
		list_append(user_rec->coord_accts, coord);
		debug2("Adding user %s as a coordinator of account %s",
		       user_rec->name, acct);
	}
}
