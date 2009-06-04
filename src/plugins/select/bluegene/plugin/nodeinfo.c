/*****************************************************************************\
 *  nodeinfo.c - functions used for the select_nodeinfo_t structure
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include "nodeinfo.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "bluegene.h"

static void _free_node_subgrp(void *object)
{
	node_subgrp_t *subgrp = (node_subgrp_t *)object;
	if(subgrp) {
		FREE_NULL_BITMAP(subgrp->bitmap);
		xfree(subgrp->str);
		xfree(subgrp->inx);
		xfree(subgrp);
	}
}

static node_subgrp_t *_find_subgrp(List subgrp_list, enum node_states state,
				   uint16_t size)
{
	node_subgrp_t *subgrp = NULL;
	ListIterator itr;
	xassert(subgrp_list);
	itr = list_iterator_create(subgrp_list);
	while((subgrp = list_next(itr))) {
		if(subgrp->state == state)
			break;			
	}
	list_iterator_destroy(itr);
	if(!subgrp) {
		subgrp = xmalloc(sizeof(node_subgrp_t));
		subgrp->state = state;
		subgrp->bitmap = bit_alloc(size);		
		list_append(subgrp_list, subgrp);
	}

	return subgrp;
}

static int _pack_node_subgrp(node_subgrp_t *subgrp, Buf buffer)
{
	pack_bit_fmt(subgrp->bitmap, buffer);
	pack16(subgrp->node_cnt, buffer);
	pack16(subgrp->state, buffer);

	return SLURM_SUCCESS;
}

static int _unpack_node_subgrp(node_subgrp_t **subgrp_pptr, Buf buffer,
			       uint16_t bitmap_size)
{
	node_subgrp_t *subgrp = xmalloc(sizeof(node_subgrp_t));
	int j;
	uint32_t uint32_tmp;
	uint16_t uint16_tmp;

	*subgrp_pptr = subgrp;

	safe_unpackstr_xmalloc(&subgrp->str, &uint32_tmp, buffer);
	if (!subgrp->str) 
		subgrp->inx = bitfmt2int("");
	else
		subgrp->inx = bitfmt2int(subgrp->str);

	subgrp->bitmap = bit_alloc(bitmap_size);

	j = 0;
	while(subgrp->inx[j] >= 0) {
		bit_nset(subgrp->bitmap, subgrp->inx[j], subgrp->inx[j+1]);
		j+=2;
	}

	safe_unpack16(&subgrp->node_cnt, buffer);
	safe_unpack16(&uint16_tmp, buffer);
	subgrp->state = uint16_tmp;

	return SLURM_SUCCESS;

unpack_error:
	_free_node_subgrp(subgrp);
	*subgrp_pptr = NULL;
	return SLURM_ERROR;
}

extern int select_nodeinfo_pack(select_nodeinfo_t *nodeinfo, Buf buffer)
{
	ListIterator itr;
	node_subgrp_t *subgrp = NULL;
	uint16_t count = 0;

	pack16(nodeinfo->bitmap_size, buffer);

	if(nodeinfo->subgrp_list) 
		count = list_count(nodeinfo->subgrp_list);

	pack16(count, buffer);

	if(count > 0) {
		itr = list_iterator_create(nodeinfo->subgrp_list);
		while((subgrp = list_next(itr))) {
			_pack_node_subgrp(subgrp, buffer);
		}
		list_iterator_destroy(itr);
	}
	return SLURM_SUCCESS;
}

extern int select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo, Buf buffer)
{
	uint16_t size = 0;
	select_nodeinfo_t *nodeinfo_ptr = NULL;
	uint32_t j = 0;

	safe_unpack16(&size, buffer);

	nodeinfo_ptr = select_nodeinfo_alloc((uint32_t)size);
	*nodeinfo = nodeinfo_ptr;

	safe_unpack16(&size, buffer);
	nodeinfo_ptr->subgrp_list = list_create(_free_node_subgrp);
	for(j=0; j<size; j++) {
		node_subgrp_t *subgrp = NULL;
		if(_unpack_node_subgrp(&subgrp, buffer, 
				       nodeinfo_ptr->bitmap_size)
		   != SLURM_SUCCESS)
			goto unpack_error;
		list_append(nodeinfo_ptr->subgrp_list, subgrp);		
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

	if(bg_conf && (!size || size == NO_VAL))
		size = bg_conf->numpsets;

	nodeinfo->bitmap_size = size;
	nodeinfo->magic = NODEINFO_MAGIC;
	nodeinfo->subgrp_list = list_create(_free_node_subgrp);
	return nodeinfo;
}

extern int select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if(nodeinfo) {
		if (nodeinfo->magic != NODEINFO_MAGIC) {
			error("free_nodeinfo: nodeinfo magic bad");
			return EINVAL;
		} 
		nodeinfo->magic = 0;
		if(nodeinfo->subgrp_list)
			list_destroy(nodeinfo->subgrp_list);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_nodeinfo_set_all(time_t last_query_time)
{
	ListIterator itr = NULL;
	struct node_record *node_ptr = NULL;
	int i=0;
	bg_record_t *bg_record = NULL;
	static time_t last_set_all = 0;

	if(!blocks_are_created) 
		return SLURM_NO_CHANGE_IN_DATA;
	
	/* only set this once when the last_bg_update is newer than
	   the last time we set things up. */
	if(last_set_all && (last_bg_update < last_set_all)) {
		debug2("Node select info for set all hasn't "
		       "changed since %d", 
		       last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;	
	}
	last_set_all = last_bg_update;
	
	/* set this here so we know things have changed */
	last_node_update = time(NULL);
	
	slurm_mutex_lock(&block_state_mutex);
	for (i=0; i<node_record_count; i++) {
		node_ptr = &(node_record_table_ptr[i]);
		xassert(node_ptr->select_nodeinfo);
		xassert(node_ptr->select_nodeinfo->subgrp_list);
		list_flush(node_ptr->select_nodeinfo->subgrp_list);
		if(node_ptr->select_nodeinfo->bitmap_size != bg_conf->numpsets)
			node_ptr->select_nodeinfo->bitmap_size =
				bg_conf->numpsets;
	}
	itr = list_iterator_create(bg_lists->main);
	while((bg_record = list_next(itr))) {
		enum node_states state = NODE_STATE_UNKNOWN;
		node_subgrp_t *subgrp = NULL;

		/* Only mark small unidle blocks */
		if((bg_record->conn_type < SELECT_SMALL)
		   || (bg_record->job_running == NO_JOB_RUNNING)) 
			continue;

		if(bg_record->state == RM_PARTITION_ERROR) 
			state = NODE_STATE_ERROR;
		else if(bg_record->job_running > NO_JOB_RUNNING) 
			state = NODE_STATE_ALLOCATED;
		else {
			error("not sure why we got here with block %s",
			      bg_record->bg_block_id);
			continue;
		}

		for(i=0; i<node_record_count; i++) {
			if(!bit_test(bg_record->bitmap, i))
				continue;
			node_ptr = &(node_record_table_ptr[i]);

			subgrp = _find_subgrp(
				node_ptr->select_nodeinfo->subgrp_list,
				state, bg_conf->numpsets);
 			
			if(subgrp->node_cnt < bg_conf->bp_node_cnt) {
				bit_or(subgrp->bitmap,
				       bg_record->ionode_bitmap);
				subgrp->node_cnt += bg_record->node_cnt;
			}
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);

	return SLURM_SUCCESS;
}

extern int select_nodeinfo_set(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_nodeinfo_get(select_nodeinfo_t *nodeinfo, 
			       enum select_nodedata_type dinfo,
			       enum node_states state,
			       void *data)
{
	int rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	bitstr_t **bitmap = (bitstr_t **) data;
	char **tmp_char = (char **) data;
	ListIterator itr = NULL;
	node_subgrp_t *subgrp = NULL;

	if (nodeinfo == NULL) {
		error("get_nodeinfo: nodeinfo not set");
		return SLURM_ERROR;
	}
	
	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("get_nodeinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (dinfo) {
	case SELECT_NODEDATA_BITMAP_SIZE:
		*uint16 = nodeinfo->bitmap_size;
		break;
	case SELECT_NODEDATA_SUBGRP_SIZE:
		*uint16 = 0;
		if(!nodeinfo->subgrp_list) 
			return SLURM_ERROR;
		*uint16 = list_count(nodeinfo->subgrp_list);
		break;
	case SELECT_NODEDATA_SUBCNT:
		*uint16 = 0;
		if(!nodeinfo->subgrp_list) 
			return SLURM_ERROR;
		itr = list_iterator_create(nodeinfo->subgrp_list);
		while((subgrp = list_next(itr))) {
			if(subgrp->state == state) {
				*uint16 = subgrp->node_cnt;	
				break;
			}
		}
		list_iterator_destroy(itr);
		break;
	case SELECT_NODEDATA_BITMAP:
		*bitmap = NULL;
		if(!nodeinfo->subgrp_list) 
			return SLURM_ERROR;
		itr = list_iterator_create(nodeinfo->subgrp_list);
		while((subgrp = list_next(itr))) {
			if(subgrp->state == state) {
				*bitmap = bit_copy(subgrp->bitmap);	
				break;
			}
		}
		list_iterator_destroy(itr);
		break;
	case SELECT_NODEDATA_STR:
		*tmp_char = NULL;
		if(!nodeinfo->subgrp_list) 
			return SLURM_ERROR;
		itr = list_iterator_create(nodeinfo->subgrp_list);
		while((subgrp = list_next(itr))) {
			if(subgrp->state == state) {
				*tmp_char = xstrdup(subgrp->str);	
				break;
			}
		}
		list_iterator_destroy(itr);
		break;
	default:
		error("Unsupported option %d for get_nodeinfo.", dinfo);
		rc = SLURM_ERROR;
		break;
	}	
	return rc;
}
