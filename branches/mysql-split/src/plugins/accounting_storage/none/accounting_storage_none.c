/*****************************************************************************\
 *  accounting_storage_none.c - account interface to none.
 *
 *  $Id: accounting_storage_none.c 13061 2008-01-22 21:23:56Z da $
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

#include "src/common/slurm_accounting_storage.h"

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
const char plugin_name[] = "Accounting storage NOT INVOKED plugin";
const char plugin_type[] = "accounting_storage/none";
const uint32_t plugin_version = 100;

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

extern void * acct_storage_p_get_connection(bool make_agent, int conn_num,
					    bool rollback)
{
	return NULL;
}

extern int acct_storage_p_close_connection(void **db_conn)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_commit(void *db_conn, bool commit)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(void *db_conn, uint32_t uid,
				    List user_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_coord(void *db_conn, uint32_t uid,
				    List acct_list, acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(void *db_conn, uint32_t uid,
				    List acct_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_clusters(void *db_conn, uint32_t uid,
				       List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_associations(void *db_conn, uint32_t uid,
					   List association_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_qos(void *db_conn, uint32_t uid,
				  List qos_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_wckeys(void *db_conn, uint32_t uid,
				  List wckey_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_reservation(void *db_conn,
					  acct_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_users(void *db_conn, uint32_t uid,
				       acct_user_cond_t *user_q,
				       acct_user_rec_t *user)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_accts(void *db_conn, uint32_t uid,
					   acct_account_cond_t *acct_q,
					   acct_account_rec_t *acct)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_clusters(void *db_conn, uint32_t uid,
					  acct_cluster_cond_t *cluster_q,
					  acct_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_associations(void *db_conn, uint32_t uid,
					      acct_association_cond_t *assoc_q,
					      acct_association_rec_t *assoc)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_qos(void *db_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_wckeys(void *db_conn, uint32_t uid,
				      acct_wckey_cond_t *wckey_cond,
				      acct_wckey_rec_t *wckey)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_reservation(void *db_conn,
					     acct_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_users(void *db_conn, uint32_t uid,
				       acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_coord(void *db_conn, uint32_t uid,
					List acct_list,
					acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_accts(void *db_conn, uint32_t uid,
				       acct_account_cond_t *acct_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_clusters(void *db_conn, uint32_t uid,
					  acct_account_cond_t *cluster_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_associations(void *db_conn, uint32_t uid,
					      acct_association_cond_t *assoc_q)
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

extern int acct_storage_p_remove_reservation(void *db_conn,
					     acct_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_get_users(void *db_conn, uid_t uid,
				     acct_user_cond_t *user_q)
{
	return NULL;
}

extern List acct_storage_p_get_accts(void *db_conn, uid_t uid,
				     acct_account_cond_t *acct_q)
{
	return NULL;
}

extern List acct_storage_p_get_clusters(void *db_conn, uid_t uid,
					acct_account_cond_t *cluster_q)
{
	return NULL;
}

extern List acct_storage_p_get_config(void *db_conn)
{
	return NULL;
}

extern List acct_storage_p_get_associations(void *db_conn, uid_t uid,
					    acct_association_cond_t *assoc_q)
{
	return NULL;
}

extern List acct_storage_p_get_events(void *db_conn, uint32_t uid,
				      acct_event_cond_t *event_cond)
{
	return NULL;
}

extern List acct_storage_p_get_problems(void *db_conn, uid_t uid,
					acct_association_cond_t *assoc_q)
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

extern List acct_storage_p_get_reservations(void *mysql_conn, uid_t uid,
					    acct_reservation_cond_t *resv_cond)
{
	return NULL;
}

extern List acct_storage_p_get_txn(void *db_conn, uid_t uid,
				   acct_txn_cond_t *txn_cond)
{
	return NULL;
}

extern int acct_storage_p_get_usage(void *db_conn, uid_t uid,
				    void *in, int type,
				    time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_roll_usage(void *db_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int clusteracct_storage_p_node_down(void *db_conn,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	return SLURM_SUCCESS;
}
extern int clusteracct_storage_p_node_up(void *db_conn,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_register_ctld(void *db_conn,
					       char *cluster,
					       uint16_t port)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_cpus(void *db_conn,
					       char *cluster,
					       char *cluster_nodes,
					       uint32_t cpus,
					       time_t event_time)
{
	return SLURM_SUCCESS;
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
extern int jobacct_storage_p_job_start(void *db_conn, char *cluster_name,
				       struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(void *db_conn,
					  struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(void *db_conn,
					struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(void *db_conn,
					   struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(void *db_conn,
				     struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * get info from the storage
 * returns List of jobacct_job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(void *db_conn, uid_t uid,
					    void *job_cond)
{
	return NULL;
}

/*
 * expire old info from the storage
 */
extern int jobacct_storage_p_archive(void *db_conn,
				     acct_archive_cond_t *arch_cond)
{
	return SLURM_SUCCESS;
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
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(
	void *db_conn, char *cluster, time_t event_time)
{
	return SLURM_SUCCESS;
}
