/*****************************************************************************\
 *  slurm_accounting_storage.c - account storage plugin wrapper.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#define _GNU_SOURCE

#include <pthread.h>
#include <string.h>

#include "src/common/list.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/node_select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/sacctmgr/sacctmgr.h"
#include "src/slurmctld/slurmctld.h"

int with_slurmdbd = 0;
uid_t db_api_uid = -1;
/*
 * Local data
 */

typedef struct slurm_acct_storage_ops {
	void *(*get_conn)          (const slurm_trigger_callbacks_t *callbacks,
				    int conn_num, uint16_t *persist_conn_flags,
				    bool rollback, char *cluster_name);
	int  (*close_conn)         (void **db_conn);
	int  (*commit)             (void *db_conn, bool commit);
	int  (*add_users)          (void *db_conn, uint32_t uid,
				    List user_list);
	int  (*add_coord)          (void *db_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond);
	int  (*add_accts)          (void *db_conn, uint32_t uid,
				    List acct_list);
	int  (*add_clusters)       (void *db_conn, uint32_t uid,
				    List cluster_list);
	int  (*add_federations)    (void *db_conn, uint32_t uid,
				    List federation_list);
	int  (*add_tres)           (void *db_conn, uint32_t uid,
				    List tres_list_in);
	int  (*add_assocs)         (void *db_conn, uint32_t uid,
				    List assoc_list);
	int  (*add_qos)            (void *db_conn, uint32_t uid,
				    List qos_list);
	int  (*add_res)            (void *db_conn, uint32_t uid,
				    List res_list);
	int  (*add_wckeys)         (void *db_conn, uint32_t uid,
				    List wckey_list);
	int  (*add_reservation)    (void *db_conn,
				    slurmdb_reservation_rec_t *resv);
	List (*modify_users)       (void *db_conn, uint32_t uid,
				    slurmdb_user_cond_t *user_cond,
				    slurmdb_user_rec_t *user);
	List (*modify_accts)       (void *db_conn, uint32_t uid,
				    slurmdb_account_cond_t *acct_cond,
				    slurmdb_account_rec_t *acct);
	List (*modify_clusters)    (void *db_conn, uint32_t uid,
				    slurmdb_cluster_cond_t *cluster_cond,
				    slurmdb_cluster_rec_t *cluster);
	List (*modify_assocs)      (void *db_conn, uint32_t uid,
				    slurmdb_assoc_cond_t *assoc_cond,
				    slurmdb_assoc_rec_t *assoc);
	List (*modify_federations) (void *db_conn, uint32_t uid,
				    slurmdb_federation_cond_t *fed_cond,
				    slurmdb_federation_rec_t *fed);
	List (*modify_job)         (void *db_conn, uint32_t uid,
				    slurmdb_job_modify_cond_t *job_cond,
				    slurmdb_job_rec_t *job);
	List (*modify_qos)         (void *db_conn, uint32_t uid,
				    slurmdb_qos_cond_t *qos_cond,
				    slurmdb_qos_rec_t *qos);
	List (*modify_res)         (void *db_conn, uint32_t uid,
				    slurmdb_res_cond_t *res_cond,
				    slurmdb_res_rec_t *res);
	List (*modify_wckeys)      (void *db_conn, uint32_t uid,
				    slurmdb_wckey_cond_t *wckey_cond,
				    slurmdb_wckey_rec_t *wckey);
	int  (*modify_reservation) (void *db_conn,
				    slurmdb_reservation_rec_t *resv);
	List (*remove_users)       (void *db_conn, uint32_t uid,
				    slurmdb_user_cond_t *user_cond);
	List (*remove_coord)       (void *db_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond);
	List (*remove_accts)       (void *db_conn, uint32_t uid,
				    slurmdb_account_cond_t *acct_cond);
	List (*remove_clusters)    (void *db_conn, uint32_t uid,
				    slurmdb_cluster_cond_t *cluster_cond);
	List (*remove_assocs)      (void *db_conn, uint32_t uid,
				    slurmdb_assoc_cond_t *assoc_cond);
	List (*remove_federations) (void *db_conn, uint32_t uid,
				    slurmdb_federation_cond_t *fed_cond);
	List (*remove_qos)         (void *db_conn, uint32_t uid,
				    slurmdb_qos_cond_t *qos_cond);
	List (*remove_res)         (void *db_conn, uint32_t uid,
				    slurmdb_res_cond_t *res_cond);
	List (*remove_wckeys)      (void *db_conn, uint32_t uid,
				    slurmdb_wckey_cond_t *wckey_cond);
	int  (*remove_reservation) (void *db_conn,
				    slurmdb_reservation_rec_t *resv);
	List (*get_users)          (void *db_conn, uint32_t uid,
				    slurmdb_user_cond_t *user_cond);
	List (*get_accts)          (void *db_conn, uint32_t uid,
				    slurmdb_account_cond_t *acct_cond);
	List (*get_clusters)       (void *db_conn, uint32_t uid,
				    slurmdb_cluster_cond_t *cluster_cond);
	List (*get_federations)    (void *db_conn, uint32_t uid,
				    slurmdb_federation_cond_t *fed_cond);
	List (*get_config)         (void *db_conn, char *config_name);
	List (*get_tres)           (void *db_conn, uint32_t uid,
				    slurmdb_tres_cond_t *tres_cond);
	List (*get_assocs)         (void *db_conn, uint32_t uid,
				    slurmdb_assoc_cond_t *assoc_cond);
	List (*get_events)         (void *db_conn, uint32_t uid,
				    slurmdb_event_cond_t *event_cond);
	List (*get_problems)       (void *db_conn, uint32_t uid,
				    slurmdb_assoc_cond_t *assoc_cond);
	List (*get_qos)            (void *db_conn, uint32_t uid,
				    slurmdb_qos_cond_t *qos_cond);
	List (*get_res)            (void *db_conn, uint32_t uid,
				    slurmdb_res_cond_t *res_cond);
	List (*get_wckeys)         (void *db_conn, uint32_t uid,
				    slurmdb_wckey_cond_t *wckey_cond);
	List (*get_resvs)          (void *db_conn, uint32_t uid,
				    slurmdb_reservation_cond_t *resv_cond);
	List (*get_txn)            (void *db_conn, uint32_t uid,
				    slurmdb_txn_cond_t *txn_cond);
	int  (*get_usage)          (void *db_conn, uint32_t uid,
				    void *in, int type,
				    time_t start,
				    time_t end);
	int (*roll_usage)          (void *db_conn,
				    time_t sent_start, time_t sent_end,
				    uint16_t archive_data,
				    rollup_stats_t *rollup_stats);
	int  (*fix_runaway_jobs)   (void *db_conn, uint32_t uid, List jobs);
	int  (*node_down)          (void *db_conn,
				    struct node_record *node_ptr,
				    time_t event_time,
				    char *reason, uint32_t reason_uid);
	int  (*node_up)            (void *db_conn,
				    struct node_record *node_ptr,
				    time_t event_time);
	int  (*cluster_tres)       (void *db_conn, char *cluster_nodes,
				    char *tres_str_in, time_t event_time,
				    uint16_t rpc_version);
	int  (*register_ctld)      (void *db_conn, uint16_t port);
	int  (*register_disconn_ctld)(void *db_conn, char *control_host);
	int  (*fini_ctld)          (void *db_conn,
				    slurmdb_cluster_rec_t *cluster_rec);
	int  (*job_start)          (void *db_conn, struct job_record *job_ptr);
	int  (*job_complete)       (void *db_conn,
				    struct job_record *job_ptr);
	int  (*step_start)         (void *db_conn,
				    struct step_record *step_ptr);
	int  (*step_complete)      (void *db_conn,
				    struct step_record *step_ptr);
	int  (*job_suspend)        (void *db_conn,
				    struct job_record *job_ptr);
	List (*get_jobs_cond)      (void *db_conn, uint32_t uid,
				    slurmdb_job_cond_t *job_cond);
	int (*archive_dump)        (void *db_conn,
				    slurmdb_archive_cond_t *arch_cond);
	int (*archive_load)        (void *db_conn,
				    slurmdb_archive_rec_t *arch_rec);
	int (*update_shares_used)  (void *db_conn,
				    List shares_used);
	int (*flush_jobs)          (void *db_conn,
				    time_t event_time);
	int (*reconfig)            (void *db_conn, bool dbd);
	int (*reset_lft_rgt)       (void *db_conn, uid_t uid,
				    List cluster_list);
	int (*get_stats)           (void *db_conn, slurmdb_stats_rec_t **stats);
	int (*clear_stats)         (void *db_conn);
	int (*get_data)            (void *db_conn, acct_storage_info_t dinfo,
				    void *data);
	int (*shutdown)            (void *db_conn);
} slurm_acct_storage_ops_t;
/*
 * Must be synchronized with slurm_acct_storage_ops_t above.
 */
