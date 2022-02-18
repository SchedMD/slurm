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
static bpf_program_t p[CG_LEVEL_CNT];
static uint32_t task_special_id = NO_VAL;
static char *ctl_names[] = {
	[CG_TRACK] = "freezer",
	[CG_CPUS] = "cpuset",
	[CG_MEMORY] = "memory",
	[CG_CPUACCT] = "cpu",
	[CG_DEVICES] = "devices",
};

typedef struct {
	xcgroup_t task_cg;
	uint32_t taskid;
	bpf_program_t p;
} task_cg_info_t;

typedef struct {
	int npids;
	pid_t *pids;
} foreach_pid_array_t;

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
	 * of the cgroup and starting by 0, so get it as our root and replace
	 * the \n:
	 * "0::/system.slice/slurmd<nodename>.service\n"
	 *
	 * The final path will look like this:
	 * /sys/fs/cgroup/system.slice/slurmd.service/
	 *
	 * If we have multiple slurmd, we will likely have one unit file per
	 * node, and the path takes the name of the service file, e.g:
	 * /sys/fs/cgroup/system.slice/slurmd-<nodename>.service/
	 */
	if (buf && (buf[0] != '0'))
		fatal("Hybrid mode is not supported. Mounted cgroups are: %s",
		      buf);

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
static int _enable_subtree_control(char *path, bitstr_t *ctl_bitmap)
{
	int i, rc = SLURM_SUCCESS;
	char *content = NULL, *file_path = NULL;

	xassert(ctl_bitmap);

	xstrfmtcat(file_path, "%s/cgroup.subtree_control", path);
	for (i = 0; i < CG_CTL_CNT; i++) {
		if (!bit_test(ctl_bitmap, i))
			continue;

		xstrfmtcat(content, "+%s", ctl_names[i]);
		rc = common_file_write_content(file_path, content,
					       sizeof(content));
		xfree(content);
		if (rc != SLURM_SUCCESS) {
			error("Cannot enable %s in %s",
			      ctl_names[i], file_path);
			bit_clear(ctl_bitmap, i);
			rc = SLURM_ERROR;
		} else {
			log_flag(CGROUP, "Enabled %s controller in %s",
				 ctl_names[i], file_path);
			bit_set(ctl_bitmap, i);
		}
	}
	xfree(file_path);
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
				bit_set(int_cg_ns.avail_controllers, i);
		}
		ptr = strtok_r(NULL, " ", &save_ptr);
	}
	xfree(buf);

	/* Field not used in v2 */
	int_cg_ns.subsystems = NULL;

	return SLURM_SUCCESS;
}

static int _rmdir_task(void *x, void *arg)
{
	task_cg_info_t *t = (task_cg_info_t *) x;

	if (common_cgroup_delete(&t->task_cg) != SLURM_SUCCESS)
		log_flag(CGROUP, "Failed to delete %s: %m", t->task_cg.path);

	return SLURM_SUCCESS;
}

static int _find_task_cg_info(void *x, void *key)
{
	task_cg_info_t *task_cg = (task_cg_info_t *)x;
	uint32_t taskid = *(uint32_t*)key;

	if (task_cg->taskid == taskid)
		return 1;

	return 0;
}

static void _free_task_cg_info(void *x)
{
	task_cg_info_t *task_cg = (task_cg_info_t *)x;

	if (task_cg) {
		common_cgroup_destroy(&task_cg->task_cg);
		free_ebpf_prog(&task_cg->p);
		xfree(task_cg);
	}
}

static void _all_tasks_destroy()
{
	/* Empty the lists of accounted tasks, do a best effort in rmdir */
	(void) list_delete_all(task_list, _rmdir_task, NULL);
}

static int _get_task_pids(void *x, void *key)
{
	task_cg_info_t *task_cg_info = (task_cg_info_t *)x;
	foreach_pid_array_t *pid_array = key;
	pid_t *pids = NULL;
	int npids = 0;

	xassert(pid_array);
	common_cgroup_get_pids(&task_cg_info->task_cg, &pids, &npids);

	if (pid_array->pids) {
		xrecalloc(pid_array->pids, (pid_array->npids + npids),
			  sizeof(*pid_array->pids));
		memcpy((pid_array->pids + pid_array->npids), pids,
		       sizeof(*pid_array->pids) * npids);
		pid_array->npids += npids;
	} else {
		pid_array->pids = pids;
		pids = NULL;
		pid_array->npids = npids;
	}
	xfree(pids);

	return SLURM_SUCCESS;
}

static int _find_pid_task(void *x, void *key)
{
	task_cg_info_t *task_cg_info = (task_cg_info_t *)x;
	pid_t pid = *(pid_t *) key;
	pid_t *pids = NULL;
	int npids = 0;
	bool found = false;

	if (common_cgroup_get_pids(&task_cg_info->task_cg, &pids, &npids) !=
	    SLURM_SUCCESS)
		return false;

	for (int i = 0; i < npids; i++) {
		if (pids[i] == pid) {
			found = true;
			break;
		}
	}

	return found;
}

