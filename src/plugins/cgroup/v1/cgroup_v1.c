/*****************************************************************************\
 *  cgroup_v1.c - Cgroup v1 plugin
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Felip Moll <felip.moll@schedmd.com>
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

#define _GNU_SOURCE

#include "cgroup_v1.h"

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for Slurm node selection) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Cgroup v1 plugin";
const char plugin_type[] = "cgroup/v1";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static char g_user_cgpath[CG_CTL_CNT][PATH_MAX];
static char g_job_cgpath[CG_CTL_CNT][PATH_MAX];
static char g_step_cgpath[CG_CTL_CNT][PATH_MAX];

static xcgroup_ns_t g_cg_ns[CG_CTL_CNT];

static xcgroup_t g_root_cg[CG_CTL_CNT];
static xcgroup_t g_user_cg[CG_CTL_CNT];
static xcgroup_t g_job_cg[CG_CTL_CNT];
static xcgroup_t g_step_cg[CG_CTL_CNT];

const char *g_cg_name[CG_CTL_CNT] = {
	"freezer",
	"cpuset",
	"memory",
	"devices",
	"cpuacct"
};

static int _cgroup_init(cgroup_ctl_type_t sub)
{
	if (sub >= CG_CTL_CNT)
		return SLURM_ERROR;

	if (xcgroup_ns_create(&g_cg_ns[sub], "", g_cg_name[sub])
	    != SLURM_SUCCESS) {
		error("unable to create %s cgroup namespace", g_cg_name[sub]);
		return SLURM_ERROR;
	}

	if (xcgroup_create(&g_cg_ns[sub], &g_root_cg[sub], "", 0, 0)
	    != SLURM_SUCCESS) {
		error("unable to create root %s xcgroup", g_cg_name[sub]);
		xcgroup_ns_destroy(&g_cg_ns[sub]);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _cpuset_create(stepd_step_rec_t *job)
{
	int rc;
	char *slurm_cgpath;
	xcgroup_t slurm_cg;
	char *value;
	size_t cpus_size;

	/* create slurm root cg in this cg namespace */
	slurm_cgpath = xcgroup_create_slurm_cg(&g_cg_ns[CG_CPUS]);
	if (slurm_cgpath == NULL)
		return SLURM_ERROR;

	/* check that this cgroup has cpus allowed or initialize them */
	if (xcgroup_load(&g_cg_ns[CG_CPUS], &slurm_cg, slurm_cgpath) !=
	    SLURM_SUCCESS){
		error("unable to load slurm cpuset xcgroup");
		xfree(slurm_cgpath);
		return SLURM_ERROR;
	}

	rc = xcgroup_get_param(&slurm_cg, "cpuset.cpus", &value, &cpus_size);

	if ((rc != SLURM_SUCCESS) || (cpus_size == 1)) {
		/* initialize the cpusets as it was non-existent */
		if (xcgroup_cpuset_init(&slurm_cg) != SLURM_SUCCESS) {
			xfree(slurm_cgpath);
			xcgroup_destroy(&slurm_cg);
			return SLURM_ERROR;
		}
	}
	xfree(slurm_cgpath);
	xcgroup_destroy(&slurm_cg);

	rc = xcgroup_create_hierarchy(__func__,
				      job,
				      &g_cg_ns[CG_CPUS],
				      &g_job_cg[CG_CPUS],
				      &g_step_cg[CG_CPUS],
				      &g_user_cg[CG_CPUS],
				      g_job_cgpath[CG_CPUS],
				      g_step_cgpath[CG_CPUS],
				      g_user_cgpath[CG_CPUS],
				      NULL, NULL);
	return rc;
}

