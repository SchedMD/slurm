/*****************************************************************************\
 *  load_leveler.h - LoadLeveler lacks the ability to spawn an interactive
 *  job like SLURM. The following functions provide an interface between an
 *  salloc front-end process and a back-end process spawned as a batch job.
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

#ifdef USE_LOADLEVELER

/*
 * salloc_back_end - Open stdin/out/err socket connections to communicate with
 *	the salloc or srun command that submitted this program as a LoadLeveler
 *	batch job, spawn the identified user program, forward its stdin/out/err
 *	communications back, forward signals, and return the program's exit
 *	code.
 *
 * argc IN - Count of elements in argv
 * argv IN - [0]:  Our executable name (e.g. salloc)
 *	     [1]:  Hostname or address of front-end
 *	     [2]:  Port number for stdin/out
 *	     [3]:  Port number for stderr
 *	     [4]:  Port number for signals/exit status
 *	     [5]:  Program to be spawned for user
 *	     [6+]: Arguments to spawned program
 * RETURN - remote processes exit code
 */
extern int salloc_back_end (int argc, char **argv);

/*
 * salloc_front_end - Open stdin/out/err socket connections to communicate with
 *	a remote node process and submit a batch job to claim that connection
 *	and execute the user's command.
 *
 * argc IN - Count of elements in argv
 * argv IN - [0]:  Our executable name (e.g. salloc)
 *	     [1]:  Program to be spawned for user
 *	     [2+]: Arguments to spawned program
 * RETURN - remote processes exit code or -1 if some internal error
 */
extern int salloc_front_end (int argc, char **argv);

/*
 * salloc front-end signal processing function, send a signal to back-end
 *	program
 * sig_num IN - signal to send
 * RETURN 0 on success, -1 on error
*/
extern int salloc_send_signal(int sig_num);



#endif	/* USE_LOADLEVELER */