static void _wait_cgroup_empty(xcgroup_t *cg, int timeout_ms)
{
	char *cgroup_events = NULL, *events_content = NULL, *ptr;
	int rc, fd, wd, populated = -1;
	size_t sz;
	struct pollfd pfd[1];

	/* Check if cgroup is empty in the first place. */
	if (common_cgroup_get_param(
		    cg, "cgroup.events", &events_content, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/cgroup.events", cg->path);

	if (events_content) {
		if ((ptr = xstrstr(events_content, "populated"))) {
			if (sscanf(ptr, "populated %u", &populated) != 1)
				error("Cannot read populated counter from cgroup.events file.");
		}
		xfree(events_content);
	}

	if (populated < 0) {
		error("Cannot determine if %s is empty.", cg->path);
		return;
	} else if (populated == 0) //We're done
		return;

	/*
	 * Cgroup is not empty, so wait for a while just monitoring any change
	 * on cgroup.events. Changing populate from 1 to 0 is what we expect.
	 */

	xstrfmtcat(cgroup_events, "%s/cgroup.events", cg->path);

	/* Initialize an inotify monitor */
	fd = inotify_init();
	if (fd < 0) {
		error("Cannot initialize inotify for checking cgroup events: %m");
		return;
	}

	/* Set the file and events we want to monitor. */
	wd = inotify_add_watch(fd, cgroup_events, IN_MODIFY);
	if (wd < 0) {
		error("Cannot add watch events to %s: %m", cgroup_events);
		goto end_inotify;
	}

	/* Wait for new events. */
	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	rc = poll(pfd, 1, timeout_ms);

	/*
	 * We don't really care about the event details, just check now if the
	 * cg event file contains what we're looking for.
	 */
	if (rc < 0)
		error("Error polling for event in %s: %m", cgroup_events);
	else if (rc == 0)
		error("Timeout waiting for %s to become empty.", cgroup_events);

	/* Check if cgroup is empty again. */
	if (common_cgroup_get_param(cg, "cgroup.events",
				    &events_content, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/cgroup.events", cg->path);

	if (events_content) {
		if ((ptr = xstrstr(events_content, "populated"))) {
			if (sscanf(ptr, "populated %u", &populated) != 1)
				error("Cannot read populated counter from cgroup.events file.");
		}
		xfree(events_content);
	}

	if (populated < 0)
		error("Cannot determine if %s is empty.", cg->path);
	else if (populated == 1)
		log_flag(CGROUP, "Cgroup %s is not empty.", cg->path);

end_inotify:
	close(fd);
	xfree(cgroup_events);
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

	if (_enable_subtree_control(int_cg[CG_LEVEL_ROOT].path,
				    int_cg_ns.avail_controllers) !=
	    SLURM_SUCCESS) {
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
	int_cg_ns.avail_controllers = bit_alloc(CG_CTL_CNT);
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
	FREE_NULL_BITMAP(int_cg_ns.avail_controllers);
	common_cgroup_destroy(&int_cg[CG_LEVEL_SYSTEM]);
	common_cgroup_destroy(&int_cg[CG_LEVEL_ROOT]);
	common_cgroup_ns_destroy(&int_cg_ns);
	FREE_NULL_LIST(task_list);
	free_ebpf_prog(&p[CG_LEVEL_JOB]);
	free_ebpf_prog(&p[CG_LEVEL_STEP_USER]);

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
		init_ebpf_prog(&p[CG_LEVEL_JOB]);
		init_ebpf_prog(&p[CG_LEVEL_STEP_USER]);
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
 * Slurmd will live in its own cgroup, not sharing anything with slurmstepd.
 * This means there's no reason to implement this function in v2.
 * Also slurmstepd is put into the user's hierarchy (see graph) and is not
 * affected by CoreSpec or MemSpec.
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
 * the cgroup v2 Top-Down constraint to not add pids to non-leaf cgroups.
 *
 * We create two directories per step because we need to put the stepd into its
 * specific slurm/ dir, otherwise suspending/constraining the user cgroup would
 * also suspend or constrain the stepd.
 *
 *  step_x/slurm (for slurm processes, slurmstepd)
 *  step_x/user (for users processes, tasks)
 *
 * No need to cleanup the directories on error because when a job ends
 * systemd does the cleanup automatically.
 *
 * Note that CoreSpec and/or MemSpec does not affect slurmstepd.
 */
extern int cgroup_p_step_create(cgroup_ctl_type_t ctl, stepd_step_rec_t *job)
{
	int rc = SLURM_SUCCESS;
	char *new_path = NULL;
	char tmp_char[64];

	/* Don't let other plugins destroy our structs. */
	step_active_cnt++;

	/* Job cgroup */
	xstrfmtcat(new_path, "/job_%u", job->step_id.job_id);
	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_JOB],
				 new_path, 0, 0) != SLURM_SUCCESS) {
		error("unable to create job %u cgroup", job->step_id.job_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_JOB]) != SLURM_SUCCESS) {
		common_cgroup_destroy(&int_cg[CG_LEVEL_JOB]);
		error("unable to instantiate job %u cgroup",
		      job->step_id.job_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);
	_enable_subtree_control(int_cg[CG_LEVEL_JOB].path,
				int_cg_ns.avail_controllers);

	/* Step cgroup */
	xstrfmtcat(new_path, "%s/step_%s", int_cg[CG_LEVEL_JOB].name,
		   log_build_step_id_str(&job->step_id, tmp_char,
					 sizeof(tmp_char),
					 STEP_ID_FLAG_NO_PREFIX |
					 STEP_ID_FLAG_NO_JOB));

	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_STEP],
				 new_path, 0, 0) != SLURM_SUCCESS) {
		error("unable to create step %ps cgroup", &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_STEP]) !=
	    SLURM_SUCCESS) {
		common_cgroup_destroy(&int_cg[CG_LEVEL_STEP]);
		error("unable to instantiate step %ps cgroup", &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);
	_enable_subtree_control(int_cg[CG_LEVEL_STEP].path,
				int_cg_ns.avail_controllers);

	/* Step User processes cgroup */
	xstrfmtcat(new_path, "%s/user", int_cg[CG_LEVEL_STEP].name);
	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_STEP_USER],
				 new_path, 0, 0) != SLURM_SUCCESS) {
		error("unable to create step %ps user procs cgroup",
		      &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_STEP_USER])
	    != SLURM_SUCCESS) {
		common_cgroup_destroy(&int_cg[CG_LEVEL_STEP_USER]);
		error("unable to instantiate step %ps user procs cgroup",
		      &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);
	_enable_subtree_control(int_cg[CG_LEVEL_STEP_USER].path,
				int_cg_ns.avail_controllers);

	/*
	 * Step Slurm processes cgroup
	 * Do not enable subtree control at this level since this is a leaf.
	 */
	xstrfmtcat(new_path, "%s/slurm", int_cg[CG_LEVEL_STEP].name);
	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_STEP_SLURM],
				 new_path, 0, 0) != SLURM_SUCCESS) {
		error("unable to create step %ps slurm procs cgroup",
		      &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_STEP_SLURM])
	    != SLURM_SUCCESS) {
		common_cgroup_destroy(&int_cg[CG_LEVEL_STEP_SLURM]);
		error("unable to instantiate step %ps slurm procs cgroup",
		      &job->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);

	/* Place this stepd is in the correct cgroup. */
	if (common_cgroup_move_process(&int_cg[CG_LEVEL_STEP_SLURM],
				       job->jmgr_pid) != SLURM_SUCCESS) {
		error("unable to move stepd pid to its dedicated cgroup");
		rc = SLURM_ERROR;
	}

	/* Use slurmstepd pid as the identifier of the container. */
	job->cont_id = (uint64_t)job->jmgr_pid;
endit:
	xfree(new_path);
	if (rc != SLURM_SUCCESS)
		step_active_cnt--;
	return rc;
}

