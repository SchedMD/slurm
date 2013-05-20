/*****************************************************************************\
 *  acct_gather_infiniband_ofed.c -slurm infiniband accounting plugin for ofed
 *****************************************************************************
 *  Copyright (C) 2013
 *  Written by Bull- Yiannis Georgiou
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/


#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#include <unistd.h>
#include <getopt.h>
#include <netinet/in.h>


#include "src/common/slurm_xlator.h"
#include "src/common/slurm_acct_gather_infiniband.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/plugins/acct_gather_profile/hdf5/hdf5_api.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "acct_gather_infiniband_ofed.h"

/*
 * ofed includes for the lib
 */

#include <infiniband/umad.h>
#include <infiniband/mad.h>

/***************************************************************/

#define ALL_PORTS 0xFF


#define _DEBUG 1
#define _DEBUG_INFINIBAND 1
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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the job accounting API
 * matures.
 */

const char plugin_name[] = "AcctGatherInfiniband OFED plugin";
const char plugin_type[] = "acct_gather_infiniband/ofed";
const uint32_t plugin_version = 100;

typedef struct {
	uint32_t freq;
	uint32_t port;
} slurm_ofed_conf_t;


struct ibmad_port *srcport = NULL;
static ib_portid_t portid;
static int ibd_timeout = 0;
char *ibd_ca = NULL;
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
static uint32_t debug_flags = 0;
static bool flag_infiniband_accounting_shutdown = false;
static bool flag_thread_run_running = false;
static bool flag_thread_started = false;
static bool flag_update_started = false;
static bool flag_slurmd_process = false;
pthread_t thread_ofed_id_launcher = 0;
pthread_t thread_ofed_id_run = 0;

static void _task_sleep(int rem)
{
	while (rem)
		rem = sleep(rem);	// subject to interupt
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

	uint64_t mask = 0xffffffffffffffff;
	uint64_t reset_limit = mask * 0.7;
	uint16_t cap_mask;
	uint64_t send_val, recv_val, send_pkts, recv_pkts;

	memset(pc, 0, sizeof(pc));
	memcpy(&cap_mask, pc + 2, sizeof(cap_mask));
	if (!port_performance_ext_query_via(pc, &portid, port, ibd_timeout,
					    srcport)) {
		error("ofed: %m");
		exit(1);
	}

	mad_decode_field(pc, IB_PC_EXT_XMT_BYTES_F, &send_val);
	ofed_sens.xmtdata = (send_val - last_update_xmtdata)*4;
	ofed_sens.total_xmtdata += ofed_sens.xmtdata;
	mad_decode_field(pc, IB_PC_EXT_RCV_BYTES_F, &recv_val);
	ofed_sens.rcvdata = (recv_val - last_update_rcvdata)*4;
	ofed_sens.total_rcvdata += ofed_sens.rcvdata;
	mad_decode_field(pc, IB_PC_EXT_XMT_PKTS_F, &send_pkts);
	ofed_sens.xmtpkts += send_pkts - last_update_xmtpkts;
	ofed_sens.total_xmtpkts += ofed_sens.xmtpkts;
	mad_decode_field(pc, IB_PC_EXT_RCV_PKTS_F, &recv_pkts);
	ofed_sens.rcvpkts += recv_pkts - last_update_rcvpkts;
	ofed_sens.total_rcvpkts += ofed_sens.rcvpkts;

	if (send_val > reset_limit || recv_val > reset_limit) {
		/* reset cost ~70 mirco secs */
		if (!port_performance_ext_reset_via(pc, &portid, port, mask,
						    ibd_timeout, srcport)) {
			error("perf reset\n");
			exit(1);
		}
		mad_decode_field(pc, IB_PC_EXT_XMT_BYTES_F, &send_val);
		mad_decode_field(pc, IB_PC_EXT_RCV_BYTES_F, &recv_val);
		mad_decode_field(pc, IB_PC_EXT_XMT_PKTS_F, &send_pkts);
		mad_decode_field(pc, IB_PC_EXT_RCV_PKTS_F, &recv_pkts);

	}
	last_update_xmtdata = send_val;
	last_update_rcvdata = recv_val;
	last_update_xmtpkts = send_pkts;
	last_update_rcvpkts = recv_pkts;

	ofed_sens.last_update_time = ofed_sens.update_time;
	ofed_sens.update_time = time(NULL);

	return rc;
}


/*
 * _thread_update_node_energy calls _read_ipmi_values and updates all values
 * for node consumption
 */
