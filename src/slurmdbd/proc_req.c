/*****************************************************************************\
 *  proc_req.c - functions for processing incoming RPCs.
 *****************************************************************************
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, Danny Auble <da@llnl.gov>
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

#include "config.h"

#include <signal.h>

#if HAVE_SYS_PRCTL_H
  #include <sys/prctl.h>
#endif

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/persist_conn.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/conn.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/jobacct_gather.h"

#include "src/slurmctld/slurmctld.h"

#include "src/slurmdbd/proc_req.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"
#include "src/slurmdbd/slurmdbd.h"

/* Local functions */
static bool _validate_slurm_user(slurmdbd_conn_t *dbd_conn);
static bool _validate_super_user(slurmdbd_conn_t *dbd_conn);
static bool _validate_operator(slurmdbd_conn_t *dbd_conn);
static int   _find_rpc_obj_in_list(void *x, void *key);
static void _process_job_start(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg,
			       dbd_job_start_msg_t *job_start_msg,
			       dbd_id_rc_msg_t *id_rc_msg);

extern void slurmdbd_conn_init(slurmdbd_conn_t *dbd_conn)
{
	*dbd_conn = (slurmdbd_conn_t) SLURMDBD_CONN_INITIALIZER;
}

extern void slurmdbd_conn_members_destroy(slurmdbd_conn_t *dbd_conn)
{
	if (!dbd_conn)
		return;

	xfree(dbd_conn->tres_str);
}

static char *_internal_rc_to_str(uint32_t rc, slurm_msg_t *msg, bool new_line)
{
	char *comment = NULL;

	if (rc == SLURM_NO_CHANGE_IN_DATA) {
		if (new_line)
			comment = "Request didn't affect anything\n";
		else
			comment = "Request didn't affect anything";
		debug2("[%s] %s", conmgr_con_get_name(msg->conmgr_con),
		       comment);
		return comment;
	}

	/*
	 * Clients of _add_accounts_cond() and _add_users_cond() expect the
	 * comment to have '\n' at the end of the strings. Since these are
	 * string literals, we have the new_line condition to handle these in
	 * one location.
	 */
	if (new_line) {
		if (rc == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action\n";
		} else if (rc == SLURM_ERROR) {
			comment = "Something was wrong with your query\n";
		} else if (rc == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(rc);
		} else if (rc == ESLURM_QOS_PREEMPTION_LOOP) {
			comment = "QOS Preemption loop detected\n";
		} else {
			if (!(comment = slurm_strerror(rc)))
				comment = "Unknown issue\n";
		}
	} else {
		if (rc == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
		} else if (rc == SLURM_ERROR) {
			comment = "Something was wrong with your query";
		} else if (rc == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(rc);
		} else if (rc == ESLURM_QOS_PREEMPTION_LOOP) {
			comment = "QOS Preemption loop detected";
		} else {
			if (!(comment = slurm_strerror(rc)))
				comment = "Unknown issue";
		}
	}
	error("[%s] %s", conmgr_con_get_name(msg->conmgr_con), comment);

	return comment;
}

/*
 * _validate_slurm_user - validate that the uid is authorized to see
 *      privileged data (either user root or SlurmUser)
 */
static bool _validate_slurm_user(slurmdbd_conn_t *dbd_conn)
{
	return (dbd_conn->auth.level == AUTH_LEVEL_INTERNAL);
}

/*
 * _validate_super_user - validate that the uid is authorized at the
 *      root, SlurmUser, or AUTH_LEVEL_ADMIN level
 */
static bool _validate_super_user(slurmdbd_conn_t *dbd_conn)
{
	return (dbd_conn->auth.level >= AUTH_LEVEL_ADMIN);
}

/*
 * _validate_operator - validate that the uid is authorized at the
 *      root, SlurmUser, or AUTH_LEVEL_OPERATOR level
 */
static bool _validate_operator(slurmdbd_conn_t *dbd_conn)
{
	return (dbd_conn->auth.level >= AUTH_LEVEL_OPERATOR);
}

static void _add_registered_cluster(slurmdbd_conn_t *dbd_conn, slurm_msg_t *msg)
{
	list_itr_t *itr;
	persist_conn_ctx_t *pcon_ctx;

	if (!dbd_conn->rem_port) {
		error("%s: trying to register a cluster (%s) with no remote port",
		      __func__, msg->pcon_ctx->cluster_name);
		return;
	}

	/* registered_clusters holds pcon_ctx, with slurmdbd_conn_t at arg */
	slurm_mutex_lock(&registered_lock);
	itr = list_iterator_create(registered_clusters);
	while ((pcon_ctx = list_next(itr))) {
		slurmdbd_conn_t *slurmdbd_conn = pcon_ctx->arg;

		xassert(pcon_ctx->magic == PERSIST_CONN_CTX_MAGIC);

		if (pcon_ctx == msg->pcon_ctx)
			break;

		if (!xstrcmp(pcon_ctx->cluster_name,
			     msg->pcon_ctx->cluster_name)) {
			error("A new registration for cluster %s [%s] just came in, but I am already talking to that cluster ([%s]), closing other connection.",
			      msg->pcon_ctx->cluster_name,
			      conmgr_con_get_name(msg->conmgr_con),
			      conmgr_con_get_name(pcon_ctx->ref));
			slurmdbd_conn->rem_port = 0;
			list_delete_item(itr);
		}
	}
	list_iterator_destroy(itr);
	if (!pcon_ctx) {
		slurm_mutex_init(&dbd_conn->pcon_send_lock);
		slurm_mutex_lock(&dbd_conn->pcon_send_lock);
		dbd_conn->pcon_send = xmalloc(sizeof(persist_conn_t));
		dbd_conn->pcon_send->cluster_name =
			xstrdup(msg->pcon_ctx->cluster_name);
		dbd_conn->pcon_send->persist_type = PERSIST_TYPE_ACCT_UPDATE;
		dbd_conn->pcon_send->my_port = slurmdbd_conf->dbd_port;
		dbd_conn->pcon_send->rem_host =
			xstrdup(msg->pcon_ctx->rem_host);
		dbd_conn->pcon_send->rem_port = dbd_conn->rem_port;
		dbd_conn->pcon_send->version = msg->protocol_version;
		dbd_conn->pcon_send->shutdown = &shutdown_time;
		/* we want timeout to be zero */
		dbd_conn->pcon_send->timeout = 0;
		dbd_conn->pcon_send->r_uid = SLURM_AUTH_UID_ANY;
		dbd_conn->pcon_send->flags |= PERSIST_FLAG_RECONNECT;
		slurm_mutex_unlock(&dbd_conn->pcon_send_lock);
		/*
		 * We can't open a pipe back to the slurmctld right
		 * now, the slurmctld might just be starting up and the
		 * rpc_mgr might not be listening yet, so we will handle
		 * this in the mysql plugin on the first commit.
		 */
		/* if (slurm_persist_conn_open(dbd_conn->pcon_send) != */
		/*     SLURM_SUCCESS) { */
		/* 	error("persist_conn_send: Unable to open connection to cluster %s who is actively talking to us.", */
		/* 	      msg->pcon_ctx->cluster_name); */
		/* } */

		list_append(registered_clusters, msg->pcon_ctx);
	}
	slurm_mutex_unlock(&registered_lock);
}


/* replace \" with \` return is the same as what is given */
static char * _replace_double_quotes(char *option)
{
	int i=0;

	if (!option)
		return NULL;

	while (option[i]) {
		if (option[i] == '\"')
			option[i] = '`';
		i++;
	}
	return option;
}

static int _handle_init_msg(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg,
			    persist_init_req_msg_t *init_msg)
{
#if HAVE_SYS_PRCTL_H
	{
	char *name = xstrdup_printf("p-%s", init_msg->cluster_name);
	if (prctl(PR_SET_NAME, name, NULL, NULL, NULL) < 0)
		error("%s: cannot set my name to %s %m", __func__, name);
	xfree(name);
	}
#endif

	debug("REQUEST_PERSIST_INIT: CLUSTER:%s VERSION:%u UID:%u IP:%s [%s]",
	      init_msg->cluster_name, init_msg->version,
	      slurmdbd_conn->auth.uid, msg->pcon_ctx->rem_host,
	      conmgr_con_get_name(msg->conmgr_con));

	/* When dealing with rollbacks it turns out it is much faster
	   to do the commit once or once in a while instead of
	   autocommit.  The SlurmDBD will periodically do a commit to
	   avoid such a slow down.
	*/
	slurmdbd_conn->db_conn =
		acct_storage_g_get_connection(msg->pcon_ctx->fd, NULL, true,
					      msg->pcon_ctx->cluster_name);
	if (errno)
		return errno;

	return acct_storage_g_auth_connection(slurmdbd_conn->db_conn,
					      &slurmdbd_conn->auth);
}

static int _unpack_persist_init(slurmdbd_conn_t *slurmdbd_conn,
				slurm_msg_t *msg)
{
	persist_init_req_msg_t *req_msg = msg->data;

	req_msg->uid = slurmdbd_conn->auth.uid;

	/* persist_conn replies. */
	return _handle_init_msg(slurmdbd_conn, msg, req_msg);
}

