/*****************************************************************************\
 *  partition_allocator.h
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

#ifndef _PARTITION_ALLOCATOR_H_
#define _PARTITION_ALLOCATOR_H_

#include "src/common/bitstring.h"
#include "src/common/macros.h"

/**
 * Initialize internal structures by either reading previous partition
 * configurations from a file or by running the graph solver.
 * 
 * IN: dunno yet, probably some stuff denoting downed nodes, etc.
 * 
 * return: success or error of the intialization.
 */
void init();
/** 
 * destroy all the internal (global) data structs.
 */
void fini();

/** 
 * set the node in the internal configuration as unusable
 * 
 * IN c: coordinate of the node to put down
 */
void set_node_down(int* c);

/** 
 * Try to allocate a partition of the given size.  If elongate is
 * true, the algorithm will try to fit that a partition of cubic shape
 * and then it will try other elongated geometries.  
 * (ie, 2x2x2 -> 4x2x1 -> 8x1x1)
 * 
 * Note that size must be a power of 2, given 3 dimensions.
 * 
 * IN - size: requested size of partition
 * IN - elongate: if true, will try to fit different geometries of
 *      same size requests
 * OUT - bitmap: bitmap of the partition allocated
 * 
 * return: success or error of request
 */
int allocate_part_by_size(int size, bool elongate, bitstr_t** bitmap);

/** 
 * Try to allocate a partition of the given geometery.  This function
 * is more flexible than allocate_part_by_size by allowing
 * configurations that are restricted by the power of 2 restriction.
 * 
 * IN - size: requested size of partition
 * IN - rotate: if true, allows rotation of partition during fit
 * OUT - bitmap: bitmap of the partition allocated
 * 
 * return: success or error of request
 */
int allocate_part_by_geometry(int* geometry, bool rotate, bitstr_t** bitmap);


#endif /* _PARTITION_ALLOCATOR_H_ */
