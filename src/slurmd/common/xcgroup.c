/*****************************************************************************\
 *  xcgroup.c - cgroup related primitives
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/mount.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "xcgroup.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

/* internal functions */
size_t _file_getsize(int fd);
int _file_read_uint32s(char* file_path, uint32_t** pvalues, int* pnb);
int _file_write_uint32s(char* file_path, uint32_t* values, int nb);
int _file_read_uint64s(char* file_path, uint64_t** pvalues, int* pnb);
int _file_write_uint64s(char* file_path, uint64_t* values, int nb);
int _file_read_content(char* file_path, char** content, size_t *csize);
int _file_write_content(char* file_path, char* content, size_t csize);


/*
 * -----------------------------------------------------------------------------
 * xcgroup_ns primitives xcgroup_ns primitives xcgroup_ns primitives
 * xcgroup_ns primitives xcgroup_ns primitives xcgroup_ns primitives
 * xcgroup_ns primitives xcgroup_ns primitives xcgroup_ns primitives
 * -----------------------------------------------------------------------------
 */

/*
 * create a cgroup namespace for tasks containment
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_create(slurm_cgroup_conf_t *conf,
		      xcgroup_ns_t *cgns, char *mnt_args, char *subsys) {

	cgns->mnt_point = xstrdup_printf("%s/%s",
					 conf->cgroup_mountpoint, subsys);
	cgns->mnt_args = xstrdup(mnt_args);
	cgns->subsystems = xstrdup(subsys);
	cgns->notify_prog = xstrdup_printf("%s/release_%s",
					   conf->cgroup_release_agent, subsys);

	/* check that freezer cgroup namespace is available */
	if (!xcgroup_ns_is_available(cgns)) {
		if (conf->cgroup_automount) {
			if (xcgroup_ns_mount(cgns)) {
				error("unable to mount %s cgroup "
				      "namespace: %s",
				      subsys, slurm_strerror(errno));
				goto clean;
			}
			info("cgroup namespace '%s' is now mounted", subsys);
		} else {
			error("cgroup namespace '%s' not mounted. aborting",
			      subsys);
			goto clean;
		}
	}

	return XCGROUP_SUCCESS;
clean:
	xcgroup_ns_destroy(cgns);
	return XCGROUP_ERROR;
}

/*
 * destroy a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_destroy(xcgroup_ns_t* cgns)
{
	xfree(cgns->mnt_point);
	xfree(cgns->mnt_args);
	xfree(cgns->subsystems);
	xfree(cgns->notify_prog);

	return XCGROUP_SUCCESS;
}

/*
 * mount a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 *
 * If an error occurs, errno will be set.
 */
int xcgroup_ns_mount(xcgroup_ns_t* cgns)
{
	int fstatus;
	char* options;
	char opt_combined[1024];

	char* mnt_point;
	char* p;

	xcgroup_t cg;

	mode_t cmask;
	mode_t omask;

	cmask = S_IWGRP | S_IWOTH;
	omask = umask(cmask);

	fstatus = mkdir(cgns->mnt_point, 0755);
	if (fstatus && errno != EEXIST) {
		if (cgns->mnt_point[0] != '/') {
			debug("unable to create cgroup ns directory '%s'"
			      " : do not start with '/'", cgns->mnt_point);
			umask(omask);
			return XCGROUP_ERROR;
		}
		mnt_point = xstrdup(cgns->mnt_point);
		p = mnt_point;
		while ((p = index(p+1, '/')) != NULL) {
			*p = '\0';
			fstatus = mkdir(mnt_point, 0755);
			if (fstatus && errno != EEXIST) {
				debug("unable to create cgroup ns required "
				      "directory '%s'", mnt_point);
				xfree(mnt_point);
				umask(omask);
				return XCGROUP_ERROR;
			}
			*p='/';
		}
		xfree(mnt_point);
		fstatus = mkdir(cgns->mnt_point, 0755);
	}

	if (fstatus && errno != EEXIST) {
		debug("unable to create cgroup ns directory '%s'"
		      " : %m", cgns->mnt_point);
		umask(omask);
		return XCGROUP_ERROR;
	}
	umask(omask);

	if (cgns->mnt_args == NULL ||
	    strlen(cgns->mnt_args) == 0)
		options = cgns->subsystems;
	else {
		if (snprintf(opt_combined, sizeof(opt_combined), "%s,%s",
			     cgns->subsystems, cgns->mnt_args)
		    >= sizeof(opt_combined)) {
			debug2("unable to build cgroup options string");
			return XCGROUP_ERROR;
		}
		options = opt_combined;
	}

#if defined(__FreeBSD__)
	if (mount("cgroup", cgns->mnt_point,
		  MS_NOSUID|MS_NOEXEC|MS_NODEV, options))
#else
	if (mount("cgroup", cgns->mnt_point, "cgroup",
		  MS_NOSUID|MS_NOEXEC|MS_NODEV, options))
#endif
		return XCGROUP_ERROR;
	else {
		/* FIXME: this only gets set when we aren't mounted at
		   all.  Since we never umount this may only be loaded
		   at startup the first time.
		*/

		/* we then set the release_agent if necessary */
		if (cgns->notify_prog) {
			if (xcgroup_create(cgns, &cg, "/", 0, 0) ==
			     XCGROUP_ERROR)
				return XCGROUP_SUCCESS;
			xcgroup_set_param(&cg, "release_agent",
					  cgns->notify_prog);
			xcgroup_destroy(&cg);
		}
		return XCGROUP_SUCCESS;
	}
}

