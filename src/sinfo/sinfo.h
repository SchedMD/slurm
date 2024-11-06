/****************************************************************************\
 *  sinfo.h - definitions used for sinfo data functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
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
	char *cpu_spec_list;
	uint64_t mem_spec_limit;

	hostlist_t *hostnames;
	hostlist_t *node_addr;
	hostlist_t *nodes;

	/* part_info contains partition, avail, max_time, job_size,
	 * root, share/oversubscribe, groups, priority */
	partition_info_t* part_info;
	uint16_t part_inx;
} sinfo_data_t;

/* Identify what fields must match for a node's information to be
 * combined into a single sinfo_data entry based upon output format */
#define MATCH_FLAG_ALLOC_MEM		SLURM_BIT(0)
#define MATCH_FLAG_AVAIL		SLURM_BIT(1)
#define MATCH_FLAG_COMMENT		SLURM_BIT(2)
#define MATCH_FLAG_CORES		SLURM_BIT(3)
#define MATCH_FLAG_CPUS			SLURM_BIT(4)
#define MATCH_FLAG_CPU_LOAD		SLURM_BIT(5)
#define MATCH_FLAG_DEFAULT_TIME		SLURM_BIT(6)
#define MATCH_FLAG_DISK			SLURM_BIT(7)
#define MATCH_FLAG_EXTRA		SLURM_BIT(8)
#define MATCH_FLAG_FEATURES		SLURM_BIT(9)
#define MATCH_FLAG_FEATURES_ACT		SLURM_BIT(10)
#define MATCH_FLAG_FREE_MEM		SLURM_BIT(11)
#define MATCH_FLAG_GROUPS		SLURM_BIT(12)
#define MATCH_FLAG_GRES			SLURM_BIT(13)
#define MATCH_FLAG_GRES_USED		SLURM_BIT(14)
#define MATCH_FLAG_HOSTNAMES		SLURM_BIT(15)
#define MATCH_FLAG_JOB_SIZE		SLURM_BIT(16)
#define MATCH_FLAG_MAX_CPUS_PER_NODE	SLURM_BIT(17)
#define MATCH_FLAG_MAX_TIME		SLURM_BIT(18)
#define MATCH_FLAG_MEMORY		SLURM_BIT(19)
#define MATCH_FLAG_NODE_ADDR		SLURM_BIT(20)
#define MATCH_FLAG_NODES_AI		SLURM_BIT(21)
#define MATCH_FLAG_OVERSUBSCRIBE	SLURM_BIT(22)
#define MATCH_FLAG_PARTITION		SLURM_BIT(23)
#define MATCH_FLAG_PREEMPT_MODE		SLURM_BIT(24)
#define MATCH_FLAG_PRIORITY_JOB_FACTOR	SLURM_BIT(25)
#define MATCH_FLAG_PRIORITY_TIER	SLURM_BIT(26)
#define MATCH_FLAG_PORT			SLURM_BIT(27)
#define MATCH_FLAG_REASON		SLURM_BIT(28)
#define MATCH_FLAG_REASON_TIMESTAMP	SLURM_BIT(29)
#define MATCH_FLAG_REASON_USER		SLURM_BIT(30)
#define MATCH_FLAG_ROOT			SLURM_BIT(31)
#define MATCH_FLAG_RESV_NAME		SLURM_BIT(32)
#define MATCH_FLAG_SCT			SLURM_BIT(33)
#define MATCH_FLAG_SOCKETS		SLURM_BIT(34)
#define MATCH_FLAG_STATE		SLURM_BIT(35)
#define MATCH_FLAG_STATE_COMPLETE	SLURM_BIT(36)
#define MATCH_FLAG_THREADS		SLURM_BIT(37)
#define MATCH_FLAG_VERSION		SLURM_BIT(38)
#define MATCH_FLAG_WEIGHT		SLURM_BIT(39)

/* Flags for fmt_data_t */
#define FMT_FLAG_HIDDEN			SLURM_BIT(0)

typedef struct fmt_data {
	char *name;		/* long format name */
	char c;			/* short format character, prefixed by '%' */
	int (*fn)(sinfo_data_t *sinfo_data, int width, bool right, char *suffix);
	uint64_t match_flags;	/* See MATCH_FLAG_* */
	uint32_t flags;		/* See FMT_FLAG_* */
} fmt_data_t;

/* Input parameters */
struct sinfo_parameters {
	bool all_flag;
	list_t *clusters;
	uint32_t cluster_flags;
	char *cluster_names;
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
	uint64_t match_flags;

	char* format;
	char *mimetype; /* --yaml or --json */
	char *data_parser; /* data_parser args */
	char* nodes;
	char* partition;
	char* sort;
	char* states;

	int iterate;
	int node_field_size;
	int part_field_size;
	int verbose;

	list_t *part_list;
	list_t *format_list;
	list_t *state_list;
	bool  state_list_and;

	slurmdb_federation_rec_t *fed;
};

extern struct sinfo_parameters params;

extern void parse_command_line( int argc, char* *argv );
extern int  parse_state( char* str, uint16_t* states );
extern void sort_sinfo_list(list_t *sinfo_list);

#endif
