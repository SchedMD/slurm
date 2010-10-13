/*****************************************************************************\
 *  as_pg_common.c - accounting interface to pgsql - common functions.
 *
 *  $Id: as_pg_common.c 13061 2008-01-22 21:23:56Z da $
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


/*
 * create_function_xfree - perform the create function query and
 *   xfree the query string
 * IN db_conn: database connection
 * IN query: create function query
 * RET: error code
 */
extern int
create_function_xfree(PGconn *db_conn, char *query)
{
	int rc = pgsql_db_query(db_conn, query);
	xfree(query);
	return rc;
}

/*
 * concat_cond_list - concat condition list to condition string
 *
 * IN cond_list: list of string values to match the column
 * IN prefix: table alias prefix
 * IN col: column name
 * OUT cond_str: condition string
 *   FORMAT: " AND ()..."
 */
extern void
concat_cond_list(List cond_list, char *prefix, char *col, char **cond_str)
{
	int set = 0;
	char *object;
	ListIterator itr = NULL;

	if(cond_list && list_count(cond_list)) {
		xstrcat(*cond_str, " AND (");
		itr = list_iterator_create(cond_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*cond_str, " OR ");
			if (prefix)
				xstrfmtcat(*cond_str, "%s.%s='%s'",
					   prefix, col, object);
			else
				xstrfmtcat(*cond_str, "%s='%s'", col, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond_str, ")");
	}
}

extern void
concat_node_state_cond_list(List cond_list, char *prefix,
			    char *col, char **cond_str)
{
	int set = 0;
	char *object;
	ListIterator itr = NULL;

	if(cond_list && list_count(cond_list)) {
		xstrcat(*cond_str, " AND (");
		itr = list_iterator_create(cond_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*cond_str, " OR ");
			/* node states are numeric */
			/* TODO: NODE_STATE_UNKNOWN == 0, fails the condition*/
			if (prefix)
				xstrfmtcat(*cond_str, "(%s.%s&%s)=%s",
					   prefix, col, object, object);
			else
				xstrfmtcat(*cond_str, "(%s&%s)=%s",
					   col, object, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond_str, ")");
	}
}

/*
 * concat_like_cond_list - concat condition list to condition string
 *   using like pattern match
 *
 * IN cond_list: list of string values to match the column
 * IN prefix: table alias prefix
 * IN col: column name
 * OUT cond_str: condition string
 *   FORMAT: " AND()..."
 */
extern void
concat_like_cond_list(List cond_list, char *prefix, char *col, char **cond_str)
{
	int set = 0;
	char *object;
	ListIterator itr = NULL;

	if(cond_list && list_count(cond_list)) {
		xstrcat(*cond_str, " AND (");
		itr = list_iterator_create(cond_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*cond_str, " OR ");
			/* XXX: strings cond_list turned to lower case
			   by slurm_addto_char_list().
			   And mixed-case strings in db are much readable */
			if (prefix)
/* 				xstrfmtcat(*cond_str, "%s.%s like '%%%s%%'", */
/* 					   prefix, col, object); */
				xstrfmtcat(*cond_str, "%s.%s ~* '.*%s.*'",
					   prefix, col, object);
			else
				xstrfmtcat(*cond_str, "%s ~* '.*%s.*'",
					   col, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond_str, ")");
	}
}

/*
 * concat_limit_32 - concat resource limit to record string and txn string
 *
 * IN col: column name
 * IN limit: limit values
 * OUT rec: record string
 * OUT txn: transcation string
 */
extern void
concat_limit_32(char *col, uint32_t limit, char **rec, char **txn)
{
	if (limit == INFINITE) {
		if (rec)
			xstrcat(*rec, "NULL, ");
		if (txn)
			xstrfmtcat(*txn, ", %s=NULL", col);
	} else if (limit != NO_VAL && (int32_t)limit >= 0) {
		if (rec)
			xstrfmtcat(*rec, "%u, ", limit);
		if (txn)
			xstrfmtcat(*txn, ", %s=%u", col, limit);
	} else {
		if (rec)
			xstrcat(*rec, "NULL, ");
	}
}

