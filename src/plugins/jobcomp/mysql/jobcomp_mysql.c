/*****************************************************************************\
 *  jobcomp_mysql.c - Store/Get all information in a mysql storage.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "mysql_jobcomp_process.h"
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include "src/common/parse_time.h"
#include "src/common/node_select.h"
#include "src/common/uid.h"

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
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Job completion MYSQL plugin";
const char plugin_type[] = "jobcomp/mysql";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

mysql_conn_t *jobcomp_mysql_conn = NULL;

char *jobcomp_table = "jobcomp_table";
storage_field_t jobcomp_table_fields[] = {
	{ "jobid", "int not null" },
	{ "uid", "int unsigned not null" },
	{ "user_name", "tinytext not null" },
	{ "gid", "int unsigned not null" },
	{ "group_name", "tinytext not null" },
	{ "name", "tinytext not null" },
	{ "state", "int unsigned not null" },
	{ "partition", "tinytext not null" },
	{ "timelimit", "tinytext not null" },
	{ "starttime", "int unsigned default 0 not null" },
	{ "endtime", "int unsigned default 0 not null" },
	{ "nodelist", "text" },
	{ "nodecnt", "int unsigned not null" },
	{ "proc_cnt", "int unsigned not null" },
	{ "connect_type", "tinytext" },
	{ "reboot", "tinytext" },
	{ "rotate", "tinytext" },
	{ "maxprocs", "int unsigned default 0 not null" },
	{ "geometry", "tinytext" },
	{ "start", "tinytext" },
	{ "blockid", "tinytext" },
	{ NULL, NULL}
};


/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

static slurm_errtab_t slurm_errtab[] = {
	{0, "No error"},
	{-1, "Unspecified error"}
};

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/* File descriptor used for logging */
static pthread_mutex_t  jobcomp_lock = PTHREAD_MUTEX_INITIALIZER;


static int _mysql_jobcomp_check_tables()
{
	if (mysql_db_create_table(jobcomp_mysql_conn, jobcomp_table,
				  jobcomp_table_fields,
				  ", primary key (jobid, starttime, endtime))")
	    == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}


/* get the user name for the give user_id */
static char *_get_user_name(uint32_t user_id)
{
	static uint32_t cache_uid      = 0;
	static char     cache_name[32] = "root", *uname;
	char *ret_name = NULL;

	slurm_mutex_lock(&jobcomp_lock);
	if (user_id != cache_uid) {
		uname = uid_to_string((uid_t) user_id);
		snprintf(cache_name, sizeof(cache_name), "%s", uname);
		xfree(uname);
		cache_uid = user_id;
	}
	ret_name = xstrdup(cache_name);
	slurm_mutex_unlock(&jobcomp_lock);

	return ret_name;
}

/* get the group name for the give group_id */
static char *_get_group_name(uint32_t group_id)
{
	static uint32_t cache_gid      = 0;
	static char     cache_name[32] = "root", *gname;
	char *ret_name = NULL;

	slurm_mutex_lock(&jobcomp_lock);
	if (group_id != cache_gid) {
		gname = gid_to_string((gid_t) group_id);
		snprintf(cache_name, sizeof(cache_name), "%s", gname);
		xfree(gname);
		cache_gid = group_id;
	}
	ret_name = xstrdup(cache_name);
	slurm_mutex_unlock(&jobcomp_lock);

	return ret_name;
}

/*
 * Linear search through table of errno values and strings,
 * returns NULL on error, string on success.
 */
static char *_lookup_slurm_api_errtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < sizeof(slurm_errtab) / sizeof(slurm_errtab_t); i++) {
		if (slurm_errtab[i].xe_number == errnum) {
			res = slurm_errtab[i].xe_message;
			break;
		}
	}
	return res;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;

	if (first) {
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
	if (jobcomp_mysql_conn) {
		destroy_mysql_conn(jobcomp_mysql_conn);
		jobcomp_mysql_conn = NULL;
	}
	return SLURM_SUCCESS;
}

