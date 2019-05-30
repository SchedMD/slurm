/*****************************************************************************\
 *  as_mysql_resv.c - functions dealing with reservations.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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

#include "as_mysql_resv.h"
#include "as_mysql_jobacct_process.h"

static int _setup_resv_limits(slurmdb_reservation_rec_t *resv,
			      char **cols, char **vals,
			      char **extra)
{
	/* strip off the action item from the flags */

	if (resv->assocs) {
		int start = 0;
		int len = strlen(resv->assocs)-1;

		if (strchr(resv->assocs, '-')) {
			int i = 0, i2 = 0;
			char *assocs = xmalloc(len);
			/* We will remove the negative's here.  This
			   is here so if we only have negatives in the
			   reservation we don't want to keep track of
			   every other id so don't keep track of any
			   since everyone except a few can use it.
			   These id's are only used to divide up idle
			   time so it isn't that important.
			*/
			while (i < len) {
				if (resv->assocs[i] == ',' &&
				    resv->assocs[i+1] == '-') {
					i+=2;
					while (i < len) {
						i++;
						if (resv->assocs[i] == ',')
							break;
					}
					continue;
				}
				assocs[i2++] = resv->assocs[i++];
			}
			xfree(resv->assocs);
			len = i2-1;
			resv->assocs = assocs;
			assocs = NULL;
		}

		/* strip off extra ,'s */
		if (resv->assocs[0] == ',')
			start = 1;
		if (resv->assocs[len] == ',')
			resv->assocs[len] = '\0';

		xstrcat(*cols, ", assoclist");
		xstrfmtcat(*vals, ", '%s'", resv->assocs+start);
		xstrfmtcat(*extra, ", assoclist='%s'", resv->assocs+start);
	}

	if (resv->flags != NO_VAL) {
		xstrcat(*cols, ", flags");
		xstrfmtcat(*vals, ", %u", resv->flags);
		xstrfmtcat(*extra, ", flags=%u", resv->flags);
	}

	if (resv->name) {
		xstrcat(*cols, ", resv_name");
		xstrfmtcat(*vals, ", '%s'", resv->name);
		xstrfmtcat(*extra, ", resv_name='%s'", resv->name);
	}

	if (resv->nodes) {
		xstrcat(*cols, ", nodelist");
		xstrfmtcat(*vals, ", '%s'", resv->nodes);
		xstrfmtcat(*extra, ", nodelist='%s'", resv->nodes);
	}

	if (resv->node_inx) {
		xstrcat(*cols, ", node_inx");
		xstrfmtcat(*vals, ", '%s'", resv->node_inx);
		xstrfmtcat(*extra, ", node_inx='%s'", resv->node_inx);
	}

	if (resv->time_end) {
		xstrcat(*cols, ", time_end");
		xstrfmtcat(*vals, ", %ld", resv->time_end);
		xstrfmtcat(*extra, ", time_end=%ld", resv->time_end);
	}

	if (resv->time_start) {
		xstrcat(*cols, ", time_start");
		xstrfmtcat(*vals, ", %ld", resv->time_start);
		xstrfmtcat(*extra, ", time_start=%ld", resv->time_start);
	}

	if (resv->tres_str) {
		xstrcat(*cols, ", tres");
		xstrfmtcat(*vals, ", '%s'", resv->tres_str);
		xstrfmtcat(*extra, ", tres='%s'", resv->tres_str);
	}

	return SLURM_SUCCESS;
}
static int _setup_resv_cond_limits(slurmdb_reservation_cond_t *resv_cond,
				   char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	char *prefix = "t1";
	time_t now = time(NULL);

	if (!resv_cond)
		return 0;

	if (resv_cond->id_list && list_count(resv_cond->id_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(resv_cond->id_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.id_resv=%s", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (resv_cond->name_list && list_count(resv_cond->name_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(resv_cond->name_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.resv_name='%s'",
				   prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (resv_cond->time_start) {
		if (!resv_cond->time_end)
			resv_cond->time_end = now;

		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		xstrfmtcat(*extra,
			   "(t1.time_start < %ld "
			   "&& (t1.time_end >= %ld || t1.time_end = 0)))",
			   resv_cond->time_end, resv_cond->time_start);
	} else if (resv_cond->time_end) {
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		xstrfmtcat(*extra,
			   "(t1.time_start < %ld))", resv_cond->time_end);
	}


	return set;
}

extern int as_mysql_add_resv(mysql_conn_t *mysql_conn,
			     slurmdb_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL;

	if (!resv) {
		error("No reservation was given to add.");
		return SLURM_ERROR;
	}

	if (!resv->id) {
		error("We need an id to add a reservation.");
		return SLURM_ERROR;
	}
	if (!resv->time_start) {
		error("We need a start time to add a reservation.");
		return SLURM_ERROR;
	}
	if (!resv->cluster || !resv->cluster[0]) {
		error("We need a cluster name to add a reservation.");
		return SLURM_ERROR;
	}

	_setup_resv_limits(resv, &cols, &vals, &extra);

	xstrfmtcat(query,
		   "insert into \"%s_%s\" (id_resv%s) values (%u%s) "
		   "on duplicate key update deleted=0%s;",
		   resv->cluster, resv_table, cols, resv->id, vals, extra);

	if (debug_flags & DEBUG_FLAG_DB_RESV)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);

	rc = mysql_db_query(mysql_conn, query);

	xfree(query);
	xfree(cols);
	xfree(vals);
	xfree(extra);

	return rc;
}

extern int as_mysql_modify_resv(mysql_conn_t *mysql_conn,
				slurmdb_reservation_rec_t *resv)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL;
	time_t start = 0, now = time(NULL);
	int i;
	int set = 0;

	char *resv_req_inx[] = {
		"assoclist",
		"time_start",
		"time_end",
		"resv_name",
		"nodelist",
		"node_inx",
		"flags",
		"tres"
	};
	enum {
		RESV_ASSOCS,
		RESV_START,
		RESV_END,
		RESV_NAME,
		RESV_NODES,
		RESV_NODE_INX,
		RESV_FLAGS,
		RESV_TRES,
		RESV_COUNT
	};

	if (!resv) {
		error("No reservation was given to edit");
		return SLURM_ERROR;
	}

	if (!resv->id) {
		error("We need an id to edit a reservation.");
		return SLURM_ERROR;
	}
	if (!resv->time_start) {
		error("We need a start time to edit a reservation.");
		return SLURM_ERROR;
	}
	if (!resv->cluster || !resv->cluster[0]) {
		error("We need a cluster name to edit a reservation.");
		return SLURM_ERROR;
	}

	if (!resv->time_start_prev) {
		error("We need a time to check for last "
		      "start of reservation.");
		return SLURM_ERROR;
	}

	xstrfmtcat(cols, "%s", resv_req_inx[0]);
	for (i=1; i<RESV_COUNT; i++) {
		xstrfmtcat(cols, ", %s", resv_req_inx[i]);
	}

	/* Get the last record of this reservation */
	query = xstrdup_printf("select %s from \"%s_%s\" where id_resv=%u "
			       "and time_start >= %ld "
			       "and deleted=0 order by time_start desc "
			       "limit 1 FOR UPDATE;",
			       cols, resv->cluster, resv_table, resv->id,
			       resv->time_start_prev);
	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		rc = SLURM_ERROR;
		goto end_it;
	}
	if (!(row = mysql_fetch_row(result))) {
		mysql_free_result(result);
		error("%s: There is no reservation by id %u, time_start %ld, and cluster '%s', creating it",
		      __func__, resv->id, resv->time_start_prev, resv->cluster);
		/*
		 * Don't set the time_start to time_start_prev as we have no
		 * idea what the reservation looked like at that time.  Doing so
		 * will also mess up future updates.
		 */
		/* resv->time_start = resv->time_start_prev; */
		rc = as_mysql_add_resv(mysql_conn, resv);
		goto end_it;
	}

	start = slurm_atoul(row[RESV_START]);

	xfree(query);
	xfree(cols);

	/*
	 * Check to see if the start is after the time we are looking for to
	 * make sure no we are the latest update.  If we aren't throw this one
	 * away.
	 */
	if (start > resv->time_start) {
		error("There is newer record for reservation with id %u, drop modification request:",
		      resv->id);
		error("assocs:'%s', cluster:'%s', flags:%u, id:%u, name:'%s', nodes:'%s', nodes_inx:'%s', time_end:%ld, time_start:%ld, time_start_prev:%ld, tres_str:'%s', unused_wall:%f",
		      resv->assocs, resv->cluster, resv->flags, resv->id,
		      resv->name, resv->nodes, resv->node_inx, resv->time_end,
		      resv->time_start, resv->time_start_prev, resv->tres_str,
		      resv->unused_wall);
		mysql_free_result(result);
		rc = SLURM_SUCCESS;
		goto end_it;
	}


	/* check differences here */

	if (!resv->name
	    && row[RESV_NAME] && row[RESV_NAME][0])
		// if this changes we just update the
		// record, no need to create a new one since
		// this doesn't really effect the
		// reservation accounting wise
		resv->name = xstrdup(row[RESV_NAME]);

	if (resv->assocs)
		set = 1;
	else if (row[RESV_ASSOCS] && row[RESV_ASSOCS][0])
		resv->assocs = xstrdup(row[RESV_ASSOCS]);

	if (resv->flags != NO_VAL)
		set = 1;
	else
		resv->flags = slurm_atoul(row[RESV_FLAGS]);

	if (resv->nodes)
		set = 1;
	else if (row[RESV_NODES] && row[RESV_NODES][0]) {
		resv->nodes = xstrdup(row[RESV_NODES]);
		resv->node_inx = xstrdup(row[RESV_NODE_INX]);
	}

	if (!resv->time_end)
		resv->time_end = slurm_atoul(row[RESV_END]);

	if (resv->tres_str)
		set = 1;
	else if (row[RESV_TRES] && row[RESV_TRES][0])
		resv->tres_str = xstrdup(row[RESV_TRES]);

	mysql_free_result(result);

	_setup_resv_limits(resv, &cols, &vals, &extra);
	/* use start below instead of resv->time_start_prev
	 * just in case we have a different one from being out
	 * of sync
	 */
	if ((start > now) || !set) {
		/* we haven't started the reservation yet, or
		   we are changing the associations or end
		   time which we can just update it */
		query = xstrdup_printf("update \"%s_%s\" set deleted=0%s "
				       "where deleted=0 and id_resv=%u "
				       "and time_start=%ld;",
				       resv->cluster, resv_table,
				       extra, resv->id, start);
	} else {
		/* time_start is already done above and we
		 * changed something that is in need on a new
		 * entry. */
		query = xstrdup_printf("update \"%s_%s\" set time_end=%ld "
				       "where deleted=0 && id_resv=%u "
				       "and time_start=%ld;",
				       resv->cluster, resv_table,
				       resv->time_start,
				       resv->id, start);
		xstrfmtcat(query,
			   "insert into \"%s_%s\" (id_resv%s) "
			   "values (%u%s) "
			   "on duplicate key update deleted=0%s;",
			   resv->cluster, resv_table, cols, resv->id,
			   vals, extra);
	}

	if (debug_flags & DEBUG_FLAG_DB_RESV)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);

	rc = mysql_db_query(mysql_conn, query);

