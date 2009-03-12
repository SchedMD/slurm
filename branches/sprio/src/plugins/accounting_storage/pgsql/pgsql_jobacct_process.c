/*****************************************************************************\
 *  pgsql_jobacct_process.c - functions the processing of
 *                               information from the pgsql jobacct
 *                               storage.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <stdlib.h>
#include "pgsql_jobacct_process.h"

#ifdef HAVE_PGSQL

extern List pgsql_jobacct_process_get_jobs(PGconn *acct_pgsql_db,
					   acct_job_cond_t *job_cond)
{

	char *query = NULL;	
	char *extra = NULL;	
	char *tmp = NULL;	
	char *object = NULL;
	jobacct_selected_step_t *selected_step = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *table_level="t2";
	PGresult *result = NULL, *step_result = NULL;
	int i, j, last_id = -1, curr_id = -1;
	jobacct_job_rec_t *job = NULL;
	jobacct_step_rec_t *step = NULL;
	time_t now = time(NULL);
	List job_list = list_create(destroy_jobacct_job_rec);
		
	/* if this changes you will need to edit the corresponding 
	 * enum below also t1 is job_table */
	char *job_req_inx[] = {
		"t1.id",
		"t1.jobid",
		"t1.associd",
		"t1.uid",
		"t1.gid",
		"t1.partition",
		"t1.blockid",
		"t1.cluster",
		"t1.account",
		"t1.eligible",
		"t1.submit",
		"t1.start",
		"t1.endtime",
		"t1.suspended",
		"t1.name",
		"t1.track_steps",
		"t1.state",
		"t1.comp_code",
		"t1.priority",
		"t1.req_cpus",
		"t1.alloc_cpus",
		"t1.nodelist",
		"t1.kill_requid",
		"t1.qos",
		"t2.user_name",
		"t2.cluster",
		"t2.lft"
	};

	/* if this changes you will need to edit the corresponding 
	 * enum below also t1 is step_table */
	char *step_req_inx[] = {
		"t1.stepid",
		"t1.start",
		"t1.endtime",
		"t1.suspended",
		"t1.name",
		"t1.nodelist",
		"t1.state",
		"t1.kill_requid",
		"t1.comp_code",
		"t1.cpus",
		"t1.user_sec",
		"t1.user_usec",
		"t1.sys_sec",
		"t1.sys_usec",
		"t1.max_vsize",
		"t1.max_vsize_task",
		"t1.max_vsize_node",
		"t1.ave_vsize",
		"t1.max_rss",
		"t1.max_rss_task",
		"t1.max_rss_node",
		"t1.ave_rss",
		"t1.max_pages",
		"t1.max_pages_task",
		"t1.max_pages_node",
		"t1.ave_pages",
		"t1.min_cpu",
		"t1.min_cpu_task",
		"t1.min_cpu_node",
		"t1.ave_cpu",
	};

	enum {
		JOB_REQ_ID,
		JOB_REQ_JOBID,
		JOB_REQ_ASSOCID,
		JOB_REQ_UID,
		JOB_REQ_GID,
		JOB_REQ_PARTITION,
		JOB_REQ_BLOCKID,
		JOB_REQ_CLUSTER1,
		JOB_REQ_ACCOUNT,
		JOB_REQ_ELIGIBLE,
		JOB_REQ_SUBMIT,
		JOB_REQ_START,
		JOB_REQ_ENDTIME,
		JOB_REQ_SUSPENDED,
		JOB_REQ_NAME,
		JOB_REQ_TRACKSTEPS,
		JOB_REQ_STATE,
		JOB_REQ_COMP_CODE,
		JOB_REQ_PRIORITY,
		JOB_REQ_REQ_CPUS,
		JOB_REQ_ALLOC_CPUS,
		JOB_REQ_NODELIST,
		JOB_REQ_KILL_REQUID,
		JOB_REQ_QOS,
		JOB_REQ_USER_NAME,
		JOB_REQ_CLUSTER,
		JOB_REQ_LFT,
		JOB_REQ_COUNT		
	};
	enum {
		STEP_REQ_STEPID,
		STEP_REQ_START,
		STEP_REQ_ENDTIME,
		STEP_REQ_SUSPENDED,
		STEP_REQ_NAME,
		STEP_REQ_NODELIST,
		STEP_REQ_STATE,
		STEP_REQ_KILL_REQUID,
		STEP_REQ_COMP_CODE,
		STEP_REQ_CPUS,
		STEP_REQ_USER_SEC,
		STEP_REQ_USER_USEC,
		STEP_REQ_SYS_SEC,
		STEP_REQ_SYS_USEC,
		STEP_REQ_MAX_VSIZE,
		STEP_REQ_MAX_VSIZE_TASK,
		STEP_REQ_MAX_VSIZE_NODE,
		STEP_REQ_AVE_VSIZE,
		STEP_REQ_MAX_RSS,
		STEP_REQ_MAX_RSS_TASK,
		STEP_REQ_MAX_RSS_NODE,
		STEP_REQ_AVE_RSS,
		STEP_REQ_MAX_PAGES,
		STEP_REQ_MAX_PAGES_TASK,
		STEP_REQ_MAX_PAGES_NODE,
		STEP_REQ_AVE_PAGES,
		STEP_REQ_MIN_CPU,
		STEP_REQ_MIN_CPU_TASK,
		STEP_REQ_MIN_CPU_NODE,
		STEP_REQ_AVE_CPU,
		STEP_REQ_COUNT
	};

	if(!job_cond)
		goto no_cond;

	/* THIS ASSOCID CHECK ALWAYS NEEDS TO BE FIRST!!!!!!! */
	if(job_cond->associd_list && list_count(job_cond->associd_list)) {
		set = 0;
		xstrfmtcat(extra, ", %s as t3 where (");
		itr = list_iterator_create(job_cond->associd_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			xstrfmtcat(extra, "t1.associd=%s", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
		table_level="t3";
		/* just incase the association is gone */
		if(set) 
			xstrcat(extra, " or ");
		xstrfmtcat(extra, "t3.id is null) and "
			   "(t2.lft between t3.lft and t3.rgt "
			   "or t2.lft is null)");
	}

	if(job_cond->acct_list && list_count(job_cond->acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " and (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(job_cond->acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			xstrfmtcat(extra, "t1.acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(job_cond->groupid_list && list_count(job_cond->groupid_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " and (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(job_cond->groupid_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			xstrfmtcat(extra, "t1.gid=", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(job_cond->userid_list && list_count(job_cond->userid_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " and (");
		else
			xstrcat(extra, " where (");

		itr = list_iterator_create(job_cond->userid_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			xstrfmtcat(extra, "t1.uid='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(job_cond->partition_list && list_count(job_cond->partition_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " and (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(job_cond->partition_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			xstrfmtcat(extra, "t1.partition='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(job_cond->step_list && list_count(job_cond->step_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " and (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(job_cond->step_list);
		while((selected_step = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			xstrfmtcat(extra, "t1.jobid=%u", selected_step->jobid);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(job_cond->usage_start) {
		if(!job_cond->usage_end)
			job_cond->usage_end = time(NULL);

		if(extra)
			xstrcat(extra, " and (");
		else
			xstrcat(extra, " where (");
		xstrfmtcat(extra, 
			   "(t1.eligible < %d and (endtime >= %d "
			   "or endtime = 0)))",
			   job_cond->usage_end, job_cond->usage_start);
	}

	if(job_cond->state_list && list_count(job_cond->state_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " and (");
		else
			xstrcat(extra, " where (");

		itr = list_iterator_create(job_cond->state_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			xstrfmtcat(extra, "t1.state='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	/* we need to put all the associations (t2) stuff together here */
	if(job_cond->cluster_list && list_count(job_cond->cluster_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " and (");
		else
			xstrcat(extra, " where (");

		itr = list_iterator_create(job_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			xstrfmtcat(extra,
				   "(t1.cluster='%s' or %s.cluster='%s')", 
				   object, table_level, object);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

no_cond:	

	xfree(tmp);
	xstrfmtcat(tmp, "%s", job_req_inx[0]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", job_req_inx[i]);
	}
	
	query = xstrdup_printf("select %s from %s as t1 left join %s as t2 "
			       "on t1.associd=t2.id",
			       tmp, job_table, assoc_table);
	xfree(tmp);
	if(extra) {
		xstrcat(query, extra);
		xfree(extra);
	}
	/* Here we want to order them this way in such a way so it is
	   easy to look for duplicates 
	*/
	if(job_cond && !job_cond->duplicates) 
		xstrcat(query, " order by jobid, submit desc");

	debug3("query\n%s", query);
	if(!(result = pgsql_db_query_ret(acct_pgsql_db, query))) {
		xfree(query);
		list_destroy(job_list);
		return NULL;
	}
	xfree(query);

	for (i = 0; i < PQntuples(result); i++) {
		char *id = PQgetvalue(result, i, JOB_REQ_ID);

		curr_id = atoi(PQgetvalue(result, i, JOB_REQ_JOBID));

		if(job_cond && !job_cond->duplicates && curr_id == last_id)
			continue;
		
		last_id = curr_id;

		job = create_jobacct_job_rec();

		job->alloc_cpus = atoi(PQgetvalue(result, i, 
						  JOB_REQ_ALLOC_CPUS));
		job->associd = atoi(PQgetvalue(result, i, JOB_REQ_ASSOCID));
		job->cluster = xstrdup(PQgetvalue(result, i, JOB_REQ_CLUSTER));
		if(job->cluster && !job->cluster[0]) {
			xfree(job->cluster);
			job->cluster = xstrdup(
				PQgetvalue(result, i, JOB_REQ_CLUSTER1));
			if(job->cluster && !job->cluster[0]) 
				xfree(job->cluster);
		}
		job->user =  xstrdup(PQgetvalue(result, i, JOB_REQ_USER_NAME));
		if(!job->user || !job->user[0]) 
			job->uid = atoi(PQgetvalue(result, i, JOB_REQ_UID));

		job->lft = atoi(PQgetvalue(result, i, JOB_REQ_LFT));

		if(!job->lft)
			job->lft = (uint32_t)NO_VAL;

		job->account = xstrdup(PQgetvalue(result, i, JOB_REQ_ACCOUNT));
		if(job->account && !job->account[0]) 
			xfree(job->account);

		job->blockid = xstrdup(PQgetvalue(result, i,
						  JOB_REQ_BLOCKID));
		if(!job->blockid || !job->blockid[0]) 
			xfree(job->blockid);
		job->eligible = atoi(PQgetvalue(result, i, JOB_REQ_SUBMIT));
		job->submit = atoi(PQgetvalue(result, i, JOB_REQ_SUBMIT));
		job->start = atoi(PQgetvalue(result, i, JOB_REQ_START));
		job->end = atoi(PQgetvalue(result, i, JOB_REQ_ENDTIME));
		job->suspended = atoi(PQgetvalue(result, i, JOB_REQ_SUSPENDED));
		if(!job->end) {
			job->elapsed = now - job->start;
		} else {
			job->elapsed = job->end - job->start;
		}
		job->elapsed -= job->suspended;

		if(job_cond && job_cond->usage_start) {
			if(job->start && (job->start < job_cond->usage_start))
				job->start = job_cond->usage_start;

			if(!job->start && job->end)
				job->start = job->end;

			if(!job->end || job->end > job_cond->usage_end) 
				job->end = job_cond->usage_end;

			job->elapsed = job->end - job->start;

			if(atoi(PQgetvalue(result, i, JOB_REQ_SUSPENDED))) {
				PGresult *result2 = NULL;
				int i2=0;
				/* get the suspended time for this job */
				query = xstrdup_printf(
					"select start, end from %s where "
					"(start < %d and (end >= %d "
					"and end = 0)) && id=%s "
					"order by start",
					suspend_table,
					job_cond->usage_end, 
					job_cond->usage_start,
					id);
				
				debug4("query\n%s", query);
				if(!(result2 = pgsql_db_query_ret(
					     acct_pgsql_db, query))) {
					list_destroy(job_list);
					job_list = NULL;
					break;
				}
				xfree(query);
				for (i2 = 0; i2 < PQntuples(result2); i2++) {
					int local_start =
						atoi(PQgetvalue(result, i2, 0));
					int local_end = 
						atoi(PQgetvalue(result, i2, 1));
					
					if(!local_start)
						continue;
					
					if(job->start > local_start)
						local_start = job->start;
					if(job->end < local_end)
						local_end = job->end;
					
					if((local_end - local_start) < 1)
						continue;
					
					job->elapsed -= 
						(local_end - local_start);
					job->suspended += 
						(local_end - local_start);
				}
				PQclear(result2);
			}
		} else {
			job->suspended =
				atoi(PQgetvalue(result, i, JOB_REQ_SUSPENDED));
			if(!job->end) {
				job->elapsed = now - job->start;
			} else {
				job->elapsed = job->end - job->start;
			}
			job->elapsed -= job->suspended;
		}

		job->jobid = curr_id;
		job->jobname = xstrdup(PQgetvalue(result, i, JOB_REQ_NAME));
		job->gid = atoi(PQgetvalue(result, i, JOB_REQ_GID));
		job->exitcode = atoi(PQgetvalue(result, i, JOB_REQ_COMP_CODE));
		job->partition = xstrdup(PQgetvalue(result, i,
						      JOB_REQ_PARTITION));
		job->nodes = xstrdup(PQgetvalue(result, i, JOB_REQ_NODELIST));
		if (!strcmp(job->nodes, "(null)")) {
			xfree(job->nodes);
			job->nodes = xstrdup("(unknown)");
		}

		job->track_steps = atoi(PQgetvalue(result, i,
						   JOB_REQ_TRACKSTEPS));
		job->state = atoi(PQgetvalue(result, i, JOB_REQ_STATE));
		job->priority = atoi(PQgetvalue(result, i, JOB_REQ_PRIORITY));
		job->req_cpus = atoi(PQgetvalue(result, i, JOB_REQ_REQ_CPUS));
		job->requid = atoi(PQgetvalue(result, i, JOB_REQ_KILL_REQUID));
		job->qos = atoi(PQgetvalue(result, i, JOB_REQ_QOS));
		job->show_full = 1;

		list_append(job_list, job);

		if(job_cond && job_cond->step_list && 
		   list_count(job_cond->step_list)) {
			set = 0;
			itr = list_iterator_create(job_cond->step_list);
			while((selected_step = list_next(itr))) {
				if(selected_step->jobid != job->jobid) {
					continue;
				} else if (selected_step->stepid
					   == (uint32_t)NO_VAL) {
					job->show_full = 1;
					break;
				}
				
				if(set) 
					xstrcat(extra, " or ");
				else 
					xstrcat(extra, " and (");
			
				xstrfmtcat(extra, "t1.stepid=%u",
					   selected_step->stepid);
				set = 1;
				job->show_full = 0;
			}
			list_iterator_destroy(itr);
			if(set)
				xstrcat(extra, ")");
		}
		for(j=0; j<STEP_REQ_COUNT; j++) {
			if(j) 
				xstrcat(tmp, ", ");
			xstrcat(tmp, step_req_inx[j]);
		}
		
		query =	xstrdup_printf("select %s from %s t1 where t1.id=%s",
				       tmp, step_table, id);
		xfree(tmp);
		
		if(extra) {
			xstrcat(query, extra);
			xfree(extra);
		}
		
		//info("query = %s", query);
		if(!(step_result = pgsql_db_query_ret(acct_pgsql_db, query))) {
			xfree(query);
			list_destroy(job_list);
			return NULL;
		}
		xfree(query);
		for(j = 0; j < PQntuples(step_result); j++) {
			step = create_jobacct_step_rec();
			step->job_ptr = job;
			if(!job->first_step_ptr)
				job->first_step_ptr = step;
			list_append(job->steps, step);
			step->stepid = atoi(
				PQgetvalue(step_result, j, STEP_REQ_STEPID));
			/* info("got step %u.%u", */
/* 			     job->header.jobnum, step->stepnum); */
			step->state = atoi(
				PQgetvalue(step_result, j, STEP_REQ_STATE));
			step->exitcode = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_COMP_CODE));
			step->ncpus = atoi(
				PQgetvalue(step_result, j, STEP_REQ_CPUS));
			step->start = atoi(
				PQgetvalue(step_result, j, STEP_REQ_START));
			step->end = atoi(
				PQgetvalue(step_result, j, STEP_REQ_ENDTIME));
			/* figure this out by start stop */
			step->suspended = atoi(
				PQgetvalue(step_result, j, STEP_REQ_SUSPENDED));
			if(!step->end) {
				step->elapsed = now - step->start;
			} else {
				step->elapsed = step->end - step->start;
			}
			step->elapsed -= step->suspended;
			step->user_cpu_sec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_USER_SEC));
			step->user_cpu_usec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_USER_USEC));
			step->sys_cpu_sec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_SYS_SEC));
			step->sys_cpu_usec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_SYS_USEC));
			job->tot_cpu_sec += 
				step->tot_cpu_sec += 
				step->user_cpu_sec + step->sys_cpu_sec;
			job->tot_cpu_usec += 
				step->tot_cpu_usec += 
				step->user_cpu_usec + step->sys_cpu_usec;
			step->sacct.max_vsize = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_VSIZE)) * 1024;
			step->sacct.max_vsize_id.taskid = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_VSIZE_TASK));
			step->sacct.ave_vsize = atof(
				PQgetvalue(step_result, j,
					   STEP_REQ_AVE_VSIZE)) * 1024;
			step->sacct.max_rss = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_RSS)) * 1024;
			step->sacct.max_rss_id.taskid = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_RSS_TASK));
			step->sacct.max_rss = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_RSS)) * 1024;
			step->sacct.max_rss_id.taskid =	atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_RSS_TASK));
			step->sacct.ave_rss = atof(
				PQgetvalue(step_result, j,
					   STEP_REQ_AVE_RSS)) * 1024;
			step->sacct.max_pages = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_PAGES));
			step->sacct.max_pages_id.taskid = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_PAGES_TASK));
			step->sacct.ave_pages = atof(
				PQgetvalue(step_result, j,
					   STEP_REQ_AVE_PAGES));
			step->sacct.min_cpu = atof(
				PQgetvalue(step_result, j, STEP_REQ_MIN_CPU));
			step->sacct.min_cpu_id.taskid =	atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MIN_CPU_TASK));
			step->sacct.ave_cpu = atof(
				PQgetvalue(step_result, j, STEP_REQ_AVE_CPU));
			step->stepname = xstrdup(
				PQgetvalue(step_result, j, STEP_REQ_NAME));
			step->nodes = xstrdup(
				PQgetvalue(step_result, j, STEP_REQ_NODELIST));
			step->sacct.max_vsize_id.nodeid = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_VSIZE_NODE));
			step->sacct.max_rss_id.nodeid =	atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_RSS_NODE));
			step->sacct.max_pages_id.nodeid = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_PAGES_NODE));
			step->sacct.min_cpu_id.nodeid =	atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MIN_CPU_NODE));
	
			step->requid = atoi(PQgetvalue(step_result, j,
						       STEP_REQ_KILL_REQUID));
		}
		PQclear(step_result);

		if(!job->track_steps) {
			/* If we don't have track_steps we want to see
			   if we have multiple steps.  If we only have
			   1 step check the job name against the step
			   name in most all cases it will be
			   different.  If it is different print out
			   the step separate.
			*/
			if(list_count(job->steps) > 1) 
				job->track_steps = 1;
			else if(step && step->stepname && job->jobname) {
				if(strcmp(step->stepname, job->jobname))
					job->track_steps = 1;
			}
               }
		/* need to reset here to make the above test valid */
		step = NULL;
	}
	PQclear(result);
	
	return job_list;
}

extern int pgsql_jobacct_process_archive(PGconn *acct_pgsql_db,
					 acct_archive_cond_t *arch_cond)
{
	return SLURM_SUCCESS;
}

extern int pgsql_jobacct_process_archive_load(PGconn *acct_pgsql_db,
					      acct_archive_rec_t *arch_rec)
{	
	return SLURM_SUCCESS;
}

#endif	
