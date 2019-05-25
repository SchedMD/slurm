/*****************************************************************************\
 *  acct_gather_interconnect_ofed.c -slurm interconnect accounting plugin for ofed
 *****************************************************************************
 *  Copyright (C) 2013
 *  Written by Bull- Yiannis Georgiou
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"
#include "src/common/assoc_mgr.h"
#include "src/common/slurm_acct_gather_interconnect.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"
#include "src/common/slurm_acct_gather_profile.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "acct_gather_interconnect_ofed.h"

/*
 * ofed includes for the lib
 */

#include <infiniband/umad.h>
#include <infiniband/mad.h>

/***************************************************************/

#define ALL_PORTS 0xFF


#define _DEBUG 1
#define _DEBUG_INTERCONNECT 1
#define TIMEOUT 20
#define IB_FREQ 4

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

const char plugin_name[] = "AcctGatherInterconnect OFED plugin";
const char plugin_type[] = "acct_gather_interconnect/ofed";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

typedef struct {
	uint32_t port;
} slurm_ofed_conf_t;


struct ibmad_port *srcport = NULL;
static ib_portid_t portid;
static int ibd_timeout = 0;
static int port = 0;

typedef struct {
	time_t last_update_time;
	time_t update_time;
	uint64_t xmtdata;
	uint64_t rcvdata;
	uint64_t xmtpkts;
	uint64_t rcvpkts;
	uint64_t total_xmtdata;
	uint64_t total_rcvdata;
	uint64_t total_xmtpkts;
	uint64_t total_rcvpkts;
} ofed_sens_t;

static ofed_sens_t ofed_sens = {0,0,0,0,0,0,0,0};

static uint8_t pc[1024];

static slurm_ofed_conf_t ofed_conf;
static uint64_t debug_flags = 0;
static pthread_mutex_t ofed_lock = PTHREAD_MUTEX_INITIALIZER;

static int dataset_id = -1; /* id of the dataset for profile data */
static int tres_pos = -1;

static uint8_t *_slurm_pma_query_via(void *rcvbuf, ib_portid_t * dest, int port,
				     unsigned timeout, unsigned id,
				     const struct ibmad_port *srcport)
{
#ifdef HAVE_OFED_PMA_QUERY_VIA
	return pma_query_via(rcvbuf, dest, port, timeout, id, srcport);
#else
	switch (id) {
	case CLASS_PORT_INFO:
		return perf_classportinfo_query_via(
			pc, &portid, port, ibd_timeout, srcport);
		break;
	case IB_GSI_PORT_COUNTERS_EXT:
		return port_performance_ext_query_via(
			pc, &portid, port, ibd_timeout, srcport);
		break;
	default:
		error("_slurm_pma_query_via: unhandled id");
	}
	return NULL;
#endif
}

/*
 * _read_ofed_values read the IB sensor and update last_update values and times
 */
