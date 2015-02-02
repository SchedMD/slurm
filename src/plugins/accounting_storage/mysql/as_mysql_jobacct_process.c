/*****************************************************************************\
 *  as_mysql_jobacct_process.c - functions the processing of
 *                               information from the as_mysql jobacct
 *                               storage.
 *****************************************************************************
 *
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "as_mysql_jobacct_process.h"

typedef struct {
	hostlist_t hl;
	time_t start;
	time_t end;
	bitstr_t *asked_bitmap;
} local_cluster_t;

/* if this changes you will need to edit the corresponding
 * enum below also t1 is job_table */
char *job_req_inx[] = {
	"t1.account",
	"t1.array_max_tasks",
	"t1.array_task_str",
	"t1.cpus_alloc",
	"t1.cpus_req",
	"t1.derived_ec",
	"t1.derived_es",
	"t1.exit_code",
	"t1.id_array_job",
	"t1.id_array_task",
	"t1.id_assoc",
	"t1.id_block",
	"t1.id_group",
	"t1.id_job",
	"t1.id_qos",
	"t1.id_resv",
	"t3.resv_name",
	"t1.id_user",
	"t1.id_wckey",
	"t1.job_db_inx",
	"t1.job_name",
	"t1.kill_requid",
	"t1.mem_req",
	"t1.node_inx",
	"t1.nodelist",
	"t1.nodes_alloc",
	"t1.partition",
	"t1.priority",
	"t1.state",
	"t1.time_eligible",
	"t1.time_end",
	"t1.time_start",
	"t1.time_submit",
	"t1.time_suspended",
	"t1.timelimit",
	"t1.track_steps",
	"t1.wckey",
	"t1.gres_alloc",
	"t1.gres_req",
	"t1.gres_used",
	"t2.acct",
	"t2.lft",
	"t2.user"
};

enum {
	JOB_REQ_ACCOUNT1,
	JOB_REQ_ARRAY_MAX,
	JOB_REQ_ARRAY_STR,
	JOB_REQ_ALLOC_CPUS,
	JOB_REQ_REQ_CPUS,
	JOB_REQ_DERIVED_EC,
	JOB_REQ_DERIVED_ES,
	JOB_REQ_EXIT_CODE,
	JOB_REQ_ARRAYJOBID,
	JOB_REQ_ARRAYTASKID,
	JOB_REQ_ASSOCID,
	JOB_REQ_BLOCKID,
	JOB_REQ_GID,
	JOB_REQ_JOBID,
	JOB_REQ_QOS,
	JOB_REQ_RESVID,
	JOB_REQ_RESV_NAME,
	JOB_REQ_UID,
	JOB_REQ_WCKEYID,
	JOB_REQ_ID,
	JOB_REQ_NAME,
	JOB_REQ_KILL_REQUID,
	JOB_REQ_REQ_MEM,
	JOB_REQ_NODE_INX,
	JOB_REQ_NODELIST,
	JOB_REQ_ALLOC_NODES,
	JOB_REQ_PARTITION,
	JOB_REQ_PRIORITY,
	JOB_REQ_STATE,
	JOB_REQ_ELIGIBLE,
	JOB_REQ_END,
	JOB_REQ_START,
	JOB_REQ_SUBMIT,
	JOB_REQ_SUSPENDED,
	JOB_REQ_TIMELIMIT,
	JOB_REQ_TRACKSTEPS,
	JOB_REQ_WCKEY,
	JOB_REQ_GRES_ALLOC,
	JOB_REQ_GRES_REQ,
	JOB_REQ_GRES_USED,
	JOB_REQ_ACCOUNT,
	JOB_REQ_LFT,
	JOB_REQ_USER_NAME,
	JOB_REQ_COUNT
};

/* if this changes you will need to edit the corresponding
 * enum below also t1 is step_table */
char *step_req_inx[] = {
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
	"t1.max_disk_read",
	"t1.max_disk_read_task",
	"t1.max_disk_read_node",
	"t1.ave_disk_read",
	"t1.max_disk_write",
	"t1.max_disk_write_task",
	"t1.max_disk_write_node",
	"t1.ave_disk_write",
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
	"t1.act_cpufreq",
	"t1.consumed_energy",
	"t1.req_cpufreq"
};

enum {
	STEP_REQ_STEPID,
	STEP_REQ_START,
	STEP_REQ_END,
	STEP_REQ_SUSPENDED,
	STEP_REQ_NAME,
	STEP_REQ_NODELIST,
	STEP_REQ_NODE_INX,
	STEP_REQ_STATE,
	STEP_REQ_KILL_REQUID,
	STEP_REQ_EXIT_CODE,
	STEP_REQ_NODES,
	STEP_REQ_CPUS,
	STEP_REQ_TASKS,
	STEP_REQ_TASKDIST,
	STEP_REQ_USER_SEC,
	STEP_REQ_USER_USEC,
	STEP_REQ_SYS_SEC,
	STEP_REQ_SYS_USEC,
	STEP_REQ_MAX_DISK_READ,
	STEP_REQ_MAX_DISK_READ_TASK,
	STEP_REQ_MAX_DISK_READ_NODE,
	STEP_REQ_AVE_DISK_READ,
	STEP_REQ_MAX_DISK_WRITE,
	STEP_REQ_MAX_DISK_WRITE_TASK,
	STEP_REQ_MAX_DISK_WRITE_NODE,
	STEP_REQ_AVE_DISK_WRITE,
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
	STEP_REQ_ACT_CPUFREQ,
	STEP_REQ_CONSUMED_ENERGY,
	STEP_REQ_REQ_CPUFREQ,
	STEP_REQ_COUNT
};

