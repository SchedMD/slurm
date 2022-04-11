/****************************************************************************\
 *  slurm_protocol_pack.c - functions to pack and unpack structures for RPCs
 *****************************************************************************
 *  Portions Copyright (C) 2010-2019 SchedMD LLC.
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
#include "src/common/gres.h"
#include "src/common/job_options.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/power.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/switch.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define _pack_job_info_msg(msg,buf)		_pack_buffer_msg(msg,buf)
#define _pack_job_step_info_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_burst_buffer_info_resp_msg(msg,buf) _pack_buffer_msg(msg,buf)
#define _pack_front_end_info_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_node_info_msg(msg,buf)		_pack_buffer_msg(msg,buf)
#define _pack_partition_info_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_stats_response_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_reserve_info_msg(msg,buf)		_pack_buffer_msg(msg,buf)
#define _pack_assoc_mgr_info_msg(msg,buf)      _pack_buffer_msg(msg,buf)

static int _unpack_node_info_members(node_info_t *node, buf_t *buffer,
				     uint16_t protocol_version);

static int _unpack_front_end_info_members(front_end_info_t *front_end,
					  buf_t *buffer,
					  uint16_t protocol_version);

static int _unpack_partition_info_members(partition_info_t *part,
					  buf_t *buffer,
					  uint16_t protocol_version);

static int _unpack_reserve_info_members(reserve_info_t *resv, buf_t *buffer,
					uint16_t protocol_version);

static void _pack_job_step_pids(job_step_pids_t *msg, buf_t *buffer,
				uint16_t protocol_version);
static int _unpack_job_step_pids(job_step_pids_t **msg, buf_t *buffer,
				 uint16_t protocol_version);

static int _unpack_job_info_members(job_info_t *job, buf_t *buffer,
				    uint16_t protocol_version);

static void _pack_ret_list(List ret_list, uint16_t size_val, buf_t *buffer,
			   uint16_t protocol_version);
static int _unpack_ret_list(List *ret_list, uint16_t size_val, buf_t *buffer,
			    uint16_t protocol_version);

static void _priority_factors_resp_list_del(void *x);

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

	if (header->version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack16(header->flags, buffer);
		pack16(header->msg_index, buffer);
		pack16(header->msg_type, buffer);
		pack32(header->body_length, buffer);
		pack16(header->forward.cnt, buffer);
		if (header->forward.cnt > 0) {
			packstr(header->forward.nodelist, buffer);
			pack32(header->forward.timeout, buffer);
			pack16(header->forward.tree_width, buffer);
		}
		pack16(header->ret_cnt, buffer);
		if (header->ret_cnt > 0) {
			_pack_ret_list(header->ret_list,
				       header->ret_cnt, buffer,
				       header->version);
		}
		slurm_pack_addr(&header->orig_addr, buffer);
	} else if (header->version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(header->flags, buffer);
		pack16(header->msg_index, buffer);
		pack16(header->msg_type, buffer);
		pack32(header->body_length, buffer);
		pack16(header->forward.cnt, buffer);
		if (header->forward.cnt > 0) {
			packstr(header->forward.nodelist, buffer);
			pack32(header->forward.timeout, buffer);
			pack16(header->forward.tree_width, buffer);
		}
		pack16(header->ret_cnt, buffer);
		if (header->ret_cnt > 0) {
			_pack_ret_list(header->ret_list,
				       header->ret_cnt, buffer,
				       header->version);
		}
		slurm_pack_slurm_addr(&header->orig_addr, buffer);
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
	forward_init(&header->forward);
	header->ret_list = NULL;
	safe_unpack16(&header->version, buffer);

	if (header->version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack16(&header->flags, buffer);
		safe_unpack16(&header->msg_index, buffer);
		safe_unpack16(&header->msg_type, buffer);
		safe_unpack32(&header->body_length, buffer);
		safe_unpack16(&header->forward.cnt, buffer);
		if (header->forward.cnt > 0) {
			safe_unpackstr(&header->forward.nodelist, buffer);
			safe_unpack32(&header->forward.timeout, buffer);
			safe_unpack16(&header->forward.tree_width, buffer);
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
	} else if (header->version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&header->flags, buffer);
		safe_unpack16(&header->msg_index, buffer);
		safe_unpack16(&header->msg_type, buffer);
		safe_unpack32(&header->body_length, buffer);
		safe_unpack16(&header->forward.cnt, buffer);
		if (header->forward.cnt > 0) {
			safe_unpackstr(&header->forward.nodelist, buffer);
			safe_unpack32(&header->forward.timeout, buffer);
			safe_unpack16(&header->forward.tree_width, buffer);
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
		slurm_unpack_slurm_addr_no_alloc(&header->orig_addr, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, header->version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	error("unpacking header");
	destroy_forward(&header->forward);
	FREE_NULL_LIST(header->ret_list);
	return SLURM_ERROR;
}


static void _pack_assoc_shares_object(void *in, uint32_t tres_cnt,
				      buf_t *buffer, uint16_t protocol_version)
{
	assoc_shares_object_t *object = (assoc_shares_object_t *)in;

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

	} else {
		error("_unpack_assoc_shares_object: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_assoc_shares_object(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

static void _pack_network_callerid_msg(network_callerid_msg_t *msg,
				       buf_t *buffer, uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packmem((char *)msg->ip_src, 16, buffer);
		packmem((char *)msg->ip_dst, 16, buffer);
		pack32(msg->port_src, buffer);
		pack32(msg->port_dst,	buffer);
		pack32((uint32_t)msg->af, buffer);
	}
}

static int _unpack_network_callerid_msg(network_callerid_msg_t **msg_ptr,
					buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	char *charptr_tmp = NULL;
	network_callerid_msg_t *msg;
	xassert(msg_ptr);

	msg = xmalloc(sizeof(network_callerid_msg_t));
	*msg_ptr = msg;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
		safe_unpack32(&msg->port_src,		buffer);
		safe_unpack32(&msg->port_dst,		buffer);
		safe_unpack32((uint32_t *)&msg->af,	buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	info("%s: error", __func__);
	*msg_ptr = NULL;
	xfree(charptr_tmp);
	slurm_free_network_callerid_msg(msg);
	return SLURM_ERROR;
}

static void _pack_network_callerid_resp_msg(network_callerid_resp_t *msg,
					    buf_t *buffer,
					    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id,		buffer);
		pack32(msg->return_code,	buffer);
		packstr(msg->node_name,		buffer);
	}
}

static int _unpack_network_callerid_resp_msg(network_callerid_resp_t **msg_ptr,
					     buf_t *buffer,
					     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	network_callerid_resp_t *msg;
	xassert(msg_ptr);

	msg = xmalloc(sizeof(network_callerid_resp_t));
	*msg_ptr = msg;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id,		buffer);
		safe_unpack32(&msg->return_code,	buffer);
		safe_unpackmem_xmalloc(&msg->node_name, &uint32_tmp, buffer);
	} else {

		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	info("%s: error", __func__);
	*msg_ptr = NULL;
	slurm_free_network_callerid_resp(msg);
	return SLURM_ERROR;
}

static void _pack_shares_request_msg(shares_request_msg_t *msg, buf_t *buffer,
				     uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;

	xassert(msg);

	if (msg->acct_list)
		count = list_count(msg->acct_list);
	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(msg->acct_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;


	if (msg->user_list)
		count = list_count(msg->user_list);
	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(msg->user_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
}

static int _unpack_shares_request_msg(shares_request_msg_t **msg, buf_t *buffer,
				      uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	int i;
	char *tmp_info = NULL;
	shares_request_msg_t *object_ptr = NULL;

	xassert(msg);

	object_ptr = xmalloc(sizeof(shares_request_msg_t));
	*msg = object_ptr;

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		object_ptr->acct_list = list_create(xfree_ptr);
		for (i = 0; i < count; i++) {
			safe_unpackstr(&tmp_info, buffer);
			list_append(object_ptr->acct_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		object_ptr->user_list = list_create(xfree_ptr);
		for (i = 0; i < count; i++) {
			safe_unpackstr(&tmp_info, buffer);
			list_append(object_ptr->user_list, tmp_info);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shares_request_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_shares_response_msg(shares_response_msg_t *msg, buf_t *buffer,
				      uint16_t protocol_version)
{
	ListIterator itr = NULL;
	assoc_shares_object_t *share = NULL;
	uint32_t count = NO_VAL;

	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
					protocol_version);
			list_iterator_destroy(itr);
		}
		pack64(msg->tot_shares, buffer);
	}
}

static int _unpack_shares_response_msg(shares_response_msg_t **msg,
				       buf_t *buffer,
				       uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	int i = 0;
	void *tmp_info = NULL;
	shares_response_msg_t *object_ptr = NULL;

	xassert(msg);

	object_ptr = xmalloc(sizeof(shares_response_msg_t));
	*msg = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_array(&object_ptr->tres_names,
				     &object_ptr->tres_cnt, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->assoc_shares_list =
				list_create(slurm_destroy_assoc_shares_object);
			for (i=0; i<count; i++) {
				if (_unpack_assoc_shares_object(
					    &tmp_info, object_ptr->tres_cnt,
					    buffer, protocol_version)
				    != SLURM_SUCCESS)
					goto unpack_error;
				list_append(object_ptr->assoc_shares_list,
					    tmp_info);
			}
		}

		safe_unpack64(&object_ptr->tot_shares, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shares_response_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;

}

static void _pack_priority_factors_object(void *in, buf_t *buffer,
					  uint16_t protocol_version)
{
	priority_factors_object_t *object = (priority_factors_object_t *)in;

	xassert(object);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(object->job_id, buffer);
		pack32(object->user_id, buffer);

		packdouble(object->priority_age, buffer);
		packdouble(object->priority_assoc, buffer);
		packdouble(object->priority_fs, buffer);
		packdouble(object->priority_js, buffer);
		packdouble(object->priority_part, buffer);
		packdouble(object->priority_qos, buffer);
		packdouble(object->direct_prio, buffer);
		pack32(object->priority_site, buffer);

		packdouble_array(object->priority_tres, object->tres_cnt,
				 buffer);
		pack32(object->tres_cnt, buffer);
		packstr_array(assoc_mgr_tres_name_array, object->tres_cnt,
			      buffer);
		packdouble_array(object->tres_weights, object->tres_cnt,
				 buffer);

		pack32(object->nice, buffer);
		packstr(object->partition, buffer);
	}
}

static int _unpack_priority_factors_object(void **object, buf_t *buffer,
					   uint16_t protocol_version)
{
	uint32_t tmp32 = 0;

	priority_factors_object_t *object_ptr =
		xmalloc(sizeof(priority_factors_object_t));
	*object = (void *) object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&object_ptr->job_id, buffer);
		safe_unpack32(&object_ptr->user_id, buffer);

		safe_unpackdouble(&object_ptr->priority_age, buffer);
		safe_unpackdouble(&object_ptr->priority_assoc, buffer);
		safe_unpackdouble(&object_ptr->priority_fs, buffer);
		safe_unpackdouble(&object_ptr->priority_js, buffer);
		safe_unpackdouble(&object_ptr->priority_part, buffer);
		safe_unpackdouble(&object_ptr->priority_qos, buffer);
		safe_unpackdouble(&object_ptr->direct_prio, buffer);
		safe_unpack32(&object_ptr->priority_site, buffer);

		safe_unpackdouble_array(&object_ptr->priority_tres, &tmp32,
					buffer);
		safe_unpack32(&object_ptr->tres_cnt, buffer);
		safe_unpackstr_array(&object_ptr->tres_names,
				     &object_ptr->tres_cnt, buffer);
		safe_unpackdouble_array(&object_ptr->tres_weights, &tmp32,
					buffer);

		safe_unpack32(&object_ptr->nice, buffer);
		safe_unpackstr(&object_ptr->partition, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	_priority_factors_resp_list_del(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

static void _pack_priority_factors_request_msg(
	priority_factors_request_msg_t *msg, buf_t *buffer,
	uint16_t protocol_version)
{
	uint32_t count;
	uint32_t* tmp = NULL;
	ListIterator itr = NULL;

	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (msg->job_id_list)
			count = list_count(msg->job_id_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(msg->job_id_list);
			while ((tmp = list_next(itr))) {
				pack32(*tmp, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (msg->uid_list)
			count = list_count(msg->uid_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(msg->uid_list);
			while ((tmp = list_next(itr))) {
				pack32(*tmp, buffer);
			}
			list_iterator_destroy(itr);
		}

		packstr(msg->partitions, buffer);
	}

}

static int _unpack_priority_factors_request_msg(
	priority_factors_request_msg_t **msg, buf_t *buffer,
	uint16_t protocol_version)
{
	uint32_t *uint32_tmp = NULL;
	uint32_t count = 0;
	int i;
	priority_factors_request_msg_t *object_ptr = NULL;

	xassert(msg);

	object_ptr = xmalloc(sizeof(priority_factors_request_msg_t));
	*msg = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->job_id_list = list_create(xfree_ptr);
			for (i = 0; i < count; i++) {
				uint32_tmp = xmalloc(sizeof(uint32_t));
				safe_unpack32(uint32_tmp, buffer);
				list_append(object_ptr->job_id_list,uint32_tmp);
				uint32_tmp = NULL;
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->uid_list = list_create(xfree_ptr);
			for (i = 0; i < count; i++) {
				uint32_tmp = xmalloc(sizeof(uint32_t));
				safe_unpack32(uint32_tmp, buffer);
				list_append(object_ptr->uid_list, uint32_tmp);
				uint32_tmp = NULL;
			}
		}

		safe_unpackstr(&object_ptr->partitions, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_priority_factors_request_msg(object_ptr);
	*msg = NULL;
	xfree(uint32_tmp);
	return SLURM_ERROR;
}

static void
_pack_priority_factors_response_msg(priority_factors_response_msg_t * msg,
				    buf_t *buffer,
				    uint16_t protocol_version)
{
	ListIterator itr = NULL;
	priority_factors_object_t *factors = NULL;
	uint32_t count = NO_VAL;

	xassert(msg);
	if (msg->priority_factors_list)
		count = list_count(msg->priority_factors_list);
	pack32(count, buffer);
	if (count && (count != NO_VAL)) {
		itr = list_iterator_create(msg->priority_factors_list);
		while ((factors = list_next(itr))) {
			_pack_priority_factors_object(factors, buffer,
						      protocol_version);
		}
		list_iterator_destroy(itr);
	}
}

static void _priority_factors_resp_list_del(void *x)
{
	priority_factors_object_t *tmp_info = (priority_factors_object_t *) x;
	int i;

	if (tmp_info) {
		xfree(tmp_info->cluster_name);
		xfree(tmp_info->partition);
		xfree(tmp_info->priority_tres);
		if (tmp_info->tres_cnt && tmp_info->tres_names) {
			for (i = 0; i < tmp_info->tres_cnt; i++)
				xfree(tmp_info->tres_names[i]);
		}
		xfree(tmp_info->tres_names);
		xfree(tmp_info->tres_weights);
		xfree(tmp_info);
	}
}

static int
_unpack_priority_factors_response_msg(priority_factors_response_msg_t ** msg,
				      buf_t *buffer,
				      uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	int i = 0;
	void *tmp_info = NULL;
	priority_factors_response_msg_t *object_ptr = NULL;
	xassert(msg);

	object_ptr = xmalloc(sizeof(priority_factors_response_msg_t));
	*msg = object_ptr;

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		object_ptr->priority_factors_list =
			list_create(_priority_factors_resp_list_del);
		for (i = 0; i < count; i++) {
			if (_unpack_priority_factors_object(&tmp_info, buffer,
							    protocol_version)
			    != SLURM_SUCCESS)
				goto unpack_error;
			list_append(object_ptr->priority_factors_list,
				    tmp_info);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_priority_factors_response_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;

}

static void
_pack_update_front_end_msg(update_front_end_msg_t * msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
		pack32(msg->node_state, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->reason_uid, buffer);
	}
}

static int
_unpack_update_front_end_msg(update_front_end_msg_t ** msg, buf_t *buffer,
			     uint16_t protocol_version)
{
	update_front_end_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(update_front_end_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&tmp_ptr->name, buffer);
		safe_unpack32(&tmp_ptr->node_state, buffer);
		safe_unpackstr(&tmp_ptr->reason, buffer);
		safe_unpack32(&tmp_ptr->reason_uid, buffer);
	} else {
		error("_unpack_update_front_end_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_front_end_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_update_node_msg(update_node_msg_t * msg, buf_t *buffer,
		      uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		packstr(msg->comment, buffer);
		pack32(msg->cpu_bind, buffer);
		packstr(msg->extra, buffer);
		packstr(msg->features, buffer);
		packstr(msg->features_act, buffer);
		packstr(msg->gres, buffer);
		packstr(msg->node_addr, buffer);
		packstr(msg->node_hostname, buffer);
		packstr(msg->node_names, buffer);
		pack32(msg->node_state, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->reason_uid, buffer);
		pack32(msg->weight, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		packstr(msg->comment, buffer);
		pack32(msg->cpu_bind, buffer);
		packstr(msg->features, buffer);
		packstr(msg->features_act, buffer);
		packstr(msg->gres, buffer);
		packstr(msg->node_addr, buffer);
		packstr(msg->node_hostname, buffer);
		packstr(msg->node_names, buffer);
		pack32(msg->node_state, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->reason_uid, buffer);
		pack32(msg->weight, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->cpu_bind, buffer);
		packstr(msg->features, buffer);
		packstr(msg->features_act, buffer);
		packstr(msg->gres, buffer);
		packstr(msg->node_addr, buffer);
		packstr(msg->node_hostname, buffer);
		packstr(msg->node_names, buffer);
		pack32(msg->node_state, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->reason_uid, buffer);
		pack32(msg->weight, buffer);
	}
}

static int
_unpack_update_node_msg(update_node_msg_t ** msg, buf_t *buffer,
			uint16_t protocol_version)
{
	update_node_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(update_node_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpackstr(&tmp_ptr->comment, buffer);
		safe_unpack32(&tmp_ptr->cpu_bind, buffer);
		safe_unpackstr(&tmp_ptr->extra, buffer);
		safe_unpackstr(&tmp_ptr->features, buffer);
		safe_unpackstr(&tmp_ptr->features_act, buffer);
		safe_unpackstr(&tmp_ptr->gres, buffer);
		safe_unpackstr(&tmp_ptr->node_addr, buffer);
		safe_unpackstr(&tmp_ptr->node_hostname, buffer);
		safe_unpackstr(&tmp_ptr->node_names, buffer);
		safe_unpack32(&tmp_ptr->node_state, buffer);
		safe_unpackstr(&tmp_ptr->reason, buffer);
		safe_unpack32(&tmp_ptr->reason_uid, buffer);
		safe_unpack32(&tmp_ptr->weight, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr(&tmp_ptr->comment, buffer);
		safe_unpack32(&tmp_ptr->cpu_bind, buffer);
		safe_unpackstr(&tmp_ptr->features, buffer);
		safe_unpackstr(&tmp_ptr->features_act, buffer);
		safe_unpackstr(&tmp_ptr->gres, buffer);
		safe_unpackstr(&tmp_ptr->node_addr, buffer);
		safe_unpackstr(&tmp_ptr->node_hostname, buffer);
		safe_unpackstr(&tmp_ptr->node_names, buffer);
		safe_unpack32(&tmp_ptr->node_state, buffer);
		safe_unpackstr(&tmp_ptr->reason, buffer);
		safe_unpack32(&tmp_ptr->reason_uid, buffer);
		safe_unpack32(&tmp_ptr->weight, buffer);

		if (tmp_ptr->node_state & NODE_STATE_POWERED_DOWN) {
			tmp_ptr->node_state &= ~NODE_STATE_POWERED_DOWN;
			tmp_ptr->node_state |= NODE_STATE_POWER_DOWN;
		}
		if (tmp_ptr->node_state & NODE_STATE_POWERING_UP) {
			tmp_ptr->node_state &= ~NODE_STATE_POWERING_UP;
			tmp_ptr->node_state |= NODE_STATE_POWER_UP;
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->cpu_bind, buffer);
		safe_unpackstr(&tmp_ptr->features, buffer);
		safe_unpackstr(&tmp_ptr->features_act, buffer);
		safe_unpackstr(&tmp_ptr->gres, buffer);
		safe_unpackstr(&tmp_ptr->node_addr, buffer);
		safe_unpackstr(&tmp_ptr->node_hostname, buffer);
		safe_unpackstr(&tmp_ptr->node_names, buffer);
		safe_unpack32(&tmp_ptr->node_state, buffer);
		safe_unpackstr(&tmp_ptr->reason, buffer);
		safe_unpack32(&tmp_ptr->reason_uid, buffer);
		safe_unpack32(&tmp_ptr->weight, buffer);

		if (tmp_ptr->node_state & NODE_STATE_POWERED_DOWN) {
			tmp_ptr->node_state &= ~NODE_STATE_POWERED_DOWN;
			tmp_ptr->node_state |= NODE_STATE_POWER_DOWN;
		}
		if (tmp_ptr->node_state & NODE_STATE_POWERING_UP) {
			tmp_ptr->node_state &= ~NODE_STATE_POWERING_UP;
			tmp_ptr->node_state |= NODE_STATE_POWER_UP;
		}
	} else {
		error("_unpack_update_node_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_node_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_acct_gather_node_resp_msg(acct_gather_node_resp_msg_t *msg,
				buf_t *buffer, uint16_t protocol_version)
{
	unsigned int i;

	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->node_name, buffer);
		pack16(msg->sensor_cnt, buffer);
		for (i = 0; i < msg->sensor_cnt; i++)
			acct_gather_energy_pack(&msg->energy[i],
						buffer, protocol_version);
	}

}
static int
_unpack_acct_gather_node_resp_msg(acct_gather_node_resp_msg_t **msg,
				  buf_t *buffer, uint16_t protocol_version)
{
	unsigned int i;
	acct_gather_node_resp_msg_t *node_data_ptr;
	acct_gather_energy_t *e;
	/* alloc memory for structure */
	xassert(msg);
	node_data_ptr = xmalloc(sizeof(acct_gather_node_resp_msg_t));
	*msg = node_data_ptr;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&node_data_ptr->node_name, buffer);
		safe_unpack16(&node_data_ptr->sensor_cnt, buffer);
		safe_xcalloc(node_data_ptr->energy, node_data_ptr->sensor_cnt,
			     sizeof(acct_gather_energy_t));
		for (i = 0; i < node_data_ptr->sensor_cnt; ++i) {
			e = &node_data_ptr->energy[i];
			if (acct_gather_energy_unpack(
				    &e, buffer, protocol_version, 0)
			    != SLURM_SUCCESS)
				goto unpack_error;
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_acct_gather_node_resp_msg(node_data_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_acct_gather_energy_req(acct_gather_energy_req_msg_t *msg,
			     buf_t *buffer, uint16_t protocol_version)
{
	xassert(msg);
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->context_id, buffer);
		pack16(msg->delta, buffer);
	}
}

static int
_unpack_acct_gather_energy_req(acct_gather_energy_req_msg_t **msg,
			       buf_t *buffer, uint16_t protocol_version)
{
	acct_gather_energy_req_msg_t *msg_ptr;

	xassert(msg);

	msg_ptr = xmalloc(sizeof(acct_gather_energy_req_msg_t));
	*msg = msg_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg_ptr->context_id, buffer);
		safe_unpack16(&msg_ptr->delta, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_acct_gather_energy_req_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;

}

static void
_pack_node_registration_status_msg(slurm_node_registration_status_msg_t *
				   msg, buf_t *buffer,
				   uint16_t protocol_version)
{
	int i;
	uint32_t gres_info_size = 0;
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_time(msg->timestamp, buffer);
		pack_time(msg->slurmd_start_time, buffer);
		pack32(msg->status, buffer);
		packstr(msg->features_active, buffer);
		packstr(msg->features_avail, buffer);
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
		for (i = 0; i < msg->job_count; i++) {
			pack_step_id(&msg->step_id[i], buffer,
				      protocol_version);
		}
		pack16(msg->flags, buffer);
		if (msg->flags & SLURMD_REG_FLAG_STARTUP)
			switch_g_pack_node_info(msg->switch_nodeinfo, buffer,
						protocol_version);
		if (msg->gres_info)
			gres_info_size = get_buf_offset(msg->gres_info);
		pack32(gres_info_size, buffer);
		if (gres_info_size) {
			packmem(get_buf_data(msg->gres_info), gres_info_size,
				buffer);
		}
		acct_gather_energy_pack(msg->energy, buffer, protocol_version);
		packstr(msg->version, buffer);

		packbool(msg->dynamic, buffer);
		packstr(msg->dynamic_feature, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->timestamp, buffer);
		pack_time(msg->slurmd_start_time, buffer);
		pack32(msg->status, buffer);
		packstr(msg->features_active, buffer);
		packstr(msg->features_avail, buffer);
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
		for (i = 0; i < msg->job_count; i++) {
			pack32(msg->step_id[i].job_id, buffer);
		}
		for (i = 0; i < msg->job_count; i++) {
			pack_old_step_id(msg->step_id[i].step_id, buffer);
		}
		pack16(msg->flags, buffer);
		if (msg->flags & SLURMD_REG_FLAG_STARTUP)
			switch_g_pack_node_info(msg->switch_nodeinfo, buffer,
						protocol_version);
		if (msg->gres_info)
			gres_info_size = get_buf_offset(msg->gres_info);
		pack32(gres_info_size, buffer);
		if (gres_info_size) {
			packmem(get_buf_data(msg->gres_info), gres_info_size,
				buffer);
		}
		acct_gather_energy_pack(msg->energy, buffer, protocol_version);
		packstr(msg->version, buffer);
	}
}

static int
_unpack_node_registration_status_msg(slurm_node_registration_status_msg_t
				     ** msg, buf_t *buffer,
				     uint16_t protocol_version)
{
	char *gres_info = NULL;
	uint32_t gres_info_size, uint32_tmp;
	int i;
	slurm_node_registration_status_msg_t *node_reg_ptr;

	/* alloc memory for structure */
	xassert(msg);
	node_reg_ptr = xmalloc(sizeof(slurm_node_registration_status_msg_t));
	*msg = node_reg_ptr;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&node_reg_ptr->timestamp, buffer);
		safe_unpack_time(&node_reg_ptr->slurmd_start_time, buffer);
		/* load the data values */
		safe_unpack32(&node_reg_ptr->status, buffer);
		safe_unpackstr(&node_reg_ptr->features_active, buffer);
		safe_unpackstr(&node_reg_ptr->features_avail, buffer);
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
			if (unpack_step_id_members(&node_reg_ptr->step_id[i],
						   buffer, protocol_version))
				goto unpack_error;

		safe_unpack16(&node_reg_ptr->flags, buffer);
		if ((node_reg_ptr->flags & SLURMD_REG_FLAG_STARTUP)
		    &&  (switch_g_unpack_node_info(
				 &node_reg_ptr->switch_nodeinfo, buffer,
				 protocol_version)))
			goto unpack_error;

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
					      protocol_version, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&node_reg_ptr->version, buffer);

		safe_unpackbool(&node_reg_ptr->dynamic, buffer);
		safe_unpackstr(&node_reg_ptr->dynamic_feature, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&node_reg_ptr->timestamp, buffer);
		safe_unpack_time(&node_reg_ptr->slurmd_start_time, buffer);
		/* load the data values */
		safe_unpack32(&node_reg_ptr->status, buffer);
		safe_unpackstr(&node_reg_ptr->features_active, buffer);
		safe_unpackstr(&node_reg_ptr->features_avail, buffer);
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
		for (i = 0; i < node_reg_ptr->job_count; i++) {
			safe_unpack32(&node_reg_ptr->step_id[i].job_id, buffer);
		}
		for (i = 0; i < node_reg_ptr->job_count; i++) {
			safe_unpack32(&node_reg_ptr->step_id[i].step_id,
				      buffer);
			convert_old_step_id(&node_reg_ptr->step_id[i].step_id);
			node_reg_ptr->step_id[i].step_het_comp = NO_VAL;
		}

		safe_unpack16(&node_reg_ptr->flags, buffer);
		if ((node_reg_ptr->flags & SLURMD_REG_FLAG_STARTUP)
		    &&  (switch_g_unpack_node_info(
				 &node_reg_ptr->switch_nodeinfo, buffer,
				 protocol_version)))
			goto unpack_error;

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
					      protocol_version, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr(&node_reg_ptr->version, buffer);
	} else {
		error("_unpack_node_registration_status_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	xfree(gres_info);
	slurm_free_node_registration_status_msg(node_reg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_resource_allocation_response_msg(resource_allocation_response_msg_t *msg,
				       buf_t *buffer,
				       uint16_t protocol_version)
{
	xassert(msg);


	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);
		packstr(msg->alias_list, buffer);
		packstr_array(msg->environment, msg->env_size, buffer);
		pack32(msg->error_code, buffer);
		packstr(msg->job_submit_user_msg, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->node_cnt, buffer);

		/* pack node_addr after node_cnt -- need it for unpacking */
		if (msg->node_addr && msg->node_cnt > 0) {
			pack8(1, buffer); /* non-null node_addr */
			slurm_pack_addr_array(msg->node_addr, msg->node_cnt,
					      buffer);
		} else {
			pack8(0, buffer);
		}
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
		select_g_select_jobinfo_pack(msg->select_jobinfo,
					     buffer,
					     protocol_version);

		if (msg->working_cluster_rec) {
			pack8(1, buffer);
			slurmdb_pack_cluster_rec(msg->working_cluster_rec,
						 protocol_version, buffer);
		} else {
			pack8(0, buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);
		packstr(msg->alias_list, buffer);
		packstr_array(msg->environment, msg->env_size, buffer);
		pack32(msg->error_code, buffer);
		packstr(msg->job_submit_user_msg, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->node_cnt, buffer);

		/* pack node_addr after node_cnt -- need it for unpacking */
		if (msg->node_addr && msg->node_cnt > 0) {
			pack8(1, buffer); /* non-null node_addr */
			slurm_pack_slurm_addr_array(msg->node_addr,
						    msg->node_cnt, buffer);
		} else {
			pack8(0, buffer);
		}
		packstr(msg->node_list, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
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
		select_g_select_jobinfo_pack(msg->select_jobinfo,
					     buffer,
					     protocol_version);

		if (msg->working_cluster_rec) {
			pack8(1, buffer);
			slurmdb_pack_cluster_rec(msg->working_cluster_rec,
						 protocol_version, buffer);
		} else {
			pack8(0, buffer);
		}
	}
}

static int
_unpack_resource_allocation_response_msg(
	resource_allocation_response_msg_t** msg, buf_t *buffer,
	uint16_t protocol_version)
{
	uint8_t  uint8_tmp;
	uint32_t uint32_tmp;
	resource_allocation_response_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(resource_allocation_response_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr(&tmp_ptr->account, buffer);
		safe_unpackstr(&tmp_ptr->alias_list, buffer);
		safe_unpackstr_array(&tmp_ptr->environment,
				     &tmp_ptr->env_size, buffer);
		safe_unpack32(&tmp_ptr->error_code, buffer);
		safe_unpackstr(&tmp_ptr->job_submit_user_msg, buffer);
		safe_unpack32(&tmp_ptr->job_id, buffer);
		safe_unpack32(&tmp_ptr->node_cnt, buffer);

		/* unpack node_addr after node_cnt -- need it to unpack */
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			if (slurm_unpack_addr_array(&tmp_ptr->node_addr,
						    &uint32_tmp, buffer))
				goto unpack_error;
			if (uint32_tmp != tmp_ptr->node_cnt)
				goto unpack_error;
		} else
			tmp_ptr->node_addr = NULL;

		safe_unpackstr(&tmp_ptr->node_list, buffer);
		safe_unpack16(&tmp_ptr->ntasks_per_board, buffer);
		safe_unpack16(&tmp_ptr->ntasks_per_core, buffer);
		safe_unpack16(&tmp_ptr->ntasks_per_tres, buffer);
		safe_unpack16(&tmp_ptr->ntasks_per_socket, buffer);
		safe_unpack32(&tmp_ptr->num_cpu_groups, buffer);
		if (tmp_ptr->num_cpu_groups > 0) {
			safe_unpack16_array(&tmp_ptr->cpus_per_node,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&tmp_ptr->cpu_count_reps,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		} else {
			tmp_ptr->cpus_per_node = NULL;
			tmp_ptr->cpu_count_reps = NULL;
		}
		safe_unpackstr(&tmp_ptr->partition, buffer);
		safe_unpack64(&tmp_ptr->pn_min_memory, buffer);
		safe_unpackstr(&tmp_ptr->qos, buffer);
		safe_unpackstr(&tmp_ptr->resv_name, buffer);
		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			slurmdb_unpack_cluster_rec(
				(void **)&tmp_ptr->working_cluster_rec,
				protocol_version, buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&tmp_ptr->account, buffer);
		safe_unpackstr(&tmp_ptr->alias_list, buffer);
		safe_unpackstr_array(&tmp_ptr->environment,
				     &tmp_ptr->env_size, buffer);
		safe_unpack32(&tmp_ptr->error_code, buffer);
		safe_unpackstr(&tmp_ptr->job_submit_user_msg, buffer);
		safe_unpack32(&tmp_ptr->job_id, buffer);
		safe_unpack32(&tmp_ptr->node_cnt, buffer);

		/* unpack node_addr after node_cnt -- need it to unpack */
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			if (slurm_unpack_slurm_addr_array(&tmp_ptr->node_addr,
							  &uint32_tmp, buffer))
				goto unpack_error;
			if (uint32_tmp != tmp_ptr->node_cnt)
				goto unpack_error;
		} else
			tmp_ptr->node_addr = NULL;

		safe_unpackstr(&tmp_ptr->node_list, buffer);
		safe_unpack16(&tmp_ptr->ntasks_per_board, buffer);
		safe_unpack16(&tmp_ptr->ntasks_per_core, buffer);
		tmp_ptr->ntasks_per_tres = NO_VAL16;
		safe_unpack16(&tmp_ptr->ntasks_per_socket, buffer);
		safe_unpack32(&tmp_ptr->num_cpu_groups, buffer);
		if (tmp_ptr->num_cpu_groups > 0) {
			safe_unpack16_array(&tmp_ptr->cpus_per_node,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&tmp_ptr->cpu_count_reps,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		} else {
			tmp_ptr->cpus_per_node = NULL;
			tmp_ptr->cpu_count_reps = NULL;
		}
		safe_unpackstr(&tmp_ptr->partition, buffer);
		safe_unpack64(&tmp_ptr->pn_min_memory, buffer);
		safe_unpackstr(&tmp_ptr->qos, buffer);
		safe_unpackstr(&tmp_ptr->resv_name, buffer);
		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			slurmdb_unpack_cluster_rec(
				(void **)&tmp_ptr->working_cluster_rec,
				protocol_version, buffer);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resource_allocation_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_sbcast_cred_msg(job_sbcast_cred_msg_t * msg, buf_t *buffer,
			  uint16_t protocol_version)
{
	xassert(msg);

	pack32(msg->job_id, buffer);
	packstr(msg->node_list, buffer);

	pack32(0, buffer); /* was node_cnt */
	pack_sbcast_cred(msg->sbcast_cred, buffer, protocol_version);
}

static int
_unpack_job_sbcast_cred_msg(job_sbcast_cred_msg_t ** msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_sbcast_cred_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(job_sbcast_cred_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	safe_unpack32(&tmp_ptr->job_id, buffer);
	safe_unpackstr(&tmp_ptr->node_list, buffer);

	safe_unpack32(&uint32_tmp, buffer); /* was node_cnt */

	tmp_ptr->sbcast_cred = unpack_sbcast_cred(buffer, protocol_version);
	if (tmp_ptr->sbcast_cred == NULL)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_sbcast_cred_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_submit_response_msg(submit_response_msg_t * msg, buf_t *buffer,
			  uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->error_code, buffer);
		packstr(msg->job_submit_user_msg, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack_old_step_id(msg->step_id, buffer);
		pack32(msg->error_code, buffer);
		packstr(msg->job_submit_user_msg, buffer);
	}
}

static int
_unpack_submit_response_msg(submit_response_msg_t ** msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	submit_response_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(submit_response_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->job_id, buffer);
		safe_unpack32(&tmp_ptr->step_id, buffer);
		safe_unpack32(&tmp_ptr->error_code, buffer);
		safe_unpackstr(&tmp_ptr->job_submit_user_msg, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->job_id, buffer);
		safe_unpack32(&tmp_ptr->step_id, buffer);
		convert_old_step_id(&tmp_ptr->step_id);
		safe_unpack32(&tmp_ptr->error_code, buffer);
		safe_unpackstr(&tmp_ptr->job_submit_user_msg, buffer);
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_submit_response_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static int _unpack_node_info_msg(node_info_msg_t **msg, buf_t *buffer,
				 uint16_t protocol_version)
{
	int i;
	node_info_msg_t *tmp_ptr;

	xassert(msg);
	tmp_ptr = xmalloc(sizeof(node_info_msg_t));
	*msg = tmp_ptr;

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->record_count, buffer);
		safe_unpack_time(&tmp_ptr->last_update, buffer);

		safe_xcalloc(tmp_ptr->node_array, tmp_ptr->record_count,
			     sizeof(node_info_t));

		/* load individual job info */
		for (i = 0; i < tmp_ptr->record_count; i++) {
			if (_unpack_node_info_members(&tmp_ptr->node_array[i],
						      buffer,
						      protocol_version))
				goto unpack_error;
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_node_info_members(node_info_t * node, buf_t *buffer,
			  uint16_t protocol_version)
{
	uint32_t uint32_tmp;

	xassert(node);
	slurm_init_node_info_t(node, false);

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&node->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->node_hostname, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&node->node_addr, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->bcast_address, &uint32_tmp,
				       buffer);
		safe_unpack16(&node->port, buffer);
		safe_unpack32(&node->next_state, buffer);
		safe_unpack32(&node->node_state, buffer);
		safe_unpackstr_xmalloc(&node->version, &uint32_tmp, buffer);

		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack64(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);

		safe_unpackstr_xmalloc(&node->mcs_label, &uint32_tmp, buffer);
		safe_unpack32(&node->owner, buffer);
		safe_unpack16(&node->core_spec_cnt, buffer);
		safe_unpack32(&node->cpu_bind, buffer);
		safe_unpack64(&node->mem_spec_limit, buffer);
		safe_unpackstr_xmalloc(&node->cpu_spec_list, &uint32_tmp,
				       buffer);

		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack64(&node->free_mem, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->last_busy, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);

		if (select_g_select_nodeinfo_unpack(&node->select_nodeinfo,
						    buffer, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&node->arch, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features_act, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres_drain, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres_used, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->os, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->comment, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->extra, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->reason, &uint32_tmp, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (ext_sensors_data_unpack(&node->ext_sensors, buffer,
					    protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (power_mgmt_data_unpack(&node->power, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&node->tres_fmt_str, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&node->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->node_hostname, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&node->node_addr, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->bcast_address, &uint32_tmp,
				       buffer);
		safe_unpack16(&node->port, buffer);
		safe_unpack32(&node->next_state, buffer);
		safe_unpack32(&node->node_state, buffer);
		safe_unpackstr_xmalloc(&node->version, &uint32_tmp, buffer);

		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack64(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);

		safe_unpackstr_xmalloc(&node->mcs_label, &uint32_tmp, buffer);
		safe_unpack32(&node->owner, buffer);
		safe_unpack16(&node->core_spec_cnt, buffer);
		safe_unpack32(&node->cpu_bind, buffer);
		safe_unpack64(&node->mem_spec_limit, buffer);
		safe_unpackstr_xmalloc(&node->cpu_spec_list, &uint32_tmp,
				       buffer);

		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack64(&node->free_mem, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);

		if (select_g_select_nodeinfo_unpack(&node->select_nodeinfo,
						    buffer, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&node->arch, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features_act, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres_drain, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres_used, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->os, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->comment, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->reason, &uint32_tmp, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (ext_sensors_data_unpack(&node->ext_sensors, buffer,
					    protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (power_mgmt_data_unpack(&node->power, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&node->tres_fmt_str, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&node->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->node_hostname, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&node->node_addr, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->bcast_address, &uint32_tmp,
				       buffer);
		safe_unpack16(&node->port, buffer);
		safe_unpack32(&node->next_state, buffer);
		safe_unpack32(&node->node_state, buffer);
		safe_unpackstr_xmalloc(&node->version, &uint32_tmp, buffer);

		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack64(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);

		safe_unpackstr_xmalloc(&node->mcs_label, &uint32_tmp, buffer);
		safe_unpack32(&node->owner, buffer);
		safe_unpack16(&node->core_spec_cnt, buffer);
		safe_unpack32(&node->cpu_bind, buffer);
		safe_unpack64(&node->mem_spec_limit, buffer);
		safe_unpackstr_xmalloc(&node->cpu_spec_list, &uint32_tmp,
				       buffer);

		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack64(&node->free_mem, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);

		if (select_g_select_nodeinfo_unpack(&node->select_nodeinfo,
						    buffer, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&node->arch, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features_act, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres_drain, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres_used, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->os, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->reason, &uint32_tmp, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (ext_sensors_data_unpack(&node->ext_sensors, buffer,
					    protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (power_mgmt_data_unpack(&node->power, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&node->tres_fmt_str, &uint32_tmp,
				       buffer);
	} else {
		error("_unpack_node_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_members(node);
	return SLURM_ERROR;
}

static void
_pack_update_partition_msg(update_part_msg_t * msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->allow_accounts, buffer);
		packstr(msg->allow_alloc_nodes, buffer);
		packstr(msg->allow_groups, buffer);
		packstr(msg->allow_qos,    buffer);
		packstr(msg->alternate,    buffer);
		packstr(msg->billing_weights_str,  buffer);

		pack32(msg->cpu_bind, buffer);
		pack64(msg-> def_mem_per_cpu, buffer);
		pack32(msg-> default_time, buffer);
		packstr(msg->deny_accounts, buffer);
		packstr(msg->deny_qos,     buffer);
		pack16(msg-> flags,        buffer);
		packstr(msg->job_defaults_str, buffer);
		pack32(msg-> grace_time,   buffer);

		pack32(msg-> max_cpus_per_node, buffer);
		pack64(msg-> max_mem_per_cpu, buffer);
		pack32(msg-> max_nodes,    buffer);
		pack16(msg-> max_share,    buffer);
		pack32(msg-> max_time,     buffer);
		pack32(msg-> min_nodes,    buffer);

		packstr(msg->name,         buffer);
		packstr(msg->nodes,        buffer);

		pack16(msg-> over_time_limit, buffer);
		pack16(msg-> preempt_mode, buffer);
		pack16(msg-> priority_job_factor, buffer);
		pack16(msg-> priority_tier, buffer);
		packstr(msg->qos_char,     buffer);
		pack16(msg-> state_up,     buffer);
	}
}

static int
_unpack_update_partition_msg(update_part_msg_t ** msg, buf_t *buffer,
			     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	update_part_msg_t *tmp_ptr;

	xassert(msg);

	/* alloc memory for structure */
	tmp_ptr = xmalloc(sizeof(update_part_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->allow_accounts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->allow_alloc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->allow_groups,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->allow_qos,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->alternate, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->billing_weights_str,
				       &uint32_tmp, buffer);

		safe_unpack32(&tmp_ptr->cpu_bind, buffer);
		safe_unpack64(&tmp_ptr->def_mem_per_cpu, buffer);
		safe_unpack32(&tmp_ptr->default_time, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->deny_accounts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->deny_qos,
				       &uint32_tmp, buffer);
		safe_unpack16(&tmp_ptr->flags,     buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->job_defaults_str, &uint32_tmp,
				       buffer);
		safe_unpack32(&tmp_ptr->grace_time, buffer);

		safe_unpack32(&tmp_ptr->max_cpus_per_node, buffer);
		safe_unpack64(&tmp_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&tmp_ptr->max_nodes, buffer);
		safe_unpack16(&tmp_ptr->max_share, buffer);
		safe_unpack32(&tmp_ptr->max_time, buffer);
		safe_unpack32(&tmp_ptr->min_nodes, buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->nodes, &uint32_tmp, buffer);

		safe_unpack16(&tmp_ptr->over_time_limit, buffer);
		safe_unpack16(&tmp_ptr->preempt_mode, buffer);
		safe_unpack16(&tmp_ptr->priority_job_factor, buffer);
		safe_unpack16(&tmp_ptr->priority_tier, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->qos_char,
				       &uint32_tmp, buffer);
		safe_unpack16(&tmp_ptr->state_up,  buffer);
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_part_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_update_resv_msg(resv_desc_msg_t * msg, buf_t *buffer,
		      uint16_t protocol_version)
{
	uint32_t array_len;
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		packstr(msg->name,         buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->end_time,   buffer);
		pack32(msg->duration,      buffer);
		pack64(msg->flags,         buffer);
		if (msg->node_cnt) {
			for (array_len = 0; msg->node_cnt[array_len];
			     array_len++) {
				/* determine array length */
			}
			array_len++;	/* Include trailing zero */
		} else
			array_len = 0;
		pack32_array(msg->node_cnt, array_len, buffer);
		if (msg->core_cnt) {
			for (array_len = 0; msg->core_cnt[array_len];
			     array_len++) {
				/* determine array length */
			}
			array_len++;	/* Include trailing zero */
		} else
			array_len = 0;
		pack32_array(msg->core_cnt, array_len, buffer);
		packstr(msg->node_list,    buffer);
		packstr(msg->features,     buffer);
		packstr(msg->licenses,     buffer);
		pack32(msg->max_start_delay, buffer);
		packstr(msg->partition,    buffer);
		pack32(msg->purge_comp_time, buffer);
		pack32(msg->resv_watts,    buffer);
		packstr(msg->users,        buffer);
		packstr(msg->accounts,     buffer);
		packstr(msg->burst_buffer, buffer);
		packstr(msg->groups, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name,         buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->end_time,   buffer);
		pack32(msg->duration,      buffer);
		pack64(msg->flags,         buffer);
		if (msg->node_cnt) {
			for (array_len = 0; msg->node_cnt[array_len];
			     array_len++) {
				/* determine array length */
			}
			array_len++;	/* Include trailing zero */
		} else
			array_len = 0;
		pack32_array(msg->node_cnt, array_len, buffer);
		if (msg->core_cnt) {
			for (array_len = 0; msg->core_cnt[array_len];
			     array_len++) {
				/* determine array length */
			}
			array_len++;	/* Include trailing zero */
		} else
			array_len = 0;
		pack32_array(msg->core_cnt, array_len, buffer);
		packstr(msg->node_list,    buffer);
		packstr(msg->features,     buffer);
		packstr(msg->licenses,     buffer);
		pack32(msg->max_start_delay, buffer);
		packstr(msg->partition,    buffer);
		pack32(msg->purge_comp_time, buffer);
		pack32(msg->resv_watts,    buffer);
		packstr(msg->users,        buffer);
		packstr(msg->accounts,     buffer);
		packstr(msg->burst_buffer, buffer);
	}
}

static int
_unpack_update_resv_msg(resv_desc_msg_t ** msg, buf_t *buffer,
			uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	resv_desc_msg_t *tmp_ptr;

	xassert(msg);

	/* alloc memory for structure */
	tmp_ptr = xmalloc(sizeof(resv_desc_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpack_time(&tmp_ptr->start_time, buffer);
		safe_unpack_time(&tmp_ptr->end_time,   buffer);
		safe_unpack32(&tmp_ptr->duration,      buffer);
		safe_unpack64(&tmp_ptr->flags,         buffer);
		safe_unpack32_array(&tmp_ptr->node_cnt, &uint32_tmp, buffer);
		if (uint32_tmp > NO_VAL)
			goto unpack_error;
		if (uint32_tmp > 0) {
			/* Must be zero terminated */
			if (tmp_ptr->node_cnt[uint32_tmp-1] != 0)
				goto unpack_error;
		} else {
			/* This avoids a pointer to a zero length buffer */
			xfree(tmp_ptr->node_cnt);
		}
		safe_unpack32_array(&tmp_ptr->core_cnt, &uint32_tmp, buffer);
		if (uint32_tmp > NO_VAL)
			goto unpack_error;
		if (uint32_tmp > 0) {
			/* Must be zero terminated */
			if (tmp_ptr->core_cnt[uint32_tmp-1] != 0)
				goto unpack_error;
		} else {
			/* This avoids a pointer to a zero length buffer */
			xfree(tmp_ptr->core_cnt);
		}
		safe_unpackstr_xmalloc(&tmp_ptr->node_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->licenses,
				       &uint32_tmp, buffer);

		safe_unpack32(&tmp_ptr->max_start_delay, buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->purge_comp_time, buffer);
		safe_unpack32(&tmp_ptr->resv_watts, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->users,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->accounts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->burst_buffer,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->groups,
				       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpack_time(&tmp_ptr->start_time, buffer);
		safe_unpack_time(&tmp_ptr->end_time,   buffer);
		safe_unpack32(&tmp_ptr->duration,      buffer);
		safe_unpack64(&tmp_ptr->flags,         buffer);
		safe_unpack32_array(&tmp_ptr->node_cnt, &uint32_tmp, buffer);
		if (uint32_tmp > NO_VAL)
			goto unpack_error;
		if (uint32_tmp > 0) {
			/* Must be zero terminated */
			if (tmp_ptr->node_cnt[uint32_tmp-1] != 0)
				goto unpack_error;
		} else {
			/* This avoids a pointer to a zero length buffer */
			xfree(tmp_ptr->node_cnt);
		}
		safe_unpack32_array(&tmp_ptr->core_cnt, &uint32_tmp, buffer);
		if (uint32_tmp > NO_VAL)
			goto unpack_error;
		if (uint32_tmp > 0) {
			/* Must be zero terminated */
			if (tmp_ptr->core_cnt[uint32_tmp-1] != 0)
				goto unpack_error;
		} else {
			/* This avoids a pointer to a zero length buffer */
			xfree(tmp_ptr->core_cnt);
		}
		safe_unpackstr_xmalloc(&tmp_ptr->node_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->licenses,
				       &uint32_tmp, buffer);

		safe_unpack32(&tmp_ptr->max_start_delay, buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->purge_comp_time, buffer);
		safe_unpack32(&tmp_ptr->resv_watts, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->users,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->accounts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->burst_buffer,
				       &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_desc_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_delete_partition_msg(delete_part_msg_t * msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
	}
}

static int
_unpack_delete_partition_msg(delete_part_msg_t ** msg, buf_t *buffer,
			     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	delete_part_msg_t *tmp_ptr;

	xassert(msg);

	/* alloc memory for structure */
	tmp_ptr = xmalloc(sizeof(delete_part_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
	} else {
		error("_unpack_delete_partition_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_delete_part_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_resv_name_msg(reservation_name_msg_t * msg, buf_t *buffer,
		    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
	}
}

static int
_unpack_resv_name_msg(reservation_name_msg_t ** msg, buf_t *buffer,
		      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	reservation_name_msg_t *tmp_ptr;

	xassert(msg);

	/* alloc memory for structure */
	tmp_ptr = xmalloc(sizeof(reservation_name_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
	} else {
		error("_unpack_resv_name_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_name_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern int slurm_pack_list(List send_list,
			   void (*pack_function) (void *object,
						  uint16_t protocol_version,
						  buf_t *buffer),
			   buf_t *buffer, uint16_t protocol_version)
{
	uint32_t count = 0;
	uint32_t header_position;
	int rc = SLURM_SUCCESS;

	if (!send_list) {
		// to let user know there wasn't a list (error)
		pack32(NO_VAL, buffer);
		return rc;
	}

	header_position = get_buf_offset(buffer);

	count = list_count(send_list);
	pack32(count, buffer);

	if (count) {
		ListIterator itr = list_iterator_create(send_list);
		void *object = NULL;
		while ((object = list_next(itr))) {
			(*(pack_function))(object, protocol_version, buffer);
			if (size_buf(buffer) > REASONABLE_BUF_SIZE) {
				error("%s: size limit exceeded", __func__);
				/*
				 * rewind buffer, pack NO_VAL as count instead
				 */
				set_buf_offset(buffer, header_position);
				pack32(NO_VAL, buffer);
				rc = ESLURM_RESULT_TOO_LARGE;
				break;
			}
		}
		list_iterator_destroy(itr);
	}

	return rc;
}

extern int slurm_pack_list_until(List send_list, pack_function_t pack_function,
				 buf_t *buffer, uint32_t max_buf_size,
				 uint16_t protocol_version)
{
	uint32_t count = 0;
	uint32_t header_position, last_good_position;
	int rc = SLURM_SUCCESS;

	if (!send_list) {
		/* let user know there wasn't a list (error) */
		pack32(NO_VAL, buffer);
		return rc;
	}

	header_position = get_buf_offset(buffer);

	count = list_count(send_list);
	pack32(count, buffer);

	if (count) {
		ListIterator itr = list_iterator_create(send_list);
		void *object = NULL;
		last_good_position = get_buf_offset(buffer);
		count = 0;
		while ((object = list_next(itr))) {
			(*(pack_function))(object, protocol_version, buffer);
			if (size_buf(buffer) > max_buf_size) {
				/*
				 * rewind by one element to stay smaller than
				 * max_buf_size
				 */
				set_buf_offset(buffer, header_position);
				pack32(count, buffer);
				set_buf_offset(buffer, last_good_position);
				rc = ESLURM_RESULT_TOO_LARGE;
				break;
			}
			last_good_position = get_buf_offset(buffer);
			count += 1;
		}
		list_iterator_destroy(itr);
	}

	return rc;
}

extern int slurm_unpack_list(List *recv_list,
			     int (*unpack_function) (void **object,
						     uint16_t protocol_version,
						     buf_t *buffer),
			     void (*destroy_function) (void *object),
			     buf_t *buffer, uint16_t protocol_version)
{
	uint32_t count;

	xassert(recv_list);

	safe_unpack32(&count, buffer);
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
			list_append(*recv_list, object);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*recv_list);
	return SLURM_ERROR;
}

extern void _pack_job_step_create_request_msg(
	job_step_create_request_msg_t *msg, buf_t *buffer,
	uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->user_id, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		packstr(msg->container, buffer);
		pack32(msg->cpu_count, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		pack32(msg->num_tasks, buffer);
		pack64(msg->pn_min_memory, buffer);
		pack32(msg->time_limit, buffer);
		pack16(msg->threads_per_core, buffer);

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
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->user_id, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		pack32(msg->cpu_count, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		pack32(msg->num_tasks, buffer);
		pack64(msg->pn_min_memory, buffer);
		pack32(msg->time_limit, buffer);
		pack16(msg->threads_per_core, buffer);

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
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_step, buffer);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_socket, buffer);
		packstr(msg->tres_per_task, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint8_t tmp8;
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->user_id, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		pack32(msg->cpu_count, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		pack32(msg->num_tasks, buffer);
		pack64(msg->pn_min_memory, buffer);
		pack32(msg->time_limit, buffer);

		pack16(msg->relative, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->plane_size, buffer);
		pack16(msg->port, buffer);
		pack16(0, buffer); /* was ckpt_interval */
		tmp8 = (msg->flags & SSF_EXCLUSIVE) ? 1 : 0;
		pack16((uint16_t)tmp8, buffer);
		pack16(msg->immediate, buffer);
		pack16(msg->resv_port_cnt, buffer);
		pack32(msg->srun_pid, buffer);

		packstr(msg->host, buffer);
		packstr(msg->name, buffer);
		packstr(msg->network, buffer);
		packstr(msg->node_list, buffer);
		packnull(buffer); /* was ckpt_dir */
		packstr(msg->features, buffer);

		tmp8 = (msg->flags & SSF_NO_KILL) ? 1 : 0;
		pack8(tmp8, buffer);
		tmp8 = (msg->flags & SSF_OVERCOMMIT) ? 1 : 0;
		pack8(tmp8, buffer);

		packstr(msg->cpus_per_tres, buffer);
		packstr(msg->mem_per_tres, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		packstr(msg->tres_per_step, buffer);
		packstr(msg->tres_per_node, buffer);
		packstr(msg->tres_per_socket, buffer);
		packstr(msg->tres_per_task, buffer);
	}

}

extern int _unpack_job_step_create_request_msg(
	job_step_create_request_msg_t **msg, buf_t *buffer,
	uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_step_create_request_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(job_step_create_request_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&tmp_ptr->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&tmp_ptr->user_id, buffer);
		safe_unpack32(&tmp_ptr->min_nodes, buffer);
		safe_unpack32(&tmp_ptr->max_nodes, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->container, &uint32_tmp,
				       buffer);
		safe_unpack32(&tmp_ptr->cpu_count, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_min, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_max, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_gov, buffer);
		safe_unpack32(&tmp_ptr->num_tasks, buffer);
		safe_unpack64(&tmp_ptr->pn_min_memory, buffer);
		safe_unpack32(&tmp_ptr->time_limit, buffer);
		safe_unpack16(&tmp_ptr->threads_per_core, buffer);

		safe_unpack16(&tmp_ptr->relative, buffer);
		safe_unpack32(&tmp_ptr->task_dist, buffer);
		safe_unpack16(&tmp_ptr->plane_size, buffer);
		safe_unpack16(&tmp_ptr->port, buffer);
		safe_unpack16(&tmp_ptr->immediate, buffer);
		safe_unpack16(&tmp_ptr->resv_port_cnt, buffer);
		safe_unpack32(&tmp_ptr->srun_pid, buffer);
		safe_unpack32(&tmp_ptr->flags, buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->network, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_list, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->exc_nodes, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->features, &uint32_tmp,
				       buffer);
		safe_unpack32(&tmp_ptr->step_het_comp_cnt, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->step_het_grps, &uint32_tmp,
				       buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->cpus_per_tres, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->mem_per_tres, &uint32_tmp,
				       buffer);
		safe_unpack16(&tmp_ptr->ntasks_per_tres, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->submit_line,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_freq, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_per_step, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_per_node, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_per_socket, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_per_task, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		char *temp_str;
		if (unpack_step_id_members(&tmp_ptr->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&tmp_ptr->user_id, buffer);
		safe_unpack32(&tmp_ptr->min_nodes, buffer);
		safe_unpack32(&tmp_ptr->max_nodes, buffer);
		safe_unpack32(&tmp_ptr->cpu_count, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_min, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_max, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_gov, buffer);
		safe_unpack32(&tmp_ptr->num_tasks, buffer);
		safe_unpack64(&tmp_ptr->pn_min_memory, buffer);
		safe_unpack32(&tmp_ptr->time_limit, buffer);
		safe_unpack16(&tmp_ptr->threads_per_core, buffer);

		safe_unpack16(&tmp_ptr->relative, buffer);
		safe_unpack32(&tmp_ptr->task_dist, buffer);
		safe_unpack16(&tmp_ptr->plane_size, buffer);
		safe_unpack16(&tmp_ptr->port, buffer);
		safe_unpack16(&tmp_ptr->immediate, buffer);
		safe_unpack16(&tmp_ptr->resv_port_cnt, buffer);
		safe_unpack32(&tmp_ptr->srun_pid, buffer);
		safe_unpack32(&tmp_ptr->flags, buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->network, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_list, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->exc_nodes, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->features, &uint32_tmp,
				       buffer);
		safe_unpack32(&tmp_ptr->step_het_comp_cnt, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->step_het_grps, &uint32_tmp,
				       buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->cpus_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->mem_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpack16(&tmp_ptr->ntasks_per_tres, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_freq, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->tres_per_step = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->tres_per_node = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->tres_per_socket = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->tres_per_task = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *temp_str;
		uint16_t uint16_tmp;
		safe_unpack32(&tmp_ptr->step_id.job_id, buffer);
		safe_unpack32(&tmp_ptr->step_id.step_id, buffer);
		tmp_ptr->step_id.step_het_comp = NO_VAL;
		safe_unpack32(&tmp_ptr->user_id, buffer);
		safe_unpack32(&tmp_ptr->min_nodes, buffer);
		safe_unpack32(&tmp_ptr->max_nodes, buffer);
		safe_unpack32(&tmp_ptr->cpu_count, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_min, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_max, buffer);
		safe_unpack32(&tmp_ptr->cpu_freq_gov, buffer);
		safe_unpack32(&tmp_ptr->num_tasks, buffer);
		safe_unpack64(&tmp_ptr->pn_min_memory, buffer);
		safe_unpack32(&tmp_ptr->time_limit, buffer);
		tmp_ptr->threads_per_core = NO_VAL16;

		safe_unpack16(&tmp_ptr->relative, buffer);
		safe_unpack32(&tmp_ptr->task_dist, buffer);
		safe_unpack16(&tmp_ptr->plane_size, buffer);
		safe_unpack16(&tmp_ptr->port, buffer);
		safe_unpack16(&uint16_tmp, buffer); /* was ckpt_interval */
		safe_unpack16(&uint16_tmp, buffer); /* was exclusive */
		if (uint16_tmp)
			tmp_ptr->flags |= SSF_EXCLUSIVE;
		else
			tmp_ptr->flags |= SSF_WHOLE;

		safe_unpack16(&tmp_ptr->immediate, buffer);
		safe_unpack16(&tmp_ptr->resv_port_cnt, buffer);
		safe_unpack32(&tmp_ptr->srun_pid, buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->network, &uint32_tmp,
				       buffer);
#ifdef HAVE_NATIVE_CRAY
		/*
		 * In 20.11 we stopped overloading network with the
		 * step_het_grps
		 */
		tmp_ptr->step_het_grps = tmp_ptr->network;
		tmp_ptr->network = NULL;
#endif
		safe_unpackstr_xmalloc(&tmp_ptr->node_list, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was ckpt_dir */
		safe_unpackstr_xmalloc(&tmp_ptr->features, &uint32_tmp,
				       buffer);

		safe_unpack8((uint8_t *)&uint16_tmp, buffer);
		if (uint16_tmp)
			tmp_ptr->flags |= SSF_NO_KILL;
		safe_unpack8((uint8_t *)&uint16_tmp, buffer);
		if (uint16_tmp)
			tmp_ptr->flags |= SSF_OVERCOMMIT;

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->cpus_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->mem_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		tmp_ptr->ntasks_per_tres = NO_VAL16;
		safe_unpackstr_xmalloc(&tmp_ptr->tres_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->tres_freq, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->tres_per_step = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->tres_per_node = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->tres_per_socket = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		tmp_ptr->tres_per_task = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__,  protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_create_request_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_kill_job_msg(kill_job_msg_t * msg, buf_t *buffer, uint16_t protocol_version)
{
	xassert(msg);


	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		gres_job_alloc_pack(msg->job_gres_info, buffer,
				    protocol_version);
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->het_job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->job_uid, buffer);
		pack32(msg->job_gid, buffer);
		packstr(msg->nodes, buffer);
		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->time, buffer);
		packstr(msg->work_dir, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		gres_job_alloc_pack(msg->job_gres_info, buffer,
				    protocol_version);
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->het_job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->job_uid, buffer);
		pack32(msg->job_gid, buffer);
		packstr(msg->nodes, buffer);
		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->time, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		gres_job_alloc_pack(msg->job_gres_info, buffer,
				    protocol_version);
		pack32(msg->step_id.job_id, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->job_uid, buffer);
		pack32(msg->job_gid, buffer);
		packstr(msg->nodes, buffer);
		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		pack_time(msg->start_time, buffer);
		pack_old_step_id(msg->step_id.step_id, buffer);
		pack_time(msg->time, buffer);
	}
}

static int
_unpack_kill_job_msg(kill_job_msg_t ** msg, buf_t *buffer,
		     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	kill_job_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(kill_job_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		if (gres_job_alloc_unpack(&tmp_ptr->job_gres_info,
					  buffer, protocol_version))
			goto unpack_error;
		if (unpack_step_id_members(&tmp_ptr->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&tmp_ptr->het_job_id, buffer);
		safe_unpack32(&tmp_ptr->job_state, buffer);
		safe_unpack32(&tmp_ptr->job_uid, buffer);
		safe_unpack32(&tmp_ptr->job_gid, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->nodes, &uint32_tmp, buffer);
		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&tmp_ptr->spank_job_env,
				     &tmp_ptr->spank_job_env_size, buffer);
		safe_unpack_time(&tmp_ptr->start_time, buffer);
		safe_unpack_time(&tmp_ptr->time, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->work_dir, &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (gres_job_alloc_unpack(&tmp_ptr->job_gres_info,
					  buffer, protocol_version))
			goto unpack_error;
		if (unpack_step_id_members(&tmp_ptr->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&tmp_ptr->het_job_id, buffer);
		safe_unpack32(&tmp_ptr->job_state, buffer);
		safe_unpack32(&tmp_ptr->job_uid, buffer);
		safe_unpack32(&tmp_ptr->job_gid, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->nodes, &uint32_tmp, buffer);
		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&tmp_ptr->spank_job_env,
				     &tmp_ptr->spank_job_env_size, buffer);
		safe_unpack_time(&tmp_ptr->start_time, buffer);
		safe_unpack_time(&tmp_ptr->time, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (gres_job_alloc_unpack(&tmp_ptr->job_gres_info,
					  buffer, protocol_version))
			goto unpack_error;
		safe_unpack32(&tmp_ptr->step_id.job_id, buffer);
		safe_unpack32(&tmp_ptr->het_job_id, buffer);
		safe_unpack32(&tmp_ptr->job_state, buffer);
		safe_unpack32(&tmp_ptr->job_uid, buffer);
		safe_unpack32(&tmp_ptr->job_gid, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->nodes, &uint32_tmp, buffer);
		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&tmp_ptr->spank_job_env,
				     &tmp_ptr->spank_job_env_size, buffer);
		safe_unpack_time(&tmp_ptr->start_time, buffer);
		safe_unpack32(&tmp_ptr->step_id.step_id, buffer);
		convert_old_step_id(&tmp_ptr->step_id.step_id);
		tmp_ptr->step_id.step_het_comp = NO_VAL;
		safe_unpack_time(&tmp_ptr->time, buffer);
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_kill_job_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_epilog_comp_msg(epilog_complete_msg_t * msg, buf_t *buffer,
		      uint16_t protocol_version)
{
	xassert(msg);
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32((uint32_t)msg->job_id, buffer);
		pack32((uint32_t)msg->return_code, buffer);
		packstr(msg->node_name, buffer);
	}
}

static int
_unpack_epilog_comp_msg(epilog_complete_msg_t ** msg, buf_t *buffer,
			uint16_t protocol_version)
{
	epilog_complete_msg_t *tmp_ptr;
	uint32_t uint32_tmp;
	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(epilog_complete_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&(tmp_ptr->job_id), buffer);
		safe_unpack32(&(tmp_ptr->return_code), buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->node_name),
				       &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_epilog_complete_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void _pack_job_step_create_response_msg(
	job_step_create_response_msg_t *msg, buf_t *buffer,
	uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(msg->def_cpu_bind_type, buffer);
		packstr(msg->resv_ports, buffer);
		pack32(msg->job_step_id, buffer);
		pack_slurm_step_layout(
			msg->step_layout, buffer, protocol_version);
		slurm_cred_pack(msg->cred, buffer, protocol_version);
		select_g_select_jobinfo_pack(
			msg->select_jobinfo, buffer, protocol_version);
		switch_g_pack_jobinfo(msg->switch_job, buffer,
				      protocol_version);
		pack16(msg->use_protocol_ver, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->def_cpu_bind_type, buffer);
		packstr(msg->resv_ports, buffer);
		pack_old_step_id(msg->job_step_id, buffer);
		pack_slurm_step_layout(
			msg->step_layout, buffer, protocol_version);
		slurm_cred_pack(msg->cred, buffer, protocol_version);
		select_g_select_jobinfo_pack(
			msg->select_jobinfo, buffer, protocol_version);
		switch_g_pack_jobinfo(msg->switch_job, buffer,
				      protocol_version);
		pack16(msg->use_protocol_ver, buffer);
	}
}

extern int _unpack_job_step_create_response_msg(
	job_step_create_response_msg_t **msg, buf_t *buffer,
	uint16_t protocol_version)
{
	job_step_create_response_msg_t *tmp_ptr = NULL;
	uint32_t uint32_tmp;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(job_step_create_response_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->def_cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(
			&tmp_ptr->resv_ports, &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->job_step_id, buffer);
		if (unpack_slurm_step_layout(&tmp_ptr->step_layout, buffer,
					     protocol_version))
			goto unpack_error;

		if (!(tmp_ptr->cred = slurm_cred_unpack(
			      buffer, protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(
			    &tmp_ptr->select_jobinfo, buffer, protocol_version))
			goto unpack_error;
		if (switch_g_unpack_jobinfo(&tmp_ptr->switch_job, buffer,
					    protocol_version)) {
			error("switch_g_unpack_jobinfo: %m");
			switch_g_free_jobinfo(tmp_ptr->switch_job);
			goto unpack_error;
		}
		safe_unpack16(&tmp_ptr->use_protocol_ver, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->def_cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(
			&tmp_ptr->resv_ports, &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->job_step_id, buffer);
		convert_old_step_id(&tmp_ptr->job_step_id);
		if (unpack_slurm_step_layout(&tmp_ptr->step_layout, buffer,
					     protocol_version))
			goto unpack_error;

		if (!(tmp_ptr->cred = slurm_cred_unpack(
			      buffer, protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(
			    &tmp_ptr->select_jobinfo, buffer, protocol_version))
			goto unpack_error;
		if (switch_g_unpack_jobinfo(&tmp_ptr->switch_job, buffer,
					    protocol_version)) {
			error("switch_g_unpack_jobinfo: %m");
			switch_g_free_jobinfo(tmp_ptr->switch_job);
			goto unpack_error;
		}
		safe_unpack16(&tmp_ptr->use_protocol_ver, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_create_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_partition_info_msg(partition_info_msg_t ** msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	int i;
	partition_info_t *partition = NULL;

	xassert(msg);
	*msg = xmalloc(sizeof(partition_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);

		safe_xcalloc((*msg)->partition_array, (*msg)->record_count,
			     sizeof(partition_info_t));
		partition = (*msg)->partition_array;

		/* load individual partition info */
		for (i = 0; i < (*msg)->record_count; i++) {
			if (_unpack_partition_info_members(&partition[i],
							   buffer,
							   protocol_version))
				goto unpack_error;
		}
	} else {
		error("_unpack_partition_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_partition_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}


static int
_unpack_partition_info_members(partition_info_t * part, buf_t *buffer,
			       uint16_t protocol_version)
{
	uint32_t uint32_tmp;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&part->name, &uint32_tmp, buffer);
		if (part->name == NULL)
			part->name = xmalloc(1);/* part->name = "" implicit */
		safe_unpack32(&part->cpu_bind,     buffer);
		safe_unpack32(&part->grace_time,   buffer);
		safe_unpack32(&part->max_time,     buffer);
		safe_unpack32(&part->default_time, buffer);
		safe_unpack32(&part->max_nodes,    buffer);
		safe_unpack32(&part->min_nodes,    buffer);
		safe_unpack32(&part->total_nodes,  buffer);
		safe_unpack32(&part->total_cpus,   buffer);
		safe_unpack64(&part->def_mem_per_cpu, buffer);
		safe_unpack32(&part->max_cpus_per_node, buffer);
		safe_unpack64(&part->max_mem_per_cpu, buffer);
		safe_unpack16(&part->flags,        buffer);
		safe_unpack16(&part->max_share,    buffer);
		safe_unpack16(&part->over_time_limit, buffer);
		safe_unpack16(&part->preempt_mode, buffer);
		safe_unpack16(&part->priority_job_factor, buffer);
		safe_unpack16(&part->priority_tier, buffer);
		safe_unpack16(&part->state_up,     buffer);
		safe_unpack16(&part->cr_type ,     buffer);
		safe_unpack16(&part->resume_timeout, buffer);
		safe_unpack16(&part->suspend_timeout, buffer);
		safe_unpack32(&part->suspend_time, buffer);

		safe_unpackstr_xmalloc(&part->allow_accounts, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_alloc_nodes, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->qos_char, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->alternate, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&part->deny_accounts, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->deny_qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->nodes, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&part->node_inx, buffer);

		safe_unpackstr_xmalloc(&part->billing_weights_str, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->tres_fmt_str, &uint32_tmp,
				       buffer);
		if (slurm_unpack_list(&part->job_defaults_list,
				      job_defaults_unpack, xfree_ptr,
				      buffer, protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&part->name, &uint32_tmp, buffer);
		if (part->name == NULL)
			part->name = xmalloc(1);/* part->name = "" implicit */
		safe_unpack32(&part->cpu_bind,     buffer);
		safe_unpack32(&part->grace_time,   buffer);
		safe_unpack32(&part->max_time,     buffer);
		safe_unpack32(&part->default_time, buffer);
		safe_unpack32(&part->max_nodes,    buffer);
		safe_unpack32(&part->min_nodes,    buffer);
		safe_unpack32(&part->total_nodes,  buffer);
		safe_unpack32(&part->total_cpus,   buffer);
		safe_unpack64(&part->def_mem_per_cpu, buffer);
		safe_unpack32(&part->max_cpus_per_node, buffer);
		safe_unpack64(&part->max_mem_per_cpu, buffer);
		safe_unpack16(&part->flags,        buffer);
		safe_unpack16(&part->max_share,    buffer);
		safe_unpack16(&part->over_time_limit, buffer);
		safe_unpack16(&part->preempt_mode, buffer);
		safe_unpack16(&part->priority_job_factor, buffer);
		safe_unpack16(&part->priority_tier, buffer);
		safe_unpack16(&part->state_up,     buffer);
		safe_unpack16(&part->cr_type ,     buffer);

		safe_unpackstr_xmalloc(&part->allow_accounts, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_alloc_nodes, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->qos_char, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->alternate, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&part->deny_accounts, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->deny_qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->nodes, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&part->node_inx, buffer);

		safe_unpackstr_xmalloc(&part->billing_weights_str, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->tres_fmt_str, &uint32_tmp,
				       buffer);
		if (slurm_unpack_list(&part->job_defaults_list,
				      job_defaults_unpack, xfree_ptr,
				      buffer, protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_partition_info_members(part);
	return SLURM_ERROR;
}

static int
_unpack_reserve_info_msg(reserve_info_msg_t ** msg, buf_t *buffer,
			 uint16_t protocol_version)
{
	int i;
	reserve_info_t *reserve = NULL;

	xassert(msg);
	*msg = xmalloc(sizeof(reserve_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);

		safe_xcalloc((*msg)->reservation_array, (*msg)->record_count,
			     sizeof(reserve_info_t));
		reserve = (*msg)->reservation_array;

		/* load individual reservation records */
		for (i = 0; i < (*msg)->record_count; i++) {
			if (_unpack_reserve_info_members(&reserve[i], buffer,
							 protocol_version))
				goto unpack_error;
		}
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reservation_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}


static int
_unpack_reserve_info_members(reserve_info_t * resv, buf_t *buffer,
			     uint16_t protocol_version)
{
	uint32_t i, uint32_tmp = 0;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&resv->accounts,	&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->burst_buffer,&uint32_tmp, buffer);
		safe_unpack32(&resv->core_cnt,          buffer);
		safe_unpack_time(&resv->end_time,	buffer);
		safe_unpackstr_xmalloc(&resv->features,	&uint32_tmp, buffer);
		safe_unpack64(&resv->flags,		buffer);
		safe_unpackstr_xmalloc(&resv->licenses, &uint32_tmp, buffer);
		safe_unpack32(&resv->max_start_delay, buffer);
		safe_unpackstr_xmalloc(&resv->name,	&uint32_tmp, buffer);
		safe_unpack32(&resv->node_cnt,		buffer);
		safe_unpackstr_xmalloc(&resv->node_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->partition, &uint32_tmp, buffer);
		safe_unpack32(&resv->purge_comp_time,   buffer);
		safe_unpack32(&resv->resv_watts,        buffer);
		safe_unpack_time(&resv->start_time,	buffer);

		safe_unpackstr_xmalloc(&resv->tres_str, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->users,	&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->groups,	&uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&resv->node_inx, buffer);

		safe_unpack32(&resv->core_spec_cnt,        buffer);
		if (resv->core_spec_cnt > 0) {
			safe_xcalloc(resv->core_spec, resv->core_spec_cnt,
				     sizeof(resv_core_spec_t));
		}
		for (i = 0; i < resv->core_spec_cnt; i++) {
			safe_unpackstr_xmalloc(&resv->core_spec[i].node_name,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&resv->core_spec[i].core_id,
					       &uint32_tmp, buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&resv->accounts,	&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->burst_buffer,&uint32_tmp, buffer);
		safe_unpack32(&resv->core_cnt,          buffer);
		safe_unpack_time(&resv->end_time,	buffer);
		safe_unpackstr_xmalloc(&resv->features,	&uint32_tmp, buffer);
		safe_unpack64(&resv->flags,		buffer);
		safe_unpackstr_xmalloc(&resv->licenses, &uint32_tmp, buffer);
		safe_unpack32(&resv->max_start_delay, buffer);
		safe_unpackstr_xmalloc(&resv->name,	&uint32_tmp, buffer);
		safe_unpack32(&resv->node_cnt,		buffer);
		safe_unpackstr_xmalloc(&resv->node_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->partition, &uint32_tmp, buffer);
		safe_unpack32(&resv->purge_comp_time,   buffer);
		safe_unpack32(&resv->resv_watts,        buffer);
		safe_unpack_time(&resv->start_time,	buffer);

		safe_unpackstr_xmalloc(&resv->tres_str, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->users,	&uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&resv->node_inx, buffer);

		safe_unpack32(&resv->core_spec_cnt,        buffer);
		if (resv->core_spec_cnt > 0) {
			safe_xcalloc(resv->core_spec, resv->core_spec_cnt,
				     sizeof(resv_core_spec_t));
		}
		for (i = 0; i < resv->core_spec_cnt; i++) {
			safe_unpackstr_xmalloc(&resv->core_spec[i].node_name,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&resv->core_spec[i].core_id,
					       &uint32_tmp, buffer);
		}
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
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
	uint32_t uint32_tmp = 0;
	char *temp_str;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpack32(&step->array_job_id, buffer);
		safe_unpack32(&step->array_task_id, buffer);
		if (unpack_step_id_members(&step->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
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

		safe_unpackstr_xmalloc(&step->cluster, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->container, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->srun_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->resv_ports, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->network, &uint32_tmp, buffer);
		unpack_bit_str_hex_as_inx(&step->node_inx, buffer);

		if (select_g_select_jobinfo_unpack(&step->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_xmalloc(&step->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpack16(&step->start_protocol_ver, buffer);

		safe_unpackstr_xmalloc(&step->cpus_per_tres,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->mem_per_tres,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->submit_line,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->tres_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->tres_freq,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->tres_per_step,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->tres_per_node,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->tres_per_socket,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->tres_per_task,
				       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&step->array_job_id, buffer);
		safe_unpack32(&step->array_task_id, buffer);
		if (unpack_step_id_members(&step->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
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

		safe_unpackstr_xmalloc(&step->cluster, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->srun_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->resv_ports, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->network, &uint32_tmp, buffer);
		unpack_bit_str_hex_as_inx(&step->node_inx, buffer);

		if (select_g_select_jobinfo_unpack(&step->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_xmalloc(&step->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpack16(&step->start_protocol_ver, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->cpus_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->mem_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&step->tres_bind, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->tres_freq, &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->tres_per_step = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->tres_per_node = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->tres_per_socket = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->tres_per_task = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&step->array_job_id, buffer);
		safe_unpack32(&step->array_task_id, buffer);
		if (unpack_step_id_members(&step->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
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

		safe_unpackstr_xmalloc(&step->cluster, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->srun_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->resv_ports, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->network, &uint32_tmp, buffer);
		unpack_bit_str_hex_as_inx(&step->node_inx, buffer);

		if (select_g_select_jobinfo_unpack(&step->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_xmalloc(&step->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpack16(&step->start_protocol_ver, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->cpus_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->mem_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&step->tres_bind, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->tres_freq, &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->tres_per_step = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->tres_per_node = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->tres_per_socket = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		step->tres_per_task = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
	} else {
		error("_unpack_job_step_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	/* no need to free here.  (we will just be freeing it 2 times
	   since this is freed in _unpack_job_step_info_response_msg
	*/
	//slurm_free_job_step_info_members(step);
	return SLURM_ERROR;
}

static int
_unpack_job_step_info_response_msg(job_step_info_response_msg_t** msg,
				   buf_t *buffer,
				   uint16_t protocol_version)
{
	int i = 0;
	job_step_info_t *step;

	xassert(msg);
	*msg = xmalloc(sizeof(job_step_info_response_msg_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&(*msg)->last_update, buffer);
		safe_unpack32(&(*msg)->job_step_count, buffer);

		safe_xcalloc((*msg)->job_steps, (*msg)->job_step_count,
			     sizeof(job_step_info_t));
		step = (*msg)->job_steps;

		for (i = 0; i < (*msg)->job_step_count; i++)
			if (_unpack_job_step_info_members(&step[i], buffer,
							  protocol_version))
				goto unpack_error;
	} else {
		error("_unpack_job_step_info_response_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_info_response_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_buffer_msg(slurm_msg_t * msg, buf_t *buffer)
{
	xassert(msg);
	packmem_array(msg->data, msg->data_size, buffer);
}

static void _pack_job_script_msg(buf_t *msg, buf_t *buffer,
				 uint16_t protocol_version)
{
	packstr(msg->head, buffer);
}

static int _unpack_job_script_msg(char **msg, buf_t *buffer,
				  uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	xassert(msg);

	safe_unpackstr_xmalloc(msg, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	xfree(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_job_info_msg(job_info_msg_t ** msg, buf_t *buffer,
		     uint16_t protocol_version)
{
	job_info_t *job = NULL;

	xassert(msg);
	*msg = xmalloc(sizeof(job_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);
		safe_unpack_time(&((*msg)->last_backfill), buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);
	} else {
		error("_unpack_job_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	if ((*msg)->record_count) {
		safe_xcalloc((*msg)->job_array, (*msg)->record_count,
			     sizeof(job_info_t));
		job = (*msg)->job_array;
	}
	/* load individual job info */
	for (int i = 0; i < (*msg)->record_count; i++) {
		job_info_t *job_ptr = &job[i];
		if (_unpack_job_info_members(job_ptr, buffer,
					     protocol_version))
			goto unpack_error;
		if ((job_ptr->bitflags & BACKFILL_SCHED) &&
		    (*msg)->last_backfill &&
		    IS_JOB_PENDING(job_ptr) &&
		    ((*msg)->last_backfill <= job_ptr->last_sched_eval))
			job_ptr->bitflags |= BACKFILL_LAST;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_info_msg(*msg);
	*msg = NULL;
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
	uint32_t uint32_tmp = 0;
	multi_core_data_t *mc_ptr;
	char *temp_str;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack32(&job->array_task_id, buffer);
		/* The array_task_str value is stored in slurmctld and passed
		 * here in hex format for best scalability. Its format needs
		 * to be converted to human readable form by the client. */
		safe_unpackstr_xmalloc(&job->array_task_str, &uint32_tmp,
				       buffer);
		safe_unpack32(&job->array_max_tasks, buffer);
		xlate_array_task_str(&job->array_task_str, job->array_max_tasks,
				     &job->array_bitmap);

		safe_unpack32(&job->assoc_id, buffer);
		safe_unpackstr_xmalloc(&job->container, &uint32_tmp, buffer);
		safe_unpack32(&job->delay_boot, buffer);
		safe_unpack32(&job->job_id, buffer);
		safe_unpack32(&job->user_id, buffer);
		safe_unpack32(&job->group_id, buffer);
		safe_unpack32(&job->het_job_id, buffer);
		safe_unpackstr_xmalloc(&job->het_job_id_set, &uint32_tmp,
				       buffer);
		safe_unpack32(&job->het_job_offset, buffer);
		safe_unpack32(&job->profile, buffer);

		safe_unpack32(&job->job_state, buffer);
		safe_unpack16(&job->batch_flag, buffer);
		safe_unpack16(&job->state_reason, buffer);
		safe_unpack8 (&job->power_flags, buffer);
		safe_unpack8 (&job->reboot, buffer);
		safe_unpack16(&job->restart_cnt, buffer);
		safe_unpack16(&job->show_flags, buffer);
		safe_unpack_time(&job->deadline, buffer);

		safe_unpack32(&job->alloc_sid, buffer);
		safe_unpack32(&job->time_limit, buffer);
		safe_unpack32(&job->time_min, buffer);

		safe_unpack32(&job->nice, buffer);

		safe_unpack_time(&job->submit_time, buffer);
		safe_unpack_time(&job->eligible_time, buffer);
		safe_unpack_time(&job->accrue_time, buffer);
		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack_time(&job->last_sched_eval, buffer);
		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpackdouble(&job->billable_tres, buffer);
		safe_unpackstr_xmalloc(&job->cluster, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->sched_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->admin_comment, &uint32_tmp,buffer);
		safe_unpack32(&job->site_factor, buffer);
		safe_unpackstr_xmalloc(&job->network, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->comment, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->container, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_features, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->batch_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->burst_buffer, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->burst_buffer_state, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->system_comment,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->qos, &uint32_tmp, buffer);
		safe_unpack_time(&job->preemptable_time, buffer);
		safe_unpackstr_xmalloc(&job->licenses, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->state_desc, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->resv_name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->mcs_label, &uint32_tmp, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpack32(&job->derived_ec, buffer);
		safe_unpackstr_xmalloc(&job->gres_total, &uint32_tmp, buffer);
		if (unpack_job_resources(&job->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&job->gres_detail_str,
				     &job->gres_detail_cnt, buffer);

		safe_unpackstr_xmalloc(&job->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->user_name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack32(&job->wait4switch, buffer);

		safe_unpackstr_xmalloc(&job->alloc_node, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->node_inx, buffer);

		if (select_g_select_jobinfo_unpack(&job->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		/*** unpack default job details ***/
		safe_unpackstr_xmalloc(&job->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->cluster_features, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->work_dir, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->dependency, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->command, &uint32_tmp, buffer);

		safe_unpack32(&job->num_cpus, buffer);
		safe_unpack32(&job->max_cpus, buffer);
		safe_unpack32(&job->num_nodes, buffer);
		safe_unpack32(&job->max_nodes, buffer);
		safe_unpack16(&job->requeue, buffer);
		safe_unpack16(&job->ntasks_per_node, buffer);
		safe_unpack16(&job->ntasks_per_tres, buffer);
		safe_unpack32(&job->num_tasks, buffer);

		safe_unpack16(&job->shared, buffer);
		safe_unpack32(&job->cpu_freq_min, buffer);
		safe_unpack32(&job->cpu_freq_max, buffer);
		safe_unpack32(&job->cpu_freq_gov, buffer);

		safe_unpackstr_xmalloc(&job->cronspec, &uint32_tmp, buffer);

		/*** unpack pending job details ***/
		safe_unpack16(&job->contiguous, buffer);
		safe_unpack16(&job->core_spec, buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack64(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);
		safe_unpackstr_xmalloc(&job->req_nodes, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->req_node_inx, buffer);

		safe_unpackstr_xmalloc(&job->exc_nodes, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->exc_node_inx, buffer);

		safe_unpackstr_xmalloc(&job->std_err, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_in, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_out, &uint32_tmp, buffer);

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
		safe_unpack64(&job->bitflags, buffer);
		safe_unpackstr_xmalloc(&job->tres_alloc_str, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->tres_req_str, &uint32_tmp, buffer);
		safe_unpack16(&job->start_protocol_ver, buffer);

		safe_unpackstr_xmalloc(&job->fed_origin_str, &uint32_tmp,
				       buffer);
		safe_unpack64(&job->fed_siblings_active, buffer);
		safe_unpackstr_xmalloc(&job->fed_siblings_active_str,
				       &uint32_tmp, buffer);
		safe_unpack64(&job->fed_siblings_viable, buffer);
		safe_unpackstr_xmalloc(&job->fed_siblings_viable_str,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&job->cpus_per_tres, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->mem_per_tres, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->tres_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->tres_freq, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->tres_per_job, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->tres_per_node, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->tres_per_socket, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->tres_per_task, &uint32_tmp,
				       buffer);

		safe_unpack16(&job->mail_type, buffer);
		safe_unpackstr_xmalloc(&job->mail_user, &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&job->selinux_context, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack32(&job->array_task_id, buffer);
		/* The array_task_str value is stored in slurmctld and passed
		 * here in hex format for best scalability. Its format needs
		 * to be converted to human readable form by the client. */
		safe_unpackstr_xmalloc(&job->array_task_str, &uint32_tmp,
				       buffer);
		safe_unpack32(&job->array_max_tasks, buffer);
		xlate_array_task_str(&job->array_task_str, job->array_max_tasks,
				     &job->array_bitmap);

		safe_unpack32(&job->assoc_id, buffer);
		safe_unpack32(&job->delay_boot, buffer);
		safe_unpack32(&job->job_id, buffer);
		safe_unpack32(&job->user_id, buffer);
		safe_unpack32(&job->group_id, buffer);
		safe_unpack32(&job->het_job_id, buffer);
		safe_unpackstr_xmalloc(&job->het_job_id_set, &uint32_tmp,
				       buffer);
		safe_unpack32(&job->het_job_offset, buffer);
		safe_unpack32(&job->profile, buffer);

		safe_unpack32(&job->job_state, buffer);
		safe_unpack16(&job->batch_flag, buffer);
		safe_unpack16(&job->state_reason, buffer);
		safe_unpack8 (&job->power_flags, buffer);
		safe_unpack8 (&job->reboot, buffer);
		safe_unpack16(&job->restart_cnt, buffer);
		safe_unpack16(&job->show_flags, buffer);
		safe_unpack_time(&job->deadline, buffer);

		safe_unpack32(&job->alloc_sid, buffer);
		safe_unpack32(&job->time_limit, buffer);
		safe_unpack32(&job->time_min, buffer);

		safe_unpack32(&job->nice, buffer);

		safe_unpack_time(&job->submit_time, buffer);
		safe_unpack_time(&job->eligible_time, buffer);
		safe_unpack_time(&job->accrue_time, buffer);
		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack_time(&job->last_sched_eval, buffer);
		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpackdouble(&job->billable_tres, buffer);
		safe_unpackstr_xmalloc(&job->cluster, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->sched_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->admin_comment, &uint32_tmp,buffer);
		safe_unpack32(&job->site_factor, buffer);
		safe_unpackstr_xmalloc(&job->network, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->comment, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_features, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->batch_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->burst_buffer, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->burst_buffer_state, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->system_comment,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->qos, &uint32_tmp, buffer);
		safe_unpack_time(&job->preemptable_time, buffer);
		safe_unpackstr_xmalloc(&job->licenses, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->state_desc, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->resv_name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->mcs_label, &uint32_tmp, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpack32(&job->derived_ec, buffer);
		safe_unpackstr_xmalloc(&job->gres_total, &uint32_tmp, buffer);
		if (unpack_job_resources(&job->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&job->gres_detail_str,
				     &job->gres_detail_cnt, buffer);

		safe_unpackstr_xmalloc(&job->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->user_name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack32(&job->wait4switch, buffer);

		safe_unpackstr_xmalloc(&job->alloc_node, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->node_inx, buffer);

		if (select_g_select_jobinfo_unpack(&job->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		/*** unpack default job details ***/
		safe_unpackstr_xmalloc(&job->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->cluster_features, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->work_dir, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->dependency, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->command, &uint32_tmp, buffer);

		safe_unpack32(&job->num_cpus, buffer);
		safe_unpack32(&job->max_cpus, buffer);
		safe_unpack32(&job->num_nodes, buffer);
		safe_unpack32(&job->max_nodes, buffer);
		safe_unpack16(&job->requeue, buffer);
		safe_unpack16(&job->ntasks_per_node, buffer);
		safe_unpack16(&job->ntasks_per_tres, buffer);
		safe_unpack32(&job->num_tasks, buffer);

		safe_unpack16(&job->shared, buffer);
		safe_unpack32(&job->cpu_freq_min, buffer);
		safe_unpack32(&job->cpu_freq_max, buffer);
		safe_unpack32(&job->cpu_freq_gov, buffer);

		safe_unpackstr_xmalloc(&job->cronspec, &uint32_tmp, buffer);

		/*** unpack pending job details ***/
		safe_unpack16(&job->contiguous, buffer);
		safe_unpack16(&job->core_spec, buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack64(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);
		safe_unpackstr_xmalloc(&job->req_nodes, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->req_node_inx, buffer);

		safe_unpackstr_xmalloc(&job->exc_nodes, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->exc_node_inx, buffer);

		safe_unpackstr_xmalloc(&job->std_err, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_in, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_out, &uint32_tmp, buffer);

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
		safe_unpack32(&uint32_tmp, buffer);
		job->bitflags = uint32_tmp;
		safe_unpackstr_xmalloc(&job->tres_alloc_str, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->tres_req_str, &uint32_tmp, buffer);
		safe_unpack16(&job->start_protocol_ver, buffer);

		safe_unpackstr_xmalloc(&job->fed_origin_str, &uint32_tmp,
				       buffer);
		safe_unpack64(&job->fed_siblings_active, buffer);
		safe_unpackstr_xmalloc(&job->fed_siblings_active_str,
				       &uint32_tmp, buffer);
		safe_unpack64(&job->fed_siblings_viable, buffer);
		safe_unpackstr_xmalloc(&job->fed_siblings_viable_str,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->cpus_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->mem_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&job->tres_bind, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->tres_freq, &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->tres_per_job = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->tres_per_node = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->tres_per_socket = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->tres_per_task = gres_prepend_tres_type(temp_str);
		xfree(temp_str);

		safe_unpack16(&job->mail_type, buffer);
		safe_unpackstr_xmalloc(&job->mail_user, &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack32(&job->array_task_id, buffer);
		/* The array_task_str value is stored in slurmctld and passed
		 * here in hex format for best scalability. Its format needs
		 * to be converted to human readable form by the client. */
		safe_unpackstr_xmalloc(&job->array_task_str, &uint32_tmp,
				       buffer);
		safe_unpack32(&job->array_max_tasks, buffer);
		xlate_array_task_str(&job->array_task_str, job->array_max_tasks,
				     &job->array_bitmap);

		safe_unpack32(&job->assoc_id, buffer);
		safe_unpack32(&job->delay_boot, buffer);
		safe_unpack32(&job->job_id,   buffer);
		safe_unpack32(&job->user_id,  buffer);
		safe_unpack32(&job->group_id, buffer);
		safe_unpack32(&job->het_job_id, buffer);
		safe_unpackstr_xmalloc(&job->het_job_id_set, &uint32_tmp,
				       buffer);
		safe_unpack32(&job->het_job_offset, buffer);
		safe_unpack32(&job->profile,  buffer);

		safe_unpack32(&job->job_state,    buffer);
		safe_unpack16(&job->batch_flag,   buffer);
		safe_unpack16(&job->state_reason, buffer);
		safe_unpack8 (&job->power_flags,  buffer);
		safe_unpack8 (&job->reboot,       buffer);
		safe_unpack16(&job->restart_cnt,  buffer);
		safe_unpack16(&job->show_flags,   buffer);
		safe_unpack_time(&job->deadline,  buffer);

		safe_unpack32(&job->alloc_sid,    buffer);
		safe_unpack32(&job->time_limit,   buffer);
		safe_unpack32(&job->time_min,     buffer);

		safe_unpack32(&job->nice, buffer);

		safe_unpack_time(&job->submit_time, buffer);
		safe_unpack_time(&job->eligible_time, buffer);
		safe_unpack_time(&job->accrue_time, buffer);
		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack_time(&job->last_sched_eval, buffer);
		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpackdouble(&job->billable_tres, buffer);
		safe_unpackstr_xmalloc(&job->cluster, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->sched_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->admin_comment, &uint32_tmp,buffer);
		safe_unpack32(&job->site_factor, buffer);
		safe_unpackstr_xmalloc(&job->network, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->comment, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_features, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->batch_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->burst_buffer, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->burst_buffer_state, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->system_comment,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->qos, &uint32_tmp, buffer);
		safe_unpack_time(&job->preemptable_time, buffer);
		safe_unpackstr_xmalloc(&job->licenses, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->state_desc, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->resv_name,  &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->mcs_label,  &uint32_tmp, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpack32(&job->derived_ec, buffer);
		safe_unpackstr_xmalloc(&job->gres_total,  &uint32_tmp, buffer);
		if (unpack_job_resources(&job->job_resrcs, buffer,
					 protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&job->gres_detail_str,
				     &job->gres_detail_cnt, buffer);

		safe_unpackstr_xmalloc(&job->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->user_name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack32(&job->wait4switch, buffer);

		safe_unpackstr_xmalloc(&job->alloc_node, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->node_inx, buffer);

		if (select_g_select_jobinfo_unpack(&job->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		/*** unpack default job details ***/
		safe_unpackstr_xmalloc(&job->features,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->cluster_features, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job->work_dir,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->dependency, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->command,    &uint32_tmp, buffer);

		safe_unpack32(&job->num_cpus, buffer);
		safe_unpack32(&job->max_cpus, buffer);
		safe_unpack32(&job->num_nodes,   buffer);
		safe_unpack32(&job->max_nodes,   buffer);
		safe_unpack16(&job->requeue,     buffer);
		safe_unpack16(&job->ntasks_per_node, buffer);
		job->ntasks_per_tres = NO_VAL16;
		safe_unpack32(&job->num_tasks, buffer);

		safe_unpack16(&job->shared,        buffer);
		safe_unpack32(&job->cpu_freq_min, buffer);
		safe_unpack32(&job->cpu_freq_max, buffer);
		safe_unpack32(&job->cpu_freq_gov, buffer);

		/*** unpack pending job details ***/
		safe_unpack16(&job->contiguous,    buffer);
		safe_unpack16(&job->core_spec,     buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack64(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);
		safe_unpackstr_xmalloc(&job->req_nodes, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->req_node_inx, buffer);

		safe_unpackstr_xmalloc(&job->exc_nodes, &uint32_tmp, buffer);

		unpack_bit_str_hex_as_inx(&job->exc_node_inx, buffer);

		safe_unpackstr_xmalloc(&job->std_err, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_in,  &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_out, &uint32_tmp, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		if (mc_ptr) {
			job->boards_per_node  = mc_ptr->boards_per_node;
			job->sockets_per_board  = mc_ptr->sockets_per_board;
			job->sockets_per_node  = mc_ptr->sockets_per_node;
			job->cores_per_socket  = mc_ptr->cores_per_socket;
			job->threads_per_core  = mc_ptr->threads_per_core;
			job->ntasks_per_board = mc_ptr->ntasks_per_board;
			job->ntasks_per_socket = mc_ptr->ntasks_per_socket;
			job->ntasks_per_core   = mc_ptr->ntasks_per_core;
			xfree(mc_ptr);
		}
		safe_unpack32(&uint32_tmp, buffer);
		job->bitflags = uint32_tmp;
		safe_unpackstr_xmalloc(&job->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->tres_req_str,
				       &uint32_tmp, buffer);
		safe_unpack16(&job->start_protocol_ver, buffer);

		safe_unpackstr_xmalloc(&job->fed_origin_str, &uint32_tmp,
				       buffer);
		safe_unpack64(&job->fed_siblings_active, buffer);
		safe_unpackstr_xmalloc(&job->fed_siblings_active_str,
				       &uint32_tmp, buffer);
		safe_unpack64(&job->fed_siblings_viable, buffer);
		safe_unpackstr_xmalloc(&job->fed_siblings_viable_str,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->cpus_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->mem_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&job->tres_bind, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->tres_freq, &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->tres_per_job = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->tres_per_node = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->tres_per_socket = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job->tres_per_task = gres_prepend_tres_type(temp_str);
		xfree(temp_str);

		safe_unpack16(&job->mail_type, buffer);
		safe_unpackstr_xmalloc(&job->mail_user, &uint32_tmp, buffer);
	} else {
		error("_unpack_job_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_info_members(job);
	return SLURM_ERROR;
}

static int _list_find_conf_entry(void *entry, void *key)
{
	config_key_pair_t *entry_ptr = NULL;

	if (key == NULL)
		return 1;

	entry_ptr = (config_key_pair_t *) entry;
	if (xstrcasecmp(entry_ptr->name, (char *) key) == 0)
		return 1;
	return 0;
}

static void
_pack_slurm_ctl_conf_msg(slurm_ctl_conf_info_msg_t * build_ptr, buf_t *buffer,
			 uint16_t protocol_version)
{
	uint32_t count = NO_VAL;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		pack_time(build_ptr->last_update, buffer);

		pack16(build_ptr->accounting_storage_enforce, buffer);
		packstr(build_ptr->accounting_storage_backup_host, buffer);
		packstr(build_ptr->accounting_storage_host, buffer);
		packstr(build_ptr->accounting_storage_ext_host, buffer);
		packstr(build_ptr->accounting_storage_params, buffer);
		pack16(build_ptr->accounting_storage_port, buffer);
		packstr(build_ptr->accounting_storage_tres, buffer);
		packstr(build_ptr->accounting_storage_type, buffer);
		packstr(build_ptr->accounting_storage_user, buffer);

		if (build_ptr->acct_gather_conf)
			count = list_count(build_ptr->acct_gather_conf);
		else
			count = NO_VAL;

		if (list_find_first(build_ptr->acct_gather_conf,
		                    _list_find_conf_entry,
		                    "ProfileInfluxDBPass"))
			count--;
		if (list_find_first(build_ptr->acct_gather_conf,
		                    _list_find_conf_entry,
		                    "ProfileInfluxDBUser"))
			count--;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->acct_gather_conf);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				if (xstrcasecmp(key_pair->name,
				                "ProfileInfluxDBPass") &&
				    xstrcasecmp(key_pair->name,
				                "ProfileInfluxDBUser"))
					pack_config_key_pair(key_pair,
					                     protocol_version,
					                     buffer);
			}
			list_iterator_destroy(itr);
		}

		packstr(build_ptr->acct_gather_energy_type, buffer);
		packstr(build_ptr->acct_gather_filesystem_type, buffer);
		packstr(build_ptr->acct_gather_interconnect_type, buffer);
		pack16(build_ptr->acct_gather_node_freq, buffer);
		packstr(build_ptr->acct_gather_profile_type, buffer);

		packstr(build_ptr->authalttypes, buffer);
		packstr(build_ptr->authalt_params, buffer);
		packstr(build_ptr->authinfo, buffer);
		packstr(build_ptr->authtype, buffer);

		pack16(build_ptr->batch_start_timeout, buffer);
		pack_time(build_ptr->boot_time, buffer);
		packstr(build_ptr->bb_type, buffer);
		packstr(build_ptr->bcast_exclude, buffer);
		packstr(build_ptr->bcast_parameters, buffer);

		pack_key_pair_list(build_ptr->cgroup_conf, protocol_version,
		                   buffer);
		packstr(build_ptr->cli_filter_plugins, buffer);
		packstr(build_ptr->cluster_name, buffer);
		packstr(build_ptr->comm_params, buffer);
		pack16(build_ptr->complete_wait, buffer);
		pack32(build_ptr->conf_flags, buffer);
		packstr_array(build_ptr->control_addr,
		              build_ptr->control_cnt, buffer);
		packstr_array(build_ptr->control_machine,
		              build_ptr->control_cnt, buffer);
		packstr(build_ptr->core_spec_plugin, buffer);
		pack32(build_ptr->cpu_freq_def, buffer);
		pack32(build_ptr->cpu_freq_govs, buffer);
		packstr(build_ptr->cred_type, buffer);

		pack64(build_ptr->def_mem_per_cpu, buffer);
		pack64(build_ptr->debug_flags, buffer);
		packstr(build_ptr->dependency_params, buffer);

		pack16(build_ptr->eio_timeout, buffer);
		pack16(build_ptr->enforce_part_limits, buffer);
		packstr(build_ptr->epilog, buffer);
		pack32(build_ptr->epilog_msg_time, buffer);
		packstr(build_ptr->epilog_slurmctld, buffer);

		pack_key_pair_list(build_ptr->ext_sensors_conf,
		                   protocol_version, buffer);

		packstr(build_ptr->ext_sensors_type, buffer);
		pack16(build_ptr->ext_sensors_freq, buffer);

		packstr(build_ptr->fed_params, buffer);
		pack32(build_ptr->first_job_id, buffer);
		pack16(build_ptr->fs_dampening_factor, buffer);

		pack16(build_ptr->get_env_timeout, buffer);
		packstr(build_ptr->gres_plugins, buffer);
		pack16(build_ptr->group_time, buffer);
		pack16(build_ptr->group_force, buffer);
		packstr(build_ptr->gpu_freq_def, buffer);

		pack32(build_ptr->hash_val, buffer);

		pack16(build_ptr->health_check_interval, buffer);
		pack16(build_ptr->health_check_node_state, buffer);
		packstr(build_ptr->health_check_program, buffer);

		pack16(build_ptr->inactive_limit, buffer);
		packstr(build_ptr->interactive_step_opts, buffer);

		packstr(build_ptr->job_acct_gather_freq, buffer);
		packstr(build_ptr->job_acct_gather_type, buffer);
		packstr(build_ptr->job_acct_gather_params, buffer);

		packstr(build_ptr->job_comp_host, buffer);
		packstr(build_ptr->job_comp_loc, buffer);
		packstr(build_ptr->job_comp_params, buffer);
		pack32((uint32_t)build_ptr->job_comp_port, buffer);
		packstr(build_ptr->job_comp_type, buffer);
		packstr(build_ptr->job_comp_user, buffer);
		packstr(build_ptr->job_container_plugin, buffer);

		packstr(build_ptr->job_credential_private_key, buffer);
		packstr(build_ptr->job_credential_public_certificate, buffer);
		(void)slurm_pack_list(build_ptr->job_defaults_list,
		                      job_defaults_pack, buffer,
		                      protocol_version);
		pack16(build_ptr->job_file_append, buffer);
		pack16(build_ptr->job_requeue, buffer);
		packstr(build_ptr->job_submit_plugins, buffer);

		pack16(build_ptr->keep_alive_time, buffer);
		pack16(build_ptr->kill_on_bad_exit, buffer);
		pack16(build_ptr->kill_wait, buffer);

		packstr(build_ptr->launch_params, buffer);
		packstr(build_ptr->launch_type, buffer);
		packstr(build_ptr->licenses, buffer);
		pack16(build_ptr->log_fmt, buffer);

		pack32(build_ptr->max_array_sz, buffer);
		pack32(build_ptr->max_dbd_msgs, buffer);
		packstr(build_ptr->mail_domain, buffer);
		packstr(build_ptr->mail_prog, buffer);
		pack32(build_ptr->max_job_cnt, buffer);
		pack32(build_ptr->max_job_id, buffer);
		pack64(build_ptr->max_mem_per_cpu, buffer);
		pack32(build_ptr->max_step_cnt, buffer);
		pack16(build_ptr->max_tasks_per_node, buffer);

		packstr(build_ptr->mcs_plugin, buffer);
		packstr(build_ptr->mcs_plugin_params, buffer);

		pack32(build_ptr->min_job_age, buffer);
		packstr(build_ptr->mpi_default, buffer);
		packstr(build_ptr->mpi_params, buffer);
		pack16(build_ptr->msg_timeout, buffer);

		pack32(build_ptr->next_job_id, buffer);

		pack_config_plugin_params_list(build_ptr->node_features_conf,
		                               protocol_version, buffer);

		packstr(build_ptr->node_features_plugins, buffer);
		packstr(build_ptr->node_prefix, buffer);

		pack16(build_ptr->over_time_limit, buffer);

		packstr(build_ptr->plugindir, buffer);
		packstr(build_ptr->plugstack, buffer);
		packstr(build_ptr->power_parameters, buffer);
		packstr(build_ptr->power_plugin, buffer);
		pack16(build_ptr->preempt_mode, buffer);
		packstr(build_ptr->preempt_type, buffer);
		pack32(build_ptr->preempt_exempt_time, buffer);
		packstr(build_ptr->prep_params, buffer);
		packstr(build_ptr->prep_plugins, buffer);

		pack32(build_ptr->priority_decay_hl, buffer);
		pack32(build_ptr->priority_calc_period, buffer);
		pack16(build_ptr->priority_favor_small, buffer);
		pack16(build_ptr->priority_flags, buffer);
		pack32(build_ptr->priority_max_age, buffer);
		packstr(build_ptr->priority_params, buffer);
		pack16(build_ptr->priority_reset_period, buffer);
		packstr(build_ptr->priority_type, buffer);
		pack32(build_ptr->priority_weight_age, buffer);
		pack32(build_ptr->priority_weight_assoc, buffer);
		pack32(build_ptr->priority_weight_fs, buffer);
		pack32(build_ptr->priority_weight_js, buffer);
		pack32(build_ptr->priority_weight_part, buffer);
		pack32(build_ptr->priority_weight_qos, buffer);
		packstr(build_ptr->priority_weight_tres, buffer);

		pack16(build_ptr->private_data, buffer);
		packstr(build_ptr->proctrack_type, buffer);
		packstr(build_ptr->prolog, buffer);
		pack16(build_ptr->prolog_epilog_timeout, buffer);
		packstr(build_ptr->prolog_slurmctld, buffer);
		pack16(build_ptr->prolog_flags, buffer);
		pack16(build_ptr->propagate_prio_process, buffer);
		packstr(build_ptr->propagate_rlimits, buffer);
		packstr(build_ptr->propagate_rlimits_except, buffer);

		packstr(build_ptr->reboot_program, buffer);
		pack16(build_ptr->reconfig_flags, buffer);
		packstr(build_ptr->requeue_exit, buffer);
		packstr(build_ptr->requeue_exit_hold, buffer);
		packstr(build_ptr->resume_fail_program, buffer);
		packstr(build_ptr->resume_program, buffer);
		pack16(build_ptr->resume_rate, buffer);
		pack16(build_ptr->resume_timeout, buffer);
		packstr(build_ptr->resv_epilog, buffer);
		pack16(build_ptr->resv_over_run, buffer);
		packstr(build_ptr->resv_prolog, buffer);
		pack16(build_ptr->ret2service, buffer);

		packstr(build_ptr->route_plugin, buffer);
		packstr(build_ptr->sched_params, buffer);
		packstr(build_ptr->sched_logfile, buffer);
		pack16(build_ptr->sched_log_level, buffer);
		pack16(build_ptr->sched_time_slice, buffer);
		packstr(build_ptr->schedtype, buffer);
		packstr(build_ptr->scron_params, buffer);
		packstr(build_ptr->select_type, buffer);

		pack_key_pair_list(build_ptr->select_conf_key_pairs,
		                   protocol_version, buffer);

		pack16(build_ptr->select_type_param, buffer);

		packstr(build_ptr->slurm_conf, buffer);
		pack32(build_ptr->slurm_user_id, buffer);
		packstr(build_ptr->slurm_user_name, buffer);
		pack32(build_ptr->slurmd_user_id, buffer);
		packstr(build_ptr->slurmd_user_name, buffer);

		packstr(build_ptr->slurmctld_addr, buffer);
		pack16(build_ptr->slurmctld_debug, buffer);
		packstr(build_ptr->slurmctld_logfile, buffer);
		packstr(build_ptr->slurmctld_params, buffer);
		packstr(build_ptr->slurmctld_pidfile, buffer);
		packstr(build_ptr->slurmctld_plugstack, buffer);
		pack_config_plugin_params_list(
			build_ptr->slurmctld_plugstack_conf,
			protocol_version,
			buffer);
		pack32(build_ptr->slurmctld_port, buffer);
		pack16(build_ptr->slurmctld_port_count, buffer);
		packstr(build_ptr->slurmctld_primary_off_prog, buffer);
		packstr(build_ptr->slurmctld_primary_on_prog, buffer);
		pack16(build_ptr->slurmctld_syslog_debug, buffer);
		pack16(build_ptr->slurmctld_timeout, buffer);

		pack16(build_ptr->slurmd_debug, buffer);
		packstr(build_ptr->slurmd_logfile, buffer);
		packstr(build_ptr->slurmd_params, buffer);
		packstr(build_ptr->slurmd_pidfile, buffer);
		pack32(build_ptr->slurmd_port, buffer);

		packstr(build_ptr->slurmd_spooldir, buffer);
		pack16(build_ptr->slurmd_syslog_debug, buffer);
		pack16(build_ptr->slurmd_timeout, buffer);
		packstr(build_ptr->srun_epilog, buffer);
		pack16(build_ptr->srun_port_range[0], buffer);
		pack16(build_ptr->srun_port_range[1], buffer);
		packstr(build_ptr->srun_prolog, buffer);
		packstr(build_ptr->state_save_location, buffer);
		packstr(build_ptr->suspend_exc_nodes, buffer);
		packstr(build_ptr->suspend_exc_parts, buffer);
		packstr(build_ptr->suspend_program, buffer);
		pack16(build_ptr->suspend_rate, buffer);
		pack32(build_ptr->suspend_time, buffer);
		pack16(build_ptr->suspend_timeout, buffer);
		packstr(build_ptr->switch_param, buffer);
		packstr(build_ptr->switch_type, buffer);

		packstr(build_ptr->task_epilog, buffer);
		packstr(build_ptr->task_prolog, buffer);
		packstr(build_ptr->task_plugin, buffer);
		pack32(build_ptr->task_plugin_param, buffer);
		pack16(build_ptr->tcp_timeout, buffer);
		packstr(build_ptr->tmp_fs, buffer);
		packstr(build_ptr->topology_param, buffer);
		packstr(build_ptr->topology_plugin, buffer);
		pack16(build_ptr->tree_width, buffer);

		packstr(build_ptr->unkillable_program, buffer);
		pack16(build_ptr->unkillable_timeout, buffer);
		packstr(build_ptr->version, buffer);
		pack16(build_ptr->vsize_factor, buffer);

		pack16(build_ptr->wait_time, buffer);
		packstr(build_ptr->x11_params, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_time(build_ptr->last_update, buffer);

		pack16(build_ptr->accounting_storage_enforce, buffer);
		packstr(build_ptr->accounting_storage_backup_host, buffer);
		packstr(build_ptr->accounting_storage_host, buffer);
		packstr(build_ptr->accounting_storage_ext_host, buffer);
		packstr(build_ptr->accounting_storage_params, buffer);
		pack16(build_ptr->accounting_storage_port, buffer);
		packstr(build_ptr->accounting_storage_tres, buffer);
		packstr(build_ptr->accounting_storage_type, buffer);
		packstr(build_ptr->accounting_storage_user, buffer);

		if (build_ptr->acct_gather_conf)
			count = list_count(build_ptr->acct_gather_conf);
		else
			count = NO_VAL;

		if (list_find_first(build_ptr->acct_gather_conf,
		                    _list_find_conf_entry,
		                    "ProfileInfluxDBPass"))
			count--;
		if (list_find_first(build_ptr->acct_gather_conf,
		                    _list_find_conf_entry,
		                    "ProfileInfluxDBUser"))
			count--;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->acct_gather_conf);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				if (xstrcasecmp(key_pair->name,
				                "ProfileInfluxDBPass") &&
				    xstrcasecmp(key_pair->name,
				                "ProfileInfluxDBUser"))
					pack_config_key_pair(key_pair,
					                     protocol_version,
					                     buffer);
			}
			list_iterator_destroy(itr);
		}

		packstr(build_ptr->acct_gather_energy_type, buffer);
		packstr(build_ptr->acct_gather_filesystem_type, buffer);
		packstr(build_ptr->acct_gather_interconnect_type, buffer);
		pack16(build_ptr->acct_gather_node_freq, buffer);
		packstr(build_ptr->acct_gather_profile_type, buffer);

		packstr(build_ptr->authalttypes, buffer);
		packstr(build_ptr->authalt_params, buffer);
		packstr(build_ptr->authinfo, buffer);
		packstr(build_ptr->authtype, buffer);

		pack16(build_ptr->batch_start_timeout, buffer);
		pack_time(build_ptr->boot_time, buffer);
		packstr(build_ptr->bb_type, buffer);

		pack_key_pair_list(build_ptr->cgroup_conf, protocol_version,
		                   buffer);
		packstr(build_ptr->cli_filter_plugins, buffer);
		packstr(build_ptr->cluster_name, buffer);
		packstr(build_ptr->comm_params, buffer);
		pack16(build_ptr->complete_wait, buffer);
		pack32(build_ptr->conf_flags, buffer);
		packstr_array(build_ptr->control_addr,
		              build_ptr->control_cnt, buffer);
		packstr_array(build_ptr->control_machine,
		              build_ptr->control_cnt, buffer);
		packstr(build_ptr->core_spec_plugin, buffer);
		pack32(build_ptr->cpu_freq_def, buffer);
		pack32(build_ptr->cpu_freq_govs, buffer);
		packstr(build_ptr->cred_type, buffer);

		pack64(build_ptr->def_mem_per_cpu, buffer);
		pack64(build_ptr->debug_flags, buffer);
		packstr(build_ptr->dependency_params, buffer);

		pack16(build_ptr->eio_timeout, buffer);
		pack16(build_ptr->enforce_part_limits, buffer);
		packstr(build_ptr->epilog, buffer);
		pack32(build_ptr->epilog_msg_time, buffer);
		packstr(build_ptr->epilog_slurmctld, buffer);

		pack_key_pair_list(build_ptr->ext_sensors_conf,
		                   protocol_version, buffer);

		packstr(build_ptr->ext_sensors_type, buffer);
		pack16(build_ptr->ext_sensors_freq, buffer);

		packstr(build_ptr->fed_params, buffer);
		pack32(build_ptr->first_job_id, buffer);
		pack16(build_ptr->fs_dampening_factor, buffer);

		pack16(build_ptr->get_env_timeout, buffer);
		packstr(build_ptr->gres_plugins, buffer);
		pack16(build_ptr->group_time, buffer);
		pack16(build_ptr->group_force, buffer);
		packstr(build_ptr->gpu_freq_def, buffer);

		pack32(build_ptr->hash_val, buffer);

		pack16(build_ptr->health_check_interval, buffer);
		pack16(build_ptr->health_check_node_state, buffer);
		packstr(build_ptr->health_check_program, buffer);

		pack16(build_ptr->inactive_limit, buffer);
		packstr(build_ptr->interactive_step_opts, buffer);

		packstr(build_ptr->job_acct_gather_freq, buffer);
		packstr(build_ptr->job_acct_gather_type, buffer);
		packstr(build_ptr->job_acct_gather_params, buffer);

		packstr(build_ptr->job_comp_host, buffer);
		packstr(build_ptr->job_comp_loc, buffer);
		packstr(build_ptr->job_comp_params, buffer);
		pack32((uint32_t)build_ptr->job_comp_port, buffer);
		packstr(build_ptr->job_comp_type, buffer);
		packstr(build_ptr->job_comp_user, buffer);
		packstr(build_ptr->job_container_plugin, buffer);

		packstr(build_ptr->job_credential_private_key, buffer);
		packstr(build_ptr->job_credential_public_certificate, buffer);
		(void)slurm_pack_list(build_ptr->job_defaults_list,
		                      job_defaults_pack, buffer,
		                      protocol_version);
		pack16(build_ptr->job_file_append, buffer);
		pack16(build_ptr->job_requeue, buffer);
		packstr(build_ptr->job_submit_plugins, buffer);

		pack16(build_ptr->keep_alive_time, buffer);
		pack16(build_ptr->kill_on_bad_exit, buffer);
		pack16(build_ptr->kill_wait, buffer);

		packstr(build_ptr->launch_params, buffer);
		packstr(build_ptr->launch_type, buffer);
		packstr(build_ptr->licenses, buffer);
		pack16(build_ptr->log_fmt, buffer);

		pack32(build_ptr->max_array_sz, buffer);
		pack32(build_ptr->max_dbd_msgs, buffer);
		packstr(build_ptr->mail_domain, buffer);
		packstr(build_ptr->mail_prog, buffer);
		pack32(build_ptr->max_job_cnt, buffer);
		pack32(build_ptr->max_job_id, buffer);
		pack64(build_ptr->max_mem_per_cpu, buffer);
		pack32(build_ptr->max_step_cnt, buffer);
		pack16(build_ptr->max_tasks_per_node, buffer);

		packstr(build_ptr->mcs_plugin, buffer);
		packstr(build_ptr->mcs_plugin_params, buffer);

		pack32(build_ptr->min_job_age, buffer);
		packstr(build_ptr->mpi_default, buffer);
		packstr(build_ptr->mpi_params, buffer);
		pack16(build_ptr->msg_timeout, buffer);

		pack32(build_ptr->next_job_id, buffer);

		pack_config_plugin_params_list(build_ptr->node_features_conf,
		                               protocol_version, buffer);

		packstr(build_ptr->node_features_plugins, buffer);
		packstr(build_ptr->node_prefix, buffer);

		pack16(build_ptr->over_time_limit, buffer);

		packstr(build_ptr->plugindir, buffer);
		packstr(build_ptr->plugstack, buffer);
		packstr(build_ptr->power_parameters, buffer);
		packstr(build_ptr->power_plugin, buffer);
		pack16(build_ptr->preempt_mode, buffer);
		packstr(build_ptr->preempt_type, buffer);
		pack32(build_ptr->preempt_exempt_time, buffer);
		packstr(build_ptr->prep_params, buffer);
		packstr(build_ptr->prep_plugins, buffer);

		pack32(build_ptr->priority_decay_hl, buffer);
		pack32(build_ptr->priority_calc_period, buffer);
		pack16(build_ptr->priority_favor_small, buffer);
		pack16(build_ptr->priority_flags, buffer);
		pack32(build_ptr->priority_max_age, buffer);
		packstr(build_ptr->priority_params, buffer);
		pack16(build_ptr->priority_reset_period, buffer);
		packstr(build_ptr->priority_type, buffer);
		pack32(build_ptr->priority_weight_age, buffer);
		pack32(build_ptr->priority_weight_assoc, buffer);
		pack32(build_ptr->priority_weight_fs, buffer);
		pack32(build_ptr->priority_weight_js, buffer);
		pack32(build_ptr->priority_weight_part, buffer);
		pack32(build_ptr->priority_weight_qos, buffer);
		packstr(build_ptr->priority_weight_tres, buffer);

		pack16(build_ptr->private_data, buffer);
		packstr(build_ptr->proctrack_type, buffer);
		packstr(build_ptr->prolog, buffer);
		pack16(build_ptr->prolog_epilog_timeout, buffer);
		packstr(build_ptr->prolog_slurmctld, buffer);
		pack16(build_ptr->prolog_flags, buffer);
		pack16(build_ptr->propagate_prio_process, buffer);
		packstr(build_ptr->propagate_rlimits, buffer);
		packstr(build_ptr->propagate_rlimits_except, buffer);

		packstr(build_ptr->reboot_program, buffer);
		pack16(build_ptr->reconfig_flags, buffer);
		packstr(build_ptr->requeue_exit, buffer);
		packstr(build_ptr->requeue_exit_hold, buffer);
		packstr(build_ptr->resume_fail_program, buffer);
		packstr(build_ptr->resume_program, buffer);
		pack16(build_ptr->resume_rate, buffer);
		pack16(build_ptr->resume_timeout, buffer);
		packstr(build_ptr->resv_epilog, buffer);
		pack16(build_ptr->resv_over_run, buffer);
		packstr(build_ptr->resv_prolog, buffer);
		pack16(build_ptr->ret2service, buffer);

		packstr(build_ptr->route_plugin, buffer);
		packstr(build_ptr->bcast_parameters, buffer);
		packstr(build_ptr->sched_params, buffer);
		packstr(build_ptr->sched_logfile, buffer);
		pack16(build_ptr->sched_log_level, buffer);
		pack16(build_ptr->sched_time_slice, buffer);
		packstr(build_ptr->schedtype, buffer);
		packstr(build_ptr->scron_params, buffer);
		packstr(build_ptr->select_type, buffer);

		pack_key_pair_list(build_ptr->select_conf_key_pairs,
		                   protocol_version, buffer);

		pack16(build_ptr->select_type_param, buffer);

		packstr(build_ptr->slurm_conf, buffer);
		pack32(build_ptr->slurm_user_id, buffer);
		packstr(build_ptr->slurm_user_name, buffer);
		pack32(build_ptr->slurmd_user_id, buffer);
		packstr(build_ptr->slurmd_user_name, buffer);

		packstr(build_ptr->slurmctld_addr, buffer);
		pack16(build_ptr->slurmctld_debug, buffer);
		packstr(build_ptr->slurmctld_logfile, buffer);
		packstr(build_ptr->slurmctld_params, buffer);
		packstr(build_ptr->slurmctld_pidfile, buffer);
		packstr(build_ptr->slurmctld_plugstack, buffer);
		pack_config_plugin_params_list(
			build_ptr->slurmctld_plugstack_conf,
			protocol_version,
			buffer);
		pack32(build_ptr->slurmctld_port, buffer);
		pack16(build_ptr->slurmctld_port_count, buffer);
		packstr(build_ptr->slurmctld_primary_off_prog, buffer);
		packstr(build_ptr->slurmctld_primary_on_prog, buffer);
		pack16(build_ptr->slurmctld_syslog_debug, buffer);
		pack16(build_ptr->slurmctld_timeout, buffer);

		pack16(build_ptr->slurmd_debug, buffer);
		packstr(build_ptr->slurmd_logfile, buffer);
		packstr(build_ptr->slurmd_params, buffer);
		packstr(build_ptr->slurmd_pidfile, buffer);
		pack32(build_ptr->slurmd_port, buffer);

		packstr(build_ptr->slurmd_spooldir, buffer);
		pack16(build_ptr->slurmd_syslog_debug, buffer);
		pack16(build_ptr->slurmd_timeout, buffer);
		packstr(build_ptr->srun_epilog, buffer);
		pack16(build_ptr->srun_port_range[0], buffer);
		pack16(build_ptr->srun_port_range[1], buffer);
		packstr(build_ptr->srun_prolog, buffer);
		packstr(build_ptr->state_save_location, buffer);
		packstr(build_ptr->suspend_exc_nodes, buffer);
		packstr(build_ptr->suspend_exc_parts, buffer);
		packstr(build_ptr->suspend_program, buffer);
		pack16(build_ptr->suspend_rate, buffer);
		pack32(build_ptr->suspend_time, buffer);
		pack16(build_ptr->suspend_timeout, buffer);
		packstr(build_ptr->switch_type, buffer);

		packstr(build_ptr->task_epilog, buffer);
		packstr(build_ptr->task_prolog, buffer);
		packstr(build_ptr->task_plugin, buffer);
		pack32(build_ptr->task_plugin_param, buffer);
		pack16(build_ptr->tcp_timeout, buffer);
		packstr(build_ptr->tmp_fs, buffer);
		packstr(build_ptr->topology_param, buffer);
		packstr(build_ptr->topology_plugin, buffer);
		pack16(build_ptr->tree_width, buffer);

		packstr(build_ptr->unkillable_program, buffer);
		pack16(build_ptr->unkillable_timeout, buffer);
		packstr(build_ptr->version, buffer);
		pack16(build_ptr->vsize_factor, buffer);

		pack16(build_ptr->wait_time, buffer);
		packstr(build_ptr->x11_params, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(build_ptr->last_update, buffer);

		pack16(build_ptr->accounting_storage_enforce, buffer);
		packstr(build_ptr->accounting_storage_backup_host, buffer);
		packstr(build_ptr->accounting_storage_host, buffer);
		packnull(buffer);
		pack32(build_ptr->accounting_storage_port, buffer);
		packstr(build_ptr->accounting_storage_tres, buffer);
		packstr(build_ptr->accounting_storage_type, buffer);
		packstr(build_ptr->accounting_storage_user, buffer);

		if (build_ptr->acct_gather_conf)
			count = list_count(build_ptr->acct_gather_conf);
		else
			count = NO_VAL;

		if (list_find_first(build_ptr->acct_gather_conf,
				    _list_find_conf_entry,
				    "ProfileInfluxDBPass"))
			count--;
		if (list_find_first(build_ptr->acct_gather_conf,
				    _list_find_conf_entry,
				    "ProfileInfluxDBUser"))
			count--;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->acct_gather_conf);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				if (xstrcasecmp(key_pair->name,
						"ProfileInfluxDBPass") &&
				    xstrcasecmp(key_pair->name,
						"ProfileInfluxDBUser"))
					pack_config_key_pair(key_pair,
							     protocol_version,
							     buffer);
			}
			list_iterator_destroy(itr);
		}

		packstr(build_ptr->acct_gather_energy_type, buffer);
		packstr(build_ptr->acct_gather_filesystem_type, buffer);
		packstr(build_ptr->acct_gather_interconnect_type, buffer);
		pack16(build_ptr->acct_gather_node_freq, buffer);
		packstr(build_ptr->acct_gather_profile_type, buffer);

		packstr(build_ptr->authinfo, buffer);
		packstr(build_ptr->authtype, buffer);

		pack16(build_ptr->batch_start_timeout, buffer);
		pack_time(build_ptr->boot_time, buffer);
		packstr(build_ptr->bb_type, buffer);

		pack_key_pair_list(build_ptr->cgroup_conf, protocol_version,
				   buffer);
		packstr(build_ptr->cli_filter_plugins, buffer);
		packstr(build_ptr->cluster_name, buffer);
		packstr(build_ptr->comm_params, buffer);
		pack16(build_ptr->complete_wait, buffer);
		pack32(build_ptr->conf_flags, buffer);
		packstr_array(build_ptr->control_addr,
			      build_ptr->control_cnt, buffer);
		packstr_array(build_ptr->control_machine,
			      build_ptr->control_cnt, buffer);
		packstr(build_ptr->core_spec_plugin, buffer);
		pack32(build_ptr->cpu_freq_def, buffer);
		pack32(build_ptr->cpu_freq_govs, buffer);
		packstr(build_ptr->cred_type, buffer);

		pack64(build_ptr->def_mem_per_cpu, buffer);
		pack64(build_ptr->debug_flags, buffer);
		packstr(build_ptr->dependency_params, buffer);

		pack16(build_ptr->eio_timeout, buffer);
		pack16(build_ptr->enforce_part_limits, buffer);
		packstr(build_ptr->epilog, buffer);
		pack32(build_ptr->epilog_msg_time, buffer);
		packstr(build_ptr->epilog_slurmctld, buffer);

		pack_key_pair_list(build_ptr->ext_sensors_conf,
				   protocol_version, buffer);

		packstr(build_ptr->ext_sensors_type, buffer);
		pack16(build_ptr->ext_sensors_freq, buffer);

		packstr(build_ptr->fed_params, buffer);
		pack32(build_ptr->first_job_id, buffer);
		pack16(build_ptr->fs_dampening_factor, buffer);

		pack16(build_ptr->get_env_timeout, buffer);
		packstr(build_ptr->gres_plugins, buffer);
		pack16(build_ptr->group_time, buffer);
		pack16(build_ptr->group_force, buffer);
		packstr(build_ptr->gpu_freq_def, buffer);

		pack32(build_ptr->hash_val, buffer);

		pack16(build_ptr->health_check_interval, buffer);
		pack16(build_ptr->health_check_node_state, buffer);
		packstr(build_ptr->health_check_program, buffer);

		pack16(build_ptr->inactive_limit, buffer);

		packstr(build_ptr->job_acct_gather_freq, buffer);
		packstr(build_ptr->job_acct_gather_type, buffer);
		packstr(build_ptr->job_acct_gather_params, buffer);

		packstr(build_ptr->job_comp_host, buffer);
		packstr(build_ptr->job_comp_loc, buffer);
		packstr(build_ptr->job_comp_params, buffer);
		pack32((uint32_t)build_ptr->job_comp_port, buffer);
		packstr(build_ptr->job_comp_type, buffer);
		packstr(build_ptr->job_comp_user, buffer);
		packstr(build_ptr->job_container_plugin, buffer);

		packstr(build_ptr->job_credential_private_key, buffer);
		packstr(build_ptr->job_credential_public_certificate, buffer);
		(void)slurm_pack_list(build_ptr->job_defaults_list,
				      job_defaults_pack, buffer,
				      protocol_version);
		pack16(build_ptr->job_file_append, buffer);
		pack16(build_ptr->job_requeue, buffer);
		packstr(build_ptr->job_submit_plugins, buffer);

		pack16(build_ptr->keep_alive_time, buffer);
		pack16(build_ptr->kill_on_bad_exit, buffer);
		pack16(build_ptr->kill_wait, buffer);

		packstr(build_ptr->launch_params, buffer);
		packstr(build_ptr->launch_type, buffer);
		packnull(buffer); /* was layouts */
		packstr(build_ptr->licenses, buffer);
		pack16(build_ptr->log_fmt, buffer);

		pack32(build_ptr->max_array_sz, buffer);
		pack32(build_ptr->max_dbd_msgs, buffer);
		packstr(build_ptr->mail_domain, buffer);
		packstr(build_ptr->mail_prog, buffer);
		pack32(build_ptr->max_job_cnt, buffer);
		pack32(build_ptr->max_job_id, buffer);
		pack64(build_ptr->max_mem_per_cpu, buffer);
		pack32(build_ptr->max_step_cnt, buffer);
		pack16(build_ptr->max_tasks_per_node, buffer);

		packstr(build_ptr->mcs_plugin, buffer);
		packstr(build_ptr->mcs_plugin_params, buffer);

		pack32(build_ptr->min_job_age, buffer);
		packstr(build_ptr->mpi_default, buffer);
		packstr(build_ptr->mpi_params, buffer);
		packnull(buffer); /* was msg_aggr_params */
		pack16(build_ptr->msg_timeout, buffer);

		pack32(build_ptr->next_job_id, buffer);

		pack_config_plugin_params_list(build_ptr->node_features_conf,
					       protocol_version, buffer);

		packstr(build_ptr->node_features_plugins, buffer);
		packstr(build_ptr->node_prefix, buffer);

		pack16(build_ptr->over_time_limit, buffer);

		packstr(build_ptr->plugindir, buffer);
		packstr(build_ptr->plugstack, buffer);
		packstr(build_ptr->power_parameters, buffer);
		packstr(build_ptr->power_plugin, buffer);
		pack16(build_ptr->preempt_mode, buffer);
		packstr(build_ptr->preempt_type, buffer);
		pack32(build_ptr->preempt_exempt_time, buffer);
		packstr(build_ptr->prep_params, buffer);
		packstr(build_ptr->prep_plugins, buffer);

		pack32(build_ptr->priority_decay_hl, buffer);
		pack32(build_ptr->priority_calc_period, buffer);
		pack16(build_ptr->priority_favor_small, buffer);
		pack16(build_ptr->priority_flags, buffer);
		pack32(build_ptr->priority_max_age, buffer);
		packstr(build_ptr->priority_params, buffer);
		pack16(build_ptr->priority_reset_period, buffer);
		packstr(build_ptr->priority_type, buffer);
		pack32(build_ptr->priority_weight_age, buffer);
		pack32(build_ptr->priority_weight_assoc, buffer);
		pack32(build_ptr->priority_weight_fs, buffer);
		pack32(build_ptr->priority_weight_js, buffer);
		pack32(build_ptr->priority_weight_part, buffer);
		pack32(build_ptr->priority_weight_qos, buffer);
		packstr(build_ptr->priority_weight_tres, buffer);

		pack16(build_ptr->private_data, buffer);
		packstr(build_ptr->proctrack_type, buffer);
		packstr(build_ptr->prolog, buffer);
		pack16(build_ptr->prolog_epilog_timeout, buffer);
		packstr(build_ptr->prolog_slurmctld, buffer);
		pack16(build_ptr->prolog_flags, buffer);
		pack16(build_ptr->propagate_prio_process, buffer);
		packstr(build_ptr->propagate_rlimits, buffer);
		packstr(build_ptr->propagate_rlimits_except, buffer);

		packstr(build_ptr->reboot_program, buffer);
		pack16(build_ptr->reconfig_flags, buffer);
		packstr(build_ptr->requeue_exit, buffer);
		packstr(build_ptr->requeue_exit_hold, buffer);
		packstr(build_ptr->resume_fail_program, buffer);
		packstr(build_ptr->resume_program, buffer);
		pack16(build_ptr->resume_rate, buffer);
		pack16(build_ptr->resume_timeout, buffer);
		packstr(build_ptr->resv_epilog, buffer);
		pack16(build_ptr->resv_over_run, buffer);
		packstr(build_ptr->resv_prolog, buffer);
		pack16(build_ptr->ret2service, buffer);

		packstr(build_ptr->route_plugin, buffer);
		packnull(buffer); /* was salloc_default_command */
		packstr(build_ptr->bcast_parameters, buffer);
		packstr(build_ptr->sched_params, buffer);
		packstr(build_ptr->sched_logfile, buffer);
		pack16(build_ptr->sched_log_level, buffer);
		pack16(build_ptr->sched_time_slice, buffer);
		packstr(build_ptr->schedtype, buffer);
		packstr(build_ptr->select_type, buffer);

		pack_key_pair_list(build_ptr->select_conf_key_pairs,
				   protocol_version, buffer);

		pack16(build_ptr->select_type_param, buffer);

		packstr(build_ptr->slurm_conf, buffer);
		pack32(build_ptr->slurm_user_id, buffer);
		packstr(build_ptr->slurm_user_name, buffer);
		pack32(build_ptr->slurmd_user_id, buffer);
		packstr(build_ptr->slurmd_user_name, buffer);

		packstr(build_ptr->slurmctld_addr, buffer);
		pack16(build_ptr->slurmctld_debug, buffer);
		packstr(build_ptr->slurmctld_logfile, buffer);
		packstr(build_ptr->slurmctld_params, buffer);
		packstr(build_ptr->slurmctld_pidfile, buffer);
		packstr(build_ptr->slurmctld_plugstack, buffer);
		pack_config_plugin_params_list(
			build_ptr->slurmctld_plugstack_conf,
			protocol_version,
			buffer);
		pack32(build_ptr->slurmctld_port, buffer);
		pack16(build_ptr->slurmctld_port_count, buffer);
		packstr(build_ptr->slurmctld_primary_off_prog, buffer);
		packstr(build_ptr->slurmctld_primary_on_prog, buffer);
		pack16(build_ptr->slurmctld_syslog_debug, buffer);
		pack16(build_ptr->slurmctld_timeout, buffer);

		pack16(build_ptr->slurmd_debug, buffer);
		packstr(build_ptr->slurmd_logfile, buffer);
		packstr(build_ptr->slurmd_params, buffer);
		packstr(build_ptr->slurmd_pidfile, buffer);
		pack32(build_ptr->slurmd_port, buffer);

		packstr(build_ptr->slurmd_spooldir, buffer);
		pack16(build_ptr->slurmd_syslog_debug, buffer);
		pack16(build_ptr->slurmd_timeout, buffer);
		packstr(build_ptr->srun_epilog, buffer);
		pack16(build_ptr->srun_port_range[0], buffer);
		pack16(build_ptr->srun_port_range[1], buffer);
		packstr(build_ptr->srun_prolog, buffer);
		packstr(build_ptr->state_save_location, buffer);
		packstr(build_ptr->suspend_exc_nodes, buffer);
		packstr(build_ptr->suspend_exc_parts, buffer);
		packstr(build_ptr->suspend_program, buffer);
		pack16(build_ptr->suspend_rate, buffer);
		pack32(build_ptr->suspend_time, buffer);
		pack16(build_ptr->suspend_timeout, buffer);
		packstr(build_ptr->switch_type, buffer);

		packstr(build_ptr->task_epilog, buffer);
		packstr(build_ptr->task_prolog, buffer);
		packstr(build_ptr->task_plugin, buffer);
		pack32(build_ptr->task_plugin_param, buffer);
		pack16(build_ptr->tcp_timeout, buffer);
		packstr(build_ptr->tmp_fs, buffer);
		packstr(build_ptr->topology_param, buffer);
		packstr(build_ptr->topology_plugin, buffer);
		pack16(build_ptr->tree_width, buffer);

		packstr(build_ptr->unkillable_program, buffer);
		pack16(build_ptr->unkillable_timeout, buffer);
		packstr(build_ptr->version, buffer);
		pack16(build_ptr->vsize_factor, buffer);

		pack16(build_ptr->wait_time, buffer);
		packstr(build_ptr->x11_params, buffer);
	}
}

static int
_unpack_slurm_ctl_conf_msg(slurm_ctl_conf_info_msg_t **build_buffer_ptr,
			   buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	slurm_ctl_conf_info_msg_t *build_ptr = xmalloc(sizeof(*build_ptr));
	*build_buffer_ptr = build_ptr;

	/* initialize this so we don't check for those not sending it */
	build_ptr->hash_val = NO_VAL;

	/* load the data values */
	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr_xmalloc(
			&build_ptr->accounting_storage_backup_host,
			&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_host,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_ext_host,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_params,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->accounting_storage_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_tres,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_user,
		                       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->acct_gather_conf,
		                         protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->acct_gather_energy_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_filesystem_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_interconnect_type,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_profile_type,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->authalttypes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authalt_params,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authinfo,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authtype,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->bb_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->bcast_exclude,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->bcast_parameters,
		                       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->cgroup_conf,
		                         protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&build_ptr->cli_filter_plugins,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cluster_name,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->comm_params,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpack32(&build_ptr->conf_flags, buffer);
		safe_unpackstr_array(&build_ptr->control_addr,
		                     &build_ptr->control_cnt, buffer);
		safe_unpackstr_array(&build_ptr->control_machine,
		                     &build_ptr->control_cnt, buffer);
		safe_unpackstr_xmalloc(&build_ptr->core_spec_plugin,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->cpu_freq_def, buffer);
		safe_unpack32(&build_ptr->cpu_freq_govs, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cred_type, &uint32_tmp,
		                       buffer);

		safe_unpack64(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack64(&build_ptr->debug_flags, buffer);
		safe_unpackstr_xmalloc(&build_ptr->dependency_params,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->eio_timeout, buffer);
		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog, &uint32_tmp,
		                       buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog_slurmctld,
		                       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->ext_sensors_conf,
		                         protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->ext_sensors_type,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->ext_sensors_freq, buffer);

		safe_unpackstr_xmalloc(&build_ptr->fed_params, &uint32_tmp,
		                       buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);
		safe_unpack16(&build_ptr->fs_dampening_factor, buffer);

		safe_unpack16(&build_ptr->get_env_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gres_plugins,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->group_time, buffer);
		safe_unpack16(&build_ptr->group_force, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gpu_freq_def,
		                       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr_xmalloc(&build_ptr->health_check_program,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);
		safe_unpackstr_xmalloc(&build_ptr->interactive_step_opts,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_freq,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_params,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_comp_host,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_loc,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_params,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_user,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_container_plugin,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_credential_private_key,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->
		                       job_credential_public_certificate,
		                       &uint32_tmp, buffer);
		if (slurm_unpack_list(&build_ptr->job_defaults_list,
		                      job_defaults_unpack, xfree_ptr,
		                      buffer,protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_submit_plugins,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->keep_alive_time, buffer);
		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr_xmalloc(&build_ptr->launch_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->launch_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->licenses,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->log_fmt, buffer);

		safe_unpack32(&build_ptr->max_array_sz, buffer);
		safe_unpack32(&build_ptr->max_dbd_msgs, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mail_domain,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mail_prog,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack64(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mcs_plugin,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mcs_plugin_params,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->min_job_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_default,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_params,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);

		if (unpack_config_plugin_params_list(
			    &build_ptr->node_features_conf,
			    protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->node_features_plugins,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->node_prefix,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->plugindir,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->plugstack,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->power_parameters,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->power_plugin,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr_xmalloc(&build_ptr->preempt_type,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->preempt_exempt_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prep_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prep_plugins,
		                       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_params, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_type, &uint32_tmp,
		                       buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_assoc, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_weight_tres,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr_xmalloc(&build_ptr->proctrack_type, &uint32_tmp,
		                       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->prolog_epilog_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog_slurmctld,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->prolog_flags, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits_except,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->reboot_program, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);

		safe_unpackstr_xmalloc(&build_ptr->requeue_exit,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->requeue_exit_hold,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->resume_fail_program,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resume_program,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_epilog, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_prolog, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr_xmalloc(&build_ptr->route_plugin,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_logfile,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr_xmalloc(&build_ptr->schedtype,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->scron_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->select_type,
		                       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->select_conf_key_pairs,
		                         protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurm_conf,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurm_user_name,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_user_name,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmctld_addr,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_logfile,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_pidfile,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_plugstack,
		                       &uint32_tmp, buffer);
		if (unpack_config_plugin_params_list(
			    &build_ptr->slurmctld_plugstack_conf,
			    protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_primary_off_prog,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_primary_on_prog,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmctld_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_logfile, &uint32_tmp,
		                       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_params, &uint32_tmp,
		                       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_pidfile, &uint32_tmp,
		                       buffer);
		safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmd_spooldir,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmd_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_epilog,
		                       &uint32_tmp, buffer);

		build_ptr->srun_port_range = xcalloc(2, sizeof(uint16_t));
		safe_unpack16(&build_ptr->srun_port_range[0], buffer);
		safe_unpack16(&build_ptr->srun_port_range[1], buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_prolog,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->state_save_location,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_nodes,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_parts,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_program,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->switch_param,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->switch_type,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->task_epilog,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_prolog,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_plugin,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->task_plugin_param, buffer);
		safe_unpack16(&build_ptr->tcp_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->tmp_fs, &uint32_tmp,
		                       buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_param,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_plugin,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpackstr_xmalloc(&build_ptr->unkillable_program,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->version,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->x11_params,
		                       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr_xmalloc(
			&build_ptr->accounting_storage_backup_host,
			&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_host,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_ext_host,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_params,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->accounting_storage_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_tres,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_user,
		                       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->acct_gather_conf,
		                         protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->acct_gather_energy_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_filesystem_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_interconnect_type,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_profile_type,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->authalttypes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authalt_params,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authinfo,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authtype,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->bb_type,
		                       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->cgroup_conf,
		                         protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&build_ptr->cli_filter_plugins,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cluster_name,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->comm_params,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpack32(&build_ptr->conf_flags, buffer);
		safe_unpackstr_array(&build_ptr->control_addr,
		                     &build_ptr->control_cnt, buffer);
		safe_unpackstr_array(&build_ptr->control_machine,
		                     &build_ptr->control_cnt, buffer);
		safe_unpackstr_xmalloc(&build_ptr->core_spec_plugin,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->cpu_freq_def, buffer);
		safe_unpack32(&build_ptr->cpu_freq_govs, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cred_type, &uint32_tmp,
		                       buffer);

		safe_unpack64(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack64(&build_ptr->debug_flags, buffer);
		safe_unpackstr_xmalloc(&build_ptr->dependency_params,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->eio_timeout, buffer);
		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog, &uint32_tmp,
		                       buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog_slurmctld,
		                       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->ext_sensors_conf,
		                         protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->ext_sensors_type,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->ext_sensors_freq, buffer);

		safe_unpackstr_xmalloc(&build_ptr->fed_params, &uint32_tmp,
		                       buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);
		safe_unpack16(&build_ptr->fs_dampening_factor, buffer);

		safe_unpack16(&build_ptr->get_env_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gres_plugins,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->group_time, buffer);
		safe_unpack16(&build_ptr->group_force, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gpu_freq_def,
		                       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr_xmalloc(&build_ptr->health_check_program,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);
		safe_unpackstr_xmalloc(&build_ptr->interactive_step_opts,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_freq,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_params,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_comp_host,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_loc,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_params,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_user,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_container_plugin,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_credential_private_key,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->
		                       job_credential_public_certificate,
		                       &uint32_tmp, buffer);
		if (slurm_unpack_list(&build_ptr->job_defaults_list,
		                      job_defaults_unpack, xfree_ptr,
		                      buffer,protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_submit_plugins,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->keep_alive_time, buffer);
		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr_xmalloc(&build_ptr->launch_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->launch_type,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->licenses,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->log_fmt, buffer);

		safe_unpack32(&build_ptr->max_array_sz, buffer);
		safe_unpack32(&build_ptr->max_dbd_msgs, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mail_domain,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mail_prog,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack64(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mcs_plugin,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mcs_plugin_params,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->min_job_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_default,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_params,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);

		if (unpack_config_plugin_params_list(
			    &build_ptr->node_features_conf,
			    protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->node_features_plugins,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->node_prefix,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->plugindir,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->plugstack,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->power_parameters,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->power_plugin,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr_xmalloc(&build_ptr->preempt_type,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->preempt_exempt_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prep_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prep_plugins,
		                       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_params, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_type, &uint32_tmp,
		                       buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_assoc, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_weight_tres,
		                       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr_xmalloc(&build_ptr->proctrack_type, &uint32_tmp,
		                       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->prolog_epilog_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog_slurmctld,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->prolog_flags, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits_except,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->reboot_program, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);

		safe_unpackstr_xmalloc(&build_ptr->requeue_exit,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->requeue_exit_hold,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->resume_fail_program,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resume_program,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_epilog, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_prolog, &uint32_tmp,
		                       buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr_xmalloc(&build_ptr->route_plugin,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->bcast_parameters,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_logfile,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr_xmalloc(&build_ptr->schedtype,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->scron_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->select_type,
		                       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->select_conf_key_pairs,
		                         protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurm_conf,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurm_user_name,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_user_name,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmctld_addr,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_logfile,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_params,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_pidfile,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_plugstack,
		                       &uint32_tmp, buffer);
		if (unpack_config_plugin_params_list(
			    &build_ptr->slurmctld_plugstack_conf,
			    protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_primary_off_prog,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_primary_on_prog,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmctld_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_logfile, &uint32_tmp,
		                       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_params, &uint32_tmp,
		                       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_pidfile, &uint32_tmp,
		                       buffer);
		safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmd_spooldir,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmd_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_epilog,
		                       &uint32_tmp, buffer);

		build_ptr->srun_port_range = xcalloc(2, sizeof(uint16_t));
		safe_unpack16(&build_ptr->srun_port_range[0], buffer);
		safe_unpack16(&build_ptr->srun_port_range[1], buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_prolog,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->state_save_location,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_nodes,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_parts,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_program,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->switch_type,
		                       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->task_epilog,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_prolog,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_plugin,
		                       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->task_plugin_param, buffer);
		safe_unpack16(&build_ptr->tcp_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->tmp_fs, &uint32_tmp,
		                       buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_param,
		                       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_plugin,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpackstr_xmalloc(&build_ptr->unkillable_program,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->version,
		                       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->x11_params,
		                       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *throw_away;
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr_xmalloc(
			&build_ptr->accounting_storage_backup_host,
			&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_host,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&throw_away,
				       &uint32_tmp, buffer);
		xfree(throw_away);
		safe_unpack32(&uint32_tmp, buffer);
		build_ptr->accounting_storage_port = uint32_tmp;
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_tres,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_user,
				       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->acct_gather_conf,
					 protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->acct_gather_energy_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_filesystem_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_interconnect_type,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_profile_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->authinfo,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authtype,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->bb_type,
				       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->cgroup_conf,
					 protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&build_ptr->cli_filter_plugins,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cluster_name,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->comm_params,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpack32(&build_ptr->conf_flags, buffer);
		safe_unpackstr_array(&build_ptr->control_addr,
				     &build_ptr->control_cnt, buffer);
		safe_unpackstr_array(&build_ptr->control_machine,
				     &build_ptr->control_cnt, buffer);
		safe_unpackstr_xmalloc(&build_ptr->core_spec_plugin,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->cpu_freq_def, buffer);
		safe_unpack32(&build_ptr->cpu_freq_govs, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cred_type, &uint32_tmp,
				       buffer);

		safe_unpack64(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack64(&build_ptr->debug_flags, buffer);
		safe_unpackstr_xmalloc(&build_ptr->dependency_params,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->eio_timeout, buffer);
		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog_slurmctld,
				       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->ext_sensors_conf,
					 protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->ext_sensors_type,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->ext_sensors_freq, buffer);

		safe_unpackstr_xmalloc(&build_ptr->fed_params, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);
		safe_unpack16(&build_ptr->fs_dampening_factor, buffer);

		safe_unpack16(&build_ptr->get_env_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gres_plugins,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->group_time, buffer);
		safe_unpack16(&build_ptr->group_force, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gpu_freq_def,
				       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr_xmalloc(&build_ptr->health_check_program,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_freq,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_params,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_comp_host,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_loc,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_params,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_container_plugin,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_credential_private_key,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->
				       job_credential_public_certificate,
				       &uint32_tmp, buffer);
		if (slurm_unpack_list(&build_ptr->job_defaults_list,
				      job_defaults_unpack, xfree_ptr,
				      buffer,protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_submit_plugins,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->keep_alive_time, buffer);
		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr_xmalloc(&build_ptr->launch_params,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->launch_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&throw_away, &uint32_tmp, buffer);
		xfree(throw_away); /* was layouts */
		safe_unpackstr_xmalloc(&build_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->log_fmt, buffer);

		safe_unpack32(&build_ptr->max_array_sz, buffer);
		safe_unpack32(&build_ptr->max_dbd_msgs, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mail_domain,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mail_prog,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack64(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mcs_plugin,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mcs_plugin_params,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->min_job_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_default,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_params,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&throw_away, &uint32_tmp, buffer);
		xfree(throw_away); /* was msg_aggr_params */
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);

		if (unpack_config_plugin_params_list(
			    &build_ptr->node_features_conf,
			    protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&build_ptr->node_features_plugins,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->node_prefix,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->plugindir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->plugstack,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->power_parameters,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->power_plugin,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr_xmalloc(&build_ptr->preempt_type,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->preempt_exempt_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prep_params,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prep_plugins,
				       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_params, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_type, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_assoc, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_weight_tres,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr_xmalloc(&build_ptr->proctrack_type, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->prolog_epilog_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog_slurmctld,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->prolog_flags, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits_except,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->reboot_program, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);

		safe_unpackstr_xmalloc(&build_ptr->requeue_exit,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->requeue_exit_hold,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->resume_fail_program,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resume_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_epilog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_prolog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr_xmalloc(&build_ptr->route_plugin,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&throw_away, &uint32_tmp, buffer);
		xfree(throw_away); /* was salloc_default_command */
		safe_unpackstr_xmalloc(&build_ptr->bcast_parameters,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_params,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_logfile,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr_xmalloc(&build_ptr->schedtype,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->select_type,
				       &uint32_tmp, buffer);

		if (unpack_key_pair_list(&build_ptr->select_conf_key_pairs,
					 protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurm_conf,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurm_user_name,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_user_name,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmctld_addr,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_logfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_params,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_pidfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_plugstack,
				       &uint32_tmp, buffer);
		if (unpack_config_plugin_params_list(
			    &build_ptr->slurmctld_plugstack_conf,
			    protocol_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_primary_off_prog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_primary_on_prog,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmctld_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_logfile, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_params, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_pidfile, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmd_spooldir,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmd_syslog_debug, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_epilog,
				       &uint32_tmp, buffer);

		build_ptr->srun_port_range = xcalloc(2, sizeof(uint16_t));
		safe_unpack16(&build_ptr->srun_port_range[0], buffer);
		safe_unpack16(&build_ptr->srun_port_range[1], buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_prolog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->state_save_location,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_parts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->switch_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->task_epilog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_prolog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_plugin,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->task_plugin_param, buffer);
		safe_unpack16(&build_ptr->tcp_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->tmp_fs, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_param,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_plugin,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpackstr_xmalloc(&build_ptr->unkillable_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->version,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->x11_params,
				       &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_ctl_conf(build_ptr);
	*build_buffer_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_sib_msg(sib_msg_t *sib_msg_ptr, buf_t *buffer, uint16_t protocol_version)
{
	xassert(sib_msg_ptr);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(sib_msg_ptr->cluster_id, buffer);
		pack16(sib_msg_ptr->data_type, buffer);
		pack16(sib_msg_ptr->data_version, buffer);
		pack64(sib_msg_ptr->fed_siblings, buffer);
		pack32(sib_msg_ptr->job_id, buffer);
		pack32(sib_msg_ptr->job_state, buffer);
		pack32(sib_msg_ptr->return_code, buffer);
		pack_time(sib_msg_ptr->start_time, buffer);
		packstr(sib_msg_ptr->resp_host, buffer);
		pack32(sib_msg_ptr->req_uid, buffer);
		pack16(sib_msg_ptr->sib_msg_type, buffer);
		packstr(sib_msg_ptr->submit_host, buffer);

		/* add already packed data_buffer to buffer */
		if (sib_msg_ptr->data_buffer &&
		    size_buf(sib_msg_ptr->data_buffer)) {
			buf_t *dbuf = sib_msg_ptr->data_buffer;
			uint32_t grow_size =
				get_buf_offset(dbuf) - sib_msg_ptr->data_offset;

			pack16(1, buffer);

			grow_buf(buffer, grow_size);
			memcpy(&buffer->head[get_buf_offset(buffer)],
			       &dbuf->head[sib_msg_ptr->data_offset],
			       grow_size);
			set_buf_offset(buffer,
				       get_buf_offset(buffer) + grow_size);
		} else {
			pack16(0, buffer);
		}
	}
}

static int
_unpack_sib_msg(sib_msg_t **sib_msg_buffer_ptr, buf_t *buffer,
		uint16_t protocol_version)
{
	sib_msg_t *sib_msg_ptr = NULL;
	slurm_msg_t tmp_msg;
	uint16_t tmp_uint16;
	uint32_t tmp_uint32;

	xassert(sib_msg_buffer_ptr);

	/* alloc memory for structure */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		sib_msg_ptr = xmalloc(sizeof(sib_msg_t));
		*sib_msg_buffer_ptr = sib_msg_ptr;

		/* load the data values */
		safe_unpack32(&sib_msg_ptr->cluster_id, buffer);
		safe_unpack16(&sib_msg_ptr->data_type, buffer);
		safe_unpack16(&sib_msg_ptr->data_version, buffer);
		safe_unpack64(&sib_msg_ptr->fed_siblings, buffer);
		safe_unpack32(&sib_msg_ptr->job_id, buffer);
		safe_unpack32(&sib_msg_ptr->job_state, buffer);
		safe_unpack32(&sib_msg_ptr->return_code, buffer);
		safe_unpack_time(&sib_msg_ptr->start_time, buffer);
		safe_unpackstr_xmalloc(&sib_msg_ptr->resp_host, &tmp_uint32,
				       buffer);
		safe_unpack32(&sib_msg_ptr->req_uid, buffer);
		safe_unpack16(&sib_msg_ptr->sib_msg_type, buffer);
		safe_unpackstr_xmalloc(&sib_msg_ptr->submit_host, &tmp_uint32,
				       buffer);

		safe_unpack16(&tmp_uint16, buffer);
		if (tmp_uint16) {
			slurm_msg_t_init(&tmp_msg);
			tmp_msg.msg_type         = sib_msg_ptr->data_type;
			tmp_msg.protocol_version = sib_msg_ptr->data_version;

			if (unpack_msg(&tmp_msg, buffer))
				goto unpack_error;

			sib_msg_ptr->data = tmp_msg.data;
			tmp_msg.data = NULL;
			slurm_free_msg_members(&tmp_msg);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_sib_msg(sib_msg_ptr);
	*sib_msg_buffer_ptr = NULL;
	return SLURM_ERROR;
}

/*
 * If this changes, then _pack_remote_dep_job() in fed_mgr.c probably
 * needs to change.
 */
static void _pack_dep_msg(dep_msg_t *dep_msg, buf_t *buffer,
			  uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(dep_msg->array_job_id, buffer);
		pack32(dep_msg->array_task_id, buffer);
		packstr(dep_msg->dependency, buffer);
		packbool(dep_msg->is_array, buffer);
		pack32(dep_msg->job_id, buffer);
		packstr(dep_msg->job_name, buffer);
		pack32(dep_msg->user_id, buffer);
	}
}

/*
 * If this changes, then _unpack_remote_dep_job() in fed_mgr.c probably
 * needs to change.
 */
static int _unpack_dep_msg(dep_msg_t **dep_msg_buffer_ptr, buf_t *buffer,
			   uint16_t protocol_version)
{
	dep_msg_t *dep_msg_ptr = NULL;
	uint32_t tmp_uint32;

	xassert(dep_msg_buffer_ptr);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		dep_msg_ptr = xmalloc(sizeof(*dep_msg_ptr));
		*dep_msg_buffer_ptr = dep_msg_ptr;

		safe_unpack32(&dep_msg_ptr->array_job_id, buffer);
		safe_unpack32(&dep_msg_ptr->array_task_id, buffer);
		safe_unpackstr_xmalloc(&dep_msg_ptr->dependency, &tmp_uint32,
				       buffer);
		safe_unpackbool(&dep_msg_ptr->is_array, buffer);
		safe_unpack32(&dep_msg_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&dep_msg_ptr->job_name, &tmp_uint32,
				       buffer);
		safe_unpack32(&dep_msg_ptr->user_id, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_dep_msg(dep_msg_ptr);
	*dep_msg_buffer_ptr = NULL;
	return SLURM_ERROR;
}

extern void pack_dep_list(List dep_list, buf_t *buffer, uint16_t protocol_version)
{
	uint32_t cnt;
	depend_spec_t *dep_ptr;
	ListIterator itr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

extern int unpack_dep_list(List *dep_list, buf_t *buffer,
			   uint16_t protocol_version)
{
	uint32_t cnt;
	depend_spec_t *dep_ptr;

	xassert(dep_list);

	*dep_list = NULL;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*dep_list);
	return SLURM_ERROR;
}

static void _pack_dep_update_origin_msg(dep_update_origin_msg_t *msg,
					buf_t *buffer, uint16_t protocol_version)
{

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_dep_list(msg->depend_list, buffer, protocol_version);
		pack32(msg->job_id, buffer);
	}
}

static int _unpack_dep_update_origin_msg(dep_update_origin_msg_t **msg_pptr,
					 buf_t *buffer, uint16_t protocol_version)
{
	dep_update_origin_msg_t *msg_ptr = NULL;

	xassert(msg_pptr);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		msg_ptr = xmalloc(sizeof *msg_ptr);
		*msg_pptr = msg_ptr;
		if (unpack_dep_list(&msg_ptr->depend_list,
				    buffer, protocol_version))
			goto unpack_error;
		safe_unpack32(&msg_ptr->job_id, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_dep_update_origin_msg(msg_ptr);
	*msg_pptr = NULL;
	return SLURM_ERROR;
}

/* _pack_job_desc_msg
 * packs a job_desc struct
 * IN job_desc_ptr - pointer to the job descriptor to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
static void _pack_job_desc_msg(job_desc_msg_t *job_desc_ptr, buf_t *buffer,
			       uint16_t protocol_version)
{
	if (job_desc_ptr->script_buf) {
		buf_t *buf = (buf_t *) job_desc_ptr->script_buf;
		job_desc_ptr->script = buf->head;
	}

	/* Set bitflags saying we did or didn't request the below */
	if (!job_desc_ptr->account)
		job_desc_ptr->bitflags |= USE_DEFAULT_ACCT;
	if (!job_desc_ptr->partition)
		job_desc_ptr->bitflags |= USE_DEFAULT_PART;
	if (!job_desc_ptr->qos)
		job_desc_ptr->bitflags |= USE_DEFAULT_QOS;
	if (!job_desc_ptr->wckey)
		job_desc_ptr->bitflags |= USE_DEFAULT_WCKEY;

	/* load the data values */
	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		pack32(job_desc_ptr->site_factor, buffer);
		packstr(job_desc_ptr->batch_features, buffer);
		packstr(job_desc_ptr->cluster_features, buffer);
		packstr(job_desc_ptr->clusters, buffer);
		pack16(job_desc_ptr->contiguous, buffer);
		packstr(job_desc_ptr->container, buffer);
		pack16(job_desc_ptr->core_spec, buffer);
		pack32(job_desc_ptr->task_dist, buffer);
		pack16(job_desc_ptr->kill_on_node_fail, buffer);
		packstr(job_desc_ptr->features, buffer);
		pack64(job_desc_ptr->fed_siblings_active, buffer);
		pack64(job_desc_ptr->fed_siblings_viable, buffer);
		pack32(job_desc_ptr->job_id, buffer);
		packstr(job_desc_ptr->job_id_str, buffer);
		packstr(job_desc_ptr->name, buffer);

		packstr(job_desc_ptr->alloc_node, buffer);
		pack32(job_desc_ptr->alloc_sid, buffer);
		packstr(job_desc_ptr->array_inx, buffer);
		packstr(job_desc_ptr->burst_buffer, buffer);
		pack16(job_desc_ptr->pn_min_cpus, buffer);
		pack64(job_desc_ptr->pn_min_memory, buffer);
		pack32(job_desc_ptr->pn_min_tmp_disk, buffer);
		pack8(job_desc_ptr->power_flags, buffer);

		pack32(job_desc_ptr->cpu_freq_min, buffer);
		pack32(job_desc_ptr->cpu_freq_max, buffer);
		pack32(job_desc_ptr->cpu_freq_gov, buffer);

		packstr(job_desc_ptr->partition, buffer);
		pack32(job_desc_ptr->priority, buffer);
		packstr(job_desc_ptr->dependency, buffer);
		packstr(job_desc_ptr->account, buffer);
		packstr(job_desc_ptr->admin_comment, buffer);
		packstr(job_desc_ptr->comment, buffer);
		pack32(job_desc_ptr->nice, buffer);
		pack32(job_desc_ptr->profile, buffer);
		packstr(job_desc_ptr->qos, buffer);
		packstr(job_desc_ptr->mcs_label, buffer);

		packstr(job_desc_ptr->origin_cluster, buffer);
		pack8(job_desc_ptr->open_mode,   buffer);
		pack8(job_desc_ptr->overcommit,  buffer);
		packstr(job_desc_ptr->acctg_freq, buffer);
		pack32(job_desc_ptr->num_tasks,  buffer);

		packstr(job_desc_ptr->req_context, buffer);
		packstr(job_desc_ptr->req_nodes, buffer);
		packstr(job_desc_ptr->exc_nodes, buffer);
		packstr_array(job_desc_ptr->environment,
			      job_desc_ptr->env_size, buffer);
		packstr_array(job_desc_ptr->spank_job_env,
			      job_desc_ptr->spank_job_env_size, buffer);
		packstr(job_desc_ptr->script, buffer);
		packstr_array(job_desc_ptr->argv, job_desc_ptr->argc, buffer);

		packstr(job_desc_ptr->std_err, buffer);
		packstr(job_desc_ptr->std_in, buffer);
		packstr(job_desc_ptr->std_out, buffer);
		packstr(job_desc_ptr->submit_line, buffer);
		packstr(job_desc_ptr->work_dir, buffer);

		pack16(job_desc_ptr->immediate, buffer);
		pack16(job_desc_ptr->reboot, buffer);
		pack16(job_desc_ptr->requeue, buffer);
		pack16(job_desc_ptr->shared, buffer);
		pack16(job_desc_ptr->cpus_per_task, buffer);
		pack16(job_desc_ptr->ntasks_per_node, buffer);
		pack16(job_desc_ptr->ntasks_per_board, buffer);
		pack16(job_desc_ptr->ntasks_per_socket, buffer);
		pack16(job_desc_ptr->ntasks_per_core, buffer);
		pack16(job_desc_ptr->ntasks_per_tres, buffer);

		pack16(job_desc_ptr->plane_size, buffer);
		pack16(job_desc_ptr->cpu_bind_type, buffer);
		pack16(job_desc_ptr->mem_bind_type, buffer);
		packstr(job_desc_ptr->cpu_bind, buffer);
		packstr(job_desc_ptr->mem_bind, buffer);

		pack32(job_desc_ptr->time_limit, buffer);
		pack32(job_desc_ptr->time_min, buffer);
		pack32(job_desc_ptr->min_cpus, buffer);
		pack32(job_desc_ptr->max_cpus, buffer);
		pack32(job_desc_ptr->min_nodes, buffer);
		pack32(job_desc_ptr->max_nodes, buffer);
		pack16(job_desc_ptr->boards_per_node, buffer);
		pack16(job_desc_ptr->sockets_per_board, buffer);
		pack16(job_desc_ptr->sockets_per_node, buffer);
		pack16(job_desc_ptr->cores_per_socket, buffer);
		pack16(job_desc_ptr->threads_per_core, buffer);
		pack32(job_desc_ptr->user_id, buffer);
		pack32(job_desc_ptr->group_id, buffer);

		pack16(job_desc_ptr->alloc_resp_port, buffer);
		packstr(job_desc_ptr->resp_host, buffer);
		pack16(job_desc_ptr->other_port, buffer);
		packstr(job_desc_ptr->network, buffer);
		pack_time(job_desc_ptr->begin_time, buffer);
		pack_time(job_desc_ptr->end_time, buffer);
		pack_time(job_desc_ptr->deadline, buffer);

		packstr(job_desc_ptr->licenses, buffer);
		pack16(job_desc_ptr->mail_type, buffer);
		packstr(job_desc_ptr->mail_user, buffer);
		packstr(job_desc_ptr->reservation, buffer);
		pack16(job_desc_ptr->restart_cnt, buffer);
		pack16(job_desc_ptr->warn_flags, buffer);
		pack16(job_desc_ptr->warn_signal, buffer);
		pack16(job_desc_ptr->warn_time, buffer);
		packstr(job_desc_ptr->wckey, buffer);
		pack32(job_desc_ptr->req_switch, buffer);
		pack32(job_desc_ptr->wait4switch, buffer);

		if (job_desc_ptr->select_jobinfo) {
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
		} else {
			dynamic_plugin_data_t *select_jobinfo;
			select_jobinfo = select_g_select_jobinfo_alloc();
			select_g_select_jobinfo_pack(select_jobinfo, buffer,
						     protocol_version);
			select_g_select_jobinfo_free(select_jobinfo);
		}
		pack16(job_desc_ptr->wait_all_nodes, buffer);
		pack64(job_desc_ptr->bitflags, buffer);
		pack32(job_desc_ptr->delay_boot, buffer);
		packstr(job_desc_ptr->extra, buffer);
		pack16(job_desc_ptr->x11, buffer);
		packstr(job_desc_ptr->x11_magic_cookie, buffer);
		packstr(job_desc_ptr->x11_target, buffer);
		pack16(job_desc_ptr->x11_target_port, buffer);

		packstr(job_desc_ptr->cpus_per_tres, buffer);
		packstr(job_desc_ptr->mem_per_tres, buffer);
		packstr(job_desc_ptr->tres_bind, buffer);
		packstr(job_desc_ptr->tres_freq, buffer);
		packstr(job_desc_ptr->tres_per_job, buffer);
		packstr(job_desc_ptr->tres_per_node, buffer);
		packstr(job_desc_ptr->tres_per_socket, buffer);
		packstr(job_desc_ptr->tres_per_task, buffer);
		pack_cron_entry(job_desc_ptr->crontab_entry, protocol_version,
				buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(job_desc_ptr->site_factor, buffer);
		packstr(job_desc_ptr->batch_features, buffer);
		packstr(job_desc_ptr->cluster_features, buffer);
		packstr(job_desc_ptr->clusters, buffer);
		pack16(job_desc_ptr->contiguous, buffer);
		pack16(job_desc_ptr->core_spec, buffer);
		pack32(job_desc_ptr->task_dist, buffer);
		pack16(job_desc_ptr->kill_on_node_fail, buffer);
		packstr(job_desc_ptr->features, buffer);
		pack64(job_desc_ptr->fed_siblings_active, buffer);
		pack64(job_desc_ptr->fed_siblings_viable, buffer);
		pack32(job_desc_ptr->job_id, buffer);
		packstr(job_desc_ptr->job_id_str, buffer);
		packstr(job_desc_ptr->name, buffer);

		packstr(job_desc_ptr->alloc_node, buffer);
		pack32(job_desc_ptr->alloc_sid, buffer);
		packstr(job_desc_ptr->array_inx, buffer);
		packstr(job_desc_ptr->burst_buffer, buffer);
		pack16(job_desc_ptr->pn_min_cpus, buffer);
		pack64(job_desc_ptr->pn_min_memory, buffer);
		pack32(job_desc_ptr->pn_min_tmp_disk, buffer);
		pack8(job_desc_ptr->power_flags, buffer);

		pack32(job_desc_ptr->cpu_freq_min, buffer);
		pack32(job_desc_ptr->cpu_freq_max, buffer);
		pack32(job_desc_ptr->cpu_freq_gov, buffer);

		packstr(job_desc_ptr->partition, buffer);
		pack32(job_desc_ptr->priority, buffer);
		packstr(job_desc_ptr->dependency, buffer);
		packstr(job_desc_ptr->account, buffer);
		packstr(job_desc_ptr->admin_comment, buffer);
		packstr(job_desc_ptr->comment, buffer);
		pack32(job_desc_ptr->nice, buffer);
		pack32(job_desc_ptr->profile, buffer);
		packstr(job_desc_ptr->qos, buffer);
		packstr(job_desc_ptr->mcs_label, buffer);

		packstr(job_desc_ptr->origin_cluster, buffer);
		pack8(job_desc_ptr->open_mode,   buffer);
		pack8(job_desc_ptr->overcommit,  buffer);
		packstr(job_desc_ptr->acctg_freq, buffer);
		pack32(job_desc_ptr->num_tasks,  buffer);

		packstr(job_desc_ptr->req_nodes, buffer);
		packstr(job_desc_ptr->exc_nodes, buffer);
		packstr_array(job_desc_ptr->environment,
			      job_desc_ptr->env_size, buffer);
		packstr_array(job_desc_ptr->spank_job_env,
			      job_desc_ptr->spank_job_env_size, buffer);
		packstr(job_desc_ptr->script, buffer);
		packstr_array(job_desc_ptr->argv, job_desc_ptr->argc, buffer);

		packstr(job_desc_ptr->std_err, buffer);
		packstr(job_desc_ptr->std_in, buffer);
		packstr(job_desc_ptr->std_out, buffer);
		packstr(job_desc_ptr->work_dir, buffer);

		pack16(job_desc_ptr->immediate, buffer);
		pack16(job_desc_ptr->reboot, buffer);
		pack16(job_desc_ptr->requeue, buffer);
		pack16(job_desc_ptr->shared, buffer);
		pack16(job_desc_ptr->cpus_per_task, buffer);
		pack16(job_desc_ptr->ntasks_per_node, buffer);
		pack16(job_desc_ptr->ntasks_per_board, buffer);
		pack16(job_desc_ptr->ntasks_per_socket, buffer);
		pack16(job_desc_ptr->ntasks_per_core, buffer);
		pack16(job_desc_ptr->ntasks_per_tres, buffer);

		pack16(job_desc_ptr->plane_size, buffer);
		pack16(job_desc_ptr->cpu_bind_type, buffer);
		pack16(job_desc_ptr->mem_bind_type, buffer);
		packstr(job_desc_ptr->cpu_bind, buffer);
		packstr(job_desc_ptr->mem_bind, buffer);

		pack32(job_desc_ptr->time_limit, buffer);
		pack32(job_desc_ptr->time_min, buffer);
		pack32(job_desc_ptr->min_cpus, buffer);
		pack32(job_desc_ptr->max_cpus, buffer);
		pack32(job_desc_ptr->min_nodes, buffer);
		pack32(job_desc_ptr->max_nodes, buffer);
		pack16(job_desc_ptr->boards_per_node, buffer);
		pack16(job_desc_ptr->sockets_per_board, buffer);
		pack16(job_desc_ptr->sockets_per_node, buffer);
		pack16(job_desc_ptr->cores_per_socket, buffer);
		pack16(job_desc_ptr->threads_per_core, buffer);
		pack32(job_desc_ptr->user_id, buffer);
		pack32(job_desc_ptr->group_id, buffer);

		pack16(job_desc_ptr->alloc_resp_port, buffer);
		packstr(job_desc_ptr->resp_host, buffer);
		pack16(job_desc_ptr->other_port, buffer);
		packstr(job_desc_ptr->network, buffer);
		pack_time(job_desc_ptr->begin_time, buffer);
		pack_time(job_desc_ptr->end_time, buffer);
		pack_time(job_desc_ptr->deadline, buffer);

		packstr(job_desc_ptr->licenses, buffer);
		pack16(job_desc_ptr->mail_type, buffer);
		packstr(job_desc_ptr->mail_user, buffer);
		packstr(job_desc_ptr->reservation, buffer);
		pack16(job_desc_ptr->restart_cnt, buffer);
		pack16(job_desc_ptr->warn_flags, buffer);
		pack16(job_desc_ptr->warn_signal, buffer);
		pack16(job_desc_ptr->warn_time, buffer);
		packstr(job_desc_ptr->wckey, buffer);
		pack32(job_desc_ptr->req_switch, buffer);
		pack32(job_desc_ptr->wait4switch, buffer);

		if (job_desc_ptr->select_jobinfo) {
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
		} else {
			dynamic_plugin_data_t *select_jobinfo;
			select_jobinfo = select_g_select_jobinfo_alloc();
			select_g_select_jobinfo_pack(select_jobinfo, buffer,
						     protocol_version);
			select_g_select_jobinfo_free(select_jobinfo);
		}
		pack16(job_desc_ptr->wait_all_nodes, buffer);
		pack32((uint32_t)job_desc_ptr->bitflags, buffer);
		pack32(job_desc_ptr->delay_boot, buffer);
		packstr(job_desc_ptr->extra, buffer);
		pack16(job_desc_ptr->x11, buffer);
		packstr(job_desc_ptr->x11_magic_cookie, buffer);
		packstr(job_desc_ptr->x11_target, buffer);
		pack16(job_desc_ptr->x11_target_port, buffer);

		packstr(job_desc_ptr->cpus_per_tres, buffer);
		packstr(job_desc_ptr->mem_per_tres, buffer);
		packstr(job_desc_ptr->tres_bind, buffer);
		packstr(job_desc_ptr->tres_freq, buffer);
		packstr(job_desc_ptr->tres_per_job, buffer);
		packstr(job_desc_ptr->tres_per_node, buffer);
		packstr(job_desc_ptr->tres_per_socket, buffer);
		packstr(job_desc_ptr->tres_per_task, buffer);
		pack_cron_entry(job_desc_ptr->crontab_entry, protocol_version,
				buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(job_desc_ptr->site_factor, buffer);
		packstr(job_desc_ptr->batch_features, buffer);
		packstr(job_desc_ptr->cluster_features, buffer);
		packstr(job_desc_ptr->clusters, buffer);
		pack16(job_desc_ptr->contiguous, buffer);
		pack16(job_desc_ptr->core_spec, buffer);
		pack32(job_desc_ptr->task_dist, buffer);
		pack16(job_desc_ptr->kill_on_node_fail, buffer);
		packstr(job_desc_ptr->features, buffer);
		pack64(job_desc_ptr->fed_siblings_active, buffer);
		pack64(job_desc_ptr->fed_siblings_viable, buffer);
		pack32(job_desc_ptr->job_id, buffer);
		packstr(job_desc_ptr->job_id_str, buffer);
		packstr(job_desc_ptr->name, buffer);

		packstr(job_desc_ptr->alloc_node, buffer);
		pack32(job_desc_ptr->alloc_sid, buffer);
		packstr(job_desc_ptr->array_inx, buffer);
		packstr(job_desc_ptr->burst_buffer, buffer);
		pack16(job_desc_ptr->pn_min_cpus, buffer);
		pack64(job_desc_ptr->pn_min_memory, buffer);
		pack32(job_desc_ptr->pn_min_tmp_disk, buffer);
		pack8(job_desc_ptr->power_flags, buffer);

		pack32(job_desc_ptr->cpu_freq_min, buffer);
		pack32(job_desc_ptr->cpu_freq_max, buffer);
		pack32(job_desc_ptr->cpu_freq_gov, buffer);

		packstr(job_desc_ptr->partition, buffer);
		pack32(job_desc_ptr->priority, buffer);
		packstr(job_desc_ptr->dependency, buffer);
		packstr(job_desc_ptr->account, buffer);
		packstr(job_desc_ptr->admin_comment, buffer);
		packstr(job_desc_ptr->comment, buffer);
		pack32(job_desc_ptr->nice, buffer);
		pack32(job_desc_ptr->profile, buffer);
		packstr(job_desc_ptr->qos, buffer);
		packstr(job_desc_ptr->mcs_label, buffer);

		packstr(job_desc_ptr->origin_cluster, buffer);
		pack8(job_desc_ptr->open_mode,   buffer);
		pack8(job_desc_ptr->overcommit,  buffer);
		packstr(job_desc_ptr->acctg_freq, buffer);
		pack32(job_desc_ptr->num_tasks,  buffer);
		pack16(0, buffer); /* was ckpt_interval */

		packstr(job_desc_ptr->req_nodes, buffer);
		packstr(job_desc_ptr->exc_nodes, buffer);
		packstr_array(job_desc_ptr->environment,
			      job_desc_ptr->env_size, buffer);
		packstr_array(job_desc_ptr->spank_job_env,
			      job_desc_ptr->spank_job_env_size, buffer);
		packstr(job_desc_ptr->script, buffer);
		packstr_array(job_desc_ptr->argv, job_desc_ptr->argc, buffer);

		packstr(job_desc_ptr->std_err, buffer);
		packstr(job_desc_ptr->std_in, buffer);
		packstr(job_desc_ptr->std_out, buffer);
		packstr(job_desc_ptr->work_dir, buffer);
		packnull(buffer); /* was ckpt_dir */

		pack16(job_desc_ptr->immediate, buffer);
		pack16(job_desc_ptr->reboot, buffer);
		pack16(job_desc_ptr->requeue, buffer);
		pack16(job_desc_ptr->shared, buffer);
		pack16(job_desc_ptr->cpus_per_task, buffer);
		pack16(job_desc_ptr->ntasks_per_node, buffer);
		pack16(job_desc_ptr->ntasks_per_board, buffer);
		pack16(job_desc_ptr->ntasks_per_socket, buffer);
		pack16(job_desc_ptr->ntasks_per_core, buffer);

		pack16(job_desc_ptr->plane_size, buffer);
		pack16(job_desc_ptr->cpu_bind_type, buffer);
		pack16(job_desc_ptr->mem_bind_type, buffer);
		packstr(job_desc_ptr->cpu_bind, buffer);
		packstr(job_desc_ptr->mem_bind, buffer);

		pack32(job_desc_ptr->time_limit, buffer);
		pack32(job_desc_ptr->time_min, buffer);
		pack32(job_desc_ptr->min_cpus, buffer);
		pack32(job_desc_ptr->max_cpus, buffer);
		pack32(job_desc_ptr->min_nodes, buffer);
		pack32(job_desc_ptr->max_nodes, buffer);
		pack16(job_desc_ptr->boards_per_node, buffer);
		pack16(job_desc_ptr->sockets_per_board, buffer);
		pack16(job_desc_ptr->sockets_per_node, buffer);
		pack16(job_desc_ptr->cores_per_socket, buffer);
		pack16(job_desc_ptr->threads_per_core, buffer);
		pack32(job_desc_ptr->user_id, buffer);
		pack32(job_desc_ptr->group_id, buffer);

		pack16(job_desc_ptr->alloc_resp_port, buffer);
		packstr(job_desc_ptr->resp_host, buffer);
		pack16(job_desc_ptr->other_port, buffer);
		packstr(job_desc_ptr->network, buffer);
		pack_time(job_desc_ptr->begin_time, buffer);
		pack_time(job_desc_ptr->end_time, buffer);
		pack_time(job_desc_ptr->deadline, buffer);

		packstr(job_desc_ptr->licenses, buffer);
		pack16(job_desc_ptr->mail_type, buffer);
		packstr(job_desc_ptr->mail_user, buffer);
		packstr(job_desc_ptr->reservation, buffer);
		pack16(job_desc_ptr->restart_cnt, buffer);
		pack16(job_desc_ptr->warn_flags, buffer);
		pack16(job_desc_ptr->warn_signal, buffer);
		pack16(job_desc_ptr->warn_time, buffer);
		packstr(job_desc_ptr->wckey, buffer);
		pack32(job_desc_ptr->req_switch, buffer);
		pack32(job_desc_ptr->wait4switch, buffer);

		if (job_desc_ptr->select_jobinfo) {
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
		} else {
			dynamic_plugin_data_t *select_jobinfo;
			select_jobinfo = select_g_select_jobinfo_alloc();
			select_g_select_jobinfo_pack(select_jobinfo, buffer,
						     protocol_version);
			select_g_select_jobinfo_free(select_jobinfo);
		}
		pack16(job_desc_ptr->wait_all_nodes, buffer);
		pack32((uint32_t)job_desc_ptr->bitflags, buffer);
		pack32(job_desc_ptr->delay_boot, buffer);
		packstr(job_desc_ptr->extra, buffer);
		pack16(job_desc_ptr->x11, buffer);
		packstr(job_desc_ptr->x11_magic_cookie, buffer);
		packstr(job_desc_ptr->x11_target, buffer);
		pack16(job_desc_ptr->x11_target_port, buffer);

		packstr(job_desc_ptr->cpus_per_tres, buffer);
		packstr(job_desc_ptr->mem_per_tres, buffer);
		packstr(job_desc_ptr->tres_bind, buffer);
		packstr(job_desc_ptr->tres_freq, buffer);
		packstr(job_desc_ptr->tres_per_job, buffer);
		packstr(job_desc_ptr->tres_per_node, buffer);
		packstr(job_desc_ptr->tres_per_socket, buffer);
		packstr(job_desc_ptr->tres_per_task, buffer);
	}

	if (job_desc_ptr->script_buf)
		job_desc_ptr->script = NULL;
}

/* _unpack_job_desc_msg
 * unpacks a job_desc struct
 * OUT job_desc_buffer_ptr - place to put pointer to allocated job desc struct
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 */
static int
_unpack_job_desc_msg(job_desc_msg_t ** job_desc_buffer_ptr, buf_t *buffer,
		     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_desc_msg_t *job_desc_ptr = NULL;
	char *temp_str;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		job_desc_ptr = xmalloc(sizeof(job_desc_msg_t));
		*job_desc_buffer_ptr = job_desc_ptr;

		/* load the data values */
		safe_unpack32(&job_desc_ptr->site_factor, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->batch_features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cluster_features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->clusters,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->contiguous, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->container, &uint32_tmp,
				       buffer);
		safe_unpack16(&job_desc_ptr->core_spec, buffer);
		safe_unpack32(&job_desc_ptr->task_dist, buffer);
		safe_unpack16(&job_desc_ptr->kill_on_node_fail, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpack64(&job_desc_ptr->fed_siblings_active, buffer);
		safe_unpack64(&job_desc_ptr->fed_siblings_viable, buffer);
		safe_unpack32(&job_desc_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->job_id_str,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->name,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->alloc_node,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->alloc_sid, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->array_inx,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->burst_buffer,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->pn_min_cpus, buffer);
		safe_unpack64(&job_desc_ptr->pn_min_memory, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_tmp_disk, buffer);
		safe_unpack8(&job_desc_ptr->power_flags,   buffer);

		safe_unpack32(&job_desc_ptr->cpu_freq_min, buffer);
		safe_unpack32(&job_desc_ptr->cpu_freq_max, buffer);
		safe_unpack32(&job_desc_ptr->cpu_freq_gov, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->priority, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->dependency,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->account,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->admin_comment,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->comment,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->nice, buffer);
		safe_unpack32(&job_desc_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mcs_label, &uint32_tmp,
				       buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->origin_cluster,
				       &uint32_tmp, buffer);
		safe_unpack8(&job_desc_ptr->open_mode,   buffer);
		safe_unpack8(&job_desc_ptr->overcommit,  buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->num_tasks,  buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->req_context,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->req_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->exc_nodes,
				       &uint32_tmp, buffer);

		safe_unpackstr_array(&job_desc_ptr->environment,
				     &job_desc_ptr->env_size, buffer);
		if (envcount(job_desc_ptr->environment)
		    != job_desc_ptr->env_size)
			goto unpack_error;
		safe_unpackstr_array(&job_desc_ptr->spank_job_env,
				     &job_desc_ptr->spank_job_env_size,
				     buffer);
		if (envcount(job_desc_ptr->spank_job_env)
		    != job_desc_ptr->spank_job_env_size)
			goto unpack_error;
		safe_unpackstr_xmalloc(&job_desc_ptr->script,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->argv,
				     &job_desc_ptr->argc, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->std_err,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_in,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_out,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->submit_line,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->work_dir,
				       &uint32_tmp, buffer);

		safe_unpack16(&job_desc_ptr->immediate, buffer);
		safe_unpack16(&job_desc_ptr->reboot, buffer);
		safe_unpack16(&job_desc_ptr->requeue, buffer);
		safe_unpack16(&job_desc_ptr->shared, buffer);
		safe_unpack16(&job_desc_ptr->cpus_per_task, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_node, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_board, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_core, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_tres, buffer);

		safe_unpack16(&job_desc_ptr->plane_size, buffer);
		safe_unpack16(&job_desc_ptr->cpu_bind_type, buffer);
		safe_unpack16(&job_desc_ptr->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cpu_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mem_bind,
				       &uint32_tmp, buffer);

		safe_unpack32(&job_desc_ptr->time_limit, buffer);
		safe_unpack32(&job_desc_ptr->time_min, buffer);
		safe_unpack32(&job_desc_ptr->min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->max_cpus, buffer);
		safe_unpack32(&job_desc_ptr->min_nodes, buffer);
		safe_unpack32(&job_desc_ptr->max_nodes, buffer);
		safe_unpack16(&job_desc_ptr->boards_per_node, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_board, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_node, buffer);
		safe_unpack16(&job_desc_ptr->cores_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->threads_per_core, buffer);
		safe_unpack32(&job_desc_ptr->user_id, buffer);
		safe_unpack32(&job_desc_ptr->group_id, buffer);

		safe_unpack16(&job_desc_ptr->alloc_resp_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->resp_host, &uint32_tmp,
				       buffer);
		safe_unpack16(&job_desc_ptr->other_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->network,
				       &uint32_tmp, buffer);
		safe_unpack_time(&job_desc_ptr->begin_time, buffer);
		safe_unpack_time(&job_desc_ptr->end_time, buffer);
		safe_unpack_time(&job_desc_ptr->deadline, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->mail_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mail_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->reservation,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->restart_cnt, buffer);
		safe_unpack16(&job_desc_ptr->warn_flags, buffer);
		safe_unpack16(&job_desc_ptr->warn_signal, buffer);
		safe_unpack16(&job_desc_ptr->warn_time, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->wckey,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->req_switch, buffer);
		safe_unpack32(&job_desc_ptr->wait4switch, buffer);

		if (select_g_select_jobinfo_unpack(
			    &job_desc_ptr->select_jobinfo,
			    buffer, protocol_version))
			goto unpack_error;

		safe_unpack16(&job_desc_ptr->wait_all_nodes, buffer);
		safe_unpack64(&job_desc_ptr->bitflags, buffer);
		safe_unpack32(&job_desc_ptr->delay_boot, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->extra, &uint32_tmp,
				       buffer);
		safe_unpack16(&job_desc_ptr->x11, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->x11_magic_cookie,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->x11_target,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->x11_target_port, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->cpus_per_tres,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mem_per_tres,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_freq,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_per_job,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_per_node,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_per_socket,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_per_task,
				       &uint32_tmp, buffer);
		if (unpack_cron_entry(&job_desc_ptr->crontab_entry,
				      protocol_version, buffer))
			goto unpack_error;
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		job_desc_ptr = xmalloc(sizeof(job_desc_msg_t));
		*job_desc_buffer_ptr = job_desc_ptr;

		/* load the data values */
		safe_unpack32(&job_desc_ptr->site_factor, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->batch_features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cluster_features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->clusters,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->contiguous, buffer);
		safe_unpack16(&job_desc_ptr->core_spec, buffer);
		safe_unpack32(&job_desc_ptr->task_dist, buffer);
		safe_unpack16(&job_desc_ptr->kill_on_node_fail, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpack64(&job_desc_ptr->fed_siblings_active, buffer);
		safe_unpack64(&job_desc_ptr->fed_siblings_viable, buffer);
		safe_unpack32(&job_desc_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->job_id_str,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->name,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->alloc_node,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->alloc_sid, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->array_inx,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->burst_buffer,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->pn_min_cpus, buffer);
		safe_unpack64(&job_desc_ptr->pn_min_memory, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_tmp_disk, buffer);
		safe_unpack8(&job_desc_ptr->power_flags,   buffer);

		safe_unpack32(&job_desc_ptr->cpu_freq_min, buffer);
		safe_unpack32(&job_desc_ptr->cpu_freq_max, buffer);
		safe_unpack32(&job_desc_ptr->cpu_freq_gov, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->priority, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->dependency,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->account,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->admin_comment,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->comment,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->nice, buffer);
		safe_unpack32(&job_desc_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mcs_label, &uint32_tmp,
				       buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->origin_cluster,
				       &uint32_tmp, buffer);
		safe_unpack8(&job_desc_ptr->open_mode,   buffer);
		safe_unpack8(&job_desc_ptr->overcommit,  buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->num_tasks,  buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->req_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->environment,
				     &job_desc_ptr->env_size, buffer);
		if (envcount(job_desc_ptr->environment)
		    != job_desc_ptr->env_size)
			goto unpack_error;
		safe_unpackstr_array(&job_desc_ptr->spank_job_env,
				     &job_desc_ptr->spank_job_env_size,
				     buffer);
		if (envcount(job_desc_ptr->spank_job_env)
		    != job_desc_ptr->spank_job_env_size)
			goto unpack_error;
		safe_unpackstr_xmalloc(&job_desc_ptr->script,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->argv,
				     &job_desc_ptr->argc, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->std_err,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_in,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_out,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->work_dir,
				       &uint32_tmp, buffer);

		safe_unpack16(&job_desc_ptr->immediate, buffer);
		safe_unpack16(&job_desc_ptr->reboot, buffer);
		safe_unpack16(&job_desc_ptr->requeue, buffer);
		safe_unpack16(&job_desc_ptr->shared, buffer);
		safe_unpack16(&job_desc_ptr->cpus_per_task, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_node, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_board, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_core, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_tres, buffer);

		safe_unpack16(&job_desc_ptr->plane_size, buffer);
		safe_unpack16(&job_desc_ptr->cpu_bind_type, buffer);
		safe_unpack16(&job_desc_ptr->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cpu_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mem_bind,
				       &uint32_tmp, buffer);

		safe_unpack32(&job_desc_ptr->time_limit, buffer);
		safe_unpack32(&job_desc_ptr->time_min, buffer);
		safe_unpack32(&job_desc_ptr->min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->max_cpus, buffer);
		safe_unpack32(&job_desc_ptr->min_nodes, buffer);
		safe_unpack32(&job_desc_ptr->max_nodes, buffer);
		safe_unpack16(&job_desc_ptr->boards_per_node, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_board, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_node, buffer);
		safe_unpack16(&job_desc_ptr->cores_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->threads_per_core, buffer);
		safe_unpack32(&job_desc_ptr->user_id, buffer);
		safe_unpack32(&job_desc_ptr->group_id, buffer);

		safe_unpack16(&job_desc_ptr->alloc_resp_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->resp_host, &uint32_tmp,
				       buffer);
		safe_unpack16(&job_desc_ptr->other_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->network,
				       &uint32_tmp, buffer);
		safe_unpack_time(&job_desc_ptr->begin_time, buffer);
		safe_unpack_time(&job_desc_ptr->end_time, buffer);
		safe_unpack_time(&job_desc_ptr->deadline, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->mail_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mail_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->reservation,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->restart_cnt, buffer);
		safe_unpack16(&job_desc_ptr->warn_flags, buffer);
		safe_unpack16(&job_desc_ptr->warn_signal, buffer);
		safe_unpack16(&job_desc_ptr->warn_time, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->wckey,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->req_switch, buffer);
		safe_unpack32(&job_desc_ptr->wait4switch, buffer);

		if (select_g_select_jobinfo_unpack(
			    &job_desc_ptr->select_jobinfo,
			    buffer, protocol_version))
			goto unpack_error;

		safe_unpack16(&job_desc_ptr->wait_all_nodes, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_desc_ptr->bitflags = uint32_tmp;
		safe_unpack32(&job_desc_ptr->delay_boot, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->extra, &uint32_tmp,
				       buffer);
		safe_unpack16(&job_desc_ptr->x11, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->x11_magic_cookie,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->x11_target,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->x11_target_port, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->cpus_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->mem_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_freq,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->tres_per_job = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->tres_per_node = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->tres_per_socket =
			gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->tres_per_task = gres_prepend_tres_type(temp_str);
		xfree(temp_str);

		if (unpack_cron_entry(&job_desc_ptr->crontab_entry,
				      protocol_version, buffer))
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *temp_str;
		uint16_t uint16_tmp;
		job_desc_ptr = xmalloc(sizeof(job_desc_msg_t));
		*job_desc_buffer_ptr = job_desc_ptr;

		/* load the data values */
		safe_unpack32(&job_desc_ptr->site_factor, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->batch_features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cluster_features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->clusters,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->contiguous, buffer);
		safe_unpack16(&job_desc_ptr->core_spec, buffer);
		safe_unpack32(&job_desc_ptr->task_dist, buffer);
		safe_unpack16(&job_desc_ptr->kill_on_node_fail, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpack64(&job_desc_ptr->fed_siblings_active, buffer);
		safe_unpack64(&job_desc_ptr->fed_siblings_viable, buffer);
		safe_unpack32(&job_desc_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->job_id_str,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->name,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->alloc_node,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->alloc_sid, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->array_inx,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->burst_buffer,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->pn_min_cpus, buffer);
		safe_unpack64(&job_desc_ptr->pn_min_memory, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_tmp_disk, buffer);
		safe_unpack8(&job_desc_ptr->power_flags,   buffer);

		safe_unpack32(&job_desc_ptr->cpu_freq_min, buffer);
		safe_unpack32(&job_desc_ptr->cpu_freq_max, buffer);
		safe_unpack32(&job_desc_ptr->cpu_freq_gov, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->priority, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->dependency,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->account,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->admin_comment,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->comment,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->nice, buffer);
		safe_unpack32(&job_desc_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mcs_label, &uint32_tmp,
				       buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->origin_cluster,
				       &uint32_tmp, buffer);
		safe_unpack8(&job_desc_ptr->open_mode,   buffer);
		safe_unpack8(&job_desc_ptr->overcommit,  buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->num_tasks,  buffer);
		safe_unpack16(&uint16_tmp, buffer); /* was ckpt_interval */

		safe_unpackstr_xmalloc(&job_desc_ptr->req_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->environment,
				     &job_desc_ptr->env_size, buffer);
		if (envcount(job_desc_ptr->environment)
		    != job_desc_ptr->env_size)
			goto unpack_error;
		safe_unpackstr_array(&job_desc_ptr->spank_job_env,
				     &job_desc_ptr->spank_job_env_size,
				     buffer);
		if (envcount(job_desc_ptr->spank_job_env)
		    != job_desc_ptr->spank_job_env_size)
			goto unpack_error;
		safe_unpackstr_xmalloc(&job_desc_ptr->script,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->argv,
				     &job_desc_ptr->argc, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->std_err,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_in,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_out,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->work_dir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was ckpt_dir */

		safe_unpack16(&job_desc_ptr->immediate, buffer);
		safe_unpack16(&job_desc_ptr->reboot, buffer);
		safe_unpack16(&job_desc_ptr->requeue, buffer);
		safe_unpack16(&job_desc_ptr->shared, buffer);
		safe_unpack16(&job_desc_ptr->cpus_per_task, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_node, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_board, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_core, buffer);
		job_desc_ptr->ntasks_per_tres = NO_VAL16;

		safe_unpack16(&job_desc_ptr->plane_size, buffer);
		safe_unpack16(&job_desc_ptr->cpu_bind_type, buffer);
		safe_unpack16(&job_desc_ptr->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cpu_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mem_bind,
				       &uint32_tmp, buffer);

		safe_unpack32(&job_desc_ptr->time_limit, buffer);
		safe_unpack32(&job_desc_ptr->time_min, buffer);
		safe_unpack32(&job_desc_ptr->min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->max_cpus, buffer);
		safe_unpack32(&job_desc_ptr->min_nodes, buffer);
		safe_unpack32(&job_desc_ptr->max_nodes, buffer);
		safe_unpack16(&job_desc_ptr->boards_per_node, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_board, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_node, buffer);
		safe_unpack16(&job_desc_ptr->cores_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->threads_per_core, buffer);
		safe_unpack32(&job_desc_ptr->user_id, buffer);
		safe_unpack32(&job_desc_ptr->group_id, buffer);

		safe_unpack16(&job_desc_ptr->alloc_resp_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->resp_host, &uint32_tmp,
				       buffer);
		safe_unpack16(&job_desc_ptr->other_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->network,
				       &uint32_tmp, buffer);
		safe_unpack_time(&job_desc_ptr->begin_time, buffer);
		safe_unpack_time(&job_desc_ptr->end_time, buffer);
		safe_unpack_time(&job_desc_ptr->deadline, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->mail_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mail_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->reservation,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->restart_cnt, buffer);
		safe_unpack16(&job_desc_ptr->warn_flags, buffer);
		safe_unpack16(&job_desc_ptr->warn_signal, buffer);
		safe_unpack16(&job_desc_ptr->warn_time, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->wckey,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->req_switch, buffer);
		safe_unpack32(&job_desc_ptr->wait4switch, buffer);

		if (select_g_select_jobinfo_unpack(
			    &job_desc_ptr->select_jobinfo,
			    buffer, protocol_version))
			goto unpack_error;

		safe_unpack16(&job_desc_ptr->wait_all_nodes, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_desc_ptr->bitflags = uint32_tmp;
		safe_unpack32(&job_desc_ptr->delay_boot, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->extra, &uint32_tmp,
				       buffer);
		safe_unpack16(&job_desc_ptr->x11, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->x11_magic_cookie,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->x11_target,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->x11_target_port, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->cpus_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->mem_per_tres = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->tres_freq,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->tres_per_job = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->tres_per_node = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->tres_per_socket =
			gres_prepend_tres_type(temp_str);
		xfree(temp_str);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		job_desc_ptr->tres_per_task = gres_prepend_tres_type(temp_str);
		xfree(temp_str);
	} else {
		error("_unpack_job_desc_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_desc_msg(job_desc_ptr);
	*job_desc_buffer_ptr = NULL;
	return SLURM_ERROR;
}

/* _pack_job_desc_list_msg
 * packs a list of job_desc structs
 * IN job_req_list - pointer to the job descriptor to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
static void
_pack_job_desc_list_msg(List job_req_list, buf_t *buffer,
			uint16_t protocol_version)
{
	job_desc_msg_t *req;
	ListIterator iter;
	uint16_t cnt = 0;

	if (job_req_list)
		cnt = list_count(job_req_list);
	pack16(cnt, buffer);
	if (cnt == 0)
		return;

	iter = list_iterator_create(job_req_list);
	while ((req = (job_desc_msg_t *) list_next(iter))) {
		_pack_job_desc_msg(req, buffer, protocol_version);
	}
	list_iterator_destroy(iter);
}

static void _free_job_desc_list(void *x)
{
	job_desc_msg_t *job_desc_ptr = (job_desc_msg_t *) x;
	slurm_free_job_desc_msg(job_desc_ptr);
}

static int
_unpack_job_desc_list_msg(List *job_req_list, buf_t *buffer,
			  uint16_t protocol_version)
{
	job_desc_msg_t *req;
	uint16_t cnt = 0;
	int i;

	*job_req_list = NULL;

	safe_unpack16(&cnt, buffer);
	if (cnt == 0)
		return SLURM_SUCCESS;
	if (cnt > NO_VAL16)
		goto unpack_error;

	*job_req_list = list_create(_free_job_desc_list);
	for (i = 0; i < cnt; i++) {
		req = NULL;
		if (_unpack_job_desc_msg(&req, buffer, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		list_append(*job_req_list, req);
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*job_req_list);
	return SLURM_ERROR;
}

static void
_pack_job_alloc_info_msg(job_alloc_info_msg_t *job_desc_ptr, buf_t *buffer,
			 uint16_t protocol_version)
{
	xassert(job_desc_ptr);

	/* load the data values */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(job_desc_ptr->job_id, buffer);
		packstr(job_desc_ptr->req_cluster, buffer);
	}
}

static int
_unpack_job_alloc_info_msg(job_alloc_info_msg_t **job_desc_buffer_ptr,
			   buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	job_alloc_info_msg_t *job_desc_ptr;

	/* alloc memory for structure */
	xassert(job_desc_buffer_ptr);
	job_desc_ptr = xmalloc(sizeof(job_alloc_info_msg_t));
	*job_desc_buffer_ptr = job_desc_ptr;

	/* load the data values */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&job_desc_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->req_cluster,
				       &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_alloc_info_msg(job_desc_ptr);
	*job_desc_buffer_ptr = NULL;
	return SLURM_ERROR;
}

/* _pack_job_info_list_msg
 * packs a list of job_alloc_info_msg_t structs
 * IN job_resp_list - pointer to the job allocation descriptor to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
static void
_pack_job_info_list_msg(List job_resp_list, buf_t *buffer,
			uint16_t protocol_version)
{
	resource_allocation_response_msg_t *resp;
	ListIterator iter;
	uint16_t cnt = 0;

	if (job_resp_list)
		cnt = list_count(job_resp_list);
	pack16(cnt, buffer);
	if (cnt == 0)
		return;

	iter = list_iterator_create(job_resp_list);
	while ((resp = (resource_allocation_response_msg_t *) list_next(iter))){
		_pack_resource_allocation_response_msg(resp, buffer,
						       protocol_version);
	}
	list_iterator_destroy(iter);
}

void _free_job_info_list(void *x)
{
	resource_allocation_response_msg_t *job_info_ptr;
	job_info_ptr = (resource_allocation_response_msg_t *) x;
	slurm_free_resource_allocation_response_msg(job_info_ptr);
}

static int
_unpack_job_info_list_msg(List *job_resp_list, buf_t *buffer,
			  uint16_t protocol_version)
{
	resource_allocation_response_msg_t *resp;
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
		resp = NULL;
		if (_unpack_resource_allocation_response_msg(&resp, buffer,
							     protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		list_append(*job_resp_list, resp);
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*job_resp_list);
	return SLURM_ERROR;
}

static void
_pack_step_alloc_info_msg(step_alloc_info_msg_t * job_desc_ptr, buf_t *buffer,
			  uint16_t protocol_version)
{
	/* load the data values */
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		slurm_pack_selected_step(job_desc_ptr, protocol_version,
					 buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(job_desc_ptr->step_id.job_id, buffer);
		pack32(job_desc_ptr->het_job_offset, buffer);
		pack_old_step_id(job_desc_ptr->step_id.step_id,
				 buffer);
	}
}

static int
_unpack_step_alloc_info_msg(step_alloc_info_msg_t **
			    job_desc_buffer_ptr, buf_t *buffer,
			    uint16_t protocol_version)
{
	/* load the data values */
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (slurm_unpack_selected_step(
			    job_desc_buffer_ptr, protocol_version, buffer) !=
		    SLURM_SUCCESS)
			return SLURM_ERROR;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		step_alloc_info_msg_t *job_desc_ptr;

		/* alloc memory for structure */
		xassert(job_desc_buffer_ptr);
		job_desc_ptr = xmalloc(sizeof(step_alloc_info_msg_t));
		*job_desc_buffer_ptr = job_desc_ptr;

		safe_unpack32(&job_desc_ptr->step_id.job_id, buffer);
		safe_unpack32(&job_desc_ptr->het_job_offset, buffer);
		safe_unpack32(&job_desc_ptr->step_id.step_id, buffer);
		job_desc_ptr->step_id.step_het_comp = NO_VAL;

		return SLURM_SUCCESS;

	unpack_error:
		slurm_destroy_selected_step(job_desc_ptr);
		*job_desc_buffer_ptr = NULL;
		return SLURM_ERROR;
	} else {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void _pack_node_reg_resp(
	slurm_node_reg_resp_msg_t *msg,
	buf_t *buffer, uint16_t protocol_version)
{
	List pack_list;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (msg->tres_list)
			pack_list = msg->tres_list;
		else
			pack_list = assoc_mgr_tres_list;

		if (pack_list == assoc_mgr_tres_list)
			assoc_mgr_lock(&locks);

		(void)slurm_pack_list(pack_list,
				      slurmdb_pack_tres_rec, buffer,
				      protocol_version);

		if (pack_list == assoc_mgr_tres_list)
			assoc_mgr_unlock(&locks);

		packstr(msg->node_name, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (msg->tres_list)
			pack_list = msg->tres_list;
		else
			pack_list = assoc_mgr_tres_list;

		if (pack_list == assoc_mgr_tres_list)
			assoc_mgr_lock(&locks);

		(void)slurm_pack_list(pack_list,
				      slurmdb_pack_tres_rec, buffer,
				      protocol_version);

		if (pack_list == assoc_mgr_tres_list)
			assoc_mgr_unlock(&locks);
	}
}

static int _unpack_node_reg_resp(
	slurm_node_reg_resp_msg_t **msg,
	buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	slurm_node_reg_resp_msg_t *msg_ptr;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		xassert(msg);
		msg_ptr = xmalloc(sizeof(slurm_node_reg_resp_msg_t));
		*msg = msg_ptr;
		if (slurm_unpack_list(&msg_ptr->tres_list,
				      slurmdb_unpack_tres_rec,
				      slurmdb_destroy_tres_rec, buffer,
				      protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpackstr_xmalloc(&msg_ptr->node_name, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		xassert(msg);
		msg_ptr = xmalloc(sizeof(slurm_node_reg_resp_msg_t));
		*msg = msg_ptr;
		if (slurm_unpack_list(&msg_ptr->tres_list,
				      slurmdb_unpack_tres_rec,
				      slurmdb_destroy_tres_rec, buffer,
				      protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_reg_resp_msg(msg_ptr);
	return SLURM_ERROR;
}

static void
_pack_last_update_msg(last_update_msg_t * msg, buf_t *buffer,
		      uint16_t protocol_version)
{
	xassert(msg);
	pack_time(msg->last_update, buffer);
}

static int
_unpack_last_update_msg(last_update_msg_t ** msg, buf_t *buffer,
			uint16_t protocol_version)
{
	last_update_msg_t *last_update_msg;

	xassert(msg);
	last_update_msg = xmalloc(sizeof(last_update_msg_t));
	*msg = last_update_msg;

	safe_unpack_time(&last_update_msg->last_update, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_last_update_msg(last_update_msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_return_code_msg(return_code_msg_t * msg, buf_t *buffer,
		      uint16_t protocol_version)
{
	xassert(msg);
	pack32(msg->return_code, buffer);
}

static int
_unpack_return_code_msg(return_code_msg_t ** msg, buf_t *buffer,
			uint16_t protocol_version)
{
	return_code_msg_t *return_code_msg;

	xassert(msg);
	return_code_msg = xmalloc(sizeof(return_code_msg_t));
	*msg = return_code_msg;

	safe_unpack32(&return_code_msg->return_code, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_return_code_msg(return_code_msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_return_code2_msg(return_code2_msg_t * msg, buf_t *buffer,
		       uint16_t protocol_version)
{
	xassert(msg);
	pack32(msg->return_code, buffer);
	packstr(msg->err_msg,    buffer);
}

/* Log error message, otherwise replicate _unpack_return_code_msg() */
static int
_unpack_return_code2_msg(return_code_msg_t ** msg, buf_t *buffer,
			 uint16_t protocol_version)
{
	return_code_msg_t *return_code_msg;
	uint32_t uint32_tmp = 0;
	char *err_msg = NULL;

	xassert(msg);
	return_code_msg = xmalloc(sizeof(return_code_msg_t));
	*msg = return_code_msg;

	safe_unpack32(&return_code_msg->return_code, buffer);
	safe_unpackstr_xmalloc(&err_msg, &uint32_tmp, buffer);
	if (err_msg) {
		print_multi_line_string(err_msg, -1, LOG_LEVEL_ERROR);
		xfree(err_msg);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_return_code_msg(return_code_msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_reroute_msg(reroute_msg_t * msg, buf_t *buffer, uint16_t protocol_version)
{
	xassert(buffer);
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (msg->working_cluster_rec) {
			pack8(1, buffer);
			slurmdb_pack_cluster_rec(msg->working_cluster_rec,
						 protocol_version, buffer);
		} else
			pack8(0, buffer);
	}
}

static int
_unpack_reroute_msg(reroute_msg_t **msg, buf_t *buffer, uint16_t protocol_version)
{
	reroute_msg_t *reroute_msg;
	uint8_t uint8_tmp = 0;

	xassert(buffer);
	xassert(msg);

	reroute_msg = xmalloc(sizeof(reroute_msg_t));
	*msg = reroute_msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			slurmdb_unpack_cluster_rec(
				(void **)&reroute_msg->working_cluster_rec,
				protocol_version, buffer);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reroute_msg(reroute_msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_reattach_tasks_request_msg(reattach_tasks_request_msg_t * msg,
				 buf_t *buffer,
				 uint16_t protocol_version)
{
	int i;

	xassert(msg);
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack16((uint16_t)msg->num_resp_port, buffer);
		for (i = 0; i < msg->num_resp_port; i++)
			pack16((uint16_t)msg->resp_port[i], buffer);
		pack16((uint16_t)msg->num_io_port, buffer);
		for (i = 0; i < msg->num_io_port; i++)
			pack16((uint16_t)msg->io_port[i], buffer);
		slurm_cred_pack(msg->cred, buffer, protocol_version);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack16((uint16_t)msg->num_resp_port, buffer);
		for (i = 0; i < msg->num_resp_port; i++)
			pack16((uint16_t)msg->resp_port[i], buffer);
		pack16((uint16_t)msg->num_io_port, buffer);
		for (i = 0; i < msg->num_io_port; i++)
			pack16((uint16_t)msg->io_port[i], buffer);
		slurm_cred_pack(msg->cred, buffer, protocol_version);
	}
}

static int
_unpack_reattach_tasks_request_msg(reattach_tasks_request_msg_t ** msg_ptr,
				   buf_t *buffer,
				   uint16_t protocol_version)
{
	reattach_tasks_request_msg_t *msg;
	int i;

	xassert(msg_ptr);
	msg = xmalloc(sizeof(*msg));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&msg->num_resp_port, buffer);
		if (msg->num_resp_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_resp_port > 0) {
			safe_xcalloc(msg->resp_port, msg->num_resp_port,
				     sizeof(uint16_t));
			for (i = 0; i < msg->num_resp_port; i++)
				safe_unpack16(&msg->resp_port[i], buffer);
		}
		safe_unpack16(&msg->num_io_port, buffer);
		if (msg->num_io_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_io_port > 0) {
			safe_xcalloc(msg->io_port, msg->num_io_port,
				     sizeof(uint16_t));
			for (i = 0; i < msg->num_io_port; i++)
				safe_unpack16(&msg->io_port[i], buffer);
		}

		if (!(msg->cred = slurm_cred_unpack(buffer, protocol_version)))
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&msg->num_resp_port, buffer);
		if (msg->num_resp_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_resp_port > 0) {
			safe_xcalloc(msg->resp_port, msg->num_resp_port,
				     sizeof(uint16_t));
			for (i = 0; i < msg->num_resp_port; i++)
				safe_unpack16(&msg->resp_port[i], buffer);
		}
		safe_unpack16(&msg->num_io_port, buffer);
		if (msg->num_io_port >= NO_VAL16)
			goto unpack_error;
		if (msg->num_io_port > 0) {
			safe_xcalloc(msg->io_port, msg->num_io_port,
				     sizeof(uint16_t));
			for (i = 0; i < msg->num_io_port; i++)
				safe_unpack16(&msg->io_port[i], buffer);
		}

		if (!(msg->cred = slurm_cred_unpack(buffer, protocol_version)))
			goto unpack_error;
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reattach_tasks_request_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_reattach_tasks_response_msg(reattach_tasks_response_msg_t * msg,
				  buf_t *buffer,
				  uint16_t protocol_version)
{
	int i;

	xassert(msg);
	packstr(msg->node_name,   buffer);
	pack32(msg->return_code, buffer);
	pack32(msg->ntasks, buffer);
	pack32_array(msg->gtids,      msg->ntasks, buffer);
	pack32_array(msg->local_pids, msg->ntasks, buffer);
	for (i = 0; i < msg->ntasks; i++) {
		packstr(msg->executable_names[i], buffer);
	}
}

static int
_unpack_reattach_tasks_response_msg(reattach_tasks_response_msg_t ** msg_ptr,
				    buf_t *buffer,
				    uint16_t protocol_version)
{
	uint32_t ntasks;
	uint32_t uint32_tmp;
	reattach_tasks_response_msg_t *msg = xmalloc(sizeof(*msg));
	int i;

	xassert(msg_ptr);
	*msg_ptr = msg;

	safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
	safe_unpack32(&msg->return_code,  buffer);
	safe_unpack32(&msg->ntasks,       buffer);
	safe_unpack32_array(&msg->gtids,      &ntasks, buffer);
	safe_unpack32_array(&msg->local_pids, &ntasks, buffer);
	if (msg->ntasks != ntasks)
		goto unpack_error;
	safe_xcalloc(msg->executable_names, msg->ntasks, sizeof(char *));
	for (i = 0; i < msg->ntasks; i++) {
		safe_unpackstr_xmalloc(&(msg->executable_names[i]), &uint32_tmp,
				       buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reattach_tasks_response_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}


static void
_pack_task_exit_msg(task_exit_msg_t * msg, buf_t *buffer,
		    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(msg->return_code, buffer);
		pack32(msg->num_tasks, buffer);
		pack32_array(msg->task_id_list,
			     msg->num_tasks, buffer);
		pack_step_id(&msg->step_id, buffer, protocol_version);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->return_code, buffer);
		pack32(msg->num_tasks, buffer);
		pack32_array(msg->task_id_list,
			     msg->num_tasks, buffer);
		pack_step_id(&msg->step_id, buffer, protocol_version);
	}
}

static int
_unpack_task_exit_msg(task_exit_msg_t ** msg_ptr, buf_t *buffer,
		      uint16_t protocol_version)
{
	task_exit_msg_t *msg;
	uint32_t uint32_tmp;

	xassert(msg_ptr);
	msg = xmalloc(sizeof(task_exit_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->return_code, buffer);
		safe_unpack32(&msg->num_tasks, buffer);
		safe_unpack32_array(&msg->task_id_list, &uint32_tmp, buffer);
		if (msg->num_tasks != uint32_tmp)
			goto unpack_error;
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->return_code, buffer);
		safe_unpack32(&msg->num_tasks, buffer);
		safe_unpack32_array(&msg->task_id_list, &uint32_tmp, buffer);
		if (msg->num_tasks != uint32_tmp)
			goto unpack_error;
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_task_exit_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}


static void
_pack_launch_tasks_response_msg(launch_tasks_response_msg_t * msg, buf_t *buffer,
				uint16_t protocol_version)
{
	xassert(msg);
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->return_code, buffer);
		packstr(msg->node_name, buffer);
		pack32(msg->count_of_pids, buffer);
		pack32_array(msg->local_pids, msg->count_of_pids, buffer);
		pack32_array(msg->task_ids, msg->count_of_pids, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->return_code, buffer);
		packstr(msg->node_name, buffer);
		pack32(msg->count_of_pids, buffer);
		pack32_array(msg->local_pids, msg->count_of_pids, buffer);
		pack32_array(msg->task_ids, msg->count_of_pids, buffer);
	}
}

static int
_unpack_launch_tasks_response_msg(launch_tasks_response_msg_t **msg_ptr,
				  buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	launch_tasks_response_msg_t *msg;

	xassert(msg_ptr);
	msg = xmalloc(sizeof(launch_tasks_response_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg->return_code, buffer);
		safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
		safe_unpack32(&msg->count_of_pids, buffer);
		safe_unpack32_array(&msg->local_pids, &uint32_tmp, buffer);
		if (msg->count_of_pids != uint32_tmp)
			goto unpack_error;
		safe_unpack32_array(&msg->task_ids, &uint32_tmp, buffer);
		if (msg->count_of_pids != uint32_tmp)
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg->return_code, buffer);
		safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
		safe_unpack32(&msg->count_of_pids, buffer);
		safe_unpack32_array(&msg->local_pids, &uint32_tmp, buffer);
		if (msg->count_of_pids != uint32_tmp)
			goto unpack_error;
		safe_unpack32_array(&msg->task_ids, &uint32_tmp, buffer);
		if (msg->count_of_pids != uint32_tmp)
			goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_launch_tasks_response_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_launch_tasks_request_msg(launch_tasks_request_msg_t *msg,
					   buf_t *buffer,
					   uint16_t protocol_version)
{
	int i = 0;

	xassert(msg);

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->user_name, buffer);
		pack32_array(msg->gids, msg->ngids, buffer);

		pack32(msg->het_job_node_offset, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->het_job_nnodes, buffer);
		if (msg->het_job_nnodes != NO_VAL) {
			for (i = 0; i < msg->het_job_nnodes; i++) {
				pack32_array(
					msg->het_job_tids[i],
					(uint32_t)msg->het_job_task_cnts[i],
					buffer);
			}
		}
		pack32(msg->het_job_ntasks, buffer);
		if (msg->het_job_ntasks != NO_VAL) {
			for (i = 0; i < msg->het_job_ntasks; i++)
				pack32(msg->het_job_tid_offsets[i], buffer);
		}
		pack32(msg->het_job_offset, buffer);
		pack32(msg->het_job_step_cnt, buffer);
		pack32(msg->het_job_task_offset, buffer);
		packstr(msg->het_job_node_list, buffer);
		pack32(msg->ntasks, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		packstr(msg->partition, buffer);
		pack64(msg->job_mem_lim, buffer);
		pack64(msg->step_mem_lim, buffer);

		pack32(msg->nnodes, buffer);
		pack16(msg->cpus_per_task, buffer);
		packstr(msg->tres_per_task, buffer);
		pack16(msg->threads_per_core, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->node_cpus, buffer);
		pack16(msg->job_core_spec, buffer);
		pack16(msg->accel_bind_type, buffer);

		slurm_cred_pack(msg->cred, buffer, protocol_version);
		for (i = 0; i < msg->nnodes; i++) {
			pack16(msg->tasks_to_launch[i], buffer);
			pack32_array(msg->global_task_ids[i],
				     (uint32_t) msg->tasks_to_launch[i],
				     buffer);
		}
		pack16(msg->num_resp_port, buffer);
		for (i = 0; i < msg->num_resp_port; i++)
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
		if ((msg->flags & LAUNCH_USER_MANAGED_IO) == 0) {
			packstr(msg->ofname, buffer);
			packstr(msg->efname, buffer);
			packstr(msg->ifname, buffer);
			pack16(msg->num_io_port, buffer);
			for (i = 0; i < msg->num_io_port; i++)
				pack16(msg->io_port[i], buffer);
		}
		pack32(msg->profile, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_epilog, buffer);
		pack16(msg->slurmd_debug, buffer);
		switch_g_pack_jobinfo(msg->switch_job, buffer,
				      protocol_version);
		job_options_pack(msg->options, buffer);
		packstr(msg->alias_list, buffer);
		packstr(msg->complete_nodelist, buffer);

		pack8(msg->open_mode, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);

		select_g_select_jobinfo_pack(msg->select_jobinfo,
					     buffer, protocol_version);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->user_name, buffer);
		pack32_array(msg->gids, msg->ngids, buffer);

		pack32(msg->het_job_node_offset, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->het_job_nnodes, buffer);
		if (msg->het_job_nnodes != NO_VAL) {
			for (i = 0; i < msg->het_job_nnodes; i++) {
				pack32_array(
					msg->het_job_tids[i],
					(uint32_t)msg->het_job_task_cnts[i],
					buffer);
			}
		}
		pack32(msg->het_job_ntasks, buffer);
		if (msg->het_job_ntasks != NO_VAL) {
			for (i = 0; i < msg->het_job_ntasks; i++)
				pack32(msg->het_job_tid_offsets[i], buffer);
		}
		pack32(msg->het_job_offset, buffer);
		pack32(msg->het_job_step_cnt, buffer);
		pack32(msg->het_job_task_offset, buffer);
		packstr(msg->het_job_node_list, buffer);
		pack32(msg->ntasks, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_tres, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		packstr(msg->partition, buffer);
		pack64(msg->job_mem_lim, buffer);
		pack64(msg->step_mem_lim, buffer);

		pack32(msg->nnodes, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->threads_per_core, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->node_cpus, buffer);
		pack16(msg->job_core_spec, buffer);
		pack16(msg->accel_bind_type, buffer);

		slurm_cred_pack(msg->cred, buffer, protocol_version);
		for (i = 0; i < msg->nnodes; i++) {
			pack16(msg->tasks_to_launch[i], buffer);
			pack32_array(msg->global_task_ids[i],
				     (uint32_t) msg->tasks_to_launch[i],
				     buffer);
		}
		pack16(msg->num_resp_port, buffer);
		for (i = 0; i < msg->num_resp_port; i++)
			pack16(msg->resp_port[i], buffer);
		slurm_pack_addr(&msg->orig_addr, buffer);
		packstr_array(msg->env, msg->envc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->cwd, buffer);
		pack16(msg->cpu_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->mem_bind, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		pack32(msg->flags, buffer);
		if ((msg->flags & LAUNCH_USER_MANAGED_IO) == 0) {
			packstr(msg->ofname, buffer);
			packstr(msg->efname, buffer);
			packstr(msg->ifname, buffer);
			pack16(msg->num_io_port, buffer);
			for (i = 0; i < msg->num_io_port; i++)
				pack16(msg->io_port[i], buffer);
		}
		pack32(msg->profile, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_epilog, buffer);
		pack16(msg->slurmd_debug, buffer);
		switch_g_pack_jobinfo(msg->switch_job, buffer,
				      protocol_version);
		job_options_pack(msg->options, buffer);
		packstr(msg->alias_list, buffer);
		packstr(msg->complete_nodelist, buffer);

		pack8(msg->open_mode, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		packnull(buffer); /* was ckpt_dir */
		packnull(buffer); /* was restart_dir */

		select_g_select_jobinfo_pack(msg->select_jobinfo,
					     buffer, protocol_version);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);

		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->user_name, buffer);
		pack32_array(msg->gids, msg->ngids, buffer);

		pack32(msg->het_job_node_offset, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->het_job_nnodes, buffer);
		if (msg->het_job_nnodes != NO_VAL) {
			pack8((uint8_t) 1, buffer);
			for (i = 0; i < msg->het_job_nnodes; i++) {
				pack16(msg->het_job_task_cnts[i], buffer);
				pack32_array(
					msg->het_job_tids[i],
					(uint32_t)msg->het_job_task_cnts[i],
					buffer);
			}
		}
		pack32(msg->het_job_ntasks, buffer);
		if (msg->het_job_ntasks != NO_VAL) {
			pack8((uint8_t) 1, buffer);
			for (i = 0; i < msg->het_job_ntasks; i++)
				pack32(msg->het_job_tid_offsets[i], buffer);
		}
		pack32(msg->het_job_offset, buffer);
		pack32(msg->het_job_step_cnt, buffer);
		pack32(msg->het_job_task_offset, buffer);
		packstr(msg->het_job_node_list, buffer);
		pack32(msg->ntasks, buffer);
		pack16(msg->ntasks_per_board, buffer);
		pack16(msg->ntasks_per_core, buffer);
		pack16(msg->ntasks_per_socket, buffer);
		packstr(msg->partition, buffer);
		pack64(msg->job_mem_lim, buffer);
		pack64(msg->step_mem_lim, buffer);

		pack32(msg->nnodes, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack32(msg->task_dist, buffer);
		pack16(msg->node_cpus, buffer);
		pack16(msg->job_core_spec, buffer);
		pack16(msg->accel_bind_type, buffer);

		slurm_cred_pack(msg->cred, buffer, protocol_version);
		for (i = 0; i < msg->nnodes; i++) {
			pack16(msg->tasks_to_launch[i], buffer);
			pack32_array(msg->global_task_ids[i],
				     (uint32_t) msg->tasks_to_launch[i],
				     buffer);
		}
		pack16(msg->num_resp_port, buffer);
		for (i = 0; i < msg->num_resp_port; i++)
			pack16(msg->resp_port[i], buffer);
		slurm_pack_slurm_addr(&msg->orig_addr, buffer);
		packstr_array(msg->env, msg->envc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->cwd, buffer);
		pack16(msg->cpu_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->mem_bind, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		pack32(msg->flags, buffer);
		if ((msg->flags & LAUNCH_USER_MANAGED_IO) == 0) {
			packstr(msg->ofname, buffer);
			packstr(msg->efname, buffer);
			packstr(msg->ifname, buffer);
			pack16(msg->num_io_port, buffer);
			for (i = 0; i < msg->num_io_port; i++)
				pack16(msg->io_port[i], buffer);
		}
		pack32(msg->profile, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_epilog, buffer);
		pack16(msg->slurmd_debug, buffer);
		switch_g_pack_jobinfo(msg->switch_job, buffer,
				      protocol_version);
		job_options_pack(msg->options, buffer);
		packstr(msg->alias_list, buffer);
		packstr(msg->complete_nodelist, buffer);

		pack8(msg->open_mode, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->cpu_freq_min, buffer);
		pack32(msg->cpu_freq_max, buffer);
		pack32(msg->cpu_freq_gov, buffer);
		packnull(buffer); /* was ckpt_dir */
		packnull(buffer); /* was restart_dir */

		select_g_select_jobinfo_pack(msg->select_jobinfo,
					     buffer, protocol_version);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);
	}
}

static int _unpack_launch_tasks_request_msg(launch_tasks_request_msg_t **msg_ptr,
					    buf_t *buffer,
					    uint16_t protocol_version)
{
	uint8_t uint8_tmp = NO_VAL8;
	uint32_t uint32_tmp = 0;
	launch_tasks_request_msg_t *msg;
	int i = 0;

	xassert(msg_ptr);
	msg = xmalloc(sizeof(launch_tasks_request_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);
		safe_unpackstr_xmalloc(&msg->user_name, &uint32_tmp, buffer);
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
		safe_unpackstr_xmalloc(&msg->het_job_node_list, &uint32_tmp,
				       buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpackstr_xmalloc(&msg->partition, &uint32_tmp, buffer);
		safe_unpack64(&msg->job_mem_lim, buffer);
		safe_unpack64(&msg->step_mem_lim, buffer);

		safe_unpack32(&msg->nnodes, buffer);
		if (msg->nnodes >= NO_VAL)
			goto unpack_error;
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpackstr_xmalloc(&msg->tres_per_task,
				       &uint32_tmp, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->node_cpus, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);
		safe_unpack16(&msg->accel_bind_type, buffer);

		if (!(msg->cred = slurm_cred_unpack(buffer, protocol_version)))
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
		safe_unpackstr_xmalloc(&msg->container, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->cwd, &uint32_tmp, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->cpu_bind, &uint32_tmp, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->mem_bind, &uint32_tmp, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpack32(&msg->flags, buffer);
		if ((msg->flags & LAUNCH_USER_MANAGED_IO) == 0) {
			safe_unpackstr_xmalloc(&msg->ofname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->efname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->ifname, &uint32_tmp,
					       buffer);
			safe_unpack16(&msg->num_io_port, buffer);
			if (msg->num_io_port >= NO_VAL16)
				goto unpack_error;
			if (msg->num_io_port > 0) {
				safe_xcalloc(msg->io_port, msg->num_io_port,
				             sizeof(uint16_t));
				for (i = 0; i < msg->num_io_port; i++)
					safe_unpack16(&msg->io_port[i],
						      buffer);
			}
		}
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr_xmalloc(&msg->task_prolog, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->task_epilog, &uint32_tmp, buffer);
		safe_unpack16(&msg->slurmd_debug, buffer);

		if (switch_g_unpack_jobinfo(&msg->switch_job, buffer,
					    protocol_version) < 0) {
			error("switch_g_unpack_jobinfo: %m");
			switch_g_free_jobinfo(msg->switch_job);
			goto unpack_error;
		}
		msg->options = job_options_create();
		if (job_options_unpack(msg->options, buffer) < 0) {
			error("Unable to unpack extra job options: %m");
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&msg->alias_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->complete_nodelist, &uint32_tmp,
				       buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpackstr_xmalloc(&msg->acctg_freq, &uint32_tmp, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);

		if (select_g_select_jobinfo_unpack(&msg->select_jobinfo,
						   buffer, protocol_version)) {
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&msg->tres_bind, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->tres_freq, &uint32_tmp, buffer);
		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr_xmalloc(&msg->x11_alloc_host, &uint32_tmp,
				       buffer);
		safe_unpack16(&msg->x11_alloc_port, buffer);
		safe_unpackstr_xmalloc(&msg->x11_magic_cookie, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->x11_target, &uint32_tmp,
				       buffer);
		safe_unpack16(&msg->x11_target_port, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		char *temp_str;
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);
		safe_unpackstr_xmalloc(&msg->user_name, &uint32_tmp, buffer);
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
		safe_unpackstr_xmalloc(&msg->het_job_node_list, &uint32_tmp,
				       buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		safe_unpack16(&msg->ntasks_per_tres, buffer);
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpackstr_xmalloc(&msg->partition, &uint32_tmp, buffer);
		safe_unpack64(&msg->job_mem_lim, buffer);
		safe_unpack64(&msg->step_mem_lim, buffer);

		safe_unpack32(&msg->nnodes, buffer);
		if (msg->nnodes >= NO_VAL)
			goto unpack_error;
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->threads_per_core, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->node_cpus, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);
		safe_unpack16(&msg->accel_bind_type, buffer);

		if (!(msg->cred = slurm_cred_unpack(buffer, protocol_version)))
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
		safe_unpackstr_xmalloc(&msg->cwd, &uint32_tmp, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->cpu_bind, &uint32_tmp, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->mem_bind, &uint32_tmp, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpack32(&msg->flags, buffer);
		if ((msg->flags & LAUNCH_USER_MANAGED_IO) == 0) {
			safe_unpackstr_xmalloc(&msg->ofname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->efname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->ifname, &uint32_tmp,
					       buffer);
			safe_unpack16(&msg->num_io_port, buffer);
			if (msg->num_io_port >= NO_VAL16)
				goto unpack_error;
			if (msg->num_io_port > 0) {
				safe_xcalloc(msg->io_port, msg->num_io_port,
				             sizeof(uint16_t));
				for (i = 0; i < msg->num_io_port; i++)
					safe_unpack16(&msg->io_port[i],
						      buffer);
			}
		}
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr_xmalloc(&msg->task_prolog, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->task_epilog, &uint32_tmp, buffer);
		safe_unpack16(&msg->slurmd_debug, buffer);

		if (switch_g_unpack_jobinfo(&msg->switch_job, buffer,
					    protocol_version) < 0) {
			error("switch_g_unpack_jobinfo: %m");
			switch_g_free_jobinfo(msg->switch_job);
			goto unpack_error;
		}
		msg->options = job_options_create();
		if (job_options_unpack(msg->options, buffer) < 0) {
			error("Unable to unpack extra job options: %m");
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&msg->alias_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->complete_nodelist, &uint32_tmp,
				       buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpackstr_xmalloc(&msg->acctg_freq, &uint32_tmp, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was ckpt_dir */
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was restart_dir */
		if (select_g_select_jobinfo_unpack(&msg->select_jobinfo,
						   buffer, protocol_version)) {
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&msg->tres_bind, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->tres_freq, &uint32_tmp, buffer);
		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr_xmalloc(&msg->x11_alloc_host, &uint32_tmp,
				       buffer);
		safe_unpack16(&msg->x11_alloc_port, buffer);
		safe_unpackstr_xmalloc(&msg->x11_magic_cookie, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->x11_target, &uint32_tmp,
				       buffer);
		safe_unpack16(&msg->x11_target_port, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *temp_str;
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);
		safe_unpackstr_xmalloc(&msg->user_name, &uint32_tmp, buffer);
		safe_unpack32_array(&msg->gids, &msg->ngids, buffer);

		safe_unpack32(&msg->het_job_node_offset, buffer);
		safe_unpack32(&msg->het_job_id, buffer);
		safe_unpack32(&msg->het_job_nnodes, buffer);
		if (msg->het_job_nnodes != NO_VAL) {
			safe_unpack8(&uint8_tmp, buffer);
			safe_xcalloc(msg->het_job_task_cnts,
				     msg->het_job_nnodes,
				     sizeof(uint16_t));
			safe_xcalloc(msg->het_job_tids, msg->het_job_nnodes,
				     sizeof(uint32_t *));
			for (i = 0; i < msg->het_job_nnodes; i++) {
				safe_unpack16(&msg->het_job_task_cnts[i],
					      buffer);
				safe_unpack32_array(&msg->het_job_tids[i],
						    &uint32_tmp,
						    buffer);
				if (msg->het_job_task_cnts[i] != uint32_tmp)
					goto unpack_error;
			}
		}
		safe_unpack32(&msg->het_job_ntasks, buffer);
		if (msg->het_job_ntasks != NO_VAL) {
			safe_unpack8(&uint8_tmp, buffer);
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
		safe_unpackstr_xmalloc(&msg->het_job_node_list, &uint32_tmp,
				       buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack16(&msg->ntasks_per_board, buffer);
		safe_unpack16(&msg->ntasks_per_core, buffer);
		msg->ntasks_per_tres = NO_VAL16;
		safe_unpack16(&msg->ntasks_per_socket, buffer);
		safe_unpackstr_xmalloc(&msg->partition, &uint32_tmp, buffer);
		safe_unpack64(&msg->job_mem_lim, buffer);
		safe_unpack64(&msg->step_mem_lim, buffer);

		safe_unpack32(&msg->nnodes, buffer);
		if (msg->nnodes >= NO_VAL)
			goto unpack_error;
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack32(&msg->task_dist, buffer);
		safe_unpack16(&msg->node_cpus, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);
		safe_unpack16(&msg->accel_bind_type, buffer);

		if (!(msg->cred = slurm_cred_unpack(buffer, protocol_version)))
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
		slurm_unpack_slurm_addr_no_alloc(&msg->orig_addr, buffer);
		safe_unpackstr_array(&msg->env, &msg->envc, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		safe_unpackstr_xmalloc(&msg->cwd, &uint32_tmp, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->cpu_bind, &uint32_tmp, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->mem_bind, &uint32_tmp, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpack32(&msg->flags, buffer);
		if ((msg->flags & LAUNCH_USER_MANAGED_IO) == 0) {
			safe_unpackstr_xmalloc(&msg->ofname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->efname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->ifname, &uint32_tmp,
					       buffer);
			safe_unpack16(&msg->num_io_port, buffer);
			if (msg->num_io_port >= NO_VAL16)
				goto unpack_error;
			if (msg->num_io_port > 0) {
				safe_xcalloc(msg->io_port, msg->num_io_port,
				             sizeof(uint16_t));
				for (i = 0; i < msg->num_io_port; i++)
					safe_unpack16(&msg->io_port[i],
						      buffer);
			}
		}
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr_xmalloc(&msg->task_prolog, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->task_epilog, &uint32_tmp, buffer);
		safe_unpack16(&msg->slurmd_debug, buffer);

		if (switch_g_unpack_jobinfo(&msg->switch_job, buffer,
					    protocol_version) < 0) {
			error("switch_g_unpack_jobinfo: %m");
			switch_g_free_jobinfo(msg->switch_job);
			goto unpack_error;
		}
		msg->options = job_options_create();
		if (job_options_unpack(msg->options, buffer) < 0) {
			error("Unable to unpack extra job options: %m");
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&msg->alias_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->complete_nodelist, &uint32_tmp,
				       buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpackstr_xmalloc(&msg->acctg_freq, &uint32_tmp, buffer);
		safe_unpack32(&msg->cpu_freq_min, buffer);
		safe_unpack32(&msg->cpu_freq_max, buffer);
		safe_unpack32(&msg->cpu_freq_gov, buffer);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was ckpt_dir */
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was restart_dir */
		if (select_g_select_jobinfo_unpack(&msg->select_jobinfo,
						   buffer, protocol_version)) {
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&msg->tres_bind, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->tres_freq, &uint32_tmp, buffer);
		safe_unpack16(&msg->x11, buffer);
		safe_unpackstr_xmalloc(&msg->x11_alloc_host, &uint32_tmp,
				       buffer);
		safe_unpack16(&msg->x11_alloc_port, buffer);
		safe_unpackstr_xmalloc(&msg->x11_magic_cookie, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->x11_target, &uint32_tmp,
				       buffer);
		safe_unpack16(&msg->x11_target_port, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_launch_tasks_request_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_task_user_managed_io_stream_msg(task_user_managed_io_msg_t * msg,
				      buf_t *buffer,
				      uint16_t protocol_version)
{
	xassert(msg);
	pack32(msg->task_id, buffer);
}

static int
_unpack_task_user_managed_io_stream_msg(task_user_managed_io_msg_t **msg_ptr,
					buf_t *buffer,
					uint16_t protocol_version)
{
	task_user_managed_io_msg_t *msg;

	xassert(msg_ptr);
	msg = xmalloc(sizeof(task_user_managed_io_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->task_id, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_task_user_managed_io_stream_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_cancel_tasks_msg(signal_tasks_msg_t *msg, buf_t *buffer,
		       uint16_t protocol_version)
{
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack16(msg->flags, buffer);
		pack16(msg->signal, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->flags, buffer);
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack16(msg->signal, buffer);
	}
}

static int
_unpack_cancel_tasks_msg(signal_tasks_msg_t **msg_ptr, buf_t *buffer,
			 uint16_t protocol_version)
{
	signal_tasks_msg_t *msg;

	msg = xmalloc(sizeof(signal_tasks_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&msg->flags, buffer);
		safe_unpack16(&msg->signal, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->flags, buffer);
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&msg->signal, buffer);
	} else {
		error("_unpack_cancel_tasks_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_signal_tasks_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_reboot_msg(reboot_msg_t * msg, buf_t *buffer,
		 uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

static int
_unpack_reboot_msg(reboot_msg_t ** msg_ptr, buf_t *buffer,
		   uint16_t protocol_version)
{
	reboot_msg_t *msg;
	uint32_t uint32_tmp;

	msg = xmalloc(sizeof(reboot_msg_t));
	slurm_init_reboot_msg(msg, false);
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->features, &uint32_tmp, buffer);
		safe_unpack16(&msg->flags, buffer);
		safe_unpack32(&msg->next_state, buffer);
		safe_unpackstr_xmalloc(&msg->node_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->reason, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reboot_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_shutdown_msg(shutdown_msg_t * msg, buf_t *buffer,
		   uint16_t protocol_version)
{
	pack16((uint16_t)msg->options, buffer);
}

static int
_unpack_shutdown_msg(shutdown_msg_t ** msg_ptr, buf_t *buffer,
		     uint16_t protocol_version)
{
	shutdown_msg_t *msg;

	msg = xmalloc(sizeof(shutdown_msg_t));
	*msg_ptr = msg;

	safe_unpack16(&msg->options, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shutdown_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

/* _pack_job_step_kill_msg
 * packs a slurm job step signal message
 * IN msg - pointer to the job step signal message
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
static void
_pack_job_step_kill_msg(job_step_kill_msg_t * msg, buf_t *buffer,
			uint16_t protocol_version)
{
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr(msg->sjob_id, buffer);
		packstr(msg->sibling, buffer);
		pack16((uint16_t)msg->signal, buffer);
		pack16((uint16_t)msg->flags, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->sjob_id, buffer);
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr(msg->sibling, buffer);
		pack16((uint16_t)msg->signal, buffer);
		pack16((uint16_t)msg->flags, buffer);
	}
}

/* _unpack_job_step_kill_msg
 * unpacks a slurm job step signal message
 * OUT msg_ptr - pointer to the job step signal message buffer
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 */
static int
_unpack_job_step_kill_msg(job_step_kill_msg_t ** msg_ptr, buf_t *buffer,
			  uint16_t protocol_version)
{
	job_step_kill_msg_t *msg;
	uint32_t cc;

	msg = xmalloc(sizeof(job_step_kill_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->sjob_id, &cc, buffer);
		safe_unpackstr_xmalloc(&msg->sibling, &cc, buffer);
		safe_unpack16(&msg->signal, buffer);
		safe_unpack16(&msg->flags, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->sjob_id, &cc, buffer);
		safe_unpack32(&msg->step_id.job_id, buffer);
		safe_unpack32(&msg->step_id.step_id, buffer);
		msg->step_id.step_het_comp = NO_VAL;
		safe_unpackstr_xmalloc(&msg->sibling, &cc, buffer);
		safe_unpack16(&msg->signal, buffer);
		safe_unpack16(&msg->flags, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_kill_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_update_job_step_msg(step_update_request_msg_t * msg, buf_t *buffer,
			  uint16_t protocol_version)
{
	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->time_limit, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_time(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack8(0, buffer);
		packnull(buffer);
		pack_time(0, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->time_limit, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack8(0, buffer);
		packnull(buffer);
		pack_time(0, buffer);
		pack_old_step_id(msg->step_id, buffer);
		pack32(msg->time_limit, buffer);
	}
}

static int
_unpack_update_job_step_msg(step_update_request_msg_t ** msg_ptr, buf_t *buffer,
			    uint16_t protocol_version)
{
	step_update_request_msg_t *msg;

	msg = xmalloc(sizeof(step_update_request_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->step_id, buffer);
		safe_unpack32(&msg->time_limit, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		uint8_t with_jobacct = 0;
		uint32_t uint32_tmp;
		time_t time_tmp;
		char *char_tmp;

		safe_unpack_time(&time_tmp, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack8(&with_jobacct, buffer);
		if (with_jobacct) {
			jobacctinfo_t *jobacct = NULL;
			if (jobacctinfo_unpack(&jobacct, protocol_version,
					       PROTOCOL_TYPE_SLURM, buffer, 1)
			    != SLURM_SUCCESS)
				goto unpack_error;
			jobacctinfo_destroy(jobacct);
		}
		safe_unpackstr_xmalloc(&char_tmp, &uint32_tmp, buffer);
		safe_unpack_time(&time_tmp, buffer);
		safe_unpack32(&msg->step_id, buffer);
		safe_unpack32(&msg->time_limit, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint8_t with_jobacct = 0;
		uint32_t uint32_tmp;
		time_t time_tmp;
		char *char_tmp;

		safe_unpack_time(&time_tmp, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack8(&with_jobacct, buffer);
		if (with_jobacct) {
			jobacctinfo_t *jobacct = NULL;
			if (jobacctinfo_unpack(&jobacct, protocol_version,
					       PROTOCOL_TYPE_SLURM, buffer, 1)
			    != SLURM_SUCCESS)
				goto unpack_error;
			jobacctinfo_destroy(jobacct);
		}
		safe_unpackstr_xmalloc(&char_tmp, &uint32_tmp, buffer);
		safe_unpack_time(&time_tmp, buffer);
		safe_unpack32(&msg->step_id, buffer);
		safe_unpack32(&msg->time_limit, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_step_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_complete_job_allocation_msg(
	complete_job_allocation_msg_t * msg, buf_t *buffer,
	uint16_t protocol_version)
{
	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->job_rc, buffer);
}

static int
_unpack_complete_job_allocation_msg(
	complete_job_allocation_msg_t ** msg_ptr, buf_t *buffer,
	uint16_t protocol_version)
{
	complete_job_allocation_msg_t *msg;

	msg = xmalloc(sizeof(complete_job_allocation_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->job_rc, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_complete_job_allocation_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_complete_prolog_msg(
	complete_prolog_msg_t * msg, buf_t *buffer,
	uint16_t protocol_version)
{
	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->prolog_rc, buffer);
}

static int
_unpack_complete_prolog_msg(
	complete_prolog_msg_t ** msg_ptr, buf_t *buffer,
	uint16_t protocol_version)
{
	complete_prolog_msg_t *msg;

	msg = xmalloc(sizeof(complete_prolog_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->prolog_rc, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_complete_prolog_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_prolog_launch_msg(prolog_launch_msg_t *msg,
				    buf_t *buffer,
				    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		gres_job_alloc_pack(msg->job_gres_info, buffer,
				    protocol_version);
		pack32(msg->job_id, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);

		packstr(msg->alias_list, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->partition, buffer);
		packstr(msg->std_err, buffer);
		packstr(msg->std_out, buffer);
		packstr(msg->work_dir, buffer);

		pack16(msg->x11, buffer);
		packstr(msg->x11_alloc_host, buffer);
		pack16(msg->x11_alloc_port, buffer);
		packstr(msg->x11_magic_cookie, buffer);
		packstr(msg->x11_target, buffer);
		pack16(msg->x11_target_port, buffer);

		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		slurm_cred_pack(msg->cred, buffer, protocol_version);
		packstr(msg->user_name, buffer);
	}
}

static int _unpack_prolog_launch_msg(prolog_launch_msg_t **msg,
				     buf_t *buffer,
				     uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	prolog_launch_msg_t *launch_msg_ptr;

	xassert(msg);
	launch_msg_ptr = xmalloc(sizeof(prolog_launch_msg_t));
	*msg = launch_msg_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (gres_job_alloc_unpack(&launch_msg_ptr->job_gres_info,
					  buffer, protocol_version))
			goto unpack_error;
		safe_unpack32(&launch_msg_ptr->job_id, buffer);
		safe_unpack32(&launch_msg_ptr->het_job_id, buffer);
		safe_unpack32(&launch_msg_ptr->uid, buffer);
		safe_unpack32(&launch_msg_ptr->gid, buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->alias_list, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->nodes, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->partition, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_err, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_out, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->work_dir, &uint32_tmp,
				       buffer);

		safe_unpack16(&launch_msg_ptr->x11, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->x11_alloc_host,
				       &uint32_tmp, buffer);
		safe_unpack16(&launch_msg_ptr->x11_alloc_port, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->x11_magic_cookie,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->x11_target,
				       &uint32_tmp, buffer);
		safe_unpack16(&launch_msg_ptr->x11_target_port, buffer);

		safe_unpackstr_array(&launch_msg_ptr->spank_job_env,
				     &launch_msg_ptr->spank_job_env_size,
				     buffer);
		if (!(launch_msg_ptr->cred = slurm_cred_unpack(buffer,
							       protocol_version)))
			goto unpack_error;

		safe_unpackstr_xmalloc(&launch_msg_ptr->user_name, &uint32_tmp,
				       buffer);
	} else
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_prolog_launch_msg(launch_msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_complete_batch_script_msg(
	complete_batch_script_msg_t * msg, buf_t *buffer,
	uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		jobacctinfo_pack(msg->jobacct, protocol_version,
				 PROTOCOL_TYPE_SLURM, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->job_rc, buffer);
		pack32(msg->slurm_rc, buffer);
		pack32(msg->user_id, buffer);
		packstr(msg->node_name, buffer);
	}
}

static int
_unpack_complete_batch_script_msg(
	complete_batch_script_msg_t ** msg_ptr, buf_t *buffer,
	uint16_t protocol_version)
{
	complete_batch_script_msg_t *msg;
	uint32_t uint32_tmp;

	msg = xmalloc(sizeof(complete_batch_script_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (jobacctinfo_unpack(&msg->jobacct, protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->job_rc, buffer);
		safe_unpack32(&msg->slurm_rc, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
	} else {
		error("_unpack_complete_batch_script_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_complete_batch_script_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_step_stat(job_step_stat_t * msg, buf_t *buffer,
		    uint16_t protocol_version)
{
	pack32((uint32_t)msg->return_code, buffer);
	pack32((uint32_t)msg->num_tasks, buffer);
	jobacctinfo_pack(msg->jobacct, protocol_version,
			 PROTOCOL_TYPE_SLURM, buffer);
	_pack_job_step_pids(msg->step_pids, buffer, protocol_version);
}


static int
_unpack_job_step_stat(job_step_stat_t ** msg_ptr, buf_t *buffer,
		      uint16_t protocol_version)
{
	job_step_stat_t *msg;
	int rc = SLURM_SUCCESS;

	msg = xmalloc(sizeof(job_step_stat_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->return_code, buffer);
	safe_unpack32(&msg->num_tasks, buffer);
	if (jobacctinfo_unpack(&msg->jobacct, protocol_version,
			       PROTOCOL_TYPE_SLURM, buffer, 1)
	    != SLURM_SUCCESS)
		goto unpack_error;
	rc = _unpack_job_step_pids(&msg->step_pids, buffer, protocol_version);

	return rc;

unpack_error:
	slurm_free_job_step_stat(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_step_pids(job_step_pids_t *msg, buf_t *buffer,
		    uint16_t protocol_version)
{
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
	uint32_t uint32_tmp;

	msg = xmalloc(sizeof(job_step_pids_t));
	*msg_ptr = msg;

	safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
	safe_unpack32_array(&msg->pid, &msg->pid_cnt, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_pids(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_step_complete_msg(step_complete_msg_t * msg, buf_t *buffer,
			uint16_t protocol_version)
{
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32((uint32_t)msg->range_first, buffer);
		pack32((uint32_t)msg->range_last, buffer);
		pack32((uint32_t)msg->step_rc, buffer);
		jobacctinfo_pack(msg->jobacct, protocol_version,
				 PROTOCOL_TYPE_SLURM, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack32((uint32_t)msg->range_first, buffer);
		pack32((uint32_t)msg->range_last, buffer);
		pack32((uint32_t)msg->step_rc, buffer);
		jobacctinfo_pack(msg->jobacct, protocol_version,
				 PROTOCOL_TYPE_SLURM, buffer);
	}
}

static int
_unpack_step_complete_msg(step_complete_msg_t ** msg_ptr, buf_t *buffer,
			  uint16_t protocol_version)
{
	step_complete_msg_t *msg;

	msg = xmalloc(sizeof(step_complete_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg->range_first, buffer);
		safe_unpack32(&msg->range_last, buffer);
		safe_unpack32(&msg->step_rc, buffer);
		if (jobacctinfo_unpack(&msg->jobacct, protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg->range_first, buffer);
		safe_unpack32(&msg->range_last, buffer);
		safe_unpack32(&msg->step_rc, buffer);
		if (jobacctinfo_unpack(&msg->jobacct, protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_step_complete_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_info_request_msg(job_info_request_msg_t * msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	ListIterator itr;

	xassert(msg);
	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);
		pack16((uint16_t)msg->show_flags, buffer);

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

static int
_unpack_job_info_request_msg(job_info_request_msg_t** msg,
			     buf_t *buffer,
			     uint16_t protocol_version)
{
	int       i;
	uint32_t  count;
	uint32_t *uint32_ptr = NULL;
	job_info_request_msg_t *job_info;

	job_info = xmalloc(sizeof(job_info_request_msg_t));
	*msg = job_info;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&job_info->last_update, buffer);
		safe_unpack16(&job_info->show_flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			job_info->job_ids = list_create(xfree_ptr);
			for (i = 0; i < count; i++) {
				uint32_ptr = xmalloc(sizeof(uint32_t));
				safe_unpack32(uint32_ptr, buffer);
				list_append(job_info->job_ids, uint32_ptr);
				uint32_ptr = NULL;
			}
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	xfree(uint32_ptr);
	slurm_free_job_info_request_msg(job_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static int _unpack_burst_buffer_info_msg(
	burst_buffer_info_msg_t **burst_buffer_info, buf_t *buffer,
	uint16_t protocol_version)
{
	int i, j;
	burst_buffer_info_msg_t *bb_msg_ptr = NULL;
	burst_buffer_info_t *bb_info_ptr;
	burst_buffer_resv_t *bb_resv_ptr;
	burst_buffer_use_t  *bb_use_ptr;
	uint32_t uint32_tmp;

	bb_msg_ptr = xmalloc(sizeof(burst_buffer_info_msg_t));
	safe_unpack32(&bb_msg_ptr->record_count, buffer);
	if (bb_msg_ptr->record_count >= NO_VAL)
		goto unpack_error;
	safe_xcalloc(bb_msg_ptr->burst_buffer_array, bb_msg_ptr->record_count,
		     sizeof(burst_buffer_info_t));
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		for (i = 0, bb_info_ptr = bb_msg_ptr->burst_buffer_array;
		     i < bb_msg_ptr->record_count; i++, bb_info_ptr++) {
			safe_unpackstr_xmalloc(&bb_info_ptr->name, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->allow_users,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->create_buffer,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->default_pool,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->deny_users,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->destroy_buffer,
					       &uint32_tmp, buffer);
			safe_unpack32(&bb_info_ptr->flags, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->get_sys_state,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->get_sys_status,
					       &uint32_tmp, buffer);
			safe_unpack64(&bb_info_ptr->granularity, buffer);
			safe_unpack32(&bb_info_ptr->pool_cnt, buffer);
			if (bb_info_ptr->pool_cnt >= NO_VAL)
				goto unpack_error;
			safe_xcalloc(bb_info_ptr->pool_ptr,
				     bb_info_ptr->pool_cnt,
				     sizeof(burst_buffer_pool_t));
			for (j = 0; j < bb_info_ptr->pool_cnt; j++) {
				safe_unpackstr_xmalloc(
					&bb_info_ptr->pool_ptr[j].name,
					&uint32_tmp, buffer);
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
			safe_unpack32(&bb_info_ptr->other_timeout, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->start_stage_in,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->start_stage_out,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->stop_stage_in,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&bb_info_ptr->stop_stage_out,
					       &uint32_tmp, buffer);
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
				safe_unpackstr_xmalloc(&bb_resv_ptr->account,
						       &uint32_tmp, buffer);
				safe_unpack32(&bb_resv_ptr->array_job_id,
					      buffer);
				safe_unpack32(&bb_resv_ptr->array_task_id,
					      buffer);
				safe_unpack_time(&bb_resv_ptr->create_time,
						 buffer);
				safe_unpack32(&bb_resv_ptr->job_id, buffer);
				safe_unpackstr_xmalloc(&bb_resv_ptr->name,
						       &uint32_tmp, buffer);
				safe_unpackstr_xmalloc(&bb_resv_ptr->partition,
						       &uint32_tmp, buffer);
				safe_unpackstr_xmalloc(&bb_resv_ptr->pool,
						       &uint32_tmp, buffer);
				safe_unpackstr_xmalloc(&bb_resv_ptr->qos,
						       &uint32_tmp, buffer);
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

	*burst_buffer_info = bb_msg_ptr;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_burst_buffer_info_msg(bb_msg_ptr);
	*burst_buffer_info = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_step_info_req_msg(job_step_info_request_msg_t * msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack16((uint16_t)msg->show_flags, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack16((uint16_t)msg->show_flags, buffer);
	}
}

static int
_unpack_job_step_info_req_msg(job_step_info_request_msg_t ** msg, buf_t *buffer,
			      uint16_t protocol_version)
{
	job_step_info_request_msg_t *job_step_info;

	job_step_info = xmalloc(sizeof(job_step_info_request_msg_t));
	*msg = job_step_info;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack_time(&job_step_info->last_update, buffer);
		if (unpack_step_id_members(&job_step_info->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack16(&job_step_info->show_flags, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&job_step_info->last_update, buffer);
		safe_unpack32(&job_step_info->step_id.job_id, buffer);
		safe_unpack32(&job_step_info->step_id.step_id, buffer);
		job_step_info->step_id.step_het_comp = NO_VAL;
		safe_unpack16(&job_step_info->show_flags, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_info_request_msg(job_step_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_node_info_request_msg(node_info_request_msg_t * msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
	pack16(msg->show_flags, buffer);
}

static int
_unpack_node_info_request_msg(node_info_request_msg_t ** msg, buf_t *buffer,
			      uint16_t protocol_version)
{
	node_info_request_msg_t* node_info;

	node_info = xmalloc(sizeof(node_info_request_msg_t));
	*msg = node_info;

	safe_unpack_time(&node_info->last_update, buffer);
	safe_unpack16(&node_info->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_request_msg(node_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_node_info_single_msg(node_info_single_msg_t * msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	packstr(msg->node_name, buffer);
	pack16(msg->show_flags, buffer);
}

static int
_unpack_node_info_single_msg(node_info_single_msg_t ** msg, buf_t *buffer,
			     uint16_t protocol_version)
{
	node_info_single_msg_t* node_info;
	uint32_t uint32_tmp;

	node_info = xmalloc(sizeof(node_info_single_msg_t));
	*msg = node_info;

	safe_unpackstr_xmalloc(&node_info->node_name, &uint32_tmp, buffer);
	safe_unpack16(&node_info->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_single_msg(node_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_front_end_info_request_msg(front_end_info_request_msg_t * msg,
				 buf_t *buffer, uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
}

static int
_unpack_front_end_info_request_msg(front_end_info_request_msg_t ** msg,
				   buf_t *buffer, uint16_t protocol_version)
{
	front_end_info_request_msg_t* front_end_info;

	front_end_info = xmalloc(sizeof(front_end_info_request_msg_t));
	*msg = front_end_info;

	safe_unpack_time(&front_end_info->last_update, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_front_end_info_request_msg(front_end_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_front_end_info_msg(front_end_info_msg_t ** msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	int i;
	front_end_info_t *front_end = NULL;

	xassert(msg);
	*msg = xmalloc(sizeof(front_end_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);
		safe_xcalloc(front_end, (*msg)->record_count,
			     sizeof(front_end_info_t));
		(*msg)->front_end_array = front_end;

		/* load individual front_end info */
		for (i = 0; i < (*msg)->record_count; i++) {
			if (_unpack_front_end_info_members(&front_end[i],
							   buffer,
							   protocol_version))
				goto unpack_error;
		}
	} else {
		error("_unpack_front_end_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_front_end_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_front_end_info_members(front_end_info_t *front_end, buf_t *buffer,
			       uint16_t protocol_version)
{
	uint32_t uint32_tmp;

	xassert(front_end);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&front_end->allow_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->allow_users, &uint32_tmp,
				       buffer);
		safe_unpack_time(&front_end->boot_time, buffer);
		safe_unpackstr_xmalloc(&front_end->deny_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->deny_users, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->name, &uint32_tmp, buffer);
		safe_unpack32(&front_end->node_state, buffer);
		safe_unpackstr_xmalloc(&front_end->version, &uint32_tmp,
				       buffer);

		safe_unpackstr_xmalloc(&front_end->reason, &uint32_tmp, buffer);
		safe_unpack_time(&front_end->reason_time, buffer);
		safe_unpack32(&front_end->reason_uid, buffer);

		safe_unpack_time(&front_end->slurmd_start_time, buffer);

	} else {
		error("_unpack_front_end_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_front_end_info_members(front_end);
	return SLURM_ERROR;
}

static void
_pack_part_info_request_msg(part_info_request_msg_t * msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
	pack16((uint16_t)msg->show_flags, buffer);
}

static int
_unpack_part_info_request_msg(part_info_request_msg_t ** msg, buf_t *buffer,
			      uint16_t protocol_version)
{
	part_info_request_msg_t* part_info;

	part_info = xmalloc(sizeof(part_info_request_msg_t));
	*msg = part_info;

	safe_unpack_time(&part_info->last_update, buffer);
	safe_unpack16(&part_info->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_part_info_request_msg(part_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_resv_info_request_msg(resv_info_request_msg_t * msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
}

static int
_unpack_resv_info_request_msg(resv_info_request_msg_t ** msg, buf_t *buffer,
			      uint16_t protocol_version)
{
	resv_info_request_msg_t* resv_info;

	resv_info = xmalloc(sizeof(resv_info_request_msg_t));
	*msg = resv_info;

	safe_unpack_time(&resv_info->last_update, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_info_request_msg(resv_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_ret_list(List ret_list,
	       uint16_t size_val, buf_t *buffer,
	       uint16_t protocol_version)
{
	ListIterator itr;
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

static int
_unpack_ret_list(List *ret_list,
		 uint16_t size_val, buf_t *buffer,
		 uint16_t protocol_version)
{
	int i = 0;
	uint32_t uint32_tmp;
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
		safe_unpackstr_xmalloc(&ret_data_info->node_name,
				       &uint32_tmp, buffer);
		msg.msg_type = ret_data_info->type;
		if (unpack_msg(&msg, buffer) != SLURM_SUCCESS)
			goto unpack_error;
		ret_data_info->data = msg.data;
	}

	return SLURM_SUCCESS;

unpack_error:
	if (ret_data_info && ret_data_info->type) {
		error("_unpack_ret_list: message type %u, record %d of %u",
		      ret_data_info->type, i, size_val);
	}
	FREE_NULL_LIST(*ret_list);
	*ret_list = NULL;
	return SLURM_ERROR;
}

static void
_pack_batch_job_launch_msg(batch_job_launch_msg_t * msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	xassert(msg);

	if (msg->script_buf)
		msg->script = msg->script_buf->head;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->het_job_id, buffer);

		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->user_name, buffer);
		pack32_array(msg->gids, msg->ngids, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->ntasks, buffer);
		pack64(msg->pn_min_memory, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);

		pack32(msg->array_job_id,   buffer);
		pack32(msg->array_task_id,  buffer);

		packstr(msg->acctg_freq,     buffer);
		packstr(msg->container, buffer);
		pack16(msg->cpu_bind_type,  buffer);
		pack16(msg->cpus_per_task,  buffer);
		pack16(msg->restart_cnt,    buffer);
		pack16(msg->job_core_spec,  buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		packstr(msg->alias_list, buffer);
		packstr(msg->cpu_bind, buffer);
		packstr(msg->nodes,    buffer);
		packstr(msg->script,   buffer);
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

		slurm_cred_pack(msg->cred, buffer, protocol_version);

		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);

		packstr(msg->account, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->het_job_id, buffer);

		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->user_name, buffer);
		pack32_array(msg->gids, msg->ngids, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->ntasks, buffer);
		pack64(msg->pn_min_memory, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);

		pack32(msg->array_job_id,   buffer);
		pack32(msg->array_task_id,  buffer);

		packstr(msg->acctg_freq,     buffer);
		pack16(msg->cpu_bind_type,  buffer);
		pack16(msg->cpus_per_task,  buffer);
		pack16(msg->restart_cnt,    buffer);
		pack16(msg->job_core_spec,  buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		packstr(msg->alias_list, buffer);
		packstr(msg->cpu_bind, buffer);
		packstr(msg->nodes,    buffer);
		packstr(msg->script,   buffer);
		packstr(msg->work_dir, buffer);
		packnull(buffer); /* was ckpt_dir */
		packnull(buffer); /* was restart_dir */

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

		slurm_cred_pack(msg->cred, buffer, protocol_version);

		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);

		packstr(msg->account, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->het_job_id, buffer);
		pack_old_step_id(SLURM_BATCH_SCRIPT, buffer);

		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->user_name, buffer);
		pack32_array(msg->gids, msg->ngids, buffer);

		packstr(msg->partition, buffer);
		pack32(msg->ntasks, buffer);
		pack64(msg->pn_min_memory, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);

		pack32(msg->array_job_id,   buffer);
		pack32(msg->array_task_id,  buffer);

		packstr(msg->acctg_freq,     buffer);
		pack16(msg->cpu_bind_type,  buffer);
		pack16(msg->cpus_per_task,  buffer);
		pack16(msg->restart_cnt,    buffer);
		pack16(msg->job_core_spec,  buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		packstr(msg->alias_list, buffer);
		packstr(msg->cpu_bind, buffer);
		packstr(msg->nodes,    buffer);
		packstr(msg->script,   buffer);
		packstr(msg->work_dir, buffer);
		packnull(buffer); /* was ckpt_dir */
		packnull(buffer); /* was restart_dir */

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

		slurm_cred_pack(msg->cred, buffer, protocol_version);

		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);

		packstr(msg->account, buffer);
		packstr(msg->qos, buffer);
		packstr(msg->resv_name, buffer);
		pack32(msg->profile, buffer);
		packstr(msg->tres_bind, buffer);
		packstr(msg->tres_freq, buffer);
	}

	if (msg->script_buf)
		msg->script = NULL;
}

static int
_unpack_batch_job_launch_msg(batch_job_launch_msg_t ** msg, buf_t *buffer,
			     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	batch_job_launch_msg_t *launch_msg_ptr;

	xassert(msg);
	launch_msg_ptr = xmalloc(sizeof(batch_job_launch_msg_t));
	*msg = launch_msg_ptr;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpack32(&launch_msg_ptr->job_id, buffer);
		safe_unpack32(&launch_msg_ptr->het_job_id, buffer);
		safe_unpack32(&launch_msg_ptr->uid, buffer);
		safe_unpack32(&launch_msg_ptr->gid, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->user_name,
				       &uint32_tmp, buffer);
		safe_unpack32_array(&launch_msg_ptr->gids,
				    &launch_msg_ptr->ngids, buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&launch_msg_ptr->ntasks, buffer);
		safe_unpack64(&launch_msg_ptr->pn_min_memory, buffer);

		safe_unpack8(&launch_msg_ptr->open_mode, buffer);
		safe_unpack8(&launch_msg_ptr->overcommit, buffer);

		safe_unpack32(&launch_msg_ptr->array_job_id,   buffer);
		safe_unpack32(&launch_msg_ptr->array_task_id,  buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->container, &uint32_tmp,
				       buffer);
		safe_unpack16(&launch_msg_ptr->cpu_bind_type,  buffer);
		safe_unpack16(&launch_msg_ptr->cpus_per_task,  buffer);
		safe_unpack16(&launch_msg_ptr->restart_cnt,    buffer);
		safe_unpack16(&launch_msg_ptr->job_core_spec,  buffer);

		safe_unpack32(&launch_msg_ptr->num_cpu_groups, buffer);
		if (launch_msg_ptr->num_cpu_groups) {
			safe_unpack16_array(&(launch_msg_ptr->cpus_per_node),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&(launch_msg_ptr->cpu_count_reps),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}

		safe_unpackstr_xmalloc(&launch_msg_ptr->alias_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->cpu_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->nodes,    &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->script,   &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->work_dir, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_err, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_in,  &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_out, &uint32_tmp,
				       buffer);

		safe_unpack32(&launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->argv,
				     &launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->spank_job_env,
				     &launch_msg_ptr->spank_job_env_size,
				     buffer);

		safe_unpack32(&launch_msg_ptr->envc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->environment,
				     &launch_msg_ptr->envc, buffer);

		safe_unpack64(&launch_msg_ptr->job_mem, buffer);

		if (!(launch_msg_ptr->cred = slurm_cred_unpack(
			      buffer, protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&launch_msg_ptr->
						   select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		safe_unpackstr_xmalloc(&launch_msg_ptr->account,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->qos,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->resv_name,
				       &uint32_tmp,
				       buffer);
		safe_unpack32(&launch_msg_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->tres_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->tres_freq, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		char *temp_str;

		safe_unpack32(&launch_msg_ptr->job_id, buffer);
		safe_unpack32(&launch_msg_ptr->het_job_id, buffer);
		safe_unpack32(&launch_msg_ptr->uid, buffer);
		safe_unpack32(&launch_msg_ptr->gid, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->user_name,
				       &uint32_tmp, buffer);
		safe_unpack32_array(&launch_msg_ptr->gids,
				    &launch_msg_ptr->ngids, buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&launch_msg_ptr->ntasks, buffer);
		safe_unpack64(&launch_msg_ptr->pn_min_memory, buffer);

		safe_unpack8(&launch_msg_ptr->open_mode, buffer);
		safe_unpack8(&launch_msg_ptr->overcommit, buffer);

		safe_unpack32(&launch_msg_ptr->array_job_id,   buffer);
		safe_unpack32(&launch_msg_ptr->array_task_id,  buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack16(&launch_msg_ptr->cpu_bind_type,  buffer);
		safe_unpack16(&launch_msg_ptr->cpus_per_task,  buffer);
		safe_unpack16(&launch_msg_ptr->restart_cnt,    buffer);
		safe_unpack16(&launch_msg_ptr->job_core_spec,  buffer);

		safe_unpack32(&launch_msg_ptr->num_cpu_groups, buffer);
		if (launch_msg_ptr->num_cpu_groups) {
			safe_unpack16_array(&(launch_msg_ptr->cpus_per_node),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&(launch_msg_ptr->cpu_count_reps),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}

		safe_unpackstr_xmalloc(&launch_msg_ptr->alias_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->cpu_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->nodes,    &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->script,   &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->work_dir, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was ckpt_dir */
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was restart_dir */

		safe_unpackstr_xmalloc(&launch_msg_ptr->std_err, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_in,  &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_out, &uint32_tmp,
				       buffer);

		safe_unpack32(&launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->argv,
				     &launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->spank_job_env,
				     &launch_msg_ptr->spank_job_env_size,
				     buffer);

		safe_unpack32(&launch_msg_ptr->envc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->environment,
				     &launch_msg_ptr->envc, buffer);

		safe_unpack64(&launch_msg_ptr->job_mem, buffer);

		if (!(launch_msg_ptr->cred = slurm_cred_unpack(
			      buffer, protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&launch_msg_ptr->
						   select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		safe_unpackstr_xmalloc(&launch_msg_ptr->account,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->qos,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->resv_name,
				       &uint32_tmp,
				       buffer);
		safe_unpack32(&launch_msg_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->tres_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->tres_freq, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *temp_str;

		safe_unpack32(&launch_msg_ptr->job_id, buffer);
		safe_unpack32(&launch_msg_ptr->het_job_id, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		safe_unpack32(&launch_msg_ptr->uid, buffer);
		safe_unpack32(&launch_msg_ptr->gid, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->user_name,
				       &uint32_tmp, buffer);
		safe_unpack32_array(&launch_msg_ptr->gids,
				    &launch_msg_ptr->ngids, buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&launch_msg_ptr->ntasks, buffer);
		safe_unpack64(&launch_msg_ptr->pn_min_memory, buffer);

		safe_unpack8(&launch_msg_ptr->open_mode, buffer);
		safe_unpack8(&launch_msg_ptr->overcommit, buffer);

		safe_unpack32(&launch_msg_ptr->array_job_id,   buffer);
		safe_unpack32(&launch_msg_ptr->array_task_id,  buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack16(&launch_msg_ptr->cpu_bind_type,  buffer);
		safe_unpack16(&launch_msg_ptr->cpus_per_task,  buffer);
		safe_unpack16(&launch_msg_ptr->restart_cnt,    buffer);
		safe_unpack16(&launch_msg_ptr->job_core_spec,  buffer);

		safe_unpack32(&launch_msg_ptr->num_cpu_groups, buffer);
		if (launch_msg_ptr->num_cpu_groups) {
			safe_unpack16_array(&(launch_msg_ptr->cpus_per_node),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&(launch_msg_ptr->cpu_count_reps),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}

		safe_unpackstr_xmalloc(&launch_msg_ptr->alias_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->cpu_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->nodes,    &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->script,   &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->work_dir, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was ckpt_dir */
		safe_unpackstr_xmalloc(&temp_str, &uint32_tmp, buffer);
		xfree(temp_str); /* was restart_dir */

		safe_unpackstr_xmalloc(&launch_msg_ptr->std_err, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_in,  &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_out, &uint32_tmp,
				       buffer);

		safe_unpack32(&launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->argv,
				     &launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->spank_job_env,
				     &launch_msg_ptr->spank_job_env_size,
				     buffer);

		safe_unpack32(&launch_msg_ptr->envc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->environment,
				     &launch_msg_ptr->envc, buffer);

		safe_unpack64(&launch_msg_ptr->job_mem, buffer);

		if (!(launch_msg_ptr->cred = slurm_cred_unpack(
			      buffer, protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&launch_msg_ptr->
						   select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		safe_unpackstr_xmalloc(&launch_msg_ptr->account,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->qos,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->resv_name,
				       &uint32_tmp,
				       buffer);
		safe_unpack32(&launch_msg_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->tres_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->tres_freq, &uint32_tmp,
				       buffer);
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_launch_msg(launch_msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_id_request_msg(job_id_request_msg_t * msg, buf_t *buffer,
			 uint16_t protocol_version)
{
	xassert(msg);

	pack32((uint32_t)msg->job_pid, buffer);
}

static int
_unpack_job_id_request_msg(job_id_request_msg_t ** msg, buf_t *buffer,
			   uint16_t protocol_version)
{
	job_id_request_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(job_id_request_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	safe_unpack32(&tmp_ptr->job_pid, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_id_request_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_id_response_msg(job_id_response_msg_t * msg, buf_t *buffer,
			  uint16_t protocol_version)
{
	xassert(msg);

	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->return_code, buffer);
}

static int
_unpack_job_id_response_msg(job_id_response_msg_t ** msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	job_id_response_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(job_id_response_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	safe_unpack32(&tmp_ptr->job_id, buffer);
	safe_unpack32(&tmp_ptr->return_code, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_id_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_config_request_msg(config_request_msg_t *msg,
				     buf_t *buffer, uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->flags, buffer);
	}
}

static int _unpack_config_request_msg(config_request_msg_t **msg_ptr,
				      buf_t *buffer, uint16_t protocol_version)
{
	config_request_msg_t *msg = xmalloc(sizeof(*msg));
	xassert(msg_ptr);
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->flags, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_config_request_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

extern void pack_config_file(void *in, uint16_t protocol_version,
			     buf_t *buffer)
{
	config_file_t *object = (config_file_t *) in;

	if (!object) {
		packbool(0, buffer);
		packnull(buffer);
		packnull(buffer);
		return;
	}

	packbool(object->exists, buffer);
	packstr(object->file_name, buffer);
	packstr(object->file_content, buffer);
}

extern int unpack_config_file(void **out, uint16_t protocol_version,
			      buf_t *buffer)
{
	uint32_t uint32_tmp;
	config_file_t *object = xmalloc(sizeof(*object));

	safe_unpackbool(&object->exists, buffer);
	safe_unpackstr_xmalloc(&object->file_name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object->file_content, &uint32_tmp, buffer);
	*out = object;
	return SLURM_SUCCESS;

unpack_error:
	xfree(object);
	*out = NULL;
	return SLURM_ERROR;
}

extern void pack_config_response_msg(config_response_msg_t *msg,
				     buf_t *buffer, uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		slurm_pack_list(msg->config_files, pack_config_file, buffer,
				protocol_version);
		packstr(msg->slurmd_spooldir, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->config, buffer);
		packstr(msg->acct_gather_config, buffer);
		packstr(msg->cgroup_config, buffer);
		packstr(msg->cgroup_allowed_devices_file_config, buffer);
		packstr(msg->ext_sensors_config, buffer);
		packstr(msg->gres_config, buffer);
		packstr(msg->knl_cray_config, buffer);
		packstr(msg->knl_generic_config, buffer);
		packstr(msg->plugstack_config, buffer);
		packstr(msg->topology_config, buffer);
		packstr(msg->job_container_config, buffer);
		packstr(msg->slurmd_spooldir, buffer);
	}
}

extern int unpack_config_response_msg(config_response_msg_t **msg_ptr,
				      buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	config_response_msg_t *msg = xmalloc(sizeof(*msg));
	xassert(msg_ptr);
	*msg_ptr = msg;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		if (slurm_unpack_list(&msg->config_files, unpack_config_file,
				      destroy_config_file, buffer,
				      protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->slurmd_spooldir,
				       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->config, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->acct_gather_config, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->cgroup_config, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->cgroup_allowed_devices_file_config,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->ext_sensors_config, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->gres_config, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->knl_cray_config, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->knl_generic_config, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->plugstack_config, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->topology_config, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->job_container_config, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->slurmd_spooldir, &uint32_tmp,
				       buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_config_response_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_srun_exec_msg(srun_exec_msg_t * msg, buf_t *buffer,
		    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr_array(msg->argv, msg->argc, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr_array(msg->argv, msg->argc, buffer);
	}
}

static int
_unpack_srun_exec_msg(srun_exec_msg_t ** msg_ptr, buf_t *buffer,
		      uint16_t protocol_version)
{
	srun_exec_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (srun_exec_msg_t) ) ;
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_exec_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_net_forward_msg(net_forward_msg_t *msg,
				  buf_t *buffer,
				  uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->flags, buffer);
		pack16(msg->port, buffer);
		packstr(msg->target, buffer);
	}
}

static int _unpack_net_forward_msg(net_forward_msg_t **msg_ptr,
				   buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	net_forward_msg_t *msg = xmalloc(sizeof(*msg));
	xassert(msg_ptr);
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->flags, buffer);
		safe_unpack16(&msg->port, buffer);
		safe_unpackstr_xmalloc(&msg->target, &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_net_forward_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_srun_ping_msg(srun_ping_msg_t * msg, buf_t *buffer,
		    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		/* empty, nothing needs to be sent */
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(NO_VAL, buffer);
	}
}

static int
_unpack_srun_ping_msg(srun_ping_msg_t ** msg_ptr, buf_t *buffer,
		      uint16_t protocol_version)
{
	xassert(msg_ptr);

	*msg_ptr = NULL;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		/* empty, nothing is sent */
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t throw_away;
		safe_unpack32(&throw_away, buffer);
		safe_unpack32(&throw_away, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_srun_node_fail_msg(srun_node_fail_msg_t * msg, buf_t *buffer,
			 uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr(msg->nodelist, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr(msg->nodelist, buffer);
	}
}

static int
_unpack_srun_node_fail_msg(srun_node_fail_msg_t ** msg_ptr, buf_t *buffer,
			   uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	srun_node_fail_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (srun_node_fail_msg_t) ) ;
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->nodelist, &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->nodelist, &uint32_tmp, buffer);
	} else  {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_node_fail_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_srun_step_missing_msg(srun_step_missing_msg_t * msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr(msg->nodelist, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr(msg->nodelist, buffer);
	}
}

static int
_unpack_srun_step_missing_msg(srun_step_missing_msg_t ** msg_ptr, buf_t *buffer,
			      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	srun_step_missing_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (srun_step_missing_msg_t) ) ;
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->nodelist, &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->nodelist, &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_step_missing_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_ready_msg(job_id_msg_t * msg, buf_t *buffer,
		    uint16_t protocol_version)
{
	xassert(msg);

	pack32(msg->job_id  , buffer ) ;
	pack16(msg->show_flags, buffer);
}

static int
_unpack_job_ready_msg(job_id_msg_t ** msg_ptr, buf_t *buffer,
		      uint16_t protocol_version)
{
	job_id_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (job_id_msg_t) );
	*msg_ptr = msg ;

	safe_unpack32(&msg->job_id  , buffer ) ;
	safe_unpack16(&msg->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_job_id_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_job_requeue_msg(requeue_msg_t *msg, buf_t *buf, uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buf);
		packstr(msg->job_id_str, buf);
		pack32(msg->flags, buf);
	}
}

static int
_unpack_job_requeue_msg(requeue_msg_t **msg, buf_t *buf, uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	*msg = xmalloc(sizeof(requeue_msg_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&(*msg)->job_id, buf);
		safe_unpackstr_xmalloc(&(*msg)->job_id_str, &uint32_tmp, buf);
		safe_unpack32(&(*msg)->flags, buf);
	}

	return SLURM_SUCCESS;
unpack_error:
	slurm_free_requeue_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_user_msg(job_user_id_msg_t * msg, buf_t *buffer,
		   uint16_t protocol_version)
{
	xassert(msg);

	pack32(msg->user_id  , buffer ) ;
	pack16(msg->show_flags, buffer);
}

static int
_unpack_job_user_msg(job_user_id_msg_t ** msg_ptr, buf_t *buffer,
		     uint16_t protocol_version)
{
	job_user_id_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (job_user_id_msg_t) );
	*msg_ptr = msg ;

	safe_unpack32(&msg->user_id  , buffer ) ;
	safe_unpack16(&msg->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_job_user_id_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_srun_timeout_msg(srun_timeout_msg_t * msg, buf_t *buffer,
		       uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack_time(msg->timeout, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		pack_time(msg->timeout, buffer);
	}
}

static int
_unpack_srun_timeout_msg(srun_timeout_msg_t ** msg_ptr, buf_t *buffer,
			 uint16_t protocol_version)
{
	srun_timeout_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (srun_timeout_msg_t) ) ;
	*msg_ptr = msg ;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack_time(&msg->timeout, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack_time(&msg->timeout, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_srun_timeout_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_srun_user_msg(srun_user_msg_t * msg, buf_t *buffer,
		    uint16_t protocol_version)
{
	xassert(msg);

	pack32((uint32_t)msg->job_id,  buffer);
	packstr(msg->msg, buffer);
}

static int
_unpack_srun_user_msg(srun_user_msg_t ** msg_ptr, buf_t *buffer,
		      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	srun_user_msg_t * msg_user;
	xassert(msg_ptr);

	msg_user = xmalloc(sizeof (srun_user_msg_t)) ;
	*msg_ptr = msg_user;

	safe_unpack32(&msg_user->job_id, buffer);
	safe_unpackstr_xmalloc(&msg_user->msg, &uint32_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_user_msg(msg_user);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_suspend_msg(suspend_msg_t *msg, buf_t *buffer,
			      uint16_t protocol_version)
{
	xassert(msg);
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg -> op, buffer);
		pack32(msg->job_id,  buffer);
		packstr(msg->job_id_str, buffer);
	}
}

static int  _unpack_suspend_msg(suspend_msg_t **msg_ptr, buf_t *buffer,
				uint16_t protocol_version)
{
	suspend_msg_t * msg;
	uint32_t uint32_tmp = 0;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (suspend_msg_t) );
	*msg_ptr = msg ;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->op,      buffer);
		safe_unpack32(&msg->job_id , buffer);
		safe_unpackstr_xmalloc(&msg->job_id_str, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_suspend_msg(msg);
	return SLURM_ERROR;
}

static void _pack_suspend_int_msg(suspend_int_msg_t *msg, buf_t *buffer,
				  uint16_t protocol_version)
{
	xassert(msg);
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack8(msg->indf_susp, buffer);
		pack16(msg->job_core_spec, buffer);
		pack32(msg->job_id,  buffer);
		pack16(msg->op, buffer);
		switch_g_job_suspend_info_pack(msg->switch_info, buffer,
					       protocol_version);
	}
}

static int  _unpack_suspend_int_msg(suspend_int_msg_t **msg_ptr, buf_t *buffer,
				    uint16_t protocol_version)
{
	suspend_int_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (suspend_int_msg_t) );
	*msg_ptr = msg ;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack8(&msg->indf_susp, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack16(&msg->op,     buffer);
		if (switch_g_job_suspend_info_unpack(&msg->switch_info, buffer,
						     protocol_version))
			goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_suspend_int_msg(msg);
	return SLURM_ERROR;
}

static void _pack_top_job_msg(top_job_msg_t *msg, buf_t *buffer,
			      uint16_t protocol_version)
{
	xassert(msg);
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg -> op, buffer);
		pack32(msg->job_id,  buffer);
		packstr(msg->job_id_str, buffer);
	}
}

static int  _unpack_top_job_msg(top_job_msg_t **msg_ptr, buf_t *buffer,
				uint16_t protocol_version)
{
	top_job_msg_t * msg;
	uint32_t uint32_tmp = 0;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (top_job_msg_t) );
	*msg_ptr = msg ;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->op,      buffer);
		safe_unpack32(&msg->job_id , buffer);
		safe_unpackstr_xmalloc(&msg->job_id_str, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_top_job_msg(msg);
	return SLURM_ERROR;
}

static void _pack_token_request_msg(token_request_msg_t *msg, buf_t *buffer,
				    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->lifespan, buffer);
		packstr(msg->username, buffer);
	}
}

static int _unpack_token_request_msg(token_request_msg_t **msg_ptr, buf_t *buffer,
				     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	token_request_msg_t *msg = xmalloc(sizeof(*msg));
	xassert(msg_ptr);
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->lifespan, buffer);
		safe_unpackstr_xmalloc(&msg->username, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_token_request_msg(msg);
	return SLURM_ERROR;
}

static void _pack_token_response_msg(token_response_msg_t *msg, buf_t *buffer,
				     uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->token, buffer);
	}
}

static int _unpack_token_response_msg(token_response_msg_t **msg_ptr,
				      buf_t *buffer,
				      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	token_response_msg_t *msg = xmalloc(sizeof(*msg));
	xassert(msg_ptr);
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->token, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_token_response_msg(msg);
	return SLURM_ERROR;
}

static void _pack_forward_data_msg(forward_data_msg_t *msg,
				   buf_t *buffer, uint16_t protocol_version)
{
	xassert(msg);
	packstr(msg->address, buffer);
	pack32(msg->len, buffer);
	packmem(msg->data, msg->len, buffer);
}

static int _unpack_forward_data_msg(forward_data_msg_t **msg_ptr,
				    buf_t *buffer, uint16_t protocol_version)
{
	forward_data_msg_t *msg;
	uint32_t temp32;

	xassert(msg_ptr);
	msg = xmalloc(sizeof(forward_data_msg_t));
	*msg_ptr = msg;
	safe_unpackstr_xmalloc(&msg->address, &temp32, buffer);
	safe_unpack32(&msg->len, buffer);
	safe_unpackmem_xmalloc(&msg->data, &temp32, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_forward_data_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_ping_slurmd_resp(ping_slurmd_resp_msg_t *msg,
				   buf_t *buffer, uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->cpu_load, buffer);
		pack64(msg->free_mem, buffer);
	}
}

static int _unpack_ping_slurmd_resp(ping_slurmd_resp_msg_t **msg_ptr,
				    buf_t *buffer, uint16_t protocol_version)
{
	ping_slurmd_resp_msg_t *msg;

	xassert(msg_ptr);
	msg = xmalloc(sizeof(ping_slurmd_resp_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->cpu_load, buffer);
		safe_unpack64(&msg->free_mem, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_ping_slurmd_resp(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_file_bcast(file_bcast_msg_t * msg , buf_t *buffer,
			     uint16_t protocol_version)
{
	xassert(msg);

	grow_buf(buffer,  msg->block_len);

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
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
		pack32(msg->block_len, buffer);
		pack32(msg->uncomp_len, buffer);
		pack64(msg->block_offset, buffer);
		pack64(msg->file_size, buffer);
		packmem(msg->block, msg->block_len, buffer);
		pack_sbcast_cred(msg->cred, buffer, protocol_version);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint16_t force = (msg->flags & FILE_BCAST_FORCE) ? 1 : 0;
		uint16_t last_block =
			(msg->flags & FILE_BCAST_LAST_BLOCK) ? 1 : 0;
		pack32(msg->block_no, buffer);
		pack16(msg->compress, buffer);
		pack16(last_block, buffer);
		pack16(force, buffer);
		pack16(msg->modes, buffer);

		pack32(msg->uid, buffer);
		packstr(msg->user_name, buffer);
		pack32(msg->gid, buffer);

		pack_time(msg->atime, buffer);
		pack_time(msg->mtime, buffer);

		packstr(msg->fname, buffer);
		pack32(msg->block_len, buffer);
		pack32(msg->uncomp_len, buffer);
		pack64(msg->block_offset, buffer);
		pack64(msg->file_size, buffer);
		packmem (msg->block, msg->block_len, buffer);
		pack_sbcast_cred(msg->cred, buffer, protocol_version);
	}
}

static int _unpack_file_bcast(file_bcast_msg_t ** msg_ptr , buf_t *buffer,
			      uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	file_bcast_msg_t *msg ;

	xassert(msg_ptr);

	msg = xmalloc ( sizeof (file_bcast_msg_t) ) ;
	*msg_ptr = msg;

	if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpack32(&msg->block_no, buffer);
		safe_unpack16(&msg->compress, buffer);
		safe_unpack16(&msg->flags, buffer);
		safe_unpack16(&msg->modes, buffer);

		safe_unpack32(&msg->uid, buffer);
		safe_unpackstr_xmalloc(&msg->user_name, &uint32_tmp, buffer);
		safe_unpack32(&msg->gid, buffer);

		safe_unpack_time(&msg->atime, buffer);
		safe_unpack_time(&msg->mtime, buffer);

		safe_unpackstr_xmalloc(&msg->fname, &uint32_tmp, buffer);
		safe_unpack32(&msg->block_len, buffer);
		safe_unpack32(&msg->uncomp_len, buffer);
		safe_unpack64(&msg->block_offset, buffer);
		safe_unpack64(&msg->file_size, buffer);
		safe_unpackmem_xmalloc(&msg->block, &uint32_tmp, buffer);
		if (uint32_tmp != msg->block_len)
			goto unpack_error;

		msg->cred = unpack_sbcast_cred(buffer, protocol_version);
		if (msg->cred == NULL)
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint16_t last_block, force;
		safe_unpack32(&msg->block_no, buffer);
		safe_unpack16(&msg->compress, buffer);
		safe_unpack16(&last_block, buffer);
		if (last_block)
			msg->flags |= FILE_BCAST_LAST_BLOCK;
		safe_unpack16(&force, buffer);
		if (force)
			msg->flags |= FILE_BCAST_FORCE;
		safe_unpack16(&msg->modes, buffer);

		safe_unpack32(&msg->uid, buffer);
		safe_unpackstr_xmalloc(&msg->user_name, &uint32_tmp, buffer);
		safe_unpack32 (&msg->gid, buffer);

		safe_unpack_time(&msg->atime, buffer);
		safe_unpack_time(&msg->mtime, buffer);

		safe_unpackstr_xmalloc ( & msg->fname, &uint32_tmp, buffer );
		safe_unpack32(&msg->block_len, buffer);
		safe_unpack32(&msg->uncomp_len, buffer);
		safe_unpack64(&msg->block_offset, buffer);
		safe_unpack64(&msg->file_size, buffer);
		safe_unpackmem_xmalloc ( & msg->block, &uint32_tmp , buffer ) ;
		if ( uint32_tmp != msg->block_len )
			goto unpack_error;

		msg->cred = unpack_sbcast_cred(buffer, protocol_version);
		if (msg->cred == NULL)
			goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_file_bcast_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_trigger_msg(trigger_info_msg_t *msg, buf_t *buffer,
			      uint16_t protocol_version)
{
	int i;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->record_count, buffer);
		for (i = 0; i < msg->record_count; i++) {
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

static int  _unpack_trigger_msg(trigger_info_msg_t ** msg_ptr , buf_t *buffer,
				uint16_t protocol_version)
{
	int i;
	uint32_t uint32_tmp;
	trigger_info_msg_t *msg = xmalloc(sizeof(trigger_info_msg_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->record_count, buffer);
		safe_xcalloc(msg->trigger_array, msg->record_count,
			     sizeof(trigger_info_t));
		for (i = 0; i < msg->record_count; i++) {
			safe_unpack16(&msg->trigger_array[i].flags, buffer);
			safe_unpack32(&msg->trigger_array[i].trig_id, buffer);
			safe_unpack16(&msg->trigger_array[i].res_type, buffer);
			safe_unpackstr_xmalloc(&msg->trigger_array[i].res_id,
					       &uint32_tmp, buffer);
			safe_unpack32(&msg->trigger_array[i].trig_type, buffer);
			safe_unpack32(&msg->trigger_array[i].control_inx, buffer);
			safe_unpack16(&msg->trigger_array[i].offset, buffer);
			safe_unpack32(&msg->trigger_array[i].user_id, buffer);
			safe_unpackstr_xmalloc(&msg->trigger_array[i].program,
					       &uint32_tmp, buffer);
		}
	} else {
		error("_unpack_trigger_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	*msg_ptr = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_trigger_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_kvs_host_rec(struct kvs_hosts *msg_ptr, buf_t *buffer,
			       uint16_t protocol_version)
{
	pack32(msg_ptr->task_id, buffer);
	pack16(msg_ptr->port, buffer);
	packstr(msg_ptr->hostname, buffer);
}

static int _unpack_kvs_host_rec(struct kvs_hosts *msg_ptr, buf_t *buffer,
				uint16_t protocol_version)
{
	uint32_t uint32_tmp;

	safe_unpack32(&msg_ptr->task_id, buffer);
	safe_unpack16(&msg_ptr->port, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->hostname, &uint32_tmp, buffer);
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
	uint32_t uint32_tmp;
	int i;
	struct kvs_comm *msg;

	msg = xmalloc(sizeof(struct kvs_comm));
	*msg_ptr = msg;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->kvs_name, &uint32_tmp, buffer);
		safe_unpack32(&msg->kvs_cnt, buffer);
		if (msg->kvs_cnt > NO_VAL)
			goto unpack_error;
		safe_xcalloc(msg->kvs_keys, msg->kvs_cnt, sizeof(char *));
		safe_xcalloc(msg->kvs_values, msg->kvs_cnt, sizeof(char *));
		for (i = 0; i < msg->kvs_cnt; i++) {
			safe_unpackstr_xmalloc(&msg->kvs_keys[i],
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&msg->kvs_values[i],
					       &uint32_tmp, buffer);
		}
	} else {
		error("_unpack_kvs_rec: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}
static void _pack_kvs_data(kvs_comm_set_t *msg_ptr, buf_t *buffer,
			   uint16_t protocol_version)
{
	int i;
	xassert(msg_ptr);

	pack16(msg_ptr->host_cnt, buffer);
	for (i = 0; i < msg_ptr->host_cnt; i++)
		_pack_kvs_host_rec(&msg_ptr->kvs_host_ptr[i], buffer,
				   protocol_version);

	pack16(msg_ptr->kvs_comm_recs, buffer);
	for (i = 0; i < msg_ptr->kvs_comm_recs; i++)
		_pack_kvs_rec(msg_ptr->kvs_comm_ptr[i], buffer,
			      protocol_version);
}

static int  _unpack_kvs_data(kvs_comm_set_t **msg_ptr, buf_t *buffer,
			     uint16_t protocol_version)
{
	kvs_comm_set_t *msg;
	int i;

	msg = xmalloc(sizeof(kvs_comm_set_t));
	*msg_ptr = msg;

	safe_unpack16(&msg->host_cnt, buffer);
	if (msg->host_cnt > NO_VAL16)
		goto unpack_error;
	safe_xcalloc(msg->kvs_host_ptr, msg->host_cnt,
		     sizeof(struct kvs_hosts));
	for (i = 0; i < msg->host_cnt; i++) {
		if (_unpack_kvs_host_rec(&msg->kvs_host_ptr[i], buffer,
					 protocol_version))
			goto unpack_error;
	}

	safe_unpack16(&msg->kvs_comm_recs, buffer);
	if (msg->kvs_comm_recs > NO_VAL16)
		goto unpack_error;
	safe_xcalloc(msg->kvs_comm_ptr, msg->kvs_comm_recs,
		     sizeof(struct kvs_comm *));
	for (i = 0; i < msg->kvs_comm_recs; i++) {
		if (_unpack_kvs_rec(&msg->kvs_comm_ptr[i], buffer,
				    protocol_version))
			goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_kvs_comm_set(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_kvs_get(kvs_get_msg_t *msg_ptr, buf_t *buffer,
			  uint16_t protocol_version)
{
	pack32((uint32_t)msg_ptr->task_id, buffer);
	pack32((uint32_t)msg_ptr->size, buffer);
	pack16((uint16_t)msg_ptr->port, buffer);
	packstr(msg_ptr->hostname, buffer);
}

static int  _unpack_kvs_get(kvs_get_msg_t **msg_ptr, buf_t *buffer,
			    uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	kvs_get_msg_t *msg;

	msg = xmalloc(sizeof(struct kvs_get_msg));
	*msg_ptr = msg;
	safe_unpack32(&msg->task_id, buffer);
	safe_unpack32(&msg->size, buffer);
	safe_unpack16(&msg->port, buffer);
	safe_unpackstr_xmalloc(&msg->hostname, &uint32_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_get_kvs_msg(msg);
	*msg_ptr = NULL;
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
	multi_core_data_t *multi_core;

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
	} else {
		error("unpack_multi_core_data: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	*mc_ptr = multi_core;
	return SLURM_SUCCESS;

unpack_error:
	xfree(multi_core);
	return SLURM_ERROR;
}

static void _pack_slurmd_status(slurmd_status_t *msg, buf_t *buffer,
				uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

static int _unpack_slurmd_status(slurmd_status_t **msg_ptr, buf_t *buffer,
				 uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	slurmd_status_t *msg;

	xassert(msg_ptr);

	msg = xmalloc(sizeof(slurmd_status_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

		safe_unpackstr_xmalloc(&msg->hostname,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->slurmd_logfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->step_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->version,
				       &uint32_tmp, buffer);
	} else {
		error("_unpack_slurmd_status: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	*msg_ptr = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_slurmd_status(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_job_notify(job_notify_msg_t *msg, buf_t *buffer,
			     uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr(msg->message, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&msg->step_id, buffer, protocol_version);
		packstr(msg->message, buffer);
	}
}

static int  _unpack_job_notify(job_notify_msg_t **msg_ptr, buf_t *buffer,
			       uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_notify_msg_t *msg;

	xassert(msg_ptr);

	msg = xmalloc(sizeof(job_notify_msg_t));

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->message, &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&msg->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg->message, &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	*msg_ptr = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_notify_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_set_debug_flags_msg(set_debug_flags_msg_t * msg, buf_t *buffer,
			  uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack64(msg->debug_flags_minus, buffer);
		pack64(msg->debug_flags_plus,  buffer);
	}
}

static int
_unpack_set_debug_flags_msg(set_debug_flags_msg_t ** msg_ptr, buf_t *buffer,
			    uint16_t protocol_version)
{
	set_debug_flags_msg_t *msg;

	msg = xmalloc(sizeof(set_debug_flags_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64(&msg->debug_flags_minus, buffer);
		safe_unpack64(&msg->debug_flags_plus,  buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_set_debug_flags_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_set_debug_level_msg(set_debug_level_msg_t * msg, buf_t *buffer,
			  uint16_t protocol_version)
{
	pack32(msg->debug_level, buffer);
}

static int
_unpack_set_debug_level_msg(set_debug_level_msg_t ** msg_ptr, buf_t *buffer,
			    uint16_t protocol_version)
{
	set_debug_level_msg_t *msg;

	msg = xmalloc(sizeof(set_debug_level_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->debug_level, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_set_debug_level_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_will_run_response_msg(will_run_response_msg_t *msg, buf_t *buffer,
			    uint16_t protocol_version)
{
	uint32_t count = NO_VAL, *job_id_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		packstr(msg->job_submit_user_msg, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->part_name, buffer);

		if (msg->preemptee_job_id)
			count = list_count(msg->preemptee_job_id);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			ListIterator itr =
				list_iterator_create(msg->preemptee_job_id);
			while ((job_id_ptr = list_next(itr)))
				pack32(job_id_ptr[0], buffer);
			list_iterator_destroy(itr);
		}

		pack32(msg->proc_cnt, buffer);
		pack_time(msg->start_time, buffer);
		packdouble(msg->sys_usage_per, buffer);
	}
}

static int
_unpack_will_run_response_msg(will_run_response_msg_t ** msg_ptr, buf_t *buffer,
			      uint16_t protocol_version)
{
	will_run_response_msg_t *msg;
	uint32_t count, i, uint32_tmp, *job_id_ptr;

	msg = xmalloc(sizeof(will_run_response_msg_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpackstr_xmalloc(&msg->job_submit_user_msg, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&msg->node_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->part_name, &uint32_tmp, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			msg->preemptee_job_id = list_create(xfree_ptr);
			for (i = 0; i < count; i++) {
				safe_unpack32(&uint32_tmp, buffer);
				job_id_ptr = xmalloc(sizeof(uint32_t));
				job_id_ptr[0] = uint32_tmp;
				list_append(msg->preemptee_job_id, job_id_ptr);
			}
		}

		safe_unpack32(&msg->proc_cnt, buffer);
		safe_unpack_time(&msg->start_time, buffer);
		safe_unpackdouble(&msg->sys_usage_per, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	*msg_ptr = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_will_run_response_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_accounting_update_msg(accounting_update_msg_t *msg,
					buf_t *buffer,
					uint16_t protocol_version)
{
	uint32_t count = 0;
	ListIterator itr = NULL;
	slurmdb_update_object_t *rec = NULL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (msg->update_list)
			count = list_count(msg->update_list);

		pack32(count, buffer);

		if (count) {
			itr = list_iterator_create(msg->update_list);
			while ((rec = list_next(itr))) {
				slurmdb_pack_update_object(
					rec, protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
	}
}

static int _unpack_accounting_update_msg(accounting_update_msg_t **msg,
					 buf_t *buffer,
					 uint16_t protocol_version)
{
	uint32_t count = 0;
	int i = 0;
	accounting_update_msg_t *msg_ptr =
		xmalloc(sizeof(accounting_update_msg_t));
	slurmdb_update_object_t *rec = NULL;

	*msg = msg_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		msg_ptr->update_list = list_create(
			slurmdb_destroy_update_object);
		for (i = 0; i < count; i++) {
			if ((slurmdb_unpack_update_object(
				     &rec, protocol_version, buffer))
			    == SLURM_ERROR)
				goto unpack_error;
			list_append(msg_ptr->update_list, rec);
		}
	} else {
		error("_unpack_accounting_update_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_accounting_update_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_topo_info_msg(topo_info_response_msg_t *msg, buf_t *buffer,
				uint16_t protocol_version)
{
	int i;

	pack32(msg->record_count, buffer);
	for (i=0; i<msg->record_count; i++) {
		pack16(msg->topo_array[i].level,      buffer);
		pack32(msg->topo_array[i].link_speed, buffer);
		packstr(msg->topo_array[i].name,      buffer);
		packstr(msg->topo_array[i].nodes,     buffer);
		packstr(msg->topo_array[i].switches,  buffer);
	}
}

static int _unpack_topo_info_msg(topo_info_response_msg_t **msg,
				 buf_t *buffer,
				 uint16_t protocol_version)
{
	int i = 0;
	uint32_t uint32_tmp;
	topo_info_response_msg_t *msg_ptr =
		xmalloc(sizeof(topo_info_response_msg_t));

	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->record_count, buffer);
	safe_xcalloc(msg_ptr->topo_array, msg_ptr->record_count,
		     sizeof(topo_info_t));
	for (i=0; i<msg_ptr->record_count; i++) {
		safe_unpack16(&msg_ptr->topo_array[i].level,      buffer);
		safe_unpack32(&msg_ptr->topo_array[i].link_speed, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->topo_array[i].name,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->topo_array[i].nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->topo_array[i].switches,
				       &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_topo_info_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_stats_request_msg(stats_info_request_msg_t *msg, buf_t *buffer,
				    uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->command_id, buffer);
	}
}

static int  _unpack_stats_request_msg(stats_info_request_msg_t **msg_ptr,
				      buf_t *buffer, uint16_t protocol_version)
{
	stats_info_request_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof(stats_info_request_msg_t) );
	*msg_ptr = msg ;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->command_id, buffer);
	} else {
		error(" _unpack_stats_request_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	info("SIM: unpack_stats_request_msg error");
	*msg_ptr = NULL;
	slurm_free_stats_info_request_msg(msg);
	return SLURM_ERROR;
}

static int  _unpack_stats_response_msg(stats_info_response_msg_t **msg_ptr,
				       buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	stats_info_response_msg_t * msg;
	xassert(msg_ptr);

	msg = xmalloc ( sizeof (stats_info_response_msg_t) );
	*msg_ptr = msg ;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->parts_packed,	buffer);
		if (msg->parts_packed) {
			safe_unpack_time(&msg->req_time,	buffer);
			safe_unpack_time(&msg->req_time_start,	buffer);
			safe_unpack32(&msg->server_thread_count,buffer);
			safe_unpack32(&msg->agent_queue_size,	buffer);
			safe_unpack32(&msg->agent_count,	buffer);
			safe_unpack32(&msg->agent_thread_count,	buffer);
			safe_unpack32(&msg->dbd_agent_queue_size, buffer);
			safe_unpack32(&msg->gettimeofday_latency, buffer);
			safe_unpack32(&msg->jobs_submitted,	buffer);
			safe_unpack32(&msg->jobs_started,	buffer);
			safe_unpack32(&msg->jobs_completed,	buffer);
			safe_unpack32(&msg->jobs_canceled,	buffer);
			safe_unpack32(&msg->jobs_failed,	buffer);

			safe_unpack32(&msg->jobs_pending,	buffer);
			safe_unpack32(&msg->jobs_running,	buffer);
			safe_unpack_time(&msg->job_states_ts,	buffer);

			safe_unpack32(&msg->schedule_cycle_max,	buffer);
			safe_unpack32(&msg->schedule_cycle_last,buffer);
			safe_unpack32(&msg->schedule_cycle_sum,	buffer);
			safe_unpack32(&msg->schedule_cycle_counter, buffer);
			safe_unpack32(&msg->schedule_cycle_depth, buffer);
			safe_unpack32(&msg->schedule_queue_len,	buffer);

			safe_unpack32(&msg->bf_backfilled_jobs,	buffer);
			safe_unpack32(&msg->bf_last_backfilled_jobs, buffer);
			safe_unpack32(&msg->bf_cycle_counter,	buffer);
			safe_unpack64(&msg->bf_cycle_sum,	buffer);
			safe_unpack32(&msg->bf_cycle_last,	buffer);
			safe_unpack32(&msg->bf_last_depth,	buffer);
			safe_unpack32(&msg->bf_last_depth_try,	buffer);

			safe_unpack32(&msg->bf_queue_len,	buffer);
			safe_unpack32(&msg->bf_cycle_max,	buffer);
			safe_unpack_time(&msg->bf_when_last_cycle, buffer);
			safe_unpack32(&msg->bf_depth_sum,	buffer);
			safe_unpack32(&msg->bf_depth_try_sum,	buffer);
			safe_unpack32(&msg->bf_queue_len_sum,	buffer);
			safe_unpack32(&msg->bf_table_size,	buffer);
			safe_unpack32(&msg->bf_table_size_sum,	buffer);

			safe_unpack32(&msg->bf_active,		buffer);
			safe_unpack32(&msg->bf_backfilled_het_jobs, buffer);
		}

		safe_unpack32(&msg->rpc_type_size,		buffer);
		safe_unpack16_array(&msg->rpc_type_id,   &uint32_tmp, buffer);
		safe_unpack32_array(&msg->rpc_type_cnt,  &uint32_tmp, buffer);
		safe_unpack64_array(&msg->rpc_type_time, &uint32_tmp, buffer);

		safe_unpack32(&msg->rpc_user_size,		buffer);
		safe_unpack32_array(&msg->rpc_user_id,   &uint32_tmp, buffer);
		safe_unpack32_array(&msg->rpc_user_cnt,  &uint32_tmp, buffer);
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	info("%s: unpack error", __func__);
	*msg_ptr = NULL;
	slurm_free_stats_response_msg(msg);
	return SLURM_ERROR;
}

/* _pack_license_info_request_msg()
 */
static void
_pack_license_info_request_msg(license_info_request_msg_t *msg,
			       buf_t *buffer,
			       uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
	pack16((uint16_t)msg->show_flags, buffer);
}

/* _unpack_license_info_request_msg()
 */
static int
_unpack_license_info_request_msg(license_info_request_msg_t **msg,
				 buf_t *buffer,
				 uint16_t protocol_version)
{
	*msg = xmalloc(sizeof(license_info_msg_t));

	safe_unpack_time(&(*msg)->last_update, buffer);
	safe_unpack16(&(*msg)->show_flags, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_license_info_request_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

/* _pack_license_info_msg()
 */
static inline void
_pack_license_info_msg(slurm_msg_t *msg, buf_t *buffer)
{
	_pack_buffer_msg(msg, buffer);
}

/*
 * Decode the array of licenses as it comes from the
 * controller and build the API licenses structures.
 */
static int _unpack_license_info_msg(license_info_msg_t **msg_ptr,
				    buf_t *buffer,
				    uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	license_info_msg_t *msg = xmalloc(sizeof(*msg));

	xassert(msg_ptr);
	*msg_ptr = msg;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->num_lic, buffer);
		safe_unpack_time(&msg->last_update, buffer);

		safe_xcalloc(msg->lic_array, msg->num_lic,
			     sizeof(slurm_license_info_t));

		/* Decode individual license data */
		for (int i = 0; i < msg->num_lic; i++) {
			safe_unpackstr_xmalloc(&msg->lic_array[i].name,
					       &uint32_tmp, buffer);
			safe_unpack32(&msg->lic_array[i].total, buffer);
			safe_unpack32(&msg->lic_array[i].in_use, buffer);
			safe_unpack32(&msg->lic_array[i].reserved, buffer);
			/* The total number of licenses can decrease
			 * at runtime.
			 */
			if (msg->lic_array[i].total < msg->lic_array[i].in_use)
				msg->lic_array[i].available = 0;
			else
				msg->lic_array[i].available =
					msg->lic_array[i].total -
					msg->lic_array[i].in_use;
			safe_unpack8(&msg->lic_array[i].remote, buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->num_lic, buffer);
		safe_unpack_time(&msg->last_update, buffer);

		safe_xcalloc(msg->lic_array, msg->num_lic,
			     sizeof(slurm_license_info_t));

		/* Decode individual license data */
		for (int i = 0; i < msg->num_lic; i++) {
			safe_unpackstr_xmalloc(&msg->lic_array[i].name,
					       &uint32_tmp, buffer);
			safe_unpack32(&msg->lic_array[i].total, buffer);
			safe_unpack32(&msg->lic_array[i].in_use, buffer);
			/* The total number of licenses can decrease
			 * at runtime.
			 */
			if (msg->lic_array[i].total < msg->lic_array[i].in_use)
				msg->lic_array[i].available = 0;
			else
				msg->lic_array[i].available =
					msg->lic_array[i].total -
					msg->lic_array[i].in_use;
			safe_unpack8(&msg->lic_array[i].remote, buffer);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_license_info_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_job_array_resp_msg(job_array_resp_msg_t *msg, buf_t *buffer,
				     uint16_t protocol_version)
{
	uint32_t i, cnt = 0;

	if (!msg) {
		pack32(cnt, buffer);
		return;
	}

	pack32(msg->job_array_count, buffer);
	for (i = 0; i < msg->job_array_count; i++) {
		pack32(msg->error_code[i], buffer);
		packstr(msg->job_array_id[i], buffer);
	}
}
static int  _unpack_job_array_resp_msg(job_array_resp_msg_t **msg, buf_t *buffer,
				       uint16_t protocol_version)
{
	job_array_resp_msg_t *resp;
	uint32_t i, uint32_tmp;

	resp = xmalloc(sizeof(job_array_resp_msg_t));
	safe_unpack32(&resp->job_array_count, buffer);
	if (resp->job_array_count > NO_VAL)
		goto unpack_error;
	safe_xcalloc(resp->error_code, resp->job_array_count, sizeof(uint32_t));
	safe_xcalloc(resp->job_array_id, resp->job_array_count, sizeof(char *));
	for (i = 0; i < resp->job_array_count; i++) {
		safe_unpack32(&resp->error_code[i], buffer);
		safe_unpackstr_xmalloc(&resp->job_array_id[i], &uint32_tmp,
				       buffer);
	}
	*msg = resp;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_array_resp(resp);
	*msg = NULL;
	return SLURM_ERROR;
}


/* _pack_assoc_mgr_info_request_msg()
 */
static void
_pack_assoc_mgr_info_request_msg(assoc_mgr_info_request_msg_t *msg,
				 buf_t *buffer,
				 uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;

	xassert(msg);

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

static int
_unpack_assoc_mgr_info_request_msg(assoc_mgr_info_request_msg_t **msg,
				   buf_t *buffer,
				   uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	uint32_t count = NO_VAL;
	int i;
	char *tmp_info = NULL;
	assoc_mgr_info_request_msg_t *object_ptr = NULL;

	xassert(msg);

	object_ptr = xmalloc(sizeof(assoc_mgr_info_request_msg_t));
	*msg = object_ptr;

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		object_ptr->acct_list = list_create(xfree_ptr);
		for (i = 0; i < count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->acct_list, tmp_info);
		}
	}

	safe_unpack32(&object_ptr->flags, buffer);

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		object_ptr->qos_list = list_create(xfree_ptr);
		for (i = 0; i < count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->qos_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		object_ptr->user_list = list_create(xfree_ptr);
		for (i = 0; i < count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->user_list, tmp_info);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_assoc_mgr_info_request_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_buf_list_msg(ctld_list_msg_t *msg, buf_t *buffer,
			       uint16_t protocol_version)
{
	ListIterator iter = NULL;
	buf_t *req_buf;
	uint32_t size;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

static int _unpack_buf_list_msg(ctld_list_msg_t **msg, buf_t *buffer,
				uint16_t protocol_version)
{
	ctld_list_msg_t *object_ptr = NULL;
	uint32_t i, list_size = 0, buf_size = 0, read_size = 0;
	char *data = NULL;
	buf_t *req_buf;

	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		object_ptr = xmalloc(sizeof(ctld_list_msg_t));
		*msg = object_ptr;

		safe_unpack32(&list_size, buffer);
		if (list_size >= NO_VAL)
			goto unpack_error;
		object_ptr->my_list = list_create(_ctld_free_list_msg);
		for (i = 0; i < list_size; i++) {
			safe_unpack32(&buf_size, buffer);
			safe_unpackmem_xmalloc(&data, &read_size, buffer);
			if (buf_size != read_size)
				goto unpack_error;
			/* Move "data" into "req_buf", NOT a memory leak */
			req_buf = create_buf(data, buf_size);
			data = NULL; /* just to be safe */
			list_append(object_ptr->my_list, req_buf);
		}
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	xfree(data);
	slurm_free_ctld_multi_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_set_fs_dampening_factor_msg(
	set_fs_dampening_factor_msg_t *msg,
	buf_t *buffer, uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION)
		pack16(msg->dampening_factor, buffer);
}


static int _unpack_set_fs_dampening_factor_msg(
	set_fs_dampening_factor_msg_t **msg_ptr,
	buf_t *buffer, uint16_t protocol_version)
{
	set_fs_dampening_factor_msg_t *msg;

	msg = xmalloc(sizeof(set_fs_dampening_factor_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION)
		safe_unpack16(&msg->dampening_factor, buffer);
	else
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_set_fs_dampening_factor_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_control_status_msg(control_status_msg_t *msg,
				     buf_t *buffer, uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->backup_inx, buffer);
		pack_time(msg->control_time, buffer);
	}
}

static int _unpack_control_status_msg(control_status_msg_t **msg_ptr,
				      buf_t *buffer, uint16_t protocol_version)
{
	control_status_msg_t *msg;

	msg = xmalloc(sizeof(control_status_msg_t));
	*msg_ptr = msg;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->backup_inx, buffer);
		safe_unpack_time(&msg->control_time, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_control_status_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_bb_status_req_msg(bb_status_req_msg_t *msg, buf_t *buffer,
				    uint16_t protocol_version)
{
	packstr_array(msg->argv, msg->argc, buffer);
}

static int _unpack_bb_status_req_msg(bb_status_req_msg_t **msg_ptr, buf_t *buffer,
				     uint16_t protocol_version)
{
	bb_status_req_msg_t *msg;
	xassert(msg_ptr);

	msg = xmalloc(sizeof(bb_status_req_msg_t));
	*msg_ptr = msg;

	safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_bb_status_req_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_bb_status_resp_msg(bb_status_resp_msg_t *msg, buf_t *buffer,
				     uint16_t protocol_version)
{
	packstr(msg->status_resp, buffer);
}

static int _unpack_bb_status_resp_msg(bb_status_resp_msg_t **msg_ptr,
				      buf_t *buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	bb_status_resp_msg_t *msg;
	xassert(msg_ptr);

	msg = xmalloc(sizeof(bb_status_resp_msg_t));
	*msg_ptr = msg;

	safe_unpackstr_xmalloc(&msg->status_resp, &uint32_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_bb_status_resp_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_crontab_request_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_request_msg_t *msg = (crontab_request_msg_t *) smsg->data;

	if (smsg->protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(msg->uid, buffer);
	}
}

static int _unpack_crontab_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_request_msg_t *msg = xmalloc(sizeof(*msg));
	smsg->data = msg;

	if (smsg->protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->uid, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_crontab_request_msg(msg);
	smsg->data = NULL;
	return SLURM_ERROR;
}

static void _pack_crontab_response_msg(const slurm_msg_t *smsg, buf_t *buffer)
{
	crontab_response_msg_t *msg = (crontab_response_msg_t *) smsg->data;

	if (smsg->protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		packstr(msg->crontab, buffer);
		packstr(msg->disabled_lines, buffer);
	}
}

static int _unpack_crontab_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp;
	crontab_response_msg_t *msg = xmalloc(sizeof(*msg));
	smsg->data = msg;

	if (smsg->protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->crontab, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->disabled_lines, &uint32_tmp,
				       buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_crontab_response_msg(msg);
	smsg->data = NULL;
	return SLURM_ERROR;
}

static void _pack_crontab_update_request_msg(const slurm_msg_t *smsg,
					     buf_t *buffer)
{
	crontab_update_request_msg_t *msg =
		(crontab_update_request_msg_t *) smsg->data;

	if (smsg->protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		packstr(msg->crontab, buffer);
		_pack_job_desc_list_msg(msg->jobs, buffer,
					smsg->protocol_version);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
	}
}

static int _unpack_crontab_update_request_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp;
	crontab_update_request_msg_t *msg = xmalloc(sizeof(*msg));
	smsg->data = msg;

	if (smsg->protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->crontab, &uint32_tmp, buffer);
		if (_unpack_job_desc_list_msg(&msg->jobs, buffer,
					      smsg->protocol_version))
			goto unpack_error;
		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_crontab_update_request_msg(msg);
	smsg->data = NULL;
	return SLURM_ERROR;
}

static void _pack_crontab_update_response_msg(const slurm_msg_t *smsg,
					      buf_t *buffer)
{
	crontab_update_response_msg_t *msg =
		(crontab_update_response_msg_t *) smsg->data;

	if (smsg->protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		packstr(msg->err_msg, buffer);
		packstr(msg->failed_lines, buffer);
		pack32_array(msg->jobids, msg->jobids_count, buffer);
		pack32(msg->return_code, buffer);
	}
}

static int _unpack_crontab_update_response_msg(slurm_msg_t *smsg, buf_t *buffer)
{
	uint32_t uint32_tmp;
	crontab_update_response_msg_t *msg = xmalloc(sizeof(*msg));
	smsg->data = msg;

	if (smsg->protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->err_msg, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->failed_lines, &uint32_tmp, buffer);
		safe_unpack32_array(&msg->jobids, &msg->jobids_count, buffer);
		safe_unpack32(&msg->return_code, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_crontab_update_response_msg(msg);
	smsg->data = NULL;
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
pack_msg(slurm_msg_t const *msg, buf_t *buffer)
{
	if (msg->protocol_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("%s: Invalid message version=%hu, type:%hu",
		      __func__, msg->protocol_version, msg->msg_type);
		return SLURM_ERROR;
	}

	switch (msg->msg_type) {
	case REQUEST_NODE_INFO:
		_pack_node_info_request_msg((node_info_request_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_NODE_INFO_SINGLE:
		_pack_node_info_single_msg((node_info_single_msg_t *)
					   msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_PARTITION_INFO:
		_pack_part_info_request_msg((part_info_request_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_RESERVATION_INFO:
		_pack_resv_info_request_msg((resv_info_request_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_BUILD_INFO:
		_pack_last_update_msg((last_update_msg_t *)
				      msg->data, buffer,
				      msg->protocol_version);
		break;
	case RESPONSE_BUILD_INFO:
		_pack_slurm_ctl_conf_msg((slurm_ctl_conf_info_msg_t *)
					 msg->data, buffer,
					 msg->protocol_version);
		break;
	case RESPONSE_JOB_INFO:
		_pack_job_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case RESPONSE_BATCH_SCRIPT:
		_pack_job_script_msg((buf_t *) msg->data, buffer,
				     msg->protocol_version);
		break;
	case RESPONSE_PARTITION_INFO:
		_pack_partition_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case RESPONSE_NODE_INFO:
		_pack_node_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		_pack_node_registration_status_msg(
			(slurm_node_registration_status_msg_t *) msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_ACCT_GATHER_UPDATE:
	case RESPONSE_ACCT_GATHER_ENERGY:
		_pack_acct_gather_node_resp_msg(
			(acct_gather_node_resp_msg_t *) msg->data,
			buffer, msg->protocol_version);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_UPDATE_JOB:
		_pack_job_desc_msg((job_desc_msg_t *) msg->data, buffer,
				   msg->protocol_version);
		break;
	case REQUEST_HET_JOB_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_HET_JOB:
		_pack_job_desc_list_msg((List) msg->data, buffer,
					msg->protocol_version);
		break;
	case RESPONSE_HET_JOB_ALLOCATION:
		_pack_job_info_list_msg((List) msg->data, buffer,
					msg->protocol_version);
		break;
	case REQUEST_SIB_JOB_LOCK:
	case REQUEST_SIB_JOB_UNLOCK:
	case REQUEST_SIB_MSG:
		_pack_sib_msg((sib_msg_t *)msg->data, buffer,
			      msg->protocol_version);
		break;
	case REQUEST_SEND_DEP:
		_pack_dep_msg((dep_msg_t *)msg->data, buffer,
			      msg->protocol_version);
		break;
	case REQUEST_UPDATE_ORIGIN_DEP:
		_pack_dep_update_origin_msg(
			(dep_update_origin_msg_t *) msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_UPDATE_JOB_STEP:
		_pack_update_job_step_msg((step_update_request_msg_t *)
					  msg->data, buffer,
					  msg->protocol_version);
		break;
	case REQUEST_JOB_ALLOCATION_INFO:
	case REQUEST_JOB_END_TIME:
	case REQUEST_HET_JOB_ALLOC_INFO:
		_pack_job_alloc_info_msg((job_alloc_info_msg_t *) msg->data,
					 buffer, msg->protocol_version);
		break;
	case REQUEST_JOB_SBCAST_CRED:
		_pack_step_alloc_info_msg((step_alloc_info_msg_t *) msg->data,
					  buffer, msg->protocol_version);
		break;
	case RESPONSE_NODE_REGISTRATION:
		_pack_node_reg_resp(
			(slurm_node_reg_resp_msg_t *)msg->data,
			buffer, msg->protocol_version);
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
	case REQUEST_TOPO_INFO:
	case REQUEST_BURST_BUFFER_INFO:
	case REQUEST_FED_INFO:
		/* Message contains no body/information */
		break;
	case REQUEST_ACCT_GATHER_ENERGY:
		_pack_acct_gather_energy_req(
			(acct_gather_energy_req_msg_t *)msg->data,
			buffer, msg->protocol_version);
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
		_pack_reboot_msg((reboot_msg_t *)msg->data, buffer,
				 msg->protocol_version);
		break;
	case REQUEST_SHUTDOWN:
		_pack_shutdown_msg((shutdown_msg_t *) msg->data, buffer,
				   msg->protocol_version);
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		_pack_submit_response_msg((submit_response_msg_t *)
					  msg->data, buffer,
					  msg->protocol_version);
		break;
	case RESPONSE_JOB_ALLOCATION_INFO:
	case RESPONSE_RESOURCE_ALLOCATION:
		_pack_resource_allocation_response_msg
			((resource_allocation_response_msg_t *) msg->data,
			 buffer,
			 msg->protocol_version);
		break;
	case RESPONSE_JOB_WILL_RUN:
		_pack_will_run_response_msg((will_run_response_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_UPDATE_FRONT_END:
		_pack_update_front_end_msg((update_front_end_msg_t *) msg->data,
					   buffer, msg->protocol_version);
		break;
	case REQUEST_UPDATE_NODE:
		_pack_update_node_msg((update_node_msg_t *) msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		_pack_update_partition_msg((update_part_msg_t *) msg->
					   data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_DELETE_PARTITION:
		_pack_delete_partition_msg((delete_part_msg_t *) msg->
					   data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_CREATE_RESERVATION:
	case REQUEST_UPDATE_RESERVATION:
		_pack_update_resv_msg((resv_desc_msg_t *) msg->
				      data, buffer,
				      msg->protocol_version);
		break;
	case RESPONSE_RESERVATION_INFO:
		_pack_reserve_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case REQUEST_DELETE_RESERVATION:
	case RESPONSE_CREATE_RESERVATION:
		_pack_resv_name_msg((reservation_name_msg_t *) msg->
				    data, buffer,
				    msg->protocol_version);
		break;
	case REQUEST_REATTACH_TASKS:
		_pack_reattach_tasks_request_msg(
			(reattach_tasks_request_msg_t *) msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_REATTACH_TASKS:
		_pack_reattach_tasks_response_msg(
			(reattach_tasks_response_msg_t *) msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_LAUNCH_TASKS:
		_pack_launch_tasks_request_msg(
			(launch_tasks_request_msg_t *) msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_LAUNCH_TASKS:
		_pack_launch_tasks_response_msg((launch_tasks_response_msg_t
						 *) msg->data, buffer,
						msg->protocol_version);
		break;
	case TASK_USER_MANAGED_IO_STREAM:
		_pack_task_user_managed_io_stream_msg(
			(task_user_managed_io_msg_t *) msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		_pack_cancel_tasks_msg((signal_tasks_msg_t *) msg->data,
				       buffer,
				       msg->protocol_version);
		break;
	case REQUEST_JOB_STEP_INFO:
		_pack_job_step_info_req_msg((job_step_info_request_msg_t
					     *) msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_JOB_INFO:
		_pack_job_info_request_msg((job_info_request_msg_t *)
					   msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_CANCEL_JOB_STEP:
	case REQUEST_KILL_JOB:
	case SRUN_STEP_SIGNAL:
		_pack_job_step_kill_msg((job_step_kill_msg_t *)
					msg->data, buffer,
					msg->protocol_version);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		_pack_complete_job_allocation_msg(
			(complete_job_allocation_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_COMPLETE_PROLOG:
		_pack_complete_prolog_msg(
			(complete_prolog_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		_pack_complete_batch_script_msg(
			(complete_batch_script_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_STEP_COMPLETE:
		_pack_step_complete_msg((step_complete_msg_t *)msg->data,
					buffer,
					msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_STAT:
		_pack_job_step_stat((job_step_stat_t *) msg->data,
				    buffer,
				    msg->protocol_version);
		break;
		/********  slurm_step_id_t Messages  ********/
	case SRUN_JOB_COMPLETE:
	case REQUEST_STEP_LAYOUT:
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
		pack_step_id((slurm_step_id_t *)msg->data, buffer,
			     msg->protocol_version);
		break;
	case RESPONSE_STEP_LAYOUT:
		pack_slurm_step_layout((slurm_step_layout_t *)msg->data,
				       buffer,
				       msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_PIDS:
		_pack_job_step_pids((job_step_pids_t *)msg->data,
				    buffer,
				    msg->protocol_version);
		break;
	case REQUEST_ABORT_JOB:
	case REQUEST_KILL_PREEMPTED:
	case REQUEST_KILL_TIMELIMIT:
	case REQUEST_TERMINATE_JOB:
		_pack_kill_job_msg((kill_job_msg_t *) msg->data, buffer,
				   msg->protocol_version);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		_pack_epilog_comp_msg((epilog_complete_msg_t *) msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_INFO:
		_pack_job_step_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case MESSAGE_TASK_EXIT:
		_pack_task_exit_msg((task_exit_msg_t *) msg->data, buffer,
				    msg->protocol_version);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		_pack_batch_job_launch_msg((batch_job_launch_msg_t *)
					   msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_LAUNCH_PROLOG:
		_pack_prolog_launch_msg((prolog_launch_msg_t *)
					msg->data, buffer, msg->protocol_version);
		break;
	case RESPONSE_PROLOG_EXECUTING:
	case RESPONSE_JOB_READY:
	case RESPONSE_SLURM_RC:
		_pack_return_code_msg((return_code_msg_t *) msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case RESPONSE_SLURM_RC_MSG:
		_pack_return_code2_msg((return_code2_msg_t *) msg->data,
				       buffer,
				       msg->protocol_version);
		break;
	case RESPONSE_SLURM_REROUTE_MSG:
		_pack_reroute_msg((reroute_msg_t *)msg->data, buffer,
				  msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_CREATE:
		_pack_job_step_create_response_msg(
			(job_step_create_response_msg_t *)
			msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_STEP_CREATE:
		_pack_job_step_create_request_msg(
			(job_step_create_request_msg_t *)
			msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_ID:
		_pack_job_id_request_msg(
			(job_id_request_msg_t *)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_ID:
		_pack_job_id_response_msg(
			(job_id_response_msg_t *)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case REQUEST_CONFIG:
		_pack_config_request_msg((config_request_msg_t *) msg->data,
					 buffer, msg->protocol_version);
		break;
	case REQUEST_RECONFIGURE_WITH_CONFIG:
	case RESPONSE_CONFIG:
		pack_config_response_msg((config_response_msg_t *) msg->data,
					 buffer, msg->protocol_version);
		break;
	case SRUN_EXEC:
		_pack_srun_exec_msg((srun_exec_msg_t *)msg->data, buffer,
				    msg->protocol_version);
		break;
	case SRUN_PING:
		_pack_srun_ping_msg((srun_ping_msg_t *)msg->data, buffer,
				    msg->protocol_version);
		break;
	case SRUN_NODE_FAIL:
		_pack_srun_node_fail_msg((srun_node_fail_msg_t *)msg->data,
					 buffer,
					 msg->protocol_version);
		break;
	case SRUN_STEP_MISSING:
		_pack_srun_step_missing_msg((srun_step_missing_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case SRUN_TIMEOUT:
		_pack_srun_timeout_msg((srun_timeout_msg_t *)msg->data, buffer,
				       msg->protocol_version);
		break;
	case SRUN_USER_MSG:
		_pack_srun_user_msg((srun_user_msg_t *)msg->data, buffer,
				    msg->protocol_version);
		break;
	case SRUN_NET_FORWARD:
		_pack_net_forward_msg((net_forward_msg_t *)msg->data,
				      buffer, msg->protocol_version);
		break;
	case REQUEST_SUSPEND:
	case SRUN_REQUEST_SUSPEND:
		_pack_suspend_msg((suspend_msg_t *)msg->data, buffer,
				  msg->protocol_version);
		break;
	case REQUEST_SUSPEND_INT:
		_pack_suspend_int_msg((suspend_int_msg_t *)msg->data, buffer,
				      msg->protocol_version);
		break;
	case REQUEST_TOP_JOB:
		_pack_top_job_msg((top_job_msg_t *)msg->data, buffer,
				  msg->protocol_version);
		break;
	case REQUEST_AUTH_TOKEN:
		_pack_token_request_msg((token_request_msg_t *) msg->data,
					buffer,
					msg->protocol_version);
		break;
	case RESPONSE_AUTH_TOKEN:
		_pack_token_response_msg((token_response_msg_t *) msg->data,
					 buffer, msg->protocol_version);
		break;
	case REQUEST_BATCH_SCRIPT:
	case REQUEST_JOB_READY:
	case REQUEST_JOB_INFO_SINGLE:
		_pack_job_ready_msg((job_id_msg_t *)msg->data, buffer,
				    msg->protocol_version);
		break;

	case REQUEST_JOB_REQUEUE:
		_pack_job_requeue_msg((requeue_msg_t *)msg->data,
				      buffer,
				      msg->protocol_version);
		break;

	case REQUEST_JOB_USER_INFO:
		_pack_job_user_msg((job_user_id_msg_t *)msg->data, buffer,
				   msg->protocol_version);
		break;

	case REQUEST_SHARE_INFO:
		_pack_shares_request_msg((shares_request_msg_t *)msg->data,
					 buffer,
					 msg->protocol_version);
		break;
	case RESPONSE_SHARE_INFO:
		_pack_shares_response_msg((shares_response_msg_t *)msg->data,
					  buffer,
					  msg->protocol_version);
		break;
	case REQUEST_PRIORITY_FACTORS:
		_pack_priority_factors_request_msg(
			(priority_factors_request_msg_t*)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_PRIORITY_FACTORS:
		_pack_priority_factors_response_msg(
			(priority_factors_response_msg_t*)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_BURST_BUFFER_INFO:
		_pack_burst_buffer_info_resp_msg((slurm_msg_t *) msg, buffer);
		break;
	case REQUEST_FILE_BCAST:
		_pack_file_bcast((file_bcast_msg_t *) msg->data, buffer,
				 msg->protocol_version);
		break;
	case PMI_KVS_PUT_REQ:
	case PMI_KVS_GET_RESP:
		_pack_kvs_data((kvs_comm_set_t *) msg->data, buffer,
			       msg->protocol_version);
		break;
	case PMI_KVS_GET_REQ:
		_pack_kvs_get((kvs_get_msg_t *) msg->data, buffer,
			      msg->protocol_version);
		break;
	case RESPONSE_FORWARD_FAILED:
		break;
	case REQUEST_TRIGGER_GET:
	case RESPONSE_TRIGGER_GET:
	case REQUEST_TRIGGER_SET:
	case REQUEST_TRIGGER_CLEAR:
	case REQUEST_TRIGGER_PULL:
		_pack_trigger_msg((trigger_info_msg_t *) msg->data, buffer,
				  msg->protocol_version);
		break;
	case RESPONSE_SLURMD_STATUS:
		_pack_slurmd_status((slurmd_status_t *) msg->data, buffer,
				    msg->protocol_version);
		break;
	case REQUEST_JOB_NOTIFY:
		_pack_job_notify((job_notify_msg_t *) msg->data, buffer,
				 msg->protocol_version);
		break;
	case REQUEST_SET_DEBUG_FLAGS:
		_pack_set_debug_flags_msg(
			(set_debug_flags_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
	case REQUEST_SET_SCHEDLOG_LEVEL:
		_pack_set_debug_level_msg(
			(set_debug_level_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case ACCOUNTING_UPDATE_MSG:
		_pack_accounting_update_msg(
			(accounting_update_msg_t *)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_TOPO_INFO:
		_pack_topo_info_msg(
			(topo_info_response_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_SBCAST_CRED:
		_pack_job_sbcast_cred_msg(
			(job_sbcast_cred_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_FRONT_END_INFO:
		_pack_front_end_info_request_msg(
			(front_end_info_request_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_FED_INFO:
		slurmdb_pack_federation_rec(
			(slurmdb_federation_rec_t *)msg->data,
			msg->protocol_version, buffer);
		break;
	case RESPONSE_FRONT_END_INFO:
		_pack_front_end_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case REQUEST_STATS_INFO:
		_pack_stats_request_msg((stats_info_request_msg_t *)msg->data,
					buffer, msg->protocol_version);
		break;

	case RESPONSE_STATS_INFO:
		_pack_stats_response_msg((slurm_msg_t *)msg, buffer);
		break;

	case REQUEST_FORWARD_DATA:
		_pack_forward_data_msg((forward_data_msg_t *)msg->data,
				       buffer, msg->protocol_version);
		break;

	case RESPONSE_PING_SLURMD:
		_pack_ping_slurmd_resp((ping_slurmd_resp_msg_t *)msg->data,
				       buffer, msg->protocol_version);
		break;
	case REQUEST_LICENSE_INFO:
		_pack_license_info_request_msg((license_info_request_msg_t *)
					       msg->data,
					       buffer,
					       msg->protocol_version);
		break;
	case RESPONSE_LICENSE_INFO:
		_pack_license_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case RESPONSE_JOB_ARRAY_ERRORS:
		_pack_job_array_resp_msg((job_array_resp_msg_t *) msg->data,
					 buffer, msg->protocol_version);
		break;
	case REQUEST_ASSOC_MGR_INFO:
		_pack_assoc_mgr_info_request_msg(
			(assoc_mgr_info_request_msg_t *)msg->data,
			buffer, msg->protocol_version);
		break;
	case RESPONSE_ASSOC_MGR_INFO:
		_pack_assoc_mgr_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case REQUEST_NETWORK_CALLERID:
		_pack_network_callerid_msg((network_callerid_msg_t *)
					   msg->data, buffer,
					   msg->protocol_version);
		break;
	case RESPONSE_NETWORK_CALLERID:
		_pack_network_callerid_resp_msg((network_callerid_resp_t *)
						msg->data, buffer,
						msg->protocol_version);
		break;
	case REQUEST_CTLD_MULT_MSG:
	case RESPONSE_CTLD_MULT_MSG:
		_pack_buf_list_msg((ctld_list_msg_t *) msg->data, buffer,
				   msg->protocol_version);
		break;
	case REQUEST_SET_FS_DAMPENING_FACTOR:
		_pack_set_fs_dampening_factor_msg(
			(set_fs_dampening_factor_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_CONTROL_STATUS:
		_pack_control_status_msg((control_status_msg_t *)(msg->data),
					 buffer, msg->protocol_version);
		break;
	case REQUEST_BURST_BUFFER_STATUS:
		_pack_bb_status_req_msg((bb_status_req_msg_t *)(msg->data),
					buffer, msg->protocol_version);
		break;
	case RESPONSE_BURST_BUFFER_STATUS:
		_pack_bb_status_resp_msg((bb_status_resp_msg_t *)(msg->data),
					 buffer, msg->protocol_version);
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

	switch (msg->msg_type) {
	case REQUEST_NODE_INFO:
		rc = _unpack_node_info_request_msg((node_info_request_msg_t **)
						   & (msg->data), buffer,
						   msg->protocol_version);
		break;
	case REQUEST_NODE_INFO_SINGLE:
		rc = _unpack_node_info_single_msg((node_info_single_msg_t **)
						  & (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_PARTITION_INFO:
		rc = _unpack_part_info_request_msg((part_info_request_msg_t **)
						   & (msg->data), buffer,
						   msg->protocol_version);
		break;
	case REQUEST_RESERVATION_INFO:
		rc = _unpack_resv_info_request_msg((resv_info_request_msg_t **)
						   & (msg->data), buffer,
						   msg->protocol_version);
		break;
	case REQUEST_BUILD_INFO:
		rc = _unpack_last_update_msg((last_update_msg_t **) &
					     (msg->data), buffer,
					     msg->protocol_version);
		break;
	case RESPONSE_BUILD_INFO:
		rc = _unpack_slurm_ctl_conf_msg((slurm_ctl_conf_info_msg_t
						 **)
						& (msg->data), buffer,
						msg->protocol_version);
		break;
	case RESPONSE_JOB_INFO:
		rc = _unpack_job_info_msg((job_info_msg_t **) & (msg->data),
					  buffer,
					  msg->protocol_version);
		break;
	case RESPONSE_BATCH_SCRIPT:
		rc = _unpack_job_script_msg((char **) &(msg->data),
					    buffer,
					    msg->protocol_version);
		break;
	case RESPONSE_PARTITION_INFO:
		rc = _unpack_partition_info_msg((partition_info_msg_t **) &
						(msg->data), buffer,
						msg->protocol_version);
		break;
	case RESPONSE_NODE_INFO:
		rc = _unpack_node_info_msg((node_info_msg_t **) &
					   (msg->data), buffer,
					   msg->protocol_version);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		rc = _unpack_node_registration_status_msg(
			(slurm_node_registration_status_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_ACCT_GATHER_UPDATE:
	case RESPONSE_ACCT_GATHER_ENERGY:
		rc = _unpack_acct_gather_node_resp_msg(
			(acct_gather_node_resp_msg_t **)&(msg->data),
			buffer, msg->protocol_version);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_UPDATE_JOB:
		rc = _unpack_job_desc_msg((job_desc_msg_t **) & (msg->data),
					  buffer, msg->protocol_version);
		break;
	case REQUEST_HET_JOB_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_HET_JOB:
		rc = _unpack_job_desc_list_msg((List *) &(msg->data),
					       buffer, msg->protocol_version);
		break;
	case RESPONSE_HET_JOB_ALLOCATION:
		rc = _unpack_job_info_list_msg((List *) &(msg->data),
					       buffer, msg->protocol_version);
		break;
	case REQUEST_SIB_JOB_LOCK:
	case REQUEST_SIB_JOB_UNLOCK:
	case REQUEST_SIB_MSG:
		rc = _unpack_sib_msg((sib_msg_t **)&(msg->data), buffer,
				     msg->protocol_version);
		break;
	case REQUEST_SEND_DEP:
		rc = _unpack_dep_msg((dep_msg_t **)&(msg->data), buffer,
				     msg->protocol_version);
		break;
	case REQUEST_UPDATE_ORIGIN_DEP:
		rc = _unpack_dep_update_origin_msg(
			(dep_update_origin_msg_t **) &(msg->data),
			buffer, msg->protocol_version);
		break;
	case REQUEST_UPDATE_JOB_STEP:
		rc = _unpack_update_job_step_msg(
			(step_update_request_msg_t **) & (msg->data),
			buffer, msg->protocol_version);
		break;
	case REQUEST_JOB_ALLOCATION_INFO:
	case REQUEST_JOB_END_TIME:
	case REQUEST_HET_JOB_ALLOC_INFO:
		rc = _unpack_job_alloc_info_msg((job_alloc_info_msg_t **) &
						(msg->data), buffer,
						msg->protocol_version);
		break;
	case REQUEST_JOB_SBCAST_CRED:
		rc = _unpack_step_alloc_info_msg((step_alloc_info_msg_t **) &
						 (msg->data), buffer,
						 msg->protocol_version);
		break;
	case RESPONSE_NODE_REGISTRATION:
		rc = _unpack_node_reg_resp(
			(slurm_node_reg_resp_msg_t **)&msg->data,
			buffer, msg->protocol_version);
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
	case REQUEST_TOPO_INFO:
	case REQUEST_BURST_BUFFER_INFO:
	case REQUEST_FED_INFO:
		/* Message contains no body/information */
		break;
	case REQUEST_ACCT_GATHER_ENERGY:
		rc = _unpack_acct_gather_energy_req(
			(acct_gather_energy_req_msg_t **) & (msg->data),
			buffer, msg->protocol_version);
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
		rc = _unpack_reboot_msg((reboot_msg_t **) & (msg->data),
					buffer, msg->protocol_version);
		break;
	case REQUEST_SHUTDOWN:
		rc = _unpack_shutdown_msg((shutdown_msg_t **) & (msg->data),
					  buffer,
					  msg->protocol_version);
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		rc = _unpack_submit_response_msg((submit_response_msg_t **)
						 & (msg->data), buffer,
						 msg->protocol_version);
		break;
	case RESPONSE_JOB_ALLOCATION_INFO:
	case RESPONSE_RESOURCE_ALLOCATION:
		rc = _unpack_resource_allocation_response_msg(
			(resource_allocation_response_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_WILL_RUN:
		rc = _unpack_will_run_response_msg((will_run_response_msg_t **)
						   &(msg->data), buffer,
						   msg->protocol_version);
		break;
	case REQUEST_UPDATE_FRONT_END:
		rc = _unpack_update_front_end_msg((update_front_end_msg_t **) &
						  (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_UPDATE_NODE:
		rc = _unpack_update_node_msg((update_node_msg_t **) &
					     (msg->data), buffer,
					     msg->protocol_version);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		rc = _unpack_update_partition_msg((update_part_msg_t **) &
						  (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_DELETE_PARTITION:
		rc = _unpack_delete_partition_msg((delete_part_msg_t **) &
						  (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_CREATE_RESERVATION:
	case REQUEST_UPDATE_RESERVATION:
		rc = _unpack_update_resv_msg((resv_desc_msg_t **)
					     &(msg->data), buffer,
					     msg->protocol_version);
		break;
	case REQUEST_DELETE_RESERVATION:
	case RESPONSE_CREATE_RESERVATION:
		rc = _unpack_resv_name_msg((reservation_name_msg_t **)
					   &(msg->data), buffer,
					   msg->protocol_version);
		break;
	case RESPONSE_RESERVATION_INFO:
		rc = _unpack_reserve_info_msg((reserve_info_msg_t **)
					      &(msg->data), buffer,
					      msg->protocol_version);
		break;
	case REQUEST_LAUNCH_TASKS:
		rc = _unpack_launch_tasks_request_msg(
			(launch_tasks_request_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_LAUNCH_TASKS:
		rc = _unpack_launch_tasks_response_msg(
			(launch_tasks_response_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case TASK_USER_MANAGED_IO_STREAM:
		rc = _unpack_task_user_managed_io_stream_msg(
			(task_user_managed_io_msg_t **) &msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_REATTACH_TASKS:
		rc = _unpack_reattach_tasks_request_msg(
			(reattach_tasks_request_msg_t **) & msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_REATTACH_TASKS:
		rc = _unpack_reattach_tasks_response_msg(
			(reattach_tasks_response_msg_t **)
			& msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		rc = _unpack_cancel_tasks_msg((signal_tasks_msg_t **) &
					      (msg->data), buffer,
					      msg->protocol_version);
		break;
	case REQUEST_JOB_STEP_INFO:
		rc = _unpack_job_step_info_req_msg(
			(job_step_info_request_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_INFO:
		rc = _unpack_job_info_request_msg((job_info_request_msg_t**)
						  & (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_CANCEL_JOB_STEP:
	case REQUEST_KILL_JOB:
	case SRUN_STEP_SIGNAL:
		rc = _unpack_job_step_kill_msg((job_step_kill_msg_t **)
					       & (msg->data), buffer,
					       msg->protocol_version);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		rc = _unpack_complete_job_allocation_msg(
			(complete_job_allocation_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_COMPLETE_PROLOG:
		rc = _unpack_complete_prolog_msg(
			(complete_prolog_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		rc = _unpack_complete_batch_script_msg(
			(complete_batch_script_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_STEP_COMPLETE:
		rc = _unpack_step_complete_msg((step_complete_msg_t
						**) & (msg->data),
					       buffer,
					       msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_STAT:
		rc = _unpack_job_step_stat(
			(job_step_stat_t **) &(msg->data), buffer,
			msg->protocol_version);
		break;
		/********  slurm_step_id_t Messages  ********/
	case SRUN_JOB_COMPLETE:
	case REQUEST_STEP_LAYOUT:
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
		rc = unpack_step_id((slurm_step_id_t **)&msg->data,
				    buffer, msg->protocol_version);
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
		rc = _unpack_kill_job_msg((kill_job_msg_t **) & (msg->data),
					  buffer,
					  msg->protocol_version);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		rc = _unpack_epilog_comp_msg((epilog_complete_msg_t **)
					     & (msg->data), buffer,
					     msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_INFO:
		rc = _unpack_job_step_info_response_msg(
			(job_step_info_response_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case MESSAGE_TASK_EXIT:
		rc = _unpack_task_exit_msg((task_exit_msg_t **)
					   & (msg->data), buffer,
					   msg->protocol_version);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		rc = _unpack_batch_job_launch_msg((batch_job_launch_msg_t **)
						  & (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_LAUNCH_PROLOG:
		rc = _unpack_prolog_launch_msg((prolog_launch_msg_t **)
					       & (msg->data),
					       buffer, msg->protocol_version);
		break;
	case RESPONSE_PROLOG_EXECUTING:
	case RESPONSE_JOB_READY:
	case RESPONSE_SLURM_RC:
		rc = _unpack_return_code_msg((return_code_msg_t **)
					     & (msg->data), buffer,
					     msg->protocol_version);
		break;
	case RESPONSE_SLURM_RC_MSG:
		/* Log error message, otherwise replicate RESPONSE_SLURM_RC */
		msg->msg_type = RESPONSE_SLURM_RC;
		rc = _unpack_return_code2_msg((return_code_msg_t **)
					      & (msg->data), buffer,
					      msg->protocol_version);
		break;
	case RESPONSE_SLURM_REROUTE_MSG:
		rc = _unpack_reroute_msg((reroute_msg_t **)&(msg->data), buffer,
					 msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_CREATE:
		rc = _unpack_job_step_create_response_msg(
			(job_step_create_response_msg_t **)
			& msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_STEP_CREATE:
		rc = _unpack_job_step_create_request_msg(
			(job_step_create_request_msg_t **) & msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_ID:
		rc = _unpack_job_id_request_msg(
			(job_id_request_msg_t **) & msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_ID:
		rc = _unpack_job_id_response_msg(
			(job_id_response_msg_t **) & msg->data,
			buffer,
			msg->protocol_version);
		break;
	case REQUEST_CONFIG:
		_unpack_config_request_msg(
			(config_request_msg_t **) &msg->data,
			buffer, msg->protocol_version);
		break;
	case REQUEST_RECONFIGURE_WITH_CONFIG:
	case RESPONSE_CONFIG:
		unpack_config_response_msg(
			(config_response_msg_t **) &msg->data,
			buffer, msg->protocol_version);
		break;
	case SRUN_EXEC:
		rc = _unpack_srun_exec_msg((srun_exec_msg_t **) & msg->data,
					   buffer,
					   msg->protocol_version);
		break;
	case SRUN_PING:
		rc = _unpack_srun_ping_msg((srun_ping_msg_t **) & msg->data,
					   buffer,
					   msg->protocol_version);
		break;
	case SRUN_NET_FORWARD:
		rc = _unpack_net_forward_msg((net_forward_msg_t **) &msg->data,
					     buffer, msg->protocol_version);
		break;
	case SRUN_NODE_FAIL:
		rc = _unpack_srun_node_fail_msg((srun_node_fail_msg_t **)
						& msg->data, buffer,
						msg->protocol_version);
		break;
	case SRUN_STEP_MISSING:
		rc = _unpack_srun_step_missing_msg((srun_step_missing_msg_t **)
						   & msg->data, buffer,
						   msg->protocol_version);
		break;
	case SRUN_TIMEOUT:
		rc = _unpack_srun_timeout_msg((srun_timeout_msg_t **)
					      & msg->data, buffer,
					      msg->protocol_version);
		break;
	case SRUN_USER_MSG:
		rc = _unpack_srun_user_msg((srun_user_msg_t **)
					   & msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_SUSPEND:
	case SRUN_REQUEST_SUSPEND:
		rc = _unpack_suspend_msg((suspend_msg_t **) &msg->data,
					 buffer,
					 msg->protocol_version);
		break;
	case REQUEST_SUSPEND_INT:
		rc = _unpack_suspend_int_msg((suspend_int_msg_t **) &msg->data,
					     buffer, msg->protocol_version);
		break;
	case REQUEST_TOP_JOB:
		rc = _unpack_top_job_msg((top_job_msg_t **) &msg->data, buffer,
					 msg->protocol_version);
		break;
	case REQUEST_AUTH_TOKEN:
		rc = _unpack_token_request_msg((token_request_msg_t **)
					       &msg->data,
					       buffer, msg->protocol_version);
		break;
	case RESPONSE_AUTH_TOKEN:
		rc = _unpack_token_response_msg((token_response_msg_t **)
						&msg->data,
					        buffer, msg->protocol_version);
		break;
	case REQUEST_BATCH_SCRIPT:
	case REQUEST_JOB_READY:
	case REQUEST_JOB_INFO_SINGLE:
		rc = _unpack_job_ready_msg((job_id_msg_t **)
					   & msg->data, buffer,
					   msg->protocol_version);
		break;

	case REQUEST_JOB_REQUEUE:
		rc = _unpack_job_requeue_msg((requeue_msg_t **)&msg->data,
					     buffer,
					     msg->protocol_version);
		break;

	case REQUEST_JOB_USER_INFO:
		rc = _unpack_job_user_msg((job_user_id_msg_t **)
					  &msg->data, buffer,
					  msg->protocol_version);
		break;

	case REQUEST_SHARE_INFO:
		rc = _unpack_shares_request_msg(
			(shares_request_msg_t **)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_SHARE_INFO:
		rc = _unpack_shares_response_msg(
			(shares_response_msg_t **)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case REQUEST_PRIORITY_FACTORS:
		rc = _unpack_priority_factors_request_msg(
			(priority_factors_request_msg_t**)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_PRIORITY_FACTORS:
		rc = _unpack_priority_factors_response_msg(
			(priority_factors_response_msg_t**)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_BURST_BUFFER_INFO:
		rc = _unpack_burst_buffer_info_msg(
			(burst_buffer_info_msg_t **) &(msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_FILE_BCAST:
		rc = _unpack_file_bcast( (file_bcast_msg_t **)
					 & msg->data, buffer,
					 msg->protocol_version);
		break;
	case PMI_KVS_PUT_REQ:
	case PMI_KVS_GET_RESP:
		rc = _unpack_kvs_data((kvs_comm_set_t **) &msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case PMI_KVS_GET_REQ:
		rc = _unpack_kvs_get((kvs_get_msg_t **) &msg->data, buffer,
				     msg->protocol_version);
		break;
	case RESPONSE_FORWARD_FAILED:
		break;
	case REQUEST_TRIGGER_GET:
	case RESPONSE_TRIGGER_GET:
	case REQUEST_TRIGGER_SET:
	case REQUEST_TRIGGER_CLEAR:
	case REQUEST_TRIGGER_PULL:
		rc = _unpack_trigger_msg((trigger_info_msg_t **)
					 &msg->data, buffer,
					 msg->protocol_version);
		break;
	case RESPONSE_SLURMD_STATUS:
		rc = _unpack_slurmd_status((slurmd_status_t **)
					   &msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_JOB_NOTIFY:
		rc =  _unpack_job_notify((job_notify_msg_t **)
					 &msg->data, buffer,
					 msg->protocol_version);
		break;
	case REQUEST_SET_DEBUG_FLAGS:
		rc = _unpack_set_debug_flags_msg(
			(set_debug_flags_msg_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
	case REQUEST_SET_SCHEDLOG_LEVEL:
		rc = _unpack_set_debug_level_msg(
			(set_debug_level_msg_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case ACCOUNTING_UPDATE_MSG:
		rc = _unpack_accounting_update_msg(
			(accounting_update_msg_t **)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_TOPO_INFO:
		rc = _unpack_topo_info_msg(
			(topo_info_response_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_SBCAST_CRED:
		rc = _unpack_job_sbcast_cred_msg(
			(job_sbcast_cred_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_FED_INFO:
		rc = slurmdb_unpack_federation_rec(&msg->data,
						   msg->protocol_version,
						   buffer);
		break;
	case REQUEST_FRONT_END_INFO:
		rc = _unpack_front_end_info_request_msg(
			(front_end_info_request_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_FRONT_END_INFO:
		rc = _unpack_front_end_info_msg(
			(front_end_info_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_STATS_INFO:
		rc = _unpack_stats_request_msg((stats_info_request_msg_t **)
					       &msg->data, buffer,
					       msg->protocol_version);
		break;

	case RESPONSE_STATS_INFO:
		rc = _unpack_stats_response_msg((stats_info_response_msg_t **)
						&msg->data, buffer,
						msg->protocol_version);
		break;

	case REQUEST_FORWARD_DATA:
		rc = _unpack_forward_data_msg((forward_data_msg_t **)&msg->data,
					      buffer, msg->protocol_version);
		break;

	case RESPONSE_PING_SLURMD:
		rc = _unpack_ping_slurmd_resp((ping_slurmd_resp_msg_t **)
					      &msg->data, buffer,
					      msg->protocol_version);
		break;
	case RESPONSE_LICENSE_INFO:
		rc = _unpack_license_info_msg((license_info_msg_t **)&(msg->data),
					      buffer,
					      msg->protocol_version);
		break;
	case REQUEST_LICENSE_INFO:
		rc = _unpack_license_info_request_msg((license_info_request_msg_t **)
						      &(msg->data),
						      buffer,
						      msg->protocol_version);
		break;
	case RESPONSE_JOB_ARRAY_ERRORS:
		rc = _unpack_job_array_resp_msg((job_array_resp_msg_t **)
						&(msg->data), buffer,
						msg->protocol_version);
		break;
	case REQUEST_ASSOC_MGR_INFO:
		rc = _unpack_assoc_mgr_info_request_msg(
			(assoc_mgr_info_request_msg_t **)&(msg->data),
			buffer, msg->protocol_version);
		break;
	case RESPONSE_ASSOC_MGR_INFO:
		rc = assoc_mgr_info_unpack_msg((assoc_mgr_info_msg_t **)
					       &(msg->data),
					       buffer,
					       msg->protocol_version);
		break;
	case REQUEST_NETWORK_CALLERID:
		rc = _unpack_network_callerid_msg((network_callerid_msg_t **)
						  &(msg->data), buffer,
						  msg->protocol_version);
		break;
	case RESPONSE_NETWORK_CALLERID:
		rc = _unpack_network_callerid_resp_msg(
			(network_callerid_resp_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_CTLD_MULT_MSG:
	case RESPONSE_CTLD_MULT_MSG:
		rc = _unpack_buf_list_msg((ctld_list_msg_t **) &(msg->data),
					  buffer, msg->protocol_version);
		break;
	case REQUEST_SET_FS_DAMPENING_FACTOR:
		rc = _unpack_set_fs_dampening_factor_msg(
			(set_fs_dampening_factor_msg_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_CONTROL_STATUS:
		rc = _unpack_control_status_msg(
			(control_status_msg_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_BURST_BUFFER_STATUS:
		rc = _unpack_bb_status_req_msg(
			(bb_status_req_msg_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_BURST_BUFFER_STATUS:
		rc = _unpack_bb_status_resp_msg(
			(bb_status_resp_msg_t **)&(msg->data), buffer,
			msg->protocol_version);
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
	default:
		debug("No unpack method for msg type %u", msg->msg_type);
		return EINVAL;
		break;
	}

	if (rc) {
		error("Malformed RPC of type %s(%u) received",
		      rpc_num2string(msg->msg_type), msg->msg_type);
	}
	return rc;
}

extern void pack_step_id(slurm_step_id_t *msg, buf_t *buffer,
			 uint16_t protocol_version)
{
	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->step_het_comp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack_old_step_id(msg->step_id, buffer);
	}
}

extern int unpack_step_id_members(slurm_step_id_t *msg, buf_t *buffer,
				  uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->step_id, buffer);
		safe_unpack32(&msg->step_het_comp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->step_id, buffer);
		convert_old_step_id(&msg->step_id);
		msg->step_het_comp = NO_VAL;
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}


extern int unpack_step_id(slurm_step_id_t **msg_ptr, buf_t *buffer,
			  uint16_t protocol_version)
{
	slurm_step_id_t *msg;

	msg = xmalloc(sizeof(*msg));
	*msg_ptr = msg;

	if (unpack_step_id_members(msg, buffer, protocol_version) ==
	    SLURM_SUCCESS)
		return SLURM_SUCCESS;

	slurm_free_step_id(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

/*
 * Remove these 2 functions pack_old_step_id and convert_old_step_id 2 versions
 * after 20.11.
 */
extern void pack_old_step_id(uint32_t step_id, buf_t *buffer)
{
	if (step_id == SLURM_BATCH_SCRIPT)
		pack32(NO_VAL, buffer);
	else if (step_id == SLURM_EXTERN_CONT)
		pack32(INFINITE, buffer);
	else
		pack32(step_id, buffer);
}

extern void convert_old_step_id(uint32_t *step_id)
{
	if (*step_id == NO_VAL)
		*step_id = SLURM_BATCH_SCRIPT;
	else if (*step_id == INFINITE)
		*step_id = SLURM_EXTERN_CONT;
}

extern void slurm_pack_selected_step(void *in, uint16_t protocol_version,
				     buf_t *buffer)
{
	slurm_selected_step_t *step = (slurm_selected_step_t *) in;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack_step_id(&step->step_id, buffer, protocol_version);
		pack32(step->array_task_id, buffer);
		pack32(step->het_job_offset, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(step->array_task_id, buffer);
		pack32(step->step_id.job_id, buffer);
		pack32(step->het_job_offset, buffer);
		pack_old_step_id(step->step_id.step_id, buffer);
	}
}

extern int slurm_unpack_selected_step(slurm_selected_step_t **step,
				      uint16_t protocol_version, buf_t *buffer)
{
	slurm_selected_step_t *step_ptr = xmalloc(sizeof(*step_ptr));

	*step = step_ptr;

	step_ptr->array_task_id = NO_VAL;

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&step_ptr->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&step_ptr->array_task_id, buffer);
		safe_unpack32(&step_ptr->het_job_offset, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&step_ptr->array_task_id, buffer);
		safe_unpack32(&step_ptr->step_id.job_id, buffer);
		safe_unpack32(&step_ptr->het_job_offset, buffer);
		safe_unpack32(&step_ptr->step_id.step_id, buffer);
		/*
		 * convert_old_step_id will not convert step_id correctly in
		 * this particular situation.
		 * Old Slurm used to use INFINITE To denote the batch script.
		 * The extern step was not searchable before 20.11. NO_VAL meant
		 * not set.
		 */
		if (step_ptr->step_id.step_id == INFINITE)
			step_ptr->step_id.step_id = SLURM_BATCH_SCRIPT;
		step_ptr->step_id.step_het_comp = NO_VAL;
	} else
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_selected_step(step_ptr);
	*step = NULL;
	return SLURM_ERROR;
}
