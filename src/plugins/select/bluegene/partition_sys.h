/*****************************************************************************\
 *  partition_sys.h
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
#ifndef _PARTITION_SYS_H_
#define _PARTITION_SYS_H_

#include <math.h>

#define SYSTEM_DIMENSIONS 3
#define X_DIMENSION 8
#define Y_DIMENSION 4
#define Z_DIMENSION 4

/** 
 * structure for use by partitioning algorithm to refer to the
 * structural elements of the BGL partition system.
 */
typedef struct partition{
	int bl_coord[SYSTEM_DIMENSIONS]; /* bottom left coordinates */
	int tr_coord[SYSTEM_DIMENSIONS]; /* top right coordinates */
	ushort dimensions[SYSTEM_DIMENSIONS]; /* X,Y,Z dimensions */
	void* bgl_record_ptr;		/* pointer to referring bgl_record */
	int size;
#ifdef _RM_API_H__
	pm_partition_id_t* bgl_part_id;	/* ID returned from CMCS	*/

#else
	ushort* bgl_part_id;	/* ID returned from CMCS	*/
	ushort* part_type;	/* Type=Mesh/Torus/		*/
#endif
} partition_t;

int configure_switches(partition_t* partition);
int partition_sys(List requests);

void copyPartition(partition_t* A, partition_t* B);
void printPartition(partition_t* part);
void printList(List list);
void printSysList(List list);

int isNotCorrectDimension(ushort* cur_part, ushort* req);
int isPartitionNotEquals(partition_t* A, partition_t* B);
void rotate_part(const ushort* config, ushort** new_config);

int intArray_size(ushort* part_geometry);
void sortIntArrayByDecSize(List configs);
void sortPartitionsByIncSize(List partitions);
void sortPartitionsByDecSize(List partitions);


#endif /* _PARTITION_SYS_H_ */
