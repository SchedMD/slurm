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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <stdlib.h>
#include "pgsql_jobacct_process.h"

#ifdef HAVE_PGSQL
static void _do_fdump(List job_list)
{
	info("fdump option not applicable from pgsql plugin");
	return;
}

extern List pgsql_jobacct_process_get_jobs(PGconn *acct_pgsql_db,
					   List selected_steps,
					   List selected_parts,
					   sacct_parameters_t *params)
{

	char *query = NULL;	
	char *extra = NULL;	
	char *tmp = NULL;	
	char *selected_part = NULL;
	jobacct_selected_step_t *selected_step = NULL;
	ListIterator itr = NULL;
	int set = 0;
	PGresult *result = NULL, *step_result = NULL;
	int i, j;
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

	if(selected_steps && list_count(selected_steps)) {
		set = 0;
		xstrcat(extra, " and (");
		itr = list_iterator_create(selected_steps);
		while((selected_step = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			tmp = xstrdup_printf("t1.jobid=%u",
					      selected_step->jobid);
			xstrcat(extra, tmp);
			set = 1;
			xfree(tmp);
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(selected_parts && list_count(selected_parts)) {
		set = 0;
		xstrcat(extra, " and (");
		itr = list_iterator_create(selected_parts);
		while((selected_part = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			tmp = xstrdup_printf("t1.partition='%s'",
					      selected_part);
			xstrcat(extra, tmp);
			set = 1;
			xfree(tmp);
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	for(i=0; i<JOB_REQ_COUNT; i++) {
		if(i) 
			xstrcat(tmp, ", ");
		xstrcat(tmp, job_req_inx[i]);
	}
	
	query = xstrdup_printf("select %s from %s t1",
			       tmp, job_table);
	xfree(tmp);

	if(extra) {
		xstrcat(query, extra);
		xfree(extra);
	}

	//info("query = %s", query);
	if(!(result = pgsql_db_query_ret(acct_pgsql_db, query))) {
		xfree(query);
		list_destroy(job_list);
		return NULL;
	}
	xfree(query);

	for (i = 0; i < PQntuples(result); i++) {
		char *id = PQgetvalue(result, i, JOB_REQ_ID);
		acct_association_rec_t account_rec;
		memset(&account_rec, 0, sizeof(acct_association_rec_t));
		job = create_jobacct_job_rec();

		job->alloc_cpus = atoi(PQgetvalue(result, i, 
						  JOB_REQ_ALLOC_CPUS));
		job->associd = atoi(PQgetvalue(result, i, JOB_REQ_ASSOCID));
		account_rec.id = job->associd;
		assoc_mgr_fill_in_assoc(acct_pgsql_db, &account_rec, 0, NULL);
		if(account_rec.cluster) {
			if(params->opt_cluster &&
			   strcmp(params->opt_cluster, account_rec.cluster)) {
				destroy_jobacct_job_rec(job);
				job = NULL;
				continue;
			}
			job->cluster = xstrdup(account_rec.cluster);
		}
		if(account_rec.user) 
			job->user = xstrdup(account_rec.user);
		else 
			job->uid = atoi(PQgetvalue(result, i, JOB_REQ_UID));
		if(account_rec.acct) 
			job->account = xstrdup(account_rec.acct);
		else
			job->account = xstrdup(PQgetvalue(result, i,
							  JOB_REQ_ACCOUNT));
		job->blockid = xstrdup(PQgetvalue(result, i,
						    JOB_REQ_BLOCKID));
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

		job->jobid = atoi(PQgetvalue(result, i, JOB_REQ_JOBID));
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

		if(selected_steps && list_count(selected_steps)) {
			set = 0;
			itr = list_iterator_create(selected_steps);
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
			
				tmp = xstrdup_printf("t1.stepid=%u",
						     selected_step->stepid);
				xstrcat(extra, tmp);
				set = 1;
				xfree(tmp);
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
			step->jobid = job->jobid;
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
				PQgetvalue(step_result, j, JOB_REQ_START));
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

		if(list_count(job->steps) > 1)
			job->track_steps = 1;
	}
	PQclear(result);
	
	if (params && params->opt_fdump) 
		_do_fdump(job_list);
	
	return job_list;
}

extern void pgsql_jobacct_process_archive(PGconn *acct_pgsql_db,
					  List selected_parts,
					  sacct_parameters_t *params)
{
	return;
}

#endif	
