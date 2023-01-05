/***************************************************************************** \
 *  slurmd_cgroup.c - slurmd system cgroup management
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Martin Perry <martin.perry@bull.com>
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
\****************************************************************************/

#include "config.h"

#define _GNU_SOURCE
#include <ctype.h>
#include <limits.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"
#include "src/common/bitstring.h"
#include "src/common/read_config.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xstring.h"
#include "src/interfaces/cgroup.h"
#include "src/slurmd/common/slurmd_cgroup.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

extern int init_system_cpuset_cgroup(void)
{
	if (cgroup_g_initialize(CG_CPUS) != SLURM_SUCCESS)
		return SLURM_ERROR;

	return cgroup_g_system_create(CG_CPUS);
}

extern int init_system_memory_cgroup(void)
{
	if (cgroup_g_initialize(CG_MEMORY) != SLURM_SUCCESS)
		return SLURM_ERROR;

        /*
         *  Warning: OOM Killer must be disabled for slurmstepd
         *  or it would be destroyed if the application use
         *  more memory than permitted
         *
         *  If an env value is already set for slurmstepd
         *  OOM killer behavior, keep it, otherwise set the
         *  -1000 value, wich means do not let OOM killer kill it
         *
         *  FYI, setting "export SLURMSTEPD_OOM_ADJ=-1000"
         *  in /etc/sysconfig/slurm would be the same
         */
	 setenv("SLURMSTEPD_OOM_ADJ", "-1000", 0);

	 if (cgroup_g_system_create(CG_MEMORY) != SLURM_SUCCESS)
		 return SLURM_ERROR;

	 if (running_in_slurmd())
		 debug("system cgroup: system memory cgroup initialized");

	 return SLURM_SUCCESS;
}

extern void fini_system_cgroup(void)
{
	cgroup_g_system_destroy(CG_CPUS);
	cgroup_g_system_destroy(CG_MEMORY);
}

extern int set_system_cgroup_cpus(char *phys_cpu_str)
{
	cgroup_limits_t limits;
	int rc;

	cgroup_init_limits(&limits);
	limits.allow_cores = phys_cpu_str;
	rc = cgroup_g_constrain_set(CG_CPUS, CG_LEVEL_SYSTEM, &limits);

	return rc;
}

extern int set_system_cgroup_mem_limit(uint64_t mem_spec_limit)
{
	cgroup_limits_t limits;
	int rc;

	cgroup_init_limits(&limits);
	limits.limit_in_bytes = mem_spec_limit * 1024 * 1024;
	rc = cgroup_g_constrain_set(CG_MEMORY, CG_LEVEL_SYSTEM, &limits);

	return rc;
}

extern int attach_system_cpuset_pid(pid_t pid)
{
	return cgroup_g_system_addto(CG_CPUS, &pid, 1);
}

extern int attach_system_memory_pid(pid_t pid)
{
	return cgroup_g_system_addto(CG_MEMORY, &pid, 1);
}

extern bool check_corespec_cgroup_job_confinement(void)
{
	if ((conf->cpu_spec_list || conf->core_spec_cnt) &&
	    slurm_cgroup_conf.constrain_cores &&
	    xstrstr(slurm_conf.task_plugin, "cgroup"))
		return true;

	return false;
}

extern void attach_system_cgroup_pid(pid_t pid)
{
	if (check_corespec_cgroup_job_confinement() &&
	    (init_system_cpuset_cgroup() ||
	     cgroup_g_system_addto(CG_CPUS, &pid, 1)))
		error("%s: failed to add stepd pid %d to system cpuset cgroup",
		      __func__, pid);

	if (conf->mem_spec_limit && cgroup_memcg_job_confinement()) {
		if (init_system_memory_cgroup() ||
		    cgroup_g_system_addto(CG_MEMORY, &pid, 1))
			error("%s: failed to add stepd pid %d to system memory cgroup",
			      __func__, pid);
	}
}
