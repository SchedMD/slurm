/****************************************************************************\
 *  smap.h - definitions used for smap data functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Moe Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
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
\****************************************************************************/

#ifndef _SMAP_H
#define _SMAP_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ncurses.h>

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */

#include <slurm/slurm.h>

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

enum {JOBS, SLURMPART, BGLPART }; 

typedef struct job_step {
  uint32_t job_id;
  uint32_t step_id;
}squeue_job_step_t;

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
  uint16_t node_state;
  
  uint32_t nodes_alloc;
  uint32_t nodes_idle;
  uint32_t nodes_other;
  uint32_t nodes_tot;
  uint32_t min_cpus;
  uint32_t max_cpus;
  uint32_t min_disk;
  uint32_t max_disk;
  uint32_t min_mem;
  uint32_t max_mem;
  uint32_t min_weight;
  uint32_t max_weight;
  
  char *features;
  char *reason;
  
  hostlist_t nodes;
  
  /* part_info contains partition, avail, max_time, job_size, 
   * root, share, groups */
  partition_info_t* part_info;
  uint16_t part_inx;
} smap_data_t;

/* Identify what fields must match for a node's information to be 
 * combined into a single smap_data entry based upon output format */
struct smap_match_flags {
  bool avail_flag;
  bool cpus_flag;
  bool disk_flag;
  bool features_flag;
  bool groups_flag;
  bool job_size_flag;
  bool max_time_flag;
  bool memory_flag;
  bool partition_flag;
  bool reason_flag;
  bool root_flag;
  bool share_flag;
  bool state_flag;
  bool weight_flag;
};

/* Input parameters */
struct smap_parameters {
  // job parts
  bool job_flag;
  bool step_flag;
  bool long_list;
  
  int  max_procs;
  
  char* jobs;
  char* partitions;
  char* steps;
  char* users;
  
  List  job_list;
  List  part_list;
  List  step_list;
  List  user_list;

  //Both
  bool all_flag;
  bool no_header;
  
  char* format;
  char* sort;
  char* states;

  int  iterate;
  int  verbose;
  int  Display;

  List  format_list;
  List  state_list;

  // partition parts
  bool dead_nodes;
  bool exact_match;
  bool filtering;
  bool long_output;
  bool node_field_flag;
  bool node_flag;
  bool responding_nodes;
  bool list_reasons;
  bool summarize;
  
  struct smap_match_flags match_flags;
  
  char* nodes;
  char* partition;
  
  int node_field_size;

};

extern struct smap_parameters params;
extern int xcord;
extern int ycord;
extern WINDOW *grid_win;
extern WINDOW *text_win;

extern void parse_command_line( int argc, char* argv[] );
extern int  parse_format( char* format );
extern int  parse_format_jobs( char* format );
extern void sort_job_list( List job_list );
extern void sort_jobs_by_start_time( List job_list );
extern void sort_step_list( List step_list );
extern int  parse_state( char* str, uint16_t* states );
extern void sort_smap_list( List smap_list );

#endif
