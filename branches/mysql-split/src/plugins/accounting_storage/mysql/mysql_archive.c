/*****************************************************************************\
 *  mysql_archive.c - functions dealing with the archiving.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mysql_archive.h"
#include "src/common/env.h"
#include "src/common/jobacct_common.h"

typedef struct {
	uint32_t associd;
	uint32_t id;
	time_t period_end;
	time_t period_start;
} local_suspend_t;

static pthread_mutex_t local_file_lock = PTHREAD_MUTEX_INITIALIZER;
static int high_buffer_size = (1024 * 1024);

static void _pack_local_suspend(local_suspend_t *object,
				uint16_t rpc_version, Buf buffer)
{
	pack32(object->associd, buffer);
	pack32(object->id, buffer);
	pack_time(object->period_end, buffer);
	pack_time(object->period_start, buffer);
}

/* this needs to be allocated before calling */
static int _unpack_local_suspend(local_suspend_t *object,
				  uint16_t rpc_version, Buf buffer)
{
	safe_unpack32(&object->associd, buffer);
	safe_unpack32(&object->id, buffer);
	safe_unpack_time(&object->period_end, buffer);
	safe_unpack_time(&object->period_start, buffer);

	return SLURM_SUCCESS;
unpack_error:
	return SLURM_ERROR;
}

static char *_make_archive_name(time_t period_start, time_t period_end,
			      char *arch_dir, char *arch_type)
{
	struct tm time_tm;
	char start_char[32];
	char end_char[32];

	localtime_r((time_t *)&period_start, &time_tm);
	time_tm.tm_sec = 0;
	time_tm.tm_min = 0;
	time_tm.tm_hour = 0;
	time_tm.tm_mday = 1;
	snprintf(start_char, sizeof(start_char),
		 "%4.4u-%2.2u-%2.2u"
		 "T%2.2u:%2.2u:%2.2u",
		 (time_tm.tm_year + 1900),
		 (time_tm.tm_mon+1),
		 time_tm.tm_mday,
		 time_tm.tm_hour,
		 time_tm.tm_min,
		 time_tm.tm_sec);

	localtime_r((time_t *)&period_end, &time_tm);
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
	return xstrdup_printf("%s/%s_archive_%s_%s",
			      arch_dir, arch_type, start_char, end_char);

}

static int _write_archive_file(Buf buffer,
			       time_t period_start, time_t period_end,
			       char *arch_dir, char *arch_type)
{
	int fd = 0;
	int error_code = SLURM_SUCCESS;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;

	xassert(buffer);

	slurm_mutex_lock(&local_file_lock);

	/* write the buffer to file */
	reg_file = _make_archive_name(
		period_start, period_end, arch_dir, arch_type);

	debug("Storing %s archive at %s", arch_type, reg_file);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	fd = creat(new_file, 0600);
	if (fd < 0) {
		error("Can't save archive, create file %s error %m", new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(fd);
		close(fd);
	}

	if (error_code)
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

	return error_code;
}

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t _archive_cluster_events(mysql_conn_t *mysql_conn,
					time_t period_end, char *arch_dir)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	acct_event_rec_t event;
	Buf buffer;
	int error_code = 0, i = 0;

	/* if this changes you will need to edit the corresponding
	 * enum below */
	char *event_req_inx[] = {
		"node_name",
		"cluster",
		"cpu_count",
		"state",
		"period_start",
		"period_end",
		"reason",
		"reason_uid",
		"cluster_nodes",
	};

	enum {
		EVENT_REQ_NODE,
		EVENT_REQ_CLUSTER,
		EVENT_REQ_CPU,
		EVENT_REQ_STATE,
		EVENT_REQ_START,
		EVENT_REQ_END,
		EVENT_REQ_REASON,
		EVENT_REQ_REASON_UID,
		EVENT_REQ_CNODES,
		EVENT_REQ_COUNT
	};

	xfree(tmp);
	xstrfmtcat(tmp, "%s", event_req_inx[0]);
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", event_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from %s where period_start <= %d "
			       "&& period_end != 0 order by period_start asc",
			       tmp, event_table, period_end);
	xfree(tmp);

