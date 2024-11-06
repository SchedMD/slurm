/*****************************************************************************\
 *  accounting_storage_ctld_relay.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "config.h"

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <inttypes.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/list.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurmdbd_defs.h"

#include "src/interfaces/accounting_storage.h"

#include "../common/common_as.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined(__APPLE__)
extern slurm_conf_t slurm_conf __attribute__((weak_import));
#else
slurm_conf_t slurm_conf;
#endif

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Accounting storage CTLD Relay plugin";
const char plugin_type[] = "accounting_storage/ctld_relay";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Satisfy common lib */
char *assoc_day_table = NULL;
char *assoc_hour_table = NULL;
char *assoc_month_table = NULL;
char *cluster_day_table = NULL;
char *cluster_hour_table = NULL;
char *cluster_month_table = NULL;
char *qos_day_table = NULL;
char *qos_hour_table = NULL;
char *qos_month_table = NULL;
char *wckey_day_table = NULL;
char *wckey_hour_table = NULL;
char *wckey_month_table = NULL;

extern int jobacct_storage_p_job_start(void *db_conn, job_record_t *job_ptr);
extern int jobacct_storage_p_job_heavy(void *db_conn, job_record_t *job_ptr);
extern void acct_storage_p_send_all(void *db_conn, time_t event_time,
				    slurm_msg_type_t msg_type);

static persist_conn_t persist_conn = {
	.flags = PERSIST_FLAG_DBD,
	.version = SLURM_PROTOCOL_VERSION,
};

static list_t *agent_list;
pthread_t agent_thread_id = 0;
pthread_cond_t  agent_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t agent_lock = PTHREAD_MUTEX_INITIALIZER;
bool agent_shutdown = false;

static void *_agent_thread(void *data)
{
	struct timespec ts = {0, 0};

	while (!agent_shutdown) {
		buf_t *buffer;

		slurm_mutex_lock(&agent_lock);
		if (!agent_shutdown) {
			ts.tv_sec = time(NULL) + 2;
			slurm_cond_timedwait(&agent_cond, &agent_lock, &ts);
		}
		slurm_mutex_unlock(&agent_lock);

		while ((buffer = list_pop(agent_list))) {
			int rc;
			slurm_msg_t msg;
			persist_msg_t persist_msg = {0};

			set_buf_offset(buffer, 0);
			if (slurm_persist_msg_unpack(&persist_conn,
						     &persist_msg, buffer) !=
			    SLURM_SUCCESS) {
				/* This should never happen, we packed it */
				error("%s: Failed to unpack persist msg, can't send '%s' to controller.",
				      __func__,
				      rpc_num2string(REQUEST_DBD_RELAY));
				slurmdbd_free_msg(&persist_msg);
				FREE_NULL_BUFFER(buffer);
				continue;
			}

			slurm_msg_t_init(&msg);
			msg.msg_type = REQUEST_DBD_RELAY;
			msg.data = &persist_msg;
			msg.protocol_version = persist_conn.version;

			while (slurm_send_recv_controller_rc_msg(&msg, &rc,
								 NULL)) {
				error("%s: failed to send '%s' to controller, retrying",
				      __func__, rpc_num2string(msg.msg_type));
				sleep(1);
			}

			slurmdbd_free_msg(&persist_msg);
			FREE_NULL_BUFFER(buffer);
		}
	}

	debug("shutting down ctld_relay agent thread");

	return NULL;
}

static void _agent_append(buf_t *buffer)
{
	list_append(agent_list, buffer);
	slurm_cond_signal(&agent_cond);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	agent_list = list_create(NULL);

	slurm_mutex_lock(&agent_lock);
	slurm_thread_create(&agent_thread_id, _agent_thread, NULL);
	slurm_mutex_unlock(&agent_lock);

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	agent_shutdown = true;

	slurm_mutex_lock(&agent_lock);
	slurm_cond_signal(&agent_cond);
	slurm_mutex_unlock(&agent_lock);

	slurm_thread_join(agent_thread_id);

	FREE_NULL_LIST(agent_list);

	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(
	int conn_num, uint16_t *persist_conn_flags,
	bool rollback, char *cluster_name)
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
				    list_t *user_list)
{
	return SLURM_SUCCESS;
}

extern char *acct_storage_p_add_users_cond(void *db_conn, uint32_t uid,
					   slurmdb_add_assoc_cond_t *add_assoc,
					   slurmdb_user_rec_t *user)
{
	return NULL;
}

extern int acct_storage_p_add_coord(void *db_conn, uint32_t uid,
				    list_t *acct_list,
				    slurmdb_user_cond_t *user_cond)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(void *db_conn, uint32_t uid,
				    list_t *acct_list)
{
	return SLURM_SUCCESS;
}

