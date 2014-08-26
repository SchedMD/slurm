/*****************************************************************************\
 *  slurm_accounting_storage.h - Define accounting storage plugin functions.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _SLURM_ACCOUNTING_STORAGE_H
#define _SLURM_ACCOUNTING_STORAGE_H

#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/slurmdb_pack.h"

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include <sys/types.h>
#include <pwd.h>

extern int slurm_acct_storage_init(char *loc); /* load the plugin */
extern int slurm_acct_storage_fini(void); /* unload the plugin */

/*
 * get a new connection to the storage unit
 * IN: callbacks - Make an agent to manage queued requests, contains
 *                 trigger callbacks
 * IN: conn_num - If running more than one connection to the database
 *     this can be used to tell which connection is doing what
 * IN: rollback - maintain journal of changes to permit rollback
 * RET: pointer used to access db
 */
extern void *acct_storage_g_get_connection(
	const slurm_trigger_callbacks_t *callbacks,
	int conn_num, bool rollback,char *cluster_name);

/*
 * release connection to the storage unit
 * IN/OUT: void ** pointer returned from
 *         acct_storage_g_get_connection() which will be freed.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_close_connection(void **db_conn);

/*
 * commit or rollback changes made without closing connection
 * IN: void * pointer returned from acct_storage_g_get_connection()
 * IN: bool - true will commit changes false will rollback
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_commit(void *db_conn, bool commit);

/*
 * add users to accounting system
 * IN:  user_list List of slurmdb_user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_users(void *db_conn, uint32_t uid,
				    List user_list);

/*
 * add users as account coordinators
 * IN: acct_list list of char *'s of names of accounts
 * IN:  slurmdb_user_cond_t *user_cond
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_coord(void *db_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond);


/*
 * add accounts to accounting system
 * IN:  account_list List of slurmdb_account_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_accounts(void *db_conn, uint32_t uid,
				       List acct_list);

/*
 * add clusters to accounting system
 * IN:  cluster_list List of slurmdb_cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_clusters(void *db_conn, uint32_t uid,
				       List cluster_list);

/*
 * add associations to accounting system
 * IN:  association_list List of slurmdb_association_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_associations(void *db_conn, uint32_t uid,
					   List association_list);

/*
 * add qos's to accounting system
 * IN:  qos_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_qos(void *db_conn, uint32_t uid,
				  List qos_list);

/*
 * add res's to accounting system
 * IN:  res_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_res(void *db_conn, uint32_t uid,
				       List res_list);

/*
 * add wckey's to accounting system
 * IN:  wckey_list List of slurmdb_wckey_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_wckeys(void *db_conn, uint32_t uid,
				     List wckey_list);

/*
 * add reservation's in accounting system
 * IN:  slurmdb_reservation_rec_t *resv reservation to be added.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_reservation(void *db_conn,
					  slurmdb_reservation_rec_t *resv);

/*
 * modify existing users in the accounting system
 * IN:  slurmdb_user_cond_t *user_cond
 * IN:  slurmdb_user_rec_t *user
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond,
					slurmdb_user_rec_t *user);

/*
 * modify existing accounts in the accounting system
 * IN:  slurmdb_acct_cond_t *acct_cond
 * IN:  slurmdb_account_rec_t *acct
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_accounts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond,
					   slurmdb_account_rec_t *acct);

/*
 * modify existing clusters in the accounting system
 * IN:  slurmdb_cluster_cond_t *cluster_cond
 * IN:  slurmdb_cluster_rec_t *cluster
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_clusters(void *db_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond,
					   slurmdb_cluster_rec_t *cluster);

/*
 * modify existing associations in the accounting system
 * IN:  slurmdb_association_cond_t *assoc_cond
 * IN:  slurmdb_association_rec_t *assoc
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_associations(
	void *db_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond,
	slurmdb_association_rec_t *assoc);

/*
 * modify existing job in the accounting system
 * IN:  slurmdb_job_modify_cond_t *job_cond
 * IN:  slurmdb_job_rec_t *job
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_job(void *db_conn, uint32_t uid,
				      slurmdb_job_modify_cond_t *job_cond,
				      slurmdb_job_rec_t *job);

/*
 * modify existing qos in the accounting system
 * IN:  slurmdb_qos_cond_t *qos_cond
 * IN:  slurmdb_qos_rec_t *qos
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos);

/*
 * modify existing res in the accounting system
 * IN:  slurmdb_res_cond_t *res_cond
 * IN:  slurmdb_res_rec_t *res
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_res(void *db_conn, uint32_t uid,
					   slurmdb_res_cond_t *res_cond,
					   slurmdb_res_rec_t *res);

/*
 * modify existing wckey in the accounting system
 * IN:  slurmdb_wckey_cond_t *wckey_cond
 * IN:  slurmdb_wckey_rec_t *wckey
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_wckeys(void *db_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond,
					 slurmdb_wckey_rec_t *wckey);

/*
 * modify reservation's in accounting system
 * IN:  slurmdb_reservation_rec_t *resv
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_modify_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv);
/*
 * remove users from accounting system
 * IN:  slurmdb_user_cond_t *user_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond);

/*
 * remove users from being a coordinator of an account
 * IN: acct_list list of char *'s of names of accounts
 * IN: slurmdb_user_cond_t *user_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_coord(void *db_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_cond);

/*
 * remove accounts from accounting system
 * IN:  slurmdb_account_cond_t *acct_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_accounts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond);

/*
 * remove clusters from accounting system
 * IN:  slurmdb_cluster_cond_t *cluster_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_clusters(void *db_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond);

/*
 * remove associations from accounting system
 * IN:  slurmdb_association_cond_t *assoc_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_associations(
	void *db_conn, uint32_t uid, slurmdb_association_cond_t *assoc_cond);

/*
 * remove qos from accounting system
 * IN:  slurmdb_qos_cond_t *qos_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_qos(
	void *db_conn, uint32_t uid, slurmdb_qos_cond_t *qos_cond);

/*
 * remove res from accounting system
 * IN:  slurmdb_res_cond_t *res_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_res(
	void *db_conn, uint32_t uid, slurmdb_res_cond_t *res_cond);

/*
 * remove wckey from accounting system
 * IN:  slurmdb_wckey_cond_t *assoc_wckey
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_wckeys(
	void *db_conn, uint32_t uid, slurmdb_wckey_cond_t *wckey_cond);

/*
 * remove reservation's in accounting system
 * IN:  slurmdb_reservation_rec_t *resv
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_remove_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv);
/*
 * get info from the storage
 * IN:  slurmdb_user_cond_t *
 * IN:  params void *
 * returns List of slurmdb_user_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_users(void *db_conn,  uint32_t uid,
				     slurmdb_user_cond_t *user_cond);

/*
 * get info from the storage
 * IN:  slurmdb_account_cond_t *
 * IN:  params void *
 * returns List of slurmdb_account_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_accounts(void *db_conn,  uint32_t uid,
					slurmdb_account_cond_t *acct_cond);

/*
 * get info from the storage
 * IN:  slurmdb_cluster_cond_t *
 * IN:  params void *
 * returns List of slurmdb_cluster_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_clusters(
	void *db_conn, uint32_t uid, slurmdb_cluster_cond_t *cluster_cond);

/*
 * get info from the storage
 * RET: List of config_key_pairs_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_config(void *db_conn, char *config_name);

/*
 * get info from the storage
 * IN:  slurmdb_association_cond_t *
 * RET: List of slurmdb_association_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_associations(
	void *db_conn, uint32_t uid, slurmdb_association_cond_t *assoc_cond);

/*
 * get info from the storage
 * IN:  slurmdb_event_cond_t *
 * RET: List of slurmdb_event_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_events(
	void *db_conn,  uint32_t uid, slurmdb_event_cond_t *event_cond);

/*
 * get info from the storage
 * IN:  slurmdb_association_cond_t *
 * RET: List of slurmdb_association_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_problems(
	void *db_conn, uint32_t uid, slurmdb_association_cond_t *assoc_cond);

/*
 * get info from the storage
 * IN:  slurmdb_qos_cond_t *
 * RET: List of slurmdb_qos_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_qos(void *db_conn, uint32_t uid,
				   slurmdb_qos_cond_t *qos_cond);

/*
 * get info from the storage
 * IN:  slurmdb_res_cond_t *
 * RET: List of slurmdb_res_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_res(void *db_conn, uint32_t uid,
				   slurmdb_res_cond_t *res_cond);

/*
 * get info from the storage
 * IN:  slurmdb_wckey_cond_t *
 * RET: List of slurmdb_wckey_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_wckeys(void *db_conn, uint32_t uid,
				      slurmdb_wckey_cond_t *wckey_cond);

/*
 * get info from the storage
 * IN:  slurmdb_reservation_cond_t *
 * RET: List of slurmdb_reservation_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_reservations(
	void *db_conn, uint32_t uid,
	slurmdb_reservation_cond_t *resv_cond);

/*
 * get info from the storage
 * IN:  slurmdb_txn_cond_t *
 * RET: List of slurmdb_txn_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_txn(void *db_conn,  uint32_t uid,
				   slurmdb_txn_cond_t *txn_cond);

/*
 * get info from the storage
 * IN/OUT:  in void * (acct_association_rec_t *) or
 *          (acct_wckey_rec_t *) with the id set
 * IN:  type what type is 'in'
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <=
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_get_usage(
	void *db_conn,  uint32_t uid, void *in, int type,
	time_t start, time_t end);

/*
 * roll up data in the storage
 * IN: sent_start (option time to do a re-roll or start from this point)
 * IN: sent_end (option time to do a re-roll or end at this point)
 * IN: archive_data (if 0 old data is not archived in a monthly rollup)
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_roll_usage(void *db_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data);
/*
 * record shares used information for backup in case slurmctld restarts
 * IN:  account_list List of shares_used_object_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_update_shares_used(void *db_conn, List acct_list);

/*
 * This should be called when a cluster does a cold start to flush out
 * any jobs that were running during the restart so we don't have any
 * jobs in the database "running" forever since no endtime will be
 * placed in there other wise.
 * IN:  char * = cluster name
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_flush_jobs_on_cluster(
	void *db_conn, time_t event_time);

 /* Reconfig the plugin, if dbd is true forward reconfig to the DBD */

