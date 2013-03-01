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
#include "src/common/fd.h"
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
	uint64_t val;
	struct {
		uint32_t low;
		uint32_t high;
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
static uint32_t debug_flags = 0;

/* one cpu in the package */
static int pkg2cpu[MAX_PKGS] = {[0 ... MAX_PKGS-1] -1};
static int pkg_fd[MAX_PKGS] = {[0 ... MAX_PKGS-1] -1};


static int nb_pkg = 0;

static char *_msr_string(int which)
{
	if (which == MSR_RAPL_POWER_UNIT)
		return "PowerUnit";
	else if (which == MSR_PKG_POWER_INFO)
		return "PowerInfo";
	return "UnknownType";
}

static uint64_t _read_msr(int fd, int which)
{
	uint64_t data = 0;

	if (lseek(fd, which, SEEK_SET) < 0)
		error("lseek of /dev/cpu/#/msr: %m");
	if (read(fd, &data, sizeof(data)) != sizeof(data)) {
		if (which == MSR_DRAM_ENERGY_STATUS) {
			if (debug_flags & DEBUG_FLAG_ENERGY)
				info("It appears you don't have any DRAM, "
				     "this can be common.  Check your system "
				     "if you think this is in error.");
		} else {
			debug("Check if your CPU has RAPL support for %s: %m",
			      _msr_string(which));
		}
	}
	return data;
}

static uint64_t _get_package_energy(int pkg)
{
	uint64_t result;

	/* MSR_PKG_ENERGY_STATUS
	 * Total Energy Consumed - bits 31:0
	 * Reserved - bits 63:32
	 * See: Intel 64 and IA-32 Architectures Software Developer's
	 * Manual, Volume 3 for details */
	result = _read_msr(pkg_fd[pkg], MSR_PKG_ENERGY_STATUS);
	result &= 0xffffffff;
	if (result < package_energy[pkg].i.low)
		package_energy[pkg].i.high++;
	package_energy[pkg].i.low = result;
	return(package_energy[pkg].val);
}

static uint64_t _get_dram_energy(int pkg)
{
	uint64_t result;

	/* MSR_DRAM_ENERGY_STATUS
	 * Total Energy Consumed - bits 31:0
	 * Reserved - bits 63:32
	 * See: Intel 64 and IA-32 Architectures Software Developer's
	 * Manual, Volume 3 for details */
	result = _read_msr(pkg_fd[pkg], MSR_DRAM_ENERGY_STATUS);
	result &= 0xffffffff;
	if (result < dram_energy[pkg].i.low)
		dram_energy[pkg].i.high++;
	dram_energy[pkg].i.low = result;
	return(dram_energy[pkg].val);
}

static int _open_msr(int core)
{
	char msr_filename[BUFSIZ];
	int fd;

	sprintf(msr_filename, "/dev/cpu/%d/msr", core);
	fd = open(msr_filename, O_RDONLY);

	if (fd < 0) {
		if ( errno == ENXIO ) {
			error("No CPU %d", core);
		} else if ( errno == EIO ) {
			error("CPU %d doesn't support MSRs", core);
		} else
			error("MSR register problem: %m");
	} else {
		/* If this is loaded in the slurmd we need to make sure it
		   gets closed when a slurmstepd launches.
		*/
		fd_set_close_on_exec(fd);
	}

	return fd;
}

static void _hardware(void)
{
	char buf[1024];
	FILE *fd;
	int cpu, pkg;

	if ((fd = fopen("/proc/cpuinfo", "r")) == 0)
		error("fopen");
	while (fgets(buf, 1024, fd)) {
		if (strncmp(buf, "processor", sizeof("processor") - 1) == 0) {
			sscanf(buf, "processor\t: %d", &cpu);
			continue;
		}
		if (!strncmp(buf, "physical id", sizeof("physical id") - 1)) {
			sscanf(buf, "physical id\t: %d", &pkg);

			if (pkg > MAX_PKGS)
				fatal("Slurm can only handle %d sockets for "
				      "rapl, you seem to have more than that.  "
				      "Update src/plugins/acct_gather_energy/"
				      "rapl/acct_gather_energy_rapl.h "
				      "(MAX_PKGS) and recompile.", MAX_PKGS);
			if (pkg2cpu[pkg] == -1) {
				nb_pkg++;
				pkg2cpu[pkg] = cpu;
			}
			continue;
		}
	}
	fclose(fd);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("RAPL Found: %d packages", nb_pkg);
}

extern int acct_gather_energy_p_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;
	int i;
	double energy_units;
	uint64_t result;
	double ret;

	if (local_energy->current_watts == NO_VAL)
		return rc;
	acct_gather_energy_shutdown = false;
	if (!acct_gather_energy_shutdown) {
		uint32_t node_current_energy;
		uint16_t node_freq;

		xassert(pkg_fd[0] != -1);

		/* MSR_RAPL_POWER_UNIT
		 * Power Units - bits 3:0
		 * Energy Status Units - bits 12:8
		 * Time Units - bits 19:16
		 * See: Intel 64 and IA-32 Architectures Software Developer's
		 * Manual, Volume 3 for details */
		result = _read_msr(pkg_fd[0], MSR_RAPL_POWER_UNIT);
		energy_units = pow(0.5,(double)((result>>8)&0x1f));
		result = 0;
		for (i = 0; i < nb_pkg; i++)
			result += _get_package_energy(i) + _get_dram_energy(i);
		ret = (double)result * energy_units;

		/* current_watts = the average power consumption between two
		 *		   measurements
		 * base_watts = base energy consumed
		 */
		node_current_energy = (int)ret;
		if (local_energy->consumed_energy != 0) {
			local_energy->consumed_energy =
				node_current_energy - local_energy->base_watts;
			local_energy->current_watts =
				node_current_energy -
				local_energy->previous_consumed_energy;
			node_freq = slurm_get_acct_gather_node_freq();
			if (node_freq)	/* Prevent divide by zero */
				local_energy->current_watts /= (float)node_freq;
		}
		if (local_energy->consumed_energy == 0) {
			local_energy->consumed_energy = 1;
			local_energy->base_watts = node_current_energy;
		}
		local_energy->previous_consumed_energy = node_current_energy;

		if (debug_flags & DEBUG_FLAG_ENERGY) {
			info("_getjoules_rapl = %d sec, current %.6f Joules, "
			     "consumed %d",
			     freq, ret, local_energy->consumed_energy);
		}
	}

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("_getjoules_rapl shutdown");
	return rc;
}

