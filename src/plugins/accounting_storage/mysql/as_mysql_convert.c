/*****************************************************************************\
 *  as_mysql_convert.c - functions dealing with converting from tables in
 *                    slurm <= 17.02.
 *****************************************************************************
 *
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "as_mysql_convert.h"
#include "as_mysql_tres.h"
#include "src/common/slurm_jobacct_gather.h"

List bad_tres_list = NULL;

/*
 * Any time you have to add to an existing convert update this number.
 * NOTE: 6 was the first version of 18.08.
 * NOTE: 7 was the first version of 19.05.
 */
#define CONVERT_VERSION 7

typedef struct {
	uint64_t count;
	uint32_t id;
} local_tres_t;

static uint32_t db_curr_ver = NO_VAL;

static void _set_tres_value(char *tres_str, uint64_t *tres_array)
{
	char *tmp_str = tres_str;
	int id;

	xassert(tres_array);

	if (!tres_str || !tres_str[0])
		return;

	while (tmp_str) {
		id = atoi(tmp_str);
		/* 0 isn't a valid tres id */
		if (id <= 0) {
			error("%s: no id found at %s",
			      __func__, tmp_str);
			break;
		}
		if (!(tmp_str = strchr(tmp_str, '='))) {
			error("%s: no value found %s", __func__, tres_str);
			break;
		}

		/*
		 * The id's of static tres will be one more than the array
		 * position.
		 */
		id--;

		if (id >= g_tres_count)
			debug2("%s: Unknown tres location %d", __func__, id);
		else
			tres_array[id] = slurm_atoull(++tmp_str);

		if (!(tmp_str = strchr(tmp_str, ',')))
			break;

		tmp_str++;
	}
}

