/*****************************************************************************\
 *  as_mysql_acct.c - functions dealing with accounts.
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
#include "as_mysql_acct.h"
#include "as_mysql_user.h"

typedef struct {
	slurmdb_account_rec_t *acct_in;
	slurmdb_assoc_rec_t *assoc_in;
	char *insert_query;
	char *insert_query_pos;
	mysql_conn_t *mysql_conn;
	time_t now;
	int rc;
	bool ret_str_err;
	char *ret_str;
	char *ret_str_pos;
	char *txn_query;
	char *txn_query_pos;
	char *user_name;
} add_acct_cond_t;


static int _foreach_add_acct(void *x, void *arg)
{
	char *name = x;
	add_acct_cond_t *add_acct_cond = arg;
	slurmdb_account_rec_t *acct;
	slurmdb_assoc_rec_t *assoc;
	char *desc;
	char *org;
	char *extra, *tmp_extra;
	MYSQL_RES *result = NULL;
	int cnt;
	char *query;

	/* Check to see if it is already in the acct_table */
	query = xstrdup_printf("select name from %s where name='%s' and !deleted",
			       acct_table, name);
	result = mysql_db_query_ret(add_acct_cond->mysql_conn, query, 0);

	xfree(query);
	if (!result)
		return -1;

	cnt = mysql_num_rows(result);
	mysql_free_result(result);
	/* If so, just return */
	if (cnt)
		return 0;

	/* Else, add it */
	acct = add_acct_cond->acct_in;
	assoc = add_acct_cond->assoc_in;
	desc = acct->description ? acct->description : x;
	org = acct->organization;

	if (!org) {
		if (assoc->parent_acct && xstrcmp(assoc->parent_acct, "root"))
			org = assoc->parent_acct;
		else
			org = x;
	}

	if (!add_acct_cond->ret_str)
		xstrcatat(add_acct_cond->ret_str, &add_acct_cond->ret_str_pos,
			  " Adding Account(s)\n");

	xstrfmtcatat(add_acct_cond->ret_str, &add_acct_cond->ret_str_pos,
		     "  %s\n", name);

	if (add_acct_cond->insert_query)
		xstrfmtcatat(add_acct_cond->insert_query,
			     &add_acct_cond->insert_query_pos,
			     ", (%ld, %ld, '%s', '%s', '%s')",
			     add_acct_cond->now, add_acct_cond->now,
			     name, desc, org);
	else
		xstrfmtcatat(add_acct_cond->insert_query,
			     &add_acct_cond->insert_query_pos,
			     "insert into %s (creation_time, mod_time, name, description, organization) values (%ld, %ld, '%s', '%s', '%s')",
			     acct_table, add_acct_cond->now, add_acct_cond->now,
			     name, desc, org);

	extra = xstrdup_printf("description='%s', organization='%s'",
			       desc, org);
	tmp_extra = slurm_add_slash_to_quotes(extra);

	if (add_acct_cond->txn_query)
		xstrfmtcatat(add_acct_cond->txn_query,
			     &add_acct_cond->txn_query_pos,
			     ", (%ld, %u, '%s', '%s', '%s')",
			     add_acct_cond->now, DBD_ADD_ACCOUNTS, name,
			     add_acct_cond->user_name, tmp_extra);
	else
		xstrfmtcatat(add_acct_cond->txn_query,
			     &add_acct_cond->txn_query_pos,
			     "insert into %s (timestamp, action, name, actor, info) values (%ld, %u, '%s', '%s', '%s')",
			     txn_table,
			     add_acct_cond->now, DBD_ADD_ACCOUNTS, name,
			     add_acct_cond->user_name, tmp_extra);
	xfree(tmp_extra);
	xfree(extra);

	return 0;
}

