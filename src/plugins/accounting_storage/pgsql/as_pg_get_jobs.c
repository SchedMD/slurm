/*****************************************************************************\
 *  as_pg_get_jobs.c - accounting interface to pgsql - get jobs.
 *
 *  $Id: as_pg_get_jobs.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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
\*****************************************************************************/
#include "as_pg_common.h"


static pthread_mutex_t req_fields_lock = PTHREAD_MUTEX_INITIALIZER;

/* if this changes you will need to edit the corresponding
 * enum below also t1 is job_table */
static char *job_fields = NULL;
static char *job_req_inx[] = {
	"t1.job_db_inx",
	"t1.id_job",
	"t1.id_assoc",
	"t1.wckey",
	"t1.id_wckey",
	"t1.uid",
	"t1.gid",
	"t1.id_resv",
	"t1.partition",
	"t1.id_block",
	"t1.account",
	"t1.time_eligible",
	"t1.time_submit",
	"t1.time_start",
	"t1.time_end",
	"t1.time_suspended",
	"t1.timelimit",
	"t1.job_name",
	"t1.track_steps",
	"t1.state",
	"t1.exit_code",
	"t1.priority",
	"t1.cpus_req",
	"t1.cpus_alloc",
	"t1.nodes_alloc",
	"t1.nodelist",
	"t1.node_inx",
	"t1.kill_requid",
	"t1.id_qos",
	"t2.user_name",
	"t2.acct",
	"t2.lft"
};
enum {
	JF_ID,
	JF_JOBID,
	JF_ASSOCID,
	JF_WCKEY,
	JF_WCKEYID,
	JF_UID,
	JF_GID,
	JF_RESVID,
	JF_PARTITION,
	JF_BLOCKID,
	JF_ACCOUNT1,
	JF_ELIGIBLE,
	JF_SUBMIT,
	JF_START,
	JF_END,
	JF_SUSPENDED,
	JF_TIMELIMIT,
	JF_NAME,
	JF_TRACKSTEPS,
	JF_STATE,
	JF_COMP_CODE,
	JF_PRIORITY,
	JF_REQ_CPUS,
	JF_ALLOC_CPUS,
	JF_ALLOC_NODES,
	JF_NODELIST,
	JF_NODE_INX,
	JF_KILL_REQUID,
	JF_QOS,
	JF_USER_NAME,
	JF_ACCOUNT,
	JF_LFT,
	JF_COUNT
};

/* if this changes you will need to edit the corresponding
 * enum below also t1 is step_table */
static char *step_fields = NULL;
static char *step_req_inx[] = {
	"t1.id_step",
	"t1.time_start",
	"t1.time_end",
	"t1.time_suspended",
	"t1.step_name",
	"t1.nodelist",
	"t1.node_inx",
	"t1.state",
	"t1.kill_requid",
	"t1.exit_code",
	"t1.nodes_alloc",
	"t1.cpus_alloc",
	"t1.task_cnt",
	"t1.task_dist",
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
	SF_STEPID,
	SF_START,
	SF_END,
	SF_SUSPENDED,
	SF_NAME,
	SF_NODELIST,
	SF_NODE_INX,
	SF_STATE,
	SF_KILL_REQUID,
	SF_COMP_CODE,
	SF_NODES,
	SF_CPUS,
	SF_TASKS,
	SF_TASKDIST,
	SF_USER_SEC,
	SF_USER_USEC,
	SF_SYS_SEC,
	SF_SYS_USEC,
	SF_MAX_VSIZE,
	SF_MAX_VSIZE_TASK,
	SF_MAX_VSIZE_NODE,
	SF_AVE_VSIZE,
	SF_MAX_RSS,
	SF_MAX_RSS_TASK,
	SF_MAX_RSS_NODE,
	SF_AVE_RSS,
	SF_MAX_PAGES,
	SF_MAX_PAGES_TASK,
	SF_MAX_PAGES_NODE,
	SF_AVE_PAGES,
	SF_MIN_CPU,
	SF_MIN_CPU_TASK,
	SF_MIN_CPU_NODE,
	SF_AVE_CPU,
	SF_COUNT
};

