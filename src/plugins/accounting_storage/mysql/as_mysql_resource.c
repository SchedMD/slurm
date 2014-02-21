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
	};
	enum {
		RES_REQ_COUNT,
		RES_REQ_FLAGS,
		RES_REQ_ID,
		RES_REQ_NAME,
		RES_REQ_SERVER,
		RES_REQ_TYPE,
		RES_REQ_NUMBER,
	};

	xassert(res);
	xassert(res->id != NO_VAL);

	xfree(tmp);
	xstrfmtcat(tmp, "%s", res_req_inx[0]);
	for (i=1; i<RES_REQ_NUMBER; i++) {
		xstrfmtcat(tmp, ", %s", res_req_inx[i]);
	}

	query = xstrdup_printf("select distinct %s from %s where id=%u;",
			       tmp, res_table, res->id);

	xfree(tmp);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
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


	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
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
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
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
	int rc = SLURM_SUCCESS;
	slurmdb_clus_res_rec_t *object;
	ListIterator itr;

	if (res->id == NO_VAL) {
		error("We need a server resource name to add to.");
		return SLURM_ERROR;
	} else if (!res->clus_res_list || !list_count(res->clus_res_list)) {
		error("No clusters given to add to %s@%s",
		      res->name, res->server);
		return SLURM_ERROR;
	}
	xstrcat(cols, "creation_time, mod_time, "
		"res_id, cluster, percent_allowed");
	xstrfmtcat(vals, "%ld, %ld, '%u'", now, now, res->id);

	itr = list_iterator_create(res->clus_res_list);
	while ((object = list_next(itr))) {
		xfree(extra);
		xstrfmtcat(extra, ", mod_time=%ld, percent_allowed=%u",
			   now, object->percent_allowed);
		xstrfmtcat(query,
			   "insert into %s (%s) values (%s, '%s', %u) "
			   "on duplicate key update deleted=0%s;",
			   clus_res_table, cols, vals,
			   object->cluster, object->percent_allowed, extra);

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
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
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
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

extern List as_mysql_get_clus_res(mysql_conn_t *mysql_conn, uid_t uid,
				  slurmdb_clus_res_cond_t *clus_res_cond)
{
	int rc = 0;
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List ser_res_list = NULL;
	List clus_res_list = NULL;
	ListIterator itr = NULL;
	ListIterator itr1 = NULL;
	char *object = NULL;
	char *cluster_name = NULL;
	int set = 0;
	int record_id = 0;
	int i=0;
	bool created_clus_res_cond = false;
	slurmdb_ser_res_rec_t *ser_res = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *clus_res_req_inx[] = {
		"id",
		"percent_allowed",
	};
	enum {
		CLUS_RES_REQ_ID,
		CLUS_RES_REQ_ALLOWED,
		CLUS_RES_REQ_NUMBER
	};

	char *ser_res_req_inx[] = {
		"name",
		"description",
		"id",
		"manager",
		"server",
		"count",
		"type",
	};

	enum {
		SER_RES_REQ_NAME,
		SER_RES_REQ_DESC,
		SER_RES_REQ_ID,
		SER_RES_REQ_MANAGER,
		SER_RES_REQ_SERVER,
		SER_RES_REQ_COUNT,
		SER_RES_REQ_TYPE,
		SER_RES_REQ_NUMBER,
	};

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if (!clus_res_cond) {
		xstrcat(extra, "where deleted=0");
		debug3("%d(%s:%d)\n",
		       mysql_conn->conn, THIS_FILE, __LINE__);
		clus_res_cond = xmalloc(sizeof(slurmdb_clus_res_cond_t));
		slurmdb_init_clus_res_cond(clus_res_cond, 0);
		created_clus_res_cond = true;
		goto empty;
	}
	if (clus_res_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");
	if (clus_res_cond->name_list
	    && list_count(clus_res_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(clus_res_cond->name_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	if (clus_res_cond->manager_list
	    && list_count(clus_res_cond->manager_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(clus_res_cond->manager_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "manager='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	if (clus_res_cond->server_list
	    && list_count(clus_res_cond->server_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(clus_res_cond->server_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "server='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	if (clus_res_cond->description_list
	    && list_count(clus_res_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(clus_res_cond->description_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:
	if (!clus_res_cond->cluster_list) {
		clus_res_cond->cluster_list =
			list_create(slurm_destroy_char);
		rc = _populate_cluster_name_list(clus_res_cond->cluster_list,
						 mysql_conn, uid);
		if (rc != SLURM_SUCCESS)  {
			fprintf(stderr,
				" Error obtaining cluster names.\n");
			if (created_clus_res_cond)
				slurmdb_destroy_clus_res_cond(clus_res_cond);
			return NULL;
		}
	}
	xfree(tmp);
	xstrfmtcat(tmp, "%s", ser_res_req_inx[i]);
	for(i=1; i<SER_RES_REQ_NUMBER; i++) {
		xstrfmtcat(tmp, ", %s", ser_res_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s", tmp,
			       ser_res_table, extra);
	xfree(tmp);
	xfree(extra);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		if (created_clus_res_cond)
			slurmdb_destroy_clus_res_cond(clus_res_cond);
		return NULL;
	}
	xfree(query);

	ser_res_list = list_create(slurmdb_destroy_ser_res_rec);
	while ((row = mysql_fetch_row(result))) {
		slurmdb_ser_res_rec_t *ser_res =
			xmalloc(sizeof(slurmdb_ser_res_rec_t));
		slurmdb_init_ser_res_rec(ser_res, 0);

		if (row[SER_RES_REQ_DESC] && row[SER_RES_REQ_DESC][0])
			ser_res->description = xstrdup(row[SER_RES_REQ_DESC]);

		ser_res->id = slurm_atoul(row[SER_RES_REQ_ID]);

		if (row[SER_RES_REQ_NAME] && row[SER_RES_REQ_NAME][0])
			ser_res->name = xstrdup(row[SER_RES_REQ_NAME]);

		if (row[SER_RES_REQ_MANAGER] && row[SER_RES_REQ_MANAGER][0])
			ser_res->manager = xstrdup(row[SER_RES_REQ_MANAGER]);

		if (row[SER_RES_REQ_SERVER] && row[SER_RES_REQ_SERVER][0])
			ser_res->server = xstrdup(row[SER_RES_REQ_SERVER]);

		if (row[SER_RES_REQ_COUNT])
			ser_res->count =
				slurm_atoul(row[SER_RES_REQ_COUNT]);
		if (row[SER_RES_REQ_TYPE])
			ser_res->type =
				slurm_atoul(row[SER_RES_REQ_TYPE]);

		list_append(ser_res_list, ser_res);
	}
	mysql_free_result(result);

	xfree(tmp);
	xfree(extra);
	if (ser_res_list
	    && list_count(ser_res_list)) {
		if (clus_res_cond->with_deleted)
			xstrcat(extra, " where (deleted=0 || deleted=1)");
		else
			xstrcat(extra, " where deleted=0");
		i=0;
		xstrfmtcat(tmp, "%s", clus_res_req_inx[i]);
		for(i=1; i<CLUS_RES_REQ_NUMBER; i++) {
			xstrfmtcat(tmp, ", %s", clus_res_req_inx[i]);
		}
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(ser_res_list );
		while ((ser_res = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id='%u'", ser_res->id);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
		clus_res_list = list_create(slurmdb_destroy_clus_res_rec);
		itr1 = list_iterator_create(clus_res_cond->cluster_list);
		while ((cluster_name = list_next(itr1))) {
			query = xstrdup_printf("select %s from %s_%s%s",
					       tmp, cluster_name, clus_res_table, extra);

			debug3("%d(%s:%d) query\n%s",
			       mysql_conn->conn, THIS_FILE, __LINE__, query);
			if (!(result = mysql_db_query_ret(
				      mysql_conn, query, 0))) {
				xfree(query);
				if (created_clus_res_cond)
					slurmdb_destroy_clus_res_cond(
						clus_res_cond);
				list_destroy(clus_res_list);
				return NULL;
			}
			xfree(query);

			while ((row = mysql_fetch_row(result))) {
				slurmdb_clus_res_rec_t *clus_res =
					xmalloc(sizeof(slurmdb_clus_res_rec_t));
				slurmdb_init_clus_res_rec(clus_res, 0);
				if (row[CLUS_RES_REQ_ALLOWED] &&
				    row[CLUS_RES_REQ_ALLOWED][0])
					clus_res->percent_allowed =
						slurm_atoul(row[
								    CLUS_RES_REQ_ALLOWED]);
				record_id = slurm_atoul(row[CLUS_RES_REQ_ID]);
				itr = list_iterator_create(ser_res_list );
				while ((ser_res = list_next(itr))) {
					if (record_id != ser_res->id)
						continue;
					clus_res->res_ptr =
						xmalloc(sizeof(
								slurmdb_ser_res_rec_t));
					slurmdb_init_ser_res_rec(
						clus_res->res_ptr, 0);
					clus_res->res_ptr->description =
						xstrdup(ser_res->description);
					clus_res->res_ptr->id = ser_res->id;
					clus_res->res_ptr->name =
						xstrdup(ser_res->name);
					clus_res->res_ptr->count =
						ser_res->count;
					clus_res->res_ptr->type =
						ser_res->type;
					clus_res->res_ptr->manager =
						xstrdup(ser_res->manager);
					clus_res->res_ptr->server =
						xstrdup(ser_res->server);
					clus_res->cluster =
						xstrdup(cluster_name);
					list_append(clus_res_list, clus_res);
				}
				list_iterator_destroy(itr);
			}
			mysql_free_result(result);
		}
		xfree(tmp);
		xfree(extra);
		list_iterator_destroy(itr1);
	}
	if (created_clus_res_cond)
		slurmdb_destroy_clus_res_cond(clus_res_cond);
	list_destroy(ser_res_list);
	return clus_res_list;
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
	int query_clusters;
	bool send_update = 0;
	bool res_added = 0;
	int last_res = -1;

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
	}

	if (!vals && !clus_vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	/* force to only do non-deleted resources */
	res_cond->with_deleted = 0;
	_setup_res_cond(res_cond, &extra);
	query_clusters = _setup_clus_res_cond(res_cond, &clus_extra);
	if (query_clusters || send_update)
		query = xstrdup_printf("select id, name, server, cluster "
				       "from %s as t1 left outer join "
				       "%s as t2 on (res_id = id) %s && %s;",
				       res_table, clus_res_table,
				       extra, clus_extra);
	else
		query = xstrdup_printf("select id, name, server "
				       "from %s as t1 %s;",
				       res_table, extra);
	xfree(clus_extra);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(extra);
		xfree(vals);
		xfree(clus_extra);
		xfree(clus_vals);
		xfree(query);
		return NULL;
	}

	if ((query_clusters || send_update) && !mysql_num_rows(result)) {
		xfree(query);
		mysql_free_result(result);
		/* since no clusters are there no reason to send
		   updates */
		query_clusters = 0;
		send_update = 0;
		query = xstrdup_printf("select id, name, server "
				       "from %s as t1 %s;",
				       res_table, extra);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(extra);
			xfree(vals);
			xfree(clus_extra);
			xfree(clus_vals);
			xfree(query);
			return NULL;
		}
	}

	if (!query_clusters && !vals) {
		xfree(clus_vals);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	xfree(extra);
	xfree(clus_extra);

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
		if (row[3] && row[3][0]) {
			slurmdb_res_rec_t *res_rec =
				xmalloc(sizeof(slurmdb_res_rec_t));
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

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
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

	if (rc == SLURM_ERROR) {
		error("Couldn't modify Server Resource");
		FREE_NULL_LIST(ret_list);
		errno = SLURM_ERROR;
	}

	return ret_list;
}
