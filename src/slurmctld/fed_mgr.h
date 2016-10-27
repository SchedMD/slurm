/*****************************************************************************\
 *  fed_mgr.h - functions for federations
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#ifndef _SLURM_FED_MGR_H
#define _SLURM_FED_MGR_H

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

extern slurmdb_federation_rec_t *fed_mgr_fed_rec;

extern int       fed_mgr_add_sibling_conn(slurm_persist_conn_t *persist_conn,
					  char **out_buffer);
extern char     *fed_mgr_cluster_ids_to_names(uint64_t cluster_ids);
extern int       fed_mgr_fini();
extern uint32_t  fed_mgr_get_cluster_id(uint32_t id);
extern char     *fed_mgr_get_cluster_name(uint32_t id);
extern uint32_t  fed_mgr_get_job_id(uint32_t orig);
extern uint32_t  fed_mgr_get_local_id(uint32_t id);
extern int       fed_mgr_init(void *db_conn);
extern bool      fed_mgr_is_active();
extern bool      fed_mgr_is_tracker_only_job(struct job_record *job_ptr);
extern int       fed_mgr_job_allocate(slurm_msg_t *msg,
				      job_desc_msg_t *job_desc, bool alloc_only,
				      uid_t uid, uint16_t protocol_version,
				      uint32_t *job_id_ptr, int *alloc_code,
				      char **err_msg);
extern int       fed_mgr_sib_will_run(slurm_msg_t *msg,
				      job_desc_msg_t *job_desc, uid_t uid,
				      will_run_response_msg_t **resp);
extern slurmdb_federation_rec_t *fed_mgr_state_load(char *state_save_location);
extern int       fed_mgr_state_save(char *state_save_location);
extern int       fed_mgr_update_feds(slurmdb_update_object_t *update);
#endif /* _SLURM_FED_MGR_H */
