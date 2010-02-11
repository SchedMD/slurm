/*****************************************************************************\
 *  common.h - accounting interface to pgsql - function declarations.
 *
 *  $Id: common.h 13061 2008-01-22 21:23:56Z da $
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
#ifndef _HAVE_AS_PGSQL_COMMON_H
#define _HAVE_AS_PGSQL_COMMON_H

#include <strings.h>
#include <stdlib.h>
#include "src/database/pgsql_common.h"
#include "src/slurmdbd/read_config.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/jobacct_common.h"
#include "src/common/uid.h"
#include "src/plugins/accounting_storage/common/common_as.h"

/*
 * To save typing and avoid wrapping long lines
 */

#define DEBUG_QUERY do { \
		debug3("as/pg(%s:%d) query\n%s", __FILE__, __LINE__, query); \
	} while (0)

/* Debug, Execute, Free query, and RETurn result */
#define DEF_QUERY_RET ({			\
	PGresult *_res; \
	DEBUG_QUERY; \
	_res = pgsql_db_query_ret(pg_conn->db_conn, query);	\
	xfree(query); \
	_res; })

/* Debug, Execute, Free query, and RETurn error code */
#define DEF_QUERY_RET_RC ({\
	int _rc; \
	DEBUG_QUERY; \
	_rc = pgsql_db_query(pg_conn->db_conn, query);	\
	xfree(query); \
	_rc; })

/* Debug, Execute, Free query, and RETurn object id */
#define DEF_QUERY_RET_ID ({\
	int _id; \
	DEBUG_QUERY; \
	_id = pgsql_query_ret_id(pg_conn->db_conn, query);	\
	xfree(query); \
	_id; })

/* XXX: special variable name 'result' */
#define PG_VAL(col) PQgetvalue(result, 0, col)
#define PG_NULL(col) PQgetisnull(result, 0, col)
#define PG_EMPTY(col) (PQgetvalue(result, 0, col)[0] == '\0')

#define FOR_EACH_ROW do { \
	int _row, _num; \
	_num = PQntuples(result); \
	for (_row = 0; _row < _num; _row ++)
#define END_EACH_ROW } while (0)
#define ROW(col) PQgetvalue(result, _row, col)
#define ISNULL(col) PQgetisnull(result, _row, col)
#define ISEMPTY(col) (PQgetvalue(result, _row, col)[0] == '\0')

#define FOR_EACH_ROW2 do { \
	int _row2, _num2; \
	_num2 = PQntuples(result2); \
	for (_row2 = 0; _row2 < _num2; _row2 ++)
#define END_EACH_ROW2 } while (0)
#define ROW2(col) PQgetvalue(result2, _row2, col)
#define ISNULL2(col) PQgetisnull(result2, _row2, col)
#define ISEMPTY2(col) (PQgetvalue(result2, _row2, col)[0] == '\0')

extern slurm_dbd_conf_t *slurmdbd_conf;

/* data structures */
typedef struct {
	hostlist_t hl;
	time_t start;
	time_t end;
	bitstr_t *asked_bitmap;
} local_cluster_t;

extern char *default_qos_str;

/* table names */
extern char *acct_table;
extern char *acct_coord_table;
extern char *assoc_table;
extern char *assoc_hour_table;
extern char *assoc_day_table;
extern char *assoc_month_table;
extern char *cluster_table;
extern char *cluster_day_table;
extern char *cluster_hour_table;
extern char *cluster_month_table;
extern char *event_table;
extern char *job_table;
extern char *last_ran_table;
extern char *qos_table;
extern char *resv_table;
extern char *step_table;
extern char *suspend_table;
extern char *txn_table;
extern char *wckey_table;
extern char *wckey_hour_table;
extern char *wckey_day_table;
extern char *wckey_month_table;

/* functions */
extern int create_function_xfree(PGconn *db_conn, char *query);

extern void concat_cond_list(List cond_list, char *prefix,
			     char *col, char **cond);
extern void concat_like_cond_list(List cond_list, char *prefix,
				  char *col, char **cond);
extern void concat_limit(char *col, int limit, char **rec, char **txn);

extern int aspg_modify_common(pgsql_conn_t *pg_conn, uint16_t type, time_t now,
			      char *user_name, char *table, char *name_char,
			      char *vals);
