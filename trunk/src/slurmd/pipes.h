/*****************************************************************************\
 *  pipes.h - headers for slurmd pipes (pipes.c)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _SLURMD_PIPES_H_
#define _SLURMD_PIPES_H_

/*pipes.c*/
/* init_parent_pipes
 * initializes pipes in the parent to be used for child io ipc after fork and exec
 * IN pipes	- array of six file desciptors
 * OUT int	- return_code
 */
int init_parent_pipes(int *pipes);

/* setup_parent_pipes 
 * setups the parent side of the pipes after fork 
 * IN pipes	- array of six file desciptors
 */
void setup_parent_pipes(int *pipes);


/* setup_child_pipes
 * setups the child side of the pipes after fork
 * IN pipes	- array of six file desciptors
 * OUT int	- return_code
 */
int setup_child_pipes(int *pipes);

/* cleanup_parent_pipes
 * cleans up the parent side of the pipes after task exit
 * IN pipes	- array of six file desciptors
 */
void cleanup_parent_pipes(int *pipes);

#endif /* !_SLURMD_PIPES_H */
