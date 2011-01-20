/*****************************************************************************\
 *  as_pg_rollup.c - accounting interface to pgsql - usage data rollup.
 *
 *  $Id: as_pg_rollup.c 13061 2008-01-22 21:23:56Z da $
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


pthread_mutex_t usage_rollup_lock = PTHREAD_MUTEX_INITIALIZER;
time_t global_last_rollup = 0;
//pthread_mutex_t rollup_lock = PTHREAD_MUTEX_INITIALIZER;


/*
 * data structures used in this file
 */
typedef struct {
	int id;
	uint64_t a_cpu;
} local_id_usage_t;

typedef struct {
	int id; /*only needed for reservations */
	uint64_t total_time;
	uint64_t a_cpu;
	int cpu_count;
	uint64_t d_cpu;
	uint64_t i_cpu;
	uint64_t o_cpu;
	uint64_t pd_cpu;
	uint64_t r_cpu;
	time_t start;
	time_t end;
} local_cluster_usage_t;

typedef struct {
	uint64_t a_cpu;
	int id;
	List local_assocs; /* list of assocs to spread unused time
			      over of type local_id_usage_t */
	uint64_t total_time;
	time_t start;
	time_t end;
} local_resv_usage_t;


static void _destroy_local_id_usage(void *object)
{
	local_id_usage_t *a_usage = (local_id_usage_t *)object;
	if(a_usage) {
		xfree(a_usage);
	}
}
static void _destroy_local_cluster_usage(void *object)
{
	local_cluster_usage_t *c_usage = (local_cluster_usage_t *)object;
	if(c_usage) {
		xfree(c_usage);
	}
}
static void _destroy_local_resv_usage(void *object)
{
	local_resv_usage_t *r_usage = (local_resv_usage_t *)object;
	if(r_usage) {
		if(r_usage->local_assocs)
			list_destroy(r_usage->local_assocs);
		xfree(r_usage);
	}
}
/*
 * ListFindF fucntions
 */

static int
_cmp_local_id(void *iu, void *id)
{
	return ((local_id_usage_t *)iu)->id == *((int *)id);
}

/* process cluster event usage data */
static int
_process_event_usage(pgsql_conn_t *pg_conn, char *cluster, time_t start,
		     time_t end, List cu_list)
{
	DEF_VARS;
	int seconds = 0;
	local_cluster_usage_t *c_usage = NULL;

	char *ge_fields = "node_name,cpu_count,time_start,time_end";
	enum {
		F_NAME,
		F_CPU,
		F_START,
		F_END,
		F_COUNT
	};

	/* events with maintainance flag is processed with the reservations */
	query = xstrdup_printf(
		"SELECT %s FROM %s.%s WHERE (state & %d)=0 AND "
		"  (time_start<%ld AND (time_end>=%ld OR time_end=0))"
		"  ORDER BY node_name, time_start", ge_fields, cluster,
              event_table, NODE_STATE_MAINT, end, start);
	result = DEF_QUERY_RET;
	if(!result) {
		error("failed to get events");
		return SLURM_ERROR;
	}

	FOR_EACH_ROW {
		int row_start = atoi(ROW(F_START));
		int row_end = atoi(ROW(F_END));
		int row_cpu = atoi(ROW(F_CPU));

		if(row_start < start)
			row_start = start;
		if(!row_end || row_end > end)
			row_end = end;
		/* Ignore time less than 1 second. */
		if((row_end - row_start) < 1)
			continue;

		/*
		 * node_name=='' means cluster registration entry,
		 * else, node down entry
		 */
		if(ISEMPTY(F_NAME)) {
			/* if the cpu count changes we will
			 * only care about the last cpu count but
			 * we will keep a total of the time for
			 * all cpus to get the correct cpu time
			 * for the entire period.
			 */
			if(!c_usage) {
				c_usage = xmalloc(
					sizeof(local_cluster_usage_t));
				list_append(cu_list, c_usage);
				c_usage->cpu_count = row_cpu;
				c_usage->total_time =
					(row_end - row_start) * row_cpu;
				c_usage->start = row_start;
				c_usage->end = row_end;
			} else {
				c_usage->cpu_count = row_cpu;
				c_usage->total_time +=
					(row_end - row_start) * row_cpu;
				c_usage->end = row_end;
			}
			continue;
		}

		/* only record down time for the cluster we
		   are looking for.  If it was during this
		   time period we would already have it.
		*/
		if(c_usage) {
			int local_start = row_start;
			int local_end = row_end;
			if(c_usage->start > local_start)
				local_start = c_usage->start;
			if(c_usage->end < local_end)
				local_end = c_usage->end;

			if((local_end - local_start) > 0) {
				seconds = (local_end - local_start);
				c_usage->d_cpu += seconds * row_cpu;
			}
		}
	} END_EACH_ROW;
	PQclear(result);
	return SLURM_SUCCESS;
}