static void
_init_req_fields(void)
{
	int i;

	slurm_mutex_lock(&req_fields_lock);
	if (!job_fields) {
		job_fields = xstrdup(job_req_inx[0]);
		for(i =1 ; i < JF_COUNT; i ++)
			xstrfmtcat(job_fields, ", %s", job_req_inx[i]);
		step_fields = xstrdup(step_req_inx[0]);
		for(i = 1; i < SF_COUNT; i ++)
			xstrfmtcat(step_fields, ", %s", step_req_inx[i]);
	}
	slurm_mutex_unlock(&req_fields_lock);
}

static void _state_time_string(char **extra, uint32_t state,
			       uint32_t start, uint32_t end)
{
	int base_state = state & JOB_STATE_BASE;

	if(!start && !end) {
		xstrfmtcat(*extra, "t1.state=%u", state);
		return;
	}

 	switch(base_state) {
	case JOB_PENDING:
		if(start && !end) {
			xstrfmtcat(*extra,
				   "(t1.time_eligible!=0 AND (t1.time_start=0"
				   " OR (%d BETWEEN "
				   "t1.time_eligible AND t1.time_start)))",
				   start);
		} else if (start && end) {
			xstrfmtcat(*extra,
				   "(t1.time_eligible!=0 AND ((%d BETWEEN"
				   "t1.time_eligible AND t1.time_start) OR "
				   "(t1.time_eligible BETWEEN %d AND %d)))",
				   start, start, end);
		} else if (end) {
			xstrfmtcat(*extra,
				   "(t1.time_eligible!=0 AND "
				   "t1.time_eligible < %d)", end);
		}
		break;
	case JOB_SUSPENDED:
		/* FIX ME: this should do something with the suspended
		   table, but it doesn't right now. */
	case JOB_RUNNING:
		if(start && !end) {
			xstrfmtcat(*extra,
				   "(t1.time_start!=0 AND (t1.time_end=0 OR "
				   "(%d BETWEEN t1.time_start AND "
				   "t1.time_end)))", start);
		} else if (start && end) {
			xstrfmtcat(*extra,
				   "(t1.time_start!=0 AND "
				   "((%d BETWEEN t1.time_start AND t1.time_end) "
				   "OR (t1.time_start BETWEEN %d AND %d)))",
				   start, start, end);
		} else if (end) {
			xstrfmtcat(*extra,
				   "(t1.time_start!=0 AND t1.time_start < %d)",
				   end);
		}
		break;
	case JOB_COMPLETE:
	case JOB_CANCELLED:
	case JOB_FAILED:
	case JOB_TIMEOUT:
	case JOB_NODE_FAIL:
	case JOB_PREEMPTED:
	default:
		xstrfmtcat(*extra, "(t1.state=%u AND (t1.time_end!=0 AND ", state);
		if(start && !end) {
			xstrfmtcat(*extra, "(t1.time_end >= %d)))", start);
		} else if (start && end) {
			xstrfmtcat(*extra,
				   "(t1.time_end BETWEEN %d AND %d)))",
				   start, end);
		} else if(end) {
			xstrfmtcat(*extra, "(t1.time_end <= %d)))", end);
		}
		break;
	}

	return;
}

/*
 * _make_job_cond_str - turn job_cond into SQL query condition string
 *
 * t1: job_table
 * t2: assoc_table
 * t3: assoc_table
 */