/*
 * Move a pid to a specific cgroup. It needs to be a leaf, we cannot move
 * a pid to an intermediate directory in the cgroup hierarchy. Since we always
 * work at task level, we will add this pid to the special task task_4294967293.
 *
 * Future: If in cgroup v2 we want to be able to enable/disable controllers for
 * the slurmstepd pid, we need to add here the logic when stepd pid is detected.
 * By default, all controllers are enabled for slurmstepd cgroup.
 *
 * - Top-down Constraint
 * - No Internal Process Constraint
 *
 * Read cgroup v2 documentation for more info.
 */
extern int cgroup_p_step_addto(cgroup_ctl_type_t ctl, pid_t *pids, int npids)
{
	stepd_step_rec_t fake_job;
	int rc = SLURM_SUCCESS;
	pid_t stepd_pid = getpid();

	/* cgroups in v2 are always owned by root. */
	fake_job.uid = 0;
	fake_job.gid = 0;

	for (int i = 0; i < npids; i++) {
		/* Ignore any possible movement of slurmstepd */
		if (pids[i] == stepd_pid)
			continue;
		if (cgroup_p_task_addto(ctl, &fake_job, pids[i],
					task_special_id) != SLURM_SUCCESS)
			rc = SLURM_ERROR;
	}
	return rc;
}

/*
 * Read the cgroup.procs of the leafs of this step.
 *
 * - count the pids of slurm/ directory
 * - for all task_x dir:
 *        read task_x/cgroup.procs and add them into **pids
 */
extern int cgroup_p_step_get_pids(pid_t **pids, int *npids)
{
	foreach_pid_array_t pid_array;

	memset(&pid_array, 0, sizeof(pid_array));

	/* Include the slurm processes (stepd) pids too. */
	common_cgroup_get_pids(&int_cg[CG_LEVEL_STEP_SLURM],
			       &pid_array.pids, &pid_array.npids);

	list_for_each(task_list, _get_task_pids, &pid_array);
	*npids = pid_array.npids;
	*pids = pid_array.pids;

	return SLURM_SUCCESS;
}

/* Freeze the user processes of this step */
extern int cgroup_p_step_suspend()
{
	/* This plugin is unloaded. */
	if (!int_cg[CG_LEVEL_STEP_USER].path)
		return SLURM_SUCCESS;

	/*
	 * Freezing of the cgroup may take some time; when this action is
	 * completed, the "frozen" value in the cgroup.events control file will
	 * be updated to "1" and the corresponding notification will be issued.
	 */
	return common_cgroup_set_param(&int_cg[CG_LEVEL_STEP_USER],
				       "cgroup.freeze", "1");
}

/* Resume the user processes of this step */
extern int cgroup_p_step_resume()
{
	/* This plugin is unloaded. */
	if (!int_cg[CG_LEVEL_STEP_USER].path)
		return SLURM_SUCCESS;

	return common_cgroup_set_param(&int_cg[CG_LEVEL_STEP_USER],
				       "cgroup.freeze", "0");
}

/*
 * Destroy the step cgroup. We need to move out ourselves to the root of
 * the cgroup filesystem first.
 */