end_it:

	xfree(query);
	xfree(cols);
	xfree(vals);
	xfree(extra);

	return rc;
}

extern int as_mysql_remove_resv(mysql_conn_t *mysql_conn,
				slurmdb_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;

	if (!resv) {
		error("No reservation was given to remove");
		return SLURM_ERROR;
	}

	if (!resv->id) {
		error("An id is needed to remove a reservation.");
		return SLURM_ERROR;
	}

	if (!resv->time_start) {
		error("A start time is needed to remove a reservation.");
		return SLURM_ERROR;
	}

	if (!resv->cluster || !resv->cluster[0]) {
		error("A cluster name is needed to remove a reservation.");
		return SLURM_ERROR;
	}


	/* first delete the resv that hasn't happened yet. */
	query = xstrdup_printf("delete from \"%s_%s\" where time_start > %ld "
			       "and id_resv=%u and time_start=%ld;",
			       resv->cluster, resv_table, resv->time_start_prev,
			       resv->id,
			       resv->time_start);
	/* then update the remaining ones with a deleted flag and end
	 * time of the time_start_prev which is set to when the
	 * command was issued */
	xstrfmtcat(query,
		   "update \"%s_%s\" set time_end=%ld, "
		   "deleted=1 where deleted=0 and "
		   "id_resv=%u and time_start=%ld;",
		   resv->cluster, resv_table, resv->time_start_prev,
		   resv->id, resv->time_start);

	if (debug_flags & DEBUG_FLAG_DB_RESV)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);

	rc = mysql_db_query(mysql_conn, query);

	xfree(query);

	return rc;
}

