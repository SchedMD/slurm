/*****************************************************************************\
 *  acct_gather_energy_ipmi.c - slurm energy accounting plugin for ipmi.
 *****************************************************************************
 *  Written by Bull-HN-PHX/d.rusak,
 *  Copyright (C) 2012 Bull-HN-PHX.
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

/*   acct_gather_energy_ipmi
 * This plugin initiates a node-level thread to periodically
 * issue reads to a BMC over an ipmi interface driver 'kipmi0'
 *
 * TODO .. the library is under ../plugins/ipmi/lib and its
 * headers under ../plugins/ipmi/include
 * Currently the code is still using the 'getwatts' app.
 * This must be changed to utilize this library directly ..
 * including adjusting the makefiles for SLUrM building .
 */

#include <fcntl.h>
#include <signal.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"

//#include "src/plugins/acct_gather_energy/ipmi/ipmi_inttypes.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_intf.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_sdr.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_sel.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_mc.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_sensor.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_sol.h"


#define _DEBUG 1
#define _DEBUG_ENERGY 1

/* These are defined here so when we link with something other than
 * the slurmd we will have these symbols defined.  They will get
 * overwritten when linking with the slurmd.
 */
#if defined (__APPLE__)
uint32_t jobacct_job_id __attribute__((weak_import));
pthread_mutex_t jobacct_lock __attribute__((weak_import));
uint32_t jobacct_mem_limit __attribute__((weak_import));
uint32_t jobacct_step_id __attribute__((weak_import));
uint32_t jobacct_vmem_limit __attribute__((weak_import));
#else
uint32_t jobacct_job_id;
pthread_mutex_t jobacct_lock;
uint32_t jobacct_mem_limit;
uint32_t jobacct_step_id;
uint32_t jobacct_vmem_limit;
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

int csv_output = 0;
int ipmi_verbose = 0;

const char plugin_name[] = "AcctGatherEnergy IPMI plugin";
const char plugin_type[] = "acct_gather_energy/ipmi";
const uint32_t plugin_version = 100;
static int freq = 0;
static float base_watts = 5; // MNP - arbitrary value for testing only
static float current_watts = 11; // MNP - arbitrary value for testing only
static float energy_calibration= 1.0;
static bool acct_gather_energy_shutdown = true;
static double seed_joules = 74.0;
static	char		joules_file[32];
static uint32_t last_time = 0;
static uint32_t node_consumed_energy = 0;

static void _task_sleep(int rem)
{
	while (rem)
		rem = sleep(rem);	/* subject to interupt */
}

static int _update_weighted_energy(uint32_t step_sampled_cputime,
				   struct jobacctinfo *jobacct)
{
	return 0;
}

static int _readbasewatts(void)
{
	return 0;
}


extern int acct_gather_energy_p_updatenodeenergy(void)
{
	int rc = SLURM_SUCCESS;
	node_consumed_energy = 999; // MNP - arbitrary value for testing only
	return rc;
}


extern uint32_t acct_gather_energy_p_getjoules_task(struct jobacctinfo *jobacct)
{
	//TODO put joules acquisition here when available
	return 88; // MNP - arbitrary value for testing only
}


extern int acct_gather_energy_p_getjoules_scaled(uint32_t stp_smpled_time,
		ListIterator itr)
{
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_setbasewatts(void)
{
	base_watts = 5; // MNP - arbitrary value for testing only
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_readbasewatts(void)
{
	return base_watts;
}

extern uint32_t acct_gather_energy_p_getcurrentwatts(void)
{
	return current_watts;
}

extern uint32_t acct_gather_energy_p_getbasewatts()
{
	return base_watts;
}

extern uint32_t acct_gather_energy_p_getnodeenergy(uint32_t up_time)
{
	last_time = up_time;
	return node_consumed_energy;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}
