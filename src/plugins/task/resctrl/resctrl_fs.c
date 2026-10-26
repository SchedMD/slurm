/*****************************************************************************\
 *  resctrl_fs.c - helpers for the Linux resctrl filesystem
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/plugins/task/resctrl/resctrl_fs.h"

/* As defined in /proc/filesystems */
#define RESCTRL_FS_TYPE "resctrl"

extern char *resctrl_fs_path(const char *group, const char *file)
{
	char *path = xstrdup(RESCTRL_MOUNT_POINT);

	if (group)
		xstrfmtcat(path, "/%s", group);
	if (file)
		xstrfmtcat(path, "/%s", file);

	return path;
}

extern int resctrl_fs_read_file(const char *path, char **content)
{
	int fd = -1, rc = SLURM_SUCCESS;
	char *data = NULL;
	size_t size = 0, buf_size = 4096;

	if ((fd = open(path, (O_RDONLY | O_CLOEXEC))) < 0)
		return errno;

	data = xmalloc(buf_size + 1);
	while (true) {
		ssize_t n = read(fd, (data + size), (buf_size - size));

		if (n < 0) {
			rc = errno;
			break;
		} else if (!n) {
			break;
		}

		size += n;
		if (size == buf_size) {
			buf_size *= 2;
			xrealloc(data, (buf_size + 1));
		}
	}
	close(fd);

	if (rc) {
		xfree(data);
		return rc;
	}

	data[size] = '\0';
	*content = data;

	return SLURM_SUCCESS;
}

extern int resctrl_fs_write_file(const char *path, const char *content)
{
	int fd = -1, rc = SLURM_SUCCESS;
	size_t len = strlen(content);
	ssize_t n = -1;

	if ((fd = open(path, (O_WRONLY | O_TRUNC | O_CLOEXEC))) < 0)
		return errno;

	if ((n = write(fd, content, len)) < 0)
		rc = errno;
	else if ((size_t) n != len)
		rc = EIO;
	close(fd);

	return rc;
}

extern bool resctrl_fs_supported(void)
{
	FILE *fp = NULL;
	char line[256];
	bool supported = false;

	if (!(fp = fopen("/proc/filesystems", "r")))
		return false;

	/*
	 * We are looking for a line like:
	 * "nodev	resctrl"
	 */
	while (fgets(line, sizeof(line), fp)) {
		char *type = xstrchr(line, '\t');

		if (type && !xstrncmp((type + 1), RESCTRL_FS_TYPE "\n",
				      strlen(RESCTRL_FS_TYPE "\n"))) {
			supported = true;
			break;
		}
	}
	fclose(fp);

	return supported;
}

/*
 * The kernel allows a single resctrl mount, but bind mounts of /sys can
 * make it appear more than once.
 * RET xmalloc()'d directory resctrl is mounted on, RESCTRL_MOUNT_POINT when
 *     it is one of them, or NULL
 */
static char *_get_mount_dir(void)
{
	FILE *fp = NULL;
	struct mntent mnt = { 0 };
	struct mntent *ent = NULL;
	char line[4096];
	char *dir = NULL;

	if (!(fp = setmntent("/proc/self/mounts", "r")))
		return NULL;

	while ((ent = getmntent_r(fp, &mnt, line, sizeof(line)))) {
		if (xstrcmp(ent->mnt_type, RESCTRL_FS_TYPE))
			continue;
		if (!xstrcmp(ent->mnt_dir, RESCTRL_MOUNT_POINT)) {
			xfree(dir);
			dir = xstrdup(ent->mnt_dir);
			break;
		}
		if (!dir)
			dir = xstrdup(ent->mnt_dir);
	}
	endmntent(fp);

	return dir;
}

extern bool resctrl_fs_is_mounted(void)
{
	char *dir = _get_mount_dir();
	bool mounted = !xstrcmp(dir, RESCTRL_MOUNT_POINT);

	xfree(dir);

	return mounted;
}

extern int resctrl_fs_mount(const char *options)
{
	char *dir = _get_mount_dir();
	int rc = SLURM_SUCCESS;

	if (dir) {
		if (xstrcmp(dir, RESCTRL_MOUNT_POINT)) {
			error("resctrl is mounted on %s instead of %s",
			      dir, RESCTRL_MOUNT_POINT);
			rc = EBUSY;
		}
		xfree(dir);
		return rc;
	}

	if (mount(RESCTRL_FS_TYPE, RESCTRL_MOUNT_POINT, RESCTRL_FS_TYPE, 0,
		  options)) {
		rc = errno;
		error("cannot mount resctrl on %s: %s",
		      RESCTRL_MOUNT_POINT, slurm_strerror(rc));
	}

	return rc;
}

