/*****************************************************************************\
 *  bg_node_info.c - functions used for the select_nodeinfo_t structure
 *****************************************************************************
 *  Copyright (C) 2009-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "src/common/slurm_xlator.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "bg_core.h"

static uint32_t g_bitmap_size = 0;

static void _free_node_subgrp(void *object)
{
	node_subgrp_t *subgrp = (node_subgrp_t *)object;
	if (subgrp) {
		FREE_NULL_BITMAP(subgrp->bitmap);
		xfree(subgrp->str);
		xfree(subgrp->inx);
		xfree(subgrp);
	}
}

static node_subgrp_t *_create_subgrp(List subgrp_list, enum node_states state,
				     uint16_t size)
{
	node_subgrp_t *subgrp = xmalloc(sizeof(node_subgrp_t));
	subgrp->state = state;
	subgrp->bitmap = bit_alloc(size);
	list_append(subgrp_list, subgrp);

	return subgrp;
}

static node_subgrp_t *_find_subgrp(List subgrp_list, enum node_states state,
				   uint16_t size)
{
	node_subgrp_t *subgrp = NULL;
	ListIterator itr;
	xassert(subgrp_list);
	itr = list_iterator_create(subgrp_list);
	while ((subgrp = list_next(itr))) {
		if (subgrp->state == state)
			break;
	}
	list_iterator_destroy(itr);

	if (!subgrp)
		subgrp = _create_subgrp(subgrp_list, state, size);

	return subgrp;
}

static int _pack_node_subgrp(node_subgrp_t *subgrp, Buf buffer,
			     uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_bit_fmt(subgrp->bitmap, buffer);
		pack16(subgrp->cnode_cnt, buffer);
		pack16(subgrp->state, buffer);
	}

	return SLURM_SUCCESS;
}

static int _unpack_node_subgrp(node_subgrp_t **subgrp_pptr, Buf buffer,
			       uint16_t bitmap_size, uint16_t protocol_version)
{
	node_subgrp_t *subgrp = xmalloc(sizeof(node_subgrp_t));
	int j;
	uint32_t uint32_tmp;
	uint16_t uint16_tmp;

	*subgrp_pptr = subgrp;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&subgrp->str, &uint32_tmp, buffer);
		if (!subgrp->str)
			subgrp->inx = bitfmt2int("");
		else
			subgrp->inx = bitfmt2int(subgrp->str);

		subgrp->bitmap = bit_alloc(bitmap_size);

		j = 0;
		while (subgrp->inx[j] >= 0) {
			bit_nset(subgrp->bitmap, subgrp->inx[j],
				 subgrp->inx[j+1]);
			j+=2;
		}

		safe_unpack16(&subgrp->cnode_cnt, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		subgrp->state = uint16_tmp;
	}
	return SLURM_SUCCESS;

unpack_error:
	_free_node_subgrp(subgrp);
	*subgrp_pptr = NULL;
	return SLURM_ERROR;
}

extern int select_nodeinfo_pack(select_nodeinfo_t *nodeinfo, Buf buffer,
				uint16_t protocol_version)
{
	ListIterator itr;
	node_subgrp_t *subgrp = NULL;
	uint16_t count = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(nodeinfo->bitmap_size, buffer);

		packstr(nodeinfo->extra_info, buffer);
		packstr(nodeinfo->failed_cnodes, buffer);

		if (nodeinfo->ba_mp)
			packstr(nodeinfo->ba_mp->loc, buffer);
		else
			packstr(nodeinfo->rack_mp, buffer);

		if (nodeinfo->subgrp_list)
			count = list_count(nodeinfo->subgrp_list);

		pack16(count, buffer);

		if (count > 0) {
			itr = list_iterator_create(nodeinfo->subgrp_list);
			while ((subgrp = list_next(itr))) {
				_pack_node_subgrp(subgrp, buffer,
						  protocol_version);
			}
			list_iterator_destroy(itr);
		}
	} else {
 		error("select_nodeinfo_pack: protocol_version "
 		      "%hu not supported", protocol_version);
	}

	return SLURM_SUCCESS;
}

extern int select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo, Buf buffer,
				  uint16_t protocol_version)
{
	uint16_t size = 0;
	select_nodeinfo_t *nodeinfo_ptr = NULL;
	uint32_t j = 0;
	uint32_t uint32_tmp;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&size, buffer);

		nodeinfo_ptr = select_nodeinfo_alloc((uint32_t)size);
		*nodeinfo = nodeinfo_ptr;

		safe_unpackstr_xmalloc(&nodeinfo_ptr->extra_info,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&nodeinfo_ptr->failed_cnodes,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&nodeinfo_ptr->rack_mp,
				       &uint32_tmp, buffer);

		safe_unpack16(&size, buffer);
		nodeinfo_ptr->subgrp_list = list_create(_free_node_subgrp);
		for (j=0; j<size; j++) {
			node_subgrp_t *subgrp = NULL;
			if (_unpack_node_subgrp(&subgrp, buffer,
						nodeinfo_ptr->bitmap_size,
						protocol_version)
			    != SLURM_SUCCESS)
				goto unpack_error;
			list_append(nodeinfo_ptr->subgrp_list, subgrp);
		}
	} else {
 		error("select_nodeinfo_unpack: protocol_version "
 		      "%hu not supported", protocol_version);
	}
	return SLURM_SUCCESS;

unpack_error:
	error("select_nodeinfo_unpack: error unpacking here");
	select_nodeinfo_free(nodeinfo_ptr);
	*nodeinfo = NULL;

	return SLURM_ERROR;
}

extern select_nodeinfo_t *select_nodeinfo_alloc(uint32_t size)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));
	//uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (bg_conf) {
		if (!g_bitmap_size) {
		/* if (cluster_flags & CLUSTER_FLAG_BGQ) */
		/* 	g_bitmap_size = bg_conf->mp_cnode_cnt; */
		/* else */
			g_bitmap_size = bg_conf->ionodes_per_mp;
		}

		if (!size || size == NO_VAL)
			size = g_bitmap_size;
	}

	nodeinfo->bitmap_size = size;
	nodeinfo->magic = NODEINFO_MAGIC;
	nodeinfo->subgrp_list = list_create(_free_node_subgrp);
	return nodeinfo;
}

