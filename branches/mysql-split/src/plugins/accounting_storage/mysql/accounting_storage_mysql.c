/*****************************************************************************\
 *  accounting_storage_mysql.c - accounting interface to mysql.
 *
 *  $Id: accounting_storage_mysql.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
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
 *****************************************************************************
 * Notes on mysql configuration
 *	Assumes mysql is installed as user root
 *	Assumes SlurmUser is configured as user slurm
 * # mysqladmin create <db_name>
 *	The <db_name> goes into slurmdbd.conf as StorageLoc
 * # mysql --user=root -p
 * mysql> GRANT ALL ON *.* TO 'slurm'@'localhost' IDENTIFIED BY PASSWORD 'pw';
 * mysql> GRANT SELECT, INSERT ON *.* TO 'slurm'@'localhost';
\*****************************************************************************/

#include "accounting_storage_mysql.h"
#include "mysql_acct.h"
#include "mysql_archive.h"
#include "mysql_assoc.h"
#include "mysql_cluster.h"
#include "mysql_job.h"
#include "mysql_jobacct_process.h"
#include "mysql_problems.h"
#include "mysql_qos.h"
#include "mysql_rollup.h"
#include "mysql_txn.h"
#include "mysql_usage.h"
#include "mysql_user.h"
#include "mysql_wckey.h"

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

static mysql_db_info_t *mysql_db_info = NULL;
static char *mysql_db_name = NULL;
static time_t global_last_rollup = 0;
static pthread_mutex_t rollup_lock = PTHREAD_MUTEX_INITIALIZER;

#define DELETE_SEC_BACK 86400

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
char *last_ran_table = "last_ran_table";
char *qos_table = "qos_table";
char *resv_table = "resv_table";
char *step_table = "step_table";
char *txn_table = "txn_table";
char *user_table = "user_table";
char *suspend_table = "suspend_table";
char *wckey_day_table = "wckey_day_usage_table";
char *wckey_hour_table = "wckey_hour_usage_table";
char *wckey_month_table = "wckey_month_usage_table";
char *wckey_table = "wckey_table";

static char *default_qos_str = NULL;

static int _set_qos_cnt(MYSQL *db_conn)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = xstrdup_printf("select MAX(id) from %s", qos_table);

	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(row = mysql_fetch_row(result))) {
		mysql_free_result(result);
		return SLURM_ERROR;
	}

	/* Set the current qos_count on the system for
	   generating bitstr of that length.  Since 0 isn't
	   possible as an id we add 1 to the total to burn 0 and
	   start at the 1 bit.
	*/
	g_qos_count = atoi(row[0]) + 1;
	mysql_free_result(result);

	return SLURM_SUCCESS;
}

static char *_get_cluster_from_associd(mysql_conn_t *mysql_conn,
				       uint32_t associd)
{
	char *cluster = NULL;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* Just so we don't have to keep a
	   cache of the associations around we
	   will just query the db for the cluster
	   name of the association id.  Since
	   this should sort of be a rare case
	   this isn't too bad.
	*/
	query = xstrdup_printf("select cluster from %s where id=%u",
			       assoc_table, associd);

	debug4("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);
	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	if((row = mysql_fetch_row(result)))
		cluster = xstrdup(row[0]);

	mysql_free_result(result);

	return cluster;
}

static char *_get_user_from_associd(mysql_conn_t *mysql_conn, uint32_t associd)
{
	char *user = NULL;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* Just so we don't have to keep a
	   cache of the associations around we
	   will just query the db for the user
	   name of the association id.  Since
	   this should sort of be a rare case
	   this isn't too bad.
	*/
	query = xstrdup_printf("select user from %s where id=%u",
			       assoc_table, associd);

	debug4("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);
	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	if((row = mysql_fetch_row(result)))
		user = xstrdup(row[0]);

	mysql_free_result(result);

	return user;
}

static uint32_t _get_wckeyid(mysql_conn_t *mysql_conn, char **name,
			     uid_t uid, char *cluster, uint32_t associd)
{
	uint32_t wckeyid = 0;

	if(slurm_get_track_wckey()) {
		/* Here we are looking for the wckeyid if it doesn't
		 * exist we will create one.  We don't need to check
		 * if it is good or not.  Right now this is the only
		 * place things are created. We do this only on a job
		 * start, not on a job submit since we don't want to
		 * slow down getting the db_index back to the
		 * controller.
		 */
		acct_wckey_rec_t wckey_rec;
		char *user = NULL;

		/* since we are unable to rely on uids here (someone could
		   not have there uid in the system yet) we must
		   first get the user name from the associd */
		if(!(user = _get_user_from_associd(mysql_conn, associd))) {
			error("No user for associd %u", associd);
			goto no_wckeyid;
		}
		/* get the default key */
		if(!*name) {
			acct_user_rec_t user_rec;
			memset(&user_rec, 0, sizeof(acct_user_rec_t));
			user_rec.uid = NO_VAL;
			user_rec.name = user;
			if(assoc_mgr_fill_in_user(mysql_conn, &user_rec,
						  1, NULL) != SLURM_SUCCESS) {
				error("No user by name of %s assoc %u",
				      user, associd);
				xfree(user);
				goto no_wckeyid;
			}

			if(user_rec.default_wckey)
				*name = xstrdup_printf("*%s",
						       user_rec.default_wckey);
			else
				*name = xstrdup_printf("*");
		}

		memset(&wckey_rec, 0, sizeof(acct_wckey_rec_t));
		wckey_rec.name = (*name);
		wckey_rec.uid = NO_VAL;
		wckey_rec.user = user;
		wckey_rec.cluster = cluster;
		if(assoc_mgr_fill_in_wckey(mysql_conn, &wckey_rec,
					   ACCOUNTING_ENFORCE_WCKEYS,
					   NULL) != SLURM_SUCCESS) {
			List wckey_list = NULL;
			acct_wckey_rec_t *wckey_ptr = NULL;

			wckey_list = list_create(destroy_acct_wckey_rec);

			wckey_ptr = xmalloc(sizeof(acct_wckey_rec_t));
			wckey_ptr->name = xstrdup((*name));
			wckey_ptr->user = xstrdup(user);
			wckey_ptr->cluster = xstrdup(cluster);
			list_append(wckey_list, wckey_ptr);
			/* info("adding wckey '%s' '%s' '%s'", */
			/* 	     wckey_ptr->name, wckey_ptr->user, */
			/* 	     wckey_ptr->cluster); */
			/* we have already checked to make
			   sure this was the slurm user before
			   calling this */
			if(mysql_add_wckeys(mysql_conn,
					    slurm_get_slurm_user_id(),
					    wckey_list)
			   == SLURM_SUCCESS)
				acct_storage_p_commit(mysql_conn, 1);
			/* If that worked lets get it */
			assoc_mgr_fill_in_wckey(mysql_conn, &wckey_rec,
						ACCOUNTING_ENFORCE_WCKEYS,
						NULL);

			list_destroy(wckey_list);
		}
		xfree(user);
		/* info("got wckeyid of %d", wckey_rec.id); */
		wckeyid = wckey_rec.id;
	}
no_wckeyid:
	return wckeyid;
}

static int _preemption_loop(mysql_conn_t *mysql_conn, int begin_qosid,
			    bitstr_t *preempt_bitstr)
{
	acct_qos_rec_t qos_rec;
	int rc = 0, i=0;

	xassert(preempt_bitstr);

	/* check in the preempt list for all qos's preempted */
	for(i=0; i<bit_size(preempt_bitstr); i++) {
		if(!bit_test(preempt_bitstr, i))
			continue;

		memset(&qos_rec, 0, sizeof(qos_rec));
		qos_rec.id = i;
		assoc_mgr_fill_in_qos(mysql_conn, &qos_rec,
				      ACCOUNTING_ENFORCE_QOS,
				      NULL);
		/* check if the begin_qosid is preempted by this qos
		 * if so we have a loop */
		if(qos_rec.preempt_bitstr
		   && bit_test(qos_rec.preempt_bitstr, begin_qosid)) {
			error("QOS id %d has a loop at QOS %s",
			      begin_qosid, qos_rec.name);
			rc = 1;
			break;
		} else if(qos_rec.preempt_bitstr) {
			/* check this qos' preempt list and make sure
			   no loops exist there either */
			if((rc = _preemption_loop(mysql_conn, begin_qosid,
						  qos_rec.preempt_bitstr)))
				break;
		}
	}
	return rc;
}

/* here to add \\ to all \" in a string */
extern char *fix_double_quotes(char *str)
{
	int i=0, start=0;
	char *fixed = NULL;

	if(!str)
		return NULL;

	while(str[i]) {
		if(str[i] == '"') {
			char *tmp = xstrndup(str+start, i-start);
			xstrfmtcat(fixed, "%s\\\"", tmp);
			xfree(tmp);
			start = i+1;
		}

		i++;
	}

	if((i-start) > 0) {
		char *tmp = xstrndup(str+start, i-start);
		xstrcat(fixed, tmp);
		xfree(tmp);
	}

	return fixed;
}

/* This should be added to the beginning of each function to make sure
 * we have a connection to the database before we try to use it.
 */
extern int check_connection(mysql_conn_t *mysql_conn)
{
	if(!mysql_conn) {
		error("We need a connection to run this");
		errno = SLURM_ERROR;
		return SLURM_ERROR;
	} else if(!mysql_conn->db_conn
		  || mysql_db_ping(mysql_conn->db_conn) != 0) {
		if(mysql_get_db_connection(&mysql_conn->db_conn,
					   mysql_db_name, mysql_db_info)
		   != SLURM_SUCCESS) {
			error("unable to re-connect to mysql database");
			errno = ESLURM_DB_CONNECTION;
			return ESLURM_DB_CONNECTION;
		}
	}
	return SLURM_SUCCESS;
}

extern int setup_association_limits(acct_association_rec_t *assoc,
				    char **cols, char **vals,
				    char **extra, qos_level_t qos_level,
				    bool get_fs)
{
	if(!assoc)
		return SLURM_ERROR;

	if((int)assoc->shares_raw >= 0) {
		xstrcat(*cols, ", fairshare");
		xstrfmtcat(*vals, ", %u", assoc->shares_raw);
		xstrfmtcat(*extra, ", fairshare=%u", assoc->shares_raw);
	} else if (((int)assoc->shares_raw == INFINITE) || get_fs) {
		xstrcat(*cols, ", fairshare");
		xstrcat(*vals, ", 1");
		xstrcat(*extra, ", fairshare=1");
		assoc->shares_raw = 1;
	}

	if((int)assoc->grp_cpu_mins >= 0) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrfmtcat(*vals, ", %llu", assoc->grp_cpu_mins);
		xstrfmtcat(*extra, ", grp_cpu_mins=%llu",
			   assoc->grp_cpu_mins);
	} else if((int)assoc->grp_cpu_mins == INFINITE) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpu_mins=NULL");
	}

	if((int)assoc->grp_cpus >= 0) {
		xstrcat(*cols, ", grp_cpus");
		xstrfmtcat(*vals, ", %u", assoc->grp_cpus);
		xstrfmtcat(*extra, ", grp_cpus=%u", assoc->grp_cpus);
	} else if((int)assoc->grp_cpus == INFINITE) {
		xstrcat(*cols, ", grp_cpus");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpus=NULL");
	}

	if((int)assoc->grp_jobs >= 0) {
		xstrcat(*cols, ", grp_jobs");
		xstrfmtcat(*vals, ", %u", assoc->grp_jobs);
		xstrfmtcat(*extra, ", grp_jobs=%u", assoc->grp_jobs);
	} else if((int)assoc->grp_jobs == INFINITE) {
		xstrcat(*cols, ", grp_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_jobs=NULL");
	}

	if((int)assoc->grp_nodes >= 0) {
		xstrcat(*cols, ", grp_nodes");
		xstrfmtcat(*vals, ", %u", assoc->grp_nodes);
		xstrfmtcat(*extra, ", grp_nodes=%u", assoc->grp_nodes);
	} else if((int)assoc->grp_nodes == INFINITE) {
		xstrcat(*cols, ", grp_nodes");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_nodes=NULL");
	}

	if((int)assoc->grp_submit_jobs >= 0) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrfmtcat(*vals, ", %u",
			   assoc->grp_submit_jobs);
		xstrfmtcat(*extra, ", grp_submit_jobs=%u",
			   assoc->grp_submit_jobs);
	} else if((int)assoc->grp_submit_jobs == INFINITE) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_submit_jobs=NULL");
	}

	if((int)assoc->grp_wall >= 0) {
		xstrcat(*cols, ", grp_wall");
		xstrfmtcat(*vals, ", %u", assoc->grp_wall);
		xstrfmtcat(*extra, ", grp_wall=%u",
			   assoc->grp_wall);
	} else if((int)assoc->grp_wall == INFINITE) {
		xstrcat(*cols, ", grp_wall");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_wall=NULL");
	}

	if((int)assoc->max_cpu_mins_pj >= 0) {
		xstrcat(*cols, ", max_cpu_mins_per_job");
		xstrfmtcat(*vals, ", %llu", assoc->max_cpu_mins_pj);
		xstrfmtcat(*extra, ", max_cpu_mins_per_job=%u",
			   assoc->max_cpu_mins_pj);
	} else if((int)assoc->max_cpu_mins_pj == INFINITE) {
		xstrcat(*cols, ", max_cpu_mins_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpu_mins_per_job=NULL");
	}

	if((int)assoc->max_cpus_pj >= 0) {
		xstrcat(*cols, ", max_cpus_per_job");
		xstrfmtcat(*vals, ", %u", assoc->max_cpus_pj);
		xstrfmtcat(*extra, ", max_cpus_per_job=%u",
			   assoc->max_cpus_pj);
	} else if((int)assoc->max_cpus_pj == INFINITE) {
		xstrcat(*cols, ", max_cpus_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpus_per_job=NULL");
	}

	if((int)assoc->max_jobs >= 0) {
		xstrcat(*cols, ", max_jobs");
		xstrfmtcat(*vals, ", %u", assoc->max_jobs);
		xstrfmtcat(*extra, ", max_jobs=%u",
			   assoc->max_jobs);
	} else if((int)assoc->max_jobs == INFINITE) {
		xstrcat(*cols, ", max_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_jobs=NULL");
	}

	if((int)assoc->max_nodes_pj >= 0) {
		xstrcat(*cols, ", max_nodes_per_job");
		xstrfmtcat(*vals, ", %u", assoc->max_nodes_pj);
		xstrfmtcat(*extra, ", max_nodes_per_job=%u",
			   assoc->max_nodes_pj);
	} else if((int)assoc->max_nodes_pj == INFINITE) {
		xstrcat(*cols, ", max_nodes_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_nodes_per_job=NULL");
	}

	if((int)assoc->max_submit_jobs >= 0) {
		xstrcat(*cols, ", max_submit_jobs");
		xstrfmtcat(*vals, ", %u", assoc->max_submit_jobs);
		xstrfmtcat(*extra, ", max_submit_jobs=%u",
			   assoc->max_submit_jobs);
	} else if((int)assoc->max_submit_jobs == INFINITE) {
		xstrcat(*cols, ", max_submit_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_submit_jobs=NULL");
	}

	if((int)assoc->max_wall_pj >= 0) {
		xstrcat(*cols, ", max_wall_duration_per_job");
		xstrfmtcat(*vals, ", %u", assoc->max_wall_pj);
		xstrfmtcat(*extra, ", max_wall_duration_per_job=%u",
			   assoc->max_wall_pj);
	} else if((int)assoc->max_wall_pj == INFINITE) {
		xstrcat(*cols, ", max_wall_duration_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_wall_duration_per_job=NULL");
	}

	/* when modifying the qos it happens in the actual function
	   since we have to wait until we hear about the parent first. */
	if(qos_level == QOS_LEVEL_MODIFY)
		goto end_qos;

	if(assoc->qos_list && list_count(assoc->qos_list)) {
		char *qos_type = "qos";
		char *qos_val = NULL;
		char *tmp_char = NULL;
		int set = 0;
		ListIterator qos_itr =
			list_iterator_create(assoc->qos_list);

		while((tmp_char = list_next(qos_itr))) {
			/* we don't want to include blank names */
			if(!tmp_char[0])
				continue;
			if(!set) {
				if(tmp_char[0] == '+' || tmp_char[0] == '-')
					qos_type = "delta_qos";
				set = 1;
			}
			xstrfmtcat(qos_val, ",%s", tmp_char);
		}

		list_iterator_destroy(qos_itr);
		if(qos_val) {
			xstrfmtcat(*cols, ", %s", qos_type);
			xstrfmtcat(*vals, ", '%s'", qos_val);
			xstrfmtcat(*extra, ", %s=\"%s\"", qos_type, qos_val);
			xfree(qos_val);
		}
	} else if((qos_level == QOS_LEVEL_SET) && default_qos_str) {
		/* Add default qos to the account */
		xstrcat(*cols, ", qos");
		xstrfmtcat(*vals, ", '%s'", default_qos_str);
		xstrfmtcat(*extra, ", qos=\"%s\"", default_qos_str);
		if(!assoc->qos_list)
			assoc->qos_list = list_create(slurm_destroy_char);
		slurm_addto_char_list(assoc->qos_list, default_qos_str);
	} else {
		/* clear the qos */
		xstrcat(*cols, ", qos, delta_qos");
		xstrcat(*vals, ", '', ''");
		xstrcat(*extra, ", qos=\"\", delta_qos=\"\"");
	}
end_qos:

	return SLURM_SUCCESS;

}

static int _setup_qos_limits(acct_qos_rec_t *qos,
			     char **cols, char **vals,
			     char **extra, char **added_preempt)
{
	if(!qos)
		return SLURM_ERROR;

	if(qos->description) {
		xstrcat(*cols, ", description");
		xstrfmtcat(*vals, ", \"%s\"", qos->description);
		xstrfmtcat(*extra, ", description=\"%s\"",
			   qos->description);

	}
	if((int)qos->priority >= 0) {
		xstrcat(*cols, ", priority");
		xstrfmtcat(*vals, ", %d", qos->priority);
		xstrfmtcat(*extra, ", priority=%d", qos->priority);
	} else if ((int)qos->priority == INFINITE) {
		xstrcat(*cols, ", priority");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", priority=NULL");
	}

	if((int)qos->grp_cpu_mins >= 0) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrfmtcat(*vals, ", %llu", qos->grp_cpu_mins);
		xstrfmtcat(*extra, ", grp_cpu_mins=%llu",
			   qos->grp_cpu_mins);
	} else if((int)qos->grp_cpu_mins == INFINITE) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpu_mins=NULL");
	}

	if((int)qos->grp_cpus >= 0) {
		xstrcat(*cols, ", grp_cpus");
		xstrfmtcat(*vals, ", %u", qos->grp_cpus);
		xstrfmtcat(*extra, ", grp_cpus=%u", qos->grp_cpus);
	} else if((int)qos->grp_cpus == INFINITE) {
		xstrcat(*cols, ", grp_cpus");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpus=NULL");
	}

	if((int)qos->grp_jobs >= 0) {
		xstrcat(*cols, ", grp_jobs");
		xstrfmtcat(*vals, ", %u", qos->grp_jobs);
		xstrfmtcat(*extra, ", grp_jobs=%u", qos->grp_jobs);
	} else if((int)qos->grp_jobs == INFINITE) {
		xstrcat(*cols, ", grp_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_jobs=NULL");
	}

	if((int)qos->grp_nodes >= 0) {
		xstrcat(*cols, ", grp_nodes");
		xstrfmtcat(*vals, ", %u", qos->grp_nodes);
		xstrfmtcat(*extra, ", grp_nodes=%u", qos->grp_nodes);
	} else if((int)qos->grp_nodes == INFINITE) {
		xstrcat(*cols, ", grp_nodes");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_nodes=NULL");
	}

	if((int)qos->grp_submit_jobs >= 0) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrfmtcat(*vals, ", %u",
			   qos->grp_submit_jobs);
		xstrfmtcat(*extra, ", grp_submit_jobs=%u",
			   qos->grp_submit_jobs);
	} else if((int)qos->grp_submit_jobs == INFINITE) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_submit_jobs=NULL");
	}

	if((int)qos->grp_wall >= 0) {
		xstrcat(*cols, ", grp_wall");
		xstrfmtcat(*vals, ", %u", qos->grp_wall);
		xstrfmtcat(*extra, ", grp_wall=%u",
			   qos->grp_wall);
	} else if((int)qos->grp_wall == INFINITE) {
		xstrcat(*cols, ", grp_wall");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_wall=NULL");
	}

	if((int)qos->max_cpu_mins_pj >= 0) {
		xstrcat(*cols, ", max_cpu_mins_per_job");
		xstrfmtcat(*vals, ", %llu", qos->max_cpu_mins_pj);
		xstrfmtcat(*extra, ", max_cpu_mins_per_job=%u",
			   qos->max_cpu_mins_pj);
	} else if((int)qos->max_cpu_mins_pj == INFINITE) {
		xstrcat(*cols, ", max_cpu_mins_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpu_mins_per_job=NULL");
	}

	if((int)qos->max_cpus_pj >= 0) {
		xstrcat(*cols, ", max_cpus_per_job");
		xstrfmtcat(*vals, ", %u", qos->max_cpus_pj);
		xstrfmtcat(*extra, ", max_cpus_per_job=%u",
			   qos->max_cpus_pj);
	} else if((int)qos->max_cpus_pj == INFINITE) {
		xstrcat(*cols, ", max_cpus_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpus_per_job=NULL");
	}

	if((int)qos->max_jobs_pu >= 0) {
		xstrcat(*cols, ", max_jobs_per_user");
		xstrfmtcat(*vals, ", %u", qos->max_jobs_pu);
		xstrfmtcat(*extra, ", max_jobs_per_user=%u",
			   qos->max_jobs_pu);
	} else if((int)qos->max_jobs_pu == INFINITE) {
		xstrcat(*cols, ", max_jobs_per_user");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_jobs_per_user=NULL");
	}

	if((int)qos->max_nodes_pj >= 0) {
		xstrcat(*cols, ", max_nodes_per_job");
		xstrfmtcat(*vals, ", %u", qos->max_nodes_pj);
		xstrfmtcat(*extra, ", max_nodes_per_job=%u",
			   qos->max_nodes_pj);
	} else if((int)qos->max_nodes_pj == INFINITE) {
		xstrcat(*cols, ", max_nodes_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_nodes_per_job=NULL");
	}

	if((int)qos->max_submit_jobs_pu >= 0) {
		xstrcat(*cols, ", max_submit_jobs_per_user");
		xstrfmtcat(*vals, ", %u", qos->max_submit_jobs_pu);
		xstrfmtcat(*extra, ", max_submit_jobs_per_user=%u",
			   qos->max_submit_jobs_pu);
	} else if((int)qos->max_submit_jobs_pu == INFINITE) {
		xstrcat(*cols, ", max_submit_jobs_per_user");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_submit_jobs_per_user=NULL");
	}

	if((int)qos->max_wall_pj >= 0) {
		xstrcat(*cols, ", max_wall_duration_per_job");
		xstrfmtcat(*vals, ", %u", qos->max_wall_pj);
		xstrfmtcat(*extra, ", max_wall_duration_per_job=%u",
			   qos->max_wall_pj);
	} else if((int)qos->max_wall_pj == INFINITE) {
		xstrcat(*cols, ", max_wall_duration_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_wall_duration_per_job=NULL");
	}

	if(qos->preempt_list && list_count(qos->preempt_list)) {
		char *preempt_val = NULL;
		char *tmp_char = NULL, *begin_preempt = NULL;
		ListIterator preempt_itr =
			list_iterator_create(qos->preempt_list);

		xstrcat(*cols, ", preempt");

		begin_preempt = xstrdup("preempt");

		while((tmp_char = list_next(preempt_itr))) {
			if(tmp_char[0] == '-') {
				xstrfmtcat(preempt_val,
					   "replace(%s, ',%s', '')",
					   begin_preempt, tmp_char+1);
				xfree(begin_preempt);
				begin_preempt = preempt_val;
			} else if(tmp_char[0] == '+') {
				xstrfmtcat(preempt_val,
					   "concat("
					   "replace(%s, ',%s', ''), ',%s')",
					   begin_preempt,
					   tmp_char+1, tmp_char+1);
				if(added_preempt)
					xstrfmtcat(*added_preempt, ",%s",
						   tmp_char+1);
				xfree(begin_preempt);
				begin_preempt = preempt_val;
			} else if(tmp_char[0]) {
				xstrfmtcat(preempt_val, ",%s", tmp_char);
				if(added_preempt)
					xstrfmtcat(*added_preempt, ",%s",
						   tmp_char);
			} else
				xstrcat(preempt_val, "");
		}
		list_iterator_destroy(preempt_itr);

		xstrfmtcat(*vals, ", \"%s\"", preempt_val);
		xstrfmtcat(*extra, ", preempt=\"%s\"", preempt_val);
		xfree(preempt_val);
	}

	if((int)qos->usage_factor >= 0) {
		xstrcat(*cols, ", usage_factor");
		xstrfmtcat(*vals, ", %f", qos->usage_factor);
		xstrfmtcat(*extra, ", usage_factor=%f", qos->usage_factor);
	} else if((int)qos->usage_factor == INFINITE) {
		xstrcat(*cols, ", usage_factor");
		xstrcat(*vals, ", 1");
		xstrcat(*extra, ", usage_factor=1");
	}

	return SLURM_SUCCESS;

}