extern int slurm_jobcomp_set_location(char *location)
{
	mysql_db_info_t *db_info;
	int rc = SLURM_SUCCESS;
	char *db_name = NULL;
	int i = 0;

	if (jobcomp_mysql_conn && mysql_db_ping(jobcomp_mysql_conn) == 0)
		return SLURM_SUCCESS;

	if (!location)
		db_name = slurm_get_jobcomp_loc();
	else {
		while(location[i]) {
			if (location[i] == '.' || location[i] == '/') {
				debug("%s doesn't look like a database "
				      "name using %s",
				      location, DEFAULT_JOB_COMP_DB);
				break;
			}
			i++;
		}
		if (location[i])
			db_name = xstrdup(DEFAULT_JOB_COMP_DB);
		else
			db_name = xstrdup(location);
	}

	debug2("mysql_connect() called for db %s", db_name);
	/* Just make sure our connection is gone. */
	fini();
	jobcomp_mysql_conn = create_mysql_conn(0, 0, NULL);

	db_info = create_mysql_db_info(SLURM_MYSQL_PLUGIN_JC);

	mysql_db_get_db_connection(jobcomp_mysql_conn, db_name, db_info);
	xfree(db_name);

	rc = _mysql_jobcomp_check_tables();

	destroy_mysql_db_info(db_info);

	if (rc == SLURM_SUCCESS)
		debug("Jobcomp database init finished");
	else
		debug("Jobcomp database init failed");
	return rc;
}

