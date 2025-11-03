/*****************************************************************************\
 *  namespace_linux.c - Define namespace plugin for creating temporary linux
 *			namespaces for the job to provide some isolation between
 *			jobs on the same node.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
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
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <sys/wait.h>

#include "src/common/slurm_xlator.h"

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/common/stepd_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/proctrack.h"
#include "src/interfaces/switch.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "read_nsconf.h"

static int _create_ns(stepd_step_rec_t *step);
static int _delete_ns(uint32_t job_id);

#if defined(__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf = NULL;
#endif

const char plugin_name[] = "namespace linux plugin";
const char plugin_type[] = "namespace/linux";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static ns_conf_t *ns_conf = NULL;
static bool plugin_disabled = false;

/* NS_L_NS must be last */
enum ns_l_types {
	NS_L_PID = 0,
	NS_L_USER,
	NS_L_NS,
	NS_L_END
};

typedef struct {
	bool enabled;
	int fd;
	int flag;
	char *path;
	char *proc_name;
} ns_l_t;

static ns_l_t ns_l_enabled[NS_L_END] = { { false, -1, 0, NULL, NULL } };

static void _create_paths(uint32_t job_id, char **job_mount, char **ns_base,
			  char **src_bind)
{
	xassert(job_mount);

	xstrfmtcat(*job_mount, "%s/%u", ns_conf->basepath, job_id);

	if (ns_base) {
		xstrfmtcat(*ns_base, "%s/.ns", *job_mount);
		if (ns_conf->clonensflags & CLONE_NEWNS) {
			ns_l_enabled[NS_L_NS].enabled = true;
			ns_l_enabled[NS_L_NS].flag = CLONE_NEWNS;
			xfree(ns_l_enabled[NS_L_NS].path);
			xstrfmtcat(ns_l_enabled[NS_L_NS].path, "%s/mnt",
				   *ns_base);
			ns_l_enabled[NS_L_NS].proc_name = "mnt";
		}
		if (ns_conf->clonensflags & CLONE_NEWPID) {
			ns_l_enabled[NS_L_PID].enabled = true;
			ns_l_enabled[NS_L_NS].flag = CLONE_NEWPID;
			xfree(ns_l_enabled[NS_L_PID].path);
			xstrfmtcat(ns_l_enabled[NS_L_PID].path, "%s/pid",
				   *ns_base);
			ns_l_enabled[NS_L_PID].proc_name = "pid";
		}
		if (ns_conf->clonensflags & CLONE_NEWUSER) {
			ns_l_enabled[NS_L_USER].enabled = true;
			ns_l_enabled[NS_L_NS].flag = CLONE_NEWUSER;
			xfree(ns_l_enabled[NS_L_USER].path);
			xstrfmtcat(ns_l_enabled[NS_L_USER].path, "%s/user",
				   *ns_base);
			ns_l_enabled[NS_L_USER].proc_name = "user";
		}
	}

	if (src_bind)
		xstrfmtcat(*src_bind, "%s/.%u", *job_mount, job_id);
}

static int _find_step_in_list(step_loc_t *stepd, uint32_t *job_id)
{
	return (stepd->step_id.job_id == *job_id);
}

static bool _is_plugin_disabled(char *basepath)
{
	return ((!basepath) || (!xstrncasecmp(basepath, "none", 4)));
}

static int _restore_ns(list_t *steps, const char *d_name)
{
	char *endptr;
	int fd;
	unsigned long job_id;
	step_loc_t *stepd;

	errno = 0;
	job_id = strtoul(d_name, &endptr, 10);
	if ((errno != 0) || (job_id >= NO_VAL) || (*endptr != '\0')) {
		debug3("ignoring %s, could not convert to jobid.", d_name);
		return SLURM_SUCCESS;
	}

	/* here we think this is a job namespace */
	log_flag(NAMESPACE, "determine if job %lu is still running", job_id);
	stepd = list_find_first(steps, (ListFindF) _find_step_in_list, &job_id);
	if (!stepd) {
		debug("%s: Job %lu not found, deleting the namespace",
		      __func__, job_id);
		return _delete_ns(job_id);
	}

	fd = stepd_connect(stepd->directory, stepd->nodename, &stepd->step_id,
			   &stepd->protocol_version);
	if (fd == -1) {
		error("%s: failed to connect to stepd for %lu.",
		      __func__, job_id);
		return _delete_ns(job_id);
	}

	close(fd);

	return SLURM_SUCCESS;
}