extern int aspg_remove_common(pgsql_conn_t *pg_conn, uint16_t type, time_t now,
			      char *user_name, char *table, char *name_char,
			      char *assoc_char);

extern int check_db_connection(pgsql_conn_t *pg_conn);
extern int check_table(PGconn *db_conn, char *table, storage_field_t *fields,
		       char *constraint, char *user);

extern List setup_cluster_list_with_inx(pgsql_conn_t *pg_conn,
					acct_job_cond_t *job_cond,
					void **curr_cluster);
extern int good_nodes_from_inx(List local_cluster_list, void **object,
			       char *node_inx, int submit);


/* assoc functions */
extern List find_children_assoc(pgsql_conn_t *pg_conn, char *parent_cond);
extern int remove_young_assoc(pgsql_conn_t *pg_conn, time_t now, char *cond);
extern List get_assoc_ids(pgsql_conn_t *pg_conn, char *cond);
extern int group_concat_assoc_field(pgsql_conn_t *pg_conn, char *field,
				    char *cond, char **val);
extern char * get_cluster_from_associd(pgsql_conn_t *pg_conn, uint32_t associd);
extern char * get_user_from_associd(pgsql_conn_t *pg_conn, uint32_t associd);

/* problem functions */
extern int get_acct_no_assocs(pgsql_conn_t *pg_conn,
			      acct_association_cond_t *assoc_q,
			      List ret_list);
extern int get_acct_no_users(pgsql_conn_t *pg_conn,
			     acct_association_cond_t *assoc_q,
			     List ret_list);
extern int get_user_no_assocs_or_no_uid(pgsql_conn_t *pg_conn,
					acct_association_cond_t *assoc_q,
					List ret_list);

/* rollup functions */
extern int pgsql_hourly_rollup(pgsql_conn_t *pg_conn, time_t start, time_t end);
extern int pgsql_daily_rollup(pgsql_conn_t *pg_conn, time_t start, time_t end,
			      uint16_t archive_data);
extern int pgsql_monthly_rollup(pgsql_conn_t *pg_conn, time_t start,
				time_t end, uint16_t archive_data);

/* check table functions */
extern int check_acct_tables(PGconn *db_conn, char *user);
extern int check_assoc_tables(PGconn *db_conn, char *user);
extern int check_clusteracct_tables(PGconn *db_conn, char *user);
extern int check_cluster_tables(PGconn *db_conn, char *user);
extern int check_jobacct_tables(PGconn *db_conn, char *user);
extern int check_qos_tables(PGconn *db_conn, char *user);
extern int check_resv_tables(PGconn *db_conn, char *user);
extern int check_txn_tables(PGconn *db_conn, char *user);
extern int check_usage_tables(PGconn *db_conn, char *user);
extern int check_user_tables(PGconn *db_conn, char *user);
extern int check_wckey_tables(PGconn *db_conn, char *user);


/* API functions */
extern void *acct_storage_p_get_connection(bool make_agent, int conn_num,
					   bool rollback);

extern int acct_storage_p_close_connection(pgsql_conn_t **pg_conn);

extern int acct_storage_p_commit(pgsql_conn_t *pg_conn, bool commit);

extern int acct_storage_p_add_users(pgsql_conn_t *pg_conn, uint32_t uid,
				    List user_list);
extern int as_p_add_users(pgsql_conn_t *pg_conn, uint32_t uid,
			  List user_list);

extern int acct_storage_p_add_coord(pgsql_conn_t *pg_conn,
				    uint32_t uid, List acct_list,
				    acct_user_cond_t *user_cond);
extern int as_p_add_coord(pgsql_conn_t *pg_conn,
			  uint32_t uid, List acct_list,
			  acct_user_cond_t *user_cond);

extern int acct_storage_p_add_accts(pgsql_conn_t *pg_conn, uint32_t uid,
				    List acct_list);
extern int as_p_add_accts(pgsql_conn_t *pg_conn, uint32_t uid,
			  List acct_list);

extern int acct_storage_p_add_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
				       List cluster_list);
extern int as_p_add_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
			     List cluster_list);

extern int acct_storage_p_add_associations(pgsql_conn_t *pg_conn, uint32_t uid,
					   List association_list);
