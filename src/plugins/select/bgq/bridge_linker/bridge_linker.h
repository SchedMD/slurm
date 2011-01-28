/*****************************************************************************\
 *  bridge_linker.h
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
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

#include "src/common/read_config.h"
#include "src/common/parse_spec.h"
#include "src/slurmctld/proc_req.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/bitstring.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "../bg_structs.h"
#include "bg_list_functions.h"

/* Global variables */
extern bg_config_t *bg_conf;
extern bg_lists_t *bg_lists;
extern time_t last_bg_update;
extern bool agent_fini;
extern pthread_mutex_t block_state_mutex;
extern pthread_mutex_t request_list_mutex;
extern int blocks_are_created;
extern int num_unused_cpus;

extern int bridge_init(char *properties_file);
extern int bridge_fini();

/*
 * Convert a BG API error code to a string
 * IN inx - error code from any of the BG Bridge APIs
 * RET - string describing the error condition
 */
extern const char *bridge_err_str(int inx);

extern int bridge_get_size(uint16_t *size);
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

extern int bridge_block_add_user(bg_record_t *bg_record, char *user_name);
extern int bridge_block_remove_user(bg_record_t *bg_record, char *user_name);
extern int bridge_block_remove_all_users(bg_record_t *bg_record,
					 char *user_name);
extern int bridge_block_set_owner(bg_record_t *bg_record, char *user_name);

extern int bridge_block_get_and_set_mps(bg_record_t *bg_record);

/* don't send the bg_record since we would need to lock things up and
 * this function could take a bit.
 */
extern int bridge_block_wait_for_jobs(char *bg_block_id);

#endif /* _BRIDGE_LINKER_H_ */