static const char *syms[] = {
	"acct_storage_p_get_connection",
	"acct_storage_p_close_connection",
	"acct_storage_p_commit",
	"acct_storage_p_add_users",
	"acct_storage_p_add_coord",
	"acct_storage_p_add_accts",
	"acct_storage_p_add_clusters",
	"acct_storage_p_add_federations",
	"acct_storage_p_add_tres",
	"acct_storage_p_add_assocs",
	"acct_storage_p_add_qos",
	"acct_storage_p_add_res",
	"acct_storage_p_add_wckeys",
	"acct_storage_p_add_reservation",
	"acct_storage_p_modify_users",
	"acct_storage_p_modify_accts",
	"acct_storage_p_modify_clusters",
	"acct_storage_p_modify_assocs",
	"acct_storage_p_modify_federations",
	"acct_storage_p_modify_job",
	"acct_storage_p_modify_qos",
	"acct_storage_p_modify_res",
	"acct_storage_p_modify_wckeys",
	"acct_storage_p_modify_reservation",
	"acct_storage_p_remove_users",
	"acct_storage_p_remove_coord",
	"acct_storage_p_remove_accts",
	"acct_storage_p_remove_clusters",
	"acct_storage_p_remove_assocs",
	"acct_storage_p_remove_federations",
	"acct_storage_p_remove_qos",
	"acct_storage_p_remove_res",
	"acct_storage_p_remove_wckeys",
	"acct_storage_p_remove_reservation",
	"acct_storage_p_get_users",
	"acct_storage_p_get_accts",
	"acct_storage_p_get_clusters",
	"acct_storage_p_get_federations",
	"acct_storage_p_get_config",
	"acct_storage_p_get_tres",
	"acct_storage_p_get_assocs",
	"acct_storage_p_get_events",
	"acct_storage_p_get_problems",
	"acct_storage_p_get_qos",
	"acct_storage_p_get_res",
	"acct_storage_p_get_wckeys",
	"acct_storage_p_get_reservations",
	"acct_storage_p_get_txn",
	"acct_storage_p_get_usage",
	"acct_storage_p_roll_usage",
	"acct_storage_p_fix_runaway_jobs",
	"clusteracct_storage_p_node_down",
	"clusteracct_storage_p_node_up",
	"clusteracct_storage_p_cluster_tres",
	"clusteracct_storage_p_register_ctld",
	"clusteracct_storage_p_register_disconn_ctld",
	"clusteracct_storage_p_fini_ctld",
	"jobacct_storage_p_job_start",
	"jobacct_storage_p_job_complete",
	"jobacct_storage_p_step_start",
	"jobacct_storage_p_step_complete",
	"jobacct_storage_p_suspend",
	"jobacct_storage_p_get_jobs_cond",
	"jobacct_storage_p_archive",
	"jobacct_storage_p_archive_load",
	"acct_storage_p_update_shares_used",
	"acct_storage_p_flush_jobs_on_cluster",
	"acct_storage_p_reconfig",
	"acct_storage_p_reset_lft_rgt",
	"acct_storage_p_get_stats",
	"acct_storage_p_clear_stats",
	"acct_storage_p_get_data",
	"acct_storage_p_shutdown",
};

