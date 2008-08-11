/*****************************************************************************\
 *  salloc.h - definitions for srun option processing
 *  $Id: opt.h 8700 2006-07-26 01:12:40Z morrone $
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Christopher J. Morrone <morrone2@llnl.gov>
 *  LLNL-CODE-402394.
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

#ifndef _HAVE_SALLOC_H
#define _HAVE_SALLOC_H

#include <sys/types.h>
#include <unistd.h>

extern char **command_argv;
extern int command_argc;
extern pid_t command_pid;

enum possible_allocation_states {NOT_GRANTED, GRANTED, REVOKED};
extern enum possible_allocation_states allocation_state;
extern pthread_mutex_t allocation_state_lock;

#endif	/* _HAVE_SALLOC_H */