static int _setup_resv_limits(acct_reservation_rec_t *resv,
			      char **cols, char **vals,
			      char **extra)
{
	/* strip off the action item from the flags */

	if(resv->assocs) {
		int start = 0;
		int len = strlen(resv->assocs)-1;

		/* strip off extra ,'s */
		if(resv->assocs[0] == ',')
			start = 1;
		if(resv->assocs[len] == ',')
			resv->assocs[len] = '\0';

		xstrcat(*cols, ", assoclist");
		xstrfmtcat(*vals, ", \"%s\"", resv->assocs+start);
		xstrfmtcat(*extra, ", assoclist=\"%s\"", resv->assocs+start);
	}

	if(resv->cpus != (uint32_t)NO_VAL) {
		xstrcat(*cols, ", cpus");
		xstrfmtcat(*vals, ", %u", resv->cpus);
		xstrfmtcat(*extra, ", cpus=%u", resv->cpus);
	}

	if(resv->flags != (uint16_t)NO_VAL) {
		xstrcat(*cols, ", flags");
		xstrfmtcat(*vals, ", %u", resv->flags);
		xstrfmtcat(*extra, ", flags=%u", resv->flags);
	}

	if(resv->name) {
		xstrcat(*cols, ", name");
		xstrfmtcat(*vals, ", \"%s\"", resv->name);
		xstrfmtcat(*extra, ", name=\"%s\"", resv->name);
	}

	if(resv->nodes) {
		xstrcat(*cols, ", nodelist");
		xstrfmtcat(*vals, ", \"%s\"", resv->nodes);
		xstrfmtcat(*extra, ", nodelist=\"%s\"", resv->nodes);
	}

	if(resv->node_inx) {
		xstrcat(*cols, ", node_inx");
		xstrfmtcat(*vals, ", \"%s\"", resv->node_inx);
		xstrfmtcat(*extra, ", node_inx=\"%s\"", resv->node_inx);
	}

	if(resv->time_end) {
		xstrcat(*cols, ", end");
		xstrfmtcat(*vals, ", %u", resv->time_end);
		xstrfmtcat(*extra, ", end=%u", resv->time_end);
	}

	if(resv->time_start) {
		xstrcat(*cols, ", start");
		xstrfmtcat(*vals, ", %u", resv->time_start);
		xstrfmtcat(*extra, ", start=%u", resv->time_start);
	}


	return SLURM_SUCCESS;
}
static int _setup_resv_cond_limits(acct_reservation_cond_t *resv_cond,
				   char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	char *prefix = "t1";
	time_t now = time(NULL);

	if(!resv_cond)
		return 0;

	if(resv_cond->cluster_list && list_count(resv_cond->cluster_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(resv_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.cluster=\"%s\"", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(resv_cond->id_list && list_count(resv_cond->id_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(resv_cond->id_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.id=%s", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(resv_cond->name_list && list_count(resv_cond->name_list)) {
		set = 0;
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		itr = list_iterator_create(resv_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.name=\"%s\"", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if(resv_cond->time_start) {
		if(!resv_cond->time_end)
			resv_cond->time_end = now;

		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		xstrfmtcat(*extra,
			   "(t1.start < %d "
			   "&& (t1.end >= %d || t1.end = 0)))",
			   resv_cond->time_end, resv_cond->time_start);
	} else if(resv_cond->time_end) {
		if(*extra)
			xstrcat(*extra, " && (");
		else
			xstrcat(*extra, " where (");
		xstrfmtcat(*extra,
			   "(t1.start < %d))", resv_cond->time_end);
	}


	return set;
}

/* Let me know if the last statement had rows that were affected.
 */
extern int last_affected_rows(MYSQL *mysql_db)
{
	int status=0, rows=0;
	MYSQL_RES *result = NULL;

	do {
		result = mysql_store_result(mysql_db);
		if (result)
			mysql_free_result(result);
		else
			if (mysql_field_count(mysql_db) == 0) {
				status = mysql_affected_rows(mysql_db);
				if(status > 0)
					rows = status;
			}
		if ((status = mysql_next_result(mysql_db)) > 0)
			debug3("Could not execute statement\n");
	} while (status == 0);

	return rows;
}

/* this function is here to see if any of what we are trying to remove
 * has jobs that are or were once running.  So if we have jobs and the
 * object is less than a day old we don't want to delete it only set
 * the deleted flag.
 */
static bool _check_jobs_before_remove(mysql_conn_t *mysql_conn,
				      char *assoc_char)
{
	char *query = NULL;
	bool rc = 0;
	MYSQL_RES *result = NULL;

	query = xstrdup_printf("select t0.associd from %s as t0, %s as t1, "
			       "%s as t2 where t1.lft between "
			       "t2.lft and t2.rgt && (%s) "
			       "and t0.associd=t1.id limit 1;",
			       job_table, assoc_table, assoc_table,
			       assoc_char);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if(mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	mysql_free_result(result);
	return rc;
}

/* Same as above but for associations instead of other tables */
static bool _check_jobs_before_remove_assoc(mysql_conn_t *mysql_conn,
					    char *assoc_char)
{
	char *query = NULL;
	bool rc = 0;
	MYSQL_RES *result = NULL;

	query = xstrdup_printf("select t1.associd from %s as t1, "
			       "%s as t2 where (%s) "
			       "and t1.associd=t2.id limit 1;",
			       job_table, assoc_table,
			       assoc_char);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if(mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	mysql_free_result(result);
	return rc;
}

/* Same as above but for things having nothing to do with associations
 * like qos or wckey */
static bool _check_jobs_before_remove_without_assoctable(
	mysql_conn_t *mysql_conn, char *where_char)
{
	char *query = NULL;
	bool rc = 0;
	MYSQL_RES *result = NULL;

	query = xstrdup_printf("select associd from %s where (%s) limit 1;",
			       job_table, where_char);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return rc;
	}
	xfree(query);

	if(mysql_num_rows(result)) {
		debug4("We have jobs for this combo");
		rc = true;
	}

	mysql_free_result(result);
	return rc;
}

/* This is called by most modify functions to alter the table and
 * insert a new line in the transaction table.
 */
extern int modify_common(mysql_conn_t *mysql_conn,
			 uint16_t type,
			 time_t now,
			 char *user_name,
			 char *table,
			 char *cond_char,
			 char *vals)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	char *tmp_cond_char = fix_double_quotes(cond_char);
	char *tmp_vals = NULL;

	if(vals[1])
		tmp_vals = fix_double_quotes(vals+2);

	xstrfmtcat(query,
		   "update %s set mod_time=%d%s "
		   "where deleted=0 && %s;",
		   table, now, vals,
		   cond_char);
	xstrfmtcat(query,
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%d, %d, \"%s\", \"%s\", \"%s\");",
		   txn_table,
		   now, type, tmp_cond_char, user_name, tmp_vals);
	xfree(tmp_cond_char);
	xfree(tmp_vals);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);

		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/* Every option in assoc_char should have a 't1.' infront of it. */
extern int remove_common(mysql_conn_t *mysql_conn,
			 uint16_t type,
			 time_t now,
			 char *user_name,
			 char *table,
			 char *name_char,
			 char *assoc_char)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	char *loc_assoc_char = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	time_t day_old = now - DELETE_SEC_BACK;
	bool has_jobs = false;
	char *tmp_name_char = fix_double_quotes(name_char);

	/* If we have jobs associated with this we do not want to
	 * really delete it for accounting purposes.  This is for
	 * corner cases most of the time this won't matter.
	 */
	if(table == acct_coord_table) {
		/* This doesn't apply for these tables since we are
		 * only looking for association type tables.
		 */
	} else if((table == qos_table) || (table == wckey_table)) {
		has_jobs = _check_jobs_before_remove_without_assoctable(
			mysql_conn, assoc_char);
	} else if(table != assoc_table) {
		has_jobs = _check_jobs_before_remove(mysql_conn, assoc_char);
	} else {
		has_jobs = _check_jobs_before_remove_assoc(mysql_conn,
							   name_char);
	}
	/* we want to remove completely all that is less than a day old */
	if(!has_jobs && table != assoc_table) {
		query = xstrdup_printf("delete from %s where creation_time>%d "
				       "&& (%s);"
				       "alter table %s AUTO_INCREMENT=0;",
				       table, day_old, name_char, table);
	}

	if(table != assoc_table)
		xstrfmtcat(query,
			   "update %s set mod_time=%d, deleted=1 "
			   "where deleted=0 && (%s);",
			   table, now, name_char);

	xstrfmtcat(query,
		   "insert into %s (timestamp, action, name, actor) "
		   "values (%d, %d, \"%s\", \"%s\");",
		   txn_table,
		   now, type, tmp_name_char, user_name);
	xfree(tmp_name_char);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);

		return SLURM_ERROR;
	} else if((table == acct_coord_table)
		  || (table == qos_table)
		  || (table == wckey_table))
		return SLURM_SUCCESS;

	/* mark deleted=1 or remove completely the accounting tables
	*/
	if(table != assoc_table) {
		if(!assoc_char) {
			error("no assoc_char");
			if(mysql_conn->rollback) {
				mysql_db_rollback(mysql_conn->db_conn);
			}
			list_flush(mysql_conn->update_list);
			return SLURM_ERROR;
		}

		/* If we are doing this on an assoc_table we have
		   already done this, so don't */
/* 		query = xstrdup_printf("select lft, rgt " */
/* 				       "from %s as t2 where %s order by lft;", */
/* 				       assoc_table, assoc_char); */
		query = xstrdup_printf("select distinct t1.id "
				       "from %s as t1, %s as t2 "
				       "where (%s) && t1.lft between "
				       "t2.lft and t2.rgt && t1.deleted=0 "
				       " && t2.deleted=0;",
				       assoc_table, assoc_table, assoc_char);

		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			if(mysql_conn->rollback) {
				mysql_db_rollback(mysql_conn->db_conn);
			}
			list_flush(mysql_conn->update_list);
			return SLURM_ERROR;
		}
		xfree(query);

		rc = 0;
		loc_assoc_char = NULL;
		while((row = mysql_fetch_row(result))) {
			acct_association_rec_t *rem_assoc = NULL;
			if(!rc) {
				xstrfmtcat(loc_assoc_char, "id=%s", row[0]);
				rc = 1;
			} else {
				xstrfmtcat(loc_assoc_char,
					   " || id=%s", row[0]);
			}
			rem_assoc = xmalloc(sizeof(acct_association_rec_t));
			rem_assoc->id = atoi(row[0]);
			if(addto_update_list(mysql_conn->update_list,
					      ACCT_REMOVE_ASSOC,
					      rem_assoc) != SLURM_SUCCESS)
				error("couldn't add to the update list");
		}
		mysql_free_result(result);
	} else
		loc_assoc_char = assoc_char;

	if(!loc_assoc_char) {
		debug2("No associations with object being deleted\n");
		return rc;
	}

	/* We should not have to delete from usage table, only flag since we
	 * only delete things that are typos.
	 */
	xstrfmtcat(query,
		   "update %s set mod_time=%d, deleted=1 where (%s);"
		   "update %s set mod_time=%d, deleted=1 where (%s);"
		   "update %s set mod_time=%d, deleted=1 where (%s);",
		   assoc_day_table, now, loc_assoc_char,
		   assoc_hour_table, now, loc_assoc_char,
		   assoc_month_table, now, loc_assoc_char);

	debug3("%d(%d) query\n%s %d",
	       mysql_conn->conn, __LINE__, query, strlen(query));
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		return SLURM_ERROR;
	}

	/* If we have jobs that have ran don't go through the logic of
	 * removing the associations. Since we may want them for
	 * reports in the future since jobs had ran.
	 */
	if(has_jobs)
		goto just_update;

	/* remove completely all the associations for this added in the last
	 * day, since they are most likely nothing we really wanted in
	 * the first place.
	 */
	query = xstrdup_printf("select id from %s as t1 where "
			       "creation_time>%d && (%s);",
			       assoc_table, day_old, loc_assoc_char);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		return SLURM_ERROR;
	}
	xfree(query);

	while((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		MYSQL_ROW row2;

		/* we have to do this one at a time since the lft's and rgt's
		   change. If you think you need to remove this make
		   sure your new way can handle changing lft and rgt's
		   in the association. */
		xstrfmtcat(query,
			   "SELECT lft, rgt, (rgt - lft + 1) "
			   "FROM %s WHERE id = %s;",
			   assoc_table, row[0]);
		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		if(!(result2 = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			rc = SLURM_ERROR;
			break;
		}
		xfree(query);
		if(!(row2 = mysql_fetch_row(result2))) {
			mysql_free_result(result2);
			continue;
		}

		xstrfmtcat(query,
			   "delete quick from %s where lft between %s AND %s;",
			   assoc_table, row2[0], row2[1]);

		xstrfmtcat(query,
			   "UPDATE %s SET rgt = rgt - %s WHERE rgt > %s;"
			   "UPDATE %s SET lft = lft - %s WHERE lft > %s;",
			   assoc_table, row2[2], row2[1],
			   assoc_table, row2[2], row2[1]);

		mysql_free_result(result2);

		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("couldn't remove assoc");
			break;
		}
	}
	mysql_free_result(result);
	if(rc == SLURM_ERROR) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		return rc;
	}

just_update:
	/* now update the associations themselves that are still
	 * around clearing all the limits since if we add them back
	 * we don't want any residue from past associations lingering
	 * around.
	 */
	query = xstrdup_printf("update %s as t1 set mod_time=%d, deleted=1, "
			       "fairshare=1, max_jobs=NULL, "
			       "max_nodes_per_job=NULL, "
			       "max_wall_duration_per_job=NULL, "
			       "max_cpu_mins_per_job=NULL "
			       "where (%s);"
			       "alter table %s AUTO_INCREMENT=0;",
			       assoc_table, now,
			       loc_assoc_char,
			       assoc_table);

	if(table != assoc_table)
		xfree(loc_assoc_char);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
	}

	return rc;
}

/* Fill in all the users that are coordinator for this account.  This
 * will fill in if there are coordinators from a parent account also.
 */
static int _get_account_coords(mysql_conn_t *mysql_conn,
			       acct_account_rec_t *acct)
{
	char *query = NULL;
	acct_coord_rec_t *coord = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!acct) {
		error("We need a account to fill in.");
		return SLURM_ERROR;
	}

	if(!acct->coordinators)
		acct->coordinators = list_create(destroy_acct_coord_rec);

	query = xstrdup_printf(
		"select user from %s where acct=\"%s\" && deleted=0",
		acct_coord_table, acct->name);

	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	while((row = mysql_fetch_row(result))) {
		coord = xmalloc(sizeof(acct_coord_rec_t));
		list_append(acct->coordinators, coord);
		coord->name = xstrdup(row[0]);
		coord->direct = 1;
	}
	mysql_free_result(result);

	query = xstrdup_printf("select distinct t0.user from %s as t0, "
			       "%s as t1, %s as t2 where t0.acct=t1.acct && "
			       "t1.lft<t2.lft && t1.rgt>t2.lft && "
			       "t1.user='' && t2.acct=\"%s\" "
			       "&& t1.acct!=\"%s\" && !t0.deleted;",
			       acct_coord_table, assoc_table, assoc_table,
			       acct->name, acct->name);
	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	while((row = mysql_fetch_row(result))) {
		coord = xmalloc(sizeof(acct_coord_rec_t));
		list_append(acct->coordinators, coord);
		coord->name = xstrdup(row[0]);
		coord->direct = 0;
	}
	return SLURM_SUCCESS;
}

/* Fill in all the accounts this user is coordinator over.  This
 * will fill in all the sub accounts they are coordinator over also.
 */
static int _get_user_coords(mysql_conn_t *mysql_conn, acct_user_rec_t *user)
{
	char *query = NULL;
	acct_coord_rec_t *coord = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	ListIterator itr = NULL;

	if(!user) {
		error("We need a user to fill in.");
		return SLURM_ERROR;
	}

	if(!user->coord_accts)
		user->coord_accts = list_create(destroy_acct_coord_rec);

	query = xstrdup_printf(
		"select acct from %s where user=\"%s\" && deleted=0",
		acct_coord_table, user->name);

	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	while((row = mysql_fetch_row(result))) {
		coord = xmalloc(sizeof(acct_coord_rec_t));
		list_append(user->coord_accts, coord);
		coord->name = xstrdup(row[0]);
		coord->direct = 1;
		if(query)
			xstrcat(query, " || ");
		else
			query = xstrdup_printf(
				"select distinct t1.acct from "
				"%s as t1, %s as t2 where t1.deleted=0 && ",
				assoc_table, assoc_table);
		/* Make sure we don't get the same
		 * account back since we want to keep
		 * track of the sub-accounts.
		 */
		xstrfmtcat(query, "(t2.acct=\"%s\" "
			   "&& t1.lft between t2.lft "
			   "and t2.rgt && t1.user='' "
			   "&& t1.acct!=\"%s\")",
			   coord->name, coord->name);
	}
	mysql_free_result(result);

	if(query) {
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);

		itr = list_iterator_create(user->coord_accts);
		while((row = mysql_fetch_row(result))) {

			while((coord = list_next(itr))) {
				if(!strcmp(coord->name, row[0]))
					break;
			}
			list_iterator_reset(itr);
			if(coord)
				continue;

			coord = xmalloc(sizeof(acct_coord_rec_t));
			list_append(user->coord_accts, coord);
			coord->name = xstrdup(row[0]);
			coord->direct = 0;
		}
		list_iterator_destroy(itr);
		mysql_free_result(result);
	}
	return SLURM_SUCCESS;
}

/* Used in job functions for getting the database index based off the
 * submit time, job and assoc id.  0 is returned if none is found
 */
static int _get_db_index(MYSQL *db_conn,
			 time_t submit, uint32_t jobid, uint32_t associd)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int db_index = 0;
	char *query = xstrdup_printf("select id from %s where "
				     "submit=%d and jobid=%u and associd=%u",
				     job_table, (int)submit, jobid, associd);

	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		return 0;
	}
	xfree(query);

	row = mysql_fetch_row(result);
	if(!row) {
		mysql_free_result(result);
		error("We can't get a db_index for this combo, "
		      "submit=%d and jobid=%u and associd=%u.",
		      (int)submit, jobid, associd);
		return 0;
	}
	db_index = atoi(row[0]);
	mysql_free_result(result);

	return db_index;
}

static mysql_db_info_t *_mysql_acct_create_db_info()
{
	mysql_db_info_t *db_info = xmalloc(sizeof(mysql_db_info_t));
	db_info->port = slurm_get_accounting_storage_port();
	if(!db_info->port) {
		db_info->port = DEFAULT_MYSQL_PORT;
		slurm_set_accounting_storage_port(db_info->port);
	}
	db_info->host = slurm_get_accounting_storage_host();
	db_info->backup = slurm_get_accounting_storage_backup_host();

	db_info->user = slurm_get_accounting_storage_user();
	db_info->pass = slurm_get_accounting_storage_pass();
	return db_info;
}

