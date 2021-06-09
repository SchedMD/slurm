/*****************************************************************************\
 *  cgroup_v2.c - Cgroup v2 plugin
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

#include "cgroup_v2.h"

#define SLURMD_CGDIR "/system"
#define JOBS_CGDIR "/jobs"

const char plugin_name[] = "Cgroup v2 plugin";
const char plugin_type[] = "cgroup/v2";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static xcgroup_ns_t g_cg_ns;

static xcgroup_t g_root_cg;
static xcgroup_t g_job_cg;
static xcgroup_t g_step_cg;
static xcgroup_t g_step_user_cg;
static xcgroup_t g_step_slurm_cg;
static xcgroup_t g_sys_cg;

static cgroup_oom_t *g_oom_step_results = NULL;

static bool avail_controllers[CG_CTL_CNT];
static bool enabled_controllers[CG_CTL_CNT];
const char *g_ctl_name[CG_CTL_CNT] = {
	"",
	"cpuset",
	"memory",
	"",
	"cpu"
};

/*
 * The cgroup v2 documented way to know which is the process root in the cgroup
 * hierarchy, is just to read /proc/self/cgroup. In Unified hierarchies this
 * must contain only one line. If there are more lines this would mean we are
 * in Hybrid or in Legacy cgroup.
 */
static void _set_g_cg_ns()
{
	char *buf, *start = NULL, *p;
	size_t sz;
	int fs;
	struct stat st;

	fs = common_file_read_content("/proc/self/cgroup", &buf, &sz);
	if (fs != SLURM_SUCCESS)
		debug2("cannot read /proc/self/cgroup contents: %m");

	/*
	 * In Unified mode there will be just one line containing the path
	 * of the cgroup, so get it as our root and replace the \n:
	 * "0::/system.slice/slurmd<nodename>.service\n"
	 *
	 * The final path will look like this:
	 * /sys/fs/cgroup/system.slice/slurmd.service/
	 *
	 * If we have multiple slurmd, we will likely have one unit file per
	 * node, and the path takes the name of the service file, e.g:
	 * /sys/fs/cgroup/system.slice/slurmd-<nodename>.service/
	 */
	if ((p = xstrchr(buf, ':')) != NULL) {
		if ((p + 2) < (buf + sz - 1))
			start = p + 2;
	}

	if (start && *start != '\0') {
		if ((p = xstrchr(start, '\n')))
			*p = '\0';

		/*
		 * Note: if we are slurmstepd, we'll be already in /system
		 * because we've been initiated by slurmd. So strip the last
		 * directory in that case.
		 */
		if (running_in_slurmstepd()) {
			if (xstrcasecmp(xbasename(start), "system"))
				goto err;
			p = xstrrchr(start, '/');
			*p = '\0';
		}

		xstrfmtcat(g_cg_ns.mnt_point, "/sys/fs/cgroup%s", start);
		if (stat(g_cg_ns.mnt_point, &st) < 0) {
			error("cannot read cgroup path %s: %m",
			      g_cg_ns.mnt_point);
			xfree(g_cg_ns.mnt_point);
		}
	}

err:
	xfree(buf);
}

/*
 * For each available controller, enable it in this path. This operation is
 * only intended to be done in the Domain controllers, never in a leaf where
 * processes reside. Enabling the controllers will make their interfaces
 * available (e.g. the memory.*, cpu.*, cpuset.* ... files) to control the
 * cgroup.
 */
