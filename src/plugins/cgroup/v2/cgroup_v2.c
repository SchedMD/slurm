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

#define SYSTEM_CGSLICE "system.slice"
#define SYSTEM_CGSCOPE "slurmstepd_home.scope"
#define SYSTEM_CGDIR "system"

const char plugin_name[] = "Cgroup v2 plugin";
const char plugin_type[] = "cgroup/v2";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Internal cgroup structs */
static List task_list;
static uint16_t step_active_cnt;
static xcgroup_ns_t int_cg_ns;
static xcgroup_t int_cg[CG_LEVEL_CNT];
static bitstr_t *avail_controllers = NULL;
static bitstr_t *enabled_controllers = NULL;
static char *ctl_names[] = {
	[CG_TRACK] = "freezer",
	[CG_CPUS] = "cpuset",
	[CG_MEMORY] = "memory",
	[CG_CPUACCT] = "cpu",
};

typedef struct {
	xcgroup_t task_cg;
	uint32_t taskid;
} task_cg_info_t;

/* Hierarchy will take this form:
 *        [int_cg_ns]             [int_cg_ns]
 *      "slurmd service"       "slurmtepds scope"
 *      root(delegated)         root(delegated) [CG_LEVEL_ROOT]
 *		|	       /	      \
 *		|	      /		      |
 *           slurmd          |         job_x ... job_n [CG_LEVEL_JOB]
 *                         system             |
 *                      (waiting area         |
 *                      for new stepds)       |
 *		                          step_0 ... step_n [CG_LEVEL_STEP]
 *                                         /   \
 *      [CG_LEVEL_STEP_USER] user_processes     slurm_processes [CG_LEVEL_STEP_SLURM]
 *			         /               (slurmstepds)
 *			        /
 *                             |
 *                    task_special...task_0...task_n [CG_LEVEL_TASK] (user pids)
 *                 (task_id = NO_VAL)
 */

/*
 * Fill up the internal cgroup namespace object. This mainly contains the path
 * to the root.
 *
 * The cgroup v2 documented way to know which is the process root in the cgroup
 * hierarchy is just to read /proc/self/cgroup. In Unified hierarchies this
 * must contain only one line. If there are more lines this would mean we are
 * in Hybrid or in Legacy cgroup.
 */
static void _set_int_cg_ns()
{
	char *buf, *start = NULL, *p;
	size_t sz;
	struct stat st;

	/* We already know where we will live if we're stepd. */
	if (running_in_slurmstepd()) {
		xstrfmtcat(int_cg_ns.mnt_point, "%s/%s/%s_%s",
			   slurm_cgroup_conf.cgroup_mountpoint, SYSTEM_CGSLICE,
			   conf->node_name, SYSTEM_CGSCOPE);
		if (stat(int_cg_ns.mnt_point, &st) < 0) {
			error("cannot read cgroup path %s: %m",
			      int_cg_ns.mnt_point);
			xfree(int_cg_ns.mnt_point);
		}
		return;
	}

	if (common_file_read_content("/proc/self/cgroup", &buf, &sz)
	    != SLURM_SUCCESS)
		fatal("cannot read /proc/self/cgroup contents: %m");

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
		xstrfmtcat(int_cg_ns.mnt_point, "%s%s",
			   slurm_cgroup_conf.cgroup_mountpoint, start);
	}

	xfree(buf);
}

/*
 * For each available controller, enable it in this path. This operation is
 * only intended to be done in the Domain controllers, never in a leaf where
 * processes reside. If it is done in a leaf it *won't be possible* to add any
 * pid to it. Enabling the controllers will make their interfaces available
 * (e.g. the memory.*, cpu.*, cpuset.* ... files) to control the cgroup.
 */
static int _enable_subtree_control(xcgroup_t *cg)
{
	int i, rc = SLURM_SUCCESS;
	char *param = NULL;

	for (i = 0; i < CG_CTL_CNT; i++) {
		if (bit_test(avail_controllers, i)) {
			xstrfmtcat(param, "+%s", ctl_names[i]);
			rc = common_cgroup_set_param(cg,
						     "cgroup.subtree_control",
						     param);
			xfree(param);
			if (rc != SLURM_SUCCESS) {
				error("Cannot enable %s in %s/cgroup.subtree_control",
				      ctl_names[i], cg->path);
				bit_clear(avail_controllers, i);
				rc = SLURM_ERROR;
			} else {
				log_flag(CGROUP, "Enabled %s controller in %s",
					 ctl_names[i], cg->path);
				bit_set(enabled_controllers, i);
			}
		}
	}

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

	xstrfmtcat(ctl_filepath, "%s/cgroup.controllers", int_cg_ns.mnt_point);
	if (common_file_read_content(ctl_filepath, &buf, &sz) !=
	    SLURM_SUCCESS || !buf) {
		error("cannot read %s: %m", ctl_filepath);
		xfree(ctl_filepath);
		return SLURM_ERROR;
	}
	xfree(ctl_filepath);

	ptr = strtok_r(buf, " ", &save_ptr);
	while (ptr) {
		for (int i = 0; i < CG_CTL_CNT; i++) {
			if (!xstrcmp(ctl_names[i], ""))
				continue;
			if (!xstrcasecmp(ctl_names[i], ptr))
				bit_set(avail_controllers, i);
		}
		ptr = strtok_r(NULL, " ", &save_ptr);
	}
	xfree(buf);

	/* Field not used in v2 */
	int_cg_ns.subsystems = NULL;

	return SLURM_SUCCESS;
}