static slurm_acct_storage_ops_t ops;
static plugin_context_t *plugin_context = NULL;
static pthread_mutex_t plugin_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;
static uint16_t enforce = 0;

/*
 * If running with slurmdbd don't run if we don't have an index, else
 * go ahead.
 */
extern int jobacct_storage_job_start_direct(void *db_conn,
					    struct job_record *job_ptr)
{
	if (with_slurmdbd && !job_ptr->db_index)
		return SLURM_SUCCESS;

	return jobacct_storage_g_job_start(db_conn, job_ptr);
}

/*
 * Initialize context for acct_storage plugin
 */
extern int slurm_acct_storage_init(char *loc)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "accounting_storage";
	char *type = NULL;

	if (init_run && plugin_context)
		return retval;

	slurm_mutex_lock(&plugin_context_lock);

	if (plugin_context)
		goto done;

	if (loc)
		slurm_set_accounting_storage_loc(loc);

	type = slurm_get_accounting_storage_type();

	plugin_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!plugin_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;
	enforce = slurm_get_accounting_storage_enforce();
done:
	slurm_mutex_unlock(&plugin_context_lock);
	xfree(type);
	return retval;
}

extern int slurm_acct_storage_fini(void)
{
	int rc;

	if (!plugin_context)
		return SLURM_SUCCESS;

	init_run = false;
//	(*(ops.acct_storage_fini))();
	rc = plugin_context_destroy(plugin_context);
	plugin_context = NULL;
	return rc;
}