/*
 * umount a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 *
 * If an error occurs, errno will be set.
 */
int xcgroup_ns_umount(xcgroup_ns_t* cgns)
{
	if (umount(cgns->mnt_point))
		return XCGROUP_ERROR;
	return XCGROUP_SUCCESS;
}

/*
 * check that a cgroup namespace is ready to be used
 *
 * returned values:
 *  - XCGROUP_ERROR : not available
 *  - XCGROUP_SUCCESS : ready to be used
 */
int xcgroup_ns_is_available(xcgroup_ns_t* cgns)
{
	int fstatus = 0;
	char* value;
	size_t s;
	xcgroup_t cg;

	if (xcgroup_create(cgns, &cg, "/", 0, 0) == XCGROUP_ERROR)
		return 0;
	if (xcgroup_get_param(&cg, "release_agent",
			      &value, &s) != XCGROUP_SUCCESS)
		fstatus = 0;
	else {
		xfree(value);
		fstatus = 1;
	}

	xcgroup_destroy(&cg);

	return fstatus;
}

/*
 * Look for the cgroup in a specific cgroup namespace that owns
 * a particular pid
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_find_by_pid(xcgroup_ns_t* cgns, xcgroup_t* cg, pid_t pid)
{
	int fstatus = SLURM_ERROR;
	char file_path[PATH_MAX];
	char* buf;
	size_t fsize;
	char* p;
	char* e;
	char* entry;
	char* subsys;

	/* build pid cgroup meta filepath */
	if (snprintf(file_path, PATH_MAX, "/proc/%u/cgroup",
		      pid) >= PATH_MAX) {
		debug2("unable to build cgroup meta filepath for pid=%u : %m",
		       pid);
		return XCGROUP_ERROR;
	}

	/*
	 * read file content
	 * multiple lines of the form :
	 * num_mask:subsystems:relative_path
	 */
	fstatus = _file_read_content(file_path, &buf, &fsize);
	if (fstatus == XCGROUP_SUCCESS) {
		fstatus = XCGROUP_ERROR;
		p = buf;
		while ((e = index(p, '\n')) != NULL) {
			*e='\0';
			/* get subsystems entry */
			subsys = index(p, ':');
			p = e + 1;
			if (subsys == NULL)
				continue;
			subsys++;
			/* get relative path entry */
			entry = index(subsys, ':');
			if (entry == NULL)
				continue;
			*entry='\0';
			/* check subsystem versus ns one */
			if (strcmp(cgns->subsystems, subsys) != 0) {
				debug("skipping cgroup subsys %s(%s)",
				      subsys, cgns->subsystems);
				continue;
			}
			entry++;
			fstatus = xcgroup_load(cgns, cg, entry);
			break;
		}
		xfree(buf);
	}

	return fstatus;
}

int xcgroup_ns_load(slurm_cgroup_conf_t *conf, xcgroup_ns_t *cgns, char *subsys)
{
	cgns->mnt_point = xstrdup_printf("%s/%s",
					 conf->cgroup_mountpoint, subsys);
	cgns->mnt_args = NULL;
	cgns->subsystems = xstrdup(subsys);
	cgns->notify_prog = xstrdup_printf("%s/release_%s",
					   conf->cgroup_release_agent, subsys);
	return XCGROUP_SUCCESS;
}

