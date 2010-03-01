/*****************************************************************************\
 *  cluster.c - accounting interface to pgsql - clusters related functions.
 *
 *  $Id: cluster.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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
#include "common.h"


char *cluster_table = "cluster_table";
static storage_field_t cluster_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "name", "TEXT NOT NULL" },
	{ "control_host", "TEXT DEFAULT '' NOT NULL" },
	{ "control_port", "INTEGER DEFAULT 0 NOT NULL" },
	{ "rpc_version", "INTEGER DEFAULT 0 NOT NULL" },
	{ "classification", "INTEGER DEFAULT 0" },
	{ NULL, NULL}
};
static char *cluster_table_constraint = ", "
	"PRIMARY KEY (name)"
	")";

/*
 * _create_function_add_cluster - create a PL/PGSQL function to add cluster
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_cluster(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_cluster "
		"(cluster %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (cluster.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s "
		"      SET (deleted, mod_time, control_host, control_port) ="
		"          (0, cluster.mod_time, '', 0)"
		"      WHERE name=cluster.name;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		cluster_table, cluster_table, cluster_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * check_cluster_tables - check cluster related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_cluster_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, cluster_table, cluster_table_fields,
			 cluster_table_constraint, user);
	rc |= _create_function_add_cluster(db_conn);
	return rc;
}

/*
 * as_p_add_clusters - add clusters
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN cluster_list: clusters to add
 * RET: error code
 */
extern int
as_p_add_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
		  List cluster_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS, added = 0;
	acct_cluster_rec_t *object = NULL;
	time_t now = time(NULL);
	List assoc_list = NULL;
	acct_association_rec_t *assoc = NULL;
	char *txn_info = NULL, *query = NULL, *user_name = NULL;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	assoc_list = list_create(destroy_acct_association_rec);

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(cluster_list);
	while((object = list_next(itr))) {
		if(!object->name) {
			error("as/pg: add_clusters: We need a cluster "
			      "name to add.");
			rc = SLURM_ERROR;
			continue;
		}

		query = xstrdup_printf(
			"SELECT add_cluster((%d, %d, 0, '%s', '', 0, 0, %u));",
			now, now, object->name,
			object->classification);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add cluster %s", object->name);
			added = 0; /* rollback modification to DB */
			break;
		}

		/* add root account assoc: <'cluster', 'root', '', ''> */
		/* TODO: does object->root_assoc valid? */
		if (add_cluster_root_assoc(pg_conn, now, object, &txn_info)
		    != SLURM_SUCCESS) {
			added = 0;
			break;
		}

		if (add_txn(pg_conn, now, DBD_ADD_CLUSTERS, object->name,
			    user_name, txn_info) != SLURM_SUCCESS) {
			error("as/pg: add_cluster: couldn't add txn");
		} else
			added ++;

		xfree(txn_info);

		/* Add user root by default to run from the root
		 * association.  This gets popped off so we need to
		 * read it every time here.
		 */
		assoc = xmalloc(sizeof(acct_association_rec_t));
		init_acct_association_rec(assoc);
		list_append(assoc_list, assoc);

		assoc->cluster = xstrdup(object->name);
		assoc->user = xstrdup("root");
		assoc->acct = xstrdup("root");
		if(acct_storage_p_add_associations(pg_conn, uid, assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding root user association");
			rc = SLURM_ERROR;
		}
		list_flush(assoc_list); /* do not add it again, in case not popped */
	}
	list_iterator_destroy(itr);
	xfree(user_name);
	list_destroy(assoc_list);

	if(!added) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
	}
	return rc;
}

/*
 * as_p_modify_clusters - modify clusters
 *   This is called by cs_p_register_ctld when ctld registers to dbd.
 *   Also called when modify classification of cluster.
 *   If you need to alter the default values of the cluster, use
 *   modify_associations to change root association of the cluster.
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN cluster_cond: which clusters to modify
 * IN cluster: attribute of clusters after modification
 * RET: list of clusters modified
 */
