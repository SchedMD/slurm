/*****************************************************************************\
 *  src/common/mapping.c - routines for compact process mapping representation
 *****************************************************************************
 *  Copyright (C) 2014 Institute of Semiconductor Physics (ISP SB RAS),
 *                                                         Novosibirsk, Russia.
 *  Written by Artem Polyakov <artpol84@gmail.com>. All rights reserved.
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

#include <stdint.h>
#include <string.h>
#include "xmalloc.h"
#include "xassert.h"
#include "xstring.h"
#include "slurm/slurm_errno.h"

BEGIN_C_DECLS

char *pack_process_mapping(uint32_t node_cnt, uint32_t task_cnt, uint16_t *tasks, uint32_t **tids)
{
	int offset, i;
	int start_node, end_node;
	char *packing = NULL;
	// next_task[i] - next process for processing
	uint16_t *next_task = xmalloc (node_cnt * sizeof(uint16_t));
	memset(next_task, 0, node_cnt * sizeof(uint16_t));
	packing = xstrdup("(vector");
	offset = 0;
	while( offset < task_cnt ){
		start_node = end_node = 0;
		// find the task with id == offset
		for(i=0;i<node_cnt; i++){
			if( next_task[i] < tasks[i] ){
				// if we didn't consume entire quota on this node
				xassert(offset >= tids[i][next_task[i]]);
				if( offset == tids[i][next_task[i]] ){
					start_node = i;
					break;
				}
			}
		}
		// Now we know idx of the first node in this bar
		int mapped = 0;    // mapped on this iteration
		end_node = node_cnt; // assume that bar lasts till the last host
		int depth = -1;
		for(i = start_node; i < end_node; i++){
			if( next_task[i] >= tasks[i] ){
				// Save first non-matching node index
				// and interrupt loop
				end_node = i;
				continue;
			}
			int j;
			for(j = next_task[i]; ((j + 1) < tasks[i]) && ((tids[i][j] + 1) == tids[i][j+1]); j++);
			j++;
			// First run determines the depth
			if( depth < 0 ){
				depth = j - next_task[i];
			}
			if( depth == (j - next_task[i]) ){
				mapped += depth;
				next_task[i] = j;
			} else {
				// Save first non-matching node index
				// and interrupt loop
				end_node = i;
			}
		}
		xstrfmtcat(packing,",(%u,%u,%u)", start_node, end_node - start_node, depth);
		offset += mapped;
	}
	xstrcat(packing,")");
	return packing;
}

END_C_DECLS