//	START_TIMER;
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_EVENTS, buffer);
	pack32(cnt, buffer);

	while((row = mysql_fetch_row(result))) {
		if(!period_start)
			period_start = atoi(row[EVENT_REQ_START]);

		memset(&event, 0, sizeof(acct_event_rec_t));


		event.node_name = row[EVENT_REQ_NODE];
		event.cluster = row[EVENT_REQ_CLUSTER];
		event.reason = row[EVENT_REQ_REASON];
		event.cluster_nodes = row[EVENT_REQ_CNODES];

		event.cpu_count = atoi(row[EVENT_REQ_CPU]);
		event.state = atoi(row[EVENT_REQ_STATE]);
		event.period_start = atoi(row[EVENT_REQ_START]);
		event.period_end = atoi(row[EVENT_REQ_END]);
		event.reason_uid = atoi(row[EVENT_REQ_REASON_UID]);

		pack_acct_event_rec(&event, SLURMDBD_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = _write_archive_file(buffer, period_start, period_end,
					 arch_dir, "event");
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t _archive_suspend(mysql_conn_t *mysql_conn,
				 time_t period_end, char *arch_dir)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_suspend_t suspend;
	Buf buffer;
	int error_code = 0, i = 0;

	/* if this changes you will need to edit the corresponding
	 * enum below */
	char *req_inx[] = {
		"id",
		"associd",
		"start",
		"end",
	};

	enum {
		SUSPEND_REQ_ID,
		SUSPEND_REQ_ASSOCID,
		SUSPEND_REQ_START,
		SUSPEND_REQ_END,
		SUSPEND_REQ_COUNT
	};

	xfree(tmp);
	xstrfmtcat(tmp, "%s", req_inx[0]);
	for(i=1; i<SUSPEND_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from %s where "
			       "start <= %d && end != 0 "
			       "order by start asc",
			       tmp, suspend_table, period_end);
	xfree(tmp);

//	START_TIMER;
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_EVENTS, buffer);
	pack32(cnt, buffer);

	while((row = mysql_fetch_row(result))) {
		if(!period_start)
			period_start = atoi(row[SUSPEND_REQ_START]);

		memset(&suspend, 0, sizeof(local_suspend_t));

		suspend.id = atoi(row[SUSPEND_REQ_ID]);
		suspend.associd = atoi(row[SUSPEND_REQ_ASSOCID]);
		suspend.period_start = atoi(row[SUSPEND_REQ_START]);
		suspend.period_end = atoi(row[SUSPEND_REQ_END]);

		_pack_local_suspend(&suspend, SLURMDBD_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = _write_archive_file(buffer, period_start, period_end,
					 arch_dir, "suspend");
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns count of steps archived or SLURM_ERROR on error */
static uint32_t _archive_steps(mysql_conn_t *mysql_conn,
			       time_t period_end, char *arch_dir)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	jobacct_step_rec_t step;
	Buf buffer;
	int error_code = 0, i = 0;

	/* if this changes you will need to edit the corresponding
	 * enum below */
	char *req_inx[] = {
		"id",
		"stepid",
		"start",
		"end",
		"suspended",
		"name",
		"nodelist",
		"node_inx",
		"state",
		"kill_requid",
		"comp_code",
		"nodes",
		"cpus",
		"tasks",
		"task_dist",
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
		STEP_REQ_ID,
		STEP_REQ_STEPID,
		STEP_REQ_START,
		STEP_REQ_END,
		STEP_REQ_SUSPENDED,
		STEP_REQ_NAME,
		STEP_REQ_NODELIST,
		STEP_REQ_NODE_INX,
		STEP_REQ_STATE,
		STEP_REQ_KILL_REQUID,
		STEP_REQ_COMP_CODE,
		STEP_REQ_NODES,
		STEP_REQ_CPUS,
		STEP_REQ_TASKS,
		STEP_REQ_TASKDIST,
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

	xfree(tmp);
	xstrfmtcat(tmp, "%s", req_inx[0]);
	for(i=1; i<STEP_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from %s where "
			       "start <= %d && end != 0 "
			       "&& !deleted order by start asc",
			       tmp, step_table, period_end);
	xfree(tmp);

//	START_TIMER;
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_EVENTS, buffer);
	pack32(cnt, buffer);

	while((row = mysql_fetch_row(result))) {
		if(!period_start)
			period_start = atoi(row[STEP_REQ_START]);

		memset(&step, 0, sizeof(jobacct_step_rec_t));

		step.stepid = atoi(row[STEP_REQ_STEPID]);
		/* info("got step %u.%u", */
/* 			     job.header.jobnum, step.stepnum); */
		step.state = atoi(row[STEP_REQ_STATE]);
		step.exitcode = atoi(row[STEP_REQ_COMP_CODE]);
		step.ncpus = atoi(row[STEP_REQ_CPUS]);
		step.nnodes = atoi(row[STEP_REQ_NODES]);

		step.ntasks = atoi(row[STEP_REQ_TASKS]);
		step.task_dist = atoi(row[STEP_REQ_TASKDIST]);
		if(!step.ntasks)
			step.ntasks = step.ncpus;

		step.start = atoi(row[STEP_REQ_START]);

		step.end = atoi(row[STEP_REQ_END]);

		step.user_cpu_sec = atoi(row[STEP_REQ_USER_SEC]);
		step.user_cpu_usec =
			atoi(row[STEP_REQ_USER_USEC]);
		step.sys_cpu_sec = atoi(row[STEP_REQ_SYS_SEC]);
		step.sys_cpu_usec = atoi(row[STEP_REQ_SYS_USEC]);
		step.sacct.max_vsize =
			atoi(row[STEP_REQ_MAX_VSIZE]);
		step.sacct.max_vsize_id.taskid =
			atoi(row[STEP_REQ_MAX_VSIZE_TASK]);
		step.sacct.ave_vsize =
			atof(row[STEP_REQ_AVE_VSIZE]);
		step.sacct.max_rss =
			atoi(row[STEP_REQ_MAX_RSS]);
		step.sacct.max_rss_id.taskid =
			atoi(row[STEP_REQ_MAX_RSS_TASK]);
		step.sacct.ave_rss =
			atof(row[STEP_REQ_AVE_RSS]);
		step.sacct.max_pages =
			atoi(row[STEP_REQ_MAX_PAGES]);
		step.sacct.max_pages_id.taskid =
			atoi(row[STEP_REQ_MAX_PAGES_TASK]);
		step.sacct.ave_pages =
			atof(row[STEP_REQ_AVE_PAGES]);
		step.sacct.min_cpu =
			atoi(row[STEP_REQ_MIN_CPU]);
		step.sacct.min_cpu_id.taskid =
			atoi(row[STEP_REQ_MIN_CPU_TASK]);
		step.sacct.ave_cpu = atof(row[STEP_REQ_AVE_CPU]);
		step.stepname = row[STEP_REQ_NAME];
		step.nodes = row[STEP_REQ_NODELIST];
		step.sacct.max_vsize_id.nodeid =
			atoi(row[STEP_REQ_MAX_VSIZE_NODE]);
		step.sacct.max_rss_id.nodeid =
			atoi(row[STEP_REQ_MAX_RSS_NODE]);
		step.sacct.max_pages_id.nodeid =
			atoi(row[STEP_REQ_MAX_PAGES_NODE]);
		step.sacct.min_cpu_id.nodeid =
			atoi(row[STEP_REQ_MIN_CPU_NODE]);

		step.requid = atoi(row[STEP_REQ_KILL_REQUID]);


		pack_jobacct_step_rec(&step, SLURMDBD_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = _write_archive_file(buffer, period_start, period_end,
					 arch_dir, "event");
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns count of jobs archived or SLURM_ERROR on error */
static uint32_t _archive_jobs(mysql_conn_t *mysql_conn,
			      time_t period_end, char *arch_dir)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	jobacct_job_rec_t job;
	Buf buffer;
	int error_code = 0, i = 0;

	/* if this changes you will need to edit the corresponding
	 * enum below */
	char *req_inx[] = {
		"id",
		"jobid",
		"associd",
		"wckey",
		"wckeyid",
		"uid",
		"gid",
		"resvid",
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
		"alloc_nodes",
		"nodelist",
		"node_inx",
		"kill_requid",
		"qos"
	};

	enum {
		JOB_REQ_ID,
		JOB_REQ_JOBID,
		JOB_REQ_ASSOCID,
		JOB_REQ_WCKEY,
		JOB_REQ_WCKEYID,
		JOB_REQ_UID,
		JOB_REQ_GID,
		JOB_REQ_RESVID,
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
		JOB_REQ_ALLOC_NODES,
		JOB_REQ_NODELIST,
		JOB_REQ_NODE_INX,
		JOB_REQ_KILL_REQUID,
		JOB_REQ_QOS,
		JOB_REQ_COUNT
	};


	xfree(tmp);
	xstrfmtcat(tmp, "%s", req_inx[0]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from %s where "
			       "submit < %d && end != 0 && !deleted "
			       "order by submit asc",
			       tmp, job_table, period_end);
	xfree(tmp);

//	START_TIMER;
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_JOBS, buffer);
	pack32(cnt, buffer);

	while((row = mysql_fetch_row(result))) {
		if(!period_start)
			period_start = atoi(row[JOB_REQ_SUBMIT]);

		memset(&job, 0, sizeof(jobacct_job_rec_t));

		job.show_full = atoi(row[JOB_REQ_ID]); /* overloaded
							   with db_inx */
		job.jobid = atoi(row[JOB_REQ_JOBID]);
		job.associd = atoi(row[JOB_REQ_ASSOCID]);
		job.wckey = row[JOB_REQ_WCKEY];
		job.wckeyid = atoi(row[JOB_REQ_WCKEYID]);
		job.uid = atoi(row[JOB_REQ_UID]);
		job.gid = atoi(row[JOB_REQ_GID]);
		job.resvid = atoi(row[JOB_REQ_RESVID]);
		job.partition = row[JOB_REQ_PARTITION];
		job.blockid = row[JOB_REQ_BLOCKID];
		job.cluster = row[JOB_REQ_CLUSTER];
		job.account = row[JOB_REQ_ACCOUNT];
		job.eligible = atoi(row[JOB_REQ_ELIGIBLE]);
		job.submit = atoi(row[JOB_REQ_SUBMIT]);
		job.start = atoi(row[JOB_REQ_START]);
		job.end = atoi(row[JOB_REQ_END]);
		job.suspended = atoi(row[JOB_REQ_SUSPENDED]);
		job.jobname = row[JOB_REQ_NAME];
		job.track_steps = atoi(row[JOB_REQ_TRACKSTEPS]);
		job.state = atoi(row[JOB_REQ_STATE]);
		job.exitcode = atoi(row[JOB_REQ_COMP_CODE]);
		job.priority = atoi(row[JOB_REQ_PRIORITY]);
		job.req_cpus = atoi(row[JOB_REQ_REQ_CPUS]);
		job.alloc_cpus = atoi(row[JOB_REQ_ALLOC_CPUS]);
		job.alloc_nodes = atoi(row[JOB_REQ_ALLOC_NODES]);
		job.nodes = row[JOB_REQ_NODELIST];
		job.user = row[JOB_REQ_NODE_INX]; /* overloaded with
						    * node_inx */
		job.requid = atoi(row[JOB_REQ_KILL_REQUID]);
		job.qos = atoi(row[JOB_REQ_QOS]);

		pack_jobacct_job_rec(&job, SLURMDBD_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = _write_archive_file(buffer, period_start, period_end,
					 arch_dir, "job");
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
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

	if(arch_cond->purge_event) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from "
			      "first event start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->purge_step;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_EVENTS", "%u",
				     arch_cond->archive_events);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_LAST_EVENT", "%d",
				     curr_end);
	}

	if(arch_cond->purge_job) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->purge_job;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);

		env_array_append_fmt(&env, "SLURM_ARCHIVE_JOBS", "%u",
				     arch_cond->archive_jobs);
		env_array_append_fmt (&env, "SLURM_ARCHIVE_LAST_JOB", "%d",
				      curr_end);
	}

	if(arch_cond->purge_step) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first step start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->purge_step;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_STEPS", "%u",
				     arch_cond->archive_steps);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_LAST_STEP", "%d",
				     curr_end);
	}

	if(arch_cond->purge_suspend) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first "
			      "suspend start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->purge_step;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_SUSPEND", "%u",
				     arch_cond->archive_steps);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_LAST_SUSPEND", "%d",
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

