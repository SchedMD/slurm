/*****************************************************************************\
 *  bgl_switch_connections.c
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>
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

#ifndef _BGL_SWITCH_CONNECTIONS_H_
#define _BGL_SWITCH_CONNECTIONS_H_

/**
 * connect the given switch up in the "A" pattern
 *       0  1
 *    /--|--|--\
 *    |  /  \  |
 *  2 --/    \-- 5
 *    |  /--\  |
 *    \__|__|__/
 *       3  4
 */
extern void connect_switch_A(rm_BGL_t *bgl, rm_partition_t *my_part, 
				rm_switch_t *my_switch, int first);

/**
 * connect the given switch up in the "B" pattern
 *       0  1
 *    /--|--|--\
 *    |  \  /  |
 *  2 ----\/---- 5
 *    |   /\   |
 *    \__|__|__/
 *       3  4
 */
extern void connect_switch_B(rm_BGL_t *bgl, rm_partition_t *my_part, 
				rm_switch_t *my_switch, int first);

/**
 * connect the given switch up in the "C" pattern
 *       0  1
 *    /--|--|--\
 *    |  \  \  |
 *  2 --\ \  \-- 5
 *    |  \ \   |
 *    \__|__|__/
 *       3  4
 */
extenn void connect_switch_C(rm_BGL_t *bgl, rm_partition_t *my_part, 
				rm_switch_t *my_switch, int first);

/**
 * connect the given switch up in the "D" pattern
 *       0  1
 *    /--|--|--\
 *    |  /  /  |
 *  2 --/  / /-- 5
 *    |   / /  |
 *    \__|__|__/
 *       3  4
 */
extern void connect_switch_D(rm_BGL_t *bgl, rm_partition_t *my_part, 
				rm_switch_t *my_switch, int first);

/**
 * connect the given switch up in the "E" pattern (loopback)
 *       0  1
 *    /--|--|--\
 *    |  \__/  |
 *  2 ---------- 5
 *    |  /--\  |
 *    \__|__|__/
 *       3  4
 */
extern void connect_switch_E(rm_BGL_t *bgl, rm_partition_t *my_part, 
				rm_switch_t *my_switch, int first);

/**
 * connect the given switch up in the "F" pattern (loopback)
 *       0  1
 *    /--|--|--\
 *    |  \__/  |
 *  2 --\    /-- 5
 *    |  \  /  |
 *    \__|__|__/
 *       3  4
 */
extern void connect_switch_F(rm_BGL_t *bgl, rm_partition_t *my_part, 
				rm_switch_t *my_switch, int first);

/**
 * connect the node to the next node (higher up number)
 *       0  1
 *    /--|--|--\
 *    |    /   |
 *  2 -   /    - 5
 *    |  /     |
 *    \__|__|__/
 *       3  4
 */
extern void connect_next(rm_partition_t *my_part, rm_switch_t *my_switch);

/**
 * connect the given switch up to the previous node
 *       0  1
 *    /--|--|--\
 *    |  \     |
 *  2 -   \    - 5
 *    |    \   |
 *    \__|__|__/
 *       3  4
 */
extern void connect_prev(rm_partition_t *my_part, rm_switch_t *my_switch);

#endif /* _BGL_SWITCH_CONNECTIONS_H_ */