/* process reservation usage data */
static int
_process_resv_usage(pgsql_conn_t *pg_conn, char *cluster, time_t start,
		    time_t end, List cu_list, List ru_list)
{
	DEF_VARS;
	local_cluster_usage_t *c_usage = NULL;
	local_resv_usage_t *r_usage = NULL;

	char *gr_fields = "id_resv,assoclist,cpus,flags,time_start,time_end";
	enum {
		F_ID,
		F_ASSOCS,
		F_CPU,
		F_FLAGS,
		F_START,
		F_END,
		F_COUNT
	};

	// now get the reservations during this time
	query = xstrdup_printf(
		"SELECT %s FROM %s.%s WHERE (time_start<%ld AND time_end >= %ld)"
		" ORDER BY time_start", gr_fields, cluster, resv_table, end,
		start);
	result = DEF_QUERY_RET;
	if(!result) {
		error("failed to get resv");
		return SLURM_ERROR;
	}

	/* If a reservation overlaps another reservation we
	   total up everything here as if they didn't but when
	   calculating the total time for a cluster we will
	   remove the extra time received.  This may result in
	   unexpected results with association based reports
	   since the association is given the total amount of
	   time of each reservation, thus equaling more time
	   that is available.  Job/Cluster/Reservation reports
	   should be fine though since we really don't over
	   allocate resources.
	*/
	FOR_EACH_ROW {
		int row_start = atoi(ROW(F_START));
		int row_end = atoi(ROW(F_END));
		int row_cpu = atoi(ROW(F_CPU));
		int row_flags = atoi(ROW(F_FLAGS));

		if(row_start < start)
			row_start = start;
		if(!row_end || row_end > end)
			row_end = end;
		/* ignore time less than 1 seconds */
		if((row_end - row_start) < 1)
			continue;

		r_usage = xmalloc(sizeof(local_resv_usage_t));
		r_usage->id = atoi(ROW(F_ID));
		r_usage->local_assocs = list_create(slurm_destroy_char);
		slurm_addto_char_list(r_usage->local_assocs,
				      ROW(F_ASSOCS));
		r_usage->total_time = (row_end - row_start) * row_cpu;
		r_usage->start = row_start;
		r_usage->end = row_end;
		list_append(ru_list, r_usage);

		/* Since this reservation was added to the
		   cluster and only certain people could run
		   there we will use this as allocated time on
		   the system.  If the reservation was a
		   maintenance then we add the time to planned
		   down time.
		*/
		c_usage = list_peek(cu_list); /* only one c_usage in cluster usage list in one hour */
		
		if (!c_usage)
			continue;
		if(row_flags & RESERVE_FLAG_MAINT)
			c_usage->pd_cpu += r_usage->total_time;
		else
			c_usage->a_cpu += r_usage->total_time;
	} END_EACH_ROW;
	PQclear(result);
	return SLURM_SUCCESS;
}