extern int init(void)
{
	if (running_in_slurmd()) {
		/*
		 * Only init the config here for the slurmd. It will be sent by
		 * the slurmd to the slurmstepd at launch time.
		 */
		if (!(ns_conf = init_slurm_ns_conf())) {
			error("%s: Configuration not read correctly: Does '%s' not exist?",
			      plugin_type, ns_conf_file);
			return SLURM_ERROR;
		}
		plugin_disabled = _is_plugin_disabled(ns_conf->basepath);
		debug("namespace.conf read successfully");
	}

	debug("%s loaded", plugin_name);

	return SLURM_SUCCESS;
}

extern void fini(void)
{
#ifdef MEMORY_LEAK_DEBUG
	for (int i = 0; i < NS_L_END; i++) {
		xfree(ns_l_enabled[i].path);
		if (ns_l_enabled[i].fd >= 0)
			close(ns_l_enabled[i].fd);
	}
	free_ns_conf();
#endif
	debug("%s unloaded", plugin_name);
}

extern int namespace_p_restore(char *dir_name, bool recover)
{
	DIR *dp;
	struct dirent *ep;
	list_t *steps;
	int rc = SLURM_SUCCESS;

	if (plugin_disabled)
		return SLURM_SUCCESS;

	if (ns_conf->auto_basepath) {
		int fstatus;
		mode_t omask = umask(S_IWGRP | S_IWOTH);

		if (ns_conf->basepath[0] != '/') {
			debug("%s: unable to create ns directory '%s' : does not start with '/'",
			      __func__, ns_conf->basepath);
			umask(omask);
			return SLURM_ERROR;
		}

		if ((fstatus = mkdirpath(ns_conf->basepath, 0755, true))) {
			debug("%s: unable to create ns directory '%s' : %s",
			      __func__, ns_conf->basepath,
			      slurm_strerror(fstatus));
			umask(omask);
			return SLURM_ERROR;
		}

		umask(omask);
	}

	steps = stepd_available(conf->spooldir, conf->node_name);

	/*
	 * Iterate over basepath, restore only the folders that seem bounded to
	 * real jobs (have .ns file). NOTE: Restoring the state could be either
	 * deleting the folder if the job is died and resources are free, or
	 * mount it otherwise.
	 */
	if (!(dp = opendir(ns_conf->basepath))) {
		error("%s: Unable to open %s", __func__, ns_conf->basepath);
		return SLURM_ERROR;
	}

	while ((ep = readdir(dp))) {
		/* If possible, only check directories */
		if ((ep->d_type == DT_DIR) || (ep->d_type == DT_UNKNOWN)) {
			if (_restore_ns(steps, ep->d_name))
				rc = SLURM_ERROR;
		}
	}
	closedir(dp);
	FREE_NULL_LIST(steps);

	if (rc)
		error("Encountered an error while restoring job namespaces.");

	return rc;
}

static int _mount_private_dirs(char *path, uid_t uid)
{
	char *buffer = NULL, *mount_path = NULL, *save_ptr = NULL, *token;
	int rc = 0;

	if (!path) {
		error("%s: no path to private directories specified.",
		      __func__);
		return -1;
	}
	buffer = xstrdup(ns_conf->dirs);
	token = strtok_r(buffer, ",", &save_ptr);
	while (token) {
		/* skip /dev/shm, this is handled elsewhere */
		if (!xstrcmp(token, "/dev/shm")) {
			token = strtok_r(NULL, ",", &save_ptr);
			continue;
		}
		xstrfmtcat(mount_path, "%s/%s", path, token);
		for (char *t = mount_path + strlen(path) + 1; *t; t++) {
			if (*t == '/')
				*t = '_';
		}
		rc = mkdir(mount_path, 0700);
		if (rc && errno != EEXIST) {
			error("%s: Failed to create %s, %m",
			      __func__, mount_path);
			goto private_mounts_exit;
		}
		if (mount(mount_path, token, NULL, MS_BIND, NULL)) {
			error("%s: %s mount failed, %m", __func__, token);
			rc = -1;
			goto private_mounts_exit;
		}
		token = strtok_r(NULL, ",", &save_ptr);
		xfree(mount_path);
	}

private_mounts_exit:
	xfree(buffer);
	xfree(mount_path);
	return rc;
}

