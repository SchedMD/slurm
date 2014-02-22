/*****************************************************************************\
 *  print.h - sinfo print job definitions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2011 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _SINFO_PRINT_H_
#define _SINFO_PRINT_H_

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/sinfo/sinfo.h"

#define FORMAT_STRING_SIZE 32

/*****************************************************************************
 * Format Structures
 *****************************************************************************/
typedef struct sinfo_format {
	int (*function) (sinfo_data_t *, int, bool, char*);
	uint32_t width;
	bool right_justify;
	char *suffix;
} sinfo_format_t;

/*****************************************************************************
 * Print Format Functions
 *****************************************************************************/
int format_add_function(List list, int width, bool right_justify,
		char * suffix,
		int (*function) (sinfo_data_t  *, int, bool, char *));

void print_date(void);
int  print_sinfo_entry(sinfo_data_t *sinfo_data);
int  print_sinfo_list(List sinfo_list);
void print_sinfo_reservation(reserve_info_msg_t *resv_ptr);

#define format_add_avail(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_avail)
#define format_add_cpus(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_cpus)
#define format_add_cpus_aiot(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_cpus_aiot)
#define format_add_sct(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_sct)
#define format_add_sockets(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_sockets)
#define format_add_cores(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_cores)
#define format_add_threads(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_threads)
#define format_add_disk(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_disk)
#define format_add_features(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_features)
#define format_add_groups(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_groups)
#define format_add_gres(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_gres)
#define format_add_memory(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_memory)
#define format_add_node_address(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_node_address)
#define format_add_node_hostnames(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_node_hostnames)
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
#define format_add_partition_name(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_partition_name)
#define format_add_prefix(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_prefix)
#define format_add_preempt_mode(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_preempt_mode)
#define format_add_priority(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_priority)
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
#define format_add_timestamp(list,wid,right,suffix)		\
	format_add_function(list,wid,right,suffix,_print_timestamp)
#define format_add_user(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_user)
#define format_add_user_long(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_user_long)
#define format_add_default_time(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_default_time)
#define format_add_weight(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_weight)
#define format_add_alloc_nodes(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_alloc_nodes)
#define format_add_invalid(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_com_invalid)
#define format_add_cpu_load(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_cpu_load)
#define format_add_max_cpus_per_node(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_max_cpus_per_node)
#define format_add_version(list,wid,right,suffix) \
	format_add_function(list,wid,right,suffix,_print_version)

/*****************************************************************************
 * Print Field Functions
 *****************************************************************************/

int _print_avail(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_cpus(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_cpus_aiot(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_sct(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_sockets(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_cores(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_threads(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_disk(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_features(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_groups(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_gres(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_memory(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_node_hostnames(sinfo_data_t * sinfo_data, int width,
			  bool right_justify, char *suffix);
int _print_node_address(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_node_list(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_nodes_t(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_nodes_ai(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_nodes_aiot(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_partition(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_partition_name(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_prefix(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_preempt_mode(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_priority(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_reason(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_root(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_share(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_size(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_state_compact(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_state_long(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_time(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_timestamp(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_user(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_user_long(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_default_time(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_weight(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix);
int _print_alloc_nodes(sinfo_data_t * sinfo_data, int width,
		       bool right_justify, char *suffix);
int _print_com_invalid(sinfo_data_t * sinfo_data, int width,
		       bool right_justify, char *suffix);
int _print_cpu_load(sinfo_data_t * node_ptr, int width,
		    bool right_justify, char *suffix);
int _print_max_cpus_per_node(sinfo_data_t * sinfo_data, int width,
			     bool right_justify, char *suffix);
int _print_version(sinfo_data_t * sinfo_data, int width,
		   bool right_justify, char *suffix);
#endif
