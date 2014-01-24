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

static int _setup_ser_res_limits(slurmdb_ser_res_rec_t *ser_res,
			     char **cols, char **vals,
			     char **extra, bool for_add)
{
	if (!ser_res)
		return SLURM_ERROR;

	if (for_add) {
		/* If we are adding we should make sure we don't get
		   old residue sitting around from a former life.
		*/
		if (!ser_res->description)
			ser_res->description = xstrdup("");
		if (!ser_res->name)
			ser_res->name = xstrdup("");
		if (!ser_res->manager)
			ser_res->manager = xstrdup("");
		if (!ser_res->server)
			ser_res->server = xstrdup("");
		if (ser_res->count == NO_VAL)
			ser_res->count = 0;
		if (!ser_res->type)
			ser_res->type = SLURMDB_RESOURCE_LICENSE;
	}

	if (ser_res->description) {
		xstrcat(*cols, ", description");
		xstrfmtcat(*vals, ", '%s'", ser_res->description);
		xstrfmtcat(*extra, ", description='%s'",
			   ser_res->description);
	}

	if (ser_res->manager) {
		xstrcat(*cols, ", manager");
		xstrfmtcat(*vals, ", '%s'", ser_res->manager);
		xstrfmtcat(*extra, ", manager='%s'",
			   ser_res->manager);
	}

	if (ser_res->server) {
		xstrcat(*cols, ", server");
		xstrfmtcat(*vals, ", '%s'", ser_res->server);
		xstrfmtcat(*extra, ", server='%s'",
			   ser_res->server);
	}
	xstrcat(*cols, ", count");
	xstrfmtcat(*vals, ", %u", ser_res->count);
	xstrfmtcat(*extra, ", count=%u", ser_res->count);

	xstrcat(*cols, ", type");
	xstrfmtcat(*vals, ", %u", ser_res->type);
	xstrfmtcat(*extra, ", type=%u", ser_res->type);

	return SLURM_SUCCESS;
}

static int _populate_cluster_name_list(List new_name_list,
				       mysql_conn_t *mysql_conn, uid_t uid)
{
	List cluster_list = NULL;
	ListIterator itr = NULL;
	char *cluster_name;
	slurmdb_cluster_rec_t *cluster = NULL;

	cluster_list = acct_storage_g_get_clusters(mysql_conn, uid, NULL);
	if (!cluster_list) {
		fprintf(stderr,
			" Error obtaining cluster records.\n");
		return SLURM_ERROR;
	}
	itr = list_iterator_create(cluster_list);
	while ((cluster = list_next(itr))) {
		cluster_name = xstrdup(cluster->name);
		list_append(new_name_list, cluster_name);
	}
		list_iterator_destroy(itr);
		list_destroy(cluster_list);
	return SLURM_SUCCESS;
}

extern int as_mysql_add_ser_res(mysql_conn_t *mysql_conn, uint32_t uid,
			    List ser_res_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_ser_res_rec_t *object = NULL;
	char *cols = NULL, *extra = NULL, *vals = NULL, *query = NULL,
		*tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(ser_res_list);
	while ((object = list_next(itr))) {
		if (!object->name || !object->name[0]) {
			error("We need a server resource name to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name");
		xstrfmtcat(vals, "%ld, %ld, '%s'",
			   now, now, object->name);
		xstrfmtcat(extra, ", mod_time=%ld", now);

		_setup_ser_res_limits(object, &cols, &vals,
				  &extra, 1);

		xstrfmtcat(query,
			   "insert into %s (%s) values (%s) "
			   "on duplicate key update deleted=0, "
			   "id=LAST_INSERT_ID(id)%s;",
			   ser_res_table, cols, vals, extra);


		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
		object->id = mysql_db_insert_ret_id(mysql_conn, query);
		xfree(query);
		if (!object->id) {
			error("Couldn't add server resource %s", object->name);
			added=0;
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

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = slurm_add_slash_to_quotes(extra+2);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%ld, %u, '%s', '%s', '%s');",
			   txn_table,
			   now, DBD_ADD_SER_RES, object->name, user_name,
			   tmp_extra);

		xfree(tmp_extra);
		xfree(cols);
		xfree(extra);
		xfree(vals);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_ADD_SER_RES,
					      object) == SLURM_SUCCESS) {
				list_remove(itr);
				added++;
			}
		}

	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if (!added) {
		reset_mysql_conn(mysql_conn);
	}

	return rc;
}

