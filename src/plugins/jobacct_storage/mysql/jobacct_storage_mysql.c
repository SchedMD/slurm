/*****************************************************************************\
 *  storage_mysql.c - Store/Get all information in a mysql storage.
 *
 *  $Id: storage_mysql.c 10893 2007-01-29 21:53:48Z da $
 *****************************************************************************
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
\*****************************************************************************/

#include "mysql_jobacct_process.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] = "Job accounting storage MYSQL plugin";
const char plugin_type[] = "jobacct_storage/mysql";
const uint32_t plugin_version = 100;

#ifdef HAVE_MYSQL

#define DEFAULT_JOBACCT_DB "slurm_jobacct_db"

MYSQL *jobacct_mysql_db = NULL;

char *job_index = "index_table";
char *job_table = "job_table";
char *step_table = "step_table";
char *rusage_table = "rusage_table";

static mysql_db_info_t *_mysql_jobacct_create_db_info()
{
	mysql_db_info_t *db_info = xmalloc(sizeof(mysql_db_info_t));
	db_info->port = slurm_get_jobacct_storage_port();
	if(!db_info->port) 
		db_info->port = 3306;
	db_info->host = slurm_get_jobacct_storage_host();	
	db_info->user = slurm_get_jobacct_storage_user();	
	db_info->pass = slurm_get_jobacct_storage_pass();	
	return db_info;
}

