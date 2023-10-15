/*****************************************************************************\
 *  cred_common.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "slurm/slurm_errno.h"

#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"
#include "src/interfaces/gres.h"

extern void cred_pack(slurm_cred_arg_t *cred, buf_t *buffer,
		      uint16_t protocol_version)
{
	uint32_t tot_core_cnt = 0;
	/*
	 * The gr_names array is optional. If the array exists the length
	 * must match that of the gids array.
	 */
	uint32_t gr_names_cnt = (cred->id.gr_names) ? cred->id.ngids : 0;
	time_t ctime = time(NULL);

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack32(cred->uid, buffer);
		pack32(cred->gid, buffer);
		packstr(cred->id.pw_name, buffer);
		packstr(cred->id.pw_gecos, buffer);
		packstr(cred->id.pw_dir, buffer);
		packstr(cred->id.pw_shell, buffer);
		pack32_array(cred->id.gids, cred->id.ngids, buffer);
		packstr_array(cred->id.gr_names, gr_names_cnt, buffer);

		(void) gres_job_state_pack(cred->job_gres_list, buffer,
					   cred->step_id.job_id, false,
					   protocol_version);
		gres_step_state_pack(cred->step_gres_list, buffer,
				     &cred->step_id, protocol_version);
		pack16(cred->job_core_spec, buffer);
		packstr(cred->job_account, buffer);
		slurm_pack_addr_array(
			cred->job_node_addrs,
			cred->job_node_addrs ? cred->job_nhosts : 0,
			buffer);
		packstr(cred->job_alias_list, buffer);
		packstr(cred->job_comment, buffer);
		packstr(cred->job_constraints, buffer);
		pack_time(cred->job_end_time, buffer);
		packstr(cred->job_extra, buffer);
		pack16(cred->job_oversubscribe, buffer);
		packstr(cred->job_partition, buffer);
		packstr(cred->job_reservation, buffer);
		pack16(cred->job_restart_cnt, buffer);
		pack_time(cred->job_start_time, buffer);
		packstr(cred->job_std_err, buffer);
		packstr(cred->job_std_in, buffer);
		packstr(cred->job_std_out, buffer);
		packstr(cred->step_hostlist, buffer);
		pack16(cred->x11, buffer);
		pack_time(ctime, buffer);

		if (cred->job_core_bitmap)
			tot_core_cnt = bit_size(cred->job_core_bitmap);
		pack32(tot_core_cnt, buffer);
		pack_bit_str_hex(cred->job_core_bitmap, buffer);
		pack_bit_str_hex(cred->step_core_bitmap, buffer);
		pack16(cred->core_array_size, buffer);
		if (cred->core_array_size) {
			pack16_array(cred->cores_per_socket,
				     cred->core_array_size,
				     buffer);
			pack16_array(cred->sockets_per_node,
				     cred->core_array_size,
				     buffer);
			pack32_array(cred->sock_core_rep_count,
				     cred->core_array_size,
				     buffer);
		}
		pack32(cred->cpu_array_count, buffer);
		if (cred->cpu_array_count) {
			pack16_array(cred->cpu_array,
				     cred->cpu_array_count,
				     buffer);
			pack32_array(cred->cpu_array_reps,
				     cred->cpu_array_count,
				     buffer);
		}
		pack32(cred->job_nhosts, buffer);
		pack32(cred->job_ntasks, buffer);
		packstr(cred->job_hostlist, buffer);
		packstr(cred->job_licenses, buffer);
		pack32(cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			pack64_array(cred->job_mem_alloc,
				     cred->job_mem_alloc_size,
				     buffer);
			pack32_array(cred->job_mem_alloc_rep_count,
				     cred->job_mem_alloc_size,
				     buffer);
		}
		pack32(cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			pack64_array(cred->step_mem_alloc,
				     cred->step_mem_alloc_size,
				     buffer);
			pack32_array(cred->step_mem_alloc_rep_count,
				     cred->step_mem_alloc_size,
				     buffer);
		}
		packstr(cred->selinux_context, buffer);
	} else if (protocol_version >= SLURM_23_02_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack32(cred->uid, buffer);
		pack32(cred->gid, buffer);
		packstr(cred->id.pw_name, buffer);
		packstr(cred->id.pw_gecos, buffer);
		packstr(cred->id.pw_dir, buffer);
		packstr(cred->id.pw_shell, buffer);
		pack32_array(cred->id.gids, cred->id.ngids, buffer);
		packstr_array(cred->id.gr_names, gr_names_cnt, buffer);

		(void) gres_job_state_pack(cred->job_gres_list, buffer,
					   cred->step_id.job_id, false,
					   protocol_version);
		gres_step_state_pack(cred->step_gres_list, buffer,
				     &cred->step_id, protocol_version);
		pack16(cred->job_core_spec, buffer);
		packstr(cred->job_account, buffer);
		packstr(cred->job_alias_list, buffer);
		packstr(cred->job_comment, buffer);
		packstr(cred->job_constraints, buffer);
		pack_time(cred->job_end_time, buffer);
		packstr(cred->job_extra, buffer);
		pack16(cred->job_oversubscribe, buffer);
		packstr(cred->job_partition, buffer);
		packstr(cred->job_reservation, buffer);
		pack16(cred->job_restart_cnt, buffer);
		pack_time(cred->job_start_time, buffer);
		packstr(cred->job_std_err, buffer);
		packstr(cred->job_std_in, buffer);
		packstr(cred->job_std_out, buffer);
		packstr(cred->step_hostlist, buffer);
		pack16(cred->x11, buffer);
		pack_time(ctime, buffer);

		if (cred->job_core_bitmap)
			tot_core_cnt = bit_size(cred->job_core_bitmap);
		pack32(tot_core_cnt, buffer);
		pack_bit_str_hex(cred->job_core_bitmap, buffer);
		pack_bit_str_hex(cred->step_core_bitmap, buffer);
		pack16(cred->core_array_size, buffer);
		if (cred->core_array_size) {
			pack16_array(cred->cores_per_socket,
				     cred->core_array_size,
				     buffer);
			pack16_array(cred->sockets_per_node,
				     cred->core_array_size,
				     buffer);
			pack32_array(cred->sock_core_rep_count,
				     cred->core_array_size,
				     buffer);
		}
		pack32(cred->cpu_array_count, buffer);
		if (cred->cpu_array_count) {
			pack16_array(cred->cpu_array,
				     cred->cpu_array_count,
				     buffer);
			pack32_array(cred->cpu_array_reps,
				     cred->cpu_array_count,
				     buffer);
		}
		pack32(cred->job_nhosts, buffer);
		pack32(cred->job_ntasks, buffer);
		packstr(cred->job_hostlist, buffer);
		packstr(cred->job_licenses, buffer);
		pack32(cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			pack64_array(cred->job_mem_alloc,
				     cred->job_mem_alloc_size,
				     buffer);
			pack32_array(cred->job_mem_alloc_rep_count,
				     cred->job_mem_alloc_size,
				     buffer);
		}
		pack32(cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			pack64_array(cred->step_mem_alloc,
				     cred->step_mem_alloc_size,
				     buffer);
			pack32_array(cred->step_mem_alloc_rep_count,
				     cred->step_mem_alloc_size,
				     buffer);
		}
		packstr(cred->selinux_context, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack32(cred->uid, buffer);
		pack32(cred->gid, buffer);
		packstr(cred->id.pw_name, buffer);
		packstr(cred->id.pw_gecos, buffer);
		packstr(cred->id.pw_dir, buffer);
		packstr(cred->id.pw_shell, buffer);
		pack32_array(cred->id.gids, cred->id.ngids, buffer);
		packstr_array(cred->id.gr_names, gr_names_cnt, buffer);

		(void) gres_job_state_pack(cred->job_gres_list, buffer,
					   cred->step_id.job_id, false,
					   protocol_version);
		gres_step_state_pack(cred->step_gres_list, buffer,
				     &cred->step_id, protocol_version);
		pack16(cred->job_core_spec, buffer);
		packstr(cred->job_account, buffer);
		packstr(cred->job_alias_list, buffer);
		packstr(cred->job_comment, buffer);
		packstr(cred->job_constraints, buffer);
		packstr(cred->job_partition, buffer);
		packstr(cred->job_reservation, buffer);
		pack16(cred->job_restart_cnt, buffer);
		packstr(cred->job_std_err, buffer);
		packstr(cred->job_std_in, buffer);
		packstr(cred->job_std_out, buffer);
		packstr(cred->step_hostlist, buffer);
		pack16(cred->x11, buffer);
		pack_time(ctime, buffer);

		if (cred->job_core_bitmap)
			tot_core_cnt = bit_size(cred->job_core_bitmap);
		pack32(tot_core_cnt, buffer);
		pack_bit_str_hex(cred->job_core_bitmap, buffer);
		pack_bit_str_hex(cred->step_core_bitmap, buffer);
		pack16(cred->core_array_size, buffer);
		if (cred->core_array_size) {
			pack16_array(cred->cores_per_socket,
				     cred->core_array_size,
				     buffer);
			pack16_array(cred->sockets_per_node,
				     cred->core_array_size,
				     buffer);
			pack32_array(cred->sock_core_rep_count,
				     cred->core_array_size,
				     buffer);
		}
		pack32(cred->cpu_array_count, buffer);
		if (cred->cpu_array_count) {
			pack16_array(cred->cpu_array,
				     cred->cpu_array_count,
				     buffer);
			pack32_array(cred->cpu_array_reps,
				     cred->cpu_array_count,
				     buffer);
		}
		pack32(cred->job_nhosts, buffer);
		pack32(cred->job_ntasks, buffer);
		packstr(cred->job_hostlist, buffer);
		pack32(cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			pack64_array(cred->job_mem_alloc,
				     cred->job_mem_alloc_size,
				     buffer);
			pack32_array(cred->job_mem_alloc_rep_count,
				     cred->job_mem_alloc_size,
				     buffer);
		}
		pack32(cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			pack64_array(cred->step_mem_alloc,
				     cred->step_mem_alloc_size,
				     buffer);
			pack32_array(cred->step_mem_alloc_rep_count,
				     cred->step_mem_alloc_size,
				     buffer);
		}
		packstr(cred->selinux_context, buffer);
	}
}