/* process job usage data */
static int
_process_job_usage(pgsql_conn_t *pg_conn, char *cluster, time_t start,
		   time_t end, List cu_list, List ru_list, List au_list,
		   List wu_list)
{
	DEF_VARS;
	PGresult *result2;
	ListIterator r_itr;
	int seconds = 0, last_id = -1, last_wckeyid = -1;
	local_cluster_usage_t *c_usage = NULL;
	local_resv_usage_t *r_usage = NULL;
	local_id_usage_t *a_usage = NULL, *w_usage = NULL;
	int track_wckey = slurm_get_track_wckey();

	char *gj_fields = "job_db_inx,id_job,id_assoc,id_wckey,time_eligible,"
		"time_start,time_end,time_suspended,cpus_alloc,cpus_req,"
		"id_resv";
	enum {
		F_DB_INX,
		F_JOBID,
		F_ASSOCID,
		F_WCKEYID,
		F_ELG,
		F_START,
		F_END,
		F_SUSPENDED,
		F_ACPU,
		F_RCPU,
		F_RESVID,
		F_COUNT
	};

	query = xstrdup_printf(
		"SELECT %s FROM %s.%s WHERE (time_eligible < %ld AND "
		"(time_end >= %ld OR time_end = 0)) ORDER BY id_assoc, "
		"time_eligible", gj_fields, cluster, job_table,
		(long)end, (long)start);
	result = DEF_QUERY_RET;
	if(!result) {
		error("failed to get jobs");
		return SLURM_ERROR;
	}

	r_itr = list_iterator_create(ru_list);
	FOR_EACH_ROW {
		int job_id = atoi(ROW(F_JOBID));
		int assoc_id = atoi(ROW(F_ASSOCID));
		int wckey_id = atoi(ROW(F_WCKEYID));
		int resv_id = atoi(ROW(F_RESVID));
		int row_eligible = atoi(ROW(F_ELG));
		int row_start = atoi(ROW(F_START));
		int row_end = atoi(ROW(F_END));
		int row_acpu = atoi(ROW(F_ACPU));
		int row_rcpu = atoi(ROW(F_RCPU));
		seconds = 0;

		if(row_start && (row_start < start))
			row_start = start;
		if(!row_start && row_end)
			row_start = row_end;
		if(!row_end || row_end > end)
			row_end = end;
		if(!row_start || ((row_end - row_start) < 1))
			goto calc_cluster;

		seconds = (row_end - row_start);

		if(strcmp(ROW(F_SUSPENDED), "0")) {
                        query = xstrdup_printf(
				"SELECT %s.get_job_suspend_time(%s, %ld, %ld);",
				cluster, ROW(F_DB_INX), start, end);
                        result2 = DEF_QUERY_RET;
			if(!result2) {
				list_iterator_destroy(r_itr);
                                return SLURM_ERROR;
			}
                        seconds -= atoi(PQgetvalue(result2, 0, 0));
                        PQclear(result2);
                }
                if(seconds < 1) {
			debug4("This job (%u) was suspended "
			       "the entire hour", job_id);
			/* TODO: how about resv usage? */
			continue;
		}

		if(last_id != assoc_id) { /* ORDER BY associd */
			a_usage = xmalloc(sizeof(local_id_usage_t));
			a_usage->id = assoc_id;
			list_append(au_list, a_usage);
			last_id = assoc_id;
		}
		a_usage->a_cpu += seconds * row_acpu;

		if(!track_wckey)
			goto calc_cluster;

		/* do the wckey calculation */
		if(last_wckeyid != wckey_id) {
			w_usage = list_find_first(wu_list, _cmp_local_id,
						  &wckey_id);
			if(!w_usage) {
				w_usage = xmalloc(sizeof(local_id_usage_t));
				w_usage->id = wckey_id;
				list_append(wu_list, w_usage);
			}
			last_wckeyid = wckey_id;
		}
		w_usage->a_cpu += seconds * row_acpu;

		/* do the cluster allocated calculation */
	calc_cluster:

		/* first figure out the reservation */
		if(resv_id) {
			if(seconds <= 0)
				continue;
			/* Since we have already added the
			   entire reservation as used time on
			   the cluster we only need to
			   calculate the used time for the
			   reservation and then divy up the
			   unused time over the associations
			   able to run in the reservation.
			   Since the job was to run, or ran a
			   reservation we don't care about eligible time
			   since that could totally skew the
			   clusters reserved time
			   since the job may be able to run
			   outside of the reservation. */
			list_iterator_reset(r_itr);
			while((r_usage = list_next(r_itr))) {
				/* since the reservation could
				   have changed in some way,
				   thus making a new
				   reservation record in the
				   database, we have to make
				   sure all the reservations
				   are checked to see if such
				   a thing has happened */
				if((r_usage->id == resv_id)) {
					int temp_end = row_end;
					int temp_start = row_start;
					if(r_usage->start > temp_start)
						temp_start = r_usage->start;
					if(r_usage->end < temp_end)
						temp_end = r_usage->end;

					if((temp_end - temp_start) > 0) {
						r_usage->a_cpu += row_acpu *
							(temp_end - temp_start);
					}
				}
			}
			/* entire resv already added to cluster usage */
			continue;
		}

		c_usage = list_peek(cu_list);
		/* only record time for the clusters that have
		   registered.  This continue should rarely if
		   ever happen.
		*/
		if(!c_usage)
			continue;

		if(row_start && (seconds > 0)) {
/* 					info("%d assoc %d adds " */
/* 					     "(%d)(%d-%d) * %d = %d " */
/* 					     "to %d", */
/* 					     job_id, */
/* 					     a_usage->id, */
/* 					     seconds, */
/* 					     row_end, row_start, */
/* 					     row_acpu, */
/* 					     seconds * row_acpu, */
/* 					     row_acpu); */

			c_usage->a_cpu += seconds * row_acpu;
		}
		/* now reserved time */
		/*
		 * job requesting for rcpu processors has been delayed
		 * by (start_time - eligible_time) seconds
		 * large r_cpu means cluster overload or bad scheduling?
		 */
		if(!row_start || (row_start >= c_usage->start)) {
			row_end = row_start;
			row_start = row_eligible;
			if(c_usage->start > row_start)
				row_start = c_usage->start;
			if(c_usage->end < row_end)
				row_end = c_usage->end;

			if((row_end - row_start) > 0) {
				seconds = (row_end - row_start)
					* row_rcpu;

/* 					info("%d assoc %d reserved " */
/* 					     "(%d)(%d-%d) * %d = %d " */
/* 					     "to %d", */
/* 					     job_id, */
/* 					     assoc_id, */
/* 					     seconds, */
/* 					     row_end, row_start, */
/* 					     row_rcpu, */
/* 					     seconds * row_rcpu, */
/* 					     row_rcpu); */
				c_usage->r_cpu += seconds;
			}
		}
	} END_EACH_ROW;
	PQclear(result);
	list_iterator_destroy(r_itr);

	return SLURM_SUCCESS;
}

