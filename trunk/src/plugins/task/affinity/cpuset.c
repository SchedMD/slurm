/*****************************************************************************\
 *  cpuset.c - Library for interacting with /dev/cpuset file system
 *****************************************************************************
 *  Copyright (C) 2007 Bull
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Written by Don Albert <Don.Albert@Bull.com> and 
 *             Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

static void _cpuset_to_cpustr(const cpu_set_t *mask, char *str)
{
	int i;
	char tmp[16];

	str[0] = '\0';
	for (i=0; i<CPU_SETSIZE; i++) {
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
		error("mkdir(%s): %m", path);
		return -1;
	}
	if (chown(path, uid, gid))
		error("chown(%s): %m", path);

	/* Copy "cpus" contents from parent directory
	 * "cpus" must be set before any tasks can be added. */
	snprintf(file_path, sizeof(file_path), "%s/cpus", base);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("read(%s): %m", file_path);
		return -1;
	}
	snprintf(file_path, sizeof(file_path), "%s/cpus", path);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = write(fd, mstr, rc);
	close(fd);
	if (rc < 1) {
		error("write(%s): %m", file_path);
		return -1;
	}

	/* Copy "mems" contents from parent directory, if it exists.
	 * "mems" must be set before any tasks can be added. */
	snprintf(file_path, sizeof(file_path), "%s/mems", base);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
	} else {
		rc = read(fd, mstr, sizeof(mstr));
		close(fd);
		if (rc < 1)
			error("read(%s): %m", file_path);
		snprintf(file_path, sizeof(file_path), "%s/mems", path);
		fd = open(file_path, O_CREAT | O_WRONLY, 0700);
		if (fd < 0) {
			error("open(%s): %m", file_path);
			return -1;
		}
		rc = write(fd, mstr, rc);
		close(fd);
		if (rc < 1)
			error("write(%s): %m", file_path);
	}

	/* Delete cpuset once its tasks complete.
	 * Dependent upon system daemon. */
	snprintf(file_path, sizeof(file_path), "%s/notify_on_release", path);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = write(fd, "1", 2);
	close(fd);

	/* Only now can we add tasks.
	 * We can't add self, so add tasks after exec. */

	return 0;
}

int	slurm_set_cpuset(char *base, char *path, pid_t pid, size_t size, 
		const cpu_set_t *mask)
{
	int fd, rc;
	char file_path[PATH_MAX];
	char mstr[1 + CPU_SETSIZE * 4];

	if (mkdir(path, 0700) && (errno != EEXIST)) {
		error("mkdir(%s): %m", path);
		return -1;
	}

	/* Set "cpus" per user request */
	snprintf(file_path, sizeof(file_path), "%s/cpus", path);
	_cpuset_to_cpustr(mask, mstr);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = write(fd, mstr, strlen(mstr)+1);
	close(fd);
	if (rc < 1) {
		error("write(%s): %m", file_path);
		return -1;
	}

	/* copy "mems" contents from parent directory, if it exists. 
	 * "mems" must be set before any tasks can be added. */
	snprintf(file_path, sizeof(file_path), "%s/mems", base);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
	} else {
		rc = read(fd, mstr, sizeof(mstr));
		close(fd);
		if (rc < 1)
			error("read(%s): %m", file_path);
		snprintf(file_path, sizeof(file_path), "%s/mems", path);
		fd = open(file_path, O_CREAT | O_WRONLY, 0700);
		if (fd < 0) {
			error("open(%s): %m", file_path);
			return -1;
		}
		rc = write(fd, mstr, rc);
		close(fd);
		if (rc < 1)
			error("write(%s): %m", file_path);
	}

	/* Delete cpuset once its tasks complete.
	 * Dependent upon system daemon. */
	snprintf(file_path, sizeof(file_path), "%s/notify_on_release", path);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = write(fd, "1", 2);
	close(fd);

	/* Only now can we add tasks. */
	snprintf(file_path, sizeof(file_path), "%s/tasks", path);
	fd = open(file_path, O_CREAT | O_WRONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	snprintf(mstr, sizeof(mstr), "%d", pid);
	rc = write(fd, mstr, strlen(mstr)+1);
	close(fd);
	if (rc < 1) {
		error("write(%s, %s): %m", file_path, mstr);
		return -1;
	}

	return 0;
}

int	slurm_get_cpuset(char *path, pid_t pid, size_t size, cpu_set_t *mask)
{
	int fd, rc;
	char file_path[PATH_MAX];
	char mstr[1 + CPU_SETSIZE * 4];

	snprintf(file_path, sizeof(file_path), "%s/cpus", path);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("read(%s): %m", file_path);
		return -1;
	}
	str_to_cpuset(mask, mstr);

	snprintf(file_path, sizeof(file_path), "%s/tasks", path);
	fd = open(file_path, O_CREAT | O_RDONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("read(%s): %m", file_path);
		return -1;
	}

	/* FIXME: verify that pid is in mstr */

	return 0;
}

#ifdef HAVE_NUMA
int	slurm_memset_available(void)
{
	char file_path[PATH_MAX];
	struct stat buf;

	snprintf(file_path, sizeof(file_path), "%s/mems", CPUSET_DIR);
	return stat(file_path, &buf);
}

int	slurm_set_memset(char *path, nodemask_t *new_mask)
{
	char file_path[PATH_MAX];
	char mstr[1 + CPU_SETSIZE * 4], tmp[10];
	int fd, i, max_node;
	ssize_t rc;

	snprintf(file_path, sizeof(file_path), "%s/mems", CPUSET_DIR);
	fd = open(file_path, O_CREAT | O_RDONLY, 0700);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
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
		return -1;
	}
	return 0;
}
#endif
