/*****************************************************************************\
 *  mysql_rollup.c - functions for rolling up data for associations
 *                   and machines from the mysql storage.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "mysql_rollup.h"

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

extern int mysql_hourly_rollup(mysql_conn_t *mysql_conn,
			       time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	int add_sec = 3600;
	int i=0;
	time_t now = time(NULL);
	time_t curr_start = start;
	time_t curr_end = curr_start + add_sec;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	ListIterator a_itr = NULL;
	ListIterator c_itr = NULL;
	ListIterator w_itr = NULL;
	ListIterator r_itr = NULL;
	List assoc_usage_list = list_create(_destroy_local_id_usage);
	List cluster_usage_list = list_create(_destroy_local_cluster_usage);
	List wckey_usage_list = list_create(_destroy_local_id_usage);
	List resv_usage_list = list_create(_destroy_local_resv_usage);
	uint16_t track_wckey = slurm_get_track_wckey();

	char *event_req_inx[] = {
		"node_name",
		"cluster",
		"cpu_count",
		"period_start",
		"period_end",
	};
	char *event_str = NULL;
	enum {
		EVENT_REQ_NAME,
		EVENT_REQ_CLUSTER,
		EVENT_REQ_CPU,
		EVENT_REQ_START,
		EVENT_REQ_END,
		EVENT_REQ_COUNT
	};

	char *job_req_inx[] = {
		"id",
		"jobid",
		"associd",
		"wckeyid",
		"cluster",
		"eligible",
		"start",
		"end",
		"suspended",
		"alloc_cpus",
		"req_cpus",
		"resvid"
	   
	};
	char *job_str = NULL;
	enum {
		JOB_REQ_DB_INX,
		JOB_REQ_JOBID,
		JOB_REQ_ASSOCID,
		JOB_REQ_WCKEYID,
		JOB_REQ_CLUSTER,
		JOB_REQ_ELG,
		JOB_REQ_START,
		JOB_REQ_END,
		JOB_REQ_SUSPENDED,
		JOB_REQ_ACPU,
		JOB_REQ_RCPU,
		JOB_REQ_RESVID,
		JOB_REQ_COUNT
	};

	char *suspend_req_inx[] = {
		"start",
		"end"
	};
	char *suspend_str = NULL;
	enum {
		SUSPEND_REQ_START,
		SUSPEND_REQ_END,
		SUSPEND_REQ_COUNT
	};

	char *resv_req_inx[] = {
		"id",
		"cluster",
		"assoclist",
		"cpus",
		"flags",
		"start",
		"end"
	};
	char *resv_str = NULL;
	enum {
		RESV_REQ_ID,
		RESV_REQ_CLUSTER,
		RESV_REQ_ASSOCS,
		RESV_REQ_CPU,
		RESV_REQ_FLAGS,
		RESV_REQ_START,
		RESV_REQ_END,
		RESV_REQ_COUNT
	};

	i=0;
	xstrfmtcat(event_str, "%s", event_req_inx[i]);
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(event_str, ", %s", event_req_inx[i]);
	}

	i=0;
	xstrfmtcat(job_str, "%s", job_req_inx[i]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(job_str, ", %s", job_req_inx[i]);
	}

	i=0;
	xstrfmtcat(suspend_str, "%s", suspend_req_inx[i]);
	for(i=1; i<SUSPEND_REQ_COUNT; i++) {
		xstrfmtcat(suspend_str, ", %s", suspend_req_inx[i]);
	}

	i=0;
	xstrfmtcat(resv_str, "%s", resv_req_inx[i]);
	for(i=1; i<RESV_REQ_COUNT; i++) {
		xstrfmtcat(resv_str, ", %s", resv_req_inx[i]);
	}

/* 	info("begin start %s", ctime(&curr_start)); */
/* 	info("begin end %s", ctime(&curr_end)); */
	a_itr = list_iterator_create(assoc_usage_list);
	c_itr = list_iterator_create(cluster_usage_list);
	w_itr = list_iterator_create(wckey_usage_list);
	r_itr = list_iterator_create(resv_usage_list);
	while(curr_start < end) {
		local_cluster_usage_t *last_c_usage = NULL;
		int last_id = -1;
		int last_wckeyid = -1;
		int seconds = 0;
		local_cluster_usage_t *c_usage = NULL;
		local_resv_usage_t *r_usage = NULL;
		local_id_usage_t *a_usage = NULL;
		local_id_usage_t *w_usage = NULL;

		debug3("curr hour is now %d-%d", curr_start, curr_end);
/* 		info("start %s", ctime(&curr_start)); */
/* 		info("end %s", ctime(&curr_end)); */
		
		/* first get the events during this time.  All that is
		 * except things with the maintainance flag set in the
		 * state.  We handle those later with the reservations.
		 */
		query = xstrdup_printf("select %s from %s where "
				       "!(state & %d) && (period_start < %d "
				       "&& (period_end >= %d "
				       "|| period_end = 0)) "
				       "order by node_name, period_start",
				       event_str, event_table,
				       NODE_STATE_MAINT,
				       curr_end, curr_start);

		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		
		while((row = mysql_fetch_row(result))) {
			int row_start = atoi(row[EVENT_REQ_START]);
			int row_end = atoi(row[EVENT_REQ_END]);
			int row_cpu = atoi(row[EVENT_REQ_CPU]);
		
			if(row_start < curr_start)
				row_start = curr_start;
		
			if(!row_end || row_end > curr_end) 
				row_end = curr_end;

			/* Don't worry about it if the time is less
			 * than 1 second.
			 */
			if((row_end - row_start) < 1)
				continue;

			if(last_c_usage && !strcmp(last_c_usage->name,
						   row[EVENT_REQ_CLUSTER])) {
				c_usage = last_c_usage;
			} else {
				list_iterator_reset(c_itr);
				while((c_usage = list_next(c_itr))) {
					if(!strcmp(c_usage->name,
						   row[EVENT_REQ_CLUSTER])) {
						last_c_usage = c_usage;
						break;
					}
				}				
			}

			/* this means we are a cluster registration
			   entry */
			if(!row[EVENT_REQ_NAME][0]) {
				/* if the cpu count changes we will
				 * only care about the last cpu count but
				 * we will keep a total of the time for
				 * all cpus to get the correct cpu time
				 * for the entire period.
				 */
				if(!c_usage) {
					c_usage = xmalloc(
						sizeof(local_cluster_usage_t));
					c_usage->name = 
						xstrdup(row[EVENT_REQ_CLUSTER]);
					c_usage->cpu_count = row_cpu;
					c_usage->total_time =
						(row_end - row_start) * row_cpu;
					c_usage->start = row_start;
					c_usage->end = row_end;
					list_append(cluster_usage_list, 
						    c_usage);
					last_c_usage = c_usage;
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
					
/* 					info("node %s adds " */
/* 					     "(%d)(%d-%d) * %d = %d " */
/* 					     "to %d", */
/* 					     row[EVENT_REQ_NAME], */
/* 					     seconds, */
/* 					     local_end, local_start, */
/* 					     row_cpu,  */
/* 					     seconds * row_cpu,  */
/* 					     row_cpu); */
					c_usage->d_cpu += seconds * row_cpu;
				}
			}
		}
		mysql_free_result(result);

		// now get the reservations during this time
		query = xstrdup_printf("select %s from %s where "
				       "(start < %d && end >= %d) "
				       "order by cluster, start",
				       resv_str, resv_table,
				       curr_end, curr_start);

		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		
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
		while((row = mysql_fetch_row(result))) {
			int row_start = atoi(row[RESV_REQ_START]);
			int row_end = atoi(row[RESV_REQ_END]);
			int row_cpu = atoi(row[RESV_REQ_CPU]);
			int row_flags = atoi(row[RESV_REQ_FLAGS]);

			if(row_start < curr_start)
				row_start = curr_start;
		
			if(!row_end || row_end > curr_end) 
				row_end = curr_end;

			/* Don't worry about it if the time is less
			 * than 1 second.
			 */
			if((row_end - row_start) < 1)
				continue;

			r_usage = xmalloc(sizeof(local_resv_usage_t));
			r_usage->id = atoi(row[RESV_REQ_ID]);

			r_usage->local_assocs = list_create(slurm_destroy_char);
			slurm_addto_char_list(r_usage->local_assocs, 
					      row[RESV_REQ_ASSOCS]);

			r_usage->cluster = xstrdup(row[RESV_REQ_CLUSTER]);
			r_usage->total_time = (row_end - row_start) * row_cpu;
			r_usage->start = row_start;
			r_usage->end = row_end;
			list_append(resv_usage_list, r_usage);

			/* Since this reservation was added to the
			   cluster and only certain people could run
			   there we will use this as allocated time on
			   the system.  If the reservation was a
			   maintenance then we add the time to planned
			   down time. 
			*/
			if(last_c_usage && !strcmp(last_c_usage->name,
						   r_usage->cluster)) {
				c_usage = last_c_usage;
			} else {
				list_iterator_reset(c_itr);
				while((c_usage = list_next(c_itr))) {
					if(!strcmp(c_usage->name,
						   r_usage->cluster)) {
						last_c_usage = c_usage;
						break;
					}
				}				
			}

			/* only record time for the clusters that have
			   registered.  This continue should rarely if
			   ever happen.
			*/
			if(!c_usage) 
				continue;
			else if(row_flags & RESERVE_FLAG_MAINT)
				c_usage->pd_cpu += r_usage->total_time;
			else
				c_usage->a_cpu += r_usage->total_time;
/* 			info("adding this much %lld to cluster %s", */
/* 			     r_usage->total_time, c_usage->name); */

		}
		mysql_free_result(result);

		/* now get the jobs during this time only  */
		query = xstrdup_printf("select %s from %s where "
				       "(eligible < %d && (end >= %d "
				       "|| end = 0)) "
				       "order by associd, eligible",
				       job_str, job_table, 
				       curr_end, curr_start);

		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		
		while((row = mysql_fetch_row(result))) {
			int job_id = atoi(row[JOB_REQ_JOBID]);
			int assoc_id = atoi(row[JOB_REQ_ASSOCID]);
			int wckey_id = atoi(row[JOB_REQ_WCKEYID]);
			int resv_id = atoi(row[JOB_REQ_RESVID]);
			int row_eligible = atoi(row[JOB_REQ_ELG]);
			int row_start = atoi(row[JOB_REQ_START]);
			int row_end = atoi(row[JOB_REQ_END]);
			int row_acpu = atoi(row[JOB_REQ_ACPU]);
			int row_rcpu = atoi(row[JOB_REQ_RCPU]);
			seconds = 0;
		       
			if(row_start && (row_start < curr_start))
				row_start = curr_start;

			if(!row_start && row_end)
				row_start = row_end;

			if(!row_end || row_end > curr_end) 
				row_end = curr_end;

			if(!row_start || ((row_end - row_start) < 1)) 
				goto calc_cluster;
			
			seconds = (row_end - row_start);

			if(row[JOB_REQ_SUSPENDED]) {
				MYSQL_RES *result2 = NULL;
				MYSQL_ROW row2;
				/* get the suspended time for this job */
				query = xstrdup_printf(
					"select %s from %s where "
					"(start < %d && (end >= %d "
					"|| end = 0)) && id=%s "
					"order by start",
					suspend_str, suspend_table,
					curr_end, curr_start,
					row[JOB_REQ_DB_INX]);
				
				debug4("%d(%d) query\n%s",
				       mysql_conn->conn, __LINE__, query);
				if(!(result2 = mysql_db_query_ret(
					     mysql_conn->db_conn,
					     query, 0))) {
					xfree(query);
					return SLURM_ERROR;
				}
				xfree(query);
				while((row2 = mysql_fetch_row(result2))) {
					int local_start =
						atoi(row2[SUSPEND_REQ_START]);
					int local_end = 
						atoi(row2[SUSPEND_REQ_END]);

					if(!local_start)
						continue;

					if(row_start > local_start)
						local_start = row_start;
					if(row_end < local_end)
						local_end = row_end;

					if((local_end - local_start) < 1)
						continue;

					seconds -= (local_end - local_start);
				}
				mysql_free_result(result2);
			}
			if(seconds < 1) {
				debug4("This job (%u) was suspended "
				       "the entire hour", job_id);
				continue;
			} 

			if(last_id != assoc_id) {
				a_usage = xmalloc(sizeof(local_id_usage_t));
				a_usage->id = assoc_id;
				list_append(assoc_usage_list, a_usage);
				last_id = assoc_id;
			} 
			
			a_usage->a_cpu += seconds * row_acpu;

			if(!track_wckey) 
				goto calc_cluster;

			/* do the wckey calculation */
			if(last_wckeyid != wckey_id) {
				list_iterator_reset(w_itr);
				while((w_usage = list_next(w_itr))) 
					if(w_usage->id == wckey_id) 
						break;
				
				if(!w_usage) {
					w_usage = xmalloc(
						sizeof(local_id_usage_t));
					w_usage->id = wckey_id;
					list_append(wckey_usage_list,
						    w_usage);
				}
				
				last_wckeyid = wckey_id;
			}
			w_usage->a_cpu += seconds * row_acpu;
			/* do the cluster allocated calculation */
		calc_cluster:
			if(!row[JOB_REQ_CLUSTER] || !row[JOB_REQ_CLUSTER][0]) 
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
						      row[JOB_REQ_CLUSTER])) {
						int temp_end = row_end;
						int temp_start = row_start;
						if(r_usage->start > temp_start)
							temp_start =
								r_usage->start;
						if(r_usage->end < temp_end)
							temp_end = r_usage->end;
						
						if((temp_end - temp_start)
						   > 0) {
							r_usage->a_cpu +=
								(temp_end
								 - temp_start)
								* row_acpu;
						}
					}
				}
				continue;
			}

			if(last_c_usage && !strcmp(last_c_usage->name,
						   row[JOB_REQ_CLUSTER])) {
				c_usage = last_c_usage;
			} else {
				list_iterator_reset(c_itr);
				while((c_usage = list_next(c_itr))) {
					if(!strcmp(c_usage->name,
						   row[JOB_REQ_CLUSTER])) {
						last_c_usage = c_usage;
						break;
					}
				}				
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
		}
		mysql_free_result(result);

		/* now figure out how much more to add to the
		   associations that could had run in the reservation
		*/
		list_iterator_reset(r_itr);
		while((r_usage = list_next(r_itr))) {
			int64_t idle = r_usage->total_time - r_usage->a_cpu;
			char *assoc = NULL;
			ListIterator tmp_itr = NULL;

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
					list_iterator_reset(a_itr);
					while((a_usage = list_next(a_itr))) {
						if(!a_usage->id == associd) {
							last_id = a_usage->id;
							break;
						}
					}
				}

				if(!a_usage) {
					a_usage = xmalloc(
						sizeof(local_id_usage_t));
					a_usage->id = associd;
					list_append(assoc_usage_list, a_usage);
					last_id = associd;
				} 
				
				a_usage->a_cpu += seconds;
			}
			list_iterator_destroy(tmp_itr);
		}

		/* Now put the lists into the usage tables */
		list_iterator_reset(c_itr);
		while((c_usage = list_next(c_itr))) {
			uint64_t total_used = 0;
				
			/* sanity check to make sure we don't have more
			   allocated cpus than possible. */
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

				/* set the planned down to 0 and the
				   down to what ever is left from the
				   allocated. */
				c_usage->pd_cpu = 0;
				c_usage->d_cpu = 
					c_usage->total_time - c_usage->a_cpu;

				total_used = c_usage->a_cpu +
					c_usage->d_cpu + c_usage->pd_cpu;
			}

			c_usage->i_cpu = c_usage->total_time -
				total_used - c_usage->r_cpu;
			/* sanity check just to make sure we have a
			 * legitimate time after we calulated
			 * idle/reserved time put extra in the over
			 * commit field
			 */
