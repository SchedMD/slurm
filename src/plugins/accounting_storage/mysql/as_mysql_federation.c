/*****************************************************************************\
 *  as_mysql_federation.c - functions dealing with federations.
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#include "as_mysql_federation.h"
#include "as_mysql_cluster.h"

char *fed_req_inx[] = {
	"t1.name",
	"t1.flags",
};
enum {
	FED_REQ_NAME,
	FED_REQ_FLAGS,
	FED_REQ_COUNT
};

static int _setup_federation_cond_limits(slurmdb_federation_cond_t *fed_cond,
					 char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;

	if (!fed_cond)
		return 0;

	if (fed_cond->with_deleted)
		xstrcat(*extra, " where (t1.deleted=0 || t1.deleted=1)");
	else
		xstrcat(*extra, " where t1.deleted=0");

	if (fed_cond->cluster_list
	    && list_count(fed_cond->cluster_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(fed_cond->cluster_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t2.name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (fed_cond->federation_list
	    && list_count(fed_cond->federation_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(fed_cond->federation_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	return set;
}

static int _setup_federation_rec_limits(slurmdb_federation_rec_t *fed,
					char **cols, char **vals, char **extra)
{
	if (!fed)
		return SLURM_ERROR;

	if (!(fed->flags & FEDERATION_FLAG_NOTSET)) {
		uint32_t flags;
		xstrcat(*cols, ", flags");
		if (fed->flags & FEDERATION_FLAG_REMOVE) {
			flags = fed->flags & ~FEDERATION_FLAG_REMOVE;
			xstrfmtcat(*vals, ", (flags & ~%u)", flags);
			xstrfmtcat(*extra, ", flags=(flags & ~%u)", flags);
		} else if (fed->flags & FEDERATION_FLAG_ADD) {
			flags = fed->flags & ~FEDERATION_FLAG_ADD;
			xstrfmtcat(*vals, ", (flags | %u)", flags);
			xstrfmtcat(*extra, ", flags=(flags | %u)", flags);
		} else {
			flags = fed->flags;
			xstrfmtcat(*vals, ", %u", flags);
			xstrfmtcat(*extra, ", flags=%u", flags);
		}
	}

	return SLURM_SUCCESS;
}

/*
 * Remove all clusters from federation.
 * IN: mysql_conn - mysql connection
 * IN: fed - fed to remove clusters from
 * IN: exceptions - list of clusters to not remove.
 */
static int _remove_all_clusters_from_fed(mysql_conn_t *mysql_conn,
					 const char *fed, List exceptions)
{
	int   rc    = SLURM_SUCCESS;
	char *query = NULL;
	char *exception_names = NULL;

	if (exceptions && list_count(exceptions)) {
		char *tmp_name;
		ListIterator itr;

		itr = list_iterator_create(exceptions);
		while ((tmp_name = list_next(itr)))
			xstrfmtcat(exception_names, "%s'%s'",
				   (exception_names) ? "," : "",
				   tmp_name);
		list_iterator_destroy(itr);
	}

	xstrfmtcat(query, "UPDATE %s "
		   	  "SET federation='', fed_id=0, fed_state=%u "
			  "WHERE federation='%s' and deleted=0",
		   cluster_table, CLUSTER_FED_STATE_NA, fed);
	if (exception_names)
		xstrfmtcat(query, " AND name NOT IN (%s)", exception_names);

	DB_DEBUG(FEDR, mysql_conn->conn, "query\n%s", query);

	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc)
		error("Failed to remove all clusters from federation %s", fed);

	if (exception_names)
		xfree(exception_names);

	return rc;
}

static int _remove_clusters_from_fed(mysql_conn_t *mysql_conn, List clusters)
{
	int   rc    = SLURM_SUCCESS;
	char *query = NULL;
	char *name  = NULL;
	char *names = NULL;
	ListIterator itr = NULL;

	xassert(clusters);

	itr = list_iterator_create(clusters);
	while ((name = list_next(itr)))
	       xstrfmtcat(names, "%s'%s'", names ? "," : "", name );

	xstrfmtcat(query, "UPDATE %s "
		   	  "SET federation='', fed_id=0, fed_state=%u "
			  "WHERE name IN (%s) and deleted=0",
		   cluster_table, CLUSTER_FED_STATE_NA, names);

	DB_DEBUG(FEDR, mysql_conn->conn, "query\n%s", query);

	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc)
		error("Failed to remove clusters %s from federation", names);
	xfree(names);

	return rc;
}

