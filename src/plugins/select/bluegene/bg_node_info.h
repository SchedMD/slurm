/*****************************************************************************\
 *  bg_node_info.h - definitions of functions used for the select_nodeinfo_t
 *              structure
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

#ifndef _HAVE_SELECT_NODEINFO_H
#define _HAVE_SELECT_NODEINFO_H

#include "src/common/node_select.h"
#include "ba_common.h"
#define NODEINFO_MAGIC 0x85ac

typedef struct {
	bitstr_t *bitmap;
	uint16_t cnode_cnt;
	int32_t *inx;
	enum node_states state;
	char *str;
} node_subgrp_t;

struct select_nodeinfo {
	ba_mp_t *ba_mp;
	uint16_t bitmap_size;
	char *extra_info;       /* Currently used to tell if a cable
				   is in an error state.
				*/
	char *failed_cnodes;   /* Currently used to any cnodes are in
				   an SoftwareFailure state.
				*/
	uint16_t magic;		/* magic number */
	char *rack_mp;          /* name of midplane in rack - midplane
				   format */
	List subgrp_list;
};

extern int select_nodeinfo_pack(select_nodeinfo_t *nodeinfo, Buf buffer,
				uint16_t protocol_version);

extern int select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo, Buf buffer,
				  uint16_t protocol_version);

extern select_nodeinfo_t *select_nodeinfo_alloc(uint32_t size);

extern int select_nodeinfo_free(select_nodeinfo_t *nodeinfo);

extern int select_nodeinfo_set_all(void);

extern int select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
			       enum select_nodedata_type dinfo,
			       enum node_states state,
			       void *data);

#endif
