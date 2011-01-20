/*****************************************************************************\
 *  as_pg_resv.c - accounting interface to pgsql - reservation related
 *  functions.
 *
 *  $Id: as_pg_resv.c 13061 2008-01-22 21:23:56Z da $
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

/* per-cluster table */
char *resv_table = "resv_table";
static storage_field_t resv_table_fields[] = {
	{ "id_resv", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0 NOT NULL" },
	{ "assoclist", "TEXT DEFAULT '' NOT NULL" },
	{ "cpus", "INTEGER NOT NULL" },
	{ "flags", "INTEGER DEFAULT 0 NOT NULL" },
	{ "nodelist", "TEXT DEFAULT '' NOT NULL" },
	{ "node_inx", "TEXT DEFAULT '' NOT NULL" },
	{ "resv_name", "TEXT NOT NULL" },
	{ "time_start", "INTEGER DEFAULT 0 NOT NULL"},
	{ "time_end", "INTEGER DEFAULT 0 NOT NULL" },
	{ NULL, NULL}
};
static char *resv_table_constraint = ", "
	"PRIMARY KEY (id_resv, time_start) "
	")";

/*
 * _create_function_add_resv - create a PL/pgSQL function to add reservation
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_resv(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_resv (rec %s.%s) "
		"RETURNS VOID AS $$ "
		"BEGIN LOOP "
		"  BEGIN"
		"    INSERT INTO %s.%s VALUES (rec.id_resv, 0, rec.assoclist,"
		"      rec.cpus, rec.flags, rec.nodelist, rec.node_inx, "
		"      rec.resv_name, rec.time_start, rec.time_end); "
		"      RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET deleted=0 WHERE id_resv=rec.id_resv AND "
		"        time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		resv_table, cluster, resv_table, cluster, resv_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_modify_resv - create a function to modify reservation
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_modify_resv(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.modify_resv (rec %s.%s) "
		"RETURNS VOID AS $$ "
		"BEGIN "
		"  UPDATE %s.%s "
		"    SET resv_name=rec.resv_name, cpus=rec.cpus, "
		"      assoclist=rec.assoclist, nodelist=rec.nodelist, "
		"      node_inx=rec.node_inx, time_start=rec.time_start, "
		"      time_end=rec.time_end, flags=rec.flags"
		"    WHERE deleted=0 AND id_resv=rec.id_resv AND "
		"      time_start=rec.time_start; "
		"END; $$ LANGUAGE PLPGSQL;", cluster,
		cluster, resv_table, cluster, resv_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _make_resv_record - make resv_table record from reservation
 * IN resv: reservation given
 * RET: record string
 */
static char *
_make_resv_record(slurmdb_reservation_rec_t *resv)
{
	char *rec;
	int len = 0, start = 0;

	if (resv->assocs) {
		len = strlen(resv->assocs) - 1;
		if (resv->assocs[0] == ',')
			start = 1;
		if (resv->assocs[len] == ',')
			resv->assocs[len] = '\0';
	}

	rec = xstrdup_printf("(%d, 0, '%s', %d, %d, '%s', '%s', "
			     "'%s', %ld, %ld)",
			     resv->id,
			     /* deleted */
			     resv->assocs ? (resv->assocs + start) : "",
			     resv->cpus,
			     resv->flags,
			     resv->nodes,
			     resv->node_inx,
			     resv->name ? : "",
			     resv->time_start,
			     resv->time_end);
	return rec;
}

/*
 * _make_resv_cond - turn reservation condition into SQL condition string
 *
 * IN resv_cond: condition
 * OUT cond: SQL query condition string
 */
static void
_make_resv_cond(slurmdb_reservation_cond_t *resv_cond, char **cond)
{
	time_t now = time(NULL);

	concat_cond_list(resv_cond->id_list, NULL, "id_resv", cond);
	concat_cond_list(resv_cond->name_list, NULL, "resv_name", cond);
	if(resv_cond->time_start) {
		if(!resv_cond->time_end)
			resv_cond->time_end = now;

		xstrfmtcat(*cond, "AND (time_start<%ld "
			   "AND (time_end>=%ld OR time_end=0))",
			   resv_cond->time_end, resv_cond->time_start);
	} else if(resv_cond->time_end) {
		xstrfmtcat(*cond,
			   "AND (time_start < %ld)", resv_cond->time_end);
	}
}

/*
 * check_resv_tables - check reservation related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_resv_tables(PGconn *db_conn, char *cluster)
{
	int rc;

	rc = check_table(db_conn, cluster, resv_table, resv_table_fields,
			 resv_table_constraint);

	rc |= _create_function_add_resv(db_conn, cluster);
	rc |= _create_function_modify_resv(db_conn, cluster);
	return rc;
}


/*
 * as_pg_add_reservation - add reservation
 *
 * IN pg_conn: database connection
 * IN resv: reservation to add
 * RET: error code
 */
extern int
as_pg_add_reservation(pgsql_conn_t *pg_conn, slurmdb_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL, *rec = NULL;

	if(!resv) {
		error("as/pg: add_reservation: no reservation given");
		return SLURM_ERROR;
	}
	if(!resv->id) {
		error("as/pg: add_reservation: reservation id not given");
		return SLURM_ERROR;
	}
	if(!resv->time_start) {
		error("as/pg: add_reservation: start time not given");
		return SLURM_ERROR;
	}
	if(!resv->cluster || !resv->cluster[0]) {
		error("as/pg: add_reservation: cluster name not given");
		return SLURM_ERROR;
	}

	rec = _make_resv_record(resv);

	query  = xstrdup_printf("SELECT %s.add_resv(%s);", resv->cluster, rec);
	xfree(rec);
	rc = DEF_QUERY_RET_RC;
	if (rc != SLURM_SUCCESS) {
		error("as/pg: add_reservation: failed to add reservation");
	}
	return rc;
}

/*
 * as_pg_modify_reservation - modify reservation
 *
 * IN pg_conn: database connection
 * IN resv: reservation to modify
 * RET: error code
 */
extern int
as_pg_modify_reservation(pgsql_conn_t *pg_conn,
			 slurmdb_reservation_rec_t *resv)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS, set = 0;
	char *rec = NULL;
	time_t start = 0, now = time(NULL);
	char *mr_fields = "assoclist, time_start, time_end, cpus, "
		"resv_name, nodelist, node_inx, flags";
	enum {
		F_ASSOCS,
		F_START,
		F_END,
		F_CPU,
		F_NAME,
		F_NODES,
		F_NODE_INX,
		F_FLAGS,
		F_COUNT
	};

	if(!resv) {
		error("as/pg: modify_reservation: no reservation given");
		return SLURM_ERROR;
	}
	if(!resv->id) {
		error("as/pg: modify_reservation: reservation id not given");
		return SLURM_ERROR;
	}
	if(!resv->time_start) {
		error("as/pg: modify_reservation: time_start not given");
		return SLURM_ERROR;
	}
	if(!resv->cluster || !resv->cluster[0]) {
		error("as/pg: modify_reservation: cluster not given");
		return SLURM_ERROR;
	}
	if(!resv->time_start_prev) {
		error("as/pg: modify_reservation: time_start_prev not given");
		return SLURM_ERROR;
	}

	/* check for both the last start and the start because most
	   likely the start time hasn't changed, but something else
	   may have since the last time we did an update to the
	   reservation. */
	query = xstrdup_printf("SELECT %s FROM %s.%s WHERE id_resv=%u "
			       "AND (time_start=%ld OR time_start=%ld) "
			       "AND deleted=0 ORDER BY time_start DESC "
			       "LIMIT 1 FOR UPDATE;",
			       mr_fields, resv->cluster, resv_table, resv->id,
			       resv->time_start, resv->time_start_prev);
try_again:
	result = DEF_QUERY_RET;
	if (!result) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (PQntuples(result) == 0) {
		rc = SLURM_ERROR;
		PQclear(result);
		error("as/pg: modify_reservation: There is no reservation"
		      " by id %u, start %ld, and cluster '%s'", resv->id,
		      resv->time_start_prev, resv->cluster);
		if(!set && resv->time_end) {
			/* This should never really happen,
			   but just incase the controller and the
			   database get out of sync we check
			   to see if there is a reservation
			   not deleted that hasn't ended yet. */
			query = xstrdup_printf(
				"SELECT %s FROM %s.%s WHERE id_resv=%u "
				"AND time_start<=%ld AND deleted=0 "
				"ORDER BY start DESC LIMIT 1;",
				mr_fields, resv->cluster, resv_table, resv->id,
				resv->time_end);
			set = 1;
			goto try_again;
		}
		goto end_it;
	}

	start = atoi(PG_VAL(F_START));

	set = 0;

	/* check differences here */
	if(!resv->name && !PG_EMPTY(F_NAME))
		// if this changes we just update the
		// record, no need to create a new one since
		// this doesn't really effect the
		// reservation accounting wise
		resv->name = xstrdup(PG_VAL(F_NAME));

	if(resv->assocs)
		set = 1;
	else if(!PG_EMPTY(F_ASSOCS))
		resv->assocs = xstrdup(PG_VAL(F_ASSOCS));

	if(resv->cpus != (uint32_t)NO_VAL)
		set = 1;
	else
		resv->cpus = atoi(PG_VAL(F_CPU));

	if(resv->flags != (uint16_t)NO_VAL)
		set = 1;
	else
		resv->flags = atoi(PG_VAL(F_FLAGS));

	if(resv->nodes)
		set = 1;
	else if(! PG_EMPTY(F_NODES)) {
		resv->nodes = xstrdup(PG_VAL(F_NODES));
		resv->node_inx = xstrdup(PG_VAL(F_NODE_INX));
	}

	if(!resv->time_end)
		resv->time_end = atoi(PG_VAL(F_END));

	PQclear(result);

	rec = _make_resv_record(resv);
	/* use start below instead of resv->time_start_prev
	 * just incase we have a different one from being out
	 * of sync
	 */
	if((start > now) || !set) {
		/* we haven't started the reservation yet, or
		   we are changing the associations or end
		   time which we can just update it */
		query = xstrdup_printf("SELECT %s.modify_resv(%s);",
				       resv->cluster, rec);
	} else {
		/* time_start is already done above and we
		 * changed something that is in need on a new
		 * entry. */
		query = xstrdup_printf(
			"UPDATE %s.%s SET time_end=%ld WHERE deleted=0 AND "
			"id_resv=%u AND time_start=%ld;", resv->cluster,
			resv_table, resv->time_start-1, resv->id, start);

		xstrfmtcat(query, "SELECT %s.add_resv(%s);",
			   resv->cluster, rec);
	}
	rc = DEF_QUERY_RET_RC;

end_it:
	return rc;
}

