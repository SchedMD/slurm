/*****************************************************************************\
 *  bluegene.h - header for blue gene configuration processing module. 
 *
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Danny Auble <da@llnl.gov>
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

#include <stdlib.h>
#include <sys/stat.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/slurmctld/slurmctld.h"
#include "src/partition_allocator/partition_allocator.h"

#ifdef HAVE_BGL_FILES
# include "src/plugins/select/bluegene/wrap_rm_api.h"

#else
  typedef char *   pm_partition_id_t;
  typedef int      rm_connection_type_t;
  typedef int      rm_partition_mode_t;
  typedef uint16_t rm_partition_t;
  typedef char *   rm_BGL_t;
  typedef char *   rm_component_id_t;
  typedef rm_component_id_t rm_bp_id_t;
  typedef int      rm_BP_state_t;
  typedef int      status_t;
  typedef int      rm_partition_state_t;
#endif

#define USER_NAME "slurm"

/* Global variables */
extern rm_BGL_t *bgl;
extern char *bluegene_blrts;
extern char *bluegene_linux;
extern char *bluegene_mloader;
extern char *bluegene_ramdisk;
extern char *bridge_api_file;
extern int numpsets;
extern pa_system_t *pa_system_ptr;
extern int DIM_SIZE[PA_SYSTEM_DIMENSIONS];
extern time_t last_bgl_update;
extern List bgl_curr_part_list; 	/* Initial bgl partition state */
extern List bgl_list;			/* List of configured BGL blocks */
extern bool agent_fini;
extern pthread_mutex_t part_state_mutex;

typedef int lifecycle_type_t;
enum part_lifecycle {DYNAMIC, STATIC};

typedef struct bgl_record {
	char *nodes;			/* String of nodes in partition */
	char *owner_name;		/* Owner of partition		*/
	uid_t owner_uid;   		/* Owner of partition uid	*/
	pm_partition_id_t bgl_part_id;	/* ID returned from MMCS	*/
	lifecycle_type_t part_lifecycle;/* either STATIC or DYNAMIC	*/
	rm_partition_state_t state;   	/* the allocated partition   */
	int geo[SYSTEM_DIMENSIONS];     /* geometry */
	rm_connection_type_t conn_type;	/* Mesh or Torus or NAV */
	rm_partition_mode_t node_use;	/* either COPROCESSOR or VIRTUAL */
	rm_partition_t *bgl_part;       /* structure to hold info from db2 */
	List bgl_part_list;             /* node list of blocks in partition */
	hostlist_t hostlist;		/* expanded form of hosts */
	int bp_count;                   /* size */
	int switch_count;               /* number of switches used. */
	int boot_state;                 /* check to see if boot failed. 
				 * -1 = fail, 0 = not booting, 1 = booting */
	int boot_count;                 /* number of attemts boot attempts */
	bitstr_t *bitmap;               /* bitmap to check the name of partition */
} bgl_record_t;

typedef struct {
	int source;
	int target;
} bgl_conn_t;

typedef struct {
	int dim;
	List conn_list;
} bgl_switch_t;

typedef struct {
	int *coord;
	int used;
	List switch_list;
} bgl_bp_t;

#include "bgl_part_info.h"
#include "bgl_job_place.h"
#include "bgl_job_run.h"
#include "state_test.h"
/*
 * bgl_conf_record is used to store the elements read from the bluegene.conf
 * file and is loaded by init().
 */
/* typedef struct bgl_conf_record { */
/* 	char* nodes; */
/* 	rm_connection_type_t conn_type;/\* Mesh or Torus or NAV *\/ */
/* 	rm_partition_mode_t node_use; */
/* 	rm_partition_t *bgl_part; */
/* } bgl_conf_record_t; */



/* bluegene.c */
/**********************************************/

/* Initialize all plugin variables */
extern int init_bgl(void);

/* Purge all plugin variables */
extern void fini_bgl(void);

/* Log a bgl_record's contents */
extern void print_bgl_record(bgl_record_t* record);
extern void destroy_bgl_record(void* object);

/* Return strings representing blue gene data types */
extern char *convert_lifecycle(lifecycle_type_t lifecycle);
extern char *convert_conn_type(rm_connection_type_t conn_type);
extern char *convert_node_use(rm_partition_mode_t pt);

/* sort a list of bgl_records by size (node count) */
extern void sort_bgl_record_inc_size(List records);

/* bluegene_agent - detached thread periodically tests status of bluegene 
 * nodes and switches */
extern void *bluegene_agent(void *args);

/*
 * Convert a BGL API error code to a string
 * IN inx - error code from any of the BGL Bridge APIs
 * RET - string describing the error condition
 */
extern char *bgl_err_str(status_t inx);

/*
 * create_static_partitions - create the static partitions that will be used
 *   for scheduling.
 * IN/OUT part_list - (global, from slurmctld): SLURM's partition 
 *   configurations. Fill in bgl_part_id                 
 * RET - success of fitting all configurations
 */
extern int create_static_partitions(List part_list);

extern int read_bgl_conf(void);

/* partition_sys.c */
/*****************************************************/
extern int configure_partition(bgl_record_t * bgl_conf_record);
extern int read_bgl_partitions(void);

/* bgl_switch_connections.c */
/*****************************************************/
extern int configure_partition_switches(bgl_record_t * bgl_conf_record);
extern int bgl_free_partition(pm_partition_id_t part_id);


#endif /* _BLUEGENE_H_ */
 