/* distribute unused reservation usage to associations that
   could have run jobs */
static int
_process_resv_idle_time(List resv_usage_list, List assoc_usage_list)
{
	ListIterator r_itr;
	local_resv_usage_t *r_usage;
	local_id_usage_t *a_usage = NULL;
	int seconds;
	int last_id = -1;

	r_itr = list_iterator_create(resv_usage_list);
	while((r_usage = list_next(r_itr))) {
		char *assoc = NULL;
		ListIterator tmp_itr = NULL;
		int64_t idle = r_usage->total_time - r_usage->a_cpu;

		if(idle <= 0)
			continue;

		/* now divide that time by the number of
		   associations in the reservation and add
		   them to each association */
		seconds = idle / list_count(r_usage->local_assocs);
/* 			info("resv %d got %d for seconds for %d assocs", */
/* 			     r_usage->id, seconds, */
/* 			     list_count(r_usage->local_assocs)); */
		tmp_itr = list_iterator_create(r_usage->local_assocs);
		while((assoc = list_next(tmp_itr))) {
			int associd = atoi(assoc);
			if(last_id != associd) {
				a_usage = list_find_first(assoc_usage_list,
							  _cmp_local_id,
							  &associd);
			}
			if(!a_usage) {
				a_usage = xmalloc(sizeof(local_id_usage_t));
				a_usage->id = associd;
				list_append(assoc_usage_list, a_usage);
				last_id = associd;
			}
			a_usage->a_cpu += seconds;
		}
		list_iterator_destroy(tmp_itr);
	}
	list_iterator_destroy(r_itr);
	return SLURM_SUCCESS;
}

/* sanity check before insert cluster usage record into database */
static void
_cluster_usage_sanity_check(char *cluster, local_cluster_usage_t *c_usage,
			    time_t curr_start, time_t curr_end)
{
	uint64_t total_used = 0;

	/* no more allocated cpus than possible. */
	if(c_usage->total_time < c_usage->a_cpu) {
		char *start_char = xstrdup(ctime(&curr_start));
		char *end_char = xstrdup(ctime(&curr_end));
		error("We have more allocated time than is "
		      "possible (%"PRIu64" > %"PRIu64") for "
		      "cluster %s(%d) from %s - %s",
		      c_usage->a_cpu, c_usage->total_time,
		      cluster, c_usage->cpu_count,
		      start_char, end_char);
		xfree(start_char);
		xfree(end_char);
		c_usage->a_cpu = c_usage->total_time;
	}

	total_used = c_usage->a_cpu +
		c_usage->d_cpu + c_usage->pd_cpu;

	/* Make sure the total time we care about
	   doesn't go over the limit */
	if(c_usage->total_time < (total_used)) {
		char *start_char = xstrdup(ctime(&curr_start));
		char *end_char = xstrdup(ctime(&curr_end));
		int64_t overtime;

		start_char[strlen(start_char)-1] = '\0';
		
		error("We have more time than is "
		      "possible (%"PRIu64"+%"PRIu64"+%"PRIu64")(%"PRIu64") "
		      "> %"PRIu64") for "
		      "cluster %s(%d) from %s - %s",
		      c_usage->a_cpu, c_usage->d_cpu,
		      c_usage->pd_cpu, total_used,
		      c_usage->total_time,
		      cluster, c_usage->cpu_count,
		      start_char, end_char);
		xfree(start_char);
		xfree(end_char);
		/* First figure out how much actual down time
		   we have and then how much
		   planned down time we have. */
		overtime = (int64_t)(c_usage->total_time -
				     (c_usage->a_cpu
				      + c_usage->d_cpu));
		if(overtime < 0)
			c_usage->d_cpu += overtime;

		overtime = (int64_t)(c_usage->total_time -
				     (c_usage->a_cpu
				      + c_usage->d_cpu
				      + c_usage->pd_cpu));
		if(overtime < 0)
			c_usage->pd_cpu += overtime;

		total_used = c_usage->a_cpu +
			c_usage->d_cpu + c_usage->pd_cpu;
		/* info("We now have (%"PRIu64"+%"PRIu64"+%"PRIu64")(%"PRIu64") " */
		/*       "?= %"PRIu64"", */
		/*       c_usage->a_cpu, c_usage->d_cpu, */
		/*       c_usage->pd_cpu, total_used, */
		/*       c_usage->total_time); */

	}

	c_usage->i_cpu = c_usage->total_time -
		total_used - c_usage->r_cpu;
	/* sanity check just to make sure we have a
	 * legitimate time after we calulated
	 * idle/reserved time put extra in the over
	 * commit field
	 */
/* 	info("%s got idle of %lld", c_usage->name,  */
/* 	     (int64_t)c_usage->i_cpu); */
	if((int64_t)c_usage->i_cpu < 0) {
/* 		info("got %d %d %d", c_usage->r_cpu, */
/* 		     c_usage->i_cpu, c_usage->o_cpu); */
		c_usage->r_cpu += (int64_t)c_usage->i_cpu;
		c_usage->o_cpu -= (int64_t)c_usage->i_cpu;
		c_usage->i_cpu = 0;
		if((int64_t)c_usage->r_cpu < 0)
			c_usage->r_cpu = 0;
	}
}

