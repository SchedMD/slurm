/*****************************************************************************\
 *  accounting_storage_pgsql.c - accounting interface to pgsql.
 *
 *  $Id: accounting_storage_pgsql.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2008 The Regents of the University of California.
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

#include "src/common/slurm_accounting_storage.h"
#include "pgsql_jobacct_process.h"

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
const char plugin_name[] = "Accounting storage PGSQL plugin";
const char plugin_type[] = "accounting_storage/pgsql";
const uint32_t plugin_version = 100;

#ifdef HAVE_PGSQL
#define DEFAULT_ACCT_DB "slurm_acct_db"

PGconn *acct_pgsql_db = NULL;

char *acct_coord_table = "acct_coord_table";
char *acct_table = "acct_table";
char *assoc_day_table = "assoc_day_usage_table";
char *assoc_hour_table = "assoc_hour_usage_table";
char *assoc_month_table = "assoc_month_usage_table";
char *assoc_table = "assoc_table";
char *cluster_day_table = "cluster_day_usage_table";
char *cluster_hour_table = "cluster_hour_usage_table";
char *cluster_month_table = "cluster_month_usage_table";
char *cluster_table = "cluster_table";
char *job_index = "job_index_table";
char *job_table = "job_table";
char *rusage_table = "rusage_table";
char *step_table = "step_table";
char *txn_table = "txn_table";
char *user_table = "user_table";

static pgsql_db_info_t *_pgsql_acct_create_db_info()
{
	pgsql_db_info_t *db_info = xmalloc(sizeof(pgsql_db_info_t));
	db_info->port = slurm_get_accounting_storage_port();
	/* it turns out it is better if using defaults to let postgres
	   handle them on it's own terms */
	if(!db_info->port)
		db_info->port = 5432;
	db_info->host = slurm_get_accounting_storage_host();
	db_info->user = slurm_get_accounting_storage_user();	
	db_info->pass = slurm_get_accounting_storage_pass();	
	return db_info;
}

