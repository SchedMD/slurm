/*****************************************************************************\
 *  accounting_storage_pgsql.c - accounting interface to pgsql.
 *
 *  $Id: accounting_storage_pgsql.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 he Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#include "as_pg_common.h"

/*
 * NOTE:
 * administrator must do the following work:
 * 1. create user slurm
 * 2. create slurm_acct_db with user slurm
 * 3. create PL/pgSQL in slurm_acct_db with user slurm
 */

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
 * minimum version for their plugins as the job accounting API
 * matures.
 */
const char plugin_name[] = "Accounting storage PGSQL plugin";
const char plugin_type[] = "accounting_storage/pgsql";
const uint32_t plugin_version = 100;

static pgsql_db_info_t *pgsql_db_info = NULL;
static char *pgsql_db_name = NULL;

List as_pg_cluster_list = NULL;
pthread_mutex_t as_pg_cluster_list_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * _pgsql_acct_create_db_info - get info from config to connect db
 *
 * TODO: The logic is really the same with _as_mysql_acct_create_db_info(), but
 *   a common db_info_t struct is needed to make this code shared between mysql
 *   and pgsql.
 *
 * RET: db info
 */
static pgsql_db_info_t *_pgsql_acct_create_db_info()
{
	pgsql_db_info_t *db_info = xmalloc(sizeof(pgsql_db_info_t));
	db_info->port = slurm_get_accounting_storage_port();
	if (!db_info->port) {
		db_info->port = DEFAULT_PGSQL_PORT;
		slurm_set_accounting_storage_port(db_info->port);
	}
	db_info->host = slurm_get_accounting_storage_host();
	db_info->backup = slurm_get_accounting_storage_backup_host();

	db_info->user = slurm_get_accounting_storage_user();
	db_info->pass = slurm_get_accounting_storage_pass();
	return db_info;
}


/* _pgsql_acct_check_tables - check tables and functions in database */
static int
_pgsql_acct_check_public_tables(PGconn *db_conn)
{
	int rc = SLURM_SUCCESS;

	if (rc == SLURM_SUCCESS)
		rc = check_acct_tables(db_conn);
	if (rc == SLURM_SUCCESS)
		rc = check_cluster_tables(db_conn);
	if (rc == SLURM_SUCCESS)
		rc = check_qos_tables(db_conn);
	if (rc == SLURM_SUCCESS)
		rc = check_txn_tables(db_conn);
	if (rc == SLURM_SUCCESS)
		rc = check_user_tables(db_conn);

	return rc;
}

static List
_get_cluster_names(PGconn *db_conn)
{
	DEF_VARS;
	List ret_list = NULL;

	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0",
			       cluster_table);
	result = pgsql_db_query_ret(db_conn, query);
	xfree(query);
	if (!result)
		return NULL;

	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		if (! ISEMPTY(0))
			list_append(ret_list, xstrdup(ROW(0)));
	} END_EACH_ROW;
	PQclear(result);
	return ret_list;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
	int rc = SLURM_SUCCESS;
	PGconn *db_conn = NULL;

	/* since this can be loaded from many different places
	   only tell us once. */
	if (!first)
		return SLURM_SUCCESS;
	first = 0;

	if (!slurmdbd_conf) {
		char *cluster_name = NULL;
		if (!(cluster_name = slurm_get_cluster_name()))
			fatal("%s requires ClusterName in slurm.conf",
			      plugin_name);
		xfree(cluster_name);
	}

	pgsql_db_info = _pgsql_acct_create_db_info();
/* 	pgsql_db_info = acct_create_db_info(DEFAULT_PGSQL_PORT); */
	pgsql_db_name = acct_get_db_name();

	debug2("pgsql_connect() called for db %s", pgsql_db_name);
	pgsql_get_db_connection(&db_conn, pgsql_db_name, pgsql_db_info);
	pgsql_db_start_transaction(db_conn);
	rc = _pgsql_acct_check_public_tables(db_conn);
	if (rc == SLURM_SUCCESS) {
		if (pgsql_db_commit(db_conn)) {
			error("commit failed, meaning %s failed", plugin_name);
			rc = SLURM_ERROR;
		} else
			verbose("%s loaded", plugin_name);
	} else {
		verbose("%s failed", plugin_name);
		if (pgsql_db_rollback(db_conn))
			error("rollback failed");
	}

	slurm_mutex_lock(&as_pg_cluster_list_lock);
	as_pg_cluster_list = _get_cluster_names(db_conn);
	if (!as_pg_cluster_list) {
		error("Failed to get cluster names");
		rc = SLURM_ERROR;
	}
	slurm_mutex_unlock(&as_pg_cluster_list_lock);

	pgsql_close_db_connection(&db_conn);

	return rc;
}

