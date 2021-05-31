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
const char plugin_name[] = "Cgroup v2 plugin";
const char plugin_type[] = "cgroup/v2";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static char *cg_root = NULL;

static bool avail_controllers[CG_CTL_CNT];
static bool enabled_controllers[CG_CTL_CNT];
const char *g_ctl_name[CG_CTL_CNT] = {
	"",
	"cpuset",
	"memory",
	"",
	"cpu"
};

/* FIXME, THIS IS IN common/xcgroup.c currently*/
/* FIXME, THIS IS IN common/xcgroup.c currently*/
/* FIXME, THIS IS IN common/xcgroup.c currently*/
/* FIXME, THIS IS IN common/xcgroup.c currently*/
size_t _file_getsize(int fd)
{
	int rc;
	size_t fsize;
	off_t offset;
	char c;

	/* store current position and rewind */
	offset = lseek(fd, 0, SEEK_CUR);
	if (offset < 0)
		return -1;
	if (lseek(fd, 0, SEEK_SET) < 0)
		error("%s: lseek(0): %m", __func__);

	/* get file size */
	fsize = 0;
	do {
		rc = read(fd, (void*)&c, 1);
		if (rc > 0)
			fsize++;
	} while ((rc < 0 && errno == EINTR) || rc > 0);

	/* restore position */
	if (lseek(fd, offset, SEEK_SET) < 0)
		error("%s: lseek(): %m", __func__);

	if (rc < 0)
		return -1;
	else
		return fsize;
}

static int _file_read_content(char* file_path, char** content, size_t *csize)
{
	int fstatus;
	int rc;
	int fd;
	size_t fsize;
	char* buf;

	fstatus = SLURM_ERROR;

	/* check input pointers */
	if (content == NULL || csize == NULL)
		return fstatus;

	/* open file for reading */
	fd = open(file_path, O_RDONLY, 0700);
	if (fd < 0) {
		debug2("%s: unable to open '%s' for reading : %m",
			__func__, file_path);
		return fstatus;
	}

	/* get file size */
	fsize=_file_getsize(fd);
	if (fsize == -1) {
		close(fd);
		return fstatus;
	}

	/* read file contents */
	buf = xmalloc(fsize + 1);
	buf[fsize]='\0';
	do {
		rc = read(fd, buf, fsize);
	} while (rc < 0 && errno == EINTR);

	/* set output values */
	if (rc >= 0) {
		*content = buf;
		*csize = rc;
		fstatus = SLURM_SUCCESS;
	} else {
		xfree(buf);
	}

	/* close file */
	close(fd);

	return fstatus;
}

int _file_write_content(char* file_path, const char* content, size_t csize)
{
	int fd;

	/* open file for writing */
	if ((fd = open(file_path, O_WRONLY, 0700)) < 0) {
		error("%s: unable to open '%s' for writing: %m",
			__func__, file_path);
		return SLURM_ERROR;
	}

	safe_write(fd, content, csize);

	/* close file */
	close(fd);
	return SLURM_SUCCESS;

rwfail:
	error("%s: unable to write %zu bytes to cgroup %s: %m",
	      __func__, csize, file_path);
	close(fd);
	return SLURM_ERROR;
}

int _xcgroup_set_param(char* cpath, char* param, const char* content)
{
	int fstatus = SLURM_ERROR;
	char file_path[PATH_MAX];

	if (!content) {
		debug2("%s: no content given, nothing to do.", __func__);
		return fstatus;
	}

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		debug2("unable to build filepath for '%s' and"
		       " parameter '%s' : %m", cpath, param);
		return fstatus;
	}

	fstatus = _file_write_content(file_path, content, strlen(content));
	if (fstatus != SLURM_SUCCESS)
		debug2("%s: unable to set parameter '%s' to '%s' for '%s'",
			__func__, param, content, cpath);
	else
		debug3("%s: parameter '%s' set to '%s' for '%s'",
			__func__, param, content, cpath);

	return fstatus;
}

int _xcgroup_instantiate(char *file_path, uid_t uid, gid_t gid)
{
	/* build cgroup */
	if (mkdir(file_path, 0755)) {
		if (errno != EEXIST) {
			error("%s: unable to create cgroup '%s' : %m",
			      __func__, file_path);
			return SLURM_ERROR;
		} else
			debug2("%s: cgroup '%s' already exists",
			       __func__, file_path);
	}

	/* change cgroup ownership as requested */
	if (chown(file_path, uid, gid))
		error("%s: unable to chown %d:%d cgroup '%s' : %m",
		      __func__, uid, gid, file_path);

	return SLURM_SUCCESS;
}

