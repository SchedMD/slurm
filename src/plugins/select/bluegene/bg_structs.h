/*****************************************************************************\
 *  bg_structs.h
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
	uint32_t actual_cnodes_per_mp; /* used only on sub_mp_systems */
	List blrts_list;
	char *bridge_api_file;
	uint16_t bridge_api_verb;
	uint16_t cpu_ratio;
	uint32_t cpus_per_mp;
	char *default_blrtsimage;
	char *default_linuximage;
	char *default_mloaderimage;
	char *default_ramdiskimage;
	uint16_t default_conn_type[HIGHEST_DIMENSIONS];
	uint16_t deny_pass;
	double io_ratio;
	uint16_t ionode_cnode_cnt;
	uint16_t ionodes_per_mp;
	bg_layout_t layout_mode;
	List linux_list;
	uint16_t max_block_err;
	List mloader_list;
	uint16_t mp_cnode_cnt;
	uint16_t mp_nodecard_cnt;
	double nc_ratio;
	uint16_t nodecard_cnode_cnt;
	uint16_t nodecard_ionode_cnt;
	uint16_t quarter_cnode_cnt;
	uint16_t quarter_ionode_cnt;
	List ramdisk_list;
	bitstr_t *reboot_qos_bitmap;
	uint64_t slurm_debug_flags;
	uint32_t slurm_debug_level;
	char *slurm_node_prefix;
	char *slurm_user_name;
	uint32_t smallest_block;
	uint16_t sub_blocks;
	uint16_t sub_mp_sys;
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
	uint16_t action;                /* Any action that might be on
					   the block.  At the moment,
					   don't pack. */
	bool avail_set;                 /* Used in sorting, don't copy
					   or pack. */
	uint32_t avail_cnode_cnt;       /* Used in sorting, don't copy
					   or pack. */
	time_t avail_job_end;           /* Used in sorting, don't copy
					   or pack. */
	void *bg_block;                 /* needed for L/P systems */
	char *bg_block_id;     	        /* ID returned from MMCS */
	List ba_mp_list;                /* List of midplanes in block */
	char *blrtsimage;               /* BlrtsImage for this block */
	int boot_count;                 /* number of attemts boot attempts */
	int boot_state;                 /* check to see if boot failed.
					   -1 = fail,
					   0 = not booting,
					   1 = booting */
	uint32_t cnode_cnt;             /* count of cnodes per block */
	uint32_t cnode_err_cnt;         /* count of cnodes in error on
					   block */
	uint16_t conn_type[SYSTEM_DIMENSIONS];  /* MESH or Torus or NAV */
	uint32_t cpu_cnt;               /* count of cpus per block */
	int destroy;                    /* if the block is being destroyed */
	uint16_t err_ratio;             /* ratio of how much of this
					   block is in an error
					   state. (doesn't apply to BGL/P) */
	int free_cnt;                   /* How many are trying
					   to free this block at the
					   same time */
	bool full_block;                /* whether or not block is the full
					   block */
	uint16_t geo[SYSTEM_DIMENSIONS];  /* geometry */
	bitstr_t *ionode_bitmap;        /* for small blocks bitmap to
					   keep track which ionodes we
					   are on.  NULL if not a small block*/
	char *ionode_str;               /* String of ionodes in block
					 * NULL if not a small block*/
	List job_list;                  /* List of job records running on a
					   block that allows multiple jobs */
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
	bitstr_t *mp_bitmap;            /* bitmap to check the midplanes
					   of block */
	int mp_count;                   /* size */
	char *mp_str;   		/* String of midplanes in block */
	uint16_t node_use;      	/* either COPROCESSOR or VIRTUAL */
	struct bg_record *original;     /* if this is a copy this is a
					   pointer to the original */
	char *ramdiskimage;             /* RamDiskImage/IoloadImg for
					 * this block */
	char *reason;                   /* reason block is in error state */
	uint16_t state;                 /* Current state of the block */
	uint16_t start[SYSTEM_DIMENSIONS];  /* start node */
	uint16_t start_small[HIGHEST_DIMENSIONS]; /* On a small block
						   * what the starting
						   * cnode is to
						   * figure out the
						   * relative position
						   * of jobs */
	uint32_t switch_count;          /* number of switches
					 * used. On L/P */
} bg_record_t;

#endif
