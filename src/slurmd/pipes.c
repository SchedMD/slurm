/*****************************************************************************\
 *  pipes.c -   
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <unistd.h>
#include <errno.h>

#include <src/common/slurm_errno.h>
#include <src/common/log.h>
#include <src/slurmd/pipes.h>
#include <src/slurmd/io.h>

void setup_parent_pipes(int *pipes)
{
	close(pipes[CHILD_IN_RD_PIPE]);
	close(pipes[CHILD_OUT_WR_PIPE]);
	close(pipes[CHILD_ERR_WR_PIPE]);
}

void cleanup_parent_pipes(int *pipes)
{
	close(pipes[CHILD_IN_WR_PIPE]);
	close(pipes[CHILD_OUT_RD_PIPE]);
	close(pipes[CHILD_ERR_RD_PIPE]);
}

int init_parent_pipes(int *pipes)
{
	int rc;

	/* open pipes to be used in dup after fork */
	if ((rc = pipe(&pipes[CHILD_IN_PIPE]))) 
		slurm_seterrno_ret(ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN);
	if ((rc = pipe(&pipes[CHILD_OUT_PIPE]))) 
		slurm_seterrno_ret(ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN);
	if ((rc = pipe(&pipes[CHILD_ERR_PIPE]))) 
		slurm_seterrno_ret(ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN);

	return SLURM_SUCCESS;
}

int setup_child_pipes(int *pipes)
{
	int error_code = SLURM_SUCCESS;

	/* dup stdin */
	/* close ( STDIN_FILENO ); */

	if (SLURM_ERROR ==
	    (error_code |= dup2(pipes[CHILD_IN_RD_PIPE], STDIN_FILENO))) {
		error("dup failed on child standard in pipe %d: %m",
		     pipes[CHILD_IN_RD_PIPE]);
	}
	close(pipes[CHILD_IN_RD_PIPE]);
	close(pipes[CHILD_IN_WR_PIPE]);

	/* dup stdout */
	/* close ( STDOUT_FILENO ); */
	if (SLURM_ERROR ==
	    (error_code |=
	     dup2(pipes[CHILD_OUT_WR_PIPE], STDOUT_FILENO))) {
		error("dup failed on child standard out pipe %i: %m",
				pipes[CHILD_OUT_WR_PIPE]);
	}
	close(pipes[CHILD_OUT_RD_PIPE]);
	close(pipes[CHILD_OUT_WR_PIPE]);

	/* dup stderr  */
	/* close ( STDERR_FILENO ); */
	if (SLURM_ERROR ==
	    (error_code |=
	     dup2(pipes[CHILD_ERR_WR_PIPE], STDERR_FILENO))) {
		error("dup failed on child standard err pipe %i: %m",
		     pipes[CHILD_ERR_WR_PIPE]);
	}
	close(pipes[CHILD_ERR_RD_PIPE]);
	close(pipes[CHILD_ERR_WR_PIPE]);
	return error_code;
}
