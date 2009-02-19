/*****************************************************************************\
 *  accounting_storage_pgsql.c - accounting interface to pgsql.
 *
 *  $Id: accounting_storage_pgsql.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include <strings.h>
#include "pgsql_jobacct_process.h"
#include "src/common/slurmdbd_defs.h"

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
#ifndef HAVE_PGSQL
typedef void PGconn;
#else
#define DEFAULT_ACCT_DB "slurm_acct_db"

static pgsql_db_info_t *pgsql_db_info = NULL;
static char *pgsql_db_name = NULL;
		
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
char *event_table = "cluster_event_table";
char *job_table = "job_table";
char *qos_table = "qos_table";
char *step_table = "step_table";
char *txn_table = "txn_table";
char *user_table = "user_table";
char *last_ran_table = "last_ran_table";
char *suspend_table = "suspend_table";

static int _get_db_index(PGconn *acct_pgsql_db,
			 time_t submit, uint32_t jobid, uint32_t associd)
{
	PGresult *result = NULL;
	int db_index = -1;
	char *query = xstrdup_printf("select id from %s where "
				     "submit=%u and jobid=%u and associd=%u",
				     job_table, (int)submit, jobid, associd);

	if(!(result = pgsql_db_query_ret(acct_pgsql_db, query))) {
		xfree(query);
		return -1;
	}

	xfree(query);
	
	if(!PQntuples(result)) {
		PQclear(result);
		error("We can't get a db_index for this combo, "
		      "submit=%u and jobid=%u and associd=%u.",
		      (int)submit, jobid, associd);
		return -1;
	}
	db_index = atoi(PQgetvalue(result, 0, 0));	
	PQclear(result);
	
	return db_index;
}


static pgsql_db_info_t *_pgsql_acct_create_db_info()
{
	pgsql_db_info_t *db_info = xmalloc(sizeof(pgsql_db_info_t));
	db_info->port = slurm_get_accounting_storage_port();
	/* it turns out it is better if using defaults to let postgres
	   handle them on it's own terms */
	if(!db_info->port) {
		db_info->port = 5432;
		slurm_set_accounting_storage_port(db_info->port);
	}
	db_info->host = slurm_get_accounting_storage_host();
	if(!db_info->host)
		db_info->host = xstrdup("localhost");
	db_info->user = slurm_get_accounting_storage_user();	
	db_info->pass = slurm_get_accounting_storage_pass();	
	return db_info;
}

