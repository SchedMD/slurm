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
#include "src/common/slurm_time.h"

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
	track_wckey = slurm_get_track_wckey();
}

static void _free_slurm_config(void)
{
}

static void _print_slurm_config(void)
{
	time_t now = time(NULL);
	char tmp_str[256], *user_name = NULL;

	slurm_make_time_str(&now, tmp_str, sizeof(tmp_str));
	printf("Configuration data as of %s\n", tmp_str);
	printf("AccountingStorageBackupHost  = %s\n",
	       slurm_conf.accounting_storage_backup_host);
	printf("AccountingStorageHost  = %s\n",
	       slurm_conf.accounting_storage_host);
	printf("AccountingStorageParameters = %s\n",
	       slurm_conf.accounting_storage_params);
	printf("AccountingStoragePass  = %s\n",
	       slurm_conf.accounting_storage_pass);
	printf("AccountingStoragePort  = %u\n",
	       slurm_conf.accounting_storage_port);
	printf("AccountingStorageType  = %s\n",
	       slurm_conf.accounting_storage_type);
	printf("AccountingStorageUser  = %s\n",
	       slurm_conf.accounting_storage_user);
	printf("AuthType               = %s\n", slurm_conf.authtype);
	printf("MessageTimeout         = %u sec\n", slurm_conf.msg_timeout);
	printf("PluginDir              = %s\n", slurm_conf.plugindir);
	private_data_string(slurm_conf.private_data, tmp_str, sizeof(tmp_str));
	printf("PrivateData            = %s\n", tmp_str);
	user_name = uid_to_string_cached(slurm_conf.slurm_user_id);
	printf("SlurmUserId            = %s(%u)\n",
	       user_name, slurm_conf.slurm_user_id);
	printf("SLURM_CONF             = %s\n", default_slurm_config_file);
	printf("SLURM_VERSION          = %s\n", SLURM_VERSION_STRING);
	printf("TCPTimeout             = %u sec\n", slurm_conf.tcp_timeout);
	printf("TrackWCKey             = %s\n", track_wckey ? "Yes" : "No");
}


static void _print_rollup_stats(slurmdb_rollup_stats_t *rollup_stats, int i)
{
	uint64_t roll_ave;

	if (!rollup_stats)
		return;

	printf(" last ran %s (%ld)\n",
	       slurm_ctime2(&rollup_stats->timestamp[i]),
	       rollup_stats->timestamp[i]);

	roll_ave = rollup_stats->time_total[i];
	if (rollup_stats->count[i] > 1)
		roll_ave /= rollup_stats->count[i];

	printf("\tLast cycle:   %"PRIu64"\n", rollup_stats->time_last[i]);
	printf("\tMax cycle:    %"PRIu64"\n", rollup_stats->time_max[i]);
	printf("\tTotal time:   %"PRIu64"\n", rollup_stats->time_total[i]);
	printf("\tTotal cycles: %u\n", rollup_stats->count[i]);


	printf("\tMean cycle:   %"PRIu64"\n", roll_ave);
}

static int _sort_rpc_obj_by_id(void *void1, void *void2)
{
	slurmdb_rpc_obj_t *rpc_obj1 = (slurmdb_rpc_obj_t *)void1;
	slurmdb_rpc_obj_t *rpc_obj2 = (slurmdb_rpc_obj_t *)void2;

	if (rpc_obj1->id < rpc_obj2->id)
		return -1;
	else if (rpc_obj1->id > rpc_obj2->id)
		return 1;
	return 0;
}

static int _sort_rpc_obj_by_ave_time(void *void1, void *void2)
{
	slurmdb_rpc_obj_t *rpc_obj1 = *(slurmdb_rpc_obj_t **)void1;
	slurmdb_rpc_obj_t *rpc_obj2 = *(slurmdb_rpc_obj_t **)void2;

	if (rpc_obj1->time_ave > rpc_obj2->time_ave)
		return -1;
	else if (rpc_obj1->time_ave < rpc_obj2->time_ave)
		return 1;

	return _sort_rpc_obj_by_id(void1, void2);
}

static int _sort_rpc_obj_by_time(void *void1, void *void2)
{
	slurmdb_rpc_obj_t *rpc_obj1 = *(slurmdb_rpc_obj_t **)void1;
	slurmdb_rpc_obj_t *rpc_obj2 = *(slurmdb_rpc_obj_t **)void2;

	if (rpc_obj1->time > rpc_obj2->time)
		return -1;
	else if (rpc_obj1->time < rpc_obj2->time)
		return 1;

	return _sort_rpc_obj_by_id(void1, void2);
}

