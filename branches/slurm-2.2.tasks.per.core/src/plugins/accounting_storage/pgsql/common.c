/*****************************************************************************\
 *  common.c - accounting interface to pgsql - common functions.
 *
 *  $Id: common.c 13061 2008-01-22 21:23:56Z da $
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

#define DELETE_SEC_BACK (3600*24)

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
			if (prefix)
				xstrfmtcat(*cond_str, "%s.%s like '%%%s%%'",
					   prefix, col, object);
			else
				xstrfmtcat(*cond_str, "%s like '%%%s%%'",
					   col, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond_str, ")");
	}
}

/*
 * concat_limit - concat resource limit to record string and txn string
 *
 * IN col: column name
 * IN limit: limit values
 * OUT rec: record string
 * OUT txn: transcation string
 */
extern void
concat_limit(char *col, int limit, char **rec, char **txn)
{
	if (limit >= 0) {
		if (rec)
			xstrfmtcat(*rec, "%d, ", limit);
		if (txn)
			xstrfmtcat(*txn, ",%s=%d", col, limit);
	} else {
		if (rec)
			xstrcat(*rec, "NULL, ");
		if (limit == INFINITE) {
			if (txn)
				xstrfmtcat(*txn, ",%s=NULL", col);
		}
	}
}

/*
 * aspg_modify_common - modify the entity table and insert a txn record
 *
 * IN pg_conn: database connection
 * IN type: modification action type
 * IN now: current time
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
aspg_modify_common(pgsql_conn_t *pg_conn, uint16_t type, time_t now,
		   char *user_name, char *table, char *name_char,
		   char *vals)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;

	xstrfmtcat(query, "UPDATE %s SET mod_time=%d %s "
		   "WHERE deleted=0 AND %s;",
		   table, now, vals, name_char);
	rc = DEF_QUERY_RET_RC;
	if (rc == SLURM_SUCCESS)
		rc = add_txn(pg_conn, now, type, name_char, user_name, vals);

	if(rc != SLURM_SUCCESS) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}


/*
 * _check_jobs_before_remove - check if there are jobs related to
 *   entities to be removed
 * IN pg_conn: database connection
 * IN assoc_char: entity condition. XXX: every option has "t1." prefix.
 * RET: true if there are related jobs
 */
