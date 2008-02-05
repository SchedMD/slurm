/*****************************************************************************\
 *  pgsql_jobcomp_process.c - functions the processing of
 *                               information from the pgsql jobcomp
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
#include "src/common/parse_time.h"
#include "src/common/xstring.h"
#include "pgsql_jobcomp_process.h"

#ifdef HAVE_PGSQL
static void _do_fdump(PGresult *result, int lc)
{
	int i = 0;
	printf("\n------- Line %d -------\n", lc);	
	while(jobcomp_table_fields[i].name) {
		printf("%12s: %s\n",  jobcomp_table_fields[i].name, 
		       PQgetvalue(result, lc, i));
		i++;
	}

	return;
}

extern List pgsql_jobcomp_process_get_jobs(List selected_steps,
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
	PGresult *result = NULL;
	int i;
	jobcomp_job_rec_t *job = NULL;
	char time_str[32];
	time_t temp_time;
	List job_list = NULL;

	if(selected_steps && list_count(selected_steps)) {
		set = 0;
		xstrcat(extra, " where (");
		itr = list_iterator_create(selected_steps);
		while((selected_step = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			tmp = xstrdup_printf("jobid=%d",
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
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		
		itr = list_iterator_create(selected_parts);
		while((selected_part = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			tmp = xstrdup_printf("partition='%s'",
					      selected_part);
			xstrcat(extra, tmp);
			set = 1;
			xfree(tmp);
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	i = 0;
	while(jobcomp_table_fields[i].name) {
		if(i) 
			xstrcat(tmp, ", ");
		xstrcat(tmp, jobcomp_table_fields[i].name);
		i++;
	}
	
	query = xstrdup_printf("select %s from %s", tmp, jobcomp_table);
	xfree(tmp);

	if(extra) {
		xstrcat(query, extra);
		xfree(extra);
	}

	//info("query = %s", query);
	if(!(result =
	     pgsql_db_query_ret(jobcomp_pgsql_db, query))) {
		xfree(query);
		return NULL;
	}
	xfree(query);
	
	job_list = list_create(jobcomp_destroy_job);
	for (i = 0; i < PQntuples(result); i++) {
		
		if (params->opt_fdump) {
			_do_fdump(result, i);
			continue;
		}
		job = xmalloc(sizeof(jobcomp_job_rec_t));
		if(PQgetvalue(result, i, JOBCOMP_REQ_JOBID))
			job->jobid = 
				atoi(PQgetvalue(result, i, JOBCOMP_REQ_JOBID));
		job->partition =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_PARTITION));
		temp_time = atoi(PQgetvalue(result, i, JOBCOMP_REQ_STARTTIME));
		slurm_make_time_str(&temp_time, 
				    time_str, 
				    sizeof(time_str));
		job->start_time = xstrdup(time_str);
		
		temp_time = atoi(PQgetvalue(result, i, JOBCOMP_REQ_ENDTIME));
		slurm_make_time_str(&temp_time, 
				    time_str, 
				    sizeof(time_str));
		job->end_time = xstrdup(time_str);
		
		if(PQgetvalue(result, i, JOBCOMP_REQ_UID))
			job->uid =
				atoi(PQgetvalue(result, i, JOBCOMP_REQ_UID));
		job->uid_name =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_USER_NAME));
		if(PQgetvalue(result, i, JOBCOMP_REQ_GID))
			job->gid =
				atoi(PQgetvalue(result, i, JOBCOMP_REQ_GID));
		job->gid_name = 
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_GROUP_NAME));
		job->jobname =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_NAME));
		job->nodelist =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_NODELIST));
		if(PQgetvalue(result, i, JOBCOMP_REQ_NODECNT))
			job->node_cnt =
				atoi(PQgetvalue(result, i, JOBCOMP_REQ_NODECNT));
		if(PQgetvalue(result, i, JOBCOMP_REQ_STATE)) {
			int j = atoi(PQgetvalue(result, i, JOBCOMP_REQ_STATE));
			job->state = xstrdup(job_state_string(j));
		}
		job->timelimit =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_TIMELIMIT));
#ifdef HAVE_BG
		if(PQgetvalue(result, i, JOBCOMP_REQ_MAXPROCS))
			job->max_procs =
				atoi(PQgetvalue(result, i, 
						JOBCOMP_REQ_MAXPROCS));
		job->blockid =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_BLOCKID));
		job->connection =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_CONNECTION));
		job->reboot =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_REBOOT));
		job->rotate =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_ROTATE));
		job->geo = 
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_GEOMETRY));
		job->bg_start_point =
			xstrdup(PQgetvalue(result, i, JOBCOMP_REQ_START));
#endif
		list_append(job_list, job);

	}
	
	PQclear(result);
	return job_list;
}

extern void pgsql_jobcomp_process_archive(List selected_parts,
					  sacct_parameters_t *params)
{
	return;
}

#endif	
