/*****************************************************************************\
 *  run_in_daemon.h - functions to determine if you are a given daemon or not
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
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

#ifndef _HAVE_RUN_IN_DAEMON_H
#define _HAVE_RUN_IN_DAEMON_H

/* Determine slurm_prog_name (calling process) is in list of daemons
 *
 * IN/OUT - run - set to true if running and false if not
 * IN/OUT - set - set to true after the first run.
 * IN - daemons (comma separated list of daemons i.e. slurmd,slurmstepd
 * returns true if slurm_prog_name (set in log.c) is in list, false otherwise.
 */
extern bool run_in_daemon(bool *run, bool *set, char *daemons);

/* check if running in a daemon */
extern bool running_in_daemon(void);

/* check if running in the slurmctld */
extern bool running_in_slurmctld(void);

/* call this if you don't want the cached value for running_in_slurmctld */
extern bool running_in_slurmctld_reset(void);

/* check if running in the slurmd */
extern bool running_in_slurmd(void);

/* check if running in the slurmdbd */
extern bool running_in_slurmdbd(void);

/* check if running in the slurmd or slurmstepd */
extern bool running_in_slurmd_stepd(void);

/* check if running in the slurmrestd */
extern bool running_in_slurmrestd(void);

/* check if running in the slurmstepd */
extern bool running_in_slurmstepd(void);

#endif
