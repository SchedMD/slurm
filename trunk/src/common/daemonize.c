/*****************************************************************************\
 * daemonize.c - daemonization routine
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>

#include <src/common/macros.h>
#include <src/common/log.h>

/* closeall FDs >= a specified value */
static void
closeall(int fd)
{
	int fdlimit = sysconf(_SC_OPEN_MAX);

	while (fd < fdlimit) 
		close(fd++);
}

/* detach and go into background.
 * caller is responsible for umasks
 *
 * if nochdir == 0, will do a chdir to /
 * if noclose == 0, will close all FDs
 */
int
daemon(int nochdir, int noclose)
{
	switch (fork()) {
		case  0 : break;        /* child */
		case -1 : return -1;
		default : _exit(0);     /* exit parent */
	}

	if (setsid() < 0)
		return -1;

	switch (fork()) {
		case 0 : break;         /* child */
		case -1: return -1;
		default: _exit(0);      /* exit parent */
	}

	if(!nochdir && chdir("/") < 0)
		fatal("chdir(/): %m");

	if (!noclose) {
		closeall(0);
		open("/dev/null", O_RDWR);
		dup(0);
		dup(0);
	}
	return 0;

}
