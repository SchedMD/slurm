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

extern void pgsql_jobacct_process_get_jobs(List job_list,
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
	jobacct_header_t header;
	time_t now = time(NULL);

	/* if this changes you will need to edit the corresponding 
	 * enum below also t1 is index_table and t2 is job_table */
	char *job_req_inx[] = {
		"t1.id",
		"t1.jobid",
		"t1.partition",
		"t1.submit",
		"t2.start",
		"t2.endtime",
		"t2.suspended",
		"t1.uid",
		"t1.gid",
		"t1.blockid",
		"t2.name",
		"t2.track_steps",
		"t2.state",
		"t2.priority",
		"t2.cpus",
		"t2.nodelist",
		"t2.account",
		"t2.kill_requid"
	};

	/* if this changes you will need to edit the corresponding 
	 * enum below also t1 is step_table and t2 is step_rusage */
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
		"t2.cpu_sec",
		"t2.cpu_usec",
		"t2.user_sec",
		"t2.user_usec",
		"t2.sys_sec",
		"t2.sys_usec",
		"t2.max_rss",
		"t2.max_ixrss",
		"t2.max_idrss",
		"t2.max_isrss",
		"t2.max_minflt",
		"t2.max_majflt",
		"t2.max_nswap",
		"t2.inblock",
		"t2.outblock",
		"t2.msgsnd",
		"t2.msgrcv",
		"t2.nsignals",
		"t2.nvcsw",
		"t2.nivcsw"
	};
	enum {
		JOB_REQ_ID,
		JOB_REQ_JOBID,
		JOB_REQ_PARTITION,
		JOB_REQ_SUBMIT,
		JOB_REQ_START,
		JOB_REQ_END,
		JOB_REQ_SUSPENDED,
		JOB_REQ_UID,
		JOB_REQ_GID,
		JOB_REQ_BLOCKID,
		JOB_REQ_NAME,
		JOB_REQ_TRACKSTEPS,
		JOB_REQ_STATE,
		JOB_REQ_PRIORITY,
		JOB_REQ_CPUS,
		JOB_REQ_NODELIST,
		JOB_REQ_ACCOUNT,
		JOB_REQ_KILL_REQUID,
		JOB_REQ_COUNT		
	};
	enum {
		STEP_REQ_STEPID,
		STEP_REQ_START,
		STEP_REQ_END,
		STEP_REQ_SUSPENDED,
		STEP_REQ_NAME,
		STEP_REQ_NODELIST,
		STEP_REQ_STATE,
		STEP_REQ_KILL_REQUID,
		STEP_REQ_COMP_CODE,
		STEP_REQ_CPUS,
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
		STEP_REQ_CPU_SEC,
		STEP_REQ_CPU_USEC,
		STEP_REQ_USER_SEC,
		STEP_REQ_USER_USEC,
		STEP_REQ_SYS_SEC,
		STEP_REQ_SYS_USEC,
		STEP_REQ_RSS,
		STEP_REQ_IXRSS,
		STEP_REQ_IDRSS,
		STEP_REQ_ISRSS,
		STEP_REQ_MINFLT,
		STEP_REQ_MAJFLT,
		STEP_REQ_NSWAP,
		STEP_REQ_INBLOCKS,
		STEP_REQ_OUTBLOCKS,
		STEP_REQ_MSGSND,
		STEP_REQ_MSGRCV,
		STEP_REQ_NSIGNALS,
		STEP_REQ_NVCSW,
		STEP_REQ_NIVCSW,
		STEP_REQ_COUNT
	};

	if(selected_steps && list_count(selected_steps)) {
		set = 0;
		xstrcat(extra, " and (");
		itr = list_iterator_create(selected_steps);
		while((selected_step = list_next(itr))) {
			if(set) 
				xstrcat(extra, " or ");
			tmp = xstrdup_printf("t1.jobid=%d",
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
	
	query = xstrdup_printf("select %s from %s t1, %s t2 where t1.id=t2.id",
			       tmp, index_table, job_table);
	xfree(tmp);

	if(extra) {
		xstrcat(query, extra);
		xfree(extra);
	}

	//info("query = %s", query);
	if(!(result =
	     pgsql_db_query_ret(jobacct_pgsql_db, jobacct_db_init, query))) {
		xfree(query);
		return;
	}
	xfree(query);

	for (i = 0; i < PQntuples(result); i++) {
		time_t job_suspended = atoi(PQgetvalue(result, i,
						       JOB_REQ_SUSPENDED));
		char *id = PQgetvalue(result, i, JOB_REQ_ID);
		header.jobnum = atoi(PQgetvalue(result, i, JOB_REQ_JOBID));
		header.partition = xstrdup(PQgetvalue(result, i,
						      JOB_REQ_PARTITION));
		header.job_submit = atoi(PQgetvalue(result, i,
						    JOB_REQ_SUBMIT));
		header.timestamp = atoi(PQgetvalue(result, i, JOB_REQ_START));
		header.uid = atoi(PQgetvalue(result, i, JOB_REQ_UID));
		header.gid = atoi(PQgetvalue(result, i, JOB_REQ_GID));
		header.blockid = xstrdup(PQgetvalue(result, i,
						    JOB_REQ_BLOCKID));

		job = create_jobacct_job_rec(header);
		job->show_full = 1;
		job->status = atoi(PQgetvalue(result, i, JOB_REQ_STATE));
		job->jobname = xstrdup(PQgetvalue(result, i, JOB_REQ_NAME));
		job->track_steps = atoi(PQgetvalue(result, i,
						   JOB_REQ_TRACKSTEPS));
		job->priority = atoi(PQgetvalue(result, i, JOB_REQ_PRIORITY));
		job->ncpus = atoi(PQgetvalue(result, i, JOB_REQ_CPUS));
		job->end = atoi(PQgetvalue(result, i, JOB_REQ_END));
		job->nodes = xstrdup(PQgetvalue(result, i, JOB_REQ_NODELIST));
		if (!strcmp(job->nodes, "(null)")) {
			xfree(job->nodes);
			job->nodes = xstrdup("(unknown)");
		}
		job->account = xstrdup(PQgetvalue(result, i, JOB_REQ_ACCOUNT));
		list_append(job_list, job);

		if(selected_steps && list_count(selected_steps)) {
			set = 0;
			itr = list_iterator_create(selected_steps);
			while((selected_step = list_next(itr))) {
				if(selected_step->jobid != header.jobnum) {
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
			
				tmp = xstrdup_printf("t1.stepid=%d",
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
		
		query =	xstrdup_printf("select %s from %s t1, "
				       "%s t2 where t1.id=t2.id "
				       "and t1.stepid=t2.stepid "
				       "and t1.id=%s",
				       tmp, step_table, rusage_table, id);
		xfree(tmp);
		
		if(extra) {
			xstrcat(query, extra);
			xfree(extra);
		}
		
		//info("query = %s", query);
		if(!(step_result = pgsql_db_query_ret(
			     jobacct_pgsql_db, jobacct_db_init, query))) {
			xfree(query);
			return;
		}
		xfree(query);
		for(j = 0; j < PQntuples(step_result); j++) {
			time_t suspended = 0;
			/* we need to do this here for all the memory
			   locations so we get new memory that will be
			   freed later.
			*/
			header.partition = xstrdup(
				PQgetvalue(result, j, JOB_REQ_PARTITION));
			header.blockid = xstrdup(
				PQgetvalue(result, j, JOB_REQ_BLOCKID));
			header.timestamp = atoi(
				PQgetvalue(step_result, j, STEP_REQ_START));
			/* set start of job if not set */
			if(job->header.timestamp < header.timestamp) {
				job->header.timestamp = header.timestamp;
			}
			step = create_jobacct_step_rec(header);
			list_append(job->steps, step);
			step->stepnum = atoi(
				PQgetvalue(step_result, j, STEP_REQ_STEPID));
			/* info("got step %u.%u", */
/* 			     job->header.jobnum, step->stepnum); */
			step->status = atoi(
				PQgetvalue(step_result, j, STEP_REQ_STATE));
			step->exitcode = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_COMP_CODE));
			step->ntasks = atoi(
				PQgetvalue(step_result, j, STEP_REQ_CPUS));
			step->ncpus = atoi(
				PQgetvalue(step_result, j, STEP_REQ_CPUS));
			step->end = atoi(
				PQgetvalue(step_result, j, STEP_REQ_END));
			/* figure this out by start stop */
			suspended = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_SUSPENDED));
			if(!step->end) {
				step->elapsed = now - step->header.timestamp;
			} else {
				step->elapsed =
					step->end - step->header.timestamp;
			}
			step->elapsed -= suspended;
			step->tot_cpu_sec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_CPU_SEC));
			step->tot_cpu_usec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_CPU_USEC));
			step->rusage.ru_utime.tv_sec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_USER_SEC));
			step->rusage.ru_utime.tv_usec = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_USER_USEC));
			step->rusage.ru_stime.tv_sec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_SYS_SEC));
			step->rusage.ru_stime.tv_usec = atoi(
				PQgetvalue(step_result, j, STEP_REQ_SYS_USEC));
			step->rusage.ru_maxrss = atoi(
				PQgetvalue(step_result, j, STEP_REQ_RSS));
			step->rusage.ru_ixrss = atoi(
				PQgetvalue(step_result, j, STEP_REQ_IXRSS));
			step->rusage.ru_idrss = atoi(
				PQgetvalue(step_result, j, STEP_REQ_IDRSS));
			step->rusage.ru_isrss = atoi(
				PQgetvalue(step_result, j, STEP_REQ_ISRSS));
			step->rusage.ru_minflt = atoi(
				PQgetvalue(step_result, j, STEP_REQ_MINFLT));
			step->rusage.ru_majflt = atoi(
				PQgetvalue(step_result, j, STEP_REQ_MAJFLT));
			step->rusage.ru_nswap = atoi(
				PQgetvalue(step_result, j, STEP_REQ_NSWAP));
			step->rusage.ru_inblock = atoi(
				PQgetvalue(step_result, j, STEP_REQ_INBLOCKS));
			step->rusage.ru_oublock = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_OUTBLOCKS));
			step->rusage.ru_msgsnd = atoi(
				PQgetvalue(step_result, j, STEP_REQ_MSGSND));
			step->rusage.ru_msgrcv = atoi(
				PQgetvalue(step_result, j, STEP_REQ_MSGRCV));
			step->rusage.ru_nsignals = atoi(
				PQgetvalue(step_result, j, STEP_REQ_NSIGNALS));
			step->rusage.ru_nvcsw =	atoi(
				PQgetvalue(step_result, j, STEP_REQ_NVCSW));
			step->rusage.ru_nivcsw = atoi(
				PQgetvalue(step_result, j, STEP_REQ_NIVCSW));
			step->sacct.max_vsize =	atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_VSIZE)) * 1024;
			step->sacct.max_vsize_id.taskid = atoi(
				PQgetvalue(step_result, j,
					   STEP_REQ_MAX_VSIZE_TASK));
			step->sacct.ave_vsize =	atof(
				PQgetvalue(step_result, j,
					   STEP_REQ_AVE_VSIZE)) * 1024;
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

		if(!job->end) {
			job->elapsed = now - job->header.timestamp;
		} else {
			job->elapsed = job->end - job->header.timestamp;
		}
		job->elapsed -= job_suspended;
	}
	PQclear(result);
	if (params->opt_fdump) {
		_do_fdump(job_list);
	}
	return;
}

extern void pgsql_jobacct_process_archive(List selected_parts,
					  sacct_parameters_t *params)
{
	return;
}

#endif	
