/*****************************************************************************\
 * src/slurmd/common/run_script.h - code shared between slurmd and slurmstepd
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _RUN_SCRIPT_H
#define _RUN_SCRIPT_H

#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>

/*
 *  Same as waitpid(2) but kill process group for pid after timeout secs.
 *   name    IN: name or class of program we're waiting on (for log messages)
 *   pid     IN: child on which to call waitpid(2)
 *   pstatus IN: pointer to integer status
 *   timeout IN: timeout in seconds
 *
 *  Returns 0 for valid status in pstatus, -1 on failure of waitpid(2).
 */
int waitpid_timeout (const char *name, pid_t pid, int *pstatus, int timeout);

/*
 * Run a prolog or epilog script (does NOT drop privileges)
 * name IN: class of program (prolog, epilog, etc.),
 * path IN: pathname of program to run
 * jobid IN: info on associated job
 * max_wait IN: maximum time to wait in seconds, -1 for no limit
 * env IN: environment variables to use on exec, sets minimal environment 
 *	if NULL
 * uid IN: user ID of job owner
 * RET 0 on success, -1 on failure.
 */
int run_script(const char *name, const char *path, uint32_t jobid, 
	       int max_wait, char **env, uid_t uid);

#endif /* _RUN_SCRIPT_H */
