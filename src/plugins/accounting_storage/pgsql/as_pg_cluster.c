/*****************************************************************************\
 *  as_pg_cluster.c - accounting interface to pgsql - clusters related
 *  functions.
 *
 *  $Id: as_pg_cluster.c 13061 2008-01-22 21:23:56Z da $
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
#include "as_pg_common.h"

/* shared table */
static char *cluster_table_name = "cluster_table";
char *cluster_table = "public.cluster_table";
static storage_field_t cluster_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "name", "TEXT NOT NULL" },
	{ "control_host", "TEXT DEFAULT '' NOT NULL" },
	{ "control_port", "INTEGER DEFAULT 0 NOT NULL" },
	{ "rpc_version", "INTEGER DEFAULT 0 NOT NULL" },
	{ "classification", "INTEGER DEFAULT 0" },
	{ "dimensions", "INTEGER DEFAULT 1" },
	{ "plugin_id_select", "INTEGER DEFAULT 0" },
	{ "flags", "INTEGER DEFAULT 0" },
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
		"CREATE OR REPLACE FUNCTION public.add_cluster "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s "
		"      SET (deleted, mod_time, control_host, control_port, "
		"           classification, flags) ="
		"          (0, rec.mod_time, '', 0, rec.classification, "
		"           rec.flags)"
		"      WHERE name=rec.name;"
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
check_cluster_tables(PGconn *db_conn)
{
	int rc;

	rc = check_table(db_conn, "public", cluster_table_name,
			 cluster_table_fields, cluster_table_constraint);
	rc |= _create_function_add_cluster(db_conn);
	return rc;
}

/* create per-cluster tables */
static int
_create_cluster_tables(pgsql_conn_t *pg_conn, char *cluster)
{
	char *query;
	int rc = SLURM_SUCCESS;

	query = xstrdup_printf("CREATE SCHEMA %s;", cluster);
	rc = DEF_QUERY_RET_RC;

	if (rc == SLURM_SUCCESS)
		rc = check_assoc_tables(pg_conn->db_conn, cluster);
	if (rc == SLURM_SUCCESS)
		rc = check_event_tables(pg_conn->db_conn, cluster);
	if (rc == SLURM_SUCCESS)
		rc = check_job_tables(pg_conn->db_conn, cluster);
	if (rc == SLURM_SUCCESS)
		rc = check_resv_tables(pg_conn->db_conn, cluster);
	if (rc == SLURM_SUCCESS)
		rc = check_wckey_tables(pg_conn->db_conn, cluster);
	if (rc == SLURM_SUCCESS)
		rc = check_usage_tables(pg_conn->db_conn, cluster);

	return rc;
}

/* remove per-cluster tables */
static int
_remove_cluster_tables(pgsql_conn_t *pg_conn, char *cluster)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS;

	/* keep one copy of backup */
	query = xstrdup_printf(
		"SELECT nspname FROM pg_namespace WHERE nspname='%s_deleted';",
		cluster);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;
	if (PQntuples(result) != 0) {
		query = xstrdup_printf("DROP SCHEMA %s_deleted CASCADE;", cluster);
		rc = DEF_QUERY_RET_RC;
	}
	PQclear(result);
	if (rc == SLURM_SUCCESS) {
		query = xstrdup_printf("ALTER SCHEMA %s RENAME TO %s_deleted;",
				       cluster, cluster);
		rc = DEF_QUERY_RET_RC;
	}
	return rc;
}


/*
 * as_pg_add_clusters - add clusters
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN cluster_list: clusters to add
 * RET: error code
 */
extern int
as_pg_add_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
		   List cluster_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS, added = 0;
	slurmdb_cluster_rec_t *object = NULL;
	time_t now = time(NULL);
	List assoc_list = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	char *txn_info = NULL, *query = NULL, *user_name = NULL;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	assoc_list = list_create(slurmdb_destroy_association_rec);
	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(cluster_list);
	while((object = list_next(itr))) {
		if(!object->name) {
			error("as/pg: add_clusters: We need a cluster "
			      "name to add.");
			rc = SLURM_ERROR;
			continue;
		}
		if (strchr(object->name, '.')) {
			error("as/pg: add_clusters: invalid cluster name %s",
			      object->name);
			rc = SLURM_ERROR;
			continue;
		}
		if (cluster_in_db(pg_conn, object->name)) {
			error("cluster %s already added", object->name);
			rc = SLURM_ERROR;
			continue;
		}

		query = xstrdup_printf(
			"SELECT public.add_cluster("
			"(%ld, %ld, 0, '%s', '', 0, 0, %u, 1, 0, 0));",
			(long)now, (long)now, object->name,
			object->classification);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add cluster %s", object->name);
			added = 0; /* rollback modification to DB */
			break;
		}

		rc = _create_cluster_tables(pg_conn, object->name);
		if (rc != SLURM_SUCCESS) {
			error("Failed creating cluster tables for %s",
			      object->name);
			added = 0;
			break;
		}

		/* add root account assoc: <'cluster', 'root', '', ''> */
		if (add_cluster_root_assoc(pg_conn, now, object, &txn_info)
		    != SLURM_SUCCESS) {
			added = 0;
			break;
		}

		if (add_txn(pg_conn, now, "", DBD_ADD_CLUSTERS, object->name,
			    user_name, txn_info) != SLURM_SUCCESS) {
			error("as/pg: add_cluster: couldn't add txn");
		} else {
			added ++;
		}
		xfree(txn_info);

		/* Add user root by default to run from the root
		 * association.  This gets popped off so we need to
		 * read it every time here.
		 */
		assoc = xmalloc(sizeof(slurmdb_association_rec_t));
		slurmdb_init_association_rec(assoc, 0);
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

	if (!added) {
		reset_pgsql_conn(pg_conn);
	} else {
		/* when loading sacctmgr cfg file,
		   get_assoc will be called before commit
		*/
		pg_conn->cluster_changed = 1;
	}

	return rc;
}