static int _remove_cg_subsystem(xcgroup_t root_cg, xcgroup_t step_cg,
				xcgroup_t job_cg, xcgroup_t user_cg,
				xcgroup_t move_to_cg, const char *log_str,
				xcgroup_t remove_from_cg)
{
	int rc = SLURM_SUCCESS;

	/*
	 * Always try to move slurmstepd process to the root cgroup, otherwise
	 * the rmdir(2) triggered by the calls below will always fail if the pid
	 * of stepd is in the cgroup. We don't know what other plugins will do
	 * and whether they will attach the stepd pid to the cg.
	 */
	rc = xcgroup_move_process(&move_to_cg, getpid());
	if (rc != SLURM_SUCCESS) {
		error("Unable to move pid %d to root cgroup", getpid());
		goto end;
	}
	xcgroup_wait_pid_moved(&remove_from_cg, log_str);

	/*
	 * Lock the root cgroup so we don't race with other steps that are being
	 * started.
	 */
	if (xcgroup_lock(&root_cg) != SLURM_SUCCESS) {
		error("xcgroup_lock error (%s)", log_str);
		return SLURM_ERROR;
	}

	/* Delete step cgroup. */
	if ((rc = xcgroup_delete(&step_cg)) != SLURM_SUCCESS) {
		debug2("unable to remove step cg (%s): %m", log_str);
		goto end;
	}

	/*
	 * At this point we'll do a best effort for the job and user cgroup,
	 * since other jobs or steps may still be alive and not let us complete
	 * the cleanup. The last job/step in the hierarchy will be the one which
	 * will finally remove these two directories
	 */
	/* Delete job cgroup. */
	if ((rc = xcgroup_delete(&job_cg)) != SLURM_SUCCESS) {
		debug2("not removing job cg (%s): %m", log_str);
		rc = SLURM_SUCCESS;
		goto end;
	}
	/* Delete user cgroup. */
	if ((rc = xcgroup_delete(&user_cg)) != SLURM_SUCCESS) {
		debug2("not removing user cg (%s): %m", log_str);
		rc = SLURM_SUCCESS;
		goto end;
	}

	/*
	 * Invalidate the cgroup structs.
	 */
	xcgroup_destroy(&user_cg);
	xcgroup_destroy(&job_cg);
	xcgroup_destroy(&step_cg);

end:
	xcgroup_unlock(&root_cg);
	return rc;
}

extern int init(void)
{
	int i;

	for (i = 0; i < CG_CTL_CNT; i++) {
		g_user_cgpath[i][0] = '\0';
		g_job_cgpath[i][0] = '\0';
		g_step_cgpath[i][0] = '\0';
	}

	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	debug("unloading %s", plugin_name);
	return SLURM_SUCCESS;
}