/* Any time a new table is added set it up here */
static int _mysql_acct_check_tables(MYSQL *db_conn)
{
	int rc = SLURM_SUCCESS;
	storage_field_t acct_coord_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
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
		{ "parent_acct", "tinytext not null default ''" },
		{ "lft", "int not null" },
		{ "rgt", "int not null" },
		{ "fairshare", "int default 1 not null" },
		{ "max_jobs", "int default NULL" },
		{ "max_submit_jobs", "int default NULL" },
		{ "max_cpus_per_job", "int default NULL" },
		{ "max_nodes_per_job", "int default NULL" },
		{ "max_wall_duration_per_job", "int default NULL" },
		{ "max_cpu_mins_per_job", "bigint default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_cpus", "int default NULL" },
		{ "grp_nodes", "int default NULL" },
		{ "grp_wall", "int default NULL" },
		{ "grp_cpu_mins", "bigint default NULL" },
		{ "qos", "blob not null default ''" },
		{ "delta_qos", "blob not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t assoc_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null" },
		{ "period_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	storage_field_t cluster_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "control_host", "tinytext not null default ''" },
		{ "control_port", "int unsigned not null default 0" },
		{ "rpc_version", "smallint unsigned not null default 0" },
		{ "classification", "smallint unsigned default 0" },
		{ NULL, NULL}
	};

	storage_field_t cluster_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "cluster", "tinytext not null" },
		{ "period_start", "int unsigned not null" },
		{ "cpu_count", "int default 0" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "down_cpu_secs", "bigint default 0" },
		{ "pdown_cpu_secs", "bigint default 0" },
		{ "idle_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	storage_field_t event_table_fields[] = {
		{ "node_name", "tinytext default '' not null" },
		{ "cluster", "tinytext not null" },
		{ "cpu_count", "int not null" },
		{ "state", "smallint unsigned default 0 not null" },
		{ "period_start", "int unsigned not null" },
		{ "period_end", "int unsigned default 0 not null" },
		{ "reason", "tinytext not null" },
		{ "reason_uid", "int unsigned default 0xfffffffe not null" },
		{ "cluster_nodes", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t job_table_fields[] = {
		{ "id", "int not null auto_increment" },
		{ "deleted", "tinyint default 0" },
		{ "jobid", "int unsigned not null" },
		{ "associd", "int unsigned not null" },
		{ "wckey", "tinytext not null default ''" },
		{ "wckeyid", "int unsigned not null" },
		{ "uid", "int unsigned not null" },
		{ "gid", "int unsigned not null" },
		{ "cluster", "tinytext not null" },
		{ "partition", "tinytext not null" },
		{ "blockid", "tinytext" },
		{ "account", "tinytext" },
		{ "eligible", "int unsigned default 0 not null" },
		{ "submit", "int unsigned default 0 not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "timelimit", "int unsigned default 0 not null" },
		{ "name", "tinytext not null" },
		{ "track_steps", "tinyint not null" },
		{ "state", "smallint unsigned not null" },
		{ "comp_code", "int default 0 not null" },
		{ "priority", "int not null" },
		{ "req_cpus", "int unsigned not null" },
		{ "alloc_cpus", "int unsigned not null" },
		{ "alloc_nodes", "int unsigned not null" },
		{ "nodelist", "text" },
		{ "node_inx", "text" },
		{ "kill_requid", "int default -1 not null" },
		{ "qos", "smallint default 0" },
		{ "resvid", "int unsigned not null" },
		{ NULL, NULL}
	};

	storage_field_t last_ran_table_fields[] = {
		{ "hourly_rollup", "int unsigned default 0 not null" },
		{ "daily_rollup", "int unsigned default 0 not null" },
		{ "monthly_rollup", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t qos_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "name", "tinytext not null" },
		{ "description", "text" },
		{ "max_jobs_per_user", "int default NULL" },
		{ "max_submit_jobs_per_user", "int default NULL" },
		{ "max_cpus_per_job", "int default NULL" },
		{ "max_nodes_per_job", "int default NULL" },
		{ "max_wall_duration_per_job", "int default NULL" },
		{ "max_cpu_mins_per_job", "bigint default NULL" },
		{ "grp_jobs", "int default NULL" },
		{ "grp_submit_jobs", "int default NULL" },
		{ "grp_cpus", "int default NULL" },
		{ "grp_nodes", "int default NULL" },
		{ "grp_wall", "int default NULL" },
		{ "grp_cpu_mins", "bigint default NULL" },
		{ "preempt", "text not null default ''" },
		{ "priority", "int default 0" },
		{ "usage_factor", "double default 1.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t resv_table_fields[] = {
		{ "id", "int unsigned default 0 not null" },
		{ "name", "text not null" },
		{ "cluster", "text not null" },
		{ "deleted", "tinyint default 0" },
		{ "cpus", "int unsigned not null" },
		{ "assoclist", "text not null default ''" },
		{ "nodelist", "text not null default ''" },
		{ "node_inx", "text not null default ''" },
		{ "start", "int unsigned default 0 not null"},
		{ "end", "int unsigned default 0 not null" },
		{ "flags", "smallint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields[] = {
		{ "id", "int not null" },
		{ "deleted", "tinyint default 0" },
		{ "stepid", "smallint not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "name", "text not null" },
		{ "nodelist", "text not null" },
		{ "node_inx", "text" },
		{ "state", "smallint unsigned not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "comp_code", "int default 0 not null" },
		{ "nodes", "int unsigned not null" },
		{ "cpus", "int unsigned not null" },
		{ "tasks", "int unsigned not null" },
		{ "task_dist", "smallint default 0" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "max_vsize", "bigint unsigned default 0 not null" },
		{ "max_vsize_task", "smallint unsigned default 0 not null" },
		{ "max_vsize_node", "int unsigned default 0 not null" },
		{ "ave_vsize", "double unsigned default 0.0 not null" },
		{ "max_rss", "bigint unsigned default 0 not null" },
		{ "max_rss_task", "smallint unsigned default 0 not null" },
		{ "max_rss_node", "int unsigned default 0 not null" },
		{ "ave_rss", "double unsigned default 0.0 not null" },
		{ "max_pages", "int unsigned default 0 not null" },
		{ "max_pages_task", "smallint unsigned default 0 not null" },
		{ "max_pages_node", "int unsigned default 0 not null" },
		{ "ave_pages", "double unsigned default 0.0 not null" },
		{ "min_cpu", "int unsigned default 0 not null" },
		{ "min_cpu_task", "smallint unsigned default 0 not null" },
		{ "min_cpu_node", "int unsigned default 0 not null" },
		{ "ave_cpu", "double unsigned default 0.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t suspend_table_fields[] = {
		{ "id", "int not null" },
		{ "associd", "int not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t txn_table_fields[] = {
		{ "id", "int not null auto_increment" },
		{ "timestamp", "int unsigned default 0 not null" },
		{ "action", "smallint not null" },
		{ "name", "text not null" },
		{ "actor", "tinytext not null" },
		{ "info", "blob" },
		{ NULL, NULL}
	};

	storage_field_t user_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "name", "tinytext not null" },
		{ "default_acct", "tinytext not null" },
		{ "default_wckey", "tinytext not null default ''" },
		{ "admin_level", "smallint default 1 not null" },
		{ NULL, NULL}
	};

	storage_field_t wckey_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null auto_increment" },
		{ "name", "tinytext not null default ''" },
		{ "cluster", "tinytext not null" },
		{ "user", "tinytext not null" },
		{ NULL, NULL}
	};

	storage_field_t wckey_usage_table_fields[] = {
		{ "creation_time", "int unsigned not null" },
		{ "mod_time", "int unsigned default 0 not null" },
		{ "deleted", "tinyint default 0" },
		{ "id", "int not null" },
		{ "period_start", "int unsigned not null" },
		{ "alloc_cpu_secs", "bigint default 0" },
		{ "resv_cpu_secs", "bigint default 0" },
		{ "over_cpu_secs", "bigint default 0" },
		{ NULL, NULL}
	};

	char *get_parent_proc =
		"drop procedure if exists get_parent_limits; "
		"create procedure get_parent_limits("
		"my_table text, acct text, cluster text, without_limits int) "
		"begin "
		"set @par_id = NULL; "
		"set @mj = NULL; "
		"set @msj = NULL; "
		"set @mcpj = NULL; "
		"set @mnpj = NULL; "
		"set @mwpj = NULL; "
		"set @mcmpj = NULL; "
		"set @qos = ''; "
		"set @delta_qos = ''; "
		"set @my_acct = acct; "
		"if without_limits then "
		"set @mj = 0; "
		"set @msj = 0; "
		"set @mcpj = 0; "
		"set @mnpj = 0; "
		"set @mwpj = 0; "
		"set @mcmpj = 0; "
		"set @qos = 0; "
		"set @delta_qos = 0; "
		"end if; "
		"REPEAT "
		"set @s = 'select '; "
		"if @par_id is NULL then set @s = CONCAT("
		"@s, '@par_id := id, '); "
		"end if; "
		"if @mj is NULL then set @s = CONCAT("
		"@s, '@mj := max_jobs, '); "
		"end if; "
		"if @msj is NULL then set @s = CONCAT("
		"@s, '@msj := max_submit_jobs, '); "
		"end if; "
		"if @mcpj is NULL then set @s = CONCAT("
		"@s, '@mcpj := max_cpus_per_job, ') ;"
		"end if; "
		"if @mnpj is NULL then set @s = CONCAT("
		"@s, '@mnpj := max_nodes_per_job, ') ;"
		"end if; "
		"if @mwpj is NULL then set @s = CONCAT("
		"@s, '@mwpj := max_wall_duration_per_job, '); "
		"end if; "
		"if @mcmpj is NULL then set @s = CONCAT("
		"@s, '@mcmpj := max_cpu_mins_per_job, '); "
		"end if; "
		"if @qos = '' then set @s = CONCAT("
		"@s, '@qos := qos, "
		"@delta_qos := CONCAT(delta_qos, @delta_qos), '); "
		"end if; "
		"set @s = concat(@s, ' @my_acct := parent_acct from ', "
		"my_table, ' where acct = \"', @my_acct, '\" && "
		"cluster = \"', cluster, '\" && user=\"\"'); "
		"prepare query from @s; "
		"execute query; "
		"deallocate prepare query; "
		"UNTIL (@mj != -1 && @msj != -1 && @mcpj != -1 "
		"&& @mnpj != -1 && @mwpj != -1 "
		"&& @mcmpj != -1 && @qos != '') || @my_acct = '' END REPEAT; "
		"END;";
	char *query = NULL;
	time_t now = time(NULL);

	if(mysql_db_create_table(db_conn, acct_coord_table,
				 acct_coord_table_fields,
				 ", primary key (acct(20), user(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, acct_table, acct_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, assoc_day_table,
				 assoc_usage_table_fields,
				 ", primary key (id, period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, assoc_hour_table,
				 assoc_usage_table_fields,
				 ", primary key (id, period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, assoc_month_table,
				 assoc_usage_table_fields,
				 ", primary key (id, period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, assoc_table, assoc_table_fields,
				 ", primary key (id), "
				 " unique index (user(20), acct(20), "
				 "cluster(20), partition(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, cluster_day_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster(20), period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, cluster_hour_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster(20), period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, cluster_month_table,
				 cluster_usage_table_fields,
				 ", primary key (cluster(21), period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, cluster_table,
				 cluster_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, event_table,
				 event_table_fields,
				 ", primary key (node_name(20), cluster(20), "
				 "period_start))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, job_table, job_table_fields,
				 ", primary key (id), "
				 "unique index (jobid, associd, submit))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, last_ran_table,
				 last_ran_table_fields,
				 ")") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, qos_table,
				 qos_table_fields,
				 ", primary key (id), "
				 "unique index (name(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;
	else {
		int qos_id = 0;
		if(slurmdbd_conf && slurmdbd_conf->default_qos) {
			List char_list = list_create(slurm_destroy_char);
			char *qos = NULL;
			ListIterator itr = NULL;
			slurm_addto_char_list(char_list,
					      slurmdbd_conf->default_qos);
			/* NOTE: you can not use list_pop, or list_push
			   anywhere either, since mysql is
			   exporting something of the same type as a macro,
			   which messes everything up
			   (my_list.h is the bad boy).
			*/
			itr = list_iterator_create(char_list);
			while((qos = list_next(itr))) {
				query = xstrdup_printf(
					"insert into %s "
					"(creation_time, mod_time, name, "
					"description) "
					"values (%d, %d, '%s', "
					"'Added as default') "
					"on duplicate key update "
					"id=LAST_INSERT_ID(id), deleted=0;",
					qos_table, now, now, qos);
				qos_id = mysql_insert_ret_id(db_conn, query);
				if(!qos_id)
					fatal("problem added qos '%s", qos);
				xstrfmtcat(default_qos_str, ",%d", qos_id);
				xfree(query);
			}
			list_iterator_destroy(itr);
			list_destroy(char_list);
		} else {
			query = xstrdup_printf(
				"insert into %s "
				"(creation_time, mod_time, name, description) "
				"values (%d, %d, 'normal', "
				"'Normal QOS default') "
				"on duplicate key update "
				"id=LAST_INSERT_ID(id), deleted=0;",
				qos_table, now, now);
			//debug3("%s", query);
			qos_id = mysql_insert_ret_id(db_conn, query);
			if(!qos_id)
				fatal("problem added qos 'normal");

			xstrfmtcat(default_qos_str, ",%d", qos_id);
			xfree(query);
		}

		if(_set_qos_cnt(db_conn) != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	if(mysql_db_create_table(db_conn, step_table,
				 step_table_fields,
				 ", primary key (id, stepid))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, resv_table,
				 resv_table_fields,
				 ", primary key (id, start, cluster(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, suspend_table,
				 suspend_table_fields,
				 ")") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, txn_table, txn_table_fields,
				 ", primary key (id))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, user_table, user_table_fields,
				 ", primary key (name(20)))") == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, wckey_table, wckey_table_fields,
				 ", primary key (id), "
				 " unique index (name(20), user(20), "
				 "cluster(20)))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, wckey_day_table,
				 wckey_usage_table_fields,
				 ", primary key (id, period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, wckey_hour_table,
				 wckey_usage_table_fields,
				 ", primary key (id, period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	if(mysql_db_create_table(db_conn, wckey_month_table,
				 wckey_usage_table_fields,
				 ", primary key (id, period_start))")
	   == SLURM_ERROR)
		return SLURM_ERROR;

	rc = mysql_db_query(db_conn, get_parent_proc);

	/* Add user root to be a user by default and have this default
	 * account be root.  If already there just update
	 * name='root'.  That way if the admins delete it it will
	 * remained deleted. Creation time will be 0 so it will never
	 * really be deleted.
	 */
	query = xstrdup_printf(
		"insert into %s (creation_time, mod_time, name, default_acct, "
		"admin_level) values (0, %d, 'root', 'root', %u) "
		"on duplicate key update name='root';",
		user_table, now, ACCT_ADMIN_SUPER_USER, now);
	xstrfmtcat(query,
		   "insert into %s (creation_time, mod_time, name, "
		   "description, organization) values (0, %d, 'root', "
		   "'default root account', 'root') on duplicate key "
		   "update name='root';",
		   acct_table, now);

	//debug3("%s", query);
	mysql_db_query(db_conn, query);
	xfree(query);

	return rc;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
	int rc = SLURM_SUCCESS;
	MYSQL *db_conn = NULL;
	char *location = NULL;

	/* since this can be loaded from many different places
	   only tell us once. */
	if(!first)
		return SLURM_SUCCESS;

	first = 0;

	if(!slurmdbd_conf) {
		char *cluster_name = NULL;
		if (!(cluster_name = slurm_get_cluster_name()))
			fatal("%s requires ClusterName in slurm.conf",
			      plugin_name);
		xfree(cluster_name);
	}

	mysql_db_info = _mysql_acct_create_db_info();

	location = slurm_get_accounting_storage_loc();
	if(!location)
		mysql_db_name = xstrdup(DEFAULT_ACCOUNTING_DB);
	else {
		int i = 0;
		while(location[i]) {
			if(location[i] == '.' || location[i] == '/') {
				debug("%s doesn't look like a database "
				      "name using %s",
				      location, DEFAULT_ACCOUNTING_DB);
				break;
			}
			i++;
		}
		if(location[i]) {
			mysql_db_name = xstrdup(DEFAULT_ACCOUNTING_DB);
			xfree(location);
		} else
			mysql_db_name = location;
	}

	debug2("mysql_connect() called for db %s", mysql_db_name);

	if(mysql_get_db_connection(&db_conn, mysql_db_name, mysql_db_info)
	   != SLURM_SUCCESS)
		fatal("The database must be up when starting "
		      "the MYSQL plugin.");

	rc = _mysql_acct_check_tables(db_conn);

	mysql_close_db_connection(&db_conn);

	if(rc == SLURM_SUCCESS)
		verbose("%s loaded", plugin_name);
	else
		verbose("%s failed", plugin_name);

	return rc;
}

extern int fini ( void )
{
	destroy_mysql_db_info(mysql_db_info);
	xfree(mysql_db_name);
	xfree(default_qos_str);
	mysql_cleanup();
	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(bool make_agent, int conn_num,
					   bool rollback)
{
	mysql_conn_t *mysql_conn = xmalloc(sizeof(mysql_conn_t));

	if(!mysql_db_info)
		init();

	debug2("acct_storage_p_get_connection: request new connection");

	mysql_conn->rollback = rollback;
	mysql_conn->conn = conn_num;
	mysql_conn->update_list = list_create(destroy_acct_update_object);

	errno = SLURM_SUCCESS;
	mysql_get_db_connection(&mysql_conn->db_conn,
				mysql_db_name, mysql_db_info);

	if(mysql_conn->db_conn) {
		if(rollback)
			mysql_autocommit(mysql_conn->db_conn, 0);
	}

	return (void *)mysql_conn;
}

extern int acct_storage_p_close_connection(mysql_conn_t **mysql_conn)
{
	if(!mysql_conn || !(*mysql_conn))
		return SLURM_SUCCESS;

	acct_storage_p_commit((*mysql_conn), 0);
	mysql_close_db_connection(&(*mysql_conn)->db_conn);
	list_destroy((*mysql_conn)->update_list);
	xfree((*mysql_conn));

	return SLURM_SUCCESS;
}

extern int acct_storage_p_commit(mysql_conn_t *mysql_conn, bool commit)
{
	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug4("got %d commits", list_count(mysql_conn->update_list));

	if(mysql_conn->rollback) {
		if(!commit) {
			if(mysql_db_rollback(mysql_conn->db_conn))
				error("rollback failed");
		} else {
			if(mysql_db_commit(mysql_conn->db_conn))
				error("commit failed");
		}
	}

	if(commit && list_count(mysql_conn->update_list)) {
		int rc;
		char *query = NULL;
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;
		accounting_update_msg_t msg;
		slurm_msg_t req;
		slurm_msg_t resp;
		ListIterator itr = NULL;
		acct_update_object_t *object = NULL;
		bool get_qos_count = 0;

		memset(&msg, 0, sizeof(accounting_update_msg_t));
		msg.update_list = mysql_conn->update_list;

		xstrfmtcat(query, "select control_host, control_port, "
			   "name, rpc_version "
			   "from %s where deleted=0 && control_port != 0",
			   cluster_table);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			goto skip;
		}
		xfree(query);
		while((row = mysql_fetch_row(result))) {
			rc = send_accounting_update(mysql_conn->update_list,
						    row[2], row[0],
						    atoi(row[1]), atoi(row[3]));
		}
		mysql_free_result(result);
	skip:
		rc = update_assoc_mgr(mysql_conn->update_list);

		if(get_qos_count)
			_set_qos_cnt(mysql_conn->db_conn);
	}

	list_flush(mysql_conn->update_list);

	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(mysql_conn_t *mysql_conn, uint32_t uid,
				    List user_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_user_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *query = NULL, *txn_query = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *extra = NULL, *tmp_extra = NULL;
	int affect_rows = 0;
	List assoc_list = list_create(destroy_acct_association_rec);
	List wckey_list = list_create(destroy_acct_wckey_rec);

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(user_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->name[0]
		   || !object->default_acct || !object->default_acct[0]) {
			error("We need a user name and "
			      "default acct to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name, default_acct");
		xstrfmtcat(vals, "%d, %d, '%s', '%s'",
			   now, now, object->name, object->default_acct);
		xstrfmtcat(extra, ", default_acct='%s'",
			   object->default_acct);

		if(object->admin_level != ACCT_ADMIN_NOTSET) {
			xstrcat(cols, ", admin_level");
			xstrfmtcat(vals, ", %u", object->admin_level);
			xstrfmtcat(extra, ", admin_level=%u",
				   object->admin_level);
		}

		if(object->default_wckey) {
			xstrcat(cols, ", default_wckey");
			xstrfmtcat(vals, ", \"%s\"", object->default_wckey);
			xstrfmtcat(extra, ", default_wckey=\"%s\"",
				   object->default_wckey);
		}

		query = xstrdup_printf(
			"insert into %s (%s) values (%s) "
			"on duplicate key update deleted=0, mod_time=%d %s;",
			user_table, cols, vals,
			now, extra);

		xfree(cols);
		xfree(vals);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add user %s", object->name);
			xfree(extra);
			continue;
		}

		affect_rows = last_affected_rows(mysql_conn->db_conn);
		if(!affect_rows) {
			debug("nothing changed");
			xfree(extra);
			continue;
		}

		if(addto_update_list(mysql_conn->update_list, ACCT_ADD_USER,
				      object) == SLURM_SUCCESS)
			list_remove(itr);

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = fix_double_quotes(extra+2);

		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%d, %u, \"%s\", \"%s\", \"%s\")",
				   now, DBD_ADD_USERS, object->name,
				   user_name, tmp_extra);
		else
			xstrfmtcat(txn_query,
				   "insert into %s "
				   "(timestamp, action, name, actor, info) "
				   "values (%d, %u, \"%s\", \"%s\", \"%s\")",
				   txn_table,
				   now, DBD_ADD_USERS, object->name,
				   user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);

		if(object->assoc_list)
			list_transfer(assoc_list, object->assoc_list);

		if(object->wckey_list)
			list_transfer(wckey_list, object->wckey_list);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(rc != SLURM_ERROR) {
		if(txn_query) {
			xstrcat(txn_query, ";");
			rc = mysql_db_query(mysql_conn->db_conn,
					    txn_query);
			xfree(txn_query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add txn");
				rc = SLURM_SUCCESS;
			}
		}
	} else
		xfree(txn_query);

	if(list_count(assoc_list)) {
		if(mysql_add_assocs(mysql_conn, uid, assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding user associations");
			rc = SLURM_ERROR;
		}
	}
	list_destroy(assoc_list);

	if(list_count(wckey_list)) {
		if(mysql_add_wckeys(mysql_conn, uid, wckey_list)
		   == SLURM_ERROR) {
			error("Problem adding user wckeys");
			rc = SLURM_ERROR;
		}
	}
	list_destroy(wckey_list);

	return rc;
}

extern int acct_storage_p_add_coord(mysql_conn_t *mysql_conn, uint32_t uid,
				    List acct_list, acct_user_cond_t *user_cond)
{
	char *query = NULL, *user = NULL, *acct = NULL;
	char *user_name = NULL, *txn_query = NULL;
	ListIterator itr, itr2;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;
	acct_user_rec_t *user_rec = NULL;

	if(!user_cond || !user_cond->assoc_cond
	   || !user_cond->assoc_cond->user_list
	   || !list_count(user_cond->assoc_cond->user_list)
	   || !acct_list || !list_count(acct_list)) {
		error("we need something to add");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(user_cond->assoc_cond->user_list);
	itr2 = list_iterator_create(acct_list);
	while((user = list_next(itr))) {
		if(!user[0])
			continue;
		while((acct = list_next(itr2))) {
			if(!acct[0])
				continue;
			if(query)
				xstrfmtcat(query, ", (%d, %d, \"%s\", \"%s\")",
					   now, now, acct, user);
			else
				query = xstrdup_printf(
					"insert into %s (creation_time, "
					"mod_time, acct, user) values "
					"(%d, %d, \"%s\", \"%s\")",
					acct_coord_table,
					now, now, acct, user);

			if(txn_query)
				xstrfmtcat(txn_query,
					   ", (%d, %u, \"%s\", \"%s\", \"%s\")",
					   now, DBD_ADD_ACCOUNT_COORDS, user,
					   user_name, acct);
			else
				xstrfmtcat(txn_query,
					   "insert into %s "
					   "(timestamp, action, name, "
					   "actor, info) "
					   "values (%d, %u, \"%s\", "
					   "\"%s\", \"%s\")",
					   txn_table,
					   now, DBD_ADD_ACCOUNT_COORDS, user,
					   user_name, acct);
		}
		list_iterator_reset(itr2);
	}
	xfree(user_name);
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	if(query) {
		xstrfmtcat(query,
			   " on duplicate key update mod_time=%d, deleted=0;%s",
			   now, txn_query);
		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		xfree(txn_query);

		if(rc != SLURM_SUCCESS) {
			error("Couldn't add cluster hour rollup");
			return rc;
		}
		/* get the update list set */
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((user = list_next(itr))) {
			user_rec = xmalloc(sizeof(acct_user_rec_t));
			user_rec->name = xstrdup(user);
			_get_user_coords(mysql_conn, user_rec);
			addto_update_list(mysql_conn->update_list,
					   ACCT_ADD_COORD, user_rec);
		}
		list_iterator_destroy(itr);
	}

	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(mysql_conn_t *mysql_conn, uint32_t uid,
				    List acct_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_account_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *query = NULL, *txn_query = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *extra = NULL, *tmp_extra = NULL;

	int affect_rows = 0;
	List assoc_list = list_create(destroy_acct_association_rec);

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(acct_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->name[0]
		   || !object->description || !object->description[0]
		   || !object->organization || !object->organization[0]) {
			error("We need an account name, description, and "
			      "organization to add. %s %s %s",
			      object->name, object->description,
			      object->organization);
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name, "
			"description, organization");
		xstrfmtcat(vals, "%d, %d, \"%s\", \"%s\", \"%s\"",
			   now, now, object->name,
			   object->description, object->organization);
		xstrfmtcat(extra, ", description=\"%s\", organization=\"%s\"",
			   object->description, object->organization);

		query = xstrdup_printf(
			"insert into %s (%s) values (%s) "
			"on duplicate key update deleted=0, mod_time=%d %s;",
			acct_table, cols, vals,
			now, extra);
		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(cols);
		xfree(vals);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add acct");
			xfree(extra);
			continue;
		}
		affect_rows = last_affected_rows(mysql_conn->db_conn);
/* 		debug3("affected %d", affect_rows); */

		if(!affect_rows) {
			debug3("nothing changed");
			xfree(extra);
			continue;
		}

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = fix_double_quotes(extra+2);

		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%d, %u, \"%s\", \"%s\", \"%s\")",
				   now, DBD_ADD_ACCOUNTS, object->name,
				   user_name, tmp_extra);
		else
			xstrfmtcat(txn_query,
				   "insert into %s "
				   "(timestamp, action, name, actor, info) "
				   "values (%d, %u, \"%s\", \"%s\", \"%s\")",
				   txn_table,
				   now, DBD_ADD_ACCOUNTS, object->name,
				   user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);

		if(!object->assoc_list)
			continue;

		list_transfer(assoc_list, object->assoc_list);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(rc != SLURM_ERROR) {
		if(txn_query) {
			xstrcat(txn_query, ";");
			rc = mysql_db_query(mysql_conn->db_conn,
					    txn_query);
			xfree(txn_query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add txn");
				rc = SLURM_SUCCESS;
			}
		}
	} else
		xfree(txn_query);

	if(list_count(assoc_list)) {
		if(mysql_add_assocs(mysql_conn, uid, assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding user associations");
			rc = SLURM_ERROR;
		}
	}
	list_destroy(assoc_list);

	return rc;
}

extern int acct_storage_p_add_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
				       List cluster_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_cluster_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL, *tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;
	List assoc_list = NULL;
	acct_association_rec_t *assoc = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	assoc_list = list_create(destroy_acct_association_rec);

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(cluster_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->name[0]) {
			error("We need a cluster name to add.");
			rc = SLURM_ERROR;
			continue;
		}

		xstrcat(cols, "creation_time, mod_time, acct, cluster");
		xstrfmtcat(vals, "%d, %d, 'root', \"%s\"",
			   now, now, object->name);
		xstrfmtcat(extra, ", mod_time=%d", now);
		if(object->root_assoc)
			setup_association_limits(object->root_assoc, &cols,
						 &vals, &extra,
						 QOS_LEVEL_SET, 1);
		xstrfmtcat(query,
			   "insert into %s (creation_time, mod_time, "
			   "name, classification) "
			   "values (%d, %d, \"%s\", %u) "
			   "on duplicate key update deleted=0, mod_time=%d, "
			   "control_host='', control_port=0;",
			   cluster_table,
			   now, now, object->name, object->classification,
			   now);
		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add cluster %s", object->name);
			xfree(extra);
			xfree(cols);
			xfree(vals);
			added=0;
			break;
		}

		affect_rows = last_affected_rows(mysql_conn->db_conn);

		if(!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(extra);
			xfree(cols);
			xfree(vals);
			continue;
		}

		xstrfmtcat(query,
			   "SELECT @MyMax := coalesce(max(rgt), 0) FROM %s "
			   "FOR UPDATE;",
			   assoc_table);
		xstrfmtcat(query,
			   "insert into %s (%s, lft, rgt) "
			   "values (%s, @MyMax+1, @MyMax+2) "
			   "on duplicate key update deleted=0, "
			   "id=LAST_INSERT_ID(id)%s;",
			   assoc_table, cols,
			   vals,
			   extra);

		xfree(cols);
		xfree(vals);
		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);

		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);

		if(rc != SLURM_SUCCESS) {
			error("Couldn't add cluster root assoc");
			xfree(extra);
			added=0;
			break;
		}

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = fix_double_quotes(extra+2);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %u, \"%s\", \"%s\", \"%s\");",
			   txn_table, now, DBD_ADD_CLUSTERS,
			   object->name, user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);
		debug4("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);

		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else
			added++;

		/* Add user root by default to run from the root
		 * association.  This gets popped off so we need to
		 * read it every time here.
		 */
		assoc = xmalloc(sizeof(acct_association_rec_t));
		init_acct_association_rec(assoc);
		list_append(assoc_list, assoc);

		assoc->cluster = xstrdup(object->name);
		assoc->user = xstrdup("root");
		assoc->acct = xstrdup("root");

		if(mysql_add_assocs(mysql_conn, uid, assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding root user association");
			rc = SLURM_ERROR;
		}

	}
	list_iterator_destroy(itr);
	xfree(user_name);

	list_destroy(assoc_list);

	if(!added) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
	}

	return rc;
}

extern int acct_storage_p_add_associations(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   List association_list)
{
	return mysql_add_assocs(mysql_conn, uid, association_list);
}

extern int acct_storage_p_add_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				  List qos_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_qos_rec_t *object = NULL;
	char *cols = NULL, *extra = NULL, *vals = NULL, *query = NULL,
		*tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;
	char *added_preempt = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(qos_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->name[0]) {
			error("We need a qos name to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name");
		xstrfmtcat(vals, "%d, %d, \"%s\"",
			   now, now, object->name);
		xstrfmtcat(extra, ", mod_time=%d", now);

		_setup_qos_limits(object, &cols, &vals, &extra, &added_preempt);
		if(added_preempt) {
			object->preempt_bitstr = bit_alloc(g_qos_count);
			bit_unfmt(object->preempt_bitstr, added_preempt+1);
			xfree(added_preempt);
		}

		xstrfmtcat(query,
			   "insert into %s (%s) values (%s) "
			   "on duplicate key update deleted=0, "
			   "id=LAST_INSERT_ID(id)%s;",
			   qos_table, cols, vals, extra);


		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		object->id = mysql_insert_ret_id(mysql_conn->db_conn, query);
		xfree(query);
		if(!object->id) {
			error("Couldn't add qos %s", object->name);
			added=0;
			xfree(cols);
			xfree(extra);
			xfree(vals);
			break;
		}

		affect_rows = last_affected_rows(mysql_conn->db_conn);

		if(!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(cols);
			xfree(extra);
			xfree(vals);
			continue;
		}

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = fix_double_quotes(extra+2);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %u, \"%s\", \"%s\", \"%s\");",
			   txn_table,
			   now, DBD_ADD_QOS, object->name, user_name,
			   tmp_extra);

		xfree(tmp_extra);
		xfree(cols);
		xfree(extra);
		xfree(vals);
		debug4("query\n%s",query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if(addto_update_list(mysql_conn->update_list,
					      ACCT_ADD_QOS,
					      object) == SLURM_SUCCESS)
				list_remove(itr);
			added++;
		}

	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(!added) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
	}

	return rc;
}

extern int acct_storage_p_add_wckeys(mysql_conn_t *mysql_conn, uint32_t uid,
				     List wckey_list)
{
	return mysql_add_wckeys(mysql_conn, uid, wckey_list);
}

extern int acct_storage_p_add_reservation(mysql_conn_t *mysql_conn,
					  acct_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL;//, *tmp_extra = NULL;

	if(!resv) {
		error("No reservation was given to edit");
		return SLURM_ERROR;
	}

	if(!resv->id) {
		error("We need an id to edit a reservation.");
		return SLURM_ERROR;
	}
	if(!resv->time_start) {
		error("We need a start time to edit a reservation.");
		return SLURM_ERROR;
	}
	if(!resv->cluster || !resv->cluster[0]) {
		error("We need a cluster name to edit a reservation.");
		return SLURM_ERROR;
	}

	_setup_resv_limits(resv, &cols, &vals, &extra);

	xstrfmtcat(query,
		   "insert into %s (id, cluster%s) values (%u, '%s'%s) "
		   "on duplicate key update deleted=0%s;",
		   resv_table, cols, resv->id, resv->cluster,
		   vals, extra);
	debug3("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);

	if((rc = mysql_db_query(mysql_conn->db_conn, query)
	    == SLURM_SUCCESS))
		rc = mysql_clear_results(mysql_conn->db_conn);

	xfree(query);
	xfree(cols);
	xfree(vals);
	xfree(extra);

	return rc;
}

extern List acct_storage_p_modify_users(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_user_cond_t *user_cond,
					acct_user_rec_t *user)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!user_cond || !user) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(user_cond->assoc_cond && user_cond->assoc_cond->user_list
	   && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_acct_list && list_count(user_cond->def_acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_acct_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_wckey_list && list_count(user_cond->def_wckey_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_wckey_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_wckey=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->admin_level != ACCT_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u", user_cond->admin_level);
	}

	if(user->default_acct)
		xstrfmtcat(vals, ", default_acct=\"%s\"", user->default_acct);

	if(user->default_wckey)
		xstrfmtcat(vals, ", default_wckey=\"%s\"", user->default_wckey);

	if(user->admin_level != ACCT_ADMIN_NOTSET)
		xstrfmtcat(vals, ", admin_level=%u", user->admin_level);

	if(!extra || !vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}
	query = xstrdup_printf("select name from %s %s;",
			       user_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		acct_user_rec_t *user_rec = NULL;

		object = xstrdup(row[0]);
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "(name=\"%s\"", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name=\"%s\"", object);
		}
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(object);
		user_rec->default_acct = xstrdup(user->default_acct);
		user_rec->default_wckey = xstrdup(user->default_wckey);
		user_rec->admin_level = user->admin_level;
		addto_update_list(mysql_conn->update_list, ACCT_MODIFY_USER,
				   user_rec);
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(vals);
		xfree(query);
		return ret_list;
	}
	xfree(query);
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = modify_common(mysql_conn, DBD_MODIFY_USERS, now,
			    user_name, user_table, name_char, vals);
	xfree(user_name);
	xfree(name_char);
	xfree(vals);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify users");
		list_destroy(ret_list);
		ret_list = NULL;
	}

	return ret_list;
}