/*
 * pgsql_hourly_rollup - rollup usage data per hour
 */
static int
pgsql_hourly_rollup(pgsql_conn_t *pg_conn, char *cluster,
		    time_t start, time_t end)
{
	int rc = SLURM_SUCCESS, add_sec = 3600;
	time_t now = time(NULL), curr_start = start,
		curr_end = curr_start + add_sec;
	char *query = NULL, *usage_recs = NULL;
	ListIterator a_itr = NULL, c_itr = NULL, w_itr = NULL, r_itr = NULL;
	List assoc_usage_list = list_create(_destroy_local_id_usage);
	List cluster_usage_list = list_create(_destroy_local_cluster_usage);
	List wckey_usage_list = list_create(_destroy_local_id_usage);
	List resv_usage_list = list_create(_destroy_local_resv_usage);
	uint16_t track_wckey = slurm_get_track_wckey();

	a_itr = list_iterator_create(assoc_usage_list);
	c_itr = list_iterator_create(cluster_usage_list);
	w_itr = list_iterator_create(wckey_usage_list);
	r_itr = list_iterator_create(resv_usage_list);
	while(curr_start < end) {
		local_cluster_usage_t *c_usage = NULL;
		local_id_usage_t *a_usage = NULL;
		local_id_usage_t *w_usage = NULL;

		debug3("curr hour is now %ld-%ld", curr_start, curr_end);

		rc = _process_event_usage(pg_conn, cluster, curr_start,
					  curr_end, cluster_usage_list);
                if (rc != SLURM_SUCCESS)
                        goto end_it;

		rc = _process_resv_usage(pg_conn, cluster, curr_start,
					 curr_end, cluster_usage_list,
					 resv_usage_list);
                if (rc != SLURM_SUCCESS)
                        goto end_it;

		rc = _process_job_usage(pg_conn, cluster, curr_start, curr_end,
					cluster_usage_list, resv_usage_list,
					assoc_usage_list, wckey_usage_list);
		if (rc != SLURM_SUCCESS)
			goto end_it;

		/* now figure out how much more to add to the
		   associations that could had run in the reservation
		*/
		rc = _process_resv_idle_time(resv_usage_list, assoc_usage_list);
		if (rc != SLURM_SUCCESS)
			goto end_it;

		/* Now put the lists into the usage tables */
		list_iterator_reset(c_itr);
		while((c_usage = list_next(c_itr))) {
			_cluster_usage_sanity_check(cluster, c_usage,
						    curr_start,curr_end);
/* 			info("cluster %s(%d) down %d alloc %d " */
/* 			     "resv %d idle %d over %d " */
/* 			     "total= %d = %d from %s", */
/* 			     c_usage->name, */
/* 			     c_usage->cpu_count, c_usage->d_cpu, */
/* 			     c_usage->a_cpu, */
/* 			     c_usage->r_cpu, c_usage->i_cpu, c_usage->o_cpu, */
/* 			     c_usage->d_cpu + c_usage->a_cpu + */
/* 			     c_usage->r_cpu + c_usage->i_cpu, */
/* 			     c_usage->total_time, */
/* 			     ctime(&c_usage->start)); */
/* 			info("to %s", ctime(&c_usage->end)); */
			if (usage_recs)
				xstrcat(usage_recs, ", ");
			xstrfmtcat(usage_recs,
				   "CAST((%ld, %ld, 0, %ld, %d, "
				   "%"PRIu64", %"PRIu64", %"PRIu64", "
				   "%"PRIu64", %"PRIu64", %"PRIu64")"
				   " AS %s.%s)",
				   now, now, curr_start, c_usage->cpu_count,
				   c_usage->a_cpu, c_usage->d_cpu,
				   c_usage->pd_cpu, c_usage->i_cpu,
				   c_usage->o_cpu, c_usage->r_cpu, cluster,
				   cluster_hour_table);
                }
                if (usage_recs) {
                        query = xstrdup_printf (
				"SELECT %s.add_cluster_hour_usages(ARRAY[%s]);",
				cluster, usage_recs);
			xfree(usage_recs);
			rc = DEF_QUERY_RET_RC;
			if (rc != SLURM_SUCCESS) {
				error("couldn't add cluster hour rollup");
				goto end_it;
			}
		}

		list_iterator_reset(a_itr);
		while((a_usage = list_next(a_itr))) {
/* 			info("association (%d) %d alloc %d", */
/* 			     a_usage->id, last_id, */
/* 			     a_usage->a_cpu); */
			if(usage_recs)
				xstrcat(usage_recs, ", ");
			xstrfmtcat(usage_recs,
				   "CAST((%ld, %ld, 0, %d, %ld, "
				   "%"PRIu64") AS %s.%s)",
                                   now, now, a_usage->id, curr_start,
				   a_usage->a_cpu, cluster, assoc_hour_table);
		}
		if(usage_recs) {
			query = xstrdup_printf(
				"SELECT %s.add_assoc_hour_usages(ARRAY[%s]);",
				cluster, usage_recs);
			xfree(usage_recs);
			rc = DEF_QUERY_RET_RC;
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add assoc hour rollup");
				goto end_it;
			}
		}

		if(!track_wckey)
			goto end_loop;

		list_iterator_reset(w_itr);
		while((w_usage = list_next(w_itr))) {
/* 			info("association (%d) %d alloc %d", */
/* 			     w_usage->id, last_id, */
/* 			     w_usage->a_cpu); */
			if(usage_recs)
				xstrcat(usage_recs, ", ");
			xstrfmtcat(usage_recs,
				   "CAST((%ld, %ld, 0, %d, %ld, "
				   "%"PRIu64", 0, 0) AS %s.%s)",
				   now, now, w_usage->id, curr_start,
				   w_usage->a_cpu, cluster, wckey_hour_table);
		}
		if(usage_recs) {
			query = xstrdup_printf(
				"SELECT %s.add_wckey_hour_usages(ARRAY[%s]);",
				cluster, usage_recs);
			xfree(usage_recs);
			rc = DEF_QUERY_RET_RC;
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add wckey hour rollup");
				goto end_it;
			}
		}

	end_loop:
		list_flush(assoc_usage_list);
		list_flush(cluster_usage_list);
		list_flush(wckey_usage_list);
		list_flush(resv_usage_list);
		curr_start = curr_end;
		curr_end = curr_start + add_sec;
	}
