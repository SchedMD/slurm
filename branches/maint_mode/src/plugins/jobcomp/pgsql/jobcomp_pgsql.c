/*****************************************************************************\
 *  jobcomp_pgsql.c - Store/Get all information in a postgresql storage.
 *
 *  $Id: storage_pgsql.c 10893 2007-01-29 21:53:48Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#include "pgsql_jobcomp_process.h"
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
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] = "Job completion POSTGRESQL plugin";
const char plugin_type[] = "jobcomp/pgsql";
const uint32_t plugin_version = 100;

#ifdef HAVE_PGSQL

#define DEFAULT_JOBCOMP_DB "slurm_jobcomp_db"

PGconn *jobcomp_pgsql_db = NULL;

char *jobcomp_table = "jobcomp_table";
storage_field_t jobcomp_table_fields[] = {
	{ "jobid", "integer not null" },
	{ "uid", "smallint not null" },
	{ "user_name", "text not null" },
	{ "gid", "smallint not null" },
	{ "group_name", "text not null" },
	{ "name", "text not null" },
	{ "state", "smallint not null" },
	{ "partition", "text not null" }, 
	{ "timelimit", "text not null" },
	{ "starttime", "bigint default 0 not null" }, 
	{ "endtime", "bigint default 0 not null" },
	{ "nodelist", "text" }, 
	{ "nodecnt", "integer not null" },
	{ "proc_cnt", "integer not null" },
	{ "connect_type", "text" },
	{ "reboot", "text" },
	{ "rotate", "text" },
	{ "maxprocs", "integer default 0 not null" },
	{ "geometry", "text" },
	{ "start", "text" },
	{ "blockid", "text" },
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

static pgsql_db_info_t *_pgsql_jobcomp_create_db_info()
{
	pgsql_db_info_t *db_info = xmalloc(sizeof(pgsql_db_info_t));
	db_info->port = slurm_get_jobcomp_port();
	/* it turns out it is better if using defaults to let postgres
	   handle them on it's own terms */
	if(!db_info->port) {
		db_info->port = 5432;
		slurm_set_jobcomp_port(db_info->port);
	}
	db_info->host = slurm_get_jobcomp_host();
	db_info->user = slurm_get_jobcomp_user();	
	db_info->pass = slurm_get_jobcomp_pass();	
	return db_info;
}