static int _enable_subtree_control(const char *path)
{
	int i, rc = SLURM_SUCCESS;
	char *param = NULL;
	xcgroup_t *parent = xmalloc(sizeof(*parent));

	parent->path = xstrdup(path);

	for (i = 0; i < CG_CTL_CNT; i++) {
		if (avail_controllers[i]) {
			xstrfmtcat(param, "+%s", g_ctl_name[i]);
			rc = common_cgroup_set_param(parent,
						     "cgroup.subtree_control",
						     param);
			xfree(param);
			if (rc != SLURM_SUCCESS) {
				error("Cannot enable %s in %s/cgroup.subtree_control",
				      g_ctl_name[i], path);
				avail_controllers[i] = false;
				rc = SLURM_ERROR;
			}
			else {
				debug("Enabled %s controller in %s",
				      g_ctl_name[i], path);
				enabled_controllers[i] = true;
			}
		}
	}

	common_cgroup_destroy(parent);
	return rc;
}

/*
 * Read the cgroup.controllers file of the root to detect which are the
 * available controllers in this system.
 */
static int _check_avail_controllers()
{
	char *buf, *ptr, *save_ptr, *ctl_filepath = NULL;
	size_t sz;
	int fs;

	xstrfmtcat(ctl_filepath, "%s/cgroup.controllers", g_cg_ns.mnt_point);
	fs = common_file_read_content(ctl_filepath, &buf, &sz);
	if (fs != SLURM_SUCCESS || !buf) {
		error("cannot read %s: %m", ctl_filepath);
		return fs;
	}

	ptr = strtok_r(buf, " ", &save_ptr);
	while (ptr) {
		if (!xstrcasecmp("cpuset", ptr))
			avail_controllers[CG_CPUS] = true;
		else if (!xstrcasecmp("cpu", ptr))
			avail_controllers[CG_CPUACCT] = true;
		else if (!xstrcasecmp("memory", ptr))
			avail_controllers[CG_MEMORY] = true;
		ptr = strtok_r(NULL, " ", &save_ptr);
	}
	xfree(buf);

	/* Field not used in v2 */
	g_cg_ns.subsystems = NULL;

	return SLURM_SUCCESS;
}