extern int fini ( void )
{
	destroy_pgsql_db_info(pgsql_db_info);
	xfree(pgsql_db_name);
	xfree(default_qos_str);
	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(const slurm_trigger_callbacks_t *cb,
					   int conn_num,bool rollback,
					   char *cluster_name)
{
	pgsql_conn_t *pg_conn = xmalloc(sizeof(pgsql_conn_t));

	if (!pgsql_db_info)
		init();

	debug2("as/pg: get_connection: request new connection: %d", rollback);

	pg_conn->rollback = rollback;
	pg_conn->conn = conn_num;
	pg_conn->cluster_name = xstrdup(cluster_name);
	pg_conn->update_list = list_create(slurmdb_destroy_update_object);
	pg_conn->cluster_changed = 0;

	errno = SLURM_SUCCESS;
	pgsql_get_db_connection(&pg_conn->db_conn,
				pgsql_db_name, pgsql_db_info);

	if (pg_conn->db_conn && rollback) {
		pgsql_db_start_transaction(pg_conn->db_conn);
	}
	return (void *)pg_conn;
}

extern int acct_storage_p_close_connection(pgsql_conn_t **pg_conn)
{
	if (!pg_conn || !*pg_conn)
		return SLURM_SUCCESS;

	acct_storage_p_commit((*pg_conn), 0); /* discard changes */
	pgsql_close_db_connection(&(*pg_conn)->db_conn);
	list_destroy((*pg_conn)->update_list);
	xfree((*pg_conn)->cluster_name);
	xfree((*pg_conn));

	return SLURM_SUCCESS;
}

extern int acct_storage_p_commit(pgsql_conn_t *pg_conn, bool commit)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug4("as/pg: commit: got %d commits",
	       list_count(pg_conn->update_list));

	if (pg_conn->rollback) {
		if (!commit) {
			if (pgsql_db_rollback(pg_conn->db_conn)) {
				error("as/pg: commit: rollback failed");
				return SLURM_ERROR;
			}
		} else {
			if (pgsql_db_commit(pg_conn->db_conn)) {
				error("as/pg: commit: commit failed");
				return SLURM_ERROR;
			}
		}
		/* start new transaction after commit/rollback */
		pgsql_db_start_transaction(pg_conn->db_conn);
	}

	if (commit && list_count(pg_conn->update_list)) {
		query = xstrdup_printf(
			"SELECT name, control_host, control_port, rpc_version "
			"  FROM %s WHERE deleted=0 AND control_port!=0",
			cluster_table);
		result = DEF_QUERY_RET;
		if (!result)
			goto skip;

		FOR_EACH_ROW {
			slurmdb_send_accounting_update(pg_conn->update_list,
						       ROW(0), ROW(1),
						       atoi(ROW(2)),
						       atoi(ROW(3)));
		} END_EACH_ROW;
		PQclear(result);
	skip:
		assoc_mgr_update(pg_conn->update_list);

		/* remove clusters */
		slurm_mutex_lock(&as_pg_cluster_list_lock);
		if (pg_conn->cluster_changed) {
			list_destroy(as_pg_cluster_list);
			as_pg_cluster_list = _get_cluster_names(
				pg_conn->db_conn);
			if (!as_pg_cluster_list) {
				error("Failed to get cluster names");
				rc = SLURM_ERROR;
			}
			pg_conn->cluster_changed = 0;
		}
		slurm_mutex_unlock(&as_pg_cluster_list_lock);
	}

	list_flush(pg_conn->update_list);
	return rc;
}

extern int acct_storage_p_add_users(pgsql_conn_t *pg_conn, uint32_t uid,
				    List user_list)
{
	return as_pg_add_users(pg_conn, uid, user_list);
}

extern int acct_storage_p_add_coord(pgsql_conn_t *pg_conn, uint32_t uid,
				    List acct_list, slurmdb_user_cond_t *user_cond)
{
	return as_pg_add_coord(pg_conn, uid, acct_list, user_cond);
}

extern int acct_storage_p_add_accts(pgsql_conn_t *pg_conn, uint32_t uid,
				    List acct_list)
{
	return as_pg_add_accts(pg_conn, uid, acct_list);
}

extern int acct_storage_p_add_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
				       List cluster_list)
{
	return as_pg_add_clusters(pg_conn, uid, cluster_list);
}

extern int acct_storage_p_add_associations(pgsql_conn_t *pg_conn, uint32_t uid,
					   List association_list)
{
	return as_pg_add_associations(pg_conn, uid, association_list);
}

