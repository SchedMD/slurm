/*****************************************************************************\
 *  state_test.h - header for Blue Gene node and switch state test. 
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> et. al.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifndef _STATE_TEST_H_
#define _STATE_TEST_H_

/* Determine if specific slurm node is already in DOWN or DRAIN ret (1) or
 * FAIL ret (2) state idle ret (0) */
extern int node_already_down(char *node_name);

/*
 * Search MMCS for failed switches and nodes. Failed resources are DRAINED in 
 * SLURM. This relies upon rm_get_BGL(), which is slow (10+ seconds) so run 
 * this test infrequently.
 */
extern void test_mmcs_failures(void);

/*
 * Search MMCS for failed switches and nodes inside of block. 
 * Failed resources are DRAINED in SLURM. This relies upon rm_get_partition(), 
 */
extern int check_block_bp_states(char *bg_block_id);

#endif /* _STATE_TEST_H_ */
