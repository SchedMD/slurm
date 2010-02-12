/*****************************************************************************\
 *  rollup.c - accounting interface to pgsql - usage data rollup.
 *
 *  $Id: rollup.c 13061 2008-01-22 21:23:56Z da $
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

/*
 * data structures used in this file
 */
typedef struct {
	int id;
	uint64_t a_cpu;
} local_id_usage_t;

typedef struct {
	char *name;
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
	char *cluster;
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
		xfree(c_usage->name);
		xfree(c_usage);
	}
}
static void _destroy_local_resv_usage(void *object)
{
	local_resv_usage_t *r_usage = (local_resv_usage_t *)object;
	if(r_usage) {
		xfree(r_usage->cluster);
		if(r_usage->local_assocs)
			list_destroy(r_usage->local_assocs);
		xfree(r_usage);
	}
}
/*
 * ListFindF fucntions
 */
static int
_cmp_cluster_name(void *cu, void *cluster_name)
{
	return ! strcmp(((local_cluster_usage_t *)cu)->name,
			*((char **)cluster_name));
}

static int
_cmp_local_id(void *iu, void *id)
{
	return ((local_id_usage_t *)iu)->id == *((int *)id);
}

/*
 * _process_event_usage - process cluster event usage data
 * IN pg_conn: database connection
 * IN start: start tiem
 * IN end: end time
 * IN/OUT cu_list: cluster usage records
 * RET: error code
 */
