/*****************************************************************************\
 *  mysql_resv.c - functions dealing with reservations.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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

#include "mysql_resv.h"
#include "mysql_jobacct_process.h"

static int _setup_resv_limits(acct_reservation_rec_t *resv,
			      char **cols, char **vals,
			      char **extra)
{
	/* strip off the action item from the flags */

	if(resv->assocs) {
		int start = 0;
		int len = strlen(resv->assocs)-1;

		/* strip off extra ,'s */
		if(resv->assocs[0] == ',')
			start = 1;
		if(resv->assocs[len] == ',')
			resv->assocs[len] = '\0';

		xstrcat(*cols, ", assoclist");
		xstrfmtcat(*vals, ", \"%s\"", resv->assocs+start);
		xstrfmtcat(*extra, ", assoclist=\"%s\"", resv->assocs+start);
	}

	if(resv->cpus != (uint32_t)NO_VAL) {
		xstrcat(*cols, ", cpus");
		xstrfmtcat(*vals, ", %u", resv->cpus);
		xstrfmtcat(*extra, ", cpus=%u", resv->cpus);
	}

	if(resv->flags != (uint16_t)NO_VAL) {
		xstrcat(*cols, ", flags");
		xstrfmtcat(*vals, ", %u", resv->flags);
		xstrfmtcat(*extra, ", flags=%u", resv->flags);
	}

	if(resv->name) {
		xstrcat(*cols, ", name");
		xstrfmtcat(*vals, ", \"%s\"", resv->name);
		xstrfmtcat(*extra, ", name=\"%s\"", resv->name);
	}

	if(resv->nodes) {
		xstrcat(*cols, ", nodelist");
		xstrfmtcat(*vals, ", \"%s\"", resv->nodes);
		xstrfmtcat(*extra, ", nodelist=\"%s\"", resv->nodes);
	}

	if(resv->node_inx) {
		xstrcat(*cols, ", node_inx");
		xstrfmtcat(*vals, ", \"%s\"", resv->node_inx);
		xstrfmtcat(*extra, ", node_inx=\"%s\"", resv->node_inx);
	}

	if(resv->time_end) {
		xstrcat(*cols, ", end");
		xstrfmtcat(*vals, ", %u", resv->time_end);
		xstrfmtcat(*extra, ", end=%u", resv->time_end);
	}

	if(resv->time_start) {
		xstrcat(*cols, ", start");
		xstrfmtcat(*vals, ", %u", resv->time_start);
		xstrfmtcat(*extra, ", start=%u", resv->time_start);
	}


	return SLURM_SUCCESS;
}
static int _setup_resv_cond_limits(acct_reservation_cond_t *resv_cond,
				   char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	char *prefix = "t1";
	time_t now = time(NULL);

	if(!resv_cond)
		return 0;

	if(resv_cond->cluster_list && list_count(resv_cond->cluster_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(resv_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.cluster=\"%s\"", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(resv_cond->id_list && list_count(resv_cond->id_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(resv_cond->id_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.id=%s", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(resv_cond->name_list && list_count(resv_cond->name_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(resv_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.name=\"%s\"", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(resv_cond->time_start) {
		if(!resv_cond->time_end)
			resv_cond->time_end = now;

		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		xstrfmtcat(*extra,
			   "(t1.start < %d "
			   "&& (t1.end >= %d || t1.end = 0)))",
			   resv_cond->time_end, resv_cond->time_start);
	} else if(resv_cond->time_end) {
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		xstrfmtcat(*extra,
			   "(t1.start < %d))", resv_cond->time_end);
	}


	return set;
}

extern int mysql_add_resv(mysql_conn_t *mysql_conn,
			  acct_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL;//, *tmp_extra = NULL;

	if(!resv) {
		error("No reservation was given to edit");
		return SLURM_ERROR;
	}

	if(!resv->id) {
		error("We need an id to edit a reservation.");
		return SLURM_ERROR;
	}
	if(!resv->time_start) {
		error("We need a start time to edit a reservation.");
		return SLURM_ERROR;
	}
	if(!resv->cluster || !resv->cluster[0]) {
		error("We need a cluster name to edit a reservation.");
		return SLURM_ERROR;
	}

	_setup_resv_limits(resv, &cols, &vals, &extra);

	xstrfmtcat(query,
		   "insert into %s (id, cluster%s) values (%u, '%s'%s) "
		   "on duplicate key update deleted=0%s;",
		   resv_table, cols, resv->id, resv->cluster,
		   vals, extra);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);

	if((rc = mysql_db_query(mysql_conn->db_conn, query)
	    == SLURM_SUCCESS))
		rc = mysql_clear_results(mysql_conn->db_conn);

	xfree(query);
	xfree(cols);
	xfree(vals);
	xfree(extra);

	return rc;
}

extern int mysql_modify_resv(mysql_conn_t *mysql_conn,
			     acct_reservation_rec_t *resv)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL;//, *tmp_extra = NULL;
	time_t start = 0, now = time(NULL);
	int i;
	int set = 0;
	char *resv_req_inx[] = {
		"assoclist",
		"start",
		"end",
		"cpus",
		"name",
		"nodelist",
		"node_inx",
		"flags"
	};
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
		error("No reservation was given to edit");
		return SLURM_ERROR;
	}

	if(!resv->id) {
		error("We need an id to edit a reservation.");
		return SLURM_ERROR;
	}
	if(!resv->time_start) {
		error("We need a start time to edit a reservation.");
		return SLURM_ERROR;
	}
	if(!resv->cluster || !resv->cluster[0]) {
		error("We need a cluster name to edit a reservation.");
		return SLURM_ERROR;
	}

	if(!resv->time_start_prev) {
		error("We need a time to check for last "
		      "start of reservation.");
		return SLURM_ERROR;
	}

	for(i=0; i<RESV_COUNT; i++) {
		if(i)
			xstrcat(cols, ", ");
		xstrcat(cols, resv_req_inx[i]);
	}

	/* check for both the last start and the start because most
	   likely the start time hasn't changed, but something else
	   may have since the last time we did an update to the
	   reservation. */
	query = xstrdup_printf("select %s from %s where id=%u "
			       "and (start=%d || start=%d) and cluster='%s' "
			       "and deleted=0 order by start desc "
			       "limit 1 FOR UPDATE;",
			       cols, resv_table, resv->id,
			       resv->time_start, resv->time_start_prev,
			       resv->cluster);
try_again:
	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		rc = SLURM_ERROR;
		goto end_it;
	}
	if(!(row = mysql_fetch_row(result))) {
		rc = SLURM_ERROR;
		mysql_free_result(result);
		error("There is no reservation by id %u, "
		      "start %d, and cluster '%s'", resv->id,
		      resv->time_start_prev, resv->cluster);
		if(!set && resv->time_end) {
			/* This should never really happen,
			   but just incase the controller and the
			   database get out of sync we check
			   to see if there is a reservation
			   not deleted that hasn't ended yet. */
			xfree(query);
			query = xstrdup_printf(
				"select %s from %s where id=%u "
				"and start <= %d and cluster='%s' "
				"and deleted=0 order by start desc "
				"limit 1;",
				cols, resv_table, resv->id,
				resv->time_end, resv->cluster);
			set = 1;
			goto try_again;
		}
		goto end_it;
	}

	start = atoi(row[RESV_START]);

	xfree(query);
	xfree(cols);

	set = 0;

	/* check differences here */

	if(!resv->name
	   && row[RESV_NAME] && row[RESV_NAME][0])
		// if this changes we just update the
		// record, no need to create a new one since
		// this doesn't really effect the
		// reservation accounting wise
		resv->name = xstrdup(row[RESV_NAME]);

	if(resv->assocs)
		set = 1;
	else if(row[RESV_ASSOCS] && row[RESV_ASSOCS][0])
		resv->assocs = xstrdup(row[RESV_ASSOCS]);

	if(resv->cpus != (uint32_t)NO_VAL)
		set = 1;
	else
		resv->cpus = atoi(row[RESV_CPU]);

	if(resv->flags != (uint16_t)NO_VAL)
		set = 1;
	else
		resv->flags = atoi(row[RESV_FLAGS]);

	if(resv->nodes)
		set = 1;
	else if(row[RESV_NODES] && row[RESV_NODES][0]) {
		resv->nodes = xstrdup(row[RESV_NODES]);
		resv->node_inx = xstrdup(row[RESV_NODE_INX]);
	}

	if(!resv->time_end)
		resv->time_end = atoi(row[RESV_END]);

	mysql_free_result(result);

	_setup_resv_limits(resv, &cols, &vals, &extra);
	/* use start below instead of resv->time_start_prev
	 * just incase we have a different one from being out
	 * of sync
	 */
	if((start > now) || !set) {
		/* we haven't started the reservation yet, or
		   we are changing the associations or end
		   time which we can just update it */
		query = xstrdup_printf("update %s set deleted=0%s "
				       "where deleted=0 and id=%u "
				       "and start=%d and cluster='%s';",
				       resv_table, extra, resv->id,
				       start,
				       resv->cluster);
	} else {
		/* time_start is already done above and we
		 * changed something that is in need on a new
		 * entry. */
		query = xstrdup_printf("update %s set end=%d "
				       "where deleted=0 && id=%u "
				       "&& start=%d and cluster='%s';",
				       resv_table, resv->time_start-1,
				       resv->id, start,
				       resv->cluster);
		xstrfmtcat(query,
			   "insert into %s (id, cluster%s) "
			   "values (%u, '%s'%s) "
			   "on duplicate key update deleted=0%s;",
			   resv_table, cols, resv->id, resv->cluster,
			   vals, extra);
	}

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);

	if((rc = mysql_db_query(mysql_conn->db_conn, query)
	    == SLURM_SUCCESS))
		rc = mysql_clear_results(mysql_conn->db_conn);

end_it:

	xfree(query);
	xfree(cols);
	xfree(vals);
	xfree(extra);

	return rc;
}

extern int mysql_remove_resv(mysql_conn_t *mysql_conn,
			    acct_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;//, *tmp_extra = NULL;

	if(!resv) {
		error("No reservation was given to edit");
		return SLURM_ERROR;
	}

	if(!resv->id || !resv->time_start || !resv->cluster) {
		error("We need an id, start time, and cluster "
		      "name to edit a reservation.");
		return SLURM_ERROR;
	}


	/* first delete the resv that hasn't happened yet. */
	query = xstrdup_printf("delete from %s where start > %d "
			       "and id=%u and start=%d "
			       "and cluster='%s';",
			       resv_table, resv->time_start_prev,
			       resv->id,
			       resv->time_start, resv->cluster);
	/* then update the remaining ones with a deleted flag and end
	 * time of the time_start_prev which is set to when the
	 * command was issued */
	xstrfmtcat(query,
		   "update %s set end=%d, deleted=1 where deleted=0 and "
		   "id=%u and start=%d and cluster='%s;'",
		   resv_table, resv->time_start_prev,
		   resv->id, resv->time_start,
		   resv->cluster);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);

	if((rc = mysql_db_query(mysql_conn->db_conn, query)
	    == SLURM_SUCCESS))
		rc = mysql_clear_results(mysql_conn->db_conn);

	xfree(query);

	return rc;
}

