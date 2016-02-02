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
#include "as_mysql_cluster.h"

char *fed_req_inx[] = {
	"name",
	"flags",
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

static int _assign_clusters_to_federation(mysql_conn_t *mysql_conn,
					  uint32_t uid, char *federation,
					  List cluster_list) {

	int          rc       = SLURM_SUCCESS;
	List         ret_list = NULL;
	ListIterator itr      = NULL;
	slurmdb_cluster_rec_t *tmp_cluster = NULL;
	slurmdb_cluster_rec_t  rec;
	slurmdb_cluster_cond_t cluster_cond;

	if (!cluster_list || !federation) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = list_create(slurm_destroy_char);

	itr = list_iterator_create(cluster_list);
	while ((tmp_cluster = list_next(itr)))
		list_append(cluster_cond.cluster_list,
			    xstrdup(tmp_cluster->name));

	slurmdb_init_cluster_rec(&rec, 0);
	rec.federation = xstrdup(federation);

	ret_list = as_mysql_modify_clusters(mysql_conn, uid,
					    &cluster_cond, &rec);
	xfree(rec.federation);
	FREE_NULL_LIST(cluster_cond.cluster_list);
	list_iterator_destroy(itr);

	if (!ret_list) {
		error("Couldn't assign clusters to federation %s", federation);
		rc = SLURM_ERROR;
		goto end_it;
	} else if (!list_count(ret_list)) {
		error("No clusters were assigned to the federation %s",
		      federation);
		rc = SLURM_ERROR;
		goto end_it;
	}

end_it:
	FREE_NULL_LIST(ret_list);

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
		if (debug_flags & DEBUG_FLAG_DB_FEDR)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
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
		    _assign_clusters_to_federation(mysql_conn, uid,
						   object->name,
						   object->cluster_list)) {
			xfree(cols);
			xfree(vals);
			xfree(extra);
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
		xstrcat(extra, " where deleted=0");
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
 		slurmdb_cluster_cond_t clus_cond;
 		List tmp_list = NULL;
 		fed = xmalloc(sizeof(slurmdb_federation_rec_t));
 		list_append(federation_list, fed);

 		fed->name  = xstrdup(row[FED_REQ_NAME]);
 		fed->flags = slurm_atoul(row[FED_REQ_FLAGS]);

 		/* clusters in federation */
 		slurmdb_init_cluster_cond(&clus_cond, 0);
 		clus_cond.federation_list = list_create(slurm_destroy_char);
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
	char *user_name = NULL;
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

	if (!extra || !vals) {
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

	xstrfmtcat(query, "select %s from %s%s;",
		   fed_items, federation_table, extra);
	xfree(fed_items);

	if (debug_flags & DEBUG_FLAG_DB_FEDR)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		xfree(vals);
		xfree(extra);
		error("no result given for %s", extra);
		return NULL;
	}
	xfree(extra);

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		object = xstrdup(row[0]);

		list_append(ret_list, object);
		if (!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name='%s'", object);
		}

		/* TODO: build federation_rec and add to update list */
	}
	mysql_free_result(result);

	if (fed->cluster_list &&
	    (_assign_clusters_to_federation(mysql_conn, uid, object,
					    fed->cluster_list))) {
		xfree(vals);
		xfree(name_char);
		xfree(query);
		return NULL;
	}

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		if (debug_flags & DEBUG_FLAG_DB_FEDR)
			DB_DEBUG(mysql_conn->conn,
				 "didn't effect anything\n%s", query);
		xfree(vals);
		xfree(name_char);
		xfree(query);
		return ret_list;
	}
	xfree(query);
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = modify_common(mysql_conn, DBD_MODIFY_FEDERATIONS, now,
			   user_name, federation_table,
			   name_char, vals, NULL);

	xfree(user_name);
	xfree(name_char);
	xfree(vals);

	if (rc == SLURM_ERROR) {
		error("Couldn't modify federation");
		FREE_NULL_LIST(ret_list);
		ret_list = NULL;
	}

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
	bool jobs_running = 0;

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

	query = xstrdup_printf("select name from %s%s;", federation_table,
			       extra);
	xfree(extra);
	if (!(result = mysql_db_query_ret( mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	rc = 0;
	ret_list = list_create(slurm_destroy_char);

	if (!mysql_num_rows(result)) {
		mysql_free_result(result);
		errno = SLURM_NO_CHANGE_IN_DATA;
		if (debug_flags & DEBUG_FLAG_DB_FEDR)
			DB_DEBUG(mysql_conn->conn,
				 "didn't effect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	user_name = uid_to_string((uid_t) uid);
	while ((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		if (!jobs_running)
			list_append(ret_list, object);

		xfree(name_char);
		xstrfmtcat(name_char, "name='%s'", object);
		if (jobs_running)
			xfree(object);
		if ((rc = remove_common(mysql_conn, DBD_REMOVE_FEDERATIONS, now,
					user_name, federation_table,
					name_char, NULL, NULL, ret_list, NULL))
		    != SLURM_SUCCESS)
			break;
	}
	mysql_free_result(result);
	xfree(user_name);
	xfree(name_char);

	if (rc != SLURM_SUCCESS) {
		FREE_NULL_LIST(ret_list);
		return NULL;
	}

	return ret_list;
}