extern int cred_unpack(void **out, buf_t *buffer, uint16_t protocol_version)
{
	uint32_t u32_ngids, len, uint32_tmp;
	slurm_cred_t *cred = NULL;
	slurm_cred_arg_t *cred_arg = NULL;
	char *bit_fmt_str = NULL;
	uint32_t tot_core_cnt;

	cred = slurm_cred_alloc(true);
	cred_arg = cred->arg;
	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&cred_arg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&cred_arg->uid, buffer);
		if (cred_arg->uid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid user nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpack32(&cred_arg->gid, buffer);
		if (cred_arg->gid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid group nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpackstr(&cred_arg->id.pw_name, buffer);
		safe_unpackstr(&cred_arg->id.pw_gecos, buffer);
		safe_unpackstr(&cred_arg->id.pw_dir, buffer);
		safe_unpackstr(&cred_arg->id.pw_shell, buffer);
		safe_unpack32_array(&cred_arg->id.gids, &u32_ngids, buffer);
		cred_arg->id.ngids = u32_ngids;
		safe_unpackstr_array(&cred_arg->id.gr_names, &u32_ngids, buffer);
		if (u32_ngids && cred_arg->id.ngids != u32_ngids) {
			error("%s: mismatch on gr_names array, %u != %u",
			      __func__, u32_ngids, cred_arg->id.ngids);
			goto unpack_error;
		}
		if (gres_job_state_unpack(&cred_arg->job_gres_list, buffer,
					  cred_arg->step_id.job_id,
					  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_step_state_unpack(&cred_arg->step_gres_list,
					   buffer, &cred_arg->step_id,
					   protocol_version)
		    != SLURM_SUCCESS) {
			goto unpack_error;
		}
		safe_unpack16(&cred_arg->job_core_spec, buffer);
		safe_unpackstr(&cred_arg->job_account, buffer);
		if (slurm_unpack_addr_array(&cred_arg->job_node_addrs,
					    &uint32_tmp, buffer))
			goto unpack_error;
		safe_unpackstr(&cred_arg->job_alias_list, buffer);
		safe_unpackstr(&cred_arg->job_comment, buffer);
		safe_unpackstr(&cred_arg->job_constraints, buffer);
		safe_unpack_time(&cred_arg->job_end_time, buffer);
		safe_unpackstr(&cred_arg->job_extra, buffer);
		safe_unpack16(&cred_arg->job_oversubscribe, buffer);
		safe_unpackstr(&cred_arg->job_partition, buffer);
		safe_unpackstr(&cred_arg->job_reservation, buffer);
		safe_unpack16(&cred_arg->job_restart_cnt, buffer);
		safe_unpack_time(&cred_arg->job_start_time, buffer);
		safe_unpackstr(&cred_arg->job_std_err, buffer);
		safe_unpackstr(&cred_arg->job_std_in, buffer);
		safe_unpackstr(&cred_arg->job_std_out, buffer);
		safe_unpackstr(&cred_arg->step_hostlist, buffer);
		safe_unpack16(&cred_arg->x11, buffer);
		safe_unpack_time(&cred->ctime, buffer);
		safe_unpack32(&tot_core_cnt, buffer);
		unpack_bit_str_hex(&cred_arg->job_core_bitmap, buffer);
		unpack_bit_str_hex(&cred_arg->step_core_bitmap, buffer);
		safe_unpack16(&cred_arg->core_array_size, buffer);
		if (cred_arg->core_array_size) {
			safe_unpack16_array(&cred_arg->cores_per_socket, &len,
					    buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
			safe_unpack16_array(&cred_arg->sockets_per_node, &len,
					    buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
			safe_unpack32_array(&cred_arg->sock_core_rep_count,
					    &len, buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
		}
		safe_unpack32(&cred_arg->cpu_array_count, buffer);
		if (cred_arg->cpu_array_count) {
			safe_unpack16_array(&cred_arg->cpu_array, &len, buffer);
			if (len != cred_arg->cpu_array_count)
				goto unpack_error;
			safe_unpack32_array(&cred_arg->cpu_array_reps, &len,
					    buffer);
			if (len != cred_arg->cpu_array_count)
				goto unpack_error;
		}
		safe_unpack32(&cred_arg->job_nhosts, buffer);
		safe_unpack32(&cred_arg->job_ntasks, buffer);
		safe_unpackstr(&cred_arg->job_hostlist, buffer);
		safe_unpackstr(&cred_arg->job_licenses, buffer);

		safe_unpack32(&cred_arg->job_mem_alloc_size, buffer);
		if (cred_arg->job_mem_alloc_size) {
			safe_unpack64_array(&cred_arg->job_mem_alloc, &len,
					    buffer);
			if (len != cred_arg->job_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred_arg->job_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred_arg->job_mem_alloc_size)
				goto unpack_error;

		}

		safe_unpack32(&cred_arg->step_mem_alloc_size, buffer);
		if (cred_arg->step_mem_alloc_size) {
			safe_unpack64_array(&cred_arg->step_mem_alloc, &len,
					    buffer);
			if (len != cred_arg->step_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred_arg->step_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred_arg->step_mem_alloc_size)
				goto unpack_error;
		}

		safe_unpackstr(&cred_arg->selinux_context, buffer);
	} else if (protocol_version >= SLURM_23_02_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&cred_arg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&cred_arg->uid, buffer);
		if (cred_arg->uid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid user nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpack32(&cred_arg->gid, buffer);
		if (cred_arg->gid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid group nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpackstr(&cred_arg->id.pw_name, buffer);
		safe_unpackstr(&cred_arg->id.pw_gecos, buffer);
		safe_unpackstr(&cred_arg->id.pw_dir, buffer);
		safe_unpackstr(&cred_arg->id.pw_shell, buffer);
		safe_unpack32_array(&cred_arg->id.gids, &u32_ngids, buffer);
		cred_arg->id.ngids = u32_ngids;
		safe_unpackstr_array(&cred_arg->id.gr_names, &u32_ngids, buffer);
		if (u32_ngids && cred_arg->id.ngids != u32_ngids) {
			error("%s: mismatch on gr_names array, %u != %u",
			      __func__, u32_ngids, cred_arg->id.ngids);
			goto unpack_error;
		}
		if (gres_job_state_unpack(&cred_arg->job_gres_list, buffer,
					  cred_arg->step_id.job_id,
					  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_step_state_unpack(&cred_arg->step_gres_list,
					   buffer, &cred_arg->step_id,
					   protocol_version)
		    != SLURM_SUCCESS) {
			goto unpack_error;
		}
		safe_unpack16(&cred_arg->job_core_spec, buffer);
		safe_unpackstr(&cred_arg->job_account, buffer);
		safe_unpackstr(&cred_arg->job_alias_list, buffer);
		safe_unpackstr(&cred_arg->job_comment, buffer);
		safe_unpackstr(&cred_arg->job_constraints, buffer);
		safe_unpack_time(&cred_arg->job_end_time, buffer);
		safe_unpackstr(&cred_arg->job_extra, buffer);
		safe_unpack16(&cred_arg->job_oversubscribe, buffer);
		safe_unpackstr(&cred_arg->job_partition, buffer);
		safe_unpackstr(&cred_arg->job_reservation, buffer);
		safe_unpack16(&cred_arg->job_restart_cnt, buffer);
		safe_unpack_time(&cred_arg->job_start_time, buffer);
		safe_unpackstr(&cred_arg->job_std_err, buffer);
		safe_unpackstr(&cred_arg->job_std_in, buffer);
		safe_unpackstr(&cred_arg->job_std_out, buffer);
		safe_unpackstr(&cred_arg->step_hostlist, buffer);
		safe_unpack16(&cred_arg->x11, buffer);
		safe_unpack_time(&cred->ctime, buffer);
		safe_unpack32(&tot_core_cnt, buffer);
		unpack_bit_str_hex(&cred_arg->job_core_bitmap, buffer);
		unpack_bit_str_hex(&cred_arg->step_core_bitmap, buffer);
		safe_unpack16(&cred_arg->core_array_size, buffer);
		if (cred_arg->core_array_size) {
			safe_unpack16_array(&cred_arg->cores_per_socket, &len,
					    buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
			safe_unpack16_array(&cred_arg->sockets_per_node, &len,
					    buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
			safe_unpack32_array(&cred_arg->sock_core_rep_count,
					    &len, buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
		}
		safe_unpack32(&cred_arg->cpu_array_count, buffer);
		if (cred_arg->cpu_array_count) {
			safe_unpack16_array(&cred_arg->cpu_array, &len, buffer);
			if (len != cred_arg->cpu_array_count)
				goto unpack_error;
			safe_unpack32_array(&cred_arg->cpu_array_reps, &len,
					    buffer);
			if (len != cred_arg->cpu_array_count)
				goto unpack_error;
		}
		safe_unpack32(&cred_arg->job_nhosts, buffer);
		safe_unpack32(&cred_arg->job_ntasks, buffer);
		safe_unpackstr(&cred_arg->job_hostlist, buffer);
		safe_unpackstr(&cred_arg->job_licenses, buffer);

		safe_unpack32(&cred_arg->job_mem_alloc_size, buffer);
		if (cred_arg->job_mem_alloc_size) {
			safe_unpack64_array(&cred_arg->job_mem_alloc, &len,
					    buffer);
			if (len != cred_arg->job_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred_arg->job_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred_arg->job_mem_alloc_size)
				goto unpack_error;

		}

		safe_unpack32(&cred_arg->step_mem_alloc_size, buffer);
		if (cred_arg->step_mem_alloc_size) {
			safe_unpack64_array(&cred_arg->step_mem_alloc, &len,
					    buffer);
			if (len != cred_arg->step_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred_arg->step_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred_arg->step_mem_alloc_size)
				goto unpack_error;
		}

		safe_unpackstr(&cred_arg->selinux_context, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&cred_arg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&cred_arg->uid, buffer);
		if (cred_arg->uid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid user nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpack32(&cred_arg->gid, buffer);
		if (cred_arg->gid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid group nobody",
			      __func__);
			goto unpack_error;
		}
		safe_unpackstr(&cred_arg->id.pw_name, buffer);
		safe_unpackstr(&cred_arg->id.pw_gecos, buffer);
		safe_unpackstr(&cred_arg->id.pw_dir, buffer);
		safe_unpackstr(&cred_arg->id.pw_shell, buffer);
		safe_unpack32_array(&cred_arg->id.gids, &u32_ngids, buffer);
		cred_arg->id.ngids = u32_ngids;
		safe_unpackstr_array(&cred_arg->id.gr_names, &u32_ngids, buffer);
		if (u32_ngids && cred_arg->id.ngids != u32_ngids) {
			error("%s: mismatch on gr_names array, %u != %u",
			      __func__, u32_ngids, cred_arg->id.ngids);
			goto unpack_error;
		}
		if (gres_job_state_unpack(&cred_arg->job_gres_list, buffer,
					  cred_arg->step_id.job_id,
					  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_step_state_unpack(&cred_arg->step_gres_list,
					   buffer, &cred_arg->step_id,
					   protocol_version)
		    != SLURM_SUCCESS) {
			goto unpack_error;
		}
		safe_unpack16(&cred_arg->job_core_spec, buffer);
		safe_unpackstr(&cred_arg->job_account, buffer);
		safe_unpackstr(&cred_arg->job_alias_list, buffer);
		safe_unpackstr(&cred_arg->job_comment, buffer);
		safe_unpackstr(&cred_arg->job_constraints, buffer);
		safe_unpackstr(&cred_arg->job_partition, buffer);
		safe_unpackstr(&cred_arg->job_reservation, buffer);
		safe_unpack16(&cred_arg->job_restart_cnt, buffer);
		safe_unpackstr(&cred_arg->job_std_err, buffer);
		safe_unpackstr(&cred_arg->job_std_in, buffer);
		safe_unpackstr(&cred_arg->job_std_out, buffer);
		safe_unpackstr(&cred_arg->step_hostlist, buffer);
		safe_unpack16(&cred_arg->x11, buffer);
		safe_unpack_time(&cred->ctime, buffer);
		safe_unpack32(&tot_core_cnt, buffer);
		unpack_bit_str_hex(&cred_arg->job_core_bitmap, buffer);
		unpack_bit_str_hex(&cred_arg->step_core_bitmap, buffer);
		safe_unpack16(&cred_arg->core_array_size, buffer);
		if (cred_arg->core_array_size) {
			safe_unpack16_array(&cred_arg->cores_per_socket, &len,
					    buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
			safe_unpack16_array(&cred_arg->sockets_per_node, &len,
					    buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
			safe_unpack32_array(&cred_arg->sock_core_rep_count,
					    &len, buffer);
			if (len != cred_arg->core_array_size)
				goto unpack_error;
		}
		safe_unpack32(&cred_arg->cpu_array_count, buffer);
		if (cred_arg->cpu_array_count) {
			safe_unpack16_array(&cred_arg->cpu_array, &len, buffer);
			if (len != cred_arg->cpu_array_count)
				goto unpack_error;
			safe_unpack32_array(&cred_arg->cpu_array_reps, &len,
					    buffer);
			if (len != cred_arg->cpu_array_count)
				goto unpack_error;
		}
		safe_unpack32(&cred_arg->job_nhosts, buffer);
		safe_unpack32(&cred_arg->job_ntasks, buffer);
		safe_unpackstr(&cred_arg->job_hostlist, buffer);

		safe_unpack32(&cred_arg->job_mem_alloc_size, buffer);
		if (cred_arg->job_mem_alloc_size) {
			safe_unpack64_array(&cred_arg->job_mem_alloc, &len,
					    buffer);
			if (len != cred_arg->job_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred_arg->job_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred_arg->job_mem_alloc_size)
				goto unpack_error;

		}

		safe_unpack32(&cred_arg->step_mem_alloc_size, buffer);
		if (cred_arg->step_mem_alloc_size) {
			safe_unpack64_array(&cred_arg->step_mem_alloc, &len,
					    buffer);
			if (len != cred_arg->step_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred_arg->step_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred_arg->step_mem_alloc_size)
				goto unpack_error;
		}

		safe_unpackstr(&cred_arg->selinux_context, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	*out = cred;
	return SLURM_SUCCESS;

unpack_error:
	xfree(bit_fmt_str);
	slurm_cred_destroy(cred);
	return SLURM_ERROR;
}