extern void
concat_limit_64(char *col, uint64_t limit, char **rec, char **txn)
{
	if (limit == (uint64_t)INFINITE) {
		if (rec)
			xstrcat(*rec, "NULL, ");
		if (txn)
			xstrfmtcat(*txn, ", %s=NULL", col);
	} else if (limit != (uint64_t) NO_VAL && (int64_t)limit >= 0) {
		if (rec)
			xstrfmtcat(*rec, "%"PRIu64", ", limit);
		if (txn)
			xstrfmtcat(*txn, ", %s=%"PRIu64"", col, limit);
	} else {
		if (rec)
			xstrcat(*rec, "NULL, ");
	}
}

/*
 * pgsql_modify_common - modify the entity table and insert a txn record
 *
 * IN pg_conn: database connection
 * IN type: modification action type
 * IN now: current time
 * IN cluster: which cluster is modified, "" for shared tables
 * IN user_name: user performing the modify operation
 * IN table: name of the table to modify
 * IN name_char: which entities to modify
 *    FORMAT: "(name=val1 OR name=val2...)"
 * IN vals: values of new attributes of the entities
 *    FORMAT: ", field1=val1,field2=val2..."
 *    NOTE the leading ", "
 * RET: error code
 */
extern int
pgsql_modify_common(pgsql_conn_t *pg_conn, uint16_t type, time_t now,
		    char *cluster, char *user_name, char *table,
		    char *name_char, char *vals)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;

	query = xstrdup_printf("UPDATE %s SET mod_time=%ld %s "
			       "WHERE deleted=0 AND %s;",
			       table, now, vals, name_char);
	rc = DEF_QUERY_RET_RC;
	if (rc == SLURM_SUCCESS)
		rc = add_txn(pg_conn, now, cluster, type, name_char,
			     user_name, (vals+2));

	if(rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}


/*
 * check_db_connection - check and re-establish database connection
 *
 * IN/OUT pg_conn: database connection
 * RET: error code
 */
extern int
check_db_connection(pgsql_conn_t *pg_conn)
{
	if (!pg_conn) {
		error("as/pg: we need a connection to run this");
		errno = SLURM_ERROR;
		return SLURM_ERROR;
	} else if(!pg_conn->db_conn ||
		  PQstatus(pg_conn->db_conn) != CONNECTION_OK) {
		info("as/pg: database connection lost.");
		PQreset(pg_conn->db_conn);
		if (PQstatus(pg_conn->db_conn) != CONNECTION_OK) {
			error("as/pg: failed to re-establish "
			      "database connection");
			errno = ESLURM_DB_CONNECTION;
			return ESLURM_DB_CONNECTION;
		}
	}
	return SLURM_SUCCESS;
}


/*
 * check_table - check account tables
 * IN db_conn: database connection
 * IN table: table name
 * IN fields: fields of the table
 * IN constraint: additional constraint of the table
 * RET: error code
 */
extern int
check_table(PGconn *db_conn, char *schema, char *table,
	    storage_field_t *fields, char *constraint)
{
	DEF_VARS;
	char **tables = NULL;
	int i, num, rc = SLURM_SUCCESS;

	query = xstrdup_printf(
		"SELECT tablename FROM pg_tables WHERE schemaname='%s' AND "
		"tableowner='%s' AND tablename !~ '^pg_+' "
		"AND tablename !~ '^sql_+'", schema, PQuser(db_conn));
	result = pgsql_db_query_ret(db_conn, query);
	xfree(query);
	if (!result)
		return SLURM_ERROR;

	num = PQntuples(result);
	tables = xmalloc(sizeof(char *) * (num + 1));
	for (i = 0; i < num; i ++)
		tables[i] = xstrdup(PQgetvalue(result, i, 0));
	tables[num] = NULL;
	PQclear(result);

	i = 0;
	while (tables[i] && strcmp(tables[i], table))
		i ++;

	if (!tables[i]) {
		debug("as/pg: table %s.%s not found, create it", schema, table);
		rc = pgsql_db_create_table(db_conn, schema, table, fields,
					   constraint);
	} else {
		rc = pgsql_db_make_table_current(
			db_conn, schema, table, fields);
	}
	for (i = 0; i < num; i ++)
		xfree(tables[i]);
	xfree(tables);
	return rc;
}

