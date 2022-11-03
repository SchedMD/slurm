/*****************************************************************************\
 *  acct_gather_interconnect_sysfs.c
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#include "src/common/slurm_xlator.h"

#include "src/common/assoc_mgr.h"
#include "src/common/list.h"
#include "src/interfaces/jobacct_gather.h"

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
const char plugin_name[] = "AcctGatherInterconnect sysfs plugin";
const char plugin_type[] = "acct_gather_interconnect/sysfs";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static int tres_pos = -1;

#define SYSFS_FMT_PREFIX "/sys/class/net/%s/statistics/"
#define RX_BYTES_FMT SYSFS_FMT_PREFIX "rx_bytes"
#define TX_BYTES_FMT SYSFS_FMT_PREFIX "tx_bytes"
#define RX_PACKETS_FMT SYSFS_FMT_PREFIX "rx_packets"
#define TX_PACKETS_FMT SYSFS_FMT_PREFIX "tx_packets"

typedef struct {
	char *name;
	char *rx_bytes_file;
	char *tx_bytes_file;
	char *rx_packets_file;
	char *tx_packets_file;
	unsigned long rx_bytes_start;
	unsigned long tx_bytes_start;
	unsigned long rx_packets_start;
	unsigned long tx_packets_start;
	unsigned long rx_bytes;
	unsigned long tx_bytes;
	unsigned long rx_packets;
	unsigned long tx_packets;
} interface_stats_t;

static List interfaces = NULL;

static char *sysfs_interfaces = NULL;

static acct_gather_data_t *last_update = NULL;

static void _destroy_interface_stats_t(void *x)
{
	interface_stats_t *interface = x;

	xfree(interface->name);
	xfree(interface->rx_bytes_file);
	xfree(interface->tx_bytes_file);
	xfree(interface->rx_packets_file);
	xfree(interface->tx_packets_file);
	xfree(interface);
}

static long unsigned _load(const char *file, unsigned long start)
{
	FILE *fp;
	unsigned long new = 0, delta = 0;

	if (!(fp = fopen(file, "r"))) {
		debug("Failed to open `%s`: %m", file);
		return 0;
	}

	if (fscanf(fp, "%lu", &new) != 1) {
		debug("Failed to read value from `%s`", file);
		fclose(fp);
		return 0;
	}

	fclose(fp);

	/*
	 * Avoid underflow. May happen on counter wrap on 32-bit systems,
	 * but on 64-bit systems this is unlikely.
	 */
	if (start < new)
		delta = new - start;

	debug3("Value from %s: %ld, delta %ld", file, new, delta);

	return delta;
}

static int _get_data(void *x, void *arg)
{
	interface_stats_t *iface = x;
	acct_gather_data_t *data = arg;

	data->num_reads += _load(iface->rx_bytes_file, iface->rx_bytes_start);
	data->num_writes += _load(iface->tx_bytes_file, iface->tx_bytes_start);
	data->size_read +=
		_load(iface->rx_packets_file, iface->rx_packets_start);
	data->size_write +=
		_load(iface->tx_packets_file, iface->tx_packets_start);

	return 0;
}

extern int init(void)
{
	slurmdb_tres_rec_t tres_rec;

	debug("loaded");

	if (!running_in_slurmstepd())
		return SLURM_SUCCESS;

	memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
	tres_rec.type = "ic";
	tres_rec.name = "sysfs";
	tres_pos = assoc_mgr_find_tres_pos(&tres_rec, false);

	if (tres_pos == -1)
		error("TRES ic/sysfs not configured");

	interfaces = list_create(_destroy_interface_stats_t);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	FREE_NULL_LIST(interfaces);
	xfree(sysfs_interfaces);
	xfree(last_update);

	return SLURM_SUCCESS;
}