static void _state_time_string(char **extra, uint32_t state,
			       uint32_t start, uint32_t end)
{
	int base_state = state & JOB_STATE_BASE;

	if (!start && !end) {
		xstrfmtcat(*extra, "t1.state='%u'", state);
		return;
	}

 	switch(base_state) {
	case JOB_PENDING:
		if (start) {
			if (!end) {
				xstrfmtcat(*extra,
					   "(t1.time_eligible && "
					   "((!t1.time_start && !t1.time_end) "
					   "|| (%d between t1.time_eligible "
					   "and t1.time_start)))",
					   start);
			} else {
				xstrfmtcat(*extra,
					   "(t1.time_eligible && ((%d between "
					   "t1.time_eligible and "
					   "t1.time_start) || "
					   "(t1.time_eligible "
					   "between %d and %d)) || "
					   "(!t1.time_start && (%d between "
					   "t1.time_eligible and "
					   "t1.time_end)))",
					   start, start,
					   end, start);
			}
		} else if (end) {
			xstrfmtcat(*extra, "(t1.time_eligible && "
				   "t1.time_eligible < %d)",
				   end);
		}
		break;
	case JOB_SUSPENDED:
		/* Handle this the same way we handle RUNNING. */
	case JOB_RUNNING:
		if (start) {
			if (!end) {
				xstrfmtcat(*extra,
					   "(t1.time_start && "
					   "((!t1.time_end && t1.state=%d) || "
					   "(%d between t1.time_start "
					   "and t1.time_end)))",
					   base_state, start);
			} else {
				xstrfmtcat(*extra,
					   "(t1.time_start && "
					   "((%d between t1.time_start "
					   "and t1.time_end) "
					   "|| (t1.time_start between "
					   "%d and %d)))",
					   start, start,
					   end);
			}
		} else if (end) {
			xstrfmtcat(*extra, "(t1.time_start && "
				   "t1.time_start < %d)", end);
		}
		break;
	case JOB_COMPLETE:
	case JOB_CANCELLED:
	case JOB_FAILED:
	case JOB_TIMEOUT:
	case JOB_NODE_FAIL:
	case JOB_PREEMPTED:
	default:
		xstrfmtcat(*extra, "(t1.state='%u' && (t1.time_end && ", state);
		if (start) {
			if (!end) {
				xstrfmtcat(*extra, "(t1.time_end >= %d)))",
					   start);
			} else {
				xstrfmtcat(*extra,
					   "(t1.time_end between %d and %d)))",
					   start, end);
			}
		} else if (end) {
			xstrfmtcat(*extra, "(t1.time_end <= %d)))", end);
		}
		break;
	}

	return;
}

static void _destroy_local_cluster(void *object)
{
	local_cluster_t *local_cluster = (local_cluster_t *)object;
	if (local_cluster) {
		if (local_cluster->hl)
			hostlist_destroy(local_cluster->hl);
		FREE_NULL_BITMAP(local_cluster->asked_bitmap);
		xfree(local_cluster);
	}
}