/*
 * as_pg_modify_clusters - modify clusters
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
as_pg_modify_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
		      slurmdb_cluster_cond_t *cluster_cond,
		      slurmdb_cluster_rec_t *cluster)
{
	DEF_VARS;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS, set = 0;
	char *object = NULL, *user_name = NULL,	*name_char = NULL;
	char *vals = NULL, *cond = NULL, *send_char = NULL;
	time_t now = time(NULL);
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
	if (cluster->control_host) {
		xstrfmtcat(vals, ", control_host='%s'", cluster->control_host);
		set++;
		clust_reg = true;
	}
	if (cluster->control_port) {
		xstrfmtcat(vals, ", control_port=%u", cluster->control_port);
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
		rc = pgsql_modify_common(pg_conn, DBD_MODIFY_CLUSTERS, now,
					 "", user_name, cluster_table,
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

/* get running jobs of specified cluster */
static List
_get_cluster_running_jobs(pgsql_conn_t *pg_conn, char *cluster)
{
	DEF_VARS;
	List job_list = NULL;
	char *job;
	char *fields = "t0.id_job,t1.acct,t1.user_name,t1.partition";

	query = xstrdup_printf(
		"SELECT %s FROM %s.%s AS t0, %s.%s AS t1, %s.%s AS t2 WHERE "
		"(t1.lft BETWEEN t2.lft AND t2.rgt) AND t2.acct='root' AND "
		"t0.id_assoc=t1.id_assoc AND t0.time_end=0 AND t0.state=%d;",
		fields, cluster, job_table, cluster, assoc_table, cluster,
		assoc_table, (int)JOB_RUNNING);
	result = DEF_QUERY_RET;
	if (!result)
		return NULL;

	FOR_EACH_ROW {
		if (ISEMPTY(2)) {
			error("how could job %s running on non-user "
			      "assoc <%s, %s, '', ''>", ROW(0),
			      ROW(4), ROW(1));
			continue;
		}
		job = xstrdup_printf(
			"JobID = %-10s C = %-10s A = %-10s U = %-9s",
			ROW(0), cluster, ROW(1), ROW(2));
		if(!ISEMPTY(3))
			xstrfmtcat(job, " P = %s", ROW(3));
		if (!job_list)
			job_list = list_create(slurm_destroy_char);
		list_append(job_list, job);
	} END_EACH_ROW;
	PQclear(result);
	return job_list;
}

/* whether specified cluster has jobs in db */
static int
_cluster_has_jobs(pgsql_conn_t *pg_conn, char *cluster)
{
	DEF_VARS;
	int has_jobs = 0;

	query = xstrdup_printf("SELECT id_assoc FROM %s.%s LIMIT 1;",
			       cluster, job_table);
	result = DEF_QUERY_RET;
	if (result) {
		has_jobs = (PQntuples(result) != 0);
		PQclear(result);
	}
	return has_jobs;
}

/*
 * as_pg_remove_clusters - remove clusters
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN cluster_cond: clusters to remove
 * RET: list of clusters removed
 */
