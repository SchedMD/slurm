/*****************************************************************************\
 *  as_mysql_cluster.c - functions dealing with clusters.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
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

#include "as_mysql_tres.h"
#include "as_mysql_assoc.h"
#include "as_mysql_cluster.h"
#include "as_mysql_federation.h"
#include "as_mysql_usage.h"
#include "as_mysql_wckey.h"

#include "src/common/select.h"
#include "src/common/slurm_time.h"

extern int as_mysql_get_fed_cluster_id(mysql_conn_t *mysql_conn,
				       const char *cluster,
				       const char *federation,
				       int last_id, int *ret_id)
{
	/* find id for cluster in federation.
	 * don't do anything if cluster is already part of federation
	 * get list of clusters that are part of the federration.
	 * loop through each cluster and find the first id available.
	 * report error if all are full in 63 slots. */

	int        id     = 1;
	char      *query  = NULL;
	MYSQL_ROW  row;
	MYSQL_RES *result = NULL;

	xassert(cluster);
	xassert(federation);
	xassert(ret_id);

	/* See if cluster is already part of federation */
	xstrfmtcat(query, "SELECT name, fed_id "
			  "FROM %s "
			  "WHERE deleted=0 AND name='%s' AND federation='%s';",
		   cluster_table, cluster, federation);
	DB_DEBUG(FEDR, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		error("no result given for %s", query);
		return SLURM_ERROR;
	}
	xfree(query);
	while ((row = mysql_fetch_row(result))) {
		int tmp_id = slurm_atoul(row[1]);
		log_flag(FEDR, "cluster '%s' already part of federation '%s', using existing id %d",
			 cluster, federation, tmp_id);
		mysql_free_result(result);
		*ret_id = tmp_id;
		return SLURM_SUCCESS;
	}
	mysql_free_result(result);

	/* Get all other clusters in the federation and find an open id. */
	xstrfmtcat(query, "SELECT name, federation, fed_id "
		   	  "FROM %s "
		   	  "WHERE name!='%s' AND federation='%s' "
			  "AND fed_id > %d AND deleted=0 ORDER BY fed_id;",
			  cluster_table, cluster, federation, last_id);
	DB_DEBUG(FEDR, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		error("no result given for %s", query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (last_id >= id)
		id = last_id + 1;
	while ((row = mysql_fetch_row(result))) {
		if (id != slurm_atoul(row[2]))
			break;
		id++;
	}
	mysql_free_result(result);

	if (id > MAX_FED_CLUSTERS) {
		error("Too many clusters in this federation.");
		errno = ESLURM_FED_CLUSTER_MAX_CNT;
		return  ESLURM_FED_CLUSTER_MAX_CNT;
	}

	*ret_id = id;
	return SLURM_SUCCESS;
}

static int _setup_cluster_cond_limits(slurmdb_cluster_cond_t *cluster_cond,
				      char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;

	if (!cluster_cond)
		return 0;

	if (cluster_cond->with_deleted)
		xstrcat(*extra, " where (deleted=0 || deleted=1)");
	else
		xstrcat(*extra, " where deleted=0");

	if (cluster_cond->cluster_list
	    && list_count(cluster_cond->cluster_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(cluster_cond->cluster_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (cluster_cond->federation_list
	    && list_count(cluster_cond->federation_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(cluster_cond->federation_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "federation='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (cluster_cond->plugin_id_select_list
	    && list_count(cluster_cond->plugin_id_select_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(cluster_cond->plugin_id_select_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "plugin_id_select='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (cluster_cond->rpc_version_list
	    && list_count(cluster_cond->rpc_version_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(cluster_cond->rpc_version_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "rpc_version='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (cluster_cond->classification) {
		xstrfmtcat(*extra, " && (classification & %u)",
			   cluster_cond->classification);
	}

	if (cluster_cond->flags != NO_VAL) {
		xstrfmtcat(*extra, " && (flags & %u)",
			   cluster_cond->flags);
	}

	return set;
}

extern int as_mysql_add_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
				 List cluster_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_cluster_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL, *tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;
	bool has_feds = false;
	List assoc_list = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	bool external_cluster = false;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!is_user_min_admin_level(mysql_conn, uid, SLURMDB_ADMIN_SUPER_USER))
		return ESLURM_ACCESS_DENIED;

	assoc_list = list_create(slurmdb_destroy_assoc_rec);

	user_name = uid_to_string((uid_t) uid);
	/* Since adding tables make it so you can't roll back, if
	   there is an error there is no way to easily remove entries
	   in the database, so we will create the tables first and
	   then after that works out then add them to the mix.
	*/
	itr = list_iterator_create(cluster_list);
	while ((object = list_next(itr))) {
		if (!object->name || !object->name[0]) {
			error("We need a cluster name to add.");
			rc = SLURM_ERROR;
			list_remove(itr);
			continue;
		}
		if ((object->flags != NO_VAL) &&
		    (object->flags & CLUSTER_FLAG_EXT))
			external_cluster = true;
		if ((rc = create_cluster_tables(mysql_conn,
						object->name))
		    != SLURM_SUCCESS) {
			added = 0;
			if (mysql_errno(mysql_conn->db_conn)
			    == ER_WRONG_TABLE_NAME)
				rc = ESLURM_BAD_NAME;
			goto end_it;
		}
	}

	/* Now that all the tables were created successfully lets go
	   ahead and add it to the system.
	*/
	list_iterator_reset(itr);
	while ((object = list_next(itr))) {
		int fed_id = 0;
		uint16_t fed_state = CLUSTER_FED_STATE_NA;
		char *features = NULL;
		xstrcat(cols, "creation_time, mod_time, acct");
		xstrfmtcat(vals, "%ld, %ld, 'root'", now, now);
		xstrfmtcat(extra, ", mod_time=%ld", now);
		if (object->root_assoc) {
			rc = setup_assoc_limits(object->root_assoc, &cols,
						&vals, &extra,
						QOS_LEVEL_SET, 1);
			if (rc) {
				xfree(extra);
				xfree(cols);
				xfree(vals);
				added=0;
				error("%s: Failed, setup_assoc_limits functions returned error",
				      __func__);
				goto end_it;

			}
		}

		if (object->fed.name) {
			has_feds = 1;
			rc = as_mysql_get_fed_cluster_id(mysql_conn,
							 object->name,
							 object->fed.name, -1,
							 &fed_id);
			if (rc) {
				error("failed to get cluster id for "
				      "federation");
				xfree(extra);
				xfree(cols);
				xfree(vals);
				added=0;
				goto end_it;
			}

			if (object->fed.state != NO_VAL)
				fed_state = object->fed.state;
			else
				fed_state = CLUSTER_FED_STATE_ACTIVE;
		}

		if (object->fed.feature_list) {
			features =
				slurm_char_list_to_xstr(
						object->fed.feature_list);
			has_feds = 1;
		}

		xstrfmtcat(query,
			   "insert into %s (creation_time, mod_time, "
			   "name, classification, federation, fed_id, "
			   "fed_state, features) "
			   "values (%ld, %ld, '%s', %u, '%s', %d, %u, '%s') "
			   "on duplicate key update deleted=0, mod_time=%ld, "
			   "control_host='', control_port=0, "
			   "classification=%u, flags=0, federation='%s', "
			   "fed_id=%d, fed_state=%u, features='%s'",
			   cluster_table,
			   now, now, object->name, object->classification,
			   (object->fed.name) ? object->fed.name : "",
			   fed_id, fed_state, (features) ? features : "",
			   now, object->classification,
			   (object->fed.name) ? object->fed.name : "",
			   fed_id, fed_state, (features) ? features : "");
		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add cluster %s", object->name);
			xfree(extra);
			xfree(cols);
			xfree(vals);
			xfree(features);
			added=0;
			break;
		}

		affect_rows = last_affected_rows(mysql_conn);

		if (!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(extra);
			xfree(cols);
			xfree(vals);
			xfree(features);
			continue;
		}

		if (!external_cluster) {
			/* Add root account */
			xstrfmtcat(query,
				   "insert into \"%s_%s\" (%s, lft, rgt) "
				   "values (%s, 1, 2) "
				   "on duplicate key update deleted=0, "
				   "id_assoc=LAST_INSERT_ID(id_assoc)%s;",
				   object->name, assoc_table, cols,
				   vals,
				   extra);
			xfree(cols);
			xfree(vals);
			DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);

			rc = mysql_db_query(mysql_conn, query);
			xfree(query);

			if (rc != SLURM_SUCCESS) {
				error("Couldn't add cluster root assoc");
				xfree(extra);
				xfree(features);
				added=0;
				break;
			}
		} else {
			xfree(cols);
			xfree(vals);
		}

		/* Build up extra with cluster specfic values for txn table */
		xstrfmtcat(extra, ", federation='%s', fed_id=%d, fed_state=%u, "
				  "features='%s'",
			   (object->fed.name) ? object->fed.name : "",
			   fed_id, fed_state, (features) ? features : "");
		xfree(features);

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = slurm_add_slash_to_quotes(extra+2);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%ld, %u, '%s', '%s', '%s');",
			   txn_table, now, DBD_ADD_CLUSTERS,
			   object->name, user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);
		debug4("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			ListIterator check_itr;
			char *tmp_name;

			added++;
			/* add it to the list and sort */
			slurm_rwlock_wrlock(&as_mysql_cluster_list_lock);
			check_itr = list_iterator_create(as_mysql_cluster_list);
			while ((tmp_name = list_next(check_itr))) {
				if (!xstrcmp(tmp_name, object->name))
					break;
			}
			list_iterator_destroy(check_itr);
			if (!tmp_name) {
				list_append(as_mysql_cluster_list,
					    xstrdup(object->name));
				list_sort(as_mysql_cluster_list,
					  (ListCmpF)slurm_sort_char_list_asc);
			} else
				error("Cluster %s(%s) appears to already be in "
				      "our cache list, not adding.", tmp_name,
				      object->name);
			slurm_rwlock_unlock(&as_mysql_cluster_list_lock);
		}

		if (!external_cluster) {
			/* Add user root by default to run from the root
			 * association.  This gets popped off so we need to
			 * read it every time here.
			 */
			assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
			slurmdb_init_assoc_rec(assoc, 0);
			list_append(assoc_list, assoc);

			assoc->cluster = xstrdup(object->name);
			assoc->user = xstrdup("root");
			assoc->acct = xstrdup("root");
			assoc->is_def = 1;

			if (as_mysql_add_assocs(mysql_conn, uid, assoc_list)
			    == SLURM_ERROR) {
				error("Problem adding root user association");
				rc = SLURM_ERROR;
			}
		}
	}
end_it:
	list_iterator_destroy(itr);
	xfree(user_name);

	FREE_NULL_LIST(assoc_list);

	if (!added)
		reset_mysql_conn(mysql_conn);
	else if (has_feds)
		as_mysql_add_feds_to_update_list(mysql_conn);

	return rc;
}

static int _reconcile_existing_features(void *object, void *arg)
{
	char *new_feature = (char *)object;
	List existing_features = (List)arg;

	if (new_feature[0] == '-')
		list_delete_all(existing_features, slurm_find_char_in_list,
				new_feature + 1);
	else if (new_feature[0] == '+')
		list_append(existing_features, xstrdup(new_feature + 1));
	else
		list_append(existing_features, xstrdup(new_feature));

	return SLURM_SUCCESS;
}

extern List as_mysql_modify_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_cluster_cond_t *cluster_cond,
				     slurmdb_cluster_rec_t *cluster)
{
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	bool clust_reg = false, fed_update = false;

	/* If you need to alter the default values of the cluster use
	 * modify_assocs since this is used only for registering
	 * the controller when it loads
	 */

	if (!cluster_cond || !cluster) {
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

	/* force to only do non-deleted clusters */
	cluster_cond->with_deleted = 0;
	_setup_cluster_cond_limits(cluster_cond, &extra);

	/* Needed if talking to older Slurm versions < 2.2 */
	if (!mysql_conn->cluster_name && cluster_cond->cluster_list
	    && list_count(cluster_cond->cluster_list))
		mysql_conn->cluster_name =
			xstrdup(list_peek(cluster_cond->cluster_list));

	set = 0;
	if (cluster->control_host) {
		xstrfmtcat(vals, ", control_host='%s'", cluster->control_host);
		set++;
		clust_reg = true;
	}

	if (cluster->control_port) {
		xstrfmtcat(vals, ", control_port=%u, last_port=%u",
			   cluster->control_port, cluster->control_port);
		set++;
		clust_reg = true;
	}

	if (cluster->rpc_version) {
		xstrfmtcat(vals, ", rpc_version=%u", cluster->rpc_version);
		set++;
		clust_reg = true;
	}

	if (cluster->dimensions) {
		xstrfmtcat(vals, ", dimensions=%u", cluster->dimensions);
		clust_reg = true;
	}

	if (cluster->plugin_id_select) {
		xstrfmtcat(vals, ", plugin_id_select=%u",
			   cluster->plugin_id_select);
		clust_reg = true;
	}
	if (cluster->flags != NO_VAL) {
		xstrfmtcat(vals, ", flags=%u", cluster->flags);
		clust_reg = true;
	}

	if (cluster->classification) {
		xstrfmtcat(vals, ", classification=%u",
			   cluster->classification);
	}

	if (cluster->fed.name) {
		xstrfmtcat(vals, ", federation='%s'", cluster->fed.name);
		fed_update = true;
	}

	if (cluster->fed.state != NO_VAL) {
		xstrfmtcat(vals, ", fed_state=%u", cluster->fed.state);
		fed_update = true;
	}

	if (!vals && !cluster->fed.feature_list) {
		xfree(extra);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	} else if (clust_reg && (set != 3)) {
		xfree(vals);
		xfree(extra);
		errno = EFAULT;
		error("Need control host, port and rpc version "
		      "to register a cluster");
		return NULL;
	}

	xstrfmtcat(query, "select name, control_port, federation, features from %s%s;",
		   cluster_table, extra);

	DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		xfree(vals);
		error("no result given for %s", extra);
		xfree(extra);
		return NULL;
	}
	xfree(extra);

	ret_list = list_create(xfree_ptr);
	user_name = uid_to_string((uid_t) uid);
	while ((row = mysql_fetch_row(result))) {
		char *tmp_vals = xstrdup(vals);

		object = xstrdup(row[0]);

		if (cluster->fed.name) {
			int id = 0;
			char *curr_fed = NULL;
			uint32_t set_state = NO_VAL;

			if (cluster->fed.name[0] != '\0') {
				rc = as_mysql_get_fed_cluster_id(
							mysql_conn, object,
							cluster->fed.name, -1,
							&id);
				if (rc) {
					error("failed to get cluster id for "
					      "federation");
					xfree(tmp_vals);
					xfree(object);
					FREE_NULL_LIST(ret_list);
					mysql_free_result(result);
					goto end_it;
				}
			}
			/* will set fed_id=0 if being removed from fed. */
			xstrfmtcat(tmp_vals, ", fed_id=%d", id);

			curr_fed = xstrdup(row[2]);
			if (cluster->fed.name[0] == '\0')
				/* clear fed_state when leaving federation */
				set_state = CLUSTER_FED_STATE_NA;
			else if (cluster->fed.state != NO_VAL) {
				/* NOOP: fed_state already set in vals */
			} else if (xstrcmp(curr_fed, cluster->fed.name))
				/* set state to active when joining fed * */
				set_state = CLUSTER_FED_STATE_ACTIVE;
			/* else use existing state */

			if (set_state != NO_VAL)
				xstrfmtcat(tmp_vals, ", fed_state=%u",
					   set_state);

			xfree(curr_fed);
		}

		if (cluster->fed.feature_list) {
			if (!list_count(cluster->fed.feature_list)) {
				/* clear all existing features */
				xstrfmtcat(tmp_vals, ", features=''");
			} else {
				char *features = NULL, *feature = NULL;
				List existing_features = list_create(xfree_ptr);

				if ((feature =
				     list_peek(cluster->fed.feature_list)) &&
				    (feature[0] == '+' || feature[0] == '-'))
					slurm_addto_char_list(existing_features,
							      row[3]);

				list_for_each(cluster->fed.feature_list,
					      _reconcile_existing_features,
					      existing_features);

				features =
					slurm_char_list_to_xstr(
							existing_features);
				xstrfmtcat(tmp_vals, ", features='%s'",
					   features ? features : "");

				xfree(features);
				FREE_NULL_LIST(existing_features);
			}

			fed_update = true;
		}

		list_append(ret_list, object);
		xstrfmtcat(name_char, "name='%s'", object);

		rc = modify_common(mysql_conn, DBD_MODIFY_CLUSTERS, now,
				   user_name, cluster_table,
				   name_char, tmp_vals, NULL);
		xfree(name_char);
		xfree(tmp_vals);
		if (rc == SLURM_ERROR) {
			error("Couldn't modify cluster 1");
			FREE_NULL_LIST(ret_list);
			mysql_free_result(result);
			goto end_it;
		}
	}
	mysql_free_result(result);
	xfree(user_name);

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		DB_DEBUG(DB_ASSOC, mysql_conn->conn,
		         "didn't affect anything\n%s", query);
		xfree(name_char);
		xfree(vals);
		xfree(query);
		return ret_list;
	}

	if (fed_update)
		as_mysql_add_feds_to_update_list(mysql_conn);

end_it:
	xfree(query);
	xfree(vals);
	xfree(user_name);

	return ret_list;
}

extern List as_mysql_remove_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
				     slurmdb_cluster_cond_t *cluster_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	List tmp_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL, *cluster_name = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	slurmdb_wckey_cond_t wckey_cond;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	bool jobs_running = 0, fed_update = false;

	if (!cluster_cond) {
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

	/* force to only do non-deleted clusters */
	cluster_cond->with_deleted = 0;
	_setup_cluster_cond_limits(cluster_cond, &extra);

	if (!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select name,federation from %s%s;",
			       cluster_table, extra);
	xfree(extra);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	rc = 0;
	ret_list = list_create(xfree_ptr);

	if (!mysql_num_rows(result)) {
		mysql_free_result(result);
		errno = SLURM_NO_CHANGE_IN_DATA;
		DB_DEBUG(DB_ASSOC, mysql_conn->conn,
		         "didn't affect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	assoc_char = xstrdup_printf("t2.acct='root'");

	user_name = uid_to_string((uid_t) uid);
	while ((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		if (!jobs_running) {
			/* strdup the cluster name because ret_list will be
			 * flushed if there are running jobs. This will cause an
			 * invalid read because _check_jobs_before_remove() will
			 * still try to access "cluster_name" which was
			 * "object". */
			list_append(ret_list, xstrdup(object));
		}

		if (row[1] && (*row[1] != '\0'))
			fed_update = true;

		xfree(name_char);
		xstrfmtcat(name_char, "name='%s'", object);
		/* We should not need to delete any cluster usage just set it
		 * to deleted */
		xstrfmtcat(query,
			   "update \"%s_%s\" set time_end=%ld where time_end=0;"
			   "update \"%s_%s\" set mod_time=%ld, deleted=1;"
			   "update \"%s_%s\" set mod_time=%ld, deleted=1;"
			   "update \"%s_%s\" set mod_time=%ld, deleted=1;",
			   object, event_table, now,
			   object, cluster_day_table, now,
			   object, cluster_hour_table, now,
			   object, cluster_month_table, now);
		rc = remove_common(mysql_conn, DBD_REMOVE_CLUSTERS, now,
				   user_name, cluster_table, name_char,
				   assoc_char, object, ret_list, &jobs_running,
				   NULL);
		xfree(object);
		if (rc != SLURM_SUCCESS)
			break;
	}
	mysql_free_result(result);
	xfree(user_name);
	xfree(name_char);
	xfree(assoc_char);

	if (rc != SLURM_SUCCESS) {
		FREE_NULL_LIST(ret_list);
		xfree(query);
		return NULL;
	}
	if (!jobs_running) {
		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			reset_mysql_conn(mysql_conn);
			FREE_NULL_LIST(ret_list);
			return NULL;
		}

		/* We need to remove these clusters from the wckey table */
		memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
		wckey_cond.cluster_list = ret_list;
		tmp_list = as_mysql_remove_wckeys(mysql_conn, uid, &wckey_cond);
		FREE_NULL_LIST(tmp_list);

		itr = list_iterator_create(ret_list);
		while ((object = list_next(itr))) {
			if ((rc = remove_cluster_tables(mysql_conn, object))
			    != SLURM_SUCCESS)
				break;
			cluster_name = xstrdup(object);
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_REMOVE_CLUSTER,
					      cluster_name) != SLURM_SUCCESS)
				xfree(cluster_name);
		}
		list_iterator_destroy(itr);

		if (rc != SLURM_SUCCESS) {
			reset_mysql_conn(mysql_conn);
			FREE_NULL_LIST(ret_list);
			errno = rc;
			return NULL;
		}

		if (fed_update)
			as_mysql_add_feds_to_update_list(mysql_conn);

		errno = SLURM_SUCCESS;
	} else
		errno = ESLURM_JOBS_RUNNING_ON_ASSOC;

	xfree(query);

	return ret_list;
}

extern List as_mysql_get_clusters(mysql_conn_t *mysql_conn, uid_t uid,
				  slurmdb_cluster_cond_t *cluster_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List cluster_list = NULL;
	ListIterator itr = NULL;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	slurmdb_assoc_cond_t assoc_cond;
	ListIterator assoc_itr = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	List assoc_list = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *cluster_req_inx[] = {
		"name",
		"classification",
		"control_host",
		"control_port",
		"features",
		"federation",
		"fed_id",
		"fed_state",
		"rpc_version",
		"dimensions",
		"flags",
		"plugin_id_select"
	};
	enum {
		CLUSTER_REQ_NAME,
		CLUSTER_REQ_CLASS,
		CLUSTER_REQ_CH,
		CLUSTER_REQ_CP,
		CLUSTER_REQ_FEATURES,
		CLUSTER_REQ_FEDR,
		CLUSTER_REQ_FEDID,
		CLUSTER_REQ_FEDSTATE,
		CLUSTER_REQ_VERSION,
		CLUSTER_REQ_DIMS,
		CLUSTER_REQ_FLAGS,
		CLUSTER_REQ_PI_SELECT,
		CLUSTER_REQ_COUNT
	};

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;


	if (!cluster_cond) {
		xstrcat(extra, " where deleted=0");
		goto empty;
	}

	_setup_cluster_cond_limits(cluster_cond, &extra);

empty:

	xfree(tmp);
	i=0;
	xstrfmtcat(tmp, "%s", cluster_req_inx[i]);
	for(i=1; i<CLUSTER_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", cluster_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s%s",
			       tmp, cluster_table, extra);
	xfree(tmp);
	xfree(extra);

	DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	cluster_list = list_create(slurmdb_destroy_cluster_rec);

	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));

	if (cluster_cond) {
		/* I don't think we want the with_usage flag here.
		 * We do need the with_deleted though. */
		//assoc_cond.with_usage = cluster_cond->with_usage;
		assoc_cond.with_deleted = cluster_cond->with_deleted;
	}
	assoc_cond.cluster_list = list_create(NULL);

	while ((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		MYSQL_ROW row2;
		char *features = NULL;
		cluster = xmalloc(sizeof(slurmdb_cluster_rec_t));
		slurmdb_init_cluster_rec(cluster, 0);
		list_append(cluster_list, cluster);

		cluster->name = xstrdup(row[CLUSTER_REQ_NAME]);

		list_append(assoc_cond.cluster_list, cluster->name);

		cluster->classification = slurm_atoul(row[CLUSTER_REQ_CLASS]);
		cluster->control_host = xstrdup(row[CLUSTER_REQ_CH]);
		cluster->control_port = slurm_atoul(row[CLUSTER_REQ_CP]);
		cluster->fed.name     = xstrdup(row[CLUSTER_REQ_FEDR]);
		features              = row[CLUSTER_REQ_FEATURES];
		if (features && *features) {
			cluster->fed.feature_list = list_create(xfree_ptr);
			slurm_addto_char_list(cluster->fed.feature_list,
					      features);
		}
		cluster->fed.id       = slurm_atoul(row[CLUSTER_REQ_FEDID]);
		cluster->fed.state    = slurm_atoul(row[CLUSTER_REQ_FEDSTATE]);
		cluster->rpc_version = slurm_atoul(row[CLUSTER_REQ_VERSION]);
		cluster->dimensions = slurm_atoul(row[CLUSTER_REQ_DIMS]);
		cluster->flags = slurm_atoul(row[CLUSTER_REQ_FLAGS]);
		cluster->plugin_id_select =
			slurm_atoul(row[CLUSTER_REQ_PI_SELECT]);

		query = xstrdup_printf(
			"select tres, cluster_nodes from "
			"\"%s_%s\" where time_end=0 and node_name='' limit 1",
			cluster->name, event_table);
		DB_DEBUG(DB_TRES, mysql_conn->conn, "query\n%s", query);
		if (!(result2 = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			continue;
		}
		xfree(query);
		if ((row2 = mysql_fetch_row(result2))) {
			cluster->tres_str = xstrdup(row2[0]);
			if (row2[1] && row2[1][0])
				cluster->nodes = xstrdup(row2[1]);
		}
		mysql_free_result(result2);

		/* get the usage if requested */
		if (cluster_cond && cluster_cond->with_usage) {
			as_mysql_get_usage(
				mysql_conn, uid, cluster,
				DBD_GET_CLUSTER_USAGE,
				cluster_cond->usage_start,
				cluster_cond->usage_end);
		}

	}
	mysql_free_result(result);

	if (!list_count(assoc_cond.cluster_list)) {
		FREE_NULL_LIST(assoc_cond.cluster_list);
		return cluster_list;
	}

	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, "root");

	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, "");

	assoc_list = as_mysql_get_assocs(mysql_conn, uid, &assoc_cond);
	FREE_NULL_LIST(assoc_cond.cluster_list);
	FREE_NULL_LIST(assoc_cond.acct_list);
	FREE_NULL_LIST(assoc_cond.user_list);

	if (!assoc_list)
		return cluster_list;

	itr = list_iterator_create(cluster_list);
	assoc_itr = list_iterator_create(assoc_list);
	while ((cluster = list_next(itr))) {
		while ((assoc = list_next(assoc_itr))) {
			if (xstrcmp(assoc->cluster, cluster->name))
				continue;

			if (cluster->root_assoc) {
				debug("This cluster %s already has "
				      "an association.", cluster->name);
				continue;
			}
			cluster->root_assoc = assoc;
			list_remove(assoc_itr);
		}
		list_iterator_reset(assoc_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(assoc_itr);
	if (list_count(assoc_list))
		error("I have %d left over associations",
		      list_count(assoc_list));
	FREE_NULL_LIST(assoc_list);

	return cluster_list;
}

extern List as_mysql_get_cluster_events(mysql_conn_t *mysql_conn, uint32_t uid,
					slurmdb_event_cond_t *event_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List ret_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	time_t now = time(NULL);
	List use_cluster_list = NULL;
	slurmdb_user_rec_t user;
	bool locked = false;

	/* if this changes you will need to edit the corresponding enum */
	char *event_req_inx[] = {
		"cluster_nodes",
		"node_name",
		"state",
		"time_start",
		"time_end",
		"reason",
		"reason_uid",
		"tres",
	};

	enum {
		EVENT_REQ_CNODES,
		EVENT_REQ_NODE,
		EVENT_REQ_STATE,
		EVENT_REQ_START,
		EVENT_REQ_END,
		EVENT_REQ_REASON,
		EVENT_REQ_REASON_UID,
		EVENT_REQ_TRES,
		EVENT_REQ_COUNT
	};

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (slurm_conf.private_data & PRIVATE_DATA_EVENTS) {
		if (!is_user_min_admin_level(
			      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR)) {
			error("UID %u tried to access events, only administrators can look at events",
			      uid);
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}
	}

	if (!event_cond)
		goto empty;

	if (event_cond->cpus_min) {
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		xstrfmtcat(extra,
			   "(CONVERT(SUBSTRING_INDEX(tres,'%d=',-1),"
			   "UNSIGNED INTEGER)",
			   TRES_CPU);

		if (event_cond->cpus_max) {
			xstrfmtcat(extra, " between %u and %u))",
				   event_cond->cpus_min, event_cond->cpus_max);

		} else {
			xstrfmtcat(extra, "='%u'))",
				   event_cond->cpus_min);

		}
	}

	switch(event_cond->event_type) {
	case SLURMDB_EVENT_ALL:
		break;
	case SLURMDB_EVENT_CLUSTER:
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrcat(extra, "node_name = '')");

		break;
	case SLURMDB_EVENT_NODE:
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrcat(extra, "node_name != '')");

		break;
	default:
		error("Unknown event %u doing all", event_cond->event_type);
		break;
	}

	if (event_cond->node_list) {
		int dims = 0;
		hostlist_t temp_hl = NULL;

		if (get_cluster_dims(mysql_conn,
				     (char *)list_peek(event_cond->cluster_list),
				     &dims))
			return NULL;

		temp_hl = hostlist_create_dims(event_cond->node_list, dims);
		if (hostlist_count(temp_hl) <= 0) {
			error("we didn't get any real hosts to look for.");
			return NULL;
		}

		set = 0;
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		while ((object = hostlist_shift(temp_hl))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "node_name='%s'", object);
			set = 1;
			free(object);
		}
		xstrcat(extra, ")");
		hostlist_destroy(temp_hl);
	}

	if (event_cond->period_start) {
		if (!event_cond->period_end)
			event_cond->period_end = now;

		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		if (event_cond->cond_flags & SLURMDB_EVENT_COND_OPEN)
			xstrfmtcat(extra,
				   "(time_start >= %ld) && (time_end = 0))",
				   event_cond->period_start);
		else
			xstrfmtcat(extra,
				   "(time_start < %ld) "
				   "&& (time_end >= %ld || time_end = 0))",
				   event_cond->period_end,
				   event_cond->period_start);

	} else if (event_cond->cond_flags & SLURMDB_EVENT_COND_OPEN) {
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		xstrfmtcat(extra, "time_end = 0)");
	}

	if (event_cond->reason_list
	    && list_count(event_cond->reason_list)) {
		set = 0;
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->reason_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "reason like '%%%s%%'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (event_cond->reason_uid_list
	    && list_count(event_cond->reason_uid_list)) {
		set = 0;
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->reason_uid_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "reason_uid='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if (event_cond->state_list
	    && list_count(event_cond->state_list)) {
		set = 0;
		if (extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->state_list);
		while ((object = list_next(itr))) {
			uint32_t tmp_state = strtol(object, NULL, 10);
			if (set)
				xstrcat(extra, " || ");
			if (tmp_state & NODE_STATE_BASE)
				xstrfmtcat(extra, "(state&%u)=%u",
					   NODE_STATE_BASE,
					   tmp_state & NODE_STATE_BASE);
			else
				xstrfmtcat(extra, "state&%u", tmp_state);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:
	xfree(tmp);
	xstrfmtcat(tmp, "%s", event_req_inx[0]);
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		bool include = true;
		if (event_cond && event_cond->format_list)
			include = list_find_first(event_cond->format_list,
						  slurm_find_char_in_list,
						  event_req_inx[i]);
		xstrfmtcat(tmp, ", %s%s",
			   include ? "" : "'' as ", event_req_inx[i]);
	}

	if (event_cond && event_cond->cluster_list &&
	    list_count(event_cond->cluster_list)) {
		use_cluster_list = event_cond->cluster_list;
	} else {
		slurm_rwlock_rdlock(&as_mysql_cluster_list_lock);
		use_cluster_list = list_shallow_copy(as_mysql_cluster_list);
		locked = true;
	}

	ret_list = list_create(slurmdb_destroy_event_rec);

	itr = list_iterator_create(use_cluster_list);
	while ((object = list_next(itr))) {
		query = xstrdup_printf("select %s from \"%s_%s\"",
				       tmp, object, event_table);
		if (extra)
			xstrfmtcat(query, " %s", extra);

		DB_DEBUG(DB_ASSOC, mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			if (mysql_errno(mysql_conn->db_conn)
			    != ER_NO_SUCH_TABLE) {
				FREE_NULL_LIST(ret_list);
				ret_list = NULL;
			}
			break;
		}
		xfree(query);

		while ((row = mysql_fetch_row(result))) {
			slurmdb_event_rec_t *event =
				xmalloc(sizeof(slurmdb_event_rec_t));

			list_append(ret_list, event);

			event->cluster = xstrdup(object);

			if (row[EVENT_REQ_NODE] && row[EVENT_REQ_NODE][0]) {
				event->node_name = xstrdup(row[EVENT_REQ_NODE]);
				event->event_type = SLURMDB_EVENT_NODE;
			} else
				event->event_type = SLURMDB_EVENT_CLUSTER;

			event->state = slurm_atoul(row[EVENT_REQ_STATE]);
			event->period_start = slurm_atoul(row[EVENT_REQ_START]);
			event->period_end = slurm_atoul(row[EVENT_REQ_END]);

			if (row[EVENT_REQ_REASON] && row[EVENT_REQ_REASON][0])
				event->reason = xstrdup(row[EVENT_REQ_REASON]);
			event->reason_uid =
				slurm_atoul(row[EVENT_REQ_REASON_UID]);

			if (row[EVENT_REQ_CNODES] && row[EVENT_REQ_CNODES][0])
				event->cluster_nodes =
					xstrdup(row[EVENT_REQ_CNODES]);

			if (row[EVENT_REQ_TRES] && row[EVENT_REQ_TRES][0])
				event->tres_str = xstrdup(row[EVENT_REQ_TRES]);
		}
		mysql_free_result(result);
	}
	list_iterator_destroy(itr);
	xfree(tmp);
	xfree(extra);

	if (locked) {
		FREE_NULL_LIST(use_cluster_list);
		slurm_rwlock_unlock(&as_mysql_cluster_list_lock);
	}

	return ret_list;
}

extern int as_mysql_node_down(mysql_conn_t *mysql_conn,
			      node_record_t *node_ptr,
			      time_t event_time, char *reason,
			      uint32_t reason_uid)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	char *my_reason;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!mysql_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	if (!node_ptr) {
		error("No node_ptr given!");
		return SLURM_ERROR;
	}

	if (!node_ptr->tres_str) {
		error("node ptr has no tres_list!");
		return SLURM_ERROR;
	}

	query = xstrdup_printf("select state, reason, time_start from \"%s_%s\" where "
			       "time_end=0 and node_name='%s';",
			       mysql_conn->cluster_name, event_table,
			       node_ptr->name);
	/* info("%d(%s:%d) query\n%s", */
	/*        mysql_conn->conn, THIS_FILE, __LINE__, query); */
	result = mysql_db_query_ret(mysql_conn, query, 0);
	xfree(query);

	if (!result)
		return SLURM_ERROR;

	if (reason)
		my_reason = reason;
	else
		my_reason = node_ptr->reason;

	if (!my_reason)
		my_reason = "";

	row = mysql_fetch_row(result);
	if (row && (node_ptr->node_state == slurm_atoul(row[0])) &&
	    !xstrcasecmp(my_reason, row[1])) {
		DB_DEBUG(DB_EVENT, mysql_conn->conn,
		         "no change to %s(%s) needed %u == %s and %s == %s",
		         node_ptr->name, mysql_conn->cluster_name,
		         node_ptr->node_state, row[0], my_reason, row[1]);
		mysql_free_result(result);
		return SLURM_SUCCESS;
	}

	if (row && (event_time == slurm_atoul(row[2]))) {
		/*
		 * If you are clean-restarting the controller over and over
		 * again you could get records that are duplicates in the
		 * database. If this is the case we will zero out the time_end
		 * we are just filled in. This will cause the last time to be
		 * erased from the last restart, but if you are restarting
		 * things this often the pervious one didn't mean anything
		 * anyway. This way we only get one for the last time we let it
		 * run.
		 */
		query = xstrdup_printf(
			"update \"%s_%s\" set reason='%s' where "
			"time_start=%ld and node_name='%s';",
			mysql_conn->cluster_name, event_table,
			my_reason, event_time, node_ptr->name);
		DB_DEBUG(DB_EVENT, mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);

		mysql_free_result(result);
		return rc;
	}

	mysql_free_result(result);

	DB_DEBUG(DB_EVENT, mysql_conn->conn,
	         "inserting %s(%s) with tres of '%s'",
		 node_ptr->name, mysql_conn->cluster_name, node_ptr->tres_str);

	query = xstrdup_printf(
		"update \"%s_%s\" set time_end=%ld where "
		"time_end=0 and node_name='%s';",
		mysql_conn->cluster_name, event_table,
		event_time, node_ptr->name);

	/*
	 * Reason for "on duplicate": slurmctld will send a time_start based on
	 * the state of the "node_state" state file. If the the slurmctld is
	 * "killed" before updating the state file, the slurmctld can send the
	 * same time_start for the node and cause a "Duplicate entry" error.
	 * This can particually happen when doing clean starts.
	 */
	xstrfmtcat(query,
		   "insert into \"%s_%s\" "
		   "(node_name, state, tres, time_start, "
		   "reason, reason_uid) "
		   "values ('%s', %u, '%s', %ld, '%s', %u) "
		   "on duplicate key update time_end=0;",
		   mysql_conn->cluster_name, event_table,
		   node_ptr->name, node_ptr->node_state,
		   node_ptr->tres_str, event_time, my_reason, reason_uid);
	DB_DEBUG(DB_EVENT, mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	return rc;
}

extern int as_mysql_node_up(mysql_conn_t *mysql_conn,
			    node_record_t *node_ptr,
			    time_t event_time)
{
	char* query;
	int rc = SLURM_SUCCESS;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!mysql_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	query = xstrdup_printf(
		"update \"%s_%s\" set time_end=%ld where "
		"time_end=0 and node_name='%s';",
		mysql_conn->cluster_name, event_table,
		event_time, node_ptr->name);
	DB_DEBUG(DB_EVENT, mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	return rc;
}

/* This function is not used in the slurmdbd. */
extern int as_mysql_register_ctld(mysql_conn_t *mysql_conn,
				  char *cluster, uint16_t port)
{
	return SLURM_ERROR;
}

extern int as_mysql_fini_ctld(mysql_conn_t *mysql_conn,
			      slurmdb_cluster_rec_t *cluster_rec)
{
	int rc = SLURM_SUCCESS;
	time_t now = time(NULL);
	char *query = NULL;
	bool free_it = false;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* Here we need to check make sure we are updating the entry
	   correctly just in case the backup has already gained
	   control.  If we check the ip and port it is a pretty safe
	   bet we have the right ctld.
	*/
	query = xstrdup_printf(
		"update %s set mod_time=%ld, control_host='', "
		"control_port=0 where name='%s' && "
		"control_host='%s' && control_port=%u;",
		cluster_table, now, cluster_rec->name,
		cluster_rec->control_host, cluster_rec->control_port);
	DB_DEBUG(DB_EVENT, mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	if (rc != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (!last_affected_rows(mysql_conn) || !slurmdbd_conf->track_ctld ||
	    (cluster_rec->flags & CLUSTER_FLAG_EXT))
		return rc;

	/* If tres is NULL we can get the current number of tres by
	   sending NULL for the tres param in the as_mysql_cluster_tres
	   function.
	*/
	if (!cluster_rec->tres_str) {
		free_it = true;
		as_mysql_cluster_tres(
			mysql_conn, cluster_rec->control_host,
			&cluster_rec->tres_str, now,
			cluster_rec->rpc_version);
	}

	/* Since as_mysql_cluster_tres could change the
	   last_affected_rows we can't group this with the above
	   return.
	*/
	if (!cluster_rec->tres_str)
		return rc;

	/* If we affected things we need to now drain the nodes in the
	 * cluster.  This is to give better stats on accounting that
	 * the ctld was gone so no jobs were able to be scheduled.  We
	 * drain the nodes since the rollup functionality understands
	 * how to deal with that and running jobs so we don't get bad
	 * info.
	 */
	query = xstrdup_printf(
		"insert into \"%s_%s\" (tres, state, time_start, reason) "
		"values ('%s', %u, %ld, 'slurmctld disconnect');",
		cluster_rec->name, event_table,
		cluster_rec->tres_str, NODE_STATE_DOWN, (long)now);

	if (free_it)
		xfree(cluster_rec->tres_str);

	DB_DEBUG(DB_EVENT, mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	return rc;
}

extern int as_mysql_cluster_tres(mysql_conn_t *mysql_conn,
				 char *cluster_nodes, char **tres_str_in,
				 time_t event_time, uint16_t rpc_version)
{
	char* query;
	int rc = SLURM_SUCCESS;
	int response = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	bool handle_disconnect = true;

	xassert(tres_str_in);

 	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!mysql_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	/* Record the processor count */
	query = xstrdup_printf(
		"select tres, cluster_nodes from \"%s_%s\" where "
		"time_end=0 and node_name='' and state=0 limit 1",
		mysql_conn->cluster_name, event_table);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		if (mysql_errno(mysql_conn->db_conn) == ER_NO_SUCH_TABLE)
			rc = ESLURM_ACCESS_DENIED;
		else
			rc = SLURM_ERROR;
		return rc;
	}
	xfree(query);

	/* we only are checking the first one here */
	if (!(row = mysql_fetch_row(result))) {
		debug("We don't have an entry for this machine %s "
		      "most likely a first time running.",
		      mysql_conn->cluster_name);

		/* Get all nodes in a down state and jobs pending or running.
		 * This is for the first time a cluster registers
		 *
		 * We will return ACCOUNTING_FIRST_REG so this
		 * is taken care of since the message thread
		 * may not be up when we run this in the controller or
		 * in the slurmdbd.
		 */
		if (!*tres_str_in) {
			rc = 0;
			goto end_it;
		}

		response = ACCOUNTING_FIRST_REG;
		goto add_it;
	}

	/* If tres is NULL we want to return the tres for this cluster */
	if (!*tres_str_in) {
		*tres_str_in = xstrdup(row[0]);
		goto end_it;
	} else if (xstrcmp(*tres_str_in, row[0])) {
		debug("%s has changed tres from %s to %s",
		      mysql_conn->cluster_name,
		      row[0], *tres_str_in);

		/*
		 * Reset all the entries for this cluster since the tres changed
		 * some of the downed nodes may have gone away.
		 * Request them again with ACCOUNTING_NODES_CHANGE_DB
		 */

		if (xstrcmp(cluster_nodes, row[1])) {
			DB_DEBUG(DB_EVENT, mysql_conn->conn,
			         "Nodes on the cluster have changed.");
			response = ACCOUNTING_NODES_CHANGE_DB;
		} else
			response = ACCOUNTING_TRES_CHANGE_DB;
	} else if (xstrcmp(cluster_nodes, row[1])) {
		DB_DEBUG(DB_EVENT, mysql_conn->conn,
		         "Node names on the cluster have changed.");
		response = ACCOUNTING_NODES_CHANGE_DB;
	} else {
		DB_DEBUG(DB_EVENT, mysql_conn->conn,
		         "We have the same TRES and node names as before for %s, no need to update the database.",
		         mysql_conn->cluster_name);
		goto remove_disconnect;
	}

	query = xstrdup_printf(
		"update \"%s_%s\" set time_end=%ld where time_end=0",
		mysql_conn->cluster_name, event_table, event_time);

	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	handle_disconnect = false;

	if (rc != SLURM_SUCCESS)
		goto end_it;
add_it:
	query = xstrdup_printf(
		"insert into \"%s_%s\" (cluster_nodes, tres, "
		"time_start, reason) "
		"values ('%s', '%s', %ld, 'Cluster Registered TRES') "
		"on duplicate key update time_end=0, tres=VALUES(tres);",
		mysql_conn->cluster_name, event_table,
		cluster_nodes, *tres_str_in, event_time);

	rc = mysql_db_query(mysql_conn, query);
	xfree(query);

	if (trigger_reroll(mysql_conn, event_time))
		debug("Need to reroll usage from %s, cluster %s changes happened before last rollup.",
		      slurm_ctime2(&event_time), mysql_conn->cluster_name);

	if (rc != SLURM_SUCCESS)
		goto end_it;

remove_disconnect:
	/*
	 * The above update clears all with time_end=0, so no
	 * need to do this again.
	 */
	if (handle_disconnect) {
		query = xstrdup_printf(
			"update \"%s_%s\" set time_end=%ld where time_end=0 and state=%u and node_name='';",
			mysql_conn->cluster_name,
			event_table, event_time,
			NODE_STATE_DOWN);
		(void) mysql_db_query(mysql_conn, query);
		xfree(query);
	}

end_it:
	mysql_free_result(result);
	if (response && rc == SLURM_SUCCESS)
		rc = response;

	return rc;
}