static void _record_oom_step_stats()
{
	char *mem_events = NULL, *mem_swap_events = NULL, *ptr;
	size_t sz;
	uint64_t job_kills, step_kills, job_swkills, step_swkills;

	if (!avail_controllers[CG_MEMORY])
		return;

	/* Get latest stats for the step */
	if (common_cgroup_get_param(&g_step_user_cg, "memory.events",
				    &mem_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.events", g_step_user_cg.path);

	if (common_cgroup_get_param(&g_step_user_cg, "memory.swap.events",
				    &mem_swap_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.swap.events", g_step_user_cg.path);

	if (mem_events != NULL) {
		if ((ptr = strstr(mem_events, "oom_kill"))) {
			sscanf(ptr, "oom_kill %lu", &step_kills);
		}
		xfree(mem_events);
	}

	if (mem_swap_events != NULL) {
		if ((ptr = strstr(mem_swap_events, "fail"))) {
			sscanf(ptr, "fail %lu", &step_swkills);
		}
		xfree(mem_swap_events);
	}

	/* Get stats for the job */
	if (common_cgroup_get_param(&g_job_cg, "memory.events", &mem_events,
				    &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.events", g_step_user_cg.path);

	if (common_cgroup_get_param(&g_job_cg, "memory.swap.events",
				    &mem_swap_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.swap.events", g_step_user_cg.path);


	if (mem_events != NULL) {
		if ((ptr = strstr(mem_events, "oom_kill"))) {
			sscanf(ptr, "oom_kill %lu", &job_kills);
		}
		xfree(mem_events);
	}

	if (mem_swap_events != NULL) {
		if ((ptr = strstr(mem_swap_events, "fail"))) {
			sscanf(ptr, "fail %lu", &job_swkills);
		}
		xfree(mem_swap_events);
	}

	/* Return stats */
	g_oom_step_results = xmalloc(sizeof(*g_oom_step_results));
	g_oom_step_results->job_mem_failcnt = job_kills;
	g_oom_step_results->job_memsw_failcnt = job_swkills;
	g_oom_step_results->step_mem_failcnt = step_kills;
	g_oom_step_results->step_memsw_failcnt = step_swkills;
}

/*
 * Initialize the cgroup plugin. We need to move the slurmd process out of
 * our delegated hierarchy in order to create child. We cannot get out upper
 * in the hierarchy because of the single-writer architecture, which is under
 * systemd control. Slurmd MUST be started by systemd and the option Delegate
 * set to Yes or to the desired controllers we want to have in this system.
 * We also play the cgroup v2 game rules:
 *
 * - Top-down Constraint
 * - No Internal Process Constraint
 *
 * Read cgroup v2 documentation for more info.
 */
extern int init(void)
{
	/*
	 * Check our current root dir. Systemd MUST have Delegated it to us,
	 * so we want slurmd to be started by systemd
	 */
	_set_g_cg_ns();
	if (g_cg_ns.mnt_point == NULL)
		return SLURM_ERROR;

	/* Check available controllers in cgroup.controller and enable them. */
	if (_check_avail_controllers() != SLURM_SUCCESS)
		return SLURM_ERROR;

	/*
	 * Setup the paths for system (where slurmd will reside) and slurm
	 * (where jobs will reside).
	 */
	common_cgroup_create(&g_cg_ns, &g_sys_cg, SLURMD_CGDIR, (uid_t) 0,
			     (gid_t) 0);
	common_cgroup_create(&g_cg_ns, &g_root_cg, JOBS_CGDIR, (uid_t) 0,
			     (gid_t) 0);

	if (!running_in_slurmd())
		goto init_end;

	/*
	 * Before enabling the controllers in the parent, we need to move out
	 * the process which systemd started (slurmd) to a leaf and prepare the
	 * system for initializing stepds.
	 */
	common_cgroup_instantiate(&g_sys_cg);
	common_cgroup_move_process(&g_sys_cg, getpid());
	common_cgroup_instantiate(&g_root_cg);

	if (_enable_subtree_control(g_cg_ns.mnt_point) != SLURM_SUCCESS) {
		error("Cannot enable subtree_control at the top cg level %s",
		      g_cg_ns.mnt_point);
		return SLURM_ERROR;
	}

	/*
	 * Now we should have controllers available in the root, enable them
	 * for future childs.
	 */
	_enable_subtree_control(g_root_cg.path);

	/*
	 * We are ready now to start job steps, which will be created under
	 * g_sys_cg.path/slurmd. Per each new step we'll need to first move it
	 * out of slurmd directory.
	 */
init_end:
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	/*
	 * Clear up the namespace and cgroups memory. Don't rmdir anything since
	 * we may not be stopping yet. When the process terminates systemd will
	 * remove the directories.
	 */
	common_cgroup_ns_destroy(&g_cg_ns);
	common_cgroup_destroy(&g_sys_cg);
	common_cgroup_destroy(&g_root_cg);

	debug("unloading %s", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * Unlike in Legacy mode (v1) where we needed to create a directory for each
 * controller, in Unified mode this function will be mostly empty because the
 * hierarchy is unified into the same path. The controllers will be enabled
 * when we create the hierarchy. The only controller that may need an init. is
 * the 'devices', which in Unified is not a real controller, but instead we
 * need to register an eBPF program.
 */
extern int cgroup_p_initialize(cgroup_ctl_type_t sub)
{
	switch(sub) {
	case CG_DEVICES:
		/* initialize_and_set_ebpf_program() */
		break;
	default:
		break;
	}
	return SLURM_SUCCESS;
}

/*
 * As part of the initialization, the slurmd directory is already created, so
 * this function will remain empty.
 */
extern int cgroup_p_system_create(cgroup_ctl_type_t sub)
{
	return SLURM_SUCCESS;
}

/*
 * As part of the initialization, the slurmd directory is already created, so
 * this function will remain empty.
 */
extern int cgroup_p_system_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids)
{
	return SLURM_SUCCESS;
}

/*
 * There's no need to do any cleanup, when systemd terminates the cgroup is
 * automatically removed by systemd.
 */
extern int cgroup_p_system_destroy(cgroup_ctl_type_t sub)
{
	return SLURM_SUCCESS;
}

/*
 * Create the step hierarchy and move the stepd process into it. Further forked
 * processes will be created in the step directory already. We need to respect
 * the Top-Down constraint not adding pids to non-leaf cgroups.
 */
extern int cgroup_p_step_create(cgroup_ctl_type_t sub, stepd_step_rec_t *job)
{
	/* PSEUDO_CODE:
	 *
	 * We need an extra two directories per each step:
	 *  step_x/slurm
	 *  step_x/user
	 *
	 * And we then need to put the stepd into slurm/ because:
	 *
	 * we do not put it in the step container because this
	 * container could be used to suspend/resume tasks using freezer
	 * properties so we need to let the slurmstepd outside of
	 * this one)
	 */
	int rc = SLURM_SUCCESS;
	char *new_path = NULL;
	char tmp_char[64];

	/* Job cgroup */
	xstrfmtcat(new_path, "%s/job_%u", g_root_cg.name, job->step_id.job_id);
	if (common_cgroup_create(&g_cg_ns, &g_job_cg, new_path, 0, 0) !=
	    SLURM_SUCCESS) {
		error("unable to create job %u cgroup", job->step_id.job_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&g_job_cg) != SLURM_SUCCESS) {
		common_cgroup_destroy(&g_job_cg);
		error("unable to instantiate job %u cgroup",
		      job->step_id.job_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);
	_enable_subtree_control(g_job_cg.path);

	/* Step cgroup */
	xstrfmtcat(new_path, "%s/step_%s", g_job_cg.name,
		   log_build_step_id_str(&job->step_id, tmp_char,
					 sizeof(tmp_char),
					 STEP_ID_FLAG_NO_PREFIX |
					 STEP_ID_FLAG_NO_JOB));

	if (common_cgroup_create(&g_cg_ns, &g_step_cg, new_path, 0, 0) !=
	    SLURM_SUCCESS) {
		error("unable to create step %ps cgroup", &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&g_step_cg) != SLURM_SUCCESS) {
		common_cgroup_destroy(&g_step_cg);
		error("unable to instantiate step %ps cgroup", &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);
	_enable_subtree_control(g_step_cg.path);

	/* Step User processes cgroup */
	xstrfmtcat(new_path, "%s/user", g_step_cg.name);
	if (common_cgroup_create(&g_cg_ns, &g_step_user_cg, new_path, 0, 0) !=
	    SLURM_SUCCESS) {
		error("unable to create step %ps user procs cgroup",
		      &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&g_step_user_cg) != SLURM_SUCCESS) {
		common_cgroup_destroy(&g_step_user_cg);
		error("unable to instantiate step %ps user procs cgroup",
		      &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);

	/* Step Slurm processes cgroup */
	xstrfmtcat(new_path, "%s/slurm", g_step_cg.name);
	if (common_cgroup_create(&g_cg_ns, &g_step_slurm_cg, new_path, 0, 0) !=
	    SLURM_SUCCESS) {
		error("unable to create step %ps slurm procs cgroup",
		      &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&g_step_slurm_cg) != SLURM_SUCCESS) {
		common_cgroup_destroy(&g_step_slurm_cg);
		error("unable to instantiate step %ps slurm procs cgroup",
		      &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);

	/*
	 * We need to remove this stepd from the user processes because limits
	 * or freeze operations could affect and deadlock stepd.
	 */
	if (common_cgroup_move_process(&g_step_slurm_cg, job->jmgr_pid) !=
	    SLURM_SUCCESS) {
		error("unable to move stepd pid to its dedicated cgroup");
		rc = SLURM_ERROR;
	}
	/* Do use slurmstepd pid as the identifier of the container */
	job->cont_id = (uint64_t)job->jmgr_pid;
endit:
	return rc;
}

/*
 * Move a pid to a specific cgroup. It needs to be a leaf, we cannot move
 * a pid to an intermediate directory in the cgroup hierarchy.
 *
 * - Top-down Constraint
 * - No Internal Process Constraint
 *
 * Read cgroup v2 documentation for more info.
 */
extern int cgroup_p_step_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids)
{
	int i, j, rc;
	pid_t *user_pids = xmalloc(sizeof(*user_pids) * npids);
	pid_t stepd_pid = getpid();

	/*
	 * Remove our stepd pid from the list, we want it always in the slurm
	 * processes's dedicated cgroup and not in the step user's cgroup.
	 */
	for (i = 0, j = 0; i < npids; i++)
		if (pids[i] != stepd_pid) {
			user_pids[j] = pids[i];
			j++;
		}

	rc = common_cgroup_add_pids(&g_step_user_cg, user_pids, j);

	xfree(user_pids);
	return rc;
}

/*
 * Read the cgroup.procs of this step.
 */
extern int cgroup_p_step_get_pids(pid_t **pids, int *npids)
{
	pid_t *pids_slurm = NULL;
	pid_t *pids_user = NULL;
	pid_t *pid_list = NULL;
	int npids_slurm, npids_user, i, j;

	/*
	 * DEV_NOTES:
	 *  We may want to determine if there are any task_X directory and if so
	 *  read the processes inside them instead of reading the step ones only.
	 *
	 *  We are including also the slurmstepd pids here at the moment.
	 *
	 *  if there are task_x directories, then:
	 *     for all task_x dir:
         *         read task_x/cgroup.procs and put them into **pids
	 *  else:
         *      read step_x/cgroup.procs and put them into **pids
	 */
	common_cgroup_get_pids(&g_step_slurm_cg, &pids_slurm, &npids_slurm);
	common_cgroup_get_pids(&g_step_user_cg, &pids_user, &npids_user);
	*npids = npids_slurm + npids_user;

	if ((npids_slurm + npids_user) <= 0)
		return SLURM_SUCCESS;

	pid_list = xmalloc(sizeof(*pid_list) * (*npids));

	for (i = 0; i < npids_slurm; i++)
		pid_list[i] = pids_slurm[i];

	for (i = 0, j = npids_slurm; j < *npids; i++, j++)
		pid_list[j] = pids_user[i];

	*pids = pid_list;

	xfree(pids_slurm);
	xfree(pids_user);

	return SLURM_SUCCESS;
}

extern int cgroup_p_step_suspend()
{
	/* Another plugin already requesed termination */
	if (g_step_user_cg.path == NULL)
		return SLURM_SUCCESS;

	/*
	 * Freezing of the cgroup may take some time; when this action is
	 * completed, the “frozen” value in the cgroup.events control file will
	 * be updated to “1” and the corresponding notification will be issued.
	 */
	return common_cgroup_set_param(&g_step_user_cg, "cgroup.freeze", "1");
}

extern int cgroup_p_step_resume()
{
	/* Another plugin already requesed termination */
	if (g_step_user_cg.path == NULL)
		return SLURM_SUCCESS;

	return common_cgroup_set_param(&g_step_user_cg, "cgroup.freeze", "0");
}

/* FIXME: Need to take into account tasks when accounting is supported */
extern int cgroup_p_step_destroy(cgroup_ctl_type_t sub)
{
	int rc = SLURM_SUCCESS;
	xcgroup_t init_root;

	/*
	 * Multiple plugins can ask us to destroy the step. If we have
	 * destroyed it before, just leave.
	 */
	if (!g_step_slurm_cg.path)
		return rc;

	/* We need to record the oom stats prior to remove the cgroup. */
	_record_oom_step_stats();

	/*
	 * Move ourselves to the init root
	 * Create a fake cgroup to call the move process function.
	 */
	init_root.ns = NULL;
	init_root.name = NULL;
	init_root.path = xstrdup("/sys/fs/cgroup");
	init_root.uid = 0;
	init_root.gid = 0;
	rc = common_cgroup_move_process(&init_root, getpid());
	if (rc != SLURM_SUCCESS) {
		error("Unable to move pid %d to init root cgroup %s", getpid(),
		      init_root.path);
		goto end;
	}

	/* Rmdir this job's stepd cgroup */
	if ((rc = common_cgroup_delete(&g_step_slurm_cg)) != SLURM_SUCCESS) {
		debug2("unable to remove slurm's step cgroup (%s): %m",
		       g_step_slurm_cg.path);
		goto end;
	}
	common_cgroup_destroy(&g_step_slurm_cg);

	/* Rmdir this job's user processes cgroup */
	if ((rc = common_cgroup_delete(&g_step_user_cg)) != SLURM_SUCCESS) {
		debug2("unable to remove user's step cgroup (%s): %m",
		       g_step_user_cg.path);
		goto end;
	}
	common_cgroup_destroy(&g_step_user_cg);

	/* Rmdir this step's processes cgroup */
	if ((rc = common_cgroup_delete(&g_step_cg)) != SLURM_SUCCESS) {
		debug2("unable to remove step cgroup (%s): %m",
		       g_step_cg.path);
		goto end;
	}
	common_cgroup_destroy(&g_step_cg);

	/* That's a try to rmdir if no more steps are in this job */
	if ((rc = common_cgroup_delete(&g_job_cg)) != SLURM_SUCCESS) {
		debug2("unable to remove job's step cgroup (%s): %m",
		       g_job_cg.path);
		goto end;
	}
	common_cgroup_destroy(&g_job_cg);

end:
	common_cgroup_destroy(&init_root);
	return rc;
}

/* Return true if the user pid is in this step/task cgroup */
extern bool cgroup_p_has_pid(pid_t pid)
{
	/*
	 * DEV_NOTES:
	 * We may want to determine also if there are any task_X directory and
	 * if so read the processes inside them instead of reading the step
	 * ones. Also, we are not including also the slurmstepd pids.
	 */

	pid_t *pids_user = NULL;
	int npids_user, i;

	common_cgroup_get_pids(&g_step_user_cg, &pids_user, &npids_user);
	for (i = 0; i < npids_user; i++)
		if (pids_user[i] == pid)
			return true;

	xfree(pids_user);
	return false;
}

extern cgroup_limits_t *cgroup_p_root_constrain_get(cgroup_ctl_type_t sub)
{
 	int rc = SLURM_SUCCESS;
	cgroup_limits_t *limits = xmalloc(sizeof(*limits));

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		rc = common_cgroup_get_param(&g_root_cg, "cpuset.cpus",
					     &limits->allow_cores,
					     &limits->cores_size);
		rc += common_cgroup_get_param(&g_root_cg, "cpuset.mems",
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
	/*
	 * No constrain is needed for our root in v2, they are applied to jobs,
	 * steps or to the slurmd cgroup only
	 */
	return SLURM_SUCCESS;
}

extern cgroup_limits_t *cgroup_p_system_constrain_get(cgroup_ctl_type_t sub)
{
	/*
	 * DEV_NOTES
	 * For future usage, just return the requested constrains from the
	 * $cg_root/slurmd/
	 */
	cgroup_limits_t *limits = NULL;
	return limits;
}

extern int cgroup_p_system_constrain_set(cgroup_ctl_type_t sub,
					 cgroup_limits_t *limits)
{
	int rc = SLURM_SUCCESS;

	if (!limits)
		return SLURM_ERROR;

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		rc = common_cgroup_set_param(&g_sys_cg, "cpuset.cpus",
					     limits->allow_cores);
		//rc += common_cgroup_set_param(&g_sys_cg, "cpuset.mems",
		//				limits->allow_mems);
		break;
	case CG_MEMORY:
		common_cgroup_set_uint64_param(&g_sys_cg,
					       "memory.limit_in_bytes",
					       limits->limit_in_bytes);
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
	/* There's no user directory in cgroup v2 hierarchy. */
	return SLURM_SUCCESS;
}

extern int cgroup_p_job_constrain_set(cgroup_ctl_type_t sub,
				      stepd_step_rec_t *job,
				      cgroup_limits_t *limits)
{
	/* DEV_NOTES - EXPERIMENTAL
	 * There's no kmem constrain support currently in v2.
	 * Memory limits have changed to high, low, min, max.
	 * Swap limits are controlled with different files too.
	 * Device control is not supported as a file interface, so here we
	 * need to interact with eBPF.
	 */
	int rc = SLURM_SUCCESS;

	if (!limits)
		return SLURM_ERROR;

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		rc = common_cgroup_set_param(&g_job_cg, "cpuset.cpus",
					     limits->allow_cores);
		rc += common_cgroup_set_param(&g_job_cg, "cpuset.mems",
					      limits->allow_mems);
		break;
	case CG_MEMORY:
		rc = common_cgroup_set_uint64_param(&g_job_cg,
						    "memory.max",
						    limits->limit_in_bytes);
		if (limits->memsw_limit_in_bytes != NO_VAL64)
			rc += common_cgroup_set_uint64_param(
				&g_job_cg,
				"memory.swap.max",
				limits->memsw_limit_in_bytes);
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

extern int cgroup_p_step_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits)
{
	/* DEV_NOTES - EXPERIMENTAL
	 * There's no kmem constrain support currently in v2.
	 * Memory limits have changed to high, low, min, max.
	 * Swap limits are controlled with different files too.
	 * Device control is not supported as a file interface, so here we
	 * need to interact with eBPF.
	 */
	int rc = SLURM_SUCCESS;

	if (!limits)
		return SLURM_ERROR;

	switch (sub) {
	case CG_TRACK:
		break;
	case CG_CPUS:
		rc = common_cgroup_set_param(&g_step_user_cg, "cpuset.cpus",
					     limits->allow_cores);
		rc += common_cgroup_set_param(&g_step_user_cg, "cpuset.mems",
					      limits->allow_mems);
		break;
	case CG_MEMORY:
		rc = common_cgroup_set_uint64_param(&g_step_user_cg,
						    "memory.max",
						    limits->limit_in_bytes);
		if (limits->memsw_limit_in_bytes != NO_VAL64)
			rc += common_cgroup_set_uint64_param(
				&g_step_user_cg,
				"memory.swap.max",
				limits->memsw_limit_in_bytes);
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

extern int cgroup_p_step_start_oom_mgr()
{
	/* Just return, no need to start anything. */
	return SLURM_SUCCESS;
}

extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *job)
{
	return g_oom_step_results;
}

extern int cgroup_p_accounting_init()
{
	/*
	 * Accounting controllers are implictly enabled in when enabling cpu
	 * and mem in subtree_control.
	 */
	/*
	 * DEV_NOTES - We need to modify the structure creating task_X and
	 * moving pids there.
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_accounting_fini()
{
	/* DEV_NOTES - IMPLEMENT ME
	 * Destroy the step calling cgroup_p_step_destroy
	 * ... may need the task_acct_list ..
	 * just return
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_task_addto_accounting(pid_t pid, stepd_step_rec_t *job,
					  uint32_t task_id)
{
	/* DEV_NOTES - IMPLEMENT ME
	 * create step_y/task_z
	 * enable +cpu +mem cgroup.subtree_control
	 * and attach pid to cgroup.procs
	 */
	return SLURM_SUCCESS;
}

extern cgroup_acct_t *cgroup_p_task_get_acct_data(uint32_t taskid)
{
	/* DEV_NOTES
	 * read cpu.stat, memory.stat
	 */
	return NULL;
}
