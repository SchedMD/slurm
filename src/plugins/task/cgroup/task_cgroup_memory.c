/***************************************************************************** \
 *  task_cgroup_memory.c - memory cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
\*****************************************************************************/

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/common/xstring.h"
#include "src/common/cgroup.h"

#include "task_cgroup.h"

static bool constrain_ram_space;
static bool constrain_kmem_space;
static bool constrain_swap_space;

static float allowed_ram_space;   /* Allowed RAM in percent */
static float allowed_swap_space;  /* Allowed Swap percent */
static float allowed_kmem_space;  /* Allowed Kmem number */
static float max_kmem_percent;	   /* Allowed Kernel memory percent*/

static uint64_t max_kmem;       /* Upper bound for kmem.limit_in_bytes */
static uint64_t max_ram;        /* Upper bound for memory.limit_in_bytes */
static uint64_t max_swap;       /* Upper bound for swap */
static uint64_t totalram;       /* Total real memory available on node */
static uint64_t min_ram_space;  /* Don't constrain RAM below this value */
static uint64_t min_kmem_space; /* Don't constrain Kernel mem below */

static bool oom_mgr_started = false;

static uint64_t percent_in_bytes(uint64_t mb, float percent)
{
	return ((mb * 1024 * 1024) * (percent / 100.0));
}

