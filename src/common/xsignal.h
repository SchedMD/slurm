/*****************************************************************************\
 * src/common/xsignal.h - POSIX signal wrapper functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-217948.
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

#ifndef _XSIGNAL_H
#define _XSIGNAL_H

#include <signal.h>

typedef void SigFunc(int);

/* 
 * Install a signal handler in the POSIX way, but with BSD signal() semantics
 */
SigFunc *xsignal(int signo, SigFunc *);

/*
 * Save current set of blocked signals into `set'
 */
int xsignal_save_mask(sigset_t *set);

/*
 *  Set the mask of blocked signals to exactly `set'
 */
int xsignal_set_mask(sigset_t *set);

/*
 *  Add the list of signals given in the signal array `sigarray' 
 *   to the current list of signals masked in the process.
 *
 *   sigarray is a zero-terminated array of signal numbers, 
 *   e.g. { SIGINT, SIGTERM, ... , 0 } 
 *
 *  Returns SLURM_SUCCESS or SLURM_ERROR.
 *
 */
int xsignal_block(int sigarray[]);

/*
 *  As xsignal_block() above, but instead remove the list of signals
 *    from the threads signal mask.
 */
int xsignal_unblock(int sigarray[]);

/*
 *  Create a sigset_t from a sigarray
 */
int xsignal_sigset_create(int sigarray[], sigset_t *setp);

#endif /* !_XSIGNAL_H */
