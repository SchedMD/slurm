/*****************************************************************************\
 *  as_mysql_resource.c - functions dealing with resources.
 *****************************************************************************
 *
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Bill Brophy <bill.brophy@bull.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
#include "as_mysql_resource.h"
#include "as_mysql_usage.h"
#include "as_mysql_wckey.h"
#include "src/common/node_select.h"

static void _setup_res_cond(slurmdb_res_cond_t *res_cond,
			    char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;

	if (!res_cond) {
		xstrcat(*extra, "where t1.deleted=0");
		return;
	}

	if (res_cond->with_deleted)
		xstrcat(*extra, "where (t1.deleted=0 || t1.deleted=1)");
	else
		xstrcat(*extra, "where t1.deleted=0");

	if (res_cond->description_list
	    && list_count(res_cond->description_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(res_cond->description_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (!(res_cond->flags & SLURMDB_RES_FLAG_NOTSET)) {
		xstrfmtcat(*extra, " && (flags & %u)",
			   res_cond->flags & SLURMDB_RES_FLAG_BASE);
	}

	if (res_cond->id_list
	    && list_count(res_cond->id_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(res_cond->id_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "id='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (res_cond->manager_list
	    && list_count(res_cond->manager_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(res_cond->manager_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "manager='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (res_cond->name_list
	    && list_count(res_cond->name_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(res_cond->name_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (res_cond->server_list
	    && list_count(res_cond->server_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(res_cond->server_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "server='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (res_cond->type_list
	    && list_count(res_cond->type_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(res_cond->type_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "type='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}
}

static int _setup_clus_res_cond(slurmdb_res_cond_t *res_cond, char **extra)
{
	ListIterator itr;
	bool set = 0;
	char *tmp = NULL;
	int query_clusters = 0;

	if (!res_cond) {
		xstrfmtcat(*extra, "%st2.deleted=0", *extra ? " && " : "");
		return SLURM_SUCCESS;
	}

	if (res_cond->with_deleted)
		xstrfmtcat(*extra, "%s(t2.deleted=0 || t2.deleted=1)",
			   *extra ? " && " : "");
	else
		xstrfmtcat(*extra, "%st2.deleted=0", *extra ? " && " : "");

	if (res_cond->cluster_list && list_count(res_cond->cluster_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(res_cond->cluster_list);
		while ((tmp = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t2.cluster='%s'", tmp);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
		query_clusters += set;
	}

	if (res_cond->percent_list && list_count(res_cond->percent_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(res_cond->percent_list);
		while ((tmp = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t2.percent_allowed='%s'", tmp);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
		query_clusters += set;
	}

	return query_clusters;
}

static int _setup_res_limits(slurmdb_res_rec_t *res,
			     char **cols, char **vals,
			     char **extra, bool for_add, bool *send_update)
{
	if (!res)
		return SLURM_ERROR;

	if (for_add) {
		/* If we are adding we should make sure we don't get
		   old residue sitting around from a former life.
		*/
		if (res->count == NO_VAL)
			res->count = 0;
		if (res->type == SLURMDB_RESOURCE_NOTSET)
			res->type = SLURMDB_RESOURCE_LICENSE;
	}

	if (res->count != NO_VAL) {
		if (cols)
			xstrcat(*cols, ", count");
		xstrfmtcat(*vals, ", %u", res->count);
		xstrfmtcat(*extra, ", count=%u", res->count);
		if (send_update)
			*send_update = 1;
	}

	if (res->description) {
		if (cols)
			xstrcat(*cols, ", description");
		xstrfmtcat(*vals, ", '%s'", res->description);
		xstrfmtcat(*extra, ", description='%s'",
			   res->description);
	}

	if (!(res->flags & SLURMDB_RES_FLAG_NOTSET)) {
		uint32_t base_flags = (res->flags & SLURMDB_RES_FLAG_BASE);
		if (cols)
			xstrcat(*cols, ", flags");
		if (res->flags & SLURMDB_RES_FLAG_REMOVE) {
			xstrfmtcat(*vals, ", (VALUES(flags) & ~%u)'",
				   base_flags);
			xstrfmtcat(*extra, ", flags=(flags & ~%u)",
				   base_flags);
		} else if (res->flags & SLURMDB_RES_FLAG_ADD) {
			xstrfmtcat(*vals, ", (VALUES(flags) | %u)'",
				   base_flags);
			xstrfmtcat(*extra, ", flags=(flags | %u)",
				   base_flags);
		} else {
			xstrfmtcat(*vals, ", '%u'", base_flags);
			xstrfmtcat(*extra, ", flags=%u", base_flags);
		}
		if (send_update)
			*send_update = 1;
	}

	if (res->manager) {
		if (cols)
			xstrcat(*cols, ", manager");
		xstrfmtcat(*vals, ", '%s'", res->manager);
		xstrfmtcat(*extra, ", manager='%s'", res->manager);
	}

	if (res->type != SLURMDB_RESOURCE_NOTSET) {
		if (cols)
			xstrcat(*cols, ", type");
		xstrfmtcat(*vals, ", %u", res->type);
		xstrfmtcat(*extra, ", type=%u", res->type);
		if (send_update)
			*send_update = 1;
	}

	return SLURM_SUCCESS;
}


