/*****************************************************************************\
 *  mapping.c - routines for compact process mapping representation
 *****************************************************************************
 *  Copyright (C) 2014 Institute of Semiconductor Physics
 *                     Siberian Branch of Russian Academy of Science
 *  Written by Artem Polyakov <artpol84@gmail.com>.
 *  All rights reserved.
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

#define _GNU_SOURCE

#include "src/plugins/mpi/pmix/mapping.h"

static void _dump_config(uint32_t node_cnt, uint32_t task_cnt,
			 uint16_t *tasks, uint32_t **tids, int offset)
{
	int i, j;

	error("%s: Unable to find task offset %d", __func__, offset);
	for (i = 0; i < node_cnt; i++) {
		for (j = 0; j < tasks[i]; j++) {
			error("TIDS[%d][%d]:%u", i, j, tids[i][j]);
		}
	}
}

/*
 * pack_process_mapping() - Return NULL on error
 */
char *
pack_process_mapping(uint32_t node_cnt,
		     uint32_t task_cnt,
		     uint16_t *tasks,
		     uint32_t **tids)
{
	int offset, i;
	int start_node, end_node;
	char *packing = NULL;

	/*
	 * next_task[i] - next process for processing
	 */
	uint16_t *next_task = xcalloc(node_cnt, sizeof(uint16_t));

	packing = xstrdup("(vector");
	offset = 0;
	while (offset < task_cnt) {
		int mapped = 0;
		int depth = -1;
		int j;
		start_node = end_node = 0;

		/* find the task with id == offset */
		for (i = 0; i < node_cnt; i++) {

			if (next_task[i] < tasks[i]) {
				/*
				 * if we didn't consume entire
				 * quota on this node
				 */
				if (offset > tids[i][next_task[i]]) {
					_dump_config(node_cnt, task_cnt,
						     tasks, tids, offset);
					abort();
				}
				if (offset == tids[i][next_task[i]]) {
					start_node = i;
					break;
				}
			}
		}

		end_node = node_cnt;
		for (i = start_node; i < end_node; i++) {
			if (next_task[i] >= tasks[i] ) {
				/*
				 * Save first non-matching node index
				 * and interrupt loop
				 */
				end_node = i;
				continue;
			}

			for (j = next_task[i]; ((j + 1) < tasks[i])
				     && ((tids[i][j]+1) == tids[i][j+1]); j++);
			j++;
			/*
			 * First run determines the depth
			 */
			if (depth < 0) {
				depth = j - next_task[i];
			} else {
				/*
				 * If this is not the first node in the bar
				 * check that: 1. First tid on this node is
				 * sequentially next after last tid
				 *    on the previous node
				 */
				if (tids[i-1][next_task[i-1]-1] + 1
				    != tids[i][next_task[i]]) {
					end_node = i;
					continue;
				}
			}

			if (depth == (j - next_task[i])) {
				mapped += depth;
				next_task[i] = j;
			} else {
				/*
				 * Save first non-matching node index
				 * and interrupt loop
				 */
				end_node = i;
			}
		}

		if (depth < 0) {
			xfree(next_task);
			xfree(packing);
			return NULL;
		}

		xstrfmtcat(packing,",(%u,%u,%u)",
			   start_node, end_node - start_node, depth);
		offset += mapped;
	}
	xfree(next_task);
	xstrcat(packing,")");
	return packing;
}

uint32_t *
unpack_process_mapping_flat(char *map,
			    uint32_t node_cnt,
			    uint32_t task_cnt,
			    uint16_t *tasks)
{
	/*
	 * Start from the flat array. For i'th task is located
	 * on the task_map[i]'th node
	 */
	uint32_t *task_map = xcalloc(task_cnt, sizeof(int));
	char *prefix = "(vector,", *p = NULL;
	uint32_t taskid, i;

	if (tasks) {
		for (i = 0; i < node_cnt; i++) {
			tasks[i] = 0;
		}
	}

	if ((p = strstr(map, prefix)) == NULL) {
		error("\
unpack_process_mapping: The mapping string should start from %s", prefix);
		goto err_exit;
	}

	/*
	 * Skip prefix
	 */
	p += strlen(prefix);
	taskid = 0;
	while ((p = strchr(p,'('))) {
		int depth, node, end_node;
		p++;
		if (3!= sscanf(p,"%d,%d,%d", &node, &end_node, &depth)) {
			goto err_exit;
		}
		end_node += node;
		xassert(node < node_cnt && end_node <= node_cnt );
		for (; node < end_node; node++) {
			for (i = 0; i < depth; i++){
				task_map[taskid++] = node;
				if (tasks != NULL) {
					/*
					 * Cont tasks on each node if was
					 * requested
					 */
					tasks[node]++;
				}
			}
		}
	}
	return task_map;
err_exit:
	xfree(task_map);
	return NULL;
}

int
unpack_process_mapping(char *map,
		       uint32_t node_cnt,
		       uint32_t task_cnt,
		       uint16_t *tasks,
		       uint32_t **tids)
{
	/*
	 * Start from the flat array. For i'th task is located
	 * on the task_map[i]'th node
	 */
	uint32_t *task_map = NULL;
	uint16_t *node_task_cnt = NULL;
	uint32_t i;
	int rc = 0;

	task_map = unpack_process_mapping_flat(map, node_cnt, task_cnt, tasks);
	if (task_map == NULL) {
		rc = SLURM_ERROR;
		goto err_exit;
	}

	node_task_cnt = xcalloc(node_cnt, sizeof(uint16_t));
	for (i = 0;  i < node_cnt; i++){
		tids[i] = xcalloc(tasks[i], sizeof(uint32_t));
		node_task_cnt[i] = 0;
	}

	for (i = 0; i < task_cnt; i++) {
		uint32_t node = task_map[i];
		tids[node][ node_task_cnt[node]++ ] = i;
		xassert( node_task_cnt[node] <= tasks[node] );
	}

	goto exit;
err_exit:
	error("unpack_process_mapping: bad mapping format");
exit:
	if (task_map != NULL){
		xfree(task_map);
	}

	if (node_task_cnt != NULL){
		xfree(node_task_cnt);
	}
	return rc;
}
