/*****************************************************************************\
 *  jobacct_gather_cgroup_memory.c - memory cgroup subsystem for
 *  jobacct_gather/cgroup
 *****************************************************************************
 *  Copyright (C) 2011 Bull
 *  Written by Martin Perry (martin.perry@bull.com) based on code from
 *  Matthieu Hautreux
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

#include <limits.h>
#include <stdlib.h>		/* getenv   */
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"
#include "src/common/xstring.h"
#include "src/plugins/jobacct_gather/cgroup/jobacct_gather_cgroup.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];
static char task_cgroup_path[PATH_MAX];

static xcgroup_ns_t memory_ns;

static xcgroup_t user_memory_cg;
static xcgroup_t job_memory_cg;
static xcgroup_t step_memory_cg;

List task_memory_cg_list = NULL;

static uint32_t max_task_id;

extern int
jobacct_gather_cgroup_memory_init(void)
{
	/* initialize user/job/jobstep cgroup relative paths */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';
	task_cgroup_path[0] ='\0';

	/* initialize memory cgroup namespace */
	if (xcgroup_ns_create(&memory_ns, "", "memory")
	    != SLURM_SUCCESS) {
		error("jobacct_gather/cgroup: unable to create memory "
		      "namespace");
		return SLURM_ERROR;
	}

	FREE_NULL_LIST(task_memory_cg_list);
	task_memory_cg_list = list_create(free_task_cg_info);

	return SLURM_SUCCESS;
}

extern int
jobacct_gather_cgroup_memory_fini(void)
{
	xcgroup_t memory_cg;
	bool lock_ok;
	int cc;

	if (user_cgroup_path[0] == '\0'
	    || job_cgroup_path[0] == '\0'
	    || jobstep_cgroup_path[0] == '\0'
	    || task_cgroup_path[0] == '\0')
		return SLURM_SUCCESS;

	/* Move the slurmstepd back to the root memory cg */
	if (xcgroup_create(&memory_ns, &memory_cg, "", 0, 0) == SLURM_SUCCESS)
		xcgroup_set_uint32_param(&memory_cg, "tasks", getpid());

	/* Lock the root of the cgroup and remove the subdirectories
	 * related to this job.
	 */
	lock_ok = true;
	if (xcgroup_lock(&memory_cg) != SLURM_SUCCESS) {
		error("failed to flock() %s %m", memory_cg.path);
		lock_ok = false;
	}

	/* Clean up starting from the leaves way up, the
	 * reverse order in which the cgroups were created.
	 * The debug2 messages are not errors as it is possible
	 * that some other processes/plugins are accessing
	 * some of those directories. The last one to leave
	 * will clean it up, eventually the release_agent.
	 */
	for (cc = 0; cc <= max_task_id; cc++) {
		xcgroup_t cgroup;
		char *buf = NULL;

		/* rmdir all tasks this running slurmstepd
		 * was responsible for.
		 */
		xstrfmtcat(buf, "%s%s/task_%d",
			   memory_ns.mnt_point, jobstep_cgroup_path, cc);
		cgroup.path = buf;

		if (xcgroup_delete(&cgroup) != SLURM_SUCCESS) {
			debug2("failed to delete %s %m", buf);
		}

		xfree(buf);
	}

	/*
	 * Clean the rest of the hierarchy.
	 * Despite rmdir() offlines memcg, the memcg may still stay there due
	 * to charged file caches. Some out-of-use page caches may keep charged
	 * until memory pressure happens. Avoid this writting to 'force_empty'.
	 * Note that when memory.kmem.limit_in_bytes is set the charges due to
	 * kernel pages will still be seen.
	 */
	xcgroup_set_param(&step_memory_cg, "memory.force_empty", "1");
	if (xcgroup_delete(&step_memory_cg) != SLURM_SUCCESS) {
		debug2("failed to delete %s %m", step_memory_cg.path);
	}

	if (xcgroup_delete(&job_memory_cg) != SLURM_SUCCESS) {
		debug2("failed to delete %s %m", job_memory_cg.path);
	}

	if (xcgroup_delete(&user_memory_cg) != SLURM_SUCCESS) {
		debug2("failed to delete %s %m", user_memory_cg.path);
	}

	if (lock_ok == true)
		xcgroup_unlock(&memory_cg);

	xcgroup_destroy(&memory_cg);
	xcgroup_destroy(&user_memory_cg);
	xcgroup_destroy(&job_memory_cg);
	xcgroup_destroy(&step_memory_cg);
	FREE_NULL_LIST(task_memory_cg_list);

	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';
	task_cgroup_path[0]='\0';

	xcgroup_ns_destroy(&memory_ns);

	return SLURM_SUCCESS;
}

extern int
jobacct_gather_cgroup_memory_attach_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	stepd_step_rec_t *job = jobacct_id->job;

	if (jobacct_id->taskid >= max_task_id)
		max_task_id = jobacct_id->taskid;

	debug("%ps taskid %u max_task_id %u", &job->step_id,
	      jobacct_id->taskid, max_task_id);

	return create_jobacct_cgroups(__func__,
				      jobacct_id,
				      pid,
				      &memory_ns,
				      &job_memory_cg,
				      &step_memory_cg,
				      task_memory_cg_list,
				      &user_memory_cg,
				      job_cgroup_path,
				      jobstep_cgroup_path,
				      task_cgroup_path,
				      user_cgroup_path);
}