static void _destroy_local_cluster(void *object)
{
	local_cluster_t *local_cluster = (local_cluster_t *)object;
	if(local_cluster) {
		if(local_cluster->hl)
			hostlist_destroy(local_cluster->hl);
		FREE_NULL_BITMAP(local_cluster->asked_bitmap);
		xfree(local_cluster);
	}
}

/*
 * setup_cluster_nodes - get cluster record list within requested
 *   time period with used nodes. Used for deciding whether a nodelist is
 *   overlapping with the required nodes.
 */
extern cluster_nodes_t *
setup_cluster_nodes(pgsql_conn_t *pg_conn, slurmdb_job_cond_t *job_cond)
{
	DEF_VARS;
	cluster_nodes_t *cnodes = NULL;
	time_t now = time(NULL);
	hostlist_t temp_hl = NULL;
	hostlist_iterator_t h_itr = NULL;

	if(!job_cond || !job_cond->used_nodes)
		return NULL;

	if(!job_cond->cluster_list || list_count(job_cond->cluster_list) != 1) {
		error("If you are doing a query against nodes "
		      "you must only have 1 cluster "
		      "you are asking for.");
		return NULL;
	}

	temp_hl = hostlist_create(job_cond->used_nodes);
	if(!hostlist_count(temp_hl)) {
		error("we didn't get any real hosts to look for.");
		hostlist_destroy(temp_hl);
		return NULL;
	}

	query = xstrdup_printf("SELECT cluster_nodes, time_start, "
			       "time_end FROM %s.%s WHERE node_name='' "
			       "AND cluster_nodes !=''",
			       (char *)list_peek(job_cond->cluster_list),
			       event_table);

	if(job_cond->usage_start) {
		if(!job_cond->usage_end)
			job_cond->usage_end = now;

		xstrfmtcat(query, " AND ((time_start<%ld) "
			   "AND (time_end>=%ld OR time_end=0))",
			   job_cond->usage_end, job_cond->usage_start);
	}

	result = DEF_QUERY_RET;
	if(!result) {
		hostlist_destroy(temp_hl);
		return NULL;
	}

	h_itr = hostlist_iterator_create(temp_hl);
	cnodes = xmalloc(sizeof(cluster_nodes_t));
	cnodes->cluster_list = list_create(_destroy_local_cluster);
	FOR_EACH_ROW {
		char *host = NULL;
		int loc = 0;
		local_cluster_t *local_cluster =
			xmalloc(sizeof(local_cluster_t));
		local_cluster->hl = hostlist_create(ROW(0));
		local_cluster->start = atoi(ROW(1));
		local_cluster->end   = atoi(ROW(2));
		local_cluster->asked_bitmap =
			bit_alloc(hostlist_count(local_cluster->hl));
		while((host = hostlist_next(h_itr))) {
			if((loc = hostlist_find(
				    local_cluster->hl, host)) != -1)
				bit_set(local_cluster->asked_bitmap, loc);
			free(host);
		}
		hostlist_iterator_reset(h_itr);
		if(bit_ffs(local_cluster->asked_bitmap) != -1) {
			list_append(cnodes->cluster_list, local_cluster);
			if(local_cluster->end == 0) {
				local_cluster->end = now;
				cnodes->curr_cluster = local_cluster;
			}
		} else
			_destroy_local_cluster(local_cluster);
	} END_EACH_ROW;
	PQclear(result);
	hostlist_iterator_destroy(h_itr);
	if(!list_count(cnodes->cluster_list)) {
		destroy_cluster_nodes(cnodes);
		cnodes = NULL;
	}

	hostlist_destroy(temp_hl);
	return cnodes;
}

extern void
destroy_cluster_nodes(cluster_nodes_t *cnodes)
{
	if (cnodes) {
		list_destroy(cnodes->cluster_list);
		xfree(cnodes);
	}
}

/*
 * good_nodes_from_inx - whether node index is within the used nodes
 *   of specified cluster
 */
