/*****************************************************************************\
 *  bg_core.h - header for blue gene core functions processing module.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Danny Auble <da@llnl.gov>
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

#ifndef _BG_CORE_H_
#define _BG_CORE_H_

#include "bg_enums.h"
#include "bg_structs.h"
#include "bg_record_functions.h"
#include "bg_job_place.h"
#include "bg_job_run.h"
#include "bg_job_info.h"
#include "bg_node_info.h"
#include "ba_common.h"
#include "bridge_linker.h"
#include "bg_status.h"

/* Change BLOCK_STATE_VERSION value when changing the state save
 * format i.e. pack_block()
 */
#define BLOCK_STATE_VERSION      "PROTOCOL_VERSION"

/* Global variables */
/* extern bg_config_t *bg_conf; */
/* extern bg_lists_t *bg_lists; */
/* extern time_t last_bg_update; */
/* extern bool agent_fini; */
/* extern pthread_mutex_t block_state_mutex; */
/* extern pthread_mutex_t request_list_mutex; */
/* extern int blocks_are_created; */
/* extern int num_unused_cpus; */

extern bool blocks_overlap(bg_record_t *rec_a, bg_record_t *rec_b);
extern bool block_mp_passthrough(bg_record_t *bg_record, int mp_bit);
extern void bg_requeue_job(uint32_t job_id, bool wait_for_start,
			   bool slurmctld_locked, uint16_t job_state,
			   bool preempted);

/* sort a list of bg_records by size (node count) */
extern void sort_bg_record_inc_size(List records);

extern int bg_free_block(bg_record_t *bg_record, bool wait, bool locked);

extern void *mult_free_block(void *args);
extern void *mult_destroy_block(void *args);
extern void free_block_list(uint32_t job_id, List track_list,
			    bool destroy, bool wait);
extern int read_bg_conf();
extern int node_already_down(char *node_name);
extern const char *bg_err_str(int inx);

#endif /* _BG_CORE_H_ */

