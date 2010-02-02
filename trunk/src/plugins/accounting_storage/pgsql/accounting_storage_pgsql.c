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

#include "common.h"

/*
 * NOTE:
 * administrator must do the following work:
 * 1. create user slurm
 * 2. create slurm_acct_db with user slurm
 * 3. create PL/pgSQL in slurm_acct_db with user postgres
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
 * minimum versions for their plugins as the job accounting API
 * matures.
 */
const char plugin_name[] = "Accounting storage PGSQL plugin";
const char plugin_type[] = "accounting_storage/pgsql";
const uint32_t plugin_version = 100;

static pgsql_db_info_t *pgsql_db_info = NULL;
static char *pgsql_db_name = NULL;

/*
 * _pgsql_acct_create_db_info - get info from config to connect db
 *
 * RET: db info
 */
static pgsql_db_info_t *_pgsql_acct_create_db_info()
{
	pgsql_db_info_t *db_info = xmalloc(sizeof(pgsql_db_info_t));
	db_info->port = slurm_get_accounting_storage_port();
	/* it turns out it is better if using defaults to let postgres
	   handle them on it's own terms */
	if(!db_info->port) {
		db_info->port = DEFAULT_PGSQL_PORT;
		slurm_set_accounting_storage_port(db_info->port);
	}
	db_info->host = slurm_get_accounting_storage_host();
	if(!db_info->host)
		db_info->host = xstrdup("localhost");
	db_info->user = slurm_get_accounting_storage_user();
	db_info->pass = slurm_get_accounting_storage_pass();
	return db_info;
}

/*
 * _pgsql_acct_check_tables - check tables and functions in database
 *
 * IN db_conn: database connection
 * IN user: database owner
 * RET: error code
 */
static int
_pgsql_acct_check_tables(PGconn *db_conn, char *user)
{
	int rc = SLURM_SUCCESS;

	if (rc == SLURM_SUCCESS)
		rc = check_acct_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_assoc_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_clusteracct_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_cluster_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_jobacct_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_qos_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_resv_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_txn_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_usage_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_user_tables(db_conn, user);
	if (rc == SLURM_SUCCESS)
		rc = check_wckey_tables(db_conn, user);

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
	PGconn *acct_pgsql_db = NULL;
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

	pgsql_db_info = _pgsql_acct_create_db_info();

	location = slurm_get_accounting_storage_loc();
	if(!location)
		pgsql_db_name = xstrdup(DEFAULT_ACCOUNTING_DB);
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
			pgsql_db_name = xstrdup(DEFAULT_ACCOUNTING_DB);
			xfree(location);
		} else
			pgsql_db_name = location;
	}

	debug2("pgsql_connect() called for db %s", pgsql_db_name);
	pgsql_get_db_connection(&acct_pgsql_db, pgsql_db_name, pgsql_db_info);
	rc = _pgsql_acct_check_tables(acct_pgsql_db, pgsql_db_info->user);
	pgsql_close_db_connection(&acct_pgsql_db);

	if(rc == SLURM_SUCCESS)
		verbose("%s loaded", plugin_name);
	else
		verbose("%s failed", plugin_name);

	return rc;
}