extern char *acct_storage_p_add_accts_cond(void *db_conn, uint32_t uid,
					   slurmdb_add_assoc_cond_t *add_assoc,
					   slurmdb_account_rec_t *acct)
{
	return NULL;
}

extern int acct_storage_p_add_clusters(void *db_conn, uint32_t uid,
				       list_t *cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_federations(void *db_conn, uint32_t uid,
					  list_t *federation_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_tres(void *db_conn,
				   uint32_t uid, list_t *tres_list_in)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_assocs(void *db_conn, uint32_t uid,
				     list_t *assoc_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_qos(void *db_conn, uint32_t uid,
				  list_t *qos_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_res(void *db_conn, uint32_t uid,
				  list_t *res_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_wckeys(void *db_conn, uint32_t uid,
				     list_t *wckey_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_reservation(void *db_conn,
					  slurmdb_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern list_t *acct_storage_p_modify_users(void *db_conn, uint32_t uid,
					   slurmdb_user_cond_t *user_cond,
					   slurmdb_user_rec_t *user)
{
	return NULL;
}

extern list_t *acct_storage_p_modify_accts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond,
					   slurmdb_account_rec_t *acct)
{
	return NULL;
}

extern list_t *acct_storage_p_modify_clusters(void *db_conn, uint32_t uid,
					      slurmdb_cluster_cond_t *cluster_cond,
					      slurmdb_cluster_rec_t *cluster)
{
	return NULL;
}

extern list_t *acct_storage_p_modify_assocs(
	void *db_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond,
	slurmdb_assoc_rec_t *assoc)
{
	return NULL;
}

extern list_t *acct_storage_p_modify_federations(
	void *db_conn, uint32_t uid,
	slurmdb_federation_cond_t *fed_cond,
	slurmdb_federation_rec_t *fed)
{
	return NULL;
}

extern list_t *acct_storage_p_modify_job(void *db_conn, uint32_t uid,
					 slurmdb_job_cond_t *job_cond,
					 slurmdb_job_rec_t *job)
{
	return NULL;
}

extern list_t *acct_storage_p_modify_qos(void *db_conn, uint32_t uid,
					 slurmdb_qos_cond_t *qos_cond,
					 slurmdb_qos_rec_t *qos)
{
	return NULL;
}

extern list_t *acct_storage_p_modify_res(void *db_conn, uint32_t uid,
					 slurmdb_res_cond_t *res_cond,
					 slurmdb_res_rec_t *res)
{
	return NULL;
}

extern list_t *acct_storage_p_modify_wckeys(void *db_conn, uint32_t uid,
					    slurmdb_wckey_cond_t *wckey_cond,
					    slurmdb_wckey_rec_t *wckey)
{
	return NULL;
}

extern int acct_storage_p_modify_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern list_t *acct_storage_p_remove_users(void *db_conn, uint32_t uid,
					   slurmdb_user_cond_t *user_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_remove_coord(void *db_conn, uint32_t uid,
					   list_t *acct_list,
					   slurmdb_user_cond_t *user_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_remove_accts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_remove_clusters(void *db_conn, uint32_t uid,
					      slurmdb_account_cond_t *cluster_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_remove_assocs(
	void *db_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_remove_federations(
	void *db_conn, uint32_t uid,
	slurmdb_federation_cond_t *fed_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_remove_qos(
	void *db_conn, uint32_t uid,
	slurmdb_qos_cond_t *qos_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_remove_res(
	void *db_conn, uint32_t uid,
	slurmdb_res_cond_t *res_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_remove_wckeys(
	void *db_conn, uint32_t uid,
	slurmdb_wckey_cond_t *wckey_cond)
{
	return NULL;
}

extern int acct_storage_p_remove_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern list_t *acct_storage_p_get_users(void *db_conn, uid_t uid,
					slurmdb_user_cond_t *user_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_accts(void *db_conn, uid_t uid,
					slurmdb_account_cond_t *acct_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_clusters(void *db_conn, uid_t uid,
					   slurmdb_cluster_cond_t *cluster_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_federations(void *db_conn, uid_t uid,
					      slurmdb_federation_cond_t *fed_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_config(void *db_conn, char *config_name)
{
	return NULL;
}

extern list_t *acct_storage_p_get_tres(void *db_conn, uid_t uid,
				       slurmdb_tres_cond_t *tres_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_assocs(
	void *db_conn, uid_t uid, slurmdb_assoc_cond_t *assoc_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_events(void *db_conn, uint32_t uid,
					 slurmdb_event_cond_t *event_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_instances(
	void *db_conn, uint32_t uid,
	slurmdb_instance_cond_t *instance_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_problems(void *db_conn, uid_t uid,
					   slurmdb_assoc_cond_t *assoc_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_qos(void *db_conn, uid_t uid,
				      slurmdb_qos_cond_t *qos_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_res(void *db_conn, uid_t uid,
				      slurmdb_res_cond_t *res_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_wckeys(void *db_conn, uid_t uid,
					 slurmdb_wckey_cond_t *wckey_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_reservations(
	void *db_conn, uid_t uid,
	slurmdb_reservation_cond_t *resv_cond)
{
	return NULL;
}

extern list_t *acct_storage_p_get_txn(void *db_conn, uid_t uid,
				      slurmdb_txn_cond_t *txn_cond)
{
	return NULL;
}

extern int acct_storage_p_get_usage(void *db_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_roll_usage(void *db_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data,
				     list_t **rollup_stats_list_in)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_fix_runaway_jobs(void *db_conn, uint32_t uid,
					   list_t *jobs)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_node_down(void *db_conn,
					   node_record_t *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	return SLURM_SUCCESS;
}

extern char *acct_storage_p_node_inx(void *db_conn, char *nodes)
{
	return NULL;
}

extern int clusteracct_storage_p_node_up(void *db_conn, node_record_t *node_ptr,
					 time_t event_time)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_node_update(void *db_conn,
					     node_record_t *node_ptr,
					     time_t event_time)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_tres(void *db_conn,
					      char *cluster_nodes_in,
					      char *tres_str_in,
					      time_t event_time,
					      uint16_t rpc_version)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_register_ctld(void *db_conn, uint16_t port)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_register_disconn_ctld(
	void *db_conn, char *control_host)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_fini_ctld(void *db_conn,
					   char *ip, uint16_t port,
					   char *cluster_nodes)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(void *db_conn, job_record_t *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int jobacct_storage_p_job_heavy(void *db_conn, job_record_t *job_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(void *db_conn, job_record_t *job_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(void *db_conn, step_record_t *step_ptr)
{
	buf_t *buffer;
	persist_msg_t persist_msg = {0};
	dbd_step_start_msg_t req = {0};

	if (as_build_step_start_msg(&req, step_ptr))
	    return SLURM_ERROR;

	persist_msg.msg_type = DBD_STEP_START;
	persist_msg.data = &req;

	buffer = slurm_persist_msg_pack(&persist_conn, &persist_msg);

	_agent_append(buffer);

	return SLURM_SUCCESS;
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(void *db_conn,
					   step_record_t *step_ptr)
{
	buf_t *buffer;
	persist_msg_t persist_msg = {0};
	dbd_step_comp_msg_t req = {0};

	if (as_build_step_comp_msg(&req, step_ptr))
		return SLURM_ERROR;

	persist_msg.msg_type = DBD_STEP_COMPLETE;
	persist_msg.data = &req;

	buffer = slurm_persist_msg_pack(&persist_conn, &persist_msg);

	_agent_append(buffer);

	return SLURM_SUCCESS;
}

/*
 * load into the storage a suspension of a job
 */
extern int jobacct_storage_p_suspend(void *db_conn, job_record_t *job_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * get info from the storage
 * returns list of job_rec_t *
 * note list needs to be freed when called
 */
extern list_t *jobacct_storage_p_get_jobs_cond(void *db_conn, uid_t uid,
					       slurmdb_job_cond_t *job_cond)
{
	return NULL;
}

/*
 * Expire old info from the storage
 * Not applicable for any database
 */
extern int jobacct_storage_p_archive(void *db_conn,
				     slurmdb_archive_cond_t *arch_cond)
{
	return SLURM_SUCCESS;
}

/*
 * load old info into the storage
 */
extern int jobacct_storage_p_archive_load(void *db_conn,
					  slurmdb_archive_rec_t *arch_rec)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_update_shares_used(void *db_conn,
					     list_t *shares_used)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(void *db_conn,
						time_t event_time)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_reconfig(void *db_conn, bool dbd)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_reset_lft_rgt(void *db_conn, uid_t uid,
					list_t *cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_get_stats(void *db_conn, slurmdb_stats_rec_t **stats)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_clear_stats(void *db_conn)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_get_data(void *db_conn, acct_storage_info_t dinfo,
				   void *data)
{
	return SLURM_SUCCESS;
}

extern void acct_storage_p_send_all(void *db_conn, time_t event_time,
				    slurm_msg_type_t msg_type)
{
}

extern int acct_storage_p_shutdown(void *db_conn)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_relay_msg(void *db_conn, persist_msg_t *msg)
{
	return SLURM_SUCCESS;
}
