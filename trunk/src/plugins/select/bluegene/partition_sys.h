/*****************************************************************************\
 *  partition_sys.h - header for partition wiring component
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
#ifndef _PARTITION_SYS_H_
#define _PARTITION_SYS_H_

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <math.h>
#include <slurm/slurm.h>

#include "bluegene.h"

#define X_DIMENSION 8
#define Y_DIMENSION 4
#define Z_DIMENSION 4

/** 
 * structure for use by partitioning algorithm to refer to the
 * structural elements of the BGL partition system.
 */
typedef struct partition {
	int bl_coord[SYSTEM_DIMENSIONS]; /* bottom left coordinates */
	int tr_coord[SYSTEM_DIMENSIONS]; /* top right coordinates */
	ushort dimensions[SYSTEM_DIMENSIONS]; /* X,Y,Z dimensions */
	void* bgl_record_ptr;		/* pointer to referring bgl_record */
	int size;
	pm_partition_id_t *bgl_part_id;	/* ID returned from CMCS	*/
	ushort conn_type;	/* Type=Mesh/Torus/NAV		*/
	ushort node_use;	/* Use=Virtual/Coprocessor	*/
} partition_t;

extern int configure_switches(partition_t* partition);
extern int partition_sys(List requests);

extern void copy_partition(partition_t* rec_a, partition_t* rec_b);
extern void print_partition(partition_t* part);
extern void print_list(List list);
extern void print_sys_list(List list);

extern int is_not_correct_dimension(ushort* cur_part, ushort* req);
extern int is_partition_not_equals(partition_t* rec_a, partition_t* rec_b);
extern void rotate_part(const ushort* config, ushort** new_config);

extern int int_array_size(ushort* part_geometry);
extern void sort_int_array_by_dec_size(List configs);
extern void sort_partitions_by_inc_size(List partitions);
extern void sort_partitions_by_dec_size(List partitions);
extern void init_bgl_partition_num(void);

#endif /* _PARTITION_SYS_H_ */