extern int acct_storage_g_reconfig(void *db_conn, bool dbd);

/*********************** CLUSTER ACCOUNTING STORAGE **************************/

extern int clusteracct_storage_g_node_down(void *db_conn,
					   struct node_record *node_ptr,
					   time_t event_time,
					   char *reason, uint32_t reason_uid);

extern int clusteracct_storage_g_node_up(void *db_conn,
					 struct node_record *node_ptr,
					 time_t event_time);

extern int clusteracct_storage_g_cluster_cpus(void *db_conn,
					      char *cluster_nodes,
					      uint32_t cpus,
					      time_t event_time);

extern int clusteracct_storage_g_register_ctld(void *db_conn, uint16_t port);
extern int clusteracct_storage_g_register_disconn_ctld(
	void *db_conn, char *control_host);
extern int clusteracct_storage_g_fini_ctld(void *db_conn,
					   slurmdb_cluster_rec_t *cluster_rec);

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_g_job_start(void *db_conn,
				       struct job_record *job_ptr);

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_g_job_complete(void *db_conn,
					  struct job_record *job_ptr);

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_g_step_start(void *db_conn,
					struct step_record *step_ptr);

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_g_step_complete(void *db_conn,
					   struct step_record *step_ptr);

/*
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_g_job_suspend(void *db_conn,
					 struct job_record *job_ptr);

/*
 * get info from the storage
 * returns List of jobacct_job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_g_get_jobs_cond(void *db_conn, uint32_t uid,
					    slurmdb_job_cond_t *job_cond);

/*
 * expire old info from the storage
 */
extern int jobacct_storage_g_archive(void *db_conn,
				     slurmdb_archive_cond_t *arch_cond);

/*
 * expire old info from the storage
 */
extern int jobacct_storage_g_archive_load(void *db_conn,
					  slurmdb_archive_rec_t *arch_rec);

#endif /*_SLURM_ACCOUNTING_STORAGE_H*/