static int
_process_event_usage(pgsql_conn_t *pg_conn, time_t start,
		     time_t end, List cu_list)
{
	char *query = NULL;
	PGresult *result;
	int seconds = 0;
	local_cluster_usage_t *last_c_usage = NULL, *c_usage = NULL;

	char *ge_fields = "node_name,cluster,cpu_count,period_start,period_end";
	enum {
		GE_NAME,
		GE_CLUSTER,
		GE_CPU,
		GE_START,
		GE_END,
		GE_COUNT
	};

	/* events with maintainance flag is processed with the reservations */
	query = xstrdup_printf(
		"SELECT %s FROM %s WHERE (state & %d)=0 AND "
		"  (period_start<%d AND (period_end>=%d OR period_end=0))"
		"  ORDER BY node_name, period_start",
	       	ge_fields, event_table, NODE_STATE_MAINT, end, start);
	result = DEF_QUERY_RET;
	if(!result) {
		error("failed to get events");
		return SLURM_ERROR;
	}

	FOR_EACH_ROW {
		int row_start = atoi(ROW(GE_START));
		int row_end = atoi(ROW(GE_END));
		int row_cpu = atoi(ROW(GE_CPU));
		char *row_cluster = ROW(GE_CLUSTER);

		if(row_start < start)
			row_start = start;
		if(!row_end || row_end > end)
			row_end = end;
		/* Ignore time less than 1 second. */
		if((row_end - row_start) < 1)
			continue;

		if(last_c_usage && !strcmp(last_c_usage->name, row_cluster))
			c_usage = last_c_usage;
		else {
			c_usage = list_find_first(cu_list, _cmp_cluster_name,
						  &row_cluster);
			last_c_usage = c_usage;
		}

		/*
		 * node_name=='' means cluster registration entry,
		 * else, node down entry
		 */
		if(ISEMPTY(GE_NAME)) {
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
				c_usage->name = xstrdup(row_cluster);
				last_c_usage = c_usage;
			}
			c_usage->cpu_count = row_cpu;
			c_usage->total_time +=
				(row_end - row_start) * row_cpu;
			c_usage->start = row_start;
			c_usage->end = row_end;
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

/*
 * _process_resv_usage - process reservation usage data
 * IN pg_conn: database connection
 * IN start: start tiem
 * IN end: end time
 * IN/OUT cu_list: cluster usage records
 * IN/OUT ru_list: reservation usage records
 * RET: error code
 */
static int
_process_resv_usage(pgsql_conn_t *pg_conn, time_t start,
		    time_t end, List cu_list, List ru_list)
{
	char *query = NULL;
	PGresult *result;
	local_cluster_usage_t *last_c_usage = NULL, *c_usage = NULL;
	local_resv_usage_t *r_usage = NULL;

	char *gr_fields = "id,cluster,assoclist,cpus,flags,start,endtime";
	enum {
		GR_ID,
		GR_CLUSTER,
		GR_ASSOCS,
		GR_CPU,
		GR_FLAGS,
		GR_START,
		GR_END,
		GR_COUNT
	};

	// now get the reservations during this time
	query = xstrdup_printf("SELECT %s FROM %s WHERE "
			       "(start < %d AND endtime >= %d) "
			       "ORDER BY cluster, start",
			       gr_fields, resv_table, end, start);
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
		int row_start = atoi(ROW(GR_START));
		int row_end = atoi(ROW(GR_END));
		int row_cpu = atoi(ROW(GR_CPU));
		int row_flags = atoi(ROW(GR_FLAGS));

		if(row_start < start)
			row_start = start;
		if(!row_end || row_end > end)
			row_end = end;
		/* ignore time less than 1 seconds */
		if((row_end - row_start) < 1)
			continue;

		r_usage = xmalloc(sizeof(local_resv_usage_t));
		r_usage->id = atoi(ROW(GR_ID));
		r_usage->local_assocs = list_create(slurm_destroy_char);
		slurm_addto_char_list(r_usage->local_assocs,
				      ROW(GR_ASSOCS));
		r_usage->cluster = xstrdup(ROW(GR_CLUSTER));
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
		if(last_c_usage && !strcmp(last_c_usage->name,
					   r_usage->cluster))
			c_usage = last_c_usage;
		else {
			c_usage = list_find_first(cu_list, _cmp_cluster_name,
						  &r_usage->cluster);
			if (!c_usage) {
				error("Failed to find cluster usage record "
				      "for reservation");
				PQclear(result);
				return SLURM_ERROR;
			}
			last_c_usage = c_usage;
		}
		if(row_flags & RESERVE_FLAG_MAINT)
			c_usage->pd_cpu += r_usage->total_time;
		else
			c_usage->a_cpu += r_usage->total_time;
	} END_EACH_ROW;
	PQclear(result);
	return SLURM_SUCCESS;
}

/*
 * _process_job_usage - process job usage data
 * IN pg_conn: database connection
 * IN start: start tiem
 * IN end: end time
 * IN/OUT cu_list: cluster usage records
 * IN/OUT ru_list: reservation usage records
 * IN/OUT au_list: association usage records
 * IN/OUT wu_list: wckey usage records
 * RET: error code
 */
static int
_process_job_usage(pgsql_conn_t *pg_conn, time_t start, time_t end,
		   List cu_list, List ru_list, List au_list, List wu_list)
{
	char *query = NULL;
	PGresult *result, *result2;
	ListIterator r_itr;
	int seconds = 0, last_id = -1, last_wckeyid = -1;
	local_cluster_usage_t *last_c_usage = NULL, *c_usage = NULL;
	local_resv_usage_t *r_usage = NULL;
	local_id_usage_t *a_usage = NULL, *w_usage = NULL;
	int track_wckey = slurm_get_track_wckey();

	char *gj_fields = "id,jobid,associd,wckeyid,cluster,eligible,start,"
		"endtime,suspended,alloc_cpus,req_cpus,resvid";
	enum {
		GJ_DB_INX,
		GJ_JOBID,
		GJ_ASSOCID,
		GJ_WCKEYID,
		GJ_CLUSTER,
		GJ_ELG,
		GJ_START,
		GJ_END,
		GJ_SUSPENDED,
		GJ_ACPU,
		GJ_RCPU,
		GJ_RESVID,
		GJ_COUNT
	};

	query = xstrdup_printf(
		"SELECT %s FROM %s WHERE (eligible < %d AND "
		"  (endtime >= %d OR endtime = 0)) ORDER BY associd, eligible",
		gj_fields, job_table, end, start);
	result = DEF_QUERY_RET;
	if(!result) {
		error("failed to get jobs");
		return SLURM_ERROR;
	}

	r_itr = list_iterator_create(ru_list);

	FOR_EACH_ROW {
		int job_id = atoi(ROW(GJ_JOBID));
		int assoc_id = atoi(ROW(GJ_ASSOCID));
		int wckey_id = atoi(ROW(GJ_WCKEYID));
		int resv_id = atoi(ROW(GJ_RESVID));
		int row_eligible = atoi(ROW(GJ_ELG));
		int row_start = atoi(ROW(GJ_START));
		int row_end = atoi(ROW(GJ_END));
		int row_acpu = atoi(ROW(GJ_ACPU));
		int row_rcpu = atoi(ROW(GJ_RCPU));
		char *row_cluster = ROW(GJ_CLUSTER);
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

		if(! ISNULL(GJ_SUSPENDED)) {
			/* function created in jobacct.c */
			query = xstrdup_printf(
				"SELECT get_job_suspend_time(%s, %d, %d);",
				ROW(GJ_DB_INX), start, end);
			result2 = DEF_QUERY_RET;
			if(!result2)
				return SLURM_ERROR;
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
		if(!row_cluster || !row_cluster[0])
			continue;

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
				if((r_usage->id == resv_id)
				   && !strcmp(r_usage->cluster,
					      row_cluster)) {
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

		if(last_c_usage && !strcmp(last_c_usage->name,
					   row_cluster)) {
			c_usage = last_c_usage;
		} else {
			c_usage = list_find_first(cu_list, _cmp_cluster_name,
						  &row_cluster);
			last_c_usage = c_usage;
		}

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

	return SLURM_SUCCESS;
}

/*
 * _process_resv_idle_time - distribute unused reservation usage to
 *   associations that could have run jobs
 * IN resv_usage_list: resv usage records
 * IN/OUT assoc_usage_list: assoc usage records
 * RET: error code
 */
static int
_process_resv_idle_time(List resv_usage_list, List assoc_usage_list)
{
	ListIterator r_itr;
	local_resv_usage_t *r_usage;
	local_id_usage_t *a_usage;
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

/*
 * _cluster_usage_sanity_check - sanity check before insert
 *   cluster usage record into database
 *
 * IN/OUT c_usage: cluster usage record
 */
static void
_cluster_usage_sanity_check(local_cluster_usage_t *c_usage,
			    time_t curr_start, time_t curr_end)
{
	uint64_t total_used = 0;

	/* no more allocated cpus than possible. */
	if(c_usage->total_time < c_usage->a_cpu) {
		char *start_char = xstrdup(ctime(&curr_start));
		char *end_char = xstrdup(ctime(&curr_end));
		error("We have more allocated time than is "
		      "possible (%llu > %llu) for "
		      "cluster %s(%d) from %s - %s",
		      c_usage->a_cpu, c_usage->total_time,
		      c_usage->name, c_usage->cpu_count,
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
		      "possible (%llu+%llu+%llu)(%llu) "
		      "> %llu) for "
		      "cluster %s(%d) from %s - %s",
		      c_usage->a_cpu, c_usage->d_cpu,
		      c_usage->pd_cpu, total_used,
		      c_usage->total_time,
		      c_usage->name, c_usage->cpu_count,
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
		  /* info("We now have (%llu+%llu+%llu)(%llu) " */
		  /*       "?= %llu", */
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
 *
 * IN pg_conn: database connection
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
extern int
pgsql_hourly_rollup(pgsql_conn_t *pg_conn, time_t start, time_t end)
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

/* 	info("begin start %s", ctime(&curr_start)); */
/* 	info("begin end %s", ctime(&curr_end)); */
	a_itr = list_iterator_create(assoc_usage_list);
	c_itr = list_iterator_create(cluster_usage_list);
	w_itr = list_iterator_create(wckey_usage_list);
	r_itr = list_iterator_create(resv_usage_list);
	while(curr_start < end) {
		local_cluster_usage_t *c_usage = NULL;
		local_id_usage_t *a_usage = NULL;
		local_id_usage_t *w_usage = NULL;

		debug3("curr hour is now %d-%d", curr_start, curr_end);
/* 		info("start %s", ctime(&curr_start)); */
/* 		info("end %s", ctime(&curr_end)); */

		rc = _process_event_usage(pg_conn, curr_start, curr_end,
					  cluster_usage_list);
		if (rc != SLURM_SUCCESS)
			goto end_it;

		rc = _process_resv_usage(pg_conn, curr_start, curr_end,
					 cluster_usage_list, resv_usage_list);
		if (rc != SLURM_SUCCESS)
			goto end_it;

		rc = _process_job_usage(pg_conn, curr_start, curr_end,
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
			_cluster_usage_sanity_check(c_usage, curr_start,
						    curr_end);
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
				   "CAST((%d, %d, 0, '%s', %d, %d, "
				   "%llu, %llu, %llu, %llu, %llu, %llu) AS %s)",
				   now, now, c_usage->name, curr_start,
				   c_usage->cpu_count, c_usage->a_cpu,
				   c_usage->d_cpu, c_usage->pd_cpu,
				   c_usage->i_cpu, c_usage->o_cpu,
				   c_usage->r_cpu, cluster_hour_table);
		}
		if (usage_recs) {
			query = xstrdup_printf (
				"SELECT add_cluster_hour_usages(ARRAY[%s]);",
				usage_recs);
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
				   "CAST((%d, %d, 0, %d, %d, %llu) AS %s)",
				   now, now, a_usage->id, curr_start,
				   a_usage->a_cpu, assoc_hour_table);
		}
		if(usage_recs) {
			query = xstrdup_printf(
				"SELECT add_assoc_hour_usages(ARRAY[%s]);",
				usage_recs);
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
				   "CAST((%d, %d, 0, %d, %d, %llu, 0, 0) AS %s)",
				   now, now, w_usage->id, curr_start,
				   w_usage->a_cpu, wckey_hour_table);
		}
		if(usage_recs) {
			query = xstrdup_printf(
				"SELECT add_wckey_hour_usages(ARRAY[%s]);",
				usage_recs);
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


/*
 * pgsql_daily_rollup - rollup usage data per day
 *
 * IN pg_conn: database connection
 * IN start: start time
 * IN end: end time
 * IN archive_data: whether to archive account data
 * RET: error code
 */
extern int
pgsql_daily_rollup(pgsql_conn_t *pg_conn, time_t start, time_t end,
		   uint16_t archive_data)
{
	/* can't just add 86400 since daylight savings starts and ends every
	 * once in a while
	 */
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	struct tm start_tm;
	time_t curr_end;
	time_t now = time(NULL);
	time_t curr_start = start; /* already aligned to day boundary */
	uint16_t track_wckey = slurm_get_track_wckey();

	if(!localtime_r(&curr_start, &start_tm)) {
		error("Couldn't get localtime from day start %d", curr_start);
		return SLURM_ERROR;
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday++;
	start_tm.tm_isdst = -1;
	curr_end = mktime(&start_tm);

	while(curr_start < end) {
		debug3("curr day is now %d-%d", curr_start, curr_end);
/* 		info("start %s", ctime(&curr_start)); */
/* 		info("end %s", ctime(&curr_end)); */
		query = xstrdup_printf("SELECT assoc_daily_rollup(%d, %d, %d);",
				       now, curr_start, curr_end);
		xstrfmtcat(query, "SELECT cluster_daily_rollup(%d, %d, %d);",
			   now, curr_start, curr_end);
		if (track_wckey) {
			xstrfmtcat(query,
				   "SELECT wckey_daily_rollup(%d, %d, %d);",
				   now, curr_start, curr_end);
		}
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add day rollup");
			return SLURM_ERROR;
		}

		curr_start = curr_end;
		if(!localtime_r(&curr_start, &start_tm)) {
			error("Couldn't get localtime from day start %d",
			      curr_start);
			return SLURM_ERROR;
		}
		start_tm.tm_sec = 0;
		start_tm.tm_min = 0;
		start_tm.tm_hour = 0;
		start_tm.tm_mday++;
		start_tm.tm_isdst = -1;
		curr_end = mktime(&start_tm);
	}

/* 	info("stop start %s", ctime(&curr_start)); */
/* 	info("stop end %s", ctime(&curr_end)); */

	return SLURM_SUCCESS;
}


/*
 * pgsql_monthly_rollup - rollup usage data per month
 *
 * IN pg_conn: database connection
 * IN start: start time
 * IN end: end time
 * IN archive_data: whether to archive account data
 * RET: error code
 */
extern int
pgsql_monthly_rollup(pgsql_conn_t *pg_conn,
		     time_t start, time_t end, uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	acct_archive_cond_t arch_cond;
	struct tm start_tm;
	time_t curr_end;
	time_t now = time(NULL);
	time_t curr_start = start; /* already aligned to month boundary */
	uint16_t track_wckey = slurm_get_track_wckey();

	if(!localtime_r(&curr_start, &start_tm)) {
		error("Couldn't get localtime from month start %d", curr_start);
		return SLURM_ERROR;
	}

	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 1;
	start_tm.tm_mon ++;
	start_tm.tm_isdst = -1;
	curr_end = mktime(&start_tm);

	while(curr_start < end) {
		debug3("curr month is now %d-%d", curr_start, curr_end);
/* 		info("start %s", ctime(&curr_start)); */
/* 		info("end %s", ctime(&curr_end)); */
		/* PL/pgSQL functions created in usage.c */
		query = xstrdup_printf("SELECT assoc_monthly_rollup(%d, %d, %d);",
				       now, curr_start, curr_end);
		xstrfmtcat(query, "SELECT cluster_monthly_rollup(%d, %d, %d);",
			   now, curr_start, curr_end);
		if (track_wckey) {
			xstrfmtcat(query,
				   "SELECT wckey_monthly_rollup(%d, %d, %d);",
				   now, curr_start, curr_end);
		}
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add month rollup");
			return SLURM_ERROR;
		}
		curr_start = curr_end;
		if(!localtime_r(&curr_start, &start_tm)) {
			error("Couldn't get localtime from month start %d",
			      curr_start);
		}
		start_tm.tm_sec = 0;
		start_tm.tm_min = 0;
		start_tm.tm_hour = 0;
		start_tm.tm_mday = 1;
		start_tm.tm_mon++;
		start_tm.tm_isdst = -1;
		curr_end = mktime(&start_tm);
	}

	/* if we didn't ask for archive data return here and don't do
	   anything extra just rollup */

	if(!archive_data)
		return SLURM_SUCCESS;

	if(!slurmdbd_conf)
		return SLURM_SUCCESS;

	memset(&arch_cond, 0, sizeof(arch_cond));
	arch_cond.archive_dir = slurmdbd_conf->archive_dir;
	arch_cond.archive_events = slurmdbd_conf->archive_events;
	arch_cond.archive_jobs = slurmdbd_conf->archive_jobs;
	arch_cond.archive_script = slurmdbd_conf->archive_script;
	arch_cond.archive_steps = slurmdbd_conf->archive_steps;
	arch_cond.archive_suspend = slurmdbd_conf->archive_suspend;
	arch_cond.purge_event = slurmdbd_conf->purge_event;
	arch_cond.purge_job = slurmdbd_conf->purge_job;
	arch_cond.purge_step = slurmdbd_conf->purge_step;
	arch_cond.purge_suspend = slurmdbd_conf->purge_suspend;

	return js_p_archive(pg_conn, &arch_cond);
}