extern void *acct_storage_g_get_connection(
	const slurm_trigger_callbacks_t *callbacks,
	int conn_num, uint16_t *persist_conn_flags,
	bool rollback,char *cluster_name)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_conn))(callbacks, conn_num, persist_conn_flags,
				 rollback, cluster_name);
}

extern int acct_storage_g_close_connection(void **db_conn)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.close_conn))(db_conn);

}

extern int acct_storage_g_commit(void *db_conn, bool commit)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.commit))(db_conn, commit);

}

extern int acct_storage_g_add_users(void *db_conn, uint32_t uid,
				    List user_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_users))(db_conn, uid, user_list);
}

extern int acct_storage_g_add_coord(void *db_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_coord))(db_conn, uid, acct_list, user_cond);
}

extern int acct_storage_g_add_accounts(void *db_conn, uint32_t uid,
				       List acct_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_accts))(db_conn, uid, acct_list);
}

extern int acct_storage_g_add_clusters(void *db_conn, uint32_t uid,
				       List cluster_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_clusters))(db_conn, uid, cluster_list);
}

extern int acct_storage_g_add_federations(void *db_conn, uint32_t uid,
					  List federation_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_federations))(db_conn, uid, federation_list);
}

extern int acct_storage_g_add_tres(void *db_conn, uint32_t uid,
				   List tres_list_in)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_tres))(db_conn, uid, tres_list_in);
}

extern int acct_storage_g_add_assocs(void *db_conn, uint32_t uid,
				     List assoc_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_assocs))(db_conn, uid, assoc_list);
}

extern int acct_storage_g_add_qos(void *db_conn, uint32_t uid,
				  List qos_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_qos))(db_conn, uid, qos_list);
}

extern int acct_storage_g_add_res(void *db_conn, uint32_t uid,
				  List res_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_res))(db_conn, uid, res_list);
}
extern int acct_storage_g_add_wckeys(void *db_conn, uint32_t uid,
				     List wckey_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.add_wckeys))(db_conn, uid, wckey_list);
}

extern int acct_storage_g_add_reservation(void *db_conn,
					  slurmdb_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(ops.add_reservation))(db_conn, resv);
}

extern List acct_storage_g_modify_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond,
					slurmdb_user_rec_t *user)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.modify_users))(db_conn, uid, user_cond, user);
}

extern List acct_storage_g_modify_accounts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond,
					   slurmdb_account_rec_t *acct)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.modify_accts))(db_conn, uid, acct_cond, acct);
}

