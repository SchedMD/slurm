/*****************************************************************************\
 *  bg_status.h
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
#ifndef _BG_STATUS_H_
#define _BG_STATUS_H_

#include "bg_core.h"

typedef struct {
	uint32_t jobid;
} kill_job_struct_t;

extern int bg_status_update_block_state(bg_record_t *bg_record,
					uint16_t state,
					List kill_job_list);
extern List bg_status_create_kill_job_list(void);
extern void bg_status_process_kill_job_list(List kill_job_list,
					    uint16_t job_state,
					    bool slurmctld_locked);

/* defined in the various bridge_status' */
extern int bridge_status_init(void);

extern int bridge_block_check_mp_states(char *bg_block_id,
					bool slurmctld_locked);
/* This needs to have block_state_mutex locked before hand. */
extern int bridge_status_update_block_list_state(List block_list);


/* This needs to have job_read locked before hand. */
extern void bg_status_add_job_kill_list(
	struct job_record *job_ptr, List *killing_list);
/* This needs to have block_state_mutex and job_read locked before hand. */
extern void bg_status_remove_jobs_from_failed_block(
	bg_record_t *bg_record, int inx,
	bool midplane, List *delete_list,
	List *killing_list);

#endif