static int _add_accounts(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_accts(slurmdbd_conn->db_conn, get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = _internal_rc_to_str(rc, msg, false);
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_accounts_cond(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *modify_msg = msg->data;
	char *comment = NULL;
	bool free_comment = true;

	/*
	 * All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if (!(comment = acct_storage_g_add_accts_cond(slurmdbd_conn->db_conn,
						      modify_msg->cond,
						      modify_msg->rec))) {
		free_comment = false;
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, true);
	} else
		rc = errno;

	slurm_send_rc_err_msg(msg, rc, comment);
	if (free_comment)
		xfree(comment);
	return rc;
}

static int _fix_runaway_jobs(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!_validate_operator(slurmdbd_conn))
		rc = ESLURM_ACCESS_DENIED;
	else
		rc = acct_storage_g_fix_runaway_jobs(slurmdbd_conn->db_conn,
						     get_msg->my_list);

	if (rc == ESLURM_ACCESS_DENIED) {
		comment = "You must have an AdminLevel>=Operator to fix runaway jobs";
		error("[%s] %s",
		      conmgr_con_get_name(msg->conmgr_con),
		      comment);
	}

	slurm_send_rc_err_msg(msg, rc, comment);

	return rc;
}

static int _add_account_coords(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_acct_coord_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_coord(slurmdbd_conn->db_conn,
				      get_msg->acct_list, get_msg->cond);

	if (rc == ESLURM_ACCESS_DENIED)
		comment = _internal_rc_to_str(rc, msg, false);

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_tres(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_tres(slurmdbd_conn->db_conn, get_msg->my_list);

	slurm_send_rc_err_msg(msg, rc, comment);

	/* This happens before the slurmctld registers and only when
	   the slurmctld starts up.  So always commit, success or not.
	   (don't ever use autocommit with innodb)
	*/
	acct_storage_g_commit(slurmdbd_conn->db_conn, 1);

	return rc;
}

static int _add_assocs(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!_validate_operator(slurmdbd_conn)) {
		list_itr_t *itr = NULL;
		list_itr_t *itr2 = NULL;
		slurmdb_user_rec_t user;
		slurmdb_coord_rec_t *coord = NULL;
		slurmdb_assoc_rec_t *object = NULL;

		memset(&user, 0, sizeof(slurmdb_user_rec_t));
		user.uid = slurmdbd_conn->auth.uid;
		if (assoc_mgr_fill_in_user(
			    slurmdbd_conn->db_conn, &user, 1, NULL, false)
		    != SLURM_SUCCESS) {
			comment = "Your user has not been added to the accounting system yet.";
			error("[%s] %s",
			      conmgr_con_get_name(msg->conmgr_con),
			      comment);
			rc = SLURM_ERROR;
			goto end_it;
		}
		if (!user.coord_accts || !list_count(user.coord_accts)) {
			rc = ESLURM_ACCESS_DENIED;
			comment = _internal_rc_to_str(rc, msg, false);
			goto end_it;
		}
		itr = list_iterator_create(get_msg->my_list);
		itr2 = list_iterator_create(user.coord_accts);
		while ((object = list_next(itr))) {
			char *account = "root";
			if (object->user)
				account = object->acct;
			else if (object->parent_acct)
				account = object->parent_acct;
			list_iterator_reset(itr2);
			while ((coord = list_next(itr2))) {
				if (!xstrcasecmp(coord->name, account))
					break;
			}
			if (!coord)
				break;
		}
		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);
		if (!coord) {
			rc = ESLURM_ACCESS_DENIED;
			comment = _internal_rc_to_str(rc, msg, false);
			goto end_it;
		}
	}

	rc = acct_storage_g_add_assocs(slurmdbd_conn->db_conn,
				       get_msg->my_list);
end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_clusters(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_clusters(slurmdbd_conn->db_conn,
					 get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = _internal_rc_to_str(rc, msg, false);
	else if (rc != SLURM_SUCCESS)
		comment = "Failed to add cluster.";

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_federations(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_federations(slurmdbd_conn->db_conn,
					    get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = _internal_rc_to_str(rc, msg, false);
	else if (rc != SLURM_SUCCESS)
		comment = "Failed to add cluster.";

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_qos(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_qos(slurmdbd_conn->db_conn, get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = _internal_rc_to_str(rc, msg, false);
	else if (rc != SLURM_SUCCESS)
		comment = "Failed to add qos.";

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_res(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_res(slurmdbd_conn->db_conn, get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = _internal_rc_to_str(rc, msg, false);
	else if (rc != SLURM_SUCCESS)
		comment = "Failed to add system resource.";

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_users(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_users(slurmdbd_conn->db_conn, get_msg->my_list);

	if (rc == ESLURM_ACCESS_DENIED)
		comment = _internal_rc_to_str(rc, msg, false);

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_users_cond(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *modify_msg = msg->data;
	char *comment = NULL;
	bool free_comment = true;

	/*
	 * All authentication needs to be done inside the plugin since we are
	 * unable to know what users this request is talking about
	 * until we process it through the database.
	 */

	if (!(comment = acct_storage_g_add_users_cond(slurmdbd_conn->db_conn,
						      modify_msg->cond,
						      modify_msg->rec))) {
		free_comment = false;
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, true);
	} else
		rc = errno;

	slurm_send_rc_err_msg(msg, rc, comment);
	if (free_comment)
		xfree(comment);
	return rc;
}

static int _add_wckeys(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_wckeys(slurmdbd_conn->db_conn,
				       get_msg->my_list);

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _add_reservation(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_add_reservation(slurmdbd_conn->db_conn,
					    rec_msg->rec);

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static void _apply_purge_default(uint32_t *purge, uint32_t conf_purge)
{
	if (*purge == NO_VAL)
		*purge = conf_purge;
	else if (!(*purge & SLURMDB_PURGE_BASE))
		/*
		 * When `sacctmgr archive dump` is given a bare keyword like
		 * "Jobs" but no duration, *purge has a value of 0 with
		 * SLURMDB_PURGE_ARCHIVE set. Use the configured default in that
		 * case too.
		 */
		*purge |= conf_purge;
}

static int _archive_dump(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	char *comment = "SUCCESS";
	slurmdb_archive_cond_t *arch_cond = NULL;

	if (!_validate_super_user(slurmdbd_conn)) {
		rc = ESLURM_ACCESS_DENIED;
		comment = _internal_rc_to_str(rc, msg, false);
		goto end_it;
	}

	if (slurmdbd_conf->flags & DBD_CONF_FLAG_DISABLE_ARCHIVE_COMMANDS) {
		rc = ESLURM_DISABLED;
		comment = "Dumping archived data is disabled";
		error("Request to dump archived data, but DisableArchiveCommands is set");
		goto end_it;
	}

	arch_cond = (slurmdb_archive_cond_t *)get_msg->cond;
	/* set up some defaults */
	if (!arch_cond->archive_dir)
		arch_cond->archive_dir = xstrdup(slurmdbd_conf->archive_dir);
	if (!arch_cond->archive_script)
		arch_cond->archive_script =
			xstrdup(slurmdbd_conf->archive_script);

	/*
	 * Use purge durations from slurmdbd.conf when the RPC from sacctmgr
	 * didn't specify them.
	 */
	_apply_purge_default(&arch_cond->purge_event,
			     slurmdbd_conf->purge_event);
	_apply_purge_default(&arch_cond->purge_job, slurmdbd_conf->purge_job);
	_apply_purge_default(&arch_cond->purge_resv, slurmdbd_conf->purge_resv);
	_apply_purge_default(&arch_cond->purge_step, slurmdbd_conf->purge_step);
	_apply_purge_default(&arch_cond->purge_suspend,
			     slurmdbd_conf->purge_suspend);
	_apply_purge_default(&arch_cond->purge_txn, slurmdbd_conf->purge_txn);
	_apply_purge_default(&arch_cond->purge_usage,
			     slurmdbd_conf->purge_usage);
	_apply_purge_default(&arch_cond->purge_jobscript,
			     slurmdbd_conf->purge_jobscript);
	_apply_purge_default(&arch_cond->purge_jobenv,
			     slurmdbd_conf->purge_jobenv);

	rc = jobacct_storage_g_archive(slurmdbd_conn->db_conn, arch_cond);
	if (rc != SLURM_SUCCESS) {
		if (errno == EACCES)
			comment = "Problem accessing file.";
		else
			comment = "Error with request.";
	}
end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _archive_load(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	slurmdb_archive_rec_t *arch_rec = msg->data;
	char *comment = "SUCCESS";

	if (!_validate_super_user(slurmdbd_conn)) {
		rc = ESLURM_ACCESS_DENIED;
		comment = _internal_rc_to_str(rc, msg, false);
		goto end_it;
	}

	if (slurmdbd_conf->flags & DBD_CONF_FLAG_DISABLE_ARCHIVE_COMMANDS) {
		rc = ESLURM_DISABLED;
		comment = "Loading archived data is disabled";
		error("Request to load archived data, but DisableArchiveCommands is set");
		goto end_it;
	}

	rc = jobacct_storage_g_archive_load(slurmdbd_conn->db_conn, arch_rec);

	if (rc == ENOENT)
		comment = "No archive file given to recover.";
	else if (rc != SLURM_SUCCESS)
		comment = "Error with request.";

end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _cluster_tres(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cluster_tres_msg_t *cluster_tres_msg = msg->data;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	debug2("DBD_CLUSTER_TRES: called in [%s] for %s(%s)",
	       conmgr_con_get_name(msg->conmgr_con),
	       msg->pcon_ctx->cluster_name, cluster_tres_msg->tres_str);

	rc = clusteracct_storage_g_cluster_tres(slurmdbd_conn->db_conn,
						cluster_tres_msg->cluster_nodes,
						cluster_tres_msg->tres_str,
						cluster_tres_msg->event_time,
						msg->protocol_version);
	if (rc == ESLURM_ACCESS_DENIED) {
		comment = _internal_rc_to_str(rc, msg, false);
		rc = SLURM_ERROR;
	}

	if (rc == SLURM_SUCCESS) {
		xfree(slurmdbd_conn->tres_str);
		slurmdbd_conn->tres_str = cluster_tres_msg->tres_str;
		cluster_tres_msg->tres_str = NULL;
	}
	if (!slurmdbd_conn->rem_port) {
		debug3("DBD_CLUSTER_TRES: cluster not registered");
		slurmdbd_conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				msg->pcon_ctx->rem_host);

		_add_registered_cluster(slurmdbd_conn, msg);
	}

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _get_accounts(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list =
		acct_storage_g_get_accts(slurmdbd_conn->db_conn, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_ACCOUNTS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_tres(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list =
		acct_storage_g_get_tres(slurmdbd_conn->db_conn, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_TRES, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_assocs(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list = acct_storage_g_get_assocs(slurmdbd_conn->db_conn,
						     get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_ASSOCS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_clusters(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list = acct_storage_g_get_clusters(slurmdbd_conn->db_conn,
						       get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_CLUSTERS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_federations(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list =
		acct_storage_g_get_federations(slurmdbd_conn->db_conn,
					       get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_FEDERATIONS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_config_keypair(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };

	list_msg.my_list = slurmdb_config_get_keypairs(slurmdbd_conf);

	(void) send_msg_response(msg, DBD_GOT_CONFIG_KEYPAIRS, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return SLURM_SUCCESS;
}

static int _get_config(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = EINVAL;

	if (msg->protocol_version < SLURM_26_05_PROTOCOL_VERSION) {
		rc = _get_config_keypair(slurmdbd_conn, msg);
	} else {
		rc = send_msg_response(msg, DBD_GOT_CONFIG, slurmdbd_conf);
	}

	return rc;
}

static int _get_events(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list = acct_storage_g_get_events(slurmdbd_conn->db_conn,
						     get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_EVENTS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_instances(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list = acct_storage_g_get_instances(slurmdbd_conn->db_conn,
							get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_INSTANCES, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_jobs_cond(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *cond_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	slurmdb_job_cond_t *job_cond = cond_msg->cond;
	int rc = SLURM_SUCCESS;

	/* fail early if requesting runaways and not super user */
	if ((job_cond->flags & JOBCOND_FLAG_RUNAWAY) &&
	    !_validate_operator(slurmdbd_conn)) {
		debug("Rejecting query of runaways from uid %u",
		      slurmdbd_conn->auth.uid);
		slurm_send_rc_err_msg(
			msg, ESLURM_ACCESS_DENIED,
			"You must have an AdminLevel>=Operator to fix runaway jobs");
		return SLURM_ERROR;
	}
	/* fail early if too wide a query */
	if (!job_cond->step_list && !_validate_operator(slurmdbd_conn) &&
	    (slurmdbd_conf->max_time_range != INFINITE)) {
		time_t start, end;

		start = job_cond->usage_start;

		if (job_cond->usage_end)
			end = job_cond->usage_end;
		else
			end = time(NULL);

		if ((end - start) > slurmdbd_conf->max_time_range) {
			info("Rejecting query > MaxQueryTimeRange from uid %u",
			     slurmdbd_conn->auth.uid);
			slurm_send_rc_err_msg(
				msg, ESLURM_DB_QUERY_TOO_WIDE,
				slurm_strerror(ESLURM_DB_QUERY_TOO_WIDE));
			return SLURM_ERROR;
		}
	}

	list_msg.my_list =
		jobacct_storage_g_get_jobs_cond(slurmdbd_conn->db_conn,
						job_cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		if (send_msg_response(msg, DBD_GOT_JOBS, &list_msg) ==
		    ESLURM_RESULT_TOO_LARGE) {
			slurm_send_rc_err_msg(
				msg, ESLURM_RESULT_TOO_LARGE,
				slurm_strerror(ESLURM_RESULT_TOO_LARGE));
			rc = SLURM_ERROR;
		}
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_probs(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list = acct_storage_g_get_problems(slurmdbd_conn->db_conn,
						       get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_PROBS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_qos(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *cond_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list =
		acct_storage_g_get_qos(slurmdbd_conn->db_conn, cond_msg->cond);

	if (errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_QOS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_res(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list =
		acct_storage_g_get_res(slurmdbd_conn->db_conn, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_RES, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);
	return rc;
}

static int _get_txn(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *cond_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list =
		acct_storage_g_get_txn(slurmdbd_conn->db_conn, cond_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_TXN, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_usage(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_usage_msg_t *get_msg = msg->data;
	dbd_usage_msg_t got_msg;
	uint16_t ret_type = 0;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	info("DBD_GET_USAGE: called in [%s]. Type is %s",
	     conmgr_con_get_name(msg->conmgr_con),
	     slurmdbd_msg_type_2_str(msg->msg_type, 1));

	switch(msg->msg_type) {
	case DBD_GET_ASSOC_USAGE:
		ret_type = DBD_GOT_ASSOC_USAGE;
		break;
	case DBD_GET_QOS_USAGE:
		ret_type = DBD_GOT_QOS_USAGE;
		break;
	case DBD_GET_WCKEY_USAGE:
		ret_type = DBD_GOT_WCKEY_USAGE;
		break;
	case DBD_GET_CLUSTER_USAGE:
		ret_type = DBD_GOT_CLUSTER_USAGE;
		break;
	default:
		comment = "Unknown type of usage to get";
		error("%s %u", comment, msg->msg_type);
		slurm_send_rc_err_msg(msg, SLURM_ERROR, comment);
		return SLURM_ERROR;
	}

	rc = acct_storage_g_get_usage(slurmdbd_conn->db_conn, get_msg->rec,
				      msg->msg_type, get_msg->start,
				      get_msg->end);

	if (rc != SLURM_SUCCESS) {
		comment = "Problem getting usage info";
		error("[%s] %s",
		      conmgr_con_get_name(msg->conmgr_con), comment);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;

	}
	memset(&got_msg, 0, sizeof(dbd_usage_msg_t));
	got_msg.rec = get_msg->rec;
	get_msg->rec = NULL;
	(void) send_msg_response(msg, ret_type, &got_msg);

	return SLURM_SUCCESS;
}

static int _get_users(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	slurmdb_user_cond_t * user_cond = NULL;

	user_cond = get_msg->cond;
	if ((!user_cond->with_assocs && !user_cond->with_wckeys) &&
	    ((msg->protocol_version < 8) ||
	     (user_cond->assoc_cond->flags & ASSOC_COND_FLAG_ONLY_DEFS))) {
		list_t *cluster_list = user_cond->assoc_cond->cluster_list;
		/* load up with just this cluster to query against
		 * since before 2.2 we had only 1 default account so
		 * send the default for this cluster. */
		if (!cluster_list) {
			cluster_list = list_create(NULL);
			list_append(cluster_list, msg->pcon_ctx->cluster_name);
			user_cond->assoc_cond->cluster_list = cluster_list;
		}
	}

	list_msg.my_list =
		acct_storage_g_get_users(slurmdbd_conn->db_conn, user_cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_USERS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_wckeys(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;
	int rc = SLURM_SUCCESS;

	/* We have to check this here, and not in the plugin.  There
	 * are places in the plugin that a non-admin can call this and
	 * it be ok. */
	if (!_validate_operator(slurmdbd_conn)) {
		int rc = ESLURM_ACCESS_DENIED;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	list_msg.my_list = acct_storage_g_get_wckeys(slurmdbd_conn->db_conn,
						     get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_WCKEYS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_reservations(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	list_msg.my_list =
		acct_storage_g_get_reservations(slurmdbd_conn->db_conn,
						get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		(void) send_msg_response(msg, DBD_GOT_RESVS, &list_msg);
	} else {
		slurm_send_rc_err_msg(msg, errno, slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _find_rpc_obj_in_list(void *x, void *key)
{
	slurmdb_rpc_obj_t *obj = (slurmdb_rpc_obj_t *)x;
	uint32_t *id = key;

	if (obj->id == *id)
		return 1;
	return 0;
}

static int _flush_jobs(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_cluster_tres_msg_t *cluster_tres_msg = msg->data;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	debug2("DBD_FLUSH_JOBS: called in [%s] for %s",
	       conmgr_con_get_name(msg->conmgr_con),
	       msg->pcon_ctx->cluster_name);

	rc = acct_storage_g_flush_jobs_on_cluster(
		slurmdbd_conn->db_conn,
		cluster_tres_msg->event_time);

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _fini_conn(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_fini_msg_t *fini_msg = msg->data;
	char *comment = NULL;
	int rc = SLURM_SUCCESS;
	bool locked = false;


	debug2("DBD_FINI: CLOSE:%u COMMIT:%u",
	       fini_msg->close_conn, fini_msg->commit);

	if (slurmdbd_conn->rem_port && slurmdbd_conf->commit_delay) {
		slurm_mutex_lock(&registered_lock);
		locked = true;
	}

	if (fini_msg->close_conn == 1)
		rc = acct_storage_g_close_connection(&slurmdbd_conn->db_conn);
	else
		rc = acct_storage_g_commit(slurmdbd_conn->db_conn,
					   fini_msg->commit);
	if (locked)
		slurm_mutex_unlock(&registered_lock);

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;

}

static int _job_complete(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_job_comp_msg_t *job_comp_msg = msg->data;
	job_record_t job;
	job_details_t details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	memset(&job, 0, sizeof(job_record_t));
	memset(&details, 0, sizeof(job_details_t));

	job.admin_comment = job_comp_msg->admin_comment;
	job.assoc_id = job_comp_msg->assoc_id;
	job.comment = job_comp_msg->comment;
	if (job_comp_msg->db_index != NO_VAL64)
		job.db_index = job_comp_msg->db_index;
	job.derived_ec = job_comp_msg->derived_ec;
	job.end_time = job_comp_msg->end_time;
	job.exit_code = job_comp_msg->exit_code;
	job.extra = job_comp_msg->extra;
	job.failed_node = job_comp_msg->failed_node;
	job.job_id = job_comp_msg->job_id;
	job.job_state = job_comp_msg->job_state;
	job.requid = job_comp_msg->req_uid;
	job.nodes = job_comp_msg->nodes;
	job.start_time = job_comp_msg->start_time;
	details.submit_time = job_comp_msg->submit_time;
	job.start_protocol_ver = msg->protocol_version;
	job.system_comment = job_comp_msg->system_comment;
	job.tres_alloc_str = job_comp_msg->tres_alloc_str;

	job.details = &details;

	if (job.job_state & JOB_RESIZING) {
		job.resize_time = job_comp_msg->end_time;
		debug2("DBD_JOB_COMPLETE: RESIZE ID:%u", job_comp_msg->job_id);
	} else
		debug2("DBD_JOB_COMPLETE: ID:%u", job_comp_msg->job_id);

	rc = jobacct_storage_g_job_complete(slurmdbd_conn->db_conn, &job);

	if (rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just in case this gets set we need to clear it */
	xfree(job.wckey);

	if (!slurmdbd_conn->rem_port) {
		debug3("DBD_JOB_COMPLETE: cluster not registered");
		slurmdbd_conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				msg->pcon_ctx->rem_host);

		_add_registered_cluster(slurmdbd_conn, msg);
	}

	slurm_send_rc_err_msg(msg, rc, comment);
	return SLURM_SUCCESS;
}

static int _job_start(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_job_start_msg_t *job_start_msg = msg->data;
	dbd_id_rc_msg_t id_rc_msg;
	char *comment = NULL;

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_JOB_START message from invalid uid";
		error("[%s] %s %u",
		      conmgr_con_get_name(msg->conmgr_con), comment,
		      slurmdbd_conn->auth.uid);
		slurm_send_rc_err_msg(msg, ESLURM_ACCESS_DENIED, comment);
		return SLURM_ERROR;
	}

	_process_job_start(slurmdbd_conn, msg, job_start_msg, &id_rc_msg);

	(void) send_msg_response(msg, DBD_ID_RC, &id_rc_msg);
	return SLURM_SUCCESS;
}

static int _job_heavy(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_job_heavy_msg_t *job_heavy_msg = msg->data;
	job_record_t job;
	job_details_t details;
	char *comment = NULL;
	int rc;

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_JOB_HEAVY message from invalid uid";
		error("[%s] %s %u",
		      conmgr_con_get_name(msg->conmgr_con), comment,
		      slurmdbd_conn->auth.uid);
		slurm_send_rc_err_msg(msg, ESLURM_ACCESS_DENIED, comment);
		return SLURM_ERROR;
	}

	debug2("DBD_JOB_HEAVY: SCRIPT:%s ENV:%s",
	       job_heavy_msg->script ? "yes" : "no",
	       job_heavy_msg->env ? "yes" : "no");

	memset(&job, 0, sizeof(job_record_t));
	memset(&details, 0, sizeof(job_details_t));

	if (job_heavy_msg->env) {
		details.env_sup = xmalloc(sizeof(*details.env_sup));
		details.env_sup[0] = job_heavy_msg->env;
	}
	details.env_hash = job_heavy_msg->env_hash;
	details.script = job_heavy_msg->script;
	details.script_hash = job_heavy_msg->script_hash;

	job.details = &details;

	rc = jobacct_storage_g_job_heavy(slurmdbd_conn->db_conn, &job);

	xfree(details.env_sup);

	slurm_send_rc_err_msg(msg, rc, comment);
	return SLURM_SUCCESS;
}

static int _job_suspend(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_job_suspend_msg_t *job_suspend_msg = msg->data;
	job_record_t job;
	job_details_t details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_JOB_SUSPEND message from invalid uid";
		error("[%s] %s %u",
		      conmgr_con_get_name(msg->conmgr_con), comment,
		      slurmdbd_conn->auth.uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_JOB_SUSPEND: ID:%u STATE:%s",
	       job_suspend_msg->job_id,
	       job_state_string(job_suspend_msg->job_state));

	memset(&job, 0, sizeof(job_record_t));
	memset(&details, 0, sizeof(job_details_t));

	job.assoc_id = job_suspend_msg->assoc_id;
	if (job_suspend_msg->db_index != NO_VAL64)
		job.db_index = job_suspend_msg->db_index;
	job.job_id = job_suspend_msg->job_id;
	job.job_state = job_suspend_msg->job_state;
	details.submit_time = job_suspend_msg->submit_time;
	job.start_protocol_ver = msg->protocol_version;
	job.suspend_time = job_suspend_msg->suspend_time;

	job.details = &details;
	rc = jobacct_storage_g_job_suspend(slurmdbd_conn->db_conn, &job);

	if (rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just in case this gets set we need to clear it */
	xfree(job.wckey);
end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return SLURM_SUCCESS;
}

static int _modify_accounts(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_modify_accts(slurmdbd_conn->db_conn,
						  get_msg->cond,
						  get_msg->rec))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_assocs(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if (!(list_msg.my_list =
		      acct_storage_g_modify_assocs(slurmdbd_conn->db_conn,
						   get_msg->cond,
						   get_msg->rec)) ||
	    (errno != SLURM_SUCCESS)) {
		rc = errno;
		if (list_msg.my_list && list_count(list_msg.my_list) &&
		    (errno == ESLURM_NO_REMOVE_DEFAULT_QOS)) {
			comment = list_peek(list_msg.my_list);
		} else {
			comment = _internal_rc_to_str(rc, msg, false);
		}

		slurm_send_rc_err_msg(msg, rc, comment);
		FREE_NULL_LIST(list_msg.my_list);
		return rc;
	}

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_clusters(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_modify_clusters(slurmdbd_conn->db_conn,
						     get_msg->cond,
						     get_msg->rec))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_federations(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_modify_federations(slurmdbd_conn->db_conn,
							get_msg->cond,
							get_msg->rec))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_job(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_modify_job(slurmdbd_conn->db_conn,
						get_msg->cond, get_msg->rec))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	if (get_msg->cond &&
	    (((slurmdb_job_cond_t *)get_msg->cond)->flags &
	     JOBCOND_FLAG_NO_WAIT)) {
		slurm_send_rc_err_msg(msg, rc, comment);
	} else {
		(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_qos(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_modify_qos(slurmdbd_conn->db_conn,
						get_msg->cond, get_msg->rec))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_res(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_modify_res(slurmdbd_conn->db_conn,
						get_msg->cond, get_msg->rec))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);
	return rc;
}

static int _modify_users(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;
	int same_user = 0;
	slurmdb_user_cond_t *user_cond = NULL;
	slurmdb_user_rec_t *user_rec = NULL;

	user_cond = (slurmdb_user_cond_t *)get_msg->cond;
	user_rec = (slurmdb_user_rec_t *)get_msg->rec;

	if (!_validate_operator(slurmdbd_conn)) {
		int rc = ESLURM_ACCESS_DENIED;
		if (user_cond && user_cond->assoc_cond
		    && user_cond->assoc_cond->user_list
		    && (list_count(user_cond->assoc_cond->user_list) == 1)) {
			uid_t pw_uid;
			char *name;
			name = list_peek (user_cond->assoc_cond->user_list);
			if ((uid_from_string(name, &pw_uid) == SLURM_SUCCESS) &&
			    (pw_uid == slurmdbd_conn->auth.uid)) {
				same_user = 1;
				goto is_same_user;
			}
		}
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);

		return rc;
	}

is_same_user:

	/* same_user can only alter the default account, default wckey
	 * nothing else */
	if (same_user) {
		/* If we add anything else here for the user we will
		 * need to document it
		 */
		if ((user_rec->admin_level != AUTH_LEVEL_NOTSET)) {
			comment = "You can only change your own default account, default wckey nothing else";
			error("[%s] %s",
			      conmgr_con_get_name(msg->conmgr_con),
			      comment);
			slurm_send_rc_err_msg(msg, ESLURM_ACCESS_DENIED,
					      comment);

			return ESLURM_ACCESS_DENIED;
		}
	}

	if ((user_rec->admin_level != AUTH_LEVEL_NOTSET) &&
	    !_validate_super_user(slurmdbd_conn)) {
		comment = "You must be a super user to modify a users admin level";
		error("[%s] %s",
		      conmgr_con_get_name(msg->conmgr_con), comment);
		slurm_send_rc_err_msg(msg, ESLURM_ACCESS_DENIED, comment);
		return ESLURM_ACCESS_DENIED;
	}

	if (!(list_msg.my_list =
		      acct_storage_g_modify_users(slurmdbd_conn->db_conn,
						  user_cond, user_rec))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_wckeys(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_modify_wckeys(slurmdbd_conn->db_conn,
						   get_msg->cond,
						   get_msg->rec))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_reservation(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = msg->data;
	char *comment = NULL;

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_MODIFY_RESV message from invalid uid";
		error("[%s] %s %u",
		      conmgr_con_get_name(msg->conmgr_con), comment,
		      slurmdbd_conn->auth.uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	rc = acct_storage_g_modify_reservation(slurmdbd_conn->db_conn,
					       rec_msg->rec);

end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _node_state(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_node_state_msg_t *node_state_msg = msg->data;
	node_record_t node_ptr;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_NODE_STATE message from invalid uid";
		error("[%s] %s %u",
		      conmgr_con_get_name(msg->conmgr_con),
		      comment, slurmdbd_conn->auth.uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	memset(&node_ptr, 0, sizeof(node_record_t));
	node_ptr.extra = node_state_msg->extra;
	node_ptr.instance_id = node_state_msg->instance_id;
	node_ptr.instance_type = node_state_msg->instance_type;
	node_ptr.name = node_state_msg->hostlist;
	node_ptr.tres_str = node_state_msg->tres_str;
	node_ptr.node_state = node_state_msg->state;
	node_ptr.reason = node_state_msg->reason;
	node_ptr.reason_time = node_state_msg->event_time;
	node_ptr.reason_uid = node_state_msg->reason_uid;

	if (!node_ptr.tres_str &&
	    (node_state_msg->new_state != DBD_NODE_STATE_UPDATE))
		node_state_msg->new_state = DBD_NODE_STATE_UP;

	switch (node_state_msg->new_state) {
	case DBD_NODE_STATE_UP:
		debug2("DBD_NODE_STATE_UP: NODE:%s REASON:%s TIME:%ld",
		       node_state_msg->hostlist,
		       node_state_msg->reason,
		       (long)node_state_msg->event_time);

		/* clusteracct_storage_g_node_up can change the reason
		 * field so copy it to avoid memory issues.
		 */
		node_ptr.reason = xstrdup(node_state_msg->reason);
		rc = clusteracct_storage_g_node_up(
			slurmdbd_conn->db_conn,
			&node_ptr,
			node_state_msg->event_time);
		xfree(node_ptr.reason);
		break;
	case DBD_NODE_STATE_DOWN:
		debug2("DBD_NODE_STATE_DOWN: NODE:%s STATE:%s REASON:%s UID:%u TIME:%ld",
		       node_state_msg->hostlist,
		       node_state_string(node_state_msg->state),
		       node_state_msg->reason,
		       node_ptr.reason_uid,
		       (long)node_state_msg->event_time);
		rc = clusteracct_storage_g_node_down(
			slurmdbd_conn->db_conn,
			&node_ptr,
			node_state_msg->event_time,
			node_state_msg->reason, node_ptr.reason_uid);
		break;
	case DBD_NODE_STATE_UPDATE:
		debug2("DBD_NODE_STATE_UPDATE: NODE:%s",
		       node_state_msg->hostlist);
		rc = clusteracct_storage_g_node_update(
			slurmdbd_conn->db_conn,
			&node_ptr);
		break;
	default:
		comment = "DBD_NODE_STATE message has invalid new_state";
		error("[%s] %s %u",
		      conmgr_con_get_name(msg->conmgr_con),
		      comment, slurmdbd_conn->auth.uid);
		rc = SLURM_ERROR;
		break;
	}

end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return SLURM_SUCCESS;
}

static void _process_job_start(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg,
			       dbd_job_start_msg_t *job_start_msg,
			       dbd_id_rc_msg_t *id_rc_msg)
{
	job_record_t job, *job_ptr;
	job_details_t details;
	job_array_struct_t array_recs;

	memset(&job, 0, sizeof(job_record_t));
	memset(&details, 0, sizeof(job_details_t));
	memset(&array_recs, 0, sizeof(job_array_struct_t));
	memset(id_rc_msg, 0, sizeof(dbd_id_rc_msg_t));

	job.total_nodes = job_start_msg->alloc_nodes;
	job.account = _replace_double_quotes(job_start_msg->account);
	job.array_job_id = job_start_msg->array_job_id;
	job.array_task_id = job_start_msg->array_task_id;
	array_recs.task_id_str = job_start_msg->array_task_str;
	array_recs.max_run_tasks = job_start_msg->array_max_tasks;
	array_recs.task_cnt = job_start_msg->array_task_pending;
	job.assoc_id = job_start_msg->assoc_id;
	job.db_index = job_start_msg->db_index;
	job.step_id.sluid = job_start_msg->sluid;
	details.begin_time = job_start_msg->eligible_time;
	details.env_hash = job_start_msg->env_hash;
	job.user_id = job_start_msg->uid;
	job.group_id = job_start_msg->gid;
	job.container = _replace_double_quotes(job_start_msg->container);
	job.het_job_id = job_start_msg->het_job_id;
	job.het_job_offset = job_start_msg->het_job_offset;
	job.job_id = job_start_msg->job_id;
	job.job_state = job_start_msg->job_state;
	job.licenses = _replace_double_quotes(job_start_msg->licenses);
	job.mcs_label = _replace_double_quotes(job_start_msg->mcs_label);
	job.name = _replace_double_quotes(job_start_msg->name);
	job.nodes = job_start_msg->nodes;
	job.network = job_start_msg->node_inx;
	job.partition = job_start_msg->partition;
	details.min_cpus = job_start_msg->req_cpus;
	details.pn_min_memory = job_start_msg->req_mem;
	job.qos_id = job_start_msg->qos_id;
	details.qos_req = job_start_msg->qos_req;
	details.resv_req = job_start_msg->resv_req;
	job.restart_cnt = job_start_msg->restart_cnt;
	job.resv_id = job_start_msg->resv_id;
	job.priority = job_start_msg->priority;
	details.script_hash = job_start_msg->script_hash;
	job.start_protocol_ver = msg->protocol_version;
	job.start_time = job_start_msg->start_time;
	details.segment_size = job_start_msg->segment_size;
	job.exclusive = job_exclusive_display_string(job_start_msg->exclusive);
	job.oversubscribe =
		job_oversubscribe_string(job_start_msg->oversubscribe);
	details.std_err = job_start_msg->std_err;
	details.std_in = job_start_msg->std_in;
	details.std_out = job_start_msg->std_out;
	details.submit_line = job_start_msg->submit_line;
	job.time_limit = job_start_msg->timelimit;
	job.tres_alloc_str = job_start_msg->tres_alloc_str;
	job.tres_req_str = job_start_msg->tres_req_str;
	job.gres_used = job_start_msg->gres_used;
	job.wckey = _replace_double_quotes(job_start_msg->wckey);
	details.work_dir = _replace_double_quotes(job_start_msg->work_dir);
	details.submit_time = job_start_msg->submit_time;
	job.db_flags = job_start_msg->db_flags;
	details.features = _replace_double_quotes(job_start_msg->constraints);
	job.state_reason_prev_db = job_start_msg->state_reason_prev;

	job.array_recs = &array_recs;
	job.details = &details;
	job_ptr = &job;

	if (job.job_state & JOB_RESIZING) {
		job.resize_time = job_start_msg->eligible_time;
		debug2("DBD_JOB_START: RESIZE CALL ID:%u NAME:%s INX:%"PRIu64,
		       job_start_msg->job_id, job_start_msg->name,
		       job.db_index);
	} else if (job.start_time && !IS_JOB_PENDING(job_ptr)) {
		debug2("DBD_JOB_START: START CALL ID:%u NAME:%s INX:%"PRIu64,
		       job_start_msg->job_id, job_start_msg->name,
		       job.db_index);
	} else {
		debug2("DBD_JOB_START: ELIGIBLE CALL ID:%u NAME:%s INX:%"PRIu64,
		       job_start_msg->job_id, job_start_msg->name,
		       job.db_index);
	}
	id_rc_msg->return_code = jobacct_storage_g_job_start(
		slurmdbd_conn->db_conn, &job);
	id_rc_msg->job_id = job.job_id;
	id_rc_msg->flags = job.bit_flags;
	id_rc_msg->db_index = job.db_index;

	/* just in case job.wckey was set because we didn't send one */
	if (!job_start_msg->wckey)
		xfree(job.wckey);

	xfree(details.env_sup);

	if (!slurmdbd_conn->rem_port) {
		debug3("DBD_JOB_START: cluster not registered");
		slurmdbd_conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				msg->pcon_ctx->rem_host);

		_add_registered_cluster(slurmdbd_conn, msg);
	}
}

static int _reconfig(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_super_user(slurmdbd_conn)) {
		int rc = ESLURM_ACCESS_DENIED;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);

		return rc;
	}

	info("Reconfigure request received");
	reconfig();

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;

}

static int _register_ctld(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_register_ctld_msg_t *register_ctld_msg = msg->data;
	int rc = SLURM_SUCCESS;
	uint32_t id_rc = 0;
	char *comment = NULL;
	slurmdb_cluster_cond_t cluster_q;
	slurmdb_cluster_rec_t cluster;
	dbd_list_msg_t list_msg = { NULL };
	list_t *cluster_list;
	const char *cname = conmgr_con_get_name(msg->conmgr_con);

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_REGISTER_CTLD message from invalid uid";
		error("[%s] %s %u", cname, comment, slurmdbd_conn->auth.uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_REGISTER_CTLD: called in [%s] for %s(%u), cluster_id=%u",
	       cname, msg->pcon_ctx->cluster_name, register_ctld_msg->port,
	       register_ctld_msg->cluster_id);

	/* Just to make sure we don't allow a NULL cluster name to attempt
	   to connect.  This should never happen, but here just for
	   sanity check.
	*/
	if (!msg->pcon_ctx->cluster_name) {
		comment = "Must have a cluster name to register it";
		error("[%s] %s", cname, comment);
		rc = ESLURM_BAD_NAME;
		goto end_it;
	}

	debug2("slurmctld at ip:%s, port:%d",
	       msg->pcon_ctx->rem_host, register_ctld_msg->port);

	slurmdb_init_cluster_cond(&cluster_q, 0);
	slurmdb_init_cluster_rec(&cluster, 0);

	cluster_q.cluster_list = list_create(NULL);
	list_append(cluster_q.cluster_list, msg->pcon_ctx->cluster_name);
	cluster.control_host = msg->pcon_ctx->rem_host;
	cluster.control_port = register_ctld_msg->port;
	cluster.dimensions = register_ctld_msg->dimensions;
	cluster.flags = register_ctld_msg->flags;
	cluster.rpc_version = msg->protocol_version;

	if ((cluster.flags != NO_VAL) && (cluster.flags & CLUSTER_FLAG_EXT))
		slurmdbd_conn->flags |= PERSIST_FLAG_EXT_DBD;

	cluster_list =
		acct_storage_g_get_clusters(slurmdbd_conn->db_conn, &cluster_q);
	if (!cluster_list || errno) {
		comment = slurm_strerror(errno);
		rc = errno;
	} else if (!list_count(cluster_list)) {
		list_t *add_list = list_create(NULL);
		list_append(add_list, &cluster);

		cluster.name = msg->pcon_ctx->cluster_name;
		cluster.flags |= CLUSTER_FLAG_REGISTER;
		cluster.id = register_ctld_msg->cluster_id;

		rc = acct_storage_g_add_clusters(slurmdbd_conn->db_conn,
						 add_list);
		if (rc == ESLURM_ACCESS_DENIED)
			comment = _internal_rc_to_str(rc, msg, false);

		else if (rc != SLURM_SUCCESS)
			comment = "Failed to add/register cluster.";
		slurmdb_destroy_assoc_rec(cluster.root_assoc);
		FREE_NULL_LIST(add_list);
	} else if ((slurmdbd_conn->flags & PERSIST_FLAG_EXT_DBD) &&
		   !(((slurmdb_cluster_rec_t *) list_peek(cluster_list))
			     ->flags &
		     CLUSTER_FLAG_EXT)) {
		comment = "Can't register to non-external cluster";
		rc = ESLURM_ACCESS_DENIED;
	}
	FREE_NULL_LIST(cluster_list);
	if (rc)
		goto end_it;

	list_msg.my_list =
		acct_storage_g_modify_clusters(slurmdbd_conn->db_conn,
					       &cluster_q, &cluster);
	if (errno == EFAULT) {
		comment = "Request to register was incomplete";
		rc = SLURM_ERROR;
	} else if (errno == ESLURM_ACCESS_DENIED) {
		comment = _internal_rc_to_str(rc, msg, false);
	} else if (errno == ESLURM_DB_CONNECTION) {
		comment = slurm_strerror(errno);
		rc = errno;
	} else if (!list_msg.my_list || !list_count(list_msg.my_list)) {
		comment = "This cluster hasn't been added to accounting yet";
		rc = SLURM_ERROR;
	} else if (cluster.id) {
		id_rc = cluster.id;
		id_rc |= RC_AS_CLUSTER_ID;
	}

	FREE_NULL_LIST(list_msg.my_list);
	FREE_NULL_LIST(cluster_q.cluster_list);

end_it:

	if (rc == SLURM_SUCCESS) {
		slurmdbd_conn->rem_port = register_ctld_msg->port;

		_add_registered_cluster(slurmdbd_conn, msg);
	}

	slurm_send_rc_err_msg(msg, id_rc, comment);

	/*
	 * We don't want/need to send updates to this cluster that just
	 * registered since it's rpc manager might not be up yet.
	 */
	slurmdbd_conn->flags |= PERSIST_FLAG_DONT_UPDATE_CLUSTER;
	return rc;
}

static int _remove_accounts(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_remove_accts(slurmdbd_conn->db_conn,
						  get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = errno;

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_account_coords(slurmdbd_conn_t *slurmdbd_conn,
				  slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_acct_coord_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if (!(list_msg.my_list =
		      acct_storage_g_remove_coord(slurmdbd_conn->db_conn,
						  get_msg->acct_list,
						  get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = SLURM_SUCCESS;

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_assocs(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if (!(list_msg.my_list =
		      acct_storage_g_remove_assocs(slurmdbd_conn->db_conn,
						   get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = errno;

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;

}

static int _remove_clusters(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_remove_clusters(slurmdbd_conn->db_conn,
						     get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = errno;

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_federations(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_remove_federations(slurmdbd_conn->db_conn,
							get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = errno;

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_qos(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_remove_qos(slurmdbd_conn->db_conn,
						get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = errno;

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_res(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_remove_res(slurmdbd_conn->db_conn,
						get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = errno;
	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_users(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_remove_users(slurmdbd_conn->db_conn,
						  get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = errno;

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_wckeys(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	if (!(list_msg.my_list =
		      acct_storage_g_remove_wckeys(slurmdbd_conn->db_conn,
						   get_msg->cond))) {
		rc = errno;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);
		return rc;
	}
	list_msg.return_code = errno;

	(void) send_msg_response(msg, DBD_GOT_LIST, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_reservation(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = msg->data;
	char *comment = NULL;

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_REMOVE_RESV message from invalid uid";
		error("DBD_REMOVE_RESV message from invalid uid %u",
		      slurmdbd_conn->auth.uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	rc = acct_storage_g_remove_reservation(slurmdbd_conn->db_conn,
					       rec_msg->rec);

end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _roll_usage(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_roll_usage_msg_t *get_msg = msg->data;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;
	list_t *rollup_stats_list = NULL;
	DEF_TIMERS;

	info("DBD_ROLL_USAGE: called in [%s]",
	     conmgr_con_get_name(msg->conmgr_con));

	if (!_validate_operator(slurmdbd_conn)) {
		rc = ESLURM_ACCESS_DENIED;
		comment = _internal_rc_to_str(rc, msg, false);
		goto end_it;
	}

	START_TIMER;
	rc = acct_storage_g_roll_usage(slurmdbd_conn->db_conn,
				       get_msg->start, get_msg->end,
				       get_msg->archive_data,
				       &rollup_stats_list);
	END_TIMER;
	handle_rollup_stats(rollup_stats_list, TIMER_DURATION_USEC(), 1);
	FREE_NULL_LIST(rollup_stats_list);

end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _send_mult_job_start(slurmdbd_conn_t *slurmdbd_conn,
				slurm_msg_t *msg)
{
	dbd_list_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;
	list_itr_t *itr = NULL;
	dbd_job_start_msg_t *job_start_msg;
	dbd_id_rc_msg_t *id_rc_msg;
	/* DEF_TIMERS; */

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_SEND_MULT_JOB_START message from invalid uid";
		error("%s %u", comment, slurmdbd_conn->auth.uid);
		slurm_send_rc_err_msg(msg, ESLURM_ACCESS_DENIED, comment);
		return SLURM_ERROR;
	}

	list_msg.my_list = list_create(slurmdbd_free_id_rc_msg);
	/* START_TIMER; */
	itr = list_iterator_create(get_msg->my_list);
	while ((job_start_msg = list_next(itr))) {
	        id_rc_msg = xmalloc(sizeof(dbd_id_rc_msg_t));
		list_append(list_msg.my_list, id_rc_msg);

		_process_job_start(slurmdbd_conn, msg, job_start_msg,
				   id_rc_msg);
	}
	list_iterator_destroy(itr);
	/* END_TIMER; */
	/* info("%d multi job took %s", */
	/*      list_count(get_msg->my_list), TIME_STR); */

	(void) send_msg_response(msg, DBD_GOT_MULT_JOB_START, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return SLURM_SUCCESS;
}

static int _send_mult_msg(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_list_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;
	list_itr_t *itr = NULL;
	buf_t *req_buf = NULL;
	int rc = SLURM_SUCCESS;
	/* DEF_TIMERS; */

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_SEND_MULT_MSG message from invalid uid";
		error("%s %u", comment, slurmdbd_conn->auth.uid);
		slurm_send_rc_err_msg(msg, ESLURM_ACCESS_DENIED, comment);
		return SLURM_ERROR;
	}

	list_msg.my_list = list_create(slurmdbd_free_buffer);
	/* START_TIMER; */
	itr = list_iterator_create(get_msg->my_list);
	while ((req_buf = list_next(itr))) {
		slurmdbd_msg_t sub_msg = { 0 };
		slurm_msg_t sub_req = *msg;
		buf_t *shadow, *ret_buf = NULL;

		/* Each sub-request's reply goes into the batched reply. */
		xassert(!sub_req.resp_buf);
		sub_req.resp_buf = &ret_buf;

		shadow = create_shadow_buf(get_buf_data(req_buf),
					   size_buf(req_buf));
		rc = unpack_slurmdbd_msg(&sub_msg, msg->protocol_version,
					 shadow);
		FREE_NULL_BUFFER(shadow);

		if ((rc == SLURM_SUCCESS) &&
		    (sub_msg.msg_type == DBD_RECONFIG)) {
			/* Batched RPCs hold reconfig_lock for reading. */
			rc = EINVAL;
			sub_req.msg_type = DBD_RECONFIG;
			slurm_send_rc_err_msg(&sub_req, rc,
					      "DBD_RECONFIG cannot be batched");
			slurmdbd_free_msg(&sub_msg);
		} else if ((rc == SLURM_SUCCESS) &&
			   (sub_msg.msg_type == REQUEST_PERSIST_INIT)) {
			/* Only the first top-level RPC may initialize. */
			comment =
				"REQUEST_PERSIST_INIT sent after connection established";
			error("[%s] %s", conmgr_con_get_name(msg->conmgr_con),
			      comment);
			rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
			sub_req.msg_type = REQUEST_PERSIST_INIT;
			slurm_send_rc_err_msg(&sub_req, rc, comment);
			slurmdbd_free_msg(&sub_msg);
		} else if (rc == SLURM_SUCCESS) {
			/* Same sender and session; another type and body. */
			sub_req.msg_type = sub_msg.msg_type;
			sub_req.data = sub_msg.data;
			rc = proc_req(slurmdbd_conn, &sub_req);
			slurmdbd_free_msg(&sub_msg);
		} else {
			/* Reply so the sender sees the failure and backs off. */
			char *err_msg =
				xstrdup_printf("Failed to unpack %s message",
					       slurmdbd_msg_type_2_str(
						       sub_msg.msg_type, true));

			error("[%s] %s", conmgr_con_get_name(msg->conmgr_con),
			      err_msg);
			sub_req.msg_type = sub_msg.msg_type;
			slurm_send_rc_err_msg(&sub_req, rc, err_msg);
			xfree(err_msg);
			if (sub_msg.data)
				slurmdbd_free_msg(&sub_msg);
		}

		if (ret_buf)
			list_append(list_msg.my_list, ret_buf);
		if ((rc != SLURM_SUCCESS) &&
		    (rc != SLURM_NO_CHANGE_IN_DATA))
			break;
	}
	list_iterator_destroy(itr);
	/* END_TIMER; */
	/* info("%d multi took %s", list_count(get_msg->my_list), TIME_STR); */

	(void) send_msg_response(msg, DBD_GOT_MULT_MSG, &list_msg);
	FREE_NULL_LIST(list_msg.my_list);

	return SLURM_SUCCESS;
}

static int _step_complete(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_step_comp_msg_t *step_comp_msg = msg->data;
	step_record_t step;
	job_record_t job;
	job_details_t details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_STEP_COMPLETE message from invalid uid";
		error("%s %u", comment, slurmdbd_conn->auth.uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_STEP_COMPLETE: %ps SUBMIT:%lu",
	       &step_comp_msg->step_id,
	       (unsigned long) step_comp_msg->job_submit_time);

	memset(&step, 0, sizeof(step_record_t));
	memset(&job, 0, sizeof(job_record_t));
	memset(&details, 0, sizeof(job_details_t));

	job.assoc_id = step_comp_msg->assoc_id;
	if (step_comp_msg->db_index != NO_VAL64)
		job.db_index = step_comp_msg->db_index;
	job.end_time = step_comp_msg->end_time;
	step.exit_code = step_comp_msg->exit_code;
	step.jobacct = step_comp_msg->jobacct;
	job.job_id = step_comp_msg->step_id.job_id;
	step.requid = step_comp_msg->req_uid;
	job.start_protocol_ver = msg->protocol_version;
	job.start_time = step_comp_msg->start_time;
	job.tres_alloc_str = step_comp_msg->job_tres_alloc_str;
	step.state = step_comp_msg->state;

	memcpy(&step.step_id, &step_comp_msg->step_id, sizeof(step.step_id));

	details.submit_time = step_comp_msg->job_submit_time;
	details.num_tasks = step_comp_msg->total_tasks;

	job.details = &details;
	step.job_ptr = &job;

	rc = jobacct_storage_g_step_complete(slurmdbd_conn->db_conn, &step);

	if (rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;
	/* just in case this gets set we need to clear it */
	xfree(job.wckey);

	if (!slurmdbd_conn->rem_port) {
		debug3("DBD_STEP_COMPLETE: cluster not registered");
		slurmdbd_conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				msg->pcon_ctx->rem_host);

		_add_registered_cluster(slurmdbd_conn, msg);
	}

end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _step_start(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	dbd_step_start_msg_t *step_start_msg = msg->data;
	step_record_t step;
	job_record_t job;
	job_details_t details;
	slurm_step_layout_t layout;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(slurmdbd_conn)) {
		comment = "DBD_STEP_START message from invalid uid";
		error("%s %u", comment, slurmdbd_conn->auth.uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_STEP_START: %ps NAME:%s SUBMIT:%lu",
	       &step_start_msg->step_id,
	       step_start_msg->name,
	       (unsigned long) step_start_msg->job_submit_time);

	memset(&step, 0, sizeof(step_record_t));
	memset(&job, 0, sizeof(job_record_t));
	memset(&details, 0, sizeof(job_details_t));
	memset(&layout, 0, sizeof(slurm_step_layout_t));

	job.assoc_id = step_start_msg->assoc_id;
	if (step_start_msg->db_index != NO_VAL64)
		job.db_index = step_start_msg->db_index;
	step.container = _replace_double_quotes(step_start_msg->container);
	job.job_id = step_start_msg->step_id.job_id;
	step.name = step_start_msg->name;
	job.nodes = step_start_msg->nodes;
	step.network = step_start_msg->node_inx;
	job.start_protocol_ver = msg->protocol_version;
	/*
	 * Set job.start_time to be the same as step.start_time. If the
	 * job_db_inx hasn't be created yet we need the start time or we will
	 * cause re-rolling to happen at details.submit_time. When dealing with
	 * job arrays that could be a long time in the past.
	 */
	job.start_time = step.start_time = step_start_msg->start_time;
	details.submit_time = step_start_msg->job_submit_time;
	step.time_limit = step_start_msg->time_limit;

	memcpy(&step.step_id, &step_start_msg->step_id, sizeof(step.step_id));

	details.num_tasks = step_start_msg->total_tasks;
	step.cpu_freq_min = step_start_msg->req_cpufreq_min;
	step.cpu_freq_max = step_start_msg->req_cpufreq_max;
	step.cpu_freq_gov = step_start_msg->req_cpufreq_gov;
	step.cwd = step_start_msg->cwd;
	step.state = step_start_msg->state;
	step.std_err = step_start_msg->std_err;
	step.std_in = step_start_msg->std_in;
	step.std_out = step_start_msg->std_out;
	step.submit_line = step_start_msg->submit_line;
	step.tres_alloc_str = step_start_msg->tres_alloc_str;

	layout.node_cnt = step_start_msg->node_cnt;
	layout.task_dist = step_start_msg->task_dist;

	job.details = &details;
	step.job_ptr = &job;
	step.step_layout = &layout;

	rc = jobacct_storage_g_step_start(slurmdbd_conn->db_conn, &step);

	if (rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just in case this gets set we need to clear it */
	xfree(job.wckey);

	if (!slurmdbd_conn->rem_port) {
		debug3("DBD_STEP_START: cluster not registered");
		slurmdbd_conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				msg->pcon_ctx->rem_host);

		_add_registered_cluster(slurmdbd_conn, msg);
	}

end_it:
	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _get_stats(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_super_user(slurmdbd_conn)) {
		int rc = ESLURM_ACCESS_DENIED;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);

		return rc;
	}

	debug2("Get stats request received from UID %u",
	       slurmdbd_conn->auth.uid);

	slurm_mutex_lock(&rpc_mutex);
	if (!rpc_stats) {
		slurm_mutex_unlock(&rpc_mutex);
		rc = ESLURM_NO_RPC_STATS;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);

		return rc;
	}
	(void) send_msg_response(msg, DBD_GOT_STATS, rpc_stats);
	slurm_mutex_unlock(&rpc_mutex);

	return rc;
}

static int _clear_stats(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_super_user(slurmdbd_conn)) {
		int rc = ESLURM_ACCESS_DENIED;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);

		return rc;
	}

	info("Clear stats request received from UID %u",
	     slurmdbd_conn->auth.uid);

	init_dbd_stats();

	slurm_send_rc_err_msg(msg, rc, comment);
	return rc;
}

static int _shutdown(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_super_user(slurmdbd_conn)) {
		int rc = ESLURM_ACCESS_DENIED;
		comment = _internal_rc_to_str(rc, msg, false);
		slurm_send_rc_err_msg(msg, rc, comment);

		return rc;
	}

	info("Shutdown request received from UID %u",
	     slurmdbd_conn->auth.uid);

	/* Queue the reply before starting the shutdown. */
	slurm_send_rc_err_msg(msg, rc, comment);
	shutdown_threads();
	return rc;
}

static const slurmdbd_rpc_t slurmdbd_rpcs[] = {
	{
		.msg_type = REQUEST_PERSIST_INIT,
		.func = _unpack_persist_init,
	},
	{
		.msg_type = DBD_ADD_ACCOUNTS,
		.func = _add_accounts,
	},
	{
		.msg_type = DBD_ADD_ACCOUNTS_COND,
		.func = _add_accounts_cond,
	},
	{
		.msg_type = DBD_ADD_ACCOUNT_COORDS,
		.func = _add_account_coords,
	},
	{
		.msg_type = DBD_ADD_TRES,
		.func = _add_tres,
	},
	{
		.msg_type = DBD_ADD_ASSOCS,
		.func = _add_assocs,
	},
	{
		.msg_type = DBD_ADD_CLUSTERS,
		.func = _add_clusters,
	},
	{
		.msg_type = DBD_ADD_FEDERATIONS,
		.func = _add_federations,
	},
	{
		.msg_type = DBD_ADD_QOS,
		.func = _add_qos,
	},
	{
		.msg_type = DBD_ADD_RES,
		.func = _add_res,
	},
	{
		.msg_type = DBD_ADD_USERS,
		.func = _add_users,
	},
	{
		.msg_type = DBD_ADD_USERS_COND,
		.func = _add_users_cond,
	},
	{
		.msg_type = DBD_ADD_WCKEYS,
		.func = _add_wckeys,
	},
	{
		.msg_type = DBD_ADD_RESV,
		.auth_level = AUTH_LEVEL_INTERNAL,
		.func = _add_reservation,
	},
	{
		.msg_type = DBD_ARCHIVE_DUMP,
		.func = _archive_dump,
	},
	{
		.msg_type = DBD_ARCHIVE_LOAD,
		.func = _archive_load,
	},
	{
		.msg_type = DBD_CLUSTER_TRES,
		.auth_level = AUTH_LEVEL_INTERNAL,
		.func = _cluster_tres,
	},
	{
		.msg_type = DBD_GET_ACCOUNTS,
		.func = _get_accounts,
	},
	{
		.msg_type = DBD_GET_TRES,
		.func = _get_tres,
	},
	{
		.msg_type = DBD_GET_ASSOCS,
		.func = _get_assocs,
	},
	{
		.msg_type = DBD_GET_ASSOC_USAGE,
		.func = _get_usage,
	},
	{
		.msg_type = DBD_GET_WCKEY_USAGE,
		.func = _get_usage,
	},
	{
		.msg_type = DBD_GET_ASSOC_NG_USAGE,
		.func = _get_usage,
	},
	{
		.msg_type = DBD_GET_CLUSTER_USAGE,
		.func = _get_usage,
	},
	{
		.msg_type = DBD_GET_QOS_USAGE,
		.func = _get_usage,
	},
	{
		.msg_type = DBD_GET_CLUSTERS,
		.func = _get_clusters,
	},
	{
		.msg_type = DBD_GET_FEDERATIONS,
		.func = _get_federations,
	},
	{
		.msg_type = DBD_GET_CONFIG,
		.func = _get_config,
	},
	{
		.msg_type = DBD_GET_EVENTS,
		.func = _get_events,
	},
	{
		.msg_type = DBD_GET_INSTANCES,
		.func = _get_instances,
	},
	{
		.msg_type = DBD_GET_JOBS_COND,
		.func = _get_jobs_cond,
	},
	{
		.msg_type = DBD_GET_PROBS,
		.func = _get_probs,
	},
	{
		.msg_type = DBD_GET_QOS,
		.func = _get_qos,
	},
	{
		.msg_type = DBD_GET_RES,
		.func = _get_res,
	},
	{
		.msg_type = DBD_GET_TXN,
		.func = _get_txn,
	},
	{
		.msg_type = DBD_GET_WCKEYS,
		.func = _get_wckeys,
	},
	{
		.msg_type = DBD_GET_RESVS,
		.func = _get_reservations,
	},
	{
		.msg_type = DBD_GET_USERS,
		.func = _get_users,
	},
	{
		.msg_type = DBD_FLUSH_JOBS,
		.auth_level = AUTH_LEVEL_INTERNAL,
		.func = _flush_jobs,
	},
	{
		.msg_type = DBD_FINI,
		.func = _fini_conn,
	},
	{
		.msg_type = DBD_JOB_COMPLETE,
		.auth_level = AUTH_LEVEL_INTERNAL,
		.func = _job_complete,
	},
	{
		.msg_type = DBD_JOB_START,
		.func = _job_start,
	},
	{
		.msg_type = DBD_JOB_HEAVY,
		.func = _job_heavy,
	},
	{
		.msg_type = DBD_JOB_SUSPEND,
		.func = _job_suspend,
	},
	{
		.msg_type = DBD_MODIFY_ACCOUNTS,
		.func = _modify_accounts,
	},
	{
		.msg_type = DBD_MODIFY_ASSOCS,
		.func = _modify_assocs,
	},
	{
		.msg_type = DBD_MODIFY_CLUSTERS,
		.func = _modify_clusters,
	},
	{
		.msg_type = DBD_MODIFY_FEDERATIONS,
		.func = _modify_federations,
	},
	{
		.msg_type = DBD_MODIFY_JOB,
		.func = _modify_job,
	},
	{
		.msg_type = DBD_MODIFY_QOS,
		.func = _modify_qos,
	},
	{
		.msg_type = DBD_MODIFY_RES,
		.func = _modify_res,
	},
	{
		.msg_type = DBD_MODIFY_USERS,
		.func = _modify_users,
	},
	{
		.msg_type = DBD_MODIFY_WCKEYS,
		.func = _modify_wckeys,
	},
	{
		.msg_type = DBD_MODIFY_RESV,
		.func = _modify_reservation,
	},
	{
		.msg_type = DBD_NODE_STATE,
		.func = _node_state,
	},
	{
		.msg_type = DBD_RECONFIG,
		.func = _reconfig,
	},
	{
		.msg_type = DBD_REGISTER_CTLD,
		.func = _register_ctld,
	},
	{
		.msg_type = DBD_REMOVE_ACCOUNTS,
		.func = _remove_accounts,
	},
	{
		.msg_type = DBD_REMOVE_ACCOUNT_COORDS,
		.func = _remove_account_coords,
	},
	{
		.msg_type = DBD_REMOVE_ASSOCS,
		.func = _remove_assocs,
	},
	{
		.msg_type = DBD_REMOVE_CLUSTERS,
		.func = _remove_clusters,
	},
	{
		.msg_type = DBD_REMOVE_FEDERATIONS,
		.func = _remove_federations,
	},
	{
		.msg_type = DBD_REMOVE_QOS,
		.func = _remove_qos,
	},
	{
		.msg_type = DBD_REMOVE_RES,
		.func = _remove_res,
	},
	{
		.msg_type = DBD_REMOVE_USERS,
		.func = _remove_users,
	},
	{
		.msg_type = DBD_REMOVE_WCKEYS,
		.func = _remove_wckeys,
	},
	{
		.msg_type = DBD_REMOVE_RESV,
		.func = _remove_reservation,
	},
	{
		.msg_type = DBD_ROLL_USAGE,
		.func = _roll_usage,
	},
	{
		.msg_type = DBD_SEND_MULT_JOB_START,
		.func = _send_mult_job_start,
	},
	{
		.msg_type = DBD_SEND_MULT_MSG,
		.func = _send_mult_msg,
	},
	{
		.msg_type = DBD_STEP_COMPLETE,
		.func = _step_complete,
	},
	{
		.msg_type = DBD_STEP_START,
		.func = _step_start,
	},
	{
		.msg_type = DBD_FIX_RUNAWAY_JOB,
		.func = _fix_runaway_jobs,
	},
	{
		.msg_type = DBD_GET_STATS,
		.func = _get_stats,
	},
	{
		.msg_type = DBD_CLEAR_STATS,
		.func = _clear_stats,
	},
	{
		.msg_type = DBD_SHUTDOWN,
		.func = _shutdown,
	},
	{
		/* terminate list */
	}
};

extern const slurmdbd_rpc_t *const find_rpc(uint16_t msg_type)
{
	for (const slurmdbd_rpc_t *q = slurmdbd_rpcs; q->msg_type; q++) {
		if (q->msg_type == msg_type) {
			xassert(q->func);
			return q;
		}
	}

	return NULL;
}

extern int proc_req(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg)
{
	const slurmdbd_rpc_t *this_rpc = NULL;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;
	slurmdb_rpc_obj_t *rpc_obj;
	uint32_t msg_type = msg->msg_type;

	DEF_TIMERS;
	START_TIMER;

	if (!slurmdbd_conn->auth.ids_set)
		fatal("%s: auth.ids_set is false, this should never happen",
		      __func__);

	if (slurm_conf.debug_flags & DEBUG_FLAG_PROTOCOL) {
		char *p = slurmdbd_msg_type_2_str(msg->msg_type, 1);
		if (msg->pcon_ctx->cluster_name) {
			info("%s: received opcode %s from persist conn on (%s)%s uid %u",
			     __func__, p, msg->pcon_ctx->cluster_name,
			     msg->pcon_ctx->rem_host,
			     slurmdbd_conn->auth.uid);
		} else {
			info("%s: received opcode %s from %s uid %u",
			     __func__, p, msg->pcon_ctx->rem_host,
			     slurmdbd_conn->auth.uid);
		}
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_AUDIT_RPCS) {
		slurm_addr_t cli_addr;
		(void) slurm_get_peer_addr(msg->pcon_ctx->fd, &cli_addr);
		log_flag(AUDIT_RPCS, "msg_type=%s uid=%u client=[%pA] protocol=%u",
			 slurmdbd_msg_type_2_str(msg->msg_type, 1),
			 slurmdbd_conn->auth.uid,
			 &cli_addr, msg->protocol_version);
	}

	if ((this_rpc = find_rpc(msg->msg_type))) {
		switch (this_rpc->auth_level) {
		case AUTH_LEVEL_INTERNAL:
			if (!_validate_slurm_user(slurmdbd_conn)) {
				error("Security violation, %s from %u is internal",
				      rpc_num2string(msg->msg_type),
				      msg->auth.uid);
				rc = ESLURM_ACCESS_DENIED;
			}
			break;
		case AUTH_LEVEL_ADMIN:
			if (slurmdbd_conn->auth.level < AUTH_LEVEL_ADMIN) {
				error("Security violation, %s from %u requires admin",
				      rpc_num2string(msg->msg_type),
				      msg->auth.uid);
				rc = ESLURM_ACCESS_DENIED;
			}
			break;
		case AUTH_LEVEL_OPERATOR:
			if (slurmdbd_conn->auth.level < AUTH_LEVEL_OPERATOR) {
				error("Security violation, %s from %u requires operator",
				      rpc_num2string(msg->msg_type),
				      msg->auth.uid);
				rc = ESLURM_ACCESS_DENIED;
			}
			break;
		case AUTH_LEVEL_USER:
		case AUTH_LEVEL_NOTSET:
			break;
		}
		if (rc == SLURM_SUCCESS) {
			rc = this_rpc->func(slurmdbd_conn, msg);
		} else {
			persist_rc_msg_t reply = {
				.comment = "Access denied",
				.rc = rc,
			};
			(void) send_msg_response(msg, PERSIST_RC, &reply);
		}
	} else {
		comment = "Invalid RPC";
		error("[%s] %s msg_type=%s",
		      conmgr_con_get_name(msg->conmgr_con),
		      comment, rpc_num2string(msg->msg_type));
		rc = EINVAL;
		/* The reply does not name the request's type. */
		persist_rc_msg_t reply = {
			.comment = comment,
			.rc = rc,
		};
		(void) send_msg_response(msg, PERSIST_RC, &reply);
	}

	if (rc == ESLURM_ACCESS_DENIED)
		error("[%s] Security violation, %s",
		      conmgr_con_get_name(msg->conmgr_con),
		      slurmdbd_msg_type_2_str(msg->msg_type, 1));
	else if (slurmdbd_conn->rem_port &&
		 (!slurmdbd_conf->commit_delay ||
		  (msg->msg_type == DBD_REGISTER_CTLD))) {
		/* If we are dealing with the slurmctld do the
		   commit (SUCCESS or NOT) afterwards since we
		   do transactions for performance reasons.
		   (don't ever use autocommit with innodb)
		*/
		acct_storage_g_commit(slurmdbd_conn->db_conn, 1);
	}
	/*
	 * Clear DONT_UPDATE flag now so that it's tied to this transaction
	 * only. Can't clear in p_commit() because if CommitDelay is set, we may
	 * not send needed updates later.
	 */
	slurmdbd_conn->flags &= ~PERSIST_FLAG_DONT_UPDATE_CLUSTER;

	END_TIMER;

	slurm_mutex_lock(&rpc_mutex);
	if (!rpc_stats) {
		slurm_mutex_unlock(&rpc_mutex);
		return rc;
	}

	if (!(rpc_obj = list_find_first(rpc_stats->rpc_list,
					_find_rpc_obj_in_list, &msg_type))) {
		rpc_obj = xmalloc(sizeof(slurmdb_rpc_obj_t));
		rpc_obj->id = msg_type;
		list_append(rpc_stats->rpc_list, rpc_obj);
	}
	rpc_obj->cnt++;
	rpc_obj->time += TIMER_DURATION_USEC();

	if (!(rpc_obj = list_find_first(rpc_stats->user_list,
					_find_rpc_obj_in_list,
					&slurmdbd_conn->auth.uid))) {
		rpc_obj = xmalloc(sizeof(slurmdb_rpc_obj_t));
		rpc_obj->id = slurmdbd_conn->auth.uid;
		list_append(rpc_stats->user_list, rpc_obj);
	}
	rpc_obj->cnt++;
	rpc_obj->time += TIMER_DURATION_USEC();

	slurm_mutex_unlock(&rpc_mutex);

	return rc;
}