static int _pgsql_acct_check_tables(PGconn *acct_pgsql_db,
				    char *user)
{
	storage_field_t acct_coord_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "smallint default 0" },
		{ "acct", "text not null" },
		{ "user_name", "text not null" },
		{ NULL, NULL}		
	};

	storage_field_t acct_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "smallint default 0" },
		{ "name", "text not null" },
		{ "description", "text not null" },
		{ "organization", "text not null" },
		{ "qos", "text not null" },
		{ NULL, NULL}		
	};

	storage_field_t assoc_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "smallint default 0" },
		{ "id", "serial" },
		{ "user_name", "text not null default ''" },
		{ "acct", "text not null" },
		{ "cluster", "text not null" },
		{ "partition", "text not null default ''" },
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
		{ "deleted", "smallint default 0" },
		{ "associd", "int not null" },
		{ "period_start", "bigint not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ NULL, NULL}		
	};

	storage_field_t cluster_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "smallint default 0" },
		{ "name", "text not null" },
		{ "control_host", "text not null" },
		{ "control_port", "int not null" },
		{ NULL, NULL}		
	};

	storage_field_t cluster_usage_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "smallint default 0" },
		{ "cluster", "text not null" },
		{ "period_start", "bigint not null" },
		{ "cpu_count", "bigint default 0" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "down_cpu_secs", "bigint default 0" },
		{ "idle_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ NULL, NULL}		
	};

	storage_field_t event_table_fields[] = {
		{ "node_name", "text default '' not null" },
		{ "cluster", "text not null" },
		{ "cpu_count", "int not null" },
		{ "period_start", "bigint not null" },
		{ "period_end", "bigint default 0 not null" },
		{ "reason", "text not null" },
		{ NULL, NULL}		
	};

	storage_field_t job_table_fields[] = {
		{ "id", "serial" },
		{ "jobid", "integer not null" },
		{ "associd", "bigint not null" },
		{ "wckey", "text not null default ''" },
		{ "uid", "smallint not null" },
		{ "gid", "smallint not null" },
		{ "cluster", "text" },
		{ "partition", "text not null" },
		{ "blockid", "text" },
		{ "account", "text" },
		{ "submit", "bigint not null" },
		{ "eligible", "bigint default 0 not null" },
		{ "start", "bigint default 0 not null" },
		{ "endtime", "bigint default 0 not null" },
		{ "suspended", "bigint default 0 not null" },
		{ "name", "text not null" }, 
		{ "track_steps", "smallint not null" },
		{ "state", "smallint not null" }, 
		{ "comp_code", "int default 0 not null" },
		{ "priority", "bigint not null" },
		{ "req_cpus", "int not null" }, 
		{ "alloc_cpus", "int not null" }, 
		{ "nodelist", "text" },
		{ "kill_requid", "smallint default -1 not null" },
		{ "qos", "smallint default 0" },
		{ NULL, NULL}
	};

	storage_field_t last_ran_table_fields[] = {
		{ "hourly_rollup", "bigint default 0 not null" },
		{ "daily_rollup", "bigint default 0 not null" },
		{ "monthly_rollup", "bigint default 0 not null" },
		{ NULL, NULL}		
	};

	storage_field_t qos_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "smallint default 0" },
		{ "id", "serial" },
		{ "name", "text not null" }, 
		{ "description", "text" }, 
		{ NULL, NULL}		
	};

	storage_field_t step_table_fields[] = {
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
		{ "user_sec", "bigint default 0 not null" },
		{ "user_usec", "bigint default 0 not null" },
		{ "sys_sec", "bigint default 0 not null" },
		{ "sys_usec", "bigint default 0 not null" },
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

	storage_field_t suspend_table_fields[] = {
		{ "id", "int not null" },
		{ "associd", "bigint not null" },
		{ "start", "bigint default 0 not null" },
		{ "endtime", "bigint default 0 not null" },
		{ NULL, NULL}		
	};

	storage_field_t txn_table_fields[] = {
		{ "id", "serial" },
		{ "timestamp", "bigint default 0" },
		{ "action", "smallint not null" },
		{ "name", "text not null" },
		{ "actor", "text not null" },
		{ "info", "text not null" },
		{ NULL, NULL}		
	};

	storage_field_t user_table_fields[] = {
		{ "creation_time", "bigint not null" },
		{ "mod_time", "bigint default 0" },
		{ "deleted", "smallint default 0" },
		{ "name", "text not null" },
		{ "default_acct", "text not null" },
		{ "qos", "smallint default 1 not null" },
		{ "admin_level", "smallint default 1 not null" },
		{ NULL, NULL}		
	};

	int i = 0, job_found = 0;
	int step_found = 0, txn_found = 0, event_found = 0, qos_found = 0;
	int user_found = 0, acct_found = 0, acct_coord_found = 0;
	int cluster_found = 0, cluster_hour_found = 0,
		cluster_day_found = 0, cluster_month_found = 0;
	int assoc_found = 0, assoc_hour_found = 0,
		assoc_day_found = 0, assoc_month_found = 0;
	int suspend_found = 0, last_ran_found = 0;

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
		else if(!event_found &&
			!strcmp(event_table, PQgetvalue(result, i, 0))) 
			event_found = 1;
		else if(!job_found &&
			!strcmp(job_table, PQgetvalue(result, i, 0))) 
			job_found = 1;
		else if(!last_ran_found &&
			!strcmp(last_ran_table, PQgetvalue(result, i, 0))) 
			last_ran_found = 1;
		else if(!qos_found &&
			!strcmp(qos_table, PQgetvalue(result, i, 0))) 
			qos_found = 1;
		else if(!step_found &&
			!strcmp(step_table, PQgetvalue(result, i, 0))) 
			step_found = 1;
		else if(!suspend_found &&
			!strcmp(suspend_table, PQgetvalue(result, i, 0))) 
			suspend_found = 1;
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
					 ", unique (acct, user_name))")
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
					 ", unique (name))") 
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
			   ", unique (associd, period_start))")
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
			   ", unique (associd, period_start))")
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
			   ", unique (associd, period_start))")
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
			   ", unique (user_name, acct, cluster, partition))") 
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
			   ", unique (cluster, period_start))")
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
			   ", unique (cluster, period_start))")
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
			   ", unique (cluster, period_start))")
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
					 ", unique (name))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       cluster_table,
					       cluster_table_fields))
			return SLURM_ERROR;
	}

	if(!event_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 event_table, event_table_fields,
					 ", unique (node_name, "
					 "cluster, period_start))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       event_table,
					       event_table_fields))
			return SLURM_ERROR;
	}

	if(!job_found) {
		if(pgsql_db_create_table(acct_pgsql_db,  
					 job_table, job_table_fields,
					 ", unique (jobid, associd, submit))")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       job_table,
					       job_table_fields))
			return SLURM_ERROR;
	}
	
	if(!last_ran_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 last_ran_table, last_ran_table_fields,
					 ")")
		   == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       last_ran_table,
					       last_ran_table_fields))
			return SLURM_ERROR;
	}

	if(!qos_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 qos_table, qos_table_fields,
					 ", unique (name))")
		   == SLURM_ERROR)
			return SLURM_ERROR;

	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       step_table,
					       step_table_fields))
			return SLURM_ERROR;
	}

	if(!step_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 step_table, step_table_fields,
					 ", unique (id, stepid))")
		   == SLURM_ERROR)
			return SLURM_ERROR;

	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       step_table,
					       step_table_fields))
			return SLURM_ERROR;
	}

	if(!suspend_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 suspend_table, suspend_table_fields,
					 ")")
		   == SLURM_ERROR)
			return SLURM_ERROR;

	} else {
		if(pgsql_db_make_table_current(acct_pgsql_db,  
					       suspend_table,
					       suspend_table_fields))
			return SLURM_ERROR;
	}

	if(!txn_found) {
		if(pgsql_db_create_table(acct_pgsql_db, 
					 txn_table, txn_table_fields,
					 ", unique (id))")
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
					 ", unique (name))")
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
	int rc = SLURM_SUCCESS;
#ifdef HAVE_PGSQL
	PGconn *acct_pgsql_db = NULL;
	char *location = NULL;
#else
	fatal("No Postgres database was found on the machine. "
	      "Please check the config.log from the run of configure "
	      "and run again.");	
#endif
	/* since this can be loaded from many different places
	   only tell us once. */
	if(!first)
		return SLURM_SUCCESS;

	first = 0;

	if(slurmdbd_conf) {
		error("This plugin is not fully compatible with association "
		      "logic.  Please use the mysql plugin for full "
		      "compatiablitly.  If you are interested in filling "
		      "this plugin in please send email to "
		      "slurm-dev@lists.llnl.gov. Job accounting without "
		      "associations will continue to work.");
	} else {
		char *cluster_name = NULL;
		if (!(cluster_name = slurm_get_cluster_name()))
			fatal("%s requires ClusterName in slurm.conf",
			      plugin_name);
		xfree(cluster_name);

		error("This plugin is not fully compatible with association "
		      "logic.  Please use the mysql or slurmdbd/mysql plugin "
		      "for full compatiablitly.  If you are interested in "
		      "filling this plugin in please send email to "
		      "slurm-dev@lists.llnl.gov.  Job accounting without "
		      "associations will continue to work.");
	}

#ifdef HAVE_PGSQL
	pgsql_db_info = _pgsql_acct_create_db_info();		

	location = slurm_get_accounting_storage_loc();
	if(!location)
		pgsql_db_name = xstrdup(DEFAULT_ACCT_DB);
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
		if(location[i]) {
			pgsql_db_name = xstrdup(DEFAULT_ACCT_DB);
			xfree(location);
		} else
			pgsql_db_name = location;
	}

	debug2("pgsql_connect() called for db %s", pgsql_db_name);
		
	pgsql_get_db_connection(&acct_pgsql_db, pgsql_db_name, pgsql_db_info);
		
	rc = _pgsql_acct_check_tables(acct_pgsql_db, pgsql_db_info->user);
	pgsql_close_db_connection(&acct_pgsql_db);
