/*****************************************************************************\
 *  config_functions.c - functions dealing with system configuration.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/sacctmgr/sacctmgr.h"

static char    *acct_storage_backup_host = NULL;
static char    *acct_storage_host = NULL;
static char    *acct_storage_loc  = NULL;
static char    *acct_storage_pass = NULL;
static uint32_t acct_storage_port;
static char    *acct_storage_type = NULL;
static char    *acct_storage_user = NULL;
static char    *auth_type = NULL;
static uint16_t msg_timeout;
static char    *plugin_dir = NULL;
static uint16_t private_data;
static uint32_t slurm_user_id;
static uint16_t tcp_timeout;
static uint16_t track_wckey;

static List dbd_config_list = NULL;


static void _load_dbd_config(void)
{
	dbd_config_list = slurmdb_config_get(db_conn);
}

static void _print_dbd_config(void)
{
	ListIterator iter = NULL;
	config_key_pair_t *key_pair;

	if (!dbd_config_list)
		return;

	printf("\nSlurmDBD configuration:\n");
	iter = list_iterator_create(dbd_config_list);
	while((key_pair = list_next(iter))) {
		printf("%-22s = %s\n", key_pair->name, key_pair->value);
	}
	list_iterator_destroy(iter);
}

static void _free_dbd_config(void)
{
	if (!dbd_config_list)
		return;

	FREE_NULL_LIST(dbd_config_list);
}

static void _load_slurm_config(void)
{
	acct_storage_backup_host = slurm_get_accounting_storage_backup_host();
	acct_storage_host = slurm_get_accounting_storage_host();
	acct_storage_loc  = slurm_get_accounting_storage_loc();
	acct_storage_pass = slurm_get_accounting_storage_pass();
	acct_storage_port = slurm_get_accounting_storage_port();
	acct_storage_type = slurm_get_accounting_storage_type();
	acct_storage_user = slurm_get_accounting_storage_user();
	auth_type = slurm_get_auth_type();
	msg_timeout = slurm_get_msg_timeout();
	plugin_dir = slurm_get_plugin_dir();
	private_data = slurm_get_private_data();
	slurm_user_id = slurm_get_slurm_user_id();
	tcp_timeout = slurm_get_tcp_timeout();
	track_wckey = slurm_get_track_wckey();
}

static void _free_slurm_config(void)
{
	xfree(acct_storage_backup_host);
	xfree(acct_storage_host);
	xfree(acct_storage_loc);
	xfree(acct_storage_pass);
	xfree(acct_storage_type);
	xfree(acct_storage_user);
	xfree(auth_type);
	xfree(plugin_dir);
}

static void _print_slurm_config(void)
{
	time_t now = time(NULL);
	char tmp_str[128], *user_name = NULL;

	slurm_make_time_str(&now, tmp_str, sizeof(tmp_str));
	printf("Configuration data as of %s\n", tmp_str);
	printf("AccountingStorageBackupHost  = %s\n", acct_storage_backup_host);
	printf("AccountingStorageHost  = %s\n", acct_storage_host);
	printf("AccountingStorageLoc   = %s\n", acct_storage_loc);
	printf("AccountingStoragePass  = %s\n", acct_storage_pass);
	printf("AccountingStoragePort  = %u\n", acct_storage_port);
	printf("AccountingStorageType  = %s\n", acct_storage_type);
	printf("AccountingStorageUser  = %s\n", acct_storage_user);
	printf("AuthType               = %s\n", auth_type);
	printf("MessageTimeout         = %u sec\n", msg_timeout);
	printf("PluginDir              = %s\n", plugin_dir);
	private_data_string(private_data, tmp_str, sizeof(tmp_str));
	printf("PrivateData            = %s\n", tmp_str);
	user_name = uid_to_string_cached(slurm_user_id);
	printf("SlurmUserId            = %s(%u)\n", user_name, slurm_user_id);
	printf("SLURM_CONF             = %s\n", default_slurm_config_file);
	printf("SLURM_VERSION          = %s\n", SLURM_VERSION_STRING);
	printf("TCPTimeout             = %u sec\n", tcp_timeout);
	printf("TrackWCKey             = %u\n", track_wckey);
}

extern int sacctmgr_list_config(bool have_db_conn)
{
	_load_slurm_config();
	_print_slurm_config();
	_free_slurm_config();

	if (have_db_conn) {
		_load_dbd_config();
		_print_dbd_config();
		_free_dbd_config();
	}

	return SLURM_SUCCESS;
}

extern int sacctmgr_list_stats(int argc, char **argv)
{
	uint32_t *rpc_type_ave_time = NULL, *rpc_user_ave_time = NULL;
	slurmdb_stats_rec_t *buf = NULL;
	int error_code, i, j;
	uint16_t type_id;
	uint32_t type_ave, type_cnt, user_ave, user_cnt, user_id;
	uint64_t roll_ave, type_time, user_time;
	bool sort_by_ave_time = false, sort_by_total_time = false;
	char *rollup_type;

	error_code = slurmdb_get_stats(db_conn, &buf);
	if (error_code != SLURM_SUCCESS)
		return error_code;

	printf("Rollup statistics\n");
	for (i = 0; i < ROLLUP_COUNT; i++) {
		if (i == ROLLUP_HOUR)
			rollup_type = "Hour";
		else if (i == ROLLUP_DAY)
			rollup_type = "Day";
		else	/* (i == ROLLUP_MONTH) */
			rollup_type = "Month";
		roll_ave = buf->rollup_time[i];
		if (buf->rollup_count[i] > 1)
			roll_ave /= buf->rollup_count[i];
		printf("\t%-10s count:%-6u ave_time:%-6"PRIu64
		       " max_time:%-12"PRIu64" total_time:%-12"PRIu64"\n",
		       rollup_type, buf->rollup_count[i], roll_ave,
		       buf->rollup_max_time[i], buf->rollup_time[i]);
	}

	if (argc) {
		if (!xstrncasecmp(argv[0], "ave_time", 2))
			sort_by_ave_time = true;
		else if (!xstrncasecmp(argv[0], "total_time", 2))
			sort_by_total_time = true;
	}

	rpc_type_ave_time = xmalloc(sizeof(uint32_t) * buf->type_cnt);
	rpc_user_ave_time = xmalloc(sizeof(uint32_t) * buf->user_cnt);

	if (sort_by_ave_time) {
		for (i = 0; i < buf->type_cnt; i++) {
			if (buf->rpc_type_cnt[i]) {
				rpc_type_ave_time[i] = buf->rpc_type_time[i] /
						       buf->rpc_type_cnt[i];
			}
		}
		for (i = 0; i < buf->type_cnt; i++) {
			for (j = i+1; j < buf->type_cnt; j++) {
				if (rpc_type_ave_time[i] >= rpc_type_ave_time[j])
					continue;
				type_ave  = rpc_type_ave_time[i];
				type_id   = buf->rpc_type_id[i];
				type_cnt  = buf->rpc_type_cnt[i];
				type_time = buf->rpc_type_time[i];
				rpc_type_ave_time[i]  = rpc_type_ave_time[j];
				buf->rpc_type_id[i]   = buf->rpc_type_id[j];
				buf->rpc_type_cnt[i]  = buf->rpc_type_cnt[j];
				buf->rpc_type_time[i] = buf->rpc_type_time[j];
				rpc_type_ave_time[j]  = type_ave;
				buf->rpc_type_id[j]   = type_id;
				buf->rpc_type_cnt[j]  = type_cnt;
				buf->rpc_type_time[j] = type_time;
			}
		}
		for (i = 0; i < buf->user_cnt; i++) {
			if (buf->rpc_user_cnt[i]) {
				rpc_user_ave_time[i] = buf->rpc_user_time[i] /
						       buf->rpc_user_cnt[i];
			}
		}
		for (i = 0; i < buf->user_cnt; i++) {
			for (j = i+1; j < buf->user_cnt; j++) {
				if (rpc_user_ave_time[i] >= rpc_user_ave_time[j])
					continue;
				user_ave  = rpc_user_ave_time[i];
				user_id   = buf->rpc_user_id[i];
				user_cnt  = buf->rpc_user_cnt[i];
				user_time = buf->rpc_user_time[i];
				rpc_user_ave_time[i]  = rpc_user_ave_time[j];
				buf->rpc_user_id[i]   = buf->rpc_user_id[j];
				buf->rpc_user_cnt[i]  = buf->rpc_user_cnt[j];
				buf->rpc_user_time[i] = buf->rpc_user_time[j];
				rpc_user_ave_time[j]  = user_ave;
				buf->rpc_user_id[j]   = user_id;
				buf->rpc_user_cnt[j]  = user_cnt;
				buf->rpc_user_time[j] = user_time;
			}
		}
	} else if (sort_by_total_time) {
		for (i = 0; i < buf->type_cnt; i++) {
			for (j = i+1; j < buf->type_cnt; j++) {
				if (buf->rpc_type_time[i] >=
				    buf->rpc_type_time[j])
					continue;
				type_id   = buf->rpc_type_id[i];
				type_cnt  = buf->rpc_type_cnt[i];
				type_time = buf->rpc_type_time[i];
				buf->rpc_type_id[i]   = buf->rpc_type_id[j];
				buf->rpc_type_cnt[i]  = buf->rpc_type_cnt[j];
				buf->rpc_type_time[i] = buf->rpc_type_time[j];
				buf->rpc_type_id[j]   = type_id;
				buf->rpc_type_cnt[j]  = type_cnt;
				buf->rpc_type_time[j] = type_time;
			}
			if (buf->rpc_type_cnt[i]) {
				rpc_type_ave_time[i] = buf->rpc_type_time[i] /
						       buf->rpc_type_cnt[i];
			}
		}
		for (i = 0; i < buf->user_cnt; i++) {
			for (j = i+1; j < buf->user_cnt; j++) {
				if (buf->rpc_user_time[i] >=
				    buf->rpc_user_time[j])
					continue;
				user_id   = buf->rpc_user_id[i];
				user_cnt  = buf->rpc_user_cnt[i];
				user_time = buf->rpc_user_time[i];
				buf->rpc_user_id[i]   = buf->rpc_user_id[j];
				buf->rpc_user_cnt[i]  = buf->rpc_user_cnt[j];
				buf->rpc_user_time[i] = buf->rpc_user_time[j];
				buf->rpc_user_id[j]   = user_id;
				buf->rpc_user_cnt[j]  = user_cnt;
				buf->rpc_user_time[j] = user_time;
			}
			if (buf->rpc_user_cnt[i]) {
				rpc_user_ave_time[i] = buf->rpc_user_time[i] /
						       buf->rpc_user_cnt[i];
			}
		}
	} else {	/* sort by RPC count */
		for (i = 0; i < buf->type_cnt; i++) {
			for (j = i+1; j < buf->type_cnt; j++) {
				if (buf->rpc_type_cnt[i] >=
				    buf->rpc_type_cnt[j])
					continue;
				type_id   = buf->rpc_type_id[i];
				type_cnt  = buf->rpc_type_cnt[i];
				type_time = buf->rpc_type_time[i];
				buf->rpc_type_id[i]   = buf->rpc_type_id[j];
				buf->rpc_type_cnt[i]  = buf->rpc_type_cnt[j];
				buf->rpc_type_time[i] = buf->rpc_type_time[j];
				buf->rpc_type_id[j]   = type_id;
				buf->rpc_type_cnt[j]  = type_cnt;
				buf->rpc_type_time[j] = type_time;
			}
			if (buf->rpc_type_cnt[i]) {
				rpc_type_ave_time[i] = buf->rpc_type_time[i] /
						       buf->rpc_type_cnt[i];
			}
		}
		for (i = 0; i < buf->user_cnt; i++) {
			for (j = i+1; j < buf->user_cnt; j++) {
				if (buf->rpc_user_cnt[i] >=
				    buf->rpc_user_cnt[j])
					continue;
				user_id   = buf->rpc_user_id[i];
				user_cnt  = buf->rpc_user_cnt[i];
				user_time = buf->rpc_user_time[i];
				buf->rpc_user_id[i]   = buf->rpc_user_id[j];
				buf->rpc_user_cnt[i]  = buf->rpc_user_cnt[j];
				buf->rpc_user_time[i] = buf->rpc_user_time[j];
				buf->rpc_user_id[j]   = user_id;
				buf->rpc_user_cnt[j]  = user_cnt;
				buf->rpc_user_time[j] = user_time;
			}
			if (buf->rpc_user_cnt[i]) {
				rpc_user_ave_time[i] = buf->rpc_user_time[i] /
						       buf->rpc_user_cnt[i];
			}
		}
	}

	printf("\nRemote Procedure Call statistics by message type\n");
	for (i = 0; i < buf->type_cnt; i++) {
		if (buf->rpc_type_cnt[i] == 0)
			continue;
		printf("\t%-25s(%5u) count:%-6u "
		       "ave_time:%-6u total_time:%"PRIu64"\n",
		       slurmdbd_msg_type_2_str(buf->rpc_type_id[i], 1),
		       buf->rpc_type_id[i], buf->rpc_type_cnt[i],
		       rpc_type_ave_time[i], buf->rpc_type_time[i]);
	}

	printf("\nRemote Procedure Call statistics by user\n");
	for (i = 0; i < buf->user_cnt; i++) {
		if (buf->rpc_user_cnt[i] == 0)
			continue;
		printf("\t%-20s(%10u) count:%-6u "
		       "ave_time:%-6u total_time:%"PRIu64"\n",
		       uid_to_string_cached((uid_t)buf->rpc_user_id[i]),
		       buf->rpc_user_id[i], buf->rpc_user_cnt[i],
		       rpc_user_ave_time[i], buf->rpc_user_time[i]);
	}

	xfree(rpc_type_ave_time);
	xfree(rpc_user_ave_time);
	slurmdb_destroy_stats_rec(buf);

	return error_code;
}
