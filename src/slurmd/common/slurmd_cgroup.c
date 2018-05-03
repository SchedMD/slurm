/***************************************************************************** \
 *  slurmd_cgroup.c - slurmd system cgroup management
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Martin Perry <martin.perry@bull.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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
#include "src/common/slurm_resource_info.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xstring.h"
#include "src/slurmd/common/xcgroup.h"
#include "src/slurmd/common/slurmd_cgroup.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

static xcgroup_t system_cpuset_cg = {NULL, NULL, NULL, 0, 0, 0};
static xcgroup_t system_memory_cg = {NULL, NULL, NULL, 0, 0, 0};

static bool cpuset_prefix_set = false;
static char *cpuset_prefix = "";

static xcgroup_ns_t cpuset_ns = {NULL, NULL, NULL};
static xcgroup_ns_t memory_ns = {NULL, NULL, NULL};

char cpuset_meta[PATH_MAX];

static char system_cgroup_path[PATH_MAX];

static slurm_cgroup_conf_t slurm_cgroup_conf;

static bool constrain_ram_space;
static bool constrain_swap_space;
static bool constrain_kmem_space;

static float allowed_ram_space;   /* Allowed RAM in percent       */
static float allowed_swap_space;  /* Allowed Swap percent         */

static uint64_t max_kmem;       /* Upper bound for kmem.limit_in_bytes  */
static uint64_t max_ram;        /* Upper bound for memory.limit_in_bytes  */
static uint64_t max_swap;       /* Upper bound for swap                   */
static uint64_t totalram;       /* Total real memory available on node    */
static uint64_t min_ram_space;  /* Don't constrain RAM below this value   */

static char* _system_cgroup_create_slurm_cg (xcgroup_ns_t* ns);

static int _xcgroup_cpuset_init(xcgroup_t* cg);

static uint64_t _percent_in_bytes (uint64_t mb, float percent)
{
	return ((mb * 1024 * 1024) * (percent / 100.0));
}

extern int init_system_cpuset_cgroup(void)
{
	int rc;
	int fstatus = SLURM_ERROR;
	char* cpus = NULL;
	size_t cpus_size;
	char* slurm_cgpath;
	xcgroup_t slurm_cg;

	/* read cgroup configuration */
	if (read_slurm_cgroup_conf(&slurm_cgroup_conf))
		return SLURM_ERROR;

	/* initialize cpuset cgroup namespace */
	if (xcgroup_ns_create(&slurm_cgroup_conf, &cpuset_ns, "", "cpuset")
	    != XCGROUP_SUCCESS) {
		error("system cgroup: unable to create cpuset namespace");
		free_slurm_cgroup_conf(&slurm_cgroup_conf);
		return SLURM_ERROR;
	}

	/* create slurm root cg in this cg namespace */
	slurm_cgpath = _system_cgroup_create_slurm_cg(&cpuset_ns);
	if ( slurm_cgpath == NULL ) {
		xcgroup_ns_destroy(&cpuset_ns);
		free_slurm_cgroup_conf(&slurm_cgroup_conf);
		return SLURM_ERROR;
	}

	/* check that this cgroup has cpus allowed or initialize them */
	if (xcgroup_load(&cpuset_ns, &slurm_cg, slurm_cgpath)
	    != XCGROUP_SUCCESS) {
		error("system cgroup: unable to load slurm cpuset xcgroup");
		xfree(slurm_cgpath);
		xcgroup_ns_destroy(&cpuset_ns);
		free_slurm_cgroup_conf(&slurm_cgroup_conf);
		return SLURM_ERROR;
	}

again:
	snprintf(cpuset_meta, sizeof(cpuset_meta), "%scpus", cpuset_prefix);
	rc = xcgroup_get_param(&slurm_cg, cpuset_meta, &cpus, &cpus_size);
	if (rc != XCGROUP_SUCCESS || cpus_size == 1) {
		if (!cpuset_prefix_set && (rc != XCGROUP_SUCCESS)) {
			cpuset_prefix_set = 1;
			cpuset_prefix = "cpuset.";
			goto again;
		}

		/* initialize the cpusets as it was nonexistent */
		if (_xcgroup_cpuset_init(&slurm_cg) != XCGROUP_SUCCESS) {
			xfree(slurm_cgpath);
			xcgroup_destroy(&slurm_cg);
			xcgroup_ns_destroy(&cpuset_ns);
			free_slurm_cgroup_conf(&slurm_cgroup_conf);
			xfree(cpus);
			return SLURM_ERROR;
		}
	}
	xcgroup_destroy(&slurm_cg);
	xfree(cpus);

	/* build system cgroup relative path */
	snprintf(system_cgroup_path, PATH_MAX, "%s/system", slurm_cgpath);
	xfree(slurm_cgpath);

	/* create system cgroup in the cpuset ns */
	if (xcgroup_create(&cpuset_ns, &system_cpuset_cg, system_cgroup_path,
			   getuid(),getgid()) != XCGROUP_SUCCESS) {
		goto error;
	}
	if (xcgroup_instantiate(&system_cpuset_cg) != XCGROUP_SUCCESS) {
		goto error;
	}
	if (_xcgroup_cpuset_init(&system_cpuset_cg) != XCGROUP_SUCCESS) {
		goto error;
	}

	free_slurm_cgroup_conf(&slurm_cgroup_conf);
	debug("system cgroup: system cpuset cgroup initialized");
	return SLURM_SUCCESS;

error:
	xcgroup_unlock(&system_cpuset_cg);
	xcgroup_destroy(&system_cpuset_cg);
	xcgroup_ns_destroy(&cpuset_ns);
	free_slurm_cgroup_conf(&slurm_cgroup_conf);
	return fstatus;
}