static uint32_t _get_res_used(mysql_conn_t *mysql_conn, uint32_t res_id,
			      char *extra)
{
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint32_t percent_used = NO_VAL;

	xassert(res_id != NO_VAL);

	/* When extra comes in it will have deleted in there as well,
	 * it appears mysql only uses the first one here and gives us
	 * what we want.
	 */
	query = xstrdup_printf("select distinct SUM(percent_allowed) "
			       "from %s as t2 where deleted=0 && res_id=%u",
			       clus_res_table, res_id);
	if (extra)
		xstrfmtcat(query, " && !(%s)", extra);

	if (debug_flags & DEBUG_FLAG_DB_RES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return percent_used;
	}
	xfree(query);

	if (!(row = mysql_fetch_row(result))) {
		error("Resource id %u is not known on the system", res_id);
		return percent_used;
	}

	/* Overwrite everything just to make sure the client side
	   didn't try anything tricky.
	*/
	if (row[0] && row[0][0])
		percent_used = slurm_atoul(row[0]);

	mysql_free_result(result);

	return percent_used;
}

static int _fill_in_res_rec(mysql_conn_t *mysql_conn, slurmdb_res_rec_t *res)
{
	int rc = SLURM_SUCCESS, i;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *res_req_inx[] = {
		"count",
		"flags",
		"id",
		"name",
		"server",
		"type",
		"SUM(percent_allowed)",
	};
	enum {
		RES_REQ_COUNT,
		RES_REQ_FLAGS,
		RES_REQ_ID,
		RES_REQ_NAME,
		RES_REQ_SERVER,
		RES_REQ_TYPE,
		RES_REQ_PU,
		RES_REQ_NUMBER,
	};

	xassert(res);
	xassert(res->id != NO_VAL);

	xfree(tmp);
	xstrfmtcat(tmp, "%s", res_req_inx[0]);
	for (i=1; i<RES_REQ_NUMBER; i++) {
		xstrfmtcat(tmp, ", %s", res_req_inx[i]);
	}

	query = xstrdup_printf("select distinct %s from %s as t1 "
			       "left outer join "
			       "%s as t2 on (res_id=id && "
			       "t2.deleted=0) "
			       "where id=%u group by id",
			       tmp, res_table, clus_res_table, res->id);

	xfree(tmp);

	if (debug_flags & DEBUG_FLAG_DB_RES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(row = mysql_fetch_row(result))) {
		error("Resource id %u is not known on the system", res->id);
		return SLURM_ERROR;
	}

	/* Overwrite everything just to make sure the client side
	   didn't try anything tricky.
	*/
	if (row[RES_REQ_COUNT] && row[RES_REQ_COUNT][0])
		res->count = slurm_atoul(row[RES_REQ_COUNT]);
	if (row[RES_REQ_FLAGS] && row[RES_REQ_FLAGS][0])
		res->flags = slurm_atoul(row[RES_REQ_FLAGS]);
	if (row[RES_REQ_NAME] && row[RES_REQ_NAME][0]) {
		xfree(res->name);
		res->name = xstrdup(row[RES_REQ_NAME]);
	}
	if (row[RES_REQ_SERVER] && row[RES_REQ_SERVER][0]) {
		xfree(res->server);
		res->server = xstrdup(row[RES_REQ_SERVER]);
	}
	if (row[RES_REQ_TYPE] && row[RES_REQ_TYPE][0])
		res->type = slurm_atoul(row[RES_REQ_TYPE]);
	if (row[RES_REQ_PU] && row[RES_REQ_PU][0])
		res->percent_used = slurm_atoul(row[RES_REQ_PU]);
	else
		res->percent_used = 0;

	mysql_free_result(result);

	return rc;
}