static void _free_task_cg_info(void *x)
{
	task_cg_info_t *task_cg = (task_cg_info_t *)x;

	if (task_cg) {
		common_cgroup_destroy(&task_cg->task_cg);
		xfree(task_cg);
	}
}

/*
 * Talk with systemd through dbus to create a new scope where we will put all
 * the slurmstepds and user processes. This way we can safely restart slurmd
 * and not affect jobs at all.
 * Technically it must do:
 *	- Start a new transient scope with Delegate=yes and all controllers.
 *      - Create a new system/ directory under it.
 */
static void _create_new_scope(const char *slice, const char *scope,
			      const char *dir)
{
	char *scope_path = NULL;
	char *full_path = NULL;

	xstrfmtcat(scope_path, "/sys/fs/cgroup/%s/%s_%s",
		   slice, conf->node_name, scope);
	xstrfmtcat(full_path, "%s/%s", scope_path, dir);

	// Don't fail if it already exists.
	mkdir(scope_path, O_CREAT);
	mkdir(full_path, 0755);

	xfree(scope_path);
	xfree(full_path);
}

/*
 * Talk with systemd through dbus to move the slurmstepd pid into the reserved
 * scope for stepds and user processes.
 */
static int _move_pid_to_scope(const char *slice, const char *scope,
			      const char *dir, pid_t pid)
{
	char *scope_path = NULL;
	char *dir_path = NULL;

	xstrfmtcat(scope_path, "/sys/fs/cgroup/%s/%s_%s",
		   slice, conf->node_name, scope);
	xstrfmtcat(dir_path, "/%s", dir);

	common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_SYSTEM],
			     dir_path, (uid_t) 0, (gid_t) 0);
	common_cgroup_move_process(&int_cg[CG_LEVEL_SYSTEM], pid);

	if (_enable_subtree_control(&int_cg[CG_LEVEL_ROOT]) != SLURM_SUCCESS) {
		error("Cannot enable subtree_control at the top level %s",
		      int_cg_ns.mnt_point);
		return SLURM_ERROR;
	}

	xfree(scope_path);
	xfree(dir_path);
	return SLURM_SUCCESS;
}

/*
 * Initialize the cgroup plugin. Slurmd MUST be started by systemd and the
 * option Delegate set to 'Yes' or equal to a string with the desired
 * controllers we want to support in this system. If we are slurmd we're going
 * to create a systemd scope for further slurmstepds. The scope is associated
 * to a cgroup directory, and it will be delegated to us too. We need to
 * separate it from slurmd because if we restart slurmd and there are living
 * steps in the same directory, then slurmd could not be put in a non-leaf
 * cgroup, and systemd will fail (no internal process constraint).
 * Take in mind also we should not do anything upper in the hierarchy because of
 * the single-writer architecture systemd imposes to us. The upper tree is
 * completely under systemd control.
 *
 * We need to play the cgroup v2 game rules:
 *
 * - No Internal Process Constraint
 * - Top-down Constraint
 *
 * And try to be compliant with systemd, or they will complain:
 *
 * - Single writer rule.
 *
 * Read cgroup v2 documentation for more info.
 */
