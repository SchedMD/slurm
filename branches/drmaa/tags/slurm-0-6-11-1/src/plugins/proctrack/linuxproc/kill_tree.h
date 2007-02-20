/*****************************************************************************\
 *  kill_tree.h - Kill process tree based upon process IDs
 *	Used primarily for MPICH-GM
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Takao Hatazaki <takao.hatazaki@hp.com>
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

#ifndef _HAVE_KILL_TREE_H
#define _HAVE_KILL_TREE_H

#include <sys/types.h>

extern int kill_proc_tree(pid_t top, int sig);
extern pid_t find_ancestor(pid_t process, char *process_name);
/*
 * Some of processes may not be in the same process group
 * (e.g. GMPI processes).  So, find out the process tree,
 * then kill all that subtree.
 */

#endif  /* _HAVE_KILL_TREE_H */