extern List
as_p_modify_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
		     acct_cluster_cond_t *cluster_cond,
		     acct_cluster_rec_t *cluster)
{
	List ret_list = NULL;
	int rc = SLURM_SUCCESS, set = 0;
	char *object = NULL, *user_name = NULL,	*name_char = NULL;
	char *vals = NULL, *cond = NULL, *query = NULL, *send_char = NULL;
	time_t now = time(NULL);
	PGresult *result = NULL;
	bool clust_reg = false;

	if (!cluster_cond || !cluster) {
		error("as/pg: modify_clusters: we need something to change");
		return NULL;
	}

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if(!pg_conn->cluster_name
	   && cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list))
		pg_conn->cluster_name =
			xstrdup(list_peek(cluster_cond->cluster_list));

	concat_cond_list(cluster_cond->cluster_list, NULL, "name", &cond);
	if(cluster_cond->classification) {
		xstrfmtcat(cond, " AND (classification & %u)",
			   cluster_cond->classification);
	}

	set = 0;
	if(cluster->control_host) {
		xstrfmtcat(vals, ", control_host='%s'", cluster->control_host);
		set++;
		clust_reg = true;
	}
	if(cluster->control_port) {
		xstrfmtcat(vals, ", control_port=%u", cluster->control_port);
		set++;
		clust_reg = true;
	}
	if(cluster->rpc_version) {
		xstrfmtcat(vals, ", rpc_version=%u", cluster->rpc_version);
		set++;
		clust_reg = true;
	}
	if(cluster->classification) {
		xstrfmtcat(vals, ", classification=%u",
			   cluster->classification);
	}

	if(!vals) {
		xfree(cond);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("as/pg: modify_clusters: nothing to change");
		return NULL;
	} else if(clust_reg && (set != 3)) {
		xfree(vals);
		xfree(cond);
		errno = EFAULT;
		error("as/pg: modify_clusters: need control host, port and "
		      "rpc version to register a cluster");
		return NULL;
	}

	query = xstrdup_printf(
		"SELECT name, control_port FROM %s WHERE deleted=0 %s;",
		cluster_table, cond ?: "");
	xfree(cond);
	result = DEF_QUERY_RET;
	if (!result) {
		xfree(vals);
		error("as/pg: modify_clusters: no result given");
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		object = xstrdup(ROW(0));
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
		}
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: modify_cluster: nothing effected");
		xfree(vals);
		return ret_list;
	}

	if(vals) {
		send_char = xstrdup_printf("(%s)", name_char);
		user_name = uid_to_string((uid_t) uid);
		rc = aspg_modify_common(pg_conn, DBD_MODIFY_CLUSTERS, now,
					user_name, cluster_table,
					send_char, vals);
		xfree(user_name);
		xfree(send_char);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't modify cluster 1");
			list_destroy(ret_list);
			ret_list = NULL;
			goto end_it;
		}
	}
end_it:
	xfree(name_char);
	xfree(vals);
	return ret_list;
}


/*
 * as_p_remove_clusters - remove clusters
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN cluster_cond: clusters to remove
 * RET: list of clusters removed
 */
extern List
as_p_remove_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
		     acct_cluster_cond_t *cluster_cond)
{
	List ret_list = NULL;
	List tmp_list = NULL;
	int rc = SLURM_SUCCESS;
	char *cond = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	acct_wckey_cond_t wckey_cond;
	PGresult *result = NULL;

	if(!cluster_cond) {
		error("as/pg: remove_clusters: we need something to remove");
		return NULL;
	}

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	concat_cond_list(cluster_cond->cluster_list, NULL, "name", &cond);
	if(!cond) {
		error("as/pg: remove_clusters: nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0 %s;",
			       cluster_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if (!result) {
		error("as/pg: remove_clusters: failed to get cluster names");
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		char *object = xstrdup(ROW(0));
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "name='%s'", object);
			xstrfmtcat(cond, "cluster='%s'", object);
			xstrfmtcat(assoc_char, "t1.cluster='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
			xstrfmtcat(cond, " OR cluster='%s'", object);
			xstrfmtcat(assoc_char, " OR t1.cluster='%s'", object);
		}
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: remove_clusters: didn't effect anything");
		return ret_list;
	}

	/* remove these clusters from the wckey table */
	memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
	wckey_cond.cluster_list = ret_list;
	tmp_list = acct_storage_p_remove_wckeys(pg_conn, uid, &wckey_cond);
	if(tmp_list)
		list_destroy(tmp_list);

	/* We should not need to delete any cluster usage just set it
	 * to deleted */
	xstrfmtcat(query,
		   "UPDATE %s SET period_end=%d WHERE period_end=0 AND (%s);"
		   "UPDATE %s SET mod_time=%d, deleted=1 WHERE (%s);"
		   "UPDATE %s SET mod_time=%d, deleted=1 WHERE (%s);"
		   "UPDATE %s SET mod_time=%d, deleted=1 WHERE (%s);",
		   event_table, now, cond,
		   cluster_day_table, now, cond,
		   cluster_hour_table, now, cond,
		   cluster_month_table, now, cond);
	xfree(cond);
	rc = DEF_QUERY_RET_RC;
	if(rc != SLURM_SUCCESS) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
		list_destroy(ret_list);
		xfree(name_char);
		xfree(assoc_char);
		return NULL;
	}

	cond = xstrdup_printf("t1.acct='root' AND (%s)", assoc_char);
	xfree(assoc_char);

	user_name = uid_to_string((uid_t) uid);
	rc = aspg_remove_common(pg_conn, DBD_REMOVE_CLUSTERS, now, user_name,
				cluster_table, name_char, cond);
	xfree(user_name);
	xfree(name_char);
	xfree(cond);
	if (rc  == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}
	return ret_list;
}