extern List acct_storage_g_modify_clusters(void *db_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond,
					   slurmdb_cluster_rec_t *cluster)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.modify_clusters))(db_conn, uid, cluster_cond, cluster);
}

extern List acct_storage_g_modify_assocs(
	void *db_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond,
	slurmdb_assoc_rec_t *assoc)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.modify_assocs))(db_conn, uid, assoc_cond, assoc);
}

extern List acct_storage_g_modify_federations(
				void *db_conn, uint32_t uid,
				slurmdb_federation_cond_t *fed_cond,
				slurmdb_federation_rec_t *fed)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.modify_federations))(db_conn, uid, fed_cond, fed);
}

extern List acct_storage_g_modify_job(void *db_conn, uint32_t uid,
				      slurmdb_job_modify_cond_t *job_cond,
				      slurmdb_job_rec_t *job)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;

	return (*(ops.modify_job))(db_conn, uid, job_cond, job);
}

extern List acct_storage_g_modify_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.modify_qos))(db_conn, uid, qos_cond, qos);
}

extern List acct_storage_g_modify_res(void *db_conn, uint32_t uid,
				      slurmdb_res_cond_t *res_cond,
				      slurmdb_res_rec_t *res)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.modify_res))(db_conn, uid, res_cond, res);
}

extern List acct_storage_g_modify_wckeys(void *db_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond,
					 slurmdb_wckey_rec_t *wckey)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.modify_wckeys))(db_conn, uid, wckey_cond, wckey);
}

extern int acct_storage_g_modify_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(ops.modify_reservation))(db_conn, resv);
}

extern List acct_storage_g_remove_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_users))(db_conn, uid, user_cond);
}

extern List acct_storage_g_remove_coord(void *db_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_coord))(db_conn, uid, acct_list, user_cond);
}

extern List acct_storage_g_remove_accounts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_accts))(db_conn, uid, acct_cond);
}

extern List acct_storage_g_remove_clusters(void *db_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_clusters))(db_conn, uid, cluster_cond);
}

extern List acct_storage_g_remove_assocs(
	void *db_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_assocs))(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_remove_federations(
					void *db_conn, uint32_t uid,
					slurmdb_federation_cond_t *fed_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_federations))(db_conn, uid, fed_cond);
}

extern List acct_storage_g_remove_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_qos))(db_conn, uid, qos_cond);
}

extern List acct_storage_g_remove_res(void *db_conn, uint32_t uid,
				      slurmdb_res_cond_t *res_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_res))(db_conn, uid, res_cond);
}

extern List acct_storage_g_remove_wckeys(void *db_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.remove_wckeys))(db_conn, uid, wckey_cond);
}

extern int acct_storage_g_remove_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(ops.remove_reservation))(db_conn, resv);
}

extern List acct_storage_g_get_users(void *db_conn, uint32_t uid,
				     slurmdb_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_users))(db_conn, uid, user_cond);
}

extern List acct_storage_g_get_accounts(void *db_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_accts))(db_conn, uid, acct_cond);
}

extern List acct_storage_g_get_clusters(void *db_conn, uint32_t uid,
					slurmdb_cluster_cond_t *cluster_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_clusters))(db_conn, uid, cluster_cond);
}

extern List acct_storage_g_get_federations(void *db_conn, uint32_t uid,
					   slurmdb_federation_cond_t *fed_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_federations))(db_conn, uid, fed_cond);
}

extern List acct_storage_g_get_config(void *db_conn, char *config_name)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_config))(db_conn, config_name);
}

extern List acct_storage_g_get_tres(
	void *db_conn, uint32_t uid,
	slurmdb_tres_cond_t *tres_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_tres))(db_conn, uid, tres_cond);
}

extern List acct_storage_g_get_assocs(
	void *db_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_assocs))(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_get_events(void *db_conn, uint32_t uid,
				      slurmdb_event_cond_t *event_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_events))(db_conn, uid, event_cond);
}

