#ifndef _BLUEGENE_H_
#define _BLUEGENE_H_

/*****************************************************************************\
 *  bluegene.h - header for bgl node allocation plugin. 
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
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

#include "src/common/bitstring.h"
#include "src/slurmctld/slurmctld.h"
#ifndef _HOSTLIST_H
#include "src/common/hostlist.h"
#endif

// #include "rm_api.h"
#ifndef _RM_API_H__
typedef int pm_partition_id_t;
typedef int rm_partition_t;
#else 
rm_BGL_t *bgl;
#endif

List slurm_part_list;			/* cached copy of slurm's part_list */
List bgl_list;				/* list of bgl_record entries */
List bgl_conf_list;			/* list of bgl_conf_record entries */
typedef int lifecycle_type_t;
enum part_lifecycle {DYNAMIC, STATIC};

typedef struct bgl_record {
	int used;
	char* slurm_part_id;		/* ID specified by admins	*/
	pm_partition_id_t* bgl_part_id;	/* ID returned from CMCS	*/
	char* nodes;			/* String of nodes in partition */
	lifecycle_type_t part_lifecycle;/* either STATIC or DYNAMIC	*/
	hostlist_t* hostlist;		/* expanded form of hosts */
	bitstr_t *bitmap;		/* bitmap of nodes for this partition */
	struct partition* alloc_part;       /* the allocated partition   */
	int size;			/* node count for the partitions */
	rm_partition_t* part_type;	/* Type=Mesh/Torus/		*/
} bgl_record_t;

/** 
 * bgl_conf_record is used to store the elements read from the config
 * file from init().
 */
typedef struct bgl_conf_record{
	char* nodes;
	rm_partition_t* part_type;
} bgl_conf_record_t;

/** 
 * process the configuration file so to interpret what partitions are
 * static, dynamic, etc.
 * 
 */
int read_bgl_conf();
/** */
int init_bgl();

int create_static_partitions();
/** */
int submit_job(struct job_record *job_ptr, bitstr_t *bitmap,
	       int min_nodes, int max_nodes);
/** */
void sort_bgl_record_inc_size(List records);
/** */
void sort_bgl_record_dec_size(List records);

/** */
void print_bgl_record(bgl_record_t* record);
/** */
char* convert_lifecycle(lifecycle_type_t lifecycle);
/** */
char* convert_part_type(rm_partition_t* pt);

/** */
void update_bgl_node_bitmap(bitstr_t* bitmap);


#endif /* _BLUEGENE_H_ */