/*
 * -----------------------------------------------------------------------------
 * xcgroup primitives xcgroup primitives xcgroup primitives xcgroup primitives
 * xcgroup primitives xcgroup primitives xcgroup primitives xcgroup primitives
 * xcgroup primitives xcgroup primitives xcgroup primitives xcgroup primitives
 * -----------------------------------------------------------------------------
 */

int xcgroup_create(xcgroup_ns_t* cgns, xcgroup_t* cg,
		   char* uri, uid_t uid,  gid_t gid)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];

	/* build cgroup absolute path*/
	if (snprintf(file_path, PATH_MAX, "%s%s", cgns->mnt_point,
		      uri) >= PATH_MAX) {
		debug2("unable to build cgroup '%s' absolute path in ns '%s' "
		       ": %m", uri, cgns->subsystems);
		return fstatus;
	}

	/* fill xcgroup structure */
	cg->ns = cgns;
	cg->name = xstrdup(uri);
	cg->path = xstrdup(file_path);
	cg->uid = uid;
	cg->gid = gid;
	cg->notify = 1;

	return XCGROUP_SUCCESS;
}

int xcgroup_destroy(xcgroup_t* cg)
{
	cg->ns = NULL;
	xfree(cg->name);
	xfree(cg->path);
	cg->uid = -1;
	cg->gid = -1;
	return XCGROUP_SUCCESS;
}

int xcgroup_lock(xcgroup_t* cg)
{
	int fstatus = XCGROUP_ERROR;

	if (cg->path == NULL)
		return fstatus;

	if ((cg->fd = open(cg->path, O_RDONLY)) < 0) {
		debug2("xcgroup_lock: error from open of cgroup '%s' : %m",
		       cg->path);
		return fstatus;
	}

	if (flock(cg->fd,  LOCK_EX) < 0) {
		debug2("xcgroup_lock: error locking cgroup '%s' : %m",
		       cg->path);
		close(cg->fd);
	}
	else
		fstatus = XCGROUP_SUCCESS;

	return fstatus;
}

int xcgroup_unlock(xcgroup_t* cg)
{
	int fstatus = XCGROUP_ERROR;

	if (flock(cg->fd,  LOCK_UN) < 0) {
		debug2("xcgroup_lock: error unlocking cgroup '%s' : %m",
		       cg->path);
	}
	else
		fstatus = XCGROUP_SUCCESS;

	close(cg->fd);
	return fstatus;
}

int xcgroup_instanciate(xcgroup_t* cg)
{
	int fstatus = XCGROUP_ERROR;
	mode_t cmask;
	mode_t omask;

	xcgroup_ns_t* cgns;
	char* file_path;
	uid_t uid;
	gid_t gid;
	int create_only;
	uint32_t notify;

	/* init variables based on input cgroup */
	cgns = cg->ns;
	file_path = cg->path;
	uid = cg->uid;
	gid = cg->gid;
	create_only=0;
	notify = cg->notify;

	/* save current mask and apply working one */
	cmask = S_IWGRP | S_IWOTH;
	omask = umask(cmask);

	/* build cgroup */
 	if (mkdir(file_path, 0755)) {
		if (create_only || errno != EEXIST) {
			debug2("%s: unable to create cgroup '%s' : %m",
			       __func__, file_path);
			umask(omask);
			return fstatus;
		}
	}
	umask(omask);

	/* change cgroup ownership as requested */
	if (chown(file_path, uid, gid)) {
		debug2("unable to chown %d:%d cgroup '%s' : %m",
		       uid, gid, file_path);
		return fstatus;
	}

	/* following operations failure might not result in a general
	 * failure so set output status to success */
	fstatus = XCGROUP_SUCCESS;

	/* set notify on release flag */
	if (notify == 1 && cgns->notify_prog)
		xcgroup_set_params(cg, "notify_on_release=1");
	else
		xcgroup_set_params(cg, "notify_on_release=0");
	return fstatus;
}