static int _chown_private_dirs(char *path, uid_t uid)
{
	char *buffer = NULL, *mount_path = NULL, *save_ptr = NULL, *token;
	int rc = 0;

	if (!path) {
		error("%s: no path to private directories specified.",
		      __func__);
		return -1;
	}
	buffer = xstrdup(ns_conf->dirs);
	token = strtok_r(buffer, ",", &save_ptr);
	while (token) {
		/* skip /dev/shm, this is handled elsewhere */
		if (!xstrcmp(token, "/dev/shm")) {
			token = strtok_r(NULL, ",", &save_ptr);
			continue;
		}
		xstrfmtcat(mount_path, "%s/%s", path, token);
		for (char *t = mount_path + strlen(path) + 1; *t; t++) {
			if (*t == '/')
				*t = '_';
		}
		rc = lchown(mount_path, uid, -1);
		if (rc) {
			error("%s: lchown failed for %s: %m",
			      __func__, mount_path);
			goto private_mounts_exit;
		}
		token = strtok_r(NULL, ",", &save_ptr);
		xfree(mount_path);
	}

private_mounts_exit:
	xfree(buffer);
	xfree(mount_path);
	return rc;
}

static int _mount_private_shm(void)
{
	char *loc = NULL;
	int rc = 0;

	/* return early if "/dev/shm" is not in the mount list */
	if (!(loc = xstrcasestr(ns_conf->dirs, "/dev/shm")))
		return rc;
	if (!((loc[8] == ',') || (loc[8] == 0)))
		return rc;

	/* handle mounting a new /dev/shm */
	if (!ns_conf->shared) {
		/*
		 * only unmount old /dev/shm if private, otherwise this can
		 * impact the root namespace
		 */
		rc = umount("/dev/shm");
		if (rc && errno != EINVAL) {
			error("%s: umount /dev/shm failed: %m", __func__);
			return rc;
		}
	}
	rc = mount("tmpfs", "/dev/shm", "tmpfs", 0, NULL);
	if (rc) {
		error("%s: /dev/shm mount failed: %m", __func__);
		return -1;
	}
	return rc;
}