static int _add_res(mysql_conn_t *mysql_conn, slurmdb_res_rec_t *object,
		    char *user_name, int *added, ListIterator itr_in)
{
	char *cols = NULL, *extra = NULL, *vals = NULL, *query = NULL,
		*tmp_extra = NULL;
	time_t now = time(NULL);
	int affect_rows = 0;
	int rc = SLURM_SUCCESS;

	if (!object->name || !object->name[0]) {
		error("We need a resource name to add.");
		return SLURM_ERROR;
	}
	if (!object->server || !object->server[0]) {
		error("We need a resource server to add.");
		return SLURM_ERROR;
	}

	xstrcat(cols, "creation_time, mod_time, name, server");
	xstrfmtcat(vals, "%ld, %ld, '%s', '%s'", now, now,
		   object->name, object->server);
	xstrfmtcat(extra, ", mod_time=%ld", now);

	_setup_res_limits(object, &cols, &vals, &extra, 1, NULL);

	xstrfmtcat(query,
		   "insert into %s (%s) values (%s) "
		   "on duplicate key update deleted=0, "
		   "id=LAST_INSERT_ID(id)%s;",
		   res_table, cols, vals, extra);


	if (debug_flags & DEBUG_FLAG_DB_RES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	object->id = mysql_db_insert_ret_id(mysql_conn, query);
	xfree(query);
	if (!object->id) {
		error("Couldn't add server resource %s", object->name);
		(*added) = 0;
		xfree(cols);
		xfree(extra);
		xfree(vals);
		return SLURM_ERROR;
	}

	affect_rows = last_affected_rows(mysql_conn);

	if (!affect_rows) {
		xfree(cols);
		xfree(extra);
		xfree(vals);
		return SLURM_SUCCESS;
	}

	/* we always have a ', ' as the first 2 chars */
	tmp_extra = slurm_add_slash_to_quotes(extra+2);

	xstrfmtcat(query,
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%ld, %u, '%u', '%s', '%s');",
		   txn_table,
		   now, DBD_ADD_RES, object->id, user_name,
		   tmp_extra);

	xfree(tmp_extra);
	xfree(cols);
	xfree(extra);
	xfree(vals);
	if (debug_flags & DEBUG_FLAG_DB_RES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS)
		error("Couldn't add txn");
	else
		(*added)++;

	return rc;
}

