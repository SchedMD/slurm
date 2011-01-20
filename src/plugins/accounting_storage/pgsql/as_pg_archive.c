/*****************************************************************************\
 *  as_pg_archive.c - accounting interface to pgsql - data archiving.
 *
 *  $Id: as_pg_archive.c 13061 2008-01-22 21:23:56Z da $
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "as_pg_common.h"
#include "src/common/pack.h"

static int high_buffer_size = (1024 * 1024);


/* generic record archiving */
static uint32_t
_archive_record(PGresult *result, char *cluster_name, time_t period_end,
		char *arch_dir, uint32_t archive_period,
		slurmdbd_msg_type_t type, char *desc)
{
	time_t period_start = 0;
	Buf buffer;
	int error_code = 0, i = 0, cnt = 0, field_cnt = PQnfields(result);

	cnt = PQntuples(result);
	if (cnt == 0)
		return 0;

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(type, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	FOR_EACH_ROW {
		if(!period_start)
			period_start = atoi(ROW(0)); /* XXX: ROW(0) must be time_start */
		for (i = 0; i < field_cnt; i ++) {
			packstr(ROW(i), buffer);
		}
	} END_EACH_ROW;

	error_code = archive_write_file(buffer, cluster_name,
					period_start, period_end,
					arch_dir, desc, archive_period);
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* generic archive record loading */
static char *
_load_record(uint16_t rpc_version, Buf buffer, char *cluster_name,
	     uint32_t rec_cnt, char *table, char *fields, int field_cnt)
{
	char *insert = NULL, *val = NULL;
	int i = 0, j = 0;
	uint32_t tmp32;

	xstrfmtcat(insert, "INSERT INTO %s.%s (%s) VALUES ",
		   cluster_name, table, fields);

	for(i = 0; i < rec_cnt; i ++) {
		if(i)
			xstrcat(insert, ", ");

		for (j = 0; j < field_cnt; j ++) {
			/* TODO: check return code of unpack */
			unpackstr_ptr(&val, &tmp32, buffer);
			if (j)
				xstrfmtcat(insert, ",'%s'", val);
			else
				xstrfmtcat(insert, "('%s'", val);
		}
		xstrcat(insert, ")");
	}
	return insert;
}


static char *event_archive_fields =
	"time_start, time_end, node_name, cluster_nodes, "
	"cpu_count, reason, reason_uid, state";
static int event_archive_field_cnt = 8;

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t
_archive_events(pgsql_conn_t *pg_conn, char *cluster_name,
		time_t period_end, char *arch_dir,
		uint32_t archive_period)
{
	DEF_VARS;
	int rc;

	/* get all the events started before this time listed */
	query = xstrdup_printf("SELECT %s FROM %s.%s WHERE time_start<=%ld "
			       "  AND time_end!=0 ORDER BY time_start ASC",
			       event_archive_fields, cluster_name,
			       event_table, (long)period_end);

	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	rc = _archive_record(result, cluster_name, period_end, arch_dir,
			     archive_period, DBD_GOT_EVENTS, "event");
	PQclear(result);
	return rc;
}

/* returns sql statement from archived data or NULL on error */
static char *
_load_events(uint16_t rpc_version, Buf buffer, char *cluster_name,
	    uint32_t rec_cnt)
{
	return _load_record(rpc_version, buffer, cluster_name, rec_cnt,
			    event_table, event_archive_fields,
			    event_archive_field_cnt);
}


static char * job_archive_fields = "time_submit,account,cpus_alloc,nodes_alloc,"
	"id_assoc,id_block,exit_code,timelimit,time_eligible,time_end,gid,"
	"job_db_inx,id_job,kill_requid,job_name,nodelist,node_inx,partition,"
	"priority,id_qos,cpus_req,id_resv,state,time_start,"
	"time_suspended,track_steps,uid,wckey,id_wckey";
static int job_archive_field_cnt = 29;

/* returns count of jobs archived or SLURM_ERROR on error */
static uint32_t
_archive_jobs(pgsql_conn_t *pg_conn, char *cluster_name,
	      time_t period_end, char *arch_dir,
	      uint32_t archive_period)
{
	DEF_VARS;
	int rc;

	/* get all the jobs submitted before this time listed */
	query = xstrdup_printf(
		"SELECT %s FROM %s.%s WHERE time_submit<%ld AND time_end!=0 "
		"AND deleted=0 ORDER BY time_submit ASC",
		job_archive_fields, cluster_name, job_table, (long)period_end);

	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	rc = _archive_record(result, cluster_name, period_end, arch_dir,
			     archive_period, DBD_GOT_JOBS, "job");
	return rc;
}

/* returns sql statement from archived data or NULL on error */
static char *
_load_jobs(uint16_t rpc_version, Buf buffer,
	   char *cluster_name, uint32_t rec_cnt)
{
	return _load_record(rpc_version, buffer, cluster_name, rec_cnt,
			    job_table, job_archive_fields,
			    job_archive_field_cnt);
}

static char *step_archive_fields = "time_start,job_db_inx,id_step,time_end,"
	"time_suspended,step_name,nodelist,node_inx,state,kill_requid,"
	"exit_code,nodes_alloc,cpus_alloc,task_cnt,task_dist,user_sec,"
	"user_usec,sys_sec,sys_usec,max_vsize,max_vsize_task,max_vsize_node,"
	"ave_vsize,max_rss,max_rss_task,max_rss_node,ave_rss,max_pages,"
	"max_pages_task,max_pages_node,ave_pages,min_cpu,min_cpu_task,"
	"min_cpu_node,ave_cpu";
static int step_archive_field_cnt = 35;

/* returns count of steps archived or SLURM_ERROR on error */
static uint32_t
_archive_steps(pgsql_conn_t *pg_conn, char *cluster_name,
	       time_t period_end, char *arch_dir,
	       uint32_t archive_period)
{
	DEF_VARS;
	int rc;

	/* get all the steps started before this time listed */
	query = xstrdup_printf(
		"SELECT %s FROM %s.%s WHERE time_start<%ld AND time_end!=0 "
		"AND deleted=0 ORDER BY time_start ASC",
		step_archive_fields, cluster_name,
		step_table, (long)period_end);

	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	rc = _archive_record(result, cluster_name, period_end, arch_dir,
			     archive_period, DBD_STEP_START, "step");
	return rc;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_steps(uint16_t rpc_version, Buf buffer,
			 char *cluster_name, uint32_t rec_cnt)
{
	return _load_record(rpc_version, buffer, cluster_name, rec_cnt,
			    step_table, step_archive_fields,
			    step_archive_field_cnt);
}

static char *suspend_archive_fields = "time_start,job_db_inx,id_assoc,"
	"time_end";
static int suspend_archive_field_cnt = 4;

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t
_archive_suspend(pgsql_conn_t *pg_conn, char *cluster_name,
		 time_t period_end, char *arch_dir,
		 uint32_t archive_period)
{
	DEF_VARS;
	int rc;

	/* get all the suspend records started before this time listed */
	query = xstrdup_printf(
		"SELECT %s FROM %s.%s WHERE time_start<=%ld AND time_end!=0 "
		"ORDER BY time_start ASC",
		suspend_archive_fields, cluster_name,
		suspend_table, (long)period_end);

	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	rc = _archive_record(result, cluster_name, period_end, arch_dir,
			     archive_period, DBD_JOB_SUSPEND, "suspend");
	return rc;
}

/* returns sql statement from archived data or NULL on error */
static char *
_load_suspend(uint16_t rpc_version, Buf buffer,
	      char *cluster_name, uint32_t rec_cnt)
{
	return _load_record(rpc_version, buffer, cluster_name, rec_cnt,
			    suspend_table, suspend_archive_fields,
			    suspend_archive_field_cnt);
}



static int
_execute_archive(pgsql_conn_t *pg_conn, char *cluster_name,
		 slurmdb_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	time_t curr_end;
	time_t last_submit = time(NULL);

	if(arch_cond->archive_script)
		return archive_run_script(arch_cond, cluster_name, last_submit);
	else if(!arch_cond->archive_dir) {
		error("No archive dir given, can't process");
		return SLURM_ERROR;
	}

	if(arch_cond->purge_event != NO_VAL) {
		/* remove all data from event table that was older than
		 * period_start * arch_cond->purge_event.
		 */
		if(!(curr_end = archive_setup_end_time(
			     last_submit, arch_cond->purge_event))) {
			error("Parsing purge event");
			return SLURM_ERROR;
		}

		debug4("Purging event entries before %ld for %s",
		       (long)curr_end, cluster_name);

		if(SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_event)) {
			rc = _archive_events(pg_conn, cluster_name,
					     curr_end, arch_cond->archive_dir,
					     arch_cond->purge_event);
			if(!rc)
				goto exit_events;
			else if(rc == SLURM_ERROR)
				return rc;
		}
		query = xstrdup_printf("DELETE FROM %s.%s WHERE "
				       "time_start<=%ld AND time_end!=0",
				       cluster_name, event_table,
				       (long)curr_end);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old event data");
			return SLURM_ERROR;
		}
	}

exit_events:

	if(arch_cond->purge_suspend != NO_VAL) {
		/* remove all data from suspend table that was older than
		 * period_start * arch_cond->purge_suspend.
		 */
		if(!(curr_end = archive_setup_end_time(
			     last_submit, arch_cond->purge_suspend))) {
			error("Parsing purge suspend");
			return SLURM_ERROR;
		}

		debug4("Purging suspend entries before %ld for %s",
		       (long)curr_end, cluster_name);

		if(SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_suspend)) {
			rc = _archive_suspend(pg_conn, cluster_name,
					      curr_end, arch_cond->archive_dir,
					      arch_cond->purge_suspend);
			if(!rc)
				goto exit_suspend;
			else if(rc == SLURM_ERROR)
				return rc;
		}
		query = xstrdup_printf("DELETE FROM %s.%s WHERE "
				       "time_start<=%ld AND time_end!=0",
				       cluster_name, suspend_table,
				       (long)curr_end);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old suspend data");
			return SLURM_ERROR;
		}
	}