extern int init_system_memory_cgroup(void)
{
	int fstatus = SLURM_ERROR;
	char* slurm_cgpath;

	/* read cgroup configuration */
	if (read_slurm_cgroup_conf(&slurm_cgroup_conf))
		return SLURM_ERROR;

	/* initialize memory cgroup namespace */
	if (xcgroup_ns_create(&slurm_cgroup_conf, &memory_ns, "", "memory")
	    != XCGROUP_SUCCESS) {
		error("system cgroup: unable to create memory namespace");
		free_slurm_cgroup_conf(&slurm_cgroup_conf);
		return SLURM_ERROR;
	}

	constrain_kmem_space = slurm_cgroup_conf.constrain_kmem_space;
	constrain_ram_space = slurm_cgroup_conf.constrain_ram_space;
	constrain_swap_space = slurm_cgroup_conf.constrain_swap_space;

	/*
	 * as the swap space threshold will be configured with a
	 * mem+swp parameter value, if RAM space is not monitored,
	 * set allowed RAM space to 100% of the job requested memory.
	 * It will help to construct the mem+swp value that will be
	 * used for both mem and mem+swp limit during memcg creation.
	 */
	if ( constrain_ram_space )
		allowed_ram_space = slurm_cgroup_conf.allowed_ram_space;
	else
		allowed_ram_space = 100.0;

	allowed_swap_space = slurm_cgroup_conf.allowed_swap_space;

	if ((totalram = (uint64_t) conf->real_memory_size) == 0)
		error ("system cgroup: Unable to get RealMemory size");

	max_kmem = _percent_in_bytes(totalram, slurm_cgroup_conf.max_kmem_percent);
	max_ram = _percent_in_bytes(totalram, slurm_cgroup_conf.max_ram_percent);
	max_swap = _percent_in_bytes(totalram, slurm_cgroup_conf.max_swap_percent);
	max_swap += max_ram;
	min_ram_space = slurm_cgroup_conf.min_ram_space * 1024 * 1024;

	debug ("system cgroup: memory: total:%luM allowed:%.4g%%(%s), "
	       "swap:%.4g%%(%s), max:%.4g%%(%luM) "
	       "max+swap:%.4g%%(%luM) min:%luM "
	       "kmem:%.4g%%(%luM %s) min:%luM",
	       (unsigned long) totalram,
	       allowed_ram_space,
	       constrain_ram_space?"enforced":"permissive",

	       allowed_swap_space,
	       constrain_swap_space?"enforced":"permissive",
	       slurm_cgroup_conf.max_ram_percent,
	       (unsigned long) (max_ram/(1024*1024)),

	       slurm_cgroup_conf.max_swap_percent,
	       (unsigned long) (max_swap/(1024*1024)),
	       (unsigned long) slurm_cgroup_conf.min_ram_space,

	       slurm_cgroup_conf.max_kmem_percent,
	       (unsigned long)(max_kmem/(1024*1024)),
	       constrain_kmem_space?"enforced":"permissive",
	       (unsigned long) slurm_cgroup_conf.min_kmem_space);

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

	/* create slurm root cg in this cg namespace */
	slurm_cgpath = _system_cgroup_create_slurm_cg(&memory_ns);
	if ( slurm_cgpath == NULL ) {
		xcgroup_ns_destroy(&memory_ns);
		free_slurm_cgroup_conf(&slurm_cgroup_conf);
		return SLURM_ERROR;
	}

	/* build system cgroup relative path */
	snprintf(system_cgroup_path, PATH_MAX, "%s/system", slurm_cgpath);
	xfree(slurm_cgpath);

	/* create system cgroup in the cpuset ns */
	if (xcgroup_create(&memory_ns, &system_memory_cg,
			   system_cgroup_path,
			   getuid(), getgid()) != XCGROUP_SUCCESS) {
		goto error;
	}
	if (xcgroup_instantiate(&system_memory_cg) != XCGROUP_SUCCESS) {
		goto error;
	}

	if ( xcgroup_set_param(&system_memory_cg, "memory.use_hierarchy", "1")
	     != XCGROUP_SUCCESS ) {
		error("system cgroup: unable to ask for hierarchical accounting"
		      "of system memcg '%s'", system_memory_cg.path);
		goto error;
	}

	free_slurm_cgroup_conf(&slurm_cgroup_conf);
	debug("system cgroup: system memory cgroup initialized");
	return SLURM_SUCCESS;

error:
	xcgroup_unlock(&system_memory_cg);
	xcgroup_destroy(&system_memory_cg);
	xcgroup_ns_destroy(&memory_ns);
	free_slurm_cgroup_conf(&slurm_cgroup_conf);
	return fstatus;
}

