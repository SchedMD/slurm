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
	uint32_t gr_names_cnt = (cred->gr_names) ? cred->ngids : 0;
	time_t ctime = time(NULL);

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack32(cred->uid, buffer);
		pack32(cred->gid, buffer);
		packstr(cred->pw_name, buffer);
		packstr(cred->pw_gecos, buffer);
		packstr(cred->pw_dir, buffer);
		packstr(cred->pw_shell, buffer);
		pack32_array(cred->gids, cred->ngids, buffer);
		packstr_array(cred->gr_names, gr_names_cnt, buffer);

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
		packstr(cred->pw_name, buffer);
		packstr(cred->pw_gecos, buffer);
		packstr(cred->pw_dir, buffer);
		packstr(cred->pw_shell, buffer);
		pack32_array(cred->gids, cred->ngids, buffer);
		packstr_array(cred->gr_names, gr_names_cnt, buffer);

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
		packstr(cred->pw_name, buffer);
		packstr(cred->pw_gecos, buffer);
		packstr(cred->pw_dir, buffer);
		packstr(cred->pw_shell, buffer);
		pack32_array(cred->gids, cred->ngids, buffer);
		packstr_array(cred->gr_names, gr_names_cnt, buffer);

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
