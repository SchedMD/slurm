/*****************************************************************************\
 *  acct_gather_energy_rapl.c - slurm energy accounting plugin for rapl.
 *****************************************************************************
 *  Copyright (C) 2012
 *  Written by Bull- Yiannis Georgiou
 *  CODE-OCEC-09-009. All rights reserved.
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

/*   acct_gather_energy_rapl
 * This plugin does not initiate a node-level thread.
 * It will be used to load energy values from cpu/core
 * sensors when harware/drivers are available
 */


#include <fcntl.h>
#include <signal.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/un.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include "acct_gather_energy_rapl.h"

union {
	ulong val;
	struct {
		uint low;
		uint high;
	} i;
} package_energy[MAX_PKGS], dram_energy[MAX_PKGS];

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
const char plugin_name[] = "AcctGatherEnergy RAPL plugin";
const char plugin_type[] = "acct_gather_energy/rapl";
const uint32_t plugin_version = 100;

static int freq = 0;
static acct_gather_energy_t *local_energy = NULL;
static bool acct_gather_energy_shutdown = true;
static uint32_t last_time = 0;

int pkg2cpu [MAX_PKGS] = {[0 ... MAX_PKGS-1] -1}; /* one cpu in the package */
int fd[MAX_PKGS] = {[0 ... MAX_PKGS-1] -1};

int nb_pkg = 0;

static ulong read_msr(int fd, int which) {
	uint64_t data;

	if (pread(fd, &data, sizeof data, which) != sizeof data) {
		error("Check your cpu has RAPL support");
		pexit("msr");
	}
	return (long long)data;
}

static ulong get_package_energy(int pkg)
{
	ulong result;
	result = read_msr(fd[pkg], MSR_PKG_ENERGY_STATUS);
	if (result < package_energy[pkg].i.low)
		package_energy[pkg].i.high++;
	package_energy[pkg].i.low = result;
	return(package_energy[pkg].val);
}

static ulong get_dram_energy(int pkg)
{
	ulong result;

	result = read_msr(fd[pkg], MSR_DRAM_ENERGY_STATUS);
	if (result < dram_energy[pkg].i.low)
		dram_energy[pkg].i.high++;
	dram_energy[pkg].i.low = result;
	return(dram_energy[pkg].val);
}

static int open_msr(int core) {
	char msr_filename[BUFSIZ];
	int fd;

	sprintf(msr_filename, "/dev/cpu/%d/msr", core);
	fd = open(msr_filename, O_RDONLY);
	if ( fd < 0 ) {
		if ( errno == ENXIO ) {
			error("No CPU %d", core);
			pexit("msr");
		} else if ( errno == EIO ) {
			error("CPU %d doesn't support MSRs", core);
			pexit("msr");
		} else
			pexit("msr");
	}
	return fd;
}

static void hardware (void) {
	char buf[1024];
	FILE *fd;
	int cpu, pkg;

	if ((fd = fopen("/proc/cpuinfo", "r")) == 0)
		pexit("fopen");
	while (0 != fgets(buf, 1024, fd)) {
		if (strncmp(buf, "processor", sizeof("processor") - 1) == 0) {
			sscanf(buf, "processor\t: %d", &cpu);
			continue;
		}
		if (strncmp(buf, "physical id", sizeof("physical id") - 1) == 0)
		{
			sscanf(buf, "physical id\t: %d", &pkg);
			if (pkg2cpu[pkg] == -1)
				nb_pkg++;
			pkg2cpu[pkg] = cpu;
			continue;
		}
	}
	debug4 ("RAPL Found: %d packages", nb_pkg);
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

extern int acct_gather_energy_p_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;
	int pkg, i;
	int core = 0;
	double energy_units;
	ulong result;
	double ret, ret_tmp;

	acct_gather_energy_shutdown = false;
	if (!acct_gather_energy_shutdown) {
		uint32_t node_current_energy;
		hardware();
		for (i = 0; i < nb_pkg; i++)
			fd[i] = open_msr(pkg2cpu[i]);

		result = read_msr(fd[0], MSR_RAPL_POWER_UNIT);
		energy_units = pow(0.5,(double)((result>>8)&0x1f));
		result = 0;
		for (i = 0; i < nb_pkg; i++)
			result += get_package_energy(i) + get_dram_energy(i);
		ret = (double)result*energy_units;

		node_current_energy = (int)ret;
		if (local_energy->consumed_energy != 0){
			local_energy->consumed_energy =
				node_current_energy - local_energy->base_watts;
		}
		if (local_energy->consumed_energy == 0){
			local_energy->consumed_energy = 1;
			local_energy->base_watts = node_current_energy;
		}

		sleep(1);
		result = 0;
		for (i = 0; i < nb_pkg; i++)
			result += get_package_energy(i) + get_dram_energy(i);
		ret_tmp = (double)result * energy_units;
		local_energy->current_watts = (float)(ret_tmp - ret);

		debug2("_getjoules_rapl = %d sec, current %.6f Joules, "
		       "consumed %d", freq, ret, local_energy->consumed_energy);
	}
	debug2("_getjoules_rapl shutdown");
	return rc;
}