extern int as_p_add_associations(pgsql_conn_t *pg_conn, uint32_t uid,
				 List association_list);

extern int acct_storage_p_add_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				  List qos_list);
extern int as_p_add_qos(pgsql_conn_t *pg_conn, uint32_t uid,
			List qos_list);

extern int acct_storage_p_add_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
				     List wckey_list);
extern int as_p_add_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
			   List wckey_list);

extern int acct_storage_p_add_reservation(pgsql_conn_t *pg_conn,
					  acct_reservation_rec_t *resv);
extern int as_p_add_reservation(pgsql_conn_t *pg_conn,
				acct_reservation_rec_t *resv);

extern List acct_storage_p_modify_users(pgsql_conn_t *pg_conn, uint32_t uid,
					acct_user_cond_t *user_cond,
					acct_user_rec_t *user);
extern List as_p_modify_users(pgsql_conn_t *pg_conn, uint32_t uid,
			      acct_user_cond_t *user_cond,
			      acct_user_rec_t *user);

extern List acct_storage_p_modify_accounts(pgsql_conn_t *pg_conn, uint32_t uid,
					   acct_account_cond_t *acct_cond,
					   acct_account_rec_t *acct);
extern List as_p_modify_accounts(pgsql_conn_t *pg_conn, uint32_t uid,
				 acct_account_cond_t *acct_cond,
				 acct_account_rec_t *acct);

extern List acct_storage_p_modify_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
					   acct_cluster_cond_t *cluster_cond,
					   acct_cluster_rec_t *cluster);
extern List as_p_modify_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
				 acct_cluster_cond_t *cluster_cond,
				 acct_cluster_rec_t *cluster);

extern List acct_storage_p_modify_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond,
	acct_association_rec_t *assoc);
extern List as_p_modify_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond,
	acct_association_rec_t *assoc);

extern List acct_storage_p_modify_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos);
extern List as_p_modify_qos(pgsql_conn_t *pg_conn, uint32_t uid,
			    acct_qos_cond_t *qos_cond,
			    acct_qos_rec_t *qos);

extern List acct_storage_p_modify_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
					 acct_wckey_cond_t *wckey_cond,
					 acct_wckey_rec_t *wckey);
extern List as_p_modify_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
			       acct_wckey_cond_t *wckey_cond,
			       acct_wckey_rec_t *wckey);

extern int acct_storage_p_modify_reservation(pgsql_conn_t *pg_conn,
					     acct_reservation_rec_t *resv);
extern int as_p_modify_reservation(pgsql_conn_t *pg_conn,
				   acct_reservation_rec_t *resv);

extern List acct_storage_p_remove_users(pgsql_conn_t *pg_conn, uint32_t uid,
					acct_user_cond_t *user_cond);
extern List as_p_remove_users(pgsql_conn_t *pg_conn, uint32_t uid,
			      acct_user_cond_t *user_cond);

extern List acct_storage_p_remove_coord(pgsql_conn_t *pg_conn, uint32_t uid,
					List acct_list,
					acct_user_cond_t *user_cond);
extern List as_p_remove_coord(pgsql_conn_t *pg_conn, uint32_t uid,
			      List acct_list,
			      acct_user_cond_t *user_cond);

extern List acct_storage_p_remove_accts(pgsql_conn_t *pg_conn, uint32_t uid,
					acct_account_cond_t *acct_cond);
extern List as_p_remove_accts(pgsql_conn_t *pg_conn, uint32_t uid,
			      acct_account_cond_t *acct_cond);

extern List acct_storage_p_remove_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
					   acct_cluster_cond_t *cluster_cond);
extern List as_p_remove_clusters(pgsql_conn_t *pg_conn, uint32_t uid,
				 acct_cluster_cond_t *cluster_cond);

extern List acct_storage_p_remove_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond);
extern List as_p_remove_associations(
	pgsql_conn_t *pg_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond);

extern List acct_storage_p_remove_qos(pgsql_conn_t *pg_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond);
extern List as_p_remove_qos(pgsql_conn_t *pg_conn, uint32_t uid,
			    acct_qos_cond_t *qos_cond);

extern List acct_storage_p_remove_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
					 acct_wckey_cond_t *wckey_cond);
extern List as_p_remove_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
			       acct_wckey_cond_t *wckey_cond);

