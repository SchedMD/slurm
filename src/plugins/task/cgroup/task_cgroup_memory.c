/***************************************************************************** \
 *  task_cgroup_memory.c - memory cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <stdlib.h>		/* getenv     */

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/common/xstring.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xcgroup.h"

#include "task_cgroup.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

extern slurmd_conf_t *conf;

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];

static xcgroup_ns_t memory_ns;

static xcgroup_t user_memory_cg;
static xcgroup_t job_memory_cg;
static xcgroup_t step_memory_cg;

static float allowed_ram_space;   /* Allowed RAM in percent       */
static float allowed_swap_space;  /* Allowed Swap percent         */

static uint64_t max_ram;        /* Upper bound for memory.limit_in_bytes  */
static uint64_t max_swap;       /* Upper bound for swap                   */
static uint64_t totalram;       /* Total real memory available on node    */
static uint64_t min_ram_space;  /* Don't constrain RAM below this value       */

static uint64_t percent_in_bytes (uint64_t mb, float percent)
{
	return ((mb * 1024 * 1024) * (percent / 100.0));
}

extern int task_cgroup_memory_init(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	char release_agent_path[PATH_MAX];

	/* initialize user/job/jobstep cgroup relative paths */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	/* initialize memory cgroup namespace */
	release_agent_path[0]='\0';
	if (snprintf(release_agent_path,PATH_MAX,"%s/release_memory",
		      slurm_cgroup_conf->cgroup_release_agent) >= PATH_MAX) {
		error("task/cgroup: unable to build memory release agent path");
		goto error;
	}
	if (xcgroup_ns_create(slurm_cgroup_conf, &memory_ns, "/memory", "",
			       "memory",release_agent_path) !=
	     XCGROUP_SUCCESS) {
		error("task/cgroup: unable to create memory namespace");
		goto error;
	}

	/* check that memory cgroup namespace is available */
	if (! xcgroup_ns_is_available(&memory_ns)) {
		if (slurm_cgroup_conf->cgroup_automount) {
			if (xcgroup_ns_mount(&memory_ns)) {
				error("task/cgroup: unable to mount memory "
				      "namespace");
				goto clean;
			}
			info("task/cgroup: memory namespace is now mounted");
		} else {
			error("task/cgroup: memory namespace not mounted. "
			      "aborting");
			goto clean;
		}
	}

	allowed_ram_space = slurm_cgroup_conf->allowed_ram_space;
	allowed_swap_space = slurm_cgroup_conf->allowed_swap_space;

	if ((totalram = (uint64_t) conf->real_memory_size) == 0)
		error ("task/cgroup: Unable to get RealMemory size");

	max_ram = percent_in_bytes(totalram, slurm_cgroup_conf->max_ram_percent);
	max_swap = percent_in_bytes(totalram, slurm_cgroup_conf->max_swap_percent);
	max_swap += max_ram;
	min_ram_space = slurm_cgroup_conf->min_ram_space * 1024 * 1024;

	debug ("task/cgroup/memory: total:%luM allowed:%.4g%%, swap:%.4g%%, "
	      "max:%.4g%%(%luM) max+swap:%.4g%%(%luM) min:%uM",
	      (unsigned long) totalram,
	      allowed_ram_space,
	      allowed_swap_space,
	      slurm_cgroup_conf->max_ram_percent,
	      (unsigned long) (max_ram/(1024*1024)),
	      slurm_cgroup_conf->max_swap_percent,
	      (unsigned long) (max_swap/(1024*1024)),
	      (unsigned) slurm_cgroup_conf->min_ram_space);

        /*
         *  Warning: OOM Killer must be disabled for slurmstepd
         *  or it would be destroyed if the application use
         *  more memory than permitted
         *
         *  If an env value is already set for slurmstepd
         *  OOM killer behavior, keep it, otherwise set the
         *  -17 value, wich means do not let OOM killer kill it
         *
         *  FYI, setting "export SLURMSTEPD_OOM_ADJ=-17"
         *  in /etc/sysconfig/slurm would be the same
         */
        setenv("SLURMSTEPD_OOM_ADJ","-17",0);

	return SLURM_SUCCESS;

clean:
	xcgroup_ns_destroy(&memory_ns);

error:
	return SLURM_ERROR;
}