static void
_make_job_cond_str(pgsql_conn_t *pg_conn, slurmdb_job_cond_t *job_cond,
		   char **extra_table, char **cond)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	slurmdb_selected_step_t *selected_step = NULL;

	xstrcat (*cond, " WHERE TRUE");

	if(!job_cond)
		return;

	/* THIS ASSOCID CHECK ALWAYS NEEDS TO BE FIRST!!!!!!! */
	if(job_cond->associd_list && list_count(job_cond->associd_list)) {
		set = 0;
		xstrfmtcat(*extra_table, ", %%s.%s AS t3", assoc_table);

		/* just incase the association is gone */
		xstrcat(*cond, " AND (t3.id_assoc IS NULL");
		itr = list_iterator_create(job_cond->associd_list);
		while((object = list_next(itr))) {
			xstrfmtcat(*cond, " OR t3.id_assoc=%s", object);
		}
		list_iterator_destroy(itr);
		xstrfmtcat(*cond, ") AND "
			   "(t2.lft BETWEEN t3.lft AND t3.rgt "
			   "OR t2.lft IS NULL)");
	}

	concat_cond_list(job_cond->acct_list, "t1", "account", cond);
	concat_cond_list(job_cond->userid_list, "t1", "uid", cond);
	concat_cond_list(job_cond->groupid_list, "t1", "gid", cond);
	concat_cond_list(job_cond->partition_list, "t1", "partition", cond);
	concat_cond_list(job_cond->qos_list, "t1", "id_qos", cond);

	if(job_cond->step_list && list_count(job_cond->step_list)) {
		set = 0;
		xstrcat(*cond, " AND (");
		itr = list_iterator_create(job_cond->step_list);
		while((selected_step = list_next(itr))) {
			if(set)
				xstrcat(*cond, " OR ");
			xstrfmtcat(*cond, "t1.id_job=%u", selected_step->jobid);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond, ")");
	}


	if(job_cond->state_list && list_count(job_cond->state_list)) {
		set = 0;
		xstrcat(*cond, " AND (");

		itr = list_iterator_create(job_cond->state_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*cond, " OR ");
			_state_time_string(cond, atoi(object),
					   job_cond->usage_start,
					   job_cond->usage_end);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond, ")");
	} else {
		/* Only do this (default of all eligible jobs) if no
		   state is given */
		if(job_cond->usage_start) {
			if(!job_cond->usage_end)
				xstrfmtcat(*cond, " AND ((t1.time_end>=%ld "
					   "OR t1.time_end=0))",
					   (long)job_cond->usage_start);
			else
				xstrfmtcat(*cond, " AND (t1.time_eligible<%ld"
					   " AND (t1.time_end>=%ld "
					   "      OR t1.time_end=0))",
					   (long)job_cond->usage_end,
					   (long)job_cond->usage_start);
		} else if(job_cond->usage_end) {
			xstrfmtcat(*cond, " AND (t1.time_eligible<%ld)",
				   (long)job_cond->usage_end);
		}
	}

	concat_cond_list(job_cond->state_list, "t1", "state", cond);
	concat_cond_list(job_cond->wckey_list, "t1", "wckey", cond);

	if(job_cond->cpus_min) {
		if(job_cond->cpus_max)
			xstrfmtcat(*cond, " AND ((t1.cpus_alloc BETWEEN %u AND %u))",
				   job_cond->cpus_min, job_cond->cpus_max);

		else
			xstrfmtcat(*cond, " AND ((t1.cpus_alloc=%u))",
				   job_cond->cpus_min);
	}

	if(job_cond->nodes_min) {
		if(job_cond->nodes_max)
			xstrfmtcat(*cond,
				   " AND ((t1.nodes_alloc BETWEEN %u AND %u))",
				   job_cond->nodes_min, job_cond->nodes_max);

		else
			xstrfmtcat(*cond, " AND ((t1.nodes_alloc=%u))",
				   job_cond->nodes_min);
	}

	if(job_cond->timelimit_min) {
		if(job_cond->timelimit_max) {
			xstrfmtcat(*cond,
				   " AND (t1.timelimit BETWEEN %u AND %u))",
				   job_cond->timelimit_min,
				   job_cond->timelimit_max);

		} else {
			xstrfmtcat(*cond, "(t1.timelimit=%u))",
				   job_cond->timelimit_min);

		}
	}

	return;
}

/* concat cluster specific condition string */
static void
_concat_cluster_job_cond_str(pgsql_conn_t *pg_conn, char *cluster,
			     slurmdb_job_cond_t *job_cond, char **cond)
{
	DEF_VARS;

	/* this must be done before resvid_list since we set
	   resvid_list up here */
	if(job_cond->resv_list && list_count(job_cond->resv_list)) {
		query = xstrdup_printf(
			"SELECT DISTINCT id_resv FROM %s.%s WHERE TRUE ",
			cluster, resv_table);

		concat_cond_list(job_cond->resv_list, NULL, "resv_name",
				 &query);
		result = DEF_QUERY_RET;
		if(!result) {
			error("as/pg: couldn't get resv id");
			goto no_resv;
		}
		if(!job_cond->resvid_list)
			job_cond->resvid_list = list_create(slurm_destroy_char);
		FOR_EACH_ROW {
			list_append(job_cond->resvid_list, xstrdup(ROW(0)));
		} END_EACH_ROW;
		PQclear(result);
	}
no_resv:
	concat_cond_list(job_cond->resvid_list, "t1", "id_resv", cond);
}

