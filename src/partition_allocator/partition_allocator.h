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

#include "src/common/list.h"
#include "src/common/bitstring.h"
#include "src/common/macros.h"

struct pa_request;
/**
 * create a partition request.  Note that if the geometry is given,
 * then size is ignored.  If elongate is true, the algorithm will try
 * to fit that a partition of cubic shape and then it will try other
 * elongated geometries.  (ie, 2x2x2 -> 4x2x1 -> 8x1x1). Note that
 * size must be a power of 2, given 3 dimensions.
 * 
 * OUT - pa_request: structure to allocate and fill in.  
 * IN - geometry: requested geometry of partition
 * IN - size: requested size of partition
 * IN - rotate: if true, allows rotation of partition during fit
 * IN - elongate: if true, will try to fit different geometries of
 *      same size requests
 * IN - conn_type: connection type of request (TORUS or MESH)
 * IN - contig: enforce contiguous regions constraint
 * 
 * return success of allocation/validation of params
 */
int new_pa_request(struct pa_request** pa_request, 
		    int* geometry, int size, 
		    bool rotate, bool elongate, 
		    bool force_contig, int conn_type);

/**
 * delete a partition request 
 */
void delete_pa_request(struct pa_request* pa_request);

/**
 * print a partition request 
 */
void print_pa_request(struct pa_request* pa_request);

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
 * Try to allocate a partition.
 * 
 * IN - pa_request: allocation request
 * OUT - results: List of results of the allocation request.  Each
 * list entry will be a coordinate.  allocate_part will create the
 * list, but the caller must destroy it.
 * 
 * return: success or error of request
 */
int allocate_part(struct pa_request* pa_request, List* results);

/** 
 * Doh!  Admin made a boo boo.  Note: Undo only has one history
 * element, so two consecutive undo's will fail.
 */
void undo_last_allocatation();


#endif /* _PARTITION_ALLOCATOR_H_ */