extern List as_mysql_get_ser_res(mysql_conn_t *mysql_conn, uid_t uid,
			     slurmdb_ser_res_cond_t *ser_res_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List ser_res_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
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

	if (!ser_res_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if (ser_res_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");


	if (ser_res_cond->description_list
	    && list_count(ser_res_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(ser_res_cond->description_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (ser_res_cond->id_list
	    && list_count(ser_res_cond->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(ser_res_cond->id_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (ser_res_cond->name_list
	    && list_count(ser_res_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(ser_res_cond->name_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (ser_res_cond->manager_list
	    && list_count(ser_res_cond->manager_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(ser_res_cond->manager_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "manager='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	if (ser_res_cond->server_list
	    && list_count(ser_res_cond->server_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(ser_res_cond->server_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "server='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

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

	return ser_res_list;
}

extern List as_mysql_remove_ser_res(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_ser_res_cond_t *ser_res_cond)
{
	List ser_res_list = NULL;
	List ret_list = NULL;
	ListIterator itr = NULL;
	slurmdb_ser_res_rec_t *object = NULL;
	char *cond_char = NULL;
	char *user_name = NULL;
	int rc = SLURM_SUCCESS;
	int added = 0;
	time_t now = time(NULL);
	char *name = NULL;

	if (!ser_res_cond || (!ser_res_cond->name_list)) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;
	/* force to only do non-deleted server resources */
	ser_res_cond->with_deleted = 0;
	ser_res_list = as_mysql_get_ser_res(mysql_conn, uid,
					      ser_res_cond);
	if (ser_res_list
	    && list_count(ser_res_list)) {
		ret_list = list_create(slurm_destroy_char);
		itr = list_iterator_create(ser_res_list);
		while ((object = list_next(itr))) {
			name = xstrdup(object->name);
			list_append(ret_list, name);
			if (object->id) {
				xstrfmtcat(cond_char, " (id=%u)",
					   object->id);
			}
			user_name = uid_to_string((uid_t) uid);
			rc = remove_common(mysql_conn, DBD_REMOVE_SER_RES,
					   now, user_name, ser_res_table,
					   cond_char, NULL,
					   mysql_conn->cluster_name,
					   NULL, NULL);
			if (rc == SLURM_ERROR) {
				error("Couldn't remove server resource");
				list_destroy(ser_res_list);
				ser_res_list = NULL;
				list_destroy(ret_list);
				ret_list = NULL;
				goto end_it;
			} else {
				if (addto_update_list(mysql_conn->update_list,
				    SLURMDB_REMOVE_SER_RES,
				    object) == SLURM_SUCCESS) {
					list_remove(itr);
					added++;

				}
			}
		}
		list_iterator_destroy(itr);
		if (!added) {
			reset_mysql_conn(mysql_conn);
			if (ret_list) {
				list_destroy(ret_list);
				ret_list = NULL;
			}
		}
		list_destroy(ser_res_list);
		ser_res_list = NULL;

	} else {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

end_it:
	xfree(cond_char);
	xfree(user_name);
	return ret_list;
}

extern List as_mysql_modify_ser_res(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_ser_res_cond_t *ser_res_cond,
				     slurmdb_ser_res_rec_t *ser_res)
{
	List ser_res_list = NULL;
	List ret_list = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_ser_res_rec_t *object = NULL;
	char *vals = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *cond_char = NULL;
	char *name = NULL;
	int added = 0;

	if (!ser_res_cond || !ser_res) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS) {
		return NULL;
	}

	/* force to only do non-deleted server resources */
	ser_res_cond->with_deleted = 0;
	ser_res_list = as_mysql_get_ser_res(mysql_conn, uid,
					      ser_res_cond);
	if (ser_res_list
	    && list_count(ser_res_list)) {
		ret_list = list_create(slurm_destroy_char);
		itr = list_iterator_create(ser_res_list);
		while ((object = list_next(itr))) {
			slurmdb_ser_res_rec_t *ser_res_rec =
			    xmalloc(sizeof(slurmdb_ser_res_rec_t));
			slurmdb_init_ser_res_rec(ser_res_rec, 0);
			name = xstrdup(object->name);
			list_append(ret_list, name);
			if (object->id) {
				xstrfmtcat(cond_char, " (id=%u)",
					   object->id);
			}
			if (ser_res->name) {
				xstrfmtcat(vals, ", name='%s'", ser_res->name);
				ser_res_rec->name = xstrdup(ser_res->name);
			} else {
				ser_res_rec->name = xstrdup(object->name);
			}
			if (ser_res->description) {
				xstrfmtcat(vals, ", description='%s'",
					   ser_res->description);
				ser_res_rec->description =
				   xstrdup(ser_res->description);
			} else {
				ser_res_rec->description =
				   xstrdup(object->description);
			}
			if (ser_res->manager) {
				xstrfmtcat(vals, ", manager='%s'",
					   ser_res->manager);
				ser_res_rec->manager =
				   xstrdup(ser_res->manager);
			} else {
				ser_res_rec->manager = xstrdup(object->manager);
			}
			if (ser_res->server) {
			    xstrfmtcat(vals, ", server='%s'",
				ser_res->server);
				ser_res_rec->server = xstrdup(ser_res->server);
			} else {
				ser_res_rec->server = xstrdup(object->server);
			}
			if (ser_res->count != NO_VAL) {
				xstrfmtcat(vals, ", count=%u", ser_res->count);
				ser_res_rec->count = ser_res->count;
			} else {
				ser_res_rec->count = object->count;
			}
			if (ser_res->type != NO_VAL) {
				xstrfmtcat(vals, ", type=%u", ser_res->type);
				ser_res_rec->type = ser_res->type;
			} else {
				ser_res_rec->type = object->type;
			}
			user_name = uid_to_string((uid_t) uid);
			rc = modify_common(mysql_conn, DBD_MODIFY_SER_RES,
					   now, user_name, ser_res_table,
					   cond_char, vals,
					   mysql_conn->cluster_name);
			if (rc == SLURM_ERROR) {
				error("Couldn't modify server resource");
				list_destroy(ser_res_list);
				ser_res_list = NULL;
				list_destroy(ret_list);
				ret_list = NULL;
				goto end_it;
			} else {
				if (addto_update_list(mysql_conn->update_list,
				    SLURMDB_MODIFY_SER_RES,
				    ser_res_rec) == SLURM_SUCCESS){
					added++;
				}
			}
		}
		list_iterator_destroy(itr);
		if (!added) {
			reset_mysql_conn(mysql_conn);
			if (ret_list) {
				list_destroy(ret_list);
				ret_list = NULL;
			}
		}
		list_destroy(ser_res_list);
		ser_res_list = NULL;

	} else {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

end_it:
	xfree(vals);
	xfree(cond_char);
	xfree(user_name);
	return ret_list;
}

extern int as_mysql_add_clus_res(mysql_conn_t *mysql_conn, uint32_t uid,
				 List clus_res_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_clus_res_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL, *tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;
	List ser_res_list = NULL;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;
	ser_res_list = list_create(slurmdb_destroy_ser_res_rec);
	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(clus_res_list);
	while ((object = list_next(itr))) {
		if(!object->cluster){
			object->cluster = xstrdup(mysql_conn->cluster_name);
		}
		xstrcat(cols, "creation_time, mod_time, id, percent_allowed");
		xstrfmtcat(vals, "%ld, %ld, %u, %u", now, now,
			  object->res_ptr->id,
			   object->percent_allowed);
		xstrfmtcat(extra, ", mod_time=%ld", now);
		xstrfmtcat(query,
			   "insert into %s_%s (%s) values (%s) "
			   "on duplicate key update deleted=0, "
			   "percent_allowed=%u, mod_time=%ld;",
			   object->cluster, clus_res_table, cols, vals,
			   object->percent_allowed, now);

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		info("rc from add cluster resource query is %u", rc);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add cluster resource %s",
			      object->res_ptr->name);
			xfree(extra);
			xfree(cols);
			xfree(vals);
			added=0;
			break;
		}

		affect_rows = last_affected_rows(mysql_conn);

		if (!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(extra);
			xfree(cols);
			xfree(vals);
			continue;
		}

		xfree(cols);
		xfree(vals);

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = slurm_add_slash_to_quotes(extra+2);
		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%ld, %u, '%s', '%s', '%s');",
			   txn_table, now, DBD_ADD_CLUS_RES,
			   object->res_ptr->name, user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);
		debug4("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_ADD_CLUS_RES,
					      object) ==  SLURM_SUCCESS) {
				list_remove(itr);
				added++;
			}
		}
	}

	list_iterator_destroy(itr);
	xfree(user_name);
	list_destroy(ser_res_list);
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

extern List as_mysql_modify_clus_res(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_clus_res_cond_t *clus_res_cond,
				     slurmdb_clus_res_rec_t *clus_res)
{
	List clus_res_list = NULL;
	List ret_list = NULL;
	ListIterator itr = NULL;
	ListIterator itr1 = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_clus_res_rec_t *object = NULL;
	char *vals = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *cluster_name = NULL;
	char *cond_char = NULL;
	char *name = NULL;
	int added = 0;

	if (!clus_res_cond || !clus_res) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS) {
		return NULL;
	}

	/* force to only do non-deleted cluster resources */
	clus_res_cond->with_deleted = 0;
	clus_res_list = as_mysql_get_clus_res(mysql_conn, uid,
					      clus_res_cond);
	if (clus_res_list
	    && list_count(clus_res_list)) {
		ret_list = list_create(slurm_destroy_char);
		itr = list_iterator_create(clus_res_list);
		while ((object = list_next(itr))) {
			name= xstrdup(object->res_ptr->name);
			list_append(ret_list, name);
			if (object->res_ptr->id) {
				xstrfmtcat(cond_char, " (id=%u)",
					   object->res_ptr->id);
			}
			if (clus_res->percent_allowed) {
				xstrfmtcat(vals, ", percent_allowed=%u",
					   clus_res->percent_allowed);
				object->percent_allowed =
				   clus_res->percent_allowed;
			}
			user_name = uid_to_string((uid_t) uid);
			itr1 = list_iterator_create(
			    clus_res_cond->cluster_list);
			while ((cluster_name = list_next(itr1))) {
				object->cluster = xstrdup(cluster_name);
				rc = modify_common(mysql_conn,
					   DBD_MODIFY_CLUS_RES,
					   now, user_name, clus_res_table,
					   cond_char, vals, cluster_name);
				if (rc == SLURM_ERROR) {
					error("Couldn't modify cluster "
					      "resource");
					list_destroy(clus_res_list);
					clus_res_list = NULL;
					list_destroy(ret_list);
					ret_list = NULL;
					goto end_it;
				} else {
					if (addto_update_list(
					    mysql_conn->update_list,
					    SLURMDB_MODIFY_CLUS_RES,
					    object) == SLURM_SUCCESS){
						list_remove(itr);
						added++;
					}
				}
			}
			list_iterator_destroy(itr1);
		}
		list_iterator_destroy(itr);
		if (!added) {
			reset_mysql_conn(mysql_conn);
			if (ret_list) {
				list_destroy(ret_list);
				ret_list = NULL;
			}
		}
		list_destroy(clus_res_list);
		clus_res_list = NULL;

	} else {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

end_it:
	xfree(vals);
	xfree(cond_char);
	xfree(user_name);
	return ret_list;
}

extern List as_mysql_remove_clus_res(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_clus_res_cond_t *clus_res_cond)
{
	List clus_res_list = NULL;
	List ret_list = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_clus_res_rec_t *object = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *cond_char = NULL;
	char *name = NULL;
	int added = 0;

	if (!clus_res_cond || (!clus_res_cond->name_list)) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	/* force to only do non-deleted cluster resources */
	clus_res_cond->with_deleted = 0;
	clus_res_list = as_mysql_get_clus_res(mysql_conn, uid,
					      clus_res_cond);
	if (clus_res_list
	    && list_count(clus_res_list)) {
		ret_list = list_create(slurm_destroy_char);
		itr = list_iterator_create(clus_res_list);
		while ((object = list_next(itr))) {
			name = xstrdup(object->res_ptr->name);
			list_append(ret_list, name);
			if (object->res_ptr->id) {
				xstrfmtcat(cond_char, " (id=%u)",
					   object->res_ptr->id);
			}
			user_name = uid_to_string((uid_t) uid);
			rc = remove_common(mysql_conn,
			     DBD_REMOVE_CLUS_RES,
			     now, user_name, clus_res_table,
			     cond_char, NULL, object->cluster,
			     NULL, NULL);
			if (rc == SLURM_ERROR) {
				error("Couldn't remove cluster "
				      "resource");
				list_destroy(clus_res_list);
				clus_res_list = NULL;
				list_destroy(ret_list);
				ret_list = NULL;
				goto end_it;
			} else {
				if (addto_update_list(
				    mysql_conn->update_list,
				    SLURMDB_REMOVE_CLUS_RES,
				    object) == SLURM_SUCCESS) {
					list_remove(itr);
					added++;
				}
			}
			xfree(cond_char);
		}
		list_iterator_destroy(itr);
		if (!added) {
			reset_mysql_conn(mysql_conn);
			if (ret_list) {
				list_destroy(ret_list);
				ret_list = NULL;
			}
		}
		list_destroy(clus_res_list);
		clus_res_list = NULL;

	} else {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

end_it:
	xfree(cond_char);
	xfree(user_name);
	return ret_list;
}