end_it:
	list_iterator_destroy(a_itr);
	list_iterator_destroy(c_itr);
	list_iterator_destroy(w_itr);
	list_iterator_destroy(r_itr);

	list_destroy(assoc_usage_list);
	list_destroy(cluster_usage_list);
	list_destroy(wckey_usage_list);
	list_destroy(resv_usage_list);

/* 	info("stop start %s", ctime(&curr_start)); */
/* 	info("stop end %s", ctime(&curr_end)); */
	return rc;
}

/* get time of next day */
inline static int
_next_day(time_t *start, time_t *end)
{
	struct tm start_tm;

	if (!localtime_r(start, &start_tm)) {
		error("couldn't get localtime from month start %ld",
		      (long)start);
		return SLURM_ERROR;
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday ++;
	start_tm.tm_isdst = -1;
	*end = mktime(&start_tm);
	return SLURM_SUCCESS;
}

/*
 * pgsql_daily_rollup - rollup usage data per day
 */
static int
pgsql_daily_rollup(pgsql_conn_t *pg_conn, char *cluster, time_t start,
		   time_t end, uint16_t archive_data)
{
	/* can't just add 86400 since daylight savings starts and ends every
	 * once in a while
	 */
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	time_t now = time(NULL);
	time_t curr_start = start, curr_end;
	uint16_t track_wckey = slurm_get_track_wckey();

	if (_next_day(&curr_start, &curr_end) != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}
	while(curr_start < end) {
		debug3("curr day is now %ld-%ld", curr_start, curr_end);
		query = xstrdup_printf(
			"SELECT %s.assoc_daily_rollup(%ld, %ld, %ld);",
			cluster, now, curr_start, curr_end);
		xstrfmtcat(query,
			   "SELECT %s.cluster_daily_rollup(%ld, %ld, %ld);",
			   cluster, now, curr_start, curr_end);
		if (track_wckey) {
			xstrfmtcat(query,
				   "SELECT %s.wckey_daily_rollup(%ld, %ld, %ld);",
				   cluster, now, curr_start, curr_end);
                }
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add day rollup");
			return SLURM_ERROR;
		}

		curr_start = curr_end;
		if (_next_day(&curr_start, &curr_end) != SLURM_SUCCESS) {
			return SLURM_ERROR;
		}
	}

/* 	info("stop start %s", ctime(&curr_start)); */
/* 	info("stop end %s", ctime(&curr_end)); */

	return SLURM_SUCCESS;
}