extern List acct_storage_p_modify_accounts(
	mysql_conn_t *mysql_conn, uint32_t uid,
	acct_account_cond_t *acct_cond,
	acct_account_rec_t *acct)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!acct_cond || !acct) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(acct_cond->assoc_cond
	   && acct_cond->assoc_cond->acct_list
	   && list_count(acct_cond->assoc_cond->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_cond->description_list
	   && list_count(acct_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_cond->organization_list
	   && list_count(acct_cond->organization_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->organization_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct->description)
		xstrfmtcat(vals, ", description=\"%s\"", acct->description);
	if(acct->organization)
		xstrfmtcat(vals, ", organization=\"%s\"", acct->organization);

	if(!extra || !vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	query = xstrdup_printf("select name from %s %s;", acct_table, extra);
	xfree(extra);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		xfree(vals);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		object = xstrdup(row[0]);
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "(name=\"%s\"", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name=\"%s\"", object);
		}

	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		xfree(vals);
		return ret_list;
	}
	xfree(query);
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = modify_common(mysql_conn, DBD_MODIFY_ACCOUNTS, now,
			    user_name, acct_table, name_char, vals);
	xfree(user_name);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify accounts");
		list_destroy(ret_list);
		errno = SLURM_ERROR;
		ret_list = NULL;
	}

	xfree(name_char);
	xfree(vals);

	return ret_list;
}

extern List acct_storage_p_modify_clusters(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   acct_cluster_cond_t *cluster_cond,
					   acct_cluster_rec_t *cluster)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL,
		*name_char = NULL, *send_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	bool clust_reg = false;

	/* If you need to alter the default values of the cluster use
	 * modify_associations since this is used only for registering
	 * the controller when it loads
	 */

	if(!cluster_cond || !cluster) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(cluster_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(cluster_cond->classification) {
		xstrfmtcat(extra, " && (classification & %u)",
			   cluster_cond->classification);
	}

	set = 0;
	if(cluster->control_host) {
		xstrfmtcat(vals, ", control_host='%s'", cluster->control_host);
		set++;
		clust_reg = true;
	}

	if(cluster->control_port) {
		xstrfmtcat(vals, ", control_port=%u", cluster->control_port);
		set++;
		clust_reg = true;
	}

	if(cluster->rpc_version) {
		xstrfmtcat(vals, ", rpc_version=%u", cluster->rpc_version);
		set++;
		clust_reg = true;
	}

	if(cluster->classification) {
		xstrfmtcat(vals, ", classification=%u",
			   cluster->classification);
	}

	if(!vals) {
		xfree(extra);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	} else if(clust_reg && (set != 3)) {
		xfree(vals);
		xfree(extra);
		errno = EFAULT;
		error("Need control host, port and rpc version "
		      "to register a cluster");
		return NULL;
	}


	xstrfmtcat(query, "select name, control_port from %s %s;",
		   cluster_table, extra);

	xfree(extra);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		xfree(vals);
		error("no result given for %s", extra);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		object = xstrdup(row[0]);

		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name='%s'", object);
		}
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(vals);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	if(vals) {
		send_char = xstrdup_printf("(%s)", name_char);
		user_name = uid_to_string((uid_t) uid);
		rc = modify_common(mysql_conn, DBD_MODIFY_CLUSTERS, now,
				    user_name, cluster_table, send_char, vals);
		xfree(user_name);
		if (rc == SLURM_ERROR) {
			error("Couldn't modify cluster 1");
			list_destroy(ret_list);
			ret_list = NULL;
			goto end_it;
		}
	}

end_it:
	xfree(name_char);
	xfree(vals);
	xfree(send_char);

	return ret_list;
}

extern List acct_storage_p_modify_associations(
	mysql_conn_t *mysql_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond,
	acct_association_rec_t *assoc)
{
	return mysql_modify_assocs(mysql_conn, uid, assoc_cond, assoc);
}

extern List acct_storage_p_modify_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp_char1=NULL, *tmp_char2=NULL;
	bitstr_t *preempt_bitstr = NULL;
	char *added_preempt = NULL;

	if(!qos_cond || !qos) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");

	if(qos_cond->description_list
	   && list_count(qos_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->id_list
	   && list_count(qos_cond->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->id_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->name_list
	   && list_count(qos_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	_setup_qos_limits(qos, &tmp_char1, &tmp_char2, &vals, &added_preempt);
	if(added_preempt) {
		preempt_bitstr = bit_alloc(g_qos_count);
		bit_unfmt(preempt_bitstr, added_preempt+1);
		xfree(added_preempt);
	}
	xfree(tmp_char1);
	xfree(tmp_char2);

	if(!extra || !vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		FREE_NULL_BITMAP(preempt_bitstr);
		error("Nothing to change");
		return NULL;
	}
	query = xstrdup_printf("select name, preempt, id from %s %s;",
			       qos_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		FREE_NULL_BITMAP(preempt_bitstr);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		acct_qos_rec_t *qos_rec = NULL;
		if(preempt_bitstr) {
			if(_preemption_loop(mysql_conn,
					    atoi(row[2]), preempt_bitstr))
				break;
		}
		object = xstrdup(row[0]);
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name='%s'", object);
		}

		qos_rec = xmalloc(sizeof(acct_qos_rec_t));
		qos_rec->name = xstrdup(object);

		qos_rec->grp_cpus = qos->grp_cpus;
		qos_rec->grp_cpu_mins = qos->grp_cpu_mins;
		qos_rec->grp_jobs = qos->grp_jobs;
		qos_rec->grp_nodes = qos->grp_nodes;
		qos_rec->grp_submit_jobs = qos->grp_submit_jobs;
		qos_rec->grp_wall = qos->grp_wall;

		qos_rec->max_cpus_pj = qos->max_cpus_pj;
		qos_rec->max_cpu_mins_pj = qos->max_cpu_mins_pj;
		qos_rec->max_jobs_pu  = qos->max_jobs_pu;
		qos_rec->max_nodes_pj = qos->max_nodes_pj;
		qos_rec->max_submit_jobs_pu  = qos->max_submit_jobs_pu;
		qos_rec->max_wall_pj = qos->max_wall_pj;

		qos_rec->priority = qos->priority;

		if(qos->preempt_list) {
			ListIterator new_preempt_itr =
				list_iterator_create(qos->preempt_list);
			char *new_preempt = NULL;

			qos->preempt_bitstr = bit_alloc(g_qos_count);
			if(row[1] && row[1][0])
				bit_unfmt(qos->preempt_bitstr, row[1]+1);

			while((new_preempt = list_next(new_preempt_itr))) {
				bool cleared = 0;
				if(new_preempt[0] == '-') {
					bit_clear(qos->preempt_bitstr,
						  atoi(new_preempt+1));
				} else if(new_preempt[0] == '+') {
					bit_set(qos->preempt_bitstr,
						atoi(new_preempt+1));
				} else {
					if(!cleared) {
						cleared = 1;
						bit_nclear(qos->preempt_bitstr,
							   0,
							   bit_size(qos->preempt_bitstr)-1);
					}

					bit_set(qos->preempt_bitstr,
						atoi(new_preempt));
				}
			}
			list_iterator_destroy(new_preempt_itr);
		}

		addto_update_list(mysql_conn->update_list, ACCT_MODIFY_QOS,
				   qos_rec);
	}
	mysql_free_result(result);

	FREE_NULL_BITMAP(preempt_bitstr);

	if(row) {
		xfree(vals);
		xfree(name_char);
		xfree(query);
		list_destroy(ret_list);
		ret_list = NULL;
		errno = ESLURM_QOS_PREEMPTION_LOOP;
		return ret_list;
	}

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(vals);
		xfree(query);
		return ret_list;
	}
	xfree(query);
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = modify_common(mysql_conn, DBD_MODIFY_QOS, now,
			    user_name, qos_table, name_char, vals);
	xfree(user_name);
	xfree(name_char);
	xfree(vals);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify qos");
		list_destroy(ret_list);
		ret_list = NULL;
	}

	return ret_list;
}

extern List acct_storage_p_modify_wckeys(mysql_conn_t *mysql_conn,
					 uint32_t uid,
					 acct_wckey_cond_t *wckey_cond,
					 acct_wckey_rec_t *wckey)
{
	return mysql_modify_wckeys(mysql_conn, uid, wckey_cond, wckey);
}

extern int acct_storage_p_modify_reservation(mysql_conn_t *mysql_conn,
					     acct_reservation_rec_t *resv)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL;//, *tmp_extra = NULL;
	time_t start = 0, now = time(NULL);
	int i;
	int set = 0;
	char *resv_req_inx[] = {
		"assoclist",
		"start",
		"end",
		"cpus",
		"name",
		"nodelist",
		"node_inx",
		"flags"
	};
	enum {
		RESV_ASSOCS,
		RESV_START,
		RESV_END,
		RESV_CPU,
		RESV_NAME,
		RESV_NODES,
		RESV_NODE_INX,
		RESV_FLAGS,
		RESV_COUNT
	};

	if(!resv) {
		error("No reservation was given to edit");
		return SLURM_ERROR;
	}

	if(!resv->id) {
		error("We need an id to edit a reservation.");
		return SLURM_ERROR;
	}
	if(!resv->time_start) {
		error("We need a start time to edit a reservation.");
		return SLURM_ERROR;
	}
	if(!resv->cluster || !resv->cluster[0]) {
		error("We need a cluster name to edit a reservation.");
		return SLURM_ERROR;
	}

	if(!resv->time_start_prev) {
		error("We need a time to check for last "
		      "start of reservation.");
		return SLURM_ERROR;
	}

	for(i=0; i<RESV_COUNT; i++) {
		if(i)
			xstrcat(cols, ", ");
		xstrcat(cols, resv_req_inx[i]);
	}

	/* check for both the last start and the start because most
	   likely the start time hasn't changed, but something else
	   may have since the last time we did an update to the
	   reservation. */
	query = xstrdup_printf("select %s from %s where id=%u "
			       "and (start=%d || start=%d) and cluster='%s' "
			       "and deleted=0 order by start desc "
			       "limit 1 FOR UPDATE;",
			       cols, resv_table, resv->id,
			       resv->time_start, resv->time_start_prev,
			       resv->cluster);
