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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <poll.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/daemonize.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/plugins/cgroup/common/cgroup_common.h"
#include "src/plugins/cgroup/v2/cgroup_dbus.h"
#include "src/plugins/cgroup/v2/ebpf.h"

#define SYSTEM_CGSLICE "system.slice"
#define SYSTEM_CGSCOPE "slurmstepd"
#define SYSTEM_CGDIR "system"
#define CGROUP_MAX_RETRIES 100

const char plugin_name[] = "Cgroup v2 plugin";
const char plugin_type[] = "cgroup/v2";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Internal cgroup structs */
static List task_list;
static uint16_t step_active_cnt;
static xcgroup_ns_t int_cg_ns;
static xcgroup_t int_cg[CG_LEVEL_CNT];
static bpf_program_t p[CG_LEVEL_CNT];
static char *stepd_scope_path = NULL;
static uint32_t task_special_id = NO_VAL;
static char *invoc_id;
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

extern bool cgroup_p_has_feature(cgroup_ctl_feature_t f);
extern int cgroup_p_task_addto(cgroup_ctl_type_t ctl, stepd_step_rec_t *step,
			       pid_t pid, uint32_t task_id);

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
 * The cgroup v2 documented way to know which is the process root in the cgroup
 * hierarchy is just to read /proc/self/cgroup. In Unified hierarchies this
 * must contain only one line. If there are more lines this would mean we are
 * in Hybrid or in Legacy cgroup.
 */
static char *_get_self_cg_path()
{
	char *buf, *start = NULL, *p, *ret = NULL;
	size_t sz;

	if (common_file_read_content("/proc/self/cgroup", &buf, &sz) !=
	    SLURM_SUCCESS)
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
	if ((p = xstrchr(buf, ':'))) {
		if ((p + 2) < (buf + sz - 1))
			start = p + 2;
	}

	if (start && (*start != '\0')) {
		if ((p = xstrchr(start, '\n')))
			*p = '\0';
		xstrfmtcat(ret, "%s%s",
			   slurm_cgroup_conf.cgroup_mountpoint, start);
	}

	xfree(buf);
	return ret;
}

/*
 * Get the cgroup root directory by reading /proc/1/cgroup path.
 *
 * We expect one single line like this:
 * "0::/init.scope\n"
 *
 * But in containerized environments it could look like:
 * "0::/docker.slice/docker-<some UUID>.scope/init.scope"
 *
 * This function just strips the "0::" and "init.scope" portions.
 *
 * In normal systems the final path will look like this:
 * /sys/fs/cgroup[/]
 *
 * In containerized environments it will look like:
 * /sys/fs/cgroup[/docker.slice/docker-<some UUID>.scope]
 *
 */