static int _cluster_get_jobs(mysql_conn_t *mysql_conn,
			     slurmdb_user_rec_t *user,
			     slurmdb_job_cond_t *job_cond,
			     char *cluster_name,
			     char *job_fields, char *step_fields,
			     char *sent_extra,
			     bool is_admin, int only_pending, List sent_list)
{
	char *query = NULL;
	char *extra = xstrdup(sent_extra);
	uint16_t private_data = slurm_get_private_data();
	slurmdb_selected_step_t *selected_step = NULL;
	MYSQL_RES *result = NULL, *step_result = NULL;
	MYSQL_ROW row, step_row;
	slurmdb_job_rec_t *job = NULL;
	slurmdb_step_rec_t *step = NULL;
	time_t now = time(NULL);
	List job_list = list_create(slurmdb_destroy_job_rec);
	ListIterator itr = NULL;
	List local_cluster_list = NULL;
	int set = 0;
	char *prefix="t2";
	int rc = SLURM_SUCCESS;
	int last_id = -1, curr_id = -1;
	local_cluster_t *curr_cluster = NULL;

	/* This is here to make sure we are looking at only this user
	 * if this flag is set.  We also include any accounts they may be
	 * coordinator of.
	 */
	if (!is_admin && (private_data & PRIVATE_DATA_JOBS)) {
		query = xstrdup_printf("select lft from \"%s_%s\" "
				       "where user='%s'",
				       cluster_name, assoc_table, user->name);
		if (user->coord_accts) {
			slurmdb_coord_rec_t *coord = NULL;
			itr = list_iterator_create(user->coord_accts);
			while ((coord = list_next(itr))) {
				xstrfmtcat(query, " || acct='%s'",
					   coord->name);
			}
			list_iterator_destroy(itr);
		}
		if (debug_flags & DEBUG_FLAG_DB_JOB)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(extra);
			xfree(query);
			rc = SLURM_ERROR;
			goto end_it;
		}
		xfree(query);
		set = 0;
		while ((row = mysql_fetch_row(result))) {
			if (set) {
				xstrfmtcat(extra,
					   " || (%s between %s.lft and %s.rgt)",
					   row[0], prefix, prefix);
			} else {
				set = 1;
				if (extra)
					xstrfmtcat(extra,
						   " && ((%s between %s.lft "
						   "and %s.rgt)",
						   row[0], prefix,
						   prefix);
				else
					xstrfmtcat(extra,
						   " where ((%s between %s.lft "
						   "and %s.rgt)",
						   row[0], prefix,
						   prefix);
			}
		}

		mysql_free_result(result);

		if (set)
			xstrcat(extra, ")");
		else {
			xfree(extra);
			debug("User %s has no assocations, and is not admin, "
			      "so not returning any jobs.", user->name);
			/* This user has no valid associations, so
			 * they will not have any jobs. */
			goto end_it;
		}
	}

	setup_job_cluster_cond_limits(mysql_conn, job_cond,
				      cluster_name, &extra);

	query = xstrdup_printf("select %s from \"%s_%s\" as t1 "
			       "left join \"%s_%s\" as t2 "
			       "on t1.id_assoc=t2.id_assoc "
			       "left join \"%s_%s\" as t3 "
			       " on t1.id_resv=t3.id_resv ",
			       job_fields, cluster_name, job_table,
			       cluster_name, assoc_table,
			       cluster_name, resv_table);
	if (extra) {
		xstrcat(query, extra);
		xfree(extra);
	}

	/* Here we want to order them this way in such a way so it is
	   easy to look for duplicates, it is also easy to sort the
	   resized jobs.
	*/
	xstrcat(query, " group by id_job, time_submit desc");

	if (debug_flags & DEBUG_FLAG_DB_JOB)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		rc = SLURM_ERROR;
		goto end_it;
	}
	xfree(query);


	/* Here we set up environment to check used nodes of jobs.
	   Since we store the bitmap of the entire cluster we can use
	   that to set up a hostlist and set up the bitmap to make
	   things work.  This should go before the setup of conds
	   since we could update the start/end time.
	*/
	if (job_cond && job_cond->used_nodes) {
		local_cluster_list = setup_cluster_list_with_inx(
			mysql_conn, job_cond, (void **)&curr_cluster);
		if (!local_cluster_list) {
			rc = SLURM_ERROR;
			goto end_it;
		}
	}

	while ((row = mysql_fetch_row(result))) {
		char *id = row[JOB_REQ_ID];
		bool job_ended = 0;
		int start = slurm_atoul(row[JOB_REQ_START]);

		curr_id = slurm_atoul(row[JOB_REQ_JOBID]);

		if (job_cond && !job_cond->duplicates
		    && (curr_id == last_id)
		    && (slurm_atoul(row[JOB_REQ_STATE]) != JOB_RESIZING))
			continue;

		/* check the bitmap to see if this is one of the jobs
		   we are looking for */
		/* Use start time instead of submit time because node
		 * indexes are determined at start time and not submit. */
		if (!good_nodes_from_inx(local_cluster_list,
					 (void **)&curr_cluster,
					 row[JOB_REQ_NODE_INX], start)) {
			last_id = curr_id;
			continue;
		}

		job = slurmdb_create_job_rec();
		job->state = slurm_atoul(row[JOB_REQ_STATE]);
		if (curr_id == last_id)
			/* put in reverse so we order by the submit getting
			   larger which it is given to us in reverse
			   order from the database */
			list_prepend(job_list, job);
		else
			list_append(job_list, job);
		last_id = curr_id;

		job->alloc_cpus = slurm_atoul(row[JOB_REQ_ALLOC_CPUS]);
		if (row[JOB_REQ_GRES_ALLOC])
			job->alloc_gres = xstrdup(row[JOB_REQ_GRES_ALLOC]);
		else
			job->alloc_gres = xstrdup("");
		job->alloc_nodes = slurm_atoul(row[JOB_REQ_ALLOC_NODES]);
		job->associd = slurm_atoul(row[JOB_REQ_ASSOCID]);
		job->array_job_id = slurm_atoul(row[JOB_REQ_ARRAYJOBID]);
		job->array_task_id = slurm_atoul(row[JOB_REQ_ARRAYTASKID]);
		job->resvid = slurm_atoul(row[JOB_REQ_RESVID]);

		if (row[JOB_REQ_RESV_NAME] && row[JOB_REQ_RESV_NAME][0])
			job->resv_name = xstrdup(row[JOB_REQ_RESV_NAME]);

		job->cluster = xstrdup(cluster_name);

		/* we want a blank wckey if the name is null */
		if (row[JOB_REQ_WCKEY])
			job->wckey = xstrdup(row[JOB_REQ_WCKEY]);
		else
			job->wckey = xstrdup("");
		job->wckeyid = slurm_atoul(row[JOB_REQ_WCKEYID]);

		if (row[JOB_REQ_USER_NAME])
			job->user = xstrdup(row[JOB_REQ_USER_NAME]);
		else
			job->uid = slurm_atoul(row[JOB_REQ_UID]);

		if (row[JOB_REQ_LFT])
			job->lft = slurm_atoul(row[JOB_REQ_LFT]);

		if (row[JOB_REQ_ACCOUNT] && row[JOB_REQ_ACCOUNT][0])
			job->account = xstrdup(row[JOB_REQ_ACCOUNT]);
		else if (row[JOB_REQ_ACCOUNT1] && row[JOB_REQ_ACCOUNT1][0])
			job->account = xstrdup(row[JOB_REQ_ACCOUNT1]);

		if (row[JOB_REQ_ARRAY_STR] && row[JOB_REQ_ARRAY_STR][0])
			job->array_task_str = xstrdup(row[JOB_REQ_ARRAY_STR]);

		if (row[JOB_REQ_ARRAY_MAX])
			job->array_max_tasks =
				slurm_atoul(row[JOB_REQ_ARRAY_MAX]);

		if (row[JOB_REQ_BLOCKID])
			job->blockid = xstrdup(row[JOB_REQ_BLOCKID]);

		job->eligible = slurm_atoul(row[JOB_REQ_ELIGIBLE]);
		job->submit = slurm_atoul(row[JOB_REQ_SUBMIT]);
		job->start = start;
		job->end = slurm_atoul(row[JOB_REQ_END]);
		job->timelimit = slurm_atoul(row[JOB_REQ_TIMELIMIT]);

		/* since the job->end could be set later end it here */
		if (job->end) {
			job_ended = 1;
			if (!job->start || (job->start > job->end))
				job->start = job->end;
		}

		if (job_cond && !job_cond->without_usage_truncation
		    && job_cond->usage_start) {
			if (job->start && (job->start < job_cond->usage_start))
				job->start = job_cond->usage_start;

			if (!job->end || job->end > job_cond->usage_end)
				job->end = job_cond->usage_end;

			if (!job->start)
				job->start = job->end;

			job->elapsed = job->end - job->start;

			if (row[JOB_REQ_SUSPENDED]) {
				MYSQL_RES *result2 = NULL;
				MYSQL_ROW row2;
				/* get the suspended time for this job */
				query = xstrdup_printf(
					"select time_start, time_end from "
					"\"%s_%s\" where "
					"(time_start < %ld && (time_end >= %ld "
					"|| time_end = 0)) && job_db_inx=%s "
					"order by time_start",
					cluster_name, suspend_table,
					job_cond->usage_end,
					job_cond->usage_start,
					id);

				debug4("%d(%s:%d) query\n%s",
				       mysql_conn->conn, THIS_FILE,
				       __LINE__, query);
				if (!(result2 = mysql_db_query_ret(
					      mysql_conn,
					      query, 0))) {
					list_destroy(job_list);
					job_list = NULL;
					break;
				}
				xfree(query);
				while ((row2 = mysql_fetch_row(result2))) {
					time_t local_start =
						slurm_atoul(row2[0]);
					time_t local_end =
						slurm_atoul(row2[1]);

					if (!local_start)
						continue;

					if (job->start > local_start)
						local_start = job->start;
					if (job->end < local_end)
						local_end = job->end;

					if ((local_end - local_start) < 1)
						continue;

					job->elapsed -=
						(local_end - local_start);
					job->suspended +=
						(local_end - local_start);
				}
				mysql_free_result(result2);

			}
		} else {
			job->suspended = slurm_atoul(row[JOB_REQ_SUSPENDED]);

			/* fix the suspended number to be correct */
			if (job->state == JOB_SUSPENDED)
				job->suspended = now - job->suspended;
			if (!job->start) {
				job->elapsed = 0;
			} else if (!job->end) {
				job->elapsed = now - job->start;
			} else {
				job->elapsed = job->end - job->start;
			}

			job->elapsed -= job->suspended;
		}

		if ((int)job->elapsed < 0)
			job->elapsed = 0;

		job->jobid = curr_id;
		job->jobname = xstrdup(row[JOB_REQ_NAME]);
		job->gid = slurm_atoul(row[JOB_REQ_GID]);
		job->exitcode = slurm_atoul(row[JOB_REQ_EXIT_CODE]);
		job->derived_ec = slurm_atoul(row[JOB_REQ_DERIVED_EC]);
		job->derived_es = xstrdup(row[JOB_REQ_DERIVED_ES]);

		if (row[JOB_REQ_PARTITION])
			job->partition = xstrdup(row[JOB_REQ_PARTITION]);

		if (row[JOB_REQ_NODELIST])
			job->nodes = xstrdup(row[JOB_REQ_NODELIST]);

		if (!job->nodes || !strcmp(job->nodes, "(null)")) {
			xfree(job->nodes);
			job->nodes = xstrdup("(unknown)");
		}

		job->track_steps = slurm_atoul(row[JOB_REQ_TRACKSTEPS]);
		job->priority = slurm_atoul(row[JOB_REQ_PRIORITY]);
		job->req_cpus = slurm_atoul(row[JOB_REQ_REQ_CPUS]);
		if (row[JOB_REQ_GRES_REQ])
			job->req_gres = xstrdup(row[JOB_REQ_GRES_REQ]);
		else
			job->req_gres = xstrdup("");
		job->req_mem = slurm_atoul(row[JOB_REQ_REQ_MEM]);
		job->requid = slurm_atoul(row[JOB_REQ_KILL_REQUID]);
		job->qosid = slurm_atoul(row[JOB_REQ_QOS]);
		job->show_full = 1;

		if (only_pending || (job_cond && job_cond->without_steps))
			goto skip_steps;

		if (job_cond && job_cond->step_list
		    && list_count(job_cond->step_list)) {
			set = 0;
			itr = list_iterator_create(job_cond->step_list);
			while ((selected_step = list_next(itr))) {
				if ((selected_step->jobid != job->jobid) &&
				    (selected_step->jobid !=
				     job->array_job_id)) {
					continue;
				} else if (selected_step->stepid == NO_VAL) {
					job->show_full = 1;
					break;
				} else if (selected_step->stepid == INFINITE)
					selected_step->stepid =
						SLURM_BATCH_SCRIPT;

				if (set)
					xstrcat(extra, " || ");
				else
					xstrcat(extra, " && (");

				/* The stepid could be -2 so use %d not %u */
				xstrfmtcat(extra, "t1.id_step=%d",
					   selected_step->stepid);
				set = 1;
				job->show_full = 0;
				/* Set it back just in case we are
				   looking at a job array.
				*/
				if (selected_step->stepid == SLURM_BATCH_SCRIPT)
					selected_step->stepid = INFINITE;
			}
			list_iterator_destroy(itr);
			if (set)
				xstrcat(extra, ")");
		}
		query =	xstrdup_printf("select %s from \"%s_%s\" as t1 "
				       "where t1.job_db_inx=%s",
				       step_fields, cluster_name,
				       step_table, id);
		if (extra) {
			xstrcat(query, extra);
			xfree(extra);
		}

		debug4("%d(%s:%d) query\n%s",
		       mysql_conn->conn, THIS_FILE, __LINE__, query);

		if (!(step_result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			rc = SLURM_ERROR;
			goto end_it;
		}
		xfree(query);

		/* Querying the steps in the fashion was faster than
		   doing only 1 query and then matching the steps up
		   later with the job.
		*/
		while ((step_row = mysql_fetch_row(step_result))) {
			/* check the bitmap to see if this is one of the steps
			   we are looking for */
			if (!good_nodes_from_inx(local_cluster_list,
						 (void **)&curr_cluster,
						 step_row[STEP_REQ_NODE_INX],
						 start))
				continue;

			step = slurmdb_create_step_rec();
			step->tot_cpu_sec = 0;
			step->tot_cpu_usec = 0;
			step->job_ptr = job;
			if (!job->first_step_ptr)
				job->first_step_ptr = step;
			list_append(job->steps, step);
			step->stepid = slurm_atoul(step_row[STEP_REQ_STEPID]);
			/* info("got step %u.%u", */
/* 			     job->header.jobnum, step->stepnum); */
			step->state = slurm_atoul(step_row[STEP_REQ_STATE]);
			step->exitcode =
				slurm_atoul(step_row[STEP_REQ_EXIT_CODE]);
			step->ncpus = slurm_atoul(step_row[STEP_REQ_CPUS]);
			step->nnodes = slurm_atoul(step_row[STEP_REQ_NODES]);

			step->ntasks = slurm_atoul(step_row[STEP_REQ_TASKS]);
			step->task_dist =
				slurm_atoul(step_row[STEP_REQ_TASKDIST]);
			if (!step->ntasks)
				step->ntasks = step->ncpus;

			step->start = slurm_atoul(step_row[STEP_REQ_START]);

			step->end = slurm_atoul(step_row[STEP_REQ_END]);
			/* if the job has ended end the step also */
			if (!step->end && job_ended) {
				step->end = job->end;
				step->state = job->state;
			}

			if (job_cond && !job_cond->without_usage_truncation
			    && job_cond->usage_start) {
				if (step->start
				    && (step->start < job_cond->usage_start))
					step->start = job_cond->usage_start;

				if (!step->start && step->end)
					step->start = step->end;

				if (!step->end
				    || (step->end > job_cond->usage_end))
					step->end = job_cond->usage_end;
			}

			/* figure this out by start stop */
			step->suspended =
				slurm_atoul(step_row[STEP_REQ_SUSPENDED]);
			if (!step->start) {
				step->elapsed = 0;
			} else if (!step->end) {
				step->elapsed = now - step->start;
			} else {
				step->elapsed = step->end - step->start;
			}
			step->elapsed -= step->suspended;

			if ((int)step->elapsed < 0)
				step->elapsed = 0;

			step->req_cpufreq =
				slurm_atoul(step_row[STEP_REQ_REQ_CPUFREQ]);
			step->stepname = xstrdup(step_row[STEP_REQ_NAME]);
			step->nodes = xstrdup(step_row[STEP_REQ_NODELIST]);
			step->requid =
				slurm_atoul(step_row[STEP_REQ_KILL_REQUID]);

			step->stats.cpu_min = slurm_atoul(
				step_row[STEP_REQ_MIN_CPU]);

			if (step->stats.cpu_min != NO_VAL) {
				step->user_cpu_sec = slurm_atoul(
					step_row[STEP_REQ_USER_SEC]);
				step->user_cpu_usec = slurm_atoul(
					step_row[STEP_REQ_USER_USEC]);
				step->sys_cpu_sec = slurm_atoul(
					step_row[STEP_REQ_SYS_SEC]);
				step->sys_cpu_usec = slurm_atoul(
					step_row[STEP_REQ_SYS_USEC]);
				step->tot_cpu_sec +=
					step->user_cpu_sec + step->sys_cpu_sec;
				step->tot_cpu_usec += step->user_cpu_usec +
					step->sys_cpu_usec;
				step->stats.disk_read_max =
					atof(step_row[STEP_REQ_MAX_DISK_READ]);
				step->stats.disk_read_max_taskid = slurm_atoul(
					step_row[STEP_REQ_MAX_DISK_READ_TASK]);
				step->stats.disk_read_ave =
					atof(step_row[STEP_REQ_AVE_DISK_READ]);
				step->stats.disk_write_max =
					atof(step_row[STEP_REQ_MAX_DISK_WRITE]);
				step->stats.disk_write_max_taskid = slurm_atoul(
					step_row[STEP_REQ_MAX_DISK_WRITE_TASK]);
				step->stats.disk_write_ave =
					atof(step_row[STEP_REQ_AVE_DISK_WRITE]);
				step->stats.vsize_max = slurm_atoul(
					step_row[STEP_REQ_MAX_VSIZE]);
				step->stats.vsize_max_taskid = slurm_atoul(
					step_row[STEP_REQ_MAX_VSIZE_TASK]);
				step->stats.vsize_ave =
					atof(step_row[STEP_REQ_AVE_VSIZE]);
				step->stats.rss_max =
					slurm_atoul(step_row[STEP_REQ_MAX_RSS]);
				step->stats.rss_max_taskid = slurm_atoul(
					step_row[STEP_REQ_MAX_RSS_TASK]);
				step->stats.rss_ave =
					atof(step_row[STEP_REQ_AVE_RSS]);
				step->stats.pages_max = slurm_atoul(
					step_row[STEP_REQ_MAX_PAGES]);
				step->stats.pages_max_taskid = slurm_atoul(
					step_row[STEP_REQ_MAX_PAGES_TASK]);
				step->stats.pages_ave =
					atof(step_row[STEP_REQ_AVE_PAGES]);
				step->stats.cpu_min_taskid = slurm_atoul(
					step_row[STEP_REQ_MIN_CPU_TASK]);
				step->stats.cpu_ave = atof(
					step_row[STEP_REQ_AVE_CPU]);
				step->stats.act_cpufreq =
					atof(step_row[STEP_REQ_ACT_CPUFREQ]);
				step->stats.consumed_energy = atof(
					step_row[STEP_REQ_CONSUMED_ENERGY]);
				step->stats.vsize_max_nodeid = slurm_atoul(
					step_row[STEP_REQ_MAX_VSIZE_NODE]);
				step->stats.rss_max_nodeid = slurm_atoul(
					step_row[STEP_REQ_MAX_RSS_NODE]);
				step->stats.pages_max_nodeid = slurm_atoul(
					step_row[STEP_REQ_MAX_PAGES_NODE]);
				step->stats.cpu_min_nodeid = slurm_atoul(
					step_row[STEP_REQ_MIN_CPU_NODE]);
			}
		}
		mysql_free_result(step_result);

		if (!job->track_steps) {
			/* If we don't have track_steps we want to see
			   if we have multiple steps.  If we only have
			   1 step check the job name against the step
			   name in most all cases it will be
			   different.  If it is different print out
			   the step separate.
			*/
			if (list_count(job->steps) > 1)
				job->track_steps = 1;
			else if (step && step->stepname && job->jobname) {
				if (strcmp(step->stepname, job->jobname))
					job->track_steps = 1;
			}
		}
	skip_steps:
		/* need to reset here to make the above test valid */
		step = NULL;
	}
	mysql_free_result(result);

end_it:
	if (local_cluster_list)
		list_destroy(local_cluster_list);

	if (rc == SLURM_SUCCESS)
		list_transfer(sent_list, job_list);

	list_destroy(job_list);
	return rc;
}

