/*****************************************************************************\
 *  block_allocator.h
 *
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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

#ifndef _BLOCK_ALLOCATOR_H_
#define _BLOCK_ALLOCATOR_H_

#include "src/common/node_select.h"
#include "../bridge_linker.h"
#include "../ba_common.h"

// #define DEBUG_PA
#define BIG_MAX 9999
#define BUFSIZE 4096

#define NUM_PORTS_PER_NODE 6

enum {X, Y, Z};

/* Global */
extern my_bluegene_t *bg;

extern ba_mp_t ***ba_main_grid;

/* If emulating a system set up a known configuration for wires in a
 * system of the size given.
 * If a real bluegene system, query the system and get all wiring
 * information of the system.
 */
extern void init_wires(void);

/*
 * get the used wires for a block out of the database and return the
 * node list.  The block_ptr here must be gotten with bridge_get_block
 * not bridge_get_block_info, if you are looking to recover from
 * before.  If you are looking to start clean it doesn't matter.
 */
extern List get_and_set_block_wiring(char *bg_block_id,
				     void *block_ptr);

#endif /* _BLOCK_ALLOCATOR_H_ */