static int _read_ofed_values(void)
{
	static uint64_t last_update_xmtdata = 0;
	static uint64_t last_update_rcvdata = 0;
	static uint64_t last_update_xmtpkts = 0;
	static uint64_t last_update_rcvpkts = 0;
	static bool first = true;

	int rc = SLURM_SUCCESS;

	uint16_t cap_mask;
	uint64_t send_val, recv_val, send_pkts, recv_pkts;

	ofed_sens.last_update_time = ofed_sens.update_time;
	ofed_sens.update_time = time(NULL);

	if (first) {
		int mgmt_classes[4] = {IB_SMI_CLASS, IB_SMI_DIRECT_CLASS,
				       IB_SA_CLASS, IB_PERFORMANCE_CLASS};
		srcport = mad_rpc_open_port(NULL, ofed_conf.port,
					    mgmt_classes, 4);
		if (!srcport) {
			debug("%s: Failed to open port '%d'",
			      __func__, ofed_conf.port);
			debug("OFED: failed");
			return SLURM_ERROR;
		}

		if (ib_resolve_self_via(&portid, &port, 0, srcport) < 0)
			error("can't resolve self port %d", port);

		memset(pc, 0, sizeof(pc));
		if (!_slurm_pma_query_via(pc, &portid, port, ibd_timeout,
					  CLASS_PORT_INFO, srcport))
			error("classportinfo query: %m");

		memcpy(&cap_mask, pc + 2, sizeof(cap_mask));
		if (!_slurm_pma_query_via(pc, &portid, port, ibd_timeout,
					  IB_GSI_PORT_COUNTERS_EXT, srcport)) {
			error("ofed: %m");
			return SLURM_ERROR;
		}

		mad_decode_field(pc, IB_PC_EXT_XMT_BYTES_F,
				 &last_update_xmtdata);
		mad_decode_field(pc, IB_PC_EXT_RCV_BYTES_F,
				 &last_update_rcvdata);
		mad_decode_field(pc, IB_PC_EXT_XMT_PKTS_F,
				 &last_update_xmtpkts);
		mad_decode_field(pc, IB_PC_EXT_RCV_PKTS_F,
				 &last_update_rcvpkts);

		if (debug_flags & DEBUG_FLAG_INTERCONNECT)
			info("%s ofed init", plugin_name);

		first = 0;
		return SLURM_SUCCESS;
	}

	memset(pc, 0, sizeof(pc));
	memcpy(&cap_mask, pc + 2, sizeof(cap_mask));
	if (!_slurm_pma_query_via(pc, &portid, port, ibd_timeout,
				  IB_GSI_PORT_COUNTERS_EXT, srcport)) {
		error("ofed: %m");
		return SLURM_ERROR;
	}

	mad_decode_field(pc, IB_PC_EXT_XMT_BYTES_F, &send_val);
	mad_decode_field(pc, IB_PC_EXT_RCV_BYTES_F, &recv_val);
	mad_decode_field(pc, IB_PC_EXT_XMT_PKTS_F, &send_pkts);
	mad_decode_field(pc, IB_PC_EXT_RCV_PKTS_F, &recv_pkts);

	ofed_sens.xmtdata = (send_val - last_update_xmtdata) * 4;
	ofed_sens.total_xmtdata += ofed_sens.xmtdata;
	ofed_sens.rcvdata = (recv_val - last_update_rcvdata) * 4;
	ofed_sens.total_rcvdata += ofed_sens.rcvdata;
	ofed_sens.xmtpkts = send_pkts - last_update_xmtpkts;
	ofed_sens.total_xmtpkts += ofed_sens.xmtpkts;
	ofed_sens.rcvpkts = recv_pkts - last_update_rcvpkts;
	ofed_sens.total_rcvpkts += ofed_sens.rcvpkts;

	last_update_xmtdata = send_val;
	last_update_rcvdata = recv_val;
	last_update_xmtpkts = send_pkts;
	last_update_rcvpkts = recv_pkts;

	return rc;
}


/*
 * _thread_update_node_energy calls _read_ipmi_values and updates all values
 * for node consumption
 */
static int _update_node_interconnect(void)
{
	int rc;

	enum {
		FIELD_PACKIN,
		FIELD_PACKOUT,
		FIELD_MBIN,
		FIELD_MBOUT,
		FIELD_CNT
	};

	acct_gather_profile_dataset_t dataset[] = {
		{ "PacketsIn", PROFILE_FIELD_UINT64 },
		{ "PacketsOut", PROFILE_FIELD_UINT64 },
		{ "InMB", PROFILE_FIELD_DOUBLE },
		{ "OutMB", PROFILE_FIELD_DOUBLE },
		{ NULL, PROFILE_FIELD_NOT_SET }
	};

	union {
		double d;
		uint64_t u64;
	} data[FIELD_CNT];

	if (dataset_id < 0) {
		dataset_id = acct_gather_profile_g_create_dataset("Network",
			NO_PARENT, dataset);
		if (debug_flags & DEBUG_FLAG_INTERCONNECT)
			debug("IB: dataset created (id = %d)", dataset_id);
		if (dataset_id == SLURM_ERROR) {
			error("IB: Failed to create the dataset for ofed");
			return SLURM_ERROR;
		}
	}

	slurm_mutex_lock(&ofed_lock);
	if ((rc = _read_ofed_values()) != SLURM_SUCCESS) {
		slurm_mutex_unlock(&ofed_lock);
		return rc;
	}

	data[FIELD_PACKIN].u64 = ofed_sens.rcvpkts;
	data[FIELD_PACKOUT].u64 = ofed_sens.xmtpkts;
	data[FIELD_MBIN].d = (double) ofed_sens.rcvdata / (1 << 20);
	data[FIELD_MBOUT].d = (double) ofed_sens.xmtdata / (1 << 20);

	if (debug_flags & DEBUG_FLAG_INTERCONNECT) {
		info("ofed-thread = %d sec, transmitted %"PRIu64" bytes, "
		     "received %"PRIu64" bytes",
		     (int) (ofed_sens.update_time - ofed_sens.last_update_time),
		     ofed_sens.xmtdata, ofed_sens.rcvdata);
	}
	slurm_mutex_unlock(&ofed_lock);

	if (debug_flags & DEBUG_FLAG_PROFILE) {
		char str[256];
		info("PROFILE-Network: %s", acct_gather_profile_dataset_str(
			     dataset, data, str, sizeof(str)));
	}
	return acct_gather_profile_g_add_sample_data(dataset_id, (void *)data,
						     ofed_sens.update_time);
}