extern List mysql_get_resvs(mysql_conn_t *mysql_conn, uid_t uid,
			    acct_reservation_cond_t *resv_cond)
{
	//DEF_TIMERS;
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List resv_list = NULL;
	int set = 0;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint16_t private_data = 0;
	acct_job_cond_t job_cond;
	void *curr_cluster = NULL;
	List local_cluster_list = NULL;

	/* needed if we don't have an resv_cond */
	uint16_t with_usage = 0;

	/* if this changes you will need to edit the corresponding enum */
	char *resv_req_inx[] = {
		"id",
		"name",
		"cluster",
		"cpus",
		"assoclist",
		"nodelist",
		"node_inx",
		"start",
		"end",
		"flags",
	};

	enum {
		RESV_REQ_ID,
		RESV_REQ_NAME,
		RESV_REQ_CLUSTER,
		RESV_REQ_CPUS,
		RESV_REQ_ASSOCS,
		RESV_REQ_NODES,
		RESV_REQ_NODE_INX,
		RESV_REQ_START,
		RESV_REQ_END,
		RESV_REQ_FLAGS,
		RESV_REQ_COUNT
	};

	if(!resv_cond) {
		xstrcat(extra, " where deleted=0");
		goto empty;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_RESERVATIONS) {
		/* This only works when running though the slurmdbd.
		 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
		 * SLURMDBD!
		 */
		if(slurmdbd_conf) {
			is_admin = 0;
			/* we have to check the authentication here in the
			 * plugin since we don't know what accounts are being
			 * referenced until after the query.  Here we will
			 * set if they are an operator or greater and then
			 * check it below after the query.
			 */
			if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
			   || assoc_mgr_get_admin_level(mysql_conn, uid)
			   >= ACCT_ADMIN_OPERATOR)
				is_admin = 1;
			else {
				error("Only admins can look at "
				      "reservation usage");
				return NULL;
			}
		}
	}

	memset(&job_cond, 0, sizeof(acct_job_cond_t));
	if(resv_cond->nodes) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
		job_cond.used_nodes = resv_cond->nodes;
		job_cond.cluster_list = resv_cond->cluster_list;
		local_cluster_list = setup_cluster_list_with_inx(
			mysql_conn, &job_cond, (void **)&curr_cluster);
	} else if(with_usage) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
	}

	set = _setup_resv_cond_limits(resv_cond, &extra);

	with_usage = resv_cond->with_usage;