/* get time of next month */
inline static int
_next_month(time_t *start, time_t *end)
{
	struct tm start_tm;

	if (!localtime_r(start, &start_tm)) {
		error("couldn't get localtime from month start %ld",
		      (long)start);
		return SLURM_ERROR;
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 1;
	start_tm.tm_mon ++;
	start_tm.tm_isdst = -1;
	*end = mktime(&start_tm);
	return SLURM_SUCCESS;
}

/*
 * pgsql_monthly_rollup - rollup usage data per month
 */
static int
pgsql_monthly_rollup(pgsql_conn_t *pg_conn, char *cluster, time_t start,
		     time_t end, uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	slurmdb_archive_cond_t arch_cond;
	slurmdb_job_cond_t job_cond;
	time_t now = time(NULL);
	time_t curr_start = start, curr_end;
	uint16_t track_wckey = slurm_get_track_wckey();

	if (_next_month(&curr_start, &curr_end) != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}
	while(curr_start < end) {
		debug3("curr month is now %ld-%ld", curr_start, curr_end);
		query = xstrdup_printf(
			"SELECT %s.assoc_monthly_rollup(%ld, %ld, %ld);",
			cluster, now, curr_start, curr_end);
		xstrfmtcat(query,
			   "SELECT %s.cluster_monthly_rollup(%ld, %ld, %ld);",
			   cluster, now, curr_start, curr_end);
		if (track_wckey) {
			xstrfmtcat(
				query,
				"SELECT %s.wckey_monthly_rollup(%ld, %ld, %ld);",
				cluster, now, curr_start, curr_end);
		}
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add month rollup");
			return SLURM_ERROR;
		}
		curr_start = curr_end;
		if (_next_month(&curr_start, &curr_end) != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	/* if we didn't ask for archive data return here and don't do
	   anything extra just rollup */

	if(!archive_data)
		return SLURM_SUCCESS;

	if(!slurmdbd_conf)
		return SLURM_SUCCESS;

	memset(&arch_cond, 0, sizeof(arch_cond));
	memset(&job_cond, 0, sizeof(job_cond));
	arch_cond.archive_dir = slurmdbd_conf->archive_dir;
	arch_cond.archive_script = slurmdbd_conf->archive_script;
	arch_cond.purge_event = slurmdbd_conf->purge_event;
	arch_cond.purge_job = slurmdbd_conf->purge_job;
	arch_cond.purge_step = slurmdbd_conf->purge_step;
	arch_cond.purge_suspend = slurmdbd_conf->purge_suspend;

	job_cond.cluster_list = list_create(NULL);
	list_append(job_cond.cluster_list, cluster);
	arch_cond.job_cond = &job_cond;

	rc = js_pg_archive(pg_conn, &arch_cond);

	list_destroy(job_cond.cluster_list);
	
	return rc;
}

/* rollup usage for one cluster */
static int
_cluster_rollup_usage(pgsql_conn_t *pg_conn,  char *cluster,
		      time_t sent_start, time_t sent_end,
		      uint16_t archive_data)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS;
	time_t last_hour = sent_start;
	time_t last_day = sent_start;
	time_t last_month = sent_start;
	time_t start_time = 0;
  	time_t end_time = 0;
	time_t my_time = sent_end;
	struct tm start_tm, end_tm;
	DEF_TIMERS;
	char *ru_fields = "hourly_rollup, daily_rollup, monthly_rollup";
	enum {
		F_HOUR,
		F_DAY,
		F_MONTH,
		F_COUNT
	};

	if(!sent_start) {
		query = xstrdup_printf("SELECT %s FROM %s.%s LIMIT 1",
				       ru_fields, cluster, last_ran_table);
		result = DEF_QUERY_RET;
		if(!result)
			return SLURM_ERROR;

		if(PQntuples(result)) {
			last_hour = atoi(PG_VAL(F_HOUR));
			last_day = atoi(PG_VAL(F_DAY));
			last_month = atoi(PG_VAL(F_MONTH));
			PQclear(result);
		} else {
			time_t now = time(NULL);
			PQclear(result);
			query = xstrdup_printf("SELECT %s.init_last_ran(%ld);",
					       cluster, now);
			result = DEF_QUERY_RET;
			if(!result)
				return SLURM_ERROR;
			last_hour = last_day = last_month =
				atoi(PG_VAL(0));
			PQclear(result);
			if (last_hour < 0) {
				debug("cluster %s not registered, "
				      "not doing rollup", cluster);
				return SLURM_SUCCESS;
			}
		}
	}

	if(!my_time)
		my_time = time(NULL);

	if(!localtime_r(&last_hour, &start_tm)) {
		error("Couldn't get localtime from hour start %ld", last_hour);
		return SLURM_ERROR;
	}
	if(!localtime_r(&my_time, &end_tm)) {
		error("Couldn't get localtime from hour end %ld", my_time);
		return SLURM_ERROR;
	}

	/* below and anywhere in a rollup plugin when dealing with
	 * epoch times we need to set the tm_isdst = -1 so we don't
	 * have to worry about the time changes.  Not setting it to -1
	 * will cause problems in the day and month with the date change.
	 */

	/* align to hour boundary */
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("hour start %s", ctime(&start_time)); */
/* 	info("hour end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	//slurm_mutex_lock(&rollup_lock);
	global_last_rollup = end_time;
	//slurm_mutex_unlock(&rollup_lock);

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = pgsql_hourly_rollup(pg_conn, cluster, start_time, end_time))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER3("hourly_rollup", 5000000);
		/* If we have a sent_end do not update the last_run_table */
		if(!sent_end)
			query = xstrdup_printf(
				"UPDATE %s.%s SET hourly_rollup=%ld",
				cluster, last_ran_table, end_time);
	} else {
		debug2("no need to run this hour %ld <= %ld",
		       end_time, start_time);
	}


	if(!localtime_r(&last_day, &start_tm)) {
		error("Couldn't get localtime from day %ld", last_day);
		return SLURM_ERROR;
	}
	/* align to day boundary */
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_tm.tm_hour = 0;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("day start %s", ctime(&start_time)); */
/* 	info("day end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = pgsql_daily_rollup(pg_conn, cluster, start_time,
					    end_time, archive_data))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER2("daily_rollup");
		if(query && !sent_end)
			xstrfmtcat(query, ", daily_rollup=%ld", (long)end_time);
		else if(!sent_end)
			query = xstrdup_printf(
				"UPDATE %s.%s SET daily_rollup=%ld",
				cluster, last_ran_table, (long)end_time);
	} else {
		debug2("no need to run this day %ld <= %ld",
		       (long)end_time, (long)start_time);
	}

	if(!localtime_r(&last_month, &start_tm)) {
		error("Couldn't get localtime from month %ld", last_month);
		return SLURM_ERROR;
	}

	/* align to month boundary */
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 1;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_time = mktime(&end_tm);

	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_hour = 0;
	end_tm.tm_mday = 1;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("month start %s", ctime(&start_time)); */
/* 	info("month end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = pgsql_monthly_rollup(pg_conn, cluster, start_time,
					      end_time, archive_data))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER2("monthly_rollup");

		if(query && !sent_end)
			xstrfmtcat(query, ", monthly_rollup=%ld",
				   (long)end_time);
		else if(!sent_end)
			query = xstrdup_printf(
				"UPDATE %s.%s SET monthly_rollup=%ld",
				cluster, last_ran_table, (long)end_time);
	} else {
		debug2("no need to run this month %ld <= %ld",
		       (long)end_time, (long)start_time);
	}

	if(query) {
		rc = DEF_QUERY_RET_RC;
	}
	return rc;
}



/*
 * as_pg_roll_usage - rollup usage information
 *
 * IN pg_conn: database connection
 * IN sent_start: start time
 * IN sent_end: end time
 * IN archive_data: whether to archive usage data
 * RET: error code
 */
extern int
as_pg_roll_usage(pgsql_conn_t *pg_conn,  time_t sent_start,
		 time_t sent_end, uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	slurm_mutex_lock(&usage_rollup_lock);
	FOR_EACH_CLUSTER(NULL) {
		rc |= _cluster_rollup_usage(pg_conn, cluster_name, sent_start,
					   sent_end, archive_data);
	} END_EACH_CLUSTER;
	slurm_mutex_unlock(&usage_rollup_lock);
	
	return rc;
}
