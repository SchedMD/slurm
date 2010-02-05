/*****************************************************************************\
 *  reservation.c - accounting interface to pgsql - reservation
 *  related functions.
 *
 *  $Id: reservation.c 13061 2008-01-22 21:23:56Z da $
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

char *resv_table = "resv_table";
static storage_field_t resv_table_fields[] = {
	{ "id", "INTEGER DEFAULT 0 NOT NULL" },
	{ "name", "TEXT NOT NULL" },
	{ "cluster", "TEXT NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "cpus", "INTEGER NOT NULL" },
	{ "assoclist", "TEXT DEFAULT '' NOT NULL" },
	{ "nodelist", "TEXT DEFAULT '' NOT NULL" },
	{ "node_inx", "TEXT DEFAULT '' NOT NULL" },
	{ "start", "INTEGER DEFAULT 0 NOT NULL"},
	{ "endtime", "INTEGER DEFAULT 0 NOT NULL" },
	{ "flags", "INTEGER DEFAULT 0 NOT NULL" },
	{ NULL, NULL}
};
static char *resv_table_constraint = ", "
	"PRIMARY KEY (id, start, cluster) "
	")";

/*
 * _create_function_add_resv - create a PL/pgSQL function to add reservation
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_resv(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_resv (rec %s) "
		"RETURNS VOID AS $$ "
		"BEGIN LOOP "
		"  BEGIN"
		"    INSERT INTO %s VALUES (DEFAULT, rec.name, rec.cluster, "
		"      0, rec.cpus, rec.assoclist, rec.nodelist, rec.node_inx, "
		"      rec.start, rec.endtime, rec.flags); "
		"      RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET deleted=0;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		resv_table, resv_table, resv_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_modify_resv - create a function to modify reservation
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_modify_resv(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION modify_resv (resv %s) "
		"RETURNS INTEGER AS $$ "
		"DECLARE rid INTEGER;"
		"BEGIN "
		"  UPDATE %s "
		"    SET name=resv.name, cpus=resv.cpus, "
		"      assoclist=resv.assoclist, nodelist=resv.nodelist, "
		"      node_inx=resv.node_inx, start=resv.start, "
		"      endtime=resv.endtime, flags=resv.flags"
		"    WHERE deleted=0 AND id=resv.id AND start=resv.start "
		"      AND cluster=resv.cluster"
		"    RETURNING id INTO rid;"
		"  RETURN rid;"
		"END; $$ LANGUAGE PLPGSQL;",
		resv_table, resv_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _make_resv_record - make resv_table record from reservation
 * IN resv: reservation given
 * RET: record string
 */
static char *
_make_resv_record(acct_reservation_rec_t *resv)
{
	char *rec;
	char *assoc_list;
	int len = strlen(resv->assocs) - 1;
	int start = 0;

	if (resv->assocs[len] == ',')
		start = 1;

	assoc_list = resv->assocs;
	if (resv->assocs[0] == ',')
		assoc_list ++;

	rec = xstrdup_printf("(%d, '%s', '%s', 0, %d, '%s', '%s', "
			     "'%s', %d, %d, %d)",
			     resv->id,
			     resv->name ? : "",
			     resv->cluster,
			     resv->cpus,
			     resv->assocs + start,
			     resv->nodes,
			     resv->node_inx,
			     resv->time_start,
			     resv->time_end,
			     resv->flags);
	return rec;
}

/*
 * _make_resv_cond - turn reservation condition into SQL condition string
 *
 * IN resv_cond: condition
 * OUT cond: SQL query condition string
 */
static void
_make_resv_cond(acct_reservation_cond_t *resv_cond, char **cond)
{
	time_t now = time(NULL);

	concat_cond_list(resv_cond->cluster_list, NULL, "cluster", cond);
	concat_cond_list(resv_cond->id_list, NULL, "id", cond);
	concat_cond_list(resv_cond->name_list, NULL, "name", cond);
	if(resv_cond->time_start) {
		if(!resv_cond->time_end)
			resv_cond->time_end = now;

		xstrfmtcat(*cond, "AND (start < %d "
			   "AND (endtime >= %d OR endtime = 0))",
			   resv_cond->time_end, resv_cond->time_start);
	} else if(resv_cond->time_end) {
		xstrfmtcat(*cond,
			   "AND (start < %d)", resv_cond->time_end);
	}
}

/*
 * check_resv_tables - check reservation related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_resv_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, resv_table, resv_table_fields,
		    resv_table_constraint, user);

	rc |= _create_function_add_resv(db_conn);
	rc |= _create_function_modify_resv(db_conn);
	return rc;
}


/*
 * as_p_add_reservation - add reservation
 *
 * IN pg_conn: database connection
 * IN resv: reservation to add
 * RET: error code
 */