extern int cgroup_p_initialize(cgroup_ctl_type_t sub)
{
	int rc = SLURM_SUCCESS;

	rc = _cgroup_init(sub);

	switch (sub) {
	case CG_TRACK:
	case CG_CPUS:
		break;
	case CG_MEMORY:
		xcgroup_set_param(&g_root_cg[sub], "memory.use_hierarchy", "1");
		break;
	case CG_DEVICES:
	case CG_CPUACCT:
		break;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

/*
 * cgroup_p_step_create - Description Description Description
 * IN/OUT param1 - description description
 * IN param2 - description description
 * RET zero on success, EINVAL otherwise
 */
extern int cgroup_p_step_create(cgroup_ctl_type_t sub, stepd_step_rec_t *job)
{
	switch (sub) {
	case CG_TRACK:
		/* create a new cgroup for that container */
		if (xcgroup_create_hierarchy(__func__,
					     job,
					     &g_cg_ns[sub],
					     &g_job_cg[sub],
					     &g_step_cg[sub],
					     &g_user_cg[sub],
					     g_job_cgpath[sub],
					     g_step_cgpath[sub],
					     g_user_cgpath[sub],
					     NULL, NULL)
		    != SLURM_SUCCESS) {
			return SLURM_ERROR;
		}

		/* stick slurmstepd pid to the newly created job container
		 * (Note: we do not put it in the step container because this
		 * container could be used to suspend/resume tasks using freezer
		 * properties so we need to let the slurmstepd outside of
		 * this one)
		 */
		if (xcgroup_add_pids(&g_job_cg[sub], &job->jmgr_pid, 1) !=
		    SLURM_SUCCESS) {
			cgroup_p_step_destroy(sub);
			return SLURM_ERROR;
		}

		/* we use slurmstepd pid as the identifier of the container */
		job->cont_id = (uint64_t)job->jmgr_pid;
		break;
	case CG_CPUS:
		return _cpuset_create(job);
	case CG_MEMORY:
		break;
	case CG_DEVICES:
		/* create a new cgroup for that container */
		if (xcgroup_create_hierarchy(__func__,
					     job,
					     &g_cg_ns[sub],
					     &g_job_cg[sub],
					     &g_step_cg[sub],
					     &g_user_cg[sub],
					     g_job_cgpath[sub],
					     g_step_cgpath[sub],
					     g_user_cgpath[sub],
					     NULL, NULL) != SLURM_SUCCESS) {
			return SLURM_ERROR;
		}
		break;
	case CG_CPUACCT:
		error("This operation is not supported for %s", g_cg_name[sub]);
		return SLURM_ERROR;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		return SLURM_ERROR;
		break;
	}

	return SLURM_SUCCESS;
}

extern int cgroup_p_step_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids)
{
	if (*g_step_cgpath[sub] == '\0')
		return SLURM_ERROR;

	switch (sub) {
	case CG_TRACK:
	case CG_CPUS:
	case CG_MEMORY:
	case CG_DEVICES:
		break;
	case CG_CPUACCT:
		error("This operation is not supported for %s", g_cg_name[sub]);
		return SLURM_ERROR;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		return SLURM_ERROR;
	}

	return xcgroup_add_pids(&g_step_cg[sub], pids, npids);
}

extern int cgroup_p_step_get_pids(pid_t **pids, int *npids)
{
	if (*g_step_cgpath[CG_TRACK] == '\0')
		return SLURM_ERROR;

	return xcgroup_get_pids(&g_step_cg[CG_TRACK], pids, npids);
}

extern int cgroup_p_step_suspend()
{
	if (*g_step_cgpath[CG_TRACK] == '\0')
		return SLURM_ERROR;

	return xcgroup_set_param(&g_step_cg[CG_TRACK], "freezer.state",
				 "FROZEN");
}

extern int cgroup_p_step_resume()
{
	if (*g_step_cgpath[CG_TRACK] == '\0')
		return SLURM_ERROR;

	return xcgroup_set_param(&g_step_cg[CG_TRACK], "freezer.state",
				 "THAWED");
}

extern int cgroup_p_step_destroy(cgroup_ctl_type_t sub)
{
	int rc = SLURM_SUCCESS;

	/* Another plugin may have already destroyed this subsystem. */
	if (!g_root_cg[sub].path)
		return SLURM_ERROR;

	/* Custom actions for every cgroup subsystem */
	switch (sub) {
	case CG_TRACK:
	case CG_CPUS:
	case CG_MEMORY:
	case CG_DEVICES:
	case CG_CPUACCT:
		break;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		return SLURM_ERROR;
		break;
	}

	rc = _remove_cg_subsystem(g_root_cg[sub],
				  g_step_cg[sub],
				  g_job_cg[sub],
				  g_user_cg[sub],
				  g_root_cg[sub],
				  g_cg_name[sub],
				  g_step_cg[sub]);

	if (rc == SLURM_SUCCESS) {
		xcgroup_destroy(&g_root_cg[sub]);
		xcgroup_ns_destroy(&g_cg_ns[sub]);
	}

	return rc;
}

/*
 * Is the specified pid in our cgroup g_cg_ns[CG_TRACK]?
 * In the future we may want to replace this with a get pids and a search.
 */
extern bool cgroup_p_has_pid(pid_t pid)
{
	bool rc;
	xcgroup_t cg;

	rc = xcgroup_ns_find_by_pid(&g_cg_ns[CG_TRACK], &cg, pid);
	if (rc != SLURM_SUCCESS)
		return false;

	rc = true;
	if (xstrcmp(cg.path, g_step_cg[CG_TRACK].path))
		rc = false;

	xcgroup_destroy(&cg);
	return rc;
}

extern cgroup_limits_t *cgroup_p_root_constrain_get(cgroup_ctl_type_t sub)
{
	int rc = SLURM_SUCCESS;
	cgroup_limits_t *limits = xmalloc(sizeof(*limits));

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		rc = xcgroup_get_param(&g_root_cg[CG_CPUS], "cpuset.cpus",
				       &limits->allow_cores,
				       &limits->cores_size);

		rc += xcgroup_get_param(&g_root_cg[CG_CPUS], "cpuset.mems",
					&limits->allow_mems,
					&limits->mems_size);

		if (limits->cores_size > 0)
			limits->allow_cores[(limits->cores_size)-1] = '\0';

		if (limits->mems_size > 0)
			limits->allow_mems[(limits->mems_size)-1] = '\0';

		if (rc != SLURM_SUCCESS)
			goto fail;
		break;
	case CG_MEMORY:
	case CG_DEVICES:
		break;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		rc = SLURM_ERROR;
		break;
	}

	return limits;
fail:
	cgroup_free_limits(limits);
	return NULL;
}