empty:
	xfree(tmp);
	xstrfmtcat(tmp, "t1.%s", resv_req_inx[i]);
	for(i=1; i<RESV_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", t1.%s", resv_req_inx[i]);
	}

	//START_TIMER;
	query = xstrdup_printf("select distinct %s from %s as t1%s "
			       "order by cluster, name;",
			       tmp, resv_table, extra);
	xfree(tmp);
	xfree(extra);
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		if(local_cluster_list)
			list_destroy(local_cluster_list);
		return NULL;
	}
	xfree(query);

	resv_list = list_create(destroy_acct_reservation_rec);

	while((row = mysql_fetch_row(result))) {
		acct_reservation_rec_t *resv =
			xmalloc(sizeof(acct_reservation_rec_t));
		int start = atoi(row[RESV_REQ_START]);
		list_append(resv_list, resv);

		if(!good_nodes_from_inx(local_cluster_list, &curr_cluster,
					row[RESV_REQ_NODE_INX], start))
			continue;

		resv->id = atoi(row[RESV_REQ_ID]);
		if(with_usage) {
			if(!job_cond.resvid_list)
				job_cond.resvid_list = list_create(NULL);
			list_append(job_cond.resvid_list, row[RESV_REQ_ID]);
		}
		resv->name = xstrdup(row[RESV_REQ_NAME]);
		resv->cluster = xstrdup(row[RESV_REQ_CLUSTER]);
		resv->cpus = atoi(row[RESV_REQ_CPUS]);
		resv->assocs = xstrdup(row[RESV_REQ_ASSOCS]);
		resv->nodes = xstrdup(row[RESV_REQ_NODES]);
		resv->time_start = start;
		resv->time_end = atoi(row[RESV_REQ_END]);
		resv->flags = atoi(row[RESV_REQ_FLAGS]);
	}

	if(local_cluster_list)
		list_destroy(local_cluster_list);

	if(with_usage && resv_list && list_count(resv_list)) {
		List job_list = mysql_jobacct_process_get_jobs(
			mysql_conn, uid, &job_cond);
		ListIterator itr = NULL, itr2 = NULL;
		jobacct_job_rec_t *job = NULL;
		acct_reservation_rec_t *resv = NULL;

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
	mysql_free_result(result);

	//END_TIMER2("get_resvs");
	return resv_list;
}