static int _update_node_infiniband(void)
{
	acct_network_data_t *net;
	int rc = SLURM_SUCCESS;

	rc = _read_ofed_values();

	net = xmalloc(sizeof(acct_network_data_t));

	net->packets_in = ofed_sens.rcvpkts;
	net->packets_out = ofed_sens.xmtpkts;
	net->size_in = ofed_sens.rcvdata;
	net->size_out = ofed_sens.xmtdata;
	acct_gather_profile_g_add_sample_data(ACCT_GATHER_PROFILE_NETWORK, net);

	if (debug_flags & DEBUG_FLAG_INFINIBAND) {
		info("ofed-thread = %d sec, transmitted %"PRIu64" bytes, "
		     "received %"PRIu64" bytes",
		     (int) (ofed_sens.update_time - ofed_sens.last_update_time),
		     ofed_sens.xmtdata, ofed_sens.rcvdata);
	}
	xfree(net);

	return rc;
}


/*
 * _thread_init initializes values and conf for the ipmi thread
 */
static int _thread_init(void)
{
	int rc = SLURM_SUCCESS;
	int mgmt_classes[4] = {IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS,
			       IB_PERFORMANCE_CLASS};
	uint64_t mask = 0xffffffffffffffff;
	uint16_t cap_mask;

	srcport = mad_rpc_open_port(ibd_ca, ofed_conf.port,
				    mgmt_classes, 4);
	if (!srcport){
		error("Failed to open '%s' port '%d'", ibd_ca,
		      ofed_conf.port);
		debug("INFINIBAND: failed");
	}

	if (ib_resolve_self_via(&portid, &port, 0, srcport) < 0)
		error("can't resolve self port %d", port);
	memset(pc, 0, sizeof(pc));
	if (!perf_classportinfo_query_via(pc, &portid, port, ibd_timeout,
					  srcport))
		error("classportinfo query\n");

	memcpy(&cap_mask, pc + 2, sizeof(cap_mask));
	if (!port_performance_ext_query_via(pc, &portid, port, ibd_timeout,
					    srcport)) {
		error("ofed\n");
		exit(1);
	}

	/* reset cost ~70 mirco secs */
	if (!port_performance_ext_reset_via(pc, &portid, port, mask,
					    ibd_timeout, srcport)) {
		error("perf reset\n");
		exit(1);
	}

	mad_decode_field(pc, IB_PC_EXT_XMT_BYTES_F, &ofed_sens.xmtdata);
	mad_decode_field(pc, IB_PC_EXT_RCV_BYTES_F, &ofed_sens.rcvdata);
	mad_decode_field(pc, IB_PC_EXT_XMT_PKTS_F, &ofed_sens.xmtpkts);
	mad_decode_field(pc, IB_PC_EXT_RCV_PKTS_F, &ofed_sens.rcvpkts);

	if (debug_flags & DEBUG_FLAG_INFINIBAND)
		info("%s thread init", plugin_name);

	return rc;
}

/*
 * _thread_fini finalizes values for the infiniband thread
 */
static int _thread_fini(void)
{
	mad_rpc_close_port(srcport);
	return SLURM_SUCCESS;
}

/*
 * _thread_ofed_run is the thread calling ipmi
 */
static void *_thread_ofed_run(void *no_data)
{
	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	flag_thread_run_running = true;
	flag_infiniband_accounting_shutdown = false;
	if (debug_flags & DEBUG_FLAG_INFINIBAND)
		info("ofed-thread: launched");

	memset(&portid, 0, sizeof(ib_portid_t));

	if (_thread_init() != SLURM_SUCCESS) {
		if (debug_flags & DEBUG_FLAG_INFINIBAND)
			info("ofed-thread: aborted");
		flag_thread_run_running = false;
		return NULL;
	}

	flag_thread_started = true;

	debug("INFINIBAND: thread_ofed_run");

	//loop until end of job
	while (!flag_infiniband_accounting_shutdown) {
		_task_sleep(ofed_conf.freq);
		_update_node_infiniband();
	}

	flag_thread_run_running = false;
	_thread_fini();
	if (debug_flags & DEBUG_FLAG_INFINIBAND)
		info("ofed-thread: ended");

	return NULL;
}