extern List
as_pg_remove_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
		      slurmdb_cluster_cond_t *cluster_cond)
{
	DEF_VARS;
	List ret_list = NULL, job_list = NULL;
	int rc = SLURM_SUCCESS, has_jobs;
	char *cond = NULL, *user_name = NULL;
	time_t now = time(NULL);

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

	ret_list = list_create(slurm_destroy_char);
	if (PQntuples(result) == 0) {
		PQclear(result);
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything");
		/* XXX: if we return NULL, test21.27 will fail to execute */
		return ret_list;
	}

	user_name = uid_to_string((uid_t)uid);
	rc = 0;
	FOR_EACH_ROW {
		char *cluster = ROW(0);

		job_list = _get_cluster_running_jobs(pg_conn, cluster);
		if (job_list)
			break;

		has_jobs = _cluster_has_jobs(pg_conn, cluster);

		if (!has_jobs)
			query = xstrdup_printf(
				"DELETE FROM %s WHERE creation_time>%ld AND "
				"name='%s';", cluster_table,
				(now - DELETE_SEC_BACK), cluster);
		xstrfmtcat(query,
			   "UPDATE %s SET mod_time=%ld, deleted=1 WHERE "
			   "deleted=0 AND name='%s';", cluster_table, now,
			   cluster);
		xstrfmtcat(query,
			   "INSERT INTO %s (timestamp, action, name, actor) "
			   "VALUES (%ld, %d, '%s', '%s');", txn_table, now,
			   (int)DBD_REMOVE_CLUSTERS, cluster, user_name);

		rc = DEF_QUERY_RET_RC;
		if (rc != SLURM_SUCCESS)
			break;

		rc = _remove_cluster_tables(pg_conn, cluster);
		if (rc != SLURM_SUCCESS)
			break;

		list_append(ret_list, xstrdup(cluster));
		addto_update_list(pg_conn->update_list, SLURMDB_REMOVE_CLUSTER,
				  xstrdup(cluster));
		pg_conn->cluster_changed = 1;
	} END_EACH_ROW;
	PQclear(result);

	if (job_list) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		error("as/pg: remove_clusters: jobs running on cluster");
		errno = ESLURM_JOBS_RUNNING_ON_ASSOC;
		return job_list;
	}
	if (rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		ret_list = NULL;
	}
	return ret_list;
}

/*
 * as_pg_get_clusters -  get clusters
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN cluster_cond: which clusters to get
 * RET: the clusters
 */
extern List
as_pg_get_clusters(pgsql_conn_t *pg_conn, uid_t uid,
		   slurmdb_cluster_cond_t *cluster_cond)
{
	DEF_VARS;
	char *cond = NULL;
	slurmdb_association_cond_t assoc_cond;
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	List cluster_list = NULL, assoc_list = NULL;
	ListIterator itr = NULL, assoc_itr = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *gc_fields = "name,classification,control_host,control_port,"
		"rpc_version,dimensions,flags,plugin_id_select";
	enum {
		F_NAME,
		F_CLASS,
		F_CH,
		F_CP,
		F_VERSION,
		F_DIMS,
		F_FLAGS,
		F_PI_SELECT,
		F_COUNT
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

	cluster_list = list_create(slurmdb_destroy_cluster_rec);
	memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
	if(cluster_cond) {
		/* I don't think we want the with_usage flag here.
		 * We do need the with_deleted though. */
		//assoc_cond.with_usage = cluster_cond->with_usage;
		assoc_cond.with_deleted = cluster_cond->with_deleted;
	}
	/* not destroyed, since owned by cluster record */
	assoc_cond.cluster_list = list_create(NULL);

	FOR_EACH_ROW {
		cluster = xmalloc(sizeof(slurmdb_cluster_rec_t));
		list_append(cluster_list, cluster);

		cluster->name = xstrdup(ROW(F_NAME));
		list_append(assoc_cond.cluster_list, cluster->name);

		/* get the usage if requested */
		if(cluster_cond && cluster_cond->with_usage) {
			as_pg_get_usage(pg_conn, uid, cluster,
				       DBD_GET_CLUSTER_USAGE,
				       cluster_cond->usage_start,
				       cluster_cond->usage_end);
		}

		cluster->classification = atoi(ROW(F_CLASS));
		cluster->control_host = xstrdup(ROW(F_CH));
		cluster->control_port = atoi(ROW(F_CP));
		cluster->rpc_version = atoi(ROW(F_VERSION));
		cluster->dimensions = atoi(ROW(F_DIMS));
		cluster->flags = atoi(ROW(F_FLAGS));
		cluster->plugin_id_select = atoi(ROW(F_PI_SELECT));

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
	if(list_count(assoc_list))
		error("I have %d left over associations",
		      list_count(assoc_list));
	list_destroy(assoc_list);
	return cluster_list;
}

extern List
get_cluster_names(PGconn *db_conn)
{
	PGresult *result = NULL;
	List ret_list = NULL;
	char *query = xstrdup_printf("SELECT name from %s WHERE deleted=0",
				     cluster_table);

	result = pgsql_db_query_ret(db_conn, query);
	xfree(query);
	if (!result)
		return NULL;

	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		if (! ISEMPTY(0))
			list_append(ret_list, xstrdup(ROW(0)));
	} END_EACH_ROW;
	PQclear(result);
	return ret_list;
}
