/*****************************************************************************\
 *  task_cgroup_cpuset.c - cpuset cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include "affinity.h"

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/log.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "task_cgroup_cpuset.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xcgroup.h"
#include "src/common/xcpuinfo.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#define CGROUP_SLURMDIR CGROUP_BASEDIR "/slurm"

static xcgroup_ns_t cpuset_ns;

static xcgroup_t user_cpuset_cg;
static xcgroup_t job_cpuset_cg;
static xcgroup_t step_cpuset_cg;
static xcgroup_t task_cpuset_cg;

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];
static char task_cgroup_path[PATH_MAX];
static char release_agent_path[PATH_MAX];

int task_cgroup_cpuset_init()
{
	/* initialize user/job/jobstep cgroup relative paths and release agent path */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';
	release_agent_path[0]='\0';

	/* build cpuset release agent path */
	if ( snprintf(release_agent_path,PATH_MAX,"%s/release_cpuset",
		      slurm_cgroup_conf->cgroup_release_agent) >= PATH_MAX ) {
		error("unable to build cgroup cpuset release agent path");
		return SLURM_ERROR;
	}

	/* initialize cpuset cgroup namespace */
	if ( xcgroup_ns_create(&cpuset_ns,CGROUP_SLURMDIR "/cpuset","",
			       "cpuset",release_agent_path) != XCGROUP_SUCCESS ) {
		error("unable to create cpuset cgroup namespace");
		return SLURM_ERROR;
	}

	/* check that cpuset cgroup namespace is available */
	if ( ! xcgroup_ns_is_available(&cpuset_ns) ) {
		if ( xcgroup_ns_mount(&cpuset_ns) ) {
			error("unable to mount cpuset cgroup namespace");
			return SLURM_ERROR;
		}
	}
	info("cpuset cgroup namespace now mounted");

	return SLURM_SUCCESS;
}

int task_cpuset_ns_is_available()
{
	return xcgroup_ns_is_available(&cpuset_ns);
}

int	task_build_cgroup_cpuset(slurmd_job_t *job, uid_t uid, gid_t gid)
{
	int fstatus;

	/* create cgroups for this job */
	fstatus = task_create_cgroup_cpuset(job,(uint32_t)job->jmgr_pid,
				       job->uid,job->gid);
	if ( fstatus ) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

int task_create_cgroup_cpuset(slurmd_job_t *job,
		                      uint32_t id,uid_t uid,gid_t gid)
{
	/* build user cgroup relative path if not set (may not be) */
	if ( *user_cgroup_path == '\0' ) {
		if ( snprintf(user_cgroup_path,PATH_MAX,
			      "/uid_%u",uid) >= PATH_MAX ) {
			error("unable to build uid %u cgroup relative "
			      "path : %m",uid);
			return SLURM_ERROR;
		}
	}

	/* build job cgroup relative path if not set (may not be) */
	if ( *job_cgroup_path == '\0' ) {
		if ( snprintf(job_cgroup_path,PATH_MAX,"%s/job_%u",
			      user_cgroup_path,job->jobid) >= PATH_MAX ) {
			error("unable to build job %u cgroup relative "
			      "path : %m",job->jobid);
			return SLURM_ERROR;
		}
	}

	/* build job step cgroup relative path (may not be) */
	if ( *jobstep_cgroup_path == '\0' ) {
		if ( snprintf(jobstep_cgroup_path,PATH_MAX,"%s/step_%u",
			      job_cgroup_path,job->stepid) >= PATH_MAX ) {
			error("unable to build job step %u cgroup relative path"
			      " : %m",job->stepid);
			return SLURM_ERROR;
		}
	}

	/* create user cgroup in the cpuset ns (it could already exist) */
	if ( xcgroup_create(&cpuset_ns,&user_cpuset_cg,
			    user_cgroup_path,
			    getuid(),getgid()) != XCGROUP_SUCCESS ) {
		return SLURM_ERROR;
	}
	if ( xcgroup_instanciate(&user_cpuset_cg) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_cpuset_cg);
		return SLURM_ERROR;
	}
/*	if ( slurm_cgroup_conf->user_cgroup_params )
		xcgroup_set_params(&user_cpuset_cg,
				   slurm_cgroup_conf->user_cgroup_params); */

	/* create job cgroup in the cpuset ns (it could already exist) */
	if ( xcgroup_create(&cpuset_ns,&job_cpuset_cg,
			    job_cgroup_path,
			    getuid(),getgid()) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_cpuset_cg);
		return SLURM_ERROR;
	}
	if ( xcgroup_instanciate(&job_cpuset_cg) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		return SLURM_ERROR;
	}