static int _sort_rpc_obj_by_cnt(void *void1, void *void2)
{
	slurmdb_rpc_obj_t *rpc_obj1 = *(slurmdb_rpc_obj_t **)void1;
	slurmdb_rpc_obj_t *rpc_obj2 = *(slurmdb_rpc_obj_t **)void2;

	if (rpc_obj1->cnt > rpc_obj2->cnt)
		return -1;
	else if (rpc_obj1->cnt < rpc_obj2->cnt)
		return 1;

	return _sort_rpc_obj_by_time(void1, void2);
}

static int _print_rpc_obj(void *x, void *arg)
{
	slurmdb_rpc_obj_t *rpc_obj = (slurmdb_rpc_obj_t *)x;
	int type = *(int *)arg;

	if (type == 0)
		printf("\t%-25s(%5u)",
		       slurmdbd_msg_type_2_str(rpc_obj->id, 1),
		       rpc_obj->id);
	else
		printf("\t%-20s(%10u)",
		       uid_to_string_cached((uid_t)rpc_obj->id),
		       rpc_obj->id);

	printf(" count:%-6u ave_time:%-6"PRIu64" total_time:%"PRIu64"\n",
	       rpc_obj->cnt,
	       rpc_obj->time_ave, rpc_obj->time);

	return 0;
}

extern int sacctmgr_list_config(void)
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
	slurmdb_stats_rec_t *stats_rec = NULL;
	slurmdb_rollup_stats_t *rollup_stats = NULL;
	int error_code, i;
	bool sort_by_ave_time = false, sort_by_total_time = false;
	time_t now = time(NULL);
	int type;

	error_code = slurmdb_get_stats(db_conn, &stats_rec);
	if (error_code != SLURM_SUCCESS)
		return error_code;

	rollup_stats = stats_rec->dbd_rollup_stats;
	printf("*******************************************************************\n");
	printf("sacctmgr show stats output at %s (%ld)\n",
	       slurm_ctime2(&now), now);
	printf("Data since                    %s (%ld)\n",
	       slurm_ctime2(&stats_rec->time_start), stats_rec->time_start);
	printf("All statistics are in microseconds\n");
	printf("*******************************************************************\n");

	for (i = 0; i < DBD_ROLLUP_COUNT; i++) {
		if (rollup_stats->time_total[i] == 0)
			continue;
		if (i == 0)
			printf("\nInternal DBD rollup");
		else if (i == 1)
			printf("\nUser RPC rollup call");
		else
			printf("\nunknown rollup");
		_print_rollup_stats(rollup_stats, i);
	}

	if (stats_rec->rollup_stats && list_count(stats_rec->rollup_stats)) {
		ListIterator itr =
			list_iterator_create(stats_rec->rollup_stats);
		while ((rollup_stats = list_next(itr))) {
			bool first = true;

			for (i = 0; i < DBD_ROLLUP_COUNT; i++) {
				if (rollup_stats->time_total[i] == 0)
					continue;
				if (first) {
					printf("\nCluster '%s' rollup statistics\n",
					       rollup_stats->cluster_name);
					first = false;
				}
				printf("%-5s", rollup_interval_to_string(i));
				_print_rollup_stats(rollup_stats, i);
			}
		}
		list_iterator_destroy(itr);
	}

	if (argc) {
		if (!xstrncasecmp(argv[0], "ave_time", 2))
			sort_by_ave_time = true;
		else if (!xstrncasecmp(argv[0], "total_time", 2))
			sort_by_total_time = true;
	}

	if (sort_by_ave_time) {
		list_sort(stats_rec->rpc_list,
			  (ListCmpF)_sort_rpc_obj_by_ave_time);
		list_sort(stats_rec->user_list,
			  (ListCmpF)_sort_rpc_obj_by_ave_time);
	} else if (sort_by_total_time) {
		list_sort(stats_rec->rpc_list, (ListCmpF)_sort_rpc_obj_by_time);
		list_sort(stats_rec->user_list,
			  (ListCmpF)_sort_rpc_obj_by_time);
	} else {	/* sort by RPC count */
		list_sort(stats_rec->rpc_list, (ListCmpF)_sort_rpc_obj_by_cnt);
		list_sort(stats_rec->user_list, (ListCmpF)_sort_rpc_obj_by_cnt);
	}

	printf("\nRemote Procedure Call statistics by message type\n");
	type = 0;
	list_for_each(stats_rec->rpc_list, _print_rpc_obj, &type);

	printf("\nRemote Procedure Call statistics by user\n");
	type = 1;
	list_for_each(stats_rec->user_list, _print_rpc_obj, &type);

	slurmdb_destroy_stats_rec(stats_rec);

	return error_code;
}