extern int as_mysql_add_accts(mysql_conn_t *mysql_conn, uint32_t uid,
			      List acct_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_account_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *query = NULL, *txn_query = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *extra = NULL, *tmp_extra = NULL;

	int affect_rows = 0;
	List assoc_list;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		slurmdb_user_rec_t user;

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
	}

	if (!acct_list || !list_count(acct_list)) {
		error("%s: Trying to add empty account list", __func__);
		return ESLURM_EMPTY_LIST;
	}

	assoc_list = list_create(slurmdb_destroy_assoc_rec);
	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(acct_list);
	while ((object = list_next(itr))) {
		if (!object->name || !object->name[0]
		    || !object->description || !object->description[0]
		    || !object->organization || !object->organization[0]) {
			error("We need an account name, description, and "
			      "organization to add. %s %s %s",
			      object->name, object->description,
			      object->organization);
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name, "
			"description, organization");
		xstrfmtcat(vals, "%ld, %ld, '%s', '%s', '%s'",
			   now, now, object->name,
			   object->description, object->organization);
		xstrfmtcat(extra, ", description='%s', organization='%s'",
			   object->description, object->organization);

		query = xstrdup_printf(
			"insert into %s (%s) values (%s) "
			"on duplicate key update deleted=0, mod_time=%ld %s;",
			acct_table, cols, vals,
			now, extra);
		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(cols);
		xfree(vals);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add acct");
			xfree(extra);
			continue;
		}
		affect_rows = last_affected_rows(mysql_conn);
		//DB_DEBUG(DB_ASSOC, mysql_conn->conn, "affected %d",
		//         affect_rows);

		if (!affect_rows) {
			DB_DEBUG(DB_ASSOC, mysql_conn->conn, "nothing changed");
			xfree(extra);
			continue;
		}

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = slurm_add_slash_to_quotes(extra+2);

		if (txn_query)
			xstrfmtcat(txn_query,
				   ", (%ld, %u, '%s', '%s', '%s')",
				   now, DBD_ADD_ACCOUNTS, object->name,
				   user_name, tmp_extra);
		else
			xstrfmtcat(txn_query,
				   "insert into %s "
				   "(timestamp, action, name, actor, info) "
				   "values (%ld, %u, '%s', '%s', '%s')",
				   txn_table,
				   now, DBD_ADD_ACCOUNTS, object->name,
				   user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);

		if (!object->assoc_list)
			continue;

		if (!assoc_list)
			assoc_list =
				list_create(slurmdb_destroy_assoc_rec);
		list_transfer(assoc_list, object->assoc_list);
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

	if (assoc_list && list_count(assoc_list)) {
		if ((rc = as_mysql_add_assocs(mysql_conn, uid, assoc_list))
		    != SLURM_SUCCESS)
			error("Problem adding accounts associations");
	}
	FREE_NULL_LIST(assoc_list);

	return rc;
}

