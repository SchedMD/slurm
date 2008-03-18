/*****************************************************************************\
 *  accounting_storage_mysql.c - accounting interface to mysql.
 *
 *  $Id: accounting_storage_mysql.c 13061 2008-01-22 21:23:56Z da $
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

#include "mysql_jobacct_process.h"
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
 * the plugin (e.g., "accounting_storage" for SLURM job completion
 * logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "accounting_storage/".
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

#ifndef HAVE_MYSQL
typedef void MYSQL;
#else

static mysql_db_info_t *mysql_db_info = NULL;
static char *mysql_db_name = NULL;

#define DEFAULT_ACCT_DB "slurm_acct_db"

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
char *step_table = "step_table";
char *txn_table = "txn_table";
char *user_table = "user_table";

extern int acct_storage_p_add_associations(MYSQL *acct_mysql_db, uint32_t uid, 
					   List association_list);
extern List acct_storage_p_get_associations(MYSQL *acct_mysql_db, 
					    acct_association_cond_t *assoc_q);

static int _get_db_index(MYSQL *acct_mysql_db, 
			 time_t submit, uint32_t jobid, uint32_t associd)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int db_index = -1;
	char *query = xstrdup_printf("select id from %s where "
				     "submit=%u and jobid=%u and associd=%u",
				     job_table, (int)submit, jobid, associd);

	if(!(result = mysql_db_query_ret(acct_mysql_db, query))) {
		xfree(query);
		return -1;
	}
	xfree(query);

	row = mysql_fetch_row(result);
	if(!row) {
		mysql_free_result(result);
		error("We can't get a db_index for this combo, "
		      "submit=%u and jobid=%u and associd=%u.",
		      (int)submit, jobid, associd);
		return -1;
	}
	db_index = atoi(row[0]);
	mysql_free_result(result);
	
	return db_index;
}

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

static int _mysql_acct_check_tables(MYSQL *acct_mysql_db)
{
	storage_field_t acct_coord_table_fields[] = {
		{ "deleted", "tinyint default 0" },
		{ "acct", "tinytext not null" },
		{ "user", "tinytext not null" },
		{ NULL, NULL}		
	};

	storage_field_t acct_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "description", "text not null" },
		{ "organization", "text not null" },
		{ "qos", "smallint default 1 not null" },
		{ NULL, NULL}		
	};

	storage_field_t assoc_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "user", "tinytext not null default ''" },
		{ "acct", "tinytext not null" },
		{ "cluster", "tinytext not null" },
		{ "partition", "tinytext not null default ''" },
		{ "parent_acct", "tinytext not null" },
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
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "associd", "int not null" },
		{ "period_start", "int unsigned not null" },
		{ "cpu_count", "int unsigned default 0" },
		{ "alloc_cpu_secs", "int unsigned default 0" },
		{ NULL, NULL}		
	};

	storage_field_t cluster_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "control_host", "tinytext not null" },
		{ "control_port", "mediumint not null" },
		{ NULL, NULL}		
	};

	storage_field_t cluster_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
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

	storage_field_t event_table_fields[] = {
		{ "node_name", "tinytext default '' not null" },
		{ "cluster", "tinytext not null" },
		{ "cpu_count", "int not null" },
		{ "period_start", "int unsigned not null" },
		{ "period_end", "int unsigned default 0 not null" },
		{ "reason", "tinytext not null" },
		{ NULL, NULL}		
	};

	storage_field_t job_table_fields[] = {
		{ "id", "int not null auto_increment" },
		{ "jobid", "mediumint unsigned not null" },
		{ "associd", "mediumint unsigned not null" },
		{ "gid", "smallint unsigned not null" },
		{ "partition", "tinytext not null" },
		{ "blockid", "tinytext" },
		{ "eligible", "int unsigned default 0 not null" },
		{ "submit", "int unsigned default 0 not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "name", "tinytext not null" }, 
		{ "track_steps", "tinyint not null" },
		{ "state", "smallint not null" }, 
		{ "comp_code", "int default 0 not null" },
		{ "priority", "int unsigned not null" },
		{ "req_cpus", "mediumint unsigned not null" }, 
		{ "alloc_cpus", "mediumint unsigned not null" }, 
		{ "nodelist", "text" },
		{ "kill_requid", "smallint default -1 not null" },
		{ "qos", "smallint default 0" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields[] = {
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
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
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

	storage_field_t txn_table_fields[] = {
		{ "id", "int not null auto_increment" },
		{ "timestamp", "int unsigned default 0 not null" },
		{ "action", "smallint not null" },
		{ "name", "tinytext not null" },
		{ "actor", "tinytext not null" },
		{ "info", "text not null" },
		{ NULL, NULL}		
	};

	storage_field_t user_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "bool default 0" },
		{ "name", "tinytext not null" },
		{ "default_acct", "tinytext not null" },
		{ "qos", "smallint default 1 not null" },
		{ "admin_level", "smallint default 1 not null" },
		{ NULL, NULL}		
	};

	if(mysql_db_create_table(acct_mysql_db, acct_coord_table,
				 acct_coord_table_fields,
				 ", primary key (acct(20), user(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, acct_table, acct_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, assoc_day_table,
				 assoc_usage_table_fields,
				 ", primary key (associd, period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, assoc_hour_table,
				 assoc_usage_table_fields,
				 ", primary key (associd, period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, assoc_month_table,
				 assoc_usage_table_fields,
				 ", primary key (associd, period_start))") 
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, assoc_table, assoc_table_fields,
				 ", primary key (id), "
				 " unique index (user(20), acct(20), "
				 "cluster(20), partition(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, cluster_day_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster(20), period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, cluster_hour_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster(20), period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, cluster_month_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster(20), period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, cluster_table,
				 cluster_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, event_table,
				 event_table_fields,
				 ", primary key (node_name(20), cluster(20), "
				 "period_start))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, job_table, job_table_fields,
				 ", primary key (id), "
				 "unique index (jobid, associd, submit))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, step_table,
				 step_table_fields, 
				 ", primary key (id, stepid))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, txn_table, txn_table_fields,
				 ", primary key (id))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(acct_mysql_db, user_table, user_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
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
	static int first = 1;
	int rc = SLURM_SUCCESS;
#ifdef HAVE_MYSQL
	MYSQL *acct_mysql_db = NULL;
	char *location = NULL;
#else
	fatal("No MySQL database was found on the machine. "
	      "Please check the configure log and run again.");
#endif
	/* since this can be loaded from many different places
	   only tell us once. */
	if(!first)
		return SLURM_SUCCESS;

	first = 0;

#ifdef HAVE_MYSQL
	mysql_db_info = _mysql_acct_create_db_info();

	location = slurm_get_accounting_storage_loc();
	if(!location)
		mysql_db_name = xstrdup(DEFAULT_ACCT_DB);
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
			mysql_db_name = xstrdup(DEFAULT_ACCT_DB);
		else
			mysql_db_name = location;
	}

	debug2("mysql_connect() called for db %s", mysql_db_name);
	
	mysql_get_db_connection(&acct_mysql_db, mysql_db_name, mysql_db_info);
		
	rc = _mysql_acct_check_tables(acct_mysql_db);

	mysql_close(acct_mysql_db);
	acct_mysql_db = NULL;