/*
 * as_p_get_clusters -  get clusters
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN cluster_cond: which clusters to get
 * RET: the clusters
 */
extern List
as_p_get_clusters(pgsql_conn_t *pg_conn, uid_t uid,
		  acct_cluster_cond_t *cluster_cond)
{
	char *query = NULL, *cond = NULL;
	PGresult *result = NULL;
	acct_association_cond_t assoc_cond;
	acct_cluster_rec_t *cluster = NULL;
	acct_association_rec_t *assoc = NULL;
	List cluster_list = NULL, assoc_list = NULL;
	ListIterator itr = NULL, assoc_itr = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *gc_fields = "name, classification, control_host, "
		"control_port, rpc_version";
	enum {
		GC_NAME,
		GC_CLASS,
		GC_CH,
		GC_CP,
		GC_VERSION,
		GC_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if(!cluster_cond) {
		xstrcat(cond, "WHERE deleted=0");
		goto empty;
	}

	if(cluster_cond->with_deleted)
		xstrcat(cond, "WHERE (deleted=0 OR deleted=1)");
	else
		xstrcat(cond, "WHERE deleted=0");
	concat_cond_list(cluster_cond->cluster_list, NULL, "name", &cond);
empty:
	query = xstrdup_printf("SELECT %s FROM %s %s",
			       gc_fields, cluster_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if (!result) {
		error("failed to get clusters");
		return NULL;
	}

	cluster_list = list_create(destroy_acct_cluster_rec);
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	if(cluster_cond) {
		/* I don't think we want the with_usage flag here.
		 * We do need the with_deleted though. */
		//assoc_cond.with_usage = cluster_cond->with_usage;
		assoc_cond.with_deleted = cluster_cond->with_deleted;
	}
	/* not destroyed, since owned by cluster record */
	assoc_cond.cluster_list = list_create(NULL);

	FOR_EACH_ROW {
		cluster = xmalloc(sizeof(acct_cluster_rec_t));
		list_append(cluster_list, cluster);

		cluster->name = xstrdup(ROW(GC_NAME));
		list_append(assoc_cond.cluster_list, cluster->name);

		/* get the usage if requested */
		if(cluster_cond && cluster_cond->with_usage) {
			clusteracct_storage_p_get_usage(
				pg_conn, uid, cluster,
				DBD_GET_CLUSTER_USAGE,
				cluster_cond->usage_start,
				cluster_cond->usage_end);
		}

		cluster->classification = atoi(ROW(GC_CLASS));
		cluster->control_host = xstrdup(ROW(GC_CH));
		cluster->control_port = atoi(ROW(GC_CP));
		cluster->rpc_version = atoi(ROW(GC_VERSION));

		get_cluster_cpu_nodes(pg_conn, cluster);
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(assoc_cond.cluster_list)) {
		list_destroy(assoc_cond.cluster_list);
		return cluster_list;
	}

	/* get root assoc: <cluster, root, '', ''> */
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, "root");

	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, "");

	assoc_list = acct_storage_p_get_associations(pg_conn, uid, &assoc_cond);
	list_destroy(assoc_cond.cluster_list);
	list_destroy(assoc_cond.acct_list);
	list_destroy(assoc_cond.user_list);

	if(!assoc_list)
		return cluster_list;

	itr = list_iterator_create(cluster_list);
	assoc_itr = list_iterator_create(assoc_list);
	while((cluster = list_next(itr))) {
		while((assoc = list_next(assoc_itr))) {
			if(strcmp(assoc->cluster, cluster->name))
				continue;

			if(cluster->root_assoc) {
				debug("This cluster %s already has "
				      "an association.");
				continue;
			}
			cluster->root_assoc = assoc;
			list_remove(assoc_itr);
		}
		list_iterator_reset(assoc_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(assoc_itr);
	if(list_count(assoc_list))
		error("I have %d left over associations",
		      list_count(assoc_list));
	list_destroy(assoc_list);
	return cluster_list;
}