extern List setup_cluster_list_with_inx(mysql_conn_t *mysql_conn,
					slurmdb_job_cond_t *job_cond,
					void **curr_cluster)
{
	List local_cluster_list = NULL;
	time_t now = time(NULL);
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	hostlist_t temp_hl = NULL;
	hostlist_iterator_t h_itr = NULL;
	char *query = NULL;
	int dims = 0;

	if (!job_cond || !job_cond->used_nodes)
		return NULL;

	if (!job_cond->cluster_list
	    || list_count(job_cond->cluster_list) != 1) {
		error("If you are doing a query against nodes "
		      "you must only have 1 cluster "
		      "you are asking for.");
		return NULL;
	}

	/* get the dimensions of this cluster so we know how to deal
	   with the hostlists */
	query = xstrdup_printf("select dimensions, flags from %s where "
			       "name='%s'",
			       cluster_table,
			       (char *)list_peek(job_cond->cluster_list));

	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	if (!(row = mysql_fetch_row(result))) {
		error("Couldn't get the dimensions of cluster '%s'.",
		      (char *)list_peek(job_cond->cluster_list));
		mysql_free_result(result);
		return NULL;
	}

	/* On a Cray System when dealing with hostlists as we are here
	   this always needs to be 1.
	*/
	if (slurm_atoul(row[1]) & CLUSTER_FLAG_CRAY_A)
		dims = 1;
	else
		dims = atoi(row[0]);

	mysql_free_result(result);

	temp_hl = hostlist_create_dims(job_cond->used_nodes, dims);
	if (hostlist_count(temp_hl) <= 0) {
		error("we didn't get any real hosts to look for.");
		goto no_hosts;
	}
	h_itr = hostlist_iterator_create(temp_hl);

	query = xstrdup_printf("select cluster_nodes, time_start, "
			       "time_end from \"%s_%s\" where node_name='' "
			       "&& cluster_nodes !=''",
			       (char *)list_peek(job_cond->cluster_list),
			       event_table);

	if (job_cond->usage_start) {
		if (!job_cond->usage_end)
			job_cond->usage_end = now;

		xstrfmtcat(query,
			   " && ((time_start < %ld) "
			   "&& (time_end >= %ld || time_end = 0))",
			   job_cond->usage_end, job_cond->usage_start);
	}

	if (debug_flags & DEBUG_FLAG_DB_JOB)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		goto no_hosts;
	}
	xfree(query);

	local_cluster_list = list_create(_destroy_local_cluster);
	while ((row = mysql_fetch_row(result))) {
		char *host = NULL;
		int loc = 0;
		local_cluster_t *local_cluster =
			xmalloc(sizeof(local_cluster_t));
		local_cluster->hl = hostlist_create_dims(row[0], dims);
		local_cluster->start = slurm_atoul(row[1]);
		local_cluster->end   = slurm_atoul(row[2]);
		local_cluster->asked_bitmap =
			bit_alloc(hostlist_count(local_cluster->hl));
		while ((host = hostlist_next_dims(h_itr, dims))) {
			if ((loc = hostlist_find(
				     local_cluster->hl, host)) != -1)
				bit_set(local_cluster->asked_bitmap, loc);
			free(host);
		}
		hostlist_iterator_reset(h_itr);
		if (bit_ffs(local_cluster->asked_bitmap) != -1) {
			list_append(local_cluster_list, local_cluster);
			if (local_cluster->end == 0) {
				local_cluster->end = now;
				(*curr_cluster) = local_cluster;
			} else if (!(*curr_cluster)
				   || (((local_cluster_t *)(*curr_cluster))->end
				       < local_cluster->end)) {
				(*curr_cluster) = local_cluster;
			}
		} else
			_destroy_local_cluster(local_cluster);
	}
	mysql_free_result(result);

	if (!list_count(local_cluster_list)) {
		list_destroy(local_cluster_list);
		local_cluster_list = NULL;
		goto no_hosts;
	}

