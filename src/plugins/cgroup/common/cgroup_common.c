/*****************************************************************************\
 *  cgroup_common.c - Cgroup plugin common functions
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

#include "cgroup_common.h"
#include <poll.h>

/* Testing read() on cgroup interfaces returns 4092 bytes at most. */
#define CGROUP_READ_COUNT 4092

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurm_conf_t slurm_conf __attribute__((weak_import));
#else
slurm_conf_t slurm_conf;
#endif

/*
 * Returns the path to the cgroup.procs file over which we have permissions
 * defined by check_mode. This path is where we'll be able to read or write
 * pids. If there are no paths available with these permissions, return NULL,
 * which means the cgroup doesn't exist or we do not have permissions to modify
 * the cg.
 */
static char *_cgroup_procs_check(xcgroup_t *cg, int check_mode)
{
	struct stat st;
	char *path = xstrdup_printf("%s/%s", cg->path, "cgroup.procs");

	if (!((stat(path, &st) >= 0) && (st.st_mode & check_mode))) {
		error("%s: failed on path %s: %m", __func__, path);
		xfree(path);
	}

	return path;
}

static char *_cgroup_procs_readable_path(xcgroup_t *cg)
{
	return _cgroup_procs_check(cg, S_IRUSR);
}

static char *_cgroup_procs_writable_path(xcgroup_t *cg)
{
	return _cgroup_procs_check(cg, S_IWUSR);
}

static int _set_uint32_param(xcgroup_t *cg, char *param, uint32_t value)
{
	int fstatus = SLURM_ERROR;
	char file_path[PATH_MAX];
	char *cpath = cg->path;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		log_flag(CGROUP, "unable to build filepath for '%s' and parameter '%s' : %m",
			 cpath, param);
		return fstatus;
	}

	fstatus = common_file_write_uint32s(file_path, &value, 1);
	if (fstatus != SLURM_SUCCESS)
		log_flag(CGROUP, "unable to set parameter '%s' to '%u' for '%s'",
			 param, value, cpath);
	else
		log_flag(CGROUP, "parameter '%s' set to '%u' for '%s'",
			 param, value, cpath);

	return fstatus;
}

static bool _is_empty_dir(const char *dirpath)
{
	DIR *d;
	struct dirent *dir;
	bool empty = true;

	if (!(d = opendir(dirpath)))
		return empty;

	while ((dir = readdir(d))) {
		if (dir->d_type == DT_DIR &&
		    (strcmp(dir->d_name, ".") && strcmp(dir->d_name, ".."))) {
			empty = false;
			log_flag(CGROUP, "Found at least one child directory: %s/%s",
				 dirpath, dir->d_name);
			break;
		}
	}

	closedir(d);
	return empty;
}

/*
 * Read a cgroup file interface in chunks of CGROUP_READ_COUNT. If the read is
 * atomic, we should have a correct snapshot of the data. If multiple read()
 * have been needed, the file might have been changed in between calls.
 *
 * IN: file_path - file path
 * IN/OUT: out - pointer to file contents
 *
 * RET: -1 on error, accumulated number of read bytes otherwise
 */
static ssize_t _read_cg_file(char *file_path, char **out)
{
	int fd, nr_reads = 0;
	size_t count = CGROUP_READ_COUNT;
	ssize_t rc, read_bytes = 0;
	char *buf;

	xassert(!*out);

	/* open file for reading */
	fd = open(file_path, O_RDONLY, 0700);
	if (fd < 0) {
		error("unable to open '%s' for reading : %m", file_path);
		return SLURM_ERROR;
	}

	/* read file contents */
	buf = xmalloc(count);
	while ((rc = read(fd, buf + read_bytes, count))) {
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			error("unable to read '%s': %m", file_path);
			xfree(buf);
			break;
		}
		read_bytes += rc;
		xrealloc(buf, (read_bytes + count));
		nr_reads++;
	}

	if (nr_reads > 1)
		log_flag(CGROUP, "%s: Read %zd bytes after %d read() syscalls. File may have changed between syscalls.",
			 file_path, read_bytes, nr_reads);

	close(fd);
	*out = buf;
	return (rc == -1) ? rc : read_bytes;
}

