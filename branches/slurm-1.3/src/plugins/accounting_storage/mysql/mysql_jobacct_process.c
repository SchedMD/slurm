/*****************************************************************************\
 *  mysql_jobacct_process.c - functions the processing of
 *                               information from the mysql jobacct
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
#include "src/common/env.h"
#include "src/common/xstring.h"
#include "mysql_jobacct_process.h"
#include <fcntl.h>

#ifdef HAVE_MYSQL
static pthread_mutex_t local_file_lock = PTHREAD_MUTEX_INITIALIZER;

static int _write_to_file(int fd, char *data)
{
	int pos = 0, nwrite = strlen(data), amount;
	int rc = SLURM_SUCCESS;

	while (nwrite > 0) {
		amount = write(fd, &data[pos], nwrite);
		if ((amount < 0) && (errno != EINTR)) {
			error("Error writing file: %m");
			rc = errno;
			break;
		}
		nwrite -= amount;
		pos    += amount;
	}
	return rc;
}

static int _archive_script(acct_archive_cond_t *arch_cond, time_t last_submit)
{
	char * args[] = {arch_cond->archive_script, NULL};
	const char *tmpdir;
	struct stat st;
	char **env = NULL;
	struct tm time_tm;
	time_t curr_end;

#ifdef _PATH_TMP
	tmpdir = _PATH_TMP;
#else
	tmpdir = "/tmp";
#endif
	if (stat(arch_cond->archive_script, &st) < 0) {
		errno = errno;
		error("mysql_jobacct_process_run_script: failed to stat %s: %m",
		      arch_cond->archive_script);
		return SLURM_ERROR;
	}

	if (!(st.st_mode & S_IFREG)) {
		errno = EACCES;
		error("mysql_jobacct_process_run_script: "
		      "%s isn't a regular file",
		      arch_cond->archive_script);
		return SLURM_ERROR;
	}

	if (access(arch_cond->archive_script, X_OK) < 0) {
		errno = EACCES;
		error("mysql_jobacct_process_run_script: "
		      "%s is not executable", arch_cond->archive_script);
		return SLURM_ERROR;
	}

	env = env_array_create();

	if(arch_cond->step_purge) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first step start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->step_purge;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_STEPS", "%u",
				     arch_cond->archive_steps);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_LAST_STEP", "%d",
				     curr_end);
	}

	if(arch_cond->job_purge) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->job_purge;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		
		env_array_append_fmt(&env, "SLURM_ARCHIVE_JOBS", "%u",
				     arch_cond->archive_jobs);
		env_array_append_fmt (&env, "SLURM_ARCHIVE_LAST_JOB", "%d",
				      curr_end);
	}

#ifdef _PATH_STDPATH
	env_array_append (&env, "PATH", _PATH_STDPATH);
#else
	env_array_append (&env, "PATH", "/bin:/usr/bin");
#endif
	execve(arch_cond->archive_script, args, env);

	env_array_free(env);

	return SLURM_SUCCESS;
}


extern int setup_job_cond_limits(acct_job_cond_t *job_cond, char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	char *table_level = "t2";
	jobacct_selected_step_t *selected_step = NULL;
	time_t now = time(NULL);

	if(!job_cond)
		return 0;
	
	/* THIS ASSOCID CHECK ALWAYS NEEDS TO BE FIRST!!!!!!! */
	if(job_cond->associd_list && list_count(job_cond->associd_list)) {
		set = 0;
		xstrfmtcat(*extra, ", %s as t3 where (", assoc_table);
		itr = list_iterator_create(job_cond->associd_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t3.id=%s", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
		table_level="t3";
		/* just incase the association is gone */
		if(set) 
			xstrcat(*extra, " || ");
		xstrfmtcat(*extra, "t3.id is null) && "
			   "(t2.lft between t3.lft and t3.rgt "
			   "|| t2.lft is null)");
	}

	if(job_cond->acct_list && list_count(job_cond->acct_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.account='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(job_cond->userid_list && list_count(job_cond->userid_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		itr = list_iterator_create(job_cond->userid_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.uid='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(job_cond->groupid_list && list_count(job_cond->groupid_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->groupid_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.gid=", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(job_cond->partition_list && list_count(job_cond->partition_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->partition_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.partition='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(job_cond->step_list && list_count(job_cond->step_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->step_list);
		while((selected_step = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.jobid=%u", selected_step->jobid);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(job_cond->usage_start) {
		if(!job_cond->usage_end)
			job_cond->usage_end = now;

		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		xstrfmtcat(*extra, 
			   "(t1.eligible < %d "
			   "&& (t1.end >= %d || t1.end = 0)))",
			   job_cond->usage_end, job_cond->usage_start);
	}

	if(job_cond->state_list && list_count(job_cond->state_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		itr = list_iterator_create(job_cond->state_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.state='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	/* we need to put all the associations (t2) stuff together here */
	if(job_cond->cluster_list && list_count(job_cond->cluster_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		itr = list_iterator_create(job_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, 
				   "(t1.cluster='%s' || %s.cluster='%s')", 
				   object, table_level, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	} 

	if(job_cond->wckey_list && list_count(job_cond->wckey_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		itr = list_iterator_create(job_cond->wckey_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.wckey='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}
	return set;
}

extern List mysql_jobacct_process_get_jobs(mysql_conn_t *mysql_conn, uid_t uid,
					   acct_job_cond_t *job_cond)
{

	char *query = NULL;	
	char *extra = NULL;	
	char *tmp = NULL;	
	jobacct_selected_step_t *selected_step = NULL;
	ListIterator itr = NULL;
	int set = 0, is_admin=1;
	char *table_level="t2";
	MYSQL_RES *result = NULL, *step_result = NULL;
	MYSQL_ROW row, step_row;
	int i, last_id = -1, curr_id = -1;
	jobacct_job_rec_t *job = NULL;
	jobacct_step_rec_t *step = NULL;
	time_t now = time(NULL);
	List job_list = list_create(destroy_jobacct_job_rec);
	uint16_t private_data = 0;
	acct_user_rec_t user;
		
	/* if this changes you will need to edit the corresponding 
	 * enum below also t1 is job_table */
	char *job_req_inx[] = {
		"t1.id",
		"t1.jobid",
		"t1.associd",
		"t1.wckey",
		"t1.wckeyid",
		"t1.uid",
		"t1.gid",
		"t1.partition",
		"t1.blockid",
		"t1.cluster",
		"t1.account",
		"t1.eligible",
		"t1.submit",
		"t1.start",
		"t1.end",
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
		"t2.user",
		"t2.cluster",
		"t2.acct",
		"t2.lft"
	};

	/* if this changes you will need to edit the corresponding 
	 * enum below also t1 is step_table */
	char *step_req_inx[] = {
		"t1.stepid",
		"t1.start",
		"t1.end",
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
		"t1.ave_cpu"
	};

	enum {
		JOB_REQ_ID,
		JOB_REQ_JOBID,
		JOB_REQ_ASSOCID,
		JOB_REQ_WCKEY,
		JOB_REQ_WCKEYID,
		JOB_REQ_UID,
		JOB_REQ_GID,
		JOB_REQ_PARTITION,
		JOB_REQ_BLOCKID,
		JOB_REQ_CLUSTER1,
		JOB_REQ_ACCOUNT1,
		JOB_REQ_ELIGIBLE,
		JOB_REQ_SUBMIT,
		JOB_REQ_START,
		JOB_REQ_END,
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
		JOB_REQ_ACCOUNT,
		JOB_REQ_LFT,
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

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_JOBS) {
		/* This only works when running though the slurmdbd.
		 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
		 * SLURMDBD!
		 */
		if(slurmdbd_conf) {
			is_admin = 0;
			/* we have to check the authentication here in the
			 * plugin since we don't know what accounts are being
			 * referenced until after the query.  Here we will
			 * set if they are an operator or greater and then
			 * check it below after the query.
			 */
			if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
			   || assoc_mgr_get_admin_level(mysql_conn, uid) 
			   >= ACCT_ADMIN_OPERATOR) 
				is_admin = 1;	
			else {
				assoc_mgr_fill_in_user(mysql_conn, &user, 1);
			}
		}
	}

	setup_job_cond_limits(job_cond, &extra);

	xfree(tmp);
	xstrfmtcat(tmp, "%s", job_req_inx[0]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", job_req_inx[i]);
	}

	/* This is here to make sure we are looking at only this user
	 * if this flag is set.  We also include any accounts they may be
	 * coordinator of.
	 */
	if(!is_admin && (private_data & PRIVATE_DATA_JOBS)) {
		query = xstrdup_printf("select lft from %s where user='%s'", 
				       assoc_table, user.name);
		if(user.coord_accts) {
			acct_coord_rec_t *coord = NULL;
			itr = list_iterator_create(user.coord_accts);
			while((coord = list_next(itr))) {
				xstrfmtcat(query, " || acct='%s'",
					   coord->name);
			}
			list_iterator_destroy(itr);
		}
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(extra);
			xfree(query);
			return NULL;
		}
		xfree(query);
		set = 0;
		while((row = mysql_fetch_row(result))) {
			if(set) {
				xstrfmtcat(extra,
					   " || (%s between %s.lft and %s.rgt)",
					   row[0], table_level, table_level);
			} else {
				set = 1;
				if(extra)
					xstrfmtcat(extra,
						   " && ((%s between %s.lft "
						   "and %s.rgt)",
						   row[0], table_level,
						   table_level);
				else
					xstrfmtcat(extra, 
						   " where ((%s between %s.lft "
						   "and %s.rgt)",
						   row[0], table_level,
						   table_level);
			}
		}		
		if(set)
			xstrcat(extra,")");
		mysql_free_result(result);
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

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		list_destroy(job_list);
		return NULL;
	}
	xfree(query);

	while((row = mysql_fetch_row(result))) {
		char *id = row[JOB_REQ_ID];
		bool job_ended = 0;

		curr_id = atoi(row[JOB_REQ_JOBID]);

		if(job_cond && !job_cond->duplicates && curr_id == last_id)
			continue;
		
		last_id = curr_id;

		job = create_jobacct_job_rec();

		job->alloc_cpus = atoi(row[JOB_REQ_ALLOC_CPUS]);
		job->associd = atoi(row[JOB_REQ_ASSOCID]);

		if(row[JOB_REQ_WCKEY] && row[JOB_REQ_WCKEY][0])
			job->wckey = xstrdup(row[JOB_REQ_WCKEY]);
		job->wckeyid = atoi(row[JOB_REQ_WCKEYID]);

		if(row[JOB_REQ_CLUSTER] && row[JOB_REQ_CLUSTER][0])
			job->cluster = xstrdup(row[JOB_REQ_CLUSTER]);
		else if(row[JOB_REQ_CLUSTER1] && row[JOB_REQ_CLUSTER1][0])
			job->cluster = xstrdup(row[JOB_REQ_CLUSTER1]);
			
		if(row[JOB_REQ_USER_NAME]) 
			job->user = xstrdup(row[JOB_REQ_USER_NAME]);
		else 
			job->uid = atoi(row[JOB_REQ_UID]);

		if(row[JOB_REQ_LFT])
			job->lft = atoi(row[JOB_REQ_LFT]);

		if(row[JOB_REQ_ACCOUNT] && row[JOB_REQ_ACCOUNT][0])
			job->account = xstrdup(row[JOB_REQ_ACCOUNT]);
		else if(row[JOB_REQ_ACCOUNT1] && row[JOB_REQ_ACCOUNT1][0])
			job->account = xstrdup(row[JOB_REQ_ACCOUNT1]);

		if(row[JOB_REQ_BLOCKID])
			job->blockid = xstrdup(row[JOB_REQ_BLOCKID]);

		job->eligible = atoi(row[JOB_REQ_ELIGIBLE]);
		job->submit = atoi(row[JOB_REQ_SUBMIT]);
		job->start = atoi(row[JOB_REQ_START]);
		job->end = atoi(row[JOB_REQ_END]);
		/* since the job->end could be set later end it here */
		if(job->end)
			job_ended = 1;

		if(job_cond && job_cond->usage_start) {
			if(job->start && (job->start < job_cond->usage_start))
				job->start = job_cond->usage_start;

			if(!job->start && job->end)
				job->start = job->end;

			if(!job->end || job->end > job_cond->usage_end) 
				job->end = job_cond->usage_end;

			job->elapsed = job->end - job->start;

			if(row[JOB_REQ_SUSPENDED]) {
				MYSQL_RES *result2 = NULL;
				MYSQL_ROW row2;
				/* get the suspended time for this job */
				query = xstrdup_printf(
					"select start, end from %s where "
					"(start < %d && (end >= %d "
					"|| end = 0)) && id=%s "
					"order by start",
					suspend_table,
					job_cond->usage_end, 
					job_cond->usage_start,
					id);
				
				debug4("%d(%d) query\n%s", 
				       mysql_conn->conn, __LINE__, query);
				if(!(result2 = mysql_db_query_ret(
					     mysql_conn->db_conn,
					     query, 0))) {
					list_destroy(job_list);
					job_list = NULL;
					break;
				}
				xfree(query);
				while((row2 = mysql_fetch_row(result2))) {
					int local_start =
						atoi(row2[0]);
					int local_end = 
						atoi(row2[1]);
					
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
				mysql_free_result(result2);			

			}
		} else {
			job->suspended = atoi(row[JOB_REQ_SUSPENDED]);

			if(!job->start) {
				job->elapsed = 0;
			} else if(!job->end) {
				job->elapsed = now - job->start;
			} else {
				job->elapsed = job->end - job->start;
			}

			job->elapsed -= job->suspended;
		}

		job->jobid = curr_id;
		job->jobname = xstrdup(row[JOB_REQ_NAME]);
		job->gid = atoi(row[JOB_REQ_GID]);
		job->exitcode = atoi(row[JOB_REQ_COMP_CODE]);

		if(row[JOB_REQ_PARTITION])
			job->partition = xstrdup(row[JOB_REQ_PARTITION]);

		if(row[JOB_REQ_NODELIST])
			job->nodes = xstrdup(row[JOB_REQ_NODELIST]);

		if (!job->nodes || !strcmp(job->nodes, "(null)")) {
			xfree(job->nodes);
			job->nodes = xstrdup("(unknown)");
		}
			
		job->track_steps = atoi(row[JOB_REQ_TRACKSTEPS]);
		job->state = atoi(row[JOB_REQ_STATE]);
		job->priority = atoi(row[JOB_REQ_PRIORITY]);
		job->req_cpus = atoi(row[JOB_REQ_REQ_CPUS]);
		job->requid = atoi(row[JOB_REQ_KILL_REQUID]);
		job->qos = atoi(row[JOB_REQ_QOS]);
		job->show_full = 1;
					
		list_append(job_list, job);

		if(job_cond && job_cond->step_list
		   && list_count(job_cond->step_list)) {
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
					xstrcat(extra, " || ");
				else 
					xstrcat(extra, " && (");
			
				xstrfmtcat(extra, "t1.stepid=%u",
					   selected_step->stepid);
				set = 1;
				job->show_full = 0;
			}
			list_iterator_destroy(itr);
			if(set)
				xstrcat(extra, ")");
		}
		for(i=0; i<STEP_REQ_COUNT; i++) {
			if(i) 
				xstrcat(tmp, ", ");
			xstrcat(tmp, step_req_inx[i]);
		}
		query =	xstrdup_printf("select %s from %s t1 where t1.id=%s",
				       tmp, step_table, id);
		xfree(tmp);
		
		if(extra) {
			xstrcat(query, extra);
			xfree(extra);
		}
		
		//info("query = %s", query);
		if(!(step_result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			list_destroy(job_list);
			return NULL;
		}
		xfree(query);
		while ((step_row = mysql_fetch_row(step_result))) {
			step = create_jobacct_step_rec();
			step->jobid = job->jobid;
			list_append(job->steps, step);
			step->stepid = atoi(step_row[STEP_REQ_STEPID]);
			/* info("got step %u.%u", */
/* 			     job->header.jobnum, step->stepnum); */
			step->state = atoi(step_row[STEP_REQ_STATE]);
			step->exitcode = atoi(step_row[STEP_REQ_COMP_CODE]);
			step->ncpus = atoi(step_row[STEP_REQ_CPUS]);
			step->start = atoi(step_row[STEP_REQ_START]);
			
			step->end = atoi(step_row[STEP_REQ_END]);
			/* if the job has ended end the step also */
			if(!step->end && job_ended) {
				step->end = job->end;
				step->state = job->state;
			}

			if(job_cond && job_cond->usage_start) {
				if(step->start 
				   && (step->start < job_cond->usage_start))
					step->start = job_cond->usage_start;
				
				if(!step->start && step->end)
					step->start = step->end;
				
				if(!step->end 
				   || (step->end > job_cond->usage_end)) 
					step->end = job_cond->usage_end;
			}

			step->elapsed = step->end - step->start;
			/* figure this out by start stop */
			step->suspended = atoi(step_row[STEP_REQ_SUSPENDED]);
			if(!step->end) {
				step->elapsed = now - step->start;
			} else {
				step->elapsed = step->end - step->start;
			}
			step->elapsed -= step->suspended;
			step->user_cpu_sec = atoi(step_row[STEP_REQ_USER_SEC]);
			step->user_cpu_usec =
				atoi(step_row[STEP_REQ_USER_USEC]);
			step->sys_cpu_sec = atoi(step_row[STEP_REQ_SYS_SEC]);
			step->sys_cpu_usec = atoi(step_row[STEP_REQ_SYS_USEC]);
			job->tot_cpu_sec += 
				step->tot_cpu_sec += 
				step->user_cpu_sec + step->sys_cpu_sec;
			job->tot_cpu_usec += 
				step->tot_cpu_usec += 
				step->user_cpu_usec + step->sys_cpu_usec;
			step->sacct.max_vsize =
				atoi(step_row[STEP_REQ_MAX_VSIZE]);
			step->sacct.max_vsize_id.taskid = 
				atoi(step_row[STEP_REQ_MAX_VSIZE_TASK]);
			step->sacct.ave_vsize = 
				atof(step_row[STEP_REQ_AVE_VSIZE]);
			step->sacct.max_rss =
				atoi(step_row[STEP_REQ_MAX_RSS]);
			step->sacct.max_rss_id.taskid = 
				atoi(step_row[STEP_REQ_MAX_RSS_TASK]);
			step->sacct.ave_rss = 
				atof(step_row[STEP_REQ_AVE_RSS]);
			step->sacct.max_pages =
				atoi(step_row[STEP_REQ_MAX_PAGES]);
			step->sacct.max_pages_id.taskid = 
				atoi(step_row[STEP_REQ_MAX_PAGES_TASK]);
			step->sacct.ave_pages =
				atof(step_row[STEP_REQ_AVE_PAGES]);
			step->sacct.min_cpu =
				atof(step_row[STEP_REQ_MIN_CPU]);
			step->sacct.min_cpu_id.taskid = 
				atoi(step_row[STEP_REQ_MIN_CPU_TASK]);
			step->sacct.ave_cpu = atof(step_row[STEP_REQ_AVE_CPU]);
			step->stepname = xstrdup(step_row[STEP_REQ_NAME]);
			step->nodes = xstrdup(step_row[STEP_REQ_NODELIST]);
			step->sacct.max_vsize_id.nodeid = 
				atoi(step_row[STEP_REQ_MAX_VSIZE_NODE]);
			step->sacct.max_rss_id.nodeid = 
				atoi(step_row[STEP_REQ_MAX_RSS_NODE]);
			step->sacct.max_pages_id.nodeid = 
				atoi(step_row[STEP_REQ_MAX_PAGES_NODE]);
			step->sacct.min_cpu_id.nodeid = 
				atoi(step_row[STEP_REQ_MIN_CPU_NODE]);

			step->requid = atoi(step_row[STEP_REQ_KILL_REQUID]);
		}
		mysql_free_result(step_result);
		
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
	}
	mysql_free_result(result);

	return job_list;
}

extern int mysql_jobacct_process_archive(mysql_conn_t *mysql_conn,
					 acct_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS, fd = 0;
	char *query = NULL;
	time_t last_submit = time(NULL);
	time_t curr_end;
	char *tmp = NULL;
	int i=0;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	struct tm time_tm;
	char start_char[32];
	char end_char[32];
//	DEF_TIMERS;

	/* if this changes you will need to edit the corresponding 
	 * enum below */
	char *job_req_inx[] = {
		"id",
		"jobid",
		"associd",
		"wckey",
		"wckeyid",
		"uid",
		"gid",
		"partition",
		"blockid",
		"cluster",
		"account",
		"eligible",
		"submit",
		"start",
		"end",
		"suspended",
		"name",
		"track_steps",
		"state",
		"comp_code",
		"priority",
		"req_cpus",
		"alloc_cpus",
		"nodelist",
		"kill_requid",
		"qos"
	};

	/* if this changes you will need to edit the corresponding 
	 * enum below */
	char *step_req_inx[] = {
		"id",
		"stepid",
		"start",
		"end",
		"suspended",
		"name",
		"nodelist",
		"state",
		"kill_requid",
		"comp_code",
		"cpus",
		"user_sec",
		"user_usec",
		"sys_sec",
		"sys_usec",
		"max_vsize",
		"max_vsize_task",
		"max_vsize_node",
		"ave_vsize",
		"max_rss",
		"max_rss_task",
		"max_rss_node",
		"ave_rss",
		"max_pages",
		"max_pages_task",
		"max_pages_node",
		"ave_pages",
		"min_cpu",
		"min_cpu_task",
		"min_cpu_node",
		"ave_cpu"
	};

	enum {
		JOB_REQ_ID,
		JOB_REQ_JOBID,
		JOB_REQ_ASSOCID,
		JOB_REQ_WCKEY,
		JOB_REQ_WCKEYID,
		JOB_REQ_UID,
		JOB_REQ_GID,
		JOB_REQ_PARTITION,
		JOB_REQ_BLOCKID,
		JOB_REQ_CLUSTER,
		JOB_REQ_ACCOUNT,
		JOB_REQ_ELIGIBLE,
		JOB_REQ_SUBMIT,
		JOB_REQ_START,
		JOB_REQ_END,
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
		STEP_REQ_ID,
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

	if(!arch_cond) {
		error("No arch_cond was given to archive from.  returning");
		return SLURM_ERROR;
	}

	if(!localtime_r(&last_submit, &time_tm)) {
		error("Couldn't get localtime from first start %d",
		      last_submit);
		return SLURM_ERROR;
	}
	time_tm.tm_sec = 0;
	time_tm.tm_min = 0;
	time_tm.tm_hour = 0;
	time_tm.tm_mday = 1;
	time_tm.tm_isdst = -1;
	last_submit = mktime(&time_tm);
	last_submit--;
	debug("adjusted last submit is (%d)", last_submit);
	
	if(arch_cond->archive_script)
		return _archive_script(arch_cond, last_submit);
	else if(!arch_cond->archive_dir) {
		error("No archive dir given, can't process");
		return SLURM_ERROR;
	}

	if(arch_cond->step_purge) {
		/* remove all data from step table that was older than
		 * start * arch_cond->step_purge. 
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->step_purge;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);

		debug4("from %d - %d months purging steps from before %d", 
		       last_submit, arch_cond->step_purge, curr_end);
		
		if(arch_cond->archive_steps) {
			char *insert = NULL;
			char *values = NULL;
			int period_start = 0;
			MYSQL_RES *result = NULL;
			MYSQL_ROW row;

			xfree(tmp);
			xstrfmtcat(tmp, "%s", step_req_inx[0]);
			for(i=1; i<STEP_REQ_COUNT; i++) {
				xstrfmtcat(tmp, ", %s", step_req_inx[i]);
			}

			/* get all the steps submitted before this time
			   listed */
			query = xstrdup_printf("select %s from %s where "
					       "start <= %d && end != 0 "
					       "&& !deleted "
					       "order by start asc",
					       tmp, step_table, curr_end);

			xstrcat(tmp, ", deleted");
			insert = xstrdup_printf("insert into %s (%s) ",
						step_table, tmp);
			xfree(tmp);
			
//			START_TIMER;
			debug3("%d(%d) query\n%s", mysql_conn->conn,
			       __LINE__, query);
			if(!(result = mysql_db_query_ret(
				     mysql_conn->db_conn, query, 0))) {
				xfree(insert);
				xfree(query);
				return SLURM_ERROR;
			}
			xfree(query);
//			END_TIMER2("step query");
//			info("step query took %s", TIME_STR);

			if(!mysql_num_rows(result)) {
				xfree(insert);
				mysql_free_result(result);
				goto exit_steps;
			}

//			START_TIMER;
			slurm_mutex_lock(&local_file_lock);
			while((row = mysql_fetch_row(result))) {
				if(period_start) {
					xstrcat(values, ",\n(");
				} else {
					period_start = 
						atoi(row[STEP_REQ_START]);
					localtime_r((time_t *)&period_start,
						    &time_tm);
					time_tm.tm_sec = 0;
					time_tm.tm_min = 0;
					time_tm.tm_hour = 0;
					time_tm.tm_mday = 1;
					time_tm.tm_isdst = -1;
					period_start = mktime(&time_tm);
					localtime_r((time_t *)&period_start,
						    &time_tm);
					snprintf(start_char, sizeof(start_char),
						 "%4.4u-%2.2u-%2.2u"
						 "T%2.2u:%2.2u:%2.2u",
						 (time_tm.tm_year + 1900),
						 (time_tm.tm_mon+1), 
						 time_tm.tm_mday,
						 time_tm.tm_hour,
						 time_tm.tm_min, 
						 time_tm.tm_sec);

					localtime_r((time_t *)&curr_end,
						    &time_tm);
					snprintf(end_char, sizeof(end_char),
						 "%4.4u-%2.2u-%2.2u"
						 "T%2.2u:%2.2u:%2.2u",
						 (time_tm.tm_year + 1900),
						 (time_tm.tm_mon+1), 
						 time_tm.tm_mday,
						 time_tm.tm_hour,
						 time_tm.tm_min, 
						 time_tm.tm_sec);

					/* write the buffer to file */
					reg_file = xstrdup_printf(
						"%s/step_archive_%s_%s.sql",
						arch_cond->archive_dir,
						start_char, end_char);
					debug("Storing step archive at %s",
					      reg_file);
					old_file = xstrdup_printf(
						"%s.old", reg_file);
					new_file = xstrdup_printf(
						"%s.new", reg_file);
					
					fd = creat(new_file, 0600);
					if (fd == 0) {
						error("Can't save archive, "
						      "create file %s error %m",
						      new_file);
						rc = errno;
						xfree(insert);
						break;
					} 
					values = xstrdup_printf("%s\nvalues\n(",
								insert);
					xfree(insert);
				}
	
				xstrfmtcat(values, "'%s'", row[0]);
				for(i=1; i<STEP_REQ_COUNT; i++) {
					xstrfmtcat(values, ", '%s'", row[i]);
				}
				xstrcat(values, ", '1')");
				
				if(!fd || ((rc = _write_to_file(fd, values))
					   != SLURM_SUCCESS)) {
					xfree(values);
					break;
				}
				xfree(values);
			}
			mysql_free_result(result);
			rc = _write_to_file(
				fd, " on duplicate key update deleted=1;");
//			END_TIMER2("write file");
//			info("write file took %s", TIME_STR);

			fsync(fd);
			close(fd);
			
			if (rc)
				(void) unlink(new_file);
			else {			/* file shuffle */
				int ign;	/* avoid warning */
				(void) unlink(old_file);
				ign =  link(reg_file, old_file);
				(void) unlink(reg_file);
				ign =   link(new_file, reg_file);
				(void) unlink(new_file);
			}
			xfree(old_file);
			xfree(reg_file);
			xfree(new_file);
			slurm_mutex_unlock(&local_file_lock);

			period_start = 0;
		}

		if(rc != SLURM_SUCCESS) 
			return rc;

		query = xstrdup_printf("delete from %s where start <= %d "
				       "&& end != 0",
				       step_table, curr_end);
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old step data");
			return SLURM_ERROR;
		}
	}
exit_steps:
	
	if(arch_cond->job_purge) {
		/* remove all data from step table that was older than
		 * last_submit * arch_cond->job_purge. 
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first submit %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->job_purge;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);

		debug4("from %d - %d months purging jobs from before %d", 
		       last_submit, arch_cond->job_purge, curr_end);

		if(arch_cond->archive_jobs) {
			char *insert = NULL;
			char *values = NULL;
			int period_start = 0;
			MYSQL_RES *result = NULL;
			MYSQL_ROW row;
			
			xfree(tmp);
			xstrfmtcat(tmp, "%s", job_req_inx[0]);
			for(i=1; i<JOB_REQ_COUNT; i++) {
				xstrfmtcat(tmp, ", %s", job_req_inx[i]);
			}
			/* get all the jobs submitted before this time
			   listed */
			query = xstrdup_printf("select %s from %s where "
					       "submit < %d && end != 0 "
					       "&& !deleted "
					       "order by submit asc",
					       tmp, job_table, curr_end);

			xstrcat(tmp, ", deleted");
			insert = xstrdup_printf("insert into %s (%s) ",
						job_table, tmp);
			xfree(tmp);

//			START_TIMER;
			debug3("%d(%d) query\n%s", mysql_conn->conn,
			       __LINE__, query);
			if(!(result = mysql_db_query_ret(
				     mysql_conn->db_conn, query, 0))) {
				xfree(insert);
				xfree(query);
				return SLURM_ERROR;
			}
			xfree(query);
//			END_TIMER2("job query");
//			info("job query took %s", TIME_STR);

			if(!mysql_num_rows(result)) {
				xfree(insert);
				mysql_free_result(result);
				goto exit_jobs;
			}
			
//			START_TIMER;
			slurm_mutex_lock(&local_file_lock);
			while((row = mysql_fetch_row(result))) {
				if(period_start) {
					xstrcat(values, ",\n(");
				} else {
					period_start = 
						atoi(row[JOB_REQ_SUBMIT]);
					localtime_r((time_t *)&period_start,
						    &time_tm);
					time_tm.tm_sec = 0;
					time_tm.tm_min = 0;
					time_tm.tm_hour = 0;
					time_tm.tm_mday = 1;
					time_tm.tm_isdst = -1;
					period_start = mktime(&time_tm);
					localtime_r((time_t *)&period_start,
						    &time_tm);
					snprintf(start_char, sizeof(start_char),
						 "%4.4u-%2.2u-%2.2u"
						 "T%2.2u:%2.2u:%2.2u",
						 (time_tm.tm_year + 1900),
						 (time_tm.tm_mon+1), 
						 time_tm.tm_mday,
						 time_tm.tm_hour,
						 time_tm.tm_min, 
						 time_tm.tm_sec);

					localtime_r((time_t *)&curr_end,
						    &time_tm);

					snprintf(end_char, sizeof(end_char),
						 "%4.4u-%2.2u-%2.2u"
						 "T%2.2u:%2.2u:%2.2u",
						 (time_tm.tm_year + 1900),
						 (time_tm.tm_mon+1), 
						 time_tm.tm_mday,
						 time_tm.tm_hour,
						 time_tm.tm_min, 
						 time_tm.tm_sec);

					/* write the buffer to file */
					reg_file = xstrdup_printf(
						"%s/job_archive_%s_%s.sql",
						arch_cond->archive_dir,
						start_char, end_char);
					debug("Storing job archive at %s",
					      reg_file);
					old_file = xstrdup_printf(
						"%s.old", reg_file);
					new_file = xstrdup_printf(
						"%s.new", reg_file);
					
					fd = creat(new_file, 0600);
					if (fd == 0) {
						error("Can't save archive, "
						      "create file %s error %m",
						      new_file);
						rc = errno;
						xfree(insert);
						break;
					} 
					values = xstrdup_printf("%s\nvalues\n(",
								insert);
					xfree(insert);
				}
				
				xstrfmtcat(values, "'%s'", row[0]);
				for(i=1; i<JOB_REQ_COUNT; i++) {
					xstrfmtcat(values, ", '%s'", row[i]);
				}
				xstrcat(values, ", '1')");
				
				if(!fd || ((rc = _write_to_file(fd, values))
					   != SLURM_SUCCESS)) {
					xfree(values);
					break;
				}
				xfree(values);
			}
			mysql_free_result(result);

			rc = _write_to_file(
				fd, " on duplicate key update deleted=1;");
//			END_TIMER2("write file");
//			info("write file took %s", TIME_STR);
			
			
			fsync(fd);
			close(fd);

			if (rc)
				(void) unlink(new_file);
			else {			/* file shuffle */
				int ign;	/* avoid warning */
				(void) unlink(old_file);
				ign =  link(reg_file, old_file);
				(void) unlink(reg_file);
				ign =  link(new_file, reg_file);
				(void) unlink(new_file);
			}
			xfree(old_file);
			xfree(reg_file);
			xfree(new_file);
			slurm_mutex_unlock(&local_file_lock);

			period_start = 0;
		}
		query = xstrdup_printf("delete from %s where submit <= %d "
				       "&& end != 0",
				       job_table, curr_end);
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old job data");
			return SLURM_ERROR;
		}
	}
exit_jobs:
	
	return SLURM_SUCCESS;
}

extern int mysql_jobacct_process_archive_load(mysql_conn_t *mysql_conn,
					      acct_archive_rec_t *arch_rec)
{
	char *data = NULL;
	int error_code = SLURM_SUCCESS;

	if(!arch_rec) {
		error("We need a acct_archive_rec to load anything.");
		return SLURM_ERROR;
	}

	if(arch_rec->insert) {
		data = xstrdup(arch_rec->insert);
	} else if(arch_rec->archive_file) {
		uint32_t data_size = 0;
		int data_allocated, data_read = 0;
		int state_fd = open(arch_rec->archive_file, O_RDONLY);
		if (state_fd < 0) {
			info("No archive file (%s) to recover", 
			     arch_rec->archive_file);
			error_code = ENOENT;
		} else {
			data_allocated = BUF_SIZE;
			data = xmalloc(data_allocated);
			while (1) {
				data_read = read(state_fd, &data[data_size],
						 BUF_SIZE);
				if (data_read < 0) {
					if (errno == EINTR)
						continue;
					else {
						error("Read error on %s: %m", 
						      arch_rec->archive_file);
						break;
					}
				} else if (data_read == 0)	/* eof */
					break;
				data_size      += data_read;
				data_allocated += data_read;
				xrealloc(data, data_allocated);
			}
			close(state_fd);
		}
		if(error_code != SLURM_SUCCESS) {
			xfree(data);
			return error_code;
		}
	} else {
		error("Nothing was set in your "
		      "acct_archive_rec so I am unable to process.");
		return SLURM_ERROR;
	}

	if(!data) {
		error("It doesn't appear we have anything to load.");
		return SLURM_ERROR;
	}
	
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, data);
	error_code = mysql_db_query_check_after(mysql_conn->db_conn, data);
	xfree(data);
	if(error_code != SLURM_SUCCESS) {
		error("Couldn't load old data");
		return SLURM_ERROR;
	}       

	return SLURM_SUCCESS;
}

#endif	