extern int task_cgroup_memory_fini(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	xcgroup_t memory_cg;

	if (user_cgroup_path[0] == '\0' ||
	     job_cgroup_path[0] == '\0' ||
	     jobstep_cgroup_path[0] == '\0')
		return SLURM_SUCCESS;

	/*
	 * Move the slurmstepd back to the root memory cg and force empty
	 * the step cgroup to move its allocated pages to its parent.
	 * The release_agent will asynchroneously be called for the step
	 * cgroup. It will do the necessary cleanup.
	 * It should be good if this force_empty mech could be done directly
	 * by the memcg implementation at the end of the last task managed
	 * by a cgroup. It is too difficult and near impossible to handle
	 * that cleanup correctly with current memcg.
	 */
	if (xcgroup_create(&memory_ns,&memory_cg,"",0,0) == XCGROUP_SUCCESS) {
		xcgroup_set_uint32_param(&memory_cg,"tasks",getpid());
		xcgroup_destroy(&memory_cg);
		xcgroup_set_param(&step_memory_cg,"memory.force_empty","1");
	}

	xcgroup_destroy(&user_memory_cg);
	xcgroup_destroy(&job_memory_cg);
	xcgroup_destroy(&step_memory_cg);

	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	xcgroup_ns_destroy(&memory_ns);

	return SLURM_SUCCESS;
}

/*
 *  Return configured memory limit in bytes given a memory limit in MB.
 */
static uint64_t mem_limit_in_bytes (uint64_t mem)
{
	/* 
	 *  If mem == 0 then assume there was no SLURM limit imposed
	 *   on the amount of memory for job or step. Use the total
	 *   amount of available RAM instead.
	 */
	if (mem == 0)
		mem = totalram * 1024 * 1024;
	else
		mem = percent_in_bytes (mem, allowed_ram_space);
	if (mem < min_ram_space)
		return (min_ram_space);
	if (mem > max_ram)
		return (max_ram);
	return (mem);
}

/*
 *  Return configured swap limit in bytes given a memory limit in MB.
 *
 *   Swap limit is calculated as:
 *
 *     mem_limit_in_bytes + (configured_swap_percent * allocated_mem_in_bytes)
 */
static uint64_t swap_limit_in_bytes (uint64_t mem)
{
	uint64_t swap;
	/*
	 *  If mem == 0 assume "unlimited" and use totalram.
	 */
	swap = percent_in_bytes (mem ? mem : totalram, allowed_swap_space);
	mem = mem_limit_in_bytes (mem) + swap;
	if (mem < min_ram_space)
		return (min_ram_space);
	if (mem > max_swap)
		return (max_swap);
	return (mem);
}