extern int common_file_read_uints(char *file_path, void **values, int *nb,
				  int base)
{
	int i;
	ssize_t fsize;
	char *buf = NULL, *p;
	uint32_t *values32 = NULL;
	uint64_t *values64 = NULL;
	long long unsigned int ll_tmp;

	/* check input pointers */
	if (values == NULL || nb == NULL)
		return SLURM_ERROR;

	if ((fsize = _read_cg_file(file_path, &buf)) < 0)
		return SLURM_ERROR;

	/* count values (splitted by \n) */
	i = 0;
	p = buf;
	while (xstrchr(p, '\n') != NULL) {
		i++;
		p = xstrchr(p, '\n') + 1;
	}

	if (base == 32) {
		/* build uint32_t list */
		if (i > 0) {
			values32 = xcalloc(i, sizeof(uint32_t));
			p = buf;
			i = 0;
			while (xstrchr(p, '\n') != NULL) {
				sscanf(p, "%u", (values32 + i));
				p = xstrchr(p, '\n') + 1;
				i++;
			}
		}
	} else if (base == 64) {
		/* build uint64_t list */
		if (i > 0) {
			values64 = xcalloc(i, sizeof(uint64_t));
			p = buf;
			i = 0;
			while (xstrchr(p, '\n') != NULL) {
				sscanf(p, "%llu", &ll_tmp);
				values64[i++] = ll_tmp;
			p = xstrchr(p, '\n') + 1;
			}
		}
	}

	/* free buffer */
	xfree(buf);

	/* set output values */
	if (base == 32)
		*values = values32;
	else if (base == 64)
		*values = values64;

	*nb = i;

	return SLURM_SUCCESS;
}

extern int common_file_write_uints(char *file_path, void *values, int nb,
				   int base)
{
	int rc;
	int fd;
	char tstr[256];
	uint32_t *values32 = NULL;
	uint64_t *values64 = NULL;

	/* open file for writing */
	if ((fd = open(file_path, O_WRONLY, 0700)) < 0) {
		error("%s: unable to open '%s' for writing: %m",
		      __func__, file_path);
		return SLURM_ERROR;
	}

	if (base == 32)
		values32 = (uint32_t *) values;
	else if (base == 64)
		values64 = (uint64_t *) values;

	/* add one value per line */
	for (int i = 0; i < nb; i++) {
		if (base == 32) {
			uint32_t value = values32[i];
			if (snprintf(tstr, sizeof(tstr), "%u", value) < 0) {
				error("%s: unable to build %u string value: %m",
				      __func__, value);
				close(fd);
				return SLURM_ERROR;
			}
		} else if (base == 64) {
			uint64_t value = values64[i];
			if (snprintf(tstr, sizeof(tstr),
				     "%"PRIu64"", value) <0) {
				error("%s: unable to build %"PRIu64" string value: %m",
				      __func__, value);
				close(fd);
				return SLURM_ERROR;
			}
		} else {
			error("%s: unexpected base %d. Unable to write to %s",
			      __func__, base, file_path);
			close(fd);
			return SLURM_ERROR;
		}

		/* write terminating NUL byte */
		safe_write(fd, tstr, strlen(tstr) + 1);
	}

	/* close file */
	close(fd);
	return SLURM_SUCCESS;
rwfail:
	rc = errno;
	error("%s: write value '%s' to '%s' failed: %m",
	      __func__, tstr, file_path);
	close(fd);
	return rc;
}

extern int common_file_write_content(char *file_path, char *content,
				     size_t csize)
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

extern int common_file_read_content(char *file_path, char **content,
				    size_t *csize)
{
	ssize_t fsize;
	char *buf = NULL;

	/* check input pointers */
	if (content == NULL || csize == NULL)
		return SLURM_ERROR;

	if ((fsize = _read_cg_file(file_path, &buf)) < 0)
		return SLURM_ERROR;

	/* set output values */
	*content = buf;
	*csize = fsize;

	return SLURM_SUCCESS;
}

extern int common_cgroup_instantiate(xcgroup_t *cg)
{
	int fstatus = SLURM_ERROR;
	mode_t cmask;
	mode_t omask;

	char *file_path;
	uid_t uid;
	gid_t gid;

	/* init variables based on input cgroup */
	file_path = cg->path;
	uid = cg->uid;
	gid = cg->gid;

	/* save current mask and apply working one */
	cmask = S_IWGRP | S_IWOTH;
	omask = umask(cmask);

	/* build cgroup */
	if (mkdir(file_path, 0755)) {
		if (errno != EEXIST) {
			error("%s: unable to create cgroup '%s' : %m",
			      __func__, file_path);
			umask(omask);
			return fstatus;
		}
	}
	umask(omask);

	/* change cgroup ownership as requested */
	if (!slurm_cgroup_conf.root_owned_cgroups &&
	    chown(file_path, uid, gid)) {
		error("%s: unable to chown %d:%d cgroup '%s' : %m",
		      __func__, uid, gid, file_path);
		return fstatus;
	}

	/* following operations failure might not result in a general
	 * failure so set output status to success */
	fstatus = SLURM_SUCCESS;

	return fstatus;
}