static void _get_joules_task(acct_gather_energy_t *energy)
{
	int i;
	double energy_units, power_units;
	uint64_t result;
	ulong max_power;
	double ret;

	xassert(pkg_fd[0] != -1);

	/* MSR_RAPL_POWER_UNIT
	 * Power Units - bits 3:0
	 * Energy Status Units - bits 12:8
	 * Time Units - bits 19:16
	 * See: Intel 64 and IA-32 Architectures Software Developer's
	 * Manual, Volume 3 for details */
	result = _read_msr(pkg_fd[0], MSR_RAPL_POWER_UNIT);
	power_units = pow(0.5, (double)(result&0xf));
	energy_units = pow(0.5, (double)((result>>8)&0x1f));
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("RAPL powercapture_debug Energy units = %.6f, "
		     "Power Units = %.6f", energy_units, power_units);

	/* MSR_PKG_POWER_INFO
	 * Thermal Spec Power - bits 14:0
	 * Minimum Power - bits 30:16
	 * Maximum Power - bits 46:32
	 * Maximum Time Window - bits 53:48
	 * See: Intel 64 and IA-32 Architectures Software Developer's
	 * Manual, Volume 3 for details */
	result = _read_msr(pkg_fd[0], MSR_PKG_POWER_INFO);
	max_power = power_units * ((result >> 32) & 0x7fff);
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("RAPL Max power = %ld w", max_power);

	result = 0;
	for (i = 0; i < nb_pkg; i++)
		result += _get_package_energy(i) + _get_dram_energy(i);
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("RAPL Result = %"PRIu64"", result);
	ret = (double)result * energy_units;
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("RAPL Result float %.6f Joules", ret);

	if (energy->consumed_energy != 0) {
		energy->consumed_energy =  ret - energy->base_consumed_energy;
	}
	if (energy->consumed_energy == 0) {
		energy->consumed_energy = 1;
		energy->base_consumed_energy = ret;
	}

	if (debug_flags & DEBUG_FLAG_ENERGY) {
		info("_get_joules_task energy = %.6f, base %u , current %u",
		     ret, energy->base_consumed_energy,
		     energy->consumed_energy);
	}
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	int i;
	uint64_t result;

	_hardware();
	for (i = 0; i < nb_pkg; i++)
		pkg_fd[i] = _open_msr(pkg2cpu[i]);

	local_energy = acct_gather_energy_alloc();

	result = _read_msr(pkg_fd[0], MSR_RAPL_POWER_UNIT);
	if (result == 0)
		local_energy->current_watts = NO_VAL;

	debug_flags = slurm_get_debug_flags();
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	int i;

	for (i = 0; i < nb_pkg; i++) {
		if (pkg_fd[i] != -1) {
			close(pkg_fd[i]);
			pkg_fd[i] = -1;
		}
	}

	acct_gather_energy_destroy(local_energy);
	local_energy = NULL;
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_get_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int rc = SLURM_SUCCESS;
	switch (data_type) {
	case ENERGY_DATA_JOULES_TASK:
		if (local_energy->current_watts == NO_VAL)
			energy->consumed_energy = NO_VAL;
		else
			_get_joules_task(energy);
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
