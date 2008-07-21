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
	uint64_t a_cpu;
} local_assoc_usage_t;

typedef struct {
	char *name;
	uint64_t total_time;
	uint64_t a_cpu;
	int cpu_count;
	uint64_t d_cpu;
	uint64_t i_cpu;
	uint64_t o_cpu;
	uint64_t r_cpu;
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
	a_itr = list_iterator_create(assoc_usage_list);
	c_itr = list_iterator_create(cluster_usage_list);
	while(curr_start < end) {
		int last_id = 0;
		int seconds = 0;
		local_cluster_usage_t *c_usage = NULL;
		local_assoc_usage_t *a_usage = NULL;
		debug3("curr hour is now %d-%d", curr_start, curr_end);
/* 		info("start %s", ctime(&curr_start)); */
/* 		info("end %s", ctime(&curr_end)); */
		
		// first get the events during this time
		query = xstrdup_printf("select %s from %s where "
				       "(period_start < %d "
				       "&& (period_end >= %d "
				       "|| period_end = 0)) "
				       "order by node_name, period_start",
				       event_str, event_table,
				       curr_end, curr_start);

		debug3("%d query\n%s", mysql_conn->conn, query);
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

			if(!row[EVENT_REQ_NAME][0]) {
				list_iterator_reset(c_itr);
				while((c_usage = list_next(c_itr))) {
					if(!strcmp(c_usage->name,
					   row[EVENT_REQ_CLUSTER])) {
						break;
					}
				}
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
				} else {
					c_usage->cpu_count = row_cpu;
					c_usage->total_time +=
						(row_end - row_start) * row_cpu;
					c_usage->end = row_end;
				}
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
					
					break;
				}				   
			}
		}
		mysql_free_result(result);

		query = xstrdup_printf("select %s from %s as t1, "
				       "%s as t2 where "
				       "(eligible < %d && (end >= %d "
				       "|| end = 0)) && associd=t2.id "
				       "order by associd, eligible",
				       job_str, job_table, assoc_table,
				       curr_end, curr_start, curr_start);

		debug3("%d query\n%s", mysql_conn->conn, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		
		while((row = mysql_fetch_row(result))) {
			int job_id = atoi(row[JOB_REQ_JOBID]);
			int assoc_id = atoi(row[JOB_REQ_ASSOCID]);
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

			if(last_id != assoc_id) {
				a_usage =
					xmalloc(sizeof(local_cluster_usage_t));
				a_usage->assoc_id = assoc_id;
				list_append(assoc_usage_list, a_usage);
				last_id = assoc_id;
			}


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
				
				debug4("%d query\n%s", mysql_conn->conn, query);
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


			a_usage->a_cpu += seconds * row_acpu;

		calc_cluster:
			if(!row[JOB_REQ_CLUSTER]) 
				continue;
			
			list_iterator_reset(c_itr);
			while((c_usage = list_next(c_itr))) {
				if(!strcmp(c_usage->name,
					   row[JOB_REQ_CLUSTER])) {
					if(!row_start || seconds < 1)
						goto calc_resv;

/* 					info("%d assoc %d adds " */
/* 					     "(%d)(%d-%d) * %d = %d " */
/* 					     "to %d", */
/* 					     job_id, */
/* 					     a_usage->assoc_id, */
/* 					     seconds, */
/* 					     row_end, row_start, */
/* 					     row_acpu, */
/* 					     seconds * row_acpu, */
/* 					     row_acpu); */

					c_usage->a_cpu += seconds * row_acpu;

				calc_resv:
					/* now reserved time */
					if(row_start && 
					   row_start < c_usage->start)
						continue;
					
					row_end = row_start;
					row_start = row_eligible;
					if(c_usage->start > row_start)
						row_start = c_usage->start;
					if(c_usage->end < row_end)
						row_end = c_usage->end;
					
					if((row_end - row_start) < 1)
						continue;
					
					seconds = (row_end - row_start);

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
					c_usage->r_cpu += seconds * row_rcpu;

					break;
				}
			}
		}
		mysql_free_result(result);

		list_iterator_reset(c_itr);
		while((c_usage = list_next(c_itr))) {
			c_usage->i_cpu = c_usage->total_time - c_usage->a_cpu -
				c_usage->d_cpu - c_usage->r_cpu;
			/* sanity check just to make sure we have a
			 * legitimate time after we calulated
			 * idle/reserved time put extra in the over
			 * commit field
			 */
			
			if(c_usage->i_cpu < 0) {
/* 				info("got %d %d %d", c_usage->r_cpu, */
/* 				     c_usage->i_cpu, c_usage->o_cpu); */
				c_usage->r_cpu += c_usage->i_cpu;
				c_usage->o_cpu -= c_usage->i_cpu;
				c_usage->i_cpu = 0;
				if(c_usage->r_cpu < 0)
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
					   "%llu, %llu, %llu, %llu, %llu)",
					   now, now, 
					   c_usage->name, c_usage->start, 
					   c_usage->cpu_count, c_usage->a_cpu,
					   c_usage->d_cpu, c_usage->i_cpu,
					   c_usage->o_cpu, c_usage->r_cpu); 
			} else {
				xstrfmtcat(query, 
					   "insert into %s (creation_time, "
					   "mod_time, cluster, period_start, "
					   "cpu_count, alloc_cpu_secs, "
					   "down_cpu_secs, idle_cpu_secs, "
					   "over_cpu_secs, resv_cpu_secs) "
					   "values (%d, %d, '%s', %d, %d, "
					   "%llu, %llu, %llu, %llu, %llu)",
					   cluster_hour_table, now, now, 
					   c_usage->name, c_usage->start, 
					   c_usage->cpu_count,
					   c_usage->a_cpu,
					   c_usage->d_cpu, c_usage->i_cpu,
					   c_usage->o_cpu, c_usage->r_cpu); 
			}
		}

		if(query) {
			xstrfmtcat(query, 
				   " on duplicate key update "
				   "mod_time=%d, cpu_count=VALUES(cpu_count), "
				   "alloc_cpu_secs=VALUES(alloc_cpu_secs), "
				   "down_cpu_secs=VALUES(down_cpu_secs), "
				   "idle_cpu_secs=VALUES(idle_cpu_secs), "
				   "over_cpu_secs=VALUES(over_cpu_secs), "
				   "resv_cpu_secs=VALUES(resv_cpu_secs)",
				   now);
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
/* 			     a_usage->assoc_id, last_id, */
/* 			     a_usage->a_cpu); */
			if(query) {
				xstrfmtcat(query, 
					   ", (%d, %d, %d, %d, %llu)",
					   now, now, 
					   a_usage->assoc_id, curr_start,
					   a_usage->a_cpu); 
			} else {
				xstrfmtcat(query, 
					   "insert into %s (creation_time, "
					   "mod_time, id, period_start, "
					   "alloc_cpu_secs) values "
					   "(%d, %d, %d, %d, %llu)",
					   assoc_hour_table, now, now, 
					   a_usage->assoc_id, curr_start,
					   a_usage->a_cpu); 
			}
		}
		if(query) {
			xstrfmtcat(query, 
				   " on duplicate key update "
				   "mod_time=%d, "
				   "alloc_cpu_secs=VALUES(alloc_cpu_secs)",
				   now);
					   	
			debug3("%d query\n%s", mysql_conn->conn, query);
			rc = mysql_db_query(mysql_conn->db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add assoc hour rollup");
				goto end_it;
			}
		}
		list_flush(assoc_usage_list);
		list_flush(cluster_usage_list);
		curr_start = curr_end;
		curr_end = curr_start + add_sec;
	}
