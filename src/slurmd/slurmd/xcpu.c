/*****************************************************************************\
 *  src/slurmd/slurmd/xcpu.c - xcpu-based process management functions
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#  include "config.h"
#endif

#ifdef HAVE_XCPU

#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"

/* Write a message to a given file name, return 1 on success, 0 on failure */
static int _send_sig(char *path, int sig, char *msg)
{
	int fd, len, rc = 0;

	fd = open(path, O_WRONLY | O_APPEND);
	if (fd == -1)
		return 0;

	if (sig == 0)
		rc = 1;
	else {
		debug2("%s to %s", msg, path);
		len = strlen(msg) + 1;
		write(fd, msg, len);
		rc = 1;
	}

	close(fd);
	return rc;
}

static char *_sig_name(int sig)
{
	static char name[8];

	switch(sig) {
	case SIGCONT:
		return "SIGCONT";
	case SIGKILL:
		return "SIGKILL";
	case SIGTERM:
		return "SIGTERM";
	default:
		snprintf(name, sizeof(name), "%d", sig);
		return name;
	}

}

/* Identify every XCPU process in a specific node and signal it.
 * Return the process count */
extern int xcpu_signal(int sig, char *nodes)
{
	int procs = 0;
	hostlist_t hl;
	char *node, sig_msg[64], dir_path[128], ctl_path[200];
	DIR *dir;
	struct dirent *sub_dir;

	/* Translate "nodes" to a hostlist */
	hl = hostlist_create(nodes);
	if (hl == NULL) {
		error("hostlist_create: %m");
		return 0;
	}

	/* Plan 9 only takes strings, so we map number to name */
	snprintf(sig_msg, sizeof(sig_msg), "signal %s",
		_sig_name(sig));

	/* For each node, look for processes */
	while ((node = hostlist_shift(hl))) {
		snprintf(dir_path, sizeof(dir_path), 
			"%s/%s/xcpu",
			XCPU_DIR, node);
		free(node);
		if ((dir = opendir(dir_path)) == NULL) {
			error("opendir(%s): %m", dir_path);
			continue;
		}
		while ((sub_dir = readdir(dir))) {
			snprintf(ctl_path, sizeof(ctl_path),
				"%s/%s/ctl",dir_path, 
				sub_dir->d_name);
			procs += _send_sig(ctl_path, sig, sig_msg);
		}
		closedir(dir);
	}

	hostlist_destroy(hl);
	return procs;
}

#else

extern int xcpu_signal(int sig, char *nodes)
{
	return 0;
}
#endif