no_hosts:

	hostlist_iterator_destroy(h_itr);
	hostlist_destroy(temp_hl);

	return local_cluster_list;
}

extern int good_nodes_from_inx(List local_cluster_list,
			       void **object, char *node_inx,
			       int start)
{
	local_cluster_t **curr_cluster = (local_cluster_t **)object;

	/* check the bitmap to see if this is one of the jobs
	   we are looking for */
	if (*curr_cluster) {
		bitstr_t *job_bitmap = NULL;
		if (!node_inx || !node_inx[0])
			return 0;
		if ((start < (*curr_cluster)->start)
		    || (start > (*curr_cluster)->end)) {
			local_cluster_t *local_cluster = NULL;

			ListIterator itr =
				list_iterator_create(local_cluster_list);
			while ((local_cluster = list_next(itr))) {
				if ((start >= local_cluster->start)
				    && (start <= local_cluster->end)) {
					*curr_cluster = local_cluster;
					break;
				}
			}
			list_iterator_destroy(itr);
			if (!local_cluster)
				return 0;
		}
		job_bitmap = bit_alloc(hostlist_count((*curr_cluster)->hl));
		bit_unfmt(job_bitmap, node_inx);
		if (!bit_overlap((*curr_cluster)->asked_bitmap, job_bitmap)) {
			FREE_NULL_BITMAP(job_bitmap);
			return 0;
		}
		FREE_NULL_BITMAP(job_bitmap);
	}
	return 1;
}