extern int cgroup_p_step_destroy(cgroup_ctl_type_t ctl)
{
	int rc = SLURM_SUCCESS;
	xcgroup_t init_root;

	/*
	 * Only destroy the step if we're the only ones using it. Log it unless
	 * loaded from slurmd, where we will not create any step but call fini.
	 */
	if (step_active_cnt == 0) {
		error("called without a previous step create. This shouldn't happen!");
		return SLURM_SUCCESS;
	}

	if (step_active_cnt > 1) {
		step_active_cnt--;
		log_flag(CGROUP, "Not destroying %s step dir, resource busy by %d other plugin",
			 ctl_names[ctl], step_active_cnt);
		return SLURM_SUCCESS;
	}

	/*
	 * FUTURE:
	 * Here we can implement a recursive kill of all pids in the step.
	 */

	/*
	 * Move ourselves to the init root. This is the only cgroup level where
	 * pids can be put and which is not a leaf.
	 */
	memset(&init_root, 0, sizeof(init_root));
	init_root.path = xstrdup(slurm_cgroup_conf.cgroup_mountpoint);
	rc = common_cgroup_move_process(&init_root, getpid());
	if (rc != SLURM_SUCCESS) {
		error("Unable to move pid %d to init root cgroup %s", getpid(),
		      init_root.path);
		goto end;
	}
	/* Wait for this cgroup to be empty, 1 second */
	_wait_cgroup_empty(&int_cg[CG_LEVEL_STEP_SLURM], 1000);

	/* Remove any possible task directories first */
	_all_tasks_destroy();

	/* Rmdir this job's stepd cgroup */
	if ((rc = common_cgroup_delete(&int_cg[CG_LEVEL_STEP_SLURM])) !=
	    SLURM_SUCCESS) {
		debug2("unable to remove slurm's step cgroup (%s): %m",
		       int_cg[CG_LEVEL_STEP_SLURM].path);
		goto end;
	}
	common_cgroup_destroy(&int_cg[CG_LEVEL_STEP_SLURM]);

	/* Rmdir this job's user processes cgroup */
	if ((rc = common_cgroup_delete(&int_cg[CG_LEVEL_STEP_USER])) !=
	    SLURM_SUCCESS) {
		debug2("unable to remove user's step cgroup (%s): %m",
		       int_cg[CG_LEVEL_STEP_USER].path);
		goto end;
	}
	common_cgroup_destroy(&int_cg[CG_LEVEL_STEP_USER]);

	/* Rmdir this step's processes cgroup */
	if ((rc = common_cgroup_delete(&int_cg[CG_LEVEL_STEP])) !=
	    SLURM_SUCCESS) {
		debug2("unable to remove step cgroup (%s): %m",
		       int_cg[CG_LEVEL_STEP].path);
		goto end;
	}
	common_cgroup_destroy(&int_cg[CG_LEVEL_STEP]);

	/*
	 * That's a best try to rmdir if no more steps are in this job,
	 * it must not fail on error because other steps can still be alive.
	 */
	if (common_cgroup_delete(&int_cg[CG_LEVEL_JOB]) != SLURM_SUCCESS) {
		debug2("still unable to remove job's step cgroup (%s): %m",
		       int_cg[CG_LEVEL_JOB].path);
		goto end;
	}
	common_cgroup_destroy(&int_cg[CG_LEVEL_JOB]);

	step_active_cnt = 0;
end:
	common_cgroup_destroy(&init_root);
	return rc;
}

/*
 * Return true if the user pid is in this step/task cgroup.
 *
 * We just need to get the pids from the task_X directories and from the slurm
 * processes cgroup, since these will be the only leafs we'll have.
 */
extern bool cgroup_p_has_pid(pid_t pid)
{
	task_cg_info_t *task_cg_info;
	pid_t *pids_slurm = NULL;
	int npids_slurm = 0, i;

	task_cg_info = list_find_first(task_list, _find_pid_task, &pid);

	if (task_cg_info)
		return true;

	/* Look for in the slurm processes cgroup too. */
	if (common_cgroup_get_pids(&int_cg[CG_LEVEL_STEP_SLURM],
				   &pids_slurm, &npids_slurm) !=
	    SLURM_SUCCESS)
		return false;

	for (i = 0; i < npids_slurm; i++) {
		if (pids_slurm[i] == pid)
			return true;
	}

	return false;
}