extern int common_cgroup_create(xcgroup_ns_t *cgns, xcgroup_t *cg, char *uri,
				uid_t uid,  gid_t gid)
{
	int fstatus = SLURM_ERROR;
	char file_path[PATH_MAX];

	/* build cgroup absolute path*/
	if (snprintf(file_path, PATH_MAX, "%s%s", cgns->mnt_point,
		      uri) >= PATH_MAX) {
		log_flag(CGROUP, "unable to build cgroup '%s' absolute path in ns '%s' : %m",
			 uri, cgns->subsystems);
		return fstatus;
	}

	/* fill xcgroup structure */
	cg->ns = cgns;
	cg->name = xstrdup(uri);
	cg->path = xstrdup(file_path);
	cg->uid = uid;
	cg->gid = gid;

	return SLURM_SUCCESS;
}

extern int common_cgroup_move_process(xcgroup_t *cg, pid_t pid)
{
	char *path = NULL;

	/*
	 * First we check permissions to see if we will be able to move the pid.
	 * The path is a path to cgroup.procs and writing there will instruct
	 * the cgroup subsystem to move the process and all its threads there.
	 */
	path = _cgroup_procs_writable_path(cg);

	if (!path) {
		error("Cannot write to cgroup.procs for %s", cg->path);
		return SLURM_ERROR;
	}

	xfree(path);

	return _set_uint32_param(cg, "cgroup.procs", pid);
}

extern int common_cgroup_set_param(xcgroup_t *cg, char *param, char *content)
{
	int fstatus = SLURM_ERROR;
	char file_path[PATH_MAX];
	char *cpath = cg->path;

	if (!cpath || !param)
		return fstatus;

	if (!content) {
		log_flag(CGROUP, "no content given, nothing to do");
		return fstatus;
	}

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		log_flag(CGROUP, "unable to build filepath for '%s' and parameter '%s' : %m",
			 cpath, param);
		return fstatus;
	}

	fstatus = common_file_write_content(file_path, content,
					    strlen(content));
	if (fstatus != SLURM_SUCCESS)
		log_flag(CGROUP, "unable to set parameter '%s' to '%s' for '%s'",
			 param, content, cpath);
	else
		debug3("%s: parameter '%s' set to '%s' for '%s'",
		       __func__, param, content, cpath);

	return fstatus;
}

extern void common_cgroup_ns_destroy(xcgroup_ns_t *cgns)
{
	xfree(cgns->mnt_point);
	xfree(cgns->mnt_args);
	xfree(cgns->subsystems);
}

extern void common_cgroup_destroy(xcgroup_t *cg)
{
	cg->ns = NULL;
	xfree(cg->name);
	xfree(cg->path);
	cg->uid = -1;
	cg->gid = -1;
}

extern int common_cgroup_delete(xcgroup_t *cg)
{
	int retries = 0, npids = -1;
	pid_t *pids = NULL;

	if (!cg || !cg->path) {
		error("invalid control group");
		return SLURM_SUCCESS;
	}

	/*
	 * Do 5 retries and wait 1000 milis on each if we receive an EBUSY and
	 * there are no pids, because we may be trying to remove the directory
	 * when the kernel hasn't yet drained the cgroup internal references
	 * (css_online), even if cgroup.procs is already empty.
	 *
	 * This workaround tries to mitigate a bug on kernels < 3.18 as per
	 * commit 41c25707d21716826e3c1f60967f5550610ec1c9 in the linux kernel.
	 */
	while ((rmdir(cg->path) < 0) && (errno != ENOENT)) {
		if (errno == EBUSY) {
			/*
			 * Do not rely in ENOTEMPTY since in cgroupfs a
			 * non-empty dir. removal will return EBUSY.
			 */
			if (!_is_empty_dir(cg->path)) {
				log_flag(CGROUP, "Cannot rmdir(%s), cgroup is not empty",
					 cg->path);
				return SLURM_ERROR;
			}

			if (npids == -1) {
				/* Do not retry on a 'really' busy cgroup */
				if ((common_cgroup_get_pids(cg, &pids, &npids)
				     != SLURM_SUCCESS))
					return SLURM_ERROR;

				if (npids > 0) {
					xfree(pids);
					debug3("Not removing %s, found %d pids",
					       cg->path, npids);
					return SLURM_ERROR;
				}
			}

			/* This should happen usually only on kernels < 3.18 */
			if (retries < 5) {
				poll(NULL, 0, 1000);
				retries++;
				continue;
			}

			log_flag(CGROUP, "Unable to rmdir(%s), did %d retries: %m",
				 cg->path, retries);
		} else {
			error("Unable to rmdir(%s), unexpected error: %m",
			      cg->path);
		}

		return SLURM_ERROR;
	}

	if (retries)
		log_flag(CGROUP, "rmdir(%s): took %d retries, possible cgroup filesystem slowness",
			 cg->path, retries);

	return SLURM_SUCCESS;
}