extern int acct_storage_p_add_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				  List qos_list)
{
	return as_pg_add_qos(pg_conn, uid, qos_list);
}

extern int acct_storage_p_add_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
				     List wckey_list)
{
	return as_pg_add_wckeys(pg_conn, uid, wckey_list);
}

extern int acct_storage_p_add_reservation(pgsql_conn_t *pg_conn,
					  slurmdb_reservation_rec_t *resv)
{
	return as_pg_add_reservation(pg_conn, resv);
}

extern List acct_storage_p_modify_users(pgsql_conn_t *pg_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond,
					slurmdb_user_rec_t *user)
{
	return as_pg_modify_users(pg_conn, uid, user_cond, user);
}

extern List acct_storage_p_modify_accts(pgsql_conn_t *pg_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond,
					slurmdb_account_rec_t *acct)
{
	return as_pg_modify_accounts(pg_conn, uid, acct_cond, acct);
}

extern List acct_storage_p_modify_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond,
					   slurmdb_cluster_rec_t *cluster)
{
	return as_pg_modify_clusters(pg_conn, uid, cluster_cond, cluster);
}

extern List acct_storage_p_modify_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond,
	slurmdb_association_rec_t *assoc)
{
	return as_pg_modify_associations(pg_conn, uid, assoc_cond, assoc);
}

extern List acct_storage_p_modify_job(pgsql_conn_t *pg_conn, uint32_t uid,
				      slurmdb_job_modify_cond_t *job_cond,
				      slurmdb_job_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos)
{
	return as_pg_modify_qos(pg_conn, uid, qos_cond, qos);
}

extern List acct_storage_p_modify_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond,
					 slurmdb_wckey_rec_t *wckey)
{
	return as_pg_modify_wckeys(pg_conn, uid, wckey_cond, wckey);
}

extern int acct_storage_p_modify_reservation(pgsql_conn_t *pg_conn,
					     slurmdb_reservation_rec_t *resv)
{
	return as_pg_modify_reservation(pg_conn, resv);
}

extern List acct_storage_p_remove_users(pgsql_conn_t *pg_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond)
{
	return as_pg_remove_users(pg_conn, uid, user_cond);
}

extern List acct_storage_p_remove_coord(pgsql_conn_t *pg_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_cond)
{
	return as_pg_remove_coord(pg_conn, uid, acct_list, user_cond);
}

extern List acct_storage_p_remove_accts(pgsql_conn_t *pg_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond)
{
	return as_pg_remove_accts(pg_conn, uid, acct_cond);
}

extern List acct_storage_p_remove_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond)
{
	return as_pg_remove_clusters(pg_conn, uid, cluster_cond);
}

extern List acct_storage_p_remove_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond)
{
	return as_pg_remove_associations(pg_conn, uid, assoc_cond);
}

extern List acct_storage_p_remove_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond)
{
	return as_pg_remove_qos(pg_conn, uid, qos_cond);
}

extern List acct_storage_p_remove_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond)
{
	return as_pg_remove_wckeys(pg_conn, uid, wckey_cond);
}

extern int acct_storage_p_remove_reservation(pgsql_conn_t *pg_conn,
					     slurmdb_reservation_rec_t *resv)
{
	return as_pg_remove_reservation(pg_conn, resv);
}

extern List acct_storage_p_get_users(pgsql_conn_t *pg_conn, uid_t uid,
				     slurmdb_user_cond_t *user_cond)
{
	return as_pg_get_users(pg_conn, uid, user_cond);
}

extern List acct_storage_p_get_accts(pgsql_conn_t *pg_conn, uid_t uid,
				     slurmdb_account_cond_t *acct_cond)
{
	return as_pg_get_accts(pg_conn, uid, acct_cond);
}

extern List acct_storage_p_get_clusters(pgsql_conn_t *pg_conn, uid_t uid,
					slurmdb_cluster_cond_t *cluster_cond)
{
	return as_pg_get_clusters(pg_conn, uid, cluster_cond);
}

extern List acct_storage_p_get_config(pgsql_conn_t *pg_conn, char *config_name)
{
	return NULL;
}

extern List acct_storage_p_get_associations(pgsql_conn_t *pg_conn, uid_t uid,
					    slurmdb_association_cond_t *assoc_cond)
{
	return as_pg_get_associations(pg_conn, uid, assoc_cond);
}

extern List acct_storage_p_get_events(pgsql_conn_t *pg_conn, uint32_t uid,
				      slurmdb_event_cond_t *event_cond)
{
	return as_pg_get_events(pg_conn, uid, event_cond);
}

