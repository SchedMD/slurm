/*****************************************************************************\
 *  jobacct_gather_cgroup_blkio.c - blkio cgroup subsystem for
 *  jobacct_gather/cgroup
 *****************************************************************************
 *  Copyright (C) 2013 Bull
 *  Written by Martin Perry (martin.perry@bull.com) based on code from
 *  Matthieu Hautreux
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

/* FIXME: Enable when kernel support is ready. */

/* #include <limits.h> */
/* #include <stdlib.h>		/\* getenv   *\/ */
/* #include <sys/types.h> */

/* #include "slurm/slurm_errno.h" */
/* #include "slurm/slurm.h" */
/* #include "src/common/xstring.h" */
/* #include "src/plugins/jobacct_gather/cgroup/jobacct_gather_cgroup.h" */
/* #include "src/slurmd/slurmstepd/slurmstepd_job.h" */
/* #include "src/slurmd/slurmd/slurmd.h" */

/* static char user_cgroup_path[PATH_MAX]; */
/* static char job_cgroup_path[PATH_MAX]; */
/* static char jobstep_cgroup_path[PATH_MAX]; */
/* static char task_cgroup_path[PATH_MAX]; */

/* static xcgroup_ns_t blkio_ns; */

/* static xcgroup_t user_blkio_cg; */
/* static xcgroup_t job_blkio_cg; */
/* static xcgroup_t step_blkio_cg; */
/* xcgroup_t task_blkio_cg; */


/* extern int jobacct_gather_cgroup_blkio_init( */
/* 	slurm_cgroup_conf_t *slurm_cgroup_conf) */
/* { */
/* 	/\* initialize user/job/jobstep cgroup relative paths *\/ */
/* 	user_cgroup_path[0]='\0'; */
/* 	job_cgroup_path[0]='\0'; */
/* 	jobstep_cgroup_path[0]='\0'; */

/* 	/\* initialize blkio cgroup namespace *\/ */
/* 	if (xcgroup_ns_create(slurm_cgroup_conf, &blkio_ns, "", "blkio") */
/* 	    != XCGROUP_SUCCESS) { */
/* 		error("jobacct_gather/cgroup: unable to create blkio " */
/* 		      "namespace"); */
/* 		return SLURM_ERROR; */
/* 	} */
/* 	return SLURM_SUCCESS; */
/* } */

/* extern int jobacct_gather_cgroup_blkio_fini( */
/* 	slurm_cgroup_conf_t *slurm_cgroup_conf) */
/* { */
/* 	if (user_cgroup_path[0] == '\0' || */
/* 	    job_cgroup_path[0] == '\0' || */
/* 	    jobstep_cgroup_path[0] == '\0') */
/* 		return SLURM_SUCCESS; */

/* 	xcgroup_destroy(&user_blkio_cg); */
/* 	xcgroup_destroy(&job_blkio_cg); */
/* 	xcgroup_destroy(&step_blkio_cg); */

/* 	user_cgroup_path[0]='\0'; */
/* 	job_cgroup_path[0]='\0'; */
/* 	jobstep_cgroup_path[0]='\0'; */

/* 	xcgroup_ns_destroy(&blkio_ns); */

/* 	return SLURM_SUCCESS; */
/* } */

/* extern int jobacct_gather_cgroup_blkio_attach_task( */
/* 	pid_t pid, jobacct_id_t *jobacct_id) */
/* { */
/* 	xcgroup_t blkio_cg; */
/* 	stepd_step_rec_t *job; */
/* 	uid_t uid; */
/* 	gid_t gid; */
/* 	uint32_t jobid; */
/* 	uint32_t stepid; */
/* 	uint32_t taskid; */
/* 	int fstatus = SLURM_SUCCESS; */
/* 	int rc; */
/* 	char* slurm_cgpath; */

/* 	job = jobacct_id->job; */
/* 	uid = job->uid; */
/* 	gid = job->gid; */
/* 	jobid = job->jobid; */
/* 	stepid = job->stepid; */
/* 	taskid = jobacct_id->taskid; */

/* 	/\* create slurm root cg in this cg namespace *\/ */
/* 	slurm_cgpath = jobacct_cgroup_create_slurm_cg(&blkio_ns); */
/* 	if (!slurm_cgpath) { */
/* 		return SLURM_ERROR; */
/* 	} */

/* 	/\* build user cgroup relative path if not set (should not be) *\/ */
/* 	if (*user_cgroup_path == '\0') { */
/* 		if (snprintf(user_cgroup_path, PATH_MAX, */
/* 			     "%s/uid_%u", slurm_cgpath, uid) >= PATH_MAX) { */
/* 			error("unable to build uid %u cgroup relative " */
/* 			      "path : %m", uid); */
/* 			xfree(slurm_cgpath); */
/* 			return SLURM_ERROR; */
/* 		} */
/* 	} */