/* constrain non-op user to access jobs of account they managed only */
static int
_concat_user_job_cond_str(pgsql_conn_t *pg_conn, char *cluster,
			  slurmdb_user_rec_t *user, char *table_level,
			  char **cond)
{
	DEF_VARS;
	ListIterator itr;
	int set = 0;

	query = xstrdup_printf("SELECT lft,rgt FROM %s.%s WHERE user_name='%s'",
			       cluster, assoc_table, user->name);
	if(user->coord_accts) {
		slurmdb_coord_rec_t *coord = NULL;
		itr = list_iterator_create(user->coord_accts);
		while((coord = list_next(itr))) {
			xstrfmtcat(query, " OR acct='%s'",
				   coord->name);
		}
		list_iterator_destroy(itr);
	}
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		if(set) {
			xstrfmtcat(*cond,
				   " OR (%s.lft BETWEEN %s AND %s)",
				   table_level, ROW(0), ROW(1));
		} else {
			set = 1;
			xstrfmtcat(*cond,
				   " AND ((%s.lft BETWEEN %s AND %s)",
				   table_level, ROW(0), ROW(1));
		}
	} END_EACH_ROW;
	if(set)
		xstrcat(*cond, ")");
	PQclear(result);
	return SLURM_SUCCESS;
}

