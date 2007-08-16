/*****************************************************************************\
 *  pgsql_jobacct.c - functions the pgsql jobacct database.
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

#include "pgsql_jobacct_process.h"

#ifdef HAVE_PGSQL

#define DEFAULT_JOBACCT_DB "slurm_jobacct_db"

PGconn *jobacct_pgsql_db = NULL;
int jobacct_db_init = 0;

char *index_table = "index_table";
char *job_table = "job_table";
char *step_table = "step_table";
char *rusage_table = "rusage_table";

static int _pgsql_jobacct_check_tables(char *user)
{
	database_field_t index_table_fields[] = {
		{ "id", "serial" },
		{ "jobid ", "integer not null" },
		{ "partition", "text not null" },
		{ "submit", "bigint not null" },
		{ "uid", "smallint not null" },
		{ "gid", "smallint not null" },
		{ "blockid", "text" },
		{ NULL, NULL}		
	};

	database_field_t job_table_fields[] = {
		{ "id", "int not null" },
		{ "start", "bigint default 0" },
		{ "endtime", "bigint default 0" },
		{ "suspended", "bigint default 0" },
		{ "name", "text not null" }, 
		{ "track_steps", "smallint not null" },
		{ "state", "smallint not null" }, 
		{ "priority", "bigint not null" },
		{ "cpus", "integer not null" }, 
		{ "nodelist", "text" },
		{ "account", "text" },
		{ "kill_requid", "smallint" },
		{ NULL, NULL}
	};

	database_field_t step_table_fields[] = {
		{ "id", "int not null" },
		{ "stepid", "smallint not null" },
		{ "start", "bigint default 0" },
		{ "endtime", "bigint default 0" },
		{ "suspended", "bigint default 0" },
		{ "name", "text not null" },
		{ "nodelist", "text not null" },
		{ "state", "smallint not null" },
		{ "kill_requid", "smallint default -1" },
		{ "comp_code", "smallint default 0" },
		{ "cpus", "int not null" },
		{ "max_vsize", "integer default 0" },
		{ "max_vsize_task", "smallint default 0" },
		{ "max_vsize_node", "integer default 0" },
		{ "ave_vsize", "float default 0.0" },
		{ "max_rss", "integer default 0" },
		{ "max_rss_task", "smallint default 0" },
		{ "max_rss_node", "integer default 0" },
		{ "ave_rss", "float default 0.0" },
		{ "max_pages", "integer default 0" },
		{ "max_pages_task", "smallint default 0" },
		{ "max_pages_node", "integer default 0" },
		{ "ave_pages", "float default 0.0" },
		{ "min_cpu", "integer default 0" },
		{ "min_cpu_task", "smallint default 0" },
		{ "min_cpu_node", "integer default 0" },
		{ "ave_cpu", "float default 0.0" },
		{ NULL, NULL}
	};

	database_field_t step_rusage_fields[] = {
		{ "id", "int not null" },
		{ "stepid", "smallint not null" },
		{ "cpu_sec", "bigint default 0" },
		{ "cpu_usec", "bigint default 0" },
		{ "user_sec", "bigint default 0" },
		{ "user_usec", "bigint default 0" },
		{ "sys_sec", "bigint default 0" },
		{ "sys_usec", "bigint default 0" },
		{ "max_rss", "bigint default 0" },
		{ "max_ixrss", "bigint default 0" },
		{ "max_idrss", "bigint default 0" },
		{ "max_isrss", "bigint default 0" },
		{ "max_minflt", "bigint default 0" },
		{ "max_majflt", "bigint default 0" },
		{ "max_nswap", "bigint default 0" },
		{ "inblock", "bigint default 0" },
		{ "outblock", "bigint default 0" },
		{ "msgsnd", "bigint default 0" },
		{ "msgrcv", "bigint default 0" },
		{ "nsignals", "bigint default 0" },
		{ "nvcsw", "bigint default 0" },
		{ "nivcsw", "bigint default 0" },
		{ NULL, NULL}
	};

	int i = 0, index_found = 0, job_found = 0;
	int step_found = 0, rusage_found = 0;
	PGresult *result = NULL;
	char *query = xstrdup_printf("select tablename from pg_tables "
				     "where tableowner='%s' "
				     "and tablename !~ '^pg_+'", user);

	if(!(result =
	     pgsql_db_query_ret(jobacct_pgsql_db, jobacct_db_init, query))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	for (i = 0; i < PQntuples(result); i++) {
		if(!index_found && 
		   !strcmp(index_table, PQgetvalue(result, i, 0))) 
			index_found = 1;
		else if(!job_found &&
			!strcmp(job_table, PQgetvalue(result, i, 0))) 
			job_found = 1;
		else if(!step_found &&
			!strcmp(step_table, PQgetvalue(result, i, 0))) 
			step_found = 1;
		else if(!rusage_found &&
			!strcmp(rusage_table, PQgetvalue(result, i, 0))) 
			rusage_found = 1;
	}
	PQclear(result);

	if(!index_found)
		if(pgsql_db_create_table(jobacct_pgsql_db, jobacct_db_init, 
					 index_table, index_table_fields,
					 ", primary key (id))") == SLURM_ERROR)
			return SLURM_ERROR;
	
	if(!job_found)
		if(pgsql_db_create_table(jobacct_pgsql_db, jobacct_db_init, 
					 job_table, job_table_fields,
					 ")") == SLURM_ERROR)
			return SLURM_ERROR;

	if(!step_found)
		if(pgsql_db_create_table(jobacct_pgsql_db, jobacct_db_init, 
					 step_table, step_table_fields,
					 ")") == SLURM_ERROR)
			return SLURM_ERROR;

	if(!rusage_found)
		if(pgsql_db_create_table(jobacct_pgsql_db, jobacct_db_init, 
					 rusage_table, step_rusage_fields,
					 ")") == SLURM_ERROR)
			return SLURM_ERROR;


	return SLURM_SUCCESS;
}


extern int pgsql_jobacct_init(char *location)
{
	pgsql_db_info_t *db_info = create_pgsql_db_info();
	int rc = SLURM_SUCCESS;
	char *db_name = NULL;

	if(jobacct_db_init) 
		return SLURM_ERROR;
	
	if(!location)
		db_name = DEFAULT_JOBACCT_DB;
	else {
		int i = 0;
		while(location[i]) {
			if(location[i] == '.' || location[i] == '/') {
				debug("%s doesn't look like a database "
				      "name using %s",
				      location, DEFAULT_JOBACCT_DB);
				break;
			}
			i++;
		}
		if(location[i]) 
			db_name = DEFAULT_JOBACCT_DB;
		else
			db_name = location;
	}
	debug2("pgsql_connect() called for db %s", db_name);
	
	pgsql_get_db_connection(&jobacct_pgsql_db, db_name, db_info,
				&jobacct_db_init);

	rc = _pgsql_jobacct_check_tables(db_info->user);

	destroy_pgsql_db_info(db_info);

	if(rc == SLURM_SUCCESS)
		debug("Database init finished");
	else
		error("Database init failed");
	return rc;
}

extern int pgsql_jobacct_fini()
{
	if (jobacct_pgsql_db) {
		PQfinish(jobacct_pgsql_db);
		jobacct_pgsql_db = NULL;
	}
	jobacct_db_init = 0;
	return SLURM_SUCCESS;
}

extern int pgsql_jobacct_job_start(struct job_record *job_ptr)
{
	int	i,
		ncpus=0,
		rc=SLURM_SUCCESS;
	char	*jname, *account, *nodes;
	long	priority;
	int track_steps = 0;
	char *block_id = NULL;
	char query[1024];
	int reinit = 0;

	if(!jobacct_pgsql_db) {
		char *loc = slurm_get_jobacct_loc();
		if(pgsql_jobacct_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}

	debug2("pgsql_jobacct_job_start() called");
	for (i=0; i < job_ptr->num_cpu_groups; i++)
		ncpus += (job_ptr->cpus_per_node[i])
			* (job_ptr->cpu_count_reps[i]);
	priority = (job_ptr->priority == NO_VAL) ?
		-1L : (long) job_ptr->priority;

	if (job_ptr->name && job_ptr->name[0]) {
		jname = job_ptr->name;
	} else {
		jname = "allocation";
		track_steps = 1;
	}

	if (job_ptr->account && job_ptr->account[0])
		account = job_ptr->account;
	else
		account = "(null)";
	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "(null)";

	if(job_ptr->batch_flag)
		track_steps = 1;
#ifdef HAVE_BG
	select_g_get_jobinfo(job_ptr->select_jobinfo, 
			     SELECT_DATA_BLOCK_ID, 
			     &block_id);
		
#endif
	if(!block_id)
		block_id = xstrdup("-");

	job_ptr->requid = -1; /* force to -1 for sacct to know this
			       * hasn't been set yet */
	snprintf(query, sizeof(query),
		 "insert into %s (jobid, partition, submit, uid, gid, "
		 "blockid) values (%u, '%s', %u, %d, %d, '%s')",
		 index_table, job_ptr->job_id, job_ptr->partition,
		 (int)job_ptr->details->submit_time, job_ptr->user_id,
		 job_ptr->group_id, block_id);
	xfree(block_id);