extern bool resctrl_fs_group_exists(const char *group)
{
	char *path = resctrl_fs_path(group, "tasks");
	struct stat st = { 0 };
	bool exists = !stat(path, &st);

	xfree(path);

	return exists;
}

extern list_t *resctrl_fs_list_groups(void)
{
	char *root = resctrl_fs_path(NULL, NULL);
	list_t *groups = list_create(xfree_ptr);
	DIR *dir = NULL;
	struct dirent *ent = NULL;

	if (!(dir = opendir(root))) {
		xfree(root);
		return groups;
	}

	while ((ent = readdir(dir))) {
		if ((ent->d_name[0] == '.') || !xstrcmp(ent->d_name, "info") ||
		    !xstrcmp(ent->d_name, "mon_data") ||
		    !xstrcmp(ent->d_name, "mon_groups"))
			continue;
		if (resctrl_fs_group_exists(ent->d_name))
			list_append(groups, xstrdup(ent->d_name));
	}
	closedir(dir);
	xfree(root);

	return groups;
}

extern uint32_t resctrl_fs_max_groups(void)
{
	char *info = resctrl_fs_path("info", NULL);
	DIR *dir = NULL;
	struct dirent *ent = NULL;
	uint32_t max_groups = 0;

	if (!(dir = opendir(info))) {
		xfree(info);
		return 0;
	}

	while ((ent = readdir(dir))) {
		char *path = NULL, *content = NULL;
		unsigned long num_closids = 0;

		if (ent->d_name[0] == '.')
			continue;

		path = xstrdup_printf("%s/%s/num_closids", info, ent->d_name);
		if (!resctrl_fs_read_file(path, &content)) {
			num_closids = strtoul(content, NULL, 10);
			if (num_closids && (num_closids <= UINT32_MAX) &&
			    (!max_groups || (num_closids < max_groups)))
				max_groups = num_closids;
			xfree(content);
		}
		xfree(path);
	}
	closedir(dir);
	xfree(info);

	return max_groups;
}

extern int resctrl_fs_group_create(const char *group)
{
	char *path = resctrl_fs_path(group, NULL);
	int rc = SLURM_SUCCESS;

	if (mkdir(path, 0755) < 0)
		rc = errno;
	xfree(path);

	return rc;
}

extern int resctrl_fs_write_schemata(const char *group, const char *schemata)
{
	char *path = resctrl_fs_path(group, "schemata");
	char *content = xstrdup(schemata);
	size_t len = strlen(content);
	int rc = EINVAL;

	if (len && (content[len - 1] != '\n'))
		xstrcat(content, "\n");

	rc = resctrl_fs_write_file(path, content);

	xfree(content);
	xfree(path);

	return rc;
}

/*
 * Write every thread listed in a /proc/<pid>/task directory to a tasks file.
 * IN task_dir - /proc/<pid>/task of the process
 * IN tasks - tasks file of the group
 * RET SLURM_SUCCESS or errno
 */
static int _move_threads(const char *task_dir, const char *tasks)
{
	DIR *dir = NULL;
	struct dirent *ent = NULL;
	int rc = SLURM_SUCCESS;

	if (!(dir = opendir(task_dir)))
		return errno;

	while ((ent = readdir(dir))) {
		if (!isdigit((unsigned char) ent->d_name[0]))
			continue;

		rc = resctrl_fs_write_file(tasks, ent->d_name);
		if (rc == ESRCH) {
			/* The thread exited in the meantime. */
			rc = SLURM_SUCCESS;
			continue;
		}
		if (rc)
			break;
	}
	closedir(dir);

	return rc;
}

extern int resctrl_fs_move_pid(const char *group, pid_t pid)
{
	char *task_dir = NULL;
	char *tasks = resctrl_fs_path(group, "tasks");
	int rc = EINVAL;

	/* The kernel takes 0 as the calling thread, so this slurmstepd. */
	if (!pid) {
		rc = resctrl_fs_write_file(tasks, "0");
		xfree(tasks);
		return rc;
	}

	/*
	 * We read the /proc/<pid>/task/ directory to find all the threads of
	 * a given pid. We then move these threads, but there's a race where
	 * a new thread can be created in the process. So do this operation
	 * twice. This should mitigate the race.
	 */
	task_dir = xstrdup_printf("/proc/%d/task", (int) pid);
	if (!(rc = _move_threads(task_dir, tasks)))
		rc = _move_threads(task_dir, tasks);

	xfree(task_dir);
	xfree(tasks);

	return rc;
}

extern char *resctrl_fs_last_cmd_status(void)
{
	char *path = resctrl_fs_path("info", "last_cmd_status");
	char *status = NULL;

	if (resctrl_fs_read_file(path, &status))
		status = NULL;
	xfree(path);

	if (status)
		xstrtrim(status);

	return status;
}
