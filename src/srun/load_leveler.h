/*****************************************************************************\
 *  load_leveler.h - Provide an srun command line interface to POE using a
 *  front-end/back-end process.
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD <http://www.schedmd.com>.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef SRUN_LOADLEVELER_H
#define SRUN_LOADLEVELER_H 1

#ifdef USE_LOADLEVELER

/* Build a POE command line based upon srun options (using global variables) */
extern char *build_poe_command(char *job_id);

/*
 * srun_back_end - Open stdin/out/err socket connections to communicate with
 *	the srun command that spawned this one, forward its stdin/out/err
 *	communications back, forward signals, and return the program's exit
 *	code.
 *
 * argc IN - Count of elements in argv
 * argv IN - [0]:  Our executable name (e.g. srun)
 *	     [1]:  "--srun-be" (argument to spawn srun backend)
 *	     [2]:  Hostname or address of front-end
 *	     [3]:  Port number for stdin/out
 *	     [4]:  Port number for stderr
 *	     [5]:  Port number for signals/exit status
 *	     [6]:  Authentication key
 *	     [7]:  Program to be spawned for user
 *	     [8+]: Arguments to spawned program
 * RETURN - remote processes exit code
 */
extern int srun_back_end (int argc, char **argv);

/*
 * srun_front_end - Open stdin/out/err socket connections to communicate with
 *	a remote node process and spawn a remote job to claim that connection
 *	and execute the user's command.
 *
 * cmd_line IN - Command execute line
 * srun_alloc IN - TRUE if this srun commanmd created the job allocation
 * RETURN - remote processes exit code or -1 if some internal error
 */
extern int srun_front_end (char *cmd_line, bool srun_alloc);

/*
 * srun front-end signal processing function, send a signal to back-end
 *	program
 * sig_num IN - signal to send
 * RETURN 0 on success, -1 on error
 */
extern int srun_send_signal(int sig_num);

#endif	/* USE_LOADLEVELER */
#endif	/* SRUN_LOADLEVELER_H */