static int _mount_private_proc(void)
{
	if (!ns_l_enabled[NS_L_PID].enabled)
		return SLURM_SUCCESS;

	if (mount("proc", "/proc", "proc", 0, NULL)) {
		error("%s: /proc mount failed: %m", __func__);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static char **_setup_script_env(uint32_t job_id, stepd_step_rec_t *step,
				char *src_bind, char *ns_base)
{
	char **env = env_array_create();

	env_array_overwrite_fmt(&env, "SLURM_JOB_ID", "%u", job_id);
	env_array_overwrite_fmt(&env, "SLURM_CONF", "%s", conf->conffile);
	env_array_overwrite_fmt(&env, "SLURMD_NODENAME", "%s", conf->node_name);
	if (src_bind)
		env_array_overwrite_fmt(&env, "SLURM_JOB_MOUNTPOINT_SRC", "%s",
					src_bind);
	if (step) {
		if (step->het_job_id && (step->het_job_id != NO_VAL))
			env_array_overwrite_fmt(&env, "SLURM_HET_JOB_ID", "%u",
						step->het_job_id);
		env_array_overwrite_fmt(&env, "SLURM_JOB_GID", "%u", step->gid);
		env_array_overwrite_fmt(&env, "SLURM_JOB_UID", "%u", step->uid);
		env_array_overwrite_fmt(&env, "SLURM_JOB_USER", "%s",
					step->user_name);
		if (step->alias_list)
			env_array_overwrite_fmt(&env, "SLURM_NODE_ALIASES",
						"%s", step->alias_list);
		if (step->cwd)
			env_array_overwrite_fmt(&env, "SLURM_JOB_WORK_DIR",
						"%s", step->cwd);
	}

	if (ns_base)
		env_array_overwrite_fmt(&env, "SLURM_NS", "%s", ns_base);

	return env;
}

static pid_t sys_clone(unsigned long flags, int *parent_tid, int *child_tid,
		       unsigned long tls)
{
#ifdef __x86_64__
	return syscall(__NR_clone, flags, NULL, parent_tid, child_tid, tls);
#else
	return syscall(__NR_clone, flags, NULL, parent_tid, tls, child_tid);
#endif
}

static void _create_ns_child(stepd_step_rec_t *step, char *src_bind,
			     char *job_mount, sem_t *sem1, sem_t *sem2)
{
	char *argv[4] = { (char *) conf->stepd_loc, "ns_infinity", NULL, NULL };
	int rc = 0;

	if (sem_wait(sem1) < 0) {
		error("%s: sem_wait failed %m", __func__);
		rc = -1;
		goto child_exit;
	}
	if (!ns_conf->shared) {
		/* Set root filesystem to private */
		if (mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL)) {
			error("%s: Failed to make root private: %m",
			      __func__);
			rc = -1;
			goto child_exit;
		}
	} else {
		/* Set root filesystem to shared */
		if (mount(NULL, "/", NULL, MS_SHARED | MS_REC, NULL)) {
			error("%s: Failed to make root shared: %m",
			      __func__);
			rc = -1;
			goto child_exit;
		}
		/* Set root filesystem to slave */
		if (mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL)) {
			error("%s: Failed to make root slave: %m",
			      __func__);
			rc = -1;
			goto child_exit;
		}
	}

	if (_mount_private_proc() == SLURM_ERROR) {
		rc = -1;
		goto child_exit;
	}

	/*
	 * Now we have a persistent mount namespace.
	 * Mount private directories inside the namespace.
	 */
	if (_mount_private_dirs(src_bind, step->uid) == -1) {
		rc = -1;
		goto child_exit;
	}

	/*
	 * switch/nvidia_imex needs to create an ephemeral device
	 * node under /dev in this new namespace.
	 */
	if ((rc = switch_g_fs_init(step))) {
		error("%s: switch_g_fs_init failed", __func__);
		rc = -1;
		goto child_exit;
	}

	if ((rc = _mount_private_shm())) {
		error("%s: could not mount private shm", __func__);
		goto child_exit;
	}

	if (sem_post(sem2) < 0) {
		error("%s: sem_post failed: %m", __func__);
		goto child_exit;
	}

	sem_destroy(sem1);
	munmap(sem1, sizeof(*sem1));
	sem_destroy(sem2);
	munmap(sem2, sizeof(*sem2));

	/* become an infinity process */
	xstrfmtcat(argv[2], "%u", step->step_id.job_id);

	execvp(argv[0], argv);
	error("execvp of slurmstepd infinity failed: %m");
	_exit(127);

child_exit:
	/* Do a final post to prevent from waiting on errors */
	sem_post(sem2);
	sem_destroy(sem1);
	munmap(sem1, sizeof(*sem1));
	sem_destroy(sem2);
	munmap(sem2, sizeof(*sem2));

	exit(rc);
}

static int _clonens_user_setup(stepd_step_rec_t *step, pid_t pid)
{
	int fd = 0, rc = SLURM_SUCCESS;
	char *tmpstr = NULL;

	if (!ns_l_enabled[NS_L_USER].enabled)
		return rc;

	/* If the script is specified, it takes precidendce */
	if (ns_conf->usernsscript) {
		char *result = NULL;
		run_command_args_t run_command_args = {
			.max_wait = 10 * MSEC_IN_SEC,
			.script_path = ns_conf->usernsscript,
			.script_type = "UserNSScript",
			.status = &rc,
		};
		run_command_args.env = _setup_script_env(step->step_id.job_id,
							 step, NULL, NULL);
		env_array_overwrite_fmt(&run_command_args.env, "SLURM_NS_PID",
					"%u", pid);

		log_flag(NAMESPACE, "Running UserNSScript");
		result = run_command(&run_command_args);
		log_flag(NAMESPACE, "UserNSScript rc: %d, stdout: %s",
			 rc, result);
		env_array_free(run_command_args.env);
		xfree(result);

		if (rc)
			error("%s: UserNSScript: %s failed with rc: %d",
			      __func__, ns_conf->usernsscript, rc);
		goto end_it;
	}

	xstrfmtcat(tmpstr, "/proc/%d/uid_map", pid);
	if (!(-1 != (fd = open(tmpstr, O_WRONLY)))) {
		error("%s: open uid_map %s failed: %m", __func__, tmpstr);
		rc = SLURM_ERROR;
		goto end_it;
	}
	if (!(1 <= dprintf(fd, "0 0 4294967295\n"))) {
		error("%s: write 0 0 4294967295 uid_map %s failed: %m",
		      __func__, tmpstr);
		rc = SLURM_ERROR;
		goto end_it;
	}
	if (fd >= 0)
		close(fd);
	xfree(tmpstr);

	xstrfmtcat(tmpstr, "/proc/%d/gid_map", pid);
	if (!(-1 != (fd = open(tmpstr, O_WRONLY)))) {
		error("%s: open gid_map failed: %m", __func__);
		rc = SLURM_ERROR;
		goto end_it;
	}
	if (!(1 <= dprintf(fd, "0 0 4294967295\n"))) {
		error("%s: write 0 0 4294967295 failed: %m",
		      __func__ );
		rc = SLURM_ERROR;
		goto end_it;
	}

end_it:
	if (fd >= 0)
		close(fd);
	xfree(tmpstr);
	return rc;
}

