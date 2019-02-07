/*****************************************************************************\
 *  fed_mgr.h - functions for federations
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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
\*****************************************************************************/

#ifndef _SLURM_FED_MGR_H
#define _SLURM_FED_MGR_H

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

extern slurmdb_federation_rec_t *fed_mgr_fed_rec;
extern slurmdb_cluster_rec_t    *fed_mgr_cluster_rec;

extern void      add_fed_job_info(struct job_record *job_ptr);
extern int       fed_mgr_add_sibling_conn(slurm_persist_conn_t *persist_conn,
					  char **out_buffer);
extern char     *fed_mgr_cluster_ids_to_names(uint64_t cluster_ids);
extern int       fed_mgr_fini(void);
extern uint32_t  fed_mgr_get_cluster_id(uint32_t id);
extern char     *fed_mgr_get_cluster_name(uint32_t id);
extern slurmdb_cluster_rec_t *fed_mgr_get_cluster_by_id(uint32_t id);
extern slurmdb_cluster_rec_t *fed_mgr_get_cluster_by_name(char *sib_name);
extern uint32_t  fed_mgr_get_job_id(uint32_t orig);
extern uint32_t  fed_mgr_get_local_id(uint32_t id);
extern int       fed_mgr_init(void *db_conn);
extern int       fed_mgr_is_origin_job(struct job_record *job_ptr);
extern bool      fed_mgr_is_tracker_only_job(struct job_record *job_ptr);
extern int       fed_mgr_job_allocate(slurm_msg_t *msg,
				      job_desc_msg_t *job_desc, bool alloc_only,
				      uid_t uid, uint16_t protocol_version,
				      uint32_t *job_id_ptr, int *alloc_code,
				      char **err_msg);
extern int       fed_mgr_job_cancel(struct job_record *job_ptr, uint16_t signal,
				    uint16_t flags, uid_t uid,
				    bool kill_viable);
extern int       fed_mgr_job_complete(struct job_record *job_ptr,
				      uint32_t return_code, time_t start_time);
extern bool      fed_mgr_job_is_locked(struct job_record *job_ptr);
extern bool      fed_mgr_job_is_self_owned(struct job_record *job_ptr);
extern int       fed_mgr_job_lock(struct job_record *job_ptr);
extern int       fed_mgr_job_lock_set(uint32_t job_id, uint32_t cluster_id);
extern int       fed_mgr_job_lock_unset(uint32_t job_id, uint32_t cluster_id);
extern int       fed_mgr_job_unlock(struct job_record *job_ptr);
extern int       fed_mgr_job_requeue(struct job_record *job_ptr);
extern int       fed_mgr_job_requeue_test(struct job_record *job_ptr,
					  uint32_t flags);
extern int       fed_mgr_job_revoke(struct job_record *job_ptr,
				    bool job_complete, uint32_t exit_code,
				    time_t start_time);
extern int       fed_mgr_job_revoke_sibs(struct job_record *job_ptr);
extern int       fed_mgr_job_start(struct job_record *job_ptr,
				   time_t start_time);
extern int       fed_mgr_q_sib_msg(slurm_msg_t *sib_msg, uint32_t rpc_uid);
extern int       fed_mgr_remove_active_sibling(uint32_t job_id, char *sib_name);
extern void      fed_mgr_remove_fed_job_info(uint32_t job_id);
extern bool      fed_mgr_sibs_synced();
extern int       fed_mgr_state_save(char *state_save_location);
extern int       fed_mgr_update_job(uint32_t job_id, job_desc_msg_t *job_specs,
				    uint64_t update_sibs, uid_t uid);
extern int       fed_mgr_update_job_clusters(struct job_record *job_ptr,
					     char *spec_clusters);
extern int       fed_mgr_update_job_cluster_features(struct job_record *job_ptr,
						     char *req_features);
extern int       fed_mgr_update_feds(slurmdb_update_object_t *update);
#endif /* _SLURM_FED_MGR_H */
