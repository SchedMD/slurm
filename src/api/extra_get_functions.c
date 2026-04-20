/*****************************************************************************\
 *  extra_get_functions.c - Interface to functions dealing with
 *                          getting info from the database, and this
 *                          these were unrelated to other functionality.
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "slurm/slurmdb.h"

#include "src/common/parse_time.h"

#include "src/interfaces/accounting_storage.h"

/*
 * reconfigure the slurmdbd
 */
extern int slurmdb_reconfig(void *db_conn)
{
	return acct_storage_g_reconfig(db_conn, 1);
}

extern list_t *slurmdb_config_get_keypairs(const slurmdbd_conf_t *slurmdbd_conf)
{
	char time_str[32];
	char *tmp_ptr = NULL;
	list_t *my_list = list_create(destroy_config_key_pair);

	add_key_pair_bool(my_list, "AllowNoDefAcct",
			  (slurmdbd_conf->flags &
			   DBD_CONF_FLAG_ALLOW_NO_DEF_ACCT));

	add_key_pair(my_list, "ArchiveDir", "%s", slurmdbd_conf->archive_dir);

	add_key_pair_bool(my_list, "ArchiveEvents",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf
							    ->purge_event));

	add_key_pair_bool(my_list, "ArchiveJobs",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf->purge_job));

	add_key_pair_bool(my_list, "ArchiveJobScript",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf
							    ->purge_jobscript));

	add_key_pair_bool(my_list, "ArchiveJobEnv",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf
							    ->purge_jobenv));

	add_key_pair_bool(my_list, "ArchiveResvs",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf->purge_resv));

	add_key_pair(my_list, "ArchiveScript", "%s",
		     slurmdbd_conf->archive_script);

	add_key_pair_bool(my_list, "ArchiveSteps",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf->purge_step));

	add_key_pair_bool(my_list, "ArchiveSuspend",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf
							    ->purge_suspend));

	add_key_pair_bool(my_list, "ArchiveTXN",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf->purge_txn));

	add_key_pair_bool(my_list, "ArchiveUsage",
			  SLURMDB_PURGE_ARCHIVE_SET(slurmdbd_conf
							    ->purge_usage));

	add_key_pair(my_list, "AuthAltTypes", "%s", slurm_conf.authalttypes);

	add_key_pair(my_list, "AuthAltParameters", "%s",
		     slurm_conf.authalt_params);

	add_key_pair(my_list, "AuthInfo", "%s", slurm_conf.authinfo);

	add_key_pair(my_list, "AuthType", "%s", slurm_conf.authtype);

	add_key_pair(my_list, "CommitDelay", "%u", slurmdbd_conf->commit_delay);

	add_key_pair(my_list, "CommunicationParameters", "%s",
		     slurm_conf.comm_params);

	add_key_pair(my_list, "DbdAddr", "%s", slurmdbd_conf->dbd_addr);

	add_key_pair(my_list, "DbdBackupHost", "%s", slurmdbd_conf->dbd_backup);

	add_key_pair(my_list, "DbdHost", "%s", slurmdbd_conf->dbd_host);

	add_key_pair(my_list, "DbdPort", "%u", slurmdbd_conf->dbd_port);

	add_key_pair_own(my_list, "DebugFlags",
			 debug_flags2str(slurm_conf.debug_flags));

	add_key_pair(my_list, "DebugLevel", "%s",
		     log_num2string(slurmdbd_conf->debug_level));

	add_key_pair(my_list, "DebugLevelSyslog", "%s",
		     log_num2string(slurmdbd_conf->syslog_debug));

	add_key_pair(my_list, "DefaultQOS", "%s", slurmdbd_conf->default_qos);

	add_key_pair_bool(my_list, "DisableCoordDBD",
			  (slurmdbd_conf->flags &
			   DBD_CONF_FLAG_DISABLE_COORD_DBD));

	add_key_pair_bool(my_list, "DisableArchiveCommands",
			  (slurmdbd_conf->flags &
			   DBD_CONF_FLAG_DISABLE_ARCHIVE_COMMANDS));

	add_key_pair_bool(my_list, "DisableRollups",
			  (slurmdbd_conf->flags &
			   DBD_CONF_FLAG_DISABLE_ROLLUPS));

	add_key_pair(my_list, "HashPlugin", "%s", slurm_conf.hash_plugin);

	add_key_pair(my_list, "LogFile", "%s", slurmdbd_conf->log_file);

	add_key_pair(my_list, "MaxPurgeLimit", "%u",
		     slurmdbd_conf->max_purge_limit);

	secs2time_str(slurmdbd_conf->max_time_range, time_str,
		      sizeof(time_str));
	add_key_pair(my_list, "MaxQueryTimeRange", "%s", time_str);

	add_key_pair(my_list, "MessageTimeout", "%u secs",
		     slurm_conf.msg_timeout);

	add_key_pair(my_list, "Parameters", "%s", slurmdbd_conf->parameters);

	add_key_pair(my_list, "PidFile", "%s", slurmdbd_conf->pid_file);

	add_key_pair(my_list, "PluginDir", "%s", slurm_conf.plugindir);

	tmp_ptr = xmalloc(128);
	private_data_string(slurm_conf.private_data, tmp_ptr, 128);
	add_key_pair(my_list, "PrivateData", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_event != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_event, tmp_ptr, 32,
				     1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeEventAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_job != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_job, tmp_ptr, 32, 1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeJobAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_resv != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_resv, tmp_ptr, 32, 1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeResvAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_step != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_step, tmp_ptr, 32, 1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeStepAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_suspend != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_suspend, tmp_ptr, 32,
				     1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeSuspendAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_txn != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_txn, tmp_ptr, 32, 1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeTXNAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_usage != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_usage, tmp_ptr, 32,
				     1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeUsageAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_jobscript != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_jobscript, tmp_ptr,
				     32, 1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeJobScriptAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	if (slurmdbd_conf->purge_jobenv != NO_VAL) {
		tmp_ptr = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_jobenv, tmp_ptr, 32,
				     1);
	} else
		tmp_ptr = xstrdup("NONE");

	add_key_pair(my_list, "PurgeJobEnvAfter", "%s", tmp_ptr);
	xfree(tmp_ptr);

	add_key_pair_own(my_list, "SLURMDBD_CONF",
			 get_extra_conf_path("slurmdbd.conf"));

	add_key_pair(my_list, "SLURMDBD_VERSION", "%s", SLURM_VERSION_STRING);

	add_key_pair(my_list, "SlurmUser", "%s(%u)", slurm_conf.slurm_user_name,
		     slurm_conf.slurm_user_id);

	add_key_pair(my_list, "StorageBackupHost", "%s",
		     slurm_conf.accounting_storage_backup_host);

	add_key_pair(my_list, "StorageHost", "%s",
		     slurm_conf.accounting_storage_host);

	add_key_pair(my_list, "StorageLoc", "%s", slurmdbd_conf->storage_loc);

	add_key_pair(my_list, "StorageParameters", "%s",
		     slurm_conf.accounting_storage_params);

	/* StoragePass should NOT be passed due to security reasons */

	add_key_pair(my_list, "StoragePassScript", "%s",
		     slurmdbd_conf->storage_pass_script);

	add_key_pair(my_list, "StoragePort", "%u",
		     slurm_conf.accounting_storage_port);

	add_key_pair(my_list, "StorageType", "%s",
		     slurm_conf.accounting_storage_type);

	add_key_pair(my_list, "StorageUser", "%s", slurmdbd_conf->storage_user);

	add_key_pair(my_list, "TCPTimeout", "%u secs", slurm_conf.tcp_timeout);

	add_key_pair(my_list, "TLSParameters", "%s", slurm_conf.tls_params);

	add_key_pair(my_list, "TLSType", "%s", slurm_conf.tls_type);

	add_key_pair_bool(my_list, "TrackWCKey", slurmdbd_conf->track_wckey);

	add_key_pair_bool(my_list, "TrackSlurmctldDown",
			  slurmdbd_conf->track_ctld);

	return my_list;
}