/* 	/\* build job cgroup relative path if not set (may not be) *\/ */
/* 	if (*job_cgroup_path == '\0') { */
/* 		if (snprintf(job_cgroup_path, PATH_MAX, "%s/job_%u", */
/* 			     user_cgroup_path, jobid) >= PATH_MAX) { */
/* 			error("jobacct_gather/cgroup: unable to build job %u " */
/* 			      "blkio cg relative path : %m", jobid); */
/* 			return SLURM_ERROR; */
/* 		} */
/* 	} */

/* 	/\* build job step cgroup relative path if not set (may not be) *\/ */
/* 	if (*jobstep_cgroup_path == '\0') { */
/*		int len; */
/*		if (stepid == SLURM_BATCH_SCRIPT) { */
/*			len = snprintf(jobstep_cgroup_path, PATH_MAX, */
/*				       "%s/step_batch", job_cgroup_path); */
/*		} else if (stepid == SLURM_EXTERN_CONT) { */
/*			len = snprintf(jobstep_cgroup_path, PATH_MAX, */
/*				       "%s/step_extern", job_cgroup_path); */
/*		} else { */
/*			len = snprintf(jobstep_cgroup_path, PATH_MAX, */
/*				       "%s/step_%u", */
/*				       job_cgroup_path, stepid); */
/*		} */
/*		if (len >= PATH_MAX) { */
/* 			error("jobacct_gather/cgroup: unable to build job step " */
/* 			      "%u.%u blkio cg relative path : %m", */
/*			      jobid, stepid); */
/* 			return SLURM_ERROR; */
/* 		} */
/* 	} */

/* 	/\* build task cgroup relative path *\/ */
/* 	if (snprintf(task_cgroup_path, PATH_MAX, "%s/task_%u", */
/* 		     jobstep_cgroup_path, taskid) >= PATH_MAX) { */
/* 		error("jobacct_gather/cgroup: unable to build task %u " */
/* 		      "blkio cg relative path : %m", taskid); */
/* 		return SLURM_ERROR; */
/* 	} */

/* 	fstatus = SLURM_SUCCESS; */

/* 	/\* */
/* 	 * create blkio root cg and lock it */
/* 	 * */
/* 	 * we will keep the lock until the end to avoid the effect of a release */
/* 	 * agent that would remove an existing cgroup hierarchy while we are */
/* 	 * setting it up. As soon as the step cgroup is created, we can release */
/* 	 * the lock. */
/* 	 * Indeed, consecutive slurm steps could result in cg being removed */
/* 	 * between the next EEXIST instanciation and the first addition of */
/* 	 * a task. The release_agent will have to lock the root blkio cgroup */
/* 	 * to avoid this scenario. */
/* 	 *\/ */

/* 	if (xcgroup_create(&blkio_ns, &blkio_cg, "", 0, 0) */
/* 	    != XCGROUP_SUCCESS) { */
/* 		error("jobacct_gather/cgroup: unable to create root blkio " */
/* 		      "xcgroup"); */
/* 		return SLURM_ERROR; */
/* 	} */
/* 	if (xcgroup_lock(&blkio_cg) != XCGROUP_SUCCESS) { */
/* 		xcgroup_destroy(&blkio_cg); */
/* 		error("jobacct_gather/cgroup: unable to lock root blkio cg"); */
/* 		return SLURM_ERROR; */
/* 	} */

/* 	/\* */
/* 	 * Create user cgroup in the blkio ns (it could already exist) */
/* 	 *\/ */
/* 	if (xcgroup_create(&blkio_ns, &user_blkio_cg, */
/* 			   user_cgroup_path, */
/* 			   uid, gid) != XCGROUP_SUCCESS) { */
/* 		error("jobacct_gather/cgroup: unable to create user %u blkio " */
/* 		      "cgroup", uid); */
/* 		fstatus = SLURM_ERROR; */
/* 		goto error; */
/* 	} */

/* 	if (xcgroup_instantiate(&user_blkio_cg) != XCGROUP_SUCCESS) { */
/* 		xcgroup_destroy(&user_blkio_cg); */
/* 		error("jobacct_gather/cgroup: unable to instantiate user %u " */
/* 		      "blkio cgroup", uid); */
/* 		fstatus = SLURM_ERROR; */
/* 		goto error; */
/* 	} */