static int _add_clusters_to_fed(mysql_conn_t *mysql_conn, List clusters,
				const char *fed)
{
	int   rc      = SLURM_SUCCESS;
	char *query   = NULL;
	char *name    = NULL;
	char *names   = NULL;
	char *indexes = NULL;
	ListIterator itr = NULL;
	int   last_id = -1;

	xassert(fed);
	xassert(clusters);

	itr = list_iterator_create(clusters);
	while ((name = list_next(itr))) {
		int id;
		if ((rc = as_mysql_get_fed_cluster_id(mysql_conn, name, fed,
						      last_id, &id)))
			goto end_it;
		last_id = id;
		xstrfmtcat(indexes, "WHEN name='%s' THEN %d ", name, id);
		xstrfmtcat(names, "%s'%s'", names ? "," : "", name);
	}

	/* Keep the same fed_state if the cluster isn't changing feds.
	 * Also note that mysql evaluates from left to right and uses the
	 * updated column values in case statements. So the check for federation
	 * in the fed_state case statement must happen before fed_state is set
	 * or the federation will always equal the federation in the case
	 * statement.  */
	xstrfmtcat(query, "UPDATE %s "
		   	  "SET "
			  "fed_state = CASE WHEN federation='%s' THEN fed_state ELSE %u END, "
			  "fed_id = CASE %s END, "
		   	  "federation='%s' "
			  "WHERE name IN (%s) and deleted=0",
		   cluster_table, fed, CLUSTER_FED_STATE_ACTIVE, indexes, fed,
		   names);

	DB_DEBUG(FEDR, mysql_conn->conn, "query\n%s", query);

	rc = mysql_db_query(mysql_conn, query);
	if (rc)
		error("Failed to add clusters %s to federation %s",
		      names, fed);

end_it:
	xfree(query);
	xfree(names);
	xfree(indexes);
	list_iterator_destroy(itr);

	return rc;
}

static int _assign_clusters_to_federation(mysql_conn_t *mysql_conn,
					  const char *federation,
					  List cluster_list)
{
	int  rc       = SLURM_SUCCESS;
	List add_list = NULL;
	List rem_list = NULL;
	ListIterator itr    = NULL;
	bool clear_clusters = false;
	slurmdb_cluster_rec_t *tmp_cluster = NULL;

	xassert(federation);
	xassert(cluster_list);

	if (!cluster_list || !federation) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	add_list = list_create(xfree_ptr);
	rem_list = list_create(xfree_ptr);

	itr = list_iterator_create(cluster_list);
	while ((tmp_cluster = list_next(itr))) {
		if (!tmp_cluster->name)
			continue;
		if (tmp_cluster->name[0] == '-')
			list_append(rem_list, xstrdup(tmp_cluster->name + 1));
		else if (tmp_cluster->name[0] == '+')
			list_append(add_list, xstrdup(tmp_cluster->name + 1));
		else {
			list_append(add_list, xstrdup(tmp_cluster->name));
			clear_clusters = true;
		}
	}
	list_iterator_destroy(itr);

	if (clear_clusters &&
	    (rc = _remove_all_clusters_from_fed(mysql_conn, federation,
						add_list)))
		goto end_it;
	if (!clear_clusters &&
	    list_count(rem_list) &&
	    (rc = _remove_clusters_from_fed(mysql_conn, rem_list)))
		goto end_it;
	if (list_count(add_list) &&
	    (rc = _add_clusters_to_fed(mysql_conn, add_list, federation)))
		goto end_it;

end_it:
	list_destroy(add_list);
	list_destroy(rem_list);

	return rc;
}