extern List acct_storage_g_get_problems(void *db_conn, uint32_t uid,
					slurmdb_assoc_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_problems))(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_get_qos(void *db_conn, uint32_t uid,
				   slurmdb_qos_cond_t *qos_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_qos))(db_conn, uid, qos_cond);
}

extern List acct_storage_g_get_res(void *db_conn, uint32_t uid,
				   slurmdb_res_cond_t *res_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_res))(db_conn, uid, res_cond);
}

extern List acct_storage_g_get_wckeys(void *db_conn, uint32_t uid,
				      slurmdb_wckey_cond_t *wckey_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_wckeys))(db_conn, uid, wckey_cond);
}

extern List acct_storage_g_get_reservations(
	void *db_conn, uint32_t uid, slurmdb_reservation_cond_t *resv_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_resvs))(db_conn, uid, resv_cond);
}

extern List acct_storage_g_get_txn(void *db_conn,  uint32_t uid,
				   slurmdb_txn_cond_t *txn_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(ops.get_txn))(db_conn, uid, txn_cond);
}

extern int acct_storage_g_get_usage(void *db_conn,  uint32_t uid,
				    void *in, int type,
				    time_t start, time_t end)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.get_usage))(db_conn, uid, in, type, start, end);
}

extern int acct_storage_g_roll_usage(void *db_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data,
				     rollup_stats_t *rollup_stats)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.roll_usage))(db_conn, sent_start, sent_end, archive_data,
				   rollup_stats);
}

extern int acct_storage_g_fix_runaway_jobs(void *db_conn,
					   uint32_t uid, List jobs)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.fix_runaway_jobs))(db_conn, uid, jobs);

}

extern int clusteracct_storage_g_node_down(void *db_conn,
					   struct node_record *node_ptr,
					   time_t event_time,
					   char *reason, uint32_t reason_uid)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.node_down))(db_conn, node_ptr, event_time,
				  reason, reason_uid);
}

extern int clusteracct_storage_g_node_up(void *db_conn,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;

	xfree(node_ptr->reason);
	node_ptr->reason_time = 0;
	node_ptr->reason_uid = NO_VAL;

	return (*(ops.node_up))(db_conn, node_ptr, event_time);
}


extern int clusteracct_storage_g_cluster_tres(void *db_conn,
					      char *cluster_nodes,
					      char *tres_str_in,
					      time_t event_time,
					      uint16_t rpc_version)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.cluster_tres))(db_conn, cluster_nodes,
				     tres_str_in, event_time, rpc_version);
}


extern int clusteracct_storage_g_register_ctld(void *db_conn, uint16_t port)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.register_ctld))(db_conn, port);
}

extern int clusteracct_storage_g_register_disconn_ctld(
	void *db_conn, char *control_host)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.register_disconn_ctld))(db_conn, control_host);
}

extern int clusteracct_storage_g_fini_ctld(void *db_conn,
					   slurmdb_cluster_rec_t *cluster_rec)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.fini_ctld))(db_conn, cluster_rec);
}

/*
 * load into the storage information about a job,
 * typically when it begins execution, but possibly earlier
 */