extern int cgroup_p_root_constrain_set(cgroup_ctl_type_t sub,
				       cgroup_limits_t *limits)
{
	int rc = SLURM_SUCCESS;

	if (!limits)
		return SLURM_ERROR;

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		break;
	case CG_MEMORY:
		rc = xcgroup_set_uint64_param(&g_root_cg[CG_MEMORY],
					      "memory.swappiness",
					      limits->swappiness);
		break;
	case CG_DEVICES:
		break;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int cgroup_p_user_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits)
{
	int rc = SLURM_SUCCESS;

	if (!limits)
		return SLURM_ERROR;

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		rc = xcgroup_set_param(&g_user_cg[CG_CPUS], "cpuset.cpus",
				       limits->allow_cores);
		rc += xcgroup_set_param(&g_user_cg[CG_CPUS], "cpuset.mems",
					limits->allow_mems);
		break;
	case CG_MEMORY:
		break;
	case CG_DEVICES:
		break;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int cgroup_p_job_constrain_set(cgroup_ctl_type_t sub,
				      stepd_step_rec_t *job,
				      cgroup_limits_t *limits)
{
	int rc = SLURM_SUCCESS;

	if (!limits)
		return SLURM_ERROR;

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		rc = xcgroup_set_param(&g_job_cg[CG_CPUS], "cpuset.cpus",
				       limits->allow_cores);
		rc += xcgroup_set_param(&g_job_cg[CG_CPUS], "cpuset.mems",
					limits->allow_mems);
		break;
	case CG_MEMORY:
		break;
	case CG_DEVICES:
		if (limits->allow_device)
			rc = xcgroup_set_param(&g_job_cg[CG_DEVICES],
					      "devices.allow",
					      limits->device_major);
		else
			rc = xcgroup_set_param(&g_job_cg[CG_DEVICES],
					      "devices.deny",
					      limits->device_major);
		break;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int cgroup_p_step_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_NATIVE_CRAY
	char expected_usage[32];
#endif

	if (!limits)
		return SLURM_ERROR;

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		rc = xcgroup_set_param(&g_step_cg[CG_CPUS], "cpuset.cpus",
				       limits->allow_cores);
		rc += xcgroup_set_param(&g_step_cg[CG_CPUS], "cpuset.mems",
				       limits->allow_mems);
#ifdef HAVE_NATIVE_CRAY
		/*
		 * on Cray systems, set the expected usage in bytes.
		 * This is used by the Cray OOM killer
		 */
		snprintf(expected_usage, sizeof(expected_usage), "%"PRIu64,
			 (uint64_t)job->step_mem * 1024 * 1024);

		rc += xcgroup_set_param(&g_step_cg[CG_CPUS],
					"cpuset.expected_usage_in_bytes",
					expected_usage);
#endif
		break;
	case CG_MEMORY:
		break;
	case CG_DEVICES:
		if (limits->allow_device)
			rc = xcgroup_set_param(&g_step_cg[CG_DEVICES],
					      "devices.allow",
					      limits->device_major);
		else
			rc = xcgroup_set_param(&g_step_cg[CG_DEVICES],
					      "devices.deny",
					      limits->device_major);
		break;
	default:
		error("cgroup subsystem %"PRIu16" not supported", sub);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int cgroup_p_step_start_oom_mgr()
{
	return SLURM_SUCCESS;
}

extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *job)
{
	return NULL;
}

extern int cgroup_p_accounting_init()
{
	return SLURM_SUCCESS;
}

extern int cgroup_p_accounting_fini()
{
	return SLURM_SUCCESS;
}

extern int cgroup_p_task_addto_accounting(pid_t pid, stepd_step_rec_t *job,
					  uint32_t task_id)
{
	return SLURM_SUCCESS;
}

extern cgroup_acct_t *cgroup_p_task_get_acct_data(uint32_t taskid)
{
	return NULL;
}