static int _convert_step_table_pre(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	storage_field_t step_table_fields_17_11[] = {
		{ "job_db_inx", "bigint unsigned not null" },
		{ "deleted", "tinyint default 0 not null" },
		{ "exit_code", "int default 0 not null" },
		{ "id_step", "int not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "nodelist", "text not null" },
		{ "nodes_alloc", "int unsigned not null" },
		{ "node_inx", "text" },
		{ "state", "smallint unsigned not null" },
		{ "step_name", "text not null" },
		{ "task_cnt", "int unsigned not null" },
		{ "task_dist", "smallint default 0 not null" },
		{ "time_start", "bigint unsigned default 0 not null" },
		{ "time_end", "bigint unsigned default 0 not null" },
		{ "time_suspended", "bigint unsigned default 0 not null" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "max_pages", "int unsigned default 0 not null" },
		{ "max_pages_task", "int unsigned default 0 not null" },
		{ "max_pages_node", "int unsigned default 0 not null" },
		{ "ave_pages", "double unsigned default 0.0 not null" },
		{ "max_rss", "bigint unsigned default 0 not null" },
		{ "max_rss_task", "int unsigned default 0 not null" },
		{ "max_rss_node", "int unsigned default 0 not null" },
		{ "ave_rss", "double unsigned default 0.0 not null" },
		{ "max_vsize", "bigint unsigned default 0 not null" },
		{ "max_vsize_task", "int unsigned default 0 not null" },
		{ "max_vsize_node", "int unsigned default 0 not null" },
		{ "ave_vsize", "double unsigned default 0.0 not null" },
		{ "min_cpu", "int unsigned default 0xfffffffe not null" },
		{ "min_cpu_task", "int unsigned default 0 not null" },
		{ "min_cpu_node", "int unsigned default 0 not null" },
		{ "ave_cpu", "double unsigned default 0.0 not null" },
		{ "act_cpufreq", "double unsigned default 0.0 not null" },
		{ "consumed_energy", "bigint unsigned default 0 not null" },
		{ "req_cpufreq_min", "int unsigned default 0 not null" },
		{ "req_cpufreq", "int unsigned default 0 not null" }, /* max */
		{ "req_cpufreq_gov", "int unsigned default 0 not null" },
		{ "max_disk_read", "double unsigned default 0.0 not null" },
		{ "max_disk_read_task", "int unsigned default 0 not null" },
		{ "max_disk_read_node", "int unsigned default 0 not null" },
		{ "ave_disk_read", "double unsigned default 0.0 not null" },
		{ "max_disk_write", "double unsigned default 0.0 not null" },
		{ "max_disk_write_task", "int unsigned default 0 not null" },
		{ "max_disk_write_node", "int unsigned default 0 not null" },
		{ "ave_disk_write", "double unsigned default 0.0 not null" },
		{ "tres_alloc", "text not null default ''" },
		{ "tres_usage_in_ave", "text not null default ''" },
		{ "tres_usage_in_max", "text not null default ''" },
		{ "tres_usage_in_max_taskid", "text not null default ''" },
		{ "tres_usage_in_max_nodeid", "text not null default ''" },
		{ "tres_usage_in_min", "text not null default ''" },
		{ "tres_usage_in_min_taskid", "text not null default ''" },
		{ "tres_usage_in_min_nodeid", "text not null default ''" },
		{ "tres_usage_in_tot", "text not null default ''" },
		{ "tres_usage_out_ave", "text not null default ''" },
		{ "tres_usage_out_max", "text not null default ''" },
		{ "tres_usage_out_max_taskid", "text not null default ''" },
		{ "tres_usage_out_max_nodeid", "text not null default ''" },
		{ "tres_usage_out_min", "text not null default ''" },
		{ "tres_usage_out_min_taskid", "text not null default ''" },
		{ "tres_usage_out_min_nodeid", "text not null default ''" },
		{ "tres_usage_out_tot", "text not null default ''" },
		{ NULL, NULL}
	};

	char *query = NULL, *tmp = NULL;
	char table_name[200];
	int i;

	if (db_curr_ver < 6) {
		char *step_req_inx[] = {
			"job_db_inx",
			"id_step",
			"max_disk_read",
			"max_disk_read_task",
			"max_disk_read_node",
			"ave_disk_read",
			"max_disk_write",
			"max_disk_write_task",
			"max_disk_write_node",
			"ave_disk_write",
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
			"ave_cpu",
			"tres_usage_in_max",
			"tres_usage_in_max_taskid",
			"tres_usage_in_max_nodeid",
			"tres_usage_in_ave",
			"tres_usage_out_max",
			"tres_usage_out_max_taskid",
			"tres_usage_out_max_nodeid",
			"tres_usage_out_ave"
		};

		enum {
			STEP_REQ_INX,
			STEP_REQ_STEPID,
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
			STEP_REQ_TRES_USAGE_IN_MAX,
			STEP_REQ_TRES_USAGE_IN_MAX_TASKID,
			STEP_REQ_TRES_USAGE_IN_MAX_NODEID,
			STEP_REQ_TRES_USAGE_IN_AVE,
			STEP_REQ_TRES_USAGE_OUT_MAX,
			STEP_REQ_TRES_USAGE_OUT_MAX_TASKID,
			STEP_REQ_TRES_USAGE_OUT_MAX_NODEID,
			STEP_REQ_TRES_USAGE_OUT_AVE,
			STEP_REQ_COUNT
		};

		jobacctinfo_t *jobacct = NULL;
		char *tres_usage_in_ave;
		char *tres_usage_in_max;
		char *tres_usage_in_max_nodeid;
		char *tres_usage_in_max_taskid;
		char *tres_usage_out_ave;
		char *tres_usage_out_max;
		char *tres_usage_out_max_nodeid;
		char *tres_usage_out_max_taskid;
		int cnt = 0;
		uint32_t tmp32;
		double tmpd, div = 1024;
		char *extra = NULL;
		uint32_t flags = TRES_STR_FLAG_SIMPLE |
			TRES_STR_FLAG_ALLOW_REAL;

		snprintf(table_name, sizeof(table_name), "\"%s_%s\"",
			 cluster_name, step_table);
		if (mysql_db_create_table(mysql_conn, table_name,
					  step_table_fields_17_11,
					  ", primary key (job_db_inx, id_step))")
		    == SLURM_ERROR)
			return SLURM_ERROR;

		xstrfmtcat(tmp, "%s", step_req_inx[0]);
		for (i = 1; i < STEP_REQ_COUNT; i++) {
			xstrfmtcat(tmp, ", %s", step_req_inx[i]);
		}

		query = xstrdup_printf(
			"select %s from \"%s_%s\"",
			tmp, cluster_name, step_table);
		xfree(tmp);

		if (debug_flags & DEBUG_FLAG_DB_QUERY)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		result = mysql_db_query_ret(mysql_conn, query, 0);
		xfree(query);

		if (!result)
			return SLURM_ERROR;

		while ((row = mysql_fetch_row(result))) {
			jobacct = jobacctinfo_create(NULL);

			/* Just in case something is already there */
			_set_tres_value(row[STEP_REQ_TRES_USAGE_IN_MAX],
					jobacct->tres_usage_in_max);
			_set_tres_value(row[STEP_REQ_TRES_USAGE_IN_MAX_TASKID],
					jobacct->tres_usage_in_max_taskid);
			_set_tres_value(row[STEP_REQ_TRES_USAGE_IN_MAX_NODEID],
					jobacct->tres_usage_in_max_nodeid);
			_set_tres_value(row[STEP_REQ_TRES_USAGE_IN_AVE],
					jobacct->tres_usage_in_tot);
			_set_tres_value(row[STEP_REQ_TRES_USAGE_OUT_MAX],
					jobacct->tres_usage_out_max);
			_set_tres_value(row[STEP_REQ_TRES_USAGE_OUT_MAX_TASKID],
					jobacct->tres_usage_out_max_taskid);
			_set_tres_value(row[STEP_REQ_TRES_USAGE_OUT_MAX_NODEID],
					jobacct->tres_usage_out_max_nodeid);
			_set_tres_value(row[STEP_REQ_TRES_USAGE_OUT_AVE],
					jobacct->tres_usage_out_tot);

			/* TRES_CPU */
			tmp32 = slurm_atoul(row[STEP_REQ_MIN_CPU]);
			if (tmp32 != NO_VAL) {
				jobacct->tres_usage_in_min[TRES_ARRAY_CPU] =
					tmp32;
				jobacct->tres_usage_in_min[TRES_ARRAY_CPU] *=
					CPU_TIME_ADJ;
				jobacct->tres_usage_in_min_nodeid[
					TRES_ARRAY_CPU] =
					slurm_atoull(
						row[STEP_REQ_MIN_CPU_NODE]);
				jobacct->tres_usage_in_min_taskid[
					TRES_ARRAY_CPU] =
					slurm_atoull(
						row[STEP_REQ_MIN_CPU_TASK]);
				tmpd = atof(row[STEP_REQ_AVE_CPU]);
				jobacct->tres_usage_in_tot[TRES_ARRAY_CPU] =
					(uint64_t)(tmpd * CPU_TIME_ADJ);
			}

			/* TRES_MEM */
			tmpd = atof(row[STEP_REQ_MAX_RSS]);
			if (tmpd) {
				jobacct->tres_usage_in_max[TRES_ARRAY_MEM] =
					(uint64_t)(tmpd * div);
				jobacct->tres_usage_in_max_nodeid[
					TRES_ARRAY_MEM] =
					slurm_atoull(
						row[STEP_REQ_MAX_RSS_NODE]);
				jobacct->tres_usage_in_max_taskid[
					TRES_ARRAY_MEM] =
					slurm_atoull(
						row[STEP_REQ_MAX_RSS_TASK]);
				tmpd = atof(row[STEP_REQ_AVE_RSS]);
				jobacct->tres_usage_in_tot[TRES_ARRAY_MEM] =
					(uint64_t)(tmpd * div);
			}

			/* TRES_VMEM */
			tmpd = atof(row[STEP_REQ_MAX_VSIZE]);
			if (tmpd) {
				jobacct->tres_usage_in_max[TRES_ARRAY_VMEM] =
					(uint64_t)(tmpd * div);
				jobacct->tres_usage_in_max_nodeid[
					TRES_ARRAY_VMEM] =
					slurm_atoull(
						row[STEP_REQ_MAX_VSIZE_NODE]);
				jobacct->tres_usage_in_max_taskid[
					TRES_ARRAY_VMEM] =
					slurm_atoull(
						row[STEP_REQ_MAX_VSIZE_TASK]);
				tmpd = atof(row[STEP_REQ_AVE_VSIZE]);
				jobacct->tres_usage_in_tot[TRES_ARRAY_VMEM] =
					(uint64_t)(tmpd * div);
			}

			/* TRES_PAGES */
			tmp32 = slurm_atoul(row[STEP_REQ_MAX_PAGES]);
			if (tmp32) {
				jobacct->tres_usage_in_max[TRES_ARRAY_PAGES] =
					tmp32;
				jobacct->tres_usage_in_max_nodeid[
					TRES_ARRAY_PAGES] =
					slurm_atoull(
						row[STEP_REQ_MAX_PAGES_NODE]);
				jobacct->tres_usage_in_max_taskid[
					TRES_ARRAY_PAGES] =
					slurm_atoull(
						row[STEP_REQ_MAX_PAGES_TASK]);
				jobacct->tres_usage_in_tot[TRES_ARRAY_PAGES] =
					(uint64_t)atof(row[STEP_REQ_AVE_PAGES]);
			}

			/* TRES_FS_DISK (READ/IN) */
			tmpd = atof(row[STEP_REQ_MAX_DISK_READ]);
			if (tmpd) {
				jobacct->tres_usage_in_max[TRES_ARRAY_FS_DISK] =
					(uint64_t)(tmpd * div);
				jobacct->tres_usage_in_max_nodeid[
					TRES_ARRAY_FS_DISK] =
					slurm_atoull(
						row[STEP_REQ_MAX_DISK_READ_NODE]);
				jobacct->tres_usage_in_max_taskid[
					TRES_ARRAY_FS_DISK] =
					slurm_atoull(
						row[STEP_REQ_MAX_DISK_READ_TASK]);
				tmpd = atof(row[STEP_REQ_AVE_DISK_READ]);
				jobacct->tres_usage_in_tot[TRES_ARRAY_FS_DISK] =
					(uint64_t)(tmpd * div);
			}
			/* TRES_FS_DISK (WRITE/OUT) */
			tmpd = atof(row[STEP_REQ_MAX_DISK_WRITE]);
			if (tmpd) {
				jobacct->tres_usage_out_max[
					TRES_ARRAY_FS_DISK] =
					(uint64_t)(tmpd * div);
				jobacct->tres_usage_out_max_nodeid[
					TRES_ARRAY_FS_DISK] =
					slurm_atoull(
						row[STEP_REQ_MAX_DISK_READ_NODE]);
				jobacct->tres_usage_out_max_taskid[
					TRES_ARRAY_FS_DISK] =
					slurm_atoull(
						row[STEP_REQ_MAX_DISK_READ_TASK]);
				tmpd = atof(row[STEP_REQ_AVE_DISK_WRITE]);
				jobacct->tres_usage_out_tot[
					TRES_ARRAY_FS_DISK] =
					(uint64_t)(tmpd * div);
			}

			tres_usage_in_max = assoc_mgr_make_tres_str_from_array(
				jobacct->tres_usage_in_max, flags, true);
			tres_usage_in_max_nodeid =
				assoc_mgr_make_tres_str_from_array(
				jobacct->tres_usage_in_max_nodeid, flags, true);
			tres_usage_in_max_taskid =
				assoc_mgr_make_tres_str_from_array(
				jobacct->tres_usage_in_max_taskid, flags, true);
			tres_usage_in_ave = assoc_mgr_make_tres_str_from_array(
				jobacct->tres_usage_in_tot, flags, true);
			tres_usage_out_max = assoc_mgr_make_tres_str_from_array(
				jobacct->tres_usage_out_max, flags, true);
			tres_usage_out_max_nodeid =
				assoc_mgr_make_tres_str_from_array(
				jobacct->tres_usage_out_max_nodeid,
				flags, true);
			tres_usage_out_max_taskid =
				assoc_mgr_make_tres_str_from_array(
				jobacct->tres_usage_out_max_taskid,
				flags, true);
			tres_usage_out_ave = assoc_mgr_make_tres_str_from_array(
				jobacct->tres_usage_out_tot, flags, true);

			jobacctinfo_destroy(jobacct);

			if (tres_usage_in_max) {
				xstrfmtcat(extra, "%stres_usage_in_max='%s'",
					   extra ? ", " : "",
					   tres_usage_in_max);
				xfree(tres_usage_in_max);
			}

			if (tres_usage_in_max_nodeid) {
				xstrfmtcat(extra,
					   "%stres_usage_in_max_nodeid='%s'",
					   extra ? ", " : "",
					   tres_usage_in_max_nodeid);
				xfree(tres_usage_in_max_nodeid);
			}
			if (tres_usage_in_max_taskid) {
				xstrfmtcat(extra,
					   "%stres_usage_in_max_taskid='%s'",
					   extra ? ", " : "",
					   tres_usage_in_max_taskid);
				xfree(tres_usage_in_max_taskid);
			}
			if (tres_usage_in_ave) {
				xstrfmtcat(extra, "%stres_usage_in_ave='%s'",
					   extra ? ", " : "",
					   tres_usage_in_ave);
				xfree(tres_usage_in_ave);
			}

			if (tres_usage_out_max) {
				xstrfmtcat(extra, "%stres_usage_out_max='%s'",
					   extra ? ", " : "",
					   tres_usage_out_max);
				xfree(tres_usage_out_max);
			}

			if (tres_usage_out_max_nodeid) {
				xstrfmtcat(extra,
					   "%stres_usage_out_max_nodeid='%s'",
					   extra ? ", " : "",
					   tres_usage_out_max_nodeid);
				xfree(tres_usage_out_max_nodeid);
			}
			if (tres_usage_out_max_taskid) {
				xstrfmtcat(extra,
					   "%stres_usage_out_max_taskid='%s'",
					   extra ? ", " : "",
					   tres_usage_out_max_taskid);
				xfree(tres_usage_out_max_taskid);
			}
			if (tres_usage_out_ave) {
				xstrfmtcat(extra, "%stres_usage_out_ave='%s'",
					   extra ? ", " : "",
					   tres_usage_out_ave);
				xfree(tres_usage_out_ave);
			}

			if (!extra)
				continue;

			xstrfmtcat(query, "update \"%s_%s\" set %s where job_db_inx=%s and id_step=%s;",
				   cluster_name, step_table, extra,
				   row[STEP_REQ_INX],
				   row[STEP_REQ_STEPID]);
			xfree(extra);

			if (cnt > 1000) {
				cnt = 0;
				if (debug_flags & DEBUG_FLAG_DB_QUERY)
					DB_DEBUG(mysql_conn->conn, "query\n%s",
						 query);
				rc = mysql_db_query(mysql_conn, query);
				xfree(query);
				if (rc != SLURM_SUCCESS) {
					error("%s: Can't convert %s_%s info: %m",
					      __func__,
					      cluster_name, step_table);
					break;
				}
			} else
				cnt++;
		}
		mysql_free_result(result);
	}

	if (query) {
		if (debug_flags & DEBUG_FLAG_DB_QUERY)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			error("%s: Can't convert %s_%s info: %m",
			      __func__, cluster_name, step_table);
	}

	return rc;
}

static int _set_db_curr_ver(mysql_conn_t *mysql_conn)
{
	char *query;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;

	if (db_curr_ver != NO_VAL)
		return SLURM_SUCCESS;

	query = xstrdup_printf("select version from %s", convert_version_table);
	debug4("%d(%s:%d) query\n%s", mysql_conn->conn,
	       THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	row = mysql_fetch_row(result);

	if (row) {
		db_curr_ver = slurm_atoul(row[0]);
		mysql_free_result(result);
	} else {
		int tmp_ver = 0;
		mysql_free_result(result);

		/* no valid clusters, just return */
		if (as_mysql_total_cluster_list &&
		    !list_count(as_mysql_total_cluster_list))
			tmp_ver = CONVERT_VERSION;

		query = xstrdup_printf("insert into %s (version) values (%d);",
				       convert_version_table, tmp_ver);
		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			return SLURM_ERROR;
		db_curr_ver = tmp_ver;
	}

	return rc;
}

extern int as_mysql_convert_get_bad_tres(mysql_conn_t *mysql_conn)
{
	char *query = NULL;
	char *tmp = NULL;
	int rc = SLURM_SUCCESS;
	int i=0, auto_inc = TRES_OFFSET;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *tres_req_inx[] = {
		"id",
		"type",
		"name"
	};
	enum {
		SLURMDB_REQ_ID,
		SLURMDB_REQ_TYPE,
		SLURMDB_REQ_NAME,
		SLURMDB_REQ_COUNT
	};

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	/* This is only for db's not having converted to 5 yet */
	if (db_curr_ver >= 5) {
		debug4("%s: No conversion needed, Horray!", __func__);
		return SLURM_SUCCESS;
	} else if (backup_dbd) {
		/*
		 * We do not want to create/check the database if we are the
		 * backup (see Bug 3827). This is only handled on the primary.
		 *
		 * To avoid situations where someone might upgrade the database
		 * through the backup we want to fatal so they know what
		 * happened instead of potentially starting with the older
		 * database.
		 */
		fatal("Backup DBD can not convert database, please start the primary DBD before starting the backup.");
		return SLURM_ERROR;
	}

	/*
	 * Check to see if we have a bad one to start with.
	 * Any bad one will be in id=5 and will also have a name.  If we
	 * don't have this then we are ok.  Otherwise fatal since it may
	 * involve manually altering the database.
	 */
	query = xstrdup_printf(
		"select id from %s where id=%d && type='billing' && name!=''",
		tres_table, TRES_BILLING);

	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if ((row = mysql_fetch_row(result))) {
		fatal("%s: There is a known bug dealing with MySQL and auto_increment numbers, unfortunately your system has hit this bug.  To temporarily resolve the issue please revert back to your last version of SlurmDBD.  Fixing this issue correctly will require manual intervention with the database.  SchedMD can assist with this.  Supported sites please open a ticket at https://bugs.schedmd.com/.  Non-supported sites please contact SchedMD at sales@schedmd.com if you would like to discuss commercial support options.",
		      __func__);
		return SLURM_ERROR;
	}
	mysql_free_result(result);

	/*
	 * Get the largest id in the tres table.
	 */
	query = xstrdup_printf("select MAX(id) from %s;", tres_table);
	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(row = mysql_fetch_row(result))) {
		fatal("%s: Couldn't get auto_increment for some reason",
		      __func__);
		return SLURM_ERROR;
	}

	/*
	 * Make sure it is at least TRES_OFFSET (blank/new databases will return
	 * NULL.
	 */
	if (row[0] && row[0][0]) {
		uint32_t max_id = slurm_atoul(row[0]);
		auto_inc = MAX(auto_inc, max_id);
	}

	/*
	 * Now get all the ones that need to me moved.
	 */
	xfree(tmp);
	xstrfmtcat(tmp, "%s", tres_req_inx[i]);
	for (i = 1; i < SLURMDB_REQ_COUNT; i++)
		xstrfmtcat(tmp, ", %s", tres_req_inx[i]);

	query = xstrdup_printf("select %s from %s where (id between 5 and 999) && type!='billing'",
			       tmp, tres_table);
	xfree(tmp);

	if (debug_flags & DEBUG_FLAG_DB_QUERY)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_tres_rec_t *tres;

		if (!bad_tres_list)
			bad_tres_list = list_create(slurmdb_destroy_tres_rec);

		tres = xmalloc(sizeof(slurmdb_tres_rec_t));
		list_append(bad_tres_list, tres);

		tres->id = slurm_atoul(row[SLURMDB_REQ_ID]);
		/* use this to say where we are moving it to */
		tres->rec_count = ++auto_inc;
		if (row[SLURMDB_REQ_TYPE] && row[SLURMDB_REQ_TYPE][0])
			tres->type = xstrdup(row[SLURMDB_REQ_TYPE]);
		if (row[SLURMDB_REQ_NAME] && row[SLURMDB_REQ_NAME][0])
			tres->name = xstrdup(row[SLURMDB_REQ_NAME]);
		xstrfmtcat(query,
			   "update %s set id=%u where id=%u;",
			   tres_table, tres->rec_count, tres->id);
	}
	mysql_free_result(result);

	if (query) {
		if (debug_flags & DEBUG_FLAG_DB_QUERY)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}

extern int as_mysql_convert_tables_pre_create(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster_name;

	xassert(as_mysql_total_cluster_list);

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if (db_curr_ver == CONVERT_VERSION) {
		debug4("%s: No conversion needed, Horray!", __func__);
		return SLURM_SUCCESS;
	} else if (backup_dbd) {
		/*
		 * We do not want to create/check the database if we are the
		 * backup (see Bug 3827). This is only handled on the primary.
		 *
		 * To avoid situations where someone might upgrade the database
		 * through the backup we want to fatal so they know what
		 * happened instead of potentially starting with the older
		 * database.
		 */
		fatal("Backup DBD can not convert database, please start the primary DBD before starting the backup.");
		return SLURM_ERROR;
	}

	if (db_curr_ver < 6) {
		/*
		 * We have to fake it here to make things work correctly since
		 * the assoc_mgr isn't set up yet.
		 */
		List tres_list = as_mysql_get_tres(mysql_conn, getuid(), NULL);
		assoc_mgr_post_tres_list(tres_list);
	}

	/* make it up to date */
	itr = list_iterator_create(as_mysql_total_cluster_list);
	while ((cluster_name = list_next(itr))) {
		info("pre-converting step table for %s", cluster_name);
		if ((rc = _convert_step_table_pre(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;
	}
	list_iterator_destroy(itr);

	if (db_curr_ver < 6)
		assoc_mgr_fini(false);

	return rc;
}

extern int as_mysql_convert_tables_post_create(mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int as_mysql_convert_non_cluster_tables_post_create(
	mysql_conn_t *mysql_conn)
{
	int rc = SLURM_SUCCESS;

	if ((rc = _set_db_curr_ver(mysql_conn)) != SLURM_SUCCESS)
		return rc;

	if (db_curr_ver == CONVERT_VERSION) {
		debug4("%s: No conversion needed, Horray!", __func__);
		return SLURM_SUCCESS;
	}

	if (db_curr_ver < 7) {
		/*
		 * In 19.05 we changed the name of the TRES bb/cray to be
		 * bb/datawarp.
		 */
		char *query = xstrdup_printf(
			"update %s set name='datawarp' where type='bb' and name='cray'",
			tres_table);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}


	if (rc == SLURM_SUCCESS) {
		char *query = xstrdup_printf(
			"update %s set version=%d, mod_time=UNIX_TIMESTAMP()",
			convert_version_table, CONVERT_VERSION);

		info("Conversion done: success!");

		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}