extern int
good_nodes_from_inx(cluster_nodes_t *cnodes, char *node_inx, int submit)
{
	bitstr_t *job_bitmap = NULL;

	if (! cnodes)
		return 1;

	if(!node_inx || !node_inx[0])
		return 0;

	if(!cnodes->curr_cluster ||
	   (submit < (cnodes->curr_cluster)->start) ||
	   (submit > (cnodes->curr_cluster)->end)) {
		local_cluster_t *local_cluster = NULL;
		ListIterator itr =
			list_iterator_create(cnodes->cluster_list);
		while((local_cluster = list_next(itr))) {
			if((submit >= local_cluster->start)
			   && (submit <= local_cluster->end)) {
				cnodes->curr_cluster = local_cluster;
				break;
			}
		}
		list_iterator_destroy(itr);
		if (! local_cluster)
			return 0;
	}
	job_bitmap = bit_alloc(hostlist_count((cnodes->curr_cluster)->hl));
	bit_unfmt(job_bitmap, node_inx);
	if(!bit_overlap((cnodes->curr_cluster)->asked_bitmap, job_bitmap)) {
		FREE_NULL_BITMAP(job_bitmap);
		return 0;
	}
	FREE_NULL_BITMAP(job_bitmap);
	return 1;
}

/* rollback and discard updates */
extern void
reset_pgsql_conn(pgsql_conn_t *pg_conn)
{
	int saved_errno = errno;

	if(pg_conn->rollback) {
		pgsql_db_rollback(pg_conn->db_conn);
	}
	list_flush(pg_conn->update_list);
	errno = saved_errno;
}


static inline int
check_user_admin_level(pgsql_conn_t *pg_conn, uid_t uid, uint16_t private,
		       slurmdb_admin_level_t min_level, int *is_admin,
		       slurmdb_user_rec_t *user)
{
	*is_admin = 1;
	if (user) {
		memset(user, 0, sizeof(slurmdb_user_rec_t));
		user->uid = uid;
	}

	if (!private || slurm_get_private_data() & private) {
		*is_admin = is_user_min_admin_level(pg_conn, uid, min_level);
		if (!*is_admin && user)
			return assoc_mgr_fill_in_user(pg_conn, user, 1, NULL);
	}
	return SLURM_SUCCESS;
}

extern int
check_user_op(pgsql_conn_t *pg_conn, uid_t uid, uint16_t private,
	      int *is_admin, slurmdb_user_rec_t *user)
{
	return check_user_admin_level(pg_conn, uid, private,
				      SLURMDB_ADMIN_OPERATOR, is_admin, user);
}


static int
_find_cluster_name(char *a, char *b)
{
	return !strcmp(a, b);
}

extern int
cluster_in_db(pgsql_conn_t *pg_conn, char *cluster_name)
{
	DEF_VARS;
	int found = 0;

	if (pg_conn->cluster_changed) {
		query = xstrdup_printf(
			"SELECT name FROM %s WHERE deleted=0 AND name='%s';",
			cluster_table, cluster_name);
		result = DEF_QUERY_RET;
		if (!result) {
			error("failed to query cluster name");
			return 0;
		}
		found = PQntuples(result);
		PQclear(result);
	} else {
		slurm_mutex_lock(&as_pg_cluster_list_lock);
		if (list_find_first(as_pg_cluster_list,
				    (ListFindF)_find_cluster_name,
				    cluster_name))
			found = 1;
		slurm_mutex_unlock(&as_pg_cluster_list_lock);
	}
	return found;
}

extern int
validate_cluster_list(List cluster_list)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster;

	slurm_mutex_lock(&as_pg_cluster_list_lock);
	if (cluster_list && list_count(cluster_list)) {
		itr = list_iterator_create(cluster_list);
		while((cluster = list_next(itr))) {
			if (!list_find_first(
				    as_pg_cluster_list,
				    (ListFindF)_find_cluster_name, cluster)) {
				error("cluster '%s' not in db", cluster);
				rc = SLURM_ERROR;
				break;
			}
		}
	}
	slurm_mutex_unlock(&as_pg_cluster_list_lock);
	return rc;
}
