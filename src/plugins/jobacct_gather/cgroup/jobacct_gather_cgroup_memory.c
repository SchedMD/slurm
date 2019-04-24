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
	    != XCGROUP_SUCCESS) {
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
	if (xcgroup_create(&memory_ns, &memory_cg, "", 0, 0) == XCGROUP_SUCCESS) {
		xcgroup_set_uint32_param(&memory_cg, "tasks", getpid());
		xcgroup_set_param(&step_memory_cg, "memory.force_empty", "1");
	}

	/* Lock the root of the cgroup and remove the subdirectories
	 * related to this job.
	 */
	lock_ok = true;
	if (xcgroup_lock(&memory_cg) != XCGROUP_SUCCESS) {
		error("%s: failed to flock() %s %m", __func__, memory_cg.path);
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

		if (xcgroup_delete(&cgroup) != XCGROUP_SUCCESS) {
			debug2("%s: failed to delete %s %m", __func__, buf);
		}

		xfree(buf);
	}

	/* Clean the rest of the hierarchy.
	 */
	if (xcgroup_delete(&step_memory_cg) != XCGROUP_SUCCESS) {
		debug2("%s: failed to delete %s %m", __func__,
		       step_memory_cg.path);
	}

	if (xcgroup_delete(&job_memory_cg) != XCGROUP_SUCCESS) {
		debug2("%s: failed to delete %s %m", __func__,
		       job_memory_cg.path);
	}

	if (xcgroup_delete(&user_memory_cg) != XCGROUP_SUCCESS) {
		debug2("%s: failed to delete %s %m", __func__,
		       user_memory_cg.path);
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
	xcgroup_t memory_cg;
	stepd_step_rec_t *job;
	uid_t uid;
	gid_t gid;
	uint32_t jobid;
	uint32_t stepid;
	uint32_t taskid;
	int fstatus = SLURM_SUCCESS;
	int rc;
	char* slurm_cgpath;
	task_cg_info_t *task_cg_info;
	bool need_to_add = false;

	job = jobacct_id->job;
	uid = job->uid;
	gid = job->gid;
	stepid = job->stepid;
	taskid = jobacct_id->taskid;
	if (job->pack_jobid && (job->pack_jobid != NO_VAL))
		jobid = job->pack_jobid;
	else
		jobid = job->jobid;

	if (taskid >= max_task_id)
		max_task_id = taskid;

	xassert(task_cpuacct_cg_list);

	debug("%s: jobid %u stepid %u taskid %u max_task_id %u",
	      __func__, jobid, stepid, taskid, max_task_id);

	/* create slurm root cg in this cg namespace */
	slurm_cgpath = jobacct_cgroup_create_slurm_cg(&memory_ns);
	if (!slurm_cgpath) {
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

	/* build job cgroup relative path if not set (may not be) */
	if (*job_cgroup_path == '\0') {
		if (snprintf(job_cgroup_path, PATH_MAX, "%s/job_%u",
			     user_cgroup_path, jobid) >= PATH_MAX) {
			error("jobacct_gather/cgroup: unable to build job %u "
			      "memory cg relative path : %m", jobid);
			return SLURM_ERROR;
		}
	}

	/* build job step cgroup relative path if not set (may not be) */
	if (*jobstep_cgroup_path == '\0') {
		int len;
		if (stepid == SLURM_BATCH_SCRIPT) {
			len = snprintf(jobstep_cgroup_path, PATH_MAX,
				       "%s/step_batch", job_cgroup_path);
		} else if (stepid == SLURM_EXTERN_CONT) {
			len = snprintf(jobstep_cgroup_path, PATH_MAX,
				       "%s/step_extern", job_cgroup_path);
		} else {
			len = snprintf(jobstep_cgroup_path, PATH_MAX,
				       "%s/step_%u",
				       job_cgroup_path, stepid);
		}
		if (len >= PATH_MAX) {
			error("jobacct_gather/cgroup: unable to build job step "
			      "%u.%u memory cg relative path : %m",
			      jobid, stepid);
			return SLURM_ERROR;
		}
	}

	/* build task cgroup relative path */
	if (snprintf(task_cgroup_path, PATH_MAX, "%s/task_%u",
		     jobstep_cgroup_path, taskid) >= PATH_MAX) {
		error("jobacct_gather/cgroup: unable to build task %u "
		      "memory cg relative path: %m", taskid);
		return SLURM_ERROR;
	}

	/*
	 * create memory root cg and lock it
	 *
	 * we will keep the lock until the end to avoid the effect of a release
	 * agent that would remove an existing cgroup hierarchy while we are
	 * setting it up. As soon as the step cgroup is created, we can release
	 * the lock.
	 * Indeed, consecutive slurm steps could result in cg being removed
	 * between the next EEXIST instantiation and the first addition of
	 * a task. The release_agent will have to lock the root memory cgroup
	 * to avoid this scenario.
	 */

	if (xcgroup_create(&memory_ns, &memory_cg, "", 0, 0)
	    != XCGROUP_SUCCESS) {
		error("jobacct_gather/cgroup: unable to create root memory "
		      "xcgroup");
		return SLURM_ERROR;
	}
	if (xcgroup_lock(&memory_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&memory_cg);
		error("jobacct_gather/cgroup: unable to lock root memory cg");
		return SLURM_ERROR;
	}

	/*
	 * Create user cgroup in the memory ns (it could already exist)
	 * Ask for hierarchical memory accounting starting from the user
	 * container in order to track the memory consumption up to the
	 * user.
	 */
	if (xcgroup_create(&memory_ns, &user_memory_cg,
			   user_cgroup_path,
			   uid, gid) != XCGROUP_SUCCESS) {
		error("jobacct_gather/cgroup: unable to create user %u memory "
		      "cgroup", uid);
		fstatus = SLURM_ERROR;
		goto error;
	}

	if (xcgroup_instantiate(&user_memory_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_memory_cg);
		error("jobacct_gather/cgroup: unable to instantiate user %u "
		      "memory cgroup", uid);
		fstatus = SLURM_ERROR;
		goto error;
	}

	/*
	 * Create job cgroup in the memory ns (it could already exist)
	 */
	if (xcgroup_create(&memory_ns, &job_memory_cg,
			   job_cgroup_path,
			   uid, gid) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_memory_cg);
		error("jobacct_gather/cgroup: unable to create job %u memory "
		      "cgroup", jobid);
		fstatus = SLURM_ERROR;
		goto error;
	}

	if (xcgroup_instantiate(&job_memory_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_memory_cg);
		xcgroup_destroy(&job_memory_cg);
		error("jobacct_gather/cgroup: unable to instantiate job %u "
		      "memory cgroup", jobid);
		fstatus = SLURM_ERROR;
		goto error;
	}

	/*
	 * Create step cgroup in the memory ns (it could already exist)
	 */
	if (xcgroup_create(&memory_ns, &step_memory_cg,
			   jobstep_cgroup_path,
			   uid, gid) != XCGROUP_SUCCESS) {
		/* do not delete user/job cgroup as they can exist for other
		 * steps, but release cgroup structures */
		xcgroup_destroy(&user_memory_cg);
		xcgroup_destroy(&job_memory_cg);
		error("jobacct_gather/cgroup: unable to create jobstep %u.%u "
		      "memory cgroup", jobid, stepid);
		fstatus = SLURM_ERROR;
		goto error;
	}

	if (xcgroup_instantiate(&step_memory_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_memory_cg);
		xcgroup_destroy(&job_memory_cg);
		xcgroup_destroy(&step_memory_cg);
		error("jobacct_gather/cgroup: unable to instantiate jobstep "
		      "%u.%u memory cgroup", jobid, stepid);
		fstatus = SLURM_ERROR;
		goto error;
	}

	if (!(task_cg_info = list_find_first(task_memory_cg_list,
					     find_task_cg_info,
					     &taskid))) {
		task_cg_info = xmalloc(sizeof(*task_cg_info));
		task_cg_info->taskid = taskid;
		need_to_add = true;
	}
	/*
	 * Create task cgroup in the memory ns
	 */
	if (xcgroup_create(&memory_ns, &task_cg_info->task_cg,
			   task_cgroup_path,
			   uid, gid) != XCGROUP_SUCCESS) {
		/*
		 * do not delete user/job cgroup as they can exist for
		 * other steps, but release cgroup structures
		 */
		xcgroup_destroy(&user_memory_cg);
		xcgroup_destroy(&job_memory_cg);

		/* Don't use free_task_cg_info as the task_cg isn't there */
		xfree(task_cg_info);

		error("jobacct_gather/cgroup: unable to create jobstep %u.%u task %u memory cgroup",
		      jobid, stepid, taskid);
		fstatus = SLURM_ERROR;
		goto error;
	}

	if (xcgroup_instantiate(&task_cg_info->task_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_memory_cg);
		xcgroup_destroy(&job_memory_cg);
		xcgroup_destroy(&step_memory_cg);
		free_task_cg_info(task_cg_info);
		error("jobacct_gather/cgroup: unable to instantiate jobstep %u.%u task %u memory cgroup",
		      jobid, stepid, taskid);
		fstatus = SLURM_ERROR;
		goto error;
	}

	/*
	 * Attach the slurmstepd to the task memory cgroup
	 */
	rc = xcgroup_add_pids(&task_cg_info->task_cg, &pid, 1);
	if (rc != XCGROUP_SUCCESS) {
		error("jobacct_gather/cgroup: unable to add slurmstepd to memory cg '%s'",
		      task_cg_info->task_cg.path);
		fstatus = SLURM_ERROR;
	} else
		fstatus = SLURM_SUCCESS;

	/* Add the task cgroup to the list now that it is initialized. */
	if (need_to_add)
		list_append(task_memory_cg_list, task_cg_info);

error:
	xcgroup_unlock(&memory_cg);
	xcgroup_destroy(&memory_cg);
	return fstatus;
}