extern int fini ( void )
{
	destroy_pgsql_db_info(pgsql_db_info);
	xfree(pgsql_db_name);
	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(bool make_agent, int conn_num,
					   bool rollback)
{
	pgsql_conn_t *pg_conn = xmalloc(sizeof(pgsql_conn_t));

	if(!pgsql_db_info)
		init();

	debug2("as/pg: get_connection: request new connection");

	pg_conn->rollback = rollback;
	pg_conn->conn = conn_num;
	pg_conn->update_list = list_create(destroy_acct_update_object);

	errno = SLURM_SUCCESS;
	pgsql_get_db_connection(&pg_conn->db_conn,
				pgsql_db_name, pgsql_db_info);

	if(pg_conn->db_conn && rollback) {
		pgsql_db_start_transaction(pg_conn->db_conn);
	}
	return (void *)pg_conn;
}

extern int acct_storage_p_close_connection(pgsql_conn_t **pg_conn)
{
	if(!pg_conn || !*pg_conn)
		return SLURM_SUCCESS;

	acct_storage_p_commit((*pg_conn), 0); /* discard changes */
	pgsql_close_db_connection(&(*pg_conn)->db_conn);
	list_destroy((*pg_conn)->update_list);
	xfree((*pg_conn));

	return SLURM_SUCCESS;
}

extern int acct_storage_p_commit(pgsql_conn_t *pg_conn, bool commit)
{
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	debug4("as/pg: commit: got %d commits",
	       list_count(pg_conn->update_list));

	if(pg_conn->rollback) {
		if(!commit) {
			if(pgsql_db_rollback(pg_conn->db_conn)) {
				error("as/pg: commit: rollback failed");
				return SLURM_ERROR;
			}
		} else {
			if(pgsql_db_commit(pg_conn->db_conn)) {
				error("as/pg: commit: commit failed");
				return SLURM_ERROR;
			}
		}
		/* start new transaction after commit/rollback */
		pgsql_db_start_transaction(pg_conn->db_conn);
	}

	if(commit && list_count(pg_conn->update_list)) {
		char *query;
		PGresult *result;

		query = xstrdup_printf(
			"SELECT name, control_host, control_port, rpc_version "
			"  FROM %s WHERE deleted=0 AND control_port!=0",
			cluster_table);
		result = DEF_QUERY_RET;
		if (!result)
			goto skip;

		FOR_EACH_ROW {
			send_accounting_update(pg_conn->update_list,
					       ROW(0), ROW(1),
					       atoi(ROW(2)),
					       atoi(ROW(3)));
		} END_EACH_ROW;
		PQclear(result);
	skip:
		update_assoc_mgr(pg_conn->update_list);
	}

	list_flush(pg_conn->update_list);
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(pgsql_conn_t *pg_conn, uint32_t uid,
				    List user_list)
{
	return as_p_add_users(pg_conn, uid, user_list);
}

extern int acct_storage_p_add_coord(pgsql_conn_t *pg_conn, uint32_t uid,
				    List acct_list, acct_user_cond_t *user_cond)
{
	return as_p_add_coord(pg_conn, uid, acct_list, user_cond);
}

extern int acct_storage_p_add_accts(pgsql_conn_t *pg_conn, uint32_t uid,
				    List acct_list)
{
	return as_p_add_accts(pg_conn, uid, acct_list);
}

extern int acct_storage_p_add_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
				       List cluster_list)
{
	return as_p_add_clusters(pg_conn, uid, cluster_list);
}

extern int acct_storage_p_add_associations(pgsql_conn_t *pg_conn, uint32_t uid,
					   List association_list)
{
	return as_p_add_associations(pg_conn, uid, association_list);
}

extern int acct_storage_p_add_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				  List qos_list)
{
	return as_p_add_qos(pg_conn, uid, qos_list);
}

extern int acct_storage_p_add_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
				     List wckey_list)
{
	return as_p_add_wckeys(pg_conn, uid, wckey_list);
}

extern int acct_storage_p_add_reservation(pgsql_conn_t *pg_conn,
					   acct_reservation_rec_t *resv)
{
	return as_p_add_reservation(pg_conn, resv);
}

extern List acct_storage_p_modify_users(pgsql_conn_t *pg_conn, uint32_t uid,
					acct_user_cond_t *user_cond,
					acct_user_rec_t *user)
{
	return as_p_modify_users(pg_conn, uid, user_cond, user);
}

extern List acct_storage_p_modify_accounts(pgsql_conn_t *pg_conn, uint32_t uid,
					   acct_account_cond_t *acct_cond,
					   acct_account_rec_t *acct)
{
	return as_p_modify_accounts(pg_conn, uid, acct_cond, acct);
}

extern List acct_storage_p_modify_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
					   acct_cluster_cond_t *cluster_cond,
					   acct_cluster_rec_t *cluster)
{
	return as_p_modify_clusters(pg_conn, uid, cluster_cond, cluster);
}

