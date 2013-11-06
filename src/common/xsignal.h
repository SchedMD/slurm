/*****************************************************************************\
 * src/common/xsignal.h - POSIX signal wrapper functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