static int _mysql_jobacct_check_tables()
{
	storage_field_t job_index_fields[] = {
		{ "id", "int not null auto_increment" },
		{ "jobid ", "mediumint unsigned not null" },
		{ "partition", "tinytext not null" },
		{ "submit", "int unsigned not null" },
		{ "uid", "smallint unsigned not null" },
		{ "gid", "smallint unsigned not null" },
		{ "blockid", "tinytext" },
		{ NULL, NULL}		
	};

	storage_field_t job_table_fields[] = {
		{ "id", "int not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "name", "tinytext not null" }, 
		{ "track_steps", "tinyint not null" },
		{ "state", "smallint not null" }, 
		{ "comp_code", "int default 0 not null" },
		{ "priority", "int unsigned not null" },
		{ "cpus", "mediumint unsigned not null" }, 
		{ "nodelist", "text" },
		{ "account", "tinytext" },
		{ "kill_requid", "smallint default -1 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields[] = {
		{ "id", "int not null" },
		{ "stepid", "smallint not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "name", "text not null" },
		{ "nodelist", "text not null" },
		{ "state", "smallint not null" },
		{ "kill_requid", "smallint default -1 not null" },
		{ "comp_code", "int default 0 not null" },
		{ "cpus", "mediumint unsigned not null" },
		{ "max_vsize", "mediumint unsigned default 0 not null" },
		{ "max_vsize_task", "smallint unsigned default 0 not null" },
		{ "max_vsize_node", "mediumint unsigned default 0 not null" },
		{ "ave_vsize", "float default 0.0 not null" },
		{ "max_rss", "mediumint unsigned default 0 not null" },
		{ "max_rss_task", "smallint unsigned default 0 not null" },
		{ "max_rss_node", "mediumint unsigned default 0 not null" },
		{ "ave_rss", "float default 0.0 not null" },
		{ "max_pages", "mediumint unsigned default 0 not null" },
		{ "max_pages_task", "smallint unsigned default 0 not null" },
		{ "max_pages_node", "mediumint unsigned default 0 not null" },
		{ "ave_pages", "float default 0.0 not null" },
		{ "min_cpu", "mediumint unsigned default 0 not null" },
		{ "min_cpu_task", "smallint unsigned default 0 not null" },
		{ "min_cpu_node", "mediumint unsigned default 0 not null" },
		{ "ave_cpu", "float default 0.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_rusage_fields[] = {
		{ "id", "int not null" },
		{ "stepid", "smallint not null" },
		{ "cpu_sec", "int unsigned default 0 not null" },
		{ "cpu_usec", "int unsigned default 0 not null" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "max_rss", "int unsigned default 0 not null" },
		{ "max_ixrss", "int unsigned default 0 not null" },
		{ "max_idrss", "int unsigned default 0 not null" },
		{ "max_isrss", "int unsigned default 0 not null" },
		{ "max_minflt", "int unsigned default 0 not null" },
		{ "max_majflt", "int unsigned default 0 not null" },
		{ "max_nswap", "int unsigned default 0 not null" },
		{ "inblock", "int unsigned default 0 not null" },
		{ "outblock", "int unsigned default 0 not null" },
		{ "msgsnd", "int unsigned default 0 not null" },
		{ "msgrcv", "int unsigned default 0 not null" },
		{ "nsignals", "int unsigned default 0 not null" },
		{ "nvcsw", "int unsigned default 0 not null" },
		{ "nivcsw", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	if(mysql_db_create_table(jobacct_mysql_db, job_index, job_index_fields,
				 ", primary key (id))") == SLURM_ERROR)
		return SLURM_ERROR;
	
	if(mysql_db_create_table(jobacct_mysql_db, job_table, job_table_fields,
				 ")") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(jobacct_mysql_db, step_table,
				 step_table_fields, ")") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(jobacct_mysql_db, rusage_table,
				 step_rusage_fields, ")") == SLURM_ERROR)
		return SLURM_ERROR;


	return SLURM_SUCCESS;
}
#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
#ifndef HAVE_MYSQL
	fatal("No MySQL storage was found on the machine. "
	      "Please check the configure ran and run again.");
#endif
	if(first) {
		char *temp = slurm_get_jobacct_gather_type();
		char *temp2 = slurm_get_jobacct_storage_type();
		if(!strcasecmp(temp, JOB_ACCT_GATHER_TYPE_NONE)) {
			fatal("WARNING: You are trying to store job "
			      "accounting info (%s) without collecting it. "
			      "This will not work.  If you want to collect "
			      "accounting data set the jobacct-gather option "
			      "to something other than '%s'", temp2, temp);
		}
		xfree(temp);
		xfree(temp2);
		/* since this can be loaded from many different places
		   only tell us once. */
		verbose("%s loaded", plugin_name);
		first = 0;
	} else {
		debug4("%s loaded", plugin_name);
	}
	
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

/* 
 * Initialize the storage make sure tables are created and in working
 * order
 */
extern int jobacct_storage_p_init(char *location)
{
#ifdef HAVE_MYSQL
	mysql_db_info_t *db_info = _mysql_jobacct_create_db_info();
	int rc = SLURM_SUCCESS;
	char *db_name = NULL;

	if(jobacct_mysql_db && mysql_ping(jobacct_mysql_db) == 0)
		return SLURM_SUCCESS;
	
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
	debug2("mysql_connect() called for db %s", db_name);
	
	mysql_get_db_connection(&jobacct_mysql_db, db_name, db_info);

	_mysql_jobacct_check_tables();

	destroy_mysql_db_info(db_info);

	debug("Jobacct storage init finished");

	return rc;
#else
	return SLURM_ERROR;
#endif
}

/*
 * finish up storage connection
 */
extern int jobacct_storage_p_fini()
{
#ifdef HAVE_MYSQL
	if (jobacct_mysql_db) {
		mysql_close(jobacct_mysql_db);
		jobacct_mysql_db = NULL;
	}

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(struct job_record *job_ptr)
{
#ifdef HAVE_MYSQL
		int	i,
		ncpus=0,
		rc=SLURM_SUCCESS;
	char	*jname, *account, *nodes;
	long	priority;
	int track_steps = 0;
	char *block_id = NULL;
	char query[1024];
	int reinit = 0;

	if(!jobacct_mysql_db || mysql_ping(jobacct_mysql_db) != 0) {
		char *loc = slurm_get_jobacct_storage_loc();
		if(jobacct_storage_p_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}

	debug2("mysql_jobacct_job_start() called");
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
		 job_index, job_ptr->job_id, job_ptr->partition,
		 (int)job_ptr->details->submit_time, job_ptr->user_id,
		 job_ptr->group_id, block_id);
	xfree(block_id);

try_again:
	if((job_ptr->db_index =
	    mysql_insert_ret_id(jobacct_mysql_db, query))) {
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
		rc = mysql_db_query(jobacct_mysql_db, query);
	} else if(!reinit) {
		char *loc = slurm_get_jobacct_storage_loc();
		error("It looks like the storage has gone "
		      "away trying to reconnect");
		jobacct_storage_p_fini();
		jobacct_storage_p_init(loc);
		xfree(loc);
		reinit = 1;
		goto try_again;
	} else
		rc = SLURM_ERROR;
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(struct job_record *job_ptr)
{
#ifdef HAVE_MYSQL
		char query[1024];
	char	*account, *nodes;
	int rc=SLURM_SUCCESS;
	
	if(!jobacct_mysql_db || mysql_ping(jobacct_mysql_db) != 0) {
		char *loc = slurm_get_jobacct_storage_loc();
		if(jobacct_storage_p_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}
	
	debug2("mysql_jobacct_job_complete() called");
	if (job_ptr->end_time == 0) {
		debug("mysql_jobacct: job %u never started", job_ptr->job_id);
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
			 "update %s set start=%u, end=%u, state=%d, "
			 "nodelist='%s', account='%s', comp_code=%u, "
			 "kill_requid=%d where id=%u",
			 job_table, (int)job_ptr->start_time,
			 (int)job_ptr->end_time, 
			 job_ptr->job_state & (~JOB_COMPLETING),
			 nodes, account, job_ptr->exit_code,
			 job_ptr->requid, job_ptr->db_index);
		rc = mysql_db_query(jobacct_mysql_db, query);
	} else 
		rc = SLURM_ERROR;

	return  rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(struct step_record *step_ptr)
{
#ifdef HAVE_MYSQL
		int cpus = 0;
	int rc=SLURM_SUCCESS;
	char node_list[BUFFER_SIZE];
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	char query[1024];
	
	if(!jobacct_mysql_db || mysql_ping(jobacct_mysql_db) != 0) {
		char *loc = slurm_get_jobacct_storage_loc();
		if(jobacct_storage_p_init(loc) == SLURM_ERROR) {
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
		rc = mysql_db_query(jobacct_mysql_db, query);
		if(rc != SLURM_ERROR) {
			snprintf(query, sizeof(query),
				 "insert into %s (id, stepid) values (%d, %u)",
				 rusage_table, step_ptr->job_ptr->db_index,
				 step_ptr->step_id);
			rc = mysql_db_query(jobacct_mysql_db, query);
		}	  
	} else 
		rc = SLURM_ERROR;
		 
	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(struct step_record *step_ptr)
{
#ifdef HAVE_MYSQL
		time_t now;
	int elapsed;
	int comp_status;
	int cpus = 0;
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	float ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	float ave_cpu = 0, ave_cpu2 = 0;
	char *account;
	char query[1024];
	int rc =SLURM_SUCCESS;
	
	if(!jobacct_mysql_db || mysql_ping(jobacct_mysql_db) != 0) {
		char *loc = slurm_get_jobacct_storage_loc();
		if(jobacct_storage_p_init(loc) == SLURM_ERROR) {
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
			 "update %s set end=%u, state=%d, "
			 "kill_requid=%d, comp_code=%u, "
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
			 step_ptr->exit_code, 
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
		rc = mysql_db_query(jobacct_mysql_db, query);
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
			
			rc = mysql_db_query(jobacct_mysql_db, query);
		}
	} else
		rc = SLURM_ERROR;
		 
	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(struct job_record *job_ptr)
{
#ifdef HAVE_MYSQL
		char query[1024];
	int rc = SLURM_SUCCESS;
	
	if(!jobacct_mysql_db || mysql_ping(jobacct_mysql_db) != 0) {
		char *loc = slurm_get_jobacct_storage_loc();
		if(jobacct_storage_p_init(loc) == SLURM_ERROR) {
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
		rc = mysql_db_query(jobacct_mysql_db, query);
		if(rc != SLURM_ERROR) {
			snprintf(query, sizeof(query),
				 "update %s set suspended=%u-suspended, "
				 "state=%d where id=%u and end=0",
				 step_table, (int)job_ptr->suspend_time, 
				 job_ptr->job_state, job_ptr->db_index);
			rc = mysql_db_query(jobacct_mysql_db, query);
		}
	} else
		rc = SLURM_ERROR;
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * get info from the storage 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern void jobacct_storage_p_get_jobs(List job_list,
					List selected_steps,
					List selected_parts,
					void *params)
{
#ifdef HAVE_MYSQL
	if(!jobacct_mysql_db || mysql_ping(jobacct_mysql_db) != 0) {
		char *loc = slurm_get_jobacct_storage_loc();
		if(jobacct_storage_p_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return;
		}
		xfree(loc);
	}
	mysql_jobacct_process_get_jobs(job_list,
				       selected_steps, selected_parts,
				       params);	
#endif
	return;
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_p_archive(List selected_parts,
				       void *params)
{
#ifdef HAVE_MYSQL
	if(!jobacct_mysql_db || mysql_ping(jobacct_mysql_db) != 0) {
		char *loc = slurm_get_jobacct_storage_loc();
		if(jobacct_storage_p_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return;
		}
		xfree(loc);
	}
	mysql_jobacct_process_archive(selected_parts, params);
#endif
	return;
}
