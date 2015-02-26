/*****************************************************************************\
 *  cpuset.c - Library for interacting with /dev/cpuset file system
 *****************************************************************************
 *  Copyright (C) 2007 Bull
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Written by Don Albert <Don.Albert@Bull.com> and
 *             Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "affinity.h"
static bool cpuset_prefix_set = false;
static char *cpuset_prefix = "";

static void _cpuset_to_cpustr(const cpu_set_t *mask, char *str)
{
	int i;
	char tmp[16];

	str[0] = '\0';
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, mask))
			continue;
		snprintf(tmp, sizeof(tmp), "%d", i);
		if (str[0])
			strcat(str, ",");
		strcat(str, tmp);
	}
}

int	slurm_build_cpuset(char *base, char *path, uid_t uid, gid_t gid)
{
	char file_path[PATH_MAX], mstr[16];
	int fd, rc;

	if (mkdir(path, 0700) && (errno != EEXIST)) {
		error("%s: mkdir(%s): %m", __func__, path);
		return SLURM_ERROR;
	}
	if (chown(path, uid, gid))
		error("%s: chown(%s): %m", __func__, path);

	/* Copy "cpus" contents from parent directory
	 * "cpus" must be set before any tasks can be added. */
	snprintf(file_path, sizeof(file_path), "%s/%scpus",
		 base, cpuset_prefix);

	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		if (!cpuset_prefix_set) {
			cpuset_prefix_set = 1;
			cpuset_prefix = "cpuset.";
			snprintf(file_path, sizeof(file_path), "%s/%scpus",
				 base, cpuset_prefix);
			fd = open(file_path, O_RDONLY);
			if (fd < 0) {
				cpuset_prefix = "";
				error("%s: open(%s): %m", __func__, file_path);
				return SLURM_ERROR;
			}
		} else {
			error("open(%s): %m", file_path);
			return SLURM_ERROR;
		}
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("%s: read(%s): %m", __func__, file_path);
		return SLURM_ERROR;
	}
	snprintf(file_path, sizeof(file_path), "%s/%scpus",
		 path, cpuset_prefix);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("%s: open(%s): %m", __func__, file_path);
		return SLURM_ERROR;
	}
	rc = write(fd, mstr, rc);
	close(fd);
	if (rc < 1) {
		error("write(%s): %m", file_path);
		return SLURM_ERROR;
	}

	/* Copy "mems" contents from parent directory, if it exists.
	 * "mems" must be set before any tasks can be added. */
	snprintf(file_path, sizeof(file_path), "%s/%smems",
		 base, cpuset_prefix);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("read(%s): %m", file_path);
		return SLURM_ERROR;
	}
	snprintf(file_path, sizeof(file_path), "%s/%smems",
		 path, cpuset_prefix);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}
	rc = write(fd, mstr, rc);
	close(fd);
	if (rc < 1) {
		error("write(%s): %m", file_path);
		return SLURM_ERROR;
	}

	/* Delete cpuset once its tasks complete.
	 * Dependent upon system daemon. */
	snprintf(file_path, sizeof(file_path), "%s/notify_on_release", path);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}
	rc = write(fd, "1", 2);
	close(fd);
	if (rc < 1) {
		error("write(%s): %m", file_path);
		return SLURM_ERROR;
	}

	/* Only now can we add tasks.
	 * We can't add self, so add tasks after exec. */

	return SLURM_SUCCESS;
}

