/*****************************************************************************\
 *  bg_structs.h
 *
 *****************************************************************************
 *  Copyright (C) 2011 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifndef _BG_STRUCTS_H_
#define _BG_STRUCTS_H_

#include "bg_enums.h"

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "slurm.h"
#include "src/common/list.h"
#include "src/common/bitstring.h"

typedef struct {
	uint16_t bp_node_cnt;
	uint16_t bp_nodecard_cnt;
	char *bridge_api_file;
	uint16_t bridge_api_verb;
	uint32_t slurm_debug_flags;
	char *default_mloaderimage;
	uint16_t deny_pass;
	double io_ratio;
	bg_layout_t layout_mode;
	List mloader_list;
	double nc_ratio;
	uint16_t nodecard_node_cnt;
	uint16_t nodecard_ionode_cnt;
	uint16_t numpsets;
	uint16_t cpu_ratio;
	uint32_t cpus_per_bp;
	uint16_t quarter_node_cnt;
	uint16_t quarter_ionode_cnt;
	List ramdisk_list;
	char *slurm_user_name;
	char *slurm_node_prefix;
	uint32_t smallest_block;
} bg_config_t;

typedef struct {
	List booted;         /* blocks that are booted */
	List job_running;    /* jobs running in these blocks */
	List main;	    /* List of configured BG blocks */
	List valid_small32;
	List valid_small64;
	List valid_small128;
	List valid_small256;
} bg_lists_t;

typedef struct bg_record {
	char *bg_block_id;     	        /* ID returned from MMCS */
	List bg_midplanes;              /* List of midplanes in block */
	List bg_pt_midplanes;           /* List of passthrough
					 * midplanes in block */
	bitstr_t *bitmap;               /* bitmap to check the nodes
					   of block */
	int boot_count;                 /* number of attemts boot attempts */
	int boot_state;                 /* check to see if boot failed.
					   -1 = fail,
					   0 = not booting,
					   1 = booting */
	int bp_count;                   /* size */
	uint16_t conn_type[HIGHEST_DIMENSIONS];  /* MESH or Torus or NAV */
	uint32_t cpu_cnt;               /* count of cpus per block */
	int free_cnt;                   /* How many are trying
					   to free this block at the
					   same time */
	bool full_block;                /* whether or not block is the full
					   block */
	uint16_t geo[HIGHEST_DIMENSIONS];  /* geometry */
	char *ionodes; 		        /* String of ionodes in block
					 * NULL if not a small block*/
	bitstr_t *ionode_bitmap;        /* for small blocks bitmap to
					   keep track which ionodes we
					   are on.  NULL if not a small block*/
	struct job_record *job_ptr;	/* pointer to job running on
					 * block or NULL if no job */
	int job_running;                /* job id of job running of if
					 * block is in an error state
					 * BLOCK_ERROR_STATE */
	char *linuximage;               /* LinuxImage/CnloadImage for
					 * this block */
	uint16_t magic;	        	/* magic number */
	char *mloaderimage;             /* mloaderImage for this block */
	int modifying;                  /* flag to say the block is
					   being modified or not at
					   job launch usually */
	char *nodes;			/* String of nodes in block */
	uint32_t node_cnt;              /* count of cnodes per block */
	struct bg_record *original;     /* if this is a copy this is a
					   pointer to the original */
	char *reason;                   /* reason block is in error state */
	uint16_t small;                 /* if this block is small or not. */
	uint16_t state;                 /* Current state of the block */
	uint16_t start[HIGHEST_DIMENSIONS];  /* start node */
	char *target_name;		/* when a block is freed this
					   is the name of the user we
					   want on the block */
	char *user_name;		/* user using the block */
	uid_t user_uid;   		/* Owner of block uid	*/
} bg_record_t;

#endif