static int _add_clus_res(mysql_conn_t *mysql_conn, slurmdb_res_rec_t *res,
			 char *user_name, int *added)
{
	char *cols = NULL, *extra = NULL, *vals = NULL, *query = NULL,
		*tmp_extra = NULL, *name = NULL;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS, cluster_cnt;
	slurmdb_clus_res_rec_t *object;
	ListIterator itr;

	if (res->id == NO_VAL) {
		error("We need a server resource name to add to.");
		return SLURM_ERROR;
	} else if (!res->clus_res_list
		   || !(cluster_cnt = list_count(res->clus_res_list))) {
		error("No clusters given to add to %s@%s",
		      res->name, res->server);
		return SLURM_ERROR;
	}

	xstrcat(cols, "creation_time, mod_time, "
		"res_id, cluster, percent_allowed");
	xstrfmtcat(vals, "%ld, %ld, '%u'", now, now, res->id);

	itr = list_iterator_create(res->clus_res_list);
	while ((object = list_next(itr))) {
		res->percent_used += object->percent_allowed;
		if (res->percent_used > 100) {
			rc = ESLURM_OVER_ALLOCATE;
			if (debug_flags & DEBUG_FLAG_DB_RES)
				DB_DEBUG(mysql_conn->conn,
					 "Adding a new cluster with %u%% "
					 "allowed to "
					 "resource %s@%s would put the usage "
					 "at %u%%, (which is over 100%%).  "
					 "Please redo your math "
					 "and resubmit.",
					 object->percent_allowed,
					 res->name, res->server,
					 res->percent_used);
			break;
		}
		xfree(extra);
		xstrfmtcat(extra, ", mod_time=%ld, percent_allowed=%u",
			   now, object->percent_allowed);
		xstrfmtcat(query,
			   "insert into %s (%s) values (%s, '%s', %u) "
			   "on duplicate key update deleted=0%s;",
			   clus_res_table, cols, vals,
			   object->cluster, object->percent_allowed, extra);

		if (debug_flags & DEBUG_FLAG_DB_RES)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add cluster %s to resource %s@%s",
			      object->cluster, res->name, res->server);
			(*added) = 0;
			xfree(extra);
			continue;
		}

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = slurm_add_slash_to_quotes(extra+2);
		name = xstrdup_printf("%u@%s", res->id, object->cluster);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%ld, %u, '%s', '%s', '%s');",
			   txn_table,
			   now, DBD_ADD_RES, name, user_name, tmp_extra);
		xfree(name);
		xfree(tmp_extra);
		xfree(extra);
		if (debug_flags & DEBUG_FLAG_DB_RES)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			error("Couldn't add txn");
		else {
			slurmdb_res_rec_t *res_rec =
				xmalloc(sizeof(slurmdb_res_rec_t));
			slurmdb_init_res_rec(res_rec, 0);

			res_rec->count = res->count;
			res_rec->id = res->id;
			res_rec->name = xstrdup(res->name);
			res_rec->server = xstrdup(res->server);
			res_rec->type = res->type;

			res_rec->clus_res_rec =
				xmalloc(sizeof(slurmdb_clus_res_rec_t));
			res_rec->clus_res_rec->cluster =
				xstrdup(object->cluster);
			res_rec->clus_res_rec->percent_allowed =
				object->percent_allowed;

			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_ADD_RES,
					      res_rec) != SLURM_SUCCESS)
				slurmdb_destroy_res_rec(res_rec);
			else
				(*added)++;
		}
	}
	xfree(cols);
	xfree(vals);

	return rc;
}

static List _get_clus_res(mysql_conn_t *mysql_conn, uint32_t res_id,
			  char *extra)
{
	List ret_list;
	char *query = NULL, *tmp = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int i;

	/* if this changes you will need to edit the corresponding enum */
	char *res_req_inx[] = {
		"cluster",
		"percent_allowed",
	};
	enum {
		RES_REQ_CLUSTER,
		RES_REQ_PA,
		RES_REQ_NUMBER,
	};

	xfree(tmp);
	xstrfmtcat(tmp, "%s", res_req_inx[0]);
	for(i=1; i<RES_REQ_NUMBER; i++) {
		xstrfmtcat(tmp, ", %s", res_req_inx[i]);
	}

	query = xstrdup_printf(
		"select %s from %s as t2 where %s && (res_id=%u);",
		tmp, clus_res_table, extra, res_id);
	xfree(tmp);
	if (debug_flags & DEBUG_FLAG_DB_RES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	if (!mysql_num_rows(result)) {
		mysql_free_result(result);
		return NULL;
	}

	ret_list = list_create(slurmdb_destroy_clus_res_rec);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_clus_res_rec_t *clus_res_rec =
			xmalloc(sizeof(slurmdb_clus_res_rec_t));

		list_append(ret_list, clus_res_rec);

		if (row[0] && row[0][0])
			clus_res_rec->cluster = xstrdup(row[0]);
		if (row[1] && row[1][0])
			clus_res_rec->percent_allowed = slurm_atoul(row[1]);
	}
	mysql_free_result(result);

	return ret_list;
}