extern int acct_storage_p_remove_reservation(pgsql_conn_t *pg_conn,
					     acct_reservation_rec_t *resv);
extern int as_p_remove_reservation(pgsql_conn_t *pg_conn,
				   acct_reservation_rec_t *resv);

extern List acct_storage_p_get_users(pgsql_conn_t *pg_conn, uid_t uid,
				     acct_user_cond_t *user_cond);
extern List as_p_get_users(pgsql_conn_t *pg_conn, uid_t uid,
			   acct_user_cond_t *user_cond);

extern List acct_storage_p_get_accts(pgsql_conn_t *pg_conn, uid_t uid,
				     acct_account_cond_t *acct_cond);
extern List as_p_get_accts(pgsql_conn_t *pg_conn, uid_t uid,
			   acct_account_cond_t *acct_cond);

extern List acct_storage_p_get_clusters(pgsql_conn_t *pg_conn, uid_t uid,
					acct_cluster_cond_t *cluster_cond);
extern List as_p_get_clusters(pgsql_conn_t *pg_conn, uid_t uid,
			      acct_cluster_cond_t *cluster_cond);

extern List acct_storage_p_get_config(pgsql_conn_t *pg_conn);
extern List as_p_get_config(pgsql_conn_t *pg_conn);

extern List acct_storage_p_get_associations(pgsql_conn_t *pg_conn, uid_t uid,
					    acct_association_cond_t *assoc_cond);
extern List as_p_get_associations(pgsql_conn_t *pg_conn, uid_t uid,
				  acct_association_cond_t *assoc_cond);
extern int add_cluster_root_assoc(pgsql_conn_t *pg_conn, time_t now,
				  acct_cluster_rec_t *cluster, char **txn_info);

extern List acct_storage_p_get_problems(pgsql_conn_t *pg_conn, uid_t uid,
					acct_association_cond_t *assoc_q);
extern List as_p_get_problems(pgsql_conn_t *pg_conn, uid_t uid,
			      acct_association_cond_t *assoc_q);

extern List acct_storage_p_get_qos(pgsql_conn_t *pg_conn, uid_t uid,
				   acct_qos_cond_t *qos_cond);
extern List as_p_get_qos(pgsql_conn_t *pg_conn, uid_t uid,
			 acct_qos_cond_t *qos_cond);

extern List acct_storage_p_get_wckeys(pgsql_conn_t *pg_conn, uid_t uid,
				      acct_wckey_cond_t *wckey_cond);
extern List as_p_get_wckeys(pgsql_conn_t *pg_conn, uid_t uid,
			    acct_wckey_cond_t *wckey_cond);

extern uint32_t get_wckeyid(pgsql_conn_t *pg_conn, char **name,
			    uid_t uid, char *cluster, uint32_t associd);

extern List acct_storage_p_get_reservations(pgsql_conn_t *pg_conn, uid_t uid,
					    acct_reservation_cond_t *resv_cond);
extern List as_p_get_reservations(pgsql_conn_t *pg_conn, uid_t uid,
				  acct_reservation_cond_t *resv_cond);

extern List acct_storage_p_get_txn(pgsql_conn_t *pg_conn, uid_t uid,
				   acct_txn_cond_t *txn_cond);
extern List as_p_get_txn(pgsql_conn_t *pg_conn, uid_t uid,
			 acct_txn_cond_t *txn_cond);

extern int add_txn(pgsql_conn_t *pg_conn, time_t now,
		   slurmdbd_msg_type_t action,
		   char *object, char *actor, char *info);

extern int delete_assoc_usage(pgsql_conn_t *pg_conn, time_t now,
			      char *assoc_cond);

extern int acct_storage_p_get_usage(pgsql_conn_t *pg_conn, uid_t uid,
				    void *in, int type,
				    time_t start, time_t end);
extern int as_p_get_usage(pgsql_conn_t *pg_conn, uid_t uid,
			  void *in, slurmdbd_msg_type_t type,
			  time_t start, time_t end);

extern int acct_storage_p_roll_usage(pgsql_conn_t *pg_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data);
extern int as_p_roll_usage(pgsql_conn_t *pg_conn,
			   time_t sent_start, time_t sent_end,
			   uint16_t archive_data);