extern int common_cgroup_add_pids(xcgroup_t *cg, pid_t *pids, int npids)
{
	int rc = SLURM_ERROR;
	char *path = _cgroup_procs_writable_path(cg);

	rc = common_file_write_uint32s(path, (uint32_t*)pids, npids);
	if (rc != SLURM_SUCCESS)
		error("unable to add pids to '%s'", cg->path);

	xfree(path);
	return rc;
}

extern int common_cgroup_get_pids(xcgroup_t *cg, pid_t **pids, int *npids)
{
	int fstatus = SLURM_ERROR;
	char *path = NULL;

	if (pids == NULL || npids == NULL || !cg->path)
		return SLURM_ERROR;

	path = _cgroup_procs_readable_path(cg);
	if (!path) {
		error("unable to read '%s/cgroup.procs'", cg->path);
		return SLURM_ERROR;
	}

	fstatus = common_file_read_uint32s(path, (uint32_t**)pids, npids);
	if (fstatus != SLURM_SUCCESS)
		log_flag(CGROUP, "unable to get pids of '%s', file disappeared?",
			 path);

	xfree(path);
	return fstatus;
}

extern int common_cgroup_get_param(xcgroup_t *cg, char *param, char **content,
				   size_t *csize)
{
	int fstatus = SLURM_ERROR;
	char file_path[PATH_MAX];
	char *cpath = cg->path;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		log_flag(CGROUP, "unable to build filepath for '%s' and parameter '%s' : %m",
			 cpath, param);
	} else {
		fstatus = common_file_read_content(file_path, content, csize);
		if (fstatus != SLURM_SUCCESS)
			log_flag(CGROUP, "unable to get parameter '%s' for '%s'",
				 param, cpath);
	}
	return fstatus;
}

extern int common_cgroup_set_uint64_param(xcgroup_t *cg, char *param,
					  uint64_t value)
{
	int fstatus = SLURM_ERROR;
	char file_path[PATH_MAX];
	char *cpath = cg->path;

	if (snprintf(file_path, PATH_MAX, "%s/%s", cpath, param) >= PATH_MAX) {
		log_flag(CGROUP, "unable to build filepath for '%s' and parameter '%s' : %m",
			 cpath, param);
		return fstatus;
	}

	fstatus = common_file_write_uint64s(file_path, &value, 1);
	if (fstatus != SLURM_SUCCESS)
		log_flag(CGROUP, "unable to set parameter '%s' to '%"PRIu64"' for '%s'",
			 param, value, cpath);
	else
		debug3("%s: parameter '%s' set to '%"PRIu64"' for '%s'",
		       __func__, param, value, cpath);

	return fstatus;
}

extern int common_cgroup_lock(xcgroup_t *cg)
{
	int fstatus = SLURM_ERROR;

	if (cg->path == NULL)
		return fstatus;

	if ((cg->fd = open(cg->path, O_RDONLY)) < 0) {
		error("error from open of cgroup '%s' : %m", cg->path);
		return fstatus;
	}

	if (flock(cg->fd,  LOCK_EX) < 0) {
		error("error locking cgroup '%s' : %m", cg->path);
		close(cg->fd);
	} else
		fstatus = SLURM_SUCCESS;

	return fstatus;
}

extern int common_cgroup_unlock(xcgroup_t *cg)
{
	int fstatus = SLURM_ERROR;

	if (flock(cg->fd,  LOCK_UN) < 0) {
		error("error unlocking cgroup '%s' : %m", cg->path);
	} else
		fstatus = SLURM_SUCCESS;

	close(cg->fd);
	return fstatus;
}