/*	if ( slurm_cgroup_conf->job_cgroup_params )
		xcgroup_set_params(&job_cpuset_cg,
				   slurm_cgroup_conf->job_cgroup_params); */

	/* create step cgroup in the cpuset ns (it should not exist) */
	if ( xcgroup_create(&cpuset_ns,&step_cpuset_cg,
			    jobstep_cgroup_path,
			    getuid(),getgid()) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		return SLURM_ERROR;
	}
	if ( xcgroup_instanciate(&step_cpuset_cg) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		xcgroup_destroy(&step_cpuset_cg);
		return SLURM_ERROR;
	}
/*	if ( slurm_cgroup_conf->jobstep_cgroup_params )
		xcgroup_set_params(&step_cpuset_cg,
				   slurm_cgroup_conf->jobstep_cgroup_params); */
	return SLURM_SUCCESS;
}

int	task_set_cgroup_cpuset(int task, pid_t pid, size_t size,
			 const cpu_set_t *mask)
{
	char cpustr[1 + CPU_SETSIZE * 4];

	/* build task cgroup relative path  */
	if ( snprintf(task_cgroup_path,PATH_MAX,"%s/task_%u",
			      jobstep_cgroup_path,task) >= PATH_MAX ) {
			error("unable to build task %u cgroup relative path"
			      " : %m",pid);
			return SLURM_ERROR;
	}

	/* create task cgroup in the cpuset ns (it should not exist) */
	if ( xcgroup_create(&cpuset_ns,&task_cpuset_cg,
			    task_cgroup_path,
			    getuid(),getgid()) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		xcgroup_destroy(&step_cpuset_cg);
		return SLURM_ERROR;
	}
	if ( xcgroup_instanciate(&task_cpuset_cg) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		xcgroup_destroy(&step_cpuset_cg);
		xcgroup_destroy(&task_cpuset_cg);
		return SLURM_ERROR;
	}
/*	if ( slurm_cgroup_conf->task_cgroup_params )
		xcgroup_set_params(&task_cpuset_cg,
				   slurm_cgroup_conf->task_cgroup_params); */

 	_cpuset_to_cpustr(mask, cpustr);
	if (xcgroup_set_cpuset_cpus(&task_cpuset_cg, &cpustr[0]) != XCGROUP_SUCCESS)
		return SLURM_ERROR;

	if (xcgroup_add_pids(&task_cpuset_cg, &pid, 1) != XCGROUP_SUCCESS)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

int task_get_cgroup_cpuset(pid_t pid, size_t size, cpu_set_t *mask)
{
	char cpustr[1 + CPU_SETSIZE * 4];

	xcgroup_get_cpuset_cpus(&task_cpuset_cg, &cpustr[0]);
	str_to_cpuset(mask, cpustr);
    /* FIXME: verify that pid is in tasks file */
    return SLURM_SUCCESS;
}

void _cpuset_to_cpustr(const cpu_set_t *mask, char *str)
{
	int i;
	char tmp[16];

	str[0] = '\0';
	for (i=0; i<CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, mask))
			continue;
		snprintf(tmp, sizeof(tmp), "%d", i);
		if (str[0])
			strcat(str, ",");
		strcat(str, tmp);
	}
}
