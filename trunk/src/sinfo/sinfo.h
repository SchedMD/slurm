/****************************************************************************\
 *  sinfo.h - definitions used for sinfo data functions
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
\****************************************************************************/

#ifndef _SINFO_H
#define _SINFO_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	uint16_t node_state;

	uint32_t nodes_alloc;
	uint32_t nodes_idle;
	uint32_t nodes_other;
	uint32_t nodes_total;

	uint32_t cpus_alloc;
	uint32_t cpus_idle;
	uint32_t cpus_other;
	uint32_t cpus_total;

	uint32_t min_cpus;
	uint32_t max_cpus;
	uint32_t min_sockets;
	uint32_t max_sockets;
	uint32_t min_cores;
	uint32_t max_cores;
	uint32_t min_threads;
	uint32_t max_threads;
	uint32_t min_disk;
	uint32_t max_disk;
	uint32_t min_mem;
	uint32_t max_mem;
	uint32_t min_weight;
	uint32_t max_weight;

	char *features;
	char *reason;

	hostlist_t nodes;
#ifdef HAVE_BG
	hostlist_t ionodes;
#endif
	/* part_info contains partition, avail, max_time, job_size, 
	 * root, share, groups, priority */
	partition_info_t* part_info;
	uint16_t part_inx;
} sinfo_data_t;

/* Identify what fields must match for a node's information to be 
 * combined into a single sinfo_data entry based upon output format */
struct sinfo_match_flags {
	bool avail_flag;
	bool cpus_flag;
	bool sockets_flag;
	bool cores_flag;
	bool threads_flag;
	bool sct_flag;
	bool disk_flag;
	bool features_flag;
	bool groups_flag;
	bool job_size_flag;
	bool default_time_flag;
	bool max_time_flag;
	bool memory_flag;
	bool partition_flag;
	bool priority_flag;
	bool reason_flag;
	bool root_flag;
	bool share_flag;
	bool state_flag;
	bool weight_flag;
};

/* Input parameters */
struct sinfo_parameters {
	bool all_flag;
	bool bg_flag;
	bool dead_nodes;
	bool exact_match;
	bool filtering;
	bool long_output;
	bool no_header;
	bool node_field_flag;
	bool node_flag;
	bool responding_nodes;
	bool list_reasons;
	bool summarize;
	struct sinfo_match_flags match_flags;

	char* format;
	char* nodes;
	char* partition;
	char* sort;
	char* states;

	int iterate;
	int node_field_size;
	int verbose;

	List  format_list;
	List  state_list;
};

extern struct sinfo_parameters params;

extern void parse_command_line( int argc, char* argv[] );
extern int  parse_state( char* str, uint16_t* states );
extern void sort_sinfo_list( List sinfo_list );

#endif