static int _pgsql_acct_check_tables(char *user)
{
	storage_field_t acct_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "tinyint default 0" },
		{ "name", "text not null" },
		{ "description", "text not null" },
		{ "organization", "text not null" },
		{ "expedite", "smallint default 1 not null" },
		{ NULL, NULL}		
	};

	storage_field_t acct_coord_table_fields[] = {
		{ "deleted", "tinyint default 0" },
		{ "acct", "text not null" },
		{ "name", "text not null" },
		{ NULL, NULL}		
	};

	storage_field_t assoc_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "tinyint default 0" },
		{ "id", "serial" },
		{ "user", "text not null" },
		{ "acct", "text not null" },
		{ "cluster", "text not null" },
		{ "partition", "text not null" },
		{ "parent", "int not null" },
		{ "lft", "int not null" },
		{ "rgt", "int not null" },
		{ "fairshare", "int default 1 not null" },
		{ "max_jobs", "int default NULL" },
		{ "max_nodes_per_job", "int default NULL" },
		{ "max_wall_duration_per_job", "int default NULL" },
		{ "max_cpu_seconds_per_job", "int default NULL" },
		{ NULL, NULL}		
	};

	storage_field_t assoc_usage_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "tinyint default 0" },
		{ "associd", "int not null" },
		{ "period_start", "bigint not null" },
		{ "cpu_count", "bigint default 0" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ NULL, NULL}		
	};

	storage_field_t cluster_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "tinyint default 0" },
		{ "name", "text not null" },
		{ "primary", "text not null" },
		{ "backup", "text not null" },
		{ NULL, NULL}		
	};

	storage_field_t cluster_usage_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "tinyint default 0" },
		{ "cluster", "text not null" },
		{ "period_start", "bigint not null" },
		{ "cpu_count", "bigint default 0" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "down_cpu_secs", "bigint default 0" },
		{ "idle_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ NULL, NULL}		
	};

	storage_field_t index_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "id", "serial" },
		{ "jobid ", "integer not null" },
		{ "partition", "text not null" },
		{ "submit", "bigint not null" },
		{ "uid", "smallint not null" },
		{ "gid", "smallint not null" },
		{ "blockid", "text" },
		{ NULL, NULL}		
	};

	storage_field_t job_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "id", "int not null" },
		{ "start", "bigint default 0 not null" },
		{ "endtime", "bigint default 0 not null" },
		{ "suspended", "bigint default 0 not null" },
		{ "name", "text not null" }, 
		{ "track_steps", "smallint not null" },
		{ "state", "smallint not null" }, 
		{ "comp_code", "int default 0 not null" },
		{ "priority", "bigint not null" },
		{ "cpus", "integer not null" }, 
		{ "nodelist", "text" },
		{ "account", "text" },
		{ "kill_requid", "smallint default -1 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_rusage_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "id", "int not null" },
		{ "stepid", "smallint not null" },
		{ "cpu_sec", "bigint default 0 not null" },
		{ "cpu_usec", "bigint default 0 not null" },
		{ "user_sec", "bigint default 0 not null" },
		{ "user_usec", "bigint default 0 not null" },
		{ "sys_sec", "bigint default 0 not null" },
		{ "sys_usec", "bigint default 0 not null" },
		{ "max_rss", "bigint default 0 not null" },
		{ "max_ixrss", "bigint default 0 not null" },
		{ "max_idrss", "bigint default 0 not null" },
		{ "max_isrss", "bigint default 0 not null" },
		{ "max_minflt", "bigint default 0 not null" },
		{ "max_majflt", "bigint default 0 not null" },
		{ "max_nswap", "bigint default 0 not null" },
		{ "inblock", "bigint default 0 not null" },
		{ "outblock", "bigint default 0 not null" },
		{ "msgsnd", "bigint default 0 not null" },
		{ "msgrcv", "bigint default 0 not null" },
		{ "nsignals", "bigint default 0 not null" },
		{ "nvcsw", "bigint default 0 not null" },
		{ "nivcsw", "bigint default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "id", "int not null" },
		{ "stepid", "smallint not null" },
		{ "start", "bigint default 0 not null" },
		{ "endtime", "bigint default 0 not null" },
		{ "suspended", "bigint default 0 not null" },
		{ "name", "text not null" },
		{ "nodelist", "text not null" },
		{ "state", "smallint not null" },
		{ "kill_requid", "smallint default -1 not null" },
		{ "comp_code", "int default 0 not null" },
		{ "cpus", "int not null" },
		{ "max_vsize", "integer default 0 not null" },
		{ "max_vsize_task", "smallint default 0 not null" },
		{ "max_vsize_node", "integer default 0 not null" },
		{ "ave_vsize", "float default 0.0 not null" },
		{ "max_rss", "integer default 0 not null" },
		{ "max_rss_task", "smallint default 0 not null" },
		{ "max_rss_node", "integer default 0 not null" },
		{ "ave_rss", "float default 0.0 not null" },
		{ "max_pages", "integer default 0 not null" },
		{ "max_pages_task", "smallint default 0 not null" },
		{ "max_pages_node", "integer default 0 not null" },
		{ "ave_pages", "float default 0.0 not null" },
		{ "min_cpu", "integer default 0 not null" },
		{ "min_cpu_task", "smallint default 0 not null" },
		{ "min_cpu_node", "integer default 0 not null" },
		{ "ave_cpu", "float default 0.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t txn_table_fields[] = {
		{ "id", "serial" },
		{ "timestamp", "bigint default 0" },
		{ "action", "text not null" },
		{ "object", "text not null" },
		{ "name", "text not null" },
		{ "actor", "text not null" },
		{ "info", "text not null" },
		{ NULL, NULL}		
	};

	storage_field_t user_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "bool default 0" },
		{ "name", "text not null" },
		{ "default_acct", "text not null" },
		{ "expedite", "smallint default 1 not null" },
		{ "admin_level", "smallint default 1 not null" },
		{ NULL, NULL}		
	};

	int i = 0, index_found = 0, job_found = 0;
	int step_found = 0, rusage_found = 0, txn_found = 0;
	int user_found = 0, acct_found = 0, acct_coord_found = 0;
	int cluster_found = 0, cluster_hour_found = 0,
		cluster_day_found = 0, cluster_month_found = 0;
	int assoc_found = 0, assoc_hour_found = 0,
		assoc_day_found = 0, assoc_month_found = 0;
	PGresult *result = NULL;
	char *query = xstrdup_printf("select tablename from pg_tables "
				     "where tableowner='%s' "
				     "and tablename !~ '^pg_+'", user);

	if(!(result =
	     pgsql_db_query_ret(acct_pgsql_db, query))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	for (i = 0; i < PQntuples(result); i++) {
		if(!acct_coord_found &&
		   !strcmp(acct_coord_table, PQgetvalue(result, i, 0))) 
			acct_coord_found = 1;
		else if(!acct_found &&
			!strcmp(acct_table, PQgetvalue(result, i, 0))) 
			acct_found = 1;
		else if(!assoc_found &&
			!strcmp(assoc_table, PQgetvalue(result, i, 0))) 
			assoc_found = 1;
		else if(!assoc_day_found &&
			!strcmp(assoc_day_table, PQgetvalue(result, i, 0))) 
			assoc_day_found = 1;
		else if(!assoc_hour_found &&
			!strcmp(assoc_hour_table, PQgetvalue(result, i, 0))) 
			assoc_hour_found = 1;
		else if(!assoc_month_found &&
			!strcmp(assoc_month_table, PQgetvalue(result, i, 0))) 
			assoc_month_found = 1;
		else if(!cluster_found &&
			!strcmp(cluster_table, PQgetvalue(result, i, 0))) 
			cluster_found = 1;
		else if(!cluster_day_found &&
			!strcmp(cluster_day_table, PQgetvalue(result, i, 0))) 
			cluster_day_found = 1;
		else if(!cluster_hour_found &&
			!strcmp(cluster_hour_table, PQgetvalue(result, i, 0))) 
			cluster_hour_found = 1;
		else if(!cluster_month_found &&
			!strcmp(cluster_month_table, PQgetvalue(result, i, 0))) 
			cluster_month_found = 1;
		else if(!index_found && 
			!strcmp(index_table, PQgetvalue(result, i, 0))) 
			index_found = 1;
		else if(!job_found &&
			!strcmp(job_table, PQgetvalue(result, i, 0))) 
			job_found = 1;
		else if(!rusage_found &&
			!strcmp(rusage_table, PQgetvalue(result, i, 0))) 
			rusage_found = 1;
		else if(!step_found &&
			!strcmp(step_table, PQgetvalue(result, i, 0))) 
			step_found = 1;
		else if(!txn_found &&
			!strcmp(txn_table, PQgetvalue(result, i, 0))) 
			txn_found = 1;
		else if(!user_found &&
			!strcmp(user_table, PQgetvalue(result, i, 0))) 
			user_found = 1;
	}
	PQclear(result);

	if(!acct_coord_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 acct_coord_table, 
					 acct_coord_table_fields,
					 ", primary key (acct(20), name(20)))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       acct_coord_table,
					       acct_coord_table_fields))
			return SLURM_ERROR;
	}

	if(!acct_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 acct_table, acct_table_fields,
					 ", primary key (name(20)))") 
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       acct_table,
					       acct_table_fields))
			return SLURM_ERROR;
	}

	if(!assoc_day_found) {
		if(pgsql_db_create_table(
			   acct_pgsql_db, 
			   assoc_day_table,
			   assoc_usage_table_fields,
			   ", primary key (associd, period_start))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       assoc_day_table,
					       assoc_usage_table_fields))
			return SLURM_ERROR;
	}

	if(!assoc_hour_found) {
		if(pgsql_db_create_table(
			   acct_pgsql_db, 
			   assoc_hour_table,
			   assoc_usage_table_fields,
			   ", primary key (associd, period_start))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       assoc_hour_table,
					       assoc_usage_table_fields))
			return SLURM_ERROR;
	}

	if(!assoc_month_found) {
		if(pgsql_db_create_table(
			   acct_pgsql_db, 
			   assoc_month_table,
			   assoc_usage_table_fields,
			   ", primary key (associd, period_start))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       assoc_month_table,
					       assoc_usage_table_fields))
			return SLURM_ERROR;
	}

	if(!assoc_found) {
		if(pgsql_db_create_table(
			   acct_pgsql_db, 
			   assoc_table, assoc_table_fields,
			   ", primary key (id), "
			   "unique index (user(20), acct(20), "
			   "cluster(20), partition(20)))") 
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       assoc_table,
					       assoc_table_fields))
			return SLURM_ERROR;
	}

	if(!cluster_day_found) {
		if(pgsql_db_create_table(
			   acct_pgsql_db, 
			   cluster_day_table, 
			   cluster_usage_table_fields,
			   ", primary key (cluster(20), period_start))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       cluster_day_table,
					       cluster_usage_table_fields))
			return SLURM_ERROR;
	}

	if(!cluster_hour_found) {
		if(pgsql_db_create_table(
			   acct_pgsql_db, 
			   cluster_hour_table, 
			   cluster_usage_table_fields,
			   ", primary key (cluster(20), period_start))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       cluster_hour_table,
					       cluster_usage_table_fields))
			return SLURM_ERROR;
	}

	if(!cluster_month_found) {
		if(pgsql_db_create_table(
			   acct_pgsql_db, 
			   cluster_month_table, 
			   cluster_usage_table_fields,
			   ", primary key (cluster(20), period_start))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       cluster_month_table,
					       cluster_usage_table_fields))
			return SLURM_ERROR;
	}

	if(!cluster_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 cluster_table, cluster_table_fields,
					 ", primary key (name(20)))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       cluster_table,
					       cluster_table_fields))
			return SLURM_ERROR;
	}

	if(!index_found) {
		if(pgsql_db_create_table(acct_pgsql_db,  
					 index_table, index_table_fields,
					 ", primary key (id), "
					 "unique index (jobid, associd))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       index_table,
					       index_table_fields))
			return SLURM_ERROR;
	}

	if(!job_found) {
		if(pgsql_db_create_table(acct_pgsql_db,  
					 job_table, job_table_fields,
					 ", primary key (id))") == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       job_table,
					       job_table_fields))
			return SLURM_ERROR;
	}
	
	if(!rusage_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 rusage_table, step_rusage_fields,
					 ", primary key (id, stepid))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       rusage_table,
					       step_rusage_fields))
			return SLURM_ERROR;
	}

	if(!step_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 step_table, step_table_fields,
					 ", primary key (id, stepid))")
		   == SLURM_ERROR)
			return SLURM_ERROR;

	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       step_table,
					       step_table_fields))
			return SLURM_ERROR;
	}

	if(!txn_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 txn_table, txn_table_fields,
					 ", primary key (id))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       txn_table,
					       txn_table_fields))
			return SLURM_ERROR;
	}

	if(!user_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 user_table, user_table_fields,
					 ", primary key (name(20)))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       user_table,
					       user_table_fields))
			return SLURM_ERROR;
	}

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
#ifdef HAVE_PGSQL
	pgsql_db_info_t *db_info = NULL;
	int rc = SLURM_SUCCESS;
	char *db_name = NULL;
	char *location = NULL;
