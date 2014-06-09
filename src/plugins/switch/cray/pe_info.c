/*****************************************************************************\
 *  pe_info.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Copyright 2014 Cray Inc. All Rights Reserved.
 *  Written by David Gloe <c16817@cray.com>
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

#include "switch_cray.h"

#if defined (HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)

#include <stdio.h>
#include <stdlib.h>

static void _print_alpsc_pe_info(alpsc_peInfo_t *alps_info);
static int _get_first_pe(uint32_t nodeid, uint32_t task_count,
			 uint32_t **host_to_task_map, int32_t *first_pe);

/*
 * Fill in an alpsc_peInfo_t structure
 */
int build_alpsc_pe_info(stepd_step_rec_t *job,
			slurm_cray_jobinfo_t *sw_job,
			alpsc_peInfo_t *alpsc_pe_info)
{
	int rc, i, j, cnt = 0;
	int32_t *task_to_nodes_map = NULL;
	int32_t *nodes = NULL;
	int32_t first_pe_here;
	uint32_t task;
	size_t size;

	alpsc_pe_info->totalPEs = job->ntasks;
	alpsc_pe_info->pesHere = job->node_tasks;
	alpsc_pe_info->peDepth = job->cpus_per_task;

	/*
	 * Fill in alpsc_pe_info->firstPeHere
	 */
	rc = _get_first_pe(job->nodeid, job->node_tasks,
			   job->msg->global_task_ids,
			   &first_pe_here);
	if (rc < 0) {
		CRAY_ERR("get_first_pe failed");
		return SLURM_ERROR;
	}
	alpsc_pe_info->firstPeHere = first_pe_here;

	/*
	 * Fill in alpsc_pe_info->peNidArray
	 *
	 * The peNidArray maps tasks to nodes.
	 * Basically, reverse the tids variable which maps nodes to tasks.
	 */
	rc = list_str_to_array(job->msg->complete_nodelist, &cnt, &nodes);
	if (rc < 0) {
		CRAY_ERR("list_str_to_array failed");
		return SLURM_ERROR;
	}
	if (cnt == 0) {
		CRAY_ERR("list_str_to_array returned a node count of zero");
		return SLURM_ERROR;
	}
	if (job->msg->nnodes != cnt) {
		CRAY_ERR("list_str_to_array returned count %"
			 PRIu32 "does not match expected count %d",
			 cnt, job->msg->nnodes);
	}

	task_to_nodes_map = xmalloc(job->msg->ntasks * sizeof(int32_t));

	for (i = 0; i < job->msg->nnodes; i++) {
		for (j = 0; j < job->msg->tasks_to_launch[i]; j++) {
			task = job->msg->global_task_ids[i][j];
			task_to_nodes_map[task] = nodes[i];
		}
	}
	alpsc_pe_info->peNidArray = task_to_nodes_map;
	xfree(nodes);

	/*
	 * Fill in alpsc_pe_info->peCmdMapArray
	 *
	 * If the job is an SPMD job, then the command index (cmd_index) is 0.
	 * Otherwise, if the job is an MPMD job, then the command index
	 * (cmd_index) is equal to the number of executables in the job minus 1.
	 *
	 * TODO: Add MPMD support once SchedMD provides the needed MPMD data.
	 */

	if (!job->multi_prog) {
		/* SPMD Launch */
		size = alpsc_pe_info->totalPEs * sizeof(int);
		alpsc_pe_info->peCmdMapArray = xmalloc(size);
		memset(alpsc_pe_info->peCmdMapArray, 0, size);
	} else {
		/* MPMD Launch */
		CRAY_ERR("MPMD Applications are not currently supported.");
		goto error_free_alpsc_pe_info_t;
	}

	/*
	 * Fill in alpsc_pe_info->nodeCpuArray
	 * I don't know how to get this information from SLURM.
	 * Cray's PMI does not need the information.
	 * It may be used by debuggers like ATP or lgdb.  If so, then it will
	 * have to be filled in when support for them is added.
	 * Currently, it's all zeros.
	 */
	size = job->msg->nnodes * sizeof(int);
	alpsc_pe_info->nodeCpuArray = xmalloc(size);
	memset(alpsc_pe_info->nodeCpuArray, 0, size);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		_print_alpsc_pe_info(alpsc_pe_info);
	}

	return SLURM_SUCCESS;

error_free_alpsc_pe_info_t:
	free_alpsc_pe_info(alpsc_pe_info);
	return SLURM_ERROR;
}

/*
 * Print information about an alpsc_peInfo_t structure
 */
static void _print_alpsc_pe_info(alpsc_peInfo_t *alps_info)
{
	int i;
	info("alpsc_peInfo totalPEs: %d firstPeHere: %d pesHere: %d peDepth: %d",
	     alps_info->totalPEs, alps_info->firstPeHere, alps_info->pesHere,
	     alps_info->peDepth);
	for (i = 0; i < alps_info->totalPEs; i++) {
		debug3("Task: %d\tNode: %d", i, alps_info->peNidArray[i]);
	}
}

/*
 * Function: get_first_pe
 * Description:
 * Returns the first (i.e. lowest) PE on the node.
 *
 * IN:
 * nodeid -- Index of the node in the host_to_task_map
 * task_count -- Number of tasks on the node
 * host_to_task_map -- 2D array mapping the host to its tasks
 *
 * OUT:
 * first_pe -- The first (i.e. lowest) PE on the node
 *
 * RETURN
 * 0 on success and -1 on error
 */
static int _get_first_pe(uint32_t nodeid, uint32_t task_count,
			 uint32_t **host_to_task_map, int32_t *first_pe)
{

	int i, ret = 0;

	if (task_count == 0) {
		CRAY_ERR("task_count == 0");
		return -1;
	}
	if (!host_to_task_map) {
		CRAY_ERR("host_to_task_map == NULL");
		return -1;
	}
	*first_pe = host_to_task_map[nodeid][0];
	for (i = 0; i < task_count; i++) {
		if (host_to_task_map[nodeid][i] < *first_pe) {
			*first_pe = host_to_task_map[nodeid][i];
		}
	}
	return ret;
}

/*
 * Function: _free_alpsc_pe_info
 * Description:
 * 	Frees any allocated members of alpsc_pe_info.
 * Parameters:
 * IN	alpsc_pe_info:  alpsc_peInfo_t structure needing to be freed
 *
 * Returns
 * 	Void.
 */
void free_alpsc_pe_info(alpsc_peInfo_t *alpsc_pe_info)
{
	if (alpsc_pe_info->peNidArray) {
		xfree(alpsc_pe_info->peNidArray);
	}
	if (alpsc_pe_info->peCmdMapArray) {
		xfree(alpsc_pe_info->peCmdMapArray);
	}
	if (alpsc_pe_info->nodeCpuArray) {
		xfree(alpsc_pe_info->nodeCpuArray);
	}
	return;
}

#endif
