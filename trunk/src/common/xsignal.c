/*****************************************************************************\
 * src/common/xsignal.c - POSIX signal functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#include <errno.h>
#include <signal.h>

#include <src/common/log.h>
#include <src/common/slurm_errno.h>


void
xsignal(int signo, void (*handler)(int))
{
	struct sigaction sa, old_sa;
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, signo);
	sa.sa_flags = 0;
	sigaction(signo, &sa, &old_sa);
	return;
}

int
unblock_all_signals(void)
{
	sigset_t set;
	if (sigfillset(&set)) {
		error("sigfillset: %m");
		return SLURM_ERROR;
	}
	if (sigprocmask(SIG_UNBLOCK, &set, NULL)) {
		error("sigprocmask: %m");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}