extern int as_mysql_add_res(mysql_conn_t *mysql_conn, uint32_t uid,
			    List res_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_res_rec_t *object = NULL;
	char *user_name = NULL;
	int added = 0;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(res_list);
	while ((object = list_next(itr))) {
		if (object->id == NO_VAL) {
			if (!object->name || !object->name[0]) {
				error("We need a server resource name to add.");
				rc = SLURM_ERROR;
				continue;
			}
			if ((rc = _add_res(mysql_conn, object,
					   user_name, &added, itr))
			    != SLURM_SUCCESS)
				break;

			/* Since we are adding it make sure we don't
			 * over commit it on the clusters we add.
			 */
			object->percent_used = 0;
		} else {
			if (_fill_in_res_rec(mysql_conn, object) !=
			    SLURM_SUCCESS) {
				rc = SLURM_ERROR;
				error("Unknown id %u", object->id);
				continue;
			}
		}

		if (object->clus_res_list
		    && list_count(object->clus_res_list)) {
			if ((rc = _add_clus_res(mysql_conn, object,
						user_name, &added))
			    != SLURM_SUCCESS)
				break;
		}
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if (!added)
		reset_mysql_conn(mysql_conn);

	return rc;
}

extern List as_mysql_get_res(mysql_conn_t *mysql_conn, uid_t uid,
			     slurmdb_res_cond_t *res_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *clus_extra = NULL;
	char *tmp = NULL;
	List res_list = NULL;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *res_req_inx[] = {
		"count",
		"description",
		"flags",
		"id",
		"manager",
		"name",
		"server",
		"type",
		"SUM(percent_allowed)",
	};
	enum {
		RES_REQ_COUNT,
		RES_REQ_DESC,
		RES_REQ_FLAGS,
		RES_REQ_ID,
		RES_REQ_MANAGER,
		RES_REQ_NAME,
		RES_REQ_SERVER,
		RES_REQ_TYPE,
		RES_REQ_PU,
		RES_REQ_NUMBER,
	};

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	_setup_res_cond(res_cond, &extra);

	xfree(tmp);
	xstrfmtcat(tmp, "%s", res_req_inx[0]);
	for(i=1; i<RES_REQ_NUMBER; i++) {
		xstrfmtcat(tmp, ", %s", res_req_inx[i]);
	}

	query = xstrdup_printf("select distinct %s from %s as t1 "
			       "left outer join "
			       "%s as t2 on (res_id=id%s) %s group by "
			       "id",
			       tmp, res_table, clus_res_table,
			       (!res_cond || !res_cond->with_deleted) ?
			       " && t2.deleted=0" : "",
			       extra);
	xfree(tmp);
	xfree(extra);

	if (debug_flags & DEBUG_FLAG_DB_RES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	if (res_cond && res_cond->with_clusters)
		_setup_clus_res_cond(res_cond, &clus_extra);

	res_list = list_create(slurmdb_destroy_res_rec);
	while ((row = mysql_fetch_row(result))) {
		uint32_t id = 0;
		List clus_res_list = NULL;
		slurmdb_res_rec_t *res;

		if (row[RES_REQ_ID] && row[RES_REQ_ID][0])
			id = slurm_atoul(row[RES_REQ_ID]);
		else {
			error("as_mysql_get_res: no id? this "
			      "should never happen");
			continue;
		}

		if (res_cond && res_cond->with_clusters) {
			clus_res_list =	_get_clus_res(
				mysql_conn, id, clus_extra);
			/* This means the clusters requested don't have
			   claim to this resource, so continue. */
			if (!clus_res_list && (res_cond->with_clusters == 1))
				continue;
		}

		res = xmalloc(sizeof(slurmdb_res_rec_t));
		list_append(res_list, res);

		slurmdb_init_res_rec(res, 0);

		res->id = id;
		res->clus_res_list = clus_res_list;
		clus_res_list = NULL;

		if (row[RES_REQ_COUNT] && row[RES_REQ_COUNT][0])
			res->count = slurm_atoul(row[RES_REQ_COUNT]);
		if (row[RES_REQ_DESC] && row[RES_REQ_DESC][0])
			res->description = xstrdup(row[RES_REQ_DESC]);
		if (row[RES_REQ_FLAGS] && row[RES_REQ_FLAGS][0])
			res->flags = slurm_atoul(row[RES_REQ_FLAGS]);
		if (row[RES_REQ_MANAGER] && row[RES_REQ_MANAGER][0])
			res->manager = xstrdup(row[RES_REQ_MANAGER]);
		if (row[RES_REQ_NAME] && row[RES_REQ_NAME][0])
			res->name = xstrdup(row[RES_REQ_NAME]);
		if (row[RES_REQ_SERVER] && row[RES_REQ_SERVER][0])
			res->server = xstrdup(row[RES_REQ_SERVER]);
		if (row[RES_REQ_TYPE] && row[RES_REQ_TYPE][0])
			res->type = slurm_atoul(row[RES_REQ_TYPE]);

		if (row[RES_REQ_PU] && row[RES_REQ_PU][0])
			res->percent_used = slurm_atoul(row[RES_REQ_PU]);
		else
			res->percent_used = 0;
	}
	mysql_free_result(result);
	xfree(clus_extra);

	return res_list;
}

extern List as_mysql_remove_res(mysql_conn_t *mysql_conn, uint32_t uid,
				slurmdb_res_cond_t *res_cond)
{
	List ret_list = NULL;
	char *name_char = NULL, *clus_char = NULL;
	char *user_name = NULL;
	char *query = NULL, *extra = NULL, *clus_extra = NULL;
	time_t now = time(NULL);
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int query_clusters;
	bool res_added = 0;
	bool have_clusters = 0;
	int last_res = -1;

	if (!res_cond) {
		error("we need something to remove");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	/* force to only do non-deleted server resources */
	res_cond->with_deleted = 0;

	_setup_res_cond(res_cond, &extra);
	query_clusters = _setup_clus_res_cond(res_cond, &clus_extra);

	query = xstrdup_printf("select id, name, server, cluster "
			       "from %s as t1 left outer join "
			       "%s as t2 on (res_id = id%s) %s && %s;",
			       res_table, clus_res_table,
			       (!res_cond || !res_cond->with_deleted) ?
			       " && t2.deleted=0" : "",
			       extra, clus_extra);
	xfree(clus_extra);

	if (debug_flags & DEBUG_FLAG_DB_RES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	if (!mysql_num_rows(result)) {
		mysql_free_result(result);
		query_clusters = 0;
		query = xstrdup_printf("select id, name, server "
				       "from %s as t1 %s;",
				       res_table, extra);
		if (debug_flags & DEBUG_FLAG_DB_RES)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			xfree(extra);
			return NULL;
		}
		xfree(query);
	} else
		have_clusters = 1;

	xfree(extra);

	name_char = NULL;
	ret_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		char *name = NULL;
		int curr_res = atoi(row[0]);

		if (last_res != curr_res) {
			res_added = 0;
			last_res = curr_res;
		}

		if (query_clusters) {
			xstrfmtcat(clus_char,
				   "%s(res_id='%s' && cluster='%s')",
				   clus_char ? " || " : "", row[0], row[3]);
		} else {
			if (!res_added) {
				name = xstrdup_printf("%s@%s", row[1], row[2]);
				list_append(ret_list, name);
				res_added = 1;
				name = NULL;
			}
			xstrfmtcat(name_char, "%sid='%s'",
				   name_char ? " || " : "", row[0]);
			xstrfmtcat(clus_char, "%sres_id='%s'",
				   clus_char ? " || " : "", row[0]);
		}
		if (have_clusters && row[3] && row[3][0]) {
			slurmdb_res_rec_t *res_rec =
				xmalloc(sizeof(slurmdb_res_rec_t));
			slurmdb_init_res_rec(res_rec, 0);
			res_rec->id = curr_res;
			res_rec->clus_res_rec =
				xmalloc(sizeof(slurmdb_clus_res_rec_t));
			res_rec->clus_res_rec->cluster = xstrdup(row[3]);
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_REMOVE_RES, res_rec)
			    != SLURM_SUCCESS)
				slurmdb_destroy_res_rec(res_rec);

			name = xstrdup_printf("Cluster - %s\t- %s@%s",
					      row[3], row[1], row[2]);
		} else if (!res_added)
			name = xstrdup_printf("%s@%s", row[1], row[2]);

		if (name)
			list_append(ret_list, name);
	}
	mysql_free_result(result);

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		if (debug_flags & DEBUG_FLAG_DB_RES)
			DB_DEBUG(mysql_conn->conn,
				 "didn't effect anything\n%s", query);
		xfree(query);
		xfree(name_char);
		xfree(clus_extra);
		return ret_list;
	}

	xfree(query);

	user_name = uid_to_string((uid_t) uid);
	if (query_clusters) {
		remove_common(mysql_conn, DBD_REMOVE_CLUS_RES,
			      now, user_name, clus_res_table,
			      clus_char, NULL, NULL, NULL, NULL);
	} else {
		remove_common(mysql_conn, DBD_REMOVE_CLUS_RES,
			      now, user_name, clus_res_table,
			      clus_char, NULL, NULL, NULL, NULL);
		remove_common(mysql_conn, DBD_REMOVE_RES,
			      now, user_name, res_table,
			      name_char, NULL, NULL, NULL, NULL);
	}

	xfree(clus_char);
	xfree(name_char);
	xfree(user_name);

	return ret_list;
}

