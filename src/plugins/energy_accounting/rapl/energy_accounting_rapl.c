/*****************************************************************************\
 *  energy_accounting_rapl.c - slurm energy accounting plugin for rapl.
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

/*   energy_accounting_rapl
 * This plugin does not initiate a node-level thread.
 * It will be used to load energy values from cpu/core
 * sensors when harware/drivers are available
 */


#include <fcntl.h>
#include <signal.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_jobacct_gather.h"
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
#include "energy_accounting_rapl.h"

union {
	ulong val;
	struct {
		uint low;
		uint high;
	} i;
} package_energy[MAX_PKGS], dram_energy[MAX_PKGS];

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
const char plugin_name[] = "Energy accounting RAPL plugin";
const char plugin_type[] = "energy_accounting/rapl";
const uint32_t plugin_version = 100;

static int freq = 0;
static float base_watts = 5; // MNP - arbitrary value for testing only
static float current_watts = 11; // MNP - arbitrary value for testing only
static float energy_calibration= 1.0;
static bool energy_accounting_shutdown = true;
static uint32_t last_time = 0;
static uint32_t node_consumed_energy = 0;
static uint32_t node_base_energy = 0;
static uint32_t node_current_energy = 0;

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
			return SLURM_ERROR;
		} else if ( errno == EIO ) {
			error("CPU %d doesn't support MSRs", core);
			pexit("msr");
			return SLURM_ERROR;
		} else
			pexit("msr");
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
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

static int *_getjoules_rapl(void)
{

	int ipmi_ret = 0;
	int rc, pkg, i;
	int core = 0;
	double energy_units, power_units;
	ulong result;
	ulong max_power;
	double ret;



	energy_accounting_shutdown = false;
	if (!energy_accounting_shutdown ) {
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
		if (node_consumed_energy != 0){
			node_consumed_energy =  
					node_current_energy - node_base_energy;
		}
		if (node_consumed_energy == 0){
			node_consumed_energy = 1;
			node_base_energy = node_current_energy;
		}
		debug2("_getjoules_rapl = %d sec, current %.6f Joules, "
		       "consumed %d", freq, ret, node_consumed_energy);
	}
	debug2("_getjoules_rapl shutdown");

	return NULL;
}


extern int energy_accounting_p_updatenodeenergy(void)
{
	int rc = SLURM_SUCCESS;

	/* The code needs to update the following variables as well:
	 *	base_watts
	 *	current_watts
	 */
	_getjoules_rapl();
	return rc;
}

extern void energy_accounting_p_getjoules_task(struct jobacctinfo *jobacct)
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

	if (jobacct->consumed_energy != 0) {
		jobacct->consumed_energy =  ret - jobacct->base_consumed_energy;
	}
	if (jobacct->consumed_energy == 0) {
		jobacct->consumed_energy = 1;
		jobacct->base_consumed_energy = ret;
	}

	debug2("getjoules_task energy = %.6f, base %u , current %u",
	       ret, jobacct->base_consumed_energy, jobacct->consumed_energy);

}

extern int energy_accounting_p_getjoules_scaled(uint32_t stp_smpled_time,
						ListIterator itr)
{
	return SLURM_SUCCESS;
}

extern int energy_accounting_p_setbasewatts(void)
{
	base_watts = 5; // MNP - arbitrary value for testing only
	return SLURM_SUCCESS;
}

extern int energy_accounting_p_readbasewatts(void)
{
	return base_watts;
}

extern uint32_t energy_accounting_p_getcurrentwatts(void)
{
	return current_watts;
}

extern uint32_t energy_accounting_p_getbasewatts()
{
	return base_watts;
}

extern uint32_t energy_accounting_p_getnodeenergy(uint32_t up_time)
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