extern int init(void)
{
	avail_controllers = bit_alloc(CG_CTL_CNT);
	enabled_controllers = bit_alloc(CG_CTL_CNT);
	step_active_cnt = 0;
	FREE_NULL_LIST(task_list);
	task_list = list_create(_free_task_cg_info);

	/*
	 * If we are slurmd we need to create a new place for forked stepds to
	 * give them its independence. If we don't do that, a slurmd restart
	 * through systemd would not succeed because the cgroup would be busy
	 * and systemd would fail to place the new slurmd in the cgroup.
	 */
	if (running_in_slurmd())
		_create_new_scope(SYSTEM_CGSLICE, SYSTEM_CGSCOPE, SYSTEM_CGDIR);

	/*
	 * Check our current root dir. Systemd MUST have Delegated it to us,
	 * so we want slurmd to be started by systemd
	 */
	_set_int_cg_ns();
	if (!int_cg_ns.mnt_point) {
		error("Cannot setup the cgroup namespace.");
		return SLURM_ERROR;
	}

	/*
	 * Check available controllers in cgroup.controller and record them in
	 * our bitmap.
	 */
	if (_check_avail_controllers() != SLURM_SUCCESS)
		return SLURM_ERROR;

	/* Setup the root cgroup object. */
	common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_ROOT], "",
			     (uid_t) 0, (gid_t) 0);

	/*
	 * If we are slurmstepd we are living in slurmd's place. We need first
	 * to emancipate to our new place and tell systemd about it or we will
	 * mess its accounting.
	 */
	if (running_in_slurmstepd())
		_move_pid_to_scope(SYSTEM_CGSLICE, SYSTEM_CGSCOPE, SYSTEM_CGDIR,
				   getpid());

	/*
	 * If we're slurmd we're all set and able to constrain things, i.e.
	 * CoreSpec* and MemSpec*.
	 *
	 * If we are a new slurmstepd we are ready now to create job steps. In
	 * that case, since we're still living in slurmd's place, we will need
	 * to emancipate to the slurmd_family cgroup, and then create
	 * int_cg[CG_LEVEL_ROOT].path/job_x/step_x. Per each new step we'll need
	 * to first move the stepd process out of slurmd directory where we
	 * still live.
	 */
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	/*
	 * Clear up the namespace and cgroups memory. Don't rmdir anything since
	 * we may not be stopping yet. When the process terminates systemd will
	 * remove the remaining directories.
	 */
	FREE_NULL_BITMAP(avail_controllers);
	FREE_NULL_BITMAP(enabled_controllers);
	common_cgroup_destroy(&int_cg[CG_LEVEL_SYSTEM]);
	common_cgroup_destroy(&int_cg[CG_LEVEL_ROOT]);
	common_cgroup_ns_destroy(&int_cg_ns);
	FREE_NULL_LIST(task_list);

	debug("unloading %s", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * Unlike in Legacy mode (v1) where we needed to create a directory for each
 * controller, in Unified mode this function will be mostly empty because the
 * hierarchy is unified into the same path. The controllers will be enabled
 * when we create the hierarchy. The only controller that may need an init is
 * the 'devices', which in Unified is not a real controller, but instead we
 * need to register an eBPF program.
 */
extern int cgroup_p_initialize(cgroup_ctl_type_t ctl)
{
	switch (ctl) {
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
extern int cgroup_p_system_create(cgroup_ctl_type_t ctl)
{
	return SLURM_SUCCESS;
}

/*
 * Note that as part of the initialization, the slurmd pid is already put
 * inside this cgroup but we still need to implement this for if somebody
 * needs to add a different pid in this cgroup.
 */
extern int cgroup_p_system_addto(cgroup_ctl_type_t ctl, pid_t *pids, int npids)
{
	return SLURM_SUCCESS;
}

/*
 * There's no need to do any cleanup, when systemd terminates the cgroup is
 * automatically removed by systemd.
 */
extern int cgroup_p_system_destroy(cgroup_ctl_type_t ctl)
{
	return SLURM_SUCCESS;
}

/*
 * Create the step hierarchy and move the stepd process into it. Further forked
 * processes will be created in the step directory as child. We need to respect
 * the Top-Down constraint not adding pids to non-leaf cgroups.
 */
extern int cgroup_p_step_create(cgroup_ctl_type_t ctl, stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

/*
 * Move a pid to a specific cgroup. It needs to be a leaf, we cannot move
 * a pid to an intermediate directory in the cgroup hierarchy. Since we always
 * work at task level, we will add this pid to the special task task_4294967293.
 *
 * - Top-down Constraint
 * - No Internal Process Constraint
 *
 * Read cgroup v2 documentation for more info.
 */
extern int cgroup_p_step_addto(cgroup_ctl_type_t ctl, pid_t *pids, int npids)
{
	return SLURM_SUCCESS;
}

/*
 * Read the cgroup.procs of this step.
 */
extern int cgroup_p_step_get_pids(pid_t **pids, int *npids)
{
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_suspend()
{
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_resume()
{
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_destroy(cgroup_ctl_type_t ctl)
{
	return SLURM_SUCCESS;
}

/* Return true if the user pid is in this step/task cgroup */
extern bool cgroup_p_has_pid(pid_t pid)
{
	return false;
}

extern int cgroup_p_constrain_set(cgroup_ctl_type_t ctl, cgroup_level_t level,
				  cgroup_limits_t *limits)
{
	return SLURM_SUCCESS;
}

extern cgroup_limits_t *cgroup_p_constrain_get(cgroup_ctl_type_t ctl,
					       cgroup_level_t level)
{
	return NULL;
}

extern int cgroup_p_step_start_oom_mgr()
{
	return SLURM_SUCCESS;
}

extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *job)
{
	return NULL;
}

extern int cgroup_p_task_addto(cgroup_ctl_type_t ctl, stepd_step_rec_t *job,
			       pid_t pid, uint32_t task_id)
{
	return SLURM_SUCCESS;
}

extern cgroup_acct_t *cgroup_p_task_get_acct_data(uint32_t task_id)
{
	return NULL;
}
