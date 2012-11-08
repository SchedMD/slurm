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

const char plugin_name[] = "AcctGatherEnergy IPMI plugin";
const char plugin_type[] = "acct_gather_energy/ipmi";
const uint32_t plugin_version = 100;

static uint32_t debug_flags = 0;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	verbose("%s loaded", plugin_name);
	debug_flags = slurm_get_debug_flags();
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int acct_gather_energy_p_get_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int rc = SLURM_SUCCESS;
	switch (data_type) {
	case ENERGY_DATA_JOULES_TASK:
		break;
	case ENERGY_DATA_STRUCT:
		break;
	default:
		error("acct_gather_energy_p_get_data: unknown enum %d",
		      data_type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int acct_gather_energy_p_set_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int rc = SLURM_SUCCESS;

	switch (data_type) {
	case ENERGY_DATA_RECONFIG:
		debug_flags = slurm_get_debug_flags();
		break;
	default:
		error("acct_gather_energy_p_set_data: unknown enum %d",
		      data_type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}