extern void fini_system_cgroup(void)
{
	xcgroup_destroy(&system_cpuset_cg);
	xcgroup_destroy(&system_memory_cg);
	xcgroup_ns_destroy(&cpuset_ns);
	xcgroup_ns_destroy(&memory_ns);
}

static char* _system_cgroup_create_slurm_cg (xcgroup_ns_t* ns)
{
	/* we do it here as we do not have access to the conf structure */
	/* in libslurm (src/common/xcgroup.c) */
	xcgroup_t slurm_cg;
	char* pre = (char*) xstrdup(slurm_cgroup_conf.cgroup_prepend);

#ifdef MULTIPLE_SLURMD
	if ( conf->node_name != NULL )
		xstrsubstitute(pre, "%n", conf->node_name);
	else {
		xfree(pre);
		pre = (char*) xstrdup("/slurm");
	}
#endif

	/* create slurm cgroup in the ns */
	if (xcgroup_create(ns, &slurm_cg, pre,
			   getuid(), getgid()) != XCGROUP_SUCCESS) {
		xfree(pre);
		return pre;
	}
	if (xcgroup_instantiate(&slurm_cg) != XCGROUP_SUCCESS) {
		error("system cgroup: unable to build slurm cgroup for "
		      "ns %s: %m",
		      ns->subsystems);
		xcgroup_destroy(&slurm_cg);
		xfree(pre);
		return pre;
	}
	else {
		debug3("system cgroup: slurm cgroup %s successfully created "
		       "for ns %s: %m",
		       pre, ns->subsystems);
		xcgroup_destroy(&slurm_cg);
	}

	return pre;
}

/* when cgroups are configured with cpuset, at least
 * cpuset.cpus and cpuset.mems must be set or the cgroup
 * will not be available at all.
 * we duplicate the ancestor configuration in the init step */
static int _xcgroup_cpuset_init(xcgroup_t* cg)
{
	int fstatus, i;

	char* cpuset_metafiles[] = {
		"cpus",
		"mems"
	};
	char* cpuset_conf = NULL;
	size_t csize = 0;

	xcgroup_t acg;
	char* acg_name = NULL;
	char* p;

	fstatus = XCGROUP_ERROR;

	/* load ancestor cg */
	acg_name = (char*) xstrdup(cg->name);
	p = xstrrchr(acg_name, '/');
	if (p == NULL) {
		debug2("system cgroup: unable to get ancestor path for "
		       "cpuset cg '%s' : %m", cg->path);
		xfree(acg_name);
		return fstatus;
	} else
		*p = '\0';
	if (xcgroup_load(cg->ns, &acg, acg_name) != XCGROUP_SUCCESS) {
		debug2("system cgroup: unable to load ancestor for "
		       "cpuset cg '%s' : %m", cg->path);
		xfree(acg_name);
		return fstatus;
	}
	xfree(acg_name);

	/* inherits ancestor params */
	for (i = 0 ; i < 2 ; i++) {
	again:
		snprintf(cpuset_meta, sizeof(cpuset_meta), "%s%s",
			 cpuset_prefix, cpuset_metafiles[i]);
		if (xcgroup_get_param(&acg ,cpuset_meta,
				      &cpuset_conf, &csize)
		    != XCGROUP_SUCCESS) {
			if (!cpuset_prefix_set) {
				cpuset_prefix_set = 1;
				cpuset_prefix = "cpuset.";
				goto again;
			}

			debug("system cgroup: assuming no cpuset cg "
			       "support for '%s'",acg.path);
			xcgroup_destroy(&acg);
			return fstatus;
		}
		if (csize > 0)
			cpuset_conf[csize-1] = '\0';
		if (xcgroup_set_param(cg,cpuset_meta, cpuset_conf)
		    != XCGROUP_SUCCESS) {
			debug("system cgroup: unable to write %s configuration "
			       "(%s) for cpuset cg '%s'",cpuset_meta,
			       cpuset_conf, cg->path);
			xcgroup_destroy(&acg);
			xfree(cpuset_conf);
			return fstatus;
		}
		xfree(cpuset_conf);
	}

	xcgroup_destroy(&acg);
	return XCGROUP_SUCCESS;
}