extern List acct_storage_p_modify_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond,
	acct_association_rec_t *assoc)
{
	return as_p_modify_associations(pg_conn, uid, assoc_cond, assoc);
}

extern List acct_storage_p_modify_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos)
{
	return as_p_modify_qos(pg_conn, uid, qos_cond, qos);
}

extern List acct_storage_p_modify_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
				      acct_wckey_cond_t *wckey_cond,
				      acct_wckey_rec_t *wckey)
{
	return as_p_modify_wckeys(pg_conn, uid, wckey_cond, wckey);
}

extern int acct_storage_p_modify_reservation(pgsql_conn_t *pg_conn,
					     acct_reservation_rec_t *resv)
{
	return as_p_modify_reservation(pg_conn, resv);
}

extern List acct_storage_p_remove_users(pgsql_conn_t *pg_conn, uint32_t uid,
					acct_user_cond_t *user_cond)
{
	return as_p_remove_users(pg_conn, uid, user_cond);
}

extern List acct_storage_p_remove_coord(pgsql_conn_t *pg_conn, uint32_t uid,
					List acct_list,
					acct_user_cond_t *user_cond)
{
	return as_p_remove_coord(pg_conn, uid, acct_list, user_cond);
}

extern List acct_storage_p_remove_accts(pgsql_conn_t *pg_conn, uint32_t uid,
					acct_account_cond_t *acct_cond)
{
	return as_p_remove_accts(pg_conn, uid, acct_cond);
}

extern List acct_storage_p_remove_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
					   acct_cluster_cond_t *cluster_cond)
{
	return as_p_remove_clusters(pg_conn, uid, cluster_cond);
}

extern List acct_storage_p_remove_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond)
{
	return as_p_remove_associations(pg_conn, uid, assoc_cond);
}

extern List acct_storage_p_remove_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond)
{
	return as_p_remove_qos(pg_conn, uid, qos_cond);
}

extern List acct_storage_p_remove_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
					 acct_wckey_cond_t *wckey_cond)
{
	return as_p_remove_wckeys(pg_conn, uid, wckey_cond);
}

extern int acct_storage_p_remove_reservation(pgsql_conn_t *pg_conn,
					     acct_reservation_rec_t *resv)
{
	return as_p_remove_reservation(pg_conn, resv);
}

extern List acct_storage_p_get_users(pgsql_conn_t *pg_conn, uid_t uid,
				     acct_user_cond_t *user_cond)
{
	return as_p_get_users(pg_conn, uid, user_cond);
}

extern List acct_storage_p_get_accts(pgsql_conn_t *pg_conn, uid_t uid,
				     acct_account_cond_t *acct_cond)
{
	return as_p_get_accts(pg_conn, uid, acct_cond);
}

extern List acct_storage_p_get_clusters(pgsql_conn_t *pg_conn, uid_t uid,
					acct_cluster_cond_t *cluster_cond)
{
	return as_p_get_clusters(pg_conn, uid, cluster_cond);
}

extern List acct_storage_p_get_config(pgsql_conn_t *pg_conn)
{
	return NULL;
}

extern List acct_storage_p_get_associations(pgsql_conn_t *pg_conn, uid_t uid,
					    acct_association_cond_t *assoc_cond)
{
	return as_p_get_associations(pg_conn, uid, assoc_cond);
}

extern List acct_storage_p_get_events(pgsql_conn_t *pg_conn, uint32_t uid,
				      acct_event_cond_t *event_cond)
{
	return NULL;
}

extern List acct_storage_p_get_problems(pgsql_conn_t *pg_conn, uid_t uid,
					acct_association_cond_t *assoc_q)
{
	return as_p_get_problems(pg_conn, uid, assoc_q);
}

extern List acct_storage_p_get_qos(pgsql_conn_t *pg_conn, uid_t uid,
				   acct_qos_cond_t *qos_cond)
{
	return as_p_get_qos(pg_conn, uid, qos_cond);
}

extern List acct_storage_p_get_wckeys(pgsql_conn_t *pg_conn, uid_t uid,
				      acct_wckey_cond_t *wckey_cond)
{
	return as_p_get_wckeys(pg_conn, uid, wckey_cond);
}