static bool _run_in_daemon(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmstepd");
	}

	return run;
}


/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	slurmdb_tres_rec_t tres_rec;

	if (!_run_in_daemon())
		return SLURM_SUCCESS;

	debug_flags = slurm_get_debug_flags();

	memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
	tres_rec.type = "ic";
	tres_rec.name = "ofed";
	tres_pos = assoc_mgr_find_tres_pos(&tres_rec, false);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	if (!_run_in_daemon())
		return SLURM_SUCCESS;

	if ((srcport) && (!(dataset_id < 0))) {
		_update_node_interconnect();
		mad_rpc_close_port(srcport);
	} else if (srcport) {
		mad_rpc_close_port(srcport);
	}

	if (debug_flags & DEBUG_FLAG_INTERCONNECT)
		info("ofed: ended");

	return SLURM_SUCCESS;
}

extern int acct_gather_interconnect_p_node_update(void)
{
	uint32_t profile;
	int rc = SLURM_SUCCESS;
	static bool set = false;
	static bool run = true;

	if (!set) {
		set = true;
		acct_gather_profile_g_get(ACCT_GATHER_PROFILE_RUNNING,
					  &profile);

		if (!(profile & ACCT_GATHER_PROFILE_NETWORK))
			run = false;
	}

	if (run)
		_update_node_interconnect();

	return rc;
}


extern void acct_gather_interconnect_p_conf_set(s_p_hashtbl_t *tbl)
{
	if (tbl) {
		if (!s_p_get_uint32(&ofed_conf.port,
				    "InterconnectOFEDPort", tbl) &&
		    !s_p_get_uint32(&ofed_conf.port,
				    "InfinibandOFEDPort", tbl))
			ofed_conf.port = INTERCONNECT_DEFAULT_PORT;
	}

	if (!_run_in_daemon())
		return;

	debug("%s loaded", plugin_name);
	ofed_sens.update_time = time(NULL);
}

extern void acct_gather_interconnect_p_conf_options(
	s_p_options_t **full_options, int *full_options_cnt)
{
	s_p_options_t options[] = {
		{"InterconnectOFEDPort", S_P_UINT32},
		{"InfinibandOFEDPort", S_P_UINT32},
		{NULL} };

	transfer_s_p_options(full_options, options, full_options_cnt);

	return;
}

extern void acct_gather_interconnect_p_conf_values(List *data)
{
	config_key_pair_t *key_pair;

	xassert(*data);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("InterconnectOFEDPort");
	key_pair->value = xstrdup_printf("%u", ofed_conf.port);
	list_append(*data, key_pair);

	return;
}

extern int acct_gather_interconnect_p_get_data(acct_gather_data_t *data)
{
	int retval = SLURM_SUCCESS;

	if ((tres_pos == -1) || !data) {
		debug2("%s: We are not tracking TRES ic/ofed", __func__);
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&ofed_lock);

	if (_read_ofed_values() != SLURM_SUCCESS) {
		debug2("%s: Cannot retrieve ofed counters", __func__);
		slurm_mutex_unlock(&ofed_lock);
		return SLURM_ERROR;
	}

	data[tres_pos].num_reads = ofed_sens.total_rcvpkts;
	data[tres_pos].num_writes = ofed_sens.total_xmtpkts;
	data[tres_pos].size_read = ofed_sens.total_rcvdata;
	data[tres_pos].size_write = ofed_sens.total_xmtdata;

	slurm_mutex_unlock(&ofed_lock);

	return retval;
}