/*
 * as_pg_remove_reservation - remove reservation
 *
 * IN pg_conn: database connection
 * IN resv: reservation to remove
 * RET error code
 */
extern int
as_pg_remove_reservation(pgsql_conn_t *pg_conn,
			 slurmdb_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if(!resv) {
		error("as/pg: remove_reservation: no reservation given");
		return SLURM_ERROR;
	}
	if(!resv->id || !resv->time_start || !resv->cluster) {
		error("as/pg: remove_reservation: id, start time "
		      " or cluster not given");
		return SLURM_ERROR;
	}

	/* first delete the resv that hasn't happened yet. */
	query = xstrdup_printf(
		"DELETE FROM %s.%s WHERE time_start>%ld AND id_resv=%u AND "
		"time_start=%ld; ", resv->cluster, resv_table,
		resv->time_start_prev, resv->id, resv->time_start);
	/* then update the remaining ones with a deleted flag and end
	 * time of the time_start_prev which is set to when the
	 * command was issued */
	xstrfmtcat(query, "UPDATE %s.%s SET time_end=%ld, deleted=1 WHERE "
		   "deleted=0 AND id_resv=%u AND time_start=%ld;",
		   resv->cluster, resv_table, resv->time_start_prev,
		   resv->id, resv->time_start);

	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * as_pg_get_reservations - get reservations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN resv_cond: which reservations to get
 * RET: reservations got
 */
extern List
as_pg_get_reservations(pgsql_conn_t *pg_conn, uid_t uid,
		       slurmdb_reservation_cond_t *resv_cond)
{
	DEF_VARS;
	//DEF_TIMERS;
	char *cond = NULL;
	List resv_list = NULL;
	int is_admin=0;
	slurmdb_job_cond_t job_cond;
	cluster_nodes_t *cnodes = NULL;
	/* needed if we don't have an resv_cond */
	uint16_t with_usage = 0;

	/* if this changes you will need to edit the corresponding enum */
	char *gr_fields = "id_resv, resv_name, cpus, assoclist, nodelist, "
		"node_inx, time_start, time_end, flags";
	enum {
		F_ID,
		F_NAME,
		F_CPUS,
		F_ASSOCS,
		F_NODES,
		F_NODE_INX,
		F_START,
		F_END,
		F_FLAGS,
		F_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if (check_user_op(pg_conn, uid, PRIVATE_DATA_RESERVATIONS,
			  &is_admin, NULL) != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found in db", uid);
		errno = ESLURM_USER_ID_MISSING;
		return NULL;
	}

	if (! is_admin) {
		error("as/pg: get_reservations: Only admins can look"
		      " at reservation");
		errno = ESLURM_ACCESS_DENIED;
		return NULL;
	}

	if(!resv_cond) {
		goto empty;
	}

	with_usage = resv_cond->with_usage;

	memset(&job_cond, 0, sizeof(slurmdb_job_cond_t));
	if(resv_cond->nodes) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
		job_cond.used_nodes = resv_cond->nodes;
		job_cond.cluster_list = resv_cond->cluster_list;
		cnodes = setup_cluster_nodes(pg_conn, &job_cond);
	} else if(with_usage) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
	}

	_make_resv_cond(resv_cond, &cond);

empty:
	FOR_EACH_CLUSTER(resv_cond->cluster_list) {
		if (query)
			xstrcat(query, " UNION ");
		query = xstrdup_printf(
			"SELECT DISTINCT %s, '%s' AS cluster FROM %s.%s "
			"WHERE deleted=0 %s ", gr_fields, cluster_name,
			cluster_name, resv_table, cond ?: "");
	} END_EACH_CLUSTER;
	xfree(cond);

	if (query)
		xstrcat(query, " ORDER BY cluster, resv_name;");
	result = DEF_QUERY_RET;
	if(!result) {
		error("as/pg: get_reservations: failed to get resv from db");
		if(cnodes)
			destroy_cluster_nodes(cnodes);
		return NULL;
	}

	resv_list = list_create(slurmdb_destroy_reservation_rec);

	FOR_EACH_ROW {
		slurmdb_reservation_rec_t *resv;
		int start;

		start = atoi(ROW(F_START));
		if(!good_nodes_from_inx(cnodes, ROW(F_NODE_INX), start))
			continue;

		resv = xmalloc(sizeof(slurmdb_reservation_rec_t));
		resv->id = atoi(ROW(F_ID));
		if(with_usage) {
			if(!job_cond.resvid_list)
				job_cond.resvid_list = list_create(NULL);
			list_append(job_cond.resvid_list, ROW(F_ID));
		}
		resv->name = xstrdup(ROW(F_NAME));
		resv->cluster = xstrdup(ROW(F_COUNT));
		resv->cpus = atoi(ROW(F_CPUS));
		resv->assocs = xstrdup(ROW(F_ASSOCS));
		resv->nodes = xstrdup(ROW(F_NODES));
		resv->time_start = start;
		resv->time_end = atoi(ROW(F_END));
		resv->flags = atoi(ROW(F_FLAGS));
		list_append(resv_list, resv);
	} END_EACH_ROW;

	if(cnodes)
		destroy_cluster_nodes(cnodes);

	if(with_usage && resv_list && list_count(resv_list)) {
		ListIterator itr = NULL, itr2 = NULL;
		slurmdb_job_rec_t *job = NULL;
		slurmdb_reservation_rec_t *resv = NULL;
		List job_list = jobacct_storage_p_get_jobs_cond(
			pg_conn, uid, &job_cond);

		if(!job_list || !list_count(job_list))
			goto no_jobs;

		itr = list_iterator_create(job_list);
		itr2 = list_iterator_create(resv_list);
		while((job = list_next(itr))) {
			int start = job->start;
			int end = job->end;
			int set = 0;
			while((resv = list_next(itr2))) {
				int elapsed = 0;
				/* since a reservation could have
				   changed while a job was running we
				   have to make sure we get the time
				   in the correct record.
				*/
				if(resv->id != job->resvid)
					continue;
				set = 1;

				if(start < resv->time_start)
					start = resv->time_start;
				if(!end || end > resv->time_end)
					end = resv->time_end;

				if((elapsed = (end - start)) < 1)
					continue;

				if(job->alloc_cpus)
					resv->alloc_secs +=
						elapsed * job->alloc_cpus;
			}
			list_iterator_reset(itr2);
			if(!set) {
				error("we got a job %u with no reservation "
				      "associatied with it?", job->jobid);
			}
		}
		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);
	no_jobs:
		if(job_list)
			list_destroy(job_list);
	}

	if(job_cond.resvid_list) {
		list_destroy(job_cond.resvid_list);
		job_cond.resvid_list = NULL;
	}

	/* free result after we use the list with resv id's in it. */
	PQclear(result);	/* job_cond.resvid_list */

	//END_TIMER2("get_resvs");
	return resv_list;
}