#else
	fatal("No Postgres database was found on the machine. "
	      "Please check the configure log and run again.");	
#endif
	if(first) {
		/* since this can be loaded from many different places
		   only tell us once. */
#ifdef HAVE_PGSQL
		db_info = _pgsql_acct_create_db_info();		
		if(acct_pgsql_db && PQstatus(acct_pgsql_db) == CONNECTION_OK) 
			return SLURM_SUCCESS;
		
		location = slurm_get_accounting_storage_loc();
		if(!location)
			db_name = DEFAULT_ACCT_DB;
		else {
			int i = 0;
			while(location[i]) {
				if(location[i] == '.' || location[i] == '/') {
					debug("%s doesn't look like a database "
					      "name using %s",
					      location, DEFAULT_ACCT_DB);
					break;
				}
				i++;
			}
			if(location[i]) 
				db_name = DEFAULT_ACCT_DB;
			else
				db_name = location;
		}
		xfree(location);

		debug2("pgsql_connect() called for db %s", db_name);
		
		pgsql_get_db_connection(&acct_pgsql_db, db_name, db_info);
		
		rc = _pgsql_acct_check_tables(db_info->user);
		
		destroy_pgsql_db_info(db_info);
		
		if(rc == SLURM_SUCCESS)
			debug("Accounting Storage init finished");
		else
			error("Accounting Storage init failed");
		verbose("%s loaded", plugin_name);
		first = 0;
#endif
	} else {
		debug4("%s loaded", plugin_name);
	}

	return rc;
}