end_it:
	xfree(suspend_str);	
	xfree(event_str);	
	xfree(job_str);
	list_iterator_destroy(a_itr);
	list_iterator_destroy(c_itr);
		
	list_destroy(assoc_usage_list);
	list_destroy(cluster_usage_list);
/* 	info("stop start %s", ctime(&curr_start)); */
/* 	info("stop end %s", ctime(&curr_end)); */
	return rc;
}
extern int mysql_daily_rollup(mysql_conn_t *mysql_conn, 
			      time_t start, time_t end)
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
			   "alloc_cpu_secs, down_cpu_secs, idle_cpu_secs, "
			   "over_cpu_secs, resv_cpu_secs) "
			   "select %d, %d, cluster, "
			   "%d, @CPU:=MAX(cpu_count), "
			   "@ASUM:=SUM(alloc_cpu_secs), "
			   "@DSUM:=SUM(down_cpu_secs), "
			   "@ISUM:=SUM(idle_cpu_secs), "
			   "@OSUM:=SUM(over_cpu_secs), "
			   "@RSUM:=SUM(resv_cpu_secs) from %s where "
			   "(period_start < %d && period_start >= %d) "
			   "group by cluster on duplicate key update "
			   "mod_time=%d, cpu_count=@CPU, "
			   "alloc_cpu_secs=@ASUM, down_cpu_secs=@DSUM, "
			   "idle_cpu_secs=@ISUM, over_cpu_secs=@OSUM, "
			   "resv_cpu_secs=@RSUM;",
			   cluster_day_table, now, now, curr_start,
			   cluster_hour_table,
			   curr_end, curr_start, now);
		debug3("%d query\n%s", mysql_conn->conn, query);
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
	/* remove all data from suspend table that was older than
	 * start. 
	 */
	query = xstrdup_printf("delete from %s where end < %d && end != 0",
			       suspend_table, start);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't remove old suspend data");
		return SLURM_ERROR;
	}
			       

/* 	info("stop start %s", ctime(&curr_start)); */
/* 	info("stop end %s", ctime(&curr_end)); */

	return SLURM_SUCCESS;
}
extern int mysql_monthly_rollup(mysql_conn_t *mysql_conn,
			       time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	struct tm start_tm;
	time_t curr_start = start;
	time_t curr_end;
	time_t now = time(NULL);
	char *query = NULL;

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
			   "alloc_cpu_secs, down_cpu_secs, idle_cpu_secs, "
			   "over_cpu_secs, resv_cpu_secs) "
			   "select %d, %d, cluster, "
			   "%d, @CPU:=MAX(cpu_count), "
			   "@ASUM:=SUM(alloc_cpu_secs), "
			   "@DSUM:=SUM(down_cpu_secs), "
			   "@ISUM:=SUM(idle_cpu_secs), "
			   "@OSUM:=SUM(over_cpu_secs), "
			   "@RSUM:=SUM(resv_cpu_secs) from %s where "
			   "(period_start < %d && period_start >= %d) "
			   "group by cluster on duplicate key update "
			   "mod_time=%d, cpu_count=@CPU, "
			   "alloc_cpu_secs=@ASUM, down_cpu_secs=@DSUM, "
			   "idle_cpu_secs=@ISUM, over_cpu_secs=@OSUM, "
			   "resv_cpu_secs=@RSUM;",
			   cluster_month_table, now, now, curr_start,
			   cluster_day_table,
			   curr_end, curr_start, now);
		debug3("%d query\n%s", mysql_conn->conn, query);
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

	/* remove all data from event table that was older than
	 * start. 
	 */
	query = xstrdup_printf("delete from %s where period_end < %d "
			       "&& period_end != 0",
			       event_table, start);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't remove old event data");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

#endif