extern int as_mysql_add_federations(mysql_conn_t *mysql_conn, uint32_t uid,
				    List federation_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_federation_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *extra = NULL, *query = NULL,
	     *tmp_extra = NULL;
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
		if (object->cluster_list &&
		    (list_count(federation_list) > 1)) {
			xfree(user_name);
			error("Clusters can only be assigned to one "
			      "federation");
			errno = ESLURM_FED_CLUSTER_MULTIPLE_ASSIGNMENT;
			return  ESLURM_FED_CLUSTER_MULTIPLE_ASSIGNMENT;
		}

		xstrcat(cols, "creation_time, mod_time, name");
		xstrfmtcat(vals, "%ld, %ld, '%s'", now, now, object->name);
		xstrfmtcat(extra, ", mod_time=%ld", now);

		_setup_federation_rec_limits(object, &cols, &vals, &extra);

		xstrfmtcat(query,
			   "insert into %s (%s) values (%s) "
			   "on duplicate key update deleted=0%s",
			   federation_table, cols, vals, extra);
		DB_DEBUG(FEDR, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add federation %s", object->name);
			xfree(cols);
			xfree(vals);
			xfree(extra);
			added = 0;
			break;
		}

		affect_rows = last_affected_rows(mysql_conn);
		if (!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(cols);
			xfree(vals);
			xfree(extra);
			continue;
		}

		if (object->cluster_list &&
		    _assign_clusters_to_federation(mysql_conn, object->name,
						   object->cluster_list)) {
			xfree(cols);
			xfree(vals);
			xfree(extra);
			xfree(user_name);
			return SLURM_ERROR;
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
		xfree(cols);
		xfree(vals);
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
	else
		as_mysql_add_feds_to_update_list(mysql_conn);

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
	slurmdb_federation_rec_t *fed = NULL;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!federation_cond) {
		xstrcat(extra, " where t1.deleted=0");
		goto empty;
	}

	_setup_federation_cond_limits(federation_cond, &extra);

empty:

	xfree(tmp);
	i=0;
	xstrfmtcat(tmp, "%s", fed_req_inx[i]);
	for(i = 1; i < FED_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", fed_req_inx[i]);
	}

	query = xstrdup_printf(
		"select distinct %s from %s as t1 "
		"left join %s as t2 on t1.name=t2.federation and t2.deleted=0"
		"%s order by t1.name",
		tmp, federation_table, cluster_table, extra);
	xfree(tmp);
	xfree(extra);

	DB_DEBUG(FEDR, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	federation_list = list_create(slurmdb_destroy_federation_rec);

	while ((row = mysql_fetch_row(result))) {
 		slurmdb_cluster_cond_t clus_cond;
 		List tmp_list = NULL;
 		fed = xmalloc(sizeof(slurmdb_federation_rec_t));
 		list_append(federation_list, fed);

 		fed->name  = xstrdup(row[FED_REQ_NAME]);
 		fed->flags = slurm_atoul(row[FED_REQ_FLAGS]);

 		/* clusters in federation */
 		slurmdb_init_cluster_cond(&clus_cond, 0);
		clus_cond.federation_list = list_create(xfree_ptr);
 		list_append(clus_cond.federation_list, xstrdup(fed->name));

 		tmp_list = as_mysql_get_clusters(mysql_conn, uid, &clus_cond);
 		FREE_NULL_LIST(clus_cond.federation_list);
 		if (!tmp_list) {
 			error("Unable to get federation clusters");
 			continue;
 		}
 		fed->cluster_list = tmp_list;
	}
	mysql_free_result(result);

	return federation_list;
}

extern List as_mysql_modify_federations(
				mysql_conn_t *mysql_conn, uint32_t uid,
				slurmdb_federation_cond_t *fed_cond,
				slurmdb_federation_rec_t *fed)
{
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	int req_inx = 0;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL,
	     *name_char = NULL, *fed_items = NULL;
	char *tmp_char1 = NULL, *tmp_char2 = NULL;
	time_t now = time(NULL);
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if (!fed_cond || !fed) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!is_user_min_admin_level(mysql_conn, uid,
				     SLURMDB_ADMIN_SUPER_USER)) {
		errno = ESLURM_ACCESS_DENIED;
		return NULL;
	}

	/* force to only do non-deleted federations */
	fed_cond->with_deleted = 0;
	_setup_federation_cond_limits(fed_cond, &extra);
	_setup_federation_rec_limits(fed, &tmp_char1, &tmp_char2, &vals);
	xfree(tmp_char1);
	xfree(tmp_char2);

	if (!extra ||
	    (!vals && (!fed->cluster_list || !list_count(fed->cluster_list)))) {
		xfree(extra);
		xfree(vals);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	if (fed->cluster_list &&
	    fed_cond->federation_list &&
	    (list_count(fed_cond->federation_list) > 1)) {
		xfree(extra);
		xfree(vals);
		error("Clusters can only be assigned to one federation");
		errno = ESLURM_FED_CLUSTER_MULTIPLE_ASSIGNMENT;
		return NULL;
	}

	/* Select records that are going to get updated.
	 * 1 - to be able to report what is getting updated
	 * 2 - to create an update object to let the controller know.  */
	xstrfmtcat(fed_items, "%s", fed_req_inx[req_inx]);
	for(req_inx = 1; req_inx < FED_REQ_COUNT; req_inx++) {
		xstrfmtcat(fed_items, ", %s", fed_req_inx[req_inx]);
	}

	xstrfmtcat(query, "select %s from %s as t1 %s;",
		   fed_items, federation_table, extra);
	xfree(fed_items);

	DB_DEBUG(FEDR, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		xfree(vals);
		xfree(extra);
		error("no result given for %s", extra);
		return NULL;
	}
	xfree(extra);

	ret_list = list_create(xfree_ptr);
	while ((row = mysql_fetch_row(result))) {
		object = xstrdup(row[0]);

		list_append(ret_list, object);
		if (!name_char) {
			xstrfmtcat(name_char, "(name='%s'", object);
		} else  {
			xstrfmtcat(name_char, " || name='%s'", object);
		}
	}
	mysql_free_result(result);

	if (fed->cluster_list &&
	    (_assign_clusters_to_federation(mysql_conn, object,
					    fed->cluster_list))) {
		xfree(vals);
		xfree(name_char);
		xfree(query);
		FREE_NULL_LIST(ret_list);
		return NULL;
	}

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		DB_DEBUG(FEDR, mysql_conn->conn,
		         "didn't affect anything\n%s", query);
		xfree(vals);
		xfree(name_char);
		xfree(query);
		return ret_list;
	}
	xfree(query);
	xstrcat(name_char, ")");

	if (vals) {
		char *user_name = uid_to_string((uid_t) uid);
		rc = modify_common(mysql_conn, DBD_MODIFY_FEDERATIONS, now,
				   user_name, federation_table,
				   name_char, vals, NULL);
		xfree(user_name);
	}
	xfree(name_char);
	xfree(vals);

	if (rc == SLURM_ERROR) {
		error("Couldn't modify federation");
		FREE_NULL_LIST(ret_list);
		ret_list = NULL;
	} else
		as_mysql_add_feds_to_update_list(mysql_conn);

	return ret_list;
}

