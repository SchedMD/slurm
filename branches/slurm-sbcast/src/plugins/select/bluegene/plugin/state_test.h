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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _STATE_TEST_H_
#define _STATE_TEST_H_

/* Determine if specific slurm node is already in DOWN or DRAIN state */
extern bool node_already_down(char *node_name);

/*
 * Search MMCS for failed switches and nodes. Failed resources are DRAINED in 
 * SLURM. This relies upon rm_get_BGL(), which is slow (10+ seconds) so run 
 * this test infrequently.
 */
extern void test_mmcs_failures(void);

#endif /* _STATE_TEST_H_ */