extern int task_cgroup_memory_init(void)
{
	if (cgroup_g_initialize(CG_MEMORY) != SLURM_SUCCESS)
		return SLURM_ERROR;

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
	if (constrain_ram_space)
		allowed_ram_space = slurm_cgroup_conf.allowed_ram_space;
	else
		allowed_ram_space = 100.0;

	allowed_kmem_space = slurm_cgroup_conf.allowed_kmem_space;
	allowed_swap_space = slurm_cgroup_conf.allowed_swap_space;

	if ((totalram = (uint64_t) conf->real_memory_size) == 0)
		error ("Unable to get RealMemory size");

	max_kmem = percent_in_bytes(totalram,
				    slurm_cgroup_conf.max_kmem_percent);
	max_ram = percent_in_bytes(totalram,
				   slurm_cgroup_conf.max_ram_percent);
	max_swap = percent_in_bytes(totalram,
				    slurm_cgroup_conf.max_swap_percent);
	max_swap += max_ram;
	min_ram_space = slurm_cgroup_conf.min_ram_space * 1024 * 1024;
	max_kmem_percent = slurm_cgroup_conf.max_kmem_percent;
	min_kmem_space = slurm_cgroup_conf.min_kmem_space * 1024 * 1024;

	debug("task/cgroup/memory: total:%"PRIu64"M allowed:%.4g%%(%s), "
	      "swap:%.4g%%(%s), max:%.4g%%(%"PRIu64"M) "
	      "max+swap:%.4g%%(%"PRIu64"M) min:%"PRIu64"M "
	      "kmem:%.4g%%(%"PRIu64"M %s) min:%"PRIu64"M ",
	      totalram, allowed_ram_space,
	      constrain_ram_space ? "enforced" : "permissive",
	      allowed_swap_space,
	      constrain_swap_space ? "enforced" : "permissive",
	      slurm_cgroup_conf.max_ram_percent,
	      (uint64_t) (max_ram / (1024 * 1024)),
	      slurm_cgroup_conf.max_swap_percent,
	      (uint64_t) (max_swap / (1024 * 1024)),
	      slurm_cgroup_conf.min_ram_space,
	      slurm_cgroup_conf.max_kmem_percent,
	      (uint64_t) (max_kmem / (1024 * 1024)),
	      constrain_kmem_space ? "enforced" : "permissive",
	      slurm_cgroup_conf.min_kmem_space);

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

extern int task_cgroup_memory_fini()
{
	return cgroup_g_step_destroy(CG_MEMORY);
}

/* Return configured memory limit in bytes given a memory limit in MB. */
static uint64_t mem_limit_in_bytes(uint64_t mem, bool with_allowed)
{
	/*
	 *  If mem == 0 then assume there was no Slurm limit imposed
	 *   on the amount of memory for job or step. Use the total
	 *   amount of available RAM instead.
	 */
	if (mem == 0)
		mem = totalram * 1024 * 1024;
	else {
		if (with_allowed)
			mem = percent_in_bytes(mem, allowed_ram_space);
		else
			mem = percent_in_bytes(mem, 100.0);
	}

	if (mem < min_ram_space)
		return min_ram_space;

	if (mem > max_ram)
		return max_ram;

	return mem;
}

/*
 *  Return configured swap limit in bytes given a memory limit in MB.
 *
 *  Swap limit is calculated as:
 *  mem_limit_in_bytes + (configured_swap_percent * allocated_mem_in_bytes)
 */
static uint64_t swap_limit_in_bytes(uint64_t mem)
{
	uint64_t swap;

	/* If mem == 0 assume "unlimited" and use totalram. */
	swap = percent_in_bytes(mem ? mem : totalram, allowed_swap_space);
	mem = mem_limit_in_bytes(mem, true) + swap;

	if (mem < min_ram_space)
		return min_ram_space;

	if (mem > max_swap)
		return max_swap;

	return mem;
}

/*
 * Return kmem memory limit in bytes given a memory limit in bytes.
 * If Kmem space is disabled, it set to max percent of its RAM usage.
 */
static uint64_t kmem_limit_in_bytes(uint64_t mlb)
{
	uint64_t totalKmem = mlb * (max_kmem_percent / 100.0);

	if (allowed_kmem_space < 0) {	/* Initial value */
		if (mlb > totalKmem)
			return totalKmem;
		if (mlb < min_kmem_space)
			return min_kmem_space;
		return mlb;
	}

	if (allowed_kmem_space > totalKmem)
		return totalKmem;

	if (allowed_kmem_space < min_kmem_space)
		return min_kmem_space;

	return allowed_kmem_space;
}

static int _memcg_initialize(stepd_step_rec_t *job, uint64_t mem_limit,
			     bool is_step)
{
	uint64_t mlb = mem_limit_in_bytes(mem_limit, true);
	uint64_t mlb_soft = mem_limit_in_bytes(mem_limit, false);
	uint64_t mls = swap_limit_in_bytes(mem_limit);
	cgroup_limits_t limits;

	if (mlb_soft > mlb) {
		/*
		 * NOTE: It is recommended to set the soft limit always below
		 * the hard limit, otherwise the hard one will take precedence.
		 */
		debug2("Setting memory soft limit (%"PRIu64" bytes) to the same value as memory limit (%"PRIu64" bytes) for %s",
		       mlb_soft, mlb, is_step ? "step" : "job");
		mlb_soft = mlb;
	}

	cgroup_init_limits(&limits);

	/*
	 * When RAM space has not to be constrained and we are here, it means
	 * that only Swap space has to be constrained. Thus set RAM space limit
	 * to the mem+swap limit too.
	 */
	if (!constrain_ram_space)
		mlb = mls;

	limits.limit_in_bytes = mlb;
	limits.soft_limit_in_bytes = mlb_soft;
	limits.kmem_limit_in_bytes = NO_VAL64;
	limits.memsw_limit_in_bytes = NO_VAL64;
	limits.swappiness = NO_VAL64;

	if (constrain_kmem_space)
		limits.kmem_limit_in_bytes = kmem_limit_in_bytes(mlb);

	/* This limit has to be set only if ConstrainSwapSpace is set to yes. */
	if (constrain_swap_space) {
		limits.swappiness = slurm_cgroup_conf.memory_swappiness;
		limits.memsw_limit_in_bytes = mls;
		info("%s: alloc=%"PRIu64"MB mem.limit=%"PRIu64"MB "
		     "memsw.limit=%"PRIu64"MB job_swappiness=%"PRIu64,
		     is_step ? "step" : "job",
		     mem_limit,
		     mlb/(1024*1024),
		     mls/(1024*1024),
		     limits.swappiness);
	} else {
		info("%s: alloc=%"PRIu64"MB mem.limit=%"PRIu64"MB "
		     "memsw.limit=unlimited", is_step ? "step" : "job",
		     mem_limit,
		     mlb/(1024*1024));
	}

	if (!is_step) {
		if (cgroup_g_constrain_set(CG_MEMORY, CG_LEVEL_JOB, &limits)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
	} else {
		if (cgroup_g_constrain_set(CG_MEMORY, CG_LEVEL_STEP, &limits)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int task_cgroup_memory_create(stepd_step_rec_t *job)
{
	pid_t pid;

	if (cgroup_g_step_create(CG_MEMORY, job) != SLURM_SUCCESS)
		return SLURM_ERROR;

	/* Set the associated memory limits for the job and for the step. */
	if (_memcg_initialize(job, job->job_mem, false) != SLURM_SUCCESS)
		return SLURM_ERROR;
	if (_memcg_initialize(job, job->step_mem, true) != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (cgroup_g_step_start_oom_mgr() == SLURM_SUCCESS)
		oom_mgr_started = true;

	/* Attach the slurmstepd to the step memory cgroup. */
	pid = getpid();
	return cgroup_g_step_addto(CG_MEMORY, &pid, 1);
}

extern int task_cgroup_memory_check_oom(stepd_step_rec_t *job)
{
	cgroup_oom_t *results;
	int rc = SLURM_SUCCESS;

	if (!oom_mgr_started)
		return SLURM_SUCCESS;

	results = cgroup_g_step_stop_oom_mgr(job);

	if (results == NULL)
		return SLURM_ERROR;

	if (results->step_memsw_failcnt > 0) {
		/*
		 * reports the number of times that the memory plus swap space
		 * limit has reached the value in memory.memsw.limit_in_bytes.
		 */
		info("%ps hit memory+swap limit at least once during execution. This may or may not result in some failure.",
		     &job->step_id);
	} else if (results->step_mem_failcnt > 0) {
		/*
		 * reports the number of times that the memory limit has reached
		 * the value set in memory.limit_in_bytes.
		 */
		info("%ps hit memory limit at least once during execution. This may or may not result in some failure.",
		     &job->step_id);
	}

	if (results->job_memsw_failcnt > 0) {
		info("%ps hit memory+swap limit at least once during execution. This may or may not result in some failure.",
		     &job->step_id);
	} else if (results->job_mem_failcnt > 0) {
		info("%ps hit memory limit at least once during execution. This may or may not result in some failure.",
		     &job->step_id);
	}

	if (results->oom_kill_cnt) {
		error("Detected %"PRIu64" oom-kill event(s) in %ps. Some of your processes may have been killed by the cgroup out-of-memory handler.",
		      results->oom_kill_cnt, &job->step_id);
		rc = ENOMEM;
	}

	xfree(results);

	return rc;
}

extern int task_cgroup_memory_add_pid(stepd_step_rec_t *job, pid_t pid,
				      uint32_t taskid)
{
	return cgroup_g_task_addto(CG_MEMORY, job, pid, taskid);
}

extern int task_cgroup_memory_add_extern_pid(pid_t pid)
{
	/* Only in the extern step we will not create specific tasks */
	return cgroup_g_step_addto(CG_MEMORY, &pid, 1);
}