static char *_get_init_cg_path()
{
	char *buf, *start = NULL, *p, *ret = NULL;
	size_t sz;

	if (common_file_read_content("/proc/1/cgroup", &buf, &sz) !=
	    SLURM_SUCCESS)
		fatal("cannot read /proc/1/cgroup contents: %m");

	/*
	 * In Unified mode there will be just one line containing the path
	 * of the cgroup and starting by 0. If there are more than one then
	 * some v1 cgroups are mounted, we do not support it.
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
		p = xdirname(start);
		if (!xstrcmp(p, "/"))
			xstrfmtcat(ret, "%s",
				   slurm_cgroup_conf.cgroup_mountpoint);
		else
			xstrfmtcat(ret, "%s%s",
				   slurm_cgroup_conf.cgroup_mountpoint, p);
		xfree(p);
	}
	xfree(buf);
	return ret;
}

/*
 * Fill up the internal cgroup namespace object. This mainly contains the path
 * to what will be our root cgroup.
 * E.g. /sys/fs/cgroup/system.slice/node1_slurmstepd.scope/ for slurmstepd.
 */
static void _set_int_cg_ns()
{
	char *init_cg_path = _get_init_cg_path();

#ifdef MULTIPLE_SLURMD
	xstrfmtcat(stepd_scope_path, "%s/%s/%s_%s.scope",
		   init_cg_path,
		   SYSTEM_CGSLICE, conf->node_name,
		   SYSTEM_CGSCOPE);
#else
	xstrfmtcat(stepd_scope_path, "%s/%s/%s.scope",
		   init_cg_path,
		   SYSTEM_CGSLICE, SYSTEM_CGSCOPE);
#endif
	if (running_in_slurmstepd())
		int_cg_ns.mnt_point = stepd_scope_path;
	else
		int_cg_ns.mnt_point = _get_self_cg_path();

	xfree(init_cg_path);
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
	int i, rc = SLURM_SUCCESS, rc2;
	char *content = NULL, *file_path = NULL;

	xassert(ctl_bitmap);

	xstrfmtcat(file_path, "%s/cgroup.subtree_control", path);
	for (i = 0; i < CG_CTL_CNT; i++) {
		if (!bit_test(ctl_bitmap, i))
			continue;

		xstrfmtcat(content, "+%s", ctl_names[i]);
		rc2 = common_file_write_content(file_path, content,
					       strlen(content));
		if (rc2 != SLURM_SUCCESS) {
			/*
			 * In a container it is possible that part of the
			 * cgroup tree is mounted in read-only mode, so skip
			 * the parts that we cannot touch.
			 */
			if (errno == EROFS) {
				log_flag(CGROUP,
					 "Cannot enable %s in %s, skipping: %m",
					 ctl_names[i], file_path);
			} else {
				/* Controller won't be available. */
				error("Cannot enable %s in %s: %m",
				      ctl_names[i], file_path);
				bit_clear(ctl_bitmap, i);
				rc = SLURM_ERROR;
			}
		} else {
			log_flag(CGROUP, "Enabled %s controller in %s",
				 ctl_names[i], file_path);
		}
		xfree(content);
	}
	xfree(file_path);
	return rc;
}

static int _get_controllers(char *path, bitstr_t *ctl_bitmap)
{
	char *buf = NULL, *ptr, *save_ptr, *ctl_filepath = NULL;
	size_t sz;

	xassert(ctl_bitmap);

	xstrfmtcat(ctl_filepath, "%s/cgroup.controllers", path);
	if (common_file_read_content(ctl_filepath, &buf, &sz) !=
	    SLURM_SUCCESS || !buf) {
		error("cannot read %s: %m", ctl_filepath);
		xfree(ctl_filepath);
		return SLURM_ERROR;
	}
	xfree(ctl_filepath);

	if (buf[sz - 1] == '\n')
		buf[sz - 1] = '\0';

	ptr = strtok_r(buf, " ", &save_ptr);
	while (ptr) {
		for (int i = 0; i < CG_CTL_CNT; i++) {
			if (!xstrcmp(ctl_names[i], ""))
				continue;
			if (!xstrcasecmp(ctl_names[i], ptr)) {
				bit_set(ctl_bitmap, i);
				break;
			}
		}
		ptr = strtok_r(NULL, " ", &save_ptr);
	}
	xfree(buf);

	for (int i = 0; i < CG_CTL_CNT; i++) {
		if ((i == CG_DEVICES) || (i == CG_TRACK))
			continue;
		if (invoc_id && !bit_test(ctl_bitmap, i))
			error("Controller %s is not enabled!", ctl_names[i]);
	}
	return SLURM_SUCCESS;
}

/*
 * Enabling the subtree from the top mountpoint to the slice we will reside
 * is needed to get all the controllers we want to support. Nevertheless note
 * that if systemd is reloaded, reset, or does any operation that implies
 * traversing the cgroup tree matching its internal database, and there's no
 * service started with Delegate=yes (like running this slurmd manually), the
 * controllers can eventually be deactivated without warning by systemd.
 *
 * Also note that usually starting any service or scope with Delegate=yes in the
 * slice we want to live, will make systemd to automatically activate the
 * controllers in the tree, so this operation here would be redundant.
 */
static int _enable_system_controllers()
{
	char *slice_path = NULL;
	bitstr_t *system_ctrls = bit_alloc(CG_CTL_CNT);
	char *tok, *next, *curr;
	char *save_ptr = NULL, *orig = NULL;
	bool started = false;

	if (_get_controllers(slurm_cgroup_conf.cgroup_mountpoint,
			     system_ctrls) != SLURM_SUCCESS) {
		FREE_NULL_BITMAP(system_ctrls);
		return SLURM_ERROR;
	}

	/* Enable controllers for the top of the tree. */
	_enable_subtree_control(slurm_cgroup_conf.cgroup_mountpoint,
				system_ctrls);

	/* Enable it for us, slurmd, recursively. We may be anywhere. */
	next = xmalloc(strlen(int_cg_ns.mnt_point) + 1);
	curr = xmalloc(strlen(int_cg_ns.mnt_point) + 1);
	orig = xstrdup(int_cg_ns.mnt_point);
	tok = strtok_r(orig, "/", &save_ptr);
	while (tok) {
		/* Start from the mnt_point skipping any previous dir. */
		if (!started &&
		    (!xstrcmp(next, slurm_cgroup_conf.cgroup_mountpoint)))
			started = true;

		sprintf(next, "%s/%s", curr, tok);
		strcpy(curr, next);

		/* Skip the last directory which is a leaf where we live. */
		if (started && !xstrcmp(curr, int_cg_ns.mnt_point))
			break;

		if (started)
			_enable_subtree_control(curr, system_ctrls);

		tok = strtok_r(NULL, "/", &save_ptr);
	}
	xfree(orig);
	xfree(curr);
	xfree(next);


	/*
	 * Enable it for system.slice, where the stepd scope will reside when
	 * it is created later.
	 */
	slice_path = xdirname(stepd_scope_path);
	_enable_subtree_control(slice_path, system_ctrls);
	xfree(slice_path);

	FREE_NULL_BITMAP(system_ctrls);
	return SLURM_SUCCESS;
}

/*
 * Read the cgroup.controllers file of the root to detect which are the
 * available controllers in this system.
 */
static int _setup_controllers()
{
	/* Field not used in v2 */
	int_cg_ns.subsystems = NULL;

	/*
	 * Check all the available controllers in this system and enable them in
	 * every level of the cgroup tree if EnableControllers=yes.
	 * Normally, if the unit we're starting up has a Delegate=yes, systemd
	 * will set the cgroup.subtree_controllers of the parent with all the
	 * available controllers on that level, making all of them available on
	 * our unit automatically. In some situations, like if the parent cgroup
	 * doesn't have write permissions or if it started with fewer
	 * controllers available than the ones on the system (when the
	 * grandfather doesn't have subtree_control set), that won't happen and
	 * we may need Enablecontrollers. This may happen in containers.
	 */
	if (running_in_slurmd() && slurm_cgroup_conf.enable_controllers)
		_enable_system_controllers();

	/* Get the controllers on our namespace. */
	return _get_controllers(int_cg_ns.mnt_point,
				int_cg_ns.avail_controllers);
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

static int _find_purge_task_special(task_cg_info_t *task_ptr, uint32_t *id)
{
	if (task_ptr->taskid == *id) {
		if (common_cgroup_delete(&task_ptr->task_cg) != SLURM_SUCCESS)
			log_flag(CGROUP, "Failed to cleanup %s: %m",
				 task_ptr->task_cg.path);
		return 1;
	}
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

	xfree(pids);
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

static int _init_stepd_system_scope(pid_t pid)
{
	char *system_dir = "/" SYSTEM_CGDIR;
	char *self_cg_path;

	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_SYSTEM],
				 system_dir, (uid_t) 0, (gid_t) 0) !=
	    SLURM_SUCCESS) {
		error("unable to create system cgroup %s", system_dir);
		return SLURM_ERROR;
	}

	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_SYSTEM]) !=
	    SLURM_SUCCESS) {
		error("Unable to instantiate system %s cgroup", system_dir);
		return SLURM_ERROR;
	}

	if (common_cgroup_move_process(&int_cg[CG_LEVEL_SYSTEM], pid) !=
	    SLURM_SUCCESS) {
		error("Unable to attach pid %d to %s cgroup.", pid, system_dir);
		return SLURM_ERROR;
	}

	/* Now check we're really where we belong to. */
	self_cg_path = _get_self_cg_path();
	if (xstrcmp(self_cg_path, int_cg[CG_LEVEL_SYSTEM].path)) {
		error("Could not move slurmstepd pid %d to a Slurm's delegated cgroup. Should be in %s, we are in %s.",
		      pid, self_cg_path, int_cg[CG_LEVEL_SYSTEM].path);
		xfree(self_cg_path);
		return SLURM_ERROR;
	}
	xfree(self_cg_path);

	if (_enable_subtree_control(int_cg[CG_LEVEL_ROOT].path,
				    int_cg_ns.avail_controllers) !=
	    SLURM_SUCCESS) {
		error("Cannot enable subtree_control at the top level %s",
		      int_cg_ns.mnt_point);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _init_new_scope(char *scope_path)
{
	int rc;

	rc = mkdir(scope_path, 0755);
	if (rc && (errno != EEXIST)) {
		error("Could not create scope directory %s: %m", scope_path);
		return SLURM_ERROR;
	}

	log_flag(CGROUP, "Created %s", scope_path);

	return SLURM_SUCCESS;
}

/*
 * Talk to systemd through dbus to move the slurmstepd pid into the reserved
 * scope for stepds and user processes.
 */
static int _init_new_scope_dbus(char *scope_path)
{
	struct stat sb;
	int status, retries = 0;
	pid_t pid, sleep_parent_pid;
	xcgroup_t sys_root;
	char *const argv[3] = {
		(char *)conf->stepd_loc,
		"infinity",
		NULL };

	pid = fork();

	if (pid < 0) {
		return SLURM_ERROR;
	} else if (pid == 0) { /* child */
		sleep_parent_pid = getpid();
		if (cgroup_dbus_attach_to_scope(sleep_parent_pid, scope_path) !=
		    SLURM_SUCCESS) {
			/*
			 * Systemd scope unit may already exist or is stuck, and
			 * the directory is not there!.
			 */
			_exit(1);
		}
		/*
		 * Wait for the scope to be alive. There's no way that dbus
		 * tells us if the mkdir operation on the cgroup filesystem went
		 * well, and we know that there's a noticeable delay. Because of
		 * systemd and because of cgroup. We need to wait.
		 */
		while ((retries < CGROUP_MAX_RETRIES) &&
		       (stat(scope_path, &sb) < 0)) {
			if (errno == ENOENT) {
				retries++;
				usleep(10000);
			} else {
				error("stat() error waiting for %s to show up after dbus call: %m",
				      scope_path);
				_exit(1);
			}
		}
		if (retries < CGROUP_MAX_RETRIES) {
			/*
			 * We need to "abandon" this scope or if we terminate,
			 * the unit will also terminate.
			 */
			if (cgroup_dbus_abandon_scope(scope_path))
				error("Cannot abandon cgroup scope %s",
				      scope_path);
			else
				log_flag(CGROUP, "Abandoned scope %s",
					 scope_path);
		} else {
			error("Long time waiting for %s to show up after dbus call.",
			      scope_path);
		}
		if (retries > 1)
			log_flag(CGROUP, "Possible systemd slowness, %d msec waiting scope to show up.",
				 (retries * 10));

		/*
		 * Assuming the scope is created, let's mkdir the /system dir
		 * which will allocate the sleep inifnity pid. This way the
		 * slurmstepd scope won't be a leaf anymore and we'll be able
		 * to create more directories. _init_new_scope here is simply
		 * used as a mkdir.
		 */
		memset(&sys_root, 0, sizeof(sys_root));
		xstrfmtcat(sys_root.path, "%s/%s", scope_path, SYSTEM_CGDIR);
		if (_init_new_scope(sys_root.path) != SLURM_SUCCESS) {
			xfree(sys_root.path);
			_exit(1);
		}
		/* Success!, we got the cg directory, move ourselves there. */
		if (common_cgroup_move_process(&sys_root, sleep_parent_pid)) {
			error("Unable to move pid %d to system cgroup %s",
			      sleep_parent_pid, sys_root.path);
			_exit(1);
		}
		common_cgroup_destroy(&sys_root);

		/*
		 * We don't need to wait to be in the new cgroup, if the fs is
		 * slow we anyway know that at some point the kernel will move
		 * us there. So continue as normal.
		 */
		if (xdaemon() != 0) {
			error("Cannot spawn dummy process for the systemd scope.");
			_exit(127);
		}
		execvp(argv[0], argv);
		error("execvp of slurmstepd wait failed: %m");
		_exit(127);
	} else if (pid > 0) {
		if ((waitpid(pid, &status, 0) != pid) || WEXITSTATUS(status)) {
			/* If we receive an error it means scope is not ok. */
			error("%s: scope and/or cgroup directory for slurmstepd could not be set.",
			      __func__);
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
}

/*
 * If IgnoreSystemd=yes in cgroup.conf we do a mkdir in
 * /sys/fs/cgroup/system.slice/<nodename>_slurmstepd or /slurmstepd if no
 * MULTIPLE_SLURMD.
 *
 * Otherwise call dbus to talk to systemd and create a 'scope' which will in
 * turn create the same cgroup directory.
 *
 * This directory will be used to place future slurmstepds.
 */
static int _init_slurmd_system_scope()
{
	struct stat sb;

	/* Do only if the cgroup associated to the scope is not created yet. */
	if (!stat(stepd_scope_path, &sb))
		return SLURM_SUCCESS;

	/*
	 * If we don't want to use systemd at all just create the cgroup
	 * directories manually and return.
	 */
	if (slurm_cgroup_conf.ignore_systemd)
		return _init_new_scope(stepd_scope_path);

	/* Call systemd through dbus to create a new scope. */
	if ((_init_new_scope_dbus(stepd_scope_path) != SLURM_SUCCESS)) {
		if (slurm_cgroup_conf.ignore_systemd_on_failure) {
			log_flag(CGROUP, "Could not create scope through systemd, doing it manually as IgnoreSystemdOnFailure is set in cgroup.conf");
			return _init_new_scope(stepd_scope_path);
		} else {
			error("cannot initialize cgroup directory for stepds: if the scope %s already exists it means the associated cgroup directories disappeared and the scope entered in a failed state. You should investigate why the scope lost its cgroup directories and possibly use the 'systemd reset-failed' command to fix this inconsistent systemd state.",
			      stepd_scope_path);
			return SLURM_ERROR;
		}
	}
	return SLURM_SUCCESS;
}

/*
 * Slurmd started manually may not remain in the actual scope. Normally there
 * are other pids there, like the terminal from where it's been launched, so
 * slurmd would affect these pids. For example a CoreSpecCount of 1 would leave
 * the bash terminal with only one core.
 *
 * Get out of there and put ourselves into a new home. This shouldn't happen on
 * production systems.
 */
static int _migrate_to_stepd_scope()
{
	char *new_home = NULL;
	pid_t slurmd_pid = getpid();

	bit_clear_all(int_cg_ns.avail_controllers);
	common_cgroup_destroy(&int_cg[CG_LEVEL_ROOT]);
	common_cgroup_ns_destroy(&int_cg_ns);

	xstrfmtcat(new_home, "%s/slurmd", stepd_scope_path);
	int_cg_ns.mnt_point = new_home;

	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_ROOT], "",
				 (uid_t) 0, (gid_t) 0) != SLURM_SUCCESS) {
		error("unable to create root cgroup");
		return SLURM_ERROR;
	}

	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_ROOT]) !=
	    SLURM_SUCCESS) {
		error("Unable to instantiate slurmd %s cgroup", new_home);
		return SLURM_ERROR;
	}
	log_flag(CGROUP, "Created %s", new_home);

	/*
	 * Set invoc_id to empty string to indicate that from now on we should
	 * behave as if we were spawned by systemd.
	 */
	invoc_id = "";

	if (_get_controllers(stepd_scope_path, int_cg_ns.avail_controllers) !=
	    SLURM_SUCCESS)
		return SLURM_ERROR;

	if (_enable_subtree_control(stepd_scope_path,
				    int_cg_ns.avail_controllers) !=
	    SLURM_SUCCESS) {
		error("Cannot enable subtree_control at the top level %s",
		      int_cg_ns.mnt_point);
		return SLURM_ERROR;
	}

	if (common_cgroup_move_process(&int_cg[CG_LEVEL_ROOT], slurmd_pid) !=
	    SLURM_SUCCESS) {
		error("Unable to attach slurmd pid %d to %s cgroup.",
		      slurmd_pid, new_home);
		return SLURM_ERROR;
	}

	return _setup_controllers();
}

