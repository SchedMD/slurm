/***************************************************************************** \
 *  task_cgroup_memory.c - memory cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

static bool constrain_ram_space;
static bool constrain_swap_space;

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
	xcgroup_t memory_cg;

	/* initialize user/job/jobstep cgroup relative paths */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	/* initialize memory cgroup namespace */
	if (xcgroup_ns_create(slurm_cgroup_conf, &memory_ns, "", "memory")
	    != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to create memory namespace");
		return SLURM_ERROR;
	}

	/* Enable memory.use_hierarchy in the root of the cgroup.
	 */
	xcgroup_create(&memory_ns, &memory_cg, "", 0, 0);
	xcgroup_set_param(&memory_cg, "memory.use_hierarchy","1");
	xcgroup_destroy(&memory_cg);

	constrain_ram_space = slurm_cgroup_conf->constrain_ram_space;
	constrain_swap_space = slurm_cgroup_conf->constrain_swap_space;

	/*
	 * as the swap space threshold will be configured with a
	 * mem+swp parameter value, if RAM space is not monitored,
	 * set allowed RAM space to 100% of the job requested memory.
	 * It will help to construct the mem+swp value that will be
	 * used for both mem and mem+swp limit during memcg creation.
	 */
	if ( constrain_ram_space )
		allowed_ram_space = slurm_cgroup_conf->allowed_ram_space;
	else
		allowed_ram_space = 100.0;

	allowed_swap_space = slurm_cgroup_conf->allowed_swap_space;

	if ((totalram = (uint64_t) conf->real_memory_size) == 0)
		error ("task/cgroup: Unable to get RealMemory size");

	max_ram = percent_in_bytes(totalram, slurm_cgroup_conf->max_ram_percent);
	max_swap = percent_in_bytes(totalram, slurm_cgroup_conf->max_swap_percent);
	max_swap += max_ram;
	min_ram_space = slurm_cgroup_conf->min_ram_space * 1024 * 1024;

	debug ("task/cgroup/memory: total:%luM allowed:%.4g%%(%s), "
	       "swap:%.4g%%(%s), max:%.4g%%(%luM) max+swap:%.4g%%(%luM) min:%uM",
	       (unsigned long) totalram,
	       allowed_ram_space,
	       constrain_ram_space?"enforced":"permissive",
	       allowed_swap_space,
	       constrain_swap_space?"enforced":"permissive",
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
         *  -1000 value, wich means do not let OOM killer kill it
         *
         *  FYI, setting "export SLURMSTEPD_OOM_ADJ=-1000"
         *  in /etc/sysconfig/slurm would be the same
         */
        setenv("SLURMSTEPD_OOM_ADJ", "-1000", 0);

	return SLURM_SUCCESS;
}