static void *_thread_launcher(void *no_data)
{
	pthread_attr_t attr_run;
	time_t begin_time;
	int rc = SLURM_SUCCESS;

	flag_slurmd_process = true;

	info("INFINIBAND: thread_launcher");

	slurm_attr_init(&attr_run);
	if (pthread_create(&thread_ofed_id_run, &attr_run,
			   &_thread_ofed_run, NULL)) {
		debug("infiniband accounting failed to create _thread_ofed_run "
		      "thread: %m");
	}
	slurm_attr_destroy(&attr_run);

	begin_time = time(NULL);
	while (rc == SLURM_SUCCESS) {
		if (time(NULL) - begin_time > TIMEOUT) {
			error("ofed thread launch timeout");
			rc = SLURM_ERROR;
			break;
		}
		if (flag_thread_run_running)
			break;
		_task_sleep(1);
	}

	begin_time = time(NULL);
	while (rc == SLURM_SUCCESS) {
		if (time(NULL) - begin_time > TIMEOUT) {
			error("ofed thread init timeout");
			rc = SLURM_ERROR;
			break;
		}
		if (!flag_thread_run_running) {
			error("ofed thread lost");
			rc = SLURM_ERROR;
			break;
		}
		if (flag_thread_started)
			break;
		_task_sleep(1);
	}

	if (rc != SLURM_SUCCESS) {
		error("%s threads failed to start in a timely manner",
		      plugin_name);
		if (thread_ofed_id_write) {
			pthread_cancel(thread_ofed_id_write);
			pthread_join(thread_ofed_id_write, NULL);
		}
		flag_thread_write_running = false;

		if (thread_ofed_id_run) {
			pthread_cancel(thread_ofed_id_run);
			pthread_join(thread_ofed_id_run, NULL);
		}
		flag_thread_run_running = false;

		flag_infiniband_accounting_shutdown = true;
	}

	return NULL;
}

static int _get_task_infiniband(void)
{
	/* if we want to store data on slurm DB 
	    read values from pipe here following the 
	   method used for acct_gather_energy/ipmi plugin
	*/
	
	return SLURM_SUCCESS;
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
	debug_flags = slurm_get_debug_flags();

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	flag_infiniband_accounting_shutdown = true;
	time_t begin_fini = time(NULL);

	while (flag_thread_run_running || flag_thread_write_running) {
		if ((time(NULL) - begin_fini) > (IB_FREQ + 1)) {
			error("Infiniband threads not finilized in time"
			    "Exit plugin without finalizing threads.");
			if (thread_ofed_id_write) {
				pthread_cancel(thread_ofed_id_write);
				pthread_join(thread_ofed_id_write, NULL);
			}
			if (thread_ofed_id_run) {
				pthread_cancel(thread_ofed_id_run);
				pthread_join(thread_ofed_id_run, NULL);
			}
			break;
		}
		_task_sleep(1);
		//wait for thread stop
	}
	return SLURM_SUCCESS;
}

/* Notes: IB semantics is to cap counters if count has exceeded limits.
 * Therefore we must check for overflows and cap the counters if necessary.
 *
 * mad_decode_field and mad_encode_field assume 32 bit integers passed in
 * for fields < 32 bits in length.
 */

extern int acct_gather_infiniband_p_node_init(void)
{
	int rc = SLURM_SUCCESS;

	if (!flag_update_started) {
		pthread_attr_t attr;
		uint32_t profile;

		flag_update_started = true;

		acct_gather_profile_g_get(ACCT_GATHER_PROFILE_RUNNING,
					  &profile);

		if (!(profile & ACCT_GATHER_PROFILE_NETWORK))
			return rc;

		debug("INFINIBAND: entered thread_launcher");
		slurm_attr_init(&attr);
		if (pthread_create(&thread_ofed_id_launcher, &attr,
				   &_thread_launcher, NULL)) {
			debug("infiniband accounting failed to create "
			      "_thread_launcher thread: %m");
			rc = SLURM_ERROR;
		}
		slurm_attr_destroy(&attr);

		if (debug_flags & DEBUG_FLAG_INFINIBAND)
			info("%s thread launched", plugin_name);
	}

	return rc;
}


extern void acct_gather_infiniband_p_conf_set(s_p_hashtbl_t *tbl)
{
	if (tbl) {
		s_p_get_uint32(&ofed_conf.freq,
			       "InfinibandOFEDFrequency", tbl);
		if (!s_p_get_uint32(&ofed_conf.port,
				    "InfinibandOFEDPort", tbl))
			ofed_conf.port = INFINIBAND_DEFAULT_PORT;
	}

	if (!_run_in_daemon())
		return;

	verbose("%s loaded", plugin_name);

}

extern void acct_gather_infiniband_p_conf_options(s_p_options_t **full_options,
						  int *full_options_cnt)
{
	s_p_options_t options[] = {
		{"InfinibandOFEDFrequency", S_P_UINT32},
		{"InfinibandOFEDPort", S_P_UINT32},
		{NULL} };

	transfer_s_p_options(full_options, options, full_options_cnt);
	return;
}