extern int cgroup_p_constrain_set(cgroup_ctl_type_t ctl, cgroup_level_t level,
				  cgroup_limits_t *limits)
{
	int rc = SLURM_SUCCESS;
	bpf_program_t *program = NULL;
	task_cg_info_t *task_cg_info;
	char *dev_id_str = gres_device_id2str(&limits->device);
	uint32_t bpf_dev_type = NO_VAL;

	/*
	 * cgroup/v1 legacy compatibility: We have no such levels in cgroup/v2
	 * but we may still get calls for them.
	 */
	if (level == CG_LEVEL_USER)
		return SLURM_SUCCESS;

	if (level == CG_LEVEL_SLURM)
		level = CG_LEVEL_ROOT;

	/* This is for CoreSpec* and MemSpec* for slurmd */
	if (level == CG_LEVEL_SYSTEM)
		level = CG_LEVEL_ROOT;

	/*
	 * Our real step level is the level for user processes. This will make
	 * that the slurmstepd is never constrained in its own cgroup, which is
	 * something we want. Instead, slurmstepd will be part of the job limit.
	 * Note that a step which initializes pmi, could cause slurmstepd to
	 * grow, and we don't want this to be part of the step, but be part of
	 * the job.
	 */
	if (level == CG_LEVEL_STEP)
		level = CG_LEVEL_STEP_USER;

	if (!limits)
		return SLURM_ERROR;

	switch (ctl) {
	case CG_TRACK:
		/* Not implemented. */
		break;
	case CG_CPUS:
		if (limits->allow_cores &&
		    common_cgroup_set_param(
			    &int_cg[level],
			    "cpuset.cpus",
			    limits->allow_cores) != SLURM_SUCCESS) {
			rc = SLURM_ERROR;
		}
		if (limits->allow_mems &&
		    common_cgroup_set_param(
			    &int_cg[level],
			    "cpuset.mems",
			    limits->allow_mems) != SLURM_SUCCESS) {
			rc = SLURM_ERROR;
		}
		break;
	case CG_MEMORY:
		if ((limits->limit_in_bytes != NO_VAL64) &&
		    common_cgroup_set_uint64_param(
			    &int_cg[level],
			    "memory.max",
			    limits->limit_in_bytes) != SLURM_SUCCESS) {
			rc = SLURM_ERROR;
		}
		if ((limits->soft_limit_in_bytes != NO_VAL64) &&
		    common_cgroup_set_uint64_param(
			    &int_cg[level],
			    "memory.high",
			    limits->soft_limit_in_bytes) != SLURM_SUCCESS) {
			rc = SLURM_ERROR;
		}
		if ((limits->memsw_limit_in_bytes != NO_VAL64) &&
		    common_cgroup_set_uint64_param(
			    &int_cg[level],
			    "memory.swap.max",
			    limits->memsw_limit_in_bytes) != SLURM_SUCCESS) {
			rc = SLURM_ERROR;
		}
		break;
	case CG_DEVICES:
		/*
		 * Set program to point to the needed bpf_program_t depending on
		 * the hierarchy level.
		 */
		switch (level) {
		case CG_LEVEL_JOB:
		case CG_LEVEL_STEP_USER:
			program = &(p[level]);
			break;
		case CG_LEVEL_TASK:
			if (!(task_cg_info = list_find_first(
				      task_list,
				      _find_task_cg_info,
				      &limits->taskid))) {
				error("No task found with id %u, this should never happen",
				      limits->taskid);
				return SLURM_ERROR;
			}
			program = &(task_cg_info->p);
			break;
		default:
			error("unknown hierarchy level %d", level);
			break;
		}
		if (!program) {
			error("Could not find a bpf program to use at level %d",
			      level);
			return SLURM_ERROR;
		}

		if (limits->allow_device)
			log_flag(CGROUP, "Allowing access to device (%s)",
				 dev_id_str);
		else
			log_flag(CGROUP, "Denying access to device (%s)",
				 dev_id_str);

		/* Determine the correct BPF device type. */
		if (limits->device.type == DEV_TYPE_BLOCK)
			bpf_dev_type = BPF_DEVCG_DEV_BLOCK;
		else if (limits->device.type == DEV_TYPE_CHAR)
			bpf_dev_type = BPF_DEVCG_DEV_CHAR;

		rc = add_device_ebpf_prog(program, bpf_dev_type,
					  limits->device.major,
					  limits->device.minor,
					  limits->allow_device);
		break;
	default:
		error("cgroup controller %u not supported", ctl);
		rc = SLURM_ERROR;
		break;
	}

	xfree(dev_id_str);
	return rc;
}

/*
 * Apply the device constrain limits, this is only used with cgroupv2 as there
 * is the need of loading and attaching the eBPF program to the cgroup.
 * It closes, loads and attach the bpf_program to the corresponding cgroup using
 * level and task_id, task_id is only used in CG_LEVEL_TASK level.
 */
extern int cgroup_p_constrain_apply(cgroup_ctl_type_t ctl, cgroup_level_t level,
                                    uint32_t task_id)
{
	bpf_program_t *program = NULL;
	task_cg_info_t *task_cg_info;
	char *cgroup_path = NULL;

	/*
	 * cgroup/v1 legacy compatibility: We have no such levels in cgroup/v2
	 * but we may still get calls for them.
	 */
	if (level == CG_LEVEL_USER)
		return SLURM_SUCCESS;

	if (level == CG_LEVEL_SLURM)
		level = CG_LEVEL_ROOT;
	/*
	 * Our real step level is the level for user processes. This will make
	 * that the slurmstepd is never constrained in its own cgroup, which is
	 * something we want. Instead, slurmstepd will be part of the job limit.
	 * Note that a step which initializes pmi, could cause slurmstepd to
	 * grow, and we don't want this to be part of the step, but be part of
	 * the job.
	 */
	if (level == CG_LEVEL_STEP)
		level = CG_LEVEL_STEP_USER;

	/* Only used in devices cgroup restriction */
	switch (ctl) {
	case CG_DEVICES:
		/*
		 * Set program to point to the needed bpf_program_t depending on
		 * the level and the task_id.
		 */
		if (level == CG_LEVEL_STEP_USER || level == CG_LEVEL_JOB) {
			program = &(p[level]);
			cgroup_path = int_cg[level].path;
		}

		if (level == CG_LEVEL_TASK) {
			if (!(task_cg_info = list_find_first(task_list,
							     _find_task_cg_info,
							     &task_id))) {
				error("No task found with id %u, this should never happen",
				      task_id);
				return SLURM_ERROR;
			}
			program = &(task_cg_info->p);
			cgroup_path = task_cg_info->task_cg.path;
		}

		if (!program) {
			error("EBPF program with task_id %u does not exist",
			      task_id);
			return SLURM_ERROR;
		}

		/*
		 * Only load the program if it has more instructions that the
		 * initial ones.
		 */
		if (program->n_inst > INIT_INST) {
			log_flag(CGROUP,"EBPF Closing and loading bpf program into %s",
				 cgroup_path);
			/* Set the default action*/
			close_ebpf_prog(program, EBPF_ACCEPT);
			/*
			 * Load the ebpf program into the cgroup without the
			 * override flag if we are at TASK level, as this is the
			 * last cgroup in the hierarchy.
			 */
			return load_ebpf_prog(program, cgroup_path,
					      (level != CG_LEVEL_TASK));
		} else {
			log_flag(CGROUP, "EBPF Not loading the program into %s because it is a noop",
				 cgroup_path);
		}
		break;
	default:
		error("cgroup controller %u not supported", ctl);
		return SLURM_ERROR;
		break;
	}

	return SLURM_SUCCESS;
}