static int _update(void)
{
	static int dataset_id = -1;
	acct_gather_data_t *current = NULL, *last = last_update;

	union {
		double d;
		uint64_t u64;
	} data[4];

	acct_gather_profile_dataset_t dataset[] = {
		{ "PacketsIn", PROFILE_FIELD_UINT64 },
		{ "PacketsOut", PROFILE_FIELD_UINT64 },
		{ "InMB", PROFILE_FIELD_DOUBLE },
		{ "OutMB", PROFILE_FIELD_DOUBLE },
		{ NULL, PROFILE_FIELD_NOT_SET }
	};

	if (dataset_id < 0) {
		dataset_id = acct_gather_profile_g_create_dataset("Network",
			NO_PARENT, dataset);
		log_flag(INTERCONNECT, "Dataset created (id = %d)",
			 dataset_id);
		if (dataset_id == SLURM_ERROR) {
			error("Failed to create the dataset for sysfs");
			return SLURM_ERROR;
		}
	}

	current = xmalloc(sizeof(*current));
	list_for_each(interfaces, _get_data, current);
	if (!last)
		last = current;

	data[0].u64 = current->num_reads - last->num_reads;
	data[1].u64 = current->num_writes - last->num_writes;
	data[2].d = (double)(current->size_read - last->size_read) / (1 << 16);
	data[3].d = (double)(current->size_write - last->size_write) / (1 << 16);

	xfree(last_update);
	last_update = current;

	return acct_gather_profile_g_add_sample_data(dataset_id, (void *) data,
						     time(NULL));

}

extern int acct_gather_interconnect_p_node_update(void)
{
	static int run = -1;

	if (run == -1) {
		uint32_t profile;
		acct_gather_profile_g_get(ACCT_GATHER_PROFILE_RUNNING,
					  &profile);
		run = (profile & ACCT_GATHER_PROFILE_NETWORK) ? 1 : 0;
	}

	if (!run)
		return SLURM_SUCCESS;

	return _update();
}

extern void acct_gather_interconnect_p_conf_set(s_p_hashtbl_t *tbl)
{
	char *save, *tmp, *token, *save_ptr = NULL;

	if (!tbl)
		return;

	if (!s_p_get_string(&sysfs_interfaces, "SysfsInterfaces", tbl)) {
		debug("no interfaces set to poll");
		return;
	}

	if (!running_in_slurmstepd())
		return;

	save = tmp = xstrdup(sysfs_interfaces);
	while ((token = strtok_r(tmp, ",", &save_ptr))) {
		interface_stats_t *iface = xmalloc(sizeof(*iface));

		iface->name = xstrdup(token);
		iface->rx_bytes_file = xstrdup_printf(RX_BYTES_FMT, token);
		iface->tx_bytes_file = xstrdup_printf(TX_BYTES_FMT, token);
		iface->rx_packets_file = xstrdup_printf(RX_PACKETS_FMT, token);
		iface->tx_packets_file = xstrdup_printf(TX_PACKETS_FMT, token);

		iface->rx_bytes_start = _load(iface->rx_bytes_file, 0);
		iface->tx_bytes_start = _load(iface->tx_bytes_file, 0);
		iface->rx_packets_start = _load(iface->rx_packets_file, 0);
		iface->tx_packets_start = _load(iface->tx_packets_file, 0);

		list_push(interfaces, iface);
		tmp = NULL;
	}
	xfree(save);
}

extern void acct_gather_interconnect_p_conf_options(
	s_p_options_t **full_options, int *full_options_cnt)
{
	s_p_options_t options[] = {
		{"SysfsInterfaces", S_P_STRING},
		{NULL}
	};

	transfer_s_p_options(full_options, options, full_options_cnt);
}

extern void acct_gather_interconnect_p_conf_values(List *data)
{
	config_key_pair_t *key_pair;

	xassert(*data);

	key_pair = xmalloc(sizeof(*key_pair));
        key_pair->name = xstrdup("SysfsInterfaces");
        key_pair->value = xstrdup(sysfs_interfaces);
        list_append(*data, key_pair);
}

static void _blank(acct_gather_data_t *data)
{
        data->num_reads = 0;
        data->num_writes = 0;
        data->size_read = 0;
        data->size_write = 0;
}

extern int acct_gather_interconnect_p_get_data(acct_gather_data_t *data)
{
	if ((tres_pos == -1) || !data) {
		debug2("We are not tracking TRES ic/sysfs");
		return SLURM_SUCCESS;
	}

	_blank(&data[tres_pos]);
	list_for_each(interfaces, _get_data, &data[tres_pos]);

	return SLURM_SUCCESS;
}