try_again:
	if((job_ptr->db_index = pgsql_insert_ret_id(
		    jobacct_pgsql_db, jobacct_db_init, 
		    "index_table_id_seq", query))) {
		snprintf(query, sizeof(query),
			 "insert into %s (id, start, name, track_steps, "
			 "state, priority, cpus, nodelist, account) "
			 "values (%d, %u, '%s', %d, %d, %ld, %u, '%s', '%s')",
			 job_table, job_ptr->db_index, 
			 (int)job_ptr->start_time,
			 jname, track_steps,
			 job_ptr->job_state & (~JOB_COMPLETING),
			 priority, job_ptr->num_procs,
			 nodes, account);
		rc = pgsql_db_query(jobacct_pgsql_db, jobacct_db_init, query);
	} else if(!reinit) {
		char *loc = slurm_get_jobacct_loc();
		error("It looks like the database has gone "
		      "away trying to reconnect");
		pgsql_jobacct_fini();
		pgsql_jobacct_init(loc);
		xfree(loc);
		reinit = 1;
		goto try_again;
	} else
		rc = SLURM_ERROR;
	
	return rc;
}

extern int pgsql_jobacct_job_complete(struct job_record *job_ptr)
{
	char query[1024];
	char	*account, *nodes;
	int rc=SLURM_SUCCESS;
	
	if(!jobacct_pgsql_db) {
		char *loc = slurm_get_jobacct_loc();
		if(pgsql_jobacct_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}
	
	debug2("pgsql_jobacct_job_complete() called");
	if (job_ptr->end_time == 0) {
		debug("pgsql_jobacct: job %u never started", job_ptr->job_id);
		return SLURM_ERROR;
	}	
	
	if (job_ptr->account && job_ptr->account[0])
		account = job_ptr->account;
	else
		account = "(null)";
	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "(null)";

	if(job_ptr->db_index) {
		snprintf(query, sizeof(query),
			 "update %s set start=%u, endtime=%u, state=%d, "
			 "nodelist='%s', account='%s', "
			 "kill_requid=%d where id=%u",
			 job_table, (int)job_ptr->start_time,
			 (int)job_ptr->end_time, 
			 job_ptr->job_state & (~JOB_COMPLETING),
			 nodes, account,
			 job_ptr->requid, job_ptr->db_index);
		rc = pgsql_db_query(jobacct_pgsql_db, jobacct_db_init, query);
	} else 
		rc = SLURM_ERROR;

	return  rc;

}

extern int pgsql_jobacct_step_start(struct step_record *step_ptr)
{
	int cpus = 0;
	int rc=SLURM_SUCCESS;
	char node_list[BUFFER_SIZE];
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	char query[1024];
	
	if(!jobacct_pgsql_db) {
		char *loc = slurm_get_jobacct_loc();
		if(pgsql_jobacct_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}

#ifdef HAVE_BG
	cpus = step_ptr->job_ptr->num_procs;
	select_g_get_jobinfo(step_ptr->job_ptr->select_jobinfo, 
			     SELECT_DATA_IONODES, 
			     &ionodes);
	if(ionodes) {
		snprintf(node_list, BUFFER_SIZE, 
			 "%s[%s]", step_ptr->job_ptr->nodes, ionodes);
		xfree(ionodes);
	} else
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
	
#else
	if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		cpus = step_ptr->job_ptr->num_procs;
		snprintf(node_list, BUFFER_SIZE, "%s", step_ptr->job_ptr->nodes);
	} else {
		cpus = step_ptr->step_layout->task_cnt;
		snprintf(node_list, BUFFER_SIZE, "%s", 
			 step_ptr->step_layout->node_list);
	}
#endif
	step_ptr->job_ptr->requid = -1; /* force to -1 for sacct to know this
					 * hasn't been set yet  */

	if(step_ptr->job_ptr->db_index) {
		snprintf(query, sizeof(query),
			 "insert into %s (id, stepid, start, name, state, "
			 "cpus, nodelist, kill_requid) "
			 "values (%d, %u, %u, '%s', %d, %u, '%s', %d)",
			 step_table, step_ptr->job_ptr->db_index,
			 step_ptr->step_id, 
			 (int)step_ptr->start_time, step_ptr->name,
			 JOB_RUNNING, cpus, node_list, 
			 step_ptr->job_ptr->requid);
		rc = pgsql_db_query(jobacct_pgsql_db, jobacct_db_init, query);
		if(rc != SLURM_ERROR) {
			snprintf(query, sizeof(query),
				 "insert into %s (id, stepid) values (%d, %u)",
				 rusage_table, step_ptr->job_ptr->db_index,
				 step_ptr->step_id);
			rc = pgsql_db_query(jobacct_pgsql_db,
					    jobacct_db_init, query);
		}	  
	} else 
		rc = SLURM_ERROR;
		 
	return rc;
}

extern int pgsql_jobacct_step_complete(struct step_record *step_ptr)
{
	time_t now;
	int elapsed;
	int comp_status;
	int cpus = 0;
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	float ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	float ave_cpu = 0, ave_cpu2 = 0;
	char *account;
	char query[1024];
	int rc =SLURM_SUCCESS;
	
	if(!jobacct_pgsql_db) {
		char *loc = slurm_get_jobacct_loc();
		if(pgsql_jobacct_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}
	
	now = time(NULL);
	
	if ((elapsed=now-step_ptr->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */
	if (step_ptr->exit_code)
		comp_status = JOB_FAILED;
	else
		comp_status = JOB_COMPLETE;

#ifdef HAVE_BG
	cpus = step_ptr->job_ptr->num_procs;
	
#else
	if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
		cpus = step_ptr->job_ptr->num_procs;
	else 
		cpus = step_ptr->step_layout->task_cnt;
#endif
	/* figure out the ave of the totals sent */
	if(cpus > 0) {
		ave_vsize = jobacct->tot_vsize;
		ave_vsize /= cpus;
		ave_rss = jobacct->tot_rss;
		ave_rss /= cpus;
		ave_pages = jobacct->tot_pages;
		ave_pages /= cpus;
		ave_cpu = jobacct->tot_cpu;
		ave_cpu /= cpus;	
		ave_cpu /= 100;
	}
 
	if(jobacct->min_cpu != (uint32_t)NO_VAL) {
		ave_cpu2 = jobacct->min_cpu;
		ave_cpu2 /= 100;
	}

	if (step_ptr->job_ptr->account && step_ptr->job_ptr->account[0])
		account = step_ptr->job_ptr->account;
	else
		account = "(null)";

	if(step_ptr->job_ptr->db_index) {
		snprintf(query, sizeof(query),
			 "update %s set endtime=%u, state=%d, "
			 "kill_requid=%d, "
			 "max_vsize=%u, max_vsize_task=%u, "
			 "max_vsize_node=%u, ave_vsize=%.2f, "
			 "max_rss=%u, max_rss_task=%u, "
			 "max_rss_node=%u, ave_rss=%.2f, "
			 "max_pages=%u, max_pages_task=%u, "
			 "max_pages_node=%u, ave_pages=%.2f, "
			 "min_cpu=%.2f, min_cpu_task=%u, "
			 "min_cpu_node=%u, ave_cpu=%.2f "
			 "where id=%u and stepid=%u",
			 step_table, (int)now,
			 comp_status,
			 step_ptr->job_ptr->requid, 
			 jobacct->max_vsize,	/* max vsize */
			 jobacct->max_vsize_id.taskid,	/* max vsize task */
			 jobacct->max_vsize_id.nodeid,	/* max vsize node */
			 ave_vsize,	/* ave vsize */
			 jobacct->max_rss,	/* max vsize */
			 jobacct->max_rss_id.taskid,	/* max rss task */
			 jobacct->max_rss_id.nodeid,	/* max rss node */
			 ave_rss,	/* ave rss */
			 jobacct->max_pages,	/* max pages */
			 jobacct->max_pages_id.taskid,	/* max pages task */
			 jobacct->max_pages_id.nodeid,	/* max pages node */
			 ave_pages,	/* ave pages */
			 ave_cpu2,	/* min cpu */
			 jobacct->min_cpu_id.taskid,	/* min cpu task */
			 jobacct->min_cpu_id.nodeid,	/* min cpu node */
			 ave_cpu,	/* ave cpu */
			 step_ptr->job_ptr->db_index, step_ptr->step_id);
		rc = pgsql_db_query(jobacct_pgsql_db, jobacct_db_init, query);
		if(rc != SLURM_ERROR) {
			snprintf(query, sizeof(query),
				 "update %s set id=%u, stepid=%u, "
				 "cpu_sec=%ld, cpu_usec=%ld, "
				 "user_sec=%ld, user_usec=%ld, "
				 "sys_sec=%ld, sys_usec=%ld, "
				 "max_rss=%ld, max_ixrss=%ld, max_idrss=%ld, "
				 "max_isrss=%ld, max_minflt=%ld, "
				 "max_majflt=%ld, max_nswap=%ld, "
				 "inblock=%ld, outblock=%ld, msgsnd=%ld, "
				 "msgrcv=%ld, nsignals=%ld, "
				 "nvcsw=%ld, nivcsw=%ld "
				 "where id=%u and stepid=%u",
				 rusage_table, step_ptr->job_ptr->db_index,
				 step_ptr->step_id,
				 /* total cputime seconds */
				 jobacct->rusage.ru_utime.tv_sec	
				 + jobacct->rusage.ru_stime.tv_sec,
				 /* total cputime seconds */
				 jobacct->rusage.ru_utime.tv_usec	
				 + jobacct->rusage.ru_stime.tv_usec,
				 /* user seconds */
				 jobacct->rusage.ru_utime.tv_sec,	
				 /* user microseconds */
				 jobacct->rusage.ru_utime.tv_usec,
				 /* system seconds */
				 jobacct->rusage.ru_stime.tv_sec,
				 /* system microsecs */
				 jobacct->rusage.ru_stime.tv_usec,
				 /* max rss */
				 jobacct->rusage.ru_maxrss,
				 /* max ixrss */
				 jobacct->rusage.ru_ixrss,
				 /* max idrss */
				 jobacct->rusage.ru_idrss,	
				 /* max isrss */
				 jobacct->rusage.ru_isrss,
				 /* max minflt */
				 jobacct->rusage.ru_minflt,
				 /* max majflt */
				 jobacct->rusage.ru_majflt,
				 /* max nswap */
				 jobacct->rusage.ru_nswap,
				 /* total inblock */
				 jobacct->rusage.ru_inblock,
				 /* total outblock */
				 jobacct->rusage.ru_oublock,
				 /* total msgsnd */
				 jobacct->rusage.ru_msgsnd,
				 /* total msgrcv */
				 jobacct->rusage.ru_msgrcv,
				 /* total nsignals */
				 jobacct->rusage.ru_nsignals,
				 /* total nvcsw */
				 jobacct->rusage.ru_nvcsw,
				 /* total nivcsw */
				 jobacct->rusage.ru_nivcsw,
				 step_ptr->job_ptr->db_index,
				 step_ptr->step_id);
			
			rc = pgsql_db_query(jobacct_pgsql_db, jobacct_db_init,
					    query);
		}
	} else
		rc = SLURM_ERROR;
		 
	return rc;
}

extern int pgsql_jobacct_suspend(struct job_record *job_ptr)
{
	char query[1024];
	int rc = SLURM_SUCCESS;
	
	if(!jobacct_pgsql_db) {
		char *loc = slurm_get_jobacct_loc();
		if(pgsql_jobacct_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}
	
	if(job_ptr->db_index) {
		snprintf(query, sizeof(query),
			 "update %s set suspended=%u-suspended, state=%d "
			 "where id=%u",
			 job_table, (int)job_ptr->suspend_time, 
			 job_ptr->job_state & (~JOB_COMPLETING),
			 job_ptr->db_index);
		rc = pgsql_db_query(jobacct_pgsql_db, jobacct_db_init, query);
		if(rc != SLURM_ERROR) {
			snprintf(query, sizeof(query),
				 "update %s set suspended=%u-suspended, "
				 "state=%d where id=%u and endtime=0",
				 step_table, (int)job_ptr->suspend_time, 
				 job_ptr->job_state, job_ptr->db_index);
			rc = pgsql_db_query(jobacct_pgsql_db, jobacct_db_init,
					    query);			
		}
	} else
		rc = SLURM_ERROR;
	
	return rc;
}

/* 
 * get info from the database 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern void pgsql_jobacct_get_jobs(List job_list,
				   List selected_steps, List selected_parts,
				   void *params)
{
	if(!jobacct_pgsql_db) {
		char *loc = slurm_get_jobacct_loc();
		if(pgsql_jobacct_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return;
		}
		xfree(loc);
	}

	pgsql_jobacct_process_get_jobs(job_list,
				       selected_steps, selected_parts,
				       params);	
	return;
}

/* 
 * expire old info from the database 
 */
extern void pgsql_jobacct_archive(List selected_parts, void *params)
{
	if(!jobacct_pgsql_db) {
		char *loc = slurm_get_jobacct_loc();
		if(pgsql_jobacct_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return;
		}
		xfree(loc);
	}

	pgsql_jobacct_process_archive(selected_parts, params);
	return;
}
#endif