extern List as_mysql_modify_res(mysql_conn_t *mysql_conn, uint32_t uid,
				slurmdb_res_cond_t *res_cond,
				slurmdb_res_rec_t *res)
{
	List ret_list = NULL;
	char *vals = NULL, *clus_vals = NULL;
	time_t now = time(NULL);
	char *user_name = NULL, *tmp = NULL;
	char *name_char = NULL, *clus_char = NULL;
	char *query = NULL;
	char *extra = NULL;
	char *clus_extra = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;
	int query_clusters = 0;
	bool send_update = 0;
	bool res_added = 0;
	bool have_clusters = 0;
	int last_res = -1;
	uint32_t percent_used = 0;

	if (!res_cond || !res) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS) {
		return NULL;
	}

	_setup_res_limits(res, NULL, &tmp, &vals, 0, &send_update);

	xfree(tmp);

	/* overloaded for easibility */
	if (res->percent_used != (uint16_t)NO_VAL) {
		xstrfmtcat(clus_vals, ", percent_allowed=%u",
			   res->percent_used);
		send_update = 1;
		query_clusters++;
	}

	if (!vals && !clus_vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	/* force to only do non-deleted resources */
	res_cond->with_deleted = 0;
	_setup_res_cond(res_cond, &extra);
	query_clusters += _setup_clus_res_cond(res_cond, &clus_extra);

	if (query_clusters || send_update)
		query = xstrdup_printf("select id, name, server, cluster "
				       "from %s as t1 left outer join "
				       "%s as t2 on (res_id = id%s) %s && %s;",
				       res_table, clus_res_table,
				       (!res_cond || !res_cond->with_deleted) ?
				       " && t2.deleted=0" : "",
				       extra, clus_extra);
	else
		query = xstrdup_printf("select id, name, server "
				       "from %s as t1 %s;",
				       res_table, extra);

	if (debug_flags & DEBUG_FLAG_DB_RES)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(extra);
		xfree(vals);
		xfree(clus_extra);
		xfree(clus_vals);
		xfree(query);
		return NULL;
	}

	if (!mysql_num_rows(result)) {
		xfree(query);
		mysql_free_result(result);
		result = NULL;
		/* since no clusters are there no reason to send
		   updates */
		query_clusters = 0;
		send_update = 0;
	} else
		have_clusters = 1;

	if (!query_clusters && !vals) {
		xfree(clus_vals);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	if (!result) {
		query = xstrdup_printf("select id, name, server "
				       "from %s as t1 %s;",
				       res_table, extra);
		if (debug_flags & DEBUG_FLAG_DB_RES)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(extra);
			xfree(vals);
			xfree(clus_extra);
			xfree(clus_vals);
			xfree(query);
			return NULL;
		}
	}

	xfree(extra);

	name_char = NULL;
	ret_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		char *name = NULL;
		int curr_res = atoi(row[0]);

		if (last_res != curr_res) {
			res_added = 0;
			last_res = curr_res;

			if (have_clusters &&
			    (res->percent_used != (uint16_t)NO_VAL)) {
				percent_used = _get_res_used(
					mysql_conn, curr_res, clus_extra);

				if (percent_used == NO_VAL)
					percent_used = 0;
			}
		}

		if (query_clusters) {
			xstrfmtcat(clus_char,
				   "%s(res_id='%s' && cluster='%s')",
				   clus_char ? " || " : "", row[0], row[3]);
		} else {
			if (!res_added) {
				name = xstrdup_printf("%s@%s", row[1], row[2]);
				list_append(ret_list, name);
				res_added = 1;
				name = NULL;
			}
			xstrfmtcat(name_char, "%sid='%s'",
				   name_char ? " || " : "", row[0]);
			xstrfmtcat(clus_char, "%sres_id='%s'",
				   clus_char ? " || " : "", row[0]);
		}
		if (have_clusters && row[3] && row[3][0]) {
			slurmdb_res_rec_t *res_rec;

			if (res->percent_used != (uint16_t)NO_VAL)
				percent_used += res->percent_used;
			if (percent_used > 100) {
				if (debug_flags & DEBUG_FLAG_DB_RES)
					DB_DEBUG(mysql_conn->conn,
						 "Modifing resource %s@%s "
						 "with %u%% allowed to each "
						 "cluster would put the usage "
						 "at %u%%, (which is "
						 "over 100%%).  Please redo "
						 "your math and resubmit.",
						 row[1], row[2],
						 res->percent_used,
						 percent_used);

				mysql_free_result(result);
				xfree(clus_extra);
				xfree(query);
				xfree(vals);
				xfree(name_char);
				xfree(clus_char);
				FREE_NULL_LIST(ret_list);
				errno = ESLURM_OVER_ALLOCATE;

				return NULL;
			}

			res_rec = xmalloc(sizeof(slurmdb_res_rec_t));
			slurmdb_init_res_rec(res_rec, 0);
			res_rec->count = res->count;
			res_rec->flags = res->flags;
			res_rec->id = curr_res;
			res_rec->type = res->type;

			res_rec->clus_res_rec =
				xmalloc(sizeof(slurmdb_clus_res_rec_t));
			res_rec->clus_res_rec->cluster = xstrdup(row[3]);
			res_rec->clus_res_rec->percent_allowed =
				res->percent_used;
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_MODIFY_RES, res_rec)
			    != SLURM_SUCCESS)
				slurmdb_destroy_res_rec(res_rec);

			name = xstrdup_printf("Cluster - %s\t- %s@%s",
					      row[3], row[1], row[2]);
		} else if (!res_added)
			name = xstrdup_printf("%s@%s", row[1], row[2]);

		if (name)
			list_append(ret_list, name);
	}
	mysql_free_result(result);

	xfree(clus_extra);

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		if (debug_flags & DEBUG_FLAG_DB_RES)
			DB_DEBUG(mysql_conn->conn,
				 "didn't effect anything\n%s", query);
		xfree(query);
		xfree(vals);
		xfree(name_char);
		xfree(clus_char);

		return ret_list;
	}
	xfree(query);

	user_name = uid_to_string((uid_t) uid);
	if (query_clusters) {
		modify_common(mysql_conn, DBD_MODIFY_CLUS_RES,
			      now, user_name, clus_res_table,
			      clus_char, clus_vals, NULL);
	} else {
		if (clus_char && clus_vals) {
			modify_common(mysql_conn, DBD_MODIFY_CLUS_RES,
				      now, user_name, clus_res_table,
				      clus_char, clus_vals, NULL);
		}
		modify_common(mysql_conn, DBD_MODIFY_RES,
			      now, user_name, res_table,
			      name_char, vals, NULL);
	}

	xfree(vals);
	xfree(clus_vals);
	xfree(clus_char);
	xfree(name_char);
	xfree(user_name);

	if (rc != SLURM_SUCCESS) {
		error("Couldn't modify Resource");
		FREE_NULL_LIST(ret_list);
		errno = SLURM_ERROR;
	}

	return ret_list;
}