try_again:
	debug4("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		rc = SLURM_ERROR;
		goto end_it;
	}
	if(!(row = mysql_fetch_row(result))) {
		rc = SLURM_ERROR;
		mysql_free_result(result);
		error("There is no reservation by id %u, "
		      "start %d, and cluster '%s'", resv->id,
		      resv->time_start_prev, resv->cluster);
		if(!set && resv->time_end) {
			/* This should never really happen,
			   but just incase the controller and the
			   database get out of sync we check
			   to see if there is a reservation
			   not deleted that hasn't ended yet. */
			xfree(query);
			query = xstrdup_printf(
				"select %s from %s where id=%u "
				"and start <= %d and cluster='%s' "
				"and deleted=0 order by start desc "
				"limit 1;",
				cols, resv_table, resv->id,
				resv->time_end, resv->cluster);
			set = 1;
			goto try_again;
		}
		goto end_it;
	}

	start = atoi(row[RESV_START]);

	xfree(query);
	xfree(cols);

	set = 0;

	/* check differences here */

	if(!resv->name
	   && row[RESV_NAME] && row[RESV_NAME][0])
		// if this changes we just update the
		// record, no need to create a new one since
		// this doesn't really effect the
		// reservation accounting wise
		resv->name = xstrdup(row[RESV_NAME]);

	if(resv->assocs)
		set = 1;
	else if(row[RESV_ASSOCS] && row[RESV_ASSOCS][0])
		resv->assocs = xstrdup(row[RESV_ASSOCS]);

	if(resv->cpus != (uint32_t)NO_VAL)
		set = 1;
	else
		resv->cpus = atoi(row[RESV_CPU]);

	if(resv->flags != (uint16_t)NO_VAL)
		set = 1;
	else
		resv->flags = atoi(row[RESV_FLAGS]);

	if(resv->nodes)
		set = 1;
	else if(row[RESV_NODES] && row[RESV_NODES][0]) {
		resv->nodes = xstrdup(row[RESV_NODES]);
		resv->node_inx = xstrdup(row[RESV_NODE_INX]);
	}

	if(!resv->time_end)
		resv->time_end = atoi(row[RESV_END]);

	mysql_free_result(result);

	_setup_resv_limits(resv, &cols, &vals, &extra);
	/* use start below instead of resv->time_start_prev
	 * just incase we have a different one from being out
	 * of sync
	 */
	if((start > now) || !set) {
		/* we haven't started the reservation yet, or
		   we are changing the associations or end
		   time which we can just update it */
		query = xstrdup_printf("update %s set deleted=0%s "
				       "where deleted=0 and id=%u "
				       "and start=%d and cluster='%s';",
				       resv_table, extra, resv->id,
				       start,
				       resv->cluster);
	} else {
		/* time_start is already done above and we
		 * changed something that is in need on a new
		 * entry. */
		query = xstrdup_printf("update %s set end=%d "
				       "where deleted=0 && id=%u "
				       "&& start=%d and cluster='%s';",
				       resv_table, resv->time_start-1,
				       resv->id, start,
				       resv->cluster);
		xstrfmtcat(query,
			   "insert into %s (id, cluster%s) "
			   "values (%u, '%s'%s) "
			   "on duplicate key update deleted=0%s;",
			   resv_table, cols, resv->id, resv->cluster,
			   vals, extra);
	}

	debug3("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);

	if((rc = mysql_db_query(mysql_conn->db_conn, query)
	    == SLURM_SUCCESS))
		rc = mysql_clear_results(mysql_conn->db_conn);

end_it:

	xfree(query);
	xfree(cols);
	xfree(vals);
	xfree(extra);

	return rc;
}

extern List acct_storage_p_remove_users(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_user_cond_t *user_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	List coord_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	acct_user_cond_t user_coord_cond;
	acct_association_cond_t assoc_cond;
	acct_wckey_cond_t wckey_cond;

	if(!user_cond) {
		error("we need something to remove");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");

	if(user_cond->assoc_cond && user_cond->assoc_cond->user_list
	   && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_acct_list && list_count(user_cond->def_acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_acct_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_wckey_list && list_count(user_cond->def_wckey_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_wckey_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_wckey=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->admin_level != ACCT_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u", user_cond->admin_level);
	}

	if(!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select name from %s %s;", user_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	memset(&user_coord_cond, 0, sizeof(acct_user_cond_t));
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	/* we do not need to free the objects we put in here since
	   they are also placed in a list that will be freed
	*/
	assoc_cond.user_list = list_create(NULL);
	user_coord_cond.assoc_cond = &assoc_cond;

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		acct_user_rec_t *user_rec = NULL;

		list_append(ret_list, object);
		list_append(assoc_cond.user_list, object);

		if(!rc) {
			xstrfmtcat(name_char, "name=\"%s\"", object);
			xstrfmtcat(assoc_char, "t2.user=\"%s\"", object);
			rc = 1;
		} else {
			xstrfmtcat(name_char, " || name=\"%s\"", object);
			xstrfmtcat(assoc_char, " || t2.user=\"%s\"", object);
		}
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(object);
		addto_update_list(mysql_conn->update_list, ACCT_REMOVE_USER,
				   user_rec);

	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		list_destroy(assoc_cond.user_list);
		return ret_list;
	}
	xfree(query);

	/* We need to remove these accounts from the coord's that have it */
	coord_list = mysql_remove_coord(
		mysql_conn, uid, NULL, &user_coord_cond);
	if(coord_list)
		list_destroy(coord_list);

	/* We need to remove these users from the wckey table */
	memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
	wckey_cond.user_list = assoc_cond.user_list;
	coord_list = mysql_remove_wckeys(
		mysql_conn, uid, &wckey_cond);
	if(coord_list)
		list_destroy(coord_list);

	list_destroy(assoc_cond.user_list);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_USERS, now,
			    user_name, user_table, name_char, assoc_char);
	xfree(user_name);
	xfree(name_char);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		xfree(assoc_char);
		return NULL;
	}

	query = xstrdup_printf(
		"update %s as t2 set deleted=1, mod_time=%d where %s",
		acct_coord_table, now, assoc_char);
	xfree(assoc_char);

	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		error("Couldn't remove user coordinators");
		list_destroy(ret_list);
		return NULL;
	}

	return ret_list;
}

extern List acct_storage_p_remove_coord(mysql_conn_t *mysql_conn, uint32_t uid,
					List acct_list,
					acct_user_cond_t *user_cond)
{
	char *query = NULL, *object = NULL, *extra = NULL, *last_user = NULL;
	char *user_name = NULL;
	time_t now = time(NULL);
	int set = 0, is_admin=0, rc;
	ListIterator itr = NULL;
	acct_user_rec_t *user_rec = NULL;
	List ret_list = NULL;
	List user_list = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	acct_user_rec_t user;

	if(!user_cond && !acct_list) {
		error("we need something to remove");
		return NULL;
	} else if(user_cond && user_cond->assoc_cond)
		user_list = user_cond->assoc_cond->user_list;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	/* This only works when running though the slurmdbd.
	 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
	 * SLURMDBD!
	 */
	if(slurmdbd_conf) {
		/* we have to check the authentication here in the
		 * plugin since we don't know what accounts are being
		 * referenced until after the query.  Here we will
		 * set if they are an operator or greater and then
		 * check it below after the query.
		 */
		if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
		   || assoc_mgr_get_admin_level(mysql_conn, uid)
		   >= ACCT_ADMIN_OPERATOR)
			is_admin = 1;
		else {
			if(assoc_mgr_fill_in_user(mysql_conn, &user, 1, NULL)
			   != SLURM_SUCCESS) {
				error("couldn't get information for this user");
				errno = SLURM_ERROR;
				return NULL;
			}
			if(!user.coord_accts || !list_count(user.coord_accts)) {
				error("This user doesn't have any "
				      "coordinator abilities");
				errno = ESLURM_ACCESS_DENIED;
				return NULL;
			}
		}
	} else {
		/* Setting this here just makes it easier down below
		 * since user will not be filled in.
		 */
		is_admin = 1;
	}

	/* Leave it this way since we are using extra below */

	if(user_list && list_count(user_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, "(");

		itr = list_iterator_create(user_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "user=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_list && list_count(acct_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, "(");

		itr = list_iterator_create(acct_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "acct=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(!extra) {
		errno = SLURM_ERROR;
		debug3("No conditions given");
		return NULL;
	}

	query = xstrdup_printf(
		"select user, acct from %s where deleted=0 && %s order by user",
		acct_coord_table, extra);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		xfree(extra);
		errno = SLURM_ERROR;
		return NULL;
	}
	xfree(query);
	ret_list = list_create(slurm_destroy_char);
	user_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		if(!is_admin) {
			acct_coord_rec_t *coord = NULL;
			if(!user.coord_accts) { // This should never
						// happen
				error("We are here with no coord accts");
				errno = ESLURM_ACCESS_DENIED;
				list_destroy(ret_list);
				list_destroy(user_list);
				xfree(extra);
				mysql_free_result(result);
				return NULL;
			}
			itr = list_iterator_create(user.coord_accts);
			while((coord = list_next(itr))) {
				if(!strcasecmp(coord->name, row[1]))
					break;
			}
			list_iterator_destroy(itr);

			if(!coord) {
				error("User %s(%d) does not have the "
				      "ability to change this account (%s)",
				      user.name, user.uid, row[1]);
				errno = ESLURM_ACCESS_DENIED;
				list_destroy(ret_list);
				list_destroy(user_list);
				xfree(extra);
				mysql_free_result(result);
				return NULL;
			}
		}
		if(!last_user || strcasecmp(last_user, row[0])) {
			list_append(user_list, xstrdup(row[0]));
			last_user = row[0];
		}
		list_append(ret_list, xstrdup_printf("U = %-9s A = %-10s",
						     row[0], row[1]));
	}
	mysql_free_result(result);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_ACCOUNT_COORDS, now,
			    user_name, acct_coord_table, extra, NULL);
	xfree(user_name);
	xfree(extra);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		list_destroy(user_list);
		errno = SLURM_ERROR;
		return NULL;
	}

	/* get the update list set */
	itr = list_iterator_create(user_list);
	while((last_user = list_next(itr))) {
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(last_user);
		_get_user_coords(mysql_conn, user_rec);
		addto_update_list(mysql_conn->update_list,
				   ACCT_REMOVE_COORD, user_rec);
	}
	list_iterator_destroy(itr);
	list_destroy(user_list);

	return ret_list;
}

extern List acct_storage_p_remove_accts(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_account_cond_t *acct_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	List coord_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!acct_cond) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(acct_cond->assoc_cond
	   && acct_cond->assoc_cond->acct_list
	   && list_count(acct_cond->assoc_cond->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_cond->description_list
	   && list_count(acct_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_cond->organization_list
	   && list_count(acct_cond->organization_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->organization_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select name from %s %s;", acct_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "name=\"%s\"", object);
			xstrfmtcat(assoc_char, "t2.acct=\"%s\"", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name=\"%s\"", object);
			xstrfmtcat(assoc_char, " || t2.acct=\"%s\"", object);
		}
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	/* We need to remove these accounts from the coord's that have it */
	coord_list = mysql_remove_coord(
		mysql_conn, uid, ret_list, NULL);
	if(coord_list)
		list_destroy(coord_list);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_ACCOUNTS, now,
			    user_name, acct_table, name_char, assoc_char);
	xfree(user_name);
	xfree(name_char);
	xfree(assoc_char);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}

	return ret_list;
}

extern List acct_storage_p_remove_clusters(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   acct_cluster_cond_t *cluster_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	List tmp_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	acct_wckey_cond_t wckey_cond;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!cluster_cond) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(cluster_cond->cluster_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select name from %s %s;", cluster_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "name=\"%s\"", object);
			xstrfmtcat(extra, "t2.cluster=\"%s\"", object);
			xstrfmtcat(assoc_char, "cluster=\"%s\"", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name=\"%s\"", object);
			xstrfmtcat(extra, " || t2.cluster=\"%s\"", object);
			xstrfmtcat(assoc_char, " || cluster=\"%s\"", object);
		}
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	/* We need to remove these clusters from the wckey table */
	memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
	wckey_cond.cluster_list = ret_list;
	tmp_list = mysql_remove_wckeys(
		mysql_conn, uid, &wckey_cond);
	if(tmp_list)
		list_destroy(tmp_list);

	/* We should not need to delete any cluster usage just set it
	 * to deleted */
	xstrfmtcat(query,
		   "update %s set period_end=%d where period_end=0 && (%s);"
		   "update %s set mod_time=%d, deleted=1 where (%s);"
		   "update %s set mod_time=%d, deleted=1 where (%s);"
		   "update %s set mod_time=%d, deleted=1 where (%s);",
		   event_table, now, assoc_char,
		   cluster_day_table, now, assoc_char,
		   cluster_hour_table, now, assoc_char,
		   cluster_month_table, now, assoc_char);
	xfree(assoc_char);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		list_destroy(ret_list);
		xfree(name_char);
		xfree(extra);
		return NULL;
	}

	assoc_char = xstrdup_printf("t2.acct='root' && (%s)", extra);
	xfree(extra);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_CLUSTERS, now,
			    user_name, cluster_table, name_char, assoc_char);
	xfree(user_name);
	xfree(name_char);
	xfree(assoc_char);
	if (rc  == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}

	return ret_list;
}

extern List acct_storage_p_remove_associations(
	mysql_conn_t *mysql_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond)
{
	return mysql_remove_assocs(mysql_conn, uid, assoc_cond);
}

