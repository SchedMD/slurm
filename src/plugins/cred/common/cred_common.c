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
#include "src/common/group_cache.h"
#include "src/common/identity.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"
#include "src/interfaces/gres.h"

extern slurm_cred_t *cred_create(slurm_cred_arg_t *cred,
				 uint16_t protocol_version)
{
	slurm_cred_t *credential;
	uint32_t tot_core_cnt = 0;
	buf_t *buffer;

	time_t ctime = time(NULL);

	credential = slurm_cred_alloc(false);
	credential->buffer = buffer = init_buf(4096);
	credential->buf_version = protocol_version;

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack_identity(cred->id, buffer, protocol_version);

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
		pack16(cred->job_x11, buffer);
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
		packstr(cred->job_selinux_context, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack_identity(cred->id, buffer, protocol_version);

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
		pack16(cred->job_x11, buffer);
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
		packstr(cred->job_selinux_context, buffer);
	}

	return credential;
}

extern int cred_unpack(void **out, buf_t *buffer, uint16_t protocol_version)
{
	uint32_t len, uint32_tmp;
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

		if (unpack_identity(&cred_arg->id, buffer, protocol_version))
			goto unpack_error;

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
		safe_unpack16(&cred_arg->job_x11, buffer);
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

		safe_unpackstr(&cred_arg->job_selinux_context, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&cred_arg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		if (unpack_identity(&cred_arg->id, buffer, protocol_version))
			goto unpack_error;

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
		safe_unpack16(&cred_arg->job_x11, buffer);
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

		safe_unpackstr(&cred_arg->job_selinux_context, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	cred_arg->uid = cred_arg->id->uid;
	cred_arg->gid = cred_arg->id->gid;

	*out = cred;
	return SLURM_SUCCESS;

unpack_error:
	xfree(bit_fmt_str);
	slurm_cred_destroy(cred);
	return SLURM_ERROR;
}

extern slurm_cred_t *cred_unpack_with_signature(buf_t *buffer,
						uint16_t protocol_version)
{
	slurm_cred_t *credential = NULL;
	uint32_t cred_start, cred_len;

	cred_start = get_buf_offset(buffer);

	if ((cred_unpack((void **) &credential, buffer, protocol_version)))
		goto unpack_error;

	credential->sig_offset = get_buf_offset(buffer) - cred_start;

	/* signature follows the main buffer */
	safe_unpackstr(&credential->signature, buffer);

	/*
	 * Both srun and slurmd will unpack the credential just to pack it
	 * again. Hold onto a buffer with the pre-packed representation.
	 */
	if (!running_in_slurmstepd()) {
		cred_len = get_buf_offset(buffer) - cred_start;
		credential->buffer = init_buf(cred_len);
		credential->buf_version = protocol_version;
		memcpy(credential->buffer->head,
		       get_buf_data(buffer) + cred_start,
		       cred_len);
		credential->buffer->processed = cred_len;
	}

	return credential;

unpack_error:
	slurm_cred_destroy(credential);
	return NULL;
}

extern buf_t *sbcast_cred_pack(sbcast_cred_arg_t *sbcast_cred,
			       uint16_t protocol_version)
{
	buf_t *buffer = init_buf(4096);
	time_t now = time(NULL);

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		pack_identity(sbcast_cred->id, buffer, protocol_version);
		pack_time(now, buffer);
		pack_time(sbcast_cred->expiration, buffer);
		pack32(sbcast_cred->job_id, buffer);
		pack32(sbcast_cred->het_job_id, buffer);
		pack32(sbcast_cred->step_id, buffer);
		packstr(sbcast_cred->nodes, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(now, buffer);
		pack_time(sbcast_cred->expiration, buffer);
		pack32(sbcast_cred->job_id, buffer);
		pack32(sbcast_cred->het_job_id, buffer);
		pack32(sbcast_cred->step_id, buffer);
		pack32(sbcast_cred->id->uid, buffer);
		pack32(sbcast_cred->id->gid, buffer);
		packstr(sbcast_cred->id->pw_name, buffer);
		pack32_array(sbcast_cred->id->gids, sbcast_cred->id->ngids,
			     buffer);
		packstr(sbcast_cred->nodes, buffer);
	}

	return buffer;
}

extern sbcast_cred_t *sbcast_cred_unpack(buf_t *buffer, uint32_t *siglen,
					 uint16_t protocol_version)
{
	sbcast_cred_t *sbcast_cred = xmalloc(sizeof(*sbcast_cred));
	uint32_t cred_start = get_buf_offset(buffer);
	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;
	char *user_name = NULL;
	uint32_t ngids = 0, *gids = NULL;

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		if (unpack_identity(&sbcast_cred->arg.id, buffer,
				    protocol_version))
			goto unpack_error;
		uid = sbcast_cred->arg.id->uid;
		gid = sbcast_cred->arg.id->gid;
		safe_unpack_time(&sbcast_cred->ctime, buffer);
		safe_unpack_time(&sbcast_cred->arg.expiration, buffer);
		safe_unpack32(&sbcast_cred->arg.job_id, buffer);
		safe_unpack32(&sbcast_cred->arg.het_job_id, buffer);
		safe_unpack32(&sbcast_cred->arg.step_id, buffer);
		safe_unpackstr(&sbcast_cred->arg.nodes, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&sbcast_cred->ctime, buffer);
		safe_unpack_time(&sbcast_cred->arg.expiration, buffer);
		safe_unpack32(&sbcast_cred->arg.job_id, buffer);
		safe_unpack32(&sbcast_cred->arg.het_job_id, buffer);
		safe_unpack32(&sbcast_cred->arg.step_id, buffer);
		safe_unpack32(&uid, buffer);
		safe_unpack32(&gid, buffer);
		safe_unpackstr(&user_name, buffer);
		safe_unpack32_array(&gids, &ngids, buffer);
		safe_unpackstr(&sbcast_cred->arg.nodes, buffer);
	} else
		goto unpack_error;

	if (sbcast_cred->arg.id && !sbcast_cred->arg.id->pw_name) {
		debug2("%s: need to fetch identity", __func__);
		FREE_NULL_IDENTITY(sbcast_cred->arg.id);
	}

	if (!user_name && !sbcast_cred->arg.id) {
		sbcast_cred->arg.id = fetch_identity(uid, gid, false);
		if (!sbcast_cred->arg.id)
			goto unpack_error;
	} else {
		sbcast_cred->arg.id = xmalloc(sizeof(*sbcast_cred->arg.id));
		sbcast_cred->arg.id->uid = uid;
		sbcast_cred->arg.id->gid = gid;
		sbcast_cred->arg.id->pw_name = user_name;
		sbcast_cred->arg.id->ngids = ngids;
		sbcast_cred->arg.id->gids = gids;
	}

	identity_debug2(sbcast_cred->arg.id, __func__);

	*siglen = get_buf_offset(buffer) - cred_start;

	/* "signature" must be last */
	safe_unpackstr(&sbcast_cred->signature, buffer);
	if (!sbcast_cred->signature)
		goto unpack_error;

	/*
	 * Preserve a copy of the buffer in srun/sbcast to avoid needing to
	 * repack it later.
	 */
	if (!running_in_slurmd()) {
		uint32_t cred_len = get_buf_offset(buffer) - cred_start;
		sbcast_cred->buffer = init_buf(cred_len);
		memcpy(sbcast_cred->buffer->head,
		       get_buf_data(buffer) + cred_start,
		       cred_len);
		sbcast_cred->buffer->processed = cred_len;
	}

	return sbcast_cred;

unpack_error:
	delete_sbcast_cred(sbcast_cred);
	return NULL;
}