static void _get_memory_events(uint64_t *job_kills, uint64_t *step_kills)
{
	size_t sz;
	char *mem_events = NULL, *ptr;

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

	if (mem_events) {
		if ((ptr = xstrstr(mem_events, "oom_kill "))) {
			if (sscanf(ptr, "oom_kill %"PRIu64, step_kills) != 1)
				error("Cannot read step's oom_kill counter from memory.events file.");
		}
		xfree(mem_events);
	}

	/* Get stats for the job */
	if (common_cgroup_get_param(&int_cg[CG_LEVEL_JOB],
				    "memory.events",
				    &mem_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.events",
		      int_cg[CG_LEVEL_STEP_USER].path);

	if (mem_events) {
		if ((ptr = xstrstr(mem_events, "oom_kill "))) {
			if (sscanf(ptr, "oom_kill %"PRIu64, job_kills) != 1)
				error("Cannot read job's oom_kill counter from memory.events file.");
		}
		xfree(mem_events);
	}
}

static void _get_swap_events(uint64_t *job_swkills, uint64_t *step_swkills)
{
	size_t sz;
	char *mem_swap_events = NULL, *ptr;

	/* Get latest swap stats for the step */
	if (common_cgroup_get_param(&int_cg[CG_LEVEL_STEP_USER],
				    "memory.swap.events",
				    &mem_swap_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.swap.events",
		      int_cg[CG_LEVEL_STEP_USER].path);

	if (mem_swap_events) {
		if ((ptr = xstrstr(mem_swap_events, "fail "))) {
			if (sscanf(ptr, "fail %"PRIu64, step_swkills) != 1)
				error("Cannot read step's fail counter from memory.swap.events file.");
		}
		xfree(mem_swap_events);
	}

	/* Get swap stats for the job */
	if (common_cgroup_get_param(&int_cg[CG_LEVEL_JOB], "memory.swap.events",
				    &mem_swap_events, &sz) != SLURM_SUCCESS)
		error("Cannot read %s/memory.swap.events",
		      int_cg[CG_LEVEL_STEP_USER].path);

	if (mem_swap_events) {
		if ((ptr = xstrstr(mem_swap_events, "fail "))) {
			if (sscanf(ptr, "fail %"PRIu64, job_swkills) != 1)
				error("Cannot read job's fail counter from memory.swap.events file.");
		}
		xfree(mem_swap_events);
	}
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
	 * Detect if we are started by systemd. Another way could be to check
	 * if our PPID=1, but we cannot rely on it because when starting slurmd
	 * with -D over a sshd session, slurmd will be reparented by 1, and
	 * doing this on a graphical session, it will be reparented by
	 * "systemd --user". So it is not a reliable check. Instead use
	 * the existence of INVOCATION_ID to know if the pid has been forked by
	 * systemd.
	 */
	invoc_id = getenv("INVOCATION_ID");

	/*
	 * Check our current root dir. Systemd MUST have Delegated it to us,
	 * so we want slurmd to be started by systemd. In the case of stepd
	 * we must guess our future path here, and make the directory later.
	 */
	_set_int_cg_ns();
	if (!int_cg_ns.mnt_point) {
		error("Cannot setup the cgroup namespace.");
		return SLURM_ERROR;
	}

	/* Setup the root cgroup object. */
	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_ROOT], "",
				 (uid_t) 0, (gid_t) 0) != SLURM_SUCCESS) {
		error("unable to create root cgroup");
		return SLURM_ERROR;
	}

	/*
	 * Check available controllers in cgroup.controller, record them in our
	 * bitmap and enable them if CgroupAutomountOption is set.
	 * We enable them manually just because we support CgroupIgnoreSystemd
	 * option. Theorically when starting a unit with Delegate=yes, you will
	 * get all controllers available at your level.
	 */
	if (_setup_controllers() != SLURM_SUCCESS)
		return SLURM_ERROR;

	/*
	 * slurmd will setup a new home for future slurmstepds. Every stepd
	 * will emigrate to this new place.
	 */
	if (running_in_slurmd()) {
		if (_init_slurmd_system_scope() != SLURM_SUCCESS)
			return SLURM_ERROR;

		/*
		 * If we are not started by systemd we need to move out to not
		 * mess with the pids that may be in our actual cgroup.
		 */
		if (!invoc_id) {
			log_flag(CGROUP, "assuming slurmd has been started manually.");
			if (_migrate_to_stepd_scope() != SLURM_SUCCESS)
				return SLURM_ERROR;
		} else {
			log_flag(CGROUP, "INVOCATION_ID env var found. Assuming slurmd has been started by systemd.");
		}
	}

	if (running_in_slurmstepd()) {
		/*
		 * We expect slurmd to already have set our scope directory.
		 * Move ourselves in the system subdirectory, which is a
		 * temporary 'parking' until we have not created the job
		 * hierarchy.
		 */
		if (_init_stepd_system_scope(getpid()) != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	/* In cgroup/v2 the entire cgroup tree is owned by root. */
	slurm_cgroup_conf.root_owned_cgroups = true;

	/*
	 * If we're slurmd we're all set and able to constrain things, i.e.
	 * CoreSpec* and MemSpec*.
	 *
	 * If we are a new slurmstepd we are ready now to create job steps. In
	 * that case, since we're still in the temporary "system" directory,
	 * we will need move ourselves out to a new job directory and then
	 * create int_cg[CG_LEVEL_ROOT].path/job_x/step_x.
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
	xfree(stepd_scope_path);

	debug("unloading %s", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * Unlike in Legacy mode (v1) where we needed to create a directory for each
 * controller, in Unified mode this function will do almost nothing except for
 * some sanity checks. That's because hierarchy is unified into the same path.
 * and the controllers will be enabled when we create the hierarchy. The only
 * controller that may need a real init is the 'devices', which in Unified is
 * not a real controller, but instead we need to register an eBPF program.
 */
extern int cgroup_p_initialize(cgroup_ctl_type_t ctl)
{
	switch (ctl) {
	case CG_DEVICES:
		init_ebpf_prog(&p[CG_LEVEL_JOB]);
		init_ebpf_prog(&p[CG_LEVEL_STEP_USER]);
		break;
	case CG_TRACK:
		/* This is not a controller in Cgroup v2.*/
		break;
	default:
		if (!bit_test(int_cg_ns.avail_controllers, ctl)) {
			error("%s cgroup controller is not available.",
			      ctl_names[ctl]);
			return SLURM_ERROR;
		}

		if (running_in_slurmd()) {
			bitstr_t *scope_ctrls = bit_alloc(CG_CTL_CNT);
			_get_controllers(stepd_scope_path, scope_ctrls);
			if (!bit_test(scope_ctrls, ctl)) {
				error("%s cgroup controller is not available for %s.",
				      ctl_names[ctl], stepd_scope_path);
				FREE_NULL_BITMAP(scope_ctrls);
				return SLURM_ERROR;
			}
			FREE_NULL_BITMAP(scope_ctrls);
		}
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
extern int cgroup_p_step_create(cgroup_ctl_type_t ctl, stepd_step_rec_t *step)
{
	int rc = SLURM_SUCCESS;
	char *new_path = NULL;
	char tmp_char[64];

	/*
	 * Lock the root cgroup so we don't race with other steps that are being
	 * terminated and trying to destroy the job_x directory.
	 */
	if (common_cgroup_lock(&int_cg[CG_LEVEL_ROOT]) != SLURM_SUCCESS) {
		error("common_cgroup_lock error (%s)", ctl_names[ctl]);
		return SLURM_ERROR;
	}

	/* Don't let other plugins destroy our structs. */
	step_active_cnt++;

	/* Job cgroup */
	xstrfmtcat(new_path, "/job_%u", step->step_id.job_id);
	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_JOB],
				 new_path, 0, 0) != SLURM_SUCCESS) {
		error("unable to create job %u cgroup", step->step_id.job_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_JOB]) != SLURM_SUCCESS) {
		common_cgroup_destroy(&int_cg[CG_LEVEL_JOB]);
		error("unable to instantiate job %u cgroup",
		      step->step_id.job_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);
	_enable_subtree_control(int_cg[CG_LEVEL_JOB].path,
				int_cg_ns.avail_controllers);

	/* Step cgroup */
	xstrfmtcat(new_path, "%s/step_%s", int_cg[CG_LEVEL_JOB].name,
		   log_build_step_id_str(&step->step_id, tmp_char,
					 sizeof(tmp_char),
					 STEP_ID_FLAG_NO_PREFIX |
					 STEP_ID_FLAG_NO_JOB));

	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_STEP],
				 new_path, 0, 0) != SLURM_SUCCESS) {
		error("unable to create step %ps cgroup", &step->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_STEP]) !=
	    SLURM_SUCCESS) {
		common_cgroup_destroy(&int_cg[CG_LEVEL_STEP]);
		error("unable to instantiate step %ps cgroup", &step->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);
	_enable_subtree_control(int_cg[CG_LEVEL_STEP].path,
				int_cg_ns.avail_controllers);

	/*
	 * We have our stepd directory already into job_x, from now one nobody
	 * can destroy this job directory. We're safe.
	 */
	common_cgroup_unlock(&int_cg[CG_LEVEL_ROOT]);

	/* Step User processes cgroup */
	xstrfmtcat(new_path, "%s/user", int_cg[CG_LEVEL_STEP].name);
	if (common_cgroup_create(&int_cg_ns, &int_cg[CG_LEVEL_STEP_USER],
				 new_path, 0, 0) != SLURM_SUCCESS) {
		error("unable to create step %ps user procs cgroup",
		      &step->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_STEP_USER]) !=
	    SLURM_SUCCESS) {
		common_cgroup_destroy(&int_cg[CG_LEVEL_STEP_USER]);
		error("unable to instantiate step %ps user procs cgroup",
		      &step->step_id);
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
		      &step->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	if (common_cgroup_instantiate(&int_cg[CG_LEVEL_STEP_SLURM]) !=
	    SLURM_SUCCESS) {
		common_cgroup_destroy(&int_cg[CG_LEVEL_STEP_SLURM]);
		error("unable to instantiate step %ps slurm procs cgroup",
		      &step->step_id);
		rc = SLURM_ERROR;
		goto endit;
	}
	xfree(new_path);

	/* Place this stepd is in the correct cgroup. */
	if (common_cgroup_move_process(&int_cg[CG_LEVEL_STEP_SLURM],
				       step->jmgr_pid) != SLURM_SUCCESS) {
		error("unable to move stepd pid to its dedicated cgroup");
		rc = SLURM_ERROR;
	}

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
	int rc = SLURM_SUCCESS;
	pid_t stepd_pid = getpid();

	for (int i = 0; i < npids; i++) {
		/* Ignore any possible movement of slurmstepd */
		if (pids[i] == stepd_pid)
			continue;
		if (cgroup_p_task_addto(ctl, NULL, pids[i],
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
	 * Lock the root cgroup so we don't race with other steps that are being
	 * started and trying to create things inside job_x directory.
	 */
	if (common_cgroup_lock(&int_cg[CG_LEVEL_ROOT]) != SLURM_SUCCESS) {
		error("common_cgroup_lock error (%s)", ctl_names[ctl]);
		return SLURM_ERROR;
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
	common_cgroup_unlock(&int_cg[CG_LEVEL_ROOT]);
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
		if (pids_slurm[i] == pid) {
			xfree(pids_slurm);
			return true;
		}
	}

	xfree(pids_slurm);
	return false;
}

extern int cgroup_p_constrain_set(cgroup_ctl_type_t ctl, cgroup_level_t level,
				  cgroup_limits_t *limits)
{
	int rc = SLURM_SUCCESS;
	bpf_program_t *program = NULL;
	task_cg_info_t *task_cg_info;
	char *dev_id_str = NULL;
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

		dev_id_str = gres_device_id2str(&limits->device);
		if (limits->allow_device)
			log_flag(CGROUP, "Allowing access to device (%s)",
				 dev_id_str);
		else
			log_flag(CGROUP, "Denying access to device (%s)",
				 dev_id_str);
		xfree(dev_id_str);

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

extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *step)
{
	cgroup_oom_t *oom_step_results = NULL;
	uint64_t job_kills = 0, step_kills = 0;
	uint64_t job_swkills = 0, step_swkills = 0;

	if (!bit_test(int_cg_ns.avail_controllers, CG_MEMORY))
		return NULL;

	_get_memory_events(&job_kills, &step_kills);

	if (cgroup_p_has_feature(CG_MEMCG_SWAP))
		_get_swap_events(&job_swkills, &step_swkills);

	/* Return stats */
	log_flag(CGROUP, "OOM detected %"PRIu64" job and %"PRIu64" step kills",
		 job_kills, step_kills);

	oom_step_results = xmalloc(sizeof(*oom_step_results));
	oom_step_results->job_mem_failcnt = job_kills;
	oom_step_results->job_memsw_failcnt = job_swkills;
	oom_step_results->oom_kill_cnt = step_kills;
	oom_step_results->step_mem_failcnt = step_kills;
	oom_step_results->step_memsw_failcnt = step_swkills;

	return oom_step_results;
}

extern int cgroup_p_task_addto(cgroup_ctl_type_t ctl, stepd_step_rec_t *step,
			       pid_t pid, uint32_t task_id)
{
	task_cg_info_t *task_cg_info;
	char *task_cg_path = NULL;
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
					 task_cg_path, 0, 0) != SLURM_SUCCESS) {
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

	/*
	 * If we did not play with task_special and task_special exists it is
	 * possible that another plugin (proctrack) added a pid there and now
	 * this pid has been moved to another normal task, leaving task_special
	 * empty. In that case, try to remove task_special directory and purge
	 * it from the tasks list.
	 */
	if (task_id != task_special_id)
		list_delete_first(task_list,
				  (ListFindF)_find_purge_task_special,
				  &task_special_id);

	return SLURM_SUCCESS;
}

extern cgroup_acct_t *cgroup_p_task_get_acct_data(uint32_t task_id)
{
	char *cpu_stat = NULL, *memory_stat = NULL, *memory_swap_current = NULL;
	char *ptr;
	size_t tmp_sz = 0;
	cgroup_acct_t *stats = NULL;
	task_cg_info_t *task_cg_info;
	uint64_t tmp = 0;

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
				    &tmp_sz) != SLURM_SUCCESS) {
		if (task_id == task_special_id)
			log_flag(CGROUP, "Cannot read task_special cpu.stat file");
		else
			log_flag(CGROUP, "Cannot read task %d cpu.stat file",
				 task_id);
	}

	if (common_cgroup_get_param(&task_cg_info->task_cg,
				    "memory.stat",
				    &memory_stat,
				    &tmp_sz) != SLURM_SUCCESS) {
		if (task_id == task_special_id)
			log_flag(CGROUP, "Cannot read task_special memory.stat file");
		else
			log_flag(CGROUP, "Cannot read task %d memory.stat file",
				 task_id);
	}

	if (common_cgroup_get_param(&task_cg_info->task_cg,
				    "memory.swap.current",
				    &memory_swap_current,
				    &tmp_sz) != SLURM_SUCCESS) {
		if (task_id == task_special_id)
			log_flag(CGROUP, "Cannot read task_special memory.swap.current file");
		else
			log_flag(CGROUP, "Cannot read task %d memory.swap.current file",
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
	stats->total_vmem = NO_VAL64;

	if (cpu_stat) {
		ptr = xstrstr(cpu_stat, "user_usec");
		if (ptr &&
		    (sscanf(ptr, "user_usec %"PRIu64, &stats->usec) != 1))
			error("Cannot parse user_sec field in cpu.stat file");

		ptr = xstrstr(cpu_stat, "system_usec");
		if (ptr &&
		    (sscanf(ptr, "system_usec %"PRIu64, &stats->ssec) != 1))
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
		if (ptr &&
		    (sscanf(ptr, "anon %"PRIu64, &stats->total_rss) != 1))
			error("Cannot parse anon field in memory.stat file");

		ptr = xstrstr(memory_stat, "anon_thp");
		if (ptr && (sscanf(ptr, "anon_thp %"PRIu64, &tmp) != 1))
			log_flag(CGROUP, "Cannot parse anon_thp field in memory.stat file");
		else
			stats->total_rss += tmp;

		ptr = xstrstr(memory_stat, "swapcached");
		if (ptr && (sscanf(ptr, "swapcached %"PRIu64, &tmp) != 1))
			log_flag(CGROUP, "Cannot parse swapcached field in memory.stat file");
		else
			stats->total_rss += tmp;

		/*
		 * Don't add more fields here.
		 * We need swapcached tmp value below.
		 */

		if (stats->total_rss != NO_VAL64) {
			stats->total_vmem = stats->total_rss;

			/* Remove swap cache from VMem before adding all swap */
			if (tmp != NO_VAL64)
				stats->total_vmem -= tmp;

			ptr = xstrstr(memory_stat, "file");
			if (ptr && (sscanf(ptr, "file %"PRIu64, &tmp) != 1))
				log_flag(CGROUP, "Cannot parse file field in memory.stat file");
			else
				stats->total_vmem += tmp;

			if (memory_swap_current) {
				if (sscanf(memory_swap_current,
					   "%"PRIu64, &tmp) != 1)
					log_flag(CGROUP, "Cannot parse file memory.swap.current file");
				else
					stats->total_vmem += tmp;
			}
		}

		/*
		 * Future: we can add more here or do a more fine-grain control
		 * with shmem or others depending on NoShare or UsePSS.
		 */

		ptr = xstrstr(memory_stat, "pgmajfault");
		if (ptr && (sscanf(ptr, "pgmajfault %"PRIu64,
				   &stats->total_pgmajfault) != 1))
			log_flag(CGROUP, "Cannot parse pgmajfault field in memory.stat file");
		xfree(memory_stat);
	}

	xfree(memory_swap_current);
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

extern bool cgroup_p_has_feature(cgroup_ctl_feature_t f)
{
	struct stat st;
	int rc;
	char *memsw_filepath = NULL;

	/* Check if swap constrain capability is enabled in this system. */
	switch (f) {
	case CG_MEMCG_SWAP:
		if (!bit_test(int_cg_ns.avail_controllers, CG_MEMORY))
			return false;
		xstrfmtcat(memsw_filepath, "%s/memory.swap.max",
			   int_cg[CG_LEVEL_ROOT].path);
		rc = stat(memsw_filepath, &st);
		xfree(memsw_filepath);
		return (rc == 0);
	default:
		break;
	}

	return false;
}
