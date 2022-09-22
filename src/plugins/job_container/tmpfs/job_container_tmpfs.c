/*****************************************************************************\
 *  job_container_tmpfs.c - Define job container plugin for creating a
 *			    temporary mount namespace for the job, to provide
 *			    quota based access to node local memory.
 *****************************************************************************
 *  Copyright (C) 2019-2021 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory
 *  Written by Aditi Gaur <agaur@lbl.gov>
 *  All rights reserved.
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
#define _XOPEN_SOURCE 500 /* For ftw.h */
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sched.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/mount.h>
#include <linux/limits.h>
#include <semaphore.h>

#include "src/common/slurm_xlator.h"

#include "src/common/env.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/common/stepd_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "read_jcconf.h"

static int _create_ns(uint32_t job_id, uid_t uid, bool remount);
static int _delete_ns(uint32_t job_id, bool is_slurmd);

#if defined (__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf = NULL;
#endif

const char plugin_name[]        = "job_container tmpfs plugin";
const char plugin_type[]        = "job_container/tmpfs";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static slurm_jc_conf_t *jc_conf = NULL;
static int step_ns_fd = -1;
static bool force_rm = true;
List legacy_jobs;

typedef struct {
	uint32_t job_id;
	uint16_t protocol_version;
} legacy_job_info_t;