extern cgroup_limits_t *cgroup_p_constrain_get(cgroup_ctl_type_t ctl,
					       cgroup_level_t level)
{
	cgroup_limits_t *limits;

	/*
	 * cgroup/v1 legacy compatibility: We have no such levels in cgroup/v2
	 * but we may still get calls for them.
	 */
	if (level == CG_LEVEL_USER) {
		error("Incorrect cgroup level: %d", level);
		return NULL;
	}

	if (level == CG_LEVEL_SLURM)
		level = CG_LEVEL_ROOT;
	/*
	 * Our real step level is the level for user processes. This will make
	 * that the slurmstepd is never constrained in its own cgroup, which is
	 * something we want. Instead, slurmstepd will be part of the job limit.
	 * Note that a step which initializes pmi, could cause slurmstepd to
	 * grow, and we don't want this to be part of the step, but be part of
	 * the job.
	 */
	if (level == CG_LEVEL_STEP)
		level = CG_LEVEL_STEP_USER;

	/* This is for CoreSpec* and MemSpec* for slurmd */
	if (level == CG_LEVEL_SYSTEM)
		level = CG_LEVEL_ROOT;

	limits = xmalloc(sizeof(*limits));
	cgroup_init_limits(limits);

	switch (ctl) {
	case CG_TRACK:
		/* Not implemented. */
		goto fail;
	case CG_CPUS:
		/*
		 * cpuset.cpus:
		 * ------------
		 * It lists the *requested* CPUs to be used by tasks within this
		 * cgroup. The actual list of CPUs to be granted, however, is
		 * subjected to constraints imposed by its parent and can differ
		 * from the requested CPUs.
		 *
		 * An empty value in cpuset.cpus indicates that the cgroup is
		 * using the same setting as the nearest cgroup ancestor with a
		 * non-empty cpuset.cpus, or all the available CPUs if none is
		 * found.
		 *
		 * cpuset.cpus.effective:
		 * ----------------------
		 * It lists the onlined CPUs that are actually granted to this
		 * cgroup by its parent. These CPUs are allowed to be used by
		 * tasks within the current cgroup.
		 *
		 * If cpuset.cpus is empty, the cpuset.cpus.effective file shows
		 * all the CPUs from the parent cgroup that can be available to
		 * be used by this cgroup.
		 *
		 * If cpuset.cpus is not empty, the cpuset.cpus.effective file
		 * should be a subset of cpuset.cpus unless none of the CPUs
		 * listed in cpuset.cpus can be granted. In this case, it will
		 * be treated just like an empty cpuset.cpus.
		 */
		if (common_cgroup_get_param(
			    &int_cg[level],
			    "cpuset.cpus",
			    &limits->allow_cores,
			    &limits->cores_size) != SLURM_SUCCESS)
			goto fail;

		if ((limits->cores_size == 1) &&
		    !xstrcmp(limits->allow_cores, "\n")) {
			xfree(limits->allow_cores);
			if (common_cgroup_get_param(
				    &int_cg[level],
				    "cpuset.cpus.effective",
				    &limits->allow_cores,
				    &limits->cores_size) != SLURM_SUCCESS)
				goto fail;
		}

		/*
		 * The same concepts from cpuset.cpus and cpuset.cpus.effective
		 * applies for cpuset.mems and cpuset.mems.effective, so follow
		 * the same logic here.
		 */
		if (common_cgroup_get_param(
			    &int_cg[level],
			    "cpuset.mems",
			    &limits->allow_mems,
			    &limits->mems_size) != SLURM_SUCCESS)
			goto fail;

		if ((limits->mems_size == 1) &&
		    !xstrcmp(limits->allow_mems, "\n")) {
			xfree(limits->allow_mems);
			if (common_cgroup_get_param(
				    &int_cg[level],
				    "cpuset.mems.effective",
				    &limits->allow_mems,
				    &limits->mems_size) != SLURM_SUCCESS)
				goto fail;
		}

		/*
		 * Replace the last \n by \0. We lose one byte but we don't care
		 * since tipically this object will be freed soon and we still
		 * keep the correct array size.
		 */
		if (limits->cores_size > 0)
			limits->allow_cores[(limits->cores_size)-1] = '\0';

		if (limits->mems_size > 0)
			limits->allow_mems[(limits->mems_size)-1] = '\0';
		break;
	case CG_MEMORY:
		/* Not implemented. */
		goto fail;
	case CG_DEVICES:
		/* Not implemented. */
		goto fail;
	default:
		error("cgroup controller %u not supported", ctl);
		goto fail;
	}

	return limits;
fail:
	log_flag(CGROUP, "Returning empty limits, this should not happen.");
	cgroup_free_limits(limits);
	return NULL;
}