exit_suspend:

	if(arch_cond->purge_step != NO_VAL) {
		/* remove all data from step table that was older than
		 * start * arch_cond->purge_step.
		 */
		if(!(curr_end = archive_setup_end_time(
			     last_submit, arch_cond->purge_step))) {
			error("Parsing purge step");
			return SLURM_ERROR;
		}

		debug4("Purging step entries before %ld for %s",
		       (long)curr_end, cluster_name);

		if(SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_step)) {
			rc = _archive_steps(pg_conn, cluster_name,
					    curr_end, arch_cond->archive_dir,
					    arch_cond->purge_step);
			if(!rc)
				goto exit_steps;
			else if(rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("DELETE FROM %s.%s WHERE "
				       "time_start<=%ld AND time_end!=0",
				       cluster_name, step_table,
				       (long)curr_end);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old step data");
			return SLURM_ERROR;
		}
	}
exit_steps:

	if(arch_cond->purge_job != NO_VAL) {
		/* remove all data from job table that was older than
		 * last_submit * arch_cond->purge_job.
		 */
		if(!(curr_end = archive_setup_end_time(
			     last_submit, arch_cond->purge_job))) {
			error("Parsing purge job");
			return SLURM_ERROR;
		}

		debug4("Purging job entires before %ld for %s",
		       (long)curr_end, cluster_name);

		if(SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_job)) {
			rc = _archive_jobs(pg_conn, cluster_name,
					   curr_end, arch_cond->archive_dir,
					   arch_cond->purge_job);
			if(!rc)
				goto exit_jobs;
			else if(rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("DELETE FROM %s.%s WHERE "
				       "time_submit<=%ld AND time_end!=0",
				       cluster_name, job_table, (long)curr_end);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old job data");
			return SLURM_ERROR;
		}
	}