int xcgroup_load(xcgroup_ns_t* cgns, xcgroup_t* cg, char* uri)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];

	struct stat buf;

	/* build cgroup absolute path*/
	if (snprintf(file_path, PATH_MAX, "%s%s", cgns->mnt_point,
		      uri) >= PATH_MAX) {
		debug2("unable to build cgroup '%s' absolute path in ns '%s' "
		       ": %m", uri, cgns->subsystems);
		return fstatus;
	}

	if (stat((const char*)file_path, &buf)) {
		debug2("unable to get cgroup '%s' entry '%s' properties"
		       ": %m", cgns->mnt_point, file_path);
		return fstatus;
	}

	/* fill xcgroup structure */
	cg->ns = cgns;
	cg->name = xstrdup(uri);
	cg->path = xstrdup(file_path);
	cg->uid = buf.st_uid;
	cg->gid = buf.st_gid;

	/* read the content of the notify flag */
	xcgroup_get_uint32_param(cg, "notify_on_release", &(cg->notify));

	return XCGROUP_SUCCESS;
}

int xcgroup_delete(xcgroup_t* cg)
{
	if (rmdir(cg->path))
		return XCGROUP_ERROR;
	else
		return XCGROUP_SUCCESS;
}

static int cgroup_procs_readable (xcgroup_t *cg)
{
	struct stat st;
	char *path = NULL;
	int rc = 0;

	xstrfmtcat (path, "%s/%s", cg->path, "cgroup.procs");
	if ((stat (path, &st) >= 0) && (st.st_mode & S_IRUSR))
		rc = 1;
	xfree (path);
	return (rc);
}

static int cgroup_procs_writable (xcgroup_t *cg)
{
	struct stat st;
	char *path = NULL;
	int rc = 0;

	xstrfmtcat (path, "%s/%s", cg->path, "cgroup.procs");
	if ((stat (path, &st) >= 0) && (st.st_mode & S_IWUSR))
		rc = 1;
	xfree (path);
	return (rc);
}

/* This call is not intended to be used to move thread pids
 */
int xcgroup_add_pids(xcgroup_t* cg, pid_t* pids, int npids)
{
	int fstatus = XCGROUP_ERROR;
	char* path = NULL;

	// If possible use cgroup.procs to add the processes atomically
	if (cgroup_procs_writable (cg))
		xstrfmtcat (path, "%s/%s", cg->path, "cgroup.procs");
	else
		xstrfmtcat (path, "%s/%s", cg->path, "tasks");

	fstatus = _file_write_uint32s(path, (uint32_t*)pids, npids);
	if (fstatus != XCGROUP_SUCCESS)
		debug2("unable to add pids to '%s'", cg->path);

	xfree(path);
	return fstatus;
}

/* This call is not intended to be used to get thread pids
 */
int xcgroup_get_pids(xcgroup_t* cg, pid_t **pids, int *npids)
{
	int fstatus = XCGROUP_ERROR;
	char* path = NULL;

	if (pids == NULL || npids == NULL)
		return SLURM_ERROR;

	if (cgroup_procs_readable (cg))
		xstrfmtcat (path, "%s/%s", cg->path, "cgroup.procs");
	else
		xstrfmtcat (path, "%s/%s", cg->path, "tasks");

	fstatus = _file_read_uint32s(path, (uint32_t**)pids, npids);
	if (fstatus != XCGROUP_SUCCESS)
		debug2("unable to get pids of '%s'", cg->path);

	xfree(path);
	return fstatus;
}

int xcgroup_set_params(xcgroup_t* cg, char* parameters)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];
	char* cpath = cg->path;
	char* params;
	char* value;
	char* p;
	char* next;

	params = (char*) xstrdup(parameters);

	p = params;
	while (p != NULL && *p != '\0') {
		next = index(p, ' ');
		if (next) {
			*next='\0';
			next++;
			while (*next == ' ')
				next++;
		}
		value = index(p, '=');
		if (value != NULL) {
			*value='\0';
			value++;
			if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, p)
			     >= PATH_MAX) {
				debug2("unable to build filepath for '%s' and"
				       " parameter '%s' : %m", cpath, p);
				goto next_loop;
			}
			fstatus = _file_write_content(file_path, value,
						      strlen(value));
			if (fstatus != XCGROUP_SUCCESS)
				debug2("unable to set parameter '%s' to "
				       "'%s' for '%s'", p, value, cpath);
			else
				debug3("parameter '%s' set to '%s' for '%s'",
				       p, value, cpath);
		}
		else
			debug2("bad parameters format for entry '%s'", p);
	next_loop:
		p = next;
	}

	xfree(params);
	return fstatus;
}