static int _create_paths(uint32_t job_id,
			 char *job_mount,
			 char *ns_holder,
			 char *src_bind)
{
	jc_conf = get_slurm_jc_conf();

	if (!jc_conf) {
		error("%s: Configuration not read correctly: did %s not exist?",
		      __func__, tmpfs_conf_file);
		return SLURM_ERROR;
	}

	xassert(job_mount);

	if (snprintf(job_mount, PATH_MAX, "%s/%u", jc_conf->basepath, job_id)
	    >= PATH_MAX) {
		error("%s: Unable to build job %u mount path: %m",
			__func__, job_id);
		return SLURM_ERROR;
	}

	if (ns_holder) {
		if (snprintf(ns_holder, PATH_MAX, "%s/.ns", job_mount)
		    >= PATH_MAX) {
			error("%s: Unable to build job %u ns_holder path: %m",
			      __func__, job_id);
			return SLURM_ERROR;
		}
	}

	if (src_bind) {
		if (snprintf(src_bind, PATH_MAX, "%s/.%u", job_mount, job_id)
		    >= PATH_MAX) {
			error("%s: Unable to build job %u src_bind path: %m",
			__func__, job_id);
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
}

static int _find_step_in_list(step_loc_t *stepd, uint32_t *job_id)
{
	return (stepd->step_id.job_id == *job_id);
}

static int _find_legacy_job_in_list(legacy_job_info_t *job, uint32_t *job_id)
{
	return (job->job_id == *job_id);
}

static int _restore_ns(List steps, const char *d_name)
{
	int fd;
	uint32_t job_id;
	step_loc_t *stepd;

	if (!(job_id = slurm_atoul(d_name))) {
		debug3("ignoring %s, could not convert to jobid.", d_name);
		return SLURM_SUCCESS;
	}

	/* here we think this is a job container */
	debug3("determine if job %u is still running", job_id);
	stepd = list_find_first(steps, (ListFindF)_find_step_in_list, &job_id);
	if (!stepd) {
		debug("%s: Job %u not found, deleting the namespace",
		      __func__, job_id);
		return _delete_ns(job_id, false);
	}

	fd = stepd_connect(stepd->directory, stepd->nodename,
			   &stepd->step_id, &stepd->protocol_version);
	if (fd == -1) {
		error("%s: failed to connect to stepd for %u.",
		      __func__, job_id);
		return _delete_ns(job_id, false);
	}

	close(fd);

	if (stepd->protocol_version == SLURM_20_11_PROTOCOL_VERSION) {
		legacy_job_info_t *job = xmalloc(sizeof(*job));
		if (!legacy_jobs)
			legacy_jobs = list_create(NULL);
		job->job_id = job_id;
		job->protocol_version = stepd->protocol_version;
		list_append(legacy_jobs, job);
		return _create_ns(job_id, 0, true);
	}

	return SLURM_SUCCESS;
}

extern void container_p_reconfig(void)
{
	return;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
#if defined(__APPLE__) || defined(__FreeBSD__)
	fatal("%s is not available on this system. (mount bind limitation)",
	      plugin_name);
#endif

	if (!get_slurm_jc_conf()) {
		error("%s: Configuration not read correctly: Does '%s' not exist?",
		      plugin_type, tmpfs_conf_file);
		return SLURM_ERROR;
	}

	debug("%s loaded", plugin_name);

	return SLURM_SUCCESS;
}

static int _legacy_fini(void *x, void *arg) {
	char job_mount[PATH_MAX];
	legacy_job_info_t *job = (legacy_job_info_t *)x;

	if (_create_paths(job->job_id, job_mount, NULL, NULL)
	    != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (umount2(job_mount, MNT_DETACH))
		debug2("umount2: %s failed: %s", job_mount, strerror(errno));

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	int rc = SLURM_SUCCESS;
	debug("%s unloaded", plugin_name);

#ifdef HAVE_NATIVE_CRAY
	return SLURM_SUCCESS;
#endif

	if (step_ns_fd != -1) {
		close(step_ns_fd);
		step_ns_fd = -1;
	}

	if (!legacy_jobs)
		return SLURM_SUCCESS;

	rc = list_for_each(legacy_jobs, _legacy_fini, NULL);

	FREE_NULL_LIST(legacy_jobs);

	return rc;
}

extern int container_p_restore(char *dir_name, bool recover)
{
	DIR *dp;
	struct dirent *ep;
	List steps;
	int rc = SLURM_SUCCESS;

#ifdef HAVE_NATIVE_CRAY
	return SLURM_SUCCESS;
#endif

	jc_conf = get_slurm_jc_conf();
	if (!jc_conf) {
		error("%s: Configuration not loaded", __func__);
		return SLURM_ERROR;
	}

	debug("job_container.conf read successfully");

	if (jc_conf->auto_basepath) {
		int fstatus;
		char *mnt_point, *p;
		mode_t omask;

		omask = umask(S_IWGRP | S_IWOTH);

		fstatus = mkdir(jc_conf->basepath, 0755);
		if (fstatus && errno != EEXIST) {
			if (jc_conf->basepath[0] != '/') {
				debug("unable to create ns directory '%s' : does not start with '/'",
				      jc_conf->basepath);
				umask(omask);
				return SLURM_ERROR;
			}
			mnt_point = xstrdup(jc_conf->basepath);
			p = mnt_point;
			while ((p = xstrchr(p+1, '/')) != NULL) {
				*p = '\0';
				fstatus = mkdir(mnt_point, 0755);
				if (fstatus && errno != EEXIST) {
					debug("unable to create ns required directory '%s'",
					      mnt_point);
					xfree(mnt_point);
					umask(omask);
					return SLURM_ERROR;
				}
				*p='/';
			}
			xfree(mnt_point);
			fstatus = mkdir(jc_conf->basepath, 0755);
		}

		if (fstatus && errno != EEXIST) {
			debug("unable to create ns directory '%s' : %m",
			      jc_conf->basepath);
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
	if (!(dp = opendir(jc_conf->basepath))) {
		error("%s: Unable to open %s", __func__, jc_conf->basepath);
		return SLURM_ERROR;
	}

	while ((ep = readdir(dp))) {
		if (_restore_ns(steps, ep->d_name))
			rc = SLURM_ERROR;
	}
	closedir(dp);
	FREE_NULL_LIST(steps);

	if (rc)
		error("Encountered an error while restoring job containers.");

	return rc;
}

static int _mount_private_tmp(char *path)
{
	if (!path) {
		error("%s: cannot mount /tmp", __func__);
		return -1;
	}
#if !defined(__APPLE__) && !defined(__FreeBSD__)
	if (mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL)) {
		error("%s: making root private: failed: %s",
		      __func__, strerror(errno));
		return -1;
	}
	if (mount(path, "/tmp", NULL, MS_BIND|MS_REC, NULL)) {
		error("%s: /tmp mount failed, %s",
		      __func__, strerror(errno));
		return -1;
	}
#endif
	return 0;
}

static int _mount_private_shm(void)
{
	int rc = 0;

	rc = umount("/dev/shm");
	if (rc && errno != EINVAL) {
		error("%s: umount /dev/shm failed: %s\n",
		      __func__, strerror(errno));
		return rc;
	}
#if !defined(__APPLE__) && !defined(__FreeBSD__)
	rc = mount("tmpfs", "/dev/shm", "tmpfs", 0, NULL);
	if (rc) {
		error("%s: mounting private /dev/shm failed: %s\n",
		      __func__, strerror(errno));
		return -1;
	}
#endif
	return rc;
}

static int _rm_data(const char *path, const struct stat *st_buf,
		    int type, struct FTW *ftwbuf)
{
	int rc = SLURM_SUCCESS;

	if (remove(path) < 0) {
		log_level_t log_lvl;
		if (force_rm) {
			rc = SLURM_ERROR;
			log_lvl = LOG_LEVEL_ERROR;
		} else
			log_lvl = LOG_LEVEL_DEBUG2;

		if (type == FTW_NS)
			log_var(log_lvl,
					"%s: Unreachable file of FTW_NS type: %s",
					__func__, path);
		else if (type == FTW_DNR)
			log_var(log_lvl,
					"%s: Unreadable directory: %s",
					__func__, path);

		log_var(log_lvl,
				"%s: could not remove path: %s: %s",
				__func__, path, strerror(errno));
	}

	return rc;
}

static int _create_ns(uint32_t job_id, uid_t uid, bool remount)
{
	char job_mount[PATH_MAX];
	char ns_holder[PATH_MAX];
	char src_bind[PATH_MAX];
	char *result = NULL;
	int fd;
	int rc = 0;
	sem_t *sem1 = NULL;
	sem_t *sem2 = NULL;
	pid_t cpid;

#ifdef HAVE_NATIVE_CRAY
	return 0;
#endif

	if (_create_paths(job_id, job_mount, ns_holder, src_bind)
	    != SLURM_SUCCESS) {
		return -1;
	}

	rc = mkdir(job_mount, 0700);
	if (rc && errno != EEXIST) {
		error("%s: mkdir %s failed: %s",
		      __func__, job_mount, strerror(errno));
		return -1;
	} else if (!remount && rc && errno == EEXIST) {
		/*
		 * This is coming from sbcast likely,
		 * exit as success
		 */
		rc = 0;
		goto exit2;
	}

#if !defined(__APPLE__) && !defined(__FreeBSD__)
	/*
	 * MS_BIND mountflag would make mount() ignore all other mountflags
	 * except MS_REC. We need MS_PRIVATE mountflag as well to make the
	 * mount (as well as all mounts inside it) private, which needs to be
	 * done by calling mount() a second time with MS_PRIVATE and MS_REC
	 * flags.
	 */
	if (mount(job_mount, job_mount, "xfs", MS_BIND, NULL)) {
		error("%s: Initial base mount failed, %s",
		      __func__, strerror(errno));
		return SLURM_ERROR;
	}
	if (mount(job_mount, job_mount, "xfs", MS_PRIVATE | MS_REC, NULL)) {
		error("%s: Initial base mount failed, %s",
		      __func__, strerror(errno));
		return SLURM_ERROR;
	}
#endif

	fd = open(ns_holder, O_CREAT|O_RDWR, S_IRWXU);
	if (fd == -1) {
		if (!remount) {
			error("%s: open failed %s: %s",
			      __func__, ns_holder, strerror(errno));
			rc = -1;
		} else
			debug3("ignoring wrong ns_holder path %s: %m",
			       ns_holder);
		goto exit2;
	}
	close(fd);

	/* run any initialization script- if any*/
	if (jc_conf->initscript) {
		run_command_args_t run_command_args = {
			.max_wait = 10000,
			.script_path = jc_conf->initscript,
			.script_type = "initscript",
			.status = &rc,
		};
		run_command_args.env = env_array_create();
		env_array_overwrite_fmt(&run_command_args.env,
					"SLURM_JOB_ID", "%u", job_id);
		env_array_overwrite_fmt(&run_command_args.env,
					"SLURM_JOB_MOUNTPOINT_SRC", "%s",
					src_bind);
		env_array_overwrite_fmt(&run_command_args.env,
					"SLURM_CONF", "%s",
					slurm_conf.slurm_conf);
		env_array_overwrite_fmt(&run_command_args.env,
					"SLURMD_NODENAME", "%s",
					conf->node_name);
		result = run_command(&run_command_args);
		env_array_free(run_command_args.env);
		if (rc) {
			error("%s: init script: %s failed",
			      __func__, jc_conf->initscript);
			xfree(result);
			goto exit2;
		} else {
			debug3("initscript stdout: %s", result);
		}
		xfree(result);
	}

	rc = mkdir(src_bind, 0700);
	if (rc && (!remount || errno != EEXIST)) {
		error("%s: mkdir failed %s, %s",
		      __func__, src_bind, strerror(errno));
		goto exit2;
	}

	sem1 = mmap(NULL, sizeof(*sem1), PROT_READ|PROT_WRITE,
		    MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (sem1 == MAP_FAILED) {
		error("%s: mmap failed: %s", __func__, strerror(errno));
		rc = -1;
		goto exit2;
	}

	sem2 = mmap(NULL, sizeof(*sem2), PROT_READ|PROT_WRITE,
		    MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (sem2 == MAP_FAILED) {
		error("%s: mmap failed: %s", __func__, strerror(errno));
		sem_destroy(sem1);
		munmap(sem1, sizeof(*sem1));
		rc = -1;
		goto exit2;
	}

	rc = sem_init(sem1, 1, 0);
	if (rc) {
		error("%s: sem_init: %s", __func__, strerror(errno));
		goto exit1;
	}
	rc = sem_init(sem2, 1, 0);
	if (rc) {
		error("%s: sem_init: %s", __func__, strerror(errno));
		goto exit1;
	}

	cpid = fork();

	if (cpid == -1) {
		error("%s: fork Failed: %s\n", __func__, strerror(errno));
		rc = -1;
		goto exit1;
	}

	if (cpid == 0) {
		rc = unshare(CLONE_NEWNS);
		if (rc) {
			error("%s: %s", __func__, strerror(errno));
			goto child_exit;
		}
		if (sem_post(sem1) < 0) {
			error("%s: sem_post failed: %s",
			      __func__, strerror(errno));
			rc = -1;
			goto child_exit;
		}
		if (sem_wait(sem2) < 0) {
			error("%s: sem_wait failed %s",
			      __func__, strerror(errno));
			rc = -1;
			goto child_exit;
		}

		/*
		 * Now we have a persistent mount namespace.
		 * Mount private /tmp inside the namespace.
		 */
		if (_mount_private_tmp(src_bind) == -1) {
			rc = -1;
			goto child_exit;
		}

		/*
		 * this happens when restarting the slurmd, the ownership should
		 * already be correct here.
		 */
		if (!remount) {
			rc = chown(src_bind, uid, -1);
			if (rc) {
				error("%s: chown failed for %s: %s",
				      __func__, src_bind, strerror(errno));
				rc = -1;
				goto child_exit;
			}
		}

		/*
		 * This umount is to remove the basepath mount from being
		 * visible inside the namespace. So if a user looks up the
		 * mounts inside the job, they will only see their job mount
		 * but not the basepath mount.
		 */
		rc = umount2(job_mount, MNT_DETACH);
		if (rc) {
			error("%s: umount2 failed: %s",
			      __func__, strerror(errno));
			goto child_exit;
		}
	child_exit:
		sem_destroy(sem1);
		munmap(sem1, sizeof(*sem1));
		sem_destroy(sem2);
		munmap(sem2, sizeof(*sem2));

		if (!rc) {
			rc = _mount_private_shm();
			if (rc)
				error("%s: could not mount private shm",
				      __func__);
		}
		exit(rc);
	} else {
		int wstatus;
		char proc_path[PATH_MAX];

		if (sem_wait(sem1) < 0) {
			error("%s: sem_Wait failed: %s",
			      __func__, strerror(errno));
			rc = -1;
			goto exit1;
		}

		if (snprintf(proc_path, PATH_MAX, "/proc/%u/ns/mnt", cpid)
		    >= PATH_MAX) {
			error("%s: Unable to build job %u /proc path: %m",
			      __func__, job_id);
			rc = -1;
			goto exit1;
		}

		/*
		 * Bind mount /proc/pid/ns/mnt to hold namespace active
		 * without a process attached to it
		 */
#if !defined(__APPLE__) && !defined(__FreeBSD__)
		rc = mount(proc_path, ns_holder, NULL, MS_BIND, NULL);
		if (rc) {
			error("%s: ns base mount failed: %s",
			      __func__, strerror(errno));
			if (sem_post(sem2) < 0)
				error("%s: Could not release semaphore: %s",
				      __func__, strerror(errno));
			goto exit1;
		}
#endif
		if (sem_post(sem2) < 0) {
			error("%s: sem_post failed: %s",
			      __func__, strerror(errno));
			goto exit1;
		}

		if ((waitpid(cpid, &wstatus, 0) != cpid) || WEXITSTATUS(wstatus)) {
			error("%s: waitpid failed", __func__);
			rc = SLURM_ERROR;
			goto exit1;
		}

		rc = 0;
	}

exit1:
	sem_destroy(sem1);
	munmap(sem1, sizeof(*sem1));
	sem_destroy(sem2);
	munmap(sem2, sizeof(*sem2));

exit2:
	if (rc) {
		/* cleanup the job mount */
		force_rm = true;
		if (nftw(job_mount, _rm_data, 64, FTW_DEPTH|FTW_PHYS) < 0) {
			error("%s: Directory traversal failed: %s: %s",
			      __func__, job_mount, strerror(errno));
			return SLURM_ERROR;
		}
		umount2(job_mount, MNT_DETACH);
	}

	return rc;
}

extern int container_p_create(uint32_t job_id, uid_t uid)
{
	return SLURM_SUCCESS;
}

extern int container_p_join_external(uint32_t job_id)
{
	char job_mount[PATH_MAX];
	char ns_holder[PATH_MAX];

	if (_create_paths(job_id, job_mount, ns_holder, NULL)
	    != SLURM_SUCCESS) {
		return -1;
	}

	if (step_ns_fd == -1) {
		step_ns_fd = open(ns_holder, O_RDONLY);
		if (step_ns_fd == -1)
			error("%s: %s", __func__, strerror(errno));
	}

	return step_ns_fd;
}

extern int container_p_add_cont(uint32_t job_id, uint64_t cont_id)
{
	return SLURM_SUCCESS;
}

extern int container_p_join(uint32_t job_id, uid_t uid)
{
	char job_mount[PATH_MAX];
	char ns_holder[PATH_MAX];
	int fd;
	int rc = 0;

#ifdef HAVE_NATIVE_CRAY
	return SLURM_SUCCESS;
#endif

	/*
	 * Jobid 0 means we are not a real job, but a script running instead we
	 * do not need to handle this request.
	 */
	if (job_id == 0)
		return SLURM_SUCCESS;

	if (_create_paths(job_id, job_mount, ns_holder, NULL)
	    != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	/* This is called on the slurmd so we can't use ns_fd. */
	fd = open(ns_holder, O_RDONLY);
	if (fd == -1) {
		error("%s: open failed for %s: %s",
		      __func__, ns_holder, strerror(errno));
		return SLURM_ERROR;
	}

	rc = setns(fd, CLONE_NEWNS);
	if (rc) {
		error("%s: setns failed for %s: %s",
		      __func__, ns_holder, strerror(errno));
		/* closed after strerror(errno) */
		close(fd);
		return SLURM_ERROR;
	} else {
		debug3("job entered namespace");
	}

	close(fd);

	return SLURM_SUCCESS;
}

static int _delete_ns(uint32_t job_id, bool is_slurmd)
{
	char job_mount[PATH_MAX];
	char ns_holder[PATH_MAX];
	int rc = 0;

#ifdef HAVE_NATIVE_CRAY
	return SLURM_SUCCESS;
#endif

	if (_create_paths(job_id, job_mount, ns_holder, NULL)
	    != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	errno = 0;

	/*
	 * FIXME: only used for SLURM_20_11_PROTOCOL_VERSION
	 * This is only here to handle upgrades from 20.11, and should be
	 * removed once upgrading from 20.11 is no longer supported.
	 */
	if (is_slurmd && legacy_jobs) {
		legacy_job_info_t *job;
		job = list_find_first(legacy_jobs,
				      (ListFindF)_find_legacy_job_in_list,
				      &job_id);
		/* if we didn't find the job, it doesn't need legacy handling */
		if (!job)
			return SLURM_SUCCESS;
		/* cleanup the list */
		list_delete_first(legacy_jobs,
				  (ListFindF)_find_legacy_job_in_list,
				  &job_id);
		xfree(job);
		if (list_count(legacy_jobs) == 0)
			FREE_NULL_LIST(legacy_jobs);
	} else if (is_slurmd)
		return SLURM_SUCCESS;

	/*
	 * umount2() sets errno to EINVAL if the target is not a mount point
	 * but also if called with invalid flags.  Consider this if changing the
	 * flags to umount2().
	 */
	rc = umount2(ns_holder, MNT_DETACH);
	if (rc) {
		if (errno == EINVAL) {
			debug2("%s: umount2 %s failed: %s",
			       __func__, ns_holder, strerror(errno));
		} else {
			error("%s: umount2 %s failed: %s",
			      __func__, ns_holder, strerror(errno));
			return SLURM_ERROR;
		}
	}

	/*
	 * Traverses the job directory, and delete all files.
	 * Doesn't -
	 *	traverse filesystem boundaries,
	 *	follow symbolic links
	 * Does -
	 *	a post order traversal and delete directory after processing
	 *      contents
	 * NOTE: Can happen EBUSY here so we need to ignore this.
	 */
	force_rm = false;
	if (nftw(job_mount, _rm_data, 64, FTW_DEPTH|FTW_PHYS) < 0) {
		error("%s: Directory traversal failed: %s: %s",
		      __func__, job_mount, strerror(errno));
		return SLURM_ERROR;
	}

	if (umount2(job_mount, MNT_DETACH))
		debug2("umount2: %s failed: %s", job_mount, strerror(errno));
	rmdir(job_mount);

	return SLURM_SUCCESS;
}

extern int container_p_delete(uint32_t job_id)
{
	/* change to return SLURM_SUCCESS when 20.11 is not supported) */
	return _delete_ns(job_id, true);
}

extern int container_p_stepd_create(uint32_t job_id, uid_t uid)
{
	return _create_ns(job_id, uid, false);
}

extern int container_p_stepd_delete(uint32_t job_id)
{
	return _delete_ns(job_id, false);
}
