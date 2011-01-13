/*****************************************************************************\
 *  bgq.h - hearder file for the Blue Gene/Q plugin.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _BGQ_H_
#define _BGQ_H_

#include "bg_record_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

/* #ifdef HAVE_CONFIG_H */
/* #  include "config.h" */
/* #  if HAVE_STDINT_H */
/* #    include <stdint.h> */
/* #  endif */
/* #  if HAVE_INTTYPES_H */
/* #    include <inttypes.h> */
/* #  endif */
/* #endif */

/* #include <stdio.h> */
/* #include <sys/types.h> */
/* #include <sys/stat.h> */
/* #include <unistd.h> */
/* #include <slurm/slurm.h> */
/* #include <slurm/slurm_errno.h> */

/* #ifdef WITH_PTHREADS */
/* #  include <pthread.h> */
/* #endif				/\* WITH_PTHREADS *\/ */


/* #include "src/common/slurm_xlator.h"	/\* Must be first *\/ */
/* #include "src/common/macros.h" */
/* #include "src/slurmctld/slurmctld.h" */
/* #include "bgq_enums.h" */
/* #include "block_allocator/block_allocator.h" */

typedef enum bg_layout_type {
	LAYOUT_STATIC,  /* no overlaps, except for full system block
			   blocks never change */
	LAYOUT_OVERLAP, /* overlaps permitted, must be defined in
			   bluegene.conf file */
	LAYOUT_DYNAMIC	/* slurm will make all blocks */
} bg_layout_t;

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

/* Global variables */
extern bg_config_t *bg_conf;
extern bg_lists_t *bg_lists;
extern ba_system_t *ba_system_ptr;
extern time_t last_bg_update;
extern bool agent_fini;
extern pthread_mutex_t block_state_mutex;
extern pthread_mutex_t request_list_mutex;
extern int blocks_are_created;
extern int num_unused_cpus;

#define MAX_PTHREAD_RETRIES  1
#define BLOCK_ERROR_STATE    -3
#define ADMIN_ERROR_STATE    -4
#define NO_JOB_RUNNING       -1
#define BUFSIZE 4096
#define BITSIZE 128
/* Change BLOCK_STATE_VERSION value when changing the state save
 * format i.e. pack_block() */
#define BLOCK_STATE_VERSION      "VER001"

#include "bg_job_place.h"
#include "bg_job_run.h"
#include "jobinfo.h"
#include "nodeinfo.h"

/* Initialize all plugin variables */
extern int init_bg(void);

/* Purge all plugin variables */
extern void fini_bg(void);

#ifdef __cplusplus
}
#endif

#endif /* _BGQ_H_ */