extern int slurm_jobcomp_log_record(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	char *usr_str = NULL, *grp_str = NULL, lim_str[32], *jname = NULL;
	char *connect_type = NULL, *reboot = NULL, *rotate = NULL,
		*geometry = NULL, *start = NULL,
		*blockid = NULL;
	uint32_t job_state;
	char *query = NULL;
	uint32_t time_limit, start_time, end_time;

	if (!jobcomp_mysql_conn || mysql_db_ping(jobcomp_mysql_conn) != 0) {
		char *loc = slurm_get_jobcomp_loc();
		if (slurm_jobcomp_set_location(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}

	usr_str = _get_user_name(job_ptr->user_id);
	grp_str = _get_group_name(job_ptr->group_id);

	if ((job_ptr->time_limit == NO_VAL) && job_ptr->part_ptr)
		time_limit = job_ptr->part_ptr->max_time;
	else
		time_limit = job_ptr->time_limit;
	if (time_limit == INFINITE)
		strcpy(lim_str, "UNLIMITED");
	else {
		snprintf(lim_str, sizeof(lim_str), "%lu",
			 (unsigned long) time_limit);
	}

	/* Job will typically be COMPLETING when this is called.
	 * We remove the flags to get the eventual completion state:
	 * JOB_FAILED, JOB_TIMEOUT, etc. */
	if (IS_JOB_RESIZING(job_ptr)) {
		job_state = JOB_RESIZING;
		if (job_ptr->resize_time)
			start_time = job_ptr->resize_time;
		else
			start_time = job_ptr->start_time;
		end_time = time(NULL);
	} else {
		job_state = job_ptr->job_state & JOB_STATE_BASE;
		if (job_ptr->resize_time)
			start_time = job_ptr->resize_time;
		else if (job_ptr->start_time > job_ptr->end_time) {
			/* Job cancelled while pending and
			 * expected start time is in the future. */
			start_time = 0;
		} else
			start_time = job_ptr->start_time;
		end_time = job_ptr->end_time;
	}

	if (job_ptr->name && job_ptr->name[0])
		jname = slurm_add_slash_to_quotes(job_ptr->name);
	else
		jname = xstrdup("allocation");

	connect_type = select_g_select_jobinfo_xstrdup(job_ptr->select_jobinfo,
						       SELECT_PRINT_CONNECTION);
	reboot = select_g_select_jobinfo_xstrdup(job_ptr->select_jobinfo,
						 SELECT_PRINT_REBOOT);
	rotate = select_g_select_jobinfo_xstrdup(job_ptr->select_jobinfo,
						 SELECT_PRINT_ROTATE);
	geometry = select_g_select_jobinfo_xstrdup(job_ptr->select_jobinfo,
						   SELECT_PRINT_GEOMETRY);
	start = select_g_select_jobinfo_xstrdup(job_ptr->select_jobinfo,
						SELECT_PRINT_START);
#ifdef HAVE_BG
	blockid = select_g_select_jobinfo_xstrdup(job_ptr->select_jobinfo,
						  SELECT_PRINT_BG_ID);
#else
	blockid = select_g_select_jobinfo_xstrdup(job_ptr->select_jobinfo,
						  SELECT_PRINT_RESV_ID);
#endif
	query = xstrdup_printf(
		"insert into %s (jobid, uid, user_name, gid, group_name, "
		"name, state, proc_cnt, `partition`, timelimit, "
		"starttime, endtime, nodecnt",
		jobcomp_table);

	if (job_ptr->nodes)
		xstrcat(query, ", nodelist");
	if (connect_type)
		xstrcat(query, ", connect_type");
	if (reboot)
		xstrcat(query, ", reboot");
	if (rotate)
		xstrcat(query, ", rotate");
	if (job_ptr->details && (job_ptr->details->max_cpus != NO_VAL))
		xstrcat(query, ", maxprocs");
	if (geometry)
		xstrcat(query, ", geometry");
	if (start)
		xstrcat(query, ", start");
	if (blockid)
		xstrcat(query, ", blockid");
	xstrfmtcat(query, ") values (%u, %u, '%s', %u, '%s', '%s', %u, %u, "
		   "'%s', '%s', %u, %u, %u",
		   job_ptr->job_id, job_ptr->user_id, usr_str,
		   job_ptr->group_id, grp_str, jname,
		   job_state, job_ptr->total_cpus, job_ptr->partition, lim_str,
		   start_time, end_time, job_ptr->node_cnt);

	if (job_ptr->nodes)
		xstrfmtcat(query, ", '%s'", job_ptr->nodes);

	if (connect_type) {
		xstrfmtcat(query, ", '%s'", connect_type);
		xfree(connect_type);
	}
	if (reboot) {
		xstrfmtcat(query, ", '%s'", reboot);
		xfree(reboot);
	}
	if (rotate) {
		xstrfmtcat(query, ", '%s'", rotate);
		xfree(rotate);
	}
	if (job_ptr->details && (job_ptr->details->max_cpus != NO_VAL))
		xstrfmtcat(query, ", '%u'", job_ptr->details->max_cpus);

	if (geometry) {
		xstrfmtcat(query, ", '%s'", geometry);
		xfree(geometry);
	}
	if (start) {
		xstrfmtcat(query, ", '%s'", start);
		xfree(start);
	}
	if (blockid) {
		xstrfmtcat(query, ", '%s'", blockid);
		xfree(blockid);
	}
	xstrcat(query, ")");
	debug3("(%s:%d) query\n%s",
	       THIS_FILE, __LINE__, query);
	rc = mysql_db_query(jobcomp_mysql_conn, query);
	xfree(usr_str);
	xfree(grp_str);
	xfree(jname);
	xfree(query);

	return rc;
}

extern int slurm_jobcomp_get_errno(void)
{
	return plugin_errno;
}

extern char *slurm_jobcomp_strerror(int errnum)
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}

/*
 * get info from the storage
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern List slurm_jobcomp_get_jobs(slurmdb_job_cond_t *job_cond)
{
	List job_list = NULL;

	if (!jobcomp_mysql_conn || mysql_db_ping(jobcomp_mysql_conn) != 0) {
		char *loc = slurm_get_jobcomp_loc();
		if (slurm_jobcomp_set_location(loc) == SLURM_ERROR) {
			xfree(loc);
			return job_list;
		}
		xfree(loc);
	}

	job_list = mysql_jobcomp_process_get_jobs(job_cond);

	return job_list;
}

/*
 * expire old info from the storage
 */
extern int slurm_jobcomp_archive(slurmdb_archive_cond_t *arch_cond)
{
	if (!jobcomp_mysql_conn || mysql_db_ping(jobcomp_mysql_conn) != 0) {
		char *loc = slurm_get_jobcomp_loc();
		if (slurm_jobcomp_set_location(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}

	return mysql_jobcomp_process_archive(arch_cond);
}