static bool
_check_jobs_before_remove(pgsql_conn_t *pg_conn, char *assoc_char)
{
	char *query = NULL;
	bool rc = false;
	PGresult *result = NULL;

	query = xstrdup_printf(
		"SELECT t0.associd FROM %s AS t0, %s AS t1, %s AS t2 "
		"WHERE (t2.lft BETWEEN t1.lft AND t1.rgt) AND (%s) "
		"AND t0.associd=t2.id limit 1;",
		job_table, assoc_table, assoc_table, assoc_char);

	result = DEF_QUERY_RET;
	if(!result) {
		return rc;
	}

	if(PQntuples(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	PQclear(result);
	return rc;
}


/*
 * _check_jobs_before_remove_assoc - check if there are jobs related with
 *   association to be removed
 * IN pg_conn: database connection
 * IN assoc_char: association condition. XXX: every option has "t1." prefix.
 * RET: true if there are related jobs
 */
static bool
_check_jobs_before_remove_assoc(pgsql_conn_t *pg_conn, char *assoc_char)
{
	char *query = NULL;
	bool rc = false;
	PGresult *result = NULL;

	query = xstrdup_printf("SELECT t1.associd FROM %s AS t1, "
			       "%s AS t2 WHERE (%s) "
			       "AND t1.associd=t2.id LIMIT 1;",
			       job_table, assoc_table, assoc_char);

	result = DEF_QUERY_RET;
	if(! result)
		return rc;
	if(PQntuples(result)) {
		debug4("We have jobs for this assoc");
		rc = true;
	}
	PQclear(result);
	return rc;
}

/*
 * _check_jobs_before_remove_without_assoctable - check if there are jobs related with
 *   entities(non-association related) to be removed
 * IN pg_conn: database connection
 * IN assoc_char: association condition. XXX: every option has "t1." prefix.
 * RET: true if there are related jobs
 */
static bool
_check_jobs_before_remove_without_assoctable(pgsql_conn_t *pg_conn, char *where_char)
{
	char *query = NULL;
	PGresult *result = NULL;
	bool rc = false;

	query = xstrdup_printf("SELECT associd FROM %s AS t1 WHERE (%s) LIMIT 1;",
			       job_table, where_char);

	result = DEF_QUERY_RET;
	if(!result)
		return rc;

	if(PQntuples(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	PQclear(result);
	return rc;
}


/*
 * aspg_remove_common - remove entities from corresponding
 *   table and insert a record in txn_table
 *
 * IN pg_conn: database connection
 * IN type: remove action type
 * IN now: current time
 * IN user_name: user performing the remove operation
 * IN table: name of the table to modify
 * IN name_char: objects to remove
 *    FORMAT: "name=val1 OR name=val2..."
 * IN assoc_char: associations related to objects removed
 *    XXX: FORMAT: "t1.field1=val1 OR t1.field2=val2..."
 * RET: error code
 */
extern int
aspg_remove_common(pgsql_conn_t *pg_conn, uint16_t type, time_t now,
		   char *user_name, char *table, char *name_char,
		   char *assoc_char)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL, *loc_assoc_char = NULL;
	time_t day_old = now - DELETE_SEC_BACK;
	bool has_jobs = false;

	/*
	 * check if there are jobs associated with the related associations.
	 * if true, do not deleted the entities physically for accouting.
	 */
	if (table == acct_coord_table) {
		/* jobs not directly relate to coordinators. */
	} else if (table == qos_table || table == wckey_table) {
		has_jobs = _check_jobs_before_remove_without_assoctable(
			pg_conn, assoc_char);
	} else if (table != assoc_table) {
		has_jobs = _check_jobs_before_remove(pg_conn, assoc_char);
	} else {
		/* XXX: name_char, instead of assoc_char */
		has_jobs = _check_jobs_before_remove_assoc(pg_conn, name_char);
	}

	/* remove completely all that is less than a day old */
	if(!has_jobs && (table != assoc_table)) {
		query = xstrdup_printf(
			"DELETE FROM %s WHERE creation_time>%d AND (%s);",
			table, day_old, name_char);
	}
	if(table != assoc_table) {
		xstrfmtcat(query,
			   "UPDATE %s SET mod_time=%d, deleted=1 "
			   "WHERE deleted=0 AND (%s);",
			   table, now, name_char);
	}
	if (query) {
		rc = DEF_QUERY_RET_RC;
	}

	if (rc == SLURM_SUCCESS)
		add_txn(pg_conn, now, type, name_char, user_name, "");
	if (rc != SLURM_SUCCESS) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);

		return SLURM_ERROR;
	}

	/* done if not assoc related entities */
	if(table == qos_table ||
	   table == acct_coord_table ||
	   table == wckey_table)
		return SLURM_SUCCESS;

	/* mark deleted=1 or remove completely the accounting tables */
	if(table == assoc_table) { /* children assoc included in assoc_char */
		loc_assoc_char = assoc_char; /* XXX: TODO: assoc_char or name_char? */
	} else { /* for other tables, find all children associations */
		List assoc_list;
		ListIterator itr;
		char *id;
		if(!assoc_char) {
			error("as/pg: remove_common: no assoc_char");
			rc = SLURM_ERROR;
			goto err_out;
		}

		/* TODO: define */
		if(!(assoc_list = find_children_assoc(pg_conn, assoc_char))) {
			error("as/pg: remove_common: failed to "
			      "find children assoc");
			goto err_out;
		}
		itr = list_iterator_create(assoc_list);
		while((id = list_next(itr))) {
			acct_association_rec_t *rem_assoc;

			if(!rc) {
				xstrfmtcat(loc_assoc_char, "t1.id=%s", id);
				rc = 1;
			} else {
				xstrfmtcat(loc_assoc_char, " OR t1.id=%s", id);
			}
			rem_assoc = xmalloc(sizeof(acct_association_rec_t));
			init_acct_association_rec(rem_assoc);

			rem_assoc->id = atoi(id);
			if(addto_update_list(pg_conn->update_list,
				     ACCT_REMOVE_ASSOC,
				     rem_assoc) != SLURM_SUCCESS)
				error("couldn't add to the update list");
		}
		list_iterator_destroy(itr);
		list_destroy(assoc_list);
	}

	if(!loc_assoc_char) {
		debug2("No associations with object being deleted\n");
		return rc;
	}

	/* mark association usage as deleted */
	rc = delete_assoc_usage(pg_conn, now, loc_assoc_char);
	if(rc != SLURM_SUCCESS) {
		goto err_out;
	}

	/* If we have jobs that have ran don't go through the logic of
	 * removing the associations. Since we may want them for
	 * reports in the future since jobs had ran.
	 */
	if(has_jobs)
		goto just_update;

	/* remove completely all the associations for this added in the last
	 * day, since they are most likely nothing we really wanted in
	 * the first place.
	 */
	rc = remove_young_assoc(pg_conn, now, loc_assoc_char);
	if(rc != SLURM_SUCCESS) {
		goto err_out;
	}

just_update:
	/* now update the associations themselves that are still
	 * around clearing all the limits since if we add them back
	 * we don't want any residue from past associations lingering
	 * around.
	 */
	query = xstrdup_printf("UPDATE %s AS t1 SET mod_time=%d, deleted=1, "
			       "fairshare=1, max_jobs=NULL, "
			       "max_nodes_per_job=NULL, "
			       "max_wall_duration_per_job=NULL, "
			       "max_cpu_mins_per_job=NULL "
			       "WHERE (%s);",
			       assoc_table, now,
			       loc_assoc_char);

	if(table != assoc_table)
		xfree(loc_assoc_char);

	DEBUG_QUERY;
	rc = pgsql_db_query(pg_conn->db_conn, query);
	xfree(query);

err_out:
	if(rc != SLURM_SUCCESS) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
	}

	return rc;
}