extern int task_cgroup_memory_fini(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	xcgroup_t memory_cg;

	if (user_cgroup_path[0] == '\0' ||
	     job_cgroup_path[0] == '\0' ||
	     jobstep_cgroup_path[0] == '\0')
		return SLURM_SUCCESS;
	/*
	 * Lock the root memcg and try to remove the different memcgs.
	 * The reason why we are locking here is that if a concurrent
	 * step is in the process of being executed, he could try to
	 * create the step memcg just after we remove the job memcg,
	 * resulting in a failure.
	 * First, delete step memcg as all the tasks have now exited.
	 * Then, try to remove the job memcg.
	 * If it fails, it is due to the fact that it is still in use by an
	 * other running step.
	 * After that, try to remove the user memcg. If it fails, it is due
	 * to jobs that are still running for the same user on the node or
	 * because of tasks attached directly to the user cg by an other
	 * component (PAM). The user memcg was created with the
	 * notify_on_release=1 flag (default) so it will be removed
	 * automatically after that.
	 * For now, do not try to detect if only externally attached tasks
	 * are present to see if they can be be moved to an orhpan memcg.
	 * That could be done in the future, if it is necessary.
	 */
	if (xcgroup_create(&memory_ns,&memory_cg,"",0,0) == XCGROUP_SUCCESS) {
		if (xcgroup_lock(&memory_cg) == XCGROUP_SUCCESS) {
			if (xcgroup_delete(&step_memory_cg) != SLURM_SUCCESS)
				debug2("task/cgroup: unable to remove step "
				       "memcg : %m");
			if (xcgroup_delete(&job_memory_cg) != XCGROUP_SUCCESS)
				debug2("task/cgroup: not removing "
				       "job memcg : %m");
			if (xcgroup_delete(&user_memory_cg) != XCGROUP_SUCCESS)
				debug2("task/cgroup: not removing "
				       "user memcg : %m");
			xcgroup_unlock(&memory_cg);
		} else
			error("task/cgroup: unable to lock root memcg : %m");
		xcgroup_destroy(&memory_cg);
	} else
		error("task/cgroup: unable to create root memcg : %m");

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
			     char *path, uint64_t mem_limit, uid_t uid,
			     gid_t gid, uint32_t notify)
{
	uint64_t mlb = mem_limit_in_bytes (mem_limit);
	uint64_t mls = swap_limit_in_bytes  (mem_limit);

	if (xcgroup_create (ns, cg, path, uid, gid) != XCGROUP_SUCCESS)
		return -1;

	cg->notify = notify;

	if (xcgroup_instanciate (cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy (cg);
		return -1;
	}

	xcgroup_set_param (cg, "memory.use_hierarchy","1");

	/* when RAM space has not to be constrained and we are here, it
	 * means that only Swap space has to be constrained. Thus set
	 * RAM space limit to the mem+swap limit too */
	if ( ! constrain_ram_space )
		mlb = mls;
	xcgroup_set_uint64_param (cg, "memory.limit_in_bytes", mlb);

	/* this limit has to be set only if ConstrainSwapSpace is set to yes */
	if ( constrain_swap_space ) {
		xcgroup_set_uint64_param (cg, "memory.memsw.limit_in_bytes",
					  mls);
		info ("task/cgroup: %s: alloc=%luMB mem.limit=%luMB "
		      "memsw.limit=%luMB", path,
		      (unsigned long) mem_limit,
		      (unsigned long) mlb/(1024*1024),
		      (unsigned long) mls/(1024*1024));
	} else {
		info ("task/cgroup: %s: alloc=%luMB mem.limit=%luMB "
		      "memsw.limit=unlimited", path,
		      (unsigned long) mem_limit,
		      (unsigned long) mlb/(1024*1024));
	}

	return 0;
}

extern int task_cgroup_memory_create(stepd_step_rec_t *job)
{
	int fstatus = SLURM_ERROR;
	xcgroup_t memory_cg;
	uint32_t jobid = job->jobid;
	uint32_t stepid = job->stepid;
	uid_t uid = job->uid;
	gid_t gid = job->gid;
	char *slurm_cgpath;

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
		int cc;

		if (stepid == SLURM_BATCH_SCRIPT) {
			cc = snprintf(jobstep_cgroup_path, PATH_MAX,
				      "%s/step_batch", job_cgroup_path);
			if (cc >= PATH_MAX) {
				error("task/cgroup: unable to build "
				      "step batch memory cg path : %m");

			}

		} else {
			if (snprintf(jobstep_cgroup_path, PATH_MAX, "%s/step_%u",
				     job_cgroup_path,stepid) >= PATH_MAX) {
				error("task/cgroup: unable to build job step %u memory "
				      "cg relative path : %m",stepid);
				return SLURM_ERROR;
			}
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
	if ( xcgroup_set_param(&user_memory_cg,"memory.use_hierarchy","1")
	     != XCGROUP_SUCCESS ) {
		error("task/cgroup: unable to ask for hierarchical accounting"
		      "of user memcg '%s'",user_memory_cg.path);
		xcgroup_destroy (&user_memory_cg);
		goto error;
	}

	/*
	 * Create job cgroup in the memory ns (it could already exist)
	 * and set the associated memory limits.
	 * Disable notify_on_release for this memcg, it will be
	 * manually removed by the plugin at the end of the step.
	 */
	if (memcg_initialize (&memory_ns, &job_memory_cg, job_cgroup_path,
	                      job->job_mem, getuid(), getgid(), 0) < 0) {
		xcgroup_destroy (&user_memory_cg);
		goto error;
	}

	/*
	 * Create step cgroup in the memory ns (it should not exists)
	 * and set the associated memory limits.
	 * Disable notify_on_release for the step memcg, it will be
	 * manually removed by the plugin at the end of the step.
	 */
	if (memcg_initialize (&memory_ns, &step_memory_cg, jobstep_cgroup_path,
	                      job->step_mem, uid, gid, 0) < 0) {
		xcgroup_destroy(&user_memory_cg);
		xcgroup_destroy(&job_memory_cg);
		goto error;
	}

error:
	xcgroup_unlock(&memory_cg);
	xcgroup_destroy(&memory_cg);

	return fstatus;
}

extern int task_cgroup_memory_attach_task(stepd_step_rec_t *job)
{
	int fstatus = SLURM_ERROR;
	pid_t pid;

	/*
	 * Attach the current task to the step memory cgroup
	 */
	pid = getpid();
	if (xcgroup_add_pids(&step_memory_cg,&pid,1) != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to add task[pid=%u] to "
		      "memory cg '%s'",pid,step_memory_cg.path);
		fstatus = SLURM_ERROR;
	} else
		fstatus = SLURM_SUCCESS;

	return fstatus;
}

/* return 1 if failcnt file exists and is > 0 */
int failcnt_non_zero(xcgroup_t* cg, char* param)
{
	int fstatus = XCGROUP_ERROR;
	uint64_t value;

	fstatus = xcgroup_get_uint64_param(cg, param, &value);

	if (fstatus != XCGROUP_SUCCESS) {
		debug2("unable to read '%s' from '%s'", param, cg->path);
		return 0;
	}

	return value > 0;
}

extern int task_cgroup_memory_check_oom(stepd_step_rec_t *job)
{
	xcgroup_t memory_cg;

	if (xcgroup_create(&memory_ns, &memory_cg, "", 0, 0)
	    == XCGROUP_SUCCESS) {
		if (xcgroup_lock(&memory_cg) == XCGROUP_SUCCESS) {
			/* for some reason the job cgroup limit is hit
			 * for a step and vice versa...
			 * can't tell which is which so we'll treat
			 * them the same */
			if (failcnt_non_zero(&step_memory_cg,
					     "memory.memsw.failcnt"))
				/* reports the number of times that the
				 * memory plus swap space limit has
				 * reached the value set in
				 * memory.memsw.limit_in_bytes.
				 */
				info("Exceeded step memory limit at some point.");
			else if (failcnt_non_zero(&step_memory_cg,
						  "memory.failcnt"))
				/* reports the number of times that the
				 * memory limit has reached the value set
				 * in memory.limit_in_bytes.
				 */
				info("Exceeded step memory limit at some point.");
			if (failcnt_non_zero(&job_memory_cg,
					     "memory.memsw.failcnt"))
				info("Exceeded job memory limit at some point.");
			else if (failcnt_non_zero(&job_memory_cg,
						  "memory.failcnt"))
				info("Exceeded job memory limit at some point.");
			xcgroup_unlock(&memory_cg);
		} else
			error("task/cgroup task_cgroup_memory_check_oom: "
			      "task_cgroup_memory_check_oom: unable to lock "
			      "root memcg : %m");
		xcgroup_destroy(&memory_cg);
	} else
		error("task/cgroup task_cgroup_memory_check_oom: "
		      "unable to create root memcg : %m");

	return SLURM_SUCCESS;
}