extern int set_system_cgroup_cpus(char *phys_cpu_str)
{
	snprintf(cpuset_meta, sizeof(cpuset_meta), "%scpus", cpuset_prefix);
	xcgroup_set_param(&system_cpuset_cg, cpuset_meta, phys_cpu_str);
	return SLURM_SUCCESS;
}

extern int set_system_cgroup_mem_limit(uint64_t mem_spec_limit)
{
	uint64_t mem_spec_bytes = mem_spec_limit * 1024 * 1024;
	xcgroup_set_uint64_param(&system_memory_cg, "memory.limit_in_bytes",
				 mem_spec_bytes);
	return SLURM_SUCCESS;
}

extern int disable_system_cgroup_mem_oom()
{
	/* 1: disables the oom killer */
	return xcgroup_set_uint64_param(&system_memory_cg, "memory.oom_control",
					1);
}

extern int attach_system_cpuset_pid(pid_t pid)
{
	if (xcgroup_add_pids(&system_cpuset_cg, &pid, 1) != XCGROUP_SUCCESS)
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

extern int attach_system_memory_pid(pid_t pid)
{
	if (xcgroup_add_pids(&system_memory_cg, &pid, 1) != XCGROUP_SUCCESS)
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

extern bool check_memspec_cgroup_job_confinement(void)
{
	char *task_plugin_type = NULL;
	bool status = false;

	if (read_slurm_cgroup_conf(&slurm_cgroup_conf))
		return false;
	task_plugin_type = slurm_get_task_plugin();
	if (slurm_cgroup_conf.constrain_ram_space &&
	    strstr(task_plugin_type, "cgroup"))
		status = true;
	xfree(task_plugin_type);
	free_slurm_cgroup_conf(&slurm_cgroup_conf);
	return status;
}

extern bool check_corespec_cgroup_job_confinement(void)
{
	char *task_plugin_type = NULL;
	bool status = false;

	if (read_slurm_cgroup_conf(&slurm_cgroup_conf))
		return false;
	task_plugin_type = slurm_get_task_plugin();
	if (slurm_cgroup_conf.constrain_cores &&
	    strstr(task_plugin_type, "cgroup"))
		status = true;
	xfree(task_plugin_type);
	free_slurm_cgroup_conf(&slurm_cgroup_conf);
	return status;
}

extern void attach_system_cgroup_pid(pid_t pid)
{
	char* slurm_cgpath;

	if (read_slurm_cgroup_conf(&slurm_cgroup_conf))
		return;

	slurm_cgpath = (char*) xstrdup(slurm_cgroup_conf.cgroup_prepend);
#ifdef MULTIPLE_SLURMD
	if ( conf->node_name != NULL )
		xstrsubstitute(slurm_cgpath,"%n", conf->node_name);
	else {
		xfree(slurm_cgpath);
		slurm_cgpath = (char*) xstrdup("/slurm");
	}
#endif
	xstrcat(slurm_cgpath,"/system");
	if (xcgroup_ns_load(&slurm_cgroup_conf, &cpuset_ns, "cpuset")
	    == XCGROUP_SUCCESS) {
		if (xcgroup_load(&cpuset_ns, &system_cpuset_cg, slurm_cgpath)
		    == XCGROUP_SUCCESS)
			if (attach_system_cpuset_pid(pid) != SLURM_SUCCESS)
				debug2("system cgroup: unable to attach pid to "
				       "system cpuset cgroup");
	}
	if (xcgroup_ns_load(&slurm_cgroup_conf, &memory_ns, "memory")
	    == XCGROUP_SUCCESS) {
		if (xcgroup_load(&memory_ns, &system_memory_cg, slurm_cgpath)
		    == XCGROUP_SUCCESS)
			if (attach_system_memory_pid(pid) != SLURM_SUCCESS)
				debug2("system cgroup: unable to attach pid to "
				       "system memory cgroup");
	}
	xfree(slurm_cgpath);
	free_slurm_cgroup_conf(&slurm_cgroup_conf);
	return;
}