extern int
as_p_add_reservation(pgsql_conn_t *pg_conn, acct_reservation_rec_t *resv)
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
	if(!resv->cluster) {
		error("as/pg: add_reservation: cluster name not given");
		return SLURM_ERROR;
	}

	rec = _make_resv_record(resv);

	query  = xstrdup_printf("SELECT add_resv(%s);", rec);
	xfree(rec);
	rc = DEF_QUERY_RET_RC;
	if (rc != SLURM_SUCCESS) {
		error("as/pg: add_reservation: failed to add reservation");
	}
	return rc;
}

/*
 * as_p_modify_reservation - modify reservation
 *
 * IN pg_conn: database connection
 * IN resv: reservation to modify
 * RET: error code
 */
extern int
as_p_modify_reservation(pgsql_conn_t *pg_conn,
			acct_reservation_rec_t *resv)
{
	PGresult *result = NULL;
	int rc = SLURM_SUCCESS, set = 0;
	char *query = NULL, *rec = NULL;
	time_t start = 0, now = time(NULL);
	char *mr_fields = "assoclist, start, endtime, cpus, "
		"name, nodelist, node_inx, flags";
	enum {
		RESV_ASSOCS,
		RESV_START,
		RESV_END,
		RESV_CPU,
		RESV_NAME,
		RESV_NODES,
		RESV_NODE_INX,
		RESV_FLAGS,
		RESV_COUNT
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
	if(!resv->cluster) {
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
	query = xstrdup_printf("SELECT %s FROM %s WHERE id=%u "
			       "AND (start=%d OR start=%d) AND cluster='%s' "
			       "AND deleted=0 ORDER BY start DESC "
			       "LIMIT 1 FOR UPDATE;",
			       mr_fields, resv_table, resv->id,
			       resv->time_start, resv->time_start_prev,
			       resv->cluster);
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
		      " by id %u, start %d, and cluster '%s'", resv->id,
		      resv->time_start_prev, resv->cluster);
		if(!set && resv->time_end) {
			/* This should never really happen,
			   but just incase the controller and the
			   database get out of sync we check
			   to see if there is a reservation
			   not deleted that hasn't ended yet. */
			xfree(query);
			query = xstrdup_printf(
				"SELECT %s FROM %s WHERE id=%u "
				"AND start <= %d AND cluster='%s' "
				"AND deleted=0 ORDER BY start DESC "
				"LIMIT 1;",
				mr_fields, resv_table, resv->id,
				resv->time_end, resv->cluster);
			set = 1;
			goto try_again;
		}
		goto end_it;
	}

	start = atoi(PG_VAL(RESV_START));

	set = 0;

	/* check differences here */
	if(!resv->name && !PG_EMPTY(RESV_NAME))
		// if this changes we just update the
		// record, no need to create a new one since
		// this doesn't really effect the
		// reservation accounting wise
		resv->name = xstrdup(PG_VAL(RESV_NAME));

	if(resv->assocs)
		set = 1;
	else if(!PG_EMPTY(RESV_ASSOCS))
		resv->assocs = xstrdup(PG_VAL(RESV_ASSOCS));

	if(resv->cpus != (uint32_t)NO_VAL)
		set = 1;
	else
		resv->cpus = atoi(PG_VAL(RESV_CPU));

	if(resv->flags != (uint16_t)NO_VAL)
		set = 1;
	else
		resv->flags = atoi(PG_VAL(RESV_FLAGS));

	if(resv->nodes)
		set = 1;
	else if(! PG_EMPTY(RESV_NODES)) {
		resv->nodes = xstrdup(PG_VAL(RESV_NODES));
		resv->node_inx = xstrdup(PG_VAL(RESV_NODE_INX));
	}

	if(!resv->time_end)
		resv->time_end = atoi(PG_VAL(RESV_END));

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
		query = xstrdup_printf("SELECT modify_resv(%s);", rec);
	} else {
		/* time_start is already done above and we
		 * changed something that is in need on a new
		 * entry. */
		query = xstrdup_printf("UPDATE %s SET endtime=%d "
				       "WHERE deleted=0 AND id=%u "
				       "AND start=%d AND cluster='%s';",
				       resv_table, resv->time_start-1,
				       resv->id, start,
				       resv->cluster);

		xstrfmtcat(query, "SELECT add_resv(%s);", rec);
	}
	rc = DEF_QUERY_RET_RC;

end_it:
	return rc;
}

/*
 * as_p_remove_reservation - remove reservation
 *
 * IN pg_conn: database connection
 * IN resv: reservation to remove
 * RET error code
 */
