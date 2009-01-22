/*
 * set_oomadj.c - prevent slurmd/slurmstepd from being killed by the
 *	kernel OOM killer
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "src/common/log.h"

extern int set_oom_adj(int adj)
{
	int fd;
	char oom_adj[16];

	fd = open("/proc/self/oom_adj", O_WRONLY);
	if (fd < 0) {
		if (errno != ENOENT)
			error("failed to open /proc/self/oom_adj: %m");
		return -1;
	}
	if (snprintf(oom_adj, 16, "%d", adj) >= 16) {
		return -1;
	}
	while ((write(fd, oom_adj, strlen(oom_adj)) < 0) && (errno == EINTR))
		;
	close(fd);
	return 0;
}