extern List acct_storage_p_get_reservations(pgsql_conn_t *pg_conn, uid_t uid,
					    acct_reservation_cond_t *resv_cond)
{
	return as_p_get_reservations(pg_conn, uid, resv_cond);
}

extern List acct_storage_p_get_txn(pgsql_conn_t *pg_conn, uid_t uid,
				   acct_txn_cond_t *txn_cond)
{
	return as_p_get_txn(pg_conn, uid, txn_cond);
}

extern int acct_storage_p_get_usage(pgsql_conn_t *pg_conn, uid_t uid,
				    void *in, int type,
				    time_t start, time_t end)
{
	return as_p_get_usage(pg_conn, uid, in, (slurmdbd_msg_type_t)type,
			      start, end);
}

extern int acct_storage_p_roll_usage(pgsql_conn_t *pg_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data)
{
	return as_p_roll_usage(pg_conn, sent_start, sent_end, archive_data);
}

extern int clusteracct_storage_p_node_down(pgsql_conn_t *pg_conn,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	return cs_p_node_down(pg_conn, cluster, node_ptr, event_time,
			      reason, reason_uid);
}
extern int clusteracct_storage_p_node_up(pgsql_conn_t *pg_conn,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	return cs_p_node_up(pg_conn, cluster, node_ptr, event_time);
}

extern int clusteracct_storage_p_register_ctld(pgsql_conn_t *pg_conn,
					       char *cluster,
					       uint16_t port)
{
	return cs_pg_register_ctld(pg_conn, cluster, port);
}

extern int clusteracct_storage_p_cluster_cpus(pgsql_conn_t *pg_conn,
					       char *cluster,
					       char *cluster_nodes,
					       uint32_t cpus,
					       time_t event_time)
{
	return cs_p_cluster_cpus(pg_conn, cluster, cluster_nodes,
				 cpus, event_time);
}

extern int clusteracct_storage_p_get_usage(
	pgsql_conn_t *pg_conn, uid_t uid,
	acct_cluster_rec_t *cluster_rec, int type, time_t start, time_t end)
{

	return cs_p_get_usage(pg_conn, uid, cluster_rec, type, start, end);
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(pgsql_conn_t *pg_conn,
				       char *cluster_name,
				       struct job_record *job_ptr)
{
	return js_p_job_start(pg_conn, cluster_name, job_ptr);
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(pgsql_conn_t *pg_conn,
					  struct job_record *job_ptr)
{
	return js_p_job_complete(pg_conn, job_ptr);
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(pgsql_conn_t *pg_conn,
					struct step_record *step_ptr)
{
	return js_p_step_start(pg_conn, step_ptr);
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(pgsql_conn_t *pg_conn,
					   struct step_record *step_ptr)
{
	return js_p_step_complete(pg_conn, step_ptr);
}

/*
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(pgsql_conn_t *pg_conn,
				     struct job_record *job_ptr)
{
	return js_p_suspend(pg_conn, job_ptr);
}

/*
 * get info from the storage
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(pgsql_conn_t *pg_conn, uid_t uid,
					    acct_job_cond_t *job_cond)
{
	return js_p_get_jobs_cond(pg_conn, uid, job_cond);
}

/*
 * expire old info from the storage
 */
extern int jobacct_storage_p_archive(pgsql_conn_t *pg_conn,
				      acct_archive_cond_t *arch_cond)
{
	return js_p_archive(pg_conn, arch_cond);
}

/*
 * load old info into the storage
 */
extern int jobacct_storage_p_archive_load(pgsql_conn_t *pg_conn,
					  acct_archive_rec_t *arch_rec)
{
	return js_p_archive_load(pg_conn, arch_rec);
}


extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used)
{
	/* This definitely needs to be fleshed out.
	 * Go through the list of shares_used_object_t objects and store them */
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(pgsql_conn_t *pg_conn,
						char *cluster,
						time_t event_time)
{
	/* put end times for a clean start */
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	return SLURM_SUCCESS;
}