#endif
	/* since this can be loaded from many different places
	   only tell us once. */
	if(rc == SLURM_SUCCESS)
		verbose("%s loaded", plugin_name);
	else 
		verbose("%s failed", plugin_name);
	
	return rc;
}

extern int fini ( void )
{
#ifdef HAVE_PGSQL
	destroy_pgsql_db_info(pgsql_db_info);		
	xfree(pgsql_db_name);
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern void *acct_storage_p_get_connection(bool make_agent, int conn_num,
					   bool rollback)
{
#ifdef HAVE_PGSQL
	PGconn *acct_pgsql_db = NULL;
	
	if(!pgsql_db_info)
		init();
	
	debug2("acct_storage_p_get_connection: request new connection");
	
	pgsql_get_db_connection(&acct_pgsql_db, pgsql_db_name, pgsql_db_info);
	if(acct_pgsql_db)
		errno = SLURM_SUCCESS;
	return (void *)acct_pgsql_db;
#else
	return NULL;
#endif
}

extern int acct_storage_p_close_connection(PGconn **acct_pgsql_db)
{
#ifdef HAVE_PGSQL
	if(acct_pgsql_db && *acct_pgsql_db)
		pgsql_close_db_connection(acct_pgsql_db);
	
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_commit(void *db_conn, bool commit)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(PGconn *acct_pgsql_db, uint32_t uid,
				    List user_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_coord(PGconn *acct_pgsql_db, uint32_t uid,
				    List acct_list, acct_user_cond_t *user_cond)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(PGconn *acct_pgsql_db, uint32_t uid,
				    List acct_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_clusters(PGconn *acct_pgsql_db, uint32_t uid, 
				       List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_associations(PGconn *acct_pgsql_db, uint32_t uid,
					   List association_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_qos(PGconn *acct_pgsql_db, uint32_t uid, 
				  List qos_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_wckeys(PGconn *acct_pgsql_db, uint32_t uid, 
				     List wckey_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_reservation(PGconn *acct_pgsql_db, 
					   acct_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_users(PGconn *acct_pgsql_db, uint32_t uid,
					acct_user_cond_t *user_cond,
					acct_user_rec_t *user)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_accounts(PGconn *acct_pgsql_db, uint32_t uid,
					   acct_account_cond_t *acct_cond,
					   acct_account_rec_t *acct)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_clusters(PGconn *acct_pgsql_db, uint32_t uid,
					   acct_cluster_cond_t *cluster_cond,
					   acct_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_associations(
	PGconn *acct_pgsql_db, uint32_t uid,
	acct_association_cond_t *assoc_cond,
	acct_association_rec_t *assoc)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_qos(PGconn *acct_pgsql_db, uint32_t uid,
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_wckeys(PGconn *acct_pgsql_db, uint32_t uid,
				      acct_wckey_cond_t *wckey_cond,
				      acct_wckey_rec_t *wckey)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_reservation(PGconn *acct_pgsql_db, 
					     acct_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_users(PGconn *acct_pgsql_db, uint32_t uid,
					acct_user_cond_t *user_cond)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_coord(PGconn *acct_pgsql_db, uint32_t uid,
					List acct_list,
					acct_user_cond_t *user_cond)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_accts(PGconn *acct_pgsql_db, uint32_t uid,
					acct_account_cond_t *acct_cond)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_clusters(PGconn *acct_pgsql_db, uint32_t uid,
					   acct_account_cond_t *cluster_cond)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_associations(
	PGconn *acct_pgsql_db, uint32_t uid,
	acct_association_cond_t *assoc_cond)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_qos(void *db_conn, uint32_t uid, 
				      acct_qos_cond_t *qos_cond)
{
	return NULL;
}

extern List acct_storage_p_remove_wckeys(void *db_conn, uint32_t uid, 
				      acct_wckey_cond_t *wckey_cond)
{
	return NULL;
}

extern int acct_storage_p_remove_reservation(PGconn *acct_pgsql_db, 
					     acct_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_get_users(PGconn *acct_pgsql_db, uid_t uid,
				     acct_user_cond_t *user_cond)
{
	return NULL;
}

extern List acct_storage_p_get_accts(PGconn *acct_pgsql_db, uid_t uid,
				     acct_account_cond_t *acct_cond)
{
	return NULL;
}

extern List acct_storage_p_get_clusters(PGconn *acct_pgsql_db, uid_t uid,
					acct_account_cond_t *cluster_cond)
{
	return NULL;
}

extern List acct_storage_p_get_config(void *db_conn)
{
	return NULL;
}

extern List acct_storage_p_get_associations(PGconn *acct_pgsql_db, uid_t uid,
					    acct_association_cond_t *assoc_cond)
{
	return NULL;
}

extern List acct_storage_p_get_qos(void *db_conn, uid_t uid,
				   acct_qos_cond_t *qos_cond)
{
	return NULL;
}

extern List acct_storage_p_get_wckeys(void *db_conn, uid_t uid,
				   acct_wckey_cond_t *wckey_cond)
{
	return NULL;
}

extern List acct_storage_p_get_txn(PGconn *acct_pgsql_db, uid_t uid,
				   acct_txn_cond_t *txn_cond)
{
	return NULL;
}

extern int acct_storage_p_get_usage(PGconn *acct_pgsql_db, uid_t uid,
				    void *in, int type,
				    time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_roll_usage(PGconn *acct_pgsql_db, 
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int clusteracct_storage_p_node_down(PGconn *acct_pgsql_db,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason)
{
#ifdef HAVE_PGSQL
	uint16_t cpus;
	int rc = SLURM_ERROR;
	char *query = NULL;
	char *my_reason;

	if (slurmctld_conf.fast_schedule && !slurmdbd_conf)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

	if (reason)
		my_reason = reason;
	else
		my_reason = node_ptr->reason;
	
	query = xstrdup_printf(
		"update %s set period_end=%d where cluster='%s' "
		"and period_end=0 and node_name='%s'",
		event_table, (event_time-1), cluster, node_ptr->name);
	rc = pgsql_db_query(acct_pgsql_db, query);
	xfree(query);

	debug2("inserting %s(%s) with %u cpus", node_ptr->name, cluster, cpus);

	query = xstrdup_printf(
		"insert into %s "
		"(node_name, cluster, cpu_count, period_start, reason) "
		"values ('%s', '%s', %u, %d, '%s')",
		event_table, node_ptr->name, cluster, 
		cpus, event_time, my_reason);
	rc = pgsql_db_query(acct_pgsql_db, query);
	xfree(query);

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}
extern int clusteracct_storage_p_node_up(PGconn *acct_pgsql_db,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
#ifdef HAVE_PGSQL
	char* query;
	int rc = SLURM_ERROR;

	query = xstrdup_printf(
		"update %s set period_end=%d where cluster='%s' "
		"and period_end=0 and node_name='%s'",
		event_table, (event_time-1), cluster, node_ptr->name);
	rc = pgsql_db_query(acct_pgsql_db, query);
	xfree(query);
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int clusteracct_storage_p_register_ctld(PGconn *acct_pgsql_db,
					       char *cluster,
					       uint16_t port)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_procs(PGconn *acct_pgsql_db,
					       char *cluster,
					       uint32_t procs,
					       time_t event_time)
{
#ifdef HAVE_PGSQL
	static uint32_t last_procs = -1;
	char* query;
	int rc = SLURM_SUCCESS;
	PGresult *result = NULL;
	int got_procs = 0;

	if (procs == last_procs) {
		debug3("we have the same procs as before no need to "
		       "update the database.");
		return SLURM_SUCCESS;
	}
	last_procs = procs;

	/* Record the processor count */
#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("cluster_acct_procs: %s has %u total CPUs at %s", 
	     cluster, procs, tmp_buff);
#endif
	query = xstrdup_printf(
		"select cpu_count from %s where cluster='%s' "
		"and period_end=0 and node_name=''",
		event_table, cluster);
	if(!(result = pgsql_db_query_ret(acct_pgsql_db, query))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	/* we only are checking the first one here */
	if(!PQntuples(result)) {
		debug("We don't have an entry for this machine %s "
		      "most likely a first time running.", cluster);
		goto add_it;
	}
	got_procs = atoi(PQgetvalue(result, 0, 0));
	if(got_procs == procs) {
		debug("%s hasn't changed since last entry", cluster);
		goto end_it;
	}
	debug("%s has changed from %d cpus to %u", cluster, got_procs, procs);

	query = xstrdup_printf(
		"update %s set period_end=%u where cluster='%s' "
		"and period_end=0 and node_name=''",
		event_table, (event_time-1), cluster);
	rc = pgsql_db_query(acct_pgsql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS)
		goto end_it;
add_it:
	query = xstrdup_printf(
		"insert into %s (cluster, cpu_count, period_start, reason) "
		"values ('%s', %u, %d, 'Cluster processor count')",
		event_table, cluster, procs, event_time);
	rc = pgsql_db_query(acct_pgsql_db, query);
	xfree(query);

end_it:
	PQclear(result);
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int clusteracct_storage_p_get_usage(
	void *db_conn, uid_t uid,
	acct_cluster_rec_t *cluster_rec, int type, time_t start, time_t end)
{

	return SLURM_SUCCESS;
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(PGconn *acct_pgsql_db, 
				       char *cluster_name,
				       struct job_record *job_ptr)
{
#ifdef HAVE_PGSQL
	int	rc=SLURM_SUCCESS;
	char	*jname, *nodes;
	long	priority;
	int track_steps = 0;
	char *block_id = NULL;
	char *query = NULL;
	int reinit = 0;

	if (!job_ptr->details || !job_ptr->details->submit_time) {
		error("jobacct_storage_p_job_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(!pgsql_get_db_connection(&acct_pgsql_db,
					    pgsql_db_name, pgsql_db_info))
			return SLURM_ERROR;
	}

	debug3("pgsql_jobacct_job_start() called");
	priority = (job_ptr->priority == NO_VAL) ?
		-1L : (long) job_ptr->priority;

	if (job_ptr->name && job_ptr->name[0]) 
		jname = xstrdup(job_ptr->name);
	else {
		jname = xstrdup("allocation");
		track_steps = 1;
	}

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "None assigned";

	if(job_ptr->batch_flag)
		track_steps = 1;

	if(slurmdbd_conf) {
		block_id = xstrdup(job_ptr->comment);
	} else {
		select_g_get_jobinfo(job_ptr->select_jobinfo, 
				     SELECT_DATA_BLOCK_ID, 
				     &block_id);
	}
	job_ptr->requid = -1; /* force to -1 for sacct to know this
			       * hasn't been set yet */

	if(!job_ptr->db_index) {
		query = xstrdup_printf(
			"insert into %s "
			"(jobid, associd, uid, gid, nodelist, ",
			job_table);

		if(cluster_name) 
			xstrcat(query, "cluster, ");
		if(job_ptr->account) 
			xstrcat(query, "account, ");
		if(job_ptr->partition) 
			xstrcat(query, "partition, ");
		if(block_id) 
			xstrcat(query, "blockid, ");
		if(job_ptr->wckey) 
			xstrcat(query, "wckey, ");
		
		xstrfmtcat(query, 
			   "eligible, submit, start, name, track_steps, "
			   "state, priority, req_cpus, alloc_cpus) "
			   "values (%u, %u, %u, %u, '%s', ",
			   job_ptr->job_id, job_ptr->assoc_id,
			   job_ptr->user_id, job_ptr->group_id, nodes);
		
		if(cluster_name) 
			xstrfmtcat(query, "'%s', ", cluster_name);
		if(job_ptr->account) 
			xstrfmtcat(query, "'%s', ", job_ptr->account);
		if(job_ptr->partition) 
			xstrfmtcat(query, "'%s', ", job_ptr->partition);
		if(block_id) 
			xstrfmtcat(query, "'%s', ", block_id);
		if(job_ptr->wckey) 
			xstrfmtcat(query, "\"%s\", ", job_ptr->wckey);
		
		xstrfmtcat(query, 
			   "%d, %d, %d, '%s', %u, %u, %u, %u, %u)",
			   (int)job_ptr->details->begin_time,
			   (int)job_ptr->details->submit_time,
			   (int)job_ptr->start_time,
			   jname, track_steps,
			   job_ptr->job_state & (~JOB_COMPLETING),
			   priority, job_ptr->num_procs,
			   job_ptr->total_procs);
	try_again:
		if(!(job_ptr->db_index = pgsql_insert_ret_id(acct_pgsql_db,  
							     "job_table_id_seq",
							     query))) {
			if(!reinit) {
				error("It looks like the storage has gone "
				      "away trying to reconnect");
				pgsql_close_db_connection(&acct_pgsql_db);
				pgsql_get_db_connection(&acct_pgsql_db,
							pgsql_db_name,
							pgsql_db_info);
				reinit = 1;
				goto try_again;
			} else
				rc = SLURM_ERROR;
		}
	} else {
		query = xstrdup_printf("update %s set nodelist='%s', ", 
				       job_table, nodes);

		if(job_ptr->account) 
			xstrfmtcat(query, "account='%s', ",
				   job_ptr->account);
		if(job_ptr->partition) 
			xstrfmtcat(query, "partition='%s', ",
				   job_ptr->partition);
		if(block_id)
			xstrfmtcat(query, "blockid='%s', ", block_id);
		if(job_ptr->wckey) 
			xstrfmtcat(query, ", wckey=\"%s\"", job_ptr->wckey);

		xstrfmtcat(query, "start=%d, name='%s', state=%u, "
			   "alloc_cpus=%u, associd=%d where id=%d",
			   (int)job_ptr->start_time,
			   jname, job_ptr->job_state & (~JOB_COMPLETING),
			   job_ptr->total_procs, job_ptr->assoc_id,
			   job_ptr->db_index);
		rc = pgsql_db_query(acct_pgsql_db, query);
	}
	xfree(block_id);
	xfree(jname);
	xfree(query);
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(PGconn *acct_pgsql_db,
					  struct job_record *job_ptr)
{
#ifdef HAVE_PGSQL
	char *query = NULL, *nodes = NULL;
	int rc=SLURM_SUCCESS;
	
	if (!job_ptr->db_index 
	    && (!job_ptr->details || !job_ptr->details->submit_time)) {
		error("jobacct_storage_p_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(!pgsql_get_db_connection(&acct_pgsql_db,
					    pgsql_db_name, pgsql_db_info))
			return SLURM_ERROR;
	}
	
	debug3("pgsql_jobacct_job_complete() called");
	if (job_ptr->end_time == 0) {
		debug("pgsql_jobacct: job %u never started", job_ptr->job_id);
		return SLURM_ERROR;
	}	
	
	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "None assigned";
	
	if(!job_ptr->db_index) {
		if(!(job_ptr->db_index =
		     _get_db_index(acct_pgsql_db,
				   job_ptr->details->submit_time,
				   job_ptr->job_id,
				   job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(jobacct_storage_p_job_start(
				   acct_pgsql_db, NULL, job_ptr)
			   == SLURM_ERROR) {
				error("couldn't add job %u at job completion",
				      job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}
	query = xstrdup_printf("update %s set start=%d, endtime=%d, state=%d, "
			       "nodelist='%s', comp_code=%d, "
			       "kill_requid=%d where id=%d",
			       job_table, (int)job_ptr->start_time,
			       (int)job_ptr->end_time, 
			       job_ptr->job_state & (~JOB_COMPLETING),
			       nodes, job_ptr->exit_code,
			       job_ptr->requid, job_ptr->db_index);
	rc = pgsql_db_query(acct_pgsql_db, query);
	xfree(query);

	return  rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(PGconn *acct_pgsql_db,
					struct step_record *step_ptr)
{
#ifdef HAVE_PGSQL
	int cpus = 0;
	int rc=SLURM_SUCCESS;
	char node_list[BUFFER_SIZE];
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	char *query = NULL;
	
	if (!step_ptr->job_ptr->db_index 
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("jobacct_storage_p_step_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(!pgsql_get_db_connection(&acct_pgsql_db,
					    pgsql_db_name, pgsql_db_info))
			return SLURM_ERROR;
	}

	if(slurmdbd_conf) {
		cpus = step_ptr->job_ptr->total_procs;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
	} else {
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
	}

	step_ptr->job_ptr->requid = -1; /* force to -1 for sacct to know this
					 * hasn't been set yet  */

	if(!step_ptr->job_ptr->db_index) {
		step_ptr->job_ptr->db_index = 
			_get_db_index(acct_pgsql_db,
				      step_ptr->job_ptr->details->submit_time,
				      step_ptr->job_ptr->job_id,
				      step_ptr->job_ptr->assoc_id);
		if(step_ptr->job_ptr->db_index == -1) 
			return SLURM_ERROR;
	}
	/* we want to print a -1 for the requid so leave it a
	   %d */
	query = xstrdup_printf(
		"insert into %s (id, stepid, start, end, name, state, "
		"cpus, nodelist) "
		"values (%d, %u, %u, 0, '%s', %d, %u, '%s')",
		step_table, step_ptr->job_ptr->db_index,
		step_ptr->step_id, 
		(int)step_ptr->start_time, step_ptr->name,
		JOB_RUNNING, cpus, node_list);
	rc = pgsql_db_query(acct_pgsql_db, query);
	xfree(query);

	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(PGconn *acct_pgsql_db,
					   struct step_record *step_ptr)
{
#ifdef HAVE_PGSQL
	time_t now;
	int elapsed;
	int comp_status;
	int cpus = 0;
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	struct jobacctinfo dummy_jobacct;
	float ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	float ave_cpu = 0, ave_cpu2 = 0;
	char *query = NULL;
	int rc =SLURM_SUCCESS;
	uint32_t exit_code;

	if (!step_ptr->job_ptr->db_index 
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("jobacct_storage_p_step_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(!pgsql_get_db_connection(&acct_pgsql_db,
					    pgsql_db_name, pgsql_db_info))
			return SLURM_ERROR;
	}
	
	if (jobacct == NULL) {
		/* JobAcctGather=jobacct_gather/none, no data to process */
		bzero(&dummy_jobacct, sizeof(dummy_jobacct));
		jobacct = &dummy_jobacct;
	}

	if(slurmdbd_conf) {
		now = step_ptr->job_ptr->end_time;
		cpus = step_ptr->job_ptr->total_procs;

	} else {
		now = time(NULL);
#ifdef HAVE_BG
		cpus = step_ptr->job_ptr->num_procs;
		
#else
		if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
			cpus = step_ptr->job_ptr->total_procs;
		else 
			cpus = step_ptr->step_layout->task_cnt;
#endif
	}

	if ((elapsed=now-step_ptr->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */

	exit_code = step_ptr->exit_code;
	if (exit_code == NO_VAL) {
		comp_status = JOB_CANCELLED;
		exit_code = 0;
	} else if (exit_code)
		comp_status = JOB_FAILED;
	else
		comp_status = JOB_COMPLETE;

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

	if(!step_ptr->job_ptr->db_index) {
		step_ptr->job_ptr->db_index = 
			_get_db_index(acct_pgsql_db,
				      step_ptr->job_ptr->details->submit_time,
				      step_ptr->job_ptr->job_id,
				      step_ptr->job_ptr->assoc_id);
		if(step_ptr->job_ptr->db_index == -1) 
			return SLURM_ERROR;
	}

	query = xstrdup_printf(
		"update %s set endtime=%u, state=%d, "
		"kill_requid=%u, comp_code=%u, "
		"user_sec=%ld, user_usec=%ld, "
		"sys_sec=%ld, sys_usec=%ld, "
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
		exit_code, 
		/* user seconds */
		jobacct->user_cpu_sec,	
		/* user microseconds */
		jobacct->user_cpu_usec,
		/* system seconds */
		jobacct->sys_cpu_sec,
		/* system microsecs */
		jobacct->sys_cpu_usec,
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
	xfree(query);
		 
	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(PGconn *acct_pgsql_db,
				     struct job_record *job_ptr)
{
#ifdef HAVE_PGSQL
	char query[1024];
	int rc = SLURM_SUCCESS;
	
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(!pgsql_get_db_connection(&acct_pgsql_db,
					    pgsql_db_name, pgsql_db_info))
			return SLURM_ERROR;
	}
	
	if(!job_ptr->db_index) {
		job_ptr->db_index = _get_db_index(acct_pgsql_db,
						  job_ptr->details->submit_time,
						  job_ptr->job_id,
						  job_ptr->assoc_id);
		if(job_ptr->db_index == -1) 
			return SLURM_ERROR;
	}

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
extern List jobacct_storage_p_get_jobs_cond(PGconn *acct_pgsql_db, uid_t uid, 
					    acct_job_cond_t *job_cond)
{
	List job_list = NULL;
#ifdef HAVE_PGSQL
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(!pgsql_get_db_connection(&acct_pgsql_db,
					    pgsql_db_name, pgsql_db_info))
			return job_list;
	}

	job_list = pgsql_jobacct_process_get_jobs(acct_pgsql_db, job_cond);	
#endif
	return job_list;
}

/* 
 * expire old info from the storage 
 */
extern int jobacct_storage_p_archive(PGconn *acct_pgsql_db,
				      acct_archive_cond_t *arch_cond)
{
#ifdef HAVE_PGSQL
	if(!acct_pgsql_db || PQstatus(acct_pgsql_db) != CONNECTION_OK) {
		if(!pgsql_get_db_connection(&acct_pgsql_db,
					    pgsql_db_name, pgsql_db_info))
			return SLURM_ERROR;
	}

	return pgsql_jobacct_process_archive(acct_pgsql_db, arch_cond);
#endif
	return SLURM_ERROR;
}

/* 
 * load old info into the storage 
 */
extern int jobacct_storage_p_archive_load(void *db_conn, 
					  acct_archive_rec_t *arch_rec)
{
	return SLURM_SUCCESS;
}


extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used)
{
	/* This definitely needs to be fleshed out.
	 * Go through the list of shares_used_object_t objects and store them */
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(
	void *db_conn, char *cluster, time_t event_time)
{
	/* put end times for a clean start */




	return SLURM_SUCCESS;
}
