/*****************************************************************************\
 * src/slurmd/daemonize.h - function definition for making a daemon
 * $Id$
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


#ifndef _HAVE_DAEMONIZE_H
#define _HAVE_DAEMONIZE_H

/* fork process into background and inherit new session
 * if nochdir is 0, performs a chdir("/")
 * if noclose is 0, closes all fds and dups stdout/err of daemon onto /dev/null
 *
 * returns -1 on error.
 */
int daemon(int nochdir, int noclose);

#endif /* !_HAVE_DAEMONIZE_H */