extern char *as_mysql_add_accts_cond(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_add_assoc_cond_t *add_assoc,
				     slurmdb_account_rec_t *acct)
{
	add_acct_cond_t add_acct_cond;
	char *ret_str = NULL;
	int rc;

	if (check_connection(mysql_conn) != SLURM_SUCCESS) {
		errno = ESLURM_DB_CONNECTION;
		return NULL;
	}

	if (!add_assoc ||
	    !add_assoc->acct_list ||
	    !list_count(add_assoc->acct_list)) {
		errno = ESLURM_EMPTY_LIST;
		return NULL;
	}

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		slurmdb_user_rec_t user;

		memset(&user, 0, sizeof(slurmdb_user_rec_t));
		user.uid = uid;

		if (!is_user_any_coord(mysql_conn, &user)) {
			char *ret_str = xstrdup("Only admins/operators/coordinators can add accounts");
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

	memset(&add_acct_cond, 0, sizeof(add_acct_cond));
	add_acct_cond.acct_in = acct;
	add_acct_cond.assoc_in = &add_assoc->assoc;
	add_acct_cond.mysql_conn = mysql_conn;
	add_acct_cond.now = time(NULL);
	add_acct_cond.user_name = uid_to_string((uid_t) uid);

	/* First add the accounts to the acct_table. */
	if (list_for_each_ro(add_assoc->acct_list, _foreach_add_acct,
			     &add_acct_cond) < 0) {
		rc = add_acct_cond.rc;
		goto end_it;
	}

	if (add_acct_cond.insert_query) {
		xstrfmtcatat(add_acct_cond.insert_query,
			     &add_acct_cond.insert_query_pos,
			     " on duplicate key update deleted=0, description=VALUES(description), mod_time=VALUES(mod_time), organization=VALUES(organization);");
		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s",
			 add_acct_cond.insert_query);
		rc = mysql_db_query(mysql_conn, add_acct_cond.insert_query);
		xfree(add_acct_cond.insert_query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add acct");
			xfree(add_acct_cond.ret_str);
			goto end_it;
		}

		/* Success means we add the defaults to the string */
		xstrfmtcatat(add_acct_cond.ret_str,
			     &add_acct_cond.ret_str_pos,
			     " Settings\n  Description     = %s\n  Organization    = %s\n",
			     acct->description ?
			     acct->description : "Account Name",
			     acct->organization ?
			     acct->organization : "Parent/Account Name");

		xstrcatat(add_acct_cond.txn_query,
			  &add_acct_cond.txn_query_pos,
			  ";");
		rc = mysql_db_query(mysql_conn, add_acct_cond.txn_query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
			rc = SLURM_SUCCESS;
		}
	}

	/* Now add the associations */
	ret_str = as_mysql_add_assocs_cond(mysql_conn, uid, add_assoc);
	rc = errno;

	if (rc == SLURM_NO_CHANGE_IN_DATA) {
		if (add_acct_cond.ret_str)
			rc = SLURM_SUCCESS;
	}

end_it:
	xfree(add_acct_cond.insert_query);
	xfree(add_acct_cond.txn_query);
	xfree(add_acct_cond.user_name);

	if (rc != SLURM_SUCCESS) {
		reset_mysql_conn(mysql_conn);
		if (!add_acct_cond.ret_str_err)
			xfree(add_acct_cond.ret_str);
		else
			xfree(ret_str);
		errno = rc;
		return add_acct_cond.ret_str ? add_acct_cond.ret_str : ret_str;
	}

	if (ret_str) {
		xstrcatat(add_acct_cond.ret_str,
			  &add_acct_cond.ret_str_pos,
			  ret_str);
		xfree(ret_str);
	}

	if (!add_acct_cond.ret_str) {
		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "didn't affect anything");
		errno = SLURM_NO_CHANGE_IN_DATA;
		return NULL;
	}

	errno = SLURM_SUCCESS;
	return add_acct_cond.ret_str;
}