extern int mysql_jobacct_process_archive(mysql_conn_t *mysql_conn,
					 acct_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	time_t last_submit = time(NULL);
	time_t curr_end;
	struct tm time_tm;

//	DEF_TIMERS;

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
	debug("archive: adjusted last submit is (%d)", last_submit);

	if(arch_cond->archive_script)
		return _archive_script(arch_cond, last_submit);
	else if(!arch_cond->archive_dir) {
		error("No archive dir given, can't process");
		return SLURM_ERROR;
	}

	if(arch_cond->purge_event) {
		/* remove all data from step table that was older than
		 * period_start * arch_cond->purge_event.
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first submit %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_sec = 0;
		time_tm.tm_min = 0;
		time_tm.tm_hour = 0;
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->purge_event;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		curr_end--;

		debug4("from %d - %d months purging events from before %d",
		       last_submit, arch_cond->purge_event, curr_end);

		if(arch_cond->archive_events) {
			rc = _archive_cluster_events(mysql_conn, curr_end,
						     arch_cond->archive_dir);
			if(!rc)
				goto exit_events;
			else if(rc == SLURM_ERROR)
				return rc;
		}
		query = xstrdup_printf("delete from %s where "
				       "period_start <= %d && period_end != 0",
				       event_table, curr_end);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old event data");
			return SLURM_ERROR;
		}
	}

exit_events:

	if(arch_cond->purge_suspend) {
		/* remove all data from step table that was older than
		 * period_start * arch_cond->purge_suspend.
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first submit %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_sec = 0;
		time_tm.tm_min = 0;
		time_tm.tm_hour = 0;
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->purge_suspend;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		curr_end--;

		debug4("from %d - %d months purging suspend from before %d",
		       last_submit, arch_cond->purge_suspend, curr_end);

		if(arch_cond->archive_suspend) {
			rc = _archive_suspend(mysql_conn, curr_end,
					      arch_cond->archive_dir);
			if(!rc)
				goto exit_suspend;
			else if(rc == SLURM_ERROR)
				return rc;
		}
		query = xstrdup_printf("delete from %s where start <= %d "
				       "&& end != 0",
				       suspend_table, curr_end);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old suspend data");
			return SLURM_ERROR;
		}
	}

exit_suspend:

	if(arch_cond->purge_step) {
		/* remove all data from step table that was older than
		 * start * arch_cond->purge_step.
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_sec = 0;
		time_tm.tm_min = 0;
		time_tm.tm_hour = 0;
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->purge_step;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		curr_end--;

		debug4("from %d - %d months purging steps from before %d",
		       last_submit, arch_cond->purge_step, curr_end);

		if(arch_cond->archive_steps) {
			rc = _archive_steps(mysql_conn, curr_end,
					    arch_cond->archive_dir);
			if(!rc)
				goto exit_steps;
			else if(rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("delete from %s where start <= %d "
				       "&& end != 0",
				       step_table, curr_end);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old step data");
			return SLURM_ERROR;
		}
	}
exit_steps:

	if(arch_cond->purge_job) {
		/* remove all data from step table that was older than
		 * last_submit * arch_cond->purge_job.
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first submit %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_sec = 0;
		time_tm.tm_min = 0;
		time_tm.tm_hour = 0;
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->purge_job;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		curr_end--;

		debug4("from %d - %d months purging jobs from before %d",
		       last_submit, arch_cond->purge_job, curr_end);

		if(arch_cond->archive_jobs) {
			rc = _archive_jobs(mysql_conn, curr_end,
					   arch_cond->archive_dir);
			if(!rc)
				goto exit_jobs;
			else if(rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("delete from %s where submit <= %d "
				       "&& end != 0",
				       job_table, curr_end);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
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

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, data);
	error_code = mysql_db_query_check_after(mysql_conn->db_conn, data);
	xfree(data);
	if(error_code != SLURM_SUCCESS) {
		error("Couldn't load old data");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
