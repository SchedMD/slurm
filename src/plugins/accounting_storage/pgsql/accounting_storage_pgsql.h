/*****************************************************************************\
 *  accounting_storage_pgsql.h - accounting interface to pgsql.
 *
 *  $Id: accounting_storage_pgsql.h 13061 2008-01-22 21:23:56Z da $
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
#ifndef _HAVE_ACCOUNTING_STORAGE_PGSQL_H
#define _HAVE_ACCOUNTING_STORAGE_PGSQL_H

#include "as_pg_common.h"

extern slurm_dbd_conf_t *slurmdbd_conf;

/* API functions */
extern void *acct_storage_p_get_connection(bool make_agent, int conn_num,
					   bool rollback, char *cluster_name);
extern int acct_storage_p_close_connection(pgsql_conn_t **pg_conn);
extern int acct_storage_p_commit(pgsql_conn_t *pg_conn, bool commit);

extern int acct_storage_p_add_users(pgsql_conn_t *pg_conn, uint32_t uid,
				    List user_list);
extern int acct_storage_p_add_coord(pgsql_conn_t *pg_conn,
				    uint32_t uid, List acct_list,
				    slurmdb_user_cond_t *user_cond);
extern int acct_storage_p_add_accts(pgsql_conn_t *pg_conn, uint32_t uid,
				    List acct_list);
extern int acct_storage_p_add_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
				       List cluster_list);
extern int acct_storage_p_add_associations(pgsql_conn_t *pg_conn, uint32_t uid,
					   List association_list);
extern int acct_storage_p_add_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				  List qos_list);
extern int acct_storage_p_add_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
				     List wckey_list);
extern int acct_storage_p_add_reservation(pgsql_conn_t *pg_conn,
					  slurmdb_reservation_rec_t *resv);

extern List acct_storage_p_modify_users(pgsql_conn_t *pg_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond,
					slurmdb_user_rec_t *user);
extern List acct_storage_p_modify_accounts(pgsql_conn_t *pg_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond,
					   slurmdb_account_rec_t *acct);
extern List acct_storage_p_modify_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond,
					   slurmdb_cluster_rec_t *cluster);
extern List acct_storage_p_modify_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond,
	slurmdb_association_rec_t *assoc);
extern List acct_storage_p_modify_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos);
extern List acct_storage_p_modify_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond,
					 slurmdb_wckey_rec_t *wckey);
extern int acct_storage_p_modify_reservation(pgsql_conn_t *pg_conn,
					     slurmdb_reservation_rec_t *resv);

extern List acct_storage_p_remove_users(pgsql_conn_t *pg_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond);
extern List acct_storage_p_remove_coord(pgsql_conn_t *pg_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_cond);
extern List acct_storage_p_remove_accts(pgsql_conn_t *pg_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond);
extern List acct_storage_p_remove_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond);
extern List acct_storage_p_remove_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond);
extern List acct_storage_p_remove_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond);
extern List acct_storage_p_remove_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond);
extern int acct_storage_p_remove_reservation(pgsql_conn_t *pg_conn,
					     slurmdb_reservation_rec_t *resv);

extern List acct_storage_p_get_users(pgsql_conn_t *pg_conn, uid_t uid,
				     slurmdb_user_cond_t *user_cond);
extern List acct_storage_p_get_accts(pgsql_conn_t *pg_conn, uid_t uid,
				     slurmdb_account_cond_t *acct_cond);
extern List acct_storage_p_get_clusters(pgsql_conn_t *pg_conn, uid_t uid,
					slurmdb_cluster_cond_t *cluster_cond);
extern List acct_storage_p_get_config(pgsql_conn_t *pg_conn);
extern List acct_storage_p_get_associations(pgsql_conn_t *pg_conn, uid_t uid,
					    slurmdb_association_cond_t *assoc_cond);
extern List acct_storage_p_get_problems(pgsql_conn_t *pg_conn, uid_t uid,
					slurmdb_association_cond_t *assoc_q);
extern List acct_storage_p_get_qos(pgsql_conn_t *pg_conn, uid_t uid,
				   slurmdb_qos_cond_t *qos_cond);
extern List acct_storage_p_get_wckeys(pgsql_conn_t *pg_conn, uid_t uid,
				      slurmdb_wckey_cond_t *wckey_cond);
extern List acct_storage_p_get_reservations(pgsql_conn_t *pg_conn, uid_t uid,
					    slurmdb_reservation_cond_t *resv_cond);
extern List acct_storage_p_get_txn(pgsql_conn_t *pg_conn, uid_t uid,
				   slurmdb_txn_cond_t *txn_cond);
extern int acct_storage_p_get_usage(pgsql_conn_t *pg_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end);
extern int acct_storage_p_roll_usage(pgsql_conn_t *pg_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data);

extern int clusteracct_storage_p_node_down(pgsql_conn_t *pg_conn,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid);
extern int clusteracct_storage_p_node_up(pgsql_conn_t *pg_conn,
					 struct node_record *node_ptr,
					 time_t event_time);
extern int clusteracct_storage_p_register_ctld(pgsql_conn_t *pg_conn,
					       uint16_t port, uint16_t dims,
					       uint32_t flags);
extern int clusteracct_storage_p_cluster_cpus(pgsql_conn_t *pg_conn,
					       char *cluster_nodes,
					       uint32_t cpus,
					       time_t event_time);
extern int clusteracct_storage_p_get_usage(
	pgsql_conn_t *pg_conn, uid_t uid,
	slurmdb_cluster_rec_t *cluster_rec, int type, time_t start, time_t end);

extern int jobacct_storage_p_job_start(pgsql_conn_t *pg_conn,
				       struct job_record *job_ptr);
extern int jobacct_storage_p_job_complete(pgsql_conn_t *pg_conn,
					  struct job_record *job_ptr);
extern int jobacct_storage_p_step_start(pgsql_conn_t *pg_conn,
					struct step_record *step_ptr);
extern int jobacct_storage_p_step_complete(pgsql_conn_t *pg_conn,
					   struct step_record *step_ptr);
extern int jobacct_storage_p_suspend(pgsql_conn_t *pg_conn,
				     struct job_record *job_ptr);
extern List jobacct_storage_p_get_jobs_cond(pgsql_conn_t *pg_conn, uid_t uid,
					    slurmdb_job_cond_t *job_cond);

extern int jobacct_storage_p_archive(pgsql_conn_t *pg_conn,
				     slurmdb_archive_cond_t *arch_cond);
extern int jobacct_storage_p_archive_load(pgsql_conn_t *pg_conn,
					  slurmdb_archive_rec_t *arch_rec);

extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used);
extern int acct_storage_p_flush_jobs_on_cluster(
	pgsql_conn_t *pg_conn, time_t event_time);

#endif /* _HAVE_ACCOUNTING_STORAGE_PGSQL_H */