int	slurm_set_cpuset(char *base, char *path, pid_t pid, size_t size,
			 const cpu_set_t *mask)
{
	int fd, rc;
	char file_path[PATH_MAX];
	char mstr[1 + CPU_SETSIZE * 4];

	if (mkdir(path, 0700) && (errno != EEXIST)) {
		error("%s: mkdir(%s): %m", __func__, path);
		return SLURM_ERROR;
	}

	/* Set "cpus" per user request */
	snprintf(file_path, sizeof(file_path), "%s/%scpus",
		 path, cpuset_prefix);
	_cpuset_to_cpustr(mask, mstr);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}
	rc = write(fd, mstr, strlen(mstr)+1);
	close(fd);
	if (rc < 1) {
		error("write(%s): %m", file_path);
		return SLURM_ERROR;
	}

	/* copy "mems" contents from parent directory, if it exists.
	 * "mems" must be set before any tasks can be added. */
	snprintf(file_path, sizeof(file_path), "%s/%smems",
		 base, cpuset_prefix);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
	} else {
		rc = read(fd, mstr, sizeof(mstr));
		close(fd);
		if (rc < 1) {
			error("read(%s): %m", file_path);
			return SLURM_ERROR;
		}
		snprintf(file_path, sizeof(file_path), "%s/%smems",
			 path, cpuset_prefix);
		fd = open(file_path, O_CREAT | O_WRONLY, 0700);
		if (fd < 0) {
			error("open(%s): %m", file_path);
			return SLURM_ERROR;
		}
		rc = write(fd, mstr, strlen(mstr)+1);
		close(fd);
		if (rc < 1) {
			error("write(%s): %m", file_path);
			return SLURM_ERROR;
		}
	}

	/* Delete cpuset once its tasks complete.
	 * Dependent upon system daemon. */
	snprintf(file_path, sizeof(file_path), "%s/notify_on_release", path);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}
	rc = write(fd, "1", 2);
	close(fd);
	if (rc < 1) {
		error("write(%s, %s): %m", file_path, mstr);
		return SLURM_ERROR;
	}

	/* Only now can we add tasks. */
	snprintf(file_path, sizeof(file_path), "%s/tasks", path);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}
	snprintf(mstr, sizeof(mstr), "%d", pid);
	rc = write(fd, mstr, strlen(mstr)+1);
	close(fd);
	if (rc < 1) {
		error("write(%s, %s): %m", file_path, mstr);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

int	slurm_get_cpuset(char *path, pid_t pid, size_t size, cpu_set_t *mask)
{
	int fd, rc;
	char file_path[PATH_MAX];
	char mstr[1 + CPU_SETSIZE * 4];

	snprintf(file_path, sizeof(file_path), "%s/%scpus",
		 path, cpuset_prefix);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("read(%s): %m", file_path);
		return SLURM_ERROR;
	}
	str_to_cpuset(mask, mstr);

	snprintf(file_path, sizeof(file_path), "%s/tasks", path);
	fd = open(file_path, O_CREAT | O_RDONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("read(%s): %m", file_path);
		return SLURM_ERROR;
	}

	/* FIXME: verify that pid is in mstr */

	return SLURM_SUCCESS;
}

#ifdef HAVE_NUMA
int	slurm_memset_available(void)
{
	char file_path[PATH_MAX];
	struct stat buf;

	snprintf(file_path, sizeof(file_path), "%s/%smems",
		 CPUSET_DIR, cpuset_prefix);
	return stat(file_path, &buf);
}

int	slurm_set_memset(char *path, nodemask_t *new_mask)
{
	char file_path[PATH_MAX];
	char mstr[1 + CPU_SETSIZE * 4], tmp[10];
	int fd, i, max_node;
	ssize_t rc;

	snprintf(file_path, sizeof(file_path), "%s/%smems",
		 path, cpuset_prefix);
	fd = open(file_path, O_CREAT | O_RDWR, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return SLURM_ERROR;
	}

	mstr[0] = '\0';
	max_node = numa_max_node();
	for (i=0; i<=max_node; i++) {
		if (!nodemask_isset(new_mask, i))
			continue;
		snprintf(tmp, sizeof(tmp), "%d", i);
		if (mstr[0])
			strcat(mstr, ",");
		strcat(mstr, tmp);
	}

	i = strlen(mstr) + 1;
	rc = write(fd, mstr, i+1);
	close(fd);
	if (rc <= i) {
		error("write(%s): %m", file_path);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}
#endif