/* 			info("%s got idle of %lld", c_usage->name,  */
/* 			     (int64_t)c_usage->i_cpu); */
			if((int64_t)c_usage->i_cpu < 0) {
/* 				info("got %d %d %d", c_usage->r_cpu, */
/* 				     c_usage->i_cpu, c_usage->o_cpu); */
				c_usage->r_cpu += c_usage->i_cpu;
				c_usage->o_cpu -= c_usage->i_cpu;
				c_usage->i_cpu = 0;
				if((int64_t)c_usage->r_cpu < 0)
					c_usage->r_cpu = 0;
			}
			
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
			if(query) {
				xstrfmtcat(query, 
					   ", (%d, %d, '%s', %d, %d, "
					   "%llu, %llu, %llu, "
					   "%llu, %llu, %llu)",
					   now, now, 
					   c_usage->name, c_usage->start, 
					   c_usage->cpu_count, c_usage->a_cpu,
					   c_usage->d_cpu, c_usage->pd_cpu,
					   c_usage->i_cpu, c_usage->o_cpu,
					   c_usage->r_cpu); 
			} else {
				xstrfmtcat(query, 
					   "insert into %s (creation_time, "
					   "mod_time, cluster, period_start, "
					   "cpu_count, alloc_cpu_secs, "
					   "down_cpu_secs, pdown_cpu_secs, "
					   "idle_cpu_secs, over_cpu_secs, "
					   "resv_cpu_secs) "
					   "values (%d, %d, '%s', %d, %d, "
					   "%llu, %llu, %llu, "
					   "%llu, %llu, %llu)",
					   cluster_hour_table, now, now, 
					   c_usage->name, c_usage->start, 
					   c_usage->cpu_count,
					   c_usage->a_cpu, c_usage->d_cpu, 
					   c_usage->pd_cpu, c_usage->i_cpu,
					   c_usage->o_cpu, c_usage->r_cpu); 
			}
		}

		/* Spacing out the inserts here instead of doing them
		   all at once in the end proves to be faster.  Just FYI
		   so we don't go testing again and again.
		*/
		if(query) {
			xstrfmtcat(query, 
				   " on duplicate key update "
				   "mod_time=%d, cpu_count=VALUES(cpu_count), "
				   "alloc_cpu_secs=VALUES(alloc_cpu_secs), "
				   "down_cpu_secs=VALUES(down_cpu_secs), "
				   "pdown_cpu_secs=VALUES(pdown_cpu_secs), "
				   "idle_cpu_secs=VALUES(idle_cpu_secs), "
				   "over_cpu_secs=VALUES(over_cpu_secs), "
				   "resv_cpu_secs=VALUES(resv_cpu_secs)",
				   now);
			debug3("%d(%d) query\n%s",
			       mysql_conn->conn, __LINE__, query);
			rc = mysql_db_query(mysql_conn->db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add cluster hour rollup");
				goto end_it;
			}
		}

		list_iterator_reset(a_itr);
		while((a_usage = list_next(a_itr))) {
/* 			info("association (%d) %d alloc %d", */
/* 			     a_usage->id, last_id, */
/* 			     a_usage->a_cpu); */
			if(query) {
				xstrfmtcat(query, 
					   ", (%d, %d, %d, %d, %llu)",
					   now, now, 
					   a_usage->id, curr_start,
					   a_usage->a_cpu); 
			} else {
				xstrfmtcat(query, 
					   "insert into %s (creation_time, "
					   "mod_time, id, period_start, "
					   "alloc_cpu_secs) values "
					   "(%d, %d, %d, %d, %llu)",
					   assoc_hour_table, now, now, 
					   a_usage->id, curr_start,
					   a_usage->a_cpu); 
			}
		}
		if(query) {
			xstrfmtcat(query, 
				   " on duplicate key update "
				   "mod_time=%d, "
				   "alloc_cpu_secs=VALUES(alloc_cpu_secs);",
				   now);
					   	
			debug3("%d(%d) query\n%s",
			       mysql_conn->conn, __LINE__, query);
			rc = mysql_db_query(mysql_conn->db_conn, query);
			xfree(query);
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
			if(query) {
				xstrfmtcat(query, 
					   ", (%d, %d, %d, %d, %llu)",
					   now, now, 
					   w_usage->id, curr_start,
					   w_usage->a_cpu); 
			} else {
				xstrfmtcat(query, 
					   "insert into %s (creation_time, "
					   "mod_time, id, period_start, "
					   "alloc_cpu_secs) values "
					   "(%d, %d, %d, %d, %llu)",
					   wckey_hour_table, now, now, 
					   w_usage->id, curr_start,
					   w_usage->a_cpu); 
			}
		}
		if(query) {
			xstrfmtcat(query, 
				   " on duplicate key update "
				   "mod_time=%d, "
				   "alloc_cpu_secs=VALUES(alloc_cpu_secs);",
				   now);
					   	
			debug3("%d(%d) query\n%s",
			       mysql_conn->conn, __LINE__, query);
			rc = mysql_db_query(mysql_conn->db_conn, query);
			xfree(query);
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
	xfree(suspend_str);	
	xfree(event_str);	
	xfree(job_str);
	xfree(resv_str);
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
extern int mysql_daily_rollup(mysql_conn_t *mysql_conn, 
			      time_t start, time_t end, uint16_t archive_data)
{
	/* can't just add 86400 since daylight savings starts and ends every
	 * once in a while
	 */
	int rc = SLURM_SUCCESS;
	struct tm start_tm;
	time_t curr_start = start;
	time_t curr_end;
	time_t now = time(NULL);
	char *query = NULL;
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
		query = xstrdup_printf(
			"insert into %s (creation_time, mod_time, id, "
			"period_start, alloc_cpu_secs) select %d, %d, id, "
			"%d, @ASUM:=SUM(alloc_cpu_secs) from %s where "
			"(period_start < %d && period_start >= %d) "
			"group by id on duplicate key update mod_time=%d, "
			"alloc_cpu_secs=@ASUM;",
			assoc_day_table, now, now, curr_start,
			assoc_hour_table,
			curr_end, curr_start, now);
		xstrfmtcat(query,
			   "insert into %s (creation_time, "
			   "mod_time, cluster, period_start, cpu_count, "
			   "alloc_cpu_secs, down_cpu_secs, pdown_cpu_secs, "
			   "idle_cpu_secs, over_cpu_secs, resv_cpu_secs) "
			   "select %d, %d, cluster, "
			   "%d, @CPU:=MAX(cpu_count), "
			   "@ASUM:=SUM(alloc_cpu_secs), "
			   "@DSUM:=SUM(down_cpu_secs), "
			   "@PDSUM:=SUM(pdown_cpu_secs), "
			   "@ISUM:=SUM(idle_cpu_secs), "
			   "@OSUM:=SUM(over_cpu_secs), "
			   "@RSUM:=SUM(resv_cpu_secs) from %s where "
			   "(period_start < %d && period_start >= %d) "
			   "group by cluster on duplicate key update "
			   "mod_time=%d, cpu_count=@CPU, "
			   "alloc_cpu_secs=@ASUM, down_cpu_secs=@DSUM, "
			   "pdown_cpu_secs=@PDSUM, idle_cpu_secs=@ISUM, "
			   "over_cpu_secs=@OSUM, resv_cpu_secs=@RSUM;",
			   cluster_day_table, now, now, curr_start,
			   cluster_hour_table,
			   curr_end, curr_start, now);
		if(track_wckey) {
			xstrfmtcat(query,
				   "insert into %s (creation_time, "
				   "mod_time, id, period_start, "
				   "alloc_cpu_secs) select %d, %d, "
				   "id, %d, @ASUM:=SUM(alloc_cpu_secs) "
				   "from %s where (period_start < %d && "
				   "period_start >= %d) "
				   "group by id on duplicate key update "
				   "mod_time=%d, alloc_cpu_secs=@ASUM;",
				   wckey_day_table, now, now, curr_start,
				   wckey_hour_table,
				   curr_end, curr_start, now);
		}
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
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
extern int mysql_monthly_rollup(mysql_conn_t *mysql_conn,
				time_t start, time_t end, uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;
	struct tm start_tm;
	time_t curr_start = start;
	time_t curr_end;
	time_t now = time(NULL);
	char *query = NULL;
	uint16_t track_wckey = slurm_get_track_wckey();
	acct_archive_cond_t arch_cond;

	if(!localtime_r(&curr_start, &start_tm)) {
		error("Couldn't get localtime from month start %d", curr_start);
		return SLURM_ERROR;
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 1;
	start_tm.tm_mon++;
	start_tm.tm_isdst = -1;
	curr_end = mktime(&start_tm);

	while(curr_start < end) {
		debug3("curr month is now %d-%d", curr_start, curr_end);
/* 		info("start %s", ctime(&curr_start)); */
/* 		info("end %s", ctime(&curr_end)); */
		query = xstrdup_printf(
			"insert into %s (creation_time, mod_time, id, "
			"period_start, alloc_cpu_secs) select %d, %d, id, "
			"%d, @ASUM:=SUM(alloc_cpu_secs) from %s where "
			"(period_start < %d && period_start >= %d) "
			"group by id on duplicate key update mod_time=%d, "
			"alloc_cpu_secs=@ASUM;",
			assoc_month_table, now, now, curr_start,
			assoc_day_table,
			curr_end, curr_start, now);
		xstrfmtcat(query,
			   "insert into %s (creation_time, "
			   "mod_time, cluster, period_start, cpu_count, "
			   "alloc_cpu_secs, down_cpu_secs, pdown_cpu_secs, "
			   "idle_cpu_secs, over_cpu_secs, resv_cpu_secs) "
			   "select %d, %d, cluster, "
			   "%d, @CPU:=MAX(cpu_count), "
			   "@ASUM:=SUM(alloc_cpu_secs), "
			   "@DSUM:=SUM(down_cpu_secs), "
			   "@PDSUM:=SUM(pdown_cpu_secs), "
			   "@ISUM:=SUM(idle_cpu_secs), "
			   "@OSUM:=SUM(over_cpu_secs), "
			   "@RSUM:=SUM(resv_cpu_secs) from %s where "
			   "(period_start < %d && period_start >= %d) "
			   "group by cluster on duplicate key update "
			   "mod_time=%d, cpu_count=@CPU, "
			   "alloc_cpu_secs=@ASUM, down_cpu_secs=@DSUM, "
			   "pdown_cpu_secs=@PDSUM, idle_cpu_secs=@ISUM, "
			   "over_cpu_secs=@OSUM, resv_cpu_secs=@RSUM;",
			   cluster_month_table, now, now, curr_start,
			   cluster_day_table,
			   curr_end, curr_start, now);
		if(track_wckey) {
			xstrfmtcat(query,
				   "insert into %s (creation_time, mod_time, "
				   "id, period_start, alloc_cpu_secs) "
				   "select %d, %d, id, %d, "
				   "@ASUM:=SUM(alloc_cpu_secs) "
				   "from %s where (period_start < %d && "
				   "period_start >= %d) "
				   "group by id on duplicate key update "
				   "mod_time=%d, alloc_cpu_secs=@ASUM;",
				   wckey_month_table, now, now, curr_start,
				   wckey_day_table,
				   curr_end, curr_start, now);
		}
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add day rollup");
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

	return mysql_jobacct_process_archive(mysql_conn, &arch_cond);
}