extern int
as_p_remove_reservation(pgsql_conn_t *pg_conn,
			acct_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;//, *tmp_extra = NULL;

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
	query = xstrdup_printf("DELETE FROM %s WHERE start > %d "
			       "AND id=%u AND start=%d "
			       "AND cluster='%s';",
			       resv_table, resv->time_start_prev,
			       resv->id,
			       resv->time_start, resv->cluster);
	/* then update the remaining ones with a deleted flag and end
	 * time of the time_start_prev which is set to when the
	 * command was issued */
	xstrfmtcat(query,
		   "UPDATE %s SET endtime=%d, deleted=1 WHERE deleted=0 AND "
		   "id=%u AND start=%d AND cluster='%s;'",
		   resv_table, resv->time_start_prev,
		   resv->id, resv->time_start,
		   resv->cluster);

	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * as_p_get_reservations - get reservations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN resv_cond: which reservations to get
 * RET: reservations got
 */
extern List
as_p_get_reservations(pgsql_conn_t *pg_conn, uid_t uid,
		      acct_reservation_cond_t *resv_cond)
{
	//DEF_TIMERS;
	char *query = NULL, *cond = NULL;
	List resv_list = NULL;
	int is_admin=0;
	PGresult *result = NULL;
	uint16_t private_data = 0;
	acct_job_cond_t job_cond;
	void *curr_cluster = NULL;
	List local_cluster_list = NULL;
	/* needed if we don't have an resv_cond */
	uint16_t with_usage = 0;

	/* if this changes you will need to edit the corresponding enum */
	char *gr_fields = "id, name, cluster, cpus, assoclist, nodelist, "
		"node_inx, start, endtime, flags";
	enum {
		GR_ID,
		GR_NAME,
		GR_CLUSTER,
		GR_CPUS,
		GR_ASSOCS,
		GR_NODES,
		GR_NODE_INX,
		GR_START,
		GR_END,
		GR_FLAGS,
		GR_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_RESERVATIONS) {
		is_admin = is_user_min_admin_level(
			pg_conn, uid, ACCT_ADMIN_OPERATOR);
		if (! is_admin) {
			error("as/pg: get_reservations: Only admins can look"
			      " at reservation usage");
			return NULL;
		}
	}

	if(!resv_cond) {
		goto empty;
	}

	with_usage = resv_cond->with_usage;

	memset(&job_cond, 0, sizeof(acct_job_cond_t));
	if(resv_cond->nodes) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
		job_cond.used_nodes = resv_cond->nodes;
		job_cond.cluster_list = resv_cond->cluster_list;
		local_cluster_list = setup_cluster_list_with_inx(
			pg_conn, &job_cond, (void **)&curr_cluster);
	} else if(with_usage) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
	}

	_make_resv_cond(resv_cond, &cond);

empty:
	//START_TIMER;
	query = xstrdup_printf("SELECT DISTINCT %s FROM %s WHERE deleted=0 %s "
			       "ORDER BY cluster, name;",
			       gr_fields, resv_table, cond ?: "");
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result) {
		error("as/pg: get_reservations: failed to get resv from db");
		if(local_cluster_list)
			list_destroy(local_cluster_list);
		return NULL;
	}

	resv_list = list_create(destroy_acct_reservation_rec);

	FOR_EACH_ROW {
		acct_reservation_rec_t *resv =
			xmalloc(sizeof(acct_reservation_rec_t));
		int start;

		list_append(resv_list, resv);

		start = atoi(ROW(GR_START));
		if(!good_nodes_from_inx(local_cluster_list, &curr_cluster,
					ROW(GR_NODE_INX), start))
			continue;

		resv->id = atoi(ROW(GR_ID));
		if(with_usage) {
			if(!job_cond.resvid_list)
				job_cond.resvid_list = list_create(NULL);
			list_append(job_cond.resvid_list, ROW(GR_ID));
		}
		resv->name = xstrdup(ROW(GR_NAME));
		resv->cluster = xstrdup(ROW(GR_CLUSTER));
		resv->cpus = atoi(ROW(GR_CPUS));
		resv->assocs = xstrdup(ROW(GR_ASSOCS));
		resv->nodes = xstrdup(ROW(GR_NODES));
		resv->time_start = start;
		resv->time_end = atoi(ROW(GR_END));
		resv->flags = atoi(ROW(GR_FLAGS));
	} END_EACH_ROW;

	if(local_cluster_list)
		list_destroy(local_cluster_list);

	if(with_usage && resv_list && list_count(resv_list)) {
		ListIterator itr = NULL, itr2 = NULL;
		jobacct_job_rec_t *job = NULL;
		acct_reservation_rec_t *resv = NULL;
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
	PQclear(result);

	//END_TIMER2("get_resvs");
	return resv_list;
}