extern int cgroup_p_step_start_oom_mgr()
{
	/* Just return, no need to start anything. */
	return SLURM_SUCCESS;
}

extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *job)
{
	cgroup_oom_t *oom_step_results = NULL;
	char *mem_events = NULL, *mem_swap_events = NULL, *ptr;
	size_t sz;
	uint64_t job_kills, step_kills, job_swkills, step_swkills;

	if (!bit_test(int_cg_ns.avail_controllers, CG_MEMORY))
		return NULL;

	/*
	 * memory.events:
	 * all fields in this file are hierarchical and the file modified event
	 * can be generated due to an event down the hierarchy. For the local
	 * events at the cgroup level we can check memory.events.local instead.
	 */

	/* Get latest stats for the step */
	if (common_cgroup_get_param(&int_cg[CG_LEVEL_STEP_USER],
				    "memory.events",
				    &mem_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.events",
		      int_cg[CG_LEVEL_STEP_USER].path);

	if (common_cgroup_get_param(&int_cg[CG_LEVEL_STEP_USER],
				    "memory.swap.events",
				    &mem_swap_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.swap.events",
		      int_cg[CG_LEVEL_STEP_USER].path);

	if (mem_events) {
		if ((ptr = xstrstr(mem_events, "oom_kill"))) {
			if (sscanf(ptr, "oom_kill %lu", &step_kills) != 1)
				error("Cannot read step's oom_kill counter from memory.events file.");
		}
		xfree(mem_events);
	}

	if (mem_swap_events) {
		if ((ptr = xstrstr(mem_swap_events, "fail"))) {
			if (sscanf(ptr, "fail %lu", &step_swkills) != 1)
				error("Cannot read step's fail counter from memory.swap.events file.");
		}
		xfree(mem_swap_events);
	}

	/* Get stats for the job */
	if (common_cgroup_get_param(&int_cg[CG_LEVEL_JOB],
				    "memory.events",
				    &mem_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.events",
		      int_cg[CG_LEVEL_STEP_USER].path);

	if (common_cgroup_get_param(&int_cg[CG_LEVEL_JOB], "memory.swap.events",
				    &mem_swap_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.swap.events",
		      int_cg[CG_LEVEL_STEP_USER].path);


	if (mem_events) {
		if ((ptr = xstrstr(mem_events, "oom_kill"))) {
			if (sscanf(ptr, "oom_kill %lu", &job_kills) != 1)
				error("Cannot read job's oom_kill counter from memory.events file.");
		}
		xfree(mem_events);
	}

	if (mem_swap_events) {
		if ((ptr = xstrstr(mem_swap_events, "fail"))) {
			if (sscanf(ptr, "fail %lu", &job_swkills) != 1)
				error("Cannot read step's fail counter from memory.swap.events file.");
		}
		xfree(mem_swap_events);
	}

	/* Return stats */
	log_flag(CGROUP, "OOM detected %lu job and %lu step kills",
		 job_kills, step_kills);
	oom_step_results = xmalloc(sizeof(*oom_step_results));
	oom_step_results->job_mem_failcnt = job_kills;
	oom_step_results->job_memsw_failcnt = job_swkills;
	oom_step_results->step_mem_failcnt = step_kills;
	oom_step_results->step_memsw_failcnt = step_swkills;

	return oom_step_results;
}

extern int cgroup_p_task_addto(cgroup_ctl_type_t ctl, stepd_step_rec_t *job,
			       pid_t pid, uint32_t task_id)
{
	task_cg_info_t *task_cg_info;
	char *task_cg_path = NULL;
	uid_t uid = job->uid;
	gid_t gid = job->gid;
	bool need_to_add = false;

	/* Ignore any possible movement of slurmstepd */
	if (pid == getpid())
		return SLURM_SUCCESS;

	if (task_id == task_special_id)
		log_flag(CGROUP, "Starting task_special cgroup accounting");
	else
		log_flag(CGROUP, "Starting task %u cgroup accounting", task_id);

	/* Let's be sure this task is not already created. */
	if (!(task_cg_info = list_find_first(task_list, _find_task_cg_info,
					     &task_id))) {
		task_cg_info = xmalloc(sizeof(*task_cg_info));
		task_cg_info->taskid = task_id;
		need_to_add = true;
	}

	if (need_to_add) {
		/* Create task hierarchy in this step. */
		if (task_id == task_special_id)
			xstrfmtcat(task_cg_path, "%s/task_special",
				   int_cg[CG_LEVEL_STEP_USER].name);
		else
			xstrfmtcat(task_cg_path, "%s/task_%u",
				   int_cg[CG_LEVEL_STEP_USER].name, task_id);

		if (common_cgroup_create(&int_cg_ns, &task_cg_info->task_cg,
					 task_cg_path, uid, gid) !=
		    SLURM_SUCCESS) {
			if (task_id == task_special_id)
				error("unable to create task_special cgroup");
			else
				error("unable to create task %u cgroup",
				      task_id);
			xfree(task_cg_info);
			xfree(task_cg_path);
			return SLURM_ERROR;
		}
		xfree(task_cg_path);

		if (common_cgroup_instantiate(&task_cg_info->task_cg) !=
		    SLURM_SUCCESS) {
			if (task_id == task_special_id)
				error("unable to instantiate task_special cgroup");
			else
				error("unable to instantiate task %u cgroup",
				      task_id);
			common_cgroup_destroy(&task_cg_info->task_cg);
			xfree(task_cg_info);
			return SLURM_ERROR;
		}
                /* Inititalize the bpf_program before appending to the list. */
		init_ebpf_prog(&task_cg_info->p);

		/* Add the cgroup to the list now that it is initialized. */
		list_append(task_list, task_cg_info);
	}

	/* Attach the pid to the corresponding step_x/task_y cgroup */
	if (common_cgroup_move_process(&task_cg_info->task_cg, pid) !=
	    SLURM_SUCCESS)
		error("Unable to move pid %d to %s cg",
		      pid, (task_cg_info->task_cg).path);

	return SLURM_SUCCESS;
}

extern cgroup_acct_t *cgroup_p_task_get_acct_data(uint32_t task_id)
{
	char *cpu_stat = NULL, *memory_stat = NULL, *ptr;
	size_t cpu_stat_sz = 0, memory_stat_sz = 0;
	cgroup_acct_t *stats = NULL;
	task_cg_info_t *task_cg_info;
	uint64_t tmp;

	if (!(task_cg_info = list_find_first(task_list, _find_task_cg_info,
					     &task_id))) {
		if (task_id == task_special_id)
			error("No task found with id %u (task_special), this should never happen",
			      task_id);
		else
			error("No task found with id %u, this should never happen",
			      task_id);
		return NULL;
	}

	if (common_cgroup_get_param(&task_cg_info->task_cg,
				    "cpu.stat",
				    &cpu_stat,
				    &cpu_stat_sz) != SLURM_SUCCESS) {
		if (task_id == task_special_id)
			log_flag(CGROUP, "Cannot read task_special cpu.stat file");
		else
			log_flag(CGROUP, "Cannot read task %d cpu.stat file",
				 task_id);
	}

	if (common_cgroup_get_param(&task_cg_info->task_cg,
				    "memory.stat",
				    &memory_stat,
				    &memory_stat_sz) != SLURM_SUCCESS) {
		if (task_id == task_special_id)
			log_flag(CGROUP, "Cannot read task_special memory.stat file");
		else
			log_flag(CGROUP, "Cannot read task %d memory.stat file",
				 task_id);
	}

	/*
	 * Initialize values. A NO_VAL64 will indicate the caller that something
	 * happened here.
	 */
	stats = xmalloc(sizeof(*stats));
	stats->usec = NO_VAL64;
	stats->ssec = NO_VAL64;
	stats->total_rss = NO_VAL64;
	stats->total_pgmajfault = NO_VAL64;

	if (cpu_stat) {
		ptr = xstrstr(cpu_stat, "user_usec");
		if (ptr && (sscanf(ptr, "user_usec %lu", &stats->usec) != 1))
			error("Cannot parse user_sec field in cpu.stat file");

		ptr = xstrstr(cpu_stat, "system_usec");
		if (ptr && (sscanf(ptr, "system_usec %lu", &stats->ssec) != 1))
			error("Cannot parse system_usec field in cpu.stat file");
		xfree(cpu_stat);
	}

	/*
	 * In cgroup/v1, total_rss was the hierarchical sum of # of bytes of
	 * anonymous and swap cache memory (including transparent huge pages),
	 * so let's make the sum here to make the same thing.
	 *
	 * In cgroup/v2 we could use memory.current, but that includes all the
	 * memory the app has touched. We opt here to do a more fine-grain
	 * calculation reading different fields.
	 *
	 * It is possible that some of the fields do not exist, for example if
	 * swap is not enabled the swapcached value won't exist, in that case
	 * we won't take it into account.
	 */
	if (memory_stat) {
		ptr = xstrstr(memory_stat, "anon");
		if (ptr && (sscanf(ptr, "anon %lu", &stats->total_rss) != 1))
			error("Cannot parse anon field in memory.stat file");

		ptr = xstrstr(memory_stat, "swapcached");
		if (ptr && (sscanf(ptr, "swapcached %lu", &tmp) != 1))
			log_flag(CGROUP, "Cannot parse swapcached field in memory.stat file");
		else
			stats->total_rss += tmp;

		ptr = xstrstr(memory_stat, "anon_thp");
		if (ptr && (sscanf(ptr, "anon_thp %lu", &tmp) != 1))
			log_flag(CGROUP, "Cannot parse anon_thp field in memory.stat file");
		else
			stats->total_rss += tmp;

		/*
		 * Future: we can add more here or do a more fine-grain control
		 * with shmem or others depending on NoShare or UsePSS.
		 */

		ptr = xstrstr(memory_stat, "pgmajfault");
		if (ptr && (sscanf(ptr, "pgmajfault %lu",
				   &stats->total_pgmajfault) != 1))
			log_flag(CGROUP, "Cannot parse pgmajfault field in memory.stat file");
		xfree(memory_stat);
	}

	return stats;
}

/*
 * Return conversion units used for stats gathered from cpuacct.
 * Dividing the provided data by this number will give seconds.
 */
extern long int cgroup_p_get_acct_units()
{
	/* usec and ssec from cpuacct.stat are provided in micro-seconds. */
	return (long int)USEC_IN_SEC;
}
