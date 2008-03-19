/*****************************************************************************\
 * src/slurmd/common/run_script.c - code shared between slurmd and slurmstepd
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <string.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"

#include "src/slurmd/common/run_script.h"


/*
 * Run a prolog or epilog script (does NOT drop privileges)
 * name IN: class of program (prolog, epilog, etc.),
 * path IN: pathname of program to run
 * jobid IN: info on associated job
 * max_wait IN: maximum time to wait in seconds, -1 for no limit
 * env IN: environment variables to use on exec, sets minimal environment 
 *	if NULL
 * RET 0 on success, -1 on failure.
 */
int
run_script(const char *name, const char *path, uint32_t jobid,
	   int max_wait, char **env)
{
	int status, rc, opt;
	pid_t cpid;

	xassert(env);
	if (path == NULL || path[0] == '\0')
		return 0;

	if (jobid) {
		debug("[job %u] attempting to run %s [%s]", 
			jobid, name, path);
	} else
		debug("attempting to run %s [%s]", name, path);

	if (access(path, R_OK | X_OK) < 0) {
		debug("Not running %s [%s]: %m", name, path);
		return 0;
	}

	if ((cpid = fork()) < 0) {
		error ("executing %s: fork: %m", name);
		return -1;
	}
	if (cpid == 0) {
		char *argv[2];

		argv[0] = (char *)xstrdup(path);
		argv[1] = NULL;

#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execve(path, argv, env);
		error("execve(): %m");
		exit(127);
	}

	if (max_wait < 0)
		opt = 0;
	else
		opt = WNOHANG;

	while (1) {
		rc = waitpid(cpid, &status, opt);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			error("waidpid: %m");
			return 0;
		} else if (rc == 0) {
			sleep(1);
			if ((--max_wait) == 0) {
				killpg(cpid, SIGKILL);
				opt = 0;
			}
		} else  {
			killpg(cpid, SIGKILL);	/* kill children too */
			return status;
		}
	}

	/* NOTREACHED */
}