#endif		

	if(rc == SLURM_SUCCESS)
		verbose("%s loaded", plugin_name);
	else 
		verbose("%s failed", plugin_name);
	
	return rc;
}

extern int fini ( void )
{
#ifdef HAVE_MYSQL
	destroy_mysql_db_info(mysql_db_info);		
	xfree(mysql_db_name);

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern void *acct_storage_p_get_connection()
{
#ifdef HAVE_MYSQL
	MYSQL *acct_mysql_db = NULL;

	if(!mysql_db_info)
		init();

	debug2("acct_storage_p_get_connection: request new connection");
	
	mysql_get_db_connection(&acct_mysql_db, mysql_db_name, mysql_db_info);

	return (void *)acct_mysql_db;
#else
	return NULL;
#endif
}

extern int acct_storage_p_close_connection(MYSQL *acct_mysql_db)
{
#ifdef HAVE_MYSQL
	if (acct_mysql_db) {
		mysql_close(acct_mysql_db);
		acct_mysql_db = NULL;
	}	
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_add_users(MYSQL *acct_mysql_db, uint32_t uid,
				    List user_list)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_user_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *query = NULL;
	struct passwd *pw = NULL;
	time_t now = time(NULL);
	char *user = NULL;
	char *extra = NULL;

	if((pw=getpwuid(uid))) {
		user = pw->pw_name;
	}

	itr = list_iterator_create(user_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->default_acct) {
			error("We need a user name and "
			      "default acct to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name, default_acct");
		xstrfmtcat(vals, "%d, %d, '%s', '%s'", 
			   now, now, object->name, object->default_acct); 
		xstrfmtcat(extra, ", default_acct='%s'", object->default_acct);
		if(object->qos != ACCT_QOS_NOTSET) {
			xstrcat(cols, ", qos");
			xstrfmtcat(vals, ", %u", object->qos); 		
			xstrfmtcat(extra, ", qos=%u", object->qos); 		
		}

		if(object->admin_level != ACCT_ADMIN_NOTSET) {
			xstrcat(cols, ", admin_level");
			xstrfmtcat(vals, ", %u", object->admin_level);
		}

		query = xstrdup_printf(
			"insert into %s (%s) values (%s)"
			"on duplicate key update deleted=0, mod_time=%d %s;",
			user_table, cols, vals,
			now, extra);

		xstrfmtcat(query, 	
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %d, '%s', '%s', \"%s\");",
			   txn_table,
			   now, DBD_ADD_USERS, object->name, user, extra);
		xfree(cols);
		xfree(vals);
		xfree(extra);
		
		rc = mysql_db_query(acct_mysql_db, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add user %s", object->name);
			continue;
		}
		
		if(acct_storage_p_add_associations(
			   acct_mysql_db, uid, object->assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding user associations");
			rc = SLURM_ERROR;
		}
	}
	list_iterator_destroy(itr);
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_add_coord(MYSQL *acct_mysql_db, uint32_t uid, 
				    char *acct, acct_user_cond_t *user_q)
{
#ifdef HAVE_MYSQL
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_add_accts(MYSQL *acct_mysql_db, uint32_t uid, 
				    List acct_list)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_account_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *query = NULL;
	struct passwd *pw = NULL;
	time_t now = time(NULL);
	char *user = NULL;
	char *extra = NULL;

	if((pw=getpwuid(uid))) {
		user = pw->pw_name;
	}

	itr = list_iterator_create(acct_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->description
		   || !object->organization) {
			error("We need a acct name, description, and "
			      "organization to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name, "
			"description, organization");
		xstrfmtcat(vals, "%d, %d, '%s', '%s', '%s'", 
			   now, now, object->name, 
			   object->description, object->organization); 
		xstrfmtcat(extra, ", description='%s', organization='%s'",
			   object->description, object->organization); 		
		
		if(object->qos != ACCT_QOS_NOTSET) {
			xstrcat(cols, ", qos");
			xstrfmtcat(vals, ", %u", object->qos); 		
			xstrfmtcat(extra, ", qos=%u", object->qos); 		
		}

		query = xstrdup_printf(
			"insert into %s (%s) values (%s)"
			"on duplicate key update deleted=0, mod_time=%d %s;",
			acct_table, cols, vals,
			now, extra);

		xstrfmtcat(query, 	
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %d, '%s', '%s', \"%s\");",
			   txn_table,
			   now, DBD_ADD_ACCOUNTS, object->name, user, extra);
		xfree(cols);
		xfree(vals);
		xfree(extra);
		
		rc = mysql_db_query(acct_mysql_db, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add acct %s", object->name);
			continue;
		}

		if(acct_storage_p_add_associations(
			   acct_mysql_db, uid, object->assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding acct associations");
			rc = SLURM_ERROR;
		}
	}
	list_iterator_destroy(itr);
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_add_clusters(MYSQL *acct_mysql_db, uint32_t uid, 
				       List cluster_list)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_cluster_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user = NULL;

	if((pw=getpwuid(uid))) {
		user = pw->pw_name;
	}

	itr = list_iterator_create(cluster_list);
	while((object = list_next(itr))) {
		if(!object->name) {
			error("We need a cluster name to add.");
			rc = SLURM_ERROR;
			continue;
		}

		xstrcat(cols, "creation_time, mod_time, acct, cluster");
		xstrfmtcat(vals, "%d, %d, 'root', '%s'",
			   now, now, object->name);

		if(object->default_fairshare) {
			xstrcat(cols, ", fairshare");
			xstrfmtcat(vals, ", %u", object->default_fairshare);
			xstrfmtcat(extra, ", fairshare=%u",
				   object->default_fairshare);
		}

		if(object->default_max_jobs) {
			xstrcat(cols, ", max_jobs");
			xstrfmtcat(vals, ", %u", object->default_max_jobs);
			xstrfmtcat(extra, ", max_jobs=%u",
				   object->default_max_jobs);
		}

		if(object->default_max_nodes_per_job) {
			xstrcat(cols, ", max_nodes_per_job");
			xstrfmtcat(vals, ", %u", 
				   object->default_max_nodes_per_job);
			xstrfmtcat(extra, ", max_nodes_per_job=%u",
				   object->default_max_nodes_per_job);
		}

		if(object->default_max_wall_duration_per_job) {
			xstrcat(cols, ", max_wall_duration_per_job");
			xstrfmtcat(vals, ", %u",
				   object->default_max_wall_duration_per_job);
			xstrfmtcat(extra, ", max_wall_duration_per_job=%u",
				   object->default_max_wall_duration_per_job);
		}

		if(object->default_max_cpu_secs_per_job) {
			xstrcat(cols, ", max_cpu_seconds_per_job");
			xstrfmtcat(vals, ", %u",
				   object->default_max_cpu_secs_per_job);
			xstrfmtcat(extra, ", max_cpu_seconds_per_job=%u",
				   object->default_max_cpu_secs_per_job);
		}
		
		xstrfmtcat(query, 
			   "insert into %s (creation_time, mod_time, name) "
			   "values (%d, %d, '%s') "
			   "on duplicate key update deleted=0, mod_time=%d;",
			   cluster_table, 
			   now, now, object->name,
			   now);

		xstrfmtcat(query, 	
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %d, '%s', '%s', \"%s\");",
			   txn_table,
			   now, DBD_ADD_CLUSTERS, object->name, user, extra);
			
		xstrfmtcat(query,
			   "SELECT @MyMax := coalesce(max(rgt), 0) FROM %s;"
			   "insert into %s (%s, lft, rgt) "
			   "values (%s, @MyMax+1, @MyMax+2) "
			   "on duplicate key update deleted=0, mod_time=%d",
			   assoc_table, 
			   assoc_table, cols, 
			   vals,
			   now);

		xfree(cols);
		xfree(vals);

		if(extra) {
			xstrfmtcat(query, " %s;", extra);
			xfree(extra);
		} else {
			xstrcat(query, ";");
		}

		//info("query is %s", query);
		rc = mysql_db_query(acct_mysql_db, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add root assoc for cluster %s",
			      object->name);
			rc = SLURM_ERROR;
			continue;
		}
	}
	list_iterator_destroy(itr);

	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_add_associations(MYSQL *acct_mysql_db, uint32_t uid, 
					   List association_list)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_association_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *extra = NULL, *query = NULL;
	char *parent = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user = NULL;
	char *assoc_name = NULL;

	if((pw=getpwuid(uid))) {
		user = pw->pw_name;
	}

	itr = list_iterator_create(association_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->acct) {
			error("We need a association cluster and "
			      "acct to add one.");
			rc = SLURM_ERROR;
			continue;
		}

		if(object->parent_acct) {
			parent = object->parent_acct;
		} else if(object->user) {
			parent = object->acct;
		} else {
			parent = "root";
		}

		xstrcat(cols, "creation_time, mod_time, cluster, acct");
		xstrfmtcat(vals, "%d, %d, '%s', '%s'", 
			   now, now, object->cluster, object->acct); 
		xstrfmtcat(extra, ", mod_time=%d", now);
		if(!object->user) {
			xstrcat(cols, ", parent_acct");
			xstrfmtcat(vals, ", '%s'", parent);
			xstrfmtcat(extra, ", parent_acct='%s'", parent);
			xstrfmtcat(assoc_name, "%s of %s on %s",
				   object->acct, parent, object->cluster);
		}
		
		if(object->user) {
			xstrcat(cols, ", user");
			xstrfmtcat(vals, ", '%s'", object->user); 		
			xstrfmtcat(extra, ", user='%s'", object->user);
			xstrfmtcat(assoc_name, "%s on %s for %s",
				   object->acct, object->cluster, object->user);
			
			if(object->partition) {
				xstrcat(cols, ", partition");
				xstrfmtcat(vals, ", '%s'", object->partition);
				xstrfmtcat(extra, ", partition'%s'",
					   object->partition);
				xstrfmtcat(assoc_name, " in %s",
					   object->partition);
			}
		}

		if((int)object->fairshare >= 0) {
			xstrcat(cols, ", fairshare");
			xstrfmtcat(vals, ", %d", object->fairshare);
			xstrfmtcat(extra, ", fairshare=%d",
				   object->fairshare);
		}

		if((int)object->max_jobs >= 0) {
			xstrcat(cols, ", max_jobs");
			xstrfmtcat(vals, ", %d", object->max_jobs);
			xstrfmtcat(extra, ", max_jobs=%d",
				   object->max_jobs);
		}

		if((int)object->max_nodes_per_job >= 0) {
			xstrcat(cols, ", max_nodes_per_job");
			xstrfmtcat(vals, ", %d", object->max_nodes_per_job);
			xstrfmtcat(extra, ", max_nodes_per_job=%d",
				   object->max_nodes_per_job);
		}

		if((int)object->max_wall_duration_per_job >= 0) {
			xstrcat(cols, ", max_wall_duration_per_job");
			xstrfmtcat(vals, ", %d",
				   object->max_wall_duration_per_job);
			xstrfmtcat(extra, ", max_wall_duration_per_job=%d",
				   object->max_wall_duration_per_job);
		}

		if((int)object->max_cpu_secs_per_job >= 0) {
			xstrcat(cols, ", max_cpu_seconds_per_job");
			xstrfmtcat(vals, ", %d", object->max_cpu_secs_per_job);
			xstrfmtcat(extra, ", max_cpu_seconds_per_job=%d",
				   object->max_cpu_secs_per_job);
		}

		xstrfmtcat(query,
			   "LOCK TABLE %s WRITE;"
			   "SELECT @myLeft := lft FROM %s WHERE acct = '%s' "
			   "and cluster = '%s' and user = '';",
			   assoc_table,
			   assoc_table, parent, object->cluster);
		xstrfmtcat(query,
			   "UPDATE %s SET rgt = rgt+2 WHERE rgt > @myLeft;"
			   "UPDATE %s SET lft = lft+2 WHERE lft > @myLeft;",
			   assoc_table, assoc_table);
		
		xstrfmtcat(query,
			   "insert into %s (%s, lft, rgt) "
			   "values (%s, @myLeft+1, @myLeft+2) "
			   "on duplicate key update deleted=0, "
			   "lft=@myLeft+1, rgt=@myLeft+2 %s;"
			   "UNLOCK TABLES;",
			   assoc_table, cols,
			   vals,
			   extra);
		xstrfmtcat(query, 	
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %d, '%s', '%s', \"%s\");",
			   txn_table,
			   now, DBD_ADD_ASSOCS, assoc_name, user, extra);
		xfree(cols);
		xfree(vals);
		xfree(extra);
			
		rc = mysql_db_query(acct_mysql_db, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add assoc");
			rc = SLURM_ERROR;
			continue;
		}
	}
	list_iterator_destroy(itr);

	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_modify_users(MYSQL *acct_mysql_db, uint32_t uid, 
				       acct_user_cond_t *user_q,
				       acct_user_rec_t *user)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user_name = NULL;
	int set = 0;

	if(!user_q) {
		error("we need something to change");
		return SLURM_ERROR;
	}

	if((pw=getpwuid(uid))) {
		user_name = pw->pw_name;
	}

	if(user_q->user_list && list_count(user_q->user_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(user_q->user_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(user_q->def_acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(user_q->qos != ACCT_QOS_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && qos=%u", user_q->qos);
		else
			xstrfmtcat(extra, " where qos=%u",
				   user_q->qos);
			
	}

	if(user_q->admin_level != ACCT_ADMIN_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && admin_level=%u",
				   user_q->admin_level);
		else
			xstrfmtcat(extra, " where admin_level=%u",
				   user_q->admin_level);
	}

	if(user->default_acct)
		xstrfmtcat(vals, ", default_acct='%s'", user->default_acct);

	if(user->qos != ACCT_QOS_NOTSET)
		xstrfmtcat(vals, ", qos=%u", user->qos);

	if(user->admin_level != ACCT_ADMIN_NOTSET)
		xstrfmtcat(vals, ", admin_level=%u", user->admin_level);

	if(!extra || !vals) {
		error("Nothing to change");
		return SLURM_ERROR;
	}

	query = xstrdup_printf("update %s set %s %s", user_table, vals, extra);
	xstrfmtcat(query, 	
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%d, %d, \"%s\", '%s', \"%s\");",
		   txn_table,
		   now, DBD_MODIFY_USERS, extra, user_name, vals);
	xfree(vals);
	xfree(extra);
			
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't modify assocs");
		rc = SLURM_ERROR;
	}
	
	return rc;


#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_modify_accts(MYSQL *acct_mysql_db, uint32_t uid, 
				       acct_account_cond_t *acct_q,
				       acct_account_rec_t *acct)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user = NULL;
	int set = 0;

	if(!acct_q) {
		error("we need something to change");
		return SLURM_ERROR;
	}

	if((pw=getpwuid(uid))) {
		user = pw->pw_name;
	}

	if(acct_q->acct_list && list_count(acct_q->acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_q->description_list && list_count(acct_q->description_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->description_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(acct_q->organization_list && list_count(acct_q->organization_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->organization_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(acct_q->qos != ACCT_QOS_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && qos=%u", acct_q->qos);
		else
			xstrfmtcat(extra, " where qos=%u",
				   acct_q->qos);
			
	}

	if(acct->description)
		xstrfmtcat(vals, ", description='%s'", acct->description);
	if(acct->organization)
		xstrfmtcat(vals, ", organization='%u'", acct->organization);
	if(acct->qos != ACCT_QOS_NOTSET)
		xstrfmtcat(vals, ", qos='%u'", acct->qos);

	if(!extra || !vals) {
		error("Nothing to change");
		return SLURM_ERROR;
	}

	query = xstrdup_printf("update %s set %s %s", acct_table, vals, extra);
	xstrfmtcat(query, 	
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%d, %d, \"%s\", '%s', \"%s\");",
		   txn_table,
		   now, DBD_MODIFY_ACCOUNTS, extra, user, vals);
	xfree(vals);
	xfree(extra);
			
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't modify assocs");
		rc = SLURM_ERROR;
	}
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_modify_clusters(MYSQL *acct_mysql_db, uint32_t uid, 
					  acct_cluster_cond_t *cluster_q,
					  acct_cluster_rec_t *cluster)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user = NULL;
	int set = 0;

	if(!cluster_q) {
		error("we need something to change");
		return SLURM_ERROR;
	}

	if((pw=getpwuid(uid))) {
		user = pw->pw_name;
	}

	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(cluster_q->cluster_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	xstrfmtcat(vals, "mod_time=%d", now);

	if(cluster->control_host)
		xstrfmtcat(vals, ", control_host='%s'", cluster->control_host);
	if(cluster->control_port)
		xstrfmtcat(vals, ", control_port='%u'", cluster->control_port);
		
	if(!extra || !vals) {
		error("Nothing to change");
		return SLURM_ERROR;
	}

	query = xstrdup_printf("update %s set %s %s",
			       cluster_table, vals, extra);
	xstrfmtcat(query, 	
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%d, %d, \"%s\", '%s', \"%s\");",
		   txn_table,
		   now, DBD_MODIFY_CLUSTERS, extra, user, vals);
	xfree(vals);
	xfree(extra);
			
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't modify assocs");
		rc = SLURM_ERROR;
	}
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_modify_associations(MYSQL *acct_mysql_db, 
					      uint32_t uid, 
					      acct_association_cond_t *assoc_q,
					      acct_association_rec_t *assoc)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user = NULL;
	int set = 0;

	if(!assoc_q) {
		error("we need something to change");
		return SLURM_ERROR;
	}

	if((pw=getpwuid(uid))) {
		user = pw->pw_name;
	}

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->cluster_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "cluster='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->user_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "user='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->id_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=%s", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(assoc_q->parent_acct) {
		xstrfmtcat(extra, " && parent_acct='%s'", assoc_q->parent_acct);
	}

	if(assoc->parent_acct) {
		/* FIX ME: We need to be able to move the account to a
		   different place here in the heiarchy
		*/
	}

	xstrfmtcat(vals, "mod_time=%d", now);
	
	if((int)assoc->fairshare >= 0) 
		xstrfmtcat(vals, ", fairshare=%d", assoc->fairshare);
	
	if((int)assoc->max_jobs >= 0) 
		xstrfmtcat(vals, ", max_jobs=%d", assoc->max_jobs);
	
	if((int)assoc->max_nodes_per_job >= 0) 
		xstrfmtcat(vals, ", max_nodes_per_job=%d",
			   assoc->max_nodes_per_job);
	
	if((int)assoc->max_wall_duration_per_job >= 0) {
		xstrfmtcat(vals, ", max_wall_duration_per_job=%d",
			   assoc->max_wall_duration_per_job);
	}

	if((int)assoc->max_cpu_secs_per_job >= 0) {
		xstrfmtcat(vals, ", max_cpu_seconds_per_job=%d",
			   assoc->max_cpu_secs_per_job);
	}

	if(!extra || !vals) {
		error("Nothing to change");
		return SLURM_ERROR;
	}

	query = xstrdup_printf("update %s set %s %s", assoc_table, vals, extra);
	xstrfmtcat(query, 	
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%d, %d, \"%s\", '%s', \"%s\");",
		   txn_table,
		   now, DBD_MODIFY_ASSOCS, extra, user, vals);
	xfree(vals);
	xfree(extra);
			
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't modify assocs");
		rc = SLURM_ERROR;
	}
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_remove_users(MYSQL *acct_mysql_db, uint32_t uid, 
				       acct_user_cond_t *user_q)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user_name = NULL;
	int set = 0;

	if(!user_q) {
		error("we need something to change");
		return SLURM_ERROR;
	}

	if((pw=getpwuid(uid))) {
		user_name = pw->pw_name;
	}

	if(user_q->user_list && list_count(user_q->user_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(user_q->user_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(user_q->def_acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(user_q->qos != ACCT_QOS_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && qos=%u", user_q->qos);
		else
			xstrfmtcat(extra, " where qos=%u",
				   user_q->qos);
			
	}

	if(user_q->admin_level != ACCT_ADMIN_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && admin_level=%u",
				   user_q->admin_level);
		else
			xstrfmtcat(extra, " where admin_level=%u",
				   user_q->admin_level);
	}

	if(!extra) {
		error("Nothing to remove");
		return SLURM_ERROR;
	}

	query = xstrdup_printf("update %s set mod_time=%d, deleted=1 %s", 
			       now, user_table, extra);
	xstrfmtcat(query, 	
		   "insert into %s "
		   "(timestamp, action, name, actor) "
		   "values (%d, %d, \"%s\", '%s');",
		   txn_table,
		   now, DBD_REMOVE_USERS, extra, user_name);
	xfree(extra);
			
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't modify assocs");
		rc = SLURM_ERROR;
	}
	
	return rc;

#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_remove_coord(MYSQL *acct_mysql_db, uint32_t uid, 
				       char *acct, acct_user_cond_t *user_q)
{
#ifdef HAVE_MYSQL
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_remove_accts(MYSQL *acct_mysql_db, uint32_t uid, 
				       acct_account_cond_t *acct_q)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user_name = NULL;
	int set = 0;

	if(!acct_q) {
		error("we need something to change");
		return SLURM_ERROR;
	}

	if((pw=getpwuid(uid))) {
		user_name = pw->pw_name;
	}

	if(acct_q->acct_list && list_count(acct_q->acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_q->description_list && list_count(acct_q->description_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->description_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(acct_q->organization_list && list_count(acct_q->organization_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->organization_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(acct_q->qos != ACCT_QOS_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && qos=%u", acct_q->qos);
		else
			xstrfmtcat(extra, " where qos=%u",
				   acct_q->qos);
			
	}

	if(!extra) {
		error("Nothing to remove");
		return SLURM_ERROR;
	}

	query = xstrdup_printf("update %s set mod_time=%d, deleted=1 %s",
			       now, acct_table, extra);
	xstrfmtcat(query, 	
		   "insert into %s "
		   "(timestamp, action, name, actor) "
		   "values (%d, %d, \"%s\", '%s');",
		   txn_table,
		   now, DBD_REMOVE_ACCOUNTS, extra, user_name);
	xfree(extra);
			
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't remove accts");
		goto end_it;
	}
	

end_it:
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_remove_clusters(MYSQL *acct_mysql_db, uint32_t uid, 
					  acct_cluster_cond_t *cluster_q)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *assoc_extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user_name = NULL;
	int set = 0;

	if(!cluster_q) {
		error("we need something to change");
		return SLURM_ERROR;
	}

	if((pw=getpwuid(uid))) {
		user_name = pw->pw_name;
	}

	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		set = 0;
		if(extra) {
			xstrcat(extra, " && (");
			xstrcat(assoc_extra, " && (");
		} else {
			xstrcat(extra, " where (");
			xstrcat(assoc_extra, " where (");
		}
		itr = list_iterator_create(cluster_q->cluster_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			xstrfmtcat(assoc_extra, "cluster='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
		xstrcat(assoc_extra, ")");
	}

	if(!extra) {
		error("Nothing to remove");
		return SLURM_ERROR;
	}

	query = xstrdup_printf("update %s set mod_time=%d, deleted=1 %s",
			       now, cluster_table, extra);
	xstrfmtcat(query, 	
		   "insert into %s "
		   "(timestamp, action, name, actor) "
		   "values (%d, %d, \"%s\", '%s');",
		   txn_table,
		   now, DBD_REMOVE_CLUSTERS, extra, user_name);
	xfree(extra);
			
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't remove clusters");
		goto end_it;
	}
	
	query = xstrdup_printf("update %s set mod_time=%d, deleted=1 %s",
			       now, assoc_table, assoc_extra);
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't remove cluster associations");
	}
	
end_it:
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_remove_associations(MYSQL *acct_mysql_db,
					      uint32_t uid, 
					      acct_association_cond_t *assoc_q)
{
#ifdef HAVE_MYSQL
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL;
	time_t now = time(NULL);
	struct passwd *pw = NULL;
	char *user_name = NULL;
	int set = 0;

	if(!assoc_q) {
		error("we need something to change");
		return SLURM_ERROR;
	}

	if((pw=getpwuid(uid))) {
		user_name = pw->pw_name;
	}

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->cluster_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "cluster='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->user_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "user='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->id_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=%s", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(assoc_q->parent_acct) {
		xstrfmtcat(extra, " && parent_acct='%s'", assoc_q->parent_acct);
	}

	query = xstrdup_printf("update %s set mod_time=%d, deleted=1 %s",
			       now, assoc_table, extra);
	xstrfmtcat(query, 	
		   "insert into %s "
		   "(timestamp, action, name, actor) "
		   "values (%d, %d, \"%s\", '%s');",
		   txn_table,
		   now, DBD_REMOVE_ASSOCS, extra, user_name);
	xfree(extra);
			
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't remove assocs");
		rc = SLURM_ERROR;
	}
	
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern List acct_storage_p_get_users(MYSQL *acct_mysql_db, 
				     acct_user_cond_t *user_q)
{
#ifdef HAVE_MYSQL
	char *query = NULL;	
	char *extra = NULL;	
	char *tmp = NULL;	
	List user_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL, *coord_result = NULL;
	MYSQL_ROW row, coord_row;

	/* if this changes you will need to edit the corresponding enum */
	char *user_req_inx[] = {
		"name",
		"default_acct",
		"qos",
		"admin_level"
	};
	enum {
		USER_REQ_NAME,
		USER_REQ_DA,
		USER_REQ_EX,
		USER_REQ_AL,
		USER_REQ_COUNT
	};

	if(!user_q) 
		goto empty;

	if(user_q->user_list && list_count(user_q->user_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(user_q->user_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(user_q->def_acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(user_q->qos != ACCT_QOS_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && qos=%u", user_q->qos);
		else
			xstrfmtcat(extra, " where qos=%u",
				   user_q->qos);
			
	}

	if(user_q->admin_level != ACCT_ADMIN_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && admin_level=%u",
				   user_q->admin_level);
		else
			xstrfmtcat(extra, " where admin_level=%u",
				   user_q->admin_level);
	}
empty:

	xfree(tmp);
	xstrfmtcat(tmp, "%s", user_req_inx[i]);
	for(i=1; i<USER_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", user_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s", tmp, user_table);
	xfree(tmp);

	if(extra) {
		xstrcat(query, extra);
		xfree(extra);
	}

	//info("query = %s", query);
	if(!(result = mysql_db_query_ret(acct_mysql_db, query))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	user_list = list_create(destroy_acct_user_rec);

	while((row = mysql_fetch_row(result))) {
		acct_user_rec_t *user = xmalloc(sizeof(acct_user_rec_t));
		struct passwd *passwd_ptr = NULL;
		list_append(user_list, user);

		user->name =  xstrdup(row[USER_REQ_NAME]);
		user->default_acct = xstrdup(row[USER_REQ_DA]);
		user->admin_level = atoi(row[USER_REQ_AL]);
		user->qos = atoi(row[USER_REQ_EX]);

		passwd_ptr = getpwnam(user->name);
		if(passwd_ptr) 
			user->uid = passwd_ptr->pw_uid;
		
		user->coord_accts = list_create(destroy_acct_coord_rec);
		query = xstrdup_printf("select acct from %s where user='%s'",
				       acct_coord_table, user->name);

		if(!(coord_result = mysql_db_query_ret(acct_mysql_db, query))) {
			xfree(query);
			continue;
		}
		xfree(query);
		
		while((coord_row = mysql_fetch_row(coord_result))) {
			acct_coord_rec_t *coord =
				xmalloc(sizeof(acct_coord_rec_t));
			list_append(user->coord_accts, coord);
			coord->acct_name = xstrdup(coord_row[0]);
			coord->sub_acct = 0;
		}
		mysql_free_result(coord_result);
		/* FIX ME: ADD SUB projects here from assoc list lft
		 * rgt */
		
		if(user_q->with_assocs) {
			acct_association_cond_t assoc_q;
			memset(&assoc_q, 0, sizeof(acct_association_cond_t));
			assoc_q.user_list = list_create(slurm_destroy_char);
			list_append(assoc_q.user_list, user->name);
			user->assoc_list = acct_storage_p_get_associations(
				acct_mysql_db, &assoc_q);
			list_destroy(assoc_q.user_list);
		}
	}
	mysql_free_result(result);

	return user_list;
#else
	return NULL;
#endif
}

extern List acct_storage_p_get_accts(MYSQL *acct_mysql_db, 
				     acct_account_cond_t *acct_q)
{
#ifdef HAVE_MYSQL
	char *query = NULL;	
	char *extra = NULL;	
	char *tmp = NULL;	
	List acct_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL, *coord_result = NULL;
	MYSQL_ROW row, coord_row;

	/* if this changes you will need to edit the corresponding enum */
	char *acct_req_inx[] = {
		"name",
		"description",
		"qos",
		"organization"
	};
	enum {
		ACCT_REQ_NAME,
		ACCT_REQ_DESC,
		ACCT_REQ_QOS,
		ACCT_REQ_ORG,
		ACCT_REQ_COUNT
	};

	if(!acct_q) 
		goto empty;

	if(acct_q->acct_list && list_count(acct_q->acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_q->description_list && list_count(acct_q->description_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->description_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(acct_q->organization_list && list_count(acct_q->organization_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(acct_q->organization_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(acct_q->qos != ACCT_QOS_NOTSET) {
		if(extra)
			xstrfmtcat(extra, " && qos=%u", acct_q->qos);
		else
			xstrfmtcat(extra, " where qos=%u",
				   acct_q->qos);
	}

empty:

	xfree(tmp);
	xstrfmtcat(tmp, "%s", acct_req_inx[i]);
	for(i=1; i<ACCT_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", acct_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s", tmp, acct_table);
	xfree(tmp);

	if(extra) {
		xstrcat(query, extra);
		xfree(extra);
	}

	//info("query = %s", query);
	if(!(result = mysql_db_query_ret(acct_mysql_db, query))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	acct_list = list_create(destroy_acct_account_rec);

	while((row = mysql_fetch_row(result))) {
		acct_account_rec_t *acct = xmalloc(sizeof(acct_account_rec_t));
		list_append(acct_list, acct);

		acct->name =  xstrdup(row[ACCT_REQ_NAME]);
		acct->description = xstrdup(row[ACCT_REQ_DESC]);
		acct->organization = xstrdup(row[ACCT_REQ_ORG]);
		acct->qos = atoi(row[ACCT_REQ_QOS]);

		acct->coordinators = list_create(slurm_destroy_char);
		query = xstrdup_printf("select user from %s where acct='%s'",
				       acct_coord_table, acct->name);

		if(!(coord_result = mysql_db_query_ret(acct_mysql_db, query))) {
			xfree(query);
			continue;
		}
		xfree(query);
		
		while((coord_row = mysql_fetch_row(coord_result))) {
			object = xstrdup(coord_row[0]);
			list_append(acct->coordinators, object);
		}
		mysql_free_result(coord_result);
	}
	mysql_free_result(result);

	return acct_list;
#else
	return NULL;
#endif
}

extern List acct_storage_p_get_clusters(MYSQL *acct_mysql_db, 
					acct_cluster_cond_t *cluster_q)
{
#ifdef HAVE_MYSQL
	char *query = NULL;	
	char *extra = NULL;	
	char *tmp = NULL;	
	List cluster_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *cluster_req_inx[] = {
		"name",
		"control_host",
		"control_port"
	};
	enum {
		CLUSTER_REQ_NAME,
		CLUSTER_REQ_CH,
		CLUSTER_REQ_CP,
		CLUSTER_REQ_COUNT
	};

	if(!cluster_q) 
		goto empty;

	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(cluster_q->cluster_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

	xfree(tmp);
	xstrfmtcat(tmp, "%s", cluster_req_inx[i]);
	for(i=1; i<CLUSTER_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", cluster_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s", tmp, cluster_table);
	xfree(tmp);

	if(extra) {
		xstrcat(query, extra);
		xfree(extra);
	}

	//info("query = %s", query);
	if(!(result = mysql_db_query_ret(acct_mysql_db, query))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	cluster_list = list_create(destroy_acct_cluster_rec);

	while((row = mysql_fetch_row(result))) {
		acct_cluster_rec_t *cluster =
			xmalloc(sizeof(acct_cluster_rec_t));
		list_append(cluster_list, cluster);

		cluster->name =  xstrdup(row[CLUSTER_REQ_NAME]);
		cluster->control_host = xstrdup(row[CLUSTER_REQ_CH]);
		cluster->control_port = atoi(row[CLUSTER_REQ_CP]);
	}
	mysql_free_result(result);

	return cluster_list;
#else
	return NULL;
#endif
}

extern List acct_storage_p_get_associations(MYSQL *acct_mysql_db, 
					    acct_association_cond_t *assoc_q)
{
#ifdef HAVE_MYSQL
	char *query = NULL;	
	char *extra = NULL;	
	char *tmp = NULL;	
	List assoc_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *assoc_req_inx[] = {
		"id",
		"user",
		"acct",
		"cluster",
		"partition",
		"parent_acct",
		"fairshare",
		"max_jobs",
		"max_nodes_per_job",
		"max_wall_duration_per_job",
		"max_cpu_seconds_per_job",
	};
	enum {
		ASSOC_REQ_ID,
		ASSOC_REQ_USER,
		ASSOC_REQ_ACCT,
		ASSOC_REQ_CLUSTER,
		ASSOC_REQ_PART,
		ASSOC_REQ_PARENT,
		ASSOC_REQ_FS,
		ASSOC_REQ_MJ,
		ASSOC_REQ_MNPJ,
		ASSOC_REQ_MWPJ,
		ASSOC_REQ_MCPJ,
		ASSOC_REQ_COUNT
	};
	xstrcat(extra, "where deleted=0");
	if(!assoc_q) 
		goto empty;

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->acct_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->cluster_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "cluster='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->user_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "user='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(assoc_q->id_list);
		while((object = list_next(itr))) {
			if(set) 
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=%s", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}
	
	if(assoc_q->parent_acct) {
		xstrfmtcat(extra, " && parent_acct='%s'", assoc_q->parent_acct);
	}
empty:
	xfree(tmp);
	xstrfmtcat(tmp, "%s", assoc_req_inx[i]);
	for(i=1; i<ASSOC_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", assoc_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s", tmp, assoc_table, extra);
	xfree(tmp);
	xfree(extra);
	//info("query =\n%s", query);
	if(!(result = mysql_db_query_ret(acct_mysql_db, query))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	assoc_list = list_create(destroy_acct_association_rec);

	while((row = mysql_fetch_row(result))) {
		acct_association_rec_t *assoc =
			xmalloc(sizeof(acct_association_rec_t));
		list_append(assoc_list, assoc);
		
		assoc->id =  atoi(row[ASSOC_REQ_ID]);
		
		if(row[ASSOC_REQ_USER][0])
			assoc->user = xstrdup(row[ASSOC_REQ_USER]);
		assoc->acct = xstrdup(row[ASSOC_REQ_ACCT]);
		assoc->cluster = xstrdup(row[ASSOC_REQ_CLUSTER]);
		if(row[ASSOC_REQ_PART][0])
			assoc->partition = xstrdup(row[ASSOC_REQ_PART]);
		if(row[ASSOC_REQ_FS])
			assoc->fairshare = atoi(row[ASSOC_REQ_FS]);
		else
			assoc->fairshare = -1;
		if(row[ASSOC_REQ_MJ])
			assoc->max_jobs = atoi(row[ASSOC_REQ_MJ]);
		else
			assoc->max_jobs = -1;
		if(row[ASSOC_REQ_MNPJ])
			assoc->max_nodes_per_job = atoi(row[ASSOC_REQ_MNPJ]);
		else
			assoc->max_nodes_per_job = -1;
		if(row[ASSOC_REQ_MWPJ])
			assoc->max_wall_duration_per_job = 
				atoi(row[ASSOC_REQ_MWPJ]);
		else
			assoc->max_wall_duration_per_job = -1;
		if(row[ASSOC_REQ_MCPJ])
			assoc->max_cpu_secs_per_job = atoi(row[ASSOC_REQ_MCPJ]);
		else
			assoc->max_cpu_secs_per_job = -1;
	}
	mysql_free_result(result);

	return assoc_list;
#else
	return NULL;
#endif
}

extern int acct_storage_p_get_usage(MYSQL *acct_mysql_db,
				    acct_usage_type_t type,
				    acct_association_rec_t *acct_assoc,
				    time_t start, time_t end)
{
#ifdef HAVE_MYSQL
	int rc = SLURM_SUCCESS;

	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int acct_storage_p_roll_usage(MYSQL *acct_mysql_db, 
				     acct_usage_type_t type,
				     time_t start)
{
#ifdef HAVE_MYSQL
	int rc = SLURM_SUCCESS;

	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int clusteracct_storage_p_node_down(MYSQL *acct_mysql_db, 
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason)
{
#ifdef HAVE_MYSQL
	uint16_t cpus;
	int rc = SLURM_SUCCESS;
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
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);

	debug2("inserting %s(%s) with %u cpus", node_ptr->name, cluster, cpus);

	query = xstrdup_printf(
		"insert into %s "
		"(node_name, cluster, cpu_count, period_start, reason) "
		"values ('%s', '%s', %u, %d, '%s')",
		event_table, node_ptr->name, cluster, 
		cpus, event_time, my_reason);
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);

	return rc;
#else
	return SLURM_ERROR;
#endif
}
extern int clusteracct_storage_p_node_up(MYSQL *acct_mysql_db, 
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
#ifdef HAVE_MYSQL
	char* query;
	int rc = SLURM_SUCCESS;

	query = xstrdup_printf(
		"update %s set period_end=%d where cluster='%s' "
		"and period_end=0 and node_name='%s'",
		event_table, (event_time-1), cluster, node_ptr->name);
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int clusteracct_storage_p_register_ctld(char *cluster,
					       uint16_t port)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_procs(MYSQL *acct_mysql_db, 
					       char *cluster,
					       uint32_t procs,
					       time_t event_time)
{
#ifdef HAVE_MYSQL
	static uint32_t last_procs = -1;
	char* query;
	int rc = SLURM_SUCCESS;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if (procs == last_procs) {
		debug3("we have the same procs as before no need to "
		       "update the database.");
		return SLURM_SUCCESS;
	}
	last_procs = procs;

	/* Record the processor count */
	query = xstrdup_printf(
		"select cpu_count from %s where cluster='%s' "
		"and period_end=0 and node_name=''",
		event_table, cluster);
	if(!(result = mysql_db_query_ret(acct_mysql_db, query))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	/* we only are checking the first one here */
	if(!(row = mysql_fetch_row(result))) {
		debug("We don't have an entry for this machine %s"
		      "most likely a first time running.", cluster);
		goto add_it;
	}

	if(atoi(row[0]) == procs) {
		debug("%s hasn't changed since last entry", cluster);
		goto end_it;
	}
	debug("%s has changed from %s cpus to %u", cluster, row[0], procs);   

	query = xstrdup_printf(
		"update %s set period_end=%d where cluster='%s' "
		"and period_end=0 and node_name=''",
		event_table, (event_time-1), cluster);
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	if(rc != SLURM_SUCCESS)
		goto end_it;
add_it:
	query = xstrdup_printf(
		"insert into %s (cluster, cpu_count, period_start) "
		"values ('%s', %u, %d)",
		event_table, cluster, procs, event_time);
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);

end_it:
	mysql_free_result(result);
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int clusteracct_storage_p_get_usage(
	MYSQL *acct_mysql_db, acct_usage_type_t type, 
	acct_cluster_rec_t *cluster_rec, time_t start, time_t end)
{
#ifdef HAVE_MYSQL

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(MYSQL *acct_mysql_db, 
				       struct job_record *job_ptr)
{
#ifdef HAVE_MYSQL
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

	if(!acct_mysql_db || mysql_ping(acct_mysql_db) != 0) {
		if(!(acct_mysql_db = acct_storage_p_get_connection())) {
			return SLURM_ERROR;
		}
	}

	debug2("mysql_jobacct_job_start() called");
	priority = (job_ptr->priority == NO_VAL) ?
		-1L : (long) job_ptr->priority;

	if (job_ptr->name && job_ptr->name[0]) {
		jname = job_ptr->name;
	} else {
		jname = "allocation";
		track_steps = 1;
	}

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "(null)";

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
	query = xstrdup_printf(
		"insert into %s "
		"(jobid, associd, gid, partition, blockid, "
		"eligible, submit, start, name, track_steps, "
		"state, priority, req_cpus, alloc_cpus, nodelist) "
		"values (%u, %u, %u, '%s', '%s', "
		"%d, %d, %d, '%s', %u, "
		"%u, %u, %u, %u, '%s') "
		"on duplicate key update id=LAST_INSERT_ID(id)",
		job_table, job_ptr->job_id, job_ptr->assoc_id,
		job_ptr->group_id, job_ptr->partition, block_id,
		(int)job_ptr->details->begin_time,
		(int)job_ptr->details->submit_time, (int)job_ptr->start_time,
		jname, track_steps, job_ptr->job_state & (~JOB_COMPLETING),
		priority, job_ptr->num_procs, job_ptr->total_procs, nodes);

	xfree(block_id);

try_again:
	if(!(job_ptr->db_index = mysql_insert_ret_id(acct_mysql_db, query))) {
		if(!reinit) {
			error("It looks like the storage has gone "
			      "away trying to reconnect");
			acct_storage_p_close_connection(acct_mysql_db);
			acct_mysql_db = acct_storage_p_get_connection();
			reinit = 1;
			goto try_again;
		} else
			rc = SLURM_ERROR;
	}
	xfree(query);

	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(MYSQL *acct_mysql_db, 
					  struct job_record *job_ptr)
{
#ifdef HAVE_MYSQL
	char *query = NULL, *nodes = NULL;
	int rc=SLURM_SUCCESS;
	
	if (!job_ptr->db_index 
	    && (!job_ptr->details || !job_ptr->details->submit_time)) {
		error("jobacct_storage_p_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(!acct_mysql_db || mysql_ping(acct_mysql_db) != 0) {
		if(!(acct_mysql_db = acct_storage_p_get_connection())) {
			return SLURM_ERROR;
		}
	}
	
	debug2("mysql_jobacct_job_complete() called");
	if (job_ptr->end_time == 0) {
		debug("mysql_jobacct: job %u never started", job_ptr->job_id);
		return SLURM_ERROR;
	}	
	
	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "(null)";

	if(!job_ptr->db_index) {
		job_ptr->db_index = _get_db_index(acct_mysql_db,
						  job_ptr->details->submit_time,
						  job_ptr->job_id,
						  job_ptr->assoc_id);
		if(job_ptr->db_index == (uint32_t)-1) 
			return SLURM_ERROR;
	}
	query = xstrdup_printf("update %s set start=%u, end=%u, state=%d, "
			       "nodelist='%s', comp_code=%u, "
			       "kill_requid=%u where id=%u",
			       job_table, (int)job_ptr->start_time,
			       (int)job_ptr->end_time, 
			       job_ptr->job_state & (~JOB_COMPLETING),
			       nodes, job_ptr->exit_code,
			       job_ptr->requid, job_ptr->db_index);
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	
	return  rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(MYSQL *acct_mysql_db, 
					struct step_record *step_ptr)
{
#ifdef HAVE_MYSQL
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

	if(!acct_mysql_db || mysql_ping(acct_mysql_db) != 0) {
		if(!(acct_mysql_db = acct_storage_p_get_connection())) {
			return SLURM_ERROR;
		}
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
			_get_db_index(acct_mysql_db,
				      step_ptr->job_ptr->details->submit_time,
				      step_ptr->job_ptr->job_id,
				      step_ptr->job_ptr->assoc_id);
		if(step_ptr->job_ptr->db_index == (uint32_t)-1) 
			return SLURM_ERROR;
	}
	/* we want to print a -1 for the requid so leave it a
	   %d */
	query = xstrdup_printf(
		"insert into %s (id, stepid, start, name, state, "
		"cpus, nodelist) "
		"values (%d, %u, %u, '%s', %d, %u, '%s') "
		"on duplicate key update cpus=%u",
		step_table, step_ptr->job_ptr->db_index,
		step_ptr->step_id, 
		(int)step_ptr->start_time, step_ptr->name,
		JOB_RUNNING, cpus, node_list, cpus);
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);

	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(MYSQL *acct_mysql_db, 
					   struct step_record *step_ptr)
{
#ifdef HAVE_MYSQL
	time_t now;
	int elapsed;
	int comp_status;
	int cpus = 0;
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	float ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	float ave_cpu = 0, ave_cpu2 = 0;
	char *query = NULL;
	int rc =SLURM_SUCCESS;
	
	if (!step_ptr->job_ptr->db_index 
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("jobacct_storage_p_step_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(!acct_mysql_db || mysql_ping(acct_mysql_db) != 0) {
		if(!(acct_mysql_db = acct_storage_p_get_connection())) {
			return SLURM_ERROR;
		}
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
	if (step_ptr->exit_code)
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
			_get_db_index(acct_mysql_db,
				      step_ptr->job_ptr->details->submit_time,
				      step_ptr->job_ptr->job_id,
				      step_ptr->job_ptr->assoc_id);
		if(step_ptr->job_ptr->db_index == -1) 
			return SLURM_ERROR;
	}

	query = xstrdup_printf(
		"update %s set end=%u, state=%d, "
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
		step_ptr->exit_code,
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
	rc = mysql_db_query(acct_mysql_db, query);
	xfree(query);
	 
	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(MYSQL *acct_mysql_db, 
				     struct job_record *job_ptr)
{
#ifdef HAVE_MYSQL
	char query[1024];
	int rc = SLURM_SUCCESS;
	
	if(!acct_mysql_db || mysql_ping(acct_mysql_db) != 0) {
		if(!(acct_mysql_db = acct_storage_p_get_connection())) {
			return SLURM_ERROR;
		}
	}
	
	if(!job_ptr->db_index) {
		job_ptr->db_index = _get_db_index(acct_mysql_db,
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
	rc = mysql_db_query(acct_mysql_db, query);
	if(rc != SLURM_ERROR) {
		snprintf(query, sizeof(query),
			 "update %s set suspended=%u-suspended, "
			 "state=%d where id=%u and end=0",
			 step_table, (int)job_ptr->suspend_time, 
			 job_ptr->job_state, job_ptr->db_index);
		rc = mysql_db_query(acct_mysql_db, query);
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
extern List jobacct_storage_p_get_jobs(MYSQL *acct_mysql_db, 
				       List selected_steps,
				       List selected_parts,
				       void *params)
{
	List job_list = NULL;
#ifdef HAVE_MYSQL
	if(!acct_mysql_db || mysql_ping(acct_mysql_db) != 0) {
		if(!(acct_mysql_db = acct_storage_p_get_connection())) {
			return NULL;
		}
	}
	job_list = mysql_jobacct_process_get_jobs(acct_mysql_db,
						  selected_steps,
						  selected_parts,
						  params);	
#endif
	return job_list;
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_p_archive(MYSQL *acct_mysql_db, 
				      List selected_parts,
				      void *params)
{
#ifdef HAVE_MYSQL
	if(!acct_mysql_db || mysql_ping(acct_mysql_db) != 0) {
		if(!(acct_mysql_db = acct_storage_p_get_connection())) {
			return;
		}
	}
	mysql_jobacct_process_archive(acct_mysql_db,
				      selected_parts, params);
#endif
	return;
}