exit_jobs:
	return SLURM_SUCCESS;
}

/*
 * js_pg_archive - expire old job info from the storage
 *
 * IN pg_conn: database connection
 * IN arch_cond: which jobs to expire
 * RET: error code
 */
extern int
js_pg_archive(pgsql_conn_t *pg_conn, slurmdb_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	List cluster_list = NULL;

	if(!arch_cond) {
		error("No arch_cond was given to archive from. returning");
		return SLURM_ERROR;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (arch_cond->job_cond)
		cluster_list = arch_cond->job_cond->cluster_list;

	FOR_EACH_CLUSTER(cluster_list) {
		rc = _execute_archive(pg_conn, cluster_name, arch_cond);
		if (rc != SLURM_SUCCESS)
			break;
	} END_EACH_CLUSTER;

	return rc;
}


/*
 * js_pg_archive_load  - load old job info into the storage
 *
 * IN pg_conn: database connection
 * IN arch_rec: old job info
 * RET: error code
 */
extern int
js_pg_archive_load(pgsql_conn_t *pg_conn,
		   slurmdb_archive_rec_t *arch_rec)
{
	char *data = NULL, *cluster_name = NULL, *query = NULL;
	int error_code = SLURM_SUCCESS;
	Buf buffer;
	time_t buf_time;
	uint16_t type = 0, ver = 0;
	uint32_t data_size = 0, rec_cnt = 0, tmp32 = 0;

	if(!arch_rec) {
		error("We need a slurmdb_archive_rec to load anything.");
		return SLURM_ERROR;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if(arch_rec->insert) {
		data = xstrdup(arch_rec->insert);
	} else if(arch_rec->archive_file) {
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
		      "slurmdb_archive_rec so I am unable to process.");
		return SLURM_ERROR;
	}

	if(!data) {
		error("It doesn't appear we have anything to load.");
		return SLURM_ERROR;
	}

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in archive header is %u", ver);
	if (ver > SLURMDBD_VERSION || ver < SLURMDBD_VERSION_MIN) {
		error("***********************************************");
		error("Can not recover archive file, incompatible version, "
		      "got %u need >= %u <= %u", ver,
		      SLURMDBD_VERSION_MIN, SLURMDBD_VERSION);
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	}
	safe_unpack_time(&buf_time, buffer);
	safe_unpack16(&type, buffer);
	unpackstr_ptr(&cluster_name, &tmp32, buffer);
	safe_unpack32(&rec_cnt, buffer);

	if(!rec_cnt) {
		error("we didn't get any records from this file of type '%s'",
		      slurmdbd_msg_type_2_str(type, 0));
		free_buf(buffer);
		goto got_sql;
	}

	switch(type) {
	case DBD_GOT_EVENTS:
		data = _load_events(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_GOT_JOBS:
		data = _load_jobs(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_STEP_START:
		data = _load_steps(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_JOB_SUSPEND:
		data = _load_suspend(ver, buffer, cluster_name, rec_cnt);
		break;
	default:
		error("Unknown type '%u' to load from archive", type);
		break;
	}
	free_buf(buffer);

got_sql:
	if(!data) {
		error("No data to load");
		return SLURM_ERROR;
	}

	query = data;
	error_code = DEF_QUERY_RET_RC;
	return error_code;

unpack_error:
	error("Couldn't load archive data");
	return SLURM_ERROR;
}