/* get jobs from a cluster */
static int
_cluster_get_jobs(pgsql_conn_t *pg_conn, char *cluster,
		  slurmdb_job_cond_t *job_cond,
		  slurmdb_user_rec_t *user, int is_admin,
		  char *sent_cond, char *sent_extra,
		  int only_pending, List sent_list)
{
	DEF_VARS;
	PGresult *result2;
	cluster_nodes_t *cnodes = NULL;
	char *extra_table;
	char *cond = xstrdup(sent_cond);
	slurmdb_job_rec_t *job = NULL;
	List cluster_job_list = NULL;
	int rc = SLURM_SUCCESS, curr_id, last_id = -1;
	time_t now = time(NULL);

	_concat_cluster_job_cond_str(pg_conn, cluster, job_cond, &cond);

	if(!is_admin) {
		if (_concat_user_job_cond_str(pg_conn, cluster, user,
					      sent_extra ? "t3" : "t2",
					      &cond)
		    != SLURM_SUCCESS) {
			xfree(cond);
			return SLURM_ERROR;
		}
	}

	query = xstrdup_printf("SELECT %s FROM %s.%s AS t1 LEFT JOIN %s.%s "
			       "AS t2 ON t1.id_assoc=t2.id_assoc ",
			       job_fields, cluster, job_table, cluster,
			       assoc_table);
	if(sent_extra) {
		extra_table = xstrdup_printf(sent_extra, cluster);
		xstrcat(query, extra_table);
		xfree(extra_table);
	}
	xstrcat(query, cond);
	xfree(cond);

	if(job_cond && job_cond->used_nodes) {
		cnodes = setup_cluster_nodes(pg_conn, job_cond);
		if (!cnodes)
			return SLURM_ERROR;
	}

	/* Here we want to order them this way in such a way so it is
	   easy to look for duplicates
	*/
	if(job_cond && !job_cond->duplicates)
		xstrcat(query, " ORDER BY id_job, time_submit DESC;");
	else
		xstrcat(query, " ORDER BY time_submit DESC;");

	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	cluster_job_list = list_create(slurmdb_destroy_job_rec);
	FOR_EACH_ROW {
		char *id;
		int submit;
		bool job_ended = 0;
		slurmdb_step_rec_t *step = NULL;

		id = ROW(JF_ID);
		submit = atoi(ROW(JF_SUBMIT));
		curr_id = atoi(ROW(JF_JOBID));

		if(job_cond && !job_cond->duplicates && curr_id == last_id)
			continue;

		last_id = curr_id;

		/* check the bitmap to see if this is one of the jobs
		   we are looking for */
		if(!good_nodes_from_inx(cnodes,	ROW(JF_NODE_INX), submit))
			continue;

		debug3("as/pg: get_jobs_cond: job %d past node test", curr_id);

		job = slurmdb_create_job_rec();
		list_append(cluster_job_list, job);

		job->alloc_cpus = atoi(ROW(JF_ALLOC_CPUS));
		job->alloc_nodes = atoi(ROW(JF_ALLOC_NODES));
		job->associd = atoi(ROW(JF_ASSOCID));
		job->resvid = atoi(ROW(JF_RESVID));
		job->state = atoi(ROW(JF_STATE));
		job->cluster = xstrdup(cluster);

		/* we want a blank wckey if the name is null */
		if(! ISNULL(JF_WCKEY))
			job->wckey = xstrdup(ROW(JF_WCKEY));
		else
			job->wckey = xstrdup("");
		job->wckeyid = atoi(ROW(JF_WCKEYID));

		if(! ISNULL(JF_USER_NAME))
			job->user = xstrdup(ROW(JF_USER_NAME));
		else
			job->uid = atoi(ROW(JF_UID));

		if(! ISNULL(JF_LFT))
			job->lft = atoi(ROW(JF_LFT));

		if(! ISEMPTY(JF_ACCOUNT))
			job->account = xstrdup(ROW(JF_ACCOUNT));
		else if(! ISEMPTY(JF_ACCOUNT1))
			job->account = xstrdup(ROW(JF_ACCOUNT1));

		if(! ISNULL(JF_BLOCKID))
			job->blockid = xstrdup(ROW(JF_BLOCKID));

		job->eligible = atoi(ROW(JF_ELIGIBLE));
		job->submit = submit;
		job->start = atoi(ROW(JF_START));
		job->end = atoi(ROW(JF_END));
		job->timelimit = atoi(ROW(JF_TIMELIMIT));

		/* since the job->end could be set later end it here */
		if(job->end) {
			job_ended = 1;
			if(!job->start || (job->start > job->end))
				job->start = job->end;
		}

		if(job_cond && !job_cond->without_usage_truncation
		   && job_cond->usage_start) {
			if(job->start && (job->start < job_cond->usage_start))
				job->start = job_cond->usage_start;

			if(!job->end || job->end > job_cond->usage_end)
				job->end = job_cond->usage_end;

			if(!job->start)
				job->start = job->end;

			job->elapsed = job->end - job->start;

			if(ROW(JF_SUSPENDED)) {
				int local_start, local_end;
				/* get the suspended time for this job */
				query = xstrdup_printf(
					"SELECT time_start, time_end "
					"FROM %s.%s WHERE (time_start < %ld "
					"AND (time_end >= %ld "
					"OR time_end = 0)) AND job_db_inx=%s "
					"ORDER BY time_start",
					cluster, suspend_table,
					(long)job_cond->usage_end,
					(long)job_cond->usage_start, id);
				result2 = DEF_QUERY_RET;
				if(!result2) {
					list_destroy(cluster_job_list);
					cluster_job_list = NULL;
					rc = SLURM_ERROR;
					goto out;
				}
				FOR_EACH_ROW2 {
					local_start = atoi(ROW2(0));
					local_end =  atoi(ROW2(1));
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
				} END_EACH_ROW2;
				PQclear(result2);
			}
		} else {
			job->suspended = atoi(ROW(JF_SUSPENDED));

			if (job->state == JOB_SUSPENDED)
				job->suspended = now - job->suspended;

			if(!job->start) {
				job->elapsed = 0;
			} else if(!job->end) {
				job->elapsed = now - job->start;
			} else {
				job->elapsed = job->end - job->start;
			}

			job->elapsed -= job->suspended;
		}

		if((int)job->elapsed < 0)
			job->elapsed = 0;

		job->jobid = curr_id;
		job->jobname = xstrdup(ROW(JF_NAME));
		job->gid = atoi(ROW(JF_GID));
		job->exitcode = atoi(ROW(JF_COMP_CODE));

		if(! ISEMPTY(JF_PARTITION))
			job->partition = xstrdup(ROW(JF_PARTITION));

		if(! ISEMPTY(JF_NODELIST))
			job->nodes = xstrdup(ROW(JF_NODELIST));

		if (!job->nodes || !strcmp(job->nodes, "(null)")) {
			xfree(job->nodes);
			job->nodes = xstrdup("(unknown)");
		}

		job->track_steps = atoi(ROW(JF_TRACKSTEPS));
		job->priority = atoi(ROW(JF_PRIORITY));
		job->req_cpus = atoi(ROW(JF_REQ_CPUS));
		job->requid = atoi(ROW(JF_KILL_REQUID));
		job->qosid = atoi(ROW(JF_QOS));
		job->show_full = 1;

		if(only_pending || (job_cond && job_cond->without_steps))
			goto skip_steps;

		if(job_cond && job_cond->step_list
		   && list_count(job_cond->step_list)) {
			slurmdb_selected_step_t *selected_step = NULL;
			int set = 0;
			ListIterator itr =
				list_iterator_create(job_cond->step_list);
			while((selected_step = list_next(itr))) {
				if(selected_step->jobid != job->jobid) {
					continue;
				} else if (selected_step->stepid
					   == (uint32_t)NO_VAL) {
					job->show_full = 1;
					break;
				}

				if(set)
					xstrcat(cond, " OR ");
				else
					xstrcat(cond, " AND (");

				xstrfmtcat(cond, "t1.stepid=%u",
					   selected_step->stepid);
				set = 1;
				job->show_full = 0;
			}
			list_iterator_destroy(itr);
			if(set)
				xstrcat(cond, ")");
		}
		query =	xstrdup_printf(
			"SELECT %s FROM %s.%s AS t1 WHERE t1.job_db_inx=%s",
			step_fields, cluster, step_table, id);
		if(cond) {
			xstrcat(query, cond);
			xfree(cond);
		}

		result2 = DEF_QUERY_RET;
		if(!result2) {
			list_destroy(cluster_job_list);
			cluster_job_list = NULL;
			rc = SLURM_ERROR;
			goto out;
		}

		/* Querying the steps in the fashion was faster than
		   doing only 1 query and then matching the steps up
		   later with the job.
		*/
		FOR_EACH_ROW2 {
			/* check the bitmap to see if this is one of the steps
			   we are looking for */
			if(!good_nodes_from_inx(cnodes,	ROW2(SF_NODE_INX),
						submit))
				continue;

			step = slurmdb_create_step_rec();
			step->tot_cpu_sec = 0;
			step->tot_cpu_usec = 0;
			step->job_ptr = job;
			if(!job->first_step_ptr)
				job->first_step_ptr = step;
			list_append(job->steps, step);
			step->stepid = atoi(ROW2(SF_STEPID));
			/* info("got step %u.%u", */
/* 			     job->header.jobnum, step->stepnum); */
			step->state = atoi(ROW2(SF_STATE));
			step->exitcode = atoi(ROW2(SF_COMP_CODE));
			step->ncpus = atoi(ROW2(SF_CPUS));
			step->nnodes = atoi(ROW2(SF_NODES));

			step->ntasks = atoi(ROW2(SF_TASKS));
			step->task_dist = atoi(ROW2(SF_TASKDIST));
			if(!step->ntasks)
				step->ntasks = step->ncpus;

			step->start = atoi(ROW2(SF_START));

			step->end = atoi(ROW2(SF_END));
			/* if the job has ended end the step also */
			if(!step->end && job_ended) {
				step->end = job->end;
				step->state = job->state;
			}

			if(job_cond && !job_cond->without_usage_truncation
			   && job_cond->usage_start) {
				if(step->start
				   && (step->start < job_cond->usage_start))
					step->start = job_cond->usage_start;

				if(!step->start && step->end)
					step->start = step->end;

				if(!step->end
				   || (step->end > job_cond->usage_end))
					step->end = job_cond->usage_end;
			}

			/* figure this out by start stop */
			step->suspended = atoi(ROW2(SF_SUSPENDED));
			if(!step->end) {
				step->elapsed = now - step->start;
			} else {
				step->elapsed = step->end - step->start;
			}
			step->elapsed -= step->suspended;

			if((int)step->elapsed < 0)
				step->elapsed = 0;

			step->user_cpu_sec = atoi(ROW2(SF_USER_SEC));
			step->user_cpu_usec =
				atoi(ROW2(SF_USER_USEC));
			step->sys_cpu_sec = atoi(ROW2(SF_SYS_SEC));
			step->sys_cpu_usec = atoi(ROW2(SF_SYS_USEC));
			step->tot_cpu_sec +=
				step->user_cpu_sec + step->sys_cpu_sec;
			step->tot_cpu_usec +=
				step->user_cpu_usec + step->sys_cpu_usec;
			step->stats.vsize_max =
				atoi(ROW2(SF_MAX_VSIZE));
			step->stats.vsize_max_taskid =
				atoi(ROW2(SF_MAX_VSIZE_TASK));
			step->stats.vsize_ave =
				atof(ROW2(SF_AVE_VSIZE));
			step->stats.rss_max =
				atoi(ROW2(SF_MAX_RSS));
			step->stats.rss_max_taskid =
				atoi(ROW2(SF_MAX_RSS_TASK));
			step->stats.rss_ave =
				atof(ROW2(SF_AVE_RSS));
			step->stats.pages_max =
				atoi(ROW2(SF_MAX_PAGES));
			step->stats.pages_max_taskid =
				atoi(ROW2(SF_MAX_PAGES_TASK));
			step->stats.pages_ave =
				atof(ROW2(SF_AVE_PAGES));
			step->stats.cpu_min =
				atoi(ROW2(SF_MIN_CPU));
			step->stats.cpu_min_taskid =
				atoi(ROW2(SF_MIN_CPU_TASK));
			step->stats.cpu_ave = atof(ROW2(SF_AVE_CPU));
			step->stepname = xstrdup(ROW2(SF_NAME));
			step->nodes = xstrdup(ROW2(SF_NODELIST));
			step->stats.vsize_max_nodeid =
				atoi(ROW2(SF_MAX_VSIZE_NODE));
			step->stats.rss_max_nodeid =
				atoi(ROW2(SF_MAX_RSS_NODE));
			step->stats.pages_max_nodeid =
				atoi(ROW2(SF_MAX_PAGES_NODE));
			step->stats.cpu_min_nodeid =
				atoi(ROW2(SF_MIN_CPU_NODE));

			step->requid = atoi(ROW2(SF_KILL_REQUID));
		} END_EACH_ROW2;
		PQclear(result2);

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
	skip_steps:
		step = NULL;
	} END_EACH_ROW;
	PQclear(result);

	if (cluster_job_list) {
		list_transfer(sent_list, cluster_job_list);
		list_destroy(cluster_job_list);
	}
out:
	if (cnodes)
		destroy_cluster_nodes(cnodes);

	return rc;
}

