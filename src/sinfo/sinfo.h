/****************************************************************************\
 *  sinfo.h - definitions used for sinfo data functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2017 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\****************************************************************************/

#ifndef _SINFO_H
#define _SINFO_H

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/slurmdb_defs.h"

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	uint16_t port;
	uint32_t node_state;

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
	uint64_t min_mem;
	uint64_t max_mem;
	uint32_t min_weight;
	uint32_t max_weight;
	uint32_t min_cpu_load;
	uint32_t max_cpu_load;
	uint64_t min_free_mem;
	uint64_t max_free_mem;

	uint32_t max_cpus_per_node;
	uint64_t alloc_memory;

	char *features;
	char *features_act;
	char *gres;
	char *gres_used;
	char *cluster_name;
	char *comment;
	char *extra;
	char *reason;
	time_t reason_time;
	char *resv_name;
	uint32_t reason_uid;
	char *version;

	hostlist_t hostnames;
	hostlist_t node_addr;
	hostlist_t nodes;

	/* part_info contains partition, avail, max_time, job_size,
	 * root, share/oversubscribe, groups, priority */
	partition_info_t* part_info;
	uint16_t part_inx;
} sinfo_data_t;

/* Identify what fields must match for a node's information to be
 * combined into a single sinfo_data entry based upon output format */
struct sinfo_match_flags {
	bool alloc_mem_flag;
	bool avail_flag;
	bool cpus_flag;
	bool sockets_flag;
	bool cores_flag;
	bool threads_flag;
	bool sct_flag;
	bool disk_flag;
	bool extra_flag;
	bool features_flag;
	bool features_act_flag;
	bool groups_flag;
	bool gres_flag;
	bool gres_used_flag;
	bool hostnames_flag;
	bool job_size_flag;
	bool default_time_flag;
	bool max_time_flag;
	bool memory_flag;
	bool node_addr_flag;
	bool partition_flag;
	bool port_flag;
	bool preempt_mode_flag;
	bool priority_job_factor_flag;
	bool priority_tier_flag;
	bool comment_flag;
	bool reason_flag;
	bool resv_name_flag;
	bool root_flag;
	bool oversubscribe_flag;
	bool state_flag;
	bool statecomplete_flag;
	bool weight_flag;
	bool reason_timestamp_flag;
	bool reason_user_flag;
	bool cpu_load_flag;
	bool free_mem_flag;
	bool max_cpus_per_node_flag;
	bool version_flag;
};

/* Input parameters */
struct sinfo_parameters {
	bool all_flag;
	List clusters;
	uint32_t cluster_flags;
	uint32_t convert_flags;
	bool dead_nodes;
	bool def_format;
	bool exact_match;
	bool federation_flag;
	bool future_flag;
	bool filtering;
	bool local;
	bool long_output;
	bool no_header;
	bool node_field_flag;
	bool node_flag;
	bool node_name_single;
	bool part_field_flag;
	bool reservation_flag;
	bool responding_nodes;
	bool list_reasons;
	bool summarize;
	struct sinfo_match_flags match_flags;

	char* format;
	char *mimetype; /* --yaml or --json */
	char* nodes;
	char* partition;
	char* sort;
	char* states;

	int iterate;
	int node_field_size;
	int part_field_size;
	int verbose;

	List  part_list;
	List  format_list;
	List  state_list;
	bool  state_list_and;

	slurmdb_federation_rec_t *fed;
};

extern struct sinfo_parameters params;

extern void parse_command_line( int argc, char* *argv );
extern int  parse_state( char* str, uint16_t* states );
extern void sort_sinfo_list( List sinfo_list );

#endif