/*
 * is_user_admin - check whether user is admin
 *
 * IN pg_conn: database connection
 * IN uid: user to check
 * RET: true if user is admin
 */
extern int
is_user_admin(pgsql_conn_t *pg_conn, uid_t uid)
{
	/* This only works when running though the slurmdbd.
	 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
	 * SLURMDBD!
	 */
	if(slurmdbd_conf) {
		/* we have to check the authentication here in the
		 * plugin since we don't know what accounts are being
		 * referenced until after the query.  Here we will
		 * set if they are an operator or greater and then
		 * check it below after the query.
		 */
		if((uid == slurmdbd_conf->slurm_user_id || uid == 0) ||
		   assoc_mgr_get_admin_level(pg_conn, uid)
		   >= ACCT_ADMIN_OPERATOR)
			return 1;
		else
			return 0;
	} else {
		return 1;
	}
}

/*
 * is_user_any_coord - is the user coord of any account
 *
 * IN pg_conn: database connection
 * IN/OUT user: user record, which will be filled in
 * RET: 1 if the user is coord of some account, 0 else
 */
extern int
is_user_any_coord(pgsql_conn_t *pg_conn, acct_user_rec_t *user)
{
	if(assoc_mgr_fill_in_user(pg_conn, user, 1, NULL)
	   != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	return (user->coord_accts && list_count(user->coord_accts));
}

/*
 * is_coord - whether user is coord of account
 *
 * IN user: user
 * IN account: account
 * RET: 1 if user is coord of account
 */
extern int
is_coord(acct_user_rec_t *user, char *account)
{
	ListIterator itr;
	acct_coord_rec_t *coord;

	if (! user->coord_accts ||
	    list_count(user->coord_accts) == 0)
		return 0;

	itr = list_iterator_create(user->coord_accts);
	while((coord = list_next(itr))) {
		if(!strcasecmp(coord->name, account))
			break;
	}
	list_iterator_destroy(itr);
	return coord ? 1 : 0;
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
 * IN user: owner of the table
 * RET: error code
 */
extern int
check_table(PGconn *db_conn, char *table, storage_field_t *fields,
	    char *constraint, char *user)
{
	static char **tables = NULL;
	PGresult *result = NULL;
	char *query;
	int i, num;

	if (!tables) {
		query = xstrdup_printf("SELECT tablename FROM pg_tables "
				       "WHERE tableowner='%s' "
				       "AND tablename !~ '^pg_+' "
				       "AND tablename !~ '^sql_+'", user);
		result = pgsql_db_query_ret(db_conn, query);
		xfree(query);
		num = PQntuples(result);
		tables = xmalloc(sizeof(char *) * (num + 1));
		for (i = 0; i < num; i ++) {
			tables[i] = xstrdup(PQgetvalue(result, i, 0));
		}
		tables[num] = NULL;
		PQclear(result);
	}

	i = 0;
	while (tables[i] && strcmp(tables[i], table))
		i ++;

	if (!tables[i]) {
		debug("as/pg: table %s not found, create it", table);
		if(pgsql_db_create_table(db_conn, table, fields, constraint)
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(db_conn, table, fields))
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
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
 * setup_cluster_list_with_inx - get cluster record list within requested
 *   time period with used nodes. Used for deciding whether a nodelist is
 *   overlapping with the required nodes.
 *
 * IN pg_conn: database connection
 * IN job_cond: condition
 * OUT curr_cluster: current cluster record
 * RET: cluster record list
 */
extern List
setup_cluster_list_with_inx(pgsql_conn_t *pg_conn, acct_job_cond_t *job_cond,
			    void **curr_cluster)
{
	List local_cluster_list = NULL;
	time_t now = time(NULL);
	PGresult *result = NULL;
	hostlist_t temp_hl = NULL;
	hostlist_iterator_t h_itr = NULL;
	char *object = NULL, *query = NULL;

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
		goto no_hosts;
	}

	query = xstrdup_printf("SELECT cluster_nodes, period_start, "
			       "period_end FROM %s WHERE node_name='' "
			       "AND cluster_nodes !=''",
			       event_table);

	if((object = list_peek(job_cond->cluster_list)))
		xstrfmtcat(query, " AND cluster='%s'", object);

	if(job_cond->usage_start) {
		if(!job_cond->usage_end)
			job_cond->usage_end = now;

		xstrfmtcat(query, " AND ((period_start < %d) "
			   "AND (period_end >= %d || period_end = 0))",
			   job_cond->usage_end, job_cond->usage_start);
	}

	result = DEF_QUERY_RET;
	if(!result) {
		hostlist_destroy(temp_hl);
		return NULL;
	}

	h_itr = hostlist_iterator_create(temp_hl);
	local_cluster_list = list_create(_destroy_local_cluster);
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
			list_append(local_cluster_list, local_cluster);
			if(local_cluster->end == 0) {
				local_cluster->end = now;
				(*curr_cluster) = local_cluster;
			}
		} else
			_destroy_local_cluster(local_cluster);
	} END_EACH_ROW;
	PQclear(result);
	hostlist_iterator_destroy(h_itr);
	if(!list_count(local_cluster_list)) {
		list_destroy(local_cluster_list);
		local_cluster_list = NULL;
	}

no_hosts:
	hostlist_destroy(temp_hl);
	return local_cluster_list;
}

/*
 * good_nodes_from_inx - whether node index is within the used nodes
 *   of specified cluster
 *
 * IN local_cluster_list: cluster record list
 * IN object: current cluster record
 * IN node_inx: nodelist index
 * IN submit: submit time of job
 * RET 1 if the nodelist overlaps with used nodes
 */
extern int
good_nodes_from_inx(List local_cluster_list, void **object,
		    char *node_inx, int submit)
{
	local_cluster_t **curr_cluster = (local_cluster_t **)object;

	/* check the bitmap to see if this is one of the jobs
	   we are looking for */
	/* TODO: curr_cluster only set if end==0 above */
	if(*curr_cluster) {
		bitstr_t *job_bitmap = NULL;
		if(!node_inx || !node_inx[0])
			return 0;
		if((submit < (*curr_cluster)->start)
		   || (submit > (*curr_cluster)->end)) {
			local_cluster_t *local_cluster = NULL;

			ListIterator itr =
				list_iterator_create(local_cluster_list);
			while((local_cluster = list_next(itr))) {
				if((submit >= local_cluster->start)
				   && (submit <= local_cluster->end)) {
					*curr_cluster = local_cluster;
						break;
				}
			}
			list_iterator_destroy(itr);
			return 0;
		}
		job_bitmap = bit_alloc(hostlist_count((*curr_cluster)->hl));
		bit_unfmt(job_bitmap, node_inx);
		if(!bit_overlap((*curr_cluster)->asked_bitmap, job_bitmap)) {
			FREE_NULL_BITMAP(job_bitmap);
			return 0;
		}
		FREE_NULL_BITMAP(job_bitmap);
	}
	return 1;
}