extern int setup_job_cluster_cond_limits(mysql_conn_t *mysql_conn,
					 slurmdb_job_cond_t *job_cond,
					 char *cluster_name, char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;

	if (!job_cond)
		return SLURM_SUCCESS;

	/* this must be done before resvid_list since we set
	   resvid_list up here */
	if (job_cond->resv_list && list_count(job_cond->resv_list)) {
		char *query = xstrdup_printf(
			"select distinct job_db_inx from \"%s_%s\" where (",
			cluster_name, job_table);
		int my_set = 0;
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;

		itr = list_iterator_create(job_cond->resv_list);
		while ((object = list_next(itr))) {
			if (my_set)
				xstrcat(query, " || ");
			xstrfmtcat(query, "resv_name='%s'", object);
			my_set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(query, ")");
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			error("couldn't query the database");
			goto no_resv;
		}
		xfree(query);
		if (!job_cond->resvid_list)
			job_cond->resvid_list = list_create(slurm_destroy_char);
		while ((row = mysql_fetch_row(result))) {
			list_append(job_cond->resvid_list, xstrdup(row[0]));
		}
		mysql_free_result(result);
	}
no_resv:

	if (job_cond->resvid_list && list_count(job_cond->resvid_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->resvid_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.id_resv='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->state_list && list_count(job_cond->state_list)) {
		itr = list_iterator_create(job_cond->state_list);
		while ((object = list_next(itr))) {
			uint32_t state = (uint32_t)slurm_atoul(object);
			state &= JOB_STATE_BASE;
			if (state == JOB_SUSPENDED)
				break;
		}
		list_iterator_destroy(itr);

		if (object) {
			MYSQL_RES *result = NULL;
			MYSQL_ROW row;
			char *query = xstrdup_printf(
				"select job_db_inx from \"%s_%s\"",
				cluster_name, suspend_table);
			if (job_cond->usage_start) {
				if (!job_cond->usage_end) {
					xstrfmtcat(query,
						   " where (!time_end "
						   "|| (%d between "
						   "time_start and time_end))",
						   (int)job_cond->usage_start);
				} else {
					xstrfmtcat(query,
						   " where (!time_end "
						   "|| (time_start && "
						   "((%d between time_start "
						   "and time_end) "
						   "|| (time_start between "
						   "%d and %d))))",
						   (int)job_cond->usage_start,
						   (int)job_cond->usage_start,
						   (int)job_cond->usage_end);
				}
			} else if (job_cond->usage_end) {
				xstrfmtcat(query, " where (time_start && "
					   "time_start < %d)",
					   (int)job_cond->usage_end);
			}

			if (debug_flags & DEBUG_FLAG_DB_JOB)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			result = mysql_db_query_ret(mysql_conn, query, 0);
			xfree(query);
			if (!result)
				return SLURM_ERROR;
			set = 0;
			while ((row = mysql_fetch_row(result))) {
				if (set)
					xstrfmtcat(*extra,
						   " || t1.job_db_inx=%s",
						   row[0]);
				else {
					set = 1;
					if (*extra)
						xstrfmtcat(
							*extra,
							" || (t1.job_db_inx=%s",
							row[0]);
					else
						xstrfmtcat(*extra, " where "
							   "(t1.job_db_inx=%s",
							   row[0]);
				}
			}
			mysql_free_result(result);
			if (set)
				xstrcat(*extra, ")");
		}
	}

	return SLURM_SUCCESS;
}

extern int setup_job_cond_limits(mysql_conn_t *mysql_conn,
				 slurmdb_job_cond_t *job_cond,
				 char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	slurmdb_selected_step_t *selected_step = NULL;

	if (!job_cond)
		return 0;

	if (job_cond->acct_list && list_count(job_cond->acct_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->acct_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.account='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->associd_list && list_count(job_cond->associd_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->associd_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.id_assoc='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->userid_list && list_count(job_cond->userid_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		itr = list_iterator_create(job_cond->userid_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.id_user='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->groupid_list && list_count(job_cond->groupid_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->groupid_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.id_group='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->jobname_list && list_count(job_cond->jobname_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		itr = list_iterator_create(job_cond->jobname_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.job_name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->partition_list && list_count(job_cond->partition_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->partition_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.partition='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->qos_list && list_count(job_cond->qos_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->qos_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.id_qos='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->step_list && list_count(job_cond->step_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(job_cond->step_list);
		while ((selected_step = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			if (selected_step->array_task_id == NO_VAL)
				xstrfmtcat(*extra, "(t1.id_job=%u || "
					   "t1.id_array_job=%u)",
					   selected_step->jobid,
					   selected_step->jobid);
			else {
				xstrfmtcat(*extra, "(t1.id_array_job=%u && "
					   "t1.id_array_task=%u)",
					   selected_step->jobid,
					   selected_step->array_task_id);
			}
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (job_cond->cpus_min) {
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		if (job_cond->cpus_max) {
			xstrfmtcat(*extra, "(t1.cpus_alloc between %u and %u))",
				   job_cond->cpus_min, job_cond->cpus_max);

		} else {
			xstrfmtcat(*extra, "(t1.cpus_alloc='%u'))",
				   job_cond->cpus_min);

		}
	}

	if (job_cond->nodes_min) {
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		if (job_cond->nodes_max) {
			xstrfmtcat(*extra,
				   "(t1.nodes_alloc between %u and %u))",
				   job_cond->nodes_min, job_cond->nodes_max);

		} else {
			xstrfmtcat(*extra, "(t1.nodes_alloc='%u'))",
				   job_cond->nodes_min);

		}
	}

	if (job_cond->timelimit_min) {
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		if (job_cond->timelimit_max) {
			xstrfmtcat(*extra, "(t1.timelimit between %u and %u))",
				   job_cond->timelimit_min,
				   job_cond->timelimit_max);

		} else {
			xstrfmtcat(*extra, "(t1.timelimit='%u'))",
				   job_cond->timelimit_min);

		}
	}

	if (job_cond->state_list && list_count(job_cond->state_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		itr = list_iterator_create(job_cond->state_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");

			_state_time_string(extra, (uint32_t)slurm_atoul(object),
					   job_cond->usage_start,
					   job_cond->usage_end);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	} else {
		/* Only do this (default of all eligible jobs) if no
		   state is given */
		if (job_cond->usage_start) {
			if (*extra)
				xstrcat(*extra, " && (");
			else
				xstrcat(*extra, " where (");

			if (!job_cond->usage_end)
				xstrfmtcat(*extra,
					   "(t1.time_end >= %ld "
					   "|| t1.time_end = 0))",
					   job_cond->usage_start);
			else
				xstrfmtcat(*extra,
					   "(t1.time_eligible < %ld "
					   "&& (t1.time_end >= %ld "
					   "|| t1.time_end = 0)))",
					   job_cond->usage_end,
					   job_cond->usage_start);
		} else if (job_cond->usage_end) {
			if (*extra)
				xstrcat(*extra, " && (");
			else
				xstrcat(*extra, " where (");
			xstrfmtcat(*extra,
				   "(t1.time_eligible < %ld))",
				   job_cond->usage_end);
		}
	}

	if (job_cond->wckey_list && list_count(job_cond->wckey_list)) {
		set = 0;
		if (*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");

		itr = list_iterator_create(job_cond->wckey_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "t1.wckey='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}
	return set;
}

extern List as_mysql_jobacct_process_get_jobs(mysql_conn_t *mysql_conn,
					      uid_t uid,
					      slurmdb_job_cond_t *job_cond)
{
	char *extra = NULL;
	char *tmp = NULL, *tmp2 = NULL;
	ListIterator itr = NULL;
	int is_admin=1;
	int i;
	List job_list = NULL;
	uint16_t private_data = 0;
	slurmdb_user_rec_t user;
	int only_pending = 0;
	List use_cluster_list = as_mysql_cluster_list;
	char *cluster_name;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_JOBS) {
		if (!(is_admin = is_user_min_admin_level(
			      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
			/* Only fill in the coordinator accounts here
			   we will check them later when we actually
			   try to get the jobs.
			*/
			is_user_any_coord(mysql_conn, &user);
		}
		if (!is_admin && !user.name) {
			debug("User %u has no assocations, and is not admin, "
			      "so not returning any jobs.", user.uid);
			return NULL;
		}
	}

	if (job_cond
	    && job_cond->state_list && (list_count(job_cond->state_list) == 1)
	    && (slurm_atoul(list_peek(job_cond->state_list)) == JOB_PENDING))
		only_pending = 1;

	setup_job_cond_limits(mysql_conn, job_cond, &extra);

	xfree(tmp);
	xstrfmtcat(tmp, "%s", job_req_inx[0]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", job_req_inx[i]);
	}

	xfree(tmp2);
	xstrfmtcat(tmp2, "%s", step_req_inx[0]);
	for(i=1; i<STEP_REQ_COUNT; i++) {
		xstrfmtcat(tmp2, ", %s", step_req_inx[i]);
	}

	if (job_cond
	    && job_cond->cluster_list && list_count(job_cond->cluster_list))
		use_cluster_list = job_cond->cluster_list;
	else
		slurm_mutex_lock(&as_mysql_cluster_list_lock);

	job_list = list_create(slurmdb_destroy_job_rec);
	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		int rc;
		if ((rc = _cluster_get_jobs(mysql_conn, &user, job_cond,
					    cluster_name, tmp, tmp2, extra,
					    is_admin, only_pending, job_list))
		    != SLURM_SUCCESS)
			error("Problem getting jobs for cluster %s",
			      cluster_name);
	}
	list_iterator_destroy(itr);

	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	xfree(tmp);
	xfree(tmp2);
	xfree(extra);

	return job_list;
}