static int _pgsql_jobcomp_check_tables(char *user)
{

	int i = 0, job_found = 0;
	PGresult *result = NULL;
	char *query = xstrdup_printf("select tablename from pg_tables "
				     "where tableowner='%s' "
				     "and tablename !~ '^pg_+'", user);

	if(!(result =
	     pgsql_db_query_ret(jobcomp_pgsql_db, query))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	for (i = 0; i < PQntuples(result); i++) {
		if(!job_found 
		   && !strcmp(jobcomp_table, PQgetvalue(result, i, 0))) 
			job_found = 1;
	}
	PQclear(result);

	if(!job_found)
		if(pgsql_db_create_table(jobcomp_pgsql_db, jobcomp_table,
					 jobcomp_table_fields,
					 ")") == SLURM_ERROR)
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
#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
#ifndef HAVE_PGSQL
	fatal("No Postgresql storage was found on the machine. "
	      "Please check the config.log from the run of configure "
	      "and run again.");	
#endif
	if(first) {
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
#ifdef HAVE_PGSQL
	if (jobcomp_pgsql_db) {
		PQfinish(jobcomp_pgsql_db);
		jobcomp_pgsql_db = NULL;
	}
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int slurm_jobcomp_set_location(char *location)
{
#ifdef HAVE_PGSQL
	pgsql_db_info_t *db_info = _pgsql_jobcomp_create_db_info();
	int rc = SLURM_SUCCESS;
	char *db_name = NULL;
	int i = 0;
	
	if(jobcomp_pgsql_db && PQstatus(jobcomp_pgsql_db) == CONNECTION_OK) 
		return SLURM_SUCCESS;
	
	if(!location)
		db_name = DEFAULT_JOBCOMP_DB;
	else {
		while(location[i]) {
			if(location[i] == '.' || location[i] == '/') {
				debug("%s doesn't look like a database "
				      "name using %s",
				      location, DEFAULT_JOBCOMP_DB);
				break;
			}
			i++;
		}
		if(location[i]) 
			db_name = DEFAULT_JOBCOMP_DB;
		else
			db_name = location;
	}
		
	debug2("pgsql_connect() called for db %s", db_name);
	
	pgsql_get_db_connection(&jobcomp_pgsql_db, db_name, db_info);
	
	rc = _pgsql_jobcomp_check_tables(db_info->user);

	destroy_pgsql_db_info(db_info);

	if(rc == SLURM_SUCCESS) 
		debug("Jobcomp database init finished");
	else
		debug("Jobcomp database init failed");
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int slurm_jobcomp_log_record(struct job_record *job_ptr)
{
#ifdef HAVE_PGSQL
	int rc = SLURM_SUCCESS;
	char *usr_str = NULL, *grp_str = NULL, lim_str[32];
	char *connect_type = NULL, *reboot = NULL, *rotate = NULL,
		*maxprocs = NULL, *geometry = NULL, *start = NULL,
		*blockid = NULL;
	enum job_states job_state;
	char *query = NULL;

	if(!jobcomp_pgsql_db || PQstatus(jobcomp_pgsql_db) != CONNECTION_OK) {
		char *loc = slurm_get_jobcomp_loc();
		if(slurm_jobcomp_set_location(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}

	usr_str = _get_user_name(job_ptr->user_id);
	grp_str = _get_group_name(job_ptr->group_id);
	if (job_ptr->time_limit == INFINITE)
		strcpy(lim_str, "UNLIMITED");
	else
		snprintf(lim_str, sizeof(lim_str), "%lu", 
				(unsigned long) job_ptr->time_limit);

	/* Job will typically be COMPLETING when this is called. 
	 * We remove this flag to get the eventual completion state:
	 * JOB_FAILED, JOB_TIMEOUT, etc. */
	job_state = job_ptr->job_state & (~JOB_COMPLETING);
	
	connect_type = select_g_xstrdup_jobinfo(job_ptr->select_jobinfo,
						SELECT_PRINT_CONNECTION);
	reboot = select_g_xstrdup_jobinfo(job_ptr->select_jobinfo,
					  SELECT_PRINT_REBOOT);
	rotate = select_g_xstrdup_jobinfo(job_ptr->select_jobinfo,
					  SELECT_PRINT_ROTATE);
	maxprocs = select_g_xstrdup_jobinfo(job_ptr->select_jobinfo,
					    SELECT_PRINT_MAX_PROCS);
	geometry = select_g_xstrdup_jobinfo(job_ptr->select_jobinfo,
					    SELECT_PRINT_GEOMETRY);
	start = select_g_xstrdup_jobinfo(job_ptr->select_jobinfo,
					 SELECT_PRINT_START);
#ifdef HAVE_BG
	blockid = select_g_xstrdup_jobinfo(job_ptr->select_jobinfo,
					   SELECT_PRINT_BG_ID);
#else
	blockid = select_g_xstrdup_jobinfo(job_ptr->select_jobinfo,
					   SELECT_PRINT_RESV_ID);
#endif
	query = xstrdup_printf(
		"insert into %s (jobid, uid, user_name, gid, group_name, "
		"name, state, proc_cnt, partition, timelimit, "
		"starttime, endtime, nodecnt",
		jobcomp_table);

	if(job_ptr->nodes)
		xstrcat(query, ", nodelist");		
	if(connect_type)
		xstrcat(query, ", connect_type");
	if(reboot)
		xstrcat(query, ", reboot");
	if(rotate)
		xstrcat(query, ", rotate");
	if(maxprocs)
		xstrcat(query, ", maxprocs");
	if(geometry)
		xstrcat(query, ", geometry");
	if(start)
		xstrcat(query, ", start");
	if(blockid)
		xstrcat(query, ", blockid");

	xstrfmtcat(query, ") values (%u, %u, '%s', %u, '%s', \"%s\", %d, %u, "
		   "'%s', \"%s\", %u, %u,  %u",
		   job_ptr->job_id, job_ptr->user_id, usr_str,
		   job_ptr->group_id, grp_str, job_ptr->name,
		   job_state, job_ptr->total_procs, job_ptr->partition, lim_str,
		   (int)job_ptr->start_time, (int)job_ptr->end_time,
		   job_ptr->node_cnt);
	
	if(job_ptr->nodes)
		xstrfmtcat(query, ", '%s'", job_ptr->nodes);		

	if(connect_type) {
		xstrfmtcat(query, ", '%s'", connect_type);
		xfree(connect_type);
	}
	if(reboot) {
		xstrfmtcat(query, ", '%s'", reboot);
		xfree(reboot);
	}
	if(rotate) {
		xstrfmtcat(query, ", '%s'", rotate);
		xfree(rotate);
	}
	if(maxprocs) {
		xstrfmtcat(query, ", '%s'", maxprocs);
		xfree(maxprocs);
	}
	if(geometry) {
		xstrfmtcat(query, ", '%s'", geometry);
		xfree(geometry);
	}
	if(start) {
		xstrfmtcat(query, ", '%s'", start);
		xfree(start);
	}
	if(blockid) {
		xstrfmtcat(query, ", '%s'", blockid);
		xfree(blockid);
	}
	xstrcat(query, ")");
	//info("here is the query %s", query);

	rc = pgsql_db_query(jobcomp_pgsql_db, query);
	xfree(usr_str);

	return rc;
#else
	return SLURM_ERROR;
#endif 
}

extern int slurm_jobcomp_get_errno()
{
#ifdef HAVE_PGSQL
	return plugin_errno;
#else
	return SLURM_ERROR;
#endif 
}

extern char *slurm_jobcomp_strerror(int errnum)
{
#ifdef HAVE_PGSQL
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
#else
	return NULL;
#endif 
}

/* 
 * get info from the storage 
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern List slurm_jobcomp_get_jobs(acct_job_cond_t *job_cond)
{
	List job_list = NULL;

#ifdef HAVE_PGSQL
	if(!jobcomp_pgsql_db || PQstatus(jobcomp_pgsql_db) != CONNECTION_OK) {
		char *loc = slurm_get_jobcomp_loc();
		if(slurm_jobcomp_set_location(loc) == SLURM_ERROR) {
			xfree(loc);
			return NULL;
		}
		xfree(loc);
	}

	job_list = pgsql_jobcomp_process_get_jobs(job_cond);	
#endif 
	return job_list;
}

/* 
 * expire old info from the storage 
 */
extern int slurm_jobcomp_archive(acct_archive_cond_t *arch_cond)
{
#ifdef HAVE_PGSQL
	if(!jobcomp_pgsql_db || PQstatus(jobcomp_pgsql_db) != CONNECTION_OK) {
		char *loc = slurm_get_jobcomp_loc();
		if(slurm_jobcomp_set_location(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}

	return pgsql_jobcomp_process_archive(arch_cond);
#endif 
	return SLURM_ERROR;
}
