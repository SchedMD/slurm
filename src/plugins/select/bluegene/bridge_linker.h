/*****************************************************************************\
 *  bridge_linker.h
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

#ifndef _BRIDGE_LINKER_H_
#define _BRIDGE_LINKER_H_

/* This must be included first for AIX systems */
#include "src/common/macros.h"

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <dlfcn.h>

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#include "src/common/node_select.h"
#include "src/common/read_config.h"
#include "src/common/parse_spec.h"
#include "src/slurmctld/proc_req.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/bitstring.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/locks.h"
#include "bg_list_functions.h"
#include "bg_enums.h"

#define MAX_POLL_RETRIES    220
#define POLL_INTERVAL        3

/* Global variables */
extern bg_config_t *bg_conf;
extern bg_lists_t *bg_lists;
extern time_t last_bg_update;
extern pthread_mutex_t block_state_mutex;
extern int blocks_are_created;
extern int num_unused_cpus;
extern int num_possible_unused_cpus;
extern slurmctld_lock_t job_read_lock;

extern int bridge_init(char *properties_file);
extern int bridge_fini();

extern int bridge_get_size(int *size);
extern int bridge_setup_system();

extern int bridge_block_create(bg_record_t *bg_record);

/*
 * Boot a block. Block state expected to be FREE upon entry.
 * NOTE: This function does not wait for the boot to complete.
 * the slurm prolog script needs to perform the waiting.
 * NOTE: block_state_mutex needs to be locked before entering.
 */
extern int bridge_block_boot(bg_record_t *bg_record);
extern int bridge_block_free(bg_record_t *bg_record);
extern int bridge_block_remove(bg_record_t *bg_record);

extern int bridge_block_add_user(bg_record_t *bg_record,
				 const char *user_name);
extern int bridge_block_remove_user(bg_record_t *bg_record,
				    const char *user_name);
extern int bridge_block_sync_users(bg_record_t *bg_record);

extern int bridge_blocks_load_curr(List curr_block_list);

extern void bridge_reset_block_list(List block_list);
extern void bridge_block_post_job(char *bg_block_id,
				  struct job_record *job_ptr);
extern uint16_t bridge_block_get_action(char *bg_block_id);
extern int bridge_check_nodeboards(char *mp_loc);

extern int bridge_set_log_params(char *api_file_name, unsigned int level);

#if defined HAVE_BG_FILES && defined HAVE_BG_L_P
extern bool have_db2;

extern status_t bridge_get_bg(my_bluegene_t **bg);
extern status_t bridge_free_bg(my_bluegene_t *bg);
extern status_t bridge_get_data(rm_element_t* element,
				enum rm_specification field, void *data);
extern status_t bridge_set_data(rm_element_t* element,
				enum rm_specification field, void *data);
extern status_t bridge_free_nodecard_list(rm_nodecard_list_t *nc_list);
extern status_t bridge_free_block(rm_partition_t *partition);
extern status_t bridge_block_modify(char *bg_block_id,
				    int op, const void *data);
extern status_t bridge_get_block(char *bg_block_id,
				 rm_partition_t **partition);
extern status_t bridge_get_block_info(char *bg_block_id,
				      rm_partition_t **partition);
extern status_t bridge_get_blocks(rm_partition_state_flag_t flag,
				  rm_partition_list_t **part_list);
extern status_t bridge_get_blocks_info(rm_partition_state_flag_t flag,
				       rm_partition_list_t **part_list);
extern status_t bridge_free_block_list(rm_partition_list_t *part_list);
extern status_t bridge_new_nodecard(rm_nodecard_t **nodecard);
extern status_t bridge_free_nodecard(rm_nodecard_t *nodecard);
extern status_t bridge_get_nodecards(rm_bp_id_t bpid,
				     rm_nodecard_list_t **nc_list);
#ifdef HAVE_BGP
extern status_t bridge_new_ionode(rm_ionode_t **ionode);
extern status_t bridge_free_ionode(rm_ionode_t *ionode);
#else
extern int bridge_find_nodecard_num(rm_partition_t *block_ptr,
				    rm_nodecard_t *ncard,
				    int *nc_id);
#endif
#endif /* HAVE_BG_FILES */

#endif /* _BRIDGE_LINKER_H_ */