int xcgroup_set_param(xcgroup_t* cg, char* param, char* content)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];
	char* cpath = cg->path;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		debug2("unable to build filepath for '%s' and"
		       " parameter '%s' : %m", cpath, param);
		return fstatus;
	}

	fstatus = _file_write_content(file_path, content, strlen(content));
	if (fstatus != XCGROUP_SUCCESS)
		debug2("unable to set parameter '%s' to "
		       "'%s' for '%s'", param, content, cpath);
	else
		debug3("parameter '%s' set to '%s' for '%s'",
		       param, content, cpath);

	return fstatus;
}

int xcgroup_get_param(xcgroup_t* cg, char* param, char **content, size_t *csize)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];
	char* cpath = cg->path;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		debug2("unable to build filepath for '%s' and"
		       " parameter '%s' : %m", cpath, param);
	} else {
		fstatus = _file_read_content(file_path, content, csize);
		if (fstatus != XCGROUP_SUCCESS)
			debug2("unable to get parameter '%s' for '%s'",
			       param, cpath);
	}
	return fstatus;
}

int xcgroup_set_uint32_param(xcgroup_t* cg, char* param, uint32_t value)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];
	char* cpath = cg->path;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		debug2("unable to build filepath for '%s' and"
		       " parameter '%s' : %m", cpath, param);
		return fstatus;
	}

	fstatus = _file_write_uint32s(file_path, &value, 1);
	if (fstatus != XCGROUP_SUCCESS)
		debug2("unable to set parameter '%s' to "
		       "'%u' for '%s'", param, value, cpath);
	else
		debug3("parameter '%s' set to '%u' for '%s'",
		       param, value, cpath);

	return fstatus;
}

int xcgroup_get_uint32_param(xcgroup_t* cg, char* param, uint32_t* value)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];
	char* cpath = cg->path;
	uint32_t* values;
	int vnb;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		debug2("unable to build filepath for '%s' and"
		       " parameter '%s' : %m", cpath, param);
	}
	else {
		fstatus = _file_read_uint32s(file_path, &values, &vnb);
		if (fstatus != XCGROUP_SUCCESS)
			debug2("unable to get parameter '%s' for '%s'",
			       param, cpath);
		else if (vnb < 1) {
			debug2("empty parameter '%s' for '%s'",
			       param, cpath);
		}
		else {
			*value = values[0];
			xfree(values);
			fstatus = XCGROUP_SUCCESS;
		}
	}
	return fstatus;
}

int xcgroup_set_uint64_param(xcgroup_t* cg, char* param, uint64_t value)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];
	char* cpath = cg->path;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		debug2("unable to build filepath for '%s' and"
		       " parameter '%s' : %m", cpath, param);
		return fstatus;
	}

	fstatus = _file_write_uint64s(file_path, &value, 1);
	if (fstatus != XCGROUP_SUCCESS)
		debug2("unable to set parameter '%s' to "
		       "'%"PRIu64"' for '%s'", param, value, cpath);
	else
		debug3("parameter '%s' set to '%"PRIu64"' for '%s'",
		       param, value, cpath);

	return fstatus;
}

int xcgroup_get_uint64_param(xcgroup_t* cg, char* param, uint64_t* value)
{
	int fstatus = XCGROUP_ERROR;
	char file_path[PATH_MAX];
	char* cpath = cg->path;
	uint64_t* values;
	int vnb;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		debug2("unable to build filepath for '%s' and"
		       " parameter '%s' : %m", cpath, param);
	}
	else {
		fstatus = _file_read_uint64s(file_path, &values, &vnb);
		if (fstatus != XCGROUP_SUCCESS)
			debug2("unable to get parameter '%s' for '%s'",
			       param, cpath);
		else if (vnb < 1) {
			debug2("empty parameter '%s' for '%s'",
			       param, cpath);
		}
		else {
			*value = values[0];
			xfree(values);
			fstatus = XCGROUP_SUCCESS;
		}
	}
	return fstatus;
}

static int cgroup_move_process_by_task (xcgroup_t *cg, pid_t pid)
{
	DIR *dir;
	struct dirent *entry;
	char path [PATH_MAX];

	if (snprintf (path, PATH_MAX, "/proc/%d/task", (int) pid) >= PATH_MAX) {
		error ("xcgroup: move_process_by_task: path overflow!");
		return XCGROUP_ERROR;
	}

	dir = opendir (path);
	if (!dir) {
		error ("xcgroup: opendir(%s): %m", path);
		return XCGROUP_ERROR;
	}

	while ((entry = readdir (dir))) {
		if (entry->d_name[0] != '.')
			xcgroup_set_param (cg, "tasks", entry->d_name);
	}
	closedir (dir);
	return XCGROUP_SUCCESS;
}

