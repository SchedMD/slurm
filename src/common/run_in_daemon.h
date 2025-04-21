/*****************************************************************************\
 *  run_in_daemon.h - functions to determine if you are a given daemon or not
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

/* Must be defined in each daemon to override the symbol from libslurm. */
extern uint32_t slurm_daemon;

#define IS_SLURMDBD SLURM_BIT(1)
#define IS_SLURMSCRIPTD SLURM_BIT(2)
#define IS_SLURMCTLD SLURM_BIT(3)
#define IS_SLURMD SLURM_BIT(4)
#define IS_SLURMSTEPD SLURM_BIT(5)
#define IS_SACKD SLURM_BIT(6)
#define IS_SLURMRESTD SLURM_BIT(7)
#define IS_ANY_DAEMON 0xFFFFFFFF

/*
 * Determine if calling process is in bitmask of daemons
 */
extern bool run_in_daemon(uint32_t daemons);

#define running_in_daemon() run_in_daemon(IS_ANY_DAEMON)
#define running_in_sackd() run_in_daemon(IS_SACKD)
#define running_in_slurmctld() run_in_daemon(IS_SLURMCTLD)
#define running_in_slurmd() run_in_daemon(IS_SLURMD)
#define running_in_slurmdbd() run_in_daemon(IS_SLURMDBD)
#define running_in_slurmd_stepd() run_in_daemon(IS_SLURMD | IS_SLURMSTEPD)
#define running_in_slurmrestd() run_in_daemon(IS_SLURMRESTD)
#define running_in_slurmstepd() run_in_daemon(IS_SLURMSTEPD)

#define error_in_daemon(fmt, ...)		\
do {						\
	if (running_in_daemon())		\
		error(fmt, ##__VA_ARGS__);	\
	else					\
		verbose(fmt, ##__VA_ARGS__);	\
} while (false)

#endif
