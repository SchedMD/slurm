/*****************************************************************************\
 *  set_oomadj.c - prevent slurmd/slurmstepd from being killed by the
 *	kernel OOM killer
 *****************************************************************************
 *  Written by Hongjia Cao, National University of Defense Technology, China.
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
	char *oom_adj_file = "/proc/self/oom_score_adj";

	fd = open(oom_adj_file, O_WRONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			debug("%s not found. Falling back to oom_adj",
			      oom_adj_file);
			oom_adj_file = "/proc/self/oom_adj";
			fd = open(oom_adj_file, O_WRONLY);
			if (fd < 0) {
				if (errno == ENOENT)
					error("%s not found", oom_adj_file);
				else
					error("failed to open %s: %m",
					      oom_adj_file);
				return -1;
			}
			/* Convert range from [-1000,1000] to [-17,15]
			 * for use with older Linux kernel before 2.6.36 */
			if (adj < 0)
				adj = (adj * 17) / 1000;
			else if (adj > 0)
				adj = (adj * 15) / 1000;
		} else {
			error("failed to open %s: %m", oom_adj_file);
			return -1;
		}
	}
	if (snprintf(oom_adj, 16, "%d", adj) >= 16) {
		close(fd);
		return -1;
	}
	while ((write(fd, oom_adj, strlen(oom_adj)) < 0) && (errno == EINTR))
		;
	close(fd);

	return 0;
}