int _xcgroup_move_pid(char *path, pid_t pid)
{
	int rc;
	char *param = NULL;

	if (!path)
		return SLURM_ERROR;

	xstrfmtcat(param, "%d", pid);
	rc = _xcgroup_set_param(path, "cgroup.procs", (const char *)param);

	debug("Writting %s to %s/cgroup.procs", param, path);

	xfree(param);
	return rc;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

/*
 * The cgroup v2 documented way to know which is the process root in the cgroup
 * hierarchy, is just to read /proc/self/cgroup. In Unified hierarchies this
 * must contain only one line. If there are more lines this would mean we are
 * in Hybrid or in Legacy cgroup.
 */
static void _set_cg_root()
{
	char *buf, *start = NULL, *p;
	size_t sz;
	int fs;
	struct stat st;

	fs = _file_read_content("/proc/self/cgroup", &buf, &sz);
	if (fs != SLURM_SUCCESS)
		debug2("cannot read /proc/self/cgroup contents: %m");

	/*
	 * In Unified mode there will be just one line containing the path
	 * of the cgroup, so get it as our root and replace the \n:
	 * "0::/user.slice/user-1001.slice/session-1.scope\n"
	 */
	if ((p = xstrchr(buf, ':')) != NULL) {
		if ((p + 2) < (buf + sz - 1))
			start = p + 2;
	}

	if (start && *start != '\0') {
		if ((p = xstrchr(start, '\n')))
			*p = '\0';
		xstrfmtcat(cg_root, "/sys/fs/cgroup%s", start);
		if (stat(cg_root, &st) < 0) {
			error("cannot read cgroup path %s: %m", cg_root);
			xfree(cg_root);
		}
	}

	xfree(buf);
}

/*
 * For each available controller, enable it in this path. This operation is
 * only intended to be done in the Domain controllers, never in a leaf where
 * processes reside. Enabling the controllers will make their interfaces
 * available (e.g. the memory.*, cpu.*, cpuset.* ... files) to control the
 * cgroup.
 */
static int _enable_subtree_control(char *path)
{
	int i, rc = SLURM_SUCCESS;
	char *param = NULL;

	for (i = 0; i < CG_CTL_CNT; i++) {
		if (avail_controllers[i]) {
			xstrfmtcat(param,"+%s",g_ctl_name[i]);
			rc = _xcgroup_set_param(path, "cgroup.subtree_control",
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

	return rc;
}

/*
 * Read the cgroup.controllers file of the root to detect which are the
 * available controllers in this system.
 */
static int _set_controllers()
{
	char *buf, *ptr, *save_ptr, *ctl_filepath = NULL;
	size_t sz;
	int fs;

	xstrfmtcat(ctl_filepath, "%s/cgroup.controllers", cg_root);
	fs = _file_read_content(ctl_filepath, &buf, &sz);
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

	return SLURM_SUCCESS;
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
	char *slurmd_cgpath = NULL;

	/*
	 * Check our current root dir. Systemd MUST have Delegated it to us,
	 * so we want slurmd to be started by systemd
	 */
	_set_cg_root();
	if (cg_root == NULL)
		return SLURM_ERROR;

	/* Check available controllers in cgroup.controller and enable them. */
	if (_set_controllers() != SLURM_SUCCESS)
		error("Some controllers could not be enabled.");

	/*
	 * Before enabling the controllers in the parent, we need to move out
	 * all the processes which systemd started (slurmd) to a child.
	 */
	/* FIXME:
	 * This is ok if we are slurmd, but if we are slurmstepd we need to
	 * add more here because the move_pid with getpid will move stepd
	 * in slurmd.
	 */
	xstrfmtcat(slurmd_cgpath, "%s/slurmd/",cg_root);
	_xcgroup_instantiate(slurmd_cgpath, (uid_t) 0, (gid_t) 0);
	_xcgroup_move_pid(slurmd_cgpath, getpid());
	xfree(slurmd_cgpath);

	if (_enable_subtree_control(cg_root) != SLURM_SUCCESS) {
		error("Cannot enable subtree_control");
		return SLURM_ERROR;
	}

	/*
	 * We are ready now to start job steps, which will be created under
	 * cg_root/slurmd. Per each new step we'll need to first move it
	 * out of slurmd directory.
	 */

	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
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
 * this function will remain probably empty or just need to check if init is ok.
 */
extern int cgroup_p_system_create(cgroup_ctl_type_t sub)
{
	/*
	 * DEV_NOTES
	 * This directory refers to $cg_root/slurmd/
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_system_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids)
{
	/*
	 * DEV_NOTES
	 * Add the pid to $cg_root/slurmd/cgroup.procs
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_system_destroy(cgroup_ctl_type_t sub)
{
	/*
	 * DEV_NOTES
	 * Move our pid to / and remove $cg_root/slurmd
	 */
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

	   The slurmstepd process cannot be in a non-leaf directory, so we need
	   to think how to manage this hierarchy. The processes forked by stepd
	   will be created in the stepd cgroup, and we want to move them to
	   a specific dedicated directory. Probably we'd need the following:

	   // Hierarchy
	   mkdir($cg_root/uid_<x>);
	   _enable_subtree_controller($cg_root/uid_<x>);

	   mkdir($cg_root/uid_<x>/job_<y>);
	   _enable_subtree_controller($cg_root/uid_<x>/job_<y>);

	   mkdir($cg_root/uid_<x>/job_<y>/step_<z>);
	   _enable_subtree_controller($cg_root/uid_<x>/job_<y>/step_<z>);

	   // Leafs for slurm processes and for user processes
	   mkdir($cg_root/uid_<x>/job_<y>/step_<z>/slurm/);
	   mkdir($cg_root/uid_<x>/job_<y>/step_<z>/user/);

	   _xcgroup_move_pid($cg_root/uid_<x>/job_<y>/step_<z>/slurm/,
	                     job->jmgr_pid);
	*/
	return SLURM_SUCCESS;
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
	int rc = SLURM_SUCCESS;
	/* PSEUDO_CODE:
	   if (enabled_controllers[sub]) {
	      for p in *pids do:
	           _xcgroup_move_pid(step_path, p);
	   }
	*/
	return rc;
}

/*
 * Read the cgroup.procs of this step.
 */
extern int cgroup_p_step_get_pids(pid_t **pids, int *npids)
{
	/* DEV_NOTES/PSEUDO_CODE:
	   We may want to determine if there are any task_X directory and if so
	   read the processes inside them instead of reading the step ones.

	   if there are task_x directories, then:
	      for all task_x dir:
                  read task_x/cgroup.procs and put them into **pids
	   else:
               read step_x/cgroup.procs and put them into **pids
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_suspend()
{
	/* PSEUDO_CODE:
	//similar to xcgroup_set_param
	   echo 1 > $cg_root/uid_x/job_z/step_y/cgroup.freeze
	*/
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_resume()
{
	/* PSEUDO_CODE:
           //similar to xcgroup_set_param
	   echo 0 > $cg_root/uid_x/job_z/step_y/cgroup.freeze
	*/
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_destroy(cgroup_ctl_type_t sub)
{
	/* DEV_NOTES/PSEUDO_CODE
	 * - Iterate over the tree,

	 if there are any task_x in the step:
	   rmdir the task_x, log an error if cgroup.procs is not empty
	 else
	   rmdir the step_x, log an error if cgroup.procs is not empty

	 set all enabled_controllers[] to false.
	 */
	return SLURM_SUCCESS;
}

extern bool cgroup_p_has_pid(pid_t pid)
{
	/* PSEUDO_CODE:

	  //could reuse parts of xcgroup_ns_find_by_pid

	  if pid found in $cg_root/uid_x/step_y/cgroup.procs or in
	  step_y/task_z/cgroup.procs then
	        return true
	 */
	return false;
}

extern cgroup_limits_t *cgroup_p_root_constrain_get(cgroup_ctl_type_t sub)
{
	/* DEV_NOTES
	 * Same as cgroup v1 but changing paths, it is a
	 * xcgroup_get_param
	 */
	return NULL;
}

extern int cgroup_p_root_constrain_set(cgroup_ctl_type_t sub,
				       cgroup_limits_t *limits)
{
	/* DEV_NOTES
	 * memory.swappiness is not available in v2, do nothing
	 * for the rest, empty. Like in v1.
	 */
	return SLURM_SUCCESS;
}

extern cgroup_limits_t *cgroup_p_system_constrain_get(cgroup_ctl_type_t sub)
{
	/*
	 * DEV_NOTES
	 * For future usage, just return the requested constrains from the
	 * $cg_root/slurmd/
	 *
	 */
	return NULL;
}

extern int cgroup_p_system_constrain_set(cgroup_ctl_type_t sub,
					 cgroup_limits_t *limits)
{
	/*
	 * DEV_NOTES
	 * Set the requiret limits in $cg_root/slurmd/
	 *
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_user_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits)
{
	/* DEV_NOTES
	 * Same as cgroup v1 but changing paths, it is a
	 * xcgroup_get_param on cpuset.cpus and cpuset.mems only.
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_job_constrain_set(cgroup_ctl_type_t sub,
				      stepd_step_rec_t *job,
				      cgroup_limits_t *limits)
{
	/* DEV_NOTES
	 * Close to cgroup v1 but changing paths, and there's no kmem
	 * constrain support currently in v2. CG_DEVICES is not supported
	 * as a file interface, so here we need to interact with eBPF.
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits)
{
	/* DEV_NOTES
	 * Close to cgroup v1 but changing paths, and there's no kmem
	 * constrain support currently in v2. CG_DEVICES is not supported
	 * as a file interface, so here we need to interact with eBPF.
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_start_oom_mgr()
{
	/* DEV_NOTES
	 * Just return, no need to start anything
	 */
	return SLURM_SUCCESS;
}

extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *job)
{
	/* DEV_NOTES
	 * Read $cg_root/uid_x/step_y/cgroup.events->oom_kill
	 * and return, no need to stop anything
	 */
	return NULL;
}

extern int cgroup_p_accounting_init()
{
	/* DEV_NOTES
	 * add +cpu +mem to $cg_root/uid_x/step_y/cgroup.subtree_control if
	 * it is available.
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_accounting_fini()
{
	/* DEV_NOTES
	 * Destroy the step calling cgroup_p_step_destroy
	 * ... may need the task_acct_list ..
	 * just return
	 */
	return SLURM_SUCCESS;
}

extern int cgroup_p_task_addto_accounting(pid_t pid, stepd_step_rec_t *job,
					  uint32_t task_id)
{
	/* DEV_NOTES
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