extern List as_mysql_modify_accts(mysql_conn_t *mysql_conn, uint32_t uid,
				  slurmdb_account_cond_t *acct_cond,
				  slurmdb_account_rec_t *acct)
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

	if (!acct_cond || !acct) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		errno = ESLURM_ACCESS_DENIED;
		return NULL;
	}

	xstrcat(extra, "where deleted=0");
	if (acct_cond->assoc_cond
	    && acct_cond->assoc_cond->acct_list
	    && list_count(acct_cond->assoc_cond->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (acct_cond->description_list
	    && list_count(acct_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->description_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (acct_cond->organization_list
	    && list_count(acct_cond->organization_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->organization_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (acct->description)
		xstrfmtcat(vals, ", description='%s'", acct->description);
	if (acct->organization)
		xstrfmtcat(vals, ", organization='%s'", acct->organization);

	if (!extra || !vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	query = xstrdup_printf("select name from %s %s;", acct_table, extra);
	xfree(extra);
	DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		xfree(vals);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(xfree_ptr);
	while ((row = mysql_fetch_row(result))) {
		object = xstrdup(row[0]);
		list_append(ret_list, object);
		if (!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name='%s'", object);
		}

	}
	mysql_free_result(result);

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		DB_DEBUG(DB_ASSOC, mysql_conn->conn,
		         "didn't affect anything\n%s", query);
		xfree(query);
		xfree(vals);
		return ret_list;
	}
	xfree(query);
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = modify_common(mysql_conn, DBD_MODIFY_ACCOUNTS, now,
			   user_name, acct_table, name_char, vals, NULL);
	xfree(user_name);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify accounts");
		FREE_NULL_LIST(ret_list);
		errno = SLURM_ERROR;
		ret_list = NULL;
	}

	xfree(name_char);
	xfree(vals);

	return ret_list;
}

extern List as_mysql_remove_accts(mysql_conn_t *mysql_conn, uint32_t uid,
				  slurmdb_account_cond_t *acct_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	List coord_list = NULL;
	List cluster_list_tmp = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *name_char_pos = NULL,
		*assoc_char = NULL, *assoc_char_pos = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	bool jobs_running = 0;
	bool default_account = 0;

	if (!acct_cond) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
		errno = ESLURM_ACCESS_DENIED;
		return NULL;
	}

	xstrcat(extra, "where deleted=0");
	if (acct_cond->assoc_cond
	    && acct_cond->assoc_cond->acct_list
	    && list_count(acct_cond->assoc_cond->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
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

	if (acct_cond->description_list
	    && list_count(acct_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->description_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (acct_cond->organization_list
	    && list_count(acct_cond->organization_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->organization_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select name from %s %s;", acct_table, extra);
	xfree(extra);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	ret_list = list_create(xfree_ptr);
	while ((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		list_append(ret_list, object);

		if (name_char)
			xstrfmtcatat(name_char, &name_char_pos, ",'%s'",
				     object);
		else
			xstrfmtcatat(name_char, &name_char_pos, "name in('%s'",
				     object);
		xstrfmtcatat(assoc_char, &assoc_char_pos,
			     "%st2.lineage like '%%/%s/%%'",
			     assoc_char ? " || " : "", object);
	}

	if (name_char)
		xstrcatat(name_char, &name_char_pos, ")");

	mysql_free_result(result);

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		DB_DEBUG(DB_ASSOC, mysql_conn->conn,
		         "didn't affect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	/* We need to remove these accounts from the coord's that have it */
	coord_list = as_mysql_remove_coord(
		mysql_conn, uid, ret_list, NULL);
	FREE_NULL_LIST(coord_list);

	user_name = uid_to_string((uid_t) uid);

	slurm_rwlock_rdlock(&as_mysql_cluster_list_lock);
	cluster_list_tmp = list_shallow_copy(as_mysql_cluster_list);
	itr = list_iterator_create(cluster_list_tmp);
	while ((object = list_next(itr))) {
		if ((rc = remove_common(mysql_conn, DBD_REMOVE_ACCOUNTS, now,
					user_name, acct_table, name_char,
					assoc_char, object, ret_list,
					&jobs_running, &default_account))
		    != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(cluster_list_tmp);
	slurm_rwlock_unlock(&as_mysql_cluster_list_lock);

	xfree(user_name);
	xfree(name_char);
	xfree(assoc_char);
	if (rc == SLURM_ERROR) {
		FREE_NULL_LIST(ret_list);
		return NULL;
	}

	if (default_account)
		errno = ESLURM_NO_REMOVE_DEFAULT_ACCOUNT;
	else if (jobs_running)
		errno = ESLURM_JOBS_RUNNING_ON_ASSOC;
	else
		errno = SLURM_SUCCESS;
	return ret_list;
}

extern List as_mysql_get_accts(mysql_conn_t *mysql_conn, uid_t uid,
			       slurmdb_account_cond_t *acct_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List acct_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	slurmdb_user_rec_t user;

	/* if this changes you will need to edit the corresponding enum */
	char *acct_req_inx[] = {
		"name",
		"description",
		"organization",
		"deleted",
	};
	enum {
		SLURMDB_REQ_NAME,
		SLURMDB_REQ_DESC,
		SLURMDB_REQ_ORG,
		SLURMDB_REQ_DELETED,
		SLURMDB_REQ_COUNT
	};

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (slurm_conf.private_data & PRIVATE_DATA_ACCOUNTS) {
		if (!(is_admin = is_user_min_admin_level(
			      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
			if (!is_user_any_coord(mysql_conn, &user)) {
				error("Only admins/coordinators "
				      "can look at account usage");
				errno = ESLURM_ACCESS_DENIED;
				return NULL;
			}
		}
	}

	if (!acct_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if (acct_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");

	if (acct_cond->assoc_cond
	    && acct_cond->assoc_cond->acct_list
	    && list_count(acct_cond->assoc_cond->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (acct_cond->description_list
	    && list_count(acct_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->description_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (acct_cond->organization_list
	    && list_count(acct_cond->organization_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->organization_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

	xfree(tmp);
	xstrfmtcat(tmp, "%s", acct_req_inx[i]);
	for(i=1; i<SLURMDB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", acct_req_inx[i]);
	}

	/* This is here to make sure we are looking at only this user
	 * if this flag is set.  We also include any accounts they may be
	 * coordinator of.
	 */
	if (!is_admin && (slurm_conf.private_data & PRIVATE_DATA_ACCOUNTS)) {
		slurmdb_coord_rec_t *coord = NULL;
		set = 0;
		itr = list_iterator_create(user.coord_accts);
		while ((coord = list_next(itr))) {
			if (set) {
				xstrfmtcat(extra, " || name='%s'",
					   coord->name);
			} else {
				set = 1;
				xstrfmtcat(extra, " && (name='%s'",
					   coord->name);
			}
		}
		list_iterator_destroy(itr);
		if (set)
			xstrcat(extra,")");
	}

	query = xstrdup_printf("select %s from %s %s", tmp, acct_table, extra);
	xfree(tmp);
	xfree(extra);

	DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	acct_list = list_create(slurmdb_destroy_account_rec);

	if (acct_cond && acct_cond->assoc_cond && acct_cond->with_assocs) {
		/* We are going to be freeing the inners of
		   this list in the acct->name so we don't
		   free it here
		*/
		FREE_NULL_LIST(acct_cond->assoc_cond->acct_list);
		acct_cond->assoc_cond->acct_list = list_create(NULL);
		acct_cond->assoc_cond->with_deleted = acct_cond->with_deleted;
	}

	while ((row = mysql_fetch_row(result))) {
		slurmdb_account_rec_t *acct =
			xmalloc(sizeof(slurmdb_account_rec_t));
		list_append(acct_list, acct);

		acct->name =  xstrdup(row[SLURMDB_REQ_NAME]);
		acct->description = xstrdup(row[SLURMDB_REQ_DESC]);
		acct->organization = xstrdup(row[SLURMDB_REQ_ORG]);

		if (slurm_atoul(row[SLURMDB_REQ_DELETED]))
			acct->flags |= SLURMDB_ACCT_FLAG_DELETED;

		if (acct_cond && acct_cond->with_coords)
			acct->coordinators =
				assoc_mgr_acct_coords(mysql_conn, acct->name);

		if (acct_cond && acct_cond->with_assocs) {
			if (!acct_cond->assoc_cond) {
				acct_cond->assoc_cond = xmalloc(
					sizeof(slurmdb_assoc_cond_t));
			}

			list_append(acct_cond->assoc_cond->acct_list,
				    acct->name);
		}
	}
	mysql_free_result(result);

	if (acct_cond && acct_cond->with_assocs && acct_cond->assoc_cond
	    && list_count(acct_cond->assoc_cond->acct_list)) {
		ListIterator assoc_itr = NULL;
		slurmdb_account_rec_t *acct = NULL;
		slurmdb_assoc_rec_t *assoc = NULL;
		List assoc_list = as_mysql_get_assocs(
			mysql_conn, uid, acct_cond->assoc_cond);

		if (!assoc_list) {
			error("no associations");
			return acct_list;
		}

		itr = list_iterator_create(acct_list);
		assoc_itr = list_iterator_create(assoc_list);
		while ((acct = list_next(itr))) {
			while ((assoc = list_next(assoc_itr))) {
				if (xstrcmp(assoc->acct, acct->name))
					continue;

				if (!acct->assoc_list)
					acct->assoc_list = list_create(
						slurmdb_destroy_assoc_rec);
				list_append(acct->assoc_list, assoc);
				list_remove(assoc_itr);
			}
			list_iterator_reset(assoc_itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(assoc_itr);

		FREE_NULL_LIST(assoc_list);
	}

	return acct_list;
}