extern List as_mysql_remove_federations(mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_federation_cond_t *fed_cond)
{
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *extra = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if (!fed_cond) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!is_user_min_admin_level(
		    mysql_conn, uid, SLURMDB_ADMIN_SUPER_USER)) {
		errno = ESLURM_ACCESS_DENIED;
		return NULL;
	}

	/* force to only do non-deleted federations */
	fed_cond->with_deleted = 0;
	_setup_federation_cond_limits(fed_cond, &extra);

	if (!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select name from %s as t1 %s;",
			       federation_table, extra);
	xfree(extra);
	if (!(result = mysql_db_query_ret( mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	rc = 0;
	ret_list = list_create(xfree_ptr);

	if (!mysql_num_rows(result)) {
		mysql_free_result(result);
		errno = SLURM_NO_CHANGE_IN_DATA;
		DB_DEBUG(FEDR, mysql_conn->conn,
		         "didn't affect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	user_name = uid_to_string((uid_t) uid);
	while ((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		list_append(ret_list, object);

		if ((rc = _remove_all_clusters_from_fed(mysql_conn, object,
							NULL)))
			break;

		xfree(name_char);
		xstrfmtcat(name_char, "name='%s'", object);

		if ((rc = remove_common(mysql_conn, DBD_REMOVE_FEDERATIONS, now,
					user_name, federation_table, name_char,
					NULL, NULL, ret_list, NULL, NULL)))
			break;
	}
	mysql_free_result(result);
	xfree(user_name);
	xfree(name_char);

	if (rc != SLURM_SUCCESS) {
		FREE_NULL_LIST(ret_list);
		return NULL;
	} else
		as_mysql_add_feds_to_update_list(mysql_conn);

	return ret_list;
}

extern int as_mysql_add_feds_to_update_list(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_ERROR;
	List feds = as_mysql_get_federations(mysql_conn, 0, NULL);

	/* Even if there are no feds, need to send an empty list for the case
	 * that all feds were removed. The controller needs to know that it was
	 * removed from a federation. */
	if (feds &&
	    ((rc = addto_update_list(mysql_conn->update_list,
				     SLURMDB_UPDATE_FEDS, feds))
	     != SLURM_SUCCESS)) {
			FREE_NULL_LIST(feds);
	}
	return rc;
}