extern int slurmdb_config_get(void *db_conn,
			      slurmdbd_conf_t **slurmdbd_conf_ptr)
{
	return acct_storage_g_get_config(db_conn, slurmdbd_conf_ptr);
}

/*
 * get info from the storage
 * IN:  slurmdb_event_cond_t *
 * RET: List of slurmdb_event_rec_t *
 * note List needs to be freed when called
 */
extern list_t *slurmdb_events_get(void *db_conn,
				  slurmdb_event_cond_t *event_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_get_events(db_conn, db_api_uid, event_cond);
}

/*
 * get info from the storage
 *
 * IN:  slurmdb_instance_cond_t *
 * RET: List of slurmdb_instance_rec_t *
 * note List needs to be freed when called
 */
extern list_t *slurmdb_instances_get(void *db_conn,
				     slurmdb_instance_cond_t *instance_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_get_instances(db_conn, db_api_uid, instance_cond);
}

/*
 * get info from the storage
 * IN:  slurmdb_assoc_cond_t *
 * RET: List of slurmdb_assoc_rec_t *
 * note List needs to be freed when called
 */
extern list_t *slurmdb_problems_get(void *db_conn,
				    slurmdb_assoc_cond_t *assoc_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_get_problems(db_conn, db_api_uid, assoc_cond);
}

/*
 * get info from the storage
 * IN:  slurmdb_reservation_cond_t *
 * RET: List of slurmdb_reservation_rec_t *
 * note List needs to be freed when called
 */
extern list_t *slurmdb_reservations_get(void *db_conn,
					slurmdb_reservation_cond_t *resv_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_get_reservations(db_conn, db_api_uid, resv_cond);
}

/*
 * get info from the storage
 * IN:  slurmdb_txn_cond_t *
 * RET: List of slurmdb_txn_rec_t *
 * note List needs to be freed when called
 */
extern list_t *slurmdb_txn_get(void *db_conn, slurmdb_txn_cond_t *txn_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_get_txn(db_conn, db_api_uid, txn_cond);
}

/*
 * shutdown the slurmdbd
 */
extern int slurmdb_shutdown(void *db_conn)
{
	return acct_storage_g_shutdown(db_conn);
}

/*
 * clear the slurmdbd statistics
 */
extern int slurmdb_clear_stats(void *db_conn)
{
	return acct_storage_g_clear_stats(db_conn);
}

/*
 * get the slurmdbd statistics
 * Call slurmdb_destroy_stats_rec() to free stats_pptr
 */
extern int slurmdb_get_stats(void *db_conn, slurmdb_stats_rec_t **stats_pptr)
{
	return acct_storage_g_get_stats(db_conn, stats_pptr);
}