extern int fini ( void )
{
#ifdef HAVE_PGSQL
	if (acct_pgsql_db) {
		PQfinish(acct_pgsql_db);
		acct_pgsql_db = NULL;
	}
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_add_users(List user_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_coord(char *acct, acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(List acct_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_clusters(List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_associations(List association_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_get_assoc_id(acct_association_rec_t *assoc)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_validate_assoc_id(uint32_t assoc_id)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_users(acct_user_cond_t *user_q,
				       acct_user_rec_t *user)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_user_admin_level(acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_accts(acct_account_cond_t *acct_q,
				       acct_account_rec_t *acct)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_clusters(acct_cluster_cond_t *cluster_q,
					  acct_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_associations(acct_association_cond_t *assoc_q,
					      acct_association_rec_t *assoc)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_users(acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_coord(char *acct, acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_accts(acct_account_cond_t *acct_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_clusters(acct_account_cond_t *cluster_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_associations(acct_association_cond_t *assoc_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_get_users(acct_user_cond_t *user_q)
{
	return NULL;
}

extern List acct_storage_p_get_accts(acct_account_cond_t *acct_q)
{
	return NULL;
}

extern List acct_storage_p_get_clusters(acct_account_cond_t *cluster_q)
{
	return NULL;
}

extern List acct_storage_p_get_associations(acct_association_cond_t *assoc_q)
{
	return NULL;
}

extern int acct_storage_p_get_hourly_usage(acct_association_rec_t *acct_assoc,
					   time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_get_daily_usage(acct_association_rec_t *acct_assoc,
					  time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_get_monthly_usage(acct_association_rec_t *acct_assoc,
					    time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int clusteracct_storage_p_node_down(char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason)
{
	return SLURM_SUCCESS;
}
extern int clusteracct_storage_p_node_up(char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	return SLURM_SUCCESS;
}
extern int clusteracct_storage_p_cluster_procs(char *cluster,
					       uint32_t procs,
					       time_t event_time)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_get_hourly_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{

	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_get_daily_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{
	
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_get_monthly_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{
	
	return SLURM_SUCCESS;
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(struct job_record *job_ptr)
{
#ifdef HAVE_PGSQL
	int	rc=SLURM_SUCCESS;
	char	*jname, *account, *nodes;
	long	priority;
	int track_steps = 0;
#ifdef HAVE_BG
	char *block_id = NULL;
#endif
	char query[1024];
	int reinit = 0;

	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(init() == SLURM_ERROR) {
			return SLURM_ERROR;
		}
	}

	debug2("pgsql_jobacct_job_start() called");
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

	job_ptr->requid = -1; /* force to -1 for sacct to know this
			       * hasn't been set yet */
	snprintf(query, sizeof(query),
		 "insert into %s (jobid, partition, submit, uid, gid"
#ifdef HAVE_BG
		 ", blockid"
#endif
		 ") values (%u, '%s', %u, %u, %u"
#ifdef HAVE_BG
		 ", '%s'"
#endif
		 ")",
		 index_table, job_ptr->job_id, job_ptr->partition,
		 (int)job_ptr->details->submit_time, job_ptr->user_id,
		 job_ptr->group_id
#ifdef HAVE_BG
		 , block_id
#endif
		);
#ifdef HAVE_BG
	xfree(block_id);
#endif

try_again:
	if((job_ptr->db_index = pgsql_insert_ret_id(
		    acct_pgsql_db,  
		    "index_table_id_seq", query))) {
		snprintf(query, sizeof(query),
			 "insert into %s (id, start, name, track_steps, "
			 "state, priority, cpus, nodelist, account) "
			 "values (%u, %u, '%s', %d, %d, %ld, %u, '%s', '%s')",
			 job_table, job_ptr->db_index, 
			 (int)job_ptr->start_time,
			 jname, track_steps,
			 job_ptr->job_state & (~JOB_COMPLETING),
			 priority, job_ptr->total_procs,
			 nodes, account);
		rc = pgsql_db_query(acct_pgsql_db, query);
	} else if(!reinit) {
		error("It looks like the storage has gone "
		      "away trying to reconnect");
		fini();
		init();
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
#ifdef HAVE_PGSQL
	char query[1024];
	char	*account, *nodes;
	int rc=SLURM_SUCCESS;
	
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(init() == SLURM_ERROR) {
			return SLURM_ERROR;
		}
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
			 "nodelist='%s', account='%s', comp_code=%u, "
			 "kill_requid=%d where id=%u",
			 job_table, (int)job_ptr->start_time,
			 (int)job_ptr->end_time, 
			 job_ptr->job_state & (~JOB_COMPLETING),
			 nodes, account, job_ptr->exit_code,
			 job_ptr->requid, job_ptr->db_index);
		rc = pgsql_db_query(acct_pgsql_db, query);
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
#ifdef HAVE_PGSQL
	int cpus = 0;
	int rc=SLURM_SUCCESS;
	char node_list[BUFFER_SIZE];
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	char query[1024];
	
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(init() == SLURM_ERROR) {
			return SLURM_ERROR;
		}
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
		cpus = step_ptr->job_ptr->total_procs;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
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
			 "values (%u, %u, %u, '%s', %d, %u, '%s', %u)",
			 step_table, step_ptr->job_ptr->db_index,
			 step_ptr->step_id, 
			 (int)step_ptr->start_time, step_ptr->name,
			 JOB_RUNNING, cpus, node_list, 
			 step_ptr->job_ptr->requid);
		rc = pgsql_db_query(acct_pgsql_db, query);
		if(rc != SLURM_ERROR) {
			snprintf(query, sizeof(query),
				 "insert into %s (id, stepid) values (%u, %u)",
				 rusage_table, step_ptr->job_ptr->db_index,
				 step_ptr->step_id);
			rc = pgsql_db_query(acct_pgsql_db, query);
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
#ifdef HAVE_PGSQL
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
	
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(init() == SLURM_ERROR) {
			return SLURM_ERROR;
		}
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
		cpus = step_ptr->job_ptr->total_procs;
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
			 "kill_requid=%u, comp_code=%u, "
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
		rc = pgsql_db_query(acct_pgsql_db, query);
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
			
			rc = pgsql_db_query(acct_pgsql_db, query);
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
#ifdef HAVE_PGSQL
	char query[1024];
	int rc = SLURM_SUCCESS;
	
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(init() == SLURM_ERROR) {
			return SLURM_ERROR;
		}
	}
	
	if(job_ptr->db_index) {
		snprintf(query, sizeof(query),
			 "update %s set suspended=%u-suspended, state=%d "
			 "where id=%u",
			 job_table, (int)job_ptr->suspend_time, 
			 job_ptr->job_state & (~JOB_COMPLETING),
			 job_ptr->db_index);
		rc = pgsql_db_query(acct_pgsql_db, query);
		if(rc != SLURM_ERROR) {
			snprintf(query, sizeof(query),
				 "update %s set suspended=%u-suspended, "
				 "state=%d where id=%u and endtime=0",
				 step_table, (int)job_ptr->suspend_time, 
				 job_ptr->job_state, job_ptr->db_index);
			rc = pgsql_db_query(acct_pgsql_db, query);			
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
extern List jobacct_storage_p_get_jobs(List selected_steps,
				       List selected_parts,
				       void *params)
{
	List job_list = NULL;
#ifdef HAVE_PGSQL
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(init() == SLURM_ERROR) {
			return job_list;
		}
	}

	job_list = pgsql_jobacct_process_get_jobs(selected_steps, 
						  selected_parts,
						  params);	
#endif
	return job_list;
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_p_archive(List selected_parts,
				       void *params)
{
#ifdef HAVE_PGSQL
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(init() == SLURM_ERROR) {
			return;
		}
	}

	pgsql_jobacct_process_archive(selected_parts, params);
#endif
	return;
}