extern List as_mysql_get_resvs(mysql_conn_t *mysql_conn, uid_t uid,
			       slurmdb_reservation_cond_t *resv_cond)
{
	//DEF_TIMERS;
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List resv_list = NULL;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint16_t private_data = 0;
	slurmdb_job_cond_t job_cond;
	void *curr_cluster = NULL;
	List local_cluster_list = NULL;
	List use_cluster_list = as_mysql_cluster_list;
	ListIterator itr = NULL;
	char *cluster_name = NULL;
	/* needed if we don't have an resv_cond */
	uint16_t with_usage = 0;

	/* if this changes you will need to edit the corresponding enum */
	char *resv_req_inx[] = {
		"id_resv",
		"assoclist",
		"flags",
		"nodelist",
		"node_inx",
		"resv_name",
		"time_start",
		"time_end",
		"tres",
		"unused_wall"
	};

	enum {
		RESV_REQ_ID,
		RESV_REQ_ASSOCS,
		RESV_REQ_FLAGS,
		RESV_REQ_NODES,
		RESV_REQ_NODE_INX,
		RESV_REQ_NAME,
		RESV_REQ_START,
		RESV_REQ_END,
		RESV_REQ_TRES,
		RESV_REQ_UNUSED,
		RESV_REQ_COUNT
	};

	if (!resv_cond) {
		xstrcat(extra, " where deleted=0");
		goto empty;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_RESERVATIONS) {
		if (!(is_admin = is_user_min_admin_level(
			      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
			error("Only admins can look at reservations");
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}
	}

	with_usage = resv_cond->with_usage;

	memset(&job_cond, 0, sizeof(slurmdb_job_cond_t));
	job_cond.db_flags = SLURMDB_JOB_FLAG_NOTSET;
	if (resv_cond->nodes) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
		job_cond.used_nodes = resv_cond->nodes;
		if (!resv_cond->cluster_list)
			resv_cond->cluster_list =
				list_create(slurm_destroy_char);
		/*
		 * If they didn't specify a cluster, give them the one they are
		 * calling from.
		 */
		if (!list_count(resv_cond->cluster_list))
			list_append(resv_cond->cluster_list,
				    xstrdup(mysql_conn->cluster_name));
		job_cond.cluster_list = resv_cond->cluster_list;
		local_cluster_list = setup_cluster_list_with_inx(
			mysql_conn, &job_cond, (void **)&curr_cluster);
	} else if (with_usage) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
	}

	(void) _setup_resv_cond_limits(resv_cond, &extra);

	if (resv_cond->cluster_list && list_count(resv_cond->cluster_list))
		use_cluster_list = resv_cond->cluster_list;
empty:
	xfree(tmp);
	xstrfmtcat(tmp, "t1.%s", resv_req_inx[i]);
	for(i=1; i<RESV_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", t1.%s", resv_req_inx[i]);
	}

	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_lock(&as_mysql_cluster_list_lock);

	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		if (query)
			xstrcat(query, " union ");
		//START_TIMER;
		xstrfmtcat(query, "select distinct %s,'%s' as cluster "
			   "from \"%s_%s\" as t1%s",
			   tmp, cluster_name, cluster_name, resv_table,
			   extra ? extra : "");
	}
	list_iterator_destroy(itr);
	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	if (query)
		xstrcat(query, " order by cluster, time_start, resv_name;");

	xfree(tmp);
	xfree(extra);
	if (debug_flags & DEBUG_FLAG_DB_RESV)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		FREE_NULL_LIST(local_cluster_list);
		return NULL;
	}
	xfree(query);

	resv_list = list_create(slurmdb_destroy_reservation_rec);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_reservation_rec_t *resv;
		int start = slurm_atoul(row[RESV_REQ_START]);

		if (!good_nodes_from_inx(local_cluster_list, &curr_cluster,
					 row[RESV_REQ_NODE_INX], start))
			continue;

		resv = xmalloc(sizeof(slurmdb_reservation_rec_t));
		list_append(resv_list, resv);
		resv->id = slurm_atoul(row[RESV_REQ_ID]);
		if (with_usage) {
			if (!job_cond.resvid_list)
				job_cond.resvid_list = list_create(NULL);
			list_append(job_cond.resvid_list, row[RESV_REQ_ID]);
		}
		resv->name = xstrdup(row[RESV_REQ_NAME]);
		resv->node_inx = xstrdup(row[RESV_REQ_NODE_INX]);
		resv->cluster = xstrdup(row[RESV_REQ_COUNT]);
		resv->assocs = xstrdup(row[RESV_REQ_ASSOCS]);
		resv->nodes = xstrdup(row[RESV_REQ_NODES]);
		resv->time_start = start;
		resv->time_end = slurm_atoul(row[RESV_REQ_END]);
		resv->flags = slurm_atoul(row[RESV_REQ_FLAGS]);
		resv->tres_str = xstrdup(row[RESV_REQ_TRES]);
		resv->unused_wall = atof(row[RESV_REQ_UNUSED]);
	}

	FREE_NULL_LIST(local_cluster_list);

	if (with_usage && resv_list && list_count(resv_list)) {
		List job_list = as_mysql_jobacct_process_get_jobs(
			mysql_conn, uid, &job_cond);
		ListIterator itr = NULL, itr2 = NULL;
		slurmdb_job_rec_t *job = NULL;
		slurmdb_reservation_rec_t *resv = NULL;

		if (!job_list || !list_count(job_list))
			goto no_jobs;

		itr = list_iterator_create(job_list);
		itr2 = list_iterator_create(resv_list);
		while ((job = list_next(itr))) {
			int set = 0;
			while ((resv = list_next(itr2))) {
				int start   = job->start;
				int end     = job->end;
				int elapsed = 0;
				/* since a reservation could have
				   changed while a job was running we
				   have to make sure we get the time
				   in the correct record.
				*/
				if (resv->id != job->resvid)
					continue;
				set = 1;

				if (start < resv->time_start)
					start = resv->time_start;
				if (!end || end > resv->time_end)
					end = resv->time_end;

				if ((elapsed = (end - start)) < 1)
					continue;

				slurmdb_transfer_tres_time(
					&resv->tres_list, job->tres_alloc_str,
					elapsed);
			}
			list_iterator_reset(itr2);
			if (!set) {
				error("we got a job %u with no reservation "
				      "associatied with it?", job->jobid);
			}
		}

		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);
	no_jobs:
		FREE_NULL_LIST(job_list);
	}

	FREE_NULL_LIST(job_cond.resvid_list);

	/* free result after we use the list with resv id's in it. */
	mysql_free_result(result);

	//END_TIMER2("get_resvs");
	return resv_list;
}