extern int jobacct_storage_g_job_start(void *db_conn,
				       struct job_record *job_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	if (enforce & ACCOUNTING_ENFORCE_NO_JOBS)
		return SLURM_SUCCESS;

	/* A pending job's start_time is it's expected initiation time
	 * (changed in slurm v2.1). Rather than changing a bunch of code
	 * in the accounting_storage plugins and SlurmDBD, just clear
	 * start_time before accounting and restore it later.
	 * If an update for a job that is being requeued[hold] happens,
	 * we don't want to modify the start_time of the old record.
	 * Pending + Completing is equivalent to Requeue.
	 */
	if (IS_JOB_PENDING(job_ptr) && !IS_JOB_COMPLETING(job_ptr)) {
		int rc;
		time_t orig_start_time = job_ptr->start_time;
		job_ptr->start_time = (time_t) 0;
		rc = (*(ops.job_start))(db_conn, job_ptr);
		job_ptr->start_time = orig_start_time;
		return rc;
	}

	return (*(ops.job_start))(db_conn, job_ptr);
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_g_job_complete(void *db_conn,
					  struct job_record *job_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	if (enforce & ACCOUNTING_ENFORCE_NO_JOBS)
		return SLURM_SUCCESS;
	return (*(ops.job_complete))(db_conn, job_ptr);
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_g_step_start(void *db_conn,
					struct step_record *step_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	if (enforce & ACCOUNTING_ENFORCE_NO_STEPS)
		return SLURM_SUCCESS;
	return (*(ops.step_start))(db_conn, step_ptr);
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_g_step_complete(void *db_conn,
					   struct step_record *step_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	if (enforce & ACCOUNTING_ENFORCE_NO_STEPS)
		return SLURM_SUCCESS;
	return (*(ops.step_complete))(db_conn, step_ptr);
}

/*
 * load into the storage a suspension of a job
 */
extern int jobacct_storage_g_job_suspend(void *db_conn,
					 struct job_record *job_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	if (enforce & ACCOUNTING_ENFORCE_NO_JOBS)
		return SLURM_SUCCESS;
	return (*(ops.job_suspend))(db_conn, job_ptr);
}

static int _sort_desc_submit_time(void *x, void *y)
{
	slurmdb_job_rec_t *j1 = *(slurmdb_job_rec_t **)x;
	slurmdb_job_rec_t *j2 = *(slurmdb_job_rec_t **)y;

	if (j1->submit < j2->submit)
		return -1;
	else if (j1->submit > j2->submit)
		return 1;

	return 0;
}

/*
 * get info from the storage
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_g_get_jobs_cond(void *db_conn, uint32_t uid,
					    slurmdb_job_cond_t *job_cond)
{
	List ret_list;

	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	ret_list = (*(ops.get_jobs_cond))(db_conn, uid, job_cond);

	/* If multiple clusters were requested, the jobs are grouped together by
	 * cluster -- each group sorted by submit time. Sort all the jobs by
	 * submit time */
	if (ret_list && job_cond && job_cond->cluster_list &&
	    (list_count(job_cond->cluster_list) > 1))
		list_sort(ret_list, (ListCmpF)_sort_desc_submit_time);

	return ret_list;
}

/*
 * expire old info from the storage
 */
extern int jobacct_storage_g_archive(void *db_conn,
				     slurmdb_archive_cond_t *arch_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.archive_dump))(db_conn, arch_cond);
}

/*
 * load expired info into the storage
 */
extern int jobacct_storage_g_archive_load(void *db_conn,
					  slurmdb_archive_rec_t *arch_rec)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.archive_load))(db_conn, arch_rec);

}

/*
 * record shares used information for backup in case slurmctld restarts
 * IN:  account_list List of shares_used_object_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_update_shares_used(void *db_conn, List acct_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.update_shares_used))(db_conn, acct_list);
}

/*
 * This should be called when a cluster does a cold start to flush out
 * any jobs that were running during the restart so we don't have any
 * jobs in the database "running" forever since no endtime will be
 * placed in there other wise.
 * IN:  char * = cluster name
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_flush_jobs_on_cluster(
	void *db_conn, time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.flush_jobs))(db_conn, event_time);

}

/*
 * When a reconfigure happens this should be called.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_reconfig(void *db_conn, bool dbd)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.reconfig))(db_conn, dbd);

}

/*
 * Reset the lft and rights of an association table.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_reset_lft_rgt(void *db_conn, uid_t uid,
					List cluster_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.reset_lft_rgt))(db_conn, uid, cluster_list);

}

/*
 * Get performance statistics.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_get_stats(void *db_conn, slurmdb_stats_rec_t **stats)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.get_stats))(db_conn, stats);
}

/*
 * Clear performance statistics.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_clear_stats(void *db_conn)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.clear_stats))(db_conn);
}

/*
 * Get generic data.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_get_data(void *db_conn, acct_storage_info_t dinfo,
				    void *data)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.get_data))(db_conn, dinfo, data);
}


/*
 * Shutdown database server.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_shutdown(void *db_conn)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(ops.shutdown))(db_conn);

}
