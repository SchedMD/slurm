/*****************************************************************************\
 *  bluegene.h - header for blue gene node allocation plugin. 
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

#ifndef _BLUEGENE_H_
#define _BLUEGENE_H_

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/slurmctld/slurmctld.h"

#ifdef HAVE_BGL_FILES
# include "rm_api.h"

/*
 * There is presently a huge amount of untested code to use the APIs.
 * Surround the code with "#ifdef USE_BGL_FILES". When it is confirmed 
 * to work, use "#ifdef HAVE_BGL_FILES" around the code using the APIs.
 */
/* #define USE_BGL_FILES 1 */

#else
  enum rm_connection_type {RM_MESH, RM_TORUS, RM_NAV};
  enum rm_partition_mode {RM_PARTITION_COPROCESSOR_MODE,
		RM_PARTITION_VIRTUAL_NODE_MODE};

  typedef char *   pm_partition_id_t;
  typedef enum rm_connection_type rm_connection_type_t;
  typedef enum rm_partition_mode rm_partition_mode_t;
  typedef uint16_t rm_partition_t;
  typedef char *   rm_BGL_t;
#endif

/* Global variables */
extern rm_BGL_t *bgl;

typedef int lifecycle_type_t;
enum part_lifecycle {DYNAMIC, STATIC};

typedef struct bgl_record {
	char* slurm_part_id;		/* ID specified by admins	*/
	pm_partition_id_t bgl_part_id;	/* ID returned from CMCS	*/
	char* nodes;			/* String of nodes in partition */
	lifecycle_type_t part_lifecycle;/* either STATIC or DYNAMIC	*/
	hostlist_t hostlist;		/* expanded form of hosts */
	bitstr_t *bitmap;		/* bitmap of nodes for this partition */
	struct partition* alloc_part;	/* the allocated partition   */
	int size;			/* node count for the partitions */
	rm_connection_type_t conn_type;/* Mesh or Torus or NAV */
	rm_partition_mode_t node_use;	/* either COPROCESSOR or VIRTUAL */
} bgl_record_t;

/** 
 * bgl_conf_record is used to store the elements read from the config
 * file from init().
 */
typedef struct bgl_conf_record{
	char* nodes;
	rm_connection_type_t conn_type;/* Mesh or Torus or NAV */
	rm_partition_mode_t node_use;
} bgl_conf_record_t;

/** 
 * process the configuration file so to interpret what partitions are
 * static, dynamic, etc.
 * 
 */
extern int read_bgl_conf(void);

/* Initialize all plugin variables */
extern int init_bgl(void);

/* Purge all plugin variables */
extern void fini_bgl(void);

/**
 * create_static_partitions - create the static partitions that will be used
 * for scheduling.  
 * IN - part_list: slurmctld's list of partition configurations 
 * OUT - (global, to slurmctld): Table of partitionIDs to geometries
 * RET - success of fitting all configurations
 */
extern int create_static_partitions(List part_list);

/* Try to find resources for a given job request */
extern int submit_job(struct job_record *job_ptr, bitstr_t *bitmap,
	       int min_nodes, int max_nodes);

/* sort a list of bgl_records by size (node count) */
extern void sort_bgl_record_inc_size(List records);
extern void sort_bgl_record_dec_size(List records);

/* Log a bgl_record's contents */
extern void print_bgl_record(bgl_record_t* record);

/* Return strings representing blue gene data types */
extern char* convert_lifecycle(lifecycle_type_t lifecycle);
extern char* convert_conn_type(rm_connection_type_t conn_type);
extern char* convert_node_use(rm_partition_mode_t pt);

/* bluegene_agent - detached thread periodically updates status of bluegene nodes */
extern void *bluegene_agent(void *args);

#endif /* _BLUEGENE_H_ */