extern int
get_usage_for_assoc_list(pgsql_conn_t *pg_conn, List assoc_list,
			 time_t start, time_t end);
extern int
get_usage_for_wckey_list(pgsql_conn_t *pg_conn, List wckey_list,
			 time_t start, time_t end);

extern int get_cluster_cpu_nodes(pgsql_conn_t *pg_conn,
				 acct_cluster_rec_t *cluster);
extern int clusteracct_storage_p_node_down(pgsql_conn_t *pg_conn,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid);
extern int cs_p_node_down(pgsql_conn_t *pg_conn,
			  struct node_record *node_ptr,
			  time_t event_time, char *reason,
			  uint32_t reason_uid);

extern int clusteracct_storage_p_node_up(pgsql_conn_t *pg_conn,
					 struct node_record *node_ptr,
					 time_t event_time);
extern int cs_p_node_up(pgsql_conn_t *pg_conn,
			struct node_record *node_ptr,
			time_t event_time);

extern int clusteracct_storage_p_register_ctld(pgsql_conn_t *pg_conn,
					       char *cluster,
					       uint16_t port);
extern int cs_pg_register_ctld(pgsql_conn_t *pg_conn,
			       char *cluster,
			       uint16_t port);

extern int clusteracct_storage_p_cluster_cpus(pgsql_conn_t *pg_conn,
					       char *cluster_nodes,
					       uint32_t cpus,
					       time_t event_time);
extern int cs_p_cluster_cpus(pgsql_conn_t *pg_conn,
			     char *cluster_nodes,
			     uint32_t cpus,
			     time_t event_time);

extern int clusteracct_storage_p_get_usage(
	pgsql_conn_t *pg_conn, uid_t uid,
	acct_cluster_rec_t *cluster_rec, int type, time_t start, time_t end);
extern int cs_p_get_usage(
	pgsql_conn_t *pg_conn, uid_t uid,
	acct_cluster_rec_t *cluster_rec, int type, time_t start, time_t end);

extern int jobacct_storage_p_job_start(pgsql_conn_t *pg_conn,
				       struct job_record *job_ptr);
extern int js_p_job_start(pgsql_conn_t *pg_conn,
			  struct job_record *job_ptr);

extern int jobacct_storage_p_job_complete(pgsql_conn_t *pg_conn,
					  struct job_record *job_ptr);
extern int js_p_job_complete(pgsql_conn_t *pg_conn,
			     struct job_record *job_ptr);

extern int jobacct_storage_p_step_start(pgsql_conn_t *pg_conn,
					struct step_record *step_ptr);
extern int js_p_step_start(pgsql_conn_t *pg_conn,
			   struct step_record *step_ptr);

extern int jobacct_storage_p_step_complete(pgsql_conn_t *pg_conn,
					   struct step_record *step_ptr);
extern int js_p_step_complete(pgsql_conn_t *pg_conn,
					   struct step_record *step_ptr);

extern int jobacct_storage_p_suspend(pgsql_conn_t *pg_conn,
				     struct job_record *job_ptr);
extern int js_p_suspend(pgsql_conn_t *pg_conn,
			struct job_record *job_ptr);

extern List jobacct_storage_p_get_jobs_cond(pgsql_conn_t *pg_conn, uid_t uid,
					    acct_job_cond_t *job_cond);
extern List js_p_get_jobs_cond(pgsql_conn_t *pg_conn, uid_t uid,
			       acct_job_cond_t *job_cond);

extern int jobacct_storage_p_archive(pgsql_conn_t *pg_conn,
				     acct_archive_cond_t *arch_cond);
extern int js_p_archive(pgsql_conn_t *pg_conn,
			acct_archive_cond_t *arch_cond);

extern int jobacct_storage_p_archive_load(pgsql_conn_t *pg_conn,
					  acct_archive_rec_t *arch_rec);
extern int js_p_archive_load(pgsql_conn_t *pg_conn,
			     acct_archive_rec_t *arch_rec);

extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used);
extern int as_p_update_shares_used(void *db_conn,
				   List shares_used);

extern int acct_storage_p_flush_jobs_on_cluster(
	pgsql_conn_t *pg_conn, time_t event_time);
extern int as_p_flush_jobs_on_cluster(
	pgsql_conn_t *pg_conn, time_t event_time);

#endif