/* 	/\* */
/* 	 * Create job cgroup in the blkio ns (it could already exist) */
/* 	 *\/ */
/* 	if (xcgroup_create(&blkio_ns, &job_blkio_cg, */
/* 			   job_cgroup_path, */
/* 			   uid, gid) != XCGROUP_SUCCESS) { */
/* 		xcgroup_destroy(&user_blkio_cg); */
/* 		error("jobacct_gather/cgroup: unable to create job %u blkio " */
/* 		      "cgroup", jobid); */
/* 		fstatus = SLURM_ERROR; */
/* 		goto error; */
/* 	} */

/* 	if (xcgroup_instantiate(&job_blkio_cg) != XCGROUP_SUCCESS) { */
/* 		xcgroup_destroy(&user_blkio_cg); */
/* 		xcgroup_destroy(&job_blkio_cg); */
/* 		error("jobacct_gather/cgroup: unable to instantiate job %u " */
/* 		      "blkio cgroup", jobid); */
/* 		fstatus = SLURM_ERROR; */
/* 		goto error; */
/* 	} */

/* 	/\* */
/* 	 * Create step cgroup in the blkio ns (it could already exist) */
/* 	 *\/ */
/* 	if (xcgroup_create(&blkio_ns, &step_blkio_cg, */
/* 			   jobstep_cgroup_path, */
/* 			   uid, gid) != XCGROUP_SUCCESS) { */
/* 		/\* do not delete user/job cgroup as they can exist for other */
/* 		 * steps, but release cgroup structures *\/ */
/* 		xcgroup_destroy(&user_blkio_cg); */
/* 		xcgroup_destroy(&job_blkio_cg); */
/* 		error("jobacct_gather/cgroup: unable to create jobstep %u.%u " */
/* 		      "blkio cgroup", jobid, stepid); */
/* 		fstatus = SLURM_ERROR; */
/* 		goto error; */
/* 	} */

/* 	if (xcgroup_instantiate(&step_blkio_cg) != XCGROUP_SUCCESS) { */
/* 		xcgroup_destroy(&user_blkio_cg); */
/* 		xcgroup_destroy(&job_blkio_cg); */
/* 		xcgroup_destroy(&step_blkio_cg); */
/* 		error("jobacct_gather/cgroup: unable to instantiate jobstep " */
/* 		      "%u.%u blkio cgroup", jobid, stepid); */
/* 		fstatus = SLURM_ERROR; */
/* 		goto error; */
/* 	} */

/* 	/\* */
/* 	 * Create task cgroup in the blkio ns */
/* 	 *\/ */
/* 	if (xcgroup_create(&blkio_ns, &task_blkio_cg, */
/* 			   task_cgroup_path, */
/* 			   uid, gid) != XCGROUP_SUCCESS) { */
/* 		/\* do not delete user/job cgroup as they can exist for other */
/* 		 * steps, but release cgroup structures *\/ */
/* 		xcgroup_destroy(&user_blkio_cg); */
/* 		xcgroup_destroy(&job_blkio_cg); */
/* 		error("jobacct_gather/cgroup: unable to create jobstep %u.%u " */
/* 		      "task %u blkio cgroup", jobid, stepid, taskid); */
/* 		fstatus = SLURM_ERROR; */
/* 		goto error; */
/* 	} */

/* 	if (xcgroup_instantiate(&task_blkio_cg) != XCGROUP_SUCCESS) { */
/* 		xcgroup_destroy(&user_blkio_cg); */
/* 		xcgroup_destroy(&job_blkio_cg); */
/* 		xcgroup_destroy(&step_blkio_cg); */
/* 		error("jobacct_gather/cgroup: unable to instantiate jobstep " */
/* 		      "%u.%u task %u blkio cgroup", jobid, stepid, taskid); */
/* 		fstatus = SLURM_ERROR; */
/* 		goto error; */
/* 	} */

/* 	/\* */
/* 	 * Attach the slurmstepd to the task blkio cgroup */
/* 	 *\/ */
/* 	rc = xcgroup_add_pids(&task_blkio_cg, &pid, 1); */
/* 	if (rc != XCGROUP_SUCCESS) { */
/* 		error("jobacct_gather/cgroup: unable to add slurmstepd to " */
/* 		      "blkio cg '%s'", task_blkio_cg.path); */
/* 		fstatus = SLURM_ERROR; */
/* 	} else */
/* 		fstatus = SLURM_SUCCESS; */

/* error: */
/* 	xcgroup_unlock(&blkio_cg); */
/* 	xcgroup_destroy(&blkio_cg); */
/* 	return fstatus; */
/* } */