int xcgroup_move_process (xcgroup_t *cg, pid_t pid)
{
	if (!cgroup_procs_writable (cg))
		return cgroup_move_process_by_task (cg, pid);

	return xcgroup_set_uint32_param (cg, "cgroup.procs", pid);
}


/*
 * -----------------------------------------------------------------------------
 * internal primitives internal primitives internal primitives
 * internal primitives internal primitives internal primitives
 * internal primitives internal primitives internal primitives
 * -----------------------------------------------------------------------------
 */

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
	lseek(fd, 0, SEEK_SET);

	/* get file size */
	fsize=0;
	do {
		rc = read(fd, (void*)&c, 1);
		if (rc > 0)
			fsize++;
	}
	while ((rc < 0 && errno == EINTR) || rc > 0);

	/* restore position */
	lseek(fd, offset, SEEK_SET);

	if (rc < 0)
		return -1;
	else
		return fsize;
}

int _file_write_uint64s(char* file_path, uint64_t* values, int nb)
{
	int fstatus;
	int rc;
	int fd;
	char tstr[256];
	uint64_t value;
	int i;

	/* open file for writing */
	fd = open(file_path, O_WRONLY, 0700);
	if (fd < 0) {
		debug2("unable to open '%s' for writing : %m", file_path);
		return XCGROUP_ERROR;
	}

	/* add one value per line */
	fstatus = XCGROUP_SUCCESS;
	for (i=0 ; i < nb ; i++) {

		value = values[i];

		rc = snprintf(tstr, sizeof(tstr), "%"PRIu64"", value);
		if (rc < 0) {
			debug2("unable to build %"PRIu64" string value, "
			       "skipping", value);
			fstatus = XCGROUP_ERROR;
			continue;
		}

		do {
			rc = write(fd, tstr, strlen(tstr)+1);
		}
		while (rc < 0 && errno == EINTR);
		if (rc < 1) {
			debug2("unable to add value '%s' to file '%s' : %m",
			       tstr, file_path);
			if ( errno != ESRCH )
				fstatus = XCGROUP_ERROR;
		}

	}

	/* close file */
	close(fd);

	return fstatus;
}

int _file_read_uint64s(char* file_path, uint64_t** pvalues, int* pnb)
{
	int rc;
	int fd;

	size_t fsize;
	char* buf;
	char* p;

	uint64_t* pa=NULL;
	int i;

	/* check input pointers */
	if (pvalues == NULL || pnb == NULL)
		return XCGROUP_ERROR;

	/* open file for reading */
	fd = open(file_path, O_RDONLY, 0700);
	if (fd < 0) {
		debug2("unable to open '%s' for reading : %m", file_path);
		return XCGROUP_ERROR;
	}

	/* get file size */
	fsize=_file_getsize(fd);
	if (fsize == -1) {
		close(fd);
		return XCGROUP_ERROR;
	}

	/* read file contents */
	buf = (char*) xmalloc((fsize+1)*sizeof(char));
	do {
		rc = read(fd, buf, fsize);
	}
	while (rc < 0 && errno == EINTR);
	close(fd);
	buf[fsize]='\0';

	/* count values (splitted by \n) */
	i=0;
	if (rc > 0) {
		p = buf;
		while (index(p, '\n') != NULL) {
			i++;
			p = index(p, '\n') + 1;
		}
	}

	/* build uint64_t list */
	if (i > 0) {
		pa = (uint64_t*) xmalloc(sizeof(uint64_t) * i);
		p = buf;
		i = 0;
		while (index(p, '\n') != NULL) {
			long long unsigned int ll_tmp;
			sscanf(p, "%llu", &ll_tmp);
			pa[i++] = ll_tmp;
			p = index(p, '\n') + 1;
		}
	}

	/* free buffer */
	xfree(buf);

	/* set output values */
	*pvalues = pa;
	*pnb = i;

	return XCGROUP_SUCCESS;
}

