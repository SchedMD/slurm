/*****************************************************************************\
 *  print.h - smap print job definitions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>
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
\*****************************************************************************/

#ifndef _SMAP_PRINT_H_
#define _SMAP_PRINT_H_

#include <slurm/slurm.h>

#include "src/common/list.h"
#include "src/smap/smap.h"

#define FORMAT_STRING_SIZE 32

/*****************************************************************************
 * Format Structures
 *****************************************************************************/
typedef struct smap_format {
	int (*function) (smap_data_t *, int, bool, char*);
	uint32_t width;
	bool right_justify;
	char *suffix;
} smap_format_t;

/*****************************************************************************
 * Print Format Functions
 *****************************************************************************/
int format_add_function(List list, int width, bool right_justify,
		char * suffix, 
		int (*function) (smap_data_t  *, int, bool, char *));

void print_date(void);
int  print_smap_entry(smap_data_t *smap_data);
int  print_smap_list(List smap_list);

#define format_add_avail(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_avail)
#define format_add_cpus(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_cpus)
#define format_add_disk(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_disk)
#define format_add_features(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_features)
#define format_add_groups(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_groups)
#define format_add_memory(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_memory)
#define format_add_node_list(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_node_list)
#define format_add_nodes(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_nodes_t)
#define format_add_nodes_aiot(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_nodes_aiot)
#define format_add_nodes_ai(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_nodes_ai)
#define format_add_partition(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_partition)
#define format_add_prefix(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_prefix)
#define format_add_reason(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_reason)
#define format_add_root(list,wid,right,prefix) \
	format_add_function(list,wid,right,prefix,_print_root)
#define format_add_share(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_share)
#define format_add_size(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_size)
#define format_add_state_compact(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_state_compact)
#define format_add_state_long(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_state_long)
#define format_add_time(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_time)
#define format_add_weight(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_weight)

/*****************************************************************************
 * Print Field Functions
 *****************************************************************************/

int _print_avail(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_cpus(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_disk(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_features(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_groups(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_memory(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_node_list(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_nodes_t(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_nodes_ai(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_nodes_aiot(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_partition(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_prefix(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_reason(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_root(smap_data_t * smap_data, int width, 
			bool right_justify, char *suffix);
int _print_share(smap_data_t * smap_data, int width, 
			bool right_justify, char *suffix);
int _print_size(smap_data_t * smap_data, int width, 
			bool right_justify, char *suffix);
int _print_state_compact(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_state_long(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);
int _print_time(smap_data_t * smap_data, int width, 
			bool right_justify, char *suffix);
int _print_weight(smap_data_t * smap_data, int width,
			bool right_justify, char *suffix);

#endif