extern List acct_storage_p_get_problems(pgsql_conn_t *pg_conn, uid_t uid,
					slurmdb_association_cond_t *assoc_q)
{
	return as_pg_get_problems(pg_conn, uid, assoc_q);
}

extern List acct_storage_p_get_qos(pgsql_conn_t *pg_conn, uid_t uid,
				   slurmdb_qos_cond_t *qos_cond)
{
	return as_pg_get_qos(pg_conn, uid, qos_cond);
}

extern List acct_storage_p_get_wckeys(pgsql_conn_t *pg_conn, uid_t uid,
				      slurmdb_wckey_cond_t *wckey_cond)
{
	return as_pg_get_wckeys(pg_conn, uid, wckey_cond);
}

extern List acct_storage_p_get_reservations(pgsql_conn_t *pg_conn, uid_t uid,
					    slurmdb_reservation_cond_t *resv_cond)
{
	return as_pg_get_reservations(pg_conn, uid, resv_cond);
}

extern List acct_storage_p_get_txn(pgsql_conn_t *pg_conn, uid_t uid,
				   slurmdb_txn_cond_t *txn_cond)
{
	return as_pg_get_txn(pg_conn, uid, txn_cond);
}

extern int acct_storage_p_get_usage(pgsql_conn_t *pg_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end)
{
	return as_pg_get_usage(pg_conn, uid, in, type, start, end);
}

extern int acct_storage_p_roll_usage(pgsql_conn_t *pg_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data)
{
	return as_pg_roll_usage(pg_conn, sent_start, sent_end, archive_data);
}

extern int clusteracct_storage_p_node_down(pgsql_conn_t *pg_conn,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return cs_pg_node_down(pg_conn, node_ptr, event_time,
			       reason, reason_uid);
}
extern int clusteracct_storage_p_node_up(pgsql_conn_t *pg_conn,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return cs_pg_node_up(pg_conn, node_ptr, event_time);
}

extern int clusteracct_storage_p_register_ctld(pgsql_conn_t *pg_conn,
					       uint16_t port)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return cs_pg_register_ctld(pg_conn, pg_conn->cluster_name, port);
}

extern int clusteracct_storage_p_register_disconn_ctld(
	pgsql_conn_t *pg_conn, char *control_host)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_fini_ctld(void *db_conn,
					   char *ip, uint16_t port,
					   char *cluster_nodes)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_cpus(pgsql_conn_t *pg_conn,
					      char *cluster_nodes,
					      uint32_t cpus,
					      time_t event_time)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return cs_pg_cluster_cpus(pg_conn, cluster_nodes,
				  cpus, event_time);
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(pgsql_conn_t *pg_conn,
				       struct job_record *job_ptr)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return js_pg_job_start(pg_conn, job_ptr);
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(pgsql_conn_t *pg_conn,
					  struct job_record *job_ptr)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return js_pg_job_complete(pg_conn, job_ptr);
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(pgsql_conn_t *pg_conn,
					struct step_record *step_ptr)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return js_pg_step_start(pg_conn, step_ptr);
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(pgsql_conn_t *pg_conn,
					   struct step_record *step_ptr)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return js_pg_step_complete(pg_conn, step_ptr);
}

/*
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(pgsql_conn_t *pg_conn,
				     struct job_record *job_ptr)
{
	if (!pg_conn->cluster_name) {
		error("%s:%d no cluster name", THIS_FILE, __LINE__);
		return SLURM_ERROR;
	}

	return js_pg_suspend(pg_conn, 0, job_ptr);
}

/*
 * get info from the storage
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(pgsql_conn_t *pg_conn, uid_t uid,
					    slurmdb_job_cond_t *job_cond)
{
	return js_pg_get_jobs_cond(pg_conn, uid, job_cond);
}

/*
 * expire old info from the storage
 */
extern int jobacct_storage_p_archive(pgsql_conn_t *pg_conn,
				     slurmdb_archive_cond_t *arch_cond)
{
	return js_pg_archive(pg_conn, arch_cond);
}

/*
 * load old info into the storage
 */
extern int jobacct_storage_p_archive_load(pgsql_conn_t *pg_conn,
					  slurmdb_archive_rec_t *arch_rec)
{
	return js_pg_archive_load(pg_conn, arch_rec);
}


extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used)
{
	/* This definitely needs to be fleshed out.
	 * Go through the list of shares_used_object_t objects and store them */
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(pgsql_conn_t *pg_conn,
						time_t event_time)
{
	return as_pg_flush_jobs_on_cluster(pg_conn, event_time);
}