/*
 * js_pg_get_jobs_cond - get jobs
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN job_cond: condition of jobs to get
 * RET: list of jobs
 */
extern List
js_pg_get_jobs_cond(pgsql_conn_t *pg_conn, uid_t uid,
		    slurmdb_job_cond_t *job_cond)
{

	int is_admin = 1, only_pending = 0;
	slurmdb_user_rec_t user;
	List job_list = list_create(slurmdb_destroy_job_rec);
	char *cond = NULL, *extra_table = NULL;

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if (check_user_op(pg_conn, uid, PRIVATE_DATA_JOBS, &is_admin, &user)
	    != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found in db", uid);
		errno = ESLURM_USER_ID_MISSING;
		return NULL;
	}

	if(job_cond->state_list && (list_count(job_cond->state_list) == 1)
	   && (atoi(list_peek(job_cond->state_list)) == JOB_PENDING))
		only_pending = 1;

	_make_job_cond_str(pg_conn, job_cond, &extra_table, &cond);

	_init_req_fields();

	job_list = list_create(slurmdb_destroy_job_rec);
	FOR_EACH_CLUSTER(job_cond->cluster_list) {
		int rc;
		if (job_cond->cluster_list &&
		    list_count(job_cond->cluster_list) &&
		    !cluster_in_db(pg_conn, cluster_name)) {
			error("cluster %s not found in db", cluster_name);
			errno = ESLURM_CLUSTER_DELETED;
			list_destroy(job_list);
			job_list = NULL;
			break;
		}
		rc = _cluster_get_jobs(pg_conn, cluster_name, job_cond, &user,
				       is_admin, cond, extra_table, only_pending,
				       job_list);
		if (rc != SLURM_SUCCESS) {
			list_destroy(job_list);
			job_list = NULL;
			break;
		}
	} END_EACH_CLUSTER;

	return job_list;
}
