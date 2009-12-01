/*****************************************************************************\
 *  nodeinfo.h - definitions of functions used for the select_nodeinfo_t
 *              structure
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

#ifndef _HAVE_SELECT_NODEINFO_H
#define _HAVE_SELECT_NODEINFO_H

#include "src/common/node_select.h"
#define NODEINFO_MAGIC 0x85ac

typedef struct {
	bitstr_t *bitmap;
	int *inx;
	uint16_t node_cnt;
	enum node_states state;
	char *str;
} node_subgrp_t;

struct select_nodeinfo {
	uint16_t bitmap_size;
	uint16_t magic;		/* magic number */
	List subgrp_list;
};

extern int select_nodeinfo_pack(select_nodeinfo_t *nodeinfo, Buf buffer);

extern int select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo, Buf buffer);

extern select_nodeinfo_t *select_nodeinfo_alloc(uint32_t size);

extern int select_nodeinfo_free(select_nodeinfo_t *nodeinfo);

extern int select_nodeinfo_set_all(time_t last_query_time);

extern int select_nodeinfo_set(struct job_record *job_ptr);

extern int select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
			       enum select_nodedata_type dinfo,
			       enum node_states state,
			       void *data);

#endif