extern int select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (nodeinfo) {
		if (nodeinfo->magic != NODEINFO_MAGIC) {
			error("free_nodeinfo: nodeinfo magic bad");
			return EINVAL;
		}
		nodeinfo->magic = 0;
		xfree(nodeinfo->extra_info);
		xfree(nodeinfo->failed_cnodes);
		xfree(nodeinfo->rack_mp);
		if (nodeinfo->subgrp_list)
			list_destroy(nodeinfo->subgrp_list);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_nodeinfo_set_all(void)
{
	ListIterator itr = NULL;
	struct node_record *node_ptr = NULL;
	int i=0;
	bg_record_t *bg_record = NULL;
	static time_t last_set_all = 0;
	ba_mp_t *ba_mp;
	node_subgrp_t *subgrp = NULL;
	int bit_count;

	//uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (!blocks_are_created)
		return SLURM_NO_CHANGE_IN_DATA;

	if (!g_bitmap_size) {
		/* if (cluster_flags & CLUSTER_FLAG_BGQ) */
		/* 	g_bitmap_size = bg_conf->mp_cnode_cnt; */
		/* else */
			g_bitmap_size = bg_conf->ionodes_per_mp;
	}

	/* only set this once when the last_bg_update is newer than
	   the last time we set things up. */
	if (last_set_all && (last_bg_update-1 < last_set_all)) {
		debug2("Node select info for set all hasn't "
		       "changed since %ld",
		       last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;
	}
	last_set_all = last_bg_update;

	/* set this here so we know things have changed */
	last_node_update = time(NULL);

	slurm_mutex_lock(&block_state_mutex);
	for (i=0; i<node_record_count; i++) {
		select_nodeinfo_t *nodeinfo;

		node_ptr = &(node_record_table_ptr[i]);
		xassert(node_ptr->select_nodeinfo);
		nodeinfo = node_ptr->select_nodeinfo->data;
		xassert(nodeinfo);
		xassert(nodeinfo->subgrp_list);
		list_flush(nodeinfo->subgrp_list);
		if (nodeinfo->bitmap_size != g_bitmap_size)
			nodeinfo->bitmap_size = g_bitmap_size;
	}

	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = list_next(itr))) {
		enum node_states state = NODE_STATE_UNKNOWN;
		select_nodeinfo_t *nodeinfo;
		bitstr_t *bitmap;
		ListIterator itr2 = NULL;

		/* Only mark unidle blocks */
		if (bg_record->job_list && list_count(bg_record->job_list)) {
			struct job_record *job_ptr;
			select_jobinfo_t *jobinfo;
			ListIterator itr =
				list_iterator_create(bg_record->job_list);
			ba_mp = list_peek(bg_record->ba_mp_list);
			node_ptr = &(node_record_table_ptr[ba_mp->index]);
			xassert(node_ptr->select_nodeinfo);
			nodeinfo = node_ptr->select_nodeinfo->data;
			xassert(nodeinfo);
			xassert(nodeinfo->subgrp_list);
			if (ba_mp->cnode_err_bitmap
			    && (bit_count =
				bit_set_count(ba_mp->cnode_err_bitmap))) {
				subgrp = _find_subgrp(nodeinfo->subgrp_list,
						      NODE_STATE_ERROR,
						      g_bitmap_size);
				/* FIXME: the subgrp->bitmap isn't set here. */
				subgrp->cnode_cnt += bit_count;
			}

			subgrp = _find_subgrp(nodeinfo->subgrp_list,
					      NODE_STATE_ALLOCATED,
					      g_bitmap_size);
			while ((job_ptr = list_next(itr))) {
				jobinfo = job_ptr->select_jobinfo->data;
				/* FIXME: the subgrp->bitmap isn't set here. */
				subgrp->cnode_cnt += jobinfo->cnode_cnt;
			}
			list_iterator_destroy(itr);
			continue;
		} else if (bg_record->job_running == NO_JOB_RUNNING)
			continue;

		if (bg_record->state & BG_BLOCK_ERROR_FLAG)
			state = NODE_STATE_ERROR;
		else if (bg_record->job_running > NO_JOB_RUNNING) {
			/* we don't need to set the allocated here
			 * since the whole midplane is allocated */
			if (bg_record->conn_type[0] < SELECT_SMALL)
				continue;
			state = NODE_STATE_ALLOCATED;
		} else {
			error("not sure why we got here with block %s %s",
			      bg_record->bg_block_id,
			      bg_block_state_string(bg_record->state));
			continue;
		}
		/* if ((cluster_flags & CLUSTER_FLAG_BGQ) */
		/*     && (state != NODE_STATE_ERROR)) */
		/* 	bitmap = bg_record->cnodes_used_bitmap; */
		/* else */
		bitmap = bg_record->ionode_bitmap;

		itr2 = list_iterator_create(bg_record->ba_mp_list);
		while ((ba_mp = list_next(itr2))) {
			if (!ba_mp->used)
				continue;

			node_ptr = &(node_record_table_ptr[ba_mp->index]);

			xassert(node_ptr->select_nodeinfo);
			nodeinfo = node_ptr->select_nodeinfo->data;
			xassert(nodeinfo);
			xassert(nodeinfo->subgrp_list);

			if (ba_mp->cnode_err_bitmap
			    && (state == NODE_STATE_ALLOCATED)
			    && (bit_count =
				bit_set_count(ba_mp->cnode_err_bitmap))) {
				subgrp = _find_subgrp(nodeinfo->subgrp_list,
						      NODE_STATE_ERROR,
						      g_bitmap_size);
				/* FIXME: the subgrp->bitmap isn't set here. */
				subgrp->cnode_cnt += bit_count;
			}

			subgrp = _find_subgrp(nodeinfo->subgrp_list,
					      state, g_bitmap_size);

			if (subgrp->cnode_cnt < bg_conf->mp_cnode_cnt) {
				/* if (cluster_flags & CLUSTER_FLAG_BGQ) { */
				/* 	bit_or(subgrp->bitmap, bitmap); */
				/* 	subgrp->cnode_cnt += */
				/* 		bit_set_count(bitmap); */
				/* } else */ if (bg_record->cnode_cnt
					   < bg_conf->mp_cnode_cnt) {
					bit_or(subgrp->bitmap, bitmap);
					subgrp->cnode_cnt +=
						bg_record->cnode_cnt;
				} else {
					bit_nset(subgrp->bitmap,
						 0,
						 (g_bitmap_size-1));
					subgrp->cnode_cnt =
						bg_conf->mp_cnode_cnt;
				}
			}
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);

	return SLURM_SUCCESS;
}

extern int select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
			       enum select_nodedata_type dinfo,
			       enum node_states state,
			       void *data)
{
	int rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint32_t *uint32 = (uint32_t *) data;
	bitstr_t **bitmap = (bitstr_t **) data;
	char **tmp_char = (char **) data;
	ListIterator itr = NULL;
	node_subgrp_t *subgrp = NULL;

	if (nodeinfo == NULL) {
		error("get_nodeinfo: nodeinfo not set");
		return SLURM_ERROR;
	}

	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("get_nodeinfo: nodeinfo magic bad");
		return SLURM_ERROR;
	}

	switch (dinfo) {
	case SELECT_NODEDATA_BITMAP_SIZE:
		*uint16 = nodeinfo->bitmap_size;
		break;
	case SELECT_NODEDATA_SUBGRP_SIZE:
		*uint16 = 0;
		if (!nodeinfo->subgrp_list)
			return SLURM_ERROR;
		*uint16 = list_count(nodeinfo->subgrp_list);
		break;
	case SELECT_NODEDATA_SUBCNT:
		*uint16 = 0;
		if (!nodeinfo->subgrp_list)
			return SLURM_ERROR;
		itr = list_iterator_create(nodeinfo->subgrp_list);
		while ((subgrp = list_next(itr))) {
			if (subgrp->state == state) {
				*uint16 = subgrp->cnode_cnt;
				break;
			}
		}
		list_iterator_destroy(itr);
		break;
	case SELECT_NODEDATA_BITMAP:
		*bitmap = NULL;
		if (!nodeinfo->subgrp_list)
			return SLURM_ERROR;
		itr = list_iterator_create(nodeinfo->subgrp_list);
		while ((subgrp = list_next(itr))) {
			if (subgrp->state == state) {
				*bitmap = bit_copy(subgrp->bitmap);
				break;
			}
		}
		list_iterator_destroy(itr);
		break;
	case SELECT_NODEDATA_RACK_MP:
		if (nodeinfo->ba_mp)
			*tmp_char = xstrdup(nodeinfo->ba_mp->loc);
		else if (nodeinfo->rack_mp)
			*tmp_char = xstrdup(nodeinfo->rack_mp);
		break;
	case SELECT_NODEDATA_STR:
		*tmp_char = NULL;
		if (!nodeinfo->subgrp_list)
			return SLURM_ERROR;
		itr = list_iterator_create(nodeinfo->subgrp_list);
		while ((subgrp = list_next(itr))) {
			if (subgrp->state == state) {
				*tmp_char = xstrdup(subgrp->str);
				break;
			}
		}
		list_iterator_destroy(itr);
		break;
	case SELECT_NODEDATA_EXTRA_INFO:
		if (nodeinfo->extra_info)
			*tmp_char = xstrdup(nodeinfo->extra_info);
		if (nodeinfo->failed_cnodes)
			xstrfmtcat(*tmp_char, "Failed cnodes=%s",
				   nodeinfo->failed_cnodes);
		break;
	case SELECT_NODEDATA_MEM_ALLOC:
		*uint32 = 0;
		break;
	default:
		error("Unsupported option %d for get_nodeinfo.", dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}
