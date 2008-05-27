/*****************************************************************************\
 *  mysql_rollup.c - functions for rolling up data for associations
 *                   and machines from the mysql storage.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_MYSQL

typedef struct {
	int assoc_id;
	int a_cpu;
} local_assoc_usage_t;

typedef struct {
	char *name;
	int a_cpu;
	int cpu_count;
	int d_cpu;
	int i_cpu;
	int r_cpu;
	time_t start;
	time_t end;
} local_cluster_usage_t;


extern void _destroy_local_assoc_usage(void *object)
{
	local_assoc_usage_t *a_usage = (local_assoc_usage_t *)object;
	if(a_usage) {
		xfree(a_usage);
	}
}

extern void _destroy_local_cluster_usage(void *object)
{
	local_cluster_usage_t *c_usage = (local_cluster_usage_t *)object;
	if(c_usage) {
		xfree(c_usage->name);
		xfree(c_usage);
	}
}

extern int mysql_hourly_rollup(mysql_conn_t *mysql_conn,
			       time_t start, time_t end)
{
	int add_sec = 3600;
	int i=0;
	time_t curr_start = start;
	time_t curr_end = curr_start + add_sec;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	ListIterator a_itr = NULL;
	ListIterator c_itr = NULL;
	List assoc_usage_list = list_create(_destroy_local_assoc_usage);
	List cluster_usage_list = list_create(_destroy_local_cluster_usage);
	char *event_req_inx[] = {
		"node_name",
		"cluster",
		"cpu_count",
		"period_start",
		"period_end"
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
		"t1.id",
		"jobid",
		"associd",
		"cluster",
		"eligible",
		"start",
		"end",
		"suspended",
		"alloc_cpus",
		"req_cpus"
	};
	char *job_str = NULL;
	enum {
		JOB_REQ_DB_INX,
		JOB_REQ_JOBID,
		JOB_REQ_ASSOCID,
		JOB_REQ_CLUSTER,
		JOB_REQ_ELG,
		JOB_REQ_START,
		JOB_REQ_END,
		JOB_REQ_SUSPENDED,
		JOB_REQ_ACPU,
		JOB_REQ_RCPU,
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

/* 	info("begin start %s", ctime(&curr_start)); */
/* 	info("begin end %s", ctime(&curr_end)); */
	a_itr = list_iterator_create(cluster_usage_list);
	c_itr = list_iterator_create(cluster_usage_list);
	while(curr_start < end) {
		int last_id = 0;
		int seconds = 0;
		local_cluster_usage_t *c_usage = NULL;
		local_assoc_usage_t *a_usage = NULL;
		
		// first get the events during this time
		query = xstrdup_printf("select %s from %s where "
				       "(period_start < %d "
				       "&& period_end >= %d) "
				       "|| period_end = 0 "
				       "order by node_name, period_start",
				       event_str, event_table,
				       curr_end, curr_start);

		debug3("%d query\n%s", mysql_conn->conn, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->acct_mysql_db, query, 0))) {
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
		
			if(!row_end) 
				row_end = curr_end;

			if(!row[EVENT_REQ_NAME][0]) {
				c_usage =
					xmalloc(sizeof(local_cluster_usage_t));
				c_usage->name = xstrdup(row[EVENT_REQ_CLUSTER]);
				c_usage->cpu_count = row_cpu;
				c_usage->start = row_start;
				c_usage->end = row_end;
				list_append(cluster_usage_list, c_usage);
				continue;
			}

			list_iterator_reset(c_itr);
			while((c_usage = list_next(c_itr))) {
				if(!strcmp(c_usage->name,
					   row[EVENT_REQ_CLUSTER])) {
					int local_start = row_start;
					int local_end = row_end;
					if(c_usage->start > local_start)
						local_start = c_usage->start;
					if(c_usage->end < local_end)
						local_end = c_usage->end;

					if((local_end - local_start) < 1)
						continue;

					seconds = (local_end - local_start);

					info("node %s adds (%d)(%d-%d) * %d = %d "
					     "to %d",
					     row[EVENT_REQ_NAME],
					     seconds,
					     local_end, local_start,
					     row_cpu, 
					     seconds * row_cpu, 
					     row_cpu);
					c_usage->d_cpu += seconds * row_cpu;
					
					/* don't break here just
					   incase the cpu count changed during
					   this time period.
					*/
				}				   
			}
		}
		mysql_free_result(result);

		query = xstrdup_printf("select %s from %s as t1, "
				       "%s as t2 where "
				       "((eligible < %d && end >= %d) "
				       "|| end = 0 || start = 0) "
				       "&& associd=t2.id "
				       "order by associd, eligible",
				       job_str, job_table, assoc_table,
				       curr_end, curr_start);

		debug3("%d query\n%s", mysql_conn->conn, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->acct_mysql_db, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		
		
		while((row = mysql_fetch_row(result))) {
			int job_id = atoi(row[JOB_REQ_ASSOCID]);
			int assoc_id = atoi(row[JOB_REQ_ASSOCID]);
			int row_eligible = atoi(row[JOB_REQ_ELG]);
			int row_start = atoi(row[JOB_REQ_START]);
			int row_end = atoi(row[JOB_REQ_END]);
			int row_acpu = atoi(row[JOB_REQ_ACPU]);
			int row_rcpu = atoi(row[JOB_REQ_RCPU]);
			MYSQL_RES *result2 = NULL;
			MYSQL_ROW row2;
					
			if(row_start && (row_start < curr_start))
				row_start = curr_start;
			if(!row_start && row_end)
				row_start = row_end;
			if(!row_end) 
				row_end = curr_end;
			
			if(last_id != assoc_id) {
				a_usage =
					xmalloc(sizeof(local_cluster_usage_t));
				a_usage->assoc_id = assoc_id;
				list_append(assoc_usage_list, a_usage);
				last_id = assoc_id;
			}


			if(!row_start || ((row_end - row_start) < 1)) 
				continue;

			seconds = (row_end - row_start);

			if(row[JOB_REQ_SUSPENDED]) {
				/* get the suspended time for this job */
				query = xstrdup_printf(
					"select %s from %s where "
					"((start < %d && end >= %d) "
					"|| end = 0) && id=%s "
					"order by start",
					suspend_str, suspend_table,
					curr_end, curr_start,
					row[JOB_REQ_DB_INX]);
				
				debug4("%d query\n%s", mysql_conn->conn, query);
				if(!(result2 = mysql_db_query_ret(
					     mysql_conn->acct_mysql_db,
					     query, 0))) {
					xfree(query);
					return SLURM_ERROR;
				}
				xfree(query);
				while((row2 = mysql_fetch_row(result2))) {
					int local_start =
						atoi(row[SUSPEND_REQ_START]);
					int local_end = 
						atoi(row[SUSPEND_REQ_END]);

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
			}
			if(seconds < 1) {
				debug("This job (%u) was suspended "
				       "the entire hour", job_id);
				if(result2)
					mysql_free_result(result2);	
				continue;
			}
			a_usage->a_cpu += seconds * row_acpu;

			if(!row[JOB_REQ_CLUSTER]) {
				if(result2)
					mysql_free_result(result2);	
				continue;
			}
			list_iterator_reset(c_itr);
			while((c_usage = list_next(c_itr))) {
				if(!strcmp(c_usage->name,
					   row[JOB_REQ_CLUSTER])) {
					int local_start = row_start;
					int local_end = row_end;
					if(!local_start)
						goto calc_resv;

					if(c_usage->start > local_start)
						local_start = c_usage->start;
					if(c_usage->end < local_end)
						local_end = c_usage->end;

					if((local_end - local_start) < 1)
						goto calc_resv;

					seconds = (local_end - local_start);

					if(result2) {
						mysql_data_seek(result2, 0);
						while((row2 =
						       mysql_fetch_row(result2)
							      )) {
							int suspend_start = 
								atoi(row[SUSPEND_REQ_START]);
							int suspend_end = 
								atoi(row[SUSPEND_REQ_END]);

							if(!suspend_start)
								continue;
							
							if(c_usage->start
							   > suspend_start)
								suspend_start =
								c_usage->start;
							if(c_usage->end
							   < suspend_end)
								suspend_end =
								c_usage->end;
							
							if((suspend_end 
							    - suspend_start)
							   < 1)
								continue;
							
							seconds -= 
								(suspend_end -
								 suspend_start);
						}

						if(seconds < 1) {
							debug("This job (%u) "
							       "was suspended "
							       "the entire "
							       "cluster time",
							       job_id);
							continue;
						}
					}

					info("%d assoc %d adds (%d)(%d-%d) * %d = %d "
					     "to %d",
					     job_id,
					     assoc_id,
					     seconds,
					     local_end, local_start,
					     row_acpu,
					     seconds * row_acpu,
					     row_acpu);
					c_usage->a_cpu += seconds * row_acpu;
				calc_resv:
					/* now reserved time */
					if(row_start < c_usage->start)
						continue;
					local_start = row_eligible;
					local_end = row_start;
					if(c_usage->start > local_start)
						local_start = c_usage->start;
					if(c_usage->end < local_end)
						local_end = c_usage->end;

					if((local_end - local_start) < 1)
						continue;
					
					seconds = (local_end - local_start);

					info("%d assoc %d reserved (%d)(%d-%d) * %d = %d "
					     "to %d",
					     job_id,
					     assoc_id,
					     seconds,
					     local_end, local_start,
					     row_rcpu,
					     seconds * row_rcpu,
					     row_rcpu);
					c_usage->r_cpu += seconds * row_rcpu;

					/* don't break here just
					   incase the cpu count changed during
					   this time period.
					*/
				}
			}
			if(result2)
				mysql_free_result(result2);			
		}
		mysql_free_result(result);

		list_iterator_reset(c_itr);
		while((c_usage = list_next(c_itr))) {
			int total_time = (curr_end - curr_start)
				* c_usage->cpu_count;
			
			c_usage->i_cpu = total_time - c_usage->a_cpu -
				c_usage->d_cpu - c_usage->r_cpu;
			if(c_usage->i_cpu < 0) {
				c_usage->r_cpu += c_usage->i_cpu;
				c_usage->i_cpu = 0;
				if(c_usage->r_cpu < 0) 
					c_usage->r_cpu = 0;
			}

			info("cluster %s(%u) down %u alloc %u "
			     "resv %u idle %u total= %u from %s", c_usage->name,
			     c_usage->cpu_count, c_usage->d_cpu, c_usage->a_cpu,
			     c_usage->r_cpu, c_usage->i_cpu,
			     c_usage->d_cpu + c_usage->a_cpu +
			     c_usage->r_cpu + c_usage->i_cpu,
			     ctime(&c_usage->start));
			info("to %s", ctime(&c_usage->end));
		}
		list_flush(assoc_usage_list);
		list_flush(cluster_usage_list);
		curr_start = curr_end;
		curr_end = curr_start + add_sec;
		debug3("curr hour is now %d-%d", curr_start, curr_end);
	}
	xfree(event_str);	
	xfree(job_str);
	list_iterator_destroy(a_itr);
	list_iterator_destroy(c_itr);
		
	list_destroy(assoc_usage_list);
	list_destroy(cluster_usage_list);
/* 	info("stop start %s", ctime(&curr_start)); */
/* 	info("stop end %s", ctime(&curr_end)); */
	return SLURM_SUCCESS;
}
extern int mysql_daily_rollup(mysql_conn_t *mysql_conn, 
			      time_t start, time_t end)
{
	int add_sec = 86400;
	time_t curr_start = start;
	time_t curr_end = curr_start + add_sec;

/* 	info("begin start %s", ctime(&curr_start)); */
/* 	info("begin end %s", ctime(&curr_end)); */
	while(curr_start < end) {

		curr_start = curr_end;
		curr_end = curr_start + add_sec;
		debug3("curr day is now %d-%d", curr_start, curr_end);
	}
/* 	info("stop start %s", ctime(&curr_start)); */
/* 	info("stop end %s", ctime(&curr_end)); */

	return SLURM_SUCCESS;
}
extern int mysql_monthly_rollup(mysql_conn_t *mysql_conn,
			       time_t start, time_t end)
{
	struct tm start_tm;
	time_t curr_start = start;
	time_t curr_end;

/* 	info("begin month start %s", ctime(&start)); */
/* 	info("begin month end %s", ctime(&end)); */
	if(!localtime_r(&curr_start, &start_tm)) {
		error("Couldn't get localtime from month start %d", curr_start);
		return SLURM_ERROR;
	}
	start_tm.tm_sec = -1;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 1;
	start_tm.tm_mon++;
	start_tm.tm_isdst = -1;
	curr_end = mktime(&start_tm);
/* 	info("begin start %s", ctime(&curr_start)); */
/* 	info("begin end %s", ctime(&curr_end)); */
	while(curr_start < end) {

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
		debug3("curr month is now %d-%d", curr_start, curr_end);
	}
/* 	info("stop start %s", ctime(&curr_start)); */
/* 	info("stop end %s", ctime(&curr_end)); */
	return SLURM_SUCCESS;
}

#endif