static int _create_ns(stepd_step_rec_t *step)
{
	int child_tid = 0, parent_tid = 0;
	char *job_mount = NULL, *ns_base = NULL, *src_bind = NULL;
	char *result = NULL;
	int fd;
	int rc = 0;
	unsigned long tls = 0;
	sem_t *sem1 = NULL;
	sem_t *sem2 = NULL;
	pid_t cpid;

	_create_paths(step->step_id.job_id, &job_mount, &ns_base, &src_bind);

	if (mkdir(job_mount, 0700)) {
		error("%s: mkdir %s failed: %m", __func__, job_mount);
		rc = SLURM_ERROR;
		goto end_it;
	}

	/*
	 * MS_BIND mountflag would make mount() ignore all other mountflags
	 * except MS_REC. We need MS_PRIVATE mountflag as well to make the
	 * mount (as well as all mounts inside it) private, which needs to be
	 * done by calling mount() a second time with MS_PRIVATE and MS_REC
	 * flags.
	 */
	if (mount(job_mount, job_mount, NULL, MS_BIND, NULL)) {
		error("%s: Initial base mount failed: %m", __func__);
		rc = SLURM_ERROR;
		goto end_it;
	}
	if (mount(job_mount, job_mount, NULL, MS_PRIVATE | MS_REC, NULL)) {
		error("%s: Initial base mount failed: %m", __func__);
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (mkdir(ns_base, 0700)) {
		error("%s: mkdir %s failed: %m", __func__, ns_base);
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* Create locations for all enabled namespaces */
	for (int i = 0; i < NS_L_END; i++) {
		if (!ns_l_enabled[i].enabled)
			continue;
		fd = open(ns_l_enabled[i].path, O_CREAT | O_RDWR, S_IRWXU);
		if (fd == -1) {
			error("%s: open failed %s: %m",
			      __func__, ns_l_enabled[i].path);
			rc = -1;
			goto exit2;
		}
		close(fd);
	}

	/* run any initialization script- if any*/
	if (ns_conf->initscript) {
		run_command_args_t run_command_args = {
			.max_wait = 10 * MSEC_IN_SEC,
			.script_path = ns_conf->initscript,
			.script_type = "initscript",
			.status = &rc,
		};
		run_command_args.env = _setup_script_env(step->step_id.job_id,
							 step, src_bind, NULL);

		log_flag(NAMESPACE, "Running InitScript");
		result = run_command(&run_command_args);
		log_flag(NAMESPACE, "InitScript rc: %d, stdout: %s", rc, result);
		env_array_free(run_command_args.env);
		xfree(result);

		if (rc) {
			error("%s: InitScript: %s failed with rc: %d",
			      __func__, ns_conf->initscript, rc);
			goto exit2;
		}
	}

	rc = mkdir(src_bind, 0700);
	if (rc && (errno != EEXIST)) {
		error("%s: mkdir failed %s, %m", __func__, src_bind);
		goto exit2;
	}

	if (chown(src_bind, step->uid, -1)) {
		error("%s: chown failed for %s: %m",
		      __func__, src_bind);
		rc = -1;
		goto exit2;
	}

	sem1 = mmap(NULL, sizeof(*sem1), PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (sem1 == MAP_FAILED) {
		error("%s: mmap failed: %m", __func__);
		rc = -1;
		goto exit2;
	}

	sem2 = mmap(NULL, sizeof(*sem2), PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (sem2 == MAP_FAILED) {
		error("%s: mmap failed: %m", __func__);
		sem_destroy(sem1);
		munmap(sem1, sizeof(*sem1));
		rc = -1;
		goto exit2;
	}

	rc = sem_init(sem1, 1, 0);
	if (rc) {
		error("%s: sem_init: %m", __func__);
		goto exit1;
	}
	rc = sem_init(sem2, 1, 0);
	if (rc) {
		error("%s: sem_init: %m", __func__);
		goto exit1;
	}
	cpid = sys_clone(ns_conf->clonensflags|SIGCHLD, &parent_tid,
			 &child_tid, tls);

	if (cpid == -1) {
		error("%s: sys_clone failed: %m", __func__);
		rc = -1;
		goto exit1;
	} else if (cpid == 0) {
		_create_ns_child(step, src_bind, job_mount, sem1, sem2);
	} else {
		char *proc_path = NULL;

		/*
		 * Bind mount /proc/pid/ns/loc to hold namespace active
		 * without a process attached to it
		 */
		for (int i = 0; i < NS_L_END; i++) {
			if (!ns_l_enabled[i].enabled)
				continue;
			xstrfmtcat(proc_path, "/proc/%u/ns/%s", cpid,
				   ns_l_enabled[i].proc_name);
			rc = mount(proc_path, ns_l_enabled[i].path, NULL,
				   MS_BIND, NULL);
			if (rc) {
				error("%s: ns %s mount failed: %m",
				      __func__, ns_l_enabled[i].proc_name);
				if (sem_post(sem1) < 0)
					error("%s: Could not release semaphore: %m",
					      __func__);
				xfree(proc_path);
				goto exit1;
			}
			xfree(proc_path);
		}

		/* setup users before setting up the rest of the container */
		if ((rc = _clonens_user_setup(step, cpid))) {
			error("%s: Unable to prepare user namespace.",
			      __func__);
			/* error needs to fall though here */
		}

		/* Setup remainder of the container */
		if (sem_post(sem1) < 0) {
			error("%s: sem_post failed: %m", __func__);
			goto exit1;
		}

		/* Wait for container to be setup */
		if (sem_wait(sem2) < 0) {
			error("%s: sem_Wait failed: %m", __func__);
			rc = -1;
			goto exit1;
		}

		if (proctrack_g_add(step, cpid) != SLURM_SUCCESS) {
			error("%s: Job %u can't add pid %d to proctrack plugin in the extern_step.",
			      __func__, step->step_id.job_id, cpid);
			rc = SLURM_ERROR;
			goto exit1;
		}

		if (_chown_private_dirs(src_bind, step->uid) == -1) {
			rc = -1;
			goto exit1;
		}

		/* Any error that remains here should skip further setup */
		if (rc)
			goto exit1;
	}

	/* run any post clone initialization script */
	if (ns_conf->clonensscript) {
		run_command_args_t run_command_args = {
			.max_wait = ns_conf->clonensscript_wait * MSEC_IN_SEC,
			.script_path = ns_conf->clonensscript,
			.script_type = "clonensscript",
			.status = &rc,
		};
		run_command_args.env =
			_setup_script_env(step->step_id.job_id, step, src_bind,
					  ns_l_enabled[NS_L_NS].path);

		log_flag(NAMESPACE, "Running CloneNSScript");
		result = run_command(&run_command_args);
		log_flag(NAMESPACE, "CloneNSScript rc: %d, stdout: %s",
			 rc, result);
		xfree(result);
		env_array_free(run_command_args.env);

		if (rc) {
			error("%s: CloneNSScript %s failed with rc=%d",
			      __func__, ns_conf->clonensscript, rc);
			goto exit2;
		}
	}

exit1:
	sem_destroy(sem1);
	munmap(sem1, sizeof(*sem1));
	sem_destroy(sem2);
	munmap(sem2, sizeof(*sem2));

exit2:
	if (rc) {
		int failures;
		/* cleanup the job mount */
		if ((failures = rmdir_recursive(job_mount, false))) {
			error("%s: failed to remove %d files from %s",
			      __func__, failures, job_mount);
			rc = SLURM_ERROR;
			goto end_it;
		}
		if (umount2(job_mount, MNT_DETACH))
			error("%s: umount2 %s failed: %m",
			      __func__, job_mount);
		if (rmdir(job_mount))
			error("rmdir %s failed: %m", job_mount);
	}

end_it:
	xfree(job_mount);
	xfree(src_bind);
	xfree(ns_base);

	return rc;
}

extern int namespace_p_join_external(slurm_step_id_t *step_id, list_t *ns_map)
{
	char *job_mount = NULL, *ns_base = NULL;
	ns_fd_map_t *tmp_map = NULL;

	xassert(ns_map);

	if (plugin_disabled)
		return 0;

	_create_paths(step_id->job_id, &job_mount, &ns_base, NULL);

	for (int i = 0; i < NS_L_END; i++) {
		if (!ns_l_enabled[i].enabled)
			continue;

		if (!ns_l_enabled[i].fd) {
			ns_l_enabled[i].fd =
				open(ns_l_enabled[i].path, O_RDONLY);
			if (ns_l_enabled[i].fd == -1) {
				error("%s: %m", __func__);
				goto end_it;
			}
		}
		tmp_map = xmalloc(sizeof(*tmp_map));
		tmp_map->type = ns_l_enabled[i].flag;
		tmp_map->fd = ns_l_enabled[i].fd;
		list_append(ns_map, tmp_map);
		tmp_map = NULL;
	}

end_it:

	xfree(job_mount);
	xfree(ns_base);

	return list_count(ns_map);
}

extern int namespace_p_join(slurm_step_id_t *step_id, uid_t uid,
			    bool step_create)
{
	char *job_mount = NULL, *ns_base = NULL;
	int rc = SLURM_SUCCESS;

	if (plugin_disabled)
		return SLURM_SUCCESS;

	/* Formerly EntireStepInNS handling, this is now the normal process */
	if ((running_in_slurmstepd() && step_id->step_id != SLURM_EXTERN_CONT))
		return SLURM_SUCCESS;

	/*
	 * Jobid 0 means we are not a real job, but a script running instead we
	 * do not need to handle this request.
	 */
	if (step_id->job_id == 0)
		return SLURM_SUCCESS;

	_create_paths(step_id->job_id, &job_mount, &ns_base, NULL);

	/* Open all namespaces first, however we cannot assume this is shared */
	for (int i = 0; i < NS_L_END; i++) {
		if (!ns_l_enabled[i].enabled)
			continue;
		/* This is called on the slurmd so we can't use ns_fd. */
		ns_l_enabled[i].fd = open(ns_l_enabled[i].path, O_RDONLY);
		if (ns_l_enabled[i].fd == -1) {
			error("%s: open failed for %s: %m",
			      __func__, ns_l_enabled[i].path);
			xfree(job_mount);
			xfree(ns_base);
			return SLURM_ERROR;
		}
	}
	for (int i = 0; i < NS_L_END; i++) {
		if (!ns_l_enabled[i].enabled)
			continue;
		rc = setns(ns_l_enabled[i].fd, 0);
		close(ns_l_enabled[i].fd);
		ns_l_enabled[i].fd = -1;
		if (rc) {
			error("%s: setns failed for %s: %m",
			      __func__, ns_l_enabled[i].path);
			/* closed after error() */
			xfree(job_mount);
			xfree(ns_base);
			return SLURM_ERROR;
		}
		log_flag(NAMESPACE, "%ps entered %s namespace", step_id,
			 ns_l_enabled[i].path);
	}

	log_flag(NAMESPACE, "%ps entered namespace", step_id);

	xfree(job_mount);
	xfree(ns_base);

	return SLURM_SUCCESS;
}

static int _delete_ns(uint32_t job_id)
{
	char *job_mount = NULL, *ns_base = NULL;
	int rc = 0, failures = 0;
	char *result = NULL;

	_create_paths(job_id, &job_mount, &ns_base, NULL);

	/* run any post clone epilog script */
	/* initialize environ variable to include jobid and namespace file */
	if (ns_conf->clonensepilog) {
		run_command_args_t run_command_args = {
			.max_wait = ns_conf->clonensepilog_wait * MSEC_IN_SEC,
			.script_path = ns_conf->clonensepilog,
			.script_type = "clonensepilog",
			.status = &rc,
		};
		run_command_args.env =
			_setup_script_env(job_id, NULL, NULL, ns_base);
		log_flag(NAMESPACE, "Running CloneNSEpilog");
		result = run_command(&run_command_args);
		env_array_free(run_command_args.env);
		log_flag(NAMESPACE, "CloneNSEpilog rc: %d, stdout: %s",
			 rc, result);
		xfree(result);

		if (rc) {
			error("%s: CloneNSEpilog script %s failed with rc=%d",
			      __func__, ns_conf->clonensepilog, rc);
		}
	}

	errno = 0;

	/*
	 * umount2() sets errno to EINVAL if the target is not a mount point
	 * but also if called with invalid flags.  Consider this if changing the
	 * flags to umount2().
	 */

	for (int i = 0; i < NS_L_END; i++) {
		if (!ns_l_enabled[i].enabled)
			continue;
		rc = umount2(ns_l_enabled[i].path, MNT_DETACH);
		if (rc) {
			if ((errno == EINVAL) || (errno == ENOENT)) {
				log_flag(NAMESPACE, "%s: umount2 %s failed: %m",
					 __func__, ns_l_enabled[i].path);
			} else {
				error("%s: umount2 %s failed: %m",
				      __func__, ns_l_enabled[i].path);
				failures = 1;
			}
		}
	}
	/* If any of the unmounts failed above, bail out here */
	if (failures) {
		xfree(job_mount);
		xfree(ns_base);
		return SLURM_ERROR;
	}

	if ((failures = rmdir_recursive(job_mount, false)))
		error("%s: failed to remove %d files from %s",
		      __func__, failures, job_mount);
	if (umount2(job_mount, MNT_DETACH))
		log_flag(NAMESPACE, "umount2: %s failed: %m", job_mount);
	if (rmdir(job_mount))
		error("rmdir %s failed: %m", job_mount);

	xfree(job_mount);
	xfree(ns_base);

	return SLURM_SUCCESS;
}

extern int namespace_p_stepd_create(stepd_step_rec_t *step)
{
	if (plugin_disabled)
		return SLURM_SUCCESS;

	return _create_ns(step);
}

extern int namespace_p_stepd_delete(slurm_step_id_t *step_id)
{
	if (plugin_disabled)
		return SLURM_SUCCESS;

	return _delete_ns(step_id->job_id);
}

extern int namespace_p_send_stepd(int fd)
{
	int len;
	buf_t *buf;

	buf = get_slurm_ns_conf_buf();

	/* The config should have been inited by now */
	xassert(buf);

	len = get_buf_offset(buf);
	safe_write(fd, &len, sizeof(len));
	safe_write(fd, get_buf_data(buf), len);

	return SLURM_SUCCESS;
rwfail:
	error("%s: failed", __func__);
	return SLURM_ERROR;
}

extern int namespace_p_recv_stepd(int fd)
{
	int len;
	buf_t *buf;

	safe_read(fd, &len, sizeof(len));

	buf = init_buf(len);
	safe_read(fd, buf->head, len);

	if (!(ns_conf = set_slurm_ns_conf(buf)))
		goto rwfail;

	plugin_disabled = _is_plugin_disabled(ns_conf->basepath);

	return SLURM_SUCCESS;
rwfail:
	error("%s: failed", __func__);
	return SLURM_ERROR;
}

extern bool namespace_p_can_bpf(stepd_step_rec_t *step)
{
	if (plugin_disabled)
		return true;

	/*
	 * Only special parts of the extern step are run in the namespace.
	 * The calls ebpf in the extern step are not in the namespace.
	 */
	if (step->step_id.step_id == SLURM_EXTERN_CONT)
		return true;

	/*
	 * bpf programs cannot be directly loaded from inside the user namespace
	 * unless a token is created.
	 */
	if (ns_conf->clonensflags & CLONE_NEWUSER)
		return false;

	return true;
}

extern int namespace_p_setup_bpf_token(stepd_step_rec_t *step)
{
	int rc = SLURM_ERROR;
	int fd = -1;
	int token_fd = SLURM_ERROR;
	uint16_t prot_ver;
	slurm_step_id_t con = step->step_id;

	/*
	 * This indicates that either this is a extern step or that the plugin
	 * is not configured to use user namespaces. In both cases we do not
	 * need to get a bpf token. Also if we already have one do not setup
	 * another.
	 */
	if (namespace_p_can_bpf(step) || cgroup_g_bpf_get_token() != -1)
		return SLURM_SUCCESS;

#ifndef HAVE_BPF_TOKENS
	error("Slurm is not compiled with BPF token support");
	return SLURM_ERROR;
#endif

	con.step_id = SLURM_EXTERN_CONT;
	con.step_het_comp = NO_VAL;

	if ((fd = stepd_connect(conf->spooldir, conf->node_name, &con,
				&prot_ver)) == -1) {
		error("%s: Connect to %ps external failed: %m",
		      __func__, &con.job_id);
		goto end;
	}

	token_fd = stepd_get_bpf_token(fd, prot_ver);
	if (token_fd != SLURM_ERROR) {
		cgroup_g_bpf_set_token(token_fd);
		rc = SLURM_SUCCESS;
	}
end:
	if (fd >= 0)
		close(fd);
	return rc;
}
