/*****************************************************************************\
 *  slurmdb_pack.h - un/pack definitions used by slurmdb api
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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
\*****************************************************************************/
#ifndef _SLURMDB_PACK_H
#define _SLURMDB_PACK_H

#include "slurm/slurmdb.h"
#include "slurmdb_defs.h"
#include "pack.h"
#include "xmalloc.h"
#include "xstring.h"

extern void slurmdb_pack_user_rec(void *in,
				  uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_user_rec(void **object,
				   uint16_t protocol_version, Buf buffer);
extern void slurmdb_pack_account_rec(void *in,
				     uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_account_rec(void **object, uint16_t protocol_version,
				      Buf buffer);
extern void slurmdb_pack_coord_rec(void *in,
				   uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_coord_rec(void **object, uint16_t protocol_version,
				    Buf buffer);
extern void slurmdb_pack_cluster_accounting_rec(void *in,
						uint16_t protocol_version,
						Buf buffer);
extern int slurmdb_unpack_cluster_accounting_rec(void **object,
						 uint16_t protocol_version,
						 Buf buffer);
extern void slurmdb_pack_clus_res_rec(void *in,
				      uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_clus_res_rec(void **object, uint16_t protocol_version,
				       Buf buffer);
extern void slurmdb_pack_cluster_rec(void *in,
				     uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_cluster_rec(void **object, uint16_t protocol_version,
				      Buf buffer);
extern void slurmdb_pack_federation_rec(void *in, uint16_t protocol_version,
					Buf buffer);
extern int slurmdb_unpack_federation_rec(void **object,
					 uint16_t protocol_version, Buf buffer);
extern void slurmdb_pack_accounting_rec(void *in,
					uint16_t protocol_version,
					Buf buffer);
extern int slurmdb_unpack_accounting_rec(void **object,
					 uint16_t protocol_version,
					 Buf buffer);
extern void slurmdb_pack_assoc_rec(void *in,
				   uint16_t protocol_version,
				   Buf buffer);
extern int slurmdb_unpack_assoc_rec_members(slurmdb_assoc_rec_t *object_ptr,
					    uint16_t protocol_version,
					    Buf buffer);
extern int slurmdb_unpack_assoc_rec(void **object, uint16_t protocol_version,
				    Buf buffer);
extern void slurmdb_pack_assoc_usage(void *in, uint16_t protocol_version,
				     Buf buffer);
extern int slurmdb_unpack_assoc_usage(void **object, uint16_t protocol_version,
				      Buf buffer);
extern void slurmdb_pack_assoc_rec_with_usage(void *in,
					      uint16_t protocol_version,
					      Buf buffer);
extern int slurmdb_unpack_assoc_rec_with_usage(void **object,
					       uint16_t protocol_version,
					       Buf buffer);
extern void slurmdb_pack_event_rec(void *in,
				   uint16_t protocol_version,
				   Buf buffer);
extern int slurmdb_unpack_event_rec(void **object, uint16_t protocol_version,
				    Buf buffer);
extern void slurmdb_pack_qos_rec(void *in,
				 uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_qos_rec(void **object,
				  uint16_t protocol_version, Buf buffer);
extern void slurmdb_pack_qos_usage(void *in, uint16_t protocol_version,
				   Buf buffer);
extern int slurmdb_unpack_qos_usage(void **object, uint16_t protocol_version,
				    Buf buffer);
extern void slurmdb_pack_qos_rec_with_usage(void *in, uint16_t protocol_version,
					    Buf buffer);
extern int slurmdb_unpack_qos_rec_with_usage(void **object,
					     uint16_t protocol_version,
					     Buf buffer);
extern void slurmdb_pack_reservation_rec(void *in,
					 uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_reservation_rec(void **object,
					  uint16_t protocol_version,
					  Buf buffer);
extern void slurmdb_pack_res_rec(void *in, uint16_t protocol_version,
				 Buf buffer);
extern int slurmdb_unpack_res_rec(void **object, uint16_t protocol_version,
				  Buf buffer);
extern void slurmdb_pack_txn_rec(void *in,
				 uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_txn_rec(void **object,
				  uint16_t protocol_version, Buf buffer);
extern void slurmdb_pack_wckey_rec(void *in,
				   uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_wckey_rec(void **object, uint16_t protocol_version,
				    Buf buffer);
extern void slurmdb_pack_archive_rec(void *in,
				     uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_archive_rec(void **object, uint16_t protocol_version,
				      Buf buffer);
extern void slurmdb_pack_tres_cond(void *in, uint16_t protocol_version,
				   Buf buffer);
extern int slurmdb_unpack_tres_cond(void **object, uint16_t protocol_version,
				     Buf buffer);
extern void slurmdb_pack_tres_rec(void *in, uint16_t protocol_version,
				  Buf buffer);
extern int slurmdb_unpack_tres_rec_noalloc(
	slurmdb_tres_rec_t *object_ptr, uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_tres_rec(void **object, uint16_t protocol_version,
				    Buf buffer);

extern void slurmdb_pack_user_cond(void *in,
				   uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_user_cond(void **object, uint16_t protocol_version,
				    Buf buffer);
extern void slurmdb_pack_account_cond(void *in,
				      uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_account_cond(void **object, uint16_t protocol_version,
				       Buf buffer);
extern void slurmdb_pack_cluster_cond(void *in,
				      uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_cluster_cond(void **object, uint16_t protocol_version,
				       Buf buffer);
extern void slurmdb_pack_federation_cond(void *in, uint16_t protocol_version,
					 Buf buffer);
extern int slurmdb_unpack_federation_cond(void **object,
					  uint16_t protocol_version,
					  Buf buffer);
extern void slurmdb_pack_assoc_cond(void *in,
				    uint16_t protocol_version,
				    Buf buffer);
extern int slurmdb_unpack_assoc_cond(void **object, uint16_t protocol_version,
				     Buf buffer);
extern void slurmdb_pack_event_cond(void *in,
				    uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_event_cond(void **object, uint16_t protocol_version,
				     Buf buffer);
extern void slurmdb_pack_job_cond(void *in,
				  uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_job_cond(void **object, uint16_t protocol_version,
				   Buf buffer);
extern void slurmdb_pack_job_modify_cond(void *in,
					 uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_job_modify_cond(void **object,
					  uint16_t protocol_version,
					  Buf buffer);
extern void slurmdb_pack_job_rec(void *object,
				 uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_job_rec(void **job, uint16_t protocol_version,
				  Buf buffer);
extern void slurmdb_pack_qos_cond(void *in,
				  uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_qos_cond(void **object, uint16_t protocol_version,
				   Buf buffer);
extern void slurmdb_pack_reservation_cond(void *in,
					  uint16_t protocol_version,
					  Buf buffer);
extern int slurmdb_unpack_reservation_cond(void **object,
					   uint16_t protocol_version,
					   Buf buffer);
extern void slurmdb_pack_selected_step(slurmdb_selected_step_t *step,
				       uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_selected_step(slurmdb_selected_step_t **step,
					uint16_t protocol_version, Buf buffer);
extern void slurmdb_pack_step_rec(slurmdb_step_rec_t *step,
				  uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_step_rec(slurmdb_step_rec_t **step,
				   uint16_t protocol_version, Buf buffer);
extern void slurmdb_pack_res_cond(void *in, uint16_t protocol_version,
				  Buf buffer);
extern int slurmdb_unpack_res_cond(void **object, uint16_t protocol_version,
				   Buf buffer);
extern void slurmdb_pack_txn_cond(void *in,
				  uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_txn_cond(void **object, uint16_t protocol_version,
				   Buf buffer);
extern void slurmdb_pack_wckey_cond(void *in,
				    uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_wckey_cond(void **object, uint16_t protocol_version,
				     Buf buffer);
extern void slurmdb_pack_archive_cond(void *in,
				      uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_archive_cond(void **object, uint16_t protocol_version,
				       Buf buffer);
extern void slurmdb_pack_update_object(slurmdb_update_object_t *object,
				       uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_update_object(slurmdb_update_object_t **object,
					uint16_t protocol_version, Buf buffer);
extern void slurmdb_pack_used_limits(void *in, uint32_t tres_cnt,
				     uint16_t protocol_version, Buf buffer);
extern int slurmdb_unpack_used_limits(void **object, uint32_t tres_cnt,
				      uint16_t protocol_version, Buf buffer);

extern void pack_update_shares_used(void *in,
				    uint16_t protocol_version, Buf buffer);
extern int unpack_update_shares_used(void **object, uint16_t protocol_version,
				     Buf buffer);

extern void slurmdb_pack_stats_msg(void *object, uint16_t protocol_version,
				   Buf buffer);
extern int slurmdb_unpack_stats_msg(void **object, uint16_t protocol_version,
				    Buf buffer);

#endif