static void _get_joules_task(acct_gather_energy_t *energy)
{
	int rc, pkg, i;
	int core = 0;
	double energy_units, power_units;
	ulong result;
	ulong max_power;
	double ret;

	hardware();
	for (i=0; i<nb_pkg; i++)
		fd[i] = open_msr(pkg2cpu[i]);

	result = read_msr(fd[0], MSR_RAPL_POWER_UNIT);
	power_units = pow(0.5, (double)(result&0xf));
	energy_units = pow(0.5, (double)((result>>8)&0x1f));
	debug2("RAPL powercapture_debug Energy units = %.6f, "
	       "Power Units = %.6f", energy_units, power_units);
	max_power = power_units *
		((read_msr(fd[0], MSR_PKG_POWER_INFO) >> 32) & 0x7fff);

	debug2("RAPL Max power = %ld w", max_power);
	result = 0;
	for (i = 0; i < nb_pkg; i++)
		result += get_package_energy(i) + get_dram_energy(i);
	debug2("RAPL Result = %lu ", result);
	ret = (double)result*energy_units;
	debug2("RAPL Result float %.6f Joules", ret);

	if (energy->consumed_energy != 0) {
		energy->consumed_energy =  ret - energy->base_consumed_energy;
	}
	if (energy->consumed_energy == 0) {
		energy->consumed_energy = 1;
		energy->base_consumed_energy = ret;
	}

	debug2("_get_joules_task energy = %.6f, base %u , current %u",
	       ret, energy->base_consumed_energy, energy->consumed_energy);

}

extern int acct_gather_energy_p_getjoules_scaled(uint32_t stp_smpled_time,
						 ListIterator itr)
{
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_setbasewatts(void)
{
	local_energy->base_watts = 0;
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_readbasewatts(void)
{
	return local_energy->base_watts;
}

extern uint32_t acct_gather_energy_p_getcurrentwatts(void)
{
	return local_energy->current_watts;
}

extern uint32_t acct_gather_energy_p_getbasewatts()
{
	return local_energy->base_watts;
}

extern uint32_t acct_gather_energy_p_getnodeenergy(uint32_t up_time)
{
	last_time = up_time;
	return local_energy->consumed_energy;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	local_energy = acct_gather_energy_alloc();
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	acct_gather_energy_destroy(local_energy);
	local_energy = NULL;
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_get_data(acct_gather_energy_t *energy,
					 enum acct_energy_type data_type)
{
	int rc = SLURM_SUCCESS;
	switch (data_type) {
	case ENERGY_DATA_JOULES_TASK:
		_get_joules_task(energy);
		break;
	case ENERGY_DATA_JOULES_SCALED:
		break;
	case ENERGY_DATA_CURR_WATTS:
		energy->current_watts = local_energy->current_watts;
		break;
	case ENERGY_DATA_BASE_WATTS:
		energy->base_watts = local_energy->base_watts;
		break;
	case ENERGY_DATA_NODE_ENERGY:
		energy->consumed_energy = local_energy->consumed_energy;
		break;
	case ENERGY_DATA_STRUCT:
		memcpy(energy, local_energy, sizeof(acct_gather_energy_t));
		break;
	default:
		error("acct_gather_energy_p_get_data: unknown enum %d",
		      data_type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int acct_gather_energy_p_set_data(acct_gather_energy_t *energy,
					 enum acct_energy_type data_type)
{
	int rc = SLURM_SUCCESS;

	switch (data_type) {
	case ENERGY_DATA_BASE_WATTS:
		local_energy->base_watts = 0;
		break;
	default:
		error("acct_gather_energy_p_set_data: unknown enum %d",
		      data_type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}