int _file_write_uint32s(char* file_path, uint32_t* values, int nb)
{
	int fstatus;
	int rc;
	int fd;
	char tstr[256];
	uint32_t value;
	int i;

	/* open file for writing */
	fd = open(file_path, O_WRONLY, 0700);
	if (fd < 0) {
		debug2("unable to open '%s' for writing : %m", file_path);
		return XCGROUP_ERROR;
	}

	/* add one value per line */
	fstatus = XCGROUP_SUCCESS;
	for (i=0 ; i < nb ; i++) {

		value = values[i];

		rc = snprintf(tstr, sizeof(tstr), "%u", value);
		if (rc < 0) {
			debug2("unable to build %u string value, skipping",
			       value);
			fstatus = XCGROUP_ERROR;
			continue;
		}

		do {
			rc = write(fd, tstr, strlen(tstr)+1);
		}
		while (rc < 0 && errno == EINTR);
		if (rc < 1) {
			debug2("unable to add value '%s' to file '%s' : %m",
			       tstr, file_path);
			if ( errno != ESRCH )
				fstatus = XCGROUP_ERROR;
		}

	}

	/* close file */
	close(fd);

	return fstatus;
}

int _file_read_uint32s(char* file_path, uint32_t** pvalues, int* pnb)
{
	int rc;
	int fd;

	size_t fsize;
	char* buf;
	char* p;

	uint32_t* pa=NULL;
	int i;

	/* check input pointers */
	if (pvalues == NULL || pnb == NULL)
		return XCGROUP_ERROR;

	/* open file for reading */
	fd = open(file_path, O_RDONLY, 0700);
	if (fd < 0) {
		debug2("unable to open '%s' for reading : %m", file_path);
		return XCGROUP_ERROR;
	}

	/* get file size */
	fsize=_file_getsize(fd);
	if (fsize == -1) {
		close(fd);
		return XCGROUP_ERROR;
	}

	/* read file contents */
	buf = (char*) xmalloc((fsize+1)*sizeof(char));
	do {
		rc = read(fd, buf, fsize);
	}
	while (rc < 0 && errno == EINTR);
	close(fd);
	buf[fsize]='\0';

	/* count values (splitted by \n) */
	i=0;
	if (rc > 0) {
		p = buf;
		while (index(p, '\n') != NULL) {
			i++;
			p = index(p, '\n') + 1;
		}
	}

	/* build uint32_t list */
	if (i > 0) {
		pa = (uint32_t*) xmalloc(sizeof(uint32_t) * i);
		p = buf;
		i = 0;
		while (index(p, '\n') != NULL) {
			sscanf(p, "%u", pa+i);
			p = index(p, '\n') + 1;
			i++;
		}
	}

	/* free buffer */
	xfree(buf);

	/* set output values */
	*pvalues = pa;
	*pnb = i;

	return XCGROUP_SUCCESS;
}

int _file_write_content(char* file_path, char* content, size_t csize)
{
	int fstatus;
	int rc;
	int fd;

	/* open file for writing */
	fd = open(file_path, O_WRONLY, 0700);
	if (fd < 0) {
		debug2("unable to open '%s' for writing : %m", file_path);
		return XCGROUP_ERROR;
	}

	/* write content */
	do {
		rc = write(fd, content, csize);
	}
	while (rc < 0 && errno == EINTR);

	/* check read size */
	if (rc < csize) {
		debug2("unable to write %lu bytes to file '%s' : %m",
		       (long unsigned int) csize, file_path);
		fstatus = XCGROUP_ERROR;
	}
	else
		fstatus = XCGROUP_SUCCESS;

	/* close file */
	close(fd);

	return fstatus;
}

int _file_read_content(char* file_path, char** content, size_t *csize)
{
	int fstatus;
	int rc;
	int fd;

	size_t fsize;
	char* buf;

	fstatus = XCGROUP_ERROR;

	/* check input pointers */
	if (content == NULL || csize == NULL)
		return fstatus;

	/* open file for reading */
	fd = open(file_path, O_RDONLY, 0700);
	if (fd < 0) {
		debug2("unable to open '%s' for reading : %m", file_path);
		return fstatus;
	}

	/* get file size */
	fsize=_file_getsize(fd);
	if (fsize == -1) {
		close(fd);
		return fstatus;
	}

	/* read file contents */
	buf = (char*) xmalloc((fsize+1)*sizeof(char));
	buf[fsize]='\0';
	do {
		rc = read(fd, buf, fsize);
	}
	while (rc < 0 && errno == EINTR);

	/* set output values */
	if (rc >= 0) {
		*content = buf;
		*csize = rc;
		fstatus = XCGROUP_SUCCESS;
	}

	/* close file */
	close(fd);

	return fstatus;
}