static int memcg_initialize (xcgroup_ns_t *ns, xcgroup_t *cg,
		char *path, uint64_t mem_limit, uid_t uid, gid_t gid)
{
	uint64_t mlb = mem_limit_in_bytes (mem_limit);
	uint64_t mls = swap_limit_in_bytes  (mem_limit);

	if (xcgroup_create (ns, cg, path, uid, gid) != XCGROUP_SUCCESS)
		return -1;

	if (xcgroup_instanciate (cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy (cg);
		return -1;
	}

	xcgroup_set_param (cg, "memory.use_hierarchy","1");
	xcgroup_set_uint64_param (cg, "memory.limit_in_bytes", mlb);
	xcgroup_set_uint64_param (cg, "memory.memsw.limit_in_bytes", mls);

	info ("task/cgroup: %s: alloc=%luMB mem.limit=%luMB memsw.limit=%luMB",
		path,
		(unsigned long) mem_limit,
		(unsigned long) mlb/(1024*1024),
		(unsigned long) mls/(1024*1024));

	return 0;
}

extern int task_cgroup_memory_create(slurmd_job_t *job)
{
	int rc;
	int fstatus = SLURM_ERROR;

	xcgroup_t memory_cg;

	uint32_t jobid = job->jobid;
	uint32_t stepid = job->stepid;
	uid_t uid = job->uid;
	gid_t gid = job->gid;
	pid_t pid;

	char* slurm_cgpath ;

	/* create slurm root cg in this cg namespace */
	slurm_cgpath = task_cgroup_create_slurm_cg(&memory_ns);
	if ( slurm_cgpath == NULL ) {
		return SLURM_ERROR;
	}

	/* build user cgroup relative path if not set (should not be) */
	if (*user_cgroup_path == '\0') {
		if (snprintf(user_cgroup_path, PATH_MAX,
			     "%s/uid_%u", slurm_cgpath, uid) >= PATH_MAX) {
			error("unable to build uid %u cgroup relative "
			      "path : %m", uid);
			xfree(slurm_cgpath);
			return SLURM_ERROR;
		}
	}
	xfree(slurm_cgpath);

	/* build job cgroup relative path if no set (should not be) */
	if (*job_cgroup_path == '\0') {
		if (snprintf(job_cgroup_path,PATH_MAX,"%s/job_%u",
			      user_cgroup_path,jobid) >= PATH_MAX) {
			error("task/cgroup: unable to build job %u memory "
			      "cg relative path : %m",jobid);
			return SLURM_ERROR;
		}
	}

	/* build job step cgroup relative path (should not be) */
	if (*jobstep_cgroup_path == '\0') {
		if (snprintf(jobstep_cgroup_path,PATH_MAX,"%s/step_%u",
			      job_cgroup_path,stepid) >= PATH_MAX) {
			error("task/cgroup: unable to build job step %u memory "
			      "cg relative path : %m",stepid);
			return SLURM_ERROR;
		}
	}

	/*
	 * create memory root cg and lock it
	 *
	 * we will keep the lock until the end to avoid the effect of a release
	 * agent that would remove an existing cgroup hierarchy while we are
	 * setting it up. As soon as the step cgroup is created, we can release
	 * the lock.
	 * Indeed, consecutive slurm steps could result in cg being removed
	 * between the next EEXIST instanciation and the first addition of
	 * a task. The release_agent will have to lock the root memory cgroup
	 * to avoid this scenario.
	 */
	if (xcgroup_create(&memory_ns,&memory_cg,"",0,0) != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to create root memory xcgroup");
		return SLURM_ERROR;
	}
	if (xcgroup_lock(&memory_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&memory_cg);
		error("task/cgroup: unable to lock root memory cg");
		return SLURM_ERROR;
	}

	/*
	 * Create user cgroup in the memory ns (it could already exist)
	 * Ask for hierarchical memory accounting starting from the user
	 * container in order to track the memory consumption up to the
	 * user.
	 * We do not set any limits at this level for now. It could be
	 * interesting to do it in the future but memcg cleanup mech
	 * are not working well so it will be really difficult to manage
	 * addition/removal of memory amounts at this level. (kernel 2.6.34)
	 */
	if (xcgroup_create(&memory_ns,&user_memory_cg,
			    user_cgroup_path,
			    getuid(),getgid()) != XCGROUP_SUCCESS) {
		goto error;
	}
	if (xcgroup_instanciate(&user_memory_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_memory_cg);
		goto error;
	}
	xcgroup_set_param(&user_memory_cg,"memory.use_hierarchy","1");

	/*
	 * Create job cgroup in the memory ns (it could already exist)
	 * and set the associated memory limits.
	 * Ask for hierarchical memory accounting starting from the job
	 * container in order to guarantee that a job will stay on track
	 * regardless of the consumption of each step.
	 */
	if (memcg_initialize (&memory_ns, &job_memory_cg, job_cgroup_path,
	                      job->job_mem, getuid(), getgid()) < 0) {
		xcgroup_destroy (&user_memory_cg);
		goto error;
	}

	/*
	 * Create step cgroup in the memory ns (it should not exists)
	 * and set the associated memory limits.
	 */
	if (memcg_initialize (&memory_ns, &step_memory_cg, jobstep_cgroup_path,
	                      job->step_mem, uid, gid) < 0) {
		xcgroup_destroy(&user_memory_cg);
		xcgroup_destroy(&job_memory_cg);
		goto error;
	}

	/*
	 * Attach the slurmstepd to the step memory cgroup
	 */
	pid = getpid();
	rc = xcgroup_add_pids(&step_memory_cg,&pid,1);
	if (rc != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to add slurmstepd to memory cg '%s'",
		      step_memory_cg.path);
		fstatus = SLURM_ERROR;
	} else
		fstatus = SLURM_SUCCESS;

error:
	xcgroup_unlock(&memory_cg);
	xcgroup_destroy(&memory_cg);

	return fstatus;
}

extern int task_cgroup_memory_attach_task(slurmd_job_t *job)
{
	int fstatus = SLURM_ERROR;

	/* tasks are automatically attached as slurmstepd is in the step cg */
	fstatus = SLURM_SUCCESS;

	return fstatus;
}