extern List acct_storage_p_remove_qos(mysql_conn_t *mysql_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!qos_cond) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(qos_cond->description_list
	   && list_count(qos_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->id_list
	   && list_count(qos_cond->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->id_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->name_list
	   && list_count(qos_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->name_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select id, name from %s %s;", qos_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	name_char = NULL;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		acct_qos_rec_t *qos_rec = NULL;

		list_append(ret_list, xstrdup(row[1]));
		if(!name_char)
			xstrfmtcat(name_char, "id=\"%s\"", row[0]);
		else
			xstrfmtcat(name_char, " || id=\"%s\"", row[0]);
		if(!assoc_char)
			xstrfmtcat(assoc_char, "qos=\"%s\"", row[0]);
		else
			xstrfmtcat(assoc_char, " || qos=\"%s\"", row[0]);
		xstrfmtcat(extra,
			   ", qos=replace(qos, ',%s', '')"
			   ", delta_qos=replace(delta_qos, ',+%s', '')"
			   ", delta_qos=replace(delta_qos, ',-%s', '')",
			   row[0], row[0], row[0]);

		qos_rec = xmalloc(sizeof(acct_qos_rec_t));
		/* we only need id when removing no real need to init */
		qos_rec->id = atoi(row[0]);
		addto_update_list(mysql_conn->update_list, ACCT_REMOVE_QOS,
				   qos_rec);
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	/* remove this qos from all the users/accts that have it */
	query = xstrdup_printf("update %s set mod_time=%d %s where deleted=0;",
			       assoc_table, now, extra);
	xfree(extra);
	debug3("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		list_destroy(ret_list);
		return NULL;
	}

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_QOS, now,
			    user_name, qos_table, name_char, assoc_char);
	xfree(assoc_char);
	xfree(name_char);
	xfree(user_name);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}

	return ret_list;
}

extern List acct_storage_p_remove_wckeys(mysql_conn_t *mysql_conn,
					 uint32_t uid,
					 acct_wckey_cond_t *wckey_cond)
{
	return mysql_remove_wckeys(mysql_conn, uid, wckey_cond);
}

extern int acct_storage_p_remove_reservation(mysql_conn_t *mysql_conn,
					     acct_reservation_rec_t *resv)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;//, *tmp_extra = NULL;

	if(!resv) {
		error("No reservation was given to edit");
		return SLURM_ERROR;
	}

	if(!resv->id || !resv->time_start || !resv->cluster) {
		error("We need an id, start time, and cluster "
		      "name to edit a reservation.");
		return SLURM_ERROR;
	}


	/* first delete the resv that hasn't happened yet. */
	query = xstrdup_printf("delete from %s where start > %d "
			       "and id=%u and start=%d "
			       "and cluster='%s';",
			       resv_table, resv->time_start_prev,
			       resv->id,
			       resv->time_start, resv->cluster);
	/* then update the remaining ones with a deleted flag and end
	 * time of the time_start_prev which is set to when the
	 * command was issued */
	xstrfmtcat(query,
		   "update %s set end=%d, deleted=1 where deleted=0 and "
		   "id=%u and start=%d and cluster='%s;'",
		   resv_table, resv->time_start_prev,
		   resv->id, resv->time_start,
		   resv->cluster);

	debug3("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);

	if((rc = mysql_db_query(mysql_conn->db_conn, query)
	    == SLURM_SUCCESS))
		rc = mysql_clear_results(mysql_conn->db_conn);

	xfree(query);

	return rc;
}

extern List acct_storage_p_get_users(mysql_conn_t *mysql_conn, uid_t uid,
				     acct_user_cond_t *user_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List user_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint16_t private_data = 0;
	acct_user_rec_t user;

	/* if this changes you will need to edit the corresponding enum */
	char *user_req_inx[] = {
		"name",
		"default_acct",
		"default_wckey",
		"admin_level"
	};
	enum {
		USER_REQ_NAME,
		USER_REQ_DA,
		USER_REQ_DW,
		USER_REQ_AL,
		USER_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USERS) {
		/* This only works when running though the slurmdbd.
		 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
		 * SLURMDBD!
		 */
		if(slurmdbd_conf) {
			is_admin = 0;
			/* we have to check the authentication here in the
			 * plugin since we don't know what accounts are being
			 * referenced until after the query.  Here we will
			 * set if they are an operator or greater and then
			 * check it below after the query.
			 */
			if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
			   || assoc_mgr_get_admin_level(mysql_conn, uid)
			   >= ACCT_ADMIN_OPERATOR)
				is_admin = 1;
			else {
				assoc_mgr_fill_in_user(mysql_conn, &user, 1,
						       NULL);
			}
		}
	}

	if(!user_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if(user_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");


	if(user_cond->assoc_cond &&
	   user_cond->assoc_cond->user_list
	   && list_count(user_cond->assoc_cond->user_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_acct_list && list_count(user_cond->def_acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_acct_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_acct=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->def_wckey_list && list_count(user_cond->def_wckey_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(user_cond->def_wckey_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "default_wckey=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(user_cond->admin_level != ACCT_ADMIN_NOTSET) {
		xstrfmtcat(extra, " && admin_level=%u",
			   user_cond->admin_level);
	}
empty:
	/* This is here to make sure we are looking at only this user
	 * if this flag is set.
	 */
	if(!is_admin && (private_data & PRIVATE_DATA_USERS)) {
		xstrfmtcat(extra, " && name=\"%s\"", user.name);
	}

	xfree(tmp);
	xstrfmtcat(tmp, "%s", user_req_inx[i]);
	for(i=1; i<USER_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", user_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s", tmp, user_table, extra);
	xfree(tmp);
	xfree(extra);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	user_list = list_create(destroy_acct_user_rec);

	while((row = mysql_fetch_row(result))) {
		acct_user_rec_t *user = xmalloc(sizeof(acct_user_rec_t));
/* 		uid_t pw_uid; */
		list_append(user_list, user);

		user->name =  xstrdup(row[USER_REQ_NAME]);
		user->default_acct = xstrdup(row[USER_REQ_DA]);
		if(row[USER_REQ_DW])
			user->default_wckey = xstrdup(row[USER_REQ_DW]);
		else
			user->default_wckey = xstrdup("");

		user->admin_level = atoi(row[USER_REQ_AL]);

		/* user id will be set on the client since this could be on a
		 * different machine where this user may not exist or
		 * may have a different uid
		 */
/* 		if (uid_from_string (user->name, &pw_uid) < 0)  */
/* 			user->uid = (uint32_t)NO_VAL; */
/* 		else */
/* 			user->uid = passwd_ptr->pw_uid; */

		if(user_cond && user_cond->with_coords)
			_get_user_coords(mysql_conn, user);
	}
	mysql_free_result(result);

	if(user_cond && user_cond->with_assocs) {
		ListIterator assoc_itr = NULL;
		acct_user_rec_t *user = NULL;
		acct_association_rec_t *assoc = NULL;
		List assoc_list = NULL;

		/* Make sure we don't get any non-user associations
		 * this is done by at least having a user_list
		 * defined */
		if(!user_cond->assoc_cond)
			user_cond->assoc_cond =
				xmalloc(sizeof(acct_association_cond_t));

		if(!user_cond->assoc_cond->user_list)
			user_cond->assoc_cond->user_list = list_create(NULL);

		assoc_list = mysql_get_assocs(
			mysql_conn, uid, user_cond->assoc_cond);

		if(!assoc_list) {
			error("no associations");
			goto get_wckeys;
		}

		itr = list_iterator_create(user_list);
		assoc_itr = list_iterator_create(assoc_list);
		while((user = list_next(itr))) {
			while((assoc = list_next(assoc_itr))) {
				if(strcmp(assoc->user, user->name))
					continue;

				if(!user->assoc_list)
					user->assoc_list = list_create(
						destroy_acct_association_rec);
				list_append(user->assoc_list, assoc);
				list_remove(assoc_itr);
			}
			list_iterator_reset(assoc_itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(assoc_itr);

		list_destroy(assoc_list);
	}

get_wckeys:
	if(user_cond && user_cond->with_wckeys) {
		ListIterator wckey_itr = NULL;
		acct_user_rec_t *user = NULL;
		acct_wckey_rec_t *wckey = NULL;
		List wckey_list = NULL;
		acct_wckey_cond_t wckey_cond;

		memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
		if(user_cond->assoc_cond) {
			wckey_cond.user_list =
				user_cond->assoc_cond->user_list;
			wckey_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
		}
		wckey_list = mysql_get_wckeys(
			mysql_conn, uid, &wckey_cond);

		if(!wckey_list) {
			error("no wckeys");
			return user_list;
		}

		itr = list_iterator_create(user_list);
		wckey_itr = list_iterator_create(wckey_list);
		while((user = list_next(itr))) {
			while((wckey = list_next(wckey_itr))) {
				if(strcmp(wckey->user, user->name))
					continue;

				if(!user->wckey_list)
					user->wckey_list = list_create(
						destroy_acct_wckey_rec);
				list_append(user->wckey_list, wckey);
				list_remove(wckey_itr);
			}
			list_iterator_reset(wckey_itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(wckey_itr);

		list_destroy(wckey_list);
	}

	return user_list;
}

extern List acct_storage_p_get_accts(mysql_conn_t *mysql_conn, uid_t uid,
				     acct_account_cond_t *acct_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List acct_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint16_t private_data = 0;
	acct_user_rec_t user;

	/* if this changes you will need to edit the corresponding enum */
	char *acct_req_inx[] = {
		"name",
		"description",
		"organization"
	};
	enum {
		ACCT_REQ_NAME,
		ACCT_REQ_DESC,
		ACCT_REQ_ORG,
		ACCT_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();

	if (private_data & PRIVATE_DATA_ACCOUNTS) {
		/* This only works when running though the slurmdbd.
		 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
		 * SLURMDBD!
		 */
		if(slurmdbd_conf) {
			is_admin = 0;
			/* we have to check the authentication here in the
			 * plugin since we don't know what accounts are being
			 * referenced until after the query.  Here we will
			 * set if they are an operator or greater and then
			 * check it below after the query.
			 */
			if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
			   || assoc_mgr_get_admin_level(mysql_conn, uid)
			   >= ACCT_ADMIN_OPERATOR)
				is_admin = 1;
			else {
				assoc_mgr_fill_in_user(mysql_conn, &user, 1,
						       NULL);
			}

			if(!is_admin && (!user.coord_accts
					 || !list_count(user.coord_accts))) {
				errno = ESLURM_ACCESS_DENIED;
				return NULL;
			}
		}
	}

	if(!acct_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if(acct_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");

	if(acct_cond->assoc_cond
	   && acct_cond->assoc_cond->acct_list
	   && list_count(acct_cond->assoc_cond->acct_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_cond->description_list
	   && list_count(acct_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(acct_cond->organization_list
	   && list_count(acct_cond->organization_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(acct_cond->organization_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "organization=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

	xfree(tmp);
	xstrfmtcat(tmp, "%s", acct_req_inx[i]);
	for(i=1; i<ACCT_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", acct_req_inx[i]);
	}

	/* This is here to make sure we are looking at only this user
	 * if this flag is set.  We also include any accounts they may be
	 * coordinator of.
	 */
	if(!is_admin && (private_data & PRIVATE_DATA_ACCOUNTS)) {
		acct_coord_rec_t *coord = NULL;
		set = 0;
		itr = list_iterator_create(user.coord_accts);
		while((coord = list_next(itr))) {
			if(set) {
				xstrfmtcat(extra, " || name=\"%s\"",
					   coord->name);
			} else {
				set = 1;
				xstrfmtcat(extra, " && (name=\"%s\"",
					   coord->name);
			}
		}
		list_iterator_destroy(itr);
		if(set)
			xstrcat(extra,")");
	}

	query = xstrdup_printf("select %s from %s %s", tmp, acct_table, extra);
	xfree(tmp);
	xfree(extra);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	acct_list = list_create(destroy_acct_account_rec);

	if(acct_cond && acct_cond->with_assocs) {
		/* We are going to be freeing the inners of
			   this list in the acct->name so we don't
			   free it here
			*/
		if(acct_cond->assoc_cond->acct_list)
			list_destroy(acct_cond->assoc_cond->acct_list);
		acct_cond->assoc_cond->acct_list = list_create(NULL);
	}

	while((row = mysql_fetch_row(result))) {
		acct_account_rec_t *acct = xmalloc(sizeof(acct_account_rec_t));
		list_append(acct_list, acct);

		acct->name =  xstrdup(row[ACCT_REQ_NAME]);
		acct->description = xstrdup(row[ACCT_REQ_DESC]);
		acct->organization = xstrdup(row[ACCT_REQ_ORG]);

		if(acct_cond && acct_cond->with_coords) {
			_get_account_coords(mysql_conn, acct);
		}

		if(acct_cond && acct_cond->with_assocs) {
			if(!acct_cond->assoc_cond) {
				acct_cond->assoc_cond = xmalloc(
					sizeof(acct_association_cond_t));
			}

			list_append(acct_cond->assoc_cond->acct_list,
				    acct->name);
		}
	}
	mysql_free_result(result);

	if(acct_cond && acct_cond->with_assocs
	   && list_count(acct_cond->assoc_cond->acct_list)) {
		ListIterator assoc_itr = NULL;
		acct_account_rec_t *acct = NULL;
		acct_association_rec_t *assoc = NULL;
		List assoc_list = mysql_get_assocs(
			mysql_conn, uid, acct_cond->assoc_cond);

		if(!assoc_list) {
			error("no associations");
			return acct_list;
		}

		itr = list_iterator_create(acct_list);
		assoc_itr = list_iterator_create(assoc_list);
		while((acct = list_next(itr))) {
			while((assoc = list_next(assoc_itr))) {
				if(strcmp(assoc->acct, acct->name))
					continue;

				if(!acct->assoc_list)
					acct->assoc_list = list_create(
						destroy_acct_association_rec);
				list_append(acct->assoc_list, assoc);
				list_remove(assoc_itr);
			}
			list_iterator_reset(assoc_itr);
			if(!acct->assoc_list)
				list_remove(itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(assoc_itr);

		list_destroy(assoc_list);
	}

	return acct_list;
}

extern List acct_storage_p_get_clusters(mysql_conn_t *mysql_conn, uid_t uid,
					acct_cluster_cond_t *cluster_cond)
{
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
	acct_association_cond_t assoc_cond;
	ListIterator assoc_itr = NULL;
	acct_cluster_rec_t *cluster = NULL;
	acct_association_rec_t *assoc = NULL;
	List assoc_list = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *cluster_req_inx[] = {
		"name",
		"classification",
		"control_host",
		"control_port",
		"rpc_version",
	};
	enum {
		CLUSTER_REQ_NAME,
		CLUSTER_REQ_CLASS,
		CLUSTER_REQ_CH,
		CLUSTER_REQ_CP,
		CLUSTER_REQ_VERSION,
		CLUSTER_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;


	if(!cluster_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if(cluster_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");

	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(cluster_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

	xfree(tmp);
	i=0;
	xstrfmtcat(tmp, "%s", cluster_req_inx[i]);
	for(i=1; i<CLUSTER_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", cluster_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s",
			       tmp, cluster_table, extra);
	xfree(tmp);
	xfree(extra);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	cluster_list = list_create(destroy_acct_cluster_rec);

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));

	if(cluster_cond) {
		/* I don't think we want the with_usage flag here.
		 * We do need the with_deleted though. */
		//assoc_cond.with_usage = cluster_cond->with_usage;
		assoc_cond.with_deleted = cluster_cond->with_deleted;
	}
	assoc_cond.cluster_list = list_create(NULL);

	while((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		MYSQL_ROW row2;

		cluster = xmalloc(sizeof(acct_cluster_rec_t));
		list_append(cluster_list, cluster);

		cluster->name = xstrdup(row[CLUSTER_REQ_NAME]);

		list_append(assoc_cond.cluster_list, cluster->name);

		/* get the usage if requested */
		if(cluster_cond && cluster_cond->with_usage) {
			clusteracct_storage_p_get_usage(
				mysql_conn, uid, cluster,
				DBD_GET_CLUSTER_USAGE,
				cluster_cond->usage_start,
				cluster_cond->usage_end);
		}

		cluster->classification = atoi(row[CLUSTER_REQ_CLASS]);
		cluster->control_host = xstrdup(row[CLUSTER_REQ_CH]);
		cluster->control_port = atoi(row[CLUSTER_REQ_CP]);
		cluster->rpc_version = atoi(row[CLUSTER_REQ_VERSION]);
		query = xstrdup_printf(
			"select cpu_count, cluster_nodes from "
			"%s where cluster=\"%s\" "
			"and period_end=0 and node_name='' limit 1",
			event_table, cluster->name);
		debug4("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		if(!(result2 = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			continue;
		}
		xfree(query);
		if((row2 = mysql_fetch_row(result2))) {
			cluster->cpu_count = atoi(row2[0]);
			if(row2[1] && row2[1][0])
				cluster->nodes = xstrdup(row2[1]);
		}
		mysql_free_result(result2);
	}
	mysql_free_result(result);

	if(!list_count(assoc_cond.cluster_list)) {
		list_destroy(assoc_cond.cluster_list);
		return cluster_list;
	}

	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, "root");

	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, "");

	assoc_list = mysql_get_assocs(mysql_conn, uid, &assoc_cond);
	list_destroy(assoc_cond.cluster_list);
	list_destroy(assoc_cond.acct_list);
	list_destroy(assoc_cond.user_list);

	if(!assoc_list)
		return cluster_list;

	itr = list_iterator_create(cluster_list);
	assoc_itr = list_iterator_create(assoc_list);
	while((cluster = list_next(itr))) {
		while((assoc = list_next(assoc_itr))) {
			if(strcmp(assoc->cluster, cluster->name))
				continue;

			if(cluster->root_assoc) {
				debug("This cluster %s already has "
				      "an association.");
				continue;
			}
			cluster->root_assoc = assoc;
			list_remove(assoc_itr);
		}
		list_iterator_reset(assoc_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(assoc_itr);
	if(list_count(assoc_list))
		error("I have %d left over associations",
		      list_count(assoc_list));
	list_destroy(assoc_list);

	return cluster_list;
}

extern List acct_storage_p_get_associations(mysql_conn_t *mysql_conn,
					    uid_t uid,
					    acct_association_cond_t *assoc_cond)
{
	return mysql_get_assocs(mysql_conn, uid, assoc_cond);
}

extern List acct_storage_p_get_events(mysql_conn_t *mysql_conn, uint32_t uid,
				      acct_event_cond_t *event_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List ret_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	time_t now = time(NULL);

	/* if this changes you will need to edit the corresponding enum */
	char *event_req_inx[] = {
		"node_name",
		"cluster",
		"cpu_count",
		"state",
		"period_start",
		"period_end",
		"reason",
		"cluster_nodes"
	};

	enum {
		EVENT_REQ_NODE,
		EVENT_REQ_CLUSTER,
		EVENT_REQ_CPU,
		EVENT_REQ_STATE,
		EVENT_REQ_START,
		EVENT_REQ_END,
		EVENT_REQ_REASON,
		EVENT_REQ_CNODES,
		EVENT_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if(!event_cond)
		goto empty;

	if(event_cond->cluster_list
	   && list_count(event_cond->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(event_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "cluster=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(event_cond->cpus_min) {
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		if(event_cond->cpus_max) {
			xstrfmtcat(extra, "cpu_count between %u and %u)",
				   event_cond->cpus_min, event_cond->cpus_max);

		} else {
			xstrfmtcat(extra, "cpu_count='%u')",
				   event_cond->cpus_min);

		}
	}

	switch(event_cond->event_type) {
	case ACCT_EVENT_ALL:
		break;
	case ACCT_EVENT_CLUSTER:
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrcat(extra, "node_name = '')");

		break;
	case ACCT_EVENT_NODE:
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrcat(extra, "node_name != '')");

		break;
	default:
		error("Unknown event %u doing all", event_cond->event_type);
		break;
	}

	if(event_cond->node_list
	   && list_count(event_cond->node_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->node_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "node_name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(event_cond->period_start) {
		if(!event_cond->period_end)
			event_cond->period_end = now;

		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		xstrfmtcat(query,
			   "(period_start < %d) "
			   "&& (period_end >= %d || period_end = 0))",
			   event_cond->period_end, event_cond->period_start);
	}

	if(event_cond->reason_list
	   && list_count(event_cond->reason_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->reason_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "reason like \"%%%s%%\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(event_cond->state_list
	   && list_count(event_cond->state_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->state_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "state=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}


empty:
	xfree(tmp);
	xstrfmtcat(tmp, "%s", event_req_inx[i]);
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", event_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s", tmp, event_table);
	xfree(tmp);
	if(extra) {
		xstrfmtcat(query, " %s", extra);
		xfree(extra);
	}

	ret_list = list_create(destroy_acct_event_rec);
	while((row = mysql_fetch_row(result))) {
		acct_event_rec_t *event = xmalloc(sizeof(acct_event_rec_t));

		list_append(ret_list, event);

		if(row[EVENT_REQ_NODE] && row[EVENT_REQ_NODE][0])
			event->node_name = xstrdup(row[EVENT_REQ_NODE]);

		if(row[EVENT_REQ_CLUSTER] && row[EVENT_REQ_CLUSTER][0])
			event->cluster = xstrdup(row[EVENT_REQ_CLUSTER]);

		event->cpu_count = atoi(row[EVENT_REQ_CPU]);
		event->state = atoi(row[EVENT_REQ_STATE]);
		event->period_start = atoi(row[EVENT_REQ_START]);
		event->period_end = atoi(row[EVENT_REQ_END]);

		if(row[EVENT_REQ_REASON] && row[EVENT_REQ_REASON][0])
			event->reason = xstrdup(row[EVENT_REQ_REASON]);

		if(row[EVENT_REQ_CLUSTER] && row[EVENT_REQ_CLUSTER][0])
			event->cluster_nodes = xstrdup(row[EVENT_REQ_CNODES]);
	}
	mysql_free_result(result);

	return ret_list;
}

extern List acct_storage_p_get_problems(mysql_conn_t *mysql_conn, uint32_t uid,
					acct_association_cond_t *assoc_cond)
{
	List ret_list = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	ret_list = list_create(destroy_acct_association_rec);

	if(mysql_acct_no_assocs(mysql_conn, assoc_cond, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

	if(mysql_acct_no_users(mysql_conn, assoc_cond, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

	if(mysql_user_no_assocs_or_no_uid(mysql_conn, assoc_cond, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

end_it:

	return ret_list;
}

extern List acct_storage_p_get_config(void *db_conn)
{
	return NULL;
}

extern List acct_storage_p_get_qos(mysql_conn_t *mysql_conn, uid_t uid,
				   acct_qos_cond_t *qos_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List qos_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *qos_req_inx[] = {
		"name",
		"description",
		"id",
		"grp_cpu_mins",
		"grp_cpus",
		"grp_jobs",
		"grp_nodes",
		"grp_submit_jobs",
		"grp_wall",
		"max_cpu_mins_per_job",
		"max_cpus_per_job",
		"max_jobs_per_user",
		"max_nodes_per_job",
		"max_submit_jobs_per_user",
		"max_wall_duration_per_job",
		"preempt",
		"priority",
		"usage_factor",
	};
	enum {
		QOS_REQ_NAME,
		QOS_REQ_DESC,
		QOS_REQ_ID,
		QOS_REQ_GCH,
		QOS_REQ_GC,
		QOS_REQ_GJ,
		QOS_REQ_GN,
		QOS_REQ_GSJ,
		QOS_REQ_GW,
		QOS_REQ_MCMPJ,
		QOS_REQ_MCPJ,
		QOS_REQ_MJPU,
		QOS_REQ_MNPJ,
		QOS_REQ_MSJPU,
		QOS_REQ_MWPJ,
		QOS_REQ_PREE,
		QOS_REQ_PRIO,
		QOS_REQ_UF,
		QOS_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if(!qos_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if(qos_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");


	if(qos_cond->description_list
	   && list_count(qos_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->id_list
	   && list_count(qos_cond->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->id_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->name_list
	   && list_count(qos_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

	xfree(tmp);
	xstrfmtcat(tmp, "%s", qos_req_inx[i]);
	for(i=1; i<QOS_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", qos_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s", tmp, qos_table, extra);
	xfree(tmp);
	xfree(extra);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	qos_list = list_create(destroy_acct_qos_rec);

	while((row = mysql_fetch_row(result))) {
		acct_qos_rec_t *qos = xmalloc(sizeof(acct_qos_rec_t));
		list_append(qos_list, qos);

		if(row[QOS_REQ_DESC] && row[QOS_REQ_DESC][0])
			qos->description = xstrdup(row[QOS_REQ_DESC]);

		qos->id = atoi(row[QOS_REQ_ID]);

		if(row[QOS_REQ_NAME] && row[QOS_REQ_NAME][0])
			qos->name =  xstrdup(row[QOS_REQ_NAME]);

		if(row[QOS_REQ_GCH])
			qos->grp_cpu_mins = atoll(row[QOS_REQ_GCH]);
		else
			qos->grp_cpu_mins = INFINITE;
		if(row[QOS_REQ_GC])
			qos->grp_cpus = atoi(row[QOS_REQ_GC]);
		else
			qos->grp_cpus = INFINITE;
		if(row[QOS_REQ_GJ])
			qos->grp_jobs = atoi(row[QOS_REQ_GJ]);
		else
			qos->grp_jobs = INFINITE;
		if(row[QOS_REQ_GN])
			qos->grp_nodes = atoi(row[QOS_REQ_GN]);
		else
			qos->grp_nodes = INFINITE;
		if(row[QOS_REQ_GSJ])
			qos->grp_submit_jobs = atoi(row[QOS_REQ_GSJ]);
		else
			qos->grp_submit_jobs = INFINITE;
		if(row[QOS_REQ_GW])
			qos->grp_wall = atoi(row[QOS_REQ_GW]);
		else
			qos->grp_wall = INFINITE;

		if(row[QOS_REQ_MCMPJ])
			qos->max_cpu_mins_pj = atoi(row[QOS_REQ_MCMPJ]);
		else
			qos->max_cpu_mins_pj = INFINITE;
		if(row[QOS_REQ_MCPJ])
			qos->max_cpus_pj = atoi(row[QOS_REQ_MCPJ]);
		else
			qos->max_cpus_pj = INFINITE;
		if(row[QOS_REQ_MJPU])
			qos->max_jobs_pu = atoi(row[QOS_REQ_MJPU]);
		else
			qos->max_jobs_pu = INFINITE;
		if(row[QOS_REQ_MNPJ])
			qos->max_nodes_pj = atoi(row[QOS_REQ_MNPJ]);
		else
			qos->max_nodes_pj = INFINITE;
		if(row[QOS_REQ_MSJPU])
			qos->max_submit_jobs_pu = atoi(row[QOS_REQ_MSJPU]);
		else
			qos->max_submit_jobs_pu = INFINITE;
		if(row[QOS_REQ_MWPJ])
			qos->max_wall_pj = atoi(row[QOS_REQ_MWPJ]);
		else
			qos->max_wall_pj = INFINITE;

		if(row[QOS_REQ_PREE] && row[QOS_REQ_PREE][0]) {
			if(!qos->preempt_bitstr)
				qos->preempt_bitstr = bit_alloc(g_qos_count);
			bit_unfmt(qos->preempt_bitstr, row[QOS_REQ_PREE]+1);
		}
		if(row[QOS_REQ_PRIO])
			qos->priority = atoi(row[QOS_REQ_PRIO]);

		if(row[QOS_REQ_UF])
			qos->usage_factor = atof(row[QOS_REQ_UF]);
	}
	mysql_free_result(result);

	return qos_list;
}

extern List acct_storage_p_get_wckeys(mysql_conn_t *mysql_conn, uid_t uid,
				      acct_wckey_cond_t *wckey_cond)
{
	return mysql_get_wckeys(mysql_conn, uid, wckey_cond);
}

extern List acct_storage_p_get_reservations(mysql_conn_t *mysql_conn, uid_t uid,
					    acct_reservation_cond_t *resv_cond)
{
	//DEF_TIMERS;
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List resv_list = NULL;
	int set = 0;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint16_t private_data = 0;
	acct_job_cond_t job_cond;
	void *curr_cluster = NULL;
	List local_cluster_list = NULL;

	/* needed if we don't have an resv_cond */
	uint16_t with_usage = 0;

	/* if this changes you will need to edit the corresponding enum */
	char *resv_req_inx[] = {
		"id",
		"name",
		"cluster",
		"cpus",
		"assoclist",
		"nodelist",
		"node_inx",
		"start",
		"end",
		"flags",
	};

	enum {
		RESV_REQ_ID,
		RESV_REQ_NAME,
		RESV_REQ_CLUSTER,
		RESV_REQ_CPUS,
		RESV_REQ_ASSOCS,
		RESV_REQ_NODES,
		RESV_REQ_NODE_INX,
		RESV_REQ_START,
		RESV_REQ_END,
		RESV_REQ_FLAGS,
		RESV_REQ_COUNT
	};

	if(!resv_cond) {
		xstrcat(extra, " where deleted=0");
		goto empty;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_RESERVATIONS) {
		/* This only works when running though the slurmdbd.
		 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
		 * SLURMDBD!
		 */
		if(slurmdbd_conf) {
			is_admin = 0;
			/* we have to check the authentication here in the
			 * plugin since we don't know what accounts are being
			 * referenced until after the query.  Here we will
			 * set if they are an operator or greater and then
			 * check it below after the query.
			 */
			if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
			   || assoc_mgr_get_admin_level(mysql_conn, uid)
			   >= ACCT_ADMIN_OPERATOR)
				is_admin = 1;
			else {
				error("Only admins can look at "
				      "reservation usage");
				return NULL;
			}
		}
	}

	memset(&job_cond, 0, sizeof(acct_job_cond_t));
	if(resv_cond->nodes) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
		job_cond.used_nodes = resv_cond->nodes;
		job_cond.cluster_list = resv_cond->cluster_list;
		local_cluster_list = setup_cluster_list_with_inx(
			mysql_conn, &job_cond, (void **)&curr_cluster);
	} else if(with_usage) {
		job_cond.usage_start = resv_cond->time_start;
		job_cond.usage_end = resv_cond->time_end;
	}

	set = _setup_resv_cond_limits(resv_cond, &extra);

	with_usage = resv_cond->with_usage;

empty:
	xfree(tmp);
	xstrfmtcat(tmp, "t1.%s", resv_req_inx[i]);
	for(i=1; i<RESV_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", t1.%s", resv_req_inx[i]);
	}

	//START_TIMER;
	query = xstrdup_printf("select distinct %s from %s as t1%s "
			       "order by cluster, name;",
			       tmp, resv_table, extra);
	xfree(tmp);
	xfree(extra);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		if(local_cluster_list)
			list_destroy(local_cluster_list);
		return NULL;
	}
	xfree(query);

	resv_list = list_create(destroy_acct_reservation_rec);

	while((row = mysql_fetch_row(result))) {
		acct_reservation_rec_t *resv =
			xmalloc(sizeof(acct_reservation_rec_t));
		int start = atoi(row[RESV_REQ_START]);
		list_append(resv_list, resv);

		if(!good_nodes_from_inx(local_cluster_list, &curr_cluster,
					row[RESV_REQ_NODE_INX], start))
			continue;

		resv->id = atoi(row[RESV_REQ_ID]);
		if(with_usage) {
			if(!job_cond.resvid_list)
				job_cond.resvid_list = list_create(NULL);
			list_append(job_cond.resvid_list, row[RESV_REQ_ID]);
		}
		resv->name = xstrdup(row[RESV_REQ_NAME]);
		resv->cluster = xstrdup(row[RESV_REQ_CLUSTER]);
		resv->cpus = atoi(row[RESV_REQ_CPUS]);
		resv->assocs = xstrdup(row[RESV_REQ_ASSOCS]);
		resv->nodes = xstrdup(row[RESV_REQ_NODES]);
		resv->time_start = start;
		resv->time_end = atoi(row[RESV_REQ_END]);
		resv->flags = atoi(row[RESV_REQ_FLAGS]);
	}

	if(local_cluster_list)
		list_destroy(local_cluster_list);

	if(with_usage && resv_list && list_count(resv_list)) {
		List job_list = mysql_jobacct_process_get_jobs(
			mysql_conn, uid, &job_cond);
		ListIterator itr = NULL, itr2 = NULL;
		jobacct_job_rec_t *job = NULL;
		acct_reservation_rec_t *resv = NULL;

		if(!job_list || !list_count(job_list))
			goto no_jobs;

		itr = list_iterator_create(job_list);
		itr2 = list_iterator_create(resv_list);
		while((job = list_next(itr))) {
			int start = job->start;
			int end = job->end;
			int set = 0;
			while((resv = list_next(itr2))) {
				int elapsed = 0;
				/* since a reservation could have
				   changed while a job was running we
				   have to make sure we get the time
				   in the correct record.
				*/
				if(resv->id != job->resvid)
					continue;
				set = 1;

				if(start < resv->time_start)
					start = resv->time_start;
				if(!end || end > resv->time_end)
					end = resv->time_end;

				if((elapsed = (end - start)) < 1)
					continue;

				if(job->alloc_cpus)
					resv->alloc_secs +=
						elapsed * job->alloc_cpus;
			}
			list_iterator_reset(itr2);
			if(!set) {
				error("we got a job %u with no reservation "
				      "associatied with it?", job->jobid);
			}
		}

		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);
	no_jobs:
		if(job_list)
			list_destroy(job_list);
	}

	if(job_cond.resvid_list) {
		list_destroy(job_cond.resvid_list);
		job_cond.resvid_list = NULL;
	}

	/* free result after we use the list with resv id's in it. */
	mysql_free_result(result);

	//END_TIMER2("get_resvs");
	return resv_list;
}

extern List acct_storage_p_get_txn(mysql_conn_t *mysql_conn, uid_t uid,
				   acct_txn_cond_t *txn_cond)
{
	char *query = NULL;
	char *assoc_extra = NULL;
	char *name_extra = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List txn_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *txn_req_inx[] = {
		"id",
		"timestamp",
		"action",
		"name",
		"actor",
		"info"
	};
	enum {
		TXN_REQ_ID,
		TXN_REQ_TS,
		TXN_REQ_ACTION,
		TXN_REQ_NAME,
		TXN_REQ_ACTOR,
		TXN_REQ_INFO,
		TXN_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if(!txn_cond)
		goto empty;

	/* handle query for associations first */
	if(txn_cond->acct_list && list_count(txn_cond->acct_list)) {
		set = 0;
		if(assoc_extra)
			xstrcat(assoc_extra, " && (");
		else
			xstrcat(assoc_extra, " where (");

		if(name_extra)
			xstrcat(name_extra, " && (");
		else
			xstrcat(name_extra, " (");
		itr = list_iterator_create(txn_cond->acct_list);
		while((object = list_next(itr))) {
			if(set) {
				xstrcat(assoc_extra, " || ");
				xstrcat(name_extra, " || ");
			}

			xstrfmtcat(assoc_extra, "acct=\"%s\"", object);

			xstrfmtcat(name_extra, "(name like \"%%\\\"%s\\\"%%\""
				   " || name=\"%s\")"
				   " || (info like \"%%acct=\\\"%s\\\"%%\")",
				   object, object, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(assoc_extra, ")");
		xstrcat(name_extra, ")");
	}

	if(txn_cond->cluster_list && list_count(txn_cond->cluster_list)) {
		set = 0;
		if(assoc_extra)
			xstrcat(assoc_extra, " && (");
		else
			xstrcat(assoc_extra, " where (");

		if(name_extra)
			xstrcat(name_extra, " && (");
		else
			xstrcat(name_extra, "(");

		itr = list_iterator_create(txn_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set) {
				xstrcat(assoc_extra, " || ");
				xstrcat(name_extra, " || ");
			}
			xstrfmtcat(assoc_extra, "cluster=\"%s\"", object);

			xstrfmtcat(name_extra, "(name like \"%%\\\"%s\\\"%%\""
				   " || name=\"%s\")"
				   " || (info like \"%%cluster=\\\"%s\\\"%%\")",
				   object, object, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(assoc_extra, ")");
		xstrcat(name_extra, ")");
	}

	if(txn_cond->user_list && list_count(txn_cond->user_list)) {
		set = 0;
		if(assoc_extra)
			xstrcat(assoc_extra, " && (");
		else
			xstrcat(assoc_extra, " where (");

		if(name_extra)
			xstrcat(name_extra, " && (");
		else
			xstrcat(name_extra, "(");

		itr = list_iterator_create(txn_cond->user_list);
		while((object = list_next(itr))) {
			if(set) {
				xstrcat(assoc_extra, " || ");
				xstrcat(name_extra, " || ");
			}
			xstrfmtcat(assoc_extra, "user=\"%s\"", object);

			xstrfmtcat(name_extra, "(name like \"%%\\\"%s\\\"%%\""
				   " || name=\"%s\")"
				   " || (info like \"%%user=\\\"%s\\\"%%\")",
				   object, object, object);

			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(assoc_extra, ")");
		xstrcat(name_extra, ")");
	}

	if(assoc_extra) {
		query = xstrdup_printf("select id from %s%s",
				       assoc_table, assoc_extra);
		xfree(assoc_extra);

		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return NULL;
		}
		xfree(query);

		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		set = 0;

		if(mysql_num_rows(result)) {
			if(name_extra) {
				xstrfmtcat(extra, "(%s) || (", name_extra);
				xfree(name_extra);
			} else
				xstrcat(extra, "(");
			while((row = mysql_fetch_row(result))) {
				if(set)
					xstrcat(extra, " || ");

				xstrfmtcat(extra, "(name like '%%id=%s %%' "
					   "|| name like '%%id=%s)' "
					   "|| name=%s)",
					   row[0], row[0], row[0]);
				set = 1;
			}
			xstrcat(extra, "))");
		} else if(name_extra) {
			xstrfmtcat(extra, "(%s))", name_extra);
			xfree(name_extra);
		}
		mysql_free_result(result);
	}

	/*******************************************/

	if(txn_cond->action_list && list_count(txn_cond->action_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->action_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "action=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->actor_list && list_count(txn_cond->actor_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->actor_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "actor=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->id_list && list_count(txn_cond->id_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->id_list);
		while((object = list_next(itr))) {
			char *ptr = NULL;
			long num = strtol(object, &ptr, 10);
			if ((num == 0) && ptr && ptr[0]) {
				error("Invalid value for txn id (%s)",
				      object);
				xfree(extra);
				list_iterator_destroy(itr);
				return NULL;
			}

			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=%s", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->info_list && list_count(txn_cond->info_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->info_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "info like '%%%s%%'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->name_list && list_count(txn_cond->name_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name like '%%%s%%'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->time_start && txn_cond->time_end) {
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrfmtcat(extra, "timestamp < %d && timestamp >= %d)",
			   txn_cond->time_end, txn_cond->time_start);
	} else if(txn_cond->time_start) {
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrfmtcat(extra, "timestamp >= %d)", txn_cond->time_start);

	} else if(txn_cond->time_end) {
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrfmtcat(extra, "timestamp < %d)", txn_cond->time_end);
	}

	/* make sure we can get the max length out of the database
	 * when grouping the names
	 */
	if(txn_cond->with_assoc_info)
		mysql_db_query(mysql_conn->db_conn,
			       "set session group_concat_max_len=65536;");

empty:
	xfree(tmp);
	xstrfmtcat(tmp, "%s", txn_req_inx[i]);
	for(i=1; i<TXN_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", txn_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s", tmp, txn_table);

	if(extra) {
		xstrfmtcat(query, "%s", extra);
		xfree(extra);
	}
	xstrcat(query, " order by timestamp;");

	xfree(tmp);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	txn_list = list_create(destroy_acct_txn_rec);

	while((row = mysql_fetch_row(result))) {
		acct_txn_rec_t *txn = xmalloc(sizeof(acct_txn_rec_t));

		list_append(txn_list, txn);

		txn->action = atoi(row[TXN_REQ_ACTION]);
		txn->actor_name = xstrdup(row[TXN_REQ_ACTOR]);
		txn->id = atoi(row[TXN_REQ_ID]);
		txn->set_info = xstrdup(row[TXN_REQ_INFO]);
		txn->timestamp = atoi(row[TXN_REQ_TS]);
		txn->where_query = xstrdup(row[TXN_REQ_NAME]);

		if(txn_cond && txn_cond->with_assoc_info
		   && (txn->action == DBD_ADD_ASSOCS
		       || txn->action == DBD_MODIFY_ASSOCS
		       || txn->action == DBD_REMOVE_ASSOCS)) {
			MYSQL_RES *result2 = NULL;
			MYSQL_ROW row2;

			query = xstrdup_printf(
				"select "
				"group_concat(distinct user order by user), "
				"group_concat(distinct acct order by acct), "
				"group_concat(distinct cluster "
				"order by cluster) from %s where %s",
				assoc_table, row[TXN_REQ_NAME]);
			debug4("%d(%d) query\n%s", mysql_conn->conn,
			       __LINE__, query);
			if(!(result2 = mysql_db_query_ret(
				     mysql_conn->db_conn, query, 0))) {
				xfree(query);
				continue;
			}
			xfree(query);

			if((row2 = mysql_fetch_row(result2))) {
				if(row2[0] && row2[0][0])
					txn->users = xstrdup(row2[0]);
				if(row2[1] && row2[1][0])
					txn->accts = xstrdup(row2[1]);
				if(row2[2] && row2[2][0])
					txn->clusters = xstrdup(row2[2]);
			}
			mysql_free_result(result2);
		}
	}
	mysql_free_result(result);

	return txn_list;
}

extern int acct_storage_p_get_usage(mysql_conn_t *mysql_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end)
{
	return mysq_get_usage(mysql_conn, uid, in, type, start, end);
}

extern int acct_storage_p_roll_usage(mysql_conn_t *mysql_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;
	int i = 0;
	time_t my_time = sent_end;
	struct tm start_tm;
	struct tm end_tm;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	char *tmp = NULL;
	time_t last_hour = sent_start;
	time_t last_day = sent_start;
	time_t last_month = sent_start;
	time_t start_time = 0;
  	time_t end_time = 0;
	DEF_TIMERS;

	char *update_req_inx[] = {
		"hourly_rollup",
		"daily_rollup",
		"monthly_rollup"
	};

	enum {
		UPDATE_HOUR,
		UPDATE_DAY,
		UPDATE_MONTH,
		UPDATE_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if(!sent_start) {
		i=0;
		xstrfmtcat(tmp, "%s", update_req_inx[i]);
		for(i=1; i<UPDATE_COUNT; i++) {
			xstrfmtcat(tmp, ", %s", update_req_inx[i]);
		}
		query = xstrdup_printf("select %s from %s",
				       tmp, last_ran_table);
		xfree(tmp);

		debug4("%d(%d) query\n%s", mysql_conn->conn,
		       __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}

		xfree(query);
		row = mysql_fetch_row(result);
		if(row) {
			last_hour = atoi(row[UPDATE_HOUR]);
			last_day = atoi(row[UPDATE_DAY]);
			last_month = atoi(row[UPDATE_MONTH]);
			mysql_free_result(result);
		} else {
			time_t now = time(NULL);
			/* If we don't have any events like adding a
			 * cluster this will not work correctly, so we
			 * will insert now as a starting point.
			 */
			query = xstrdup_printf(
				"set @PS = %d;"
				"select @PS := period_start from %s limit 1;"
				"insert into %s "
				"(hourly_rollup, daily_rollup, monthly_rollup) "
				"values (@PS, @PS, @PS);",
				now, event_table, last_ran_table);

			debug3("%d(%d) query\n%s", mysql_conn->conn,
			       __LINE__, query);
			mysql_free_result(result);
			if(!(result = mysql_db_query_ret(
				     mysql_conn->db_conn, query, 0))) {
				xfree(query);
				return SLURM_ERROR;
			}
			xfree(query);
			row = mysql_fetch_row(result);
			if(!row) {
				debug("No clusters have been added "
				      "not doing rollup");
				mysql_free_result(result);
				return SLURM_SUCCESS;
			}

			last_hour = last_day = last_month = atoi(row[0]);
			mysql_free_result(result);
		}
	}

	if(!my_time)
		my_time = time(NULL);

	/* test month gap */
/* 	last_hour = 1212299999; */
/* 	last_day = 1212217200; */
/* 	last_month = 1212217200; */
/* 	my_time = 1212307200; */

/* 	last_hour = 1211475599; */
/* 	last_day = 1211475599; */
/* 	last_month = 1211475599; */

//	last_hour = 1211403599;
	//	last_hour = 1206946800;
//	last_day = 1207033199;
//	last_day = 1197033199;
//	last_month = 1204358399;

	if(!localtime_r(&last_hour, &start_tm)) {
		error("Couldn't get localtime from hour start %d", last_hour);
		return SLURM_ERROR;
	}

	if(!localtime_r(&my_time, &end_tm)) {
		error("Couldn't get localtime from hour end %d", my_time);
		return SLURM_ERROR;
	}

	/* below and anywhere in a rollup plugin when dealing with
	 * epoch times we need to set the tm_isdst = -1 so we don't
	 * have to worry about the time changes.  Not setting it to -1
	 * will cause problems in the day and month with the date change.
	 */

	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("hour start %s", ctime(&start_time)); */
/* 	info("hour end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	slurm_mutex_lock(&rollup_lock);
	global_last_rollup = end_time;
	slurm_mutex_unlock(&rollup_lock);

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = mysql_hourly_rollup(mysql_conn, start_time, end_time))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER3("hourly_rollup", 5000000);
		/* If we have a sent_end do not update the last_run_table */
		if(!sent_end)
			query = xstrdup_printf("update %s set hourly_rollup=%d",
					       last_ran_table, end_time);
	} else {
		debug2("no need to run this hour %d <= %d",
		       end_time, start_time);
	}

	if(!localtime_r(&last_day, &start_tm)) {
		error("Couldn't get localtime from day %d", last_day);
		return SLURM_ERROR;
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_tm.tm_hour = 0;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("day start %s", ctime(&start_time)); */
/* 	info("day end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = mysql_daily_rollup(mysql_conn, start_time, end_time,
					    archive_data))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER2("daily_rollup");
		if(query && !sent_end)
			xstrfmtcat(query, ", daily_rollup=%d", end_time);
		else if(!sent_end)
			query = xstrdup_printf("update %s set daily_rollup=%d",
					       last_ran_table, end_time);
	} else {
		debug2("no need to run this day %d <= %d",
		       end_time, start_time);
	}

	if(!localtime_r(&last_month, &start_tm)) {
		error("Couldn't get localtime from month %d", last_month);
		return SLURM_ERROR;
	}

	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 1;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_time = mktime(&end_tm);

	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_hour = 0;
	end_tm.tm_mday = 1;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("month start %s", ctime(&start_time)); */
/* 	info("month end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = mysql_monthly_rollup(
			    mysql_conn, start_time, end_time, archive_data))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER2("monthly_rollup");

		if(query && !sent_end)
			xstrfmtcat(query, ", monthly_rollup=%d", end_time);
		else if(!sent_end)
			query = xstrdup_printf(
				"update %s set monthly_rollup=%d",
				last_ran_table, end_time);
	} else {
		debug2("no need to run this month %d <= %d",
		       end_time, start_time);
	}

	if(query) {
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	}
	return rc;
}

extern int clusteracct_storage_p_node_down(mysql_conn_t *mysql_conn,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	uint16_t cpus;
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	char *my_reason;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if(!node_ptr) {
		error("No node_ptr given!");
		return SLURM_ERROR;
	}

	if (slurmctld_conf.fast_schedule && !slurmdbd_conf)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

	if (reason)
		my_reason = reason;
	else
		my_reason = node_ptr->reason;

	debug2("inserting %s(%s) with %u cpus", node_ptr->name, cluster, cpus);

	query = xstrdup_printf(
		"update %s set period_end=%d where cluster=\"%s\" "
		"and period_end=0 and node_name=\"%s\";",
		event_table, event_time, cluster, node_ptr->name);
	/* If you are clean-restarting the controller over and over again you
	 * could get records that are duplicates in the database.  If
	 * this is the case we will zero out the period_end we are
	 * just filled in.  This will cause the last time to be erased
	 * from the last restart, but if you are restarting things
	 * this often the pervious one didn't mean anything anyway.
	 * This way we only get one for the last time we let it run.
	 */
	xstrfmtcat(query,
		   "insert into %s "
		   "(node_name, state, cluster, cpu_count, "
		   "period_start, reason) "
		   "values (\"%s\", %u, \"%s\", %u, %d, \"%s\", %u) "
		   "on duplicate key "
		   "update period_end=0;",
		   event_table, node_ptr->name, node_ptr->node_state, cluster,
		   cpus, event_time, my_reason, reason_uid);
	debug4("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	return rc;
}
extern int clusteracct_storage_p_node_up(mysql_conn_t *mysql_conn,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	char* query;
	int rc = SLURM_SUCCESS;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	query = xstrdup_printf(
		"update %s set period_end=%d where cluster=\"%s\" "
		"and period_end=0 and node_name=\"%s\";",
		event_table, event_time, cluster, node_ptr->name);
	debug4("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	return rc;
}

/* This is only called when not running from the slurmdbd so we can
 * assumes some things like rpc_version.
 */
extern int clusteracct_storage_p_register_ctld(mysql_conn_t *mysql_conn,
					       char *cluster,
					       uint16_t port)
{
	char *query = NULL;
	char *address = NULL;
	char hostname[255];
	time_t now = time(NULL);

	if(slurmdbd_conf)
		fatal("clusteracct_storage_g_register_ctld "
		      "should never be called from the slurmdbd.");

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	info("Registering slurmctld for cluster %s at port %u in database.",
	     cluster, port);
	gethostname(hostname, sizeof(hostname));

	/* check if we are running on the backup controller */
	if(slurmctld_conf.backup_controller
	   && !strcmp(slurmctld_conf.backup_controller, hostname)) {
		address = slurmctld_conf.backup_addr;
	} else
		address = slurmctld_conf.control_addr;

	query = xstrdup_printf(
		"update %s set deleted=0, mod_time=%d, "
		"control_host='%s', control_port=%u, rpc_version=%d "
		"where name='%s';",
		cluster_table, now, address, port,
		SLURMDBD_VERSION,
		cluster);
	xstrfmtcat(query,
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%d, %d, \"%s\", \"%s\", \"%s %u\");",
		   txn_table,
		   now, DBD_MODIFY_CLUSTERS, cluster,
		   slurmctld_conf.slurm_user_name, address, port);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);

	return mysql_db_query(mysql_conn->db_conn, query);
}

extern int clusteracct_storage_p_cluster_cpus(mysql_conn_t *mysql_conn,
					       char *cluster,
					       char *cluster_nodes,
					       uint32_t cpus,
					       time_t event_time)
{
	char* query;
	int rc = SLURM_SUCCESS;
	int first = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

 	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* Record the processor count */
	query = xstrdup_printf(
		"select cpu_count, cluster_nodes from %s where cluster=\"%s\" "
		"and period_end=0 and node_name='' limit 1",
		event_table, cluster);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	/* we only are checking the first one here */
	if(!(row = mysql_fetch_row(result))) {
		debug("We don't have an entry for this machine %s "
		      "most likely a first time running.", cluster);

		/* Get all nodes in a down state and jobs pending or running.
		 * This is for the first time a cluster registers
		 *
		 * We will return ACCOUNTING_FIRST_REG so this
		 * is taken care of since the message thread
		 * may not be up when we run this in the controller or
		 * in the slurmdbd.
		 */
		first = 1;
		goto add_it;
	}

	if(atoi(row[0]) == cpus) {
		debug3("we have the same cpu count as before for %s, "
		       "no need to update the database.", cluster);
		if(cluster_nodes) {
			if(!row[1][0]) {
				debug("Adding cluster nodes '%s' to "
				      "last instance of cluster '%s'.",
				      cluster_nodes, cluster);
				query = xstrdup_printf(
					"update %s set cluster_nodes=\"%s\" "
					"where cluster=\"%s\" "
					"and period_end=0 and node_name=''",
					event_table, cluster_nodes, cluster);
				rc = mysql_db_query(mysql_conn->db_conn, query);
				xfree(query);
				goto end_it;
			} else if(!strcmp(cluster_nodes, row[1])) {
				debug3("we have the same nodes in the cluster "
				       "as before no need to "
				       "update the database.");
				goto end_it;
			}
		} else
			goto end_it;
	} else
		debug("%s has changed from %s cpus to %u",
		      cluster, row[0], cpus);

	/* reset all the entries for this cluster since the cpus
	   changed some of the downed nodes may have gone away.
	   Request them again with ACCOUNTING_FIRST_REG */
	query = xstrdup_printf(
		"update %s set period_end=%d where cluster=\"%s\" "
		"and period_end=0",
		event_table, event_time, cluster);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	first = 1;
	if(rc != SLURM_SUCCESS)
		goto end_it;
add_it:
	query = xstrdup_printf(
		"insert into %s (cluster, cluster_nodes, cpu_count, "
		"period_start, reason) "
		"values (\"%s\", \"%s\", %u, %d, 'Cluster processor count')",
		event_table, cluster, cluster_nodes, cpus, event_time);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
end_it:
	mysql_free_result(result);
	if(first && rc == SLURM_SUCCESS)
		rc = ACCOUNTING_FIRST_REG;

	return rc;
}

extern int clusteracct_storage_p_get_usage(
	mysql_conn_t *mysql_conn, uid_t uid,
	acct_cluster_rec_t *cluster_rec, slurmdbd_msg_type_t type,
	time_t start, time_t end)
{
	return mysq_get_usage(mysql_conn, uid, cluster_rec, type, start, end);
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(mysql_conn_t *mysql_conn,
				       char *cluster_name,
				       struct job_record *job_ptr)
{
	int	rc=SLURM_SUCCESS;
	char	*nodes = NULL, *jname = NULL, *node_inx = NULL;
	int track_steps = 0;
	char *block_id = NULL;
	char *query = NULL;
	int reinit = 0;
	time_t check_time = job_ptr->start_time;
	uint32_t wckeyid = 0;
	int no_cluster = 0;
	int node_cnt = 0;

	if (!job_ptr->details || !job_ptr->details->submit_time) {
		error("mysql_job_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug2("mysql_jobacct_job_start() called");

	/* See what we are hearing about here if no start time. If
	 * this job latest time is before the last roll up we will
	 * need to reset it to look at this job. */
	if(!check_time) {
		check_time = job_ptr->details->begin_time;

		if(!check_time)
			check_time = job_ptr->details->submit_time;
	}

	slurm_mutex_lock(&rollup_lock);
	if(check_time < global_last_rollup) {
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;

		/* check to see if we are hearing about this time for the
		 * first time.
		 */
		query = xstrdup_printf("select id from %s where jobid=%u and "
				       "submit=%d and eligible=%d "
				       "and start=%d;",
				       job_table, job_ptr->job_id,
				       job_ptr->details->submit_time,
				       job_ptr->details->begin_time,
				       job_ptr->start_time);
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		if(!(result =
		     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
			xfree(query);
			slurm_mutex_unlock(&rollup_lock);
			return SLURM_ERROR;
		}
		xfree(query);
		if((row = mysql_fetch_row(result))) {
			mysql_free_result(result);
			debug4("revieved an update for a "
			       "job (%u) already known about",
			       job_ptr->job_id);
			slurm_mutex_unlock(&rollup_lock);
			goto no_rollup_change;
		}
		mysql_free_result(result);

		if(job_ptr->start_time)
			debug("Need to reroll usage from %sJob %u "
			      "from %s started then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, cluster_name);
		else if(job_ptr->details->begin_time)
			debug("Need to reroll usage from %sJob %u "
			      "from %s became eligible then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, cluster_name);
		else
			debug("Need to reroll usage from %sJob %u "
			      "from %s was submitted then and we are just "
			      "now hearing about it.",
			      ctime(&check_time),
			      job_ptr->job_id, cluster_name);

		global_last_rollup = check_time;
		slurm_mutex_unlock(&rollup_lock);

		query = xstrdup_printf("update %s set hourly_rollup=%d, "
				       "daily_rollup=%d, monthly_rollup=%d",
				       last_ran_table, check_time,
				       check_time, check_time);
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	} else
		slurm_mutex_unlock(&rollup_lock);

no_rollup_change:

	if(!cluster_name && job_ptr->assoc_id) {
		no_cluster = 1;
		cluster_name = _get_cluster_from_associd(mysql_conn,
							 job_ptr->assoc_id);
	}


	if (job_ptr->name && job_ptr->name[0])
		jname = job_ptr->name;
	else {
		jname = "allocation";
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
		node_cnt = job_ptr->node_cnt;
		node_inx = job_ptr->network;
	} else {
		char temp_bit[BUF_SIZE];

		if(job_ptr->node_bitmap) {
			node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					   job_ptr->node_bitmap);
		}
#ifdef HAVE_BG
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				     SELECT_JOBDATA_BLOCK_ID,
				     &block_id);
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				     SELECT_JOBDATA_NODE_CNT,
				     &node_cnt);
#else
		node_cnt = job_ptr->node_cnt;
#endif
	}

	/* If there is a start_time get the wckeyid.  If the job is
	 * cancelled before the job starts we also want to grab it. */
	if(job_ptr->assoc_id
	   && (job_ptr->start_time || IS_JOB_CANCELLED(job_ptr)))
		wckeyid = _get_wckeyid(mysql_conn, &job_ptr->wckey,
				       job_ptr->user_id, cluster_name,
				       job_ptr->assoc_id);


	/* We need to put a 0 for 'end' incase of funky job state
	 * files from a hot start of the controllers we call
	 * job_start on jobs we may still know about after
	 * job_flush has been called so we need to restart
	 * them by zeroing out the end.
	 */
	if(!job_ptr->db_index) {
		if(!job_ptr->details->begin_time)
			job_ptr->details->begin_time =
				job_ptr->details->submit_time;
		query = xstrdup_printf(
			"insert into %s "
			"(jobid, associd, wckeyid, uid, "
			"gid, nodelist, resvid, timelimit, ",
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
		if(node_inx)
			xstrcat(query, "node_inx, ");

		xstrfmtcat(query,
			   "eligible, submit, start, name, track_steps, "
			   "state, priority, req_cpus, "
			   "alloc_cpus, alloc_nodes) "
			   "values (%u, %u, %u, %u, %u, \"%s\", %u, %u, ",
			   job_ptr->job_id, job_ptr->assoc_id, wckeyid,
			   job_ptr->user_id, job_ptr->group_id, nodes,
			   job_ptr->resv_id, job_ptr->time_limit);

		if(cluster_name)
			xstrfmtcat(query, "\"%s\", ", cluster_name);
		if(job_ptr->account)
			xstrfmtcat(query, "\"%s\", ", job_ptr->account);
		if(job_ptr->partition)
			xstrfmtcat(query, "\"%s\", ", job_ptr->partition);
		if(block_id)
			xstrfmtcat(query, "\"%s\", ", block_id);
		if(job_ptr->wckey)
			xstrfmtcat(query, "\"%s\", ", job_ptr->wckey);
		if(node_inx)
			xstrfmtcat(query, "\"%s\", ", node_inx);

		xstrfmtcat(query,
			   "%d, %d, %d, \"%s\", %u, %u, %u, %u, %u, %u) "
			   "on duplicate key update "
			   "id=LAST_INSERT_ID(id), state=%u, "
			   "associd=%u, wckeyid=%u, resvid=%u, timelimit=%u",
			   (int)job_ptr->details->begin_time,
			   (int)job_ptr->details->submit_time,
			   (int)job_ptr->start_time,
			   jname, track_steps,
			   job_ptr->job_state & JOB_STATE_BASE,
			   job_ptr->priority, job_ptr->details->min_cpus,
			   job_ptr->total_cpus, node_cnt,
			   job_ptr->job_state & JOB_STATE_BASE,
			   job_ptr->assoc_id, wckeyid, job_ptr->resv_id,
			   job_ptr->time_limit);

		if(job_ptr->account)
			xstrfmtcat(query, ", account=\"%s\"", job_ptr->account);
		if(job_ptr->partition)
			xstrfmtcat(query, ", partition=\"%s\"",
				   job_ptr->partition);
		if(block_id)
			xstrfmtcat(query, ", blockid=\"%s\"", block_id);
		if(job_ptr->wckey)
			xstrfmtcat(query, ", wckey=\"%s\"", job_ptr->wckey);
		if(node_inx)
			xstrfmtcat(query, ", node_inx=\"%s\"", node_inx);

		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	try_again:
		if(!(job_ptr->db_index = mysql_insert_ret_id(
			     mysql_conn->db_conn, query))) {
			if(!reinit) {
				error("It looks like the storage has gone "
				      "away trying to reconnect");
				mysql_close_db_connection(
					&mysql_conn->db_conn);
				mysql_get_db_connection(
					&mysql_conn->db_conn,
					mysql_db_name, mysql_db_info);
				reinit = 1;
				goto try_again;
			} else
				rc = SLURM_ERROR;
		}
	} else {
		query = xstrdup_printf("update %s set nodelist=\"%s\", ",
				       job_table, nodes);

		if(job_ptr->account)
			xstrfmtcat(query, "account=\"%s\", ", job_ptr->account);
		if(job_ptr->partition)
			xstrfmtcat(query, "partition=\"%s\", ",
				   job_ptr->partition);
		if(block_id)
			xstrfmtcat(query, "blockid=\"%s\", ", block_id);
		if(job_ptr->wckey)
			xstrfmtcat(query, "wckey=\"%s\", ", job_ptr->wckey);
		if(node_inx)
			xstrfmtcat(query, "node_inx=\"%s\", ", node_inx);

		xstrfmtcat(query, "start=%d, name=\"%s\", state=%u, "
			   "alloc_cpus=%u, alloc_nodes=%u, "
			   "associd=%u, wckeyid=%u, resvid=%u, timelimit=%u "
			   "where id=%d",
			   (int)job_ptr->start_time,
			   jname, job_ptr->job_state & JOB_STATE_BASE,
			   job_ptr->total_cpus, node_cnt,
			   job_ptr->assoc_id, wckeyid,
			   job_ptr->resv_id, job_ptr->time_limit,
			   job_ptr->db_index);
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
	}

	xfree(block_id);
	xfree(query);
	if(no_cluster)
		xfree(cluster_name);
	return rc;
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(mysql_conn_t *mysql_conn,
					  struct job_record *job_ptr)
{
	char *query = NULL, *nodes = NULL;
	int rc=SLURM_SUCCESS;
	time_t start_time = job_ptr->start_time;

	if (!job_ptr->db_index
	    && (!job_ptr->details || !job_ptr->details->submit_time)) {
		error("mysql_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;
	debug2("mysql_jobacct_job_complete() called");

	/* If we get an error with this just fall through to avoid an
	 * infinite loop
	 */
	if (job_ptr->end_time == 0) {
		debug("mysql_jobacct: job %u never started", job_ptr->job_id);
		return SLURM_SUCCESS;
	} else if(start_time > job_ptr->end_time)
		start_time = 0;

	slurm_mutex_lock(&rollup_lock);
	if(job_ptr->end_time < global_last_rollup) {
		global_last_rollup = job_ptr->end_time;
		slurm_mutex_unlock(&rollup_lock);

		query = xstrdup_printf("update %s set hourly_rollup=%d, "
				       "daily_rollup=%d, monthly_rollup=%d",
				       last_ran_table, job_ptr->end_time,
				       job_ptr->end_time, job_ptr->end_time);
		debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	} else
		slurm_mutex_unlock(&rollup_lock);

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "None assigned";

	if(!job_ptr->db_index) {
		if(!(job_ptr->db_index =
		     _get_db_index(mysql_conn->db_conn,
				   job_ptr->details->submit_time,
				   job_ptr->job_id,
				   job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(mysql_job_start(
				   mysql_conn, NULL, job_ptr) == SLURM_ERROR) {
				error("couldn't add job %u at job completion",
				      job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	query = xstrdup_printf("update %s set start=%d, end=%d, state=%d, "
			       "nodelist=\"%s\", comp_code=%d, "
			       "kill_requid=%d where id=%d",
			       job_table, (int)start_time,
			       (int)job_ptr->end_time,
			       job_ptr->job_state & JOB_STATE_BASE,
			       nodes, job_ptr->exit_code,
			       job_ptr->requid, job_ptr->db_index);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	return rc;
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(mysql_conn_t *mysql_conn,
					struct step_record *step_ptr)
{
	int cpus = 0, tasks = 0, nodes = 0, task_dist = 0;
	int rc=SLURM_SUCCESS;
	char node_list[BUFFER_SIZE];
	char *node_inx = NULL;
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	char *query = NULL;

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("mysql_step_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;
	if(slurmdbd_conf) {
		tasks = step_ptr->job_ptr->details->num_tasks;
		cpus = step_ptr->cpu_count;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
		nodes = step_ptr->step_layout->node_cnt;
		task_dist = step_ptr->step_layout->task_dist;
		node_inx = step_ptr->network;
	} else {
		char temp_bit[BUF_SIZE];

		if(step_ptr->step_node_bitmap) {
			node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					   step_ptr->step_node_bitmap);
		}
#ifdef HAVE_BG
		tasks = cpus = step_ptr->job_ptr->details->min_cpus;
		select_g_select_jobinfo_get(step_ptr->job_ptr->select_jobinfo,
				     SELECT_JOBDATA_IONODES,
				     &ionodes);
		if(ionodes) {
			snprintf(node_list, BUFFER_SIZE,
				 "%s[%s]", step_ptr->job_ptr->nodes, ionodes);
			xfree(ionodes);
		} else
			snprintf(node_list, BUFFER_SIZE, "%s",
				 step_ptr->job_ptr->nodes);
		select_g_select_jobinfo_get(step_ptr->job_ptr->select_jobinfo,
				     SELECT_JOBDATA_NODE_CNT,
				     &nodes);
#else
		if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
			tasks = cpus = step_ptr->job_ptr->total_cpus;
			snprintf(node_list, BUFFER_SIZE, "%s",
				 step_ptr->job_ptr->nodes);
			nodes = step_ptr->job_ptr->node_cnt;
		} else {
			cpus = step_ptr->cpu_count;
			tasks = step_ptr->step_layout->task_cnt;
			nodes = step_ptr->step_layout->node_cnt;
			task_dist = step_ptr->step_layout->task_dist;
			snprintf(node_list, BUFFER_SIZE, "%s",
				 step_ptr->step_layout->node_list);
		}
#endif
	}

	if(!step_ptr->job_ptr->db_index) {
		if(!(step_ptr->job_ptr->db_index =
		     _get_db_index(mysql_conn->db_conn,
				   step_ptr->job_ptr->details->submit_time,
				   step_ptr->job_ptr->job_id,
				   step_ptr->job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(mysql_job_start(
				   mysql_conn, NULL, step_ptr->job_ptr)
			   == SLURM_ERROR) {
				error("couldn't add job %u at step start",
				      step_ptr->job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	/* we want to print a -1 for the requid so leave it a
	   %d */
	query = xstrdup_printf(
		"insert into %s (id, stepid, start, name, state, "
		"cpus, nodes, tasks, nodelist, node_inx, task_dist) "
		"values (%d, %d, %d, \"%s\", %d, %d, %d, %d, "
		"\"%s\", \"%s\", %d) "
		"on duplicate key update cpus=%d, nodes=%d, "
		"tasks=%d, end=0, state=%d, "
		"nodelist=\"%s\", node_inx=\"%s\", task_dist=%d",
		step_table, step_ptr->job_ptr->db_index,
		step_ptr->step_id,
		(int)step_ptr->start_time, step_ptr->name,
		JOB_RUNNING, cpus, nodes, tasks, node_list, node_inx, task_dist,
		cpus, nodes, tasks, JOB_RUNNING,
		node_list, node_inx, task_dist);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	return rc;
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(mysql_conn_t *mysql_conn,
					   struct step_record *step_ptr)
{
	time_t now;
	int elapsed;
	int comp_status;
	int cpus = 0, tasks = 0;
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	struct jobacctinfo dummy_jobacct;
	double ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	double ave_cpu = 0, ave_cpu2 = 0;
	char *query = NULL;
	int rc =SLURM_SUCCESS;
	uint32_t exit_code = 0;

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("mysql_step_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	if (jobacct == NULL) {
		/* JobAcctGather=jobacct_gather/none, no data to process */
		memset(&dummy_jobacct, 0, sizeof(dummy_jobacct));
		jobacct = &dummy_jobacct;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if(slurmdbd_conf) {
		now = step_ptr->job_ptr->end_time;
		tasks = step_ptr->job_ptr->details->num_tasks;
		cpus = step_ptr->cpu_count;
	} else {
		now = time(NULL);
#ifdef HAVE_BG
		tasks = cpus = step_ptr->job_ptr->details->min_cpus;

#else
		if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
			tasks = cpus = step_ptr->job_ptr->total_cpus;
		else {
			cpus = step_ptr->cpu_count;
			tasks = step_ptr->step_layout->task_cnt;
		}
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
		ave_vsize = (double)jobacct->tot_vsize;
		ave_vsize /= (double)cpus;
		ave_rss = (double)jobacct->tot_rss;
		ave_rss /= (double)cpus;
		ave_pages = (double)jobacct->tot_pages;
		ave_pages /= (double)cpus;
		ave_cpu = (double)jobacct->tot_cpu;
		ave_cpu /= (double)cpus;
		ave_cpu /= (double)100;
	}

	if(jobacct->min_cpu != NO_VAL) {
		ave_cpu2 = (double)jobacct->min_cpu;
		ave_cpu2 /= (double)100;
	}

	if(!step_ptr->job_ptr->db_index) {
		if(!(step_ptr->job_ptr->db_index =
		     _get_db_index(mysql_conn->db_conn,
				   step_ptr->job_ptr->details->submit_time,
				   step_ptr->job_ptr->job_id,
				   step_ptr->job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(mysql_job_start(mysql_conn, NULL,
						       step_ptr->job_ptr)
			   == SLURM_ERROR) {
				error("couldn't add job %u "
				      "at step completion",
				      step_ptr->job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	query = xstrdup_printf(
		"update %s set end=%d, state=%d, "
		"kill_requid=%d, comp_code=%d, "
		"user_sec=%u, user_usec=%u, "
		"sys_sec=%u, sys_usec=%u, "
		"max_vsize=%u, max_vsize_task=%u, "
		"max_vsize_node=%u, ave_vsize=%f, "
		"max_rss=%u, max_rss_task=%u, "
		"max_rss_node=%u, ave_rss=%f, "
		"max_pages=%u, max_pages_task=%u, "
		"max_pages_node=%u, ave_pages=%f, "
		"min_cpu=%f, min_cpu_task=%u, "
		"min_cpu_node=%u, ave_cpu=%f "
		"where id=%d and stepid=%u",
		step_table, (int)now,
		comp_status,
		step_ptr->requid,
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
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	return rc;
}

/*
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(mysql_conn_t *mysql_conn,
				     struct job_record *job_ptr)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;
	bool suspended = false;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;
	if(!job_ptr->db_index) {
		if(!(job_ptr->db_index =
		     _get_db_index(mysql_conn->db_conn,
				   job_ptr->details->submit_time,
				   job_ptr->job_id,
				   job_ptr->assoc_id))) {
			/* If we get an error with this just fall
			 * through to avoid an infinite loop
			 */
			if(mysql_job_start(
				   mysql_conn, NULL, job_ptr) == SLURM_ERROR) {
				error("couldn't suspend job %u",
				      job_ptr->job_id);
				return SLURM_SUCCESS;
			}
		}
	}

	if (job_ptr->job_state == JOB_SUSPENDED)
		suspended = true;

	xstrfmtcat(query,
		   "update %s set suspended=%d-suspended, state=%d "
		   "where id=%d;",
		   job_table, (int)job_ptr->suspend_time,
		   job_ptr->job_state & JOB_STATE_BASE,
		   job_ptr->db_index);
	if(suspended)
		xstrfmtcat(query,
			   "insert into %s (id, associd, start, end) "
			   "values (%u, %u, %d, 0);",
			   suspend_table, job_ptr->db_index, job_ptr->assoc_id,
			   (int)job_ptr->suspend_time);
	else
		xstrfmtcat(query,
			   "update %s set end=%d where id=%u && end=0;",
			   suspend_table, (int)job_ptr->suspend_time,
			   job_ptr->db_index);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);

	rc = mysql_db_query(mysql_conn->db_conn, query);

	xfree(query);
	if(rc != SLURM_ERROR) {
		xstrfmtcat(query,
			   "update %s set suspended=%u-suspended, "
			   "state=%d where id=%u and end=0",
			   step_table, (int)job_ptr->suspend_time,
			   job_ptr->job_state, job_ptr->db_index);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	}

	return rc;
}

/*
 * get info from the storage
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(mysql_conn_t *mysql_conn,
					    uid_t uid,
					    acct_job_cond_t *job_cond)
{
	List job_list = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS) {
		return NULL;
	}
	job_list = mysql_jobacct_process_get_jobs(mysql_conn, uid, job_cond);

	return job_list;
}

/*
 * expire old info from the storage
 */
extern int jobacct_storage_p_archive(mysql_conn_t *mysql_conn,
				     acct_archive_cond_t *arch_cond)
{
	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return mysql_jobacct_process_archive(mysql_conn, arch_cond);
}

/*
 * load old info into the storage
 */
extern int jobacct_storage_p_archive_load(mysql_conn_t *mysql_conn,
					  acct_archive_rec_t *arch_rec)
{
	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return mysql_jobacct_process_archive_load(mysql_conn, arch_rec);
}

extern int acct_storage_p_update_shares_used(mysql_conn_t *mysql_conn,
					     List shares_used)
{
	/* No plans to have the database hold the used shares */
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(
	mysql_conn_t *mysql_conn, char *cluster, time_t event_time)
{
	int rc = SLURM_SUCCESS;
	/* put end times for a clean start */
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	char *id_char = NULL;
	char *suspended_char = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* First we need to get the id's and states so we can clean up
	 * the suspend table and the step table
	 */
	query = xstrdup_printf(
		"select distinct t1.id, t1.state from %s as t1 where "
		"t1.cluster=\"%s\" && t1.end=0;",
		job_table, cluster);
	debug3("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);
	if(!(result =
	     mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	while((row = mysql_fetch_row(result))) {
		int state = atoi(row[1]);
		if(state == JOB_SUSPENDED) {
			if(suspended_char)
				xstrfmtcat(suspended_char, " || id=%s", row[0]);
			else
				xstrfmtcat(suspended_char, "id=%s", row[0]);
		}

		if(id_char)
			xstrfmtcat(id_char, " || id=%s", row[0]);
		else
			xstrfmtcat(id_char, "id=%s", row[0]);
	}
	mysql_free_result(result);

	if(suspended_char) {
		xstrfmtcat(query,
			   "update %s set suspended=%d-suspended where %s;",
			   job_table, event_time, suspended_char);
		xstrfmtcat(query,
			   "update %s set suspended=%d-suspended where %s;",
			   step_table, event_time, suspended_char);
		xstrfmtcat(query,
			   "update %s set end=%d where (%s) && end=0;",
			   suspend_table, event_time, suspended_char);
		xfree(suspended_char);
	}
	if(id_char) {
		xstrfmtcat(query,
			   "update %s set state=%d, end=%u where %s;",
			   job_table, JOB_CANCELLED, event_time, id_char);
		xstrfmtcat(query,
			   "update %s set state=%d, end=%u where %s;",
			   step_table, JOB_CANCELLED, event_time, id_char);
		xfree(id_char);
	}
/* 	query = xstrdup_printf("update %s as t1, %s as t2 set " */
/* 			       "t1.state=%u, t1.end=%u where " */
/* 			       "t2.id=t1.associd and t2.cluster=\"%s\" " */
/* 			       "&& t1.end=0;", */
/* 			       job_table, assoc_table, JOB_CANCELLED,  */
/* 			       event_time, cluster); */
	if(query) {
		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);

		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	}

	return rc;
}
