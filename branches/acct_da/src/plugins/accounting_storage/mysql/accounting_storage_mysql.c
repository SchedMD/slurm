/*****************************************************************************\
 *  accounting_storage_mysql.c - accounting interface to mysql.
 *
 *  $Id: accounting_storage_mysql.c 13061 2008-01-22 21:23:56Z da $
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
#include "mysql_jobacct_process.h"

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
const char plugin_name[] = "Accounting storage MYSQL plugin";
const char plugin_type[] = "accounting_storage/mysql";
const uint32_t plugin_version = 100;

#ifdef HAVE_MYSQL

#define DEFAULT_JOBACCT_DB "slurm_acct_db"

MYSQL *acct_mysql_db = NULL;

char *job_index = "job_index_table";
char *job_table = "job_table";
char *step_table = "step_table";
char *rusage_table = "rusage_table";
char *user_table = "user_table";
char *acct_table = "acct_table";
char *acct_coord_table = "acct_coord_table";
char *cluster_table = "cluster_table";
char *cluster_hour_table = "cluster_hour_usage_table";
char *cluster_day_table = "cluster_day_usage_table";
char *cluster_month_table = "cluster_month_usage_table";
char *assoc_table = "assoc_table";
char *assoc_hour_table = "assoc_hour_usage_table";
char *assoc_day_table = "assoc_day_usage_table";
char *assoc_month_table = "assoc_month_usage_table";

static mysql_db_info_t *_mysql_acct_create_db_info()
{
	mysql_db_info_t *db_info = xmalloc(sizeof(mysql_db_info_t));
	db_info->port = slurm_get_accounting_storage_port();
	if(!db_info->port) 
		db_info->port = 3306;
	db_info->host = slurm_get_accounting_storage_host();	
	db_info->user = slurm_get_accounting_storage_user();	
	db_info->pass = slurm_get_accounting_storage_pass();	
	return db_info;
}

static int _mysql_acct_check_tables()
{
	storage_field_t user_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "deleted", "bool default 0" },
		{ "name", "tinytext not null" },
		{ "default_acct", "tinytext not null" },
		{ "expedite", "smallint default 1 not null" },
		{ "admin_level", "smallint default 1 not null" },
		{ NULL, NULL}		
	};

	storage_field_t acct_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "description", "text not null" },
		{ "organization", "text not null" },
		{ "expedite", "smallint default 1 not null" },
		{ NULL, NULL}		
	};

	storage_field_t acct_coord_table_fields[] = {
		{ "deleted", "tinyint default 0" },
		{ "acct", "tinytext not null" },
		{ "name", "tinytext not null" },
		{ NULL, NULL}		
	};

	storage_field_t cluster_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "primary", "tinytext not null" },
		{ "backup", "tinytext not null" },
		{ NULL, NULL}		
	};

	storage_field_t cluster_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "deleted", "tinyint default 0" },
		{ "cluster", "tinytext not null" },
		{ "period_start", "int unsigned not null" },
		{ "cpu_count", "int unsigned default 0" },
		{ "alloc_cpu_secs", "int unsigned default 0" },
		{ "down_cpu_secs", "int unsigned default 0" },
		{ "idle_cpu_secs", "int unsigned default 0" },
		{ "resv_cpu_secs", "int unsigned default 0" },
		{ NULL, NULL}		
	};

	storage_field_t assoc_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "userid", "mediumint unsigned not null" },
		{ "user", "tinytext not null" },
		{ "acct", "tinytext not null" },
		{ "cluster", "tinytext not null" },
		{ "partition", "tinytext not null" },
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
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "deleted", "tinyint default 0" },
		{ "assoc_id", "tinytext not null" },
		{ "period_start", "int unsigned not null" },
		{ "cpu_count", "int unsigned default 0" },
		{ "alloc_cpu_secs", "int unsigned default 0" },
		{ NULL, NULL}		
	};

	storage_field_t job_index_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "id", "int not null auto_increment" },
		{ "jobid", "mediumint unsigned not null" },
		{ "associd", "mediumint unsigned not null" },
		{ "partition", "tinytext not null" },
		{ "submit", "int unsigned not null" },
		{ "uid", "smallint unsigned not null" },
		{ "gid", "smallint unsigned not null" },
		{ "blockid", "tinytext" },
		{ NULL, NULL}		
	};

	storage_field_t job_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "mod_time", "int unsigned not null" },
		{ "id", "int not null" },
		{ "eligible", "int unsigned default 0 not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "name", "tinytext not null" }, 
		{ "track_steps", "tinyint not null" },
		{ "state", "smallint not null" }, 
		{ "comp_code", "int default 0 not null" },
		{ "priority", "int unsigned not null" },
		{ "cpus", "mediumint unsigned not null" }, 
		{ "nodelist", "text" },
		{ "kill_requid", "smallint default -1 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "id", "int not null" },
		{ "stepid", "smallint not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "name", "text not null" },
		{ "nodelist", "text not null" },
		{ "state", "smallint not null" },
		{ "kill_requid", "smallint default -1 not null" },
		{ "comp_code", "int default 0 not null" },
		{ "cpus", "mediumint unsigned not null" },
		{ "max_vsize", "mediumint unsigned default 0 not null" },
		{ "max_vsize_task", "smallint unsigned default 0 not null" },
		{ "max_vsize_node", "mediumint unsigned default 0 not null" },
		{ "ave_vsize", "float default 0.0 not null" },
		{ "max_rss", "mediumint unsigned default 0 not null" },
		{ "max_rss_task", "smallint unsigned default 0 not null" },
		{ "max_rss_node", "mediumint unsigned default 0 not null" },
		{ "ave_rss", "float default 0.0 not null" },
		{ "max_pages", "mediumint unsigned default 0 not null" },
		{ "max_pages_task", "smallint unsigned default 0 not null" },
		{ "max_pages_node", "mediumint unsigned default 0 not null" },
		{ "ave_pages", "float default 0.0 not null" },
		{ "min_cpu", "mediumint unsigned default 0 not null" },
		{ "min_cpu_task", "smallint unsigned default 0 not null" },
		{ "min_cpu_node", "mediumint unsigned default 0 not null" },
		{ "ave_cpu", "float default 0.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_rusage_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default unix_timestamp()" },
		{ "id", "int not null" },
		{ "stepid", "smallint not null" },
		{ "cpu_sec", "int unsigned default 0 not null" },
		{ "cpu_usec", "int unsigned default 0 not null" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "max_rss", "int unsigned default 0 not null" },
		{ "max_ixrss", "int unsigned default 0 not null" },
		{ "max_idrss", "int unsigned default 0 not null" },
		{ "max_isrss", "int unsigned default 0 not null" },
		{ "max_minflt", "int unsigned default 0 not null" },
		{ "max_majflt", "int unsigned default 0 not null" },
		{ "max_nswap", "int unsigned default 0 not null" },
		{ "inblock", "int unsigned default 0 not null" },
		{ "outblock", "int unsigned default 0 not null" },
		{ "msgsnd", "int unsigned default 0 not null" },
		{ "msgrcv", "int unsigned default 0 not null" },
		{ "nsignals", "int unsigned default 0 not null" },
		{ "nvcsw", "int unsigned default 0 not null" },
		{ "nivcsw", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	if(mysql_db_create_table(acct_mysql_db, user_table, user_table_fields,
				 ", primary key (name))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, acct_table, acct_table_fields,
				 ", primary key (name))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, acct_coord_table,
				 acct_coord_table_fields,
				 ", primary key (acct))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, cluster_table,
				 cluster_table_fields,
				 ", primary key (name))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, cluster_hour_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, cluster_day_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, cluster_month_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, assoc_table, assoc_table_fields,
				 ", primary key (id))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, assoc_hour_table,
				 assoc_usage_table_fields,
				 ", primary key (assoc_id))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, assoc_day_table,
				 assoc_usage_table_fields,
				 ", primary key (assoc_id))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, assoc_month_table,
				 assoc_usage_table_fields,
				 ", primary key (assoc_id))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, job_index, job_index_fields,
				 ", primary key (id))") == SLURM_ERROR)
		return SLURM_ERROR;
	
	if(mysql_db_create_table(acct_mysql_db, job_table, job_table_fields,
				 ")") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, step_table,
				 step_table_fields, ")") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, rusage_table,
				 step_rusage_fields, ")") == SLURM_ERROR)
		return SLURM_ERROR;


	return SLURM_SUCCESS;
}
#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
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
