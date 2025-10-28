/****************************************************************************\
 *  slurm_protocol_pack.c - functions to pack and unpack structures for RPCs
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/cron.h"
#include "src/common/fetch_config.h"
#include "src/common/forward.h"
#include "src/common/job_options.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/part_record.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/hash.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/stepmgr/stepmgr.h"

#define safe_unpack_step_id_members(step, buf, version) \
	do { \
		if (unpack_step_id_members(step, buf, version)) \
			goto unpack_error; \
	} while (0)

typedef struct {
	buf_t *buffer;
	int count;
	uint32_t header_position;
	uint32_t last_good_position;
	uint32_t max_buf_size;
	void (*pack_function)(void *object,
			      uint16_t protocol_version,
			      buf_t *buffer);
	uint16_t protocol_version;
	int rc;
} pack_list_t;

static int _unpack_node_info_members(node_info_t *node, buf_t *buffer,
				     uint16_t protocol_version);

static int _unpack_partition_info_members(partition_info_t *part,
					  buf_t *buffer,
					  uint16_t protocol_version);

static int _unpack_reserve_info_members(reserve_info_t *resv, buf_t *buffer,
					uint16_t protocol_version);

static void _pack_job_step_pids(const slurm_msg_t *smsg, buf_t *buffer);
static int _unpack_job_step_pids(job_step_pids_t **msg, buf_t *buffer,
				 uint16_t protocol_version);

static int _unpack_job_info_members(job_info_t *job, buf_t *buffer,
				    uint16_t protocol_version);

static void _pack_ret_list(list_t *ret_list, uint16_t size_val, buf_t *buffer,
			   uint16_t protocol_version);
static int _unpack_ret_list(list_t **ret_list, uint16_t size_val, buf_t *buffer,
			    uint16_t protocol_version);

static void _set_min_memory_tres(char *mem_per_tres, uint64_t *min_memory)
{
	xassert(min_memory);

	/*
	 * If there is a mem_per_tres pn_min_memory will not be
	 * set, let's figure it from the first tres there.
	 */
	if (mem_per_tres) {
		char *save_ptr = NULL, *tres_type = NULL,
			*name = NULL, *type = NULL;

		(void) slurm_get_next_tres(&tres_type,
					   mem_per_tres,
					   &name, &type,
					   min_memory,
					   &save_ptr);
		xfree(tres_type);
		xfree(name);
		xfree(type);
	}
}

/* pack_header
 * packs a slurm protocol header that precedes every slurm message
 * IN header - the header structure to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
void pack_header(header_t *header, buf_t *buffer)
{
	/*
	 * The DBD always unpacks the message type first.
	 * DO NOT UNPACK THIS ON THE UNPACK SIDE.
	 */
	if (header->flags & SLURMDBD_CONNECTION)
		pack16(header->msg_type, buffer);

	pack16(header->version, buffer);

	if (header->version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(header->flags, buffer);
		pack16(header->msg_type, buffer);
		pack32(header->body_length, buffer);
		pack16(header->forward.cnt, buffer);
		if (header->forward.cnt > 0) {
			packstr(header->forward.nodelist, buffer);
			pack32(header->forward.timeout, buffer);
			pack16(header->forward.tree_width, buffer);
			if (header->flags & SLURM_PACK_ADDRS)
				packstr(header->forward.alias_addrs.net_cred,
					buffer);
			pack16(header->forward.tree_depth, buffer);
		}
		pack16(header->ret_cnt, buffer);
		if (header->ret_cnt > 0) {
			_pack_ret_list(header->ret_list,
				       header->ret_cnt, buffer,
				       header->version);
		}
		slurm_pack_addr(&header->orig_addr, buffer);
	}
}

/* unpack_header
 * unpacks a slurm protocol header that precedes every slurm message
 * OUT header - the header structure to unpack
 * IN/OUT buffer - source of the unpack data, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
int unpack_header(header_t *header, buf_t *buffer)
{
	memset(header, 0, sizeof(header_t));

	safe_unpack16(&header->version, buffer);

	/* Slurm supports the current RPC version, plus three prior. */
	if ((header->version != SLURM_PROTOCOL_VERSION) &&
	    (header->version != SLURM_ONE_BACK_PROTOCOL_VERSION) &&
	    (header->version != SLURM_TWO_BACK_PROTOCOL_VERSION) &&
	    (header->version != SLURM_MIN_PROTOCOL_VERSION)) {
		error("%s: protocol_version %hu not supported",
		      __func__, header->version);
		return SLURM_PROTOCOL_VERSION_ERROR;
	}

	forward_init(&header->forward);

	if (header->version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&header->flags, buffer);
		safe_unpack16(&header->msg_type, buffer);
		safe_unpack32(&header->body_length, buffer);
		safe_unpack16(&header->forward.cnt, buffer);
		if (header->forward.cnt > 0) {
			safe_unpackstr(&header->forward.nodelist, buffer);
			safe_unpack32(&header->forward.timeout, buffer);
			safe_unpack16(&header->forward.tree_width, buffer);
			if (header->flags & SLURM_PACK_ADDRS) {
				safe_unpackstr(
					&header->forward.alias_addrs.net_cred,
					buffer);
			}
			safe_unpack16(&header->forward.tree_depth, buffer);
		}

		safe_unpack16(&header->ret_cnt, buffer);
		if (header->ret_cnt > 0) {
			if (_unpack_ret_list(&(header->ret_list),
					     header->ret_cnt, buffer,
					     header->version))
				goto unpack_error;
		} else {
			header->ret_list = NULL;
		}
		slurm_unpack_addr_no_alloc(&header->orig_addr, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("unpacking header");
	destroy_forward(&header->forward);
	FREE_NULL_LIST(header->ret_list);
	return SLURM_COMMUNICATIONS_RECEIVE_ERROR;
}


static void _pack_assoc_shares_object(void *in, uint32_t tres_cnt,
				      buf_t *buffer, uint16_t protocol_version)
{
	assoc_shares_object_t *object = in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack32(0, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			packdouble(0, buffer);
			pack32(0, buffer);

			pack64_array(NULL, 0, buffer);
			pack64_array(NULL, 0, buffer);

			packdouble(0, buffer);
			packdouble(0, buffer);
			pack64(0, buffer);
			packlongdouble_array(NULL, 0, buffer);

			packdouble(0, buffer);
			packdouble(0, buffer);

			pack16(0, buffer);

			return;
		}

		pack32(object->assoc_id, buffer);

		packstr(object->cluster, buffer);
		packstr(object->name, buffer);
		packstr(object->parent, buffer);
		packstr(object->partition, buffer);

		packdouble(object->shares_norm, buffer);
		pack32(object->shares_raw, buffer);

		pack64_array(object->tres_run_secs, tres_cnt, buffer);
		pack64_array(object->tres_grp_mins, tres_cnt, buffer);

		packdouble(object->usage_efctv, buffer);
		packdouble(object->usage_norm, buffer);
		pack64(object->usage_raw, buffer);
		packlongdouble_array(object->usage_tres_raw, tres_cnt, buffer);

		packdouble(object->fs_factor, buffer);
		packdouble(object->level_fs, buffer);

		pack16(object->user, buffer);
	}
}

static int _unpack_assoc_shares_object(void **object, uint32_t tres_cnt,
				       buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	assoc_shares_object_t *object_ptr =
		xmalloc(sizeof(assoc_shares_object_t));

	*object = (void *) object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&object_ptr->assoc_id, buffer);

		safe_unpackstr(&object_ptr->cluster, buffer);
		safe_unpackstr(&object_ptr->name, buffer);
		safe_unpackstr(&object_ptr->parent, buffer);
		safe_unpackstr(&object_ptr->partition, buffer);

		safe_unpackdouble(&object_ptr->shares_norm, buffer);
		safe_unpack32(&object_ptr->shares_raw, buffer);

		safe_unpack64_array(&object_ptr->tres_run_secs,
				    &uint32_tmp, buffer);
		if (uint32_tmp != tres_cnt)
			goto unpack_error;
		safe_unpack64_array(&object_ptr->tres_grp_mins,
				    &uint32_tmp, buffer);
		if (uint32_tmp != tres_cnt)
			goto unpack_error;

		safe_unpackdouble(&object_ptr->usage_efctv, buffer);
		safe_unpackdouble(&object_ptr->usage_norm, buffer);
		safe_unpack64(&object_ptr->usage_raw, buffer);
		safe_unpacklongdouble_array(&object_ptr->usage_tres_raw,
					    &uint32_tmp, buffer);

		safe_unpackdouble(&object_ptr->fs_factor, buffer);
		safe_unpackdouble(&object_ptr->level_fs, buffer);

		safe_unpack16(&object_ptr->user, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_assoc_shares_object(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

static void _pack_network_callerid_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	network_callerid_msg_t *msg = smsg->data;
	xassert(msg);

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packmem((char *)msg->ip_src, 16, buffer);
		packmem((char *)msg->ip_dst, 16, buffer);
		pack32(msg->port_src, buffer);
		pack32(msg->port_dst, buffer);
		pack32((uint32_t)msg->af, buffer);
	}
}

static int _unpack_network_callerid_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp;
	char *charptr_tmp = NULL;
	network_callerid_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackmem_xmalloc(&charptr_tmp, &uint32_tmp, buffer);
		if (uint32_tmp > (uint32_t)sizeof(msg->ip_src)) {
			error("%s: ip_src that came across is %u and we can only handle %lu",
			      __func__, uint32_tmp,
			      (long unsigned) sizeof(msg->ip_src));
			goto unpack_error;
		}
		memcpy(msg->ip_src, charptr_tmp, (size_t)uint32_tmp);
		xfree(charptr_tmp);
		safe_unpackmem_xmalloc(&charptr_tmp, &uint32_tmp, buffer);
		if (uint32_tmp > (uint32_t)sizeof(msg->ip_dst)) {
			error("%s: ip_dst that came across is %u and we can only handle %lu",
			      __func__, uint32_tmp,
			      (long unsigned) sizeof(msg->ip_dst));
			goto unpack_error;
		}
		memcpy(msg->ip_dst, charptr_tmp, (size_t)uint32_tmp);
		xfree(charptr_tmp);
		safe_unpack32(&msg->port_src, buffer);
		safe_unpack32(&msg->port_dst, buffer);
		safe_unpack32((uint32_t *)&msg->af, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	xfree(charptr_tmp);
	slurm_free_network_callerid_msg(msg);
	return SLURM_ERROR;
}

static void _pack_network_callerid_resp_msg(const slurm_msg_t *smsg,
					    buf_t *buffer)
{
	network_callerid_resp_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->return_code, buffer);
		packstr(msg->node_name, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->return_code, buffer);
		packstr(msg->node_name, buffer);
	}
}

static int _unpack_network_callerid_resp_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	network_callerid_resp_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->return_code, buffer);
		safe_unpackstr(&msg->node_name, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->return_code, buffer);
		safe_unpackstr(&msg->node_name, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_network_callerid_resp(msg);
	return SLURM_ERROR;
}

extern void packstr_with_version(void *object, uint16_t protocol_version,
				  buf_t *buffer)
{
	packstr(object, buffer);
}

extern int unpackstr_with_version(void **object, uint16_t protocol_version,
				  buf_t *buffer)
{
	safe_unpackstr((char **) object, buffer);
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

static void _pack_shares_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	shares_request_msg_t *msg = smsg->data;
	xassert(msg);

	(void) slurm_pack_list(msg->acct_list, packstr_with_version, buffer,
			       smsg->protocol_version);
	(void) slurm_pack_list(msg->user_list, packstr_with_version, buffer,
			       smsg->protocol_version);
}

static int _unpack_shares_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	shares_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (slurm_unpack_list(&msg->acct_list, unpackstr_with_version,
			      xfree_ptr, buffer,
			      smsg->protocol_version) != SLURM_SUCCESS)
		goto unpack_error;

	if (slurm_unpack_list(&msg->user_list, unpackstr_with_version,
			      xfree_ptr, buffer,
			      smsg->protocol_version) != SLURM_SUCCESS)
		goto unpack_error;

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shares_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_shares_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	shares_response_msg_t *msg = smsg->data;
	list_itr_t *itr = NULL;
	assoc_shares_object_t *share = NULL;
	uint32_t count = NO_VAL;

	xassert(msg);

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr_array(msg->tres_names, msg->tres_cnt, buffer);

		if (!msg->assoc_shares_list ||
		    !(count = list_count(msg->assoc_shares_list)))
			count = NO_VAL;

		pack32(count, buffer);
		if (count != NO_VAL) {
			itr = list_iterator_create(msg->assoc_shares_list);
			while ((share = list_next(itr)))
				_pack_assoc_shares_object(
					share, msg->tres_cnt, buffer,
					smsg->protocol_version);
			list_iterator_destroy(itr);
		}
		pack64(msg->tot_shares, buffer);
	}
}

static int _unpack_shares_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t count = NO_VAL;
	void *tmp_info = NULL;
	shares_response_msg_t *object_ptr = xmalloc(sizeof(*object_ptr));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_array(&object_ptr->tres_names,
				     &object_ptr->tres_cnt, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->assoc_shares_list =
				list_create(slurm_destroy_assoc_shares_object);
			for (int i = 0; i < count; i++) {
				if (_unpack_assoc_shares_object(
					    &tmp_info, object_ptr->tres_cnt,
					    buffer, smsg->protocol_version)
				    != SLURM_SUCCESS)
					goto unpack_error;
				list_append(object_ptr->assoc_shares_list,
					    tmp_info);
			}
		}

		safe_unpack64(&object_ptr->tot_shares, buffer);
	}

	smsg->data = object_ptr;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shares_response_msg(object_ptr);
	return SLURM_ERROR;
}

static void _pack_priority_factors(priority_factors_t *object, buf_t *buffer,
				   uint16_t protocol_version)
{
	xassert(object);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packdouble(object->priority_age, buffer);
		packdouble(object->priority_assoc, buffer);
		packdouble(object->priority_fs, buffer);
		packdouble(object->priority_js, buffer);
		packdouble(object->priority_part, buffer);
		packdouble(object->priority_qos, buffer);
		pack32(object->priority_site, buffer);

		packdouble_array(object->priority_tres, object->tres_cnt,
				 buffer);
		packstr_array(assoc_mgr_tres_name_array, object->tres_cnt,
			      buffer);
		packdouble_array(object->tres_weights, object->tres_cnt,
				 buffer);

		pack32(object->nice, buffer);
	}
}

static int _unpack_priority_factors(priority_factors_t **object, buf_t *buffer,
				    uint16_t protocol_version)
{
	uint32_t tmp32 = 0;
	priority_factors_t *object_ptr = xmalloc(sizeof(priority_factors_t));

	*object = (void *) object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackdouble(&object_ptr->priority_age, buffer);
		safe_unpackdouble(&object_ptr->priority_assoc, buffer);
		safe_unpackdouble(&object_ptr->priority_fs, buffer);
		safe_unpackdouble(&object_ptr->priority_js, buffer);
		safe_unpackdouble(&object_ptr->priority_part, buffer);
		safe_unpackdouble(&object_ptr->priority_qos, buffer);
		safe_unpack32(&object_ptr->priority_site, buffer);

		safe_unpackdouble_array(&object_ptr->priority_tres, &tmp32,
					buffer);

		object_ptr->tres_cnt = tmp32;

		safe_unpackstr_array(&object_ptr->tres_names,
				     &tmp32, buffer);
		safe_unpackdouble_array(&object_ptr->tres_weights, &tmp32,
					buffer);

		safe_unpack32(&object_ptr->nice, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_priority_factors(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

static void _pack_priority_factors_object(void *in, uint16_t protocol_version,
					  buf_t *buffer)
{
	priority_factors_object_t *object = in;

	xassert(object);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(object->account, buffer);
		pack32(object->job_id, buffer);
		packstr(object->partition, buffer);

		packdouble(object->direct_prio, buffer);
		if (!object->direct_prio)
			_pack_priority_factors(object->prio_factors,
					       buffer, protocol_version);

		packstr(object->qos, buffer);
		pack32(object->user_id, buffer);

	}
}

static int _unpack_priority_factors_object(void **object, buf_t *buffer,
					   uint16_t protocol_version)
{
	priority_factors_object_t *object_ptr =
		xmalloc(sizeof(priority_factors_object_t));
	*object = (void *) object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&object_ptr->account, buffer);
		safe_unpack32(&object_ptr->job_id, buffer);
		safe_unpackstr(&object_ptr->partition, buffer);
		safe_unpackdouble(&object_ptr->direct_prio, buffer);

		if (!object_ptr->direct_prio &&
		    _unpack_priority_factors(&object_ptr->prio_factors,
					     buffer, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&object_ptr->qos, buffer);
		safe_unpack32(&object_ptr->user_id, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_priority_factors_object(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

static void _pack_priority_factors_response_msg(const slurm_msg_t *smsg,
						buf_t *buffer)
{
	priority_factors_response_msg_t *msg = smsg->data;

	slurm_pack_list(msg->priority_factors_list,
			_pack_priority_factors_object, buffer,
			smsg->protocol_version);
}

static int _unpack_priority_factors_response_msg(slurm_msg_t *smsg,
						 buf_t *buffer)
{
	uint32_t count = NO_VAL;
	void *tmp_info = NULL;
	priority_factors_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			msg->priority_factors_list = list_create(
				slurm_destroy_priority_factors_object);
			for (int i = 0; i < count; i++) {
				if (_unpack_priority_factors_object(
					    &tmp_info, buffer,
					    smsg->protocol_version) !=
				    SLURM_SUCCESS)
					goto unpack_error;
				list_append(msg->priority_factors_list,
					    tmp_info);
			}
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_priority_factors_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_update_node_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	update_node_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		packstr(msg->cert_token, buffer);
		packstr(msg->comment, buffer);
		pack32(msg->cpu_bind, buffer);
		packstr(msg->extra, buffer);
		packstr(msg->features, buffer);
		packstr(msg->features_act, buffer);
		packstr(msg->gres, buffer);
		packstr(msg->instance_id, buffer);
		packstr(msg->instance_type, buffer);
		packstr(msg->node_addr, buffer);
		packstr(msg->node_hostname, buffer);
		packstr(msg->node_names, buffer);
		pack32(msg->node_state, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->resume_after, buffer);
		packstr(msg->topology_str, buffer);
		pack32(msg->weight, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->cert_token, buffer);
		packstr(msg->comment, buffer);
		pack32(msg->cpu_bind, buffer);
		packstr(msg->extra, buffer);
		packstr(msg->features, buffer);
		packstr(msg->features_act, buffer);
		packstr(msg->gres, buffer);
		packstr(msg->instance_id, buffer);
		packstr(msg->instance_type, buffer);
		packstr(msg->node_addr, buffer);
		packstr(msg->node_hostname, buffer);
		packstr(msg->node_names, buffer);
		pack32(msg->node_state, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->resume_after, buffer);
		pack32(msg->weight, buffer);
	}
}

static int _unpack_update_node_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	update_node_msg_t *msg = xmalloc(sizeof(*msg));

	slurm_init_update_node_msg(msg);

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->cert_token, buffer);
		safe_unpackstr(&msg->comment, buffer);
		safe_unpack32(&msg->cpu_bind, buffer);
		safe_unpackstr(&msg->extra, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpackstr(&msg->features_act, buffer);
		safe_unpackstr(&msg->gres, buffer);
		safe_unpackstr(&msg->instance_id, buffer);
		safe_unpackstr(&msg->instance_type, buffer);
		safe_unpackstr(&msg->node_addr, buffer);
		safe_unpackstr(&msg->node_hostname, buffer);
		safe_unpackstr(&msg->node_names, buffer);
		safe_unpack32(&msg->node_state, buffer);
		safe_unpackstr(&msg->reason, buffer);
		safe_unpack32(&msg->resume_after, buffer);
		safe_unpackstr(&msg->topology_str, buffer);
		safe_unpack32(&msg->weight, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->cert_token, buffer);
		safe_unpackstr(&msg->comment, buffer);
		safe_unpack32(&msg->cpu_bind, buffer);
		safe_unpackstr(&msg->extra, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpackstr(&msg->features_act, buffer);
		safe_unpackstr(&msg->gres, buffer);
		safe_unpackstr(&msg->instance_id, buffer);
		safe_unpackstr(&msg->instance_type, buffer);
		safe_unpackstr(&msg->node_addr, buffer);
		safe_unpackstr(&msg->node_hostname, buffer);
		safe_unpackstr(&msg->node_names, buffer);
		safe_unpack32(&msg->node_state, buffer);
		safe_unpackstr(&msg->reason, buffer);
		safe_unpack32(&msg->resume_after, buffer);
		safe_unpack32(&msg->weight, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_node_msg(msg);
	return SLURM_ERROR;
}

static void _pack_acct_gather_node_resp_msg(const slurm_msg_t *smsg,
					    buf_t *buffer)
{
	acct_gather_node_resp_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->node_name, buffer);
		pack16(msg->sensor_cnt, buffer);
		for (int i = 0; i < msg->sensor_cnt; i++)
			acct_gather_energy_pack(&msg->energy[i], buffer,
						smsg->protocol_version);
	}
}

static int _unpack_acct_gather_node_resp_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	acct_gather_energy_t *e;
	acct_gather_node_resp_msg_t *node_data_ptr =
		xmalloc(sizeof(*node_data_ptr));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&node_data_ptr->node_name, buffer);
		safe_unpack16(&node_data_ptr->sensor_cnt, buffer);
		safe_xcalloc(node_data_ptr->energy, node_data_ptr->sensor_cnt,
			     sizeof(acct_gather_energy_t));
		for (int i = 0; i < node_data_ptr->sensor_cnt; ++i) {
			e = &node_data_ptr->energy[i];
			if (acct_gather_energy_unpack(&e, buffer,
						      smsg->protocol_version,
						      0) != SLURM_SUCCESS)
				goto unpack_error;
		}
	}

	smsg->data = node_data_ptr;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_acct_gather_node_resp_msg(node_data_ptr);
	return SLURM_ERROR;
}

static void _pack_acct_gather_energy_req(const slurm_msg_t *smsg, buf_t *buffer)
{
	acct_gather_energy_req_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->context_id, buffer);
		pack16(msg->delta, buffer);
	}
}

static int _unpack_acct_gather_energy_req(slurm_msg_t *smsg, buf_t *buffer)
{
	acct_gather_energy_req_msg_t *msg_ptr = xmalloc(sizeof(*msg_ptr));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg_ptr->context_id, buffer);
		safe_unpack16(&msg_ptr->delta, buffer);
	}

	smsg->data = msg_ptr;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_acct_gather_energy_req_msg(msg_ptr);
	return SLURM_ERROR;
}

static void _pack_node_registration_status_msg(const slurm_msg_t *smsg,
					       buf_t *buffer)
{
	slurm_node_registration_status_msg_t *msg = smsg->data;
	uint32_t gres_info_size = 0;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_time(msg->timestamp, buffer);
		pack_time(msg->slurmd_start_time, buffer);
		pack32(msg->status, buffer);
		packstr(msg->extra, buffer);
		packstr(msg->features_active, buffer);
		packstr(msg->features_avail, buffer);
		packstr(msg->hostname, buffer);
		packstr(msg->instance_id, buffer);
		packstr(msg->instance_type, buffer);
		packstr(msg->node_name, buffer);
		packstr(msg->arch, buffer);
		packstr(msg->cpu_spec_list, buffer);
		pack64(msg->mem_spec_limit, buffer);
		packstr(msg->os, buffer);
		packstr(msg->parameters, buffer);
		pack16(msg->cpus, buffer);
		pack16(msg->boards, buffer);
		pack16(msg->sockets, buffer);
		pack16(msg->cores, buffer);
		pack16(msg->threads, buffer);
		pack64(msg->real_memory, buffer);
		pack32(msg->tmp_disk, buffer);
		pack32(msg->up_time, buffer);
		pack32(msg->hash_val, buffer);
		pack32(msg->cpu_load, buffer);
		pack64(msg->free_mem, buffer);

		pack32(msg->job_count, buffer);
		for (int i = 0; i < msg->job_count; i++) {
			pack_step_id(&msg->step_id[i], buffer,
				     smsg->protocol_version);
		}
		pack16(msg->flags, buffer);
		if (msg->gres_info)
			gres_info_size = get_buf_offset(msg->gres_info);
		pack32(gres_info_size, buffer);
		if (gres_info_size) {
			packmem(get_buf_data(msg->gres_info), gres_info_size,
				buffer);
		}
		acct_gather_energy_pack(msg->energy, buffer,
					smsg->protocol_version);
		packstr(msg->version, buffer);

		pack8(msg->dynamic_type, buffer);
		packstr(msg->dynamic_conf, buffer);
		packstr(msg->dynamic_feature, buffer);
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		pack_time(msg->timestamp, buffer);
		pack_time(msg->slurmd_start_time, buffer);
		pack32(msg->status, buffer);
		packstr(msg->extra, buffer);
		packstr(msg->features_active, buffer);
		packstr(msg->features_avail, buffer);
		packstr(msg->hostname, buffer);
		packstr(msg->instance_id, buffer);
		packstr(msg->instance_type, buffer);
		packstr(msg->node_name, buffer);
		packstr(msg->arch, buffer);
		packstr(msg->cpu_spec_list, buffer);
		pack64(msg->mem_spec_limit, buffer);
		packstr(msg->os, buffer);
		pack16(msg->cpus, buffer);
		pack16(msg->boards, buffer);
		pack16(msg->sockets, buffer);
		pack16(msg->cores, buffer);
		pack16(msg->threads, buffer);
		pack64(msg->real_memory, buffer);
		pack32(msg->tmp_disk, buffer);
		pack32(msg->up_time, buffer);
		pack32(msg->hash_val, buffer);
		pack32(msg->cpu_load, buffer);
		pack64(msg->free_mem, buffer);

		pack32(msg->job_count, buffer);
		for (int i = 0; i < msg->job_count; i++) {
			pack_step_id(&msg->step_id[i], buffer,
				     smsg->protocol_version);
		}
		pack16(msg->flags, buffer);
		if (msg->gres_info)
			gres_info_size = get_buf_offset(msg->gres_info);
		pack32(gres_info_size, buffer);
		if (gres_info_size) {
			packmem(get_buf_data(msg->gres_info), gres_info_size,
				buffer);
		}
		acct_gather_energy_pack(msg->energy, buffer,
					smsg->protocol_version);
		packstr(msg->version, buffer);

		pack8(msg->dynamic_type, buffer);
		packstr(msg->dynamic_conf, buffer);
		packstr(msg->dynamic_feature, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->timestamp, buffer);
		pack_time(msg->slurmd_start_time, buffer);
		pack32(msg->status, buffer);
		packstr(msg->extra, buffer);
		packstr(msg->features_active, buffer);
		packstr(msg->features_avail, buffer);
		packstr(msg->hostname, buffer);
		packstr(msg->instance_id, buffer);
		packstr(msg->instance_type, buffer);
		packstr(msg->node_name, buffer);
		packstr(msg->arch, buffer);
		packstr(msg->cpu_spec_list, buffer);
		packstr(msg->os, buffer);
		pack16(msg->cpus, buffer);
		pack16(msg->boards, buffer);
		pack16(msg->sockets, buffer);
		pack16(msg->cores, buffer);
		pack16(msg->threads, buffer);
		pack64(msg->real_memory, buffer);
		pack32(msg->tmp_disk, buffer);
		pack32(msg->up_time, buffer);
		pack32(msg->hash_val, buffer);
		pack32(msg->cpu_load, buffer);
		pack64(msg->free_mem, buffer);

		pack32(msg->job_count, buffer);
		for (int i = 0; i < msg->job_count; i++) {
			pack_step_id(&msg->step_id[i], buffer,
				     smsg->protocol_version);
		}
		pack16(msg->flags, buffer);
		if (msg->gres_info)
			gres_info_size = get_buf_offset(msg->gres_info);
		pack32(gres_info_size, buffer);
		if (gres_info_size) {
			packmem(get_buf_data(msg->gres_info), gres_info_size,
				buffer);
		}
		acct_gather_energy_pack(msg->energy, buffer,
					smsg->protocol_version);
		packstr(msg->version, buffer);

		pack8(msg->dynamic_type, buffer);
		packstr(msg->dynamic_conf, buffer);
		packstr(msg->dynamic_feature, buffer);
	}
}

static int _unpack_node_registration_status_msg(slurm_msg_t *smsg,
						buf_t *buffer)
{
	char *gres_info = NULL;
	uint32_t gres_info_size, uint32_tmp;
	int i;
	slurm_node_registration_status_msg_t *node_reg_ptr =
		xmalloc(sizeof(*node_reg_ptr));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&node_reg_ptr->timestamp, buffer);
		safe_unpack_time(&node_reg_ptr->slurmd_start_time, buffer);
		/* load the data values */
		safe_unpack32(&node_reg_ptr->status, buffer);
		safe_unpackstr(&node_reg_ptr->extra, buffer);
		safe_unpackstr(&node_reg_ptr->features_active, buffer);
		safe_unpackstr(&node_reg_ptr->features_avail, buffer);
		safe_unpackstr(&node_reg_ptr->hostname, buffer);
		safe_unpackstr(&node_reg_ptr->instance_id, buffer);
		safe_unpackstr(&node_reg_ptr->instance_type, buffer);
		safe_unpackstr(&node_reg_ptr->node_name, buffer);
		safe_unpackstr(&node_reg_ptr->arch, buffer);
		safe_unpackstr(&node_reg_ptr->cpu_spec_list, buffer);
		safe_unpack64(&node_reg_ptr->mem_spec_limit, buffer);
		safe_unpackstr(&node_reg_ptr->os, buffer);
		safe_unpackstr(&node_reg_ptr->parameters, buffer);
		safe_unpack16(&node_reg_ptr->cpus, buffer);
		safe_unpack16(&node_reg_ptr->boards, buffer);
		safe_unpack16(&node_reg_ptr->sockets, buffer);
		safe_unpack16(&node_reg_ptr->cores, buffer);
		safe_unpack16(&node_reg_ptr->threads, buffer);
		safe_unpack64(&node_reg_ptr->real_memory, buffer);
		safe_unpack32(&node_reg_ptr->tmp_disk, buffer);
		safe_unpack32(&node_reg_ptr->up_time, buffer);
		safe_unpack32(&node_reg_ptr->hash_val, buffer);
		safe_unpack32(&node_reg_ptr->cpu_load, buffer);
		safe_unpack64(&node_reg_ptr->free_mem, buffer);

		safe_unpack32(&node_reg_ptr->job_count, buffer);
		if (node_reg_ptr->job_count > NO_VAL)
			goto unpack_error;
		safe_xcalloc(node_reg_ptr->step_id, node_reg_ptr->job_count,
			     sizeof(*node_reg_ptr->step_id));
		for (i = 0; i < node_reg_ptr->job_count; i++)
			safe_unpack_step_id_members(&node_reg_ptr->step_id[i],
						    buffer,
						    smsg->protocol_version);

		safe_unpack16(&node_reg_ptr->flags, buffer);

		safe_unpack32(&gres_info_size, buffer);
		if (gres_info_size) {
			safe_unpackmem_xmalloc(&gres_info, &uint32_tmp, buffer);
			if (gres_info_size != uint32_tmp)
				goto unpack_error;
			node_reg_ptr->gres_info = create_buf(gres_info,
							     gres_info_size);
			gres_info = NULL;
		}
		if (acct_gather_energy_unpack(&node_reg_ptr->energy, buffer,
					      smsg->protocol_version,
					      1) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&node_reg_ptr->version, buffer);

		safe_unpack8(&node_reg_ptr->dynamic_type, buffer);
		safe_unpackstr(&node_reg_ptr->dynamic_conf, buffer);
		safe_unpackstr(&node_reg_ptr->dynamic_feature, buffer);
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&node_reg_ptr->timestamp, buffer);
		safe_unpack_time(&node_reg_ptr->slurmd_start_time, buffer);
		/* load the data values */
		safe_unpack32(&node_reg_ptr->status, buffer);
		safe_unpackstr(&node_reg_ptr->extra, buffer);
		safe_unpackstr(&node_reg_ptr->features_active, buffer);
		safe_unpackstr(&node_reg_ptr->features_avail, buffer);
		safe_unpackstr(&node_reg_ptr->hostname, buffer);
		safe_unpackstr(&node_reg_ptr->instance_id, buffer);
		safe_unpackstr(&node_reg_ptr->instance_type, buffer);
		safe_unpackstr(&node_reg_ptr->node_name, buffer);
		safe_unpackstr(&node_reg_ptr->arch, buffer);
		safe_unpackstr(&node_reg_ptr->cpu_spec_list, buffer);
		safe_unpack64(&node_reg_ptr->mem_spec_limit, buffer);
		safe_unpackstr(&node_reg_ptr->os, buffer);
		safe_unpack16(&node_reg_ptr->cpus, buffer);
		safe_unpack16(&node_reg_ptr->boards, buffer);
		safe_unpack16(&node_reg_ptr->sockets, buffer);
		safe_unpack16(&node_reg_ptr->cores, buffer);
		safe_unpack16(&node_reg_ptr->threads, buffer);
		safe_unpack64(&node_reg_ptr->real_memory, buffer);
		safe_unpack32(&node_reg_ptr->tmp_disk, buffer);
		safe_unpack32(&node_reg_ptr->up_time, buffer);
		safe_unpack32(&node_reg_ptr->hash_val, buffer);
		safe_unpack32(&node_reg_ptr->cpu_load, buffer);
		safe_unpack64(&node_reg_ptr->free_mem, buffer);

		safe_unpack32(&node_reg_ptr->job_count, buffer);
		if (node_reg_ptr->job_count > NO_VAL)
			goto unpack_error;
		safe_xcalloc(node_reg_ptr->step_id, node_reg_ptr->job_count,
			     sizeof(*node_reg_ptr->step_id));
		for (i = 0; i < node_reg_ptr->job_count; i++)
			safe_unpack_step_id_members(&node_reg_ptr->step_id[i],
						    buffer,
						    smsg->protocol_version);

		safe_unpack16(&node_reg_ptr->flags, buffer);

		safe_unpack32(&gres_info_size, buffer);
		if (gres_info_size) {
			safe_unpackmem_xmalloc(&gres_info, &uint32_tmp, buffer);
			if (gres_info_size != uint32_tmp)
				goto unpack_error;
			node_reg_ptr->gres_info = create_buf(gres_info,
							     gres_info_size);
			gres_info = NULL;
		}
		if (acct_gather_energy_unpack(&node_reg_ptr->energy, buffer,
					      smsg->protocol_version,
					      1) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&node_reg_ptr->version, buffer);

		safe_unpack8(&node_reg_ptr->dynamic_type, buffer);
		safe_unpackstr(&node_reg_ptr->dynamic_conf, buffer);
		safe_unpackstr(&node_reg_ptr->dynamic_feature, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&node_reg_ptr->timestamp, buffer);
		safe_unpack_time(&node_reg_ptr->slurmd_start_time, buffer);
		/* load the data values */
		safe_unpack32(&node_reg_ptr->status, buffer);
		safe_unpackstr(&node_reg_ptr->extra, buffer);
		safe_unpackstr(&node_reg_ptr->features_active, buffer);
		safe_unpackstr(&node_reg_ptr->features_avail, buffer);
		safe_unpackstr(&node_reg_ptr->hostname, buffer);
		safe_unpackstr(&node_reg_ptr->instance_id, buffer);
		safe_unpackstr(&node_reg_ptr->instance_type, buffer);
		safe_unpackstr(&node_reg_ptr->node_name, buffer);
		safe_unpackstr(&node_reg_ptr->arch, buffer);
		safe_unpackstr(&node_reg_ptr->cpu_spec_list, buffer);
		safe_unpackstr(&node_reg_ptr->os, buffer);
		safe_unpack16(&node_reg_ptr->cpus, buffer);
		safe_unpack16(&node_reg_ptr->boards, buffer);
		safe_unpack16(&node_reg_ptr->sockets, buffer);
		safe_unpack16(&node_reg_ptr->cores, buffer);
		safe_unpack16(&node_reg_ptr->threads, buffer);
		safe_unpack64(&node_reg_ptr->real_memory, buffer);
		safe_unpack32(&node_reg_ptr->tmp_disk, buffer);
		safe_unpack32(&node_reg_ptr->up_time, buffer);
		safe_unpack32(&node_reg_ptr->hash_val, buffer);
		safe_unpack32(&node_reg_ptr->cpu_load, buffer);
		safe_unpack64(&node_reg_ptr->free_mem, buffer);

		safe_unpack32(&node_reg_ptr->job_count, buffer);
		if (node_reg_ptr->job_count > NO_VAL)
			goto unpack_error;
		safe_xcalloc(node_reg_ptr->step_id, node_reg_ptr->job_count,
			     sizeof(*node_reg_ptr->step_id));
		for (i = 0; i < node_reg_ptr->job_count; i++)
			safe_unpack_step_id_members(&node_reg_ptr->step_id[i],
						    buffer,
						    smsg->protocol_version);

		safe_unpack16(&node_reg_ptr->flags, buffer);

		safe_unpack32(&gres_info_size, buffer);
		if (gres_info_size) {
			safe_unpackmem_xmalloc(&gres_info, &uint32_tmp, buffer);
			if (gres_info_size != uint32_tmp)
				goto unpack_error;
			node_reg_ptr->gres_info = create_buf(gres_info,
							     gres_info_size);
			gres_info = NULL;
		}
		if (acct_gather_energy_unpack(&node_reg_ptr->energy, buffer,
					      smsg->protocol_version,
					      1) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&node_reg_ptr->version, buffer);

		safe_unpack8(&node_reg_ptr->dynamic_type, buffer);
		safe_unpackstr(&node_reg_ptr->dynamic_conf, buffer);
		safe_unpackstr(&node_reg_ptr->dynamic_feature, buffer);
	}

	smsg->data = node_reg_ptr;
	return SLURM_SUCCESS;

unpack_error:
	xfree(gres_info);
	slurm_free_node_registration_status_msg(node_reg_ptr);
	return SLURM_ERROR;
}

static void _pack_resource_allocation_response_msg(const slurm_msg_t *smsg,
						   buf_t *buffer)
{
	resource_allocation_response_msg_t *msg = smsg->data;
	xassert(msg);

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);

		packstr(msg->batch_host, buffer);
		packstr_array(msg->environment, msg->env_size, buffer);
		pack32(msg->error_code, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->group_name, buffer);
		packstr(msg->job_submit_user_msg, buffer);
		pack32(msg->node_cnt, buffer);

		packstr(msg->node_list, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node,
				     msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps,
				     msg->num_cpu_groups,
				     buffer);
		}
		packstr(msg->partition, buffer);
		pack64(msg->pn_min_memory, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		pack16(msg->segment_size, buffer);
		pack16(msg->start_protocol_ver, buffer);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_task, buffer);
		pack32(msg->uid, buffer);
		packstr(msg->user_name, buffer);

		if (msg->working_cluster_rec) {
			pack8(1, buffer);
			slurmdb_pack_cluster_rec(msg->working_cluster_rec,
						 smsg->protocol_version,
						 buffer);
		} else {
			pack8(0, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);

		packstr(msg->batch_host, buffer);
		packstr_array(msg->environment, msg->env_size, buffer);
		pack32(msg->error_code, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->group_name, buffer);
		packstr(msg->job_submit_user_msg, buffer);
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->node_cnt, buffer);

		packstr(msg->node_list, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node,
				     msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps,
				     msg->num_cpu_groups,
				     buffer);
		}
		packstr(msg->partition, buffer);
		pack64(msg->pn_min_memory, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_task, buffer);
		pack32(msg->uid, buffer);
		packstr(msg->user_name, buffer);

		if (msg->working_cluster_rec) {
			pack8(1, buffer);
			slurmdb_pack_cluster_rec(msg->working_cluster_rec,
						 smsg->protocol_version,
						 buffer);
		} else {
			pack8(0, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);

		packnull(buffer);

		packstr(msg->batch_host, buffer);
		packstr_array(msg->environment, msg->env_size, buffer);
		pack32(msg->error_code, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->group_name, buffer);
		packstr(msg->job_submit_user_msg, buffer);
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->node_cnt, buffer);

		pack8(0, buffer);

		packstr(msg->node_list, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node,
				     msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps,
				     msg->num_cpu_groups,
				     buffer);
		}
		packstr(msg->partition, buffer);
		pack64(msg->pn_min_memory, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		packstr(msg->tres_per_node, buffer);
		pack32(msg->uid, buffer);
		packstr(msg->user_name, buffer);

		if (msg->working_cluster_rec) {
			pack8(1, buffer);
			slurmdb_pack_cluster_rec(msg->working_cluster_rec,
						 smsg->protocol_version,
						 buffer);
		} else {
			pack8(0, buffer);
		}
	}
}

static int _unpack_resource_allocation_response_msg(slurm_msg_t *smsg,
						    buf_t *buffer)
{
	uint8_t  uint8_tmp;
	uint32_t uint32_tmp;
	char *tmp_char = NULL;
	resource_allocation_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->batch_host, buffer);
		safe_unpackstr_array(&msg->environment, &msg->env_size, buffer);
		safe_unpack32(&msg->error_code, buffer);
		safe_unpack32(&msg->gid, buffer);
		safe_unpackstr(&msg->group_name, buffer);
		safe_unpackstr(&msg->job_submit_user_msg, buffer);
		safe_unpack32(&msg->node_cnt, buffer);

		safe_unpackstr(&msg->node_list, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpack32(&msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups > 0) {
			safe_unpack16_array(&msg->cpus_per_node, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&msg->cpu_count_reps, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		} else {
			msg->cpus_per_node = NULL;
			msg->cpu_count_reps = NULL;
		}
		safe_unpackstr(&msg->partition, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->resv_name, buffer);
		safe_unpack16(&msg->segment_size, buffer);
		safe_unpack16(&msg->start_protocol_ver, buffer);
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->tres_per_node, buffer);
		safe_unpackstr(&msg->tres_per_task, buffer);
		safe_unpack32(&msg->uid, buffer);
		safe_unpackstr(&msg->user_name, buffer);

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			slurmdb_unpack_cluster_rec(
				(void **) &msg->working_cluster_rec,
				smsg->protocol_version, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->batch_host, buffer);
		safe_unpackstr_array(&msg->environment, &msg->env_size, buffer);
		safe_unpack32(&msg->error_code, buffer);
		safe_unpack32(&msg->gid, buffer);
		safe_unpackstr(&msg->group_name, buffer);
		safe_unpackstr(&msg->job_submit_user_msg, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->node_cnt, buffer);

		safe_unpackstr(&msg->node_list, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpack32(&msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups > 0) {
			safe_unpack16_array(&msg->cpus_per_node, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&msg->cpu_count_reps, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		} else {
			msg->cpus_per_node = NULL;
			msg->cpu_count_reps = NULL;
		}
		safe_unpackstr(&msg->partition, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->resv_name, buffer);
		safe_unpackstr(&msg->tres_per_node, buffer);
		safe_unpackstr(&msg->tres_per_task, buffer);
		safe_unpack32(&msg->uid, buffer);
		safe_unpackstr(&msg->user_name, buffer);

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			slurmdb_unpack_cluster_rec(
				(void **) &msg->working_cluster_rec,
				smsg->protocol_version, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&tmp_char, buffer);
		xfree(tmp_char);
		safe_unpackstr(&msg->batch_host, buffer);
		safe_unpackstr_array(&msg->environment, &msg->env_size, buffer);
		safe_unpack32(&msg->error_code, buffer);
		safe_unpack32(&msg->gid, buffer);
		safe_unpackstr(&msg->group_name, buffer);
		safe_unpackstr(&msg->job_submit_user_msg, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->node_cnt, buffer);

		safe_unpack8(&uint8_tmp, buffer);

		safe_unpackstr(&msg->node_list, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpack32(&msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups > 0) {
			safe_unpack16_array(&msg->cpus_per_node, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&msg->cpu_count_reps, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		} else {
			msg->cpus_per_node = NULL;
			msg->cpu_count_reps = NULL;
		}
		safe_unpackstr(&msg->partition, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->resv_name, buffer);
		safe_unpackstr(&msg->tres_per_node, buffer);
		safe_unpack32(&msg->uid, buffer);
		safe_unpackstr(&msg->user_name, buffer);

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			slurmdb_unpack_cluster_rec(
				(void **) &msg->working_cluster_rec,
				smsg->protocol_version, buffer);
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resource_allocation_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_sbcast_cred_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_sbcast_cred_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		packstr(msg->node_list, buffer);
		pack_sbcast_cred(msg->sbcast_cred, buffer,
				 smsg->protocol_version);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(0, buffer); /* was job_id */
		packstr(msg->node_list, buffer);

		pack32(0, buffer); /* was node_cnt */
		pack_sbcast_cred(msg->sbcast_cred, buffer,
				 smsg->protocol_version);
	}
}

static int _unpack_job_sbcast_cred_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_sbcast_cred_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->node_list, buffer);
		msg->sbcast_cred = unpack_sbcast_cred(buffer, NULL,
						      smsg->protocol_version);
		if (msg->sbcast_cred == NULL)
			goto unpack_error;
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer); /* was job_id */
		safe_unpackstr(&msg->node_list, buffer);

		safe_unpack32(&uint32_tmp, buffer); /* was node_cnt */

		msg->sbcast_cred = unpack_sbcast_cred(buffer, NULL,
						      smsg->protocol_version);
		if (msg->sbcast_cred == NULL)
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_sbcast_cred_msg(msg);
	return SLURM_ERROR;
}

static void _pack_submit_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	submit_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->error_code, buffer);
		packstr(msg->job_submit_user_msg, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->step_id.step_id, buffer);
		pack32(msg->error_code, buffer);
		packstr(msg->job_submit_user_msg, buffer);
	}
}

static int _unpack_submit_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	submit_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->error_code, buffer);
		safe_unpackstr(&msg->job_submit_user_msg, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->step_id.step_id, buffer);
		safe_unpack32(&msg->error_code, buffer);
		safe_unpackstr(&msg->job_submit_user_msg, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_submit_response_response_msg(msg);
	return SLURM_ERROR;
}

static int _unpack_node_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	bitstr_t *hidden_nodes = NULL;
	node_info_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->record_count, buffer);
		safe_unpack_time(&msg->last_update, buffer);
		unpack_bit_str_hex(&hidden_nodes, buffer);

		safe_xcalloc(msg->node_array, msg->record_count,
			     sizeof(node_info_t));

		/* load individual job info */
		for (int i = 0; i < msg->record_count; i++) {
			if (hidden_nodes && bit_test(hidden_nodes, i)) {
				/* Nothing to unpack */
			} else if (_unpack_node_info_members(
					   &msg->node_array[i], buffer,
					   smsg->protocol_version)) {
				goto unpack_error;
			}
		}

		FREE_NULL_BITMAP(hidden_nodes);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_BITMAP(hidden_nodes);
	slurm_free_node_info_msg(msg);
	return SLURM_ERROR;
}

static int
_unpack_node_info_members(node_info_t * node, buf_t *buffer,
			  uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	double double_tmp;
	xassert(node);
	slurm_init_node_info_t(node, false);

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpackstr(&node->name, buffer);
		safe_unpackstr(&node->node_hostname, buffer);
		safe_unpackstr(&node->node_addr, buffer);
		safe_unpackstr(&node->bcast_address, buffer);
		safe_unpack16(&node->cert_flags, buffer);
		safe_unpack16(&node->port, buffer);
		safe_unpack32(&node->next_state, buffer);
		safe_unpack32(&node->node_state, buffer);
		safe_unpackstr(&node->version, buffer);

		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack64(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);

		safe_unpackstr(&node->gpu_spec, buffer);
		safe_unpackstr(&node->mcs_label, buffer);
		safe_unpack32(&node->owner, buffer);
		safe_unpack16(&node->core_spec_cnt, buffer);
		safe_unpack32(&node->cpu_bind, buffer);
		safe_unpack64(&node->mem_spec_limit, buffer);
		safe_unpackstr(&node->cpu_spec_list, buffer);
		safe_unpack16(&node->cpus_efctv, buffer);

		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack64(&node->free_mem, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack16(&node->res_cores_per_gpu, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->last_busy, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->resume_after, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);
		safe_unpack_time(&node->cert_last_renewal, buffer);

		safe_unpack16(&node->alloc_cpus, buffer);
		safe_unpack64(&node->alloc_memory, buffer);
		safe_unpackstr(&node->alloc_tres_fmt_str, buffer);

		safe_unpackstr(&node->arch, buffer);
		safe_unpackstr(&node->features, buffer);
		safe_unpackstr(&node->features_act, buffer);
		safe_unpackstr(&node->gres, buffer);
		safe_unpackstr(&node->gres_drain, buffer);
		safe_unpackstr(&node->gres_used, buffer);
		safe_unpackstr(&node->os, buffer);
		safe_unpackstr(&node->comment, buffer);
		safe_unpackstr(&node->extra, buffer);
		safe_unpackstr(&node->instance_id, buffer);
		safe_unpackstr(&node->instance_type, buffer);
		safe_unpackstr(&node->parameters, buffer);
		safe_unpackstr(&node->reason, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version,
					      1) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&node->tres_fmt_str, buffer);
		safe_unpackstr(&node->resv_name, buffer);
		safe_unpackstr(&node->topology_str, buffer);
	} else if (protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpackstr(&node->name, buffer);
		safe_unpackstr(&node->node_hostname, buffer);
		safe_unpackstr(&node->node_addr, buffer);
		safe_unpackstr(&node->bcast_address, buffer);
		safe_unpack16(&node->cert_flags, buffer);
		safe_unpack16(&node->port, buffer);
		safe_unpack32(&node->next_state, buffer);
		safe_unpack32(&node->node_state, buffer);
		safe_unpackstr(&node->version, buffer);

		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack64(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);

		safe_unpackstr(&node->gpu_spec, buffer);
		safe_unpackstr(&node->mcs_label, buffer);
		safe_unpack32(&node->owner, buffer);
		safe_unpack16(&node->core_spec_cnt, buffer);
		safe_unpack32(&node->cpu_bind, buffer);
		safe_unpack64(&node->mem_spec_limit, buffer);
		safe_unpackstr(&node->cpu_spec_list, buffer);
		safe_unpack16(&node->cpus_efctv, buffer);

		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack64(&node->free_mem, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack16(&node->res_cores_per_gpu, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->last_busy, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->resume_after, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);
		safe_unpack_time(&node->cert_last_renewal, buffer);

		safe_unpack16(&node->alloc_cpus, buffer);
		safe_unpack64(&node->alloc_memory, buffer);
		safe_unpackstr(&node->alloc_tres_fmt_str, buffer);

		safe_unpackstr(&node->arch, buffer);
		safe_unpackstr(&node->features, buffer);
		safe_unpackstr(&node->features_act, buffer);
		safe_unpackstr(&node->gres, buffer);
		safe_unpackstr(&node->gres_drain, buffer);
		safe_unpackstr(&node->gres_used, buffer);
		safe_unpackstr(&node->os, buffer);
		safe_unpackstr(&node->comment, buffer);
		safe_unpackstr(&node->extra, buffer);
		safe_unpackstr(&node->instance_id, buffer);
		safe_unpackstr(&node->instance_type, buffer);
		safe_unpackstr(&node->reason, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version,
					      1) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&node->tres_fmt_str, buffer);
		safe_unpackstr(&node->resv_name, buffer);
		safe_unpackstr(&node->topology_str, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&node->name, buffer);
		safe_unpackstr(&node->node_hostname, buffer);
		safe_unpackstr(&node->node_addr, buffer);
		safe_unpackstr(&node->bcast_address, buffer);
		safe_unpack16(&node->port, buffer);
		safe_unpack32(&node->next_state, buffer);
		safe_unpack32(&node->node_state, buffer);
		safe_unpackstr(&node->version, buffer);

		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack64(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);

		safe_unpackstr(&node->gpu_spec, buffer);
		safe_unpackstr(&node->mcs_label, buffer);
		safe_unpack32(&node->owner, buffer);
		safe_unpack16(&node->core_spec_cnt, buffer);
		safe_unpack32(&node->cpu_bind, buffer);
		safe_unpack64(&node->mem_spec_limit, buffer);
		safe_unpackstr(&node->cpu_spec_list, buffer);
		safe_unpack16(&node->cpus_efctv, buffer);

		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack64(&node->free_mem, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack16(&node->res_cores_per_gpu, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->last_busy, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->resume_after, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);

		safe_unpack32(&uint32_tmp, buffer); /* was select plugin_id */
		safe_unpack16(&node->alloc_cpus, buffer);
		safe_unpack64(&node->alloc_memory, buffer);
		safe_unpackstr(&node->alloc_tres_fmt_str, buffer);
		safe_unpackdouble(&double_tmp, buffer); /* was alloc_tres_weighted */

		safe_unpackstr(&node->arch, buffer);
		safe_unpackstr(&node->features, buffer);
		safe_unpackstr(&node->features_act, buffer);
		safe_unpackstr(&node->gres, buffer);
		safe_unpackstr(&node->gres_drain, buffer);
		safe_unpackstr(&node->gres_used, buffer);
		safe_unpackstr(&node->os, buffer);
		safe_unpackstr(&node->comment, buffer);
		safe_unpackstr(&node->extra, buffer);
		safe_unpackstr(&node->instance_id, buffer);
		safe_unpackstr(&node->instance_type, buffer);
		safe_unpackstr(&node->reason, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&node->tres_fmt_str, buffer);
		safe_unpackstr(&node->resv_name, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_members(node);
	return SLURM_ERROR;
}

static void _pack_update_partition_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	update_part_msg_t *msg = smsg->data;
	xassert(msg);

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		packstr(msg->allow_accounts, buffer);
		packstr(msg->allow_alloc_nodes, buffer);
		packstr(msg->allow_groups, buffer);
		packstr(msg->allow_qos, buffer);
		packstr(msg->alternate, buffer);
		packstr(msg->billing_weights_str, buffer);

		pack32(msg->cpu_bind, buffer);
		pack64(msg->def_mem_per_cpu, buffer);
		pack32(msg->default_time, buffer);
		packstr(msg->deny_accounts, buffer);
		packstr(msg->deny_qos, buffer);
		pack32(msg->flags, buffer);
		packstr(msg->job_defaults_str, buffer);
		pack32(msg->grace_time, buffer);

		pack32(msg->max_cpus_per_node, buffer);
		pack32(msg->max_cpus_per_socket, buffer);
		pack64(msg->max_mem_per_cpu, buffer);
		pack32(msg->max_nodes, buffer);
		pack16(msg->max_share, buffer);
		pack32(msg->max_time, buffer);
		pack32(msg->min_nodes, buffer);

		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);

		pack16(msg->over_time_limit, buffer);
		pack16(msg->preempt_mode, buffer);
		pack16(msg->priority_job_factor, buffer);
		pack16(msg->priority_tier, buffer);
		packstr(msg->qos_char, buffer);
		pack16(msg->state_up, buffer);
		packstr(msg->topology_name, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->allow_accounts, buffer);
		packstr(msg->allow_alloc_nodes, buffer);
		packstr(msg->allow_groups, buffer);
		packstr(msg->allow_qos, buffer);
		packstr(msg->alternate, buffer);
		packstr(msg->billing_weights_str,  buffer);

		pack32(msg->cpu_bind, buffer);
		pack64(msg->def_mem_per_cpu, buffer);
		pack32(msg->default_time, buffer);
		packstr(msg->deny_accounts, buffer);
		packstr(msg->deny_qos, buffer);
		pack32(msg->flags, buffer);
		packstr(msg->job_defaults_str, buffer);
		pack32(msg->grace_time, buffer);

		pack32(msg->max_cpus_per_node, buffer);
		pack32(msg->max_cpus_per_socket, buffer);
		pack64(msg->max_mem_per_cpu, buffer);
		pack32(msg->max_nodes, buffer);
		pack16(msg->max_share, buffer);
		pack32(msg->max_time, buffer);
		pack32(msg->min_nodes, buffer);

		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);

		pack16(msg->over_time_limit, buffer);
		pack16(msg->preempt_mode, buffer);
		pack16(msg->priority_job_factor, buffer);
		pack16(msg->priority_tier, buffer);
		packstr(msg->qos_char, buffer);
		pack16(msg->state_up, buffer);
	}
}

static int _unpack_update_partition_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	update_part_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->allow_accounts, buffer);
		safe_unpackstr(&msg->allow_alloc_nodes, buffer);
		safe_unpackstr(&msg->allow_groups, buffer);
		safe_unpackstr(&msg->allow_qos, buffer);
		safe_unpackstr(&msg->alternate, buffer);
		safe_unpackstr(&msg->billing_weights_str, buffer);

		safe_unpack32(&msg->cpu_bind, buffer);
		safe_unpack64(&msg->def_mem_per_cpu, buffer);
		safe_unpack32(&msg->default_time, buffer);
		safe_unpackstr(&msg->deny_accounts, buffer);
		safe_unpackstr(&msg->deny_qos, buffer);
		safe_unpack32(&msg->flags, buffer);
		safe_unpackstr(&msg->job_defaults_str, buffer);
		safe_unpack32(&msg->grace_time, buffer);

		safe_unpack32(&msg->max_cpus_per_node, buffer);
		safe_unpack32(&msg->max_cpus_per_socket, buffer);
		safe_unpack64(&msg->max_mem_per_cpu, buffer);
		safe_unpack32(&msg->max_nodes, buffer);
		safe_unpack16(&msg->max_share, buffer);
		safe_unpack32(&msg->max_time, buffer);
		safe_unpack32(&msg->min_nodes, buffer);

		safe_unpackstr(&msg->name, buffer);
		safe_unpackstr(&msg->nodes, buffer);

		safe_unpack16(&msg->over_time_limit, buffer);
		safe_unpack16(&msg->preempt_mode, buffer);
		safe_unpack16(&msg->priority_job_factor, buffer);
		safe_unpack16(&msg->priority_tier, buffer);
		safe_unpackstr(&msg->qos_char, buffer);
		safe_unpack16(&msg->state_up, buffer);
		safe_unpackstr(&msg->topology_name, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->allow_accounts, buffer);
		safe_unpackstr(&msg->allow_alloc_nodes, buffer);
		safe_unpackstr(&msg->allow_groups, buffer);
		safe_unpackstr(&msg->allow_qos, buffer);
		safe_unpackstr(&msg->alternate, buffer);
		safe_unpackstr(&msg->billing_weights_str, buffer);

		safe_unpack32(&msg->cpu_bind, buffer);
		safe_unpack64(&msg->def_mem_per_cpu, buffer);
		safe_unpack32(&msg->default_time, buffer);
		safe_unpackstr(&msg->deny_accounts, buffer);
		safe_unpackstr(&msg->deny_qos, buffer);
		safe_unpack32(&msg->flags, buffer);
		safe_unpackstr(&msg->job_defaults_str, buffer);
		safe_unpack32(&msg->grace_time, buffer);

		safe_unpack32(&msg->max_cpus_per_node, buffer);
		safe_unpack32(&msg->max_cpus_per_socket, buffer);
		safe_unpack64(&msg->max_mem_per_cpu, buffer);
		safe_unpack32(&msg->max_nodes, buffer);
		safe_unpack16(&msg->max_share, buffer);
		safe_unpack32(&msg->max_time, buffer);
		safe_unpack32(&msg->min_nodes, buffer);

		safe_unpackstr(&msg->name, buffer);
		safe_unpackstr(&msg->nodes, buffer);

		safe_unpack16(&msg->over_time_limit, buffer);
		safe_unpack16(&msg->preempt_mode, buffer);
		safe_unpack16(&msg->priority_job_factor, buffer);
		safe_unpack16(&msg->priority_tier, buffer);
		safe_unpackstr(&msg->qos_char, buffer);
		safe_unpack16(&msg->state_up, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_part_msg(msg);
	return SLURM_ERROR;
}

static void _pack_update_resv_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	resv_desc_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->end_time, buffer);
		pack32(msg->duration, buffer);
		pack64(msg->flags, buffer);
		pack32(msg->node_cnt, buffer);
		pack32(msg->core_cnt, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->features, buffer);
		packstr(msg->licenses, buffer);
		pack32(msg->max_start_delay, buffer);
		packstr(msg->partition, buffer);
		pack32(msg->purge_comp_time, buffer);
		packstr(msg->allowed_parts, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->users, buffer);
		packstr(msg->accounts, buffer);
		packstr(msg->burst_buffer, buffer);
		packstr(msg->groups, buffer);
		packstr(msg->comment, buffer);
		packstr(msg->tres_str, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->end_time, buffer);
		pack32(msg->duration, buffer);
		pack64(msg->flags, buffer);
		pack32(msg->node_cnt, buffer);
		pack32(msg->core_cnt, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->features, buffer);
		packstr(msg->licenses, buffer);
		pack32(msg->max_start_delay, buffer);
		packstr(msg->partition, buffer);
		pack32(msg->purge_comp_time, buffer);
		pack32(NO_VAL, buffer); /* was resv_watts */
		packstr(msg->users, buffer);
		packstr(msg->accounts, buffer);
		packstr(msg->burst_buffer, buffer);
		packstr(msg->groups, buffer);
		packstr(msg->comment, buffer);
		packstr(msg->tres_str, buffer);
	}
}

static int _unpack_update_resv_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp = 0;
	resv_desc_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->name, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpack_time(&msg->end_time, buffer);
		safe_unpack32(&msg->duration, buffer);
		safe_unpack64(&msg->flags, buffer);
		safe_unpack32(&msg->node_cnt, buffer);
		safe_unpack32(&msg->core_cnt, buffer);
		safe_unpackstr(&msg->node_list, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpackstr(&msg->licenses, buffer);

		safe_unpack32(&msg->max_start_delay, buffer);

		safe_unpackstr(&msg->partition, buffer);
		safe_unpack32(&msg->purge_comp_time, buffer);

		safe_unpackstr(&msg->allowed_parts, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->users, buffer);
		safe_unpackstr(&msg->accounts, buffer);
		safe_unpackstr(&msg->burst_buffer, buffer);
		safe_unpackstr(&msg->groups, buffer);
		safe_unpackstr(&msg->comment, buffer);
		safe_unpackstr(&msg->tres_str, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->name, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpack_time(&msg->end_time, buffer);
		safe_unpack32(&msg->duration, buffer);
		safe_unpack64(&msg->flags, buffer);
		safe_unpack32(&msg->node_cnt, buffer);
		safe_unpack32(&msg->core_cnt, buffer);
		safe_unpackstr(&msg->node_list, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpackstr(&msg->licenses, buffer);

		safe_unpack32(&msg->max_start_delay, buffer);

		safe_unpackstr(&msg->partition, buffer);
		safe_unpack32(&msg->purge_comp_time, buffer);
		safe_unpack32(&uint32_tmp, buffer); /* was resv_watts */
		safe_unpackstr(&msg->users, buffer);
		safe_unpackstr(&msg->accounts, buffer);
		safe_unpackstr(&msg->burst_buffer, buffer);
		safe_unpackstr(&msg->groups, buffer);
		safe_unpackstr(&msg->comment, buffer);
		safe_unpackstr(&msg->tres_str, buffer);
	}

	if (!msg->core_cnt)
		msg->core_cnt = NO_VAL;

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_desc_msg(msg);
	return SLURM_ERROR;
}

static void _pack_delete_partition_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	delete_part_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
	}
}

static int _unpack_delete_partition_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	delete_part_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->name, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_delete_part_msg(msg);
	return SLURM_ERROR;
}

static void _pack_resv_name_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	reservation_name_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
	}
}

static int _unpack_resv_name_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	reservation_name_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->name, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_name_msg(msg);
	return SLURM_ERROR;
}

static int _foreach_pack_list(void *x, void *arg)
{
	void *object = x;
	pack_list_t *pack_list = arg;

	(*(pack_list->pack_function))(object,
				      pack_list->protocol_version,
				      pack_list->buffer);
	if (size_buf(pack_list->buffer) > pack_list->max_buf_size) {
		error("%s: size limit exceeded", __func__);
		/*
		 * rewind by one element to stay smaller than
		 * pack_list->max_buf_size
		 */
		set_buf_offset(pack_list->buffer, pack_list->header_position);
		pack32(pack_list->count, pack_list->buffer);
		set_buf_offset(pack_list->buffer,
			       pack_list->last_good_position);
		pack_list->rc = ESLURM_RESULT_TOO_LARGE;
		return -1;
	}

	pack_list->last_good_position =	get_buf_offset(pack_list->buffer);
	pack_list->count += 1;

	return 0;
}

static int _pack_list_internal(list_t *send_list, pack_list_t *pack_list)
{
	int count;

	if (!send_list) {
		// to let user know there wasn't a list (error)
		pack32(NO_VAL, pack_list->buffer);
		return pack_list->rc;
	}

	pack_list->header_position = get_buf_offset(pack_list->buffer);

	count = list_count(send_list);
	pack32(count, pack_list->buffer);

	if (count) {
		pack_list->count = 0; /* force zero */
		pack_list->last_good_position =
			get_buf_offset(pack_list->buffer);
		(void) list_for_each_ro(
			send_list, _foreach_pack_list, pack_list);
	}

	return pack_list->rc;
}

extern int slurm_pack_list(list_t *send_list,
			   void (*pack_function) (void *object,
						  uint16_t protocol_version,
						  buf_t *buffer),
			   buf_t *buffer, uint16_t protocol_version)
{
	pack_list_t pack_list = {
		.buffer = buffer,
		.max_buf_size = REASONABLE_BUF_SIZE,
		.pack_function = pack_function,
		.protocol_version = protocol_version,
		.rc = SLURM_SUCCESS,
	};

	return _pack_list_internal(send_list, &pack_list);
}

extern int slurm_pack_list_until(list_t *send_list,
				 pack_function_t pack_function,
				 buf_t *buffer, uint32_t max_buf_size,
				 uint16_t protocol_version)
{
	pack_list_t pack_list = {
		.buffer = buffer,
		.max_buf_size = max_buf_size,
		.pack_function = pack_function,
		.protocol_version = protocol_version,
		.rc = SLURM_SUCCESS,
	};

	return _pack_list_internal(send_list, &pack_list);
}

extern int slurm_unpack_list(list_t **recv_list,
			     int (*unpack_function) (void **object,
						     uint16_t protocol_version,
						     buf_t *buffer),
			     void (*destroy_function) (void *object),
			     buf_t *buffer, uint16_t protocol_version)
{
	uint32_t count;

	xassert(recv_list);

	safe_unpack32(&count, buffer);

	if (count > NO_VAL)
		return SLURM_ERROR;

	if (count != NO_VAL) {
		int i;
		void *object = NULL;

		/*
		 * Build the list for zero or more objects. If NO_VAL
		 * was packed this indicates an error, and no list is
		 * created.
		 */
		*recv_list = list_create((*(destroy_function)));
		for (i = 0; i < count; i++) {
			if (((*(unpack_function))(&object,
						  protocol_version, buffer))
			    == SLURM_ERROR)
				goto unpack_error;
			if (object)
				list_append(*recv_list, object);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*recv_list);
	return SLURM_ERROR;
}

static void _pack_job_step_create_request_msg(const slurm_msg_t *smsg,
					      buf_t *buffer)
{
	job_step_create_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->array_task_id, buffer);
		pack32(msg->user_id, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		packstr(msg->container, buffer);
		packstr(msg->container_id, buffer);
		pack32(msg->cpu_count, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		pack32(msg->num_tasks, buffer);
		pack64(msg->pn_min_memory, buffer);
		pack32(msg->time_limit, buffer);
		pack16(msg->threads_per_core, buffer);
		pack16(msg->ntasks_per_core, buffer);

		pack16(msg->relative, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->plane_size, buffer);
		pack16(msg->port, buffer);
		pack16(msg->immediate, buffer);
		pack16(msg->resv_port_cnt, buffer);
		pack32(msg->srun_pid, buffer);
		pack32(msg->flags, buffer);

		packstr(msg->host, buffer);
		packstr(msg->name, buffer);
		packstr(msg->network, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->exc_nodes, buffer);
		packstr(msg->features, buffer);
		pack32(msg->step_het_comp_cnt, buffer);
		packstr(msg->step_het_grps, buffer);

		packstr(msg->cpus_per_tres, buffer);
		packstr(msg->mem_per_tres, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		packstr(msg->cwd, buffer);
		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);
		packstr(msg->submit_line, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_step, buffer);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_socket, buffer);
		packstr(msg->tres_per_task, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->array_task_id, buffer);
		pack32(msg->user_id, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		packstr(msg->container, buffer);
		packstr(msg->container_id, buffer);
		pack32(msg->cpu_count, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		pack32(msg->num_tasks, buffer);
		pack64(msg->pn_min_memory, buffer);
		pack32(msg->time_limit, buffer);
		pack16(msg->threads_per_core, buffer);
		pack16(msg->ntasks_per_core, buffer);

		pack16(msg->relative, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->plane_size, buffer);
		pack16(msg->port, buffer);
		pack16(msg->immediate, buffer);
		pack16(msg->resv_port_cnt, buffer);
		pack32(msg->srun_pid, buffer);
		pack32(msg->flags, buffer);

		packstr(msg->host, buffer);
		packstr(msg->name, buffer);
		packstr(msg->network, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->exc_nodes, buffer);
		packstr(msg->features, buffer);
		pack32(msg->step_het_comp_cnt, buffer);
		packstr(msg->step_het_grps, buffer);

		packstr(msg->cpus_per_tres, buffer);
		packstr(msg->mem_per_tres, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		packstr(msg->submit_line, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_step, buffer);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_socket, buffer);
		packstr(msg->tres_per_task, buffer);
	}
}

static int _unpack_job_step_create_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_create_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->array_task_id, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpack32(&msg->min_nodes, buffer);
		safe_unpack32(&msg->max_nodes, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpackstr(&msg->container_id, buffer);
		safe_unpack32(&msg->cpu_count, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);
		safe_unpack32(&msg->num_tasks, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);
		safe_unpack32(&msg->time_limit, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);

		safe_unpack16(&msg->relative, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->plane_size, buffer);
		safe_unpack16(&msg->port, buffer);
		safe_unpack16(&msg->immediate, buffer);
		safe_unpack16(&msg->resv_port_cnt, buffer);
		safe_unpack32(&msg->srun_pid, buffer);
		safe_unpack32(&msg->flags, buffer);

		safe_unpackstr(&msg->host, buffer);
		safe_unpackstr(&msg->name, buffer);
		safe_unpackstr(&msg->network, buffer);
		safe_unpackstr(&msg->node_list, buffer);
		safe_unpackstr(&msg->exc_nodes, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpack32(&msg->step_het_comp_cnt, buffer);
		safe_unpackstr(&msg->step_het_grps, buffer);

		safe_unpackstr(&msg->cpus_per_tres, buffer);
		safe_unpackstr(&msg->mem_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpackstr(&msg->cwd, buffer);
		safe_unpackstr(&msg->std_err, buffer);
		safe_unpackstr(&msg->std_in, buffer);
		safe_unpackstr(&msg->std_out, buffer);
		safe_unpackstr(&msg->submit_line, buffer);
		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpackstr(&msg->tres_per_step, buffer);
		safe_unpackstr(&msg->tres_per_node, buffer);
		safe_unpackstr(&msg->tres_per_socket, buffer);
		safe_unpackstr(&msg->tres_per_task, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->array_task_id, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpack32(&msg->min_nodes, buffer);
		safe_unpack32(&msg->max_nodes, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpackstr(&msg->container_id, buffer);
		safe_unpack32(&msg->cpu_count, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);
		safe_unpack32(&msg->num_tasks, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);
		safe_unpack32(&msg->time_limit, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);

		safe_unpack16(&msg->relative, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->plane_size, buffer);
		safe_unpack16(&msg->port, buffer);
		safe_unpack16(&msg->immediate, buffer);
		safe_unpack16(&msg->resv_port_cnt, buffer);
		safe_unpack32(&msg->srun_pid, buffer);
		safe_unpack32(&msg->flags, buffer);

		safe_unpackstr(&msg->host, buffer);
		safe_unpackstr(&msg->name, buffer);
		safe_unpackstr(&msg->network, buffer);
		safe_unpackstr(&msg->node_list, buffer);
		safe_unpackstr(&msg->exc_nodes, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpack32(&msg->step_het_comp_cnt, buffer);
		safe_unpackstr(&msg->step_het_grps, buffer);

		safe_unpackstr(&msg->cpus_per_tres, buffer);
		safe_unpackstr(&msg->mem_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpackstr(&msg->submit_line, buffer);
		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpackstr(&msg->tres_per_step, buffer);
		safe_unpackstr(&msg->tres_per_node, buffer);
		safe_unpackstr(&msg->tres_per_socket, buffer);
		safe_unpackstr(&msg->tres_per_task, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_create_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_kill_job_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	kill_job_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		if (msg->cred) {
			pack8(1, buffer);
			slurm_cred_pack(msg->cred, buffer,
					smsg->protocol_version);
		} else
			pack8(0, buffer);
		packstr(msg->details, buffer);
		pack32(msg->derived_ec, buffer);
		pack32(msg->exit_code, buffer);
		slurm_pack_list(msg->job_gres_prep, gres_prep_pack, buffer,
				smsg->protocol_version);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->het_job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->job_uid, buffer);
		pack32(msg->job_gid, buffer);
		packstr(msg->nodes, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->time, buffer);
		packstr(msg->work_dir, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (msg->cred) {
			pack8(1, buffer);
			slurm_cred_pack(msg->cred, buffer,
					smsg->protocol_version);
		} else
			pack8(0, buffer);
		packstr(msg->details, buffer);
		pack32(msg->derived_ec, buffer);
		pack32(msg->exit_code, buffer);
		gres_prep_pack_legacy(msg->job_gres_prep, buffer,
				      smsg->protocol_version);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->het_job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->job_uid, buffer);
		pack32(msg->job_gid, buffer);
		packstr(msg->nodes, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->time, buffer);
		packstr(msg->work_dir, buffer);
	}
}

static int _unpack_kill_job_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint8_t uint8_tmp;
	kill_job_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			msg->cred = slurm_cred_unpack(buffer,
						      smsg->protocol_version);
			if (!msg->cred)
				goto unpack_error;
		}
		safe_unpackstr(&msg->details, buffer);
		safe_unpack32(&msg->derived_ec, buffer);
		safe_unpack32(&msg->exit_code, buffer);
		if (gres_prep_unpack_list(&msg->job_gres_prep, buffer,
					  smsg->protocol_version))
			goto unpack_error;
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32(&msg->job_state, buffer);
		safe_unpack32(&msg->job_uid, buffer);
		safe_unpack32(&msg->job_gid, buffer);
		safe_unpackstr(&msg->nodes, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpack_time(&msg->time, buffer);
		safe_unpackstr(&msg->work_dir, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			msg->cred = slurm_cred_unpack(buffer,
						      smsg->protocol_version);
			if (!msg->cred)
				goto unpack_error;
		}
		safe_unpackstr(&msg->details, buffer);
		safe_unpack32(&msg->derived_ec, buffer);
		safe_unpack32(&msg->exit_code, buffer);
		if (gres_prep_unpack_legacy(&msg->job_gres_prep, buffer,
					    smsg->protocol_version))
			goto unpack_error;
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32(&msg->job_state, buffer);
		safe_unpack32(&msg->job_uid, buffer);
		safe_unpack32(&msg->job_gid, buffer);
		safe_unpackstr(&msg->nodes, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpack_time(&msg->time, buffer);
		safe_unpackstr(&msg->work_dir, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_kill_job_msg(msg);
	return SLURM_ERROR;
}

static void _pack_epilog_comp_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	epilog_complete_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->return_code, buffer);
		packstr(msg->node_name, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->return_code, buffer);
		packstr(msg->node_name, buffer);
	}
}

static int _unpack_epilog_comp_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	epilog_complete_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->return_code, buffer);
		safe_unpackstr(&msg->node_name, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->return_code, buffer);
		safe_unpackstr(&msg->node_name, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_epilog_complete_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_step_create_response_msg(const slurm_msg_t *smsg,
					       buf_t *buffer)
{
	job_step_create_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(msg->def_cpu_bind_type, buffer);
		packstr(msg->resv_ports, buffer);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack_slurm_step_layout(msg->step_layout, buffer,
				       smsg->protocol_version);
		packstr(msg->stepmgr, buffer);
		slurm_cred_pack(msg->cred, buffer, smsg->protocol_version);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->def_cpu_bind_type, buffer);
		packstr(msg->resv_ports, buffer);
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->step_id.step_id, buffer);
		pack_slurm_step_layout(msg->step_layout, buffer,
				       smsg->protocol_version);
		packstr(msg->stepmgr, buffer);
		slurm_cred_pack(msg->cred, buffer, smsg->protocol_version);
		pack16(msg->use_protocol_ver, buffer);
	}
}

static int _unpack_job_step_create_response_msg(slurm_msg_t *smsg,
						buf_t *buffer)
{
	job_step_create_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->def_cpu_bind_type, buffer);
		safe_unpackstr(&msg->resv_ports, buffer);
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		if (unpack_slurm_step_layout(&msg->step_layout, buffer,
					     smsg->protocol_version))
			goto unpack_error;
		safe_unpackstr(&msg->stepmgr, buffer);

		if (!(msg->cred = slurm_cred_unpack(buffer,
						    smsg->protocol_version)))
			goto unpack_error;
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->def_cpu_bind_type, buffer);
		safe_unpackstr(&msg->resv_ports, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->step_id.step_id, buffer);
		if (unpack_slurm_step_layout(&msg->step_layout, buffer,
					     smsg->protocol_version))
			goto unpack_error;
		safe_unpackstr(&msg->stepmgr, buffer);

		if (!(msg->cred = slurm_cred_unpack(buffer,
						    smsg->protocol_version)))
			goto unpack_error;

		safe_unpack16(&msg->use_protocol_ver, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_create_response_msg(msg);
	return SLURM_ERROR;
}

static int _unpack_partition_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	partition_info_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->record_count, buffer);
		safe_unpack_time(&msg->last_update, buffer);

		safe_xcalloc(msg->partition_array, msg->record_count,
			     sizeof(partition_info_t));

		/* load individual partition info */
		for (int i = 0; i < msg->record_count; i++) {
			if (_unpack_partition_info_members(
				    &msg->partition_array[i], buffer,
				    smsg->protocol_version))
				goto unpack_error;
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_partition_info_msg(msg);
	return SLURM_ERROR;
}


static int
_unpack_partition_info_members(partition_info_t * part, buf_t *buffer,
			       uint16_t protocol_version)
{
	if (protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpackstr(&part->name, buffer);
		if (part->name == NULL)
			part->name = xmalloc(1); /* part->name = "" implicit */
		safe_unpack32(&part->cpu_bind, buffer);
		safe_unpack32(&part->grace_time, buffer);
		safe_unpack32(&part->max_time, buffer);
		safe_unpack32(&part->default_time, buffer);
		safe_unpack32(&part->max_nodes, buffer);
		safe_unpack32(&part->min_nodes, buffer);
		safe_unpack32(&part->total_nodes, buffer);
		safe_unpack32(&part->total_cpus, buffer);
		safe_unpack64(&part->def_mem_per_cpu, buffer);
		safe_unpack32(&part->max_cpus_per_node, buffer);
		safe_unpack32(&part->max_cpus_per_socket, buffer);
		safe_unpack64(&part->max_mem_per_cpu, buffer);
		safe_unpack32(&part->flags, buffer);
		safe_unpack16(&part->max_share, buffer);
		safe_unpack16(&part->over_time_limit, buffer);
		safe_unpack16(&part->preempt_mode, buffer);
		safe_unpack16(&part->priority_job_factor, buffer);
		safe_unpack16(&part->priority_tier, buffer);
		safe_unpack16(&part->state_up, buffer);
		safe_unpack16(&part->cr_type, buffer);
		safe_unpack16(&part->resume_timeout, buffer);
		safe_unpack16(&part->suspend_timeout, buffer);
		safe_unpack32(&part->suspend_time, buffer);

		safe_unpackstr(&part->allow_accounts, buffer);
		safe_unpackstr(&part->allow_groups, buffer);
		safe_unpackstr(&part->allow_alloc_nodes, buffer);
		safe_unpackstr(&part->allow_qos, buffer);
		safe_unpackstr(&part->qos_char, buffer);
		safe_unpackstr(&part->alternate, buffer);
		safe_unpackstr(&part->deny_accounts, buffer);
		safe_unpackstr(&part->deny_qos, buffer);
		safe_unpackstr(&part->nodes, buffer);
		safe_unpackstr(&part->nodesets, buffer);

		unpack_bit_str_hex_as_inx(&part->node_inx, buffer);

		safe_unpackstr(&part->billing_weights_str, buffer);
		safe_unpackstr(&part->topology_name, buffer);
		safe_unpackstr(&part->tres_fmt_str, buffer);
		if (slurm_unpack_list(&part->job_defaults_list,
				      job_defaults_unpack, xfree_ptr, buffer,
				      protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&part->name, buffer);
		if (part->name == NULL)
			part->name = xmalloc(1);/* part->name = "" implicit */
		safe_unpack32(&part->cpu_bind, buffer);
		safe_unpack32(&part->grace_time, buffer);
		safe_unpack32(&part->max_time, buffer);
		safe_unpack32(&part->default_time, buffer);
		safe_unpack32(&part->max_nodes, buffer);
		safe_unpack32(&part->min_nodes, buffer);
		safe_unpack32(&part->total_nodes, buffer);
		safe_unpack32(&part->total_cpus, buffer);
		safe_unpack64(&part->def_mem_per_cpu, buffer);
		safe_unpack32(&part->max_cpus_per_node, buffer);
		safe_unpack32(&part->max_cpus_per_socket, buffer);
		safe_unpack64(&part->max_mem_per_cpu, buffer);
		safe_unpack32(&part->flags, buffer);
		safe_unpack16(&part->max_share, buffer);
		safe_unpack16(&part->over_time_limit, buffer);
		safe_unpack16(&part->preempt_mode, buffer);
		safe_unpack16(&part->priority_job_factor, buffer);
		safe_unpack16(&part->priority_tier, buffer);
		safe_unpack16(&part->state_up, buffer);
		safe_unpack16(&part->cr_type, buffer);
		safe_unpack16(&part->resume_timeout, buffer);
		safe_unpack16(&part->suspend_timeout, buffer);
		safe_unpack32(&part->suspend_time, buffer);

		safe_unpackstr(&part->allow_accounts, buffer);
		safe_unpackstr(&part->allow_groups, buffer);
		safe_unpackstr(&part->allow_alloc_nodes, buffer);
		safe_unpackstr(&part->allow_qos, buffer);
		safe_unpackstr(&part->qos_char, buffer);
		safe_unpackstr(&part->alternate, buffer);
		safe_unpackstr(&part->deny_accounts, buffer);
		safe_unpackstr(&part->deny_qos, buffer);
		safe_unpackstr(&part->nodes, buffer);
		safe_unpackstr(&part->nodesets, buffer);

		unpack_bit_str_hex_as_inx(&part->node_inx, buffer);

		safe_unpackstr(&part->billing_weights_str, buffer);
		safe_unpackstr(&part->tres_fmt_str, buffer);
		if (slurm_unpack_list(&part->job_defaults_list,
				      job_defaults_unpack, xfree_ptr,
				      buffer, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_partition_info_members(part);
	return SLURM_ERROR;
}

static int _unpack_reserve_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	reserve_info_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->record_count, buffer);
		safe_unpack_time(&msg->last_update, buffer);

		safe_xcalloc(msg->reservation_array, msg->record_count,
			     sizeof(reserve_info_t));

		/* load individual reservation records */
		for (int i = 0; i < msg->record_count; i++) {
			if (_unpack_reserve_info_members(
				    &msg->reservation_array[i], buffer,
				    smsg->protocol_version))
				goto unpack_error;
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reservation_info_msg(msg);
	return SLURM_ERROR;
}


static int
_unpack_reserve_info_members(reserve_info_t * resv, buf_t *buffer,
			     uint16_t protocol_version)
{
	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpackstr(&resv->accounts, buffer);
		safe_unpackstr(&resv->burst_buffer,buffer);
		safe_unpackstr(&resv->comment, buffer);
		safe_unpack32(&resv->core_cnt, buffer);
		safe_unpack_time(&resv->end_time, buffer);
		safe_unpackstr(&resv->features, buffer);
		safe_unpack64(&resv->flags, buffer);
		safe_unpackstr(&resv->licenses, buffer);
		safe_unpack32(&resv->max_start_delay, buffer);
		safe_unpackstr(&resv->name, buffer);
		safe_unpack32(&resv->node_cnt, buffer);
		safe_unpackstr(&resv->node_list, buffer);
		safe_unpackstr(&resv->partition, buffer);
		safe_unpack32(&resv->purge_comp_time, buffer);
		safe_unpack_time(&resv->start_time, buffer);

		safe_unpackstr(&resv->tres_str, buffer);
		safe_unpackstr(&resv->users, buffer);
		safe_unpackstr(&resv->groups, buffer);
		safe_unpackstr(&resv->qos, buffer);
		safe_unpackstr(&resv->allowed_parts, buffer);

		unpack_bit_str_hex_as_inx(&resv->node_inx, buffer);

		safe_unpack32(&resv->core_spec_cnt, buffer);
		if (resv->core_spec_cnt > 0) {
			safe_xcalloc(resv->core_spec, resv->core_spec_cnt,
				     sizeof(resv_core_spec_t));
		}
		for (int i = 0; i < resv->core_spec_cnt; i++) {
			safe_unpackstr(&resv->core_spec[i].node_name, buffer);
			safe_unpackstr(&resv->core_spec[i].core_id, buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t uint32_tmp;
		safe_unpackstr(&resv->accounts, buffer);
		safe_unpackstr(&resv->burst_buffer,buffer);
		safe_unpackstr(&resv->comment, buffer);
		safe_unpack32(&resv->core_cnt, buffer);
		safe_unpack_time(&resv->end_time, buffer);
		safe_unpackstr(&resv->features, buffer);
		safe_unpack64(&resv->flags, buffer);
		safe_unpackstr(&resv->licenses, buffer);
		safe_unpack32(&resv->max_start_delay, buffer);
		safe_unpackstr(&resv->name, buffer);
		safe_unpack32(&resv->node_cnt, buffer);
		safe_unpackstr(&resv->node_list, buffer);
		safe_unpackstr(&resv->partition, buffer);
		safe_unpack32(&resv->purge_comp_time, buffer);
		safe_unpack32(&uint32_tmp, buffer); /* was resv_watts */
		safe_unpack_time(&resv->start_time, buffer);

		safe_unpackstr(&resv->tres_str, buffer);
		safe_unpackstr(&resv->users, buffer);
		safe_unpackstr(&resv->groups, buffer);

		unpack_bit_str_hex_as_inx(&resv->node_inx, buffer);

		safe_unpack32(&resv->core_spec_cnt, buffer);
		if (resv->core_spec_cnt > 0) {
			safe_xcalloc(resv->core_spec, resv->core_spec_cnt,
				     sizeof(resv_core_spec_t));
		}
		for (int i = 0; i < resv->core_spec_cnt; i++) {
			safe_unpackstr(&resv->core_spec[i].node_name, buffer);
			safe_unpackstr(&resv->core_spec[i].core_id, buffer);
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reserve_info_members(resv);
	return SLURM_ERROR;
}

/* _unpack_job_step_info_members
 * unpacks a set of slurm job step info for one job step
 * OUT step - pointer to the job step info buffer
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 * Note: This is packed by _pack_ctld_job_step_info()
 */
static int
_unpack_job_step_info_members(job_step_info_t * step, buf_t *buffer,
			      uint16_t protocol_version)
{
	if (protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpack32(&step->array_job_id, buffer);
		safe_unpack32(&step->array_task_id, buffer);
		safe_unpack_step_id_members(&step->step_id, buffer,
					    protocol_version);
		safe_unpack32(&step->user_id, buffer);
		safe_unpack32(&step->num_cpus, buffer);
		safe_unpack32(&step->cpu_freq_min, buffer);
		safe_unpack32(&step->cpu_freq_max, buffer);
		safe_unpack32(&step->cpu_freq_gov, buffer);
		safe_unpack32(&step->num_tasks, buffer);
		safe_unpack32(&step->task_dist, buffer);
		safe_unpack32(&step->time_limit, buffer);
		safe_unpack32(&step->state, buffer);
		safe_unpack32(&step->srun_pid, buffer);

		safe_unpack_time(&step->start_time, buffer);
		safe_unpack_time(&step->run_time, buffer);

		safe_unpackstr(&step->cluster, buffer);
		safe_unpackstr(&step->container, buffer);
		safe_unpackstr(&step->container_id, buffer);
		safe_unpackstr(&step->partition, buffer);
		safe_unpackstr(&step->srun_host, buffer);
		safe_unpackstr(&step->resv_ports, buffer);
		safe_unpackstr(&step->nodes, buffer);
		safe_unpackstr(&step->name, buffer);
		safe_unpackstr(&step->network, buffer);
		unpack_bit_str_hex_as_inx(&step->node_inx, buffer);

		safe_unpackstr(&step->tres_fmt_alloc_str, buffer);
		safe_unpack16(&step->start_protocol_ver, buffer);

		safe_unpackstr(&step->cpus_per_tres, buffer);
		safe_unpackstr(&step->mem_per_tres, buffer);
		safe_unpackstr(&step->job_name, buffer);
		safe_unpackstr(&step->cwd, buffer);
		safe_unpackstr(&step->std_err, buffer);
		safe_unpackstr(&step->std_in, buffer);
		safe_unpackstr(&step->std_out, buffer);
		safe_unpackstr(&step->submit_line, buffer);
		safe_unpackstr(&step->tres_bind, buffer);
		safe_unpackstr(&step->tres_freq, buffer);
		safe_unpackstr(&step->tres_per_step, buffer);
		safe_unpackstr(&step->tres_per_node, buffer);
		safe_unpackstr(&step->tres_per_socket, buffer);
		safe_unpackstr(&step->tres_per_task, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&step->array_job_id, buffer);
		safe_unpack32(&step->array_task_id, buffer);
		safe_unpack_step_id_members(&step->step_id, buffer,
					    protocol_version);
		safe_unpack32(&step->user_id, buffer);
		safe_unpack32(&step->num_cpus, buffer);
		safe_unpack32(&step->cpu_freq_min, buffer);
		safe_unpack32(&step->cpu_freq_max, buffer);
		safe_unpack32(&step->cpu_freq_gov, buffer);
		safe_unpack32(&step->num_tasks, buffer);
		safe_unpack32(&step->task_dist, buffer);
		safe_unpack32(&step->time_limit, buffer);
		safe_unpack32(&step->state, buffer);
		safe_unpack32(&step->srun_pid, buffer);

		safe_unpack_time(&step->start_time, buffer);
		safe_unpack_time(&step->run_time, buffer);

		safe_unpackstr(&step->cluster, buffer);
		safe_unpackstr(&step->container, buffer);
		safe_unpackstr(&step->container_id, buffer);
		safe_unpackstr(&step->partition, buffer);
		safe_unpackstr(&step->srun_host, buffer);
		safe_unpackstr(&step->resv_ports, buffer);
		safe_unpackstr(&step->nodes, buffer);
		safe_unpackstr(&step->name, buffer);
		safe_unpackstr(&step->network, buffer);
		unpack_bit_str_hex_as_inx(&step->node_inx, buffer);

		safe_unpackstr(&step->tres_fmt_alloc_str, buffer);
		safe_unpack16(&step->start_protocol_ver, buffer);

		safe_unpackstr(&step->cpus_per_tres, buffer);
		safe_unpackstr(&step->mem_per_tres, buffer);
		safe_unpackstr(&step->submit_line, buffer);
		safe_unpackstr(&step->tres_bind, buffer);
		safe_unpackstr(&step->tres_freq, buffer);
		safe_unpackstr(&step->tres_per_step, buffer);
		safe_unpackstr(&step->tres_per_node, buffer);
		safe_unpackstr(&step->tres_per_socket, buffer);
		safe_unpackstr(&step->tres_per_task, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	/* no need to free here.  (we will just be freeing it 2 times
	   since this is freed in _unpack_job_step_info_response_msg
	*/
	//slurm_free_job_step_info_members(step);
	return SLURM_ERROR;
}

static int _unpack_job_step_info_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_info_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_step_count, buffer);
		safe_unpack_time(&msg->last_update, buffer);

		safe_xcalloc(msg->job_steps, msg->job_step_count,
			     sizeof(job_step_info_t));

		for (int i = 0; i < msg->job_step_count; i++)
			if (_unpack_job_step_info_members(
				    &msg->job_steps[i], buffer,
				    smsg->protocol_version))
				goto unpack_error;

		if (slurm_unpack_list(&msg->stepmgr_jobs,
				      slurm_unpack_stepmgr_job_info, xfree_ptr,
				      buffer, smsg->protocol_version))
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_info_response_msg(msg);
	return SLURM_ERROR;
}

extern void slurm_pack_stepmgr_job_info(void *in, uint16_t protocol_version,
					 buf_t *buffer)
{
	stepmgr_job_info_t *object = in;

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&object->step_id, buffer, protocol_version);
		packstr(object->stepmgr, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(object->step_id.job_id, buffer);
		packstr(object->stepmgr, buffer);
	}
}

extern int slurm_unpack_stepmgr_job_info(void **out,
					  uint16_t protocol_version,
					  buf_t *buffer)
{
	stepmgr_job_info_t *object = xmalloc(sizeof(*object));
	*out = object;

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&object->step_id, buffer,
					    protocol_version);
		safe_unpackstr(&object->stepmgr, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		object->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&object->step_id.job_id, buffer);
		safe_unpackstr(&object->stepmgr, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:

	slurm_free_stepmgr_job_info(object);
	return SLURM_ERROR;
}

static void _pack_buf_msg(const slurm_msg_t *msg, buf_t *buffer)
{
	buf_t *msg_buffer = msg->data;
	packmem_array(msg_buffer->head, msg_buffer->processed, buffer);
}

static void _pack_job_script_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	buf_t *msg = smsg->data;

	packstr(msg->head, buffer);
}

static int _unpack_job_script_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	char *msg = NULL;

	safe_unpackstr(&msg, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg);
	return SLURM_ERROR;
}

static int _unpack_job_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_info_t *job = NULL;
	job_info_msg_t *msg = xmalloc(sizeof(*msg));

	/* load buffer's header (data structure version and time) */
	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->record_count, buffer);
		safe_unpack_time(&msg->last_update, buffer);
		safe_unpack_time(&msg->last_backfill, buffer);
	}

	if (msg->record_count) {
		safe_xcalloc(msg->job_array, msg->record_count,
			     sizeof(job_info_t));
		job = msg->job_array;
	}
	/* load individual job info */
	for (int i = 0; i < msg->record_count; i++) {
		job_info_t *job_ptr = &job[i];
		if (_unpack_job_info_members(job_ptr, buffer,
					     smsg->protocol_version))
			goto unpack_error;
		if ((job_ptr->bitflags & BACKFILL_SCHED) &&
		    msg->last_backfill && IS_JOB_PENDING(job_ptr) &&
		    (msg->last_backfill <= job_ptr->last_sched_eval))
			job_ptr->bitflags |= BACKFILL_LAST;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_info_msg(msg);
	return SLURM_ERROR;
}

/* _unpack_job_info_members
 * unpacks a set of slurm job info for one job
 * OUT job - pointer to the job info buffer
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 */
static int
_unpack_job_info_members(job_info_t * job, buf_t *buffer,
			 uint16_t protocol_version)
{
	multi_core_data_t *mc_ptr;
	uint32_t uint32_tmp;
	bool need_unpack = false;

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		/* job_record_pack_common */
		safe_unpack_step_id_members(&job->step_id, buffer,
					    protocol_version);
		safe_unpackstr(&job->account, buffer);
		safe_unpackstr(&job->admin_comment, buffer);
		safe_unpackstr(&job->alloc_node, buffer);
		safe_unpack32(&job->alloc_sid, buffer);
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack32(&job->array_task_id, buffer);
		safe_unpack32(&job->assoc_id, buffer);

		safe_unpackstr(&job->batch_features, buffer);
		safe_unpack16(&job->batch_flag, buffer);
		safe_unpackstr(&job->batch_host, buffer);
		safe_unpack64(&job->bitflags, buffer);
		safe_unpackstr(&job->burst_buffer, buffer);
		safe_unpackstr(&job->burst_buffer_state, buffer);
		safe_unpackdouble(&job->billable_tres, buffer);

		safe_unpackstr(&job->comment, buffer);
		safe_unpackstr(&job->container, buffer);
		safe_unpackstr(&job->container_id, buffer);
		safe_unpackstr(&job->cpus_per_tres, buffer);

		safe_unpack_time(&job->deadline, buffer);
		safe_unpack32(&job->delay_boot, buffer);
		safe_unpack32(&job->derived_ec, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpackstr(&job->extra, buffer);

		safe_unpackstr(&job->failed_node, buffer);
		/* job_record_pack_fed_details */
		safe_unpackbool(&need_unpack, buffer);
		if (need_unpack) {
			safe_unpackstr(&job->fed_origin_str, buffer);
			safe_unpack64(&job->fed_siblings_active, buffer);
			safe_unpackstr(&job->fed_siblings_active_str, buffer);
			safe_unpack64(&job->fed_siblings_viable, buffer);
			safe_unpackstr(&job->fed_siblings_viable_str, buffer);
		}
		/*******************************/

		safe_unpackstr(&job->gres_total, buffer);
		safe_unpack32(&job->group_id, buffer);

		safe_unpack32(&job->het_job_id, buffer);
		safe_unpackstr(&job->het_job_id_set, buffer);
		safe_unpack32(&job->het_job_offset, buffer);

		safe_unpack32(&job->job_state, buffer);

		safe_unpack_time(&job->last_sched_eval, buffer);
		safe_unpackstr(&job->licenses, buffer);
		safe_unpackstr(&job->licenses_allocated, buffer);

		safe_unpack16(&job->mail_type, buffer);
		safe_unpackstr(&job->mail_user, buffer);
		safe_unpackstr(&job->mcs_label, buffer);
		safe_unpackstr(&job->mem_per_tres, buffer);

		safe_unpackstr(&job->name, buffer);
		safe_unpackstr(&job->network, buffer);

		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpack32(&job->profile, buffer);

		safe_unpack8(&job->reboot, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack16(&job->restart_cnt, buffer);
		safe_unpackstr(&job->resv_name, buffer);
		safe_unpackstr(&job->resv_ports, buffer);

		safe_unpackstr(&job->selinux_context, buffer);
		safe_unpack32(&job->site_factor, buffer);
		safe_unpack16(&job->start_protocol_ver, buffer);
		safe_unpackstr(&job->state_desc, buffer);
		safe_unpack32(&job->state_reason, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpackstr(&job->system_comment, buffer);

		safe_unpack32(&job->time_min, buffer);
		safe_unpackstr(&job->tres_bind, buffer);
		safe_unpackstr(&job->tres_alloc_str, buffer);
		safe_unpackstr(&job->tres_req_str, buffer);
		safe_unpackstr(&job->tres_freq, buffer);
		safe_unpackstr(&job->tres_per_job, buffer);
		safe_unpackstr(&job->tres_per_node, buffer);
		safe_unpackstr(&job->tres_per_socket, buffer);
		safe_unpackstr(&job->tres_per_task, buffer);

		safe_unpack32(&job->user_id, buffer);
		safe_unpackstr(&job->user_name, buffer);

		safe_unpack32(&job->wait4switch, buffer);
		safe_unpackstr(&job->wckey, buffer);
		/**************************************/


		/* The array_task_str value is stored in slurmctld and passed
		 * here in hex format for best scalability. Its format needs
		 * to be converted to human readable form by the client. */
		safe_unpackstr(&job->array_task_str, buffer);
		safe_unpack32(&job->array_max_tasks, buffer);
		xlate_array_task_str(&job->array_task_str, job->array_max_tasks,
				     &job->array_bitmap);

		safe_unpack32(&job->time_limit, buffer);

		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack32_array(&job->priority_array, &uint32_tmp, buffer);
		safe_unpackstr(&job->priority_array_names, buffer);
		safe_unpackstr(&job->cluster, buffer);
		safe_unpackstr(&job->nodes, buffer);
		safe_unpackstr(&job->sched_nodes, buffer);
		safe_unpackstr(&job->partition, buffer);
		safe_unpackstr(&job->qos, buffer);
		safe_unpack_time(&job->preemptable_time, buffer);

		if (unpack_job_resources(&job->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&job->gres_detail_str,
				     &job->gres_detail_cnt, buffer);

		unpack_bit_str_hex_as_inx(&job->node_inx, buffer);

		/*** unpack default job details ***/
		safe_unpackbool(&need_unpack, buffer);
		if (!need_unpack) {
			safe_unpack32(&job->num_cpus, buffer);
			safe_unpack32(&job->num_nodes, buffer);
			safe_unpack32(&job->nice, buffer);
		} else {
			/* job_record_pack_details_common */
			safe_unpack_time(&job->accrue_time, buffer);
			safe_unpack_time(&job->eligible_time, buffer);
			safe_unpackstr(&job->cluster_features, buffer);
			safe_unpack32(&job->cpu_freq_gov, buffer);
			safe_unpack32(&job->cpu_freq_max, buffer);
			safe_unpack32(&job->cpu_freq_min, buffer);
			safe_unpackstr(&job->dependency, buffer);
			unpack_bit_str_hex_as_fmt_str(&job->job_size_str,
						      buffer);
			safe_unpack32(&job->nice, buffer);
			safe_unpack16(&job->ntasks_per_node, buffer);
			safe_unpack16(&job->ntasks_per_tres, buffer);
			safe_unpack16(&job->requeue, buffer);
			safe_unpack16(&job->segment_size, buffer);
			safe_unpack_time(&job->submit_time, buffer);
			safe_unpackstr(&job->work_dir, buffer);
			/**********************************/

			safe_unpackstr(&job->features, buffer);
			safe_unpackstr(&job->prefer, buffer);
			safe_unpackstr(&job->command, buffer);
			safe_unpackstr(&job->submit_line, buffer);

			safe_unpack32(&job->num_cpus, buffer);
			safe_unpack32(&job->max_cpus, buffer);
			safe_unpack32(&job->num_nodes, buffer);
			safe_unpack32(&job->max_nodes, buffer);
			safe_unpack32(&job->num_tasks, buffer);

			safe_unpack16(&job->shared, buffer);

			safe_unpackstr(&job->cronspec, buffer);
		}

		/*** unpack pending job details ***/
		safe_unpack16(&job->contiguous, buffer);
		safe_unpack16(&job->core_spec, buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack64(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);
		safe_unpack16(&job->oom_kill_step, buffer);
		safe_unpackstr(&job->req_nodes, buffer);

		unpack_bit_str_hex_as_inx(&job->req_node_inx, buffer);

		safe_unpackstr(&job->exc_nodes, buffer);

		unpack_bit_str_hex_as_inx(&job->exc_node_inx, buffer);

		safe_unpackstr(&job->std_err, buffer);
		safe_unpackstr(&job->std_in, buffer);
		safe_unpackstr(&job->std_out, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		if (mc_ptr) {
			job->boards_per_node = mc_ptr->boards_per_node;
			job->sockets_per_board = mc_ptr->sockets_per_board;
			job->sockets_per_node = mc_ptr->sockets_per_node;
			job->cores_per_socket = mc_ptr->cores_per_socket;
			job->threads_per_core = mc_ptr->threads_per_core;
			job->ntasks_per_board = mc_ptr->ntasks_per_board;
			job->ntasks_per_socket = mc_ptr->ntasks_per_socket;
			job->ntasks_per_core = mc_ptr->ntasks_per_core;
			xfree(mc_ptr);
		}
	} else if (protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		job->step_id = SLURM_STEP_ID_INITIALIZER;
		/* job_record_pack_common */
		safe_unpackstr(&job->account, buffer);
		safe_unpackstr(&job->admin_comment, buffer);
		safe_unpackstr(&job->alloc_node, buffer);
		safe_unpack32(&job->alloc_sid, buffer);
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack32(&job->array_task_id, buffer);
		safe_unpack32(&job->assoc_id, buffer);

		safe_unpackstr(&job->batch_features, buffer);
		safe_unpack16(&job->batch_flag, buffer);
		safe_unpackstr(&job->batch_host, buffer);
		safe_unpack64(&job->bitflags, buffer);
		safe_unpackstr(&job->burst_buffer, buffer);
		safe_unpackstr(&job->burst_buffer_state, buffer);
		safe_unpackdouble(&job->billable_tres, buffer);

		safe_unpackstr(&job->comment, buffer);
		safe_unpackstr(&job->container, buffer);
		safe_unpackstr(&job->container_id, buffer);
		safe_unpackstr(&job->cpus_per_tres, buffer);

		safe_unpack_time(&job->deadline, buffer);
		safe_unpack32(&job->delay_boot, buffer);
		safe_unpack32(&job->derived_ec, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpackstr(&job->extra, buffer);

		safe_unpackstr(&job->failed_node, buffer);
		/* job_record_pack_fed_details */
		safe_unpackbool(&need_unpack, buffer);
		if (need_unpack) {
			safe_unpackstr(&job->fed_origin_str, buffer);
			safe_unpack64(&job->fed_siblings_active, buffer);
			safe_unpackstr(&job->fed_siblings_active_str, buffer);
			safe_unpack64(&job->fed_siblings_viable, buffer);
			safe_unpackstr(&job->fed_siblings_viable_str, buffer);
		}
		/*******************************/

		safe_unpackstr(&job->gres_total, buffer);
		safe_unpack32(&job->group_id, buffer);

		safe_unpack32(&job->het_job_id, buffer);
		safe_unpackstr(&job->het_job_id_set, buffer);
		safe_unpack32(&job->het_job_offset, buffer);

		safe_unpack32(&job->step_id.job_id, buffer);
		safe_unpack32(&job->job_state, buffer);

		safe_unpack_time(&job->last_sched_eval, buffer);
		safe_unpackstr(&job->licenses, buffer);
		safe_unpackstr(&job->licenses_allocated, buffer);

		safe_unpack16(&job->mail_type, buffer);
		safe_unpackstr(&job->mail_user, buffer);
		safe_unpackstr(&job->mcs_label, buffer);
		safe_unpackstr(&job->mem_per_tres, buffer);

		safe_unpackstr(&job->name, buffer);
		safe_unpackstr(&job->network, buffer);

		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpack32(&job->profile, buffer);

		safe_unpack8(&job->reboot, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack16(&job->restart_cnt, buffer);
		safe_unpackstr(&job->resv_name, buffer);
		safe_unpackstr(&job->resv_ports, buffer);

		safe_unpackstr(&job->selinux_context, buffer);
		safe_unpack32(&job->site_factor, buffer);
		safe_unpack16(&job->start_protocol_ver, buffer);
		safe_unpackstr(&job->state_desc, buffer);
		safe_unpack32(&job->state_reason, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpackstr(&job->system_comment, buffer);

		safe_unpack32(&job->time_min, buffer);
		safe_unpackstr(&job->tres_bind, buffer);
		safe_unpackstr(&job->tres_alloc_str, buffer);
		safe_unpackstr(&job->tres_req_str, buffer);
		safe_unpackstr(&job->tres_freq, buffer);
		safe_unpackstr(&job->tres_per_job, buffer);
		safe_unpackstr(&job->tres_per_node, buffer);
		safe_unpackstr(&job->tres_per_socket, buffer);
		safe_unpackstr(&job->tres_per_task, buffer);

		safe_unpack32(&job->user_id, buffer);
		safe_unpackstr(&job->user_name, buffer);

		safe_unpack32(&job->wait4switch, buffer);
		safe_unpackstr(&job->wckey, buffer);
		/**************************************/


		/* The array_task_str value is stored in slurmctld and passed
		 * here in hex format for best scalability. Its format needs
		 * to be converted to human readable form by the client. */
		safe_unpackstr(&job->array_task_str, buffer);
		safe_unpack32(&job->array_max_tasks, buffer);
		xlate_array_task_str(&job->array_task_str, job->array_max_tasks,
				     &job->array_bitmap);

		safe_unpack32(&job->time_limit, buffer);

		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack32_array(&job->priority_array, &uint32_tmp, buffer);
		safe_unpackstr(&job->priority_array_names, buffer);
		safe_unpackstr(&job->cluster, buffer);
		safe_unpackstr(&job->nodes, buffer);
		safe_unpackstr(&job->sched_nodes, buffer);
		safe_unpackstr(&job->partition, buffer);
		safe_unpackstr(&job->qos, buffer);
		safe_unpack_time(&job->preemptable_time, buffer);

		if (unpack_job_resources(&job->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&job->gres_detail_str,
				     &job->gres_detail_cnt, buffer);

		unpack_bit_str_hex_as_inx(&job->node_inx, buffer);

		/*** unpack default job details ***/
		safe_unpackbool(&need_unpack, buffer);
		if (!need_unpack) {
			safe_unpack32(&job->num_cpus, buffer);
			safe_unpack32(&job->num_nodes, buffer);
			safe_unpack32(&job->nice, buffer);
		} else {
			/* job_record_pack_details_common */
			safe_unpack_time(&job->accrue_time, buffer);
			safe_unpack_time(&job->eligible_time, buffer);
			safe_unpackstr(&job->cluster_features, buffer);
			safe_unpack32(&job->cpu_freq_gov, buffer);
			safe_unpack32(&job->cpu_freq_max, buffer);
			safe_unpack32(&job->cpu_freq_min, buffer);
			safe_unpackstr(&job->dependency, buffer);
			unpack_bit_str_hex_as_fmt_str(&job->job_size_str,
						      buffer);
			safe_unpack32(&job->nice, buffer);
			safe_unpack16(&job->ntasks_per_node, buffer);
			safe_unpack16(&job->ntasks_per_tres, buffer);
			safe_unpack16(&job->requeue, buffer);
			safe_unpack16(&job->segment_size, buffer);
			safe_unpack_time(&job->submit_time, buffer);
			safe_unpackstr(&job->work_dir, buffer);
			/**********************************/

			safe_unpackstr(&job->features, buffer);
			safe_unpackstr(&job->prefer, buffer);
			safe_unpackstr(&job->command, buffer);

			safe_unpack32(&job->num_cpus, buffer);
			safe_unpack32(&job->max_cpus, buffer);
			safe_unpack32(&job->num_nodes, buffer);
			safe_unpack32(&job->max_nodes, buffer);
			safe_unpack32(&job->num_tasks, buffer);

			safe_unpack16(&job->shared, buffer);

			safe_unpackstr(&job->cronspec, buffer);
		}

		/*** unpack pending job details ***/
		safe_unpack16(&job->contiguous, buffer);
		safe_unpack16(&job->core_spec, buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack64(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);
		safe_unpack16(&job->oom_kill_step, buffer);
		safe_unpackstr(&job->req_nodes, buffer);

		unpack_bit_str_hex_as_inx(&job->req_node_inx, buffer);

		safe_unpackstr(&job->exc_nodes, buffer);

		unpack_bit_str_hex_as_inx(&job->exc_node_inx, buffer);

		safe_unpackstr(&job->std_err, buffer);
		safe_unpackstr(&job->std_in, buffer);
		safe_unpackstr(&job->std_out, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		if (mc_ptr) {
			job->boards_per_node = mc_ptr->boards_per_node;
			job->sockets_per_board = mc_ptr->sockets_per_board;
			job->sockets_per_node = mc_ptr->sockets_per_node;
			job->cores_per_socket = mc_ptr->cores_per_socket;
			job->threads_per_core = mc_ptr->threads_per_core;
			job->ntasks_per_board = mc_ptr->ntasks_per_board;
			job->ntasks_per_socket = mc_ptr->ntasks_per_socket;
			job->ntasks_per_core = mc_ptr->ntasks_per_core;
			xfree(mc_ptr);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		job->step_id = SLURM_STEP_ID_INITIALIZER;
		/* job_record_pack_common */
		safe_unpackstr(&job->account, buffer);
		safe_unpackstr(&job->admin_comment, buffer);
		safe_unpackstr(&job->alloc_node, buffer);
		safe_unpack32(&job->alloc_sid, buffer);
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack32(&job->array_task_id, buffer);
		safe_unpack32(&job->assoc_id, buffer);

		safe_unpackstr(&job->batch_features, buffer);
		safe_unpack16(&job->batch_flag, buffer);
		safe_unpackstr(&job->batch_host, buffer);
		safe_unpack64(&job->bitflags, buffer);
		safe_unpackstr(&job->burst_buffer, buffer);
		safe_unpackstr(&job->burst_buffer_state, buffer);
		safe_unpackdouble(&job->billable_tres, buffer);

		safe_unpackstr(&job->comment, buffer);
		safe_unpackstr(&job->container, buffer);
		safe_unpackstr(&job->container_id, buffer);
		safe_unpackstr(&job->cpus_per_tres, buffer);

		safe_unpack_time(&job->deadline, buffer);
		safe_unpack32(&job->delay_boot, buffer);
		safe_unpack32(&job->derived_ec, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpackstr(&job->extra, buffer);

		safe_unpackstr(&job->failed_node, buffer);
		/* job_record_pack_fed_details */
		safe_unpackbool(&need_unpack, buffer);
		if (need_unpack) {
			safe_unpackstr(&job->fed_origin_str, buffer);
			safe_unpack64(&job->fed_siblings_active, buffer);
			safe_unpackstr(&job->fed_siblings_active_str, buffer);
			safe_unpack64(&job->fed_siblings_viable, buffer);
			safe_unpackstr(&job->fed_siblings_viable_str, buffer);
		}
		/*******************************/

		safe_unpackstr(&job->gres_total, buffer);
		safe_unpack32(&job->group_id, buffer);

		safe_unpack32(&job->het_job_id, buffer);
		safe_unpackstr(&job->het_job_id_set, buffer);
		safe_unpack32(&job->het_job_offset, buffer);

		safe_unpack32(&job->step_id.job_id, buffer);
		safe_unpack32(&job->job_state, buffer);

		safe_unpack_time(&job->last_sched_eval, buffer);
		safe_unpackstr(&job->licenses, buffer);

		safe_unpack16(&job->mail_type, buffer);
		safe_unpackstr(&job->mail_user, buffer);
		safe_unpackstr(&job->mcs_label, buffer);
		safe_unpackstr(&job->mem_per_tres, buffer);

		safe_unpackstr(&job->name, buffer);
		safe_unpackstr(&job->network, buffer);

		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpack32(&job->profile, buffer);

		safe_unpack8(&job->reboot, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack16(&job->restart_cnt, buffer);
		safe_unpackstr(&job->resv_name, buffer);
		safe_unpackstr(&job->resv_ports, buffer);

		safe_unpackstr(&job->selinux_context, buffer);
		safe_unpack32(&job->site_factor, buffer);
		safe_unpack16(&job->start_protocol_ver, buffer);
		safe_unpackstr(&job->state_desc, buffer);
		safe_unpack32(&job->state_reason, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpackstr(&job->system_comment, buffer);

		safe_unpack32(&job->time_min, buffer);
		safe_unpackstr(&job->tres_bind, buffer);
		safe_unpackstr(&job->tres_alloc_str, buffer);
		safe_unpackstr(&job->tres_req_str, buffer);
		safe_unpackstr(&job->tres_freq, buffer);
		safe_unpackstr(&job->tres_per_job, buffer);
		safe_unpackstr(&job->tres_per_node, buffer);
		safe_unpackstr(&job->tres_per_socket, buffer);
		safe_unpackstr(&job->tres_per_task, buffer);

		safe_unpack32(&job->user_id, buffer);
		safe_unpackstr(&job->user_name, buffer);

		safe_unpack32(&job->wait4switch, buffer);
		safe_unpackstr(&job->wckey, buffer);
		/**************************************/


		/* The array_task_str value is stored in slurmctld and passed
		 * here in hex format for best scalability. Its format needs
		 * to be converted to human readable form by the client. */
		safe_unpackstr(&job->array_task_str, buffer);
		safe_unpack32(&job->array_max_tasks, buffer);
		xlate_array_task_str(&job->array_task_str, job->array_max_tasks,
				     &job->array_bitmap);

		safe_unpack32(&job->time_limit, buffer);

		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack32_array(&job->priority_array, &uint32_tmp, buffer);
		safe_unpackstr(&job->priority_array_names, buffer);
		safe_unpackstr(&job->cluster, buffer);
		safe_unpackstr(&job->nodes, buffer);
		safe_unpackstr(&job->sched_nodes, buffer);
		safe_unpackstr(&job->partition, buffer);
		safe_unpackstr(&job->qos, buffer);
		safe_unpack_time(&job->preemptable_time, buffer);

		if (unpack_job_resources(&job->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&job->gres_detail_str,
				     &job->gres_detail_cnt, buffer);

		unpack_bit_str_hex_as_inx(&job->node_inx, buffer);

		/*** unpack default job details ***/
		safe_unpackbool(&need_unpack, buffer);
		if (!need_unpack) {
			safe_unpack32(&job->num_cpus, buffer);
			safe_unpack32(&job->num_nodes, buffer);
			safe_unpack32(&job->nice, buffer);
		} else {
			/* job_record_pack_details_common */
			safe_unpack_time(&job->accrue_time, buffer);
			safe_unpack_time(&job->eligible_time, buffer);
			safe_unpackstr(&job->cluster_features, buffer);
			safe_unpack32(&job->cpu_freq_gov, buffer);
			safe_unpack32(&job->cpu_freq_max, buffer);
			safe_unpack32(&job->cpu_freq_min, buffer);
			safe_unpackstr(&job->dependency, buffer);
			unpack_bit_str_hex_as_fmt_str(&job->job_size_str,
						      buffer);
			safe_unpack32(&job->nice, buffer);
			safe_unpack16(&job->ntasks_per_node, buffer);
			safe_unpack16(&job->ntasks_per_tres, buffer);
			safe_unpack16(&job->requeue, buffer);
			safe_unpack_time(&job->submit_time, buffer);
			safe_unpackstr(&job->work_dir, buffer);
			/**********************************/

			safe_unpackstr(&job->features, buffer);
			safe_unpackstr(&job->prefer, buffer);
			safe_unpackstr(&job->command, buffer);

			safe_unpack32(&job->num_cpus, buffer);
			safe_unpack32(&job->max_cpus, buffer);
			safe_unpack32(&job->num_nodes, buffer);
			safe_unpack32(&job->max_nodes, buffer);
			safe_unpack32(&job->num_tasks, buffer);

			safe_unpack16(&job->shared, buffer);

			safe_unpackstr(&job->cronspec, buffer);
		}

		/*** unpack pending job details ***/
		safe_unpack16(&job->contiguous, buffer);
		safe_unpack16(&job->core_spec, buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack64(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);
		safe_unpack16(&job->oom_kill_step, buffer);
		safe_unpackstr(&job->req_nodes, buffer);

		unpack_bit_str_hex_as_inx(&job->req_node_inx, buffer);

		safe_unpackstr(&job->exc_nodes, buffer);

		unpack_bit_str_hex_as_inx(&job->exc_node_inx, buffer);

		safe_unpackstr(&job->std_err, buffer);
		safe_unpackstr(&job->std_in, buffer);
		safe_unpackstr(&job->std_out, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		if (mc_ptr) {
			job->boards_per_node = mc_ptr->boards_per_node;
			job->sockets_per_board = mc_ptr->sockets_per_board;
			job->sockets_per_node = mc_ptr->sockets_per_node;
			job->cores_per_socket = mc_ptr->cores_per_socket;
			job->threads_per_core = mc_ptr->threads_per_core;
			job->ntasks_per_board = mc_ptr->ntasks_per_board;
			job->ntasks_per_socket = mc_ptr->ntasks_per_socket;
			job->ntasks_per_core = mc_ptr->ntasks_per_core;
			xfree(mc_ptr);
		}
	}

	_set_min_memory_tres(job->mem_per_tres, &job->pn_min_memory);

	/* set automatically for external applications */
	job->job_id = job->step_id.job_id;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_info_members(job);
	return SLURM_ERROR;
}

static void _pack_slurm_ctl_conf_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	slurm_ctl_conf_info_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);

		pack16(msg->accounting_storage_enforce, buffer);
		packstr(msg->accounting_storage_backup_host, buffer);
		packstr(msg->accounting_storage_host, buffer);
		packstr(msg->accounting_storage_ext_host, buffer);
		packstr(msg->accounting_storage_params, buffer);
		pack16(msg->accounting_storage_port, buffer);
		packstr(msg->accounting_storage_tres, buffer);
		packstr(msg->accounting_storage_type, buffer);

		pack_key_pair_list(msg->acct_gather_conf,
				   smsg->protocol_version, buffer);

		packstr(msg->acct_gather_energy_type, buffer);
		packstr(msg->acct_gather_filesystem_type, buffer);
		packstr(msg->acct_gather_interconnect_type, buffer);
		pack16(msg->acct_gather_node_freq, buffer);
		packstr(msg->acct_gather_profile_type, buffer);

		packstr(msg->authalttypes, buffer);
		packstr(msg->authalt_params, buffer);
		packstr(msg->authinfo, buffer);
		packstr(msg->authtype, buffer);

		pack16(msg->batch_start_timeout, buffer);
		pack_time(msg->boot_time, buffer);
		packstr(msg->bb_type, buffer);
		packstr(msg->bcast_exclude, buffer);
		packstr(msg->bcast_parameters, buffer);
		packstr(msg->certmgr_params, buffer);
		packstr(msg->certmgr_type, buffer);

		pack_key_pair_list(msg->cgroup_conf, smsg->protocol_version,
				   buffer);
		packstr(msg->cli_filter_params, buffer);
		packstr(msg->cli_filter_plugins, buffer);
		packstr(msg->cluster_name, buffer);
		packstr(msg->comm_params, buffer);
		pack16(msg->complete_wait, buffer);
		pack32(msg->conf_flags, buffer);
		packstr_array(msg->control_addr, msg->control_cnt, buffer);
		packstr_array(msg->control_machine, msg->control_cnt, buffer);
		pack32(msg->cpu_freq_def, buffer);
		pack32(msg->cpu_freq_govs, buffer);
		packstr(msg->cred_type, buffer);
		packstr(msg->data_parser_parameters, buffer);

		pack64(msg->def_mem_per_cpu, buffer);
		pack64(msg->debug_flags, buffer);
		packstr(msg->dependency_params, buffer);

		pack16(msg->eio_timeout, buffer);
		pack16(msg->enforce_part_limits, buffer);
		packstr_array(msg->epilog, msg->epilog_cnt, buffer);
		pack32(msg->epilog_msg_time, buffer);
		packstr_array(msg->epilog_slurmctld, msg->epilog_slurmctld_cnt,
			      buffer);
		pack16(msg->epilog_timeout, buffer);

		packstr(msg->fed_params, buffer);
		pack32(msg->first_job_id, buffer);
		pack16(msg->fs_dampening_factor, buffer);

		packstr(msg->gres_plugins, buffer);
		pack16(msg->group_time, buffer);
		pack16(msg->group_force, buffer);
		packstr(msg->gpu_freq_def, buffer);

		packstr(msg->hash_plugin, buffer);
		pack32(msg->hash_val, buffer);

		pack16(msg->health_check_interval, buffer);
		pack16(msg->health_check_node_state, buffer);
		packstr(msg->health_check_program, buffer);

		packstr(msg->http_parser_type, buffer);

		pack16(msg->inactive_limit, buffer);
		packstr(msg->interactive_step_opts, buffer);

		packstr(msg->job_acct_gather_freq, buffer);
		packstr(msg->job_acct_gather_type, buffer);
		packstr(msg->job_acct_gather_params, buffer);

		packstr(msg->job_comp_host, buffer);
		packstr(msg->job_comp_loc, buffer);
		packstr(msg->job_comp_params, buffer);
		pack32((uint32_t) msg->job_comp_port, buffer);
		packstr(msg->job_comp_type, buffer);
		packstr(msg->job_comp_user, buffer);
		packstr(msg->namespace_plugin, buffer);

		(void) slurm_pack_list(msg->job_defaults_list,
				       job_defaults_pack, buffer,
				       smsg->protocol_version);
		pack16(msg->job_file_append, buffer);
		pack16(msg->job_requeue, buffer);
		packstr(msg->job_submit_plugins, buffer);

		pack16(msg->kill_on_bad_exit, buffer);
		pack16(msg->kill_wait, buffer);

		packstr(msg->launch_params, buffer);
		packstr(msg->licenses, buffer);
		pack16(msg->log_fmt, buffer);

		pack32(msg->max_array_sz, buffer);
		pack32(msg->max_batch_requeue, buffer);
		pack32(msg->max_dbd_msgs, buffer);
		packstr(msg->mail_domain, buffer);
		packstr(msg->mail_prog, buffer);
		pack32(msg->max_job_cnt, buffer);
		pack32(msg->max_job_id, buffer);
		pack64(msg->max_mem_per_cpu, buffer);
		pack32(msg->max_node_cnt, buffer);
		pack32(msg->max_step_cnt, buffer);
		pack16(msg->max_tasks_per_node, buffer);

		packstr(msg->mcs_plugin, buffer);
		packstr(msg->mcs_plugin_params, buffer);

		packstr(msg->metrics_type, buffer);

		pack32(msg->min_job_age, buffer);
		pack_key_pair_list(msg->mpi_conf, smsg->protocol_version,
				   buffer);
		packstr(msg->mpi_default, buffer);
		packstr(msg->mpi_params, buffer);
		pack16(msg->msg_timeout, buffer);

		pack32(msg->next_job_id, buffer);

		pack_config_plugin_params_list(msg->node_features_conf,
					       smsg->protocol_version, buffer);

		packstr(msg->node_features_plugins, buffer);

		pack16(msg->over_time_limit, buffer);

		packstr(msg->plugindir, buffer);
		packstr(msg->plugstack, buffer);
		pack16(msg->preempt_mode, buffer);
		packstr(msg->preempt_params, buffer);
		packstr(msg->preempt_type, buffer);
		pack32(msg->preempt_exempt_time, buffer);
		packstr(msg->prep_params, buffer);
		packstr(msg->prep_plugins, buffer);

		pack32(msg->priority_decay_hl, buffer);
		pack32(msg->priority_calc_period, buffer);
		pack16(msg->priority_favor_small, buffer);
		pack16(msg->priority_flags, buffer);
		pack32(msg->priority_max_age, buffer);
		packstr(msg->priority_params, buffer);
		pack16(msg->priority_reset_period, buffer);
		packstr(msg->priority_type, buffer);
		pack32(msg->priority_weight_age, buffer);
		pack32(msg->priority_weight_assoc, buffer);
		pack32(msg->priority_weight_fs, buffer);
		pack32(msg->priority_weight_js, buffer);
		pack32(msg->priority_weight_part, buffer);
		pack32(msg->priority_weight_qos, buffer);
		packstr(msg->priority_weight_tres, buffer);

		pack16(msg->private_data, buffer);
		packstr(msg->proctrack_type, buffer);
		packstr_array(msg->prolog, msg->prolog_cnt, buffer);
		packstr_array(msg->prolog_slurmctld, msg->prolog_slurmctld_cnt,
			      buffer);
		pack16(msg->prolog_timeout, buffer);
		pack16(msg->prolog_flags, buffer);
		pack16(msg->propagate_prio_process, buffer);
		packstr(msg->propagate_rlimits, buffer);
		packstr(msg->propagate_rlimits_except, buffer);

		packstr(msg->reboot_program, buffer);
		pack16(msg->reconfig_flags, buffer);
		packstr(msg->requeue_exit, buffer);
		packstr(msg->requeue_exit_hold, buffer);
		packstr(msg->resume_fail_program, buffer);
		packstr(msg->resume_program, buffer);
		pack16(msg->resume_rate, buffer);
		pack16(msg->resume_timeout, buffer);
		packstr(msg->resv_epilog, buffer);
		pack16(msg->resv_over_run, buffer);
		packstr(msg->resv_prolog, buffer);
		pack16(msg->ret2service, buffer);

		packstr(msg->sched_params, buffer);
		packstr(msg->sched_logfile, buffer);
		pack16(msg->sched_log_level, buffer);
		pack16(msg->sched_time_slice, buffer);
		packstr(msg->schedtype, buffer);
		packstr(msg->scron_params, buffer);
		packstr(msg->select_type, buffer);

		pack_key_pair_list(msg->select_conf_key_pairs,
				   smsg->protocol_version, buffer);

		pack16(msg->select_type_param, buffer);

		packstr(msg->slurm_conf, buffer);
		pack32(msg->slurm_user_id, buffer);
		packstr(msg->slurm_user_name, buffer);
		pack32(msg->slurmd_user_id, buffer);
		packstr(msg->slurmd_user_name, buffer);

		packstr(msg->slurmctld_addr, buffer);
		pack16(msg->slurmctld_debug, buffer);
		packstr(msg->slurmctld_logfile, buffer);
		packstr(msg->slurmctld_params, buffer);
		packstr(msg->slurmctld_pidfile, buffer);
		pack32(msg->slurmctld_port, buffer);
		pack16(msg->slurmctld_port_count, buffer);
		packstr(msg->slurmctld_primary_off_prog, buffer);
		packstr(msg->slurmctld_primary_on_prog, buffer);
		pack16(msg->slurmctld_syslog_debug, buffer);
		pack16(msg->slurmctld_timeout, buffer);

		pack16(msg->slurmd_debug, buffer);
		packstr(msg->slurmd_logfile, buffer);
		packstr(msg->slurmd_params, buffer);
		packstr(msg->slurmd_pidfile, buffer);
		pack32(msg->slurmd_port, buffer);

		packstr(msg->slurmd_spooldir, buffer);
		pack16(msg->slurmd_syslog_debug, buffer);
		pack16(msg->slurmd_timeout, buffer);
		packstr(msg->srun_epilog, buffer);
		pack16(msg->srun_port_range[0], buffer);
		pack16(msg->srun_port_range[1], buffer);
		packstr(msg->srun_prolog, buffer);
		packstr(msg->state_save_location, buffer);
		packstr(msg->suspend_exc_nodes, buffer);
		packstr(msg->suspend_exc_parts, buffer);
		packstr(msg->suspend_exc_states, buffer);
		packstr(msg->suspend_program, buffer);
		pack16(msg->suspend_rate, buffer);
		pack32(msg->suspend_time, buffer);
		pack16(msg->suspend_timeout, buffer);
		packstr(msg->switch_param, buffer);
		packstr(msg->switch_type, buffer);

		packstr(msg->task_epilog, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_plugin, buffer);
		pack32(msg->task_plugin_param, buffer);
		pack16(msg->tcp_timeout, buffer);
		packstr(msg->tls_params, buffer);
		packstr(msg->tls_type, buffer);
		packstr(msg->tmp_fs, buffer);
		packstr(msg->topology_param, buffer);
		packstr(msg->topology_plugin, buffer);
		pack16(msg->tree_width, buffer);

		packstr(msg->unkillable_program, buffer);
		pack16(msg->unkillable_timeout, buffer);
		packstr(msg->url_parser_type, buffer);
		packstr(msg->version, buffer);
		pack16(msg->vsize_factor, buffer);

		pack16(msg->wait_time, buffer);
		packstr(msg->x11_params, buffer);
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);

		pack16(msg->accounting_storage_enforce, buffer);
		packstr(msg->accounting_storage_backup_host, buffer);
		packstr(msg->accounting_storage_host, buffer);
		packstr(msg->accounting_storage_ext_host, buffer);
		packstr(msg->accounting_storage_params, buffer);
		pack16(msg->accounting_storage_port, buffer);
		packstr(msg->accounting_storage_tres, buffer);
		packstr(msg->accounting_storage_type, buffer);
		packstr("N/A", buffer); /* was accounting_storage_user */

		pack_key_pair_list(msg->acct_gather_conf,
				   smsg->protocol_version, buffer);

		packstr(msg->acct_gather_energy_type, buffer);
		packstr(msg->acct_gather_filesystem_type, buffer);
		packstr(msg->acct_gather_interconnect_type, buffer);
		pack16(msg->acct_gather_node_freq, buffer);
		packstr(msg->acct_gather_profile_type, buffer);

		packstr(msg->authalttypes, buffer);
		packstr(msg->authalt_params, buffer);
		packstr(msg->authinfo, buffer);
		packstr(msg->authtype, buffer);

		pack16(msg->batch_start_timeout, buffer);
		pack_time(msg->boot_time, buffer);
		packstr(msg->bb_type, buffer);
		packstr(msg->bcast_exclude, buffer);
		packstr(msg->bcast_parameters, buffer);
		packstr(msg->certmgr_params, buffer);
		packstr(msg->certmgr_type, buffer);

		pack_key_pair_list(msg->cgroup_conf, smsg->protocol_version,
				   buffer);
		packstr(msg->cli_filter_plugins, buffer);
		packstr(msg->cluster_name, buffer);
		packstr(msg->comm_params, buffer);
		pack16(msg->complete_wait, buffer);
		pack32(msg->conf_flags, buffer);
		packstr_array(msg->control_addr, msg->control_cnt, buffer);
		packstr_array(msg->control_machine, msg->control_cnt, buffer);
		pack32(msg->cpu_freq_def, buffer);
		pack32(msg->cpu_freq_govs, buffer);
		packstr(msg->cred_type, buffer);
		packstr(msg->data_parser_parameters, buffer);

		pack64(msg->def_mem_per_cpu, buffer);
		pack64(msg->debug_flags, buffer);
		packstr(msg->dependency_params, buffer);

		pack16(msg->eio_timeout, buffer);
		pack16(msg->enforce_part_limits, buffer);
		packstr_array(msg->epilog, msg->epilog_cnt, buffer);
		pack32(msg->epilog_msg_time, buffer);
		packstr_array(msg->epilog_slurmctld, msg->epilog_slurmctld_cnt,
			      buffer);
		pack16(msg->epilog_timeout, buffer);

		packstr(msg->fed_params, buffer);
		pack32(msg->first_job_id, buffer);
		pack16(msg->fs_dampening_factor, buffer);

		packstr(msg->gres_plugins, buffer);
		pack16(msg->group_time, buffer);
		pack16(msg->group_force, buffer);
		packstr(msg->gpu_freq_def, buffer);

		packstr(msg->hash_plugin, buffer);
		pack32(msg->hash_val, buffer);

		pack16(msg->health_check_interval, buffer);
		pack16(msg->health_check_node_state, buffer);
		packstr(msg->health_check_program, buffer);

		pack16(msg->inactive_limit, buffer);
		packstr(msg->interactive_step_opts, buffer);

		packstr(msg->job_acct_gather_freq, buffer);
		packstr(msg->job_acct_gather_type, buffer);
		packstr(msg->job_acct_gather_params, buffer);

		packstr(msg->job_comp_host, buffer);
		packstr(msg->job_comp_loc, buffer);
		packstr(msg->job_comp_params, buffer);
		pack32((uint32_t) msg->job_comp_port, buffer);
		packstr(msg->job_comp_type, buffer);
		packstr(msg->job_comp_user, buffer);
		packstr(msg->namespace_plugin, buffer);

		(void) slurm_pack_list(msg->job_defaults_list,
				       job_defaults_pack, buffer,
				       smsg->protocol_version);
		pack16(msg->job_file_append, buffer);
		pack16(msg->job_requeue, buffer);
		packstr(msg->job_submit_plugins, buffer);

		pack16(msg->kill_on_bad_exit, buffer);
		pack16(msg->kill_wait, buffer);

		packstr(msg->launch_params, buffer);
		packstr(msg->licenses, buffer);
		pack16(msg->log_fmt, buffer);

		pack32(msg->max_array_sz, buffer);
		pack32(msg->max_batch_requeue, buffer);
		pack32(msg->max_dbd_msgs, buffer);
		packstr(msg->mail_domain, buffer);
		packstr(msg->mail_prog, buffer);
		pack32(msg->max_job_cnt, buffer);
		pack32(msg->max_job_id, buffer);
		pack64(msg->max_mem_per_cpu, buffer);
		pack32(msg->max_node_cnt, buffer);
		pack32(msg->max_step_cnt, buffer);
		pack16(msg->max_tasks_per_node, buffer);

		packstr(msg->mcs_plugin, buffer);
		packstr(msg->mcs_plugin_params, buffer);

		pack32(msg->min_job_age, buffer);
		pack_key_pair_list(msg->mpi_conf, smsg->protocol_version,
				   buffer);
		packstr(msg->mpi_default, buffer);
		packstr(msg->mpi_params, buffer);
		pack16(msg->msg_timeout, buffer);

		pack32(msg->next_job_id, buffer);

		pack_config_plugin_params_list(msg->node_features_conf,
					       smsg->protocol_version, buffer);

		packstr(msg->node_features_plugins, buffer);

		pack16(msg->over_time_limit, buffer);

		packstr(msg->plugindir, buffer);
		packstr(msg->plugstack, buffer);
		pack16(msg->preempt_mode, buffer);
		packstr(msg->preempt_params, buffer);
		packstr(msg->preempt_type, buffer);
		pack32(msg->preempt_exempt_time, buffer);
		packstr(msg->prep_params, buffer);
		packstr(msg->prep_plugins, buffer);

		pack32(msg->priority_decay_hl, buffer);
		pack32(msg->priority_calc_period, buffer);
		pack16(msg->priority_favor_small, buffer);
		pack16(msg->priority_flags, buffer);
		pack32(msg->priority_max_age, buffer);
		packstr(msg->priority_params, buffer);
		pack16(msg->priority_reset_period, buffer);
		packstr(msg->priority_type, buffer);
		pack32(msg->priority_weight_age, buffer);
		pack32(msg->priority_weight_assoc, buffer);
		pack32(msg->priority_weight_fs, buffer);
		pack32(msg->priority_weight_js, buffer);
		pack32(msg->priority_weight_part, buffer);
		pack32(msg->priority_weight_qos, buffer);
		packstr(msg->priority_weight_tres, buffer);

		pack16(msg->private_data, buffer);
		packstr(msg->proctrack_type, buffer);
		packstr_array(msg->prolog, msg->prolog_cnt, buffer);
		packstr_array(msg->prolog_slurmctld, msg->prolog_slurmctld_cnt,
			      buffer);
		pack16(msg->prolog_timeout, buffer);
		pack16(msg->prolog_flags, buffer);
		pack16(msg->propagate_prio_process, buffer);
		packstr(msg->propagate_rlimits, buffer);
		packstr(msg->propagate_rlimits_except, buffer);

		packstr(msg->reboot_program, buffer);
		pack16(msg->reconfig_flags, buffer);
		packstr(msg->requeue_exit, buffer);
		packstr(msg->requeue_exit_hold, buffer);
		packstr(msg->resume_fail_program, buffer);
		packstr(msg->resume_program, buffer);
		pack16(msg->resume_rate, buffer);
		pack16(msg->resume_timeout, buffer);
		packstr(msg->resv_epilog, buffer);
		pack16(msg->resv_over_run, buffer);
		packstr(msg->resv_prolog, buffer);
		pack16(msg->ret2service, buffer);

		packstr(msg->sched_params, buffer);
		packstr(msg->sched_logfile, buffer);
		pack16(msg->sched_log_level, buffer);
		pack16(msg->sched_time_slice, buffer);
		packstr(msg->schedtype, buffer);
		packstr(msg->scron_params, buffer);
		packstr(msg->select_type, buffer);

		pack_key_pair_list(msg->select_conf_key_pairs,
				   smsg->protocol_version, buffer);

		pack16(msg->select_type_param, buffer);

		packstr(msg->slurm_conf, buffer);
		pack32(msg->slurm_user_id, buffer);
		packstr(msg->slurm_user_name, buffer);
		pack32(msg->slurmd_user_id, buffer);
		packstr(msg->slurmd_user_name, buffer);

		packstr(msg->slurmctld_addr, buffer);
		pack16(msg->slurmctld_debug, buffer);
		packstr(msg->slurmctld_logfile, buffer);
		packstr(msg->slurmctld_params, buffer);
		packstr(msg->slurmctld_pidfile, buffer);
		pack32(msg->slurmctld_port, buffer);
		pack16(msg->slurmctld_port_count, buffer);
		packstr(msg->slurmctld_primary_off_prog, buffer);
		packstr(msg->slurmctld_primary_on_prog, buffer);
		pack16(msg->slurmctld_syslog_debug, buffer);
		pack16(msg->slurmctld_timeout, buffer);

		pack16(msg->slurmd_debug, buffer);
		packstr(msg->slurmd_logfile, buffer);
		packstr(msg->slurmd_params, buffer);
		packstr(msg->slurmd_pidfile, buffer);
		pack32(msg->slurmd_port, buffer);

		packstr(msg->slurmd_spooldir, buffer);
		pack16(msg->slurmd_syslog_debug, buffer);
		pack16(msg->slurmd_timeout, buffer);
		packstr(msg->srun_epilog, buffer);
		pack16(msg->srun_port_range[0], buffer);
		pack16(msg->srun_port_range[1], buffer);
		packstr(msg->srun_prolog, buffer);
		packstr(msg->state_save_location, buffer);
		packstr(msg->suspend_exc_nodes, buffer);
		packstr(msg->suspend_exc_parts, buffer);
		packstr(msg->suspend_exc_states, buffer);
		packstr(msg->suspend_program, buffer);
		pack16(msg->suspend_rate, buffer);
		pack32(msg->suspend_time, buffer);
		pack16(msg->suspend_timeout, buffer);
		packstr(msg->switch_param, buffer);
		packstr(msg->switch_type, buffer);

		packstr(msg->task_epilog, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_plugin, buffer);
		pack32(msg->task_plugin_param, buffer);
		pack16(msg->tcp_timeout, buffer);
		packstr(msg->tls_params, buffer);
		packstr(msg->tls_type, buffer);
		packstr(msg->tmp_fs, buffer);
		packstr(msg->topology_param, buffer);
		packstr(msg->topology_plugin, buffer);
		pack16(msg->tree_width, buffer);

		packstr(msg->unkillable_program, buffer);
		pack16(msg->unkillable_timeout, buffer);
		packstr(msg->version, buffer);
		pack16(msg->vsize_factor, buffer);

		pack16(msg->wait_time, buffer);
		packstr(msg->x11_params, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);

		pack16(msg->accounting_storage_enforce, buffer);
		packstr(msg->accounting_storage_backup_host, buffer);
		packstr(msg->accounting_storage_host, buffer);
		packstr(msg->accounting_storage_ext_host, buffer);
		packstr(msg->accounting_storage_params, buffer);
		pack16(msg->accounting_storage_port, buffer);
		packstr(msg->accounting_storage_tres, buffer);
		packstr(msg->accounting_storage_type, buffer);
		packstr("N/A", buffer); /* was accounting_storage_user */

		pack_key_pair_list(msg->acct_gather_conf,
				   smsg->protocol_version, buffer);

		packstr(msg->acct_gather_energy_type, buffer);
		packstr(msg->acct_gather_filesystem_type, buffer);
		packstr(msg->acct_gather_interconnect_type, buffer);
		pack16(msg->acct_gather_node_freq, buffer);
		packstr(msg->acct_gather_profile_type, buffer);

		packstr(msg->authalttypes, buffer);
		packstr(msg->authalt_params, buffer);
		packstr(msg->authinfo, buffer);
		packstr(msg->authtype, buffer);

		pack16(msg->batch_start_timeout, buffer);
		pack_time(msg->boot_time, buffer);
		packstr(msg->bb_type, buffer);
		packstr(msg->bcast_exclude, buffer);
		packstr(msg->bcast_parameters, buffer);

		pack_key_pair_list(msg->cgroup_conf, smsg->protocol_version,
				   buffer);
		packstr(msg->cli_filter_plugins, buffer);
		packstr(msg->cluster_name, buffer);
		packstr(msg->comm_params, buffer);
		pack16(msg->complete_wait, buffer);
		pack32(msg->conf_flags, buffer);
		packstr_array(msg->control_addr, msg->control_cnt, buffer);
		packstr_array(msg->control_machine, msg->control_cnt, buffer);
		pack32(msg->cpu_freq_def, buffer);
		pack32(msg->cpu_freq_govs, buffer);
		packstr(msg->cred_type, buffer);
		packstr(msg->data_parser_parameters, buffer);

		pack64(msg->def_mem_per_cpu, buffer);
		pack64(msg->debug_flags, buffer);
		packstr(msg->dependency_params, buffer);

		pack16(msg->eio_timeout, buffer);
		pack16(msg->enforce_part_limits, buffer);
		packstr_array(msg->epilog, msg->epilog_cnt, buffer);
		pack32(msg->epilog_msg_time, buffer);
		packstr_array(msg->epilog_slurmctld, msg->epilog_slurmctld_cnt,
			      buffer);

		packstr(msg->fed_params, buffer);
		pack32(msg->first_job_id, buffer);
		pack16(msg->fs_dampening_factor, buffer);

		pack16(DEFAULT_GET_ENV_TIMEOUT, buffer); /* was get_env_timeout */
		packstr(msg->gres_plugins, buffer);
		pack16(msg->group_time, buffer);
		pack16(msg->group_force, buffer);
		packstr(msg->gpu_freq_def, buffer);

		packstr(msg->hash_plugin, buffer);
		pack32(msg->hash_val, buffer);

		pack16(msg->health_check_interval, buffer);
		pack16(msg->health_check_node_state, buffer);
		packstr(msg->health_check_program, buffer);

		pack16(msg->inactive_limit, buffer);
		packstr(msg->interactive_step_opts, buffer);

		packstr(msg->job_acct_gather_freq, buffer);
		packstr(msg->job_acct_gather_type, buffer);
		packstr(msg->job_acct_gather_params, buffer);

		packstr(msg->job_comp_host, buffer);
		packstr(msg->job_comp_loc, buffer);
		packstr(msg->job_comp_params, buffer);
		pack32((uint32_t) msg->job_comp_port, buffer);
		packstr(msg->job_comp_type, buffer);
		packstr(msg->job_comp_user, buffer);
		packstr(msg->namespace_plugin, buffer);

		(void) slurm_pack_list(msg->job_defaults_list,
				       job_defaults_pack, buffer,
				       smsg->protocol_version);
		pack16(msg->job_file_append, buffer);
		pack16(msg->job_requeue, buffer);
		packstr(msg->job_submit_plugins, buffer);

		pack16(msg->kill_on_bad_exit, buffer);
		pack16(msg->kill_wait, buffer);

		packstr(msg->launch_params, buffer);
		packstr(msg->licenses, buffer);
		pack16(msg->log_fmt, buffer);

		pack32(msg->max_array_sz, buffer);
		pack32(msg->max_batch_requeue, buffer);
		pack32(msg->max_dbd_msgs, buffer);
		packstr(msg->mail_domain, buffer);
		packstr(msg->mail_prog, buffer);
		pack32(msg->max_job_cnt, buffer);
		pack32(msg->max_job_id, buffer);
		pack64(msg->max_mem_per_cpu, buffer);
		pack32(msg->max_node_cnt, buffer);
		pack32(msg->max_step_cnt, buffer);
		pack16(msg->max_tasks_per_node, buffer);

		packstr(msg->mcs_plugin, buffer);
		packstr(msg->mcs_plugin_params, buffer);

		pack32(msg->min_job_age, buffer);
		pack_key_pair_list(msg->mpi_conf, smsg->protocol_version,
				   buffer);
		packstr(msg->mpi_default, buffer);
		packstr(msg->mpi_params, buffer);
		pack16(msg->msg_timeout, buffer);

		pack32(msg->next_job_id, buffer);

		pack_config_plugin_params_list(msg->node_features_conf,
					       smsg->protocol_version, buffer);

		packstr(msg->node_features_plugins, buffer);
		packnull(buffer); /* was node_prefix */

		pack16(msg->over_time_limit, buffer);

		packstr(msg->plugindir, buffer);
		packstr(msg->plugstack, buffer);
		pack16(msg->preempt_mode, buffer);
		packstr(msg->preempt_params, buffer);
		packstr(msg->preempt_type, buffer);
		pack32(msg->preempt_exempt_time, buffer);
		packstr(msg->prep_params, buffer);
		packstr(msg->prep_plugins, buffer);

		pack32(msg->priority_decay_hl, buffer);
		pack32(msg->priority_calc_period, buffer);
		pack16(msg->priority_favor_small, buffer);
		pack16(msg->priority_flags, buffer);
		pack32(msg->priority_max_age, buffer);
		packstr(msg->priority_params, buffer);
		pack16(msg->priority_reset_period, buffer);
		packstr(msg->priority_type, buffer);
		pack32(msg->priority_weight_age, buffer);
		pack32(msg->priority_weight_assoc, buffer);
		pack32(msg->priority_weight_fs, buffer);
		pack32(msg->priority_weight_js, buffer);
		pack32(msg->priority_weight_part, buffer);
		pack32(msg->priority_weight_qos, buffer);
		packstr(msg->priority_weight_tres, buffer);

		pack16(msg->private_data, buffer);
		packstr(msg->proctrack_type, buffer);
		packstr_array(msg->prolog, msg->prolog_cnt, buffer);
		pack16(MAX(msg->prolog_timeout, msg->epilog_timeout), buffer);
		packstr_array(msg->prolog_slurmctld, msg->prolog_slurmctld_cnt,
			      buffer);
		pack16(msg->prolog_flags, buffer);
		pack16(msg->propagate_prio_process, buffer);
		packstr(msg->propagate_rlimits, buffer);
		packstr(msg->propagate_rlimits_except, buffer);

		packstr(msg->reboot_program, buffer);
		pack16(msg->reconfig_flags, buffer);
		packstr(msg->requeue_exit, buffer);
		packstr(msg->requeue_exit_hold, buffer);
		packstr(msg->resume_fail_program, buffer);
		packstr(msg->resume_program, buffer);
		pack16(msg->resume_rate, buffer);
		pack16(msg->resume_timeout, buffer);
		packstr(msg->resv_epilog, buffer);
		pack16(msg->resv_over_run, buffer);
		packstr(msg->resv_prolog, buffer);
		pack16(msg->ret2service, buffer);

		packstr(msg->sched_params, buffer);
		packstr(msg->sched_logfile, buffer);
		pack16(msg->sched_log_level, buffer);
		pack16(msg->sched_time_slice, buffer);
		packstr(msg->schedtype, buffer);
		packstr(msg->scron_params, buffer);
		packstr(msg->select_type, buffer);

		pack_key_pair_list(msg->select_conf_key_pairs,
				   smsg->protocol_version, buffer);

		pack16(msg->select_type_param, buffer);

		packstr(msg->slurm_conf, buffer);
		pack32(msg->slurm_user_id, buffer);
		packstr(msg->slurm_user_name, buffer);
		pack32(msg->slurmd_user_id, buffer);
		packstr(msg->slurmd_user_name, buffer);

		packstr(msg->slurmctld_addr, buffer);
		pack16(msg->slurmctld_debug, buffer);
		packstr(msg->slurmctld_logfile, buffer);
		packstr(msg->slurmctld_params, buffer);
		packstr(msg->slurmctld_pidfile, buffer);
		pack32(msg->slurmctld_port, buffer);
		pack16(msg->slurmctld_port_count, buffer);
		packstr(msg->slurmctld_primary_off_prog, buffer);
		packstr(msg->slurmctld_primary_on_prog, buffer);
		pack16(msg->slurmctld_syslog_debug, buffer);
		pack16(msg->slurmctld_timeout, buffer);

		pack16(msg->slurmd_debug, buffer);
		packstr(msg->slurmd_logfile, buffer);
		packstr(msg->slurmd_params, buffer);
		packstr(msg->slurmd_pidfile, buffer);
		pack32(msg->slurmd_port, buffer);

		packstr(msg->slurmd_spooldir, buffer);
		pack16(msg->slurmd_syslog_debug, buffer);
		pack16(msg->slurmd_timeout, buffer);
		packstr(msg->srun_epilog, buffer);
		pack16(msg->srun_port_range[0], buffer);
		pack16(msg->srun_port_range[1], buffer);
		packstr(msg->srun_prolog, buffer);
		packstr(msg->state_save_location, buffer);
		packstr(msg->suspend_exc_nodes, buffer);
		packstr(msg->suspend_exc_parts, buffer);
		packstr(msg->suspend_exc_states, buffer);
		packstr(msg->suspend_program, buffer);
		pack16(msg->suspend_rate, buffer);
		pack32(msg->suspend_time, buffer);
		pack16(msg->suspend_timeout, buffer);
		packstr(msg->switch_param, buffer);
		packstr(msg->switch_type, buffer);

		packstr(msg->task_epilog, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_plugin, buffer);
		pack32(msg->task_plugin_param, buffer);
		pack16(msg->tcp_timeout, buffer);
		packstr(msg->tls_type, buffer);
		packstr(msg->tmp_fs, buffer);
		packstr(msg->topology_param, buffer);
		packstr(msg->topology_plugin, buffer);
		pack16(msg->tree_width, buffer);

		packstr(msg->unkillable_program, buffer);
		pack16(msg->unkillable_timeout, buffer);
		packstr(msg->version, buffer);
		pack16(msg->vsize_factor, buffer);

		pack16(msg->wait_time, buffer);
		packstr(msg->x11_params, buffer);
	}
}

static int _unpack_slurm_ctl_conf_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp = 0;
	uint16_t uint16_tmp = 0;
	slurm_ctl_conf_info_msg_t *build_ptr = xmalloc(sizeof(*build_ptr));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_backup_host,
			       buffer);
		safe_unpackstr(&build_ptr->accounting_storage_host, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_ext_host, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_params, buffer);
		safe_unpack16(&build_ptr->accounting_storage_port, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_tres, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_type, buffer);

		if (unpack_key_pair_list(&build_ptr->acct_gather_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&build_ptr->acct_gather_energy_type, buffer);
		safe_unpackstr(&build_ptr->acct_gather_filesystem_type, buffer);
		safe_unpackstr(&build_ptr->acct_gather_interconnect_type,
			       buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr(&build_ptr->acct_gather_profile_type, buffer);

		safe_unpackstr(&build_ptr->authalttypes, buffer);
		safe_unpackstr(&build_ptr->authalt_params, buffer);
		safe_unpackstr(&build_ptr->authinfo, buffer);
		safe_unpackstr(&build_ptr->authtype, buffer);

		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);
		safe_unpackstr(&build_ptr->bb_type, buffer);
		safe_unpackstr(&build_ptr->bcast_exclude, buffer);
		safe_unpackstr(&build_ptr->bcast_parameters, buffer);
		safe_unpackstr(&build_ptr->certmgr_params, buffer);
		safe_unpackstr(&build_ptr->certmgr_type, buffer);

		if (unpack_key_pair_list(&build_ptr->cgroup_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&build_ptr->cli_filter_params, buffer);
		safe_unpackstr(&build_ptr->cli_filter_plugins, buffer);
		safe_unpackstr(&build_ptr->cluster_name, buffer);
		safe_unpackstr(&build_ptr->comm_params, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpack32(&build_ptr->conf_flags, buffer);
		safe_unpackstr_array(&build_ptr->control_addr,
		                     &build_ptr->control_cnt, buffer);
		safe_unpackstr_array(&build_ptr->control_machine,
		                     &uint32_tmp, buffer);
		if (build_ptr->control_cnt != uint32_tmp)
			goto unpack_error;
		safe_unpack32(&build_ptr->cpu_freq_def, buffer);
		safe_unpack32(&build_ptr->cpu_freq_govs, buffer);
		safe_unpackstr(&build_ptr->cred_type, buffer);
		safe_unpackstr(&build_ptr->data_parser_parameters, buffer);

		safe_unpack64(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack64(&build_ptr->debug_flags, buffer);
		safe_unpackstr(&build_ptr->dependency_params, buffer);

		safe_unpack16(&build_ptr->eio_timeout, buffer);
		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_array(&build_ptr->epilog,
				     &build_ptr->epilog_cnt, buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_array(&build_ptr->epilog_slurmctld,
				     &build_ptr->epilog_slurmctld_cnt, buffer);
		safe_unpack16(&build_ptr->epilog_timeout, buffer);

		safe_unpackstr(&build_ptr->fed_params, buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);
		safe_unpack16(&build_ptr->fs_dampening_factor, buffer);

		safe_unpackstr(&build_ptr->gres_plugins, buffer);
		safe_unpack16(&build_ptr->group_time, buffer);
		safe_unpack16(&build_ptr->group_force, buffer);
		safe_unpackstr(&build_ptr->gpu_freq_def, buffer);

		safe_unpackstr(&build_ptr->hash_plugin, buffer);
		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr(&build_ptr->health_check_program, buffer);

		safe_unpackstr(&build_ptr->http_parser_type, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);
		safe_unpackstr(&build_ptr->interactive_step_opts, buffer);

		safe_unpackstr(&build_ptr->job_acct_gather_freq, buffer);
		safe_unpackstr(&build_ptr->job_acct_gather_type, buffer);
		safe_unpackstr(&build_ptr->job_acct_gather_params, buffer);

		safe_unpackstr(&build_ptr->job_comp_host, buffer);
		safe_unpackstr(&build_ptr->job_comp_loc, buffer);
		safe_unpackstr(&build_ptr->job_comp_params, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr(&build_ptr->job_comp_type, buffer);
		safe_unpackstr(&build_ptr->job_comp_user, buffer);
		safe_unpackstr(&build_ptr->namespace_plugin, buffer);

		if (slurm_unpack_list(&build_ptr->job_defaults_list,
				      job_defaults_unpack, xfree_ptr, buffer,
				      smsg->protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr(&build_ptr->job_submit_plugins, buffer);

		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr(&build_ptr->launch_params, buffer);
		safe_unpackstr(&build_ptr->licenses, buffer);
		safe_unpack16(&build_ptr->log_fmt, buffer);

		safe_unpack32(&build_ptr->max_array_sz, buffer);
		safe_unpack32(&build_ptr->max_batch_requeue, buffer);
		safe_unpack32(&build_ptr->max_dbd_msgs, buffer);
		safe_unpackstr(&build_ptr->mail_domain, buffer);
		safe_unpackstr(&build_ptr->mail_prog, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack64(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_node_cnt, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpackstr(&build_ptr->mcs_plugin, buffer);
		safe_unpackstr(&build_ptr->mcs_plugin_params, buffer);
		safe_unpackstr(&build_ptr->metrics_type, buffer);
		safe_unpack32(&build_ptr->min_job_age, buffer);
		if (unpack_key_pair_list(&build_ptr->mpi_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&build_ptr->mpi_default, buffer);
		safe_unpackstr(&build_ptr->mpi_params, buffer);
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);

		if (unpack_config_plugin_params_list(
			    &build_ptr->node_features_conf,
			    smsg->protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&build_ptr->node_features_plugins, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr(&build_ptr->plugindir, buffer);
		safe_unpackstr(&build_ptr->plugstack, buffer);

		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr(&build_ptr->preempt_params, buffer);
		safe_unpackstr(&build_ptr->preempt_type, buffer);
		safe_unpack32(&build_ptr->preempt_exempt_time, buffer);
		safe_unpackstr(&build_ptr->prep_params, buffer);
		safe_unpackstr(&build_ptr->prep_plugins, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpackstr(&build_ptr->priority_params, buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr(&build_ptr->priority_type, buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_assoc, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);
		safe_unpackstr(&build_ptr->priority_weight_tres, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr(&build_ptr->proctrack_type, buffer);
		safe_unpackstr_array(&build_ptr->prolog,
				     &build_ptr->prolog_cnt, buffer);
		safe_unpackstr_array(&build_ptr->prolog_slurmctld,
				     &build_ptr->prolog_slurmctld_cnt, buffer);
		safe_unpack16(&build_ptr->prolog_timeout, buffer);
		safe_unpack16(&build_ptr->prolog_flags, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr(&build_ptr->propagate_rlimits, buffer);
		safe_unpackstr(&build_ptr->propagate_rlimits_except, buffer);

		safe_unpackstr(&build_ptr->reboot_program, buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);

		safe_unpackstr(&build_ptr->requeue_exit, buffer);
		safe_unpackstr(&build_ptr->requeue_exit_hold, buffer);

		safe_unpackstr(&build_ptr->resume_fail_program, buffer);
		safe_unpackstr(&build_ptr->resume_program, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr(&build_ptr->resv_epilog, buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr(&build_ptr->resv_prolog, buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr(&build_ptr->sched_params, buffer);
		safe_unpackstr(&build_ptr->sched_logfile, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr(&build_ptr->schedtype, buffer);
		safe_unpackstr(&build_ptr->scron_params, buffer);
		safe_unpackstr(&build_ptr->select_type, buffer);

		if (unpack_key_pair_list(&build_ptr->select_conf_key_pairs,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr(&build_ptr->slurm_conf, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr(&build_ptr->slurm_user_name, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr(&build_ptr->slurmd_user_name, buffer);

		safe_unpackstr(&build_ptr->slurmctld_addr, buffer);
		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr(&build_ptr->slurmctld_logfile, buffer);
		safe_unpackstr(&build_ptr->slurmctld_params, buffer);
		safe_unpackstr(&build_ptr->slurmctld_pidfile, buffer);
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpackstr(&build_ptr->slurmctld_primary_off_prog, buffer);
		safe_unpackstr(&build_ptr->slurmctld_primary_on_prog, buffer);
		safe_unpack16(&build_ptr->slurmctld_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr(&build_ptr->slurmd_logfile, buffer);
		safe_unpackstr(&build_ptr->slurmd_params, buffer);
		safe_unpackstr(&build_ptr->slurmd_pidfile, buffer);
		safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr(&build_ptr->slurmd_spooldir, buffer);
		safe_unpack16(&build_ptr->slurmd_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr(&build_ptr->srun_epilog, buffer);

		build_ptr->srun_port_range = xcalloc(2, sizeof(uint16_t));
		safe_unpack16(&build_ptr->srun_port_range[0], buffer);
		safe_unpack16(&build_ptr->srun_port_range[1], buffer);

		safe_unpackstr(&build_ptr->srun_prolog, buffer);
		safe_unpackstr(&build_ptr->state_save_location, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_nodes, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_parts, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_states, buffer);
		safe_unpackstr(&build_ptr->suspend_program, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr(&build_ptr->switch_param, buffer);
		safe_unpackstr(&build_ptr->switch_type, buffer);

		safe_unpackstr(&build_ptr->task_epilog, buffer);
		safe_unpackstr(&build_ptr->task_prolog, buffer);
		safe_unpackstr(&build_ptr->task_plugin, buffer);
		safe_unpack32(&build_ptr->task_plugin_param, buffer);
		safe_unpack16(&build_ptr->tcp_timeout, buffer);
		safe_unpackstr(&build_ptr->tls_params, buffer);
		safe_unpackstr(&build_ptr->tls_type, buffer);
		safe_unpackstr(&build_ptr->tmp_fs, buffer);
		safe_unpackstr(&build_ptr->topology_param, buffer);
		safe_unpackstr(&build_ptr->topology_plugin, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpackstr(&build_ptr->unkillable_program, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr(&build_ptr->url_parser_type, buffer);
		safe_unpackstr(&build_ptr->version, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);
		safe_unpackstr(&build_ptr->x11_params, buffer);
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_backup_host,
			       buffer);
		safe_unpackstr(&build_ptr->accounting_storage_host, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_ext_host, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_params, buffer);
		safe_unpack16(&build_ptr->accounting_storage_port, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_tres, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_type, buffer);
		safe_skipstr(buffer); /* was accounting_storage_user */

		if (unpack_key_pair_list(&build_ptr->acct_gather_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&build_ptr->acct_gather_energy_type, buffer);
		safe_unpackstr(&build_ptr->acct_gather_filesystem_type, buffer);
		safe_unpackstr(&build_ptr->acct_gather_interconnect_type,
			       buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr(&build_ptr->acct_gather_profile_type, buffer);

		safe_unpackstr(&build_ptr->authalttypes, buffer);
		safe_unpackstr(&build_ptr->authalt_params, buffer);
		safe_unpackstr(&build_ptr->authinfo, buffer);
		safe_unpackstr(&build_ptr->authtype, buffer);

		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);
		safe_unpackstr(&build_ptr->bb_type, buffer);
		safe_unpackstr(&build_ptr->bcast_exclude, buffer);
		safe_unpackstr(&build_ptr->bcast_parameters, buffer);
		safe_unpackstr(&build_ptr->certmgr_params, buffer);
		safe_unpackstr(&build_ptr->certmgr_type, buffer);

		if (unpack_key_pair_list(&build_ptr->cgroup_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&build_ptr->cli_filter_plugins, buffer);
		safe_unpackstr(&build_ptr->cluster_name, buffer);
		safe_unpackstr(&build_ptr->comm_params, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpack32(&build_ptr->conf_flags, buffer);
		safe_unpackstr_array(&build_ptr->control_addr,
		                     &build_ptr->control_cnt, buffer);
		safe_unpackstr_array(&build_ptr->control_machine,
		                     &uint32_tmp, buffer);
		if (build_ptr->control_cnt != uint32_tmp)
			goto unpack_error;
		safe_unpack32(&build_ptr->cpu_freq_def, buffer);
		safe_unpack32(&build_ptr->cpu_freq_govs, buffer);
		safe_unpackstr(&build_ptr->cred_type, buffer);
		safe_unpackstr(&build_ptr->data_parser_parameters, buffer);

		safe_unpack64(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack64(&build_ptr->debug_flags, buffer);
		safe_unpackstr(&build_ptr->dependency_params, buffer);

		safe_unpack16(&build_ptr->eio_timeout, buffer);
		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_array(&build_ptr->epilog,
				     &build_ptr->epilog_cnt, buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_array(&build_ptr->epilog_slurmctld,
				     &build_ptr->epilog_slurmctld_cnt, buffer);
		safe_unpack16(&build_ptr->epilog_timeout, buffer);

		safe_unpackstr(&build_ptr->fed_params, buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);
		safe_unpack16(&build_ptr->fs_dampening_factor, buffer);

		safe_unpackstr(&build_ptr->gres_plugins, buffer);
		safe_unpack16(&build_ptr->group_time, buffer);
		safe_unpack16(&build_ptr->group_force, buffer);
		safe_unpackstr(&build_ptr->gpu_freq_def, buffer);

		safe_unpackstr(&build_ptr->hash_plugin, buffer);
		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr(&build_ptr->health_check_program, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);
		safe_unpackstr(&build_ptr->interactive_step_opts, buffer);

		safe_unpackstr(&build_ptr->job_acct_gather_freq, buffer);
		safe_unpackstr(&build_ptr->job_acct_gather_type, buffer);
		safe_unpackstr(&build_ptr->job_acct_gather_params, buffer);

		safe_unpackstr(&build_ptr->job_comp_host, buffer);
		safe_unpackstr(&build_ptr->job_comp_loc, buffer);
		safe_unpackstr(&build_ptr->job_comp_params, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr(&build_ptr->job_comp_type, buffer);
		safe_unpackstr(&build_ptr->job_comp_user, buffer);
		safe_unpackstr(&build_ptr->namespace_plugin, buffer);

		if (slurm_unpack_list(&build_ptr->job_defaults_list,
				      job_defaults_unpack, xfree_ptr, buffer,
				      smsg->protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr(&build_ptr->job_submit_plugins, buffer);

		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr(&build_ptr->launch_params, buffer);
		safe_unpackstr(&build_ptr->licenses, buffer);
		safe_unpack16(&build_ptr->log_fmt, buffer);

		safe_unpack32(&build_ptr->max_array_sz, buffer);
		safe_unpack32(&build_ptr->max_batch_requeue, buffer);
		safe_unpack32(&build_ptr->max_dbd_msgs, buffer);
		safe_unpackstr(&build_ptr->mail_domain, buffer);
		safe_unpackstr(&build_ptr->mail_prog, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack64(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_node_cnt, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpackstr(&build_ptr->mcs_plugin, buffer);
		safe_unpackstr(&build_ptr->mcs_plugin_params, buffer);
		safe_unpack32(&build_ptr->min_job_age, buffer);
		if (unpack_key_pair_list(&build_ptr->mpi_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&build_ptr->mpi_default, buffer);
		safe_unpackstr(&build_ptr->mpi_params, buffer);
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);

		if (unpack_config_plugin_params_list(
			    &build_ptr->node_features_conf,
			    smsg->protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&build_ptr->node_features_plugins, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr(&build_ptr->plugindir, buffer);
		safe_unpackstr(&build_ptr->plugstack, buffer);

		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr(&build_ptr->preempt_params, buffer);
		safe_unpackstr(&build_ptr->preempt_type, buffer);
		safe_unpack32(&build_ptr->preempt_exempt_time, buffer);
		safe_unpackstr(&build_ptr->prep_params, buffer);
		safe_unpackstr(&build_ptr->prep_plugins, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpackstr(&build_ptr->priority_params, buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr(&build_ptr->priority_type, buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_assoc, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);
		safe_unpackstr(&build_ptr->priority_weight_tres, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr(&build_ptr->proctrack_type, buffer);
		safe_unpackstr_array(&build_ptr->prolog,
				     &build_ptr->prolog_cnt, buffer);
		safe_unpackstr_array(&build_ptr->prolog_slurmctld,
				     &build_ptr->prolog_slurmctld_cnt, buffer);
		safe_unpack16(&build_ptr->prolog_timeout, buffer);
		safe_unpack16(&build_ptr->prolog_flags, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr(&build_ptr->propagate_rlimits, buffer);
		safe_unpackstr(&build_ptr->propagate_rlimits_except, buffer);

		safe_unpackstr(&build_ptr->reboot_program, buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);

		safe_unpackstr(&build_ptr->requeue_exit, buffer);
		safe_unpackstr(&build_ptr->requeue_exit_hold, buffer);

		safe_unpackstr(&build_ptr->resume_fail_program, buffer);
		safe_unpackstr(&build_ptr->resume_program, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr(&build_ptr->resv_epilog, buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr(&build_ptr->resv_prolog, buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr(&build_ptr->sched_params, buffer);
		safe_unpackstr(&build_ptr->sched_logfile, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr(&build_ptr->schedtype, buffer);
		safe_unpackstr(&build_ptr->scron_params, buffer);
		safe_unpackstr(&build_ptr->select_type, buffer);

		if (unpack_key_pair_list(&build_ptr->select_conf_key_pairs,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr(&build_ptr->slurm_conf, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr(&build_ptr->slurm_user_name, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr(&build_ptr->slurmd_user_name, buffer);

		safe_unpackstr(&build_ptr->slurmctld_addr, buffer);
		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr(&build_ptr->slurmctld_logfile, buffer);
		safe_unpackstr(&build_ptr->slurmctld_params, buffer);
		safe_unpackstr(&build_ptr->slurmctld_pidfile, buffer);
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpackstr(&build_ptr->slurmctld_primary_off_prog, buffer);
		safe_unpackstr(&build_ptr->slurmctld_primary_on_prog, buffer);
		safe_unpack16(&build_ptr->slurmctld_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr(&build_ptr->slurmd_logfile, buffer);
		safe_unpackstr(&build_ptr->slurmd_params, buffer);
		safe_unpackstr(&build_ptr->slurmd_pidfile, buffer);
		safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr(&build_ptr->slurmd_spooldir, buffer);
		safe_unpack16(&build_ptr->slurmd_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr(&build_ptr->srun_epilog, buffer);

		build_ptr->srun_port_range = xcalloc(2, sizeof(uint16_t));
		safe_unpack16(&build_ptr->srun_port_range[0], buffer);
		safe_unpack16(&build_ptr->srun_port_range[1], buffer);

		safe_unpackstr(&build_ptr->srun_prolog, buffer);
		safe_unpackstr(&build_ptr->state_save_location, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_nodes, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_parts, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_states, buffer);
		safe_unpackstr(&build_ptr->suspend_program, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr(&build_ptr->switch_param, buffer);
		safe_unpackstr(&build_ptr->switch_type, buffer);

		safe_unpackstr(&build_ptr->task_epilog, buffer);
		safe_unpackstr(&build_ptr->task_prolog, buffer);
		safe_unpackstr(&build_ptr->task_plugin, buffer);
		safe_unpack32(&build_ptr->task_plugin_param, buffer);
		safe_unpack16(&build_ptr->tcp_timeout, buffer);
		safe_unpackstr(&build_ptr->tls_params, buffer);
		safe_unpackstr(&build_ptr->tls_type, buffer);
		safe_unpackstr(&build_ptr->tmp_fs, buffer);
		safe_unpackstr(&build_ptr->topology_param, buffer);
		safe_unpackstr(&build_ptr->topology_plugin, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpackstr(&build_ptr->unkillable_program, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr(&build_ptr->version, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);
		safe_unpackstr(&build_ptr->x11_params, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_backup_host,
			       buffer);
		safe_unpackstr(&build_ptr->accounting_storage_host, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_ext_host, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_params, buffer);
		safe_unpack16(&build_ptr->accounting_storage_port, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_tres, buffer);
		safe_unpackstr(&build_ptr->accounting_storage_type, buffer);
		safe_skipstr(buffer); /* was accounting_storage_user */

		if (unpack_key_pair_list(&build_ptr->acct_gather_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&build_ptr->acct_gather_energy_type, buffer);
		safe_unpackstr(&build_ptr->acct_gather_filesystem_type, buffer);
		safe_unpackstr(&build_ptr->acct_gather_interconnect_type,
			       buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr(&build_ptr->acct_gather_profile_type, buffer);

		safe_unpackstr(&build_ptr->authalttypes, buffer);
		safe_unpackstr(&build_ptr->authalt_params, buffer);
		safe_unpackstr(&build_ptr->authinfo, buffer);
		safe_unpackstr(&build_ptr->authtype, buffer);

		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);
		safe_unpackstr(&build_ptr->bb_type, buffer);
		safe_unpackstr(&build_ptr->bcast_exclude, buffer);
		safe_unpackstr(&build_ptr->bcast_parameters, buffer);

		if (unpack_key_pair_list(&build_ptr->cgroup_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&build_ptr->cli_filter_plugins, buffer);
		safe_unpackstr(&build_ptr->cluster_name, buffer);
		safe_unpackstr(&build_ptr->comm_params, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpack32(&build_ptr->conf_flags, buffer);
		safe_unpackstr_array(&build_ptr->control_addr,
		                     &build_ptr->control_cnt, buffer);
		safe_unpackstr_array(&build_ptr->control_machine,
		                     &uint32_tmp, buffer);
		if (build_ptr->control_cnt != uint32_tmp)
			goto unpack_error;
		safe_unpack32(&build_ptr->cpu_freq_def, buffer);
		safe_unpack32(&build_ptr->cpu_freq_govs, buffer);
		safe_unpackstr(&build_ptr->cred_type, buffer);
		safe_unpackstr(&build_ptr->data_parser_parameters, buffer);

		safe_unpack64(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack64(&build_ptr->debug_flags, buffer);
		safe_unpackstr(&build_ptr->dependency_params, buffer);

		safe_unpack16(&build_ptr->eio_timeout, buffer);
		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_array(&build_ptr->epilog,
				     &build_ptr->epilog_cnt, buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_array(&build_ptr->epilog_slurmctld,
				     &build_ptr->epilog_slurmctld_cnt, buffer);

		safe_unpackstr(&build_ptr->fed_params, buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);
		safe_unpack16(&build_ptr->fs_dampening_factor, buffer);

		safe_unpack16(&uint16_tmp, buffer); /* was get_env_timeout */
		safe_unpackstr(&build_ptr->gres_plugins, buffer);
		safe_unpack16(&build_ptr->group_time, buffer);
		safe_unpack16(&build_ptr->group_force, buffer);
		safe_unpackstr(&build_ptr->gpu_freq_def, buffer);

		safe_unpackstr(&build_ptr->hash_plugin, buffer);
		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr(&build_ptr->health_check_program, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);
		safe_unpackstr(&build_ptr->interactive_step_opts, buffer);

		safe_unpackstr(&build_ptr->job_acct_gather_freq, buffer);
		safe_unpackstr(&build_ptr->job_acct_gather_type, buffer);
		safe_unpackstr(&build_ptr->job_acct_gather_params, buffer);

		safe_unpackstr(&build_ptr->job_comp_host, buffer);
		safe_unpackstr(&build_ptr->job_comp_loc, buffer);
		safe_unpackstr(&build_ptr->job_comp_params, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr(&build_ptr->job_comp_type, buffer);
		safe_unpackstr(&build_ptr->job_comp_user, buffer);
		safe_unpackstr(&build_ptr->namespace_plugin, buffer);

		if (slurm_unpack_list(&build_ptr->job_defaults_list,
				      job_defaults_unpack, xfree_ptr, buffer,
				      smsg->protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr(&build_ptr->job_submit_plugins, buffer);

		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr(&build_ptr->launch_params, buffer);
		safe_unpackstr(&build_ptr->licenses, buffer);
		safe_unpack16(&build_ptr->log_fmt, buffer);

		safe_unpack32(&build_ptr->max_array_sz, buffer);
		safe_unpack32(&build_ptr->max_batch_requeue, buffer);
		safe_unpack32(&build_ptr->max_dbd_msgs, buffer);
		safe_unpackstr(&build_ptr->mail_domain, buffer);
		safe_unpackstr(&build_ptr->mail_prog, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack64(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_node_cnt, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpackstr(&build_ptr->mcs_plugin, buffer);
		safe_unpackstr(&build_ptr->mcs_plugin_params, buffer);
		safe_unpack32(&build_ptr->min_job_age, buffer);
		if (unpack_key_pair_list(&build_ptr->mpi_conf,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&build_ptr->mpi_default, buffer);
		safe_unpackstr(&build_ptr->mpi_params, buffer);
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);

		if (unpack_config_plugin_params_list(
			    &build_ptr->node_features_conf,
			    smsg->protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&build_ptr->node_features_plugins, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr(&build_ptr->plugindir, buffer);
		safe_unpackstr(&build_ptr->plugstack, buffer);

		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr(&build_ptr->preempt_params, buffer);
		safe_unpackstr(&build_ptr->preempt_type, buffer);
		safe_unpack32(&build_ptr->preempt_exempt_time, buffer);
		safe_unpackstr(&build_ptr->prep_params, buffer);
		safe_unpackstr(&build_ptr->prep_plugins, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpackstr(&build_ptr->priority_params, buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr(&build_ptr->priority_type, buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_assoc, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);
		safe_unpackstr(&build_ptr->priority_weight_tres, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr(&build_ptr->proctrack_type, buffer);
		safe_unpackstr_array(&build_ptr->prolog,
				     &build_ptr->prolog_cnt, buffer);

		/* Originally prolog_epilog_timeout */
		safe_unpack16(&build_ptr->prolog_timeout, buffer);
		build_ptr->epilog_timeout = build_ptr->prolog_timeout;

		safe_unpackstr_array(&build_ptr->prolog_slurmctld,
				     &build_ptr->prolog_slurmctld_cnt, buffer);
		safe_unpack16(&build_ptr->prolog_flags, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr(&build_ptr->propagate_rlimits, buffer);
		safe_unpackstr(&build_ptr->propagate_rlimits_except, buffer);

		safe_unpackstr(&build_ptr->reboot_program, buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);

		safe_unpackstr(&build_ptr->requeue_exit, buffer);
		safe_unpackstr(&build_ptr->requeue_exit_hold, buffer);

		safe_unpackstr(&build_ptr->resume_fail_program, buffer);
		safe_unpackstr(&build_ptr->resume_program, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr(&build_ptr->resv_epilog, buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr(&build_ptr->resv_prolog, buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr(&build_ptr->sched_params, buffer);
		safe_unpackstr(&build_ptr->sched_logfile, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr(&build_ptr->schedtype, buffer);
		safe_unpackstr(&build_ptr->scron_params, buffer);
		safe_unpackstr(&build_ptr->select_type, buffer);

		if (unpack_key_pair_list(&build_ptr->select_conf_key_pairs,
					 smsg->protocol_version,
					 buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr(&build_ptr->slurm_conf, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr(&build_ptr->slurm_user_name, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr(&build_ptr->slurmd_user_name, buffer);

		safe_unpackstr(&build_ptr->slurmctld_addr, buffer);
		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr(&build_ptr->slurmctld_logfile, buffer);
		safe_unpackstr(&build_ptr->slurmctld_params, buffer);
		safe_unpackstr(&build_ptr->slurmctld_pidfile, buffer);
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpackstr(&build_ptr->slurmctld_primary_off_prog, buffer);
		safe_unpackstr(&build_ptr->slurmctld_primary_on_prog, buffer);
		safe_unpack16(&build_ptr->slurmctld_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr(&build_ptr->slurmd_logfile, buffer);
		safe_unpackstr(&build_ptr->slurmd_params, buffer);
		safe_unpackstr(&build_ptr->slurmd_pidfile, buffer);
		safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr(&build_ptr->slurmd_spooldir, buffer);
		safe_unpack16(&build_ptr->slurmd_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr(&build_ptr->srun_epilog, buffer);

		build_ptr->srun_port_range = xcalloc(2, sizeof(uint16_t));
		safe_unpack16(&build_ptr->srun_port_range[0], buffer);
		safe_unpack16(&build_ptr->srun_port_range[1], buffer);

		safe_unpackstr(&build_ptr->srun_prolog, buffer);
		safe_unpackstr(&build_ptr->state_save_location, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_nodes, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_parts, buffer);
		safe_unpackstr(&build_ptr->suspend_exc_states, buffer);
		safe_unpackstr(&build_ptr->suspend_program, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr(&build_ptr->switch_param, buffer);
		safe_unpackstr(&build_ptr->switch_type, buffer);

		safe_unpackstr(&build_ptr->task_epilog, buffer);
		safe_unpackstr(&build_ptr->task_prolog, buffer);
		safe_unpackstr(&build_ptr->task_plugin, buffer);
		safe_unpack32(&build_ptr->task_plugin_param, buffer);
		safe_unpack16(&build_ptr->tcp_timeout, buffer);
		safe_unpackstr(&build_ptr->tls_type, buffer);
		safe_unpackstr(&build_ptr->tmp_fs, buffer);
		safe_unpackstr(&build_ptr->topology_param, buffer);
		safe_unpackstr(&build_ptr->topology_plugin, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpackstr(&build_ptr->unkillable_program, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr(&build_ptr->version, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);
		safe_unpackstr(&build_ptr->x11_params, buffer);
	}

	smsg->data = build_ptr;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_ctl_conf(build_ptr);
	return SLURM_ERROR;
}

static void _pack_sib_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	sib_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(msg->cluster_id, buffer);
		pack16(msg->data_type, buffer);
		pack16(msg->data_version, buffer);
		pack64(msg->fed_siblings, buffer);
		pack32(msg->group_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->return_code, buffer);
		pack_time(msg->start_time, buffer);
		packstr(msg->resp_host, buffer);
		pack32(msg->req_uid, buffer);
		pack16(msg->sib_msg_type, buffer);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->submit_host, buffer);
		pack16(msg->submit_proto_ver, buffer);
		pack32(msg->user_id, buffer);

		/* add already packed data_buffer to buffer */
		if (msg->data_buffer && size_buf(msg->data_buffer)) {
			buf_t *dbuf = msg->data_buffer;
			uint32_t grow_size =
				get_buf_offset(dbuf) - msg->data_offset;

			pack16(1, buffer);

			grow_buf(buffer, grow_size);
			memcpy(&buffer->head[get_buf_offset(buffer)],
			       &dbuf->head[msg->data_offset], grow_size);
			set_buf_offset(buffer,
				       get_buf_offset(buffer) + grow_size);
		} else {
			pack16(0, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->cluster_id, buffer);
		pack16(msg->data_type, buffer);
		pack16(msg->data_version, buffer);
		pack64(msg->fed_siblings, buffer);
		pack32(msg->group_id, buffer);
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->return_code, buffer);
		pack_time(msg->start_time, buffer);
		packstr(msg->resp_host, buffer);
		pack32(msg->req_uid, buffer);
		pack16(msg->sib_msg_type, buffer);
		packstr(msg->submit_host, buffer);
		pack16(msg->submit_proto_ver, buffer);
		pack32(msg->user_id, buffer);

		/* add already packed data_buffer to buffer */
		if (msg->data_buffer && size_buf(msg->data_buffer)) {
			buf_t *dbuf = msg->data_buffer;
			uint32_t grow_size =
				get_buf_offset(dbuf) - msg->data_offset;

			pack16(1, buffer);

			grow_buf(buffer, grow_size);
			memcpy(&buffer->head[get_buf_offset(buffer)],
			       &dbuf->head[msg->data_offset], grow_size);
			set_buf_offset(buffer,
				       get_buf_offset(buffer) + grow_size);
		} else {
			pack16(0, buffer);
		}
	}
}

static int _unpack_sib_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint16_t tmp_uint16;
	sib_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->cluster_id, buffer);
		safe_unpack16(&msg->data_type, buffer);
		safe_unpack16(&msg->data_version, buffer);
		safe_unpack64(&msg->fed_siblings, buffer);
		safe_unpack32(&msg->group_id, buffer);
		safe_unpack32(&msg->job_state, buffer);
		safe_unpack32(&msg->return_code, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpackstr(&msg->resp_host, buffer);
		safe_unpack32(&msg->req_uid, buffer);
		safe_unpack16(&msg->sib_msg_type, buffer);
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->submit_host, buffer);
		safe_unpack16(&msg->submit_proto_ver, buffer);
		safe_unpack32(&msg->user_id, buffer);

		safe_unpack16(&tmp_uint16, buffer);
		if (tmp_uint16) {
			slurm_msg_t tmp_msg;
			slurm_msg_t_init(&tmp_msg);
			tmp_msg.msg_type = msg->data_type;
			tmp_msg.protocol_version = msg->data_version;

			if (unpack_msg(&tmp_msg, buffer))
				goto unpack_error;

			msg->data = tmp_msg.data;
			tmp_msg.data = NULL;
			slurm_free_msg_members(&tmp_msg);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->cluster_id, buffer);
		safe_unpack16(&msg->data_type, buffer);
		safe_unpack16(&msg->data_version, buffer);
		safe_unpack64(&msg->fed_siblings, buffer);
		safe_unpack32(&msg->group_id, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->job_state, buffer);
		safe_unpack32(&msg->return_code, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpackstr(&msg->resp_host, buffer);
		safe_unpack32(&msg->req_uid, buffer);
		safe_unpack16(&msg->sib_msg_type, buffer);
		safe_unpackstr(&msg->submit_host, buffer);
		safe_unpack16(&msg->submit_proto_ver, buffer);
		safe_unpack32(&msg->user_id, buffer);

		safe_unpack16(&tmp_uint16, buffer);
		if (tmp_uint16) {
			slurm_msg_t tmp_msg;
			slurm_msg_t_init(&tmp_msg);
			tmp_msg.msg_type = msg->data_type;
			tmp_msg.protocol_version = msg->data_version;

			if (unpack_msg(&tmp_msg, buffer))
				goto unpack_error;

			msg->data = tmp_msg.data;
			tmp_msg.data = NULL;
			slurm_free_msg_members(&tmp_msg);
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_sib_msg(msg);
	return SLURM_ERROR;
}

/*
 * If this changes, then _pack_remote_dep_job() in fed_mgr.c probably
 * needs to change.
 */
static void _pack_dep_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	dep_msg_t *dep_msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(dep_msg->array_job_id, buffer);
		pack32(dep_msg->array_task_id, buffer);
		packstr(dep_msg->dependency, buffer);
		packbool(dep_msg->is_array, buffer);
		packstr(dep_msg->job_name, buffer);
		pack_step_id(&dep_msg->step_id, buffer, smsg->protocol_version);
		pack32(dep_msg->user_id, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(dep_msg->array_job_id, buffer);
		pack32(dep_msg->array_task_id, buffer);
		packstr(dep_msg->dependency, buffer);
		packbool(dep_msg->is_array, buffer);
		pack32(dep_msg->step_id.job_id, buffer);
		packstr(dep_msg->job_name, buffer);
		pack32(dep_msg->user_id, buffer);
	}
}

/*
 * If this changes, then _unpack_remote_dep_job() in fed_mgr.c probably
 * needs to change.
 */
static int _unpack_dep_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	dep_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->array_job_id, buffer);
		safe_unpack32(&msg->array_task_id, buffer);
		safe_unpackstr(&msg->dependency, buffer);
		safe_unpackbool(&msg->is_array, buffer);
		safe_unpackstr(&msg->job_name, buffer);
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->user_id, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->array_job_id, buffer);
		safe_unpack32(&msg->array_task_id, buffer);
		safe_unpackstr(&msg->dependency, buffer);
		safe_unpackbool(&msg->is_array, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->job_name, buffer);
		safe_unpack32(&msg->user_id, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_dep_msg(msg);
	return SLURM_ERROR;
}

extern void pack_dep_list(list_t *dep_list, buf_t *buffer,
			  uint16_t protocol_version)
{
	uint32_t cnt;
	depend_spec_t *dep_ptr;
	list_itr_t *itr;

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		cnt = list_count(dep_list);
		pack32(cnt, buffer);
		if (!cnt)
			return;

		itr = list_iterator_create(dep_list);
		while ((dep_ptr = list_next(itr))) {
			pack32(dep_ptr->array_task_id, buffer);
			pack16(dep_ptr->depend_type, buffer);
			pack16(dep_ptr->depend_flags, buffer);
			pack32(dep_ptr->depend_state, buffer);
			pack32(dep_ptr->depend_time, buffer);
			pack32(dep_ptr->job_id, buffer);
			pack32(dep_ptr->parsed_array_task_id, buffer);
			pack64(dep_ptr->singleton_bits, buffer);
		}
		list_iterator_destroy(itr);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		cnt = list_count(dep_list);
		pack32(cnt, buffer);
		if (!cnt)
			return;

		itr = list_iterator_create(dep_list);
		while ((dep_ptr = list_next(itr))) {
			pack32(dep_ptr->array_task_id, buffer);
			pack16(dep_ptr->depend_type, buffer);
			pack16(dep_ptr->depend_flags, buffer);
			pack32(dep_ptr->depend_state, buffer);
			pack32(dep_ptr->depend_time, buffer);
			pack32(dep_ptr->job_id, buffer);
			pack64(dep_ptr->singleton_bits, buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int unpack_dep_list(list_t **dep_list, buf_t *buffer,
			   uint16_t protocol_version)
{
	uint32_t cnt;
	depend_spec_t *dep_ptr;

	xassert(dep_list);

	*dep_list = NULL;
	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&cnt, buffer);
		if (!cnt)
			return SLURM_SUCCESS;

		*dep_list = list_create(xfree_ptr);
		for (int i = 0; i < cnt; i++) {
			dep_ptr = xmalloc(sizeof *dep_ptr);
			list_push(*dep_list, dep_ptr);

			safe_unpack32(&dep_ptr->array_task_id, buffer);
			safe_unpack16(&dep_ptr->depend_type, buffer);
			safe_unpack16(&dep_ptr->depend_flags, buffer);
			safe_unpack32(&dep_ptr->depend_state, buffer);
			safe_unpack32(&dep_ptr->depend_time, buffer);
			safe_unpack32(&dep_ptr->job_id, buffer);
			safe_unpack32(&dep_ptr->parsed_array_task_id, buffer);
			safe_unpack64(&dep_ptr->singleton_bits, buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&cnt, buffer);
		if (!cnt)
			return SLURM_SUCCESS;

		*dep_list = list_create(xfree_ptr);
		for (int i = 0; i < cnt; i++) {
			dep_ptr = xmalloc(sizeof *dep_ptr);
			list_push(*dep_list, dep_ptr);

			safe_unpack32(&dep_ptr->array_task_id, buffer);
			safe_unpack16(&dep_ptr->depend_type, buffer);
			safe_unpack16(&dep_ptr->depend_flags, buffer);
			safe_unpack32(&dep_ptr->depend_state, buffer);
			safe_unpack32(&dep_ptr->depend_time, buffer);
			safe_unpack32(&dep_ptr->job_id, buffer);
			safe_unpack64(&dep_ptr->singleton_bits, buffer);
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*dep_list);
	return SLURM_ERROR;
}

static void _pack_dep_update_origin_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	dep_update_origin_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_dep_list(msg->depend_list, buffer, smsg->protocol_version);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_dep_list(msg->depend_list, buffer, smsg->protocol_version);
		pack32(msg->step_id.job_id, buffer);
	}
}

static int _unpack_dep_update_origin_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	dep_update_origin_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		if (unpack_dep_list(&msg->depend_list, buffer,
				    smsg->protocol_version))
			goto unpack_error;
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		if (unpack_dep_list(&msg->depend_list, buffer,
				    smsg->protocol_version))
			goto unpack_error;
		safe_unpack32(&msg->step_id.job_id, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_dep_update_origin_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_desc_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_desc_msg_t *msg = smsg->data;

	if (msg->script_buf) {
		buf_t *buf = (buf_t *) msg->script_buf;
		msg->script = buf->head;
	}

	/* Set bitflags saying we did or didn't request the below */
	if (!msg->account)
		msg->bitflags |= USE_DEFAULT_ACCT;
	if (!msg->partition)
		msg->bitflags |= USE_DEFAULT_PART;
	if (!msg->qos)
		msg->bitflags |= USE_DEFAULT_QOS;
	if (!msg->wckey)
		msg->bitflags |= USE_DEFAULT_WCKEY;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->site_factor, buffer);
		packstr(msg->batch_features, buffer);
		packstr(msg->cluster_features, buffer);
		packstr(msg->clusters, buffer);
		pack16(msg->contiguous, buffer);
		packstr(msg->container, buffer);
		packstr(msg->container_id, buffer);
		pack16(msg->core_spec, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->kill_on_node_fail, buffer);
		packstr(msg->features, buffer);
		pack64(msg->fed_siblings_active, buffer);
		pack64(msg->fed_siblings_viable, buffer);
		packstr(msg->job_id_str, buffer);
		packstr(msg->name, buffer);

		packstr(msg->alloc_node, buffer);
		pack32(msg->alloc_sid, buffer);
		packstr(msg->array_inx, buffer);
		packstr(msg->burst_buffer, buffer);
		pack16(msg->pn_min_cpus, buffer);
		pack64(msg->pn_min_memory, buffer);
		pack16(msg->oom_kill_step, buffer);
		pack32(msg->pn_min_tmp_disk, buffer);
		packstr(msg->prefer, buffer);

		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->priority, buffer);
		packstr(msg->dependency, buffer);
		packstr(msg->account, buffer);
		packstr(msg->admin_comment, buffer);
		packstr(msg->comment, buffer);
		pack32(msg->nice, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->mcs_label, buffer);

		packstr(msg->origin_cluster, buffer);
		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->num_tasks, buffer);

		packstr(msg->req_context, buffer);
		packstr(msg->req_nodes, buffer);
		packstr(msg->exc_nodes, buffer);
		packstr_array(msg->environment, msg->env_size, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->script, buffer);
		packstr_array(msg->argv, msg->argc, buffer);

		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);
		packstr(msg->submit_line, buffer);
		packstr(msg->work_dir, buffer);

		pack16(msg->immediate, buffer);
		pack16(msg->reboot, buffer);
		pack16(msg->requeue, buffer);
		pack16(msg->shared, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->ntasks_per_node, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);

		pack16(msg->plane_size, buffer);
		pack16(msg->cpu_bind_type, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		packstr(msg->mem_bind, buffer);

		pack32(msg->time_limit, buffer);
		pack32(msg->time_min, buffer);
		pack32(msg->min_cpus, buffer);
		pack32(msg->max_cpus, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		packstr(msg->job_size_str, buffer);
		pack16(msg->boards_per_node, buffer);
		pack16(msg->sockets_per_board, buffer);
		pack16(msg->sockets_per_node, buffer);
		pack16(msg->cores_per_socket, buffer);
		pack16(msg->threads_per_core, buffer);
		pack32(msg->user_id, buffer);
		pack32(msg->group_id, buffer);

		pack16(msg->alloc_resp_port, buffer);
		packstr(msg->alloc_tls_cert, buffer);
		packstr(msg->resp_host, buffer);
		pack16(msg->other_port, buffer);
		pack16(msg->resv_port_cnt, buffer);
		packstr(msg->network, buffer);
		pack_time(msg->begin_time, buffer);
		pack_time(msg->end_time, buffer);
		pack_time(msg->deadline, buffer);

		packstr(msg->licenses, buffer);
		pack16(msg->mail_type, buffer);
		packstr(msg->mail_user, buffer);
		packstr(msg->reservation, buffer);
		pack16(msg->restart_cnt, buffer);
		pack16(msg->warn_flags, buffer);
		pack16(msg->warn_signal, buffer);
		pack16(msg->warn_time, buffer);
		packstr(msg->wckey, buffer);
		pack32(msg->req_switch, buffer);
		pack32(msg->wait4switch, buffer);

		pack16(msg->wait_all_nodes, buffer);
		pack64(msg->bitflags, buffer);
		pack32(msg->delay_boot, buffer);
		packstr(msg->extra, buffer);
		pack16(msg->x11, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr(msg->cpus_per_tres, buffer);
		packstr(msg->mem_per_tres, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_job, buffer);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_socket, buffer);
		packstr(msg->tres_per_task, buffer);
		pack_cron_entry(msg->crontab_entry, smsg->protocol_version,
				buffer);
		pack16(msg->segment_size, buffer);
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		pack32(msg->site_factor, buffer);
		packstr(msg->batch_features, buffer);
		packstr(msg->cluster_features, buffer);
		packstr(msg->clusters, buffer);
		pack16(msg->contiguous, buffer);
		packstr(msg->container, buffer);
		packstr(msg->container_id, buffer);
		pack16(msg->core_spec, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->kill_on_node_fail, buffer);
		packstr(msg->features, buffer);
		pack64(msg->fed_siblings_active, buffer);
		pack64(msg->fed_siblings_viable, buffer);
		pack32(msg->step_id.job_id, buffer);
		packstr(msg->job_id_str, buffer);
		packstr(msg->name, buffer);

		packstr(msg->alloc_node, buffer);
		pack32(msg->alloc_sid, buffer);
		packstr(msg->array_inx, buffer);
		packstr(msg->burst_buffer, buffer);
		pack16(msg->pn_min_cpus, buffer);
		pack64(msg->pn_min_memory, buffer);
		pack16(msg->oom_kill_step, buffer);
		pack32(msg->pn_min_tmp_disk, buffer);
		packstr(msg->prefer, buffer);

		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->priority, buffer);
		packstr(msg->dependency, buffer);
		packstr(msg->account, buffer);
		packstr(msg->admin_comment, buffer);
		packstr(msg->comment, buffer);
		pack32(msg->nice, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->mcs_label, buffer);

		packstr(msg->origin_cluster, buffer);
		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->num_tasks, buffer);

		packstr(msg->req_context, buffer);
		packstr(msg->req_nodes, buffer);
		packstr(msg->exc_nodes, buffer);
		packstr_array(msg->environment, msg->env_size, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->script, buffer);
		packstr_array(msg->argv, msg->argc, buffer);

		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);
		packstr(msg->submit_line, buffer);
		packstr(msg->work_dir, buffer);

		pack16(msg->immediate, buffer);
		pack16(msg->reboot, buffer);
		pack16(msg->requeue, buffer);
		pack16(msg->shared, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->ntasks_per_node, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);

		pack16(msg->plane_size, buffer);
		pack16(msg->cpu_bind_type, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		packstr(msg->mem_bind, buffer);

		pack32(msg->time_limit, buffer);
		pack32(msg->time_min, buffer);
		pack32(msg->min_cpus, buffer);
		pack32(msg->max_cpus, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		packstr(msg->job_size_str, buffer);
		pack16(msg->boards_per_node, buffer);
		pack16(msg->sockets_per_board, buffer);
		pack16(msg->sockets_per_node, buffer);
		pack16(msg->cores_per_socket, buffer);
		pack16(msg->threads_per_core, buffer);
		pack32(msg->user_id, buffer);
		pack32(msg->group_id, buffer);

		pack16(msg->alloc_resp_port, buffer);
		packstr(msg->alloc_tls_cert, buffer);
		packstr(msg->resp_host, buffer);
		pack16(msg->other_port, buffer);
		pack16(msg->resv_port_cnt, buffer);
		packstr(msg->network, buffer);
		pack_time(msg->begin_time, buffer);
		pack_time(msg->end_time, buffer);
		pack_time(msg->deadline, buffer);

		packstr(msg->licenses, buffer);
		pack16(msg->mail_type, buffer);
		packstr(msg->mail_user, buffer);
		packstr(msg->reservation, buffer);
		pack16(msg->restart_cnt, buffer);
		pack16(msg->warn_flags, buffer);
		pack16(msg->warn_signal, buffer);
		pack16(msg->warn_time, buffer);
		packstr(msg->wckey, buffer);
		pack32(msg->req_switch, buffer);
		pack32(msg->wait4switch, buffer);

		pack16(msg->wait_all_nodes, buffer);
		pack64(msg->bitflags, buffer);
		pack32(msg->delay_boot, buffer);
		packstr(msg->extra, buffer);
		pack16(msg->x11, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr(msg->cpus_per_tres, buffer);
		packstr(msg->mem_per_tres, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_job, buffer);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_socket, buffer);
		packstr(msg->tres_per_task, buffer);
		pack_cron_entry(msg->crontab_entry, smsg->protocol_version,
				buffer);
		pack16(msg->segment_size, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->site_factor, buffer);
		packstr(msg->batch_features, buffer);
		packstr(msg->cluster_features, buffer);
		packstr(msg->clusters, buffer);
		pack16(msg->contiguous, buffer);
		packstr(msg->container, buffer);
		packstr(msg->container_id, buffer);
		pack16(msg->core_spec, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->kill_on_node_fail, buffer);
		packstr(msg->features, buffer);
		pack64(msg->fed_siblings_active, buffer);
		pack64(msg->fed_siblings_viable, buffer);
		pack32(msg->step_id.job_id, buffer);
		packstr(msg->job_id_str, buffer);
		packstr(msg->name, buffer);

		packstr(msg->alloc_node, buffer);
		pack32(msg->alloc_sid, buffer);
		packstr(msg->array_inx, buffer);
		packstr(msg->burst_buffer, buffer);
		pack16(msg->pn_min_cpus, buffer);
		pack64(msg->pn_min_memory, buffer);
		pack16(msg->oom_kill_step, buffer);
		pack32(msg->pn_min_tmp_disk, buffer);
		packstr(msg->prefer, buffer);

		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->priority, buffer);
		packstr(msg->dependency, buffer);
		packstr(msg->account, buffer);
		packstr(msg->admin_comment, buffer);
		packstr(msg->comment, buffer);
		pack32(msg->nice, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->mcs_label, buffer);

		packstr(msg->origin_cluster, buffer);
		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->num_tasks, buffer);

		packstr(msg->req_context, buffer);
		packstr(msg->req_nodes, buffer);
		packstr(msg->exc_nodes, buffer);
		packstr_array(msg->environment, msg->env_size, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->script, buffer);
		packstr_array(msg->argv, msg->argc, buffer);

		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);
		packstr(msg->submit_line, buffer);
		packstr(msg->work_dir, buffer);

		pack16(msg->immediate, buffer);
		pack16(msg->reboot, buffer);
		pack16(msg->requeue, buffer);
		pack16(msg->shared, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->ntasks_per_node, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);

		pack16(msg->plane_size, buffer);
		pack16(msg->cpu_bind_type, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		packstr(msg->mem_bind, buffer);

		pack32(msg->time_limit, buffer);
		pack32(msg->time_min, buffer);
		pack32(msg->min_cpus, buffer);
		pack32(msg->max_cpus, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		packstr(msg->job_size_str, buffer);
		pack16(msg->boards_per_node, buffer);
		pack16(msg->sockets_per_board, buffer);
		pack16(msg->sockets_per_node, buffer);
		pack16(msg->cores_per_socket, buffer);
		pack16(msg->threads_per_core, buffer);
		pack32(msg->user_id, buffer);
		pack32(msg->group_id, buffer);

		pack16(msg->alloc_resp_port, buffer);
		packstr(msg->resp_host, buffer);
		pack16(msg->other_port, buffer);
		pack16(msg->resv_port_cnt, buffer);
		packstr(msg->network, buffer);
		pack_time(msg->begin_time, buffer);
		pack_time(msg->end_time, buffer);
		pack_time(msg->deadline, buffer);

		packstr(msg->licenses, buffer);
		pack16(msg->mail_type, buffer);
		packstr(msg->mail_user, buffer);
		packstr(msg->reservation, buffer);
		pack16(msg->restart_cnt, buffer);
		pack16(msg->warn_flags, buffer);
		pack16(msg->warn_signal, buffer);
		pack16(msg->warn_time, buffer);
		packstr(msg->wckey, buffer);
		pack32(msg->req_switch, buffer);
		pack32(msg->wait4switch, buffer);

		pack16(msg->wait_all_nodes, buffer);
		pack64(msg->bitflags, buffer);
		pack32(msg->delay_boot, buffer);
		packstr(msg->extra, buffer);
		pack16(msg->x11, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr(msg->cpus_per_tres, buffer);
		packstr(msg->mem_per_tres, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_job, buffer);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_socket, buffer);
		packstr(msg->tres_per_task, buffer);
		pack_cron_entry(msg->crontab_entry, smsg->protocol_version,
				buffer);
		pack16(msg->segment_size, buffer);
	}

	if (msg->script_buf)
		msg->script = NULL;
}

static int _unpack_job_desc_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t start, script_len;
	job_desc_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->site_factor, buffer);
		safe_unpackstr(&msg->batch_features, buffer);
		safe_unpackstr(&msg->cluster_features, buffer);
		safe_unpackstr(&msg->clusters, buffer);
		safe_unpack16(&msg->contiguous, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpackstr(&msg->container_id, buffer);
		safe_unpack16(&msg->core_spec, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->kill_on_node_fail, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpack64(&msg->fed_siblings_active, buffer);
		safe_unpack64(&msg->fed_siblings_viable, buffer);
		safe_unpackstr(&msg->job_id_str, buffer);
		safe_unpackstr(&msg->name, buffer);

		safe_unpackstr(&msg->alloc_node, buffer);
		safe_unpack32(&msg->alloc_sid, buffer);
		safe_unpackstr(&msg->array_inx, buffer);
		safe_unpackstr(&msg->burst_buffer, buffer);
		safe_unpack16(&msg->pn_min_cpus, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);
		safe_unpack16(&msg->oom_kill_step, buffer);
		safe_unpack32(&msg->pn_min_tmp_disk, buffer);

		safe_unpackstr(&msg->prefer, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);

		safe_unpackstr(&msg->partition, buffer);
		safe_unpack32(&msg->priority, buffer);
		safe_unpackstr(&msg->dependency, buffer);
		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->admin_comment, buffer);
		safe_unpackstr(&msg->comment, buffer);
		safe_unpack32(&msg->nice, buffer);
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->mcs_label, buffer);

		safe_unpackstr(&msg->origin_cluster, buffer);
		safe_unpack8(&msg->open_mode, buffer);
		safe_unpack8(&msg->overcommit, buffer);
		safe_unpackstr(&msg->acctg_freq, buffer);
		safe_unpack32(&msg->num_tasks, buffer);

		safe_unpackstr(&msg->req_context, buffer);
		safe_unpackstr(&msg->req_nodes, buffer);
		safe_unpackstr(&msg->exc_nodes, buffer);
		start = buffer->processed;
		safe_unpackstr_array(&msg->environment, &msg->env_size, buffer);

		if (msg->env_size) {
			msg->env_hash.type = HASH_PLUGIN_K12;
			(void) hash_g_compute(&buffer->head[start],
					      buffer->processed - start, NULL,
					      0, &msg->env_hash);
		}

		if (envcount(msg->environment) != msg->env_size)
			goto unpack_error;
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		if (envcount(msg->spank_job_env) != msg->spank_job_env_size)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->script, &script_len, buffer);

		msg->script_hash.type = HASH_PLUGIN_K12;
		(void) hash_g_compute(msg->script, script_len, NULL, 0,
				      &msg->script_hash);

		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);

		safe_unpackstr(&msg->std_err, buffer);
		safe_unpackstr(&msg->std_in, buffer);
		safe_unpackstr(&msg->std_out, buffer);
		safe_unpackstr(&msg->submit_line, buffer);
		safe_unpackstr(&msg->work_dir, buffer);

		safe_unpack16(&msg->immediate, buffer);
		safe_unpack16(&msg->reboot, buffer);
		safe_unpack16(&msg->requeue, buffer);
		safe_unpack16(&msg->shared, buffer);
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->ntasks_per_node, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);

		safe_unpack16(&msg->plane_size, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr(&msg->cpu_bind, buffer);
		safe_unpackstr(&msg->mem_bind, buffer);

		safe_unpack32(&msg->time_limit, buffer);
		safe_unpack32(&msg->time_min, buffer);
		safe_unpack32(&msg->min_cpus, buffer);
		safe_unpack32(&msg->max_cpus, buffer);
		safe_unpack32(&msg->min_nodes, buffer);
		safe_unpack32(&msg->max_nodes, buffer);
		safe_unpackstr(&msg->job_size_str, buffer);
		safe_unpack16(&msg->boards_per_node, buffer);
		safe_unpack16(&msg->sockets_per_board, buffer);
		safe_unpack16(&msg->sockets_per_node, buffer);
		safe_unpack16(&msg->cores_per_socket, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpack32(&msg->group_id, buffer);

		safe_unpack16(&msg->alloc_resp_port, buffer);
		safe_unpackstr(&msg->alloc_tls_cert, buffer);
		safe_unpackstr(&msg->resp_host, buffer);
		safe_unpack16(&msg->other_port, buffer);
		safe_unpack16(&msg->resv_port_cnt, buffer);
		safe_unpackstr(&msg->network, buffer);
		safe_unpack_time(&msg->begin_time, buffer);
		safe_unpack_time(&msg->end_time, buffer);
		safe_unpack_time(&msg->deadline, buffer);

		safe_unpackstr(&msg->licenses, buffer);
		safe_unpack16(&msg->mail_type, buffer);
		safe_unpackstr(&msg->mail_user, buffer);
		safe_unpackstr(&msg->reservation, buffer);
		safe_unpack16(&msg->restart_cnt, buffer);
		safe_unpack16(&msg->warn_flags, buffer);
		safe_unpack16(&msg->warn_signal, buffer);
		safe_unpack16(&msg->warn_time, buffer);
		safe_unpackstr(&msg->wckey, buffer);
		safe_unpack32(&msg->req_switch, buffer);
		safe_unpack32(&msg->wait4switch, buffer);

		safe_unpack16(&msg->wait_all_nodes, buffer);
		safe_unpack64(&msg->bitflags, buffer);
		safe_unpack32(&msg->delay_boot, buffer);
		safe_unpackstr(&msg->extra, buffer);
		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr(&msg->x11_magic_cookie, buffer);
		safe_unpackstr(&msg->x11_target, buffer);
		safe_unpack16(&msg->x11_target_port, buffer);

		safe_unpackstr(&msg->cpus_per_tres, buffer);
		slurm_format_tres_string(&msg->cpus_per_tres, "gres");

		safe_unpackstr(&msg->mem_per_tres, buffer);
		slurm_format_tres_string(&msg->mem_per_tres, "gres");

		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpackstr(&msg->tres_per_job, buffer);
		slurm_format_tres_string(&msg->tres_per_job, "gres");

		safe_unpackstr(&msg->tres_per_node, buffer);
		slurm_format_tres_string(&msg->tres_per_node, "gres");

		safe_unpackstr(&msg->tres_per_socket, buffer);
		slurm_format_tres_string(&msg->tres_per_socket, "gres");

		safe_unpackstr(&msg->tres_per_task, buffer);
		slurm_format_tres_string(&msg->tres_per_task, "gres");

		if (unpack_cron_entry(&msg->crontab_entry,
				      smsg->protocol_version, buffer))
			goto unpack_error;
		safe_unpack16(&msg->segment_size, buffer);
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->site_factor, buffer);
		safe_unpackstr(&msg->batch_features, buffer);
		safe_unpackstr(&msg->cluster_features, buffer);
		safe_unpackstr(&msg->clusters, buffer);
		safe_unpack16(&msg->contiguous, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpackstr(&msg->container_id, buffer);
		safe_unpack16(&msg->core_spec, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->kill_on_node_fail, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpack64(&msg->fed_siblings_active, buffer);
		safe_unpack64(&msg->fed_siblings_viable, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->job_id_str, buffer);
		safe_unpackstr(&msg->name, buffer);

		safe_unpackstr(&msg->alloc_node, buffer);
		safe_unpack32(&msg->alloc_sid, buffer);
		safe_unpackstr(&msg->array_inx, buffer);
		safe_unpackstr(&msg->burst_buffer, buffer);
		safe_unpack16(&msg->pn_min_cpus, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);
		safe_unpack16(&msg->oom_kill_step, buffer);
		safe_unpack32(&msg->pn_min_tmp_disk, buffer);

		safe_unpackstr(&msg->prefer, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);

		safe_unpackstr(&msg->partition, buffer);
		safe_unpack32(&msg->priority, buffer);
		safe_unpackstr(&msg->dependency, buffer);
		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->admin_comment, buffer);
		safe_unpackstr(&msg->comment, buffer);
		safe_unpack32(&msg->nice, buffer);
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->mcs_label, buffer);

		safe_unpackstr(&msg->origin_cluster, buffer);
		safe_unpack8(&msg->open_mode, buffer);
		safe_unpack8(&msg->overcommit, buffer);
		safe_unpackstr(&msg->acctg_freq, buffer);
		safe_unpack32(&msg->num_tasks, buffer);

		safe_unpackstr(&msg->req_context, buffer);
		safe_unpackstr(&msg->req_nodes, buffer);
		safe_unpackstr(&msg->exc_nodes, buffer);
		start = buffer->processed;
		safe_unpackstr_array(&msg->environment, &msg->env_size, buffer);

		if (msg->env_size) {
			msg->env_hash.type = HASH_PLUGIN_K12;
			(void) hash_g_compute(&buffer->head[start],
					      buffer->processed - start, NULL,
					      0, &msg->env_hash);
		}

		if (envcount(msg->environment) != msg->env_size)
			goto unpack_error;
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		if (envcount(msg->spank_job_env) != msg->spank_job_env_size)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->script, &script_len, buffer);

		msg->script_hash.type = HASH_PLUGIN_K12;
		(void) hash_g_compute(msg->script, script_len, NULL, 0,
				      &msg->script_hash);

		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);

		safe_unpackstr(&msg->std_err, buffer);
		safe_unpackstr(&msg->std_in, buffer);
		safe_unpackstr(&msg->std_out, buffer);
		safe_unpackstr(&msg->submit_line, buffer);
		safe_unpackstr(&msg->work_dir, buffer);

		safe_unpack16(&msg->immediate, buffer);
		safe_unpack16(&msg->reboot, buffer);
		safe_unpack16(&msg->requeue, buffer);
		safe_unpack16(&msg->shared, buffer);
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->ntasks_per_node, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);

		safe_unpack16(&msg->plane_size, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr(&msg->cpu_bind, buffer);
		safe_unpackstr(&msg->mem_bind, buffer);

		safe_unpack32(&msg->time_limit, buffer);
		safe_unpack32(&msg->time_min, buffer);
		safe_unpack32(&msg->min_cpus, buffer);
		safe_unpack32(&msg->max_cpus, buffer);
		safe_unpack32(&msg->min_nodes, buffer);
		safe_unpack32(&msg->max_nodes, buffer);
		safe_unpackstr(&msg->job_size_str, buffer);
		safe_unpack16(&msg->boards_per_node, buffer);
		safe_unpack16(&msg->sockets_per_board, buffer);
		safe_unpack16(&msg->sockets_per_node, buffer);
		safe_unpack16(&msg->cores_per_socket, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpack32(&msg->group_id, buffer);

		safe_unpack16(&msg->alloc_resp_port, buffer);
		safe_unpackstr(&msg->alloc_tls_cert, buffer);
		safe_unpackstr(&msg->resp_host, buffer);
		safe_unpack16(&msg->other_port, buffer);
		safe_unpack16(&msg->resv_port_cnt, buffer);
		safe_unpackstr(&msg->network, buffer);
		safe_unpack_time(&msg->begin_time, buffer);
		safe_unpack_time(&msg->end_time, buffer);
		safe_unpack_time(&msg->deadline, buffer);

		safe_unpackstr(&msg->licenses, buffer);
		safe_unpack16(&msg->mail_type, buffer);
		safe_unpackstr(&msg->mail_user, buffer);
		safe_unpackstr(&msg->reservation, buffer);
		safe_unpack16(&msg->restart_cnt, buffer);
		safe_unpack16(&msg->warn_flags, buffer);
		safe_unpack16(&msg->warn_signal, buffer);
		safe_unpack16(&msg->warn_time, buffer);
		safe_unpackstr(&msg->wckey, buffer);
		safe_unpack32(&msg->req_switch, buffer);
		safe_unpack32(&msg->wait4switch, buffer);

		safe_unpack16(&msg->wait_all_nodes, buffer);
		safe_unpack64(&msg->bitflags, buffer);
		safe_unpack32(&msg->delay_boot, buffer);
		safe_unpackstr(&msg->extra, buffer);
		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr(&msg->x11_magic_cookie, buffer);
		safe_unpackstr(&msg->x11_target, buffer);
		safe_unpack16(&msg->x11_target_port, buffer);

		safe_unpackstr(&msg->cpus_per_tres, buffer);
		slurm_format_tres_string(&msg->cpus_per_tres, "gres");

		safe_unpackstr(&msg->mem_per_tres, buffer);
		slurm_format_tres_string(&msg->mem_per_tres, "gres");

		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpackstr(&msg->tres_per_job, buffer);
		slurm_format_tres_string(&msg->tres_per_job, "gres");

		safe_unpackstr(&msg->tres_per_node, buffer);
		slurm_format_tres_string(&msg->tres_per_node, "gres");

		safe_unpackstr(&msg->tres_per_socket, buffer);
		slurm_format_tres_string(&msg->tres_per_socket, "gres");

		safe_unpackstr(&msg->tres_per_task, buffer);
		slurm_format_tres_string(&msg->tres_per_task, "gres");

		if (unpack_cron_entry(&msg->crontab_entry,
				      smsg->protocol_version, buffer))
			goto unpack_error;
		safe_unpack16(&msg->segment_size, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->site_factor, buffer);
		safe_unpackstr(&msg->batch_features, buffer);
		safe_unpackstr(&msg->cluster_features, buffer);
		safe_unpackstr(&msg->clusters, buffer);
		safe_unpack16(&msg->contiguous, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpackstr(&msg->container_id, buffer);
		safe_unpack16(&msg->core_spec, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->kill_on_node_fail, buffer);
		safe_unpackstr(&msg->features, buffer);
		safe_unpack64(&msg->fed_siblings_active, buffer);
		safe_unpack64(&msg->fed_siblings_viable, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->job_id_str, buffer);
		safe_unpackstr(&msg->name, buffer);

		safe_unpackstr(&msg->alloc_node, buffer);
		safe_unpack32(&msg->alloc_sid, buffer);
		safe_unpackstr(&msg->array_inx, buffer);
		safe_unpackstr(&msg->burst_buffer, buffer);
		safe_unpack16(&msg->pn_min_cpus, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);
		safe_unpack16(&msg->oom_kill_step, buffer);
		safe_unpack32(&msg->pn_min_tmp_disk, buffer);

		safe_unpackstr(&msg->prefer, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);

		safe_unpackstr(&msg->partition, buffer);
		safe_unpack32(&msg->priority, buffer);
		safe_unpackstr(&msg->dependency, buffer);
		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->admin_comment, buffer);
		safe_unpackstr(&msg->comment, buffer);
		safe_unpack32(&msg->nice, buffer);
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->mcs_label, buffer);

		safe_unpackstr(&msg->origin_cluster, buffer);
		safe_unpack8(&msg->open_mode, buffer);
		safe_unpack8(&msg->overcommit, buffer);
		safe_unpackstr(&msg->acctg_freq, buffer);
		safe_unpack32(&msg->num_tasks, buffer);

		safe_unpackstr(&msg->req_context, buffer);
		safe_unpackstr(&msg->req_nodes, buffer);
		safe_unpackstr(&msg->exc_nodes, buffer);
		start = buffer->processed;
		safe_unpackstr_array(&msg->environment, &msg->env_size, buffer);

		if (msg->env_size) {
			msg->env_hash.type = HASH_PLUGIN_K12;
			(void) hash_g_compute(&buffer->head[start],
					      buffer->processed - start, NULL,
					      0, &msg->env_hash);
		}

		if (envcount(msg->environment) != msg->env_size)
			goto unpack_error;
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		if (envcount(msg->spank_job_env) != msg->spank_job_env_size)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->script, &script_len, buffer);

		msg->script_hash.type = HASH_PLUGIN_K12;
		(void) hash_g_compute(msg->script, script_len, NULL, 0,
				      &msg->script_hash);

		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);

		safe_unpackstr(&msg->std_err, buffer);
		safe_unpackstr(&msg->std_in, buffer);
		safe_unpackstr(&msg->std_out, buffer);
		safe_unpackstr(&msg->submit_line, buffer);
		safe_unpackstr(&msg->work_dir, buffer);

		safe_unpack16(&msg->immediate, buffer);
		safe_unpack16(&msg->reboot, buffer);
		safe_unpack16(&msg->requeue, buffer);
		safe_unpack16(&msg->shared, buffer);
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->ntasks_per_node, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);

		safe_unpack16(&msg->plane_size, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr(&msg->cpu_bind, buffer);
		safe_unpackstr(&msg->mem_bind, buffer);

		safe_unpack32(&msg->time_limit, buffer);
		safe_unpack32(&msg->time_min, buffer);
		safe_unpack32(&msg->min_cpus, buffer);
		safe_unpack32(&msg->max_cpus, buffer);
		safe_unpack32(&msg->min_nodes, buffer);
		safe_unpack32(&msg->max_nodes, buffer);
		safe_unpackstr(&msg->job_size_str, buffer);
		safe_unpack16(&msg->boards_per_node, buffer);
		safe_unpack16(&msg->sockets_per_board, buffer);
		safe_unpack16(&msg->sockets_per_node, buffer);
		safe_unpack16(&msg->cores_per_socket, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpack32(&msg->group_id, buffer);

		safe_unpack16(&msg->alloc_resp_port, buffer);
		safe_unpackstr(&msg->resp_host, buffer);
		safe_unpack16(&msg->other_port, buffer);
		safe_unpack16(&msg->resv_port_cnt, buffer);
		safe_unpackstr(&msg->network, buffer);
		safe_unpack_time(&msg->begin_time, buffer);
		safe_unpack_time(&msg->end_time, buffer);
		safe_unpack_time(&msg->deadline, buffer);

		safe_unpackstr(&msg->licenses, buffer);
		safe_unpack16(&msg->mail_type, buffer);
		safe_unpackstr(&msg->mail_user, buffer);
		safe_unpackstr(&msg->reservation, buffer);
		safe_unpack16(&msg->restart_cnt, buffer);
		safe_unpack16(&msg->warn_flags, buffer);
		safe_unpack16(&msg->warn_signal, buffer);
		safe_unpack16(&msg->warn_time, buffer);
		safe_unpackstr(&msg->wckey, buffer);
		safe_unpack32(&msg->req_switch, buffer);
		safe_unpack32(&msg->wait4switch, buffer);

		safe_unpack16(&msg->wait_all_nodes, buffer);
		safe_unpack64(&msg->bitflags, buffer);
		safe_unpack32(&msg->delay_boot, buffer);
		safe_unpackstr(&msg->extra, buffer);
		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr(&msg->x11_magic_cookie, buffer);
		safe_unpackstr(&msg->x11_target, buffer);
		safe_unpack16(&msg->x11_target_port, buffer);

		safe_unpackstr(&msg->cpus_per_tres, buffer);
		slurm_format_tres_string(&msg->cpus_per_tres, "gres");

		safe_unpackstr(&msg->mem_per_tres, buffer);
		slurm_format_tres_string(&msg->mem_per_tres, "gres");

		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpackstr(&msg->tres_per_job, buffer);
		slurm_format_tres_string(&msg->tres_per_job, "gres");

		safe_unpackstr(&msg->tres_per_node, buffer);
		slurm_format_tres_string(&msg->tres_per_node, "gres");

		safe_unpackstr(&msg->tres_per_socket, buffer);
		slurm_format_tres_string(&msg->tres_per_socket, "gres");

		safe_unpackstr(&msg->tres_per_task, buffer);
		slurm_format_tres_string(&msg->tres_per_task, "gres");

		if (unpack_cron_entry(&msg->crontab_entry,
				      smsg->protocol_version, buffer))
			goto unpack_error;
		safe_unpack16(&msg->segment_size, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_desc_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_desc_list_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	list_t *job_req_list = smsg->data;
	job_desc_msg_t *req;
	list_itr_t *iter;
	uint16_t cnt = 0;

	if (job_req_list)
		cnt = list_count(job_req_list);
	pack16(cnt, buffer);
	if (cnt == 0)
		return;

	iter = list_iterator_create(job_req_list);
	while ((req = list_next(iter))) {
		slurm_msg_t msg_wrapper = {
			.data = req,
			.protocol_version = smsg->protocol_version,
		};

		_pack_job_desc_msg(&msg_wrapper, buffer);
	}
	list_iterator_destroy(iter);
}

static int _unpack_job_desc_list_msg(list_t **job_req_list, buf_t *buffer,
				     uint16_t protocol_version)
{
	uint16_t cnt = 0;
	int i;

	*job_req_list = NULL;

	safe_unpack16(&cnt, buffer);
	if (cnt == 0)
		return SLURM_SUCCESS;
	if (cnt > NO_VAL16)
		goto unpack_error;

	*job_req_list = list_create((ListDelF) slurm_free_job_desc_msg);
	for (i = 0; i < cnt; i++) {
		slurm_msg_t tmp_msg = {
			.protocol_version = protocol_version,
		};
		if (_unpack_job_desc_msg(&tmp_msg, buffer))
			goto unpack_error;
		list_append(*job_req_list, tmp_msg.data);
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*job_req_list);
	return SLURM_ERROR;
}

static void _pack_job_alloc_info_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_alloc_info_msg_t *job_desc_ptr = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&job_desc_ptr->step_id, buffer,
			     smsg->protocol_version);
		packstr(job_desc_ptr->req_cluster, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(job_desc_ptr->step_id.job_id, buffer);
		packstr(job_desc_ptr->req_cluster, buffer);
	}
}

static int _unpack_job_alloc_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_alloc_info_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->req_cluster, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->req_cluster, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_alloc_info_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_info_list_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	list_t *job_resp_list = smsg->data;
	slurm_msg_t msg = { .protocol_version = smsg->protocol_version };
	resource_allocation_response_msg_t *resp;
	list_itr_t *iter;
	uint16_t cnt = 0;

	if (job_resp_list)
		cnt = list_count(job_resp_list);

	/* WARNING - this cannot be converted to slurm_pack_list() */
	pack16(cnt, buffer);
	if (cnt == 0)
		return;

	iter = list_iterator_create(job_resp_list);
	while ((resp = list_next(iter))){
		msg.data = resp;
		_pack_resource_allocation_response_msg(&msg, buffer);
	}
	list_iterator_destroy(iter);
}

void _free_job_info_list(void *x)
{
	resource_allocation_response_msg_t *job_info_ptr = x;
	slurm_free_resource_allocation_response_msg(job_info_ptr);
}

static int _unpack_job_info_list_msg(list_t **job_resp_list, buf_t *buffer,
				     uint16_t protocol_version)
{
	slurm_msg_t msg = { .protocol_version = protocol_version };
	uint16_t cnt = 0;
	int i;

	*job_resp_list = NULL;

	safe_unpack16(&cnt, buffer);
	if (cnt == 0)
		return SLURM_SUCCESS;
	if (cnt > NO_VAL16)
		goto unpack_error;

	*job_resp_list = list_create(_free_job_info_list);
	for (i = 0; i < cnt; i++) {
		if (_unpack_resource_allocation_response_msg(&msg, buffer))
			goto unpack_error;
		list_append(*job_resp_list, msg.data);
		msg.data = NULL;
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*job_resp_list);
	return SLURM_ERROR;
}

static void _pack_step_alloc_info_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	step_alloc_info_msg_t *job_desc_ptr = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		slurm_pack_selected_step(job_desc_ptr, smsg->protocol_version,
					 buffer);
	}
}

static int _unpack_step_alloc_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	slurm_selected_step_t **msg = (slurm_selected_step_t **) &smsg->data;
	return slurm_unpack_selected_step(msg, smsg->protocol_version, buffer);
}

static void _pack_sbcast_cred_no_job_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	sbcast_cred_req_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->node_list, buffer);
	}
}

static int _unpack_sbcast_cred_no_job_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	sbcast_cred_req_msg_t *cred_msg = xmalloc(sizeof(*cred_msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&cred_msg->node_list, buffer);
	}

	smsg->data = cred_msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_sbcast_cred_req_msg(cred_msg);
	return SLURM_ERROR;
}

static void _pack_node_reg_resp(const slurm_msg_t *smsg, buf_t *buffer)
{
	slurm_node_reg_resp_msg_t *msg = smsg->data;
	list_t *pack_list;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		bool locked = false;

		if (msg->tres_list) {
			pack_list = msg->tres_list;
		} else {
			assoc_mgr_lock(&locks);
			pack_list = assoc_mgr_tres_list;
			locked = true;
		}

		(void) slurm_pack_list(pack_list, slurmdb_pack_tres_rec, buffer,
				       smsg->protocol_version);

		if (locked)
			assoc_mgr_unlock(&locks);

		packstr(msg->node_name, buffer);
	}
}

static int _unpack_node_reg_resp(slurm_msg_t *smsg, buf_t *buffer)
{
	slurm_node_reg_resp_msg_t *msg_ptr = xmalloc(sizeof(*msg_ptr));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (slurm_unpack_list(&msg_ptr->tres_list,
				      slurmdb_unpack_tres_rec,
				      slurmdb_destroy_tres_rec, buffer,
				      smsg->protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr(&msg_ptr->node_name, buffer);
	}

	smsg->data = msg_ptr;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_reg_resp_msg(msg_ptr);
	return SLURM_ERROR;
}

static void _pack_last_update_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	last_update_msg_t *msg = smsg->data;

	pack_time(msg->last_update, buffer);
}

static int _unpack_last_update_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	last_update_msg_t *last_update_msg = xmalloc(sizeof(*last_update_msg));

	safe_unpack_time(&last_update_msg->last_update, buffer);

	smsg->data = last_update_msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_last_update_msg(last_update_msg);
	return SLURM_ERROR;
}

static void _pack_return_code_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	return_code_msg_t *msg = smsg->data;

	pack32(msg->return_code, buffer);
}

static int _unpack_return_code_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	return_code_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpack32(&msg->return_code, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_return_code_msg(msg);
	return SLURM_ERROR;
}

static void _pack_return_code2_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	return_code2_msg_t *msg = smsg->data;

	xassert(msg);
	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->return_code, buffer);
		packstr(msg->err_msg, buffer);
	}
}

/* Log error message, otherwise replicate _unpack_return_code_msg() */
static int _unpack_return_code2_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	char *err_msg = NULL;
	return_code_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->return_code, buffer);
		safe_unpackstr(&err_msg, buffer);
		if (err_msg) {
			print_multi_line_string(err_msg, -1, LOG_LEVEL_ERROR);
			xfree(err_msg);
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_return_code_msg(msg);
	return SLURM_ERROR;
}

static void _pack_reroute_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	reroute_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (msg->working_cluster_rec) {
			pack8(1, buffer);
			slurmdb_pack_cluster_rec(msg->working_cluster_rec,
						 smsg->protocol_version,
						 buffer);
		} else
			pack8(0, buffer);
		packstr(msg->stepmgr, buffer);
	}
}

static int _unpack_reroute_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint8_t uint8_tmp = 0;
	reroute_msg_t *reroute_msg = xmalloc(sizeof(*reroute_msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			slurmdb_unpack_cluster_rec(
				(void **) &reroute_msg->working_cluster_rec,
				smsg->protocol_version, buffer);
		}
		safe_unpackstr(&reroute_msg->stepmgr, buffer);
	}

	smsg->data = reroute_msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reroute_msg(reroute_msg);
	return SLURM_ERROR;
}

static void _pack_reattach_tasks_request_msg(const slurm_msg_t *smsg,
					     buf_t *buffer)
{
	reattach_tasks_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		packstr(msg->tls_cert, buffer);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->io_key, buffer);
		pack16(msg->num_resp_port, buffer);
		for (int i = 0; i < msg->num_resp_port; i++)
			pack16(msg->resp_port[i], buffer);
		pack16(msg->num_io_port, buffer);
		for (int i = 0; i < msg->num_io_port; i++)
			pack16(msg->io_port[i], buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->io_key, buffer);
		pack16(msg->num_resp_port, buffer);
		for (int i = 0; i < msg->num_resp_port; i++)
			pack16(msg->resp_port[i], buffer);
		pack16(msg->num_io_port, buffer);
		for (int i = 0; i < msg->num_io_port; i++)
			pack16(msg->io_port[i], buffer);
	}
}

static int _unpack_reattach_tasks_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	reattach_tasks_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->tls_cert, buffer);
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->io_key, buffer);
		safe_unpack16(&msg->num_resp_port, buffer);
		if (msg->num_resp_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_resp_port > 0) {
			safe_xcalloc(msg->resp_port, msg->num_resp_port,
				     sizeof(uint16_t));
			for (int i = 0; i < msg->num_resp_port; i++)
				safe_unpack16(&msg->resp_port[i], buffer);
		}
		safe_unpack16(&msg->num_io_port, buffer);
		if (msg->num_io_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_io_port > 0) {
			safe_xcalloc(msg->io_port, msg->num_io_port,
				     sizeof(uint16_t));
			for (int i = 0; i < msg->num_io_port; i++)
				safe_unpack16(&msg->io_port[i], buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->io_key, buffer);
		safe_unpack16(&msg->num_resp_port, buffer);
		if (msg->num_resp_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_resp_port > 0) {
			safe_xcalloc(msg->resp_port, msg->num_resp_port,
				     sizeof(uint16_t));
			for (int i = 0; i < msg->num_resp_port; i++)
				safe_unpack16(&msg->resp_port[i], buffer);
		}
		safe_unpack16(&msg->num_io_port, buffer);
		if (msg->num_io_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_io_port > 0) {
			safe_xcalloc(msg->io_port, msg->num_io_port,
				     sizeof(uint16_t));
			for (int i = 0; i < msg->num_io_port; i++)
				safe_unpack16(&msg->io_port[i], buffer);
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reattach_tasks_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_reattach_tasks_response_msg(const slurm_msg_t *smsg,
					      buf_t *buffer)
{
	reattach_tasks_response_msg_t *msg = smsg->data;

	packstr(msg->node_name, buffer);
	pack32(msg->return_code, buffer);
	pack32(msg->ntasks, buffer);
	pack32_array(msg->gtids, msg->ntasks, buffer);
	pack32_array(msg->local_pids, msg->ntasks, buffer);
	for (int i = 0; i < msg->ntasks; i++) {
		packstr(msg->executable_names[i], buffer);
	}
}

static int _unpack_reattach_tasks_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t ntasks;
	reattach_tasks_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->node_name, buffer);
		safe_unpack32(&msg->return_code, buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack32_array(&msg->gtids, &ntasks, buffer);
		safe_unpack32_array(&msg->local_pids, &ntasks, buffer);
		if (msg->ntasks != ntasks)
			goto unpack_error;
		safe_xcalloc(msg->executable_names, msg->ntasks,
			     sizeof(char *));
		for (int i = 0; i < msg->ntasks; i++)
			safe_unpackstr(&msg->executable_names[i], buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reattach_tasks_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_task_exit_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	task_exit_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->return_code, buffer);
		pack32(msg->num_tasks, buffer);
		pack32_array(msg->task_id_list,
			     msg->num_tasks, buffer);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
	}
}

static int _unpack_task_exit_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp;
	task_exit_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->return_code, buffer);
		safe_unpack32(&msg->num_tasks, buffer);
		safe_unpack32_array(&msg->task_id_list, &uint32_tmp, buffer);
		if (msg->num_tasks != uint32_tmp)
			goto unpack_error;
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_task_exit_msg(msg);
	return SLURM_ERROR;
}

static void _pack_launch_tasks_response_msg(const slurm_msg_t *smsg,
					    buf_t *buffer)
{
	launch_tasks_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->return_code, buffer);
		packstr(msg->node_name, buffer);
		pack32(msg->count_of_pids, buffer);
		pack32_array(msg->local_pids, msg->count_of_pids, buffer);
		pack32_array(msg->task_ids, msg->count_of_pids, buffer);
	}
}

static int _unpack_launch_tasks_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp;
	launch_tasks_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->return_code, buffer);
		safe_unpackstr(&msg->node_name, buffer);
		safe_unpack32(&msg->count_of_pids, buffer);
		safe_unpack32_array(&msg->local_pids, &uint32_tmp, buffer);
		if (msg->count_of_pids != uint32_tmp)
			goto unpack_error;
		safe_unpack32_array(&msg->task_ids, &uint32_tmp, buffer);
		if (msg->count_of_pids != uint32_tmp)
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_launch_tasks_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_launch_tasks_request_msg(const slurm_msg_t *smsg,
					   buf_t *buffer)
{
	launch_tasks_request_msg_t *msg = smsg->data;
	uint16_t cred_version =
		msg->cred_version ? msg->cred_version : smsg->protocol_version;

	xassert(msg);

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32_array(msg->gids, msg->ngids, buffer);

		pack32(msg->het_job_node_offset, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->het_job_nnodes, buffer);
		if (msg->het_job_nnodes != NO_VAL) {
			for (int i = 0; i < msg->het_job_nnodes; i++) {
				pack32_array(
					msg->het_job_tids[i],
					(uint32_t)msg->het_job_task_cnts[i],
					buffer);
			}
		}
		pack32(msg->het_job_ntasks, buffer);
		if (msg->het_job_ntasks != NO_VAL) {
			for (int i = 0; i < msg->het_job_ntasks; i++)
				pack32(msg->het_job_tid_offsets[i], buffer);
		}
		pack32(msg->het_job_offset, buffer);
		pack32(msg->het_job_step_cnt, buffer);
		if ((msg->het_job_offset < NO_VAL) &&
		    (msg->het_job_step_cnt > 0)) {
			pack32_array(msg->het_job_step_task_cnts,
				     msg->het_job_step_cnt, buffer);
		}
		pack32(msg->het_job_task_offset, buffer);
		packstr(msg->het_job_node_list, buffer);
		pack32(msg->mpi_plugin_id, buffer);
		pack32(msg->ntasks, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		pack64(msg->job_mem_lim, buffer);
		pack64(msg->step_mem_lim, buffer);

		pack32(msg->nnodes, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16_array(msg->cpt_compact_array,
			     msg->cpt_compact_cnt, buffer);
		pack32_array(msg->cpt_compact_reps,
			     msg->cpt_compact_cnt, buffer);
		packstr(msg->tres_per_task, buffer);
		pack16(msg->threads_per_core, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->node_cpus, buffer);
		pack16(msg->job_core_spec, buffer);
		pack16(msg->accel_bind_type, buffer);

		pack16(cred_version, buffer);
		slurm_cred_pack(msg->cred, buffer, cred_version);
		for (int i = 0; i < msg->nnodes; i++) {
			pack16(msg->tasks_to_launch[i], buffer);
			pack32_array(msg->global_task_ids[i],
				     (uint32_t) msg->tasks_to_launch[i],
				     buffer);
		}
		pack16(msg->num_resp_port, buffer);
		for (int i = 0; i < msg->num_resp_port; i++)
			pack16(msg->resp_port[i], buffer);
		slurm_pack_addr(&msg->orig_addr, buffer);
		packstr_array(msg->env, msg->envc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->container, buffer);
		packstr(msg->cwd, buffer);
		pack16(msg->cpu_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->mem_bind, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		pack32(msg->flags, buffer);
		packstr(msg->ofname, buffer);
		packstr(msg->efname, buffer);
		packstr(msg->ifname, buffer);
		pack16(msg->num_io_port, buffer);
		for (int i = 0; i < msg->num_io_port; i++)
			pack16(msg->io_port[i], buffer);
		packstr(msg->alloc_tls_cert, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_epilog, buffer);
		pack16(msg->slurmd_debug, buffer);
		job_options_pack(msg->options, buffer);

		packstr(msg->complete_nodelist, buffer);

		pack8(msg->open_mode, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);

		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr(msg->stepmgr, buffer);
		packbool(msg->oom_kill_step, buffer);

		if (msg->job_ptr) {
			packbool(true, buffer);
			job_record_pack(msg->job_ptr, 0, buffer,
					smsg->protocol_version);
			slurm_pack_list(msg->job_node_array, node_record_pack,
					buffer, smsg->protocol_version);
			part_record_pack(msg->part_ptr, buffer,
					 smsg->protocol_version);
		} else {
			packbool(false, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32_array(msg->gids, msg->ngids, buffer);

		pack32(msg->het_job_node_offset, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->het_job_nnodes, buffer);
		if (msg->het_job_nnodes != NO_VAL) {
			for (int i = 0; i < msg->het_job_nnodes; i++) {
				pack32_array(
					msg->het_job_tids[i],
					(uint32_t)msg->het_job_task_cnts[i],
					buffer);
			}
		}
		pack32(msg->het_job_ntasks, buffer);
		if (msg->het_job_ntasks != NO_VAL) {
			for (int i = 0; i < msg->het_job_ntasks; i++)
				pack32(msg->het_job_tid_offsets[i], buffer);
		}
		pack32(msg->het_job_offset, buffer);
		pack32(msg->het_job_step_cnt, buffer);
		pack32(msg->het_job_task_offset, buffer);
		packstr(msg->het_job_node_list, buffer);
		pack32(msg->mpi_plugin_id, buffer);
		pack32(msg->ntasks, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		pack64(msg->job_mem_lim, buffer);
		pack64(msg->step_mem_lim, buffer);

		pack32(msg->nnodes, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16_array(msg->cpt_compact_array,
			     msg->cpt_compact_cnt, buffer);
		pack32_array(msg->cpt_compact_reps,
			     msg->cpt_compact_cnt, buffer);
		packstr(msg->tres_per_task, buffer);
		pack16(msg->threads_per_core, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->node_cpus, buffer);
		pack16(msg->job_core_spec, buffer);
		pack16(msg->accel_bind_type, buffer);

		pack16(cred_version, buffer);
		slurm_cred_pack(msg->cred, buffer, cred_version);
		for (int i = 0; i < msg->nnodes; i++) {
			pack16(msg->tasks_to_launch[i], buffer);
			pack32_array(msg->global_task_ids[i],
				     (uint32_t) msg->tasks_to_launch[i],
				     buffer);
		}
		pack16(msg->num_resp_port, buffer);
		for (int i = 0; i < msg->num_resp_port; i++)
			pack16(msg->resp_port[i], buffer);
		slurm_pack_addr(&msg->orig_addr, buffer);
		packstr_array(msg->env, msg->envc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->container, buffer);
		packstr(msg->cwd, buffer);
		pack16(msg->cpu_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->mem_bind, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		pack32(msg->flags, buffer);
		packstr(msg->ofname, buffer);
		packstr(msg->efname, buffer);
		packstr(msg->ifname, buffer);
		pack16(msg->num_io_port, buffer);
		for (int i = 0; i < msg->num_io_port; i++)
			pack16(msg->io_port[i], buffer);
		pack32(msg->profile, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_epilog, buffer);
		pack16(msg->slurmd_debug, buffer);
		job_options_pack(msg->options, buffer);

		packnull(buffer);

		packstr(msg->complete_nodelist, buffer);

		pack8(msg->open_mode, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);

		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr(msg->stepmgr, buffer);
		packbool(msg->oom_kill_step, buffer);

		if (msg->job_ptr) {
			packbool(true, buffer);
			job_record_pack(msg->job_ptr, 0, buffer,
					smsg->protocol_version);
			slurm_pack_list(msg->job_node_array, node_record_pack,
					buffer, smsg->protocol_version);
			part_record_pack(msg->part_ptr, buffer,
					 smsg->protocol_version);
		} else {
			packbool(false, buffer);
		}
	}
}

static int _unpack_launch_tasks_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp = 0;
	bool tmp_bool;
	char *tmp_char = NULL;
	int i = 0;
	launch_tasks_request_msg_t *msg = xmalloc(sizeof(*msg));
	slurm_cred_arg_t *cred_arg = NULL;

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);

		safe_unpack32_array(&msg->gids, &msg->ngids, buffer);

		safe_unpack32(&msg->het_job_node_offset, buffer);
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32(&msg->het_job_nnodes, buffer);
		if (msg->het_job_nnodes != NO_VAL) {
			safe_xcalloc(msg->het_job_task_cnts,
				     msg->het_job_nnodes,
				     sizeof(uint16_t));
			safe_xcalloc(msg->het_job_tids, msg->het_job_nnodes,
				     sizeof(uint32_t *));
			for (i = 0; i < msg->het_job_nnodes; i++) {
				safe_unpack32_array(&msg->het_job_tids[i],
						    &uint32_tmp,
						    buffer);
				msg->het_job_task_cnts[i] = uint32_tmp;
			}
		}
		safe_unpack32(&msg->het_job_ntasks, buffer);
		if (msg->het_job_ntasks != NO_VAL) {
			safe_xcalloc(msg->het_job_tid_offsets,
				     msg->het_job_ntasks,
				     sizeof(uint32_t));
			for (i = 0; i < msg->het_job_ntasks; i++)
				safe_unpack32(&msg->het_job_tid_offsets[i],
					      buffer);
		}
		safe_unpack32(&msg->het_job_offset, buffer);
		safe_unpack32(&msg->het_job_step_cnt, buffer);
		if ((msg->het_job_offset < NO_VAL) &&
		    (msg->het_job_step_cnt > 0)) {
			safe_unpack32_array(&msg->het_job_step_task_cnts,
					    &msg->het_job_step_cnt, buffer);
		}
		safe_unpack32(&msg->het_job_task_offset, buffer);
		safe_unpackstr(&msg->het_job_node_list, buffer);
		safe_unpack32(&msg->mpi_plugin_id, buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpack64(&msg->job_mem_lim, buffer);
		safe_unpack64(&msg->step_mem_lim, buffer);

		safe_unpack32(&msg->nnodes, buffer);
		if (msg->nnodes >= NO_VAL)
			goto unpack_error;
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16_array(&msg->cpt_compact_array,
				    &msg->cpt_compact_cnt, buffer);
		safe_unpack32_array(&msg->cpt_compact_reps,
				    &uint32_tmp, buffer);
		xassert(uint32_tmp == msg->cpt_compact_cnt);
		safe_unpackstr(&msg->tres_per_task, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->node_cpus, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);
		safe_unpack16(&msg->accel_bind_type, buffer);

		safe_unpack16(&msg->cred_version, buffer);
		if (!(msg->cred = slurm_cred_unpack(buffer, msg->cred_version)))
			goto unpack_error;
		safe_xcalloc(msg->tasks_to_launch, msg->nnodes,
			     sizeof(uint16_t));
		safe_xcalloc(msg->global_task_ids, msg->nnodes,
			     sizeof(uint32_t *));
		for (i = 0; i < msg->nnodes; i++) {
			safe_unpack16(&msg->tasks_to_launch[i], buffer);
			safe_unpack32_array(&msg->global_task_ids[i],
					    &uint32_tmp,
					    buffer);
			if (msg->tasks_to_launch[i] != (uint16_t) uint32_tmp)
				goto unpack_error;
		}
		safe_unpack16(&msg->num_resp_port, buffer);
		if (msg->num_resp_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_resp_port > 0) {
			safe_xcalloc(msg->resp_port, msg->num_resp_port,
				     sizeof(uint16_t));
			for (i = 0; i < msg->num_resp_port; i++)
				safe_unpack16(&msg->resp_port[i], buffer);
		}
		slurm_unpack_addr_no_alloc(&msg->orig_addr, buffer);
		safe_unpackstr_array(&msg->env, &msg->envc, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpackstr(&msg->cwd, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpackstr(&msg->cpu_bind, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr(&msg->mem_bind, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpack32(&msg->flags, buffer);
		safe_unpackstr(&msg->ofname, buffer);
		safe_unpackstr(&msg->efname, buffer);
		safe_unpackstr(&msg->ifname, buffer);
		safe_unpack16(&msg->num_io_port, buffer);
		if (msg->num_io_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_io_port > 0) {
			safe_xcalloc(msg->io_port, msg->num_io_port,
			             sizeof(uint16_t));
			for (i = 0; i < msg->num_io_port; i++)
				safe_unpack16(&msg->io_port[i], buffer);
		}
		safe_unpackstr(&msg->alloc_tls_cert, buffer);
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr(&msg->task_prolog, buffer);
		safe_unpackstr(&msg->task_epilog, buffer);
		safe_unpack16(&msg->slurmd_debug, buffer);

		msg->options = job_options_create();
		if (job_options_unpack(msg->options, buffer) < 0) {
			error("Unable to unpack extra job options: %m");
			goto unpack_error;
		}
		safe_unpackstr(&msg->complete_nodelist, buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpackstr(&msg->acctg_freq, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);

		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr(&msg->x11_alloc_host, buffer);
		safe_unpack16(&msg->x11_alloc_port, buffer);
		safe_unpackstr(&msg->x11_magic_cookie, buffer);
		safe_unpackstr(&msg->x11_target, buffer);
		safe_unpack16(&msg->x11_target_port, buffer);

		safe_unpackstr(&msg->stepmgr, buffer);

		safe_unpackbool(&msg->oom_kill_step, buffer);
		safe_unpackbool(&tmp_bool, buffer);
		if (tmp_bool) {
			if (job_record_unpack(&msg->job_ptr, 0, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (slurm_unpack_list(&msg->job_node_array,
					      node_record_unpack,
					      purge_node_rec, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (part_record_unpack(&msg->part_ptr, buffer,
					       smsg->protocol_version))
				goto unpack_error;
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);

		safe_unpack32_array(&msg->gids, &msg->ngids, buffer);

		safe_unpack32(&msg->het_job_node_offset, buffer);
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32(&msg->het_job_nnodes, buffer);
		if (msg->het_job_nnodes != NO_VAL) {
			safe_xcalloc(msg->het_job_task_cnts,
				     msg->het_job_nnodes,
				     sizeof(uint16_t));
			safe_xcalloc(msg->het_job_tids, msg->het_job_nnodes,
				     sizeof(uint32_t *));
			for (i = 0; i < msg->het_job_nnodes; i++) {
				safe_unpack32_array(&msg->het_job_tids[i],
						    &uint32_tmp,
						    buffer);
				msg->het_job_task_cnts[i] = uint32_tmp;
			}
		}
		safe_unpack32(&msg->het_job_ntasks, buffer);
		if (msg->het_job_ntasks != NO_VAL) {
			safe_xcalloc(msg->het_job_tid_offsets,
				     msg->het_job_ntasks,
				     sizeof(uint32_t));
			for (i = 0; i < msg->het_job_ntasks; i++)
				safe_unpack32(&msg->het_job_tid_offsets[i],
					      buffer);
		}
		safe_unpack32(&msg->het_job_offset, buffer);
		safe_unpack32(&msg->het_job_step_cnt, buffer);
		safe_unpack32(&msg->het_job_task_offset, buffer);
		safe_unpackstr(&msg->het_job_node_list, buffer);
		safe_unpack32(&msg->mpi_plugin_id, buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpack64(&msg->job_mem_lim, buffer);
		safe_unpack64(&msg->step_mem_lim, buffer);

		safe_unpack32(&msg->nnodes, buffer);
		if (msg->nnodes >= NO_VAL)
			goto unpack_error;
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16_array(&msg->cpt_compact_array,
				    &msg->cpt_compact_cnt, buffer);
		safe_unpack32_array(&msg->cpt_compact_reps,
				    &uint32_tmp, buffer);
		xassert(uint32_tmp == msg->cpt_compact_cnt);
		safe_unpackstr(&msg->tres_per_task, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->node_cpus, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);
		safe_unpack16(&msg->accel_bind_type, buffer);

		safe_unpack16(&msg->cred_version, buffer);
		if (!(msg->cred = slurm_cred_unpack(buffer, msg->cred_version)))
			goto unpack_error;
		safe_xcalloc(msg->tasks_to_launch, msg->nnodes,
			     sizeof(uint16_t));
		safe_xcalloc(msg->global_task_ids, msg->nnodes,
			     sizeof(uint32_t *));
		for (i = 0; i < msg->nnodes; i++) {
			safe_unpack16(&msg->tasks_to_launch[i], buffer);
			safe_unpack32_array(&msg->global_task_ids[i],
					    &uint32_tmp,
					    buffer);
			if (msg->tasks_to_launch[i] != (uint16_t) uint32_tmp)
				goto unpack_error;
		}
		safe_unpack16(&msg->num_resp_port, buffer);
		if (msg->num_resp_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_resp_port > 0) {
			safe_xcalloc(msg->resp_port, msg->num_resp_port,
				     sizeof(uint16_t));
			for (i = 0; i < msg->num_resp_port; i++)
				safe_unpack16(&msg->resp_port[i], buffer);
		}
		slurm_unpack_addr_no_alloc(&msg->orig_addr, buffer);
		safe_unpackstr_array(&msg->env, &msg->envc, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpackstr(&msg->cwd, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpackstr(&msg->cpu_bind, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr(&msg->mem_bind, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpack32(&msg->flags, buffer);
		safe_unpackstr(&msg->ofname, buffer);
		safe_unpackstr(&msg->efname, buffer);
		safe_unpackstr(&msg->ifname, buffer);
		safe_unpack16(&msg->num_io_port, buffer);
		if (msg->num_io_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_io_port > 0) {
			safe_xcalloc(msg->io_port, msg->num_io_port,
			             sizeof(uint16_t));
			for (i = 0; i < msg->num_io_port; i++)
				safe_unpack16(&msg->io_port[i], buffer);
		}
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr(&msg->task_prolog, buffer);
		safe_unpackstr(&msg->task_epilog, buffer);
		safe_unpack16(&msg->slurmd_debug, buffer);

		msg->options = job_options_create();
		if (job_options_unpack(msg->options, buffer) < 0) {
			error("Unable to unpack extra job options: %m");
			goto unpack_error;
		}
		safe_unpackstr(&tmp_char, buffer);
		xfree(tmp_char);
		safe_unpackstr(&msg->complete_nodelist, buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpackstr(&msg->acctg_freq, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);

		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr(&msg->x11_alloc_host, buffer);
		safe_unpack16(&msg->x11_alloc_port, buffer);
		safe_unpackstr(&msg->x11_magic_cookie, buffer);
		safe_unpackstr(&msg->x11_target, buffer);
		safe_unpack16(&msg->x11_target_port, buffer);

		safe_unpackstr(&msg->stepmgr, buffer);

		safe_unpackbool(&msg->oom_kill_step, buffer);
		safe_unpackbool(&tmp_bool, buffer);
		if (tmp_bool) {
			if (job_record_unpack(&msg->job_ptr, 0, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (slurm_unpack_list(&msg->job_node_array,
					      node_record_unpack,
					      purge_node_rec, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (part_record_unpack(&msg->part_ptr, buffer,
					       smsg->protocol_version))
				goto unpack_error;
		}
	}

	cred_arg = slurm_cred_get_args(msg->cred);
	msg->step_id = cred_arg->step_id;
	slurm_cred_unlock_args(msg->cred);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_launch_tasks_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_cancel_tasks_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	signal_tasks_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack16(msg->flags, buffer);
		pack16(msg->signal, buffer);
	}
}

static int _unpack_cancel_tasks_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	signal_tasks_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack16(&msg->flags, buffer);
		safe_unpack16(&msg->signal, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_signal_tasks_msg(msg);
	return SLURM_ERROR;
}

static void _pack_reboot_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	reboot_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (msg) {
			packstr(msg->features, buffer);
			pack16(msg->flags, buffer);
			pack32(msg->next_state, buffer);
			packstr(msg->node_list, buffer);
			packstr(msg->reason, buffer);
		} else {
			packnull(buffer);
			pack16((uint16_t) 0, buffer);
			pack32((uint32_t) NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
		}
	}
}

static int _unpack_reboot_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	reboot_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->features, buffer);
		safe_unpack16(&msg->flags, buffer);
		safe_unpack32(&msg->next_state, buffer);
		safe_unpackstr(&msg->node_list, buffer);
		safe_unpackstr(&msg->reason, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reboot_msg(msg);
	return SLURM_ERROR;
}

static void _pack_shutdown_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	shutdown_msg_t *msg = smsg->data;

	pack16(msg->options, buffer);
}

static int _unpack_shutdown_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	shutdown_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpack16(&msg->options, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shutdown_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_step_kill_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_kill_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->sjob_id, buffer);
		packstr(msg->sibling, buffer);
		pack16(msg->signal, buffer);
		pack16(msg->flags, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->sjob_id, buffer);
		packstr(msg->sibling, buffer);
		pack16(msg->signal, buffer);
		pack16(msg->flags, buffer);
	}
}

static int _unpack_job_step_kill_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_kill_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->sjob_id, buffer);
		safe_unpackstr(&msg->sibling, buffer);
		safe_unpack16(&msg->signal, buffer);
		safe_unpack16(&msg->flags, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->sjob_id, buffer);
		safe_unpackstr(&msg->sibling, buffer);
		safe_unpack16(&msg->signal, buffer);
		safe_unpack16(&msg->flags, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_kill_msg(msg);
	return SLURM_ERROR;
}

static void _pack_update_job_step_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	step_update_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->time_limit, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->step_id.step_id, buffer);
		pack32(msg->time_limit, buffer);
	}
}

static int _unpack_update_job_step_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	step_update_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->time_limit, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->step_id.step_id, buffer);
		safe_unpack32(&msg->time_limit, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_step_msg(msg);
	return SLURM_ERROR;
}

static void _pack_complete_job_allocation_msg(const slurm_msg_t *smsg,
					      buf_t *buffer)
{
	complete_job_allocation_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->job_rc, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->job_rc, buffer);
	}
}

static int _unpack_complete_job_allocation_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	complete_job_allocation_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->job_rc, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->job_rc, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_complete_job_allocation_msg(msg);
	return SLURM_ERROR;
}

static void _pack_prolog_complete_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	prolog_complete_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->node_name, buffer);
		pack32(msg->prolog_rc, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		packstr(msg->node_name, buffer);
		pack32(msg->prolog_rc, buffer);
	}
}

static int _unpack_prolog_complete_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	prolog_complete_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->node_name, buffer);
		safe_unpack32(&msg->prolog_rc, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->node_name, buffer);
		safe_unpack32(&msg->prolog_rc, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_prolog_complete_msg(msg);
	return SLURM_ERROR;
}

static void _pack_prolog_launch_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	prolog_launch_msg_t *msg = smsg->data;
	xassert(msg);

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		packstr(msg->alloc_tls_cert, buffer);
		slurm_pack_list(msg->job_gres_prep, gres_prep_pack, buffer,
				smsg->protocol_version);
		pack32(msg->het_job_id, buffer);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);

		packstr(msg->nodes, buffer);
		packstr(msg->work_dir, buffer);

		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		slurm_cred_pack(msg->cred, buffer, smsg->protocol_version);

		if (msg->job_ptr_buf) {
			packbool(true, buffer);
			packbuf(msg->job_ptr_buf, buffer);
			packbuf(msg->job_node_array_buf, buffer);
			packbuf(msg->part_ptr_buf, buffer);
		} else {
			packbool(false, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		packstr(msg->alloc_tls_cert, buffer);
		slurm_pack_list(msg->job_gres_prep, gres_prep_pack, buffer,
				smsg->protocol_version);
		pack32(msg->deprecated.job_id, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);

		packstr(msg->nodes, buffer);
		packstr(msg->work_dir, buffer);

		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		slurm_cred_pack(msg->cred, buffer, smsg->protocol_version);

		if (msg->job_ptr_buf) {
			packbool(true, buffer);
			packbuf(msg->job_ptr_buf, buffer);
			packbuf(msg->job_node_array_buf, buffer);
			packbuf(msg->part_ptr_buf, buffer);
		} else {
			packbool(false, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		gres_prep_pack_legacy(msg->job_gres_prep, buffer,
				      smsg->protocol_version);
		pack32(msg->deprecated.job_id, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);

		packnull(buffer);

		packstr(msg->nodes, buffer);
		packstr(msg->work_dir, buffer);

		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		slurm_cred_pack(msg->cred, buffer, smsg->protocol_version);

		if (msg->job_ptr_buf) {
			packbool(true, buffer);
			packbuf(msg->job_ptr_buf, buffer);
			packbuf(msg->job_node_array_buf, buffer);
			packbuf(msg->part_ptr_buf, buffer);
		} else {
			packbool(false, buffer);
		}
	}
}

static int _unpack_prolog_launch_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	bool tmp_bool;
	char *tmp_char = NULL;
	prolog_launch_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->alloc_tls_cert, buffer);
		if (gres_prep_unpack_list(&msg->job_gres_prep, buffer,
					  smsg->protocol_version))
			goto unpack_error;
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);

		safe_unpackstr(&msg->nodes, buffer);
		safe_unpackstr(&msg->work_dir, buffer);

		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr(&msg->x11_alloc_host, buffer);
		safe_unpack16(&msg->x11_alloc_port, buffer);
		safe_unpackstr(&msg->x11_magic_cookie, buffer);
		safe_unpackstr(&msg->x11_target, buffer);
		safe_unpack16(&msg->x11_target_port, buffer);

		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		if (!(msg->cred = slurm_cred_unpack(buffer,
						    smsg->protocol_version)))
			goto unpack_error;

		safe_unpackbool(&tmp_bool, buffer);
		if (tmp_bool) {
			if (job_record_unpack(&msg->job_ptr, 0, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (slurm_unpack_list(&msg->job_node_array,
					      node_record_unpack,
					      purge_node_rec, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (part_record_unpack(&msg->part_ptr, buffer,
					       smsg->protocol_version))
				goto unpack_error;
		}
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		uint32_t uint32_tmp;
		safe_unpackstr(&msg->alloc_tls_cert, buffer);
		if (gres_prep_unpack_list(&msg->job_gres_prep, buffer,
					  smsg->protocol_version))
			goto unpack_error;
		safe_unpack32(&uint32_tmp, buffer); /* was job_id */
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);

		safe_unpackstr(&msg->nodes, buffer);
		safe_unpackstr(&msg->work_dir, buffer);

		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr(&msg->x11_alloc_host, buffer);
		safe_unpack16(&msg->x11_alloc_port, buffer);
		safe_unpackstr(&msg->x11_magic_cookie, buffer);
		safe_unpackstr(&msg->x11_target, buffer);
		safe_unpack16(&msg->x11_target_port, buffer);

		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size,
				     buffer);
		if (!(msg->cred = slurm_cred_unpack(buffer,
						    smsg->protocol_version)))
			goto unpack_error;

		safe_unpackbool(&tmp_bool, buffer);
		if (tmp_bool) {
			if (job_record_unpack(&msg->job_ptr, 0, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (slurm_unpack_list(&msg->job_node_array,
					      node_record_unpack,
					      purge_node_rec, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (part_record_unpack(&msg->part_ptr, buffer,
					       smsg->protocol_version))
				goto unpack_error;
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t uint32_tmp;
		if (gres_prep_unpack_legacy(&msg->job_gres_prep, buffer,
					    smsg->protocol_version))
			goto unpack_error;
		safe_unpack32(&uint32_tmp, buffer); /* was job_id */
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);

		safe_unpackstr(&tmp_char, buffer);
		xfree(tmp_char);
		safe_unpackstr(&msg->nodes, buffer);
		safe_unpackstr(&msg->work_dir, buffer);

		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr(&msg->x11_alloc_host, buffer);
		safe_unpack16(&msg->x11_alloc_port, buffer);
		safe_unpackstr(&msg->x11_magic_cookie, buffer);
		safe_unpackstr(&msg->x11_target, buffer);
		safe_unpack16(&msg->x11_target_port, buffer);

		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size,
				     buffer);
		if (!(msg->cred = slurm_cred_unpack(buffer,
						    smsg->protocol_version)))
			goto unpack_error;

		safe_unpackbool(&tmp_bool, buffer);
		if (tmp_bool) {
			if (job_record_unpack(&msg->job_ptr, 0, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (slurm_unpack_list(&msg->job_node_array,
					      node_record_unpack,
					      purge_node_rec, buffer,
					      smsg->protocol_version))
				goto unpack_error;
			if (part_record_unpack(&msg->part_ptr, buffer,
					       smsg->protocol_version))
				goto unpack_error;
		}
	}

	if (msg->cred) {
		slurm_cred_arg_t *cred_arg = NULL;
		cred_arg = slurm_cred_get_args(msg->cred);
		msg->step_id = cred_arg->step_id;
		slurm_cred_unlock_args(msg->cred);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_prolog_launch_msg(msg);
	return SLURM_ERROR;
}

static void _pack_complete_batch_script_msg(const slurm_msg_t *smsg,
					    buf_t *buffer)
{
	complete_batch_script_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		jobacctinfo_pack(msg->jobacct, smsg->protocol_version,
				 PROTOCOL_TYPE_SLURM, buffer);
		pack32(msg->job_rc, buffer);
		pack32(msg->slurm_rc, buffer);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->user_id, buffer);
		packstr(msg->node_name, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		jobacctinfo_pack(msg->jobacct, smsg->protocol_version,
				 PROTOCOL_TYPE_SLURM, buffer);
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->job_rc, buffer);
		pack32(msg->slurm_rc, buffer);
		pack32(msg->user_id, buffer);
		packstr(msg->node_name, buffer);
	}
}

static int _unpack_complete_batch_script_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	complete_batch_script_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		if (jobacctinfo_unpack(&msg->jobacct, smsg->protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer,
				       1) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg->job_rc, buffer);
		safe_unpack32(&msg->slurm_rc, buffer);
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpackstr(&msg->node_name, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		if (jobacctinfo_unpack(&msg->jobacct, smsg->protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer,
				       1) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->job_rc, buffer);
		safe_unpack32(&msg->slurm_rc, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpackstr(&msg->node_name, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_complete_batch_script_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_step_stat(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_stat_t *msg = smsg->data;
	slurm_msg_t msg_wrapper = {
		.data = msg->step_pids,
		.protocol_version = smsg->protocol_version,
	};

	pack32(msg->return_code, buffer);
	pack32(msg->num_tasks, buffer);
	jobacctinfo_pack(msg->jobacct, smsg->protocol_version,
			 PROTOCOL_TYPE_SLURM, buffer);
	_pack_job_step_pids(&msg_wrapper, buffer);
}

static int _unpack_job_step_stat(slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_stat_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->return_code, buffer);
		safe_unpack32(&msg->num_tasks, buffer);
		if (jobacctinfo_unpack(&msg->jobacct, smsg->protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer,
				       1) != SLURM_SUCCESS)
			goto unpack_error;
		if (_unpack_job_step_pids(&msg->step_pids, buffer,
					  smsg->protocol_version))
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_stat(msg);
	return SLURM_ERROR;
}

static void _pack_step_id_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	pack_step_id(smsg->data, buffer, smsg->protocol_version);
}

static int _unpack_step_id_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	slurm_step_id_t *msg = xmalloc(sizeof(*msg));

	safe_unpack_step_id_members(msg, buffer, smsg->protocol_version);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_step_id(msg);
	return SLURM_ERROR;
}

static void _pack_job_step_pids(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_pids_t *msg = smsg->data;

	if (!msg) {
		packnull(buffer);
		pack32(0, buffer);
		return;
	}
	packstr(msg->node_name, buffer);
	pack32_array(msg->pid, msg->pid_cnt, buffer);
}

static int
_unpack_job_step_pids(job_step_pids_t **msg_ptr, buf_t *buffer,
		      uint16_t protocol_version)
{
	job_step_pids_t *msg;

	msg = xmalloc(sizeof(job_step_pids_t));
	*msg_ptr = msg;

	safe_unpackstr(&msg->node_name, buffer);
	safe_unpack32_array(&msg->pid, &msg->pid_cnt, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_pids(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_step_complete_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	step_complete_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->range_first, buffer);
		pack32(msg->range_last, buffer);
		pack32(msg->step_rc, buffer);
		jobacctinfo_pack(msg->jobacct, smsg->protocol_version,
				 PROTOCOL_TYPE_SLURM, buffer);
		packbool(msg->send_to_stepmgr, buffer);
	}
}

static int _unpack_step_complete_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	step_complete_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->range_first, buffer);
		safe_unpack32(&msg->range_last, buffer);
		safe_unpack32(&msg->step_rc, buffer);
		if (jobacctinfo_unpack(&msg->jobacct, smsg->protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer,
				       1) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackbool(&msg->send_to_stepmgr, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_step_complete_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_info_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_info_request_msg_t *msg = smsg->data;
	uint32_t count = NO_VAL;
	list_itr_t *itr;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);
		pack16(msg->show_flags, buffer);

		if (msg->job_ids)
			count = list_count(msg->job_ids);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(msg->job_ids);
			uint32_t *uint32_ptr;
			while ((uint32_ptr = list_next(itr)))
				pack32(*uint32_ptr, buffer);
			list_iterator_destroy(itr);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);
		pack16(msg->show_flags, buffer);

		if (msg->job_ids)
			count = list_count(msg->job_ids);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(msg->job_ids);
			uint32_t *uint32_ptr;
			while ((uint32_ptr = list_next(itr)))
				pack32(*uint32_ptr, buffer);
			list_iterator_destroy(itr);
		}
	}
}

static int _unpack_job_info_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t  count;
	uint32_t *uint32_ptr = NULL;
	job_info_request_msg_t *job_info = xmalloc(sizeof(*job_info));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_time(&job_info->last_update, buffer);
		safe_unpack16(&job_info->show_flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			job_info->job_ids = list_create(xfree_ptr);
			for (int i = 0; i < count; i++) {
				uint32_ptr = xmalloc(sizeof(uint32_t));
				safe_unpack32(uint32_ptr, buffer);
				list_append(job_info->job_ids, uint32_ptr);
				uint32_ptr = NULL;
			}
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&job_info->last_update, buffer);
		safe_unpack16(&job_info->show_flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			job_info->job_ids = list_create(xfree_ptr);
			for (int i = 0; i < count; i++) {
				uint32_ptr = xmalloc(sizeof(uint32_t));
				safe_unpack32(uint32_ptr, buffer);
				list_append(job_info->job_ids, uint32_ptr);
				uint32_ptr = NULL;
			}
		}
	}

	smsg->data = job_info;
	return SLURM_SUCCESS;

unpack_error:
	xfree(uint32_ptr);
	slurm_free_job_info_request_msg(job_info);
	return SLURM_ERROR;
}

static void _pack_job_state_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_state_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(msg->count, buffer);
		for (int i = 0; i < msg->count; i++) {
			pack32(msg->job_ids[i].step_id.job_id, buffer);
			pack32(msg->job_ids[i].array_task_id, buffer);
			pack32(msg->job_ids[i].het_job_offset, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->count, buffer);
		for (int i = 0; i < msg->count; i++) {
			pack32(msg->job_ids[i].step_id.job_id, buffer);
			pack32(msg->job_ids[i].array_task_id, buffer);
			pack32(msg->job_ids[i].het_job_offset, buffer);
		}
	}
}

static int _unpack_job_state_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_state_request_msg_t *js = xmalloc(sizeof(*js));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&js->count, buffer);

		if (js->count >= MAX_JOB_ID)
			goto unpack_error;

		if (js->count &&
		    !(js->job_ids =
			      try_xcalloc(js->count, sizeof(*js->job_ids))))
			goto unpack_error;

		for (int i = 0; i < js->count; i++) {
			/*
			 * Do not use slurm_unpack_selected_step to avoid
			 * unpacking the step id which is unused in this rpc.
			 */
			js->job_ids[i] = (slurm_selected_step_t)
				SLURM_SELECTED_STEP_INITIALIZER;
			safe_unpack32(&js->job_ids[i].step_id.job_id, buffer);
			safe_unpack32(&js->job_ids[i].array_task_id, buffer);
			safe_unpack32(&js->job_ids[i].het_job_offset, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&js->count, buffer);

		if (js->count >= MAX_JOB_ID)
			goto unpack_error;

		if (js->count &&
		    !(js->job_ids =
			      try_xcalloc(js->count, sizeof(*js->job_ids))))
			goto unpack_error;

		for (int i = 0; i < js->count; i++) {
			/*
			 * Do not use slurm_unpack_selected_step to avoid
			 * unpacking the step id which is unused in this rpc.
			 */
			js->job_ids[i] = (slurm_selected_step_t)
				SLURM_SELECTED_STEP_INITIALIZER;
			safe_unpack32(&js->job_ids[i].step_id.job_id, buffer);
			safe_unpack32(&js->job_ids[i].array_task_id, buffer);
			safe_unpack32(&js->job_ids[i].het_job_offset, buffer);
		}
	}

	smsg->data = js;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_state_request_msg(js);
	return SLURM_ERROR;
}

static void _pack_job_state_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_state_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(msg->jobs_count, buffer);
		for (int i = 0; i < msg->jobs_count; i++) {
			/*
			 * Do not use slurm_pack_selected_step to avoid
			 * packing the step id which is unused in this rpc.
			 */
			job_state_response_job_t *job = &msg->jobs[i];
			pack32(job->job_id, buffer);
			pack32(job->array_job_id, buffer);
			if (job->array_job_id) {
				pack32(job->array_task_id, buffer);
				pack_bit_str_hex(job->array_task_id_bitmap,
						 buffer);

				xassert(!job->het_job_id);
			} else {
				pack32(job->het_job_id, buffer);

				xassert(job->array_task_id == NO_VAL);
				xassert(!job->array_task_id_bitmap);
			}
			pack32(job->state, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->jobs_count, buffer);
		for (int i = 0; i < msg->jobs_count; i++) {
			/*
			 * Do not use slurm_pack_selected_step to avoid
			 * packing the step id which is unused in this rpc.
			 */
			job_state_response_job_t *job = &msg->jobs[i];
			pack32(job->job_id, buffer);
			pack32(job->array_job_id, buffer);
			if (job->array_job_id) {
				pack32(job->array_task_id, buffer);
				pack_bit_str_hex(job->array_task_id_bitmap,
						 buffer);

				xassert(!job->het_job_id);
			} else {
				pack32(job->het_job_id, buffer);

				xassert(job->array_task_id == NO_VAL);
				xassert(!job->array_task_id_bitmap);
			}
			pack32(job->state, buffer);
		}
	}
}

static int _unpack_job_state_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_state_response_msg_t *jsr = xmalloc(sizeof(*jsr));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&jsr->jobs_count, buffer);

		if (jsr->jobs_count >= MAX_JOB_ID)
			goto unpack_error;

		if (jsr->jobs_count &&
		    !(jsr->jobs =
			      try_xcalloc(jsr->jobs_count, sizeof(*jsr->jobs))))
			goto unpack_error;

		for (int i = 0; i < jsr->jobs_count; i++) {
			job_state_response_job_t *job = &jsr->jobs[i];
			safe_unpack32(&job->job_id, buffer);
			safe_unpack32(&job->array_job_id, buffer);
			if (job->array_job_id) {
				safe_unpack32(&job->array_task_id, buffer);
				unpack_bit_str_hex(&job->array_task_id_bitmap,
						   buffer);
			} else {
				safe_unpack32(&job->het_job_id, buffer);
				job->array_task_id = NO_VAL;
			}
			safe_unpack32(&job->state, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&jsr->jobs_count, buffer);

		if (jsr->jobs_count >= MAX_JOB_ID)
			goto unpack_error;

		if (jsr->jobs_count &&
		    !(jsr->jobs =
			      try_xcalloc(jsr->jobs_count, sizeof(*jsr->jobs))))
			goto unpack_error;

		for (int i = 0; i < jsr->jobs_count; i++) {
			job_state_response_job_t *job = &jsr->jobs[i];
			safe_unpack32(&job->job_id, buffer);
			safe_unpack32(&job->array_job_id, buffer);
			if (job->array_job_id) {
				safe_unpack32(&job->array_task_id, buffer);
				unpack_bit_str_hex(&job->array_task_id_bitmap,
						   buffer);
			} else {
				safe_unpack32(&job->het_job_id, buffer);
				job->array_task_id = NO_VAL;
			}
			safe_unpack32(&job->state, buffer);
		}
	}

	smsg->data = jsr;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_state_response_msg(jsr);
	return SLURM_ERROR;
}

static int _unpack_burst_buffer_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	int i, j;
	burst_buffer_info_t *bb_info_ptr;
	burst_buffer_resv_t *bb_resv_ptr;
	burst_buffer_use_t  *bb_use_ptr;
	burst_buffer_info_msg_t *bb_msg_ptr = xmalloc(sizeof(*bb_msg_ptr));

	safe_unpack32(&bb_msg_ptr->record_count, buffer);
	if (bb_msg_ptr->record_count >= NO_VAL)
		goto unpack_error;
	safe_xcalloc(bb_msg_ptr->burst_buffer_array, bb_msg_ptr->record_count,
		     sizeof(burst_buffer_info_t));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		for (i = 0, bb_info_ptr = bb_msg_ptr->burst_buffer_array;
		     i < bb_msg_ptr->record_count; i++, bb_info_ptr++) {
			safe_unpackstr(&bb_info_ptr->name, buffer);
			safe_unpackstr(&bb_info_ptr->allow_users, buffer);
			safe_unpackstr(&bb_info_ptr->create_buffer, buffer);
			safe_unpackstr(&bb_info_ptr->default_pool, buffer);
			safe_unpackstr(&bb_info_ptr->deny_users, buffer);
			safe_unpackstr(&bb_info_ptr->destroy_buffer, buffer);
			safe_unpack32(&bb_info_ptr->flags, buffer);
			safe_unpackstr(&bb_info_ptr->get_sys_state, buffer);
			safe_unpackstr(&bb_info_ptr->get_sys_status, buffer);
			safe_unpack64(&bb_info_ptr->granularity, buffer);
			safe_unpack32(&bb_info_ptr->pool_cnt, buffer);
			if (bb_info_ptr->pool_cnt >= NO_VAL)
				goto unpack_error;
			safe_xcalloc(bb_info_ptr->pool_ptr,
				     bb_info_ptr->pool_cnt,
				     sizeof(burst_buffer_pool_t));
			for (j = 0; j < bb_info_ptr->pool_cnt; j++) {
				safe_unpackstr(
					&bb_info_ptr->pool_ptr[j].name,
					buffer);
				safe_unpack64(
					&bb_info_ptr->pool_ptr[j].total_space,
					buffer);
				safe_unpack64(
					&bb_info_ptr->pool_ptr[j].granularity,
					buffer);
				safe_unpack64(
					&bb_info_ptr->pool_ptr[j].unfree_space,
					buffer);
				safe_unpack64(
					&bb_info_ptr->pool_ptr[j].used_space,
					buffer);
			}
			safe_unpack32(&bb_info_ptr->poll_interval, buffer);
			safe_unpack32(&bb_info_ptr->other_timeout, buffer);
			safe_unpackstr(&bb_info_ptr->start_stage_in, buffer);
			safe_unpackstr(&bb_info_ptr->start_stage_out, buffer);
			safe_unpackstr(&bb_info_ptr->stop_stage_in, buffer);
			safe_unpackstr(&bb_info_ptr->stop_stage_out, buffer);
			safe_unpack32(&bb_info_ptr->stage_in_timeout, buffer);
			safe_unpack32(&bb_info_ptr->stage_out_timeout, buffer);
			safe_unpack64(&bb_info_ptr->total_space, buffer);
			safe_unpack64(&bb_info_ptr->unfree_space, buffer);
			safe_unpack64(&bb_info_ptr->used_space, buffer);
			safe_unpack32(&bb_info_ptr->validate_timeout, buffer);

			safe_unpack32(&bb_info_ptr->buffer_count, buffer);
			if (bb_info_ptr->buffer_count >= NO_VAL)
				goto unpack_error;
			safe_xcalloc(bb_info_ptr->burst_buffer_resv_ptr,
				     bb_info_ptr->buffer_count,
				     sizeof(burst_buffer_resv_t));
			for (j = 0,
				     bb_resv_ptr = bb_info_ptr->burst_buffer_resv_ptr;
			     j < bb_info_ptr->buffer_count; j++, bb_resv_ptr++){
				safe_unpackstr(&bb_resv_ptr->account, buffer);
				safe_unpack32(&bb_resv_ptr->array_job_id,
					      buffer);
				safe_unpack32(&bb_resv_ptr->array_task_id,
					      buffer);
				safe_unpack_time(&bb_resv_ptr->create_time,
						 buffer);
				safe_unpack32(&bb_resv_ptr->job_id, buffer);
				safe_unpackstr(&bb_resv_ptr->name, buffer);
				safe_unpackstr(&bb_resv_ptr->partition, buffer);
				safe_unpackstr(&bb_resv_ptr->pool, buffer);
				safe_unpackstr(&bb_resv_ptr->qos, buffer);
				safe_unpack64(&bb_resv_ptr->size, buffer);
				safe_unpack16(&bb_resv_ptr->state, buffer);
				safe_unpack32(&bb_resv_ptr->user_id, buffer);
			}

			safe_unpack32(&bb_info_ptr->use_count, buffer);
			if (bb_info_ptr->use_count >= NO_VAL)
				goto unpack_error;
			safe_xcalloc(bb_info_ptr->burst_buffer_use_ptr,
				     bb_info_ptr->use_count,
				     sizeof(burst_buffer_use_t));
			for (j = 0,
				     bb_use_ptr = bb_info_ptr->burst_buffer_use_ptr;
			     j < bb_info_ptr->use_count; j++, bb_use_ptr++) {
				safe_unpack64(&bb_use_ptr->used, buffer);
				safe_unpack32(&bb_use_ptr->user_id, buffer);
			}
		}
	}

	smsg->data = bb_msg_ptr;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_burst_buffer_info_msg(bb_msg_ptr);
	return SLURM_ERROR;
}

static void _pack_job_step_info_req_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_info_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack16((uint16_t)msg->show_flags, buffer);
	}
}

static int _unpack_job_step_info_req_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_step_info_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&msg->last_update, buffer);
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack16(&msg->show_flags, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_info_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_node_info_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	node_info_request_msg_t *msg = smsg->data;

	pack_time(msg->last_update, buffer);
	pack16(msg->show_flags, buffer);
}

static int _unpack_node_info_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	node_info_request_msg_t *node_info = xmalloc(sizeof(*node_info));

	safe_unpack_time(&node_info->last_update, buffer);
	safe_unpack16(&node_info->show_flags, buffer);

	smsg->data = node_info;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_request_msg(node_info);
	return SLURM_ERROR;
}

static void _pack_hostlist_expansion_request(const slurm_msg_t *smsg,
					     buf_t *buffer)
{
	packstr(smsg->data, buffer);
}

static int _unpack_hostlist_expansion_request(slurm_msg_t *smsg, buf_t *buffer)
{
	safe_unpackstr((char **) &smsg->data, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(smsg->data);
	return SLURM_ERROR;
}

static void _pack_hostlist_expansion_response(const slurm_msg_t *smsg,
					      buf_t *buffer)
{
	packstr(smsg->data, buffer);
}

static int _unpack_hostlist_expansion_response(slurm_msg_t *smsg, buf_t *buffer)
{
	safe_unpackstr((char **) &smsg->data, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(smsg->data);
	return SLURM_ERROR;
}

static void _pack_node_info_single_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	node_info_single_msg_t *msg = smsg->data;

	packstr(msg->node_name, buffer);
	pack16(msg->show_flags, buffer);
}

static int _unpack_node_info_single_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	node_info_single_msg_t *node_info = xmalloc(sizeof(*node_info));

	safe_unpackstr(&node_info->node_name, buffer);
	safe_unpack16(&node_info->show_flags, buffer);

	smsg->data = node_info;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_single_msg(node_info);
	return SLURM_ERROR;
}

static void _pack_part_info_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	part_info_request_msg_t *msg = smsg->data;

	pack_time(msg->last_update, buffer);
	pack16(msg->show_flags, buffer);
}

static int _unpack_part_info_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	part_info_request_msg_t *part_info = xmalloc(sizeof(*part_info));

	safe_unpack_time(&part_info->last_update, buffer);
	safe_unpack16(&part_info->show_flags, buffer);

	smsg->data = part_info;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_part_info_request_msg(part_info);
	return SLURM_ERROR;
}

static void _pack_resv_info_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	resv_info_request_msg_t *msg = smsg->data;

	pack_time(msg->last_update, buffer);
}

static int _unpack_resv_info_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	resv_info_request_msg_t *resv_info = xmalloc(sizeof(*resv_info));

	safe_unpack_time(&resv_info->last_update, buffer);

	smsg->data = resv_info;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_info_request_msg(resv_info);
	return SLURM_ERROR;
}

static void _pack_ret_list(list_t *ret_list, uint16_t size_val, buf_t *buffer,
			   uint16_t protocol_version)
{
	list_itr_t *itr;
	ret_data_info_t *ret_data_info = NULL;
	slurm_msg_t msg;

	slurm_msg_t_init(&msg);
	msg.protocol_version = protocol_version;
	itr = list_iterator_create(ret_list);
	while ((ret_data_info = list_next(itr))) {
		pack32((uint32_t)ret_data_info->err, buffer);
		pack16((uint16_t)ret_data_info->type, buffer);
		packstr(ret_data_info->node_name, buffer);

		msg.msg_type = ret_data_info->type;
		msg.data = ret_data_info->data;
		pack_msg(&msg, buffer);
	}
	list_iterator_destroy(itr);
}

static int _unpack_ret_list(list_t **ret_list, uint16_t size_val, buf_t *buffer,
			    uint16_t protocol_version)
{
	int i = 0;
	ret_data_info_t *ret_data_info = NULL;
	slurm_msg_t msg;

	slurm_msg_t_init(&msg);
	msg.protocol_version = protocol_version;

	*ret_list = list_create(destroy_data_info);

	for (i=0; i<size_val; i++) {
		ret_data_info = xmalloc(sizeof(ret_data_info_t));
		list_push(*ret_list, ret_data_info);

		safe_unpack32((uint32_t *)&ret_data_info->err, buffer);
		safe_unpack16(&ret_data_info->type, buffer);
		safe_unpackstr(&ret_data_info->node_name, buffer);
		msg.msg_type = ret_data_info->type;
		if (unpack_msg(&msg, buffer) != SLURM_SUCCESS)
			goto unpack_error;
		ret_data_info->data = msg.data;
	}

	return SLURM_SUCCESS;

unpack_error:
	if (ret_data_info && ret_data_info->type) {
		error("_unpack_ret_list: message type %s, record %d of %u",
		      rpc_num2string(ret_data_info->type), i, size_val);
	}
	FREE_NULL_LIST(*ret_list);
	*ret_list = NULL;
	return SLURM_ERROR;
}

static void _pack_batch_job_launch_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	batch_job_launch_msg_t *msg = smsg->data;
	uint16_t cred_version =
		msg->cred_version ? msg->cred_version : smsg->protocol_version;
	xassert(msg);

	if (msg->script_buf)
		msg->script = msg->script_buf->head;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(msg->het_job_id, buffer);

		pack32_array(msg->gids, msg->ngids, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->ntasks, buffer);
		pack64(msg->pn_min_memory, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);

		pack32(msg->array_job_id, buffer);
		pack32(msg->array_task_id, buffer);

		packstr(msg->acctg_freq, buffer);
		packstr(msg->container, buffer);
		pack16(msg->cpu_bind_type, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->restart_cnt, buffer);
		pack16(msg->job_core_spec, buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		packstr(msg->cpu_bind, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->script, buffer);
		packstr(msg->work_dir, buffer);
		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);

		pack32(msg->argc, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);

		pack32(msg->envc, buffer);
		packstr_array(msg->environment, msg->envc, buffer);

		pack64(msg->job_mem, buffer);

		pack16(cred_version, buffer);
		slurm_cred_pack(msg->cred, buffer, cred_version);

		packstr(msg->account, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_task, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		packbool(msg->oom_kill_step, buffer);
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		pack32(msg->deprecated.job_id, buffer);
		pack32(msg->het_job_id, buffer);

		pack32_array(msg->gids, msg->ngids, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->ntasks, buffer);
		pack64(msg->pn_min_memory, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);

		pack32(msg->array_job_id, buffer);
		pack32(msg->array_task_id, buffer);

		packstr(msg->acctg_freq, buffer);
		packstr(msg->container, buffer);
		pack16(msg->cpu_bind_type, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->restart_cnt, buffer);
		pack16(msg->job_core_spec, buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		packstr(msg->cpu_bind, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->script, buffer);
		packstr(msg->work_dir, buffer);
		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);

		pack32(msg->argc, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);

		pack32(msg->envc, buffer);
		packstr_array(msg->environment, msg->envc, buffer);

		pack64(msg->job_mem, buffer);

		pack16(cred_version, buffer);
		slurm_cred_pack(msg->cred, buffer, cred_version);

		packstr(msg->account, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_task, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		packbool(msg->oom_kill_step, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->deprecated.job_id, buffer);
		pack32(msg->het_job_id, buffer);

		pack32_array(msg->gids, msg->ngids, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->ntasks, buffer);
		pack64(msg->pn_min_memory, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);

		pack32(msg->array_job_id, buffer);
		pack32(msg->array_task_id, buffer);

		packstr(msg->acctg_freq, buffer);
		packstr(msg->container, buffer);
		pack16(msg->cpu_bind_type, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->restart_cnt, buffer);
		pack16(msg->job_core_spec, buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		packstr(msg->cpu_bind, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->script, buffer);
		packstr(msg->work_dir, buffer);
		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);

		pack32(msg->argc, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);

		pack32(msg->envc, buffer);
		packstr_array(msg->environment, msg->envc, buffer);

		pack64(msg->job_mem, buffer);

		pack16(cred_version, buffer);
		slurm_cred_pack(msg->cred, buffer, cred_version);

		packstr(msg->account, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		packbool(msg->oom_kill_step, buffer);
	}

	if (msg->script_buf)
		msg->script = NULL;
}

static int _unpack_batch_job_launch_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp;
	batch_job_launch_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32_array(&msg->gids, &msg->ngids, buffer);

		safe_unpackstr(&msg->partition, buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpack8(&msg->overcommit, buffer);

		safe_unpack32(&msg->array_job_id, buffer);
		safe_unpack32(&msg->array_task_id, buffer);

		safe_unpackstr(&msg->acctg_freq, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->restart_cnt, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);

		safe_unpack32(&msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			safe_unpack16_array(&msg->cpus_per_node, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&msg->cpu_count_reps, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}

		safe_unpackstr(&msg->cpu_bind, buffer);
		safe_unpackstr(&msg->nodes, buffer);
		safe_unpackstr(&msg->script, buffer);
		safe_unpackstr(&msg->work_dir, buffer);
		safe_unpackstr(&msg->std_err, buffer);
		safe_unpackstr(&msg->std_in, buffer);
		safe_unpackstr(&msg->std_out, buffer);

		safe_unpack32(&msg->argc, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);

		safe_unpack32(&msg->envc, buffer);
		safe_unpackstr_array(&msg->environment, &msg->envc, buffer);

		safe_unpack64(&msg->job_mem, buffer);

		safe_unpack16(&msg->cred_version, buffer);
		if (!(msg->cred = slurm_cred_unpack(buffer, msg->cred_version)))
			goto unpack_error;

		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->resv_name, buffer);
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpackstr(&msg->tres_per_task, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);
		safe_unpackbool(&msg->oom_kill_step, buffer);
	} else if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpack32(&uint32_tmp, buffer); /* was job_id */
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32_array(&msg->gids, &msg->ngids, buffer);

		safe_unpackstr(&msg->partition, buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpack8(&msg->overcommit, buffer);

		safe_unpack32(&msg->array_job_id, buffer);
		safe_unpack32(&msg->array_task_id, buffer);

		safe_unpackstr(&msg->acctg_freq, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->restart_cnt, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);

		safe_unpack32(&msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			safe_unpack16_array(&msg->cpus_per_node, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&msg->cpu_count_reps, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}

		safe_unpackstr(&msg->cpu_bind, buffer);
		safe_unpackstr(&msg->nodes, buffer);
		safe_unpackstr(&msg->script, buffer);
		safe_unpackstr(&msg->work_dir, buffer);
		safe_unpackstr(&msg->std_err, buffer);
		safe_unpackstr(&msg->std_in, buffer);
		safe_unpackstr(&msg->std_out, buffer);

		safe_unpack32(&msg->argc, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);

		safe_unpack32(&msg->envc, buffer);
		safe_unpackstr_array(&msg->environment, &msg->envc, buffer);

		safe_unpack64(&msg->job_mem, buffer);

		safe_unpack16(&msg->cred_version, buffer);
		if (!(msg->cred = slurm_cred_unpack(buffer, msg->cred_version)))
			goto unpack_error;

		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->resv_name, buffer);
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpackstr(&msg->tres_per_task, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);
		safe_unpackbool(&msg->oom_kill_step, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&uint32_tmp, buffer); /* was job_id */
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32_array(&msg->gids, &msg->ngids, buffer);

		safe_unpackstr(&msg->partition, buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack64(&msg->pn_min_memory, buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpack8(&msg->overcommit, buffer);

		safe_unpack32(&msg->array_job_id, buffer);
		safe_unpack32(&msg->array_task_id, buffer);

		safe_unpackstr(&msg->acctg_freq, buffer);
		safe_unpackstr(&msg->container, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->restart_cnt, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);

		safe_unpack32(&msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			safe_unpack16_array(&msg->cpus_per_node, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&msg->cpu_count_reps, &uint32_tmp,
					    buffer);
			if (msg->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}

		safe_unpackstr(&msg->cpu_bind, buffer);
		safe_unpackstr(&msg->nodes, buffer);
		safe_unpackstr(&msg->script, buffer);
		safe_unpackstr(&msg->work_dir, buffer);
		safe_unpackstr(&msg->std_err, buffer);
		safe_unpackstr(&msg->std_in, buffer);
		safe_unpackstr(&msg->std_out, buffer);

		safe_unpack32(&msg->argc, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);

		safe_unpack32(&msg->envc, buffer);
		safe_unpackstr_array(&msg->environment, &msg->envc, buffer);

		safe_unpack64(&msg->job_mem, buffer);

		safe_unpack16(&msg->cred_version, buffer);
		if (!(msg->cred = slurm_cred_unpack(buffer, msg->cred_version)))
			goto unpack_error;

		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->resv_name, buffer);
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr(&msg->tres_bind, buffer);
		safe_unpackstr(&msg->tres_freq, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);
		safe_unpackbool(&msg->oom_kill_step, buffer);
	}

	if (msg->cred) {
		slurm_cred_arg_t *cred_arg = NULL;
		cred_arg = slurm_cred_get_args(msg->cred);
		msg->step_id = cred_arg->step_id;
		slurm_cred_unlock_args(msg->cred);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_launch_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_id_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_id_request_msg_t *msg = smsg->data;

	pack32(msg->job_pid, buffer);
}

static int _unpack_job_id_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_id_request_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpack32(&msg->job_pid, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_id_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_id_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_id_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->return_code, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->return_code, buffer);
	}
}

static int _unpack_job_id_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_id_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->return_code, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->return_code, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_id_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_config_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	config_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		pack32(msg->flags, buffer);
		pack16(msg->port, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->flags, buffer);
	}
}

static int _unpack_config_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	config_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpack32(&msg->flags, buffer);
		safe_unpack16(&msg->port, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->flags, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_config_request_msg(msg);
	return SLURM_ERROR;
}

extern void pack_config_file(void *in, uint16_t protocol_version,
			     buf_t *buffer)
{
	config_file_t *object = in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packbool(0, buffer);
			packbool(0, buffer);
			packnull(buffer);
			packnull(buffer);
			return;
		}

		packbool(object->exists, buffer);
		packbool(object->execute, buffer);
		packstr(object->file_name, buffer);
		packstr(object->file_content, buffer);
	}
}

extern int unpack_config_file(void **out, uint16_t protocol_version,
			      buf_t *buffer)
{
	config_file_t *object = xmalloc(sizeof(*object));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackbool(&object->exists, buffer);
		safe_unpackbool(&object->execute, buffer);
		safe_unpackstr(&object->file_name, buffer);
		safe_unpackstr(&object->file_content, buffer);
	}

	*out = object;
	return SLURM_SUCCESS;

unpack_error:
	xfree(object);
	*out = NULL;
	return SLURM_ERROR;
}

extern void pack_config_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	config_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		slurm_pack_list(msg->config_files, pack_config_file, buffer,
				smsg->protocol_version);
		packstr(msg->slurmd_spooldir, buffer);
	}
}

extern int unpack_config_response_msg(config_response_msg_t **msg_ptr,
				      buf_t *buffer, uint16_t protocol_version)
{
	config_response_msg_t *msg = xmalloc(sizeof(*msg));
	xassert(msg_ptr);
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (slurm_unpack_list(&msg->config_files, unpack_config_file,
				      destroy_config_file, buffer,
				      protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&msg->slurmd_spooldir, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_config_response_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_net_forward_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	net_forward_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack32(msg->flags, buffer);
		pack16(msg->port, buffer);
		packstr(msg->target, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->flags, buffer);
		pack16(msg->port, buffer);
		packstr(msg->target, buffer);
	}
}

static int _unpack_net_forward_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	net_forward_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack32(&msg->flags, buffer);
		safe_unpack16(&msg->port, buffer);
		safe_unpackstr(&msg->target, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->flags, buffer);
		safe_unpack16(&msg->port, buffer);
		safe_unpackstr(&msg->target, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_net_forward_msg(msg);
	return SLURM_ERROR;
}

static void _pack_srun_node_fail_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	srun_node_fail_msg_t *msg = smsg->data;
	xassert(msg);

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->nodelist, buffer);
	}
}

static int _unpack_srun_node_fail_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	srun_node_fail_msg_t *msg = xmalloc(sizeof(srun_node_fail_msg_t));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->nodelist, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_node_fail_msg(msg);
	return SLURM_ERROR;
}

static void _pack_srun_step_missing_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	srun_step_missing_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->nodelist, buffer);
	}
}

static int _unpack_srun_step_missing_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	srun_step_missing_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->nodelist, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_step_missing_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_ready_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_id_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack16(msg->show_flags, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack16(msg->show_flags, buffer);
	}
}

static int _unpack_job_ready_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_id_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack16(&msg->show_flags, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack16(&msg->show_flags, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_id_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_requeue_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	requeue_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->job_id_str, buffer);
		pack32(msg->flags, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		packstr(msg->job_id_str, buffer);
		pack32(msg->flags, buffer);
	}
}

static int _unpack_job_requeue_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	requeue_msg_t *msg = xmalloc(sizeof(requeue_msg_t));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->job_id_str, buffer);
		safe_unpack32(&msg->flags, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->job_id_str, buffer);
		safe_unpack32(&msg->flags, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_requeue_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_user_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_user_id_msg_t *msg = smsg->data;

	pack32(msg->user_id, buffer);
	pack16(msg->show_flags, buffer);
}

static int _unpack_job_user_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_user_id_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->user_id, buffer);
		safe_unpack16(&msg->show_flags, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_user_id_msg(msg);
	return SLURM_ERROR;
}

static void _pack_srun_timeout_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	srun_timeout_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack_time(msg->timeout, buffer);
	}
}

static int _unpack_srun_timeout_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	srun_timeout_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack_time(&msg->timeout, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_timeout_msg(msg);
	return SLURM_ERROR;
}

static void _pack_srun_user_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	srun_user_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->msg, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		packstr(msg->msg, buffer);
	}
}

static int _unpack_srun_user_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	srun_user_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->msg, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->msg, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_user_msg(msg);
	return SLURM_ERROR;
}

static void _pack_suspend_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	suspend_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack16(msg->op, buffer);
		packstr(msg->job_id_str, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->op, buffer);
		pack32(msg->step_id.job_id, buffer);
		packstr(msg->job_id_str, buffer);
	}
}

static int _unpack_suspend_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	suspend_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack16(&msg->op, buffer);
		safe_unpackstr(&msg->job_id_str, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack16(&msg->op, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->job_id_str, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_suspend_msg(msg);
	return SLURM_ERROR;
}

static void _pack_suspend_int_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	suspend_int_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		pack16(msg->op, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		pack16(msg->op, buffer);
	}
}

static int _unpack_suspend_int_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	suspend_int_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpack16(&msg->op, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack16(&msg->op, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_suspend_int_msg(msg);
	return SLURM_ERROR;
}

static void _pack_top_job_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	top_job_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack16(msg->op, buffer);
		packstr(msg->job_id_str, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->op, buffer);
		pack32(0, buffer);
		packstr(msg->job_id_str, buffer);
	}
}

static int _unpack_top_job_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	top_job_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack16(&msg->op, buffer);
		safe_unpackstr(&msg->job_id_str, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t uint32_tmp;
		safe_unpack16(&msg->op, buffer);
		safe_unpack32(&uint32_tmp, buffer); /* was job_id */
		safe_unpackstr(&msg->job_id_str, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_top_job_msg(msg);
	return SLURM_ERROR;
}

static void _pack_token_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	token_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->lifespan, buffer);
		packstr(msg->username, buffer);
	}
}

static int _unpack_token_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	token_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->lifespan, buffer);
		safe_unpackstr(&msg->username, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_token_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_token_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	token_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->token, buffer);
	}
}

static int _unpack_token_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	token_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->token, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_token_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_kill_jobs_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	kill_jobs_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);
		packstr(msg->admin_comment, buffer);
		pack16(msg->flags, buffer);
		packstr(msg->job_name, buffer);
		packstr_array(msg->jobs_array, msg->jobs_cnt, buffer);
		packstr(msg->partition, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->reservation, buffer);
		pack16(msg->signal, buffer);
		pack32(msg->state, buffer);
		pack32(msg->user_id, buffer);
		packstr(msg->user_name, buffer);
		packstr(msg->wckey, buffer);
		packstr(msg->nodelist, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);
		pack16(msg->flags, buffer);
		packstr(msg->job_name, buffer);
		packstr_array(msg->jobs_array, msg->jobs_cnt, buffer);
		packstr(msg->partition, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->reservation, buffer);
		pack16(msg->signal, buffer);
		pack32(msg->state, buffer);
		pack32(msg->user_id, buffer);
		packstr(msg->user_name, buffer);
		packstr(msg->wckey, buffer);
		packstr(msg->nodelist, buffer);
	}
}

static int _unpack_kill_jobs_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	kill_jobs_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->account, buffer);
		safe_unpackstr(&msg->admin_comment, buffer);
		safe_unpack16(&msg->flags, buffer);
		safe_unpackstr(&msg->job_name, buffer);
		safe_unpackstr_array(&msg->jobs_array, &msg->jobs_cnt, buffer);
		safe_unpackstr(&msg->partition, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->reservation, buffer);
		safe_unpack16(&msg->signal, buffer);
		safe_unpack32(&msg->state, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpackstr(&msg->user_name, buffer);
		safe_unpackstr(&msg->wckey, buffer);
		safe_unpackstr(&msg->nodelist, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->account, buffer);
		safe_unpack16(&msg->flags, buffer);
		safe_unpackstr(&msg->job_name, buffer);
		safe_unpackstr_array(&msg->jobs_array, &msg->jobs_cnt, buffer);
		safe_unpackstr(&msg->partition, buffer);
		safe_unpackstr(&msg->qos, buffer);
		safe_unpackstr(&msg->reservation, buffer);
		safe_unpack16(&msg->signal, buffer);
		safe_unpack32(&msg->state, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpackstr(&msg->user_name, buffer);
		safe_unpackstr(&msg->wckey, buffer);
		safe_unpackstr(&msg->nodelist, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_kill_jobs_msg(msg);
	return SLURM_ERROR;
}

static void _pack_kill_jobs_resp_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	kill_jobs_resp_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(msg->jobs_cnt, buffer);
		for (int i = 0; i < msg->jobs_cnt; i++) {
			kill_jobs_resp_job_t job_resp = msg->job_responses[i];

			pack32(job_resp.error_code, buffer);
			packstr(job_resp.error_msg, buffer);
			slurm_pack_selected_step(job_resp.id,
						 smsg->protocol_version,
						 buffer);
			pack32(job_resp.real_job_id, buffer);
			packstr(job_resp.sibling_name, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->jobs_cnt, buffer);
		for (int i = 0; i < msg->jobs_cnt; i++) {
			kill_jobs_resp_job_t job_resp = msg->job_responses[i];

			pack32(job_resp.error_code, buffer);
			packstr(job_resp.error_msg, buffer);
			slurm_pack_selected_step(job_resp.id,
						 smsg->protocol_version,
						 buffer);
			pack32(job_resp.real_job_id, buffer);
			packstr(job_resp.sibling_name, buffer);
		}
	}
}

static int _unpack_kill_jobs_resp_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	kill_jobs_resp_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->jobs_cnt, buffer);
		msg->job_responses =
			xcalloc(msg->jobs_cnt, sizeof(*msg->job_responses));
		for (int i = 0; i < msg->jobs_cnt; i++) {
			kill_jobs_resp_job_t *job_resp = &msg->job_responses[i];

			safe_unpack32(&job_resp->error_code, buffer);
			safe_unpackstr(&job_resp->error_msg, buffer);
			if (slurm_unpack_selected_step(&job_resp->id,
						       smsg->protocol_version,
						       buffer) != SLURM_SUCCESS)
				goto unpack_error;
			safe_unpack32(&job_resp->real_job_id, buffer);
			safe_unpackstr(&job_resp->sibling_name, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->jobs_cnt, buffer);
		msg->job_responses = xcalloc(msg->jobs_cnt,
					     sizeof(*msg->job_responses));
		for (int i = 0; i < msg->jobs_cnt; i++) {
			kill_jobs_resp_job_t *job_resp = &msg->job_responses[i];

			safe_unpack32(&job_resp->error_code, buffer);
			safe_unpackstr(&job_resp->error_msg, buffer);
			if (slurm_unpack_selected_step(&job_resp->id,
						       smsg->protocol_version,
						       buffer) != SLURM_SUCCESS)
				goto unpack_error;
			safe_unpack32(&job_resp->real_job_id, buffer);
			safe_unpackstr(&job_resp->sibling_name, buffer);
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_kill_jobs_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_forward_data_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	forward_data_msg_t *msg = smsg->data;

	packstr(msg->address, buffer);
	pack32(msg->len, buffer);
	packmem(msg->data, msg->len, buffer);
}

static int _unpack_forward_data_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp;
	forward_data_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpackstr(&msg->address, buffer);
	safe_unpack32(&msg->len, buffer);
	safe_unpackmem_xmalloc(&msg->data, &uint32_tmp, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_forward_data_msg(msg);
	return SLURM_ERROR;
}

static void _pack_ping_slurmd_resp(const slurm_msg_t *smsg, buf_t *buffer)
{
	ping_slurmd_resp_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->cpu_load, buffer);
		pack64(msg->free_mem, buffer);
	}
}

static int _unpack_ping_slurmd_resp(slurm_msg_t *smsg, buf_t *buffer)
{
	ping_slurmd_resp_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->cpu_load, buffer);
		safe_unpack64(&msg->free_mem, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_ping_slurmd_resp(msg);
	return SLURM_ERROR;
}

static void _pack_file_bcast(const slurm_msg_t *smsg, buf_t *buffer)
{
	file_bcast_msg_t *msg = smsg->data;

	grow_buf(buffer,  msg->block_len);

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->block_no, buffer);
		pack16(msg->compress, buffer);
		pack16(msg->flags, buffer);
		pack16(msg->modes, buffer);

		pack32(msg->uid, buffer);
		packstr(msg->user_name, buffer);
		pack32(msg->gid, buffer);

		pack_time(msg->atime, buffer);
		pack_time(msg->mtime, buffer);

		packstr(msg->fname, buffer);
		packstr(msg->exe_fname, buffer);
		pack32(msg->block_len, buffer);
		pack32(msg->uncomp_len, buffer);
		pack64(msg->block_offset, buffer);
		pack64(msg->file_size, buffer);
		packmem(msg->block, msg->block_len, buffer);
		pack_sbcast_cred(msg->cred, buffer, smsg->protocol_version);
	}
}

static int _unpack_file_bcast(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp = 0;
	file_bcast_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->block_no, buffer);
		safe_unpack16(&msg->compress, buffer);
		safe_unpack16(&msg->flags, buffer);
		safe_unpack16(&msg->modes, buffer);

		safe_unpack32(&msg->uid, buffer);
		safe_unpackstr(&msg->user_name, buffer);
		safe_unpack32(&msg->gid, buffer);

		safe_unpack_time(&msg->atime, buffer);
		safe_unpack_time(&msg->mtime, buffer);

		safe_unpackstr(&msg->fname, buffer);
		safe_unpackstr(&msg->exe_fname, buffer);
		safe_unpack32(&msg->block_len, buffer);
		safe_unpack32(&msg->uncomp_len, buffer);
		safe_unpack64(&msg->block_offset, buffer);
		safe_unpack64(&msg->file_size, buffer);
		safe_unpackmem_xmalloc(&msg->block, &uint32_tmp, buffer);
		if (uint32_tmp != msg->block_len)
			goto unpack_error;

		msg->cred =
			unpack_sbcast_cred(buffer, msg, smsg->protocol_version);
		if (msg->cred == NULL)
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_file_bcast_msg(msg);
	return SLURM_ERROR;
}

static void _pack_trigger_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	trigger_info_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->record_count, buffer);
		for (int i = 0; i < msg->record_count; i++) {
			pack16(msg->trigger_array[i].flags, buffer);
			pack32(msg->trigger_array[i].trig_id, buffer);
			pack16(msg->trigger_array[i].res_type, buffer);
			packstr(msg->trigger_array[i].res_id, buffer);
			pack32(msg->trigger_array[i].trig_type, buffer);
			pack32(msg->trigger_array[i].control_inx, buffer);
			pack16(msg->trigger_array[i].offset, buffer);
			pack32(msg->trigger_array[i].user_id, buffer);
			packstr(msg->trigger_array[i].program, buffer);
		}
	}
}

static int _unpack_trigger_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	trigger_info_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->record_count, buffer);
		safe_xcalloc(msg->trigger_array, msg->record_count,
			     sizeof(trigger_info_t));
		for (int i = 0; i < msg->record_count; i++) {
			safe_unpack16(&msg->trigger_array[i].flags, buffer);
			safe_unpack32(&msg->trigger_array[i].trig_id, buffer);
			safe_unpack16(&msg->trigger_array[i].res_type, buffer);
			safe_unpackstr(&msg->trigger_array[i].res_id, buffer);
			safe_unpack32(&msg->trigger_array[i].trig_type, buffer);
			safe_unpack32(&msg->trigger_array[i].control_inx, buffer);
			safe_unpack16(&msg->trigger_array[i].offset, buffer);
			safe_unpack32(&msg->trigger_array[i].user_id, buffer);
			safe_unpackstr(&msg->trigger_array[i].program, buffer);
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_trigger_msg(msg);
	return SLURM_ERROR;
}

static void _pack_kvs_host_rec(struct kvs_hosts *msg_ptr, buf_t *buffer,
			       uint16_t protocol_version)
{
	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(msg_ptr->task_id, buffer);
		pack16(msg_ptr->port, buffer);
		packstr(msg_ptr->hostname, buffer);
		packstr(msg_ptr->tls_cert, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg_ptr->task_id, buffer);
		pack16(msg_ptr->port, buffer);
		packstr(msg_ptr->hostname, buffer);
	}
}

static int _unpack_kvs_host_rec(struct kvs_hosts *msg_ptr, buf_t *buffer,
				uint16_t protocol_version)
{
	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->task_id, buffer);
		safe_unpack16(&msg_ptr->port, buffer);
		safe_unpackstr(&msg_ptr->hostname, buffer);
		safe_unpackstr(&msg_ptr->tls_cert, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->task_id, buffer);
		safe_unpack16(&msg_ptr->port, buffer);
		safe_unpackstr(&msg_ptr->hostname, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

static void _pack_kvs_rec(struct kvs_comm *msg_ptr, buf_t *buffer,
			  uint16_t protocol_version)
{
	int i;
	xassert(msg_ptr);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg_ptr->kvs_name, buffer);
		pack32(msg_ptr->kvs_cnt, buffer);
		for (i = 0; i < msg_ptr->kvs_cnt; i++) {
			packstr(msg_ptr->kvs_keys[i], buffer);
			packstr(msg_ptr->kvs_values[i], buffer);
		}
	}
}
static int  _unpack_kvs_rec(struct kvs_comm **msg_ptr, buf_t *buffer,
			    uint16_t protocol_version)
{
	int i;
	struct kvs_comm *msg;

	msg = xmalloc(sizeof(struct kvs_comm));
	*msg_ptr = msg;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->kvs_name, buffer);
		safe_unpack32(&msg->kvs_cnt, buffer);
		if (msg->kvs_cnt > NO_VAL)
			goto unpack_error;
		safe_xcalloc(msg->kvs_keys, msg->kvs_cnt, sizeof(char *));
		safe_xcalloc(msg->kvs_values, msg->kvs_cnt, sizeof(char *));
		for (i = 0; i < msg->kvs_cnt; i++) {
			safe_unpackstr(&msg->kvs_keys[i], buffer);
			safe_unpackstr(&msg->kvs_values[i], buffer);
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

static void _pack_kvs_data(const slurm_msg_t *smsg, buf_t *buffer)
{
	kvs_comm_set_t *msg = smsg->data;

	pack16(msg->host_cnt, buffer);
	for (int i = 0; i < msg->host_cnt; i++)
		_pack_kvs_host_rec(&msg->kvs_host_ptr[i], buffer,
				   smsg->protocol_version);

	pack16(msg->kvs_comm_recs, buffer);
	for (int i = 0; i < msg->kvs_comm_recs; i++)
		_pack_kvs_rec(msg->kvs_comm_ptr[i], buffer,
			      smsg->protocol_version);
}

static int _unpack_kvs_data(slurm_msg_t *smsg, buf_t *buffer)
{
	kvs_comm_set_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->host_cnt, buffer);
		if (msg->host_cnt > NO_VAL16)
			goto unpack_error;
		safe_xcalloc(msg->kvs_host_ptr, msg->host_cnt,
			     sizeof(struct kvs_hosts));
		for (int i = 0; i < msg->host_cnt; i++) {
			if (_unpack_kvs_host_rec(&msg->kvs_host_ptr[i], buffer,
						 smsg->protocol_version))
				goto unpack_error;
		}

		safe_unpack16(&msg->kvs_comm_recs, buffer);
		if (msg->kvs_comm_recs > NO_VAL16)
			goto unpack_error;
		safe_xcalloc(msg->kvs_comm_ptr, msg->kvs_comm_recs,
			     sizeof(struct kvs_comm *));
		for (int i = 0; i < msg->kvs_comm_recs; i++) {
			if (_unpack_kvs_rec(&msg->kvs_comm_ptr[i], buffer,
					    smsg->protocol_version))
				goto unpack_error;
		}
	}
	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_kvs_comm_set(msg);
	return SLURM_ERROR;
}

static void _pack_kvs_get(const slurm_msg_t *smsg, buf_t *buffer)
{
	kvs_get_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(msg->task_id, buffer);
		pack32(msg->size, buffer);
		pack16(msg->port, buffer);
		packstr(msg->hostname, buffer);
		packstr(msg->tls_cert, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->task_id, buffer);
		pack32(msg->size, buffer);
		pack16(msg->port, buffer);
		packstr(msg->hostname, buffer);
	}
}

static int _unpack_kvs_get(slurm_msg_t *smsg, buf_t *buffer)
{
	kvs_get_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->task_id, buffer);
		safe_unpack32(&msg->size, buffer);
		safe_unpack16(&msg->port, buffer);
		safe_unpackstr(&msg->hostname, buffer);
		safe_unpackstr(&msg->tls_cert, buffer);
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->task_id, buffer);
		safe_unpack32(&msg->size, buffer);
		safe_unpack16(&msg->port, buffer);
		safe_unpackstr(&msg->hostname, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_get_kvs_msg(msg);
	return SLURM_ERROR;
}

extern void
pack_multi_core_data (multi_core_data_t *multi_core, buf_t *buffer,
		      uint16_t protocol_version)
{
	if (multi_core == NULL) {
		pack8((uint8_t) 0, buffer);	/* flag as Empty */
		return;
	}

	pack8((uint8_t) 0xff, buffer);		/* flag as Full */

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(multi_core->boards_per_node,  buffer);
		pack16(multi_core->sockets_per_board, buffer);
		pack16(multi_core->sockets_per_node, buffer);
		pack16(multi_core->cores_per_socket, buffer);
		pack16(multi_core->threads_per_core, buffer);

		pack16(multi_core->ntasks_per_board,  buffer);
		pack16(multi_core->ntasks_per_socket, buffer);
		pack16(multi_core->ntasks_per_core,   buffer);
		pack16(multi_core->plane_size,        buffer);
	}
}

extern int
unpack_multi_core_data (multi_core_data_t **mc_ptr, buf_t *buffer,
			uint16_t protocol_version)
{
	uint8_t flag;
	multi_core_data_t *multi_core = NULL;

	*mc_ptr = NULL;
	safe_unpack8(&flag, buffer);
	if (flag == 0)
		return SLURM_SUCCESS;
	if (flag != 0xff)
		return SLURM_ERROR;

	multi_core = xmalloc(sizeof(multi_core_data_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&multi_core->boards_per_node,  buffer);
		safe_unpack16(&multi_core->sockets_per_board, buffer);
		safe_unpack16(&multi_core->sockets_per_node, buffer);
		safe_unpack16(&multi_core->cores_per_socket, buffer);
		safe_unpack16(&multi_core->threads_per_core, buffer);
		safe_unpack16(&multi_core->ntasks_per_board,  buffer);
		safe_unpack16(&multi_core->ntasks_per_socket, buffer);
		safe_unpack16(&multi_core->ntasks_per_core,   buffer);
		safe_unpack16(&multi_core->plane_size,        buffer);
	}

	*mc_ptr = multi_core;
	return SLURM_SUCCESS;

unpack_error:
	xfree(multi_core);
	return SLURM_ERROR;
}

static void _pack_slurmd_status(const slurm_msg_t *smsg, buf_t *buffer)
{
	slurmd_status_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->booted, buffer);
		pack_time(msg->last_slurmctld_msg, buffer);

		pack16(msg->slurmd_debug, buffer);
		pack16(msg->actual_cpus, buffer);
		pack16(msg->actual_boards, buffer);
		pack16(msg->actual_sockets, buffer);
		pack16(msg->actual_cores, buffer);
		pack16(msg->actual_threads, buffer);

		pack64(msg->actual_real_mem, buffer);
		pack32(msg->actual_tmp_disk, buffer);
		pack32(msg->pid, buffer);

		packstr(msg->hostname, buffer);
		packstr(msg->slurmd_logfile, buffer);
		packstr(msg->step_list, buffer);
		packstr(msg->version, buffer);
	}
}

static int _unpack_slurmd_status(slurm_msg_t *smsg, buf_t *buffer)
{
	slurmd_status_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&msg->booted, buffer);
		safe_unpack_time(&msg->last_slurmctld_msg, buffer);

		safe_unpack16(&msg->slurmd_debug, buffer);
		safe_unpack16(&msg->actual_cpus, buffer);
		safe_unpack16(&msg->actual_boards, buffer);
		safe_unpack16(&msg->actual_sockets, buffer);
		safe_unpack16(&msg->actual_cores, buffer);
		safe_unpack16(&msg->actual_threads, buffer);

		safe_unpack64(&msg->actual_real_mem, buffer);
		safe_unpack32(&msg->actual_tmp_disk, buffer);
		safe_unpack32(&msg->pid, buffer);

		safe_unpackstr(&msg->hostname, buffer);
		safe_unpackstr(&msg->slurmd_logfile, buffer);
		safe_unpackstr(&msg->step_list, buffer);
		safe_unpackstr(&msg->version, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_slurmd_status(msg);
	return SLURM_ERROR;
}

static void _pack_job_notify(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_notify_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->message, buffer);
	}
}

static int _unpack_job_notify(slurm_msg_t *smsg, buf_t *buffer)
{
	job_notify_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->message, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_notify_msg(msg);
	return SLURM_ERROR;
}

static void _pack_set_debug_flags_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	set_debug_flags_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack64(msg->debug_flags_minus, buffer);
		pack64(msg->debug_flags_plus, buffer);
	}
}

static int _unpack_set_debug_flags_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	set_debug_flags_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64(&msg->debug_flags_minus, buffer);
		safe_unpack64(&msg->debug_flags_plus, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_set_debug_flags_msg(msg);
	return SLURM_ERROR;
}

static void _pack_set_debug_level_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	set_debug_level_msg_t *msg = smsg->data;

	pack32(msg->debug_level, buffer);
}

static int _unpack_set_debug_level_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	set_debug_level_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpack32(&msg->debug_level, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_set_debug_level_msg(msg);
	return SLURM_ERROR;
}

static void _pack_suspend_exc_update_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	suspend_exc_update_msg_t *msg = smsg->data;

	packstr(msg->update_str, buffer);
	pack32(msg->mode, buffer);
}

static int _unpack_suspend_exc_update_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	suspend_exc_update_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpackstr(&msg->update_str, buffer);
	safe_unpack32(&msg->mode, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_suspend_exc_update_msg(msg);
	return SLURM_ERROR;
}

static void _pack_will_run_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	will_run_response_msg_t *msg = smsg->data;
	uint32_t count = NO_VAL, *job_id_ptr;

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
		packstr(msg->job_submit_user_msg, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->part_name, buffer);

		if (msg->preemptee_job_id)
			count = list_count(msg->preemptee_job_id);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			list_itr_t *itr =
				list_iterator_create(msg->preemptee_job_id);
			while ((job_id_ptr = list_next(itr)))
				pack32(job_id_ptr[0], buffer);
			list_iterator_destroy(itr);
		}

		pack32(msg->proc_cnt, buffer);
		pack_time(msg->start_time, buffer);
		packdouble(0, buffer); /* was sys_usage_per */
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->step_id.job_id, buffer);
		packstr(msg->job_submit_user_msg, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->part_name, buffer);

		if (msg->preemptee_job_id)
			count = list_count(msg->preemptee_job_id);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			list_itr_t *itr =
				list_iterator_create(msg->preemptee_job_id);
			while ((job_id_ptr = list_next(itr)))
				pack32(job_id_ptr[0], buffer);
			list_iterator_destroy(itr);
		}

		pack32(msg->proc_cnt, buffer);
		pack_time(msg->start_time, buffer);
		packdouble(0, buffer); /* was sys_usage_per */
	}
}

static int _unpack_will_run_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t count, uint32_tmp, *job_id_ptr;
	double double_tmp;
	will_run_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
		safe_unpackstr(&msg->job_submit_user_msg, buffer);
		safe_unpackstr(&msg->node_list, buffer);
		safe_unpackstr(&msg->part_name, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			msg->preemptee_job_id = list_create(xfree_ptr);
			for (int i = 0; i < count; i++) {
				safe_unpack32(&uint32_tmp, buffer);
				job_id_ptr = xmalloc(sizeof(uint32_t));
				job_id_ptr[0] = uint32_tmp;
				list_append(msg->preemptee_job_id, job_id_ptr);
			}
		}

		safe_unpack32(&msg->proc_cnt, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpackdouble(&double_tmp, buffer); /* was sys_usage_per */
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg->step_id = SLURM_STEP_ID_INITIALIZER;
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpackstr(&msg->job_submit_user_msg, buffer);
		safe_unpackstr(&msg->node_list, buffer);
		safe_unpackstr(&msg->part_name, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			msg->preemptee_job_id = list_create(xfree_ptr);
			for (int i = 0; i < count; i++) {
				safe_unpack32(&uint32_tmp, buffer);
				job_id_ptr = xmalloc(sizeof(uint32_t));
				job_id_ptr[0] = uint32_tmp;
				list_append(msg->preemptee_job_id, job_id_ptr);
			}
		}

		safe_unpack32(&msg->proc_cnt, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpackdouble(&double_tmp, buffer); /* was sys_usage_per */
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_will_run_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_accounting_update_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	accounting_update_msg_t *msg = smsg->data;
	uint32_t count = 0;
	list_itr_t *itr = NULL;
	slurmdb_update_object_t *rec = NULL;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (msg->update_list)
			count = list_count(msg->update_list);

		pack32(count, buffer);

		if (count) {
			itr = list_iterator_create(msg->update_list);
			while ((rec = list_next(itr))) {
				slurmdb_pack_update_object(
					rec, smsg->protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
	}
}

static int _unpack_accounting_update_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t count = 0;
	slurmdb_update_object_t *rec = NULL;
	accounting_update_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		msg->update_list = list_create(slurmdb_destroy_update_object);
		for (int i = 0; i < count; i++) {
			if ((slurmdb_unpack_update_object(
				    &rec, smsg->protocol_version, buffer)) ==
			    SLURM_ERROR)
				goto unpack_error;
			list_append(msg->update_list, rec);
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_accounting_update_msg(msg);
	return SLURM_ERROR;
}

static void _pack_topo_info_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	topo_info_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		topology_g_topoinfo_pack(msg->topo_info, buffer,
					 smsg->protocol_version);
	}
}

static int _unpack_topo_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	topo_info_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (topology_g_topoinfo_unpack((dynamic_plugin_data_t **) &msg
						       ->topo_info,
					       buffer, smsg->protocol_version))
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_topo_info_msg(msg);
	return SLURM_ERROR;
}

static void _pack_topo_info_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	topo_info_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
	}
}

static int _unpack_topo_info_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	topo_info_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->name, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_topo_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_topo_config_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	topo_config_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		packstr(msg->config, buffer);
	}
}

static int _unpack_topo_config_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	topo_config_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->config, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_topo_config_msg(msg);
	return SLURM_ERROR;
}

static void _pack_stats_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	stats_info_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->command_id, buffer);
	}
}

static int _unpack_stats_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	stats_info_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->command_id, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_stats_info_request_msg(msg);
	return SLURM_ERROR;
}

static int _unpack_stats_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp = 0;
	stats_info_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack_time(&msg->req_time, buffer);
		safe_unpack_time(&msg->req_time_start, buffer);
		safe_unpack32(&msg->server_thread_count, buffer);
		safe_unpack32(&msg->agent_queue_size, buffer);
		safe_unpack32(&msg->agent_count, buffer);
		safe_unpack32(&msg->agent_thread_count, buffer);
		safe_unpack32(&msg->dbd_agent_queue_size, buffer);
		safe_unpack32(&msg->gettimeofday_latency, buffer);
		safe_unpack32(&msg->jobs_submitted, buffer);
		safe_unpack32(&msg->jobs_started, buffer);
		safe_unpack32(&msg->jobs_completed, buffer);
		safe_unpack32(&msg->jobs_canceled, buffer);
		safe_unpack32(&msg->jobs_failed, buffer);
		safe_unpack32(&msg->jobs_pending, buffer);
		safe_unpack32(&msg->jobs_running, buffer);
		safe_unpack_time(&msg->job_states_ts, buffer);

		safe_unpack32(&msg->schedule_cycle_max, buffer);
		safe_unpack32(&msg->schedule_cycle_last, buffer);
		safe_unpack64(&msg->schedule_cycle_sum, buffer);
		safe_unpack32(&msg->schedule_cycle_counter, buffer);
		safe_unpack32(&msg->schedule_cycle_depth, buffer);
		safe_unpack32_array(&msg->schedule_exit,
				    &msg->schedule_exit_cnt, buffer);
		safe_unpack32(&msg->schedule_queue_len, buffer);

		safe_unpack32(&msg->bf_backfilled_jobs, buffer);
		safe_unpack32(&msg->bf_last_backfilled_jobs, buffer);
		safe_unpack32(&msg->bf_cycle_counter, buffer);
		safe_unpack64(&msg->bf_cycle_sum, buffer);
		safe_unpack32(&msg->bf_cycle_last, buffer);
		safe_unpack32(&msg->bf_last_depth, buffer);
		safe_unpack32(&msg->bf_last_depth_try, buffer);

		safe_unpack32(&msg->bf_queue_len, buffer);
		safe_unpack32(&msg->bf_cycle_max, buffer);
		safe_unpack_time(&msg->bf_when_last_cycle, buffer);
		safe_unpack32(&msg->bf_depth_sum, buffer);
		safe_unpack32(&msg->bf_depth_try_sum, buffer);
		safe_unpack32(&msg->bf_queue_len_sum, buffer);
		safe_unpack32(&msg->bf_table_size, buffer);
		safe_unpack32(&msg->bf_table_size_sum, buffer);

		safe_unpack32(&msg->bf_active, buffer);
		safe_unpack32(&msg->bf_backfilled_het_jobs, buffer);
		safe_unpack32_array(&msg->bf_exit, &msg->bf_exit_cnt, buffer);

		safe_unpack32(&msg->rpc_type_size, buffer);
		safe_unpack16_array(&msg->rpc_type_id, &uint32_tmp, buffer);
		safe_unpack32_array(&msg->rpc_type_cnt, &uint32_tmp, buffer);
		safe_unpack64_array(&msg->rpc_type_time, &uint32_tmp, buffer);

		safe_unpack8(&msg->rpc_queue_enabled, buffer);
		if (msg->rpc_queue_enabled) {
			safe_unpack16_array(&msg->rpc_type_queued, &uint32_tmp,
					    buffer);
			safe_unpack64_array(&msg->rpc_type_dropped, &uint32_tmp,
					    buffer);
			safe_unpack16_array(&msg->rpc_type_cycle_last,
					    &uint32_tmp, buffer);
			safe_unpack16_array(&msg->rpc_type_cycle_max,
					    &uint32_tmp, buffer);
		}

		safe_unpack32(&msg->rpc_user_size, buffer);
		safe_unpack32_array(&msg->rpc_user_id, &uint32_tmp, buffer);
		safe_unpack32_array(&msg->rpc_user_cnt, &uint32_tmp, buffer);
		safe_unpack64_array(&msg->rpc_user_time, &uint32_tmp, buffer);

		safe_unpack32_array(&msg->rpc_queue_type_id,
				    &msg->rpc_queue_type_count, buffer);
		safe_unpack32_array(&msg->rpc_queue_count, &uint32_tmp, buffer);
		if (uint32_tmp != msg->rpc_queue_type_count)
			goto unpack_error;

		safe_unpack32_array(&msg->rpc_dump_types, &msg->rpc_dump_count,
				    buffer);
		safe_unpackstr_array(&msg->rpc_dump_hostlist, &uint32_tmp,
				     buffer);
		if (uint32_tmp != msg->rpc_dump_count)
			goto unpack_error;
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&uint32_tmp, buffer); /* was parts_packed */
		safe_unpack_time(&msg->req_time, buffer);
		safe_unpack_time(&msg->req_time_start, buffer);
		safe_unpack32(&msg->server_thread_count, buffer);
		safe_unpack32(&msg->agent_queue_size, buffer);
		safe_unpack32(&msg->agent_count, buffer);
		safe_unpack32(&msg->agent_thread_count, buffer);
		safe_unpack32(&msg->dbd_agent_queue_size, buffer);
		safe_unpack32(&msg->gettimeofday_latency, buffer);
		safe_unpack32(&msg->jobs_submitted, buffer);
		safe_unpack32(&msg->jobs_started, buffer);
		safe_unpack32(&msg->jobs_completed, buffer);
		safe_unpack32(&msg->jobs_canceled, buffer);
		safe_unpack32(&msg->jobs_failed, buffer);
		safe_unpack32(&msg->jobs_pending, buffer);
		safe_unpack32(&msg->jobs_running, buffer);
		safe_unpack_time(&msg->job_states_ts, buffer);

		safe_unpack32(&msg->schedule_cycle_max, buffer);
		safe_unpack32(&msg->schedule_cycle_last, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		msg->schedule_cycle_sum = uint32_tmp;
		safe_unpack32(&msg->schedule_cycle_counter, buffer);
		safe_unpack32(&msg->schedule_cycle_depth, buffer);
		safe_unpack32_array(&msg->schedule_exit,
				    &msg->schedule_exit_cnt, buffer);
		safe_unpack32(&msg->schedule_queue_len, buffer);

		safe_unpack32(&msg->bf_backfilled_jobs, buffer);
		safe_unpack32(&msg->bf_last_backfilled_jobs, buffer);
		safe_unpack32(&msg->bf_cycle_counter, buffer);
		safe_unpack64(&msg->bf_cycle_sum, buffer);
		safe_unpack32(&msg->bf_cycle_last, buffer);
		safe_unpack32(&msg->bf_last_depth, buffer);
		safe_unpack32(&msg->bf_last_depth_try, buffer);

		safe_unpack32(&msg->bf_queue_len, buffer);
		safe_unpack32(&msg->bf_cycle_max, buffer);
		safe_unpack_time(&msg->bf_when_last_cycle, buffer);
		safe_unpack32(&msg->bf_depth_sum, buffer);
		safe_unpack32(&msg->bf_depth_try_sum, buffer);
		safe_unpack32(&msg->bf_queue_len_sum, buffer);
		safe_unpack32(&msg->bf_table_size, buffer);
		safe_unpack32(&msg->bf_table_size_sum, buffer);

		safe_unpack32(&msg->bf_active, buffer);
		safe_unpack32(&msg->bf_backfilled_het_jobs, buffer);
		safe_unpack32_array(&msg->bf_exit, &msg->bf_exit_cnt, buffer);

		safe_unpack32(&msg->rpc_type_size, buffer);
		safe_unpack16_array(&msg->rpc_type_id, &uint32_tmp, buffer);
		safe_unpack32_array(&msg->rpc_type_cnt, &uint32_tmp, buffer);
		safe_unpack64_array(&msg->rpc_type_time, &uint32_tmp, buffer);

		safe_unpack8(&msg->rpc_queue_enabled, buffer);
		if (msg->rpc_queue_enabled) {
			safe_unpack16_array(&msg->rpc_type_queued,
					    &uint32_tmp, buffer);
			safe_unpack64_array(&msg->rpc_type_dropped,
					    &uint32_tmp, buffer);
			safe_unpack16_array(&msg->rpc_type_cycle_last,
					    &uint32_tmp, buffer);
			safe_unpack16_array(&msg->rpc_type_cycle_max,
					    &uint32_tmp, buffer);
		}

		safe_unpack32(&msg->rpc_user_size, buffer);
		safe_unpack32_array(&msg->rpc_user_id, &uint32_tmp, buffer);
		safe_unpack32_array(&msg->rpc_user_cnt, &uint32_tmp, buffer);
		safe_unpack64_array(&msg->rpc_user_time, &uint32_tmp, buffer);

		safe_unpack32_array(&msg->rpc_queue_type_id,
				    &msg->rpc_queue_type_count,
				    buffer);
		safe_unpack32_array(&msg->rpc_queue_count,
				    &uint32_tmp, buffer);
		if (uint32_tmp != msg->rpc_queue_type_count)
			goto unpack_error;

		safe_unpack32_array(&msg->rpc_dump_types,
				    &msg->rpc_dump_count,
				    buffer);
		safe_unpackstr_array(&msg->rpc_dump_hostlist,
				     &uint32_tmp,
				     buffer);
		if (uint32_tmp != msg->rpc_dump_count)
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_stats_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_license_info_request_msg(const slurm_msg_t *smsg,
					   buf_t *buffer)
{
	license_info_request_msg_t *msg = smsg->data;

	pack_time(msg->last_update, buffer);
	pack16(msg->show_flags, buffer);
}

static int _unpack_license_info_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	license_info_request_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpack_time(&msg->last_update, buffer);
	safe_unpack16(&msg->show_flags, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_license_info_request_msg(msg);
	return SLURM_ERROR;
}

static int _unpack_license_info_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	license_info_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpack32(&msg->num_lic, buffer);
		safe_unpack_time(&msg->last_update, buffer);

		safe_xcalloc(msg->lic_array, msg->num_lic,
			     sizeof(slurm_license_info_t));

		/* Decode individual license data */
		for (int i = 0; i < msg->num_lic; i++) {
			safe_unpackstr(&msg->lic_array[i].name, buffer);
			safe_unpack32(&msg->lic_array[i].total, buffer);
			safe_unpack32(&msg->lic_array[i].in_use, buffer);
			safe_unpack32(&msg->lic_array[i].reserved, buffer);
			safe_unpack8(&msg->lic_array[i].remote, buffer);
			safe_unpack32(&msg->lic_array[i].last_consumed, buffer);
			safe_unpack32(&msg->lic_array[i].last_deficit, buffer);
			safe_unpack_time(&msg->lic_array[i].last_update,
					 buffer);

			/* The total number of licenses can decrease
			 * at runtime.
			 */
			if (msg->lic_array[i].total == INFINITE)
				msg->lic_array[i].available = INFINITE;
			else if (msg->lic_array[i].total <
				 (msg->lic_array[i].in_use +
				  msg->lic_array[i].last_deficit))
				msg->lic_array[i].available = 0;
			else
				msg->lic_array[i].available =
					msg->lic_array[i].total -
					msg->lic_array[i].in_use -
					msg->lic_array[i].last_deficit;
			safe_unpack8(&msg->lic_array[i].mode, buffer);
			safe_unpackstr(&msg->lic_array[i].nodes, buffer);
		}
	} else if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->num_lic, buffer);
		safe_unpack_time(&msg->last_update, buffer);

		safe_xcalloc(msg->lic_array, msg->num_lic,
			     sizeof(slurm_license_info_t));

		/* Decode individual license data */
		for (int i = 0; i < msg->num_lic; i++) {
			safe_unpackstr(&msg->lic_array[i].name, buffer);
			safe_unpack32(&msg->lic_array[i].total, buffer);
			safe_unpack32(&msg->lic_array[i].in_use, buffer);
			safe_unpack32(&msg->lic_array[i].reserved, buffer);
			safe_unpack8(&msg->lic_array[i].remote, buffer);
			safe_unpack32(&msg->lic_array[i].last_consumed, buffer);
			safe_unpack32(&msg->lic_array[i].last_deficit, buffer);
			safe_unpack_time(&msg->lic_array[i].last_update,
					 buffer);

			/* The total number of licenses can decrease
			 * at runtime.
			 */
			if (msg->lic_array[i].total <
				(msg->lic_array[i].in_use +
				 msg->lic_array[i].last_deficit))
				msg->lic_array[i].available = 0;
			else
				msg->lic_array[i].available =
					msg->lic_array[i].total -
					msg->lic_array[i].in_use -
					msg->lic_array[i].last_deficit;
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_license_info_msg(msg);
	return SLURM_ERROR;
}

static void _pack_job_array_resp_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	job_array_resp_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!msg) {
			pack32(0, buffer);
			return;
		}

		pack32(msg->job_array_count, buffer);
		for (int i = 0; i < msg->job_array_count; i++) {
			pack32(msg->error_code[i], buffer);
			packstr(msg->job_array_id[i], buffer);
			packstr(msg->err_msg[i], buffer);
		}
	}
}

static int _unpack_job_array_resp_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	job_array_resp_msg_t *resp = xmalloc(sizeof(*resp));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&resp->job_array_count, buffer);
		if (resp->job_array_count > NO_VAL)
			goto unpack_error;
		safe_xcalloc(resp->error_code, resp->job_array_count,
			     sizeof(uint32_t));
		safe_xcalloc(resp->job_array_id, resp->job_array_count,
			     sizeof(char *));
		safe_xcalloc(resp->err_msg, resp->job_array_count,
			     sizeof(char *));
		for (int i = 0; i < resp->job_array_count; i++) {
			safe_unpack32(&resp->error_code[i], buffer);
			safe_unpackstr(&resp->job_array_id[i], buffer);
			safe_unpackstr(&resp->err_msg[i], buffer);
		}
	}

	smsg->data = resp;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_array_resp(resp);
	return SLURM_ERROR;
}

static void _pack_assoc_mgr_info_request_msg(const slurm_msg_t *smsg,
					     buf_t *buffer)
{
	assoc_mgr_info_request_msg_t *msg = smsg->data;
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	list_itr_t *itr = NULL;

	if (!msg->acct_list || !(count = list_count(msg->acct_list)))
		count = NO_VAL;

	pack32(count, buffer);
	if (count != NO_VAL) {
		itr = list_iterator_create(msg->acct_list);
		while ((tmp_info = list_next(itr)))
			packstr(tmp_info, buffer);
		list_iterator_destroy(itr);
	}

	pack32(msg->flags, buffer);

	if (!msg->qos_list || !(count = list_count(msg->qos_list)))
		count = NO_VAL;

	pack32(count, buffer);
	if (count != NO_VAL) {
		itr = list_iterator_create(msg->qos_list);
		while ((tmp_info = list_next(itr)))
			packstr(tmp_info, buffer);
		list_iterator_destroy(itr);
	}

	if (!msg->user_list || !(count = list_count(msg->user_list)))
		count = NO_VAL;

	pack32(count, buffer);
	if (count != NO_VAL) {
		itr = list_iterator_create(msg->user_list);
		while ((tmp_info = list_next(itr)))
			packstr(tmp_info, buffer);
		list_iterator_destroy(itr);
	}
}

static int _unpack_assoc_mgr_info_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	assoc_mgr_info_request_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		msg->acct_list = list_create(xfree_ptr);
		for (int i = 0; i < count; i++) {
			safe_unpackstr(&tmp_info, buffer);
			list_append(msg->acct_list, tmp_info);
		}
	}

	safe_unpack32(&msg->flags, buffer);

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		msg->qos_list = list_create(xfree_ptr);
		for (int i = 0; i < count; i++) {
			safe_unpackstr(&tmp_info, buffer);
			list_append(msg->qos_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		msg->user_list = list_create(xfree_ptr);
		for (int i = 0; i < count; i++) {
			safe_unpackstr(&tmp_info, buffer);
			list_append(msg->user_list, tmp_info);
		}
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_assoc_mgr_info_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_buf_list_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	ctld_list_msg_t *msg = smsg->data;
	list_itr_t *iter = NULL;
	buf_t *req_buf;
	uint32_t size;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		size = list_count(msg->my_list);
		pack32(size, buffer);
		iter = list_iterator_create(msg->my_list);
		while ((req_buf = list_next(iter))) {
			size = get_buf_offset(req_buf);
			pack32(size, buffer);
			packmem((char *) get_buf_data(req_buf), size,
				buffer);
		}
		list_iterator_destroy(iter);
	}
}

/* Free buf_t *record from a list */
static void _ctld_free_list_msg(void *x)
{
	FREE_NULL_BUFFER(x);
}

static int _unpack_buf_list_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t list_size = 0, buf_size = 0, read_size = 0;
	char *data = NULL;
	buf_t *req_buf;
	ctld_list_msg_t *object_ptr = xmalloc(sizeof(*object_ptr));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&list_size, buffer);
		if (list_size >= NO_VAL)
			goto unpack_error;
		object_ptr->my_list = list_create(_ctld_free_list_msg);
		for (int i = 0; i < list_size; i++) {
			safe_unpack32(&buf_size, buffer);
			safe_unpackmem_xmalloc(&data, &read_size, buffer);
			if (buf_size != read_size)
				goto unpack_error;
			/* Move "data" into "req_buf", NOT a memory leak */
			req_buf = create_buf(data, buf_size);
			data = NULL; /* just to be safe */
			list_append(object_ptr->my_list, req_buf);
		}
	}

	smsg->data = object_ptr;
	return SLURM_SUCCESS;

unpack_error:
	xfree(data);
	slurm_free_ctld_multi_msg(object_ptr);
	return SLURM_ERROR;
}

static void _pack_set_fs_dampening_factor_msg(const slurm_msg_t *smsg,
					      buf_t *buffer)
{
	set_fs_dampening_factor_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION)
		pack16(msg->dampening_factor, buffer);
}


static int _unpack_set_fs_dampening_factor_msg(slurm_msg_t *smsg,
					       buf_t *buffer)
{
	set_fs_dampening_factor_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION)
		safe_unpack16(&msg->dampening_factor, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_set_fs_dampening_factor_msg(msg);
	return SLURM_ERROR;
}

static void _pack_control_status_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	control_status_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->backup_inx, buffer);
		pack_time(msg->control_time, buffer);
	}
}

static int _unpack_control_status_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	control_status_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->backup_inx, buffer);
		safe_unpack_time(&msg->control_time, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_control_status_msg(msg);
	return SLURM_ERROR;
}

static void _pack_bb_status_req_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	bb_status_req_msg_t *msg = smsg->data;

	packstr_array(msg->argv, msg->argc, buffer);
}

static int _unpack_bb_status_req_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	bb_status_req_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpackstr_array(&msg->argv, &msg->argc, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_bb_status_req_msg(msg);
	return SLURM_ERROR;
}

static void _pack_bb_status_resp_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	bb_status_resp_msg_t *msg = smsg->data;

	packstr(msg->status_resp, buffer);
}

static int _unpack_bb_status_resp_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	bb_status_resp_msg_t *msg = xmalloc(sizeof(*msg));

	safe_unpackstr(&msg->status_resp, buffer);

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_bb_status_resp_msg(msg);
	return SLURM_ERROR;
}

static void _pack_crontab_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->uid, buffer);
	}
}

static int _unpack_crontab_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->uid, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_crontab_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_crontab_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->crontab, buffer);
		packstr(msg->disabled_lines, buffer);
	}
}

static int _unpack_crontab_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->crontab, buffer);
		safe_unpackstr(&msg->disabled_lines, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_crontab_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_crontab_update_request_msg(const slurm_msg_t *smsg,
					     buf_t *buffer)
{
	crontab_update_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		slurm_msg_t msg_wrapper = {
			.data = msg->jobs,
			.protocol_version = smsg->protocol_version,
		};

		packstr(msg->crontab, buffer);
		_pack_job_desc_list_msg(&msg_wrapper, buffer);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
	}
}

static int _unpack_crontab_update_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_update_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->crontab, buffer);
		if (_unpack_job_desc_list_msg(&msg->jobs, buffer,
					      smsg->protocol_version))
			goto unpack_error;
		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_crontab_update_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_crontab_update_response_msg(const slurm_msg_t *smsg,
					      buf_t *buffer)
{
	crontab_update_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->err_msg, buffer);
		packstr(msg->job_submit_user_msg, buffer);
		packstr(msg->failed_lines, buffer);
		pack32_array(msg->jobids, msg->jobids_count, buffer);
		pack32(msg->return_code, buffer);
	}
}

static int _unpack_crontab_update_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_update_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->err_msg, buffer);
		safe_unpackstr(&msg->job_submit_user_msg, buffer);
		safe_unpackstr(&msg->failed_lines, buffer);
		safe_unpack32_array(&msg->jobids, &msg->jobids_count, buffer);
		safe_unpack32(&msg->return_code, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_crontab_update_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_tls_cert_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	tls_cert_request_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->csr, buffer);
		packstr(msg->node_name, buffer);
		packstr(msg->token, buffer);
	}
}

static int _unpack_tls_cert_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	tls_cert_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->csr, buffer);
		safe_unpackstr(&msg->node_name, buffer);
		safe_unpackstr(&msg->token, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_tls_cert_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_tls_cert_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	tls_cert_response_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->signed_cert, buffer);
	}
}

static int _unpack_tls_cert_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	tls_cert_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->signed_cert, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_tls_cert_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_container_id_request_msg(const slurm_msg_t *smsg,
					   buf_t *buffer)
{
	container_id_request_msg_t *msg = smsg->data;
	xassert(msg);

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->show_flags, buffer);
		packstr(msg->container_id, buffer);
		pack32(msg->uid, buffer);
	}
}

static int _unpack_container_id_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	container_id_request_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->show_flags, buffer);
		safe_unpackstr(&msg->container_id, buffer);
		safe_unpack32(&msg->uid, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_container_id_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_each_container_id_step(void *object,
					 uint16_t protocol_version,
					 buf_t *buffer)
{
	slurm_step_id_t *step = object;
	pack_step_id(step, buffer, protocol_version);
}

static void _pack_container_id_response_msg(const slurm_msg_t *smsg,
					    buf_t *buffer)
{
	container_id_response_msg_t *msg = smsg->data;
	xassert(msg);

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		(void) slurm_pack_list(msg->steps,
				       _pack_each_container_id_step,
				       buffer, smsg->protocol_version);
	}
}

static int _unpack_each_container_id(void **object, uint16_t protocol_version,
				     buf_t *buffer)
{
	slurm_step_id_t *step = xmalloc(sizeof(*step));

	safe_unpack_step_id_members(step, buffer, protocol_version);

	*object = step;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_step_id(step);
	return SLURM_ERROR;
}

static int _unpack_container_id_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	container_id_response_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (slurm_unpack_list(&msg->steps, _unpack_each_container_id,
				      (ListDelF) slurm_free_step_id, buffer,
				      smsg->protocol_version))
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_container_id_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_container_state_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	container_state_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->oci_version, buffer);
		packstr(msg->id, buffer);
		pack32(msg->status, buffer);
		pack32(msg->pid, buffer);
		packstr(msg->bundle, buffer);
		pack_key_pair_list(msg->annotations, smsg->protocol_version,
				   buffer);
	}
}

static int _unpack_container_state_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	container_state_msg_t *msg = slurm_create_container_state_msg();

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->oci_version, buffer);
		safe_unpackstr(&msg->id, buffer);
		safe_unpack32(&msg->status, buffer);
		safe_unpack32(&msg->pid, buffer);
		safe_unpackstr(&msg->bundle, buffer);
		if (unpack_key_pair_list((void **) &msg->annotations,
					 smsg->protocol_version, buffer))
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_container_state_msg(msg);
	return SLURM_ERROR;
}

static void _pack_container_signal_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	container_signal_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->signal, buffer);
	}
}

static int _unpack_container_signal_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	container_signal_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->signal, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg);
	return SLURM_ERROR;
}

static void _pack_container_delete_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	container_delete_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packbool(msg->force, buffer);
	}
}

static int _unpack_container_delete_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	container_delete_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackbool(&msg->force, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg);
	return SLURM_ERROR;
}

static void _pack_container_started_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	container_started_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->rc, buffer);
		pack_step_id(&msg->step_id, buffer, smsg->protocol_version);
	}
}

static int _unpack_container_started_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	container_started_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->rc, buffer);
		safe_unpack_step_id_members(&msg->step_id, buffer,
					    smsg->protocol_version);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg);
	return SLURM_ERROR;
}

static void _pack_container_exec_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	container_exec_msg_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->args, buffer);
		packstr(msg->env, buffer);
	}
}

static int _unpack_container_exec_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	container_exec_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&msg->args, buffer);
		safe_unpackstr(&msg->env, buffer);
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_container_exec_msg(msg);
	return SLURM_ERROR;
}

extern void slurm_pack_node_alias_addrs(slurm_node_alias_addrs_t *msg,
					buf_t *buffer,
					uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		slurm_pack_addr_array(msg->node_addrs, msg->node_cnt, buffer);
		pack32(msg->node_cnt, buffer);
		packstr(msg->node_list, buffer);
	}
}

static void _pack_node_alias_addrs(const slurm_msg_t *smsg, buf_t *buffer)
{
	slurm_pack_node_alias_addrs(smsg->data, buffer, smsg->protocol_version);
}

extern int slurm_unpack_node_alias_addrs(slurm_node_alias_addrs_t **msg_ptr,
					 buf_t *buffer,
					 uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	slurm_node_alias_addrs_t *msg;

	xassert(msg_ptr);

	msg = xmalloc(sizeof(*msg));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (slurm_unpack_addr_array(&msg->node_addrs,
					    &uint32_tmp, buffer))
			goto unpack_error;
		safe_unpack32(&msg->node_cnt, buffer);
		safe_unpackstr(&msg->node_list, buffer);

		xassert(uint32_tmp == msg->node_cnt);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_alias_addrs(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_node_alias_addrs_resp_msg(const slurm_msg_t *smsg,
					    buf_t *buffer)
{
	slurm_node_alias_addrs_t *msg = smsg->data;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *tmp_str = create_net_cred(msg, smsg->protocol_version);
		packstr(tmp_str, buffer);
		xfree(tmp_str);
	}
}

static int _unpack_node_alias_addrs_resp_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	slurm_node_alias_addrs_t *msg = NULL;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *tmp_str = NULL;
		safe_unpackstr(&tmp_str, buffer);
		msg = extract_net_cred(tmp_str, smsg->protocol_version);
		if (!msg) {
			xfree(tmp_str);
			goto unpack_error;
		}
		msg->net_cred = tmp_str;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_alias_addrs(msg);
	return SLURM_ERROR;
}

static void _pack_dbd_relay(const slurm_msg_t *smsg, buf_t *buffer)
{
	persist_msg_t *msg = smsg->data;
	uint32_t grow_size;

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->msg_type, buffer);

		buf_t *dbd_buffer = pack_slurmdbd_msg(msg,
						      smsg->protocol_version);
		grow_size = size_buf(dbd_buffer);
		grow_buf(buffer, grow_size);
		memcpy(&buffer->head[get_buf_offset(buffer)],
		       get_buf_data(dbd_buffer), grow_size);
		set_buf_offset(buffer,
			       get_buf_offset(buffer) + grow_size);
		FREE_NULL_BUFFER(dbd_buffer);
	}
}

static int _unpack_dbd_relay(slurm_msg_t *smsg, buf_t *buffer)
{
	persist_msg_t *msg = xmalloc(sizeof(*msg));

	if (smsg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->msg_type, buffer);
		if (unpack_slurmdbd_msg(msg, smsg->protocol_version, buffer))
			goto unpack_error;
	}

	smsg->data = msg;
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg);
	return SLURM_ERROR;
}

/* pack_msg
 * packs a generic slurm protocol message body
 * IN msg - the body structure to pack (note: includes message type)
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
int
pack_msg(slurm_msg_t *msg, buf_t *buffer)
{
	if (msg->protocol_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("%s: Invalid message version=%hu, type:%s",
		      __func__, msg->protocol_version,
		      rpc_num2string(msg->msg_type));
		return SLURM_ERROR;
	}

	/* Figure out protocol version to use */
	if (msg->protocol_version != NO_VAL16) {
		/* use what is set */
	} else if (working_cluster_rec) {
		msg->protocol_version = working_cluster_rec->rpc_version;
	} else if ((msg->msg_type == ACCOUNTING_UPDATE_MSG) ||
		   (msg->msg_type == ACCOUNTING_FIRST_REG)) {
		uint16_t rpc_version =
			((accounting_update_msg_t *)msg->data)->rpc_version;
		msg->protocol_version = rpc_version;
	} else {
		msg->protocol_version = SLURM_PROTOCOL_VERSION;
	}

	switch (msg->msg_type) {
	case RESPONSE_ASSOC_MGR_INFO:
	case RESPONSE_BURST_BUFFER_INFO:
	case RESPONSE_JOB_INFO:
	case RESPONSE_JOB_STEP_INFO:
	case RESPONSE_LICENSE_INFO:
	case RESPONSE_NODE_INFO:
	case RESPONSE_PARTITION_INFO:
	case RESPONSE_RESERVATION_INFO:
	case RESPONSE_STATS_INFO:
		_pack_buf_msg(msg, buffer);
		break;
	case REQUEST_NODE_INFO:
		_pack_node_info_request_msg(msg, buffer);
		break;
	case REQUEST_NODE_INFO_SINGLE:
		_pack_node_info_single_msg(msg, buffer);
		break;
	case REQUEST_HOSTLIST_EXPANSION:
		_pack_hostlist_expansion_request(msg, buffer);
		break;
	case RESPONSE_HOSTLIST_EXPANSION:
		_pack_hostlist_expansion_response(msg, buffer);
		break;
	case REQUEST_PARTITION_INFO:
		_pack_part_info_request_msg(msg, buffer);
		break;
	case REQUEST_RESERVATION_INFO:
		_pack_resv_info_request_msg(msg, buffer);
		break;
	case REQUEST_BUILD_INFO:
		_pack_last_update_msg(msg, buffer);
		break;
	case RESPONSE_BUILD_INFO:
		_pack_slurm_ctl_conf_msg(msg, buffer);
		break;
	case RESPONSE_BATCH_SCRIPT:
		_pack_job_script_msg(msg, buffer);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		_pack_node_registration_status_msg(msg, buffer);
		break;
	case RESPONSE_ACCT_GATHER_UPDATE:
	case RESPONSE_ACCT_GATHER_ENERGY:
		_pack_acct_gather_node_resp_msg(msg, buffer);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_UPDATE_JOB:
		_pack_job_desc_msg(msg, buffer);
		break;
	case REQUEST_HET_JOB_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_HET_JOB:
		_pack_job_desc_list_msg(msg, buffer);
		break;
	case RESPONSE_HET_JOB_ALLOCATION:
		_pack_job_info_list_msg(msg, buffer);
		break;
	case REQUEST_SIB_JOB_LOCK:
	case REQUEST_SIB_JOB_UNLOCK:
	case REQUEST_SIB_MSG:
		_pack_sib_msg(msg, buffer);
		break;
	case REQUEST_SEND_DEP:
		_pack_dep_msg(msg, buffer);
		break;
	case REQUEST_UPDATE_ORIGIN_DEP:
		_pack_dep_update_origin_msg(msg, buffer);
		break;
	case REQUEST_UPDATE_JOB_STEP:
		_pack_update_job_step_msg(msg, buffer);
		break;
	case REQUEST_JOB_ALLOCATION_INFO:
	case REQUEST_JOB_END_TIME:
	case REQUEST_HET_JOB_ALLOC_INFO:
		_pack_job_alloc_info_msg(msg, buffer);
		break;
	case REQUEST_JOB_SBCAST_CRED:
		_pack_step_alloc_info_msg(msg, buffer);
		break;
	case REQUEST_SBCAST_CRED_NO_JOB:
		_pack_sbcast_cred_no_job_msg(msg, buffer);
		break;
	case RESPONSE_NODE_REGISTRATION:
		_pack_node_reg_resp(msg, buffer);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
	case REQUEST_RECONFIGURE:
	case REQUEST_PING:
	case REQUEST_CONTROL:
	case REQUEST_CONTROL_STATUS:
	case REQUEST_TAKEOVER:
	case REQUEST_DAEMON_STATUS:
	case REQUEST_HEALTH_CHECK:
	case REQUEST_ACCT_GATHER_UPDATE:
	case ACCOUNTING_FIRST_REG:
	case ACCOUNTING_REGISTER_CTLD:
	case REQUEST_BURST_BUFFER_INFO:
	case REQUEST_FED_INFO:
	case SRUN_PING:
	case REQUEST_CONTAINER_START:
	case REQUEST_CONTAINER_STATE:
	case REQUEST_CONTAINER_PTY:
	case REQUEST_TOPO_CONFIG:
		/* Message contains no body/information */
		break;
	case REQUEST_ACCT_GATHER_ENERGY:
		_pack_acct_gather_energy_req(msg, buffer);
		break;
	case REQUEST_PERSIST_INIT:
		slurm_persist_pack_init_req_msg(
			(persist_init_req_msg_t *)msg->data,
			buffer);
		break;
	case PERSIST_RC:
		slurm_persist_pack_rc_msg(
			(persist_rc_msg_t *)msg->data,
			buffer, msg->protocol_version);
		break;
	case REQUEST_REBOOT_NODES:
		_pack_reboot_msg(msg, buffer);
		break;
	case REQUEST_SHUTDOWN:
		_pack_shutdown_msg(msg, buffer);
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		_pack_submit_response_msg(msg, buffer);
		break;
	case RESPONSE_JOB_ALLOCATION_INFO:
	case RESPONSE_RESOURCE_ALLOCATION:
		_pack_resource_allocation_response_msg(msg, buffer);
		break;
	case RESPONSE_JOB_WILL_RUN:
		_pack_will_run_response_msg(msg, buffer);
		break;
	case REQUEST_CREATE_NODE:
	case REQUEST_UPDATE_NODE:
	case REQUEST_DELETE_NODE:
		_pack_update_node_msg(msg, buffer);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		_pack_update_partition_msg(msg, buffer);
		break;
	case REQUEST_DELETE_PARTITION:
		_pack_delete_partition_msg(msg, buffer);
		break;
	case REQUEST_CREATE_RESERVATION:
	case REQUEST_UPDATE_RESERVATION:
		_pack_update_resv_msg(msg, buffer);
		break;
	case REQUEST_DELETE_RESERVATION:
	case RESPONSE_CREATE_RESERVATION:
		_pack_resv_name_msg(msg, buffer);
		break;
	case REQUEST_REATTACH_TASKS:
		_pack_reattach_tasks_request_msg(msg, buffer);
		break;
	case RESPONSE_REATTACH_TASKS:
		_pack_reattach_tasks_response_msg(msg, buffer);
		break;
	case REQUEST_LAUNCH_TASKS:
		_pack_launch_tasks_request_msg(msg, buffer);
		break;
	case RESPONSE_LAUNCH_TASKS:
		_pack_launch_tasks_response_msg(msg, buffer);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		_pack_cancel_tasks_msg(msg, buffer);
		break;
	case REQUEST_JOB_STEP_INFO:
		_pack_job_step_info_req_msg(msg, buffer);
		break;
	case REQUEST_STEP_BY_CONTAINER_ID:
		_pack_container_id_request_msg(msg, buffer);
		break;
	case RESPONSE_STEP_BY_CONTAINER_ID:
		_pack_container_id_response_msg(msg, buffer);
		break;
	case REQUEST_JOB_INFO:
		_pack_job_info_request_msg(msg, buffer);
		break;
	case REQUEST_JOB_STATE:
		_pack_job_state_request_msg(msg, buffer);
		break;
	case RESPONSE_JOB_STATE:
		_pack_job_state_response_msg(msg, buffer);
		break;
	case REQUEST_CANCEL_JOB_STEP:
	case REQUEST_KILL_JOB:
	case SRUN_STEP_SIGNAL:
		_pack_job_step_kill_msg(msg, buffer);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		_pack_complete_job_allocation_msg(msg, buffer);
		break;
	case REQUEST_COMPLETE_PROLOG:
		_pack_prolog_complete_msg(msg, buffer);
		break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		_pack_complete_batch_script_msg(msg, buffer);
		break;
	case REQUEST_STEP_COMPLETE:
		_pack_step_complete_msg(msg, buffer);
		break;
	case RESPONSE_JOB_STEP_STAT:
		_pack_job_step_stat(msg, buffer);
		break;
		/********  slurm_step_id_t Messages  ********/
	case SRUN_JOB_COMPLETE:
	case REQUEST_STEP_LAYOUT:
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
		_pack_step_id_msg(msg, buffer);
		break;
	case RESPONSE_STEP_LAYOUT:
		pack_slurm_step_layout((slurm_step_layout_t *)msg->data,
				       buffer,
				       msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_PIDS:
		_pack_job_step_pids(msg, buffer);
		break;
	case REQUEST_ABORT_JOB:
	case REQUEST_KILL_PREEMPTED:
	case REQUEST_KILL_TIMELIMIT:
	case REQUEST_TERMINATE_JOB:
		_pack_kill_job_msg(msg, buffer);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		_pack_epilog_comp_msg(msg, buffer);
		break;
	case MESSAGE_TASK_EXIT:
		_pack_task_exit_msg(msg, buffer);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		_pack_batch_job_launch_msg(msg, buffer);
		break;
	case REQUEST_LAUNCH_PROLOG:
		_pack_prolog_launch_msg(msg, buffer);
		break;
	case RESPONSE_CONTAINER_PTY:
	case RESPONSE_CONTAINER_KILL:
	case RESPONSE_CONTAINER_DELETE:
	case RESPONSE_CONTAINER_EXEC:
	case RESPONSE_PROLOG_EXECUTING:
	case RESPONSE_JOB_READY:
	case RESPONSE_SLURM_RC:
		_pack_return_code_msg(msg, buffer);
		break;
	case RESPONSE_SLURM_RC_MSG:
		_pack_return_code2_msg(msg, buffer);
		break;
	case RESPONSE_SLURM_REROUTE_MSG:
		_pack_reroute_msg(msg, buffer);
		break;
	case RESPONSE_JOB_STEP_CREATE:
		_pack_job_step_create_response_msg(msg, buffer);
		break;
	case REQUEST_JOB_STEP_CREATE:
		_pack_job_step_create_request_msg(msg, buffer);
		break;
	case REQUEST_JOB_ID:
		_pack_job_id_request_msg(msg, buffer);
		break;
	case RESPONSE_JOB_ID:
		_pack_job_id_response_msg(msg, buffer);
		break;
	case REQUEST_CONFIG:
		_pack_config_request_msg(msg, buffer);
		break;
	case REQUEST_RECONFIGURE_SACKD:
	case REQUEST_RECONFIGURE_WITH_CONFIG:
	case RESPONSE_CONFIG:
		pack_config_response_msg(msg, buffer);
		break;
	case SRUN_NODE_FAIL:
		_pack_srun_node_fail_msg(msg, buffer);
		break;
	case SRUN_STEP_MISSING:
		_pack_srun_step_missing_msg(msg, buffer);
		break;
	case SRUN_TIMEOUT:
		_pack_srun_timeout_msg(msg, buffer);
		break;
	case SRUN_USER_MSG:
		_pack_srun_user_msg(msg, buffer);
		break;
	case SRUN_NET_FORWARD:
		_pack_net_forward_msg(msg, buffer);
		break;
	case REQUEST_SUSPEND:
	case SRUN_REQUEST_SUSPEND:
		_pack_suspend_msg(msg, buffer);
		break;
	case REQUEST_SUSPEND_INT:
		_pack_suspend_int_msg(msg, buffer);
		break;
	case REQUEST_TOP_JOB:
		_pack_top_job_msg(msg, buffer);
		break;
	case REQUEST_AUTH_TOKEN:
		_pack_token_request_msg(msg, buffer);
		break;
	case RESPONSE_AUTH_TOKEN:
		_pack_token_response_msg(msg, buffer);
		break;
	case REQUEST_KILL_JOBS:
		_pack_kill_jobs_msg(msg, buffer);
		break;
	case RESPONSE_KILL_JOBS:
		_pack_kill_jobs_resp_msg(msg, buffer);
		break;
	case REQUEST_BATCH_SCRIPT:
	case REQUEST_JOB_READY:
	case REQUEST_JOB_INFO_SINGLE:
		_pack_job_ready_msg(msg, buffer);
		break;
	case REQUEST_JOB_REQUEUE:
		_pack_job_requeue_msg(msg, buffer);
		break;
	case REQUEST_JOB_USER_INFO:
		_pack_job_user_msg(msg, buffer);
		break;
	case REQUEST_SHARE_INFO:
		_pack_shares_request_msg(msg, buffer);
		break;
	case RESPONSE_SHARE_INFO:
		_pack_shares_response_msg(msg, buffer);
		break;
	case REQUEST_PRIORITY_FACTORS:
		break;
	case RESPONSE_PRIORITY_FACTORS:
		_pack_priority_factors_response_msg(msg, buffer);
		break;
	case REQUEST_FILE_BCAST:
		_pack_file_bcast(msg, buffer);
		break;
	case PMI_KVS_PUT_REQ:
	case PMI_KVS_GET_RESP:
		_pack_kvs_data(msg, buffer);
		break;
	case PMI_KVS_GET_REQ:
		_pack_kvs_get(msg, buffer);
		break;
	case RESPONSE_FORWARD_FAILED:
		break;
	case REQUEST_TRIGGER_GET:
	case RESPONSE_TRIGGER_GET:
	case REQUEST_TRIGGER_SET:
	case REQUEST_TRIGGER_CLEAR:
	case REQUEST_TRIGGER_PULL:
		_pack_trigger_msg(msg, buffer);
		break;
	case RESPONSE_SLURMD_STATUS:
		_pack_slurmd_status(msg, buffer);
		break;
	case REQUEST_JOB_NOTIFY:
		_pack_job_notify(msg, buffer);
		break;
	case REQUEST_SET_DEBUG_FLAGS:
		_pack_set_debug_flags_msg(msg, buffer);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
	case REQUEST_SET_SCHEDLOG_LEVEL:
		_pack_set_debug_level_msg(msg, buffer);
		break;
	case REQUEST_SET_SUSPEND_EXC_NODES:
	case REQUEST_SET_SUSPEND_EXC_PARTS:
	case REQUEST_SET_SUSPEND_EXC_STATES:
		_pack_suspend_exc_update_msg(msg, buffer);
		break;
	case REQUEST_DBD_RELAY:
		_pack_dbd_relay(msg, buffer);
		break;
	case ACCOUNTING_UPDATE_MSG:
		_pack_accounting_update_msg(msg, buffer);
		break;
	case REQUEST_TOPO_INFO:
		_pack_topo_info_request_msg(msg, buffer);
		break;
	case RESPONSE_TOPO_INFO:
		_pack_topo_info_msg(msg, buffer);
		break;
	case RESPONSE_TOPO_CONFIG:
		_pack_topo_config_msg(msg, buffer);
		break;
	case RESPONSE_JOB_SBCAST_CRED:
		_pack_job_sbcast_cred_msg(msg, buffer);
		break;
	case RESPONSE_FED_INFO:
		slurmdb_pack_federation_rec(
			(slurmdb_federation_rec_t *)msg->data,
			msg->protocol_version, buffer);
		break;
	case REQUEST_STATS_INFO:
		_pack_stats_request_msg(msg, buffer);
		break;
	case REQUEST_FORWARD_DATA:
		_pack_forward_data_msg(msg, buffer);
		break;
	case RESPONSE_PING_SLURMD:
		_pack_ping_slurmd_resp(msg, buffer);
		break;
	case REQUEST_LICENSE_INFO:
		_pack_license_info_request_msg(msg, buffer);
		break;
	case RESPONSE_JOB_ARRAY_ERRORS:
		_pack_job_array_resp_msg(msg, buffer);
		break;
	case REQUEST_ASSOC_MGR_INFO:
		_pack_assoc_mgr_info_request_msg(msg, buffer);
		break;
	case REQUEST_NETWORK_CALLERID:
		_pack_network_callerid_msg(msg, buffer);
		break;
	case RESPONSE_NETWORK_CALLERID:
		_pack_network_callerid_resp_msg(msg, buffer);
		break;
	case REQUEST_CTLD_MULT_MSG:
	case RESPONSE_CTLD_MULT_MSG:
		_pack_buf_list_msg(msg, buffer);
		break;
	case REQUEST_SET_FS_DAMPENING_FACTOR:
		_pack_set_fs_dampening_factor_msg(msg, buffer);
		break;
	case RESPONSE_CONTROL_STATUS:
		_pack_control_status_msg(msg, buffer);
		break;
	case REQUEST_BURST_BUFFER_STATUS:
		_pack_bb_status_req_msg(msg, buffer);
		break;
	case RESPONSE_BURST_BUFFER_STATUS:
		_pack_bb_status_resp_msg(msg, buffer);
		break;
	case REQUEST_CRONTAB:
		_pack_crontab_request_msg(msg, buffer);
		break;
	case RESPONSE_CRONTAB:
		_pack_crontab_response_msg(msg, buffer);
		break;
	case REQUEST_UPDATE_CRONTAB:
		_pack_crontab_update_request_msg(msg, buffer);
		break;
	case RESPONSE_UPDATE_CRONTAB:
		_pack_crontab_update_response_msg(msg, buffer);
		break;
	case REQUEST_TLS_CERT:
		_pack_tls_cert_request_msg(msg, buffer);
		break;
	case RESPONSE_TLS_CERT:
		_pack_tls_cert_response_msg(msg, buffer);
		break;
	case RESPONSE_CONTAINER_STATE:
		_pack_container_state_msg(msg, buffer);
		break;
	case REQUEST_CONTAINER_KILL:
		_pack_container_signal_msg(msg, buffer);
		break;
	case REQUEST_CONTAINER_DELETE:
		_pack_container_delete_msg(msg, buffer);
		break;
	case RESPONSE_CONTAINER_START:
		_pack_container_started_msg(msg, buffer);
		break;
	case REQUEST_CONTAINER_EXEC:
		_pack_container_exec_msg(msg, buffer);
		break;
	case REQUEST_NODE_ALIAS_ADDRS:
		_pack_node_alias_addrs(msg, buffer);
		break;
	case RESPONSE_NODE_ALIAS_ADDRS:
		_pack_node_alias_addrs_resp_msg(msg, buffer);
		break;
	default:
		debug("No pack method for msg type %u", msg->msg_type);
		return EINVAL;
		break;
	}
	return SLURM_SUCCESS;
}

/* unpack_msg
 * unpacks a generic slurm protocol message body
 * OUT msg - the body structure to unpack (note: includes message type)
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
int
unpack_msg(slurm_msg_t * msg, buf_t *buffer)
{
	int rc = SLURM_SUCCESS;
	msg->data = NULL;	/* Initialize to no data for now */

	if (msg->protocol_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("%s: Invalid message version=%hu, type:%s",
		      __func__, msg->protocol_version,
		      rpc_num2string(msg->msg_type));
		return SLURM_ERROR;
	}

	switch (msg->msg_type) {
	case REQUEST_NODE_INFO:
		rc = _unpack_node_info_request_msg(msg, buffer);
		break;
	case REQUEST_NODE_INFO_SINGLE:
		rc = _unpack_node_info_single_msg(msg, buffer);
		break;
	case REQUEST_HOSTLIST_EXPANSION:
		rc = _unpack_hostlist_expansion_request(msg, buffer);
		break;
	case RESPONSE_HOSTLIST_EXPANSION:
		rc = _unpack_hostlist_expansion_response(msg, buffer);
		break;
	case REQUEST_PARTITION_INFO:
		rc = _unpack_part_info_request_msg(msg, buffer);
		break;
	case REQUEST_RESERVATION_INFO:
		rc = _unpack_resv_info_request_msg(msg, buffer);
		break;
	case REQUEST_BUILD_INFO:
		rc = _unpack_last_update_msg(msg, buffer);
		break;
	case RESPONSE_BUILD_INFO:
		rc = _unpack_slurm_ctl_conf_msg(msg, buffer);
		break;
	case REQUEST_STEP_BY_CONTAINER_ID:
		rc = _unpack_container_id_request_msg(msg, buffer);
		break;
	case RESPONSE_STEP_BY_CONTAINER_ID:
		rc = _unpack_container_id_response_msg(msg, buffer);
		break;
	case RESPONSE_JOB_INFO:
		rc = _unpack_job_info_msg(msg, buffer);
		break;
	case RESPONSE_BATCH_SCRIPT:
		rc = _unpack_job_script_msg(msg, buffer);
		break;
	case RESPONSE_PARTITION_INFO:
		rc = _unpack_partition_info_msg(msg, buffer);
		break;
	case RESPONSE_NODE_INFO:
		rc = _unpack_node_info_msg(msg, buffer);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		rc = _unpack_node_registration_status_msg(msg, buffer);
		break;
	case RESPONSE_ACCT_GATHER_UPDATE:
	case RESPONSE_ACCT_GATHER_ENERGY:
		rc = _unpack_acct_gather_node_resp_msg(msg, buffer);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_UPDATE_JOB:
		rc = _unpack_job_desc_msg(msg, buffer);
		break;
	case REQUEST_HET_JOB_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_HET_JOB:
		rc = _unpack_job_desc_list_msg((list_t **) &(msg->data),
					       buffer, msg->protocol_version);
		break;
	case RESPONSE_HET_JOB_ALLOCATION:
		rc = _unpack_job_info_list_msg((list_t **) &(msg->data),
					       buffer, msg->protocol_version);
		break;
	case REQUEST_SIB_JOB_LOCK:
	case REQUEST_SIB_JOB_UNLOCK:
	case REQUEST_SIB_MSG:
		rc = _unpack_sib_msg(msg, buffer);
		break;
	case REQUEST_SEND_DEP:
		rc = _unpack_dep_msg(msg, buffer);
		break;
	case REQUEST_UPDATE_ORIGIN_DEP:
		rc = _unpack_dep_update_origin_msg(msg, buffer);
		break;
	case REQUEST_UPDATE_JOB_STEP:
		rc = _unpack_update_job_step_msg(msg, buffer);
		break;
	case REQUEST_JOB_ALLOCATION_INFO:
	case REQUEST_JOB_END_TIME:
	case REQUEST_HET_JOB_ALLOC_INFO:
		rc = _unpack_job_alloc_info_msg(msg, buffer);
		break;
	case REQUEST_JOB_SBCAST_CRED:
		rc = _unpack_step_alloc_info_msg(msg, buffer);
		break;
	case REQUEST_SBCAST_CRED_NO_JOB:
		rc = _unpack_sbcast_cred_no_job_msg(msg, buffer);
		break;
	case RESPONSE_NODE_REGISTRATION:
		rc = _unpack_node_reg_resp(msg, buffer);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
	case REQUEST_RECONFIGURE:
	case REQUEST_PING:
	case REQUEST_CONTROL:
	case REQUEST_CONTROL_STATUS:
	case REQUEST_TAKEOVER:
	case REQUEST_DAEMON_STATUS:
	case REQUEST_HEALTH_CHECK:
	case REQUEST_ACCT_GATHER_UPDATE:
	case ACCOUNTING_FIRST_REG:
	case ACCOUNTING_REGISTER_CTLD:
	case REQUEST_BURST_BUFFER_INFO:
	case REQUEST_FED_INFO:
	case SRUN_PING:
	case REQUEST_CONTAINER_START:
	case REQUEST_CONTAINER_STATE:
	case REQUEST_CONTAINER_PTY:
	case REQUEST_TOPO_CONFIG:
		/* Message contains no body/information */
		break;
	case REQUEST_ACCT_GATHER_ENERGY:
		rc = _unpack_acct_gather_energy_req(msg, buffer);
		break;
	case REQUEST_PERSIST_INIT:
		/* the version is contained in the data so use that instead of
		   what is in the message */
		rc = slurm_persist_unpack_init_req_msg(
			(persist_init_req_msg_t **)&msg->data, buffer);
		break;
	case PERSIST_RC:
		rc = slurm_persist_unpack_rc_msg(
			(persist_rc_msg_t **)&msg->data,
			buffer, msg->protocol_version);
		break;
	case REQUEST_REBOOT_NODES:
		rc = _unpack_reboot_msg(msg, buffer);
		break;
	case REQUEST_SHUTDOWN:
		rc = _unpack_shutdown_msg(msg, buffer);
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		rc = _unpack_submit_response_msg(msg, buffer);
		break;
	case RESPONSE_JOB_ALLOCATION_INFO:
	case RESPONSE_RESOURCE_ALLOCATION:
		rc = _unpack_resource_allocation_response_msg(msg, buffer);
		break;
	case RESPONSE_JOB_WILL_RUN:
		rc = _unpack_will_run_response_msg(msg, buffer);
		break;
	case REQUEST_CREATE_NODE:
	case REQUEST_UPDATE_NODE:
	case REQUEST_DELETE_NODE:
		rc = _unpack_update_node_msg(msg, buffer);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		rc = _unpack_update_partition_msg(msg, buffer);
		break;
	case REQUEST_DELETE_PARTITION:
		rc = _unpack_delete_partition_msg(msg, buffer);
		break;
	case REQUEST_CREATE_RESERVATION:
	case REQUEST_UPDATE_RESERVATION:
		rc = _unpack_update_resv_msg(msg, buffer);
		break;
	case REQUEST_DELETE_RESERVATION:
	case RESPONSE_CREATE_RESERVATION:
		rc = _unpack_resv_name_msg(msg, buffer);
		break;
	case RESPONSE_RESERVATION_INFO:
		rc = _unpack_reserve_info_msg(msg, buffer);
		break;
	case REQUEST_LAUNCH_TASKS:
		rc = _unpack_launch_tasks_request_msg(msg, buffer);
		break;
	case RESPONSE_LAUNCH_TASKS:
		rc = _unpack_launch_tasks_response_msg(msg, buffer);
		break;
	case REQUEST_REATTACH_TASKS:
		rc = _unpack_reattach_tasks_request_msg(msg, buffer);
		break;
	case RESPONSE_REATTACH_TASKS:
		rc = _unpack_reattach_tasks_response_msg(msg, buffer);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		rc = _unpack_cancel_tasks_msg(msg, buffer);
		break;
	case REQUEST_JOB_STEP_INFO:
		rc = _unpack_job_step_info_req_msg(msg, buffer);
		break;
	case REQUEST_JOB_INFO:
		rc = _unpack_job_info_request_msg(msg, buffer);
		break;
	case REQUEST_JOB_STATE:
		rc = _unpack_job_state_request_msg(msg, buffer);
		break;
	case RESPONSE_JOB_STATE:
		rc = _unpack_job_state_response_msg(msg, buffer);
		break;
	case REQUEST_CANCEL_JOB_STEP:
	case REQUEST_KILL_JOB:
	case SRUN_STEP_SIGNAL:
		rc = _unpack_job_step_kill_msg(msg, buffer);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		rc = _unpack_complete_job_allocation_msg(msg, buffer);
		break;
	case REQUEST_COMPLETE_PROLOG:
		rc = _unpack_prolog_complete_msg(msg, buffer);
		break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		rc = _unpack_complete_batch_script_msg(msg, buffer);
		break;
	case REQUEST_STEP_COMPLETE:
		rc = _unpack_step_complete_msg(msg, buffer);
		break;
	case RESPONSE_JOB_STEP_STAT:
		rc = _unpack_job_step_stat(msg, buffer);
		break;
		/********  slurm_step_id_t Messages  ********/
	case SRUN_JOB_COMPLETE:
	case REQUEST_STEP_LAYOUT:
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
		rc = _unpack_step_id_msg(msg, buffer);
		break;
	case RESPONSE_STEP_LAYOUT:
		rc = unpack_slurm_step_layout(
			(slurm_step_layout_t **)&msg->data,
			buffer, msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_PIDS:
		rc = _unpack_job_step_pids(
			(job_step_pids_t **)&msg->data,
			buffer,	msg->protocol_version);
		break;
	case REQUEST_ABORT_JOB:
	case REQUEST_KILL_PREEMPTED:
	case REQUEST_KILL_TIMELIMIT:
	case REQUEST_TERMINATE_JOB:
		rc = _unpack_kill_job_msg(msg, buffer);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		rc = _unpack_epilog_comp_msg(msg, buffer);
		break;
	case RESPONSE_JOB_STEP_INFO:
		rc = _unpack_job_step_info_response_msg(msg, buffer);
		break;
	case MESSAGE_TASK_EXIT:
		rc = _unpack_task_exit_msg(msg, buffer);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		rc = _unpack_batch_job_launch_msg(msg, buffer);
		break;
	case REQUEST_LAUNCH_PROLOG:
		rc = _unpack_prolog_launch_msg(msg, buffer);
		break;
	case RESPONSE_CONTAINER_PTY:
	case RESPONSE_CONTAINER_KILL:
	case RESPONSE_CONTAINER_DELETE:
	case RESPONSE_CONTAINER_EXEC:
	case RESPONSE_PROLOG_EXECUTING:
	case RESPONSE_JOB_READY:
	case RESPONSE_SLURM_RC:
		rc = _unpack_return_code_msg(msg, buffer);
		break;
	case RESPONSE_SLURM_RC_MSG:
		/* Log error message, otherwise replicate RESPONSE_SLURM_RC */
		msg->msg_type = RESPONSE_SLURM_RC;
		rc = _unpack_return_code2_msg(msg, buffer);
		break;
	case RESPONSE_SLURM_REROUTE_MSG:
		rc = _unpack_reroute_msg(msg, buffer);
		break;
	case RESPONSE_JOB_STEP_CREATE:
		rc = _unpack_job_step_create_response_msg(msg, buffer);
		break;
	case REQUEST_JOB_STEP_CREATE:
		rc = _unpack_job_step_create_request_msg(msg, buffer);
		break;
	case REQUEST_JOB_ID:
		rc = _unpack_job_id_request_msg(msg, buffer);
		break;
	case RESPONSE_JOB_ID:
		rc = _unpack_job_id_response_msg(msg, buffer);
		break;
	case REQUEST_CONFIG:
		_unpack_config_request_msg(msg, buffer);
		break;
	case REQUEST_RECONFIGURE_SACKD:
	case REQUEST_RECONFIGURE_WITH_CONFIG:
	case RESPONSE_CONFIG:
		unpack_config_response_msg(
			(config_response_msg_t **) &msg->data,
			buffer, msg->protocol_version);
		break;
	case SRUN_NET_FORWARD:
		rc = _unpack_net_forward_msg(msg, buffer);
		break;
	case SRUN_NODE_FAIL:
		rc = _unpack_srun_node_fail_msg(msg, buffer);
		break;
	case SRUN_STEP_MISSING:
		rc = _unpack_srun_step_missing_msg(msg, buffer);
		break;
	case SRUN_TIMEOUT:
		rc = _unpack_srun_timeout_msg(msg, buffer);
		break;
	case SRUN_USER_MSG:
		rc = _unpack_srun_user_msg(msg, buffer);
		break;
	case REQUEST_SUSPEND:
	case SRUN_REQUEST_SUSPEND:
		rc = _unpack_suspend_msg(msg, buffer);
		break;
	case REQUEST_SUSPEND_INT:
		rc = _unpack_suspend_int_msg(msg, buffer);
		break;
	case REQUEST_TOP_JOB:
		rc = _unpack_top_job_msg(msg, buffer);
		break;
	case REQUEST_AUTH_TOKEN:
		rc = _unpack_token_request_msg(msg, buffer);
		break;
	case RESPONSE_AUTH_TOKEN:
		rc = _unpack_token_response_msg(msg, buffer);
		break;
	case REQUEST_KILL_JOBS:
		rc = _unpack_kill_jobs_msg(msg, buffer);
		break;
	case RESPONSE_KILL_JOBS:
		rc = _unpack_kill_jobs_resp_msg(msg, buffer);
		break;
	case REQUEST_BATCH_SCRIPT:
	case REQUEST_JOB_READY:
	case REQUEST_JOB_INFO_SINGLE:
		rc = _unpack_job_ready_msg(msg, buffer);
		break;
	case REQUEST_JOB_REQUEUE:
		rc = _unpack_job_requeue_msg(msg, buffer);
		break;
	case REQUEST_JOB_USER_INFO:
		rc = _unpack_job_user_msg(msg, buffer);
		break;
	case REQUEST_SHARE_INFO:
		rc = _unpack_shares_request_msg(msg, buffer);
		break;
	case RESPONSE_SHARE_INFO:
		rc = _unpack_shares_response_msg(msg, buffer);
		break;
	case REQUEST_PRIORITY_FACTORS:
		break;
	case RESPONSE_PRIORITY_FACTORS:
		rc = _unpack_priority_factors_response_msg(msg, buffer);
		break;
	case RESPONSE_BURST_BUFFER_INFO:
		rc = _unpack_burst_buffer_info_msg(msg, buffer);
		break;
	case REQUEST_FILE_BCAST:
		rc = _unpack_file_bcast(msg, buffer);
		break;
	case PMI_KVS_PUT_REQ:
	case PMI_KVS_GET_RESP:
		rc = _unpack_kvs_data(msg, buffer);
		break;
	case PMI_KVS_GET_REQ:
		rc = _unpack_kvs_get(msg, buffer);
		break;
	case RESPONSE_FORWARD_FAILED:
		break;
	case REQUEST_TRIGGER_GET:
	case RESPONSE_TRIGGER_GET:
	case REQUEST_TRIGGER_SET:
	case REQUEST_TRIGGER_CLEAR:
	case REQUEST_TRIGGER_PULL:
		rc = _unpack_trigger_msg(msg, buffer);
		break;
	case RESPONSE_SLURMD_STATUS:
		rc = _unpack_slurmd_status(msg, buffer);
		break;
	case REQUEST_JOB_NOTIFY:
		rc = _unpack_job_notify(msg, buffer);
		break;
	case REQUEST_SET_DEBUG_FLAGS:
		rc = _unpack_set_debug_flags_msg(msg, buffer);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
	case REQUEST_SET_SCHEDLOG_LEVEL:
		rc = _unpack_set_debug_level_msg(msg, buffer);
		break;
	case REQUEST_SET_SUSPEND_EXC_NODES:
	case REQUEST_SET_SUSPEND_EXC_PARTS:
	case REQUEST_SET_SUSPEND_EXC_STATES:
		rc = _unpack_suspend_exc_update_msg(msg, buffer);
		break;
	case REQUEST_DBD_RELAY:
		rc = _unpack_dbd_relay(msg, buffer);
		break;
	case ACCOUNTING_UPDATE_MSG:
		rc = _unpack_accounting_update_msg(msg, buffer);
		break;
	case REQUEST_TOPO_INFO:
		rc = _unpack_topo_info_request_msg(msg, buffer);
		break;
	case RESPONSE_TOPO_INFO:
		rc = _unpack_topo_info_msg(msg, buffer);
		break;
	case RESPONSE_TOPO_CONFIG:
		rc = _unpack_topo_config_msg(msg, buffer);
		break;
	case RESPONSE_JOB_SBCAST_CRED:
		rc = _unpack_job_sbcast_cred_msg(msg, buffer);
		break;
	case RESPONSE_FED_INFO:
		rc = slurmdb_unpack_federation_rec(&msg->data,
						   msg->protocol_version,
						   buffer);
		break;
	case REQUEST_STATS_INFO:
		rc = _unpack_stats_request_msg(msg, buffer);
		break;
	case RESPONSE_STATS_INFO:
		rc = _unpack_stats_response_msg(msg, buffer);
		break;
	case REQUEST_FORWARD_DATA:
		rc = _unpack_forward_data_msg(msg, buffer);
		break;
	case RESPONSE_PING_SLURMD:
		rc = _unpack_ping_slurmd_resp(msg, buffer);
		break;
	case RESPONSE_LICENSE_INFO:
		rc = _unpack_license_info_msg(msg, buffer);
		break;
	case REQUEST_LICENSE_INFO:
		rc = _unpack_license_info_request_msg(msg, buffer);
		break;
	case RESPONSE_JOB_ARRAY_ERRORS:
		rc = _unpack_job_array_resp_msg(msg, buffer);
		break;
	case REQUEST_ASSOC_MGR_INFO:
		rc = _unpack_assoc_mgr_info_request_msg(msg, buffer);
		break;
	case RESPONSE_ASSOC_MGR_INFO:
		rc = assoc_mgr_info_unpack_msg((assoc_mgr_info_msg_t **)
					       &(msg->data),
					       buffer,
					       msg->protocol_version);
		break;
	case REQUEST_NETWORK_CALLERID:
		rc = _unpack_network_callerid_msg(msg, buffer);
		break;
	case RESPONSE_NETWORK_CALLERID:
		rc = _unpack_network_callerid_resp_msg(msg, buffer);
		break;
	case REQUEST_CTLD_MULT_MSG:
	case RESPONSE_CTLD_MULT_MSG:
		rc = _unpack_buf_list_msg(msg, buffer);
		break;
	case REQUEST_SET_FS_DAMPENING_FACTOR:
		rc = _unpack_set_fs_dampening_factor_msg(msg, buffer);
		break;
	case RESPONSE_CONTROL_STATUS:
		rc = _unpack_control_status_msg(msg, buffer);
		break;
	case REQUEST_BURST_BUFFER_STATUS:
		rc = _unpack_bb_status_req_msg(msg, buffer);
		break;
	case RESPONSE_BURST_BUFFER_STATUS:
		rc = _unpack_bb_status_resp_msg(msg, buffer);
		break;
	case REQUEST_CRONTAB:
		rc = _unpack_crontab_request_msg(msg, buffer);
		break;
	case RESPONSE_CRONTAB:
		rc = _unpack_crontab_response_msg(msg, buffer);
		break;
	case REQUEST_UPDATE_CRONTAB:
		rc = _unpack_crontab_update_request_msg(msg, buffer);
		break;
	case RESPONSE_UPDATE_CRONTAB:
		rc = _unpack_crontab_update_response_msg(msg, buffer);
		break;
	case REQUEST_TLS_CERT:
		rc = _unpack_tls_cert_request_msg(msg, buffer);
		break;
	case RESPONSE_TLS_CERT:
		rc = _unpack_tls_cert_response_msg(msg, buffer);
		break;
	case RESPONSE_CONTAINER_STATE:
		rc = _unpack_container_state_msg(msg, buffer);
		break;
	case REQUEST_CONTAINER_KILL:
		rc = _unpack_container_signal_msg(msg, buffer);
		break;
	case REQUEST_CONTAINER_DELETE:
		rc = _unpack_container_delete_msg(msg, buffer);
		break;
	case RESPONSE_CONTAINER_START:
		rc = _unpack_container_started_msg(msg, buffer);
		break;
	case REQUEST_CONTAINER_EXEC:
		rc = _unpack_container_exec_msg(msg, buffer);
		break;
	case REQUEST_NODE_ALIAS_ADDRS:
		rc = slurm_unpack_node_alias_addrs(
			(slurm_node_alias_addrs_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_NODE_ALIAS_ADDRS:
		rc = _unpack_node_alias_addrs_resp_msg(msg, buffer);
		break;
	default:
		debug("No unpack method for msg type %u", msg->msg_type);
		return EINVAL;
		break;
	}

	if (rc) {
		error("Malformed RPC of type %s(%u) received",
		      rpc_num2string(msg->msg_type), msg->msg_type);

		/*
		 * The unpack functions should not leave this set on error,
		 * doing so would likely result in a double xfree() if we
		 * did not proactively clear it. (Which, instead, may cause
		 * a memory leak. But that's preferable.)
		 */
		xassert(!msg->data);
		msg->data = NULL;
	}
	return rc;
}

extern void pack_step_id(slurm_step_id_t *msg, buf_t *buffer,
			 uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack64(msg->sluid, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->step_het_comp, buffer);
	}
}

extern int unpack_step_id_members(slurm_step_id_t *msg, buf_t *buffer,
				  uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64(&msg->sluid, buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->step_id, buffer);
		safe_unpack32(&msg->step_het_comp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

extern void slurm_pack_selected_step(void *in, uint16_t protocol_version,
				     buf_t *buffer)
{
	slurm_selected_step_t *step = in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&step->step_id, buffer, protocol_version);
		pack32(step->array_task_id, buffer);
		pack32(step->het_job_offset, buffer);
		pack_bit_str_hex(step->array_bitmap, buffer);
	}
}

extern int slurm_unpack_selected_step(slurm_selected_step_t **step,
				      uint16_t protocol_version, buf_t *buffer)
{
	slurm_selected_step_t *step_ptr = xmalloc(sizeof(*step_ptr));

	*step = step_ptr;

	step_ptr->array_task_id = NO_VAL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_step_id_members(&step_ptr->step_id, buffer,
					    protocol_version);
		safe_unpack32(&step_ptr->array_task_id, buffer);
		safe_unpack32(&step_ptr->het_job_offset, buffer);
		unpack_bit_str_hex(&step_ptr->array_bitmap, buffer);
	} else
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_selected_step(step_ptr);
	*step = NULL;
	return SLURM_ERROR;
}
