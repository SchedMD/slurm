/*****************************************************************************\
 *  pe_info.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Copyright 2014 Cray Inc. All Rights Reserved.
 *  Written by David Gloe <c16817@cray.com>
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

#include "switch_cray_aries.h"

#if defined (HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)

#include <stdio.h>
#include <stdlib.h>

typedef struct {
	int nnodes;
	char *nodelist;
	int32_t *nodes;
	int ntasks;
	stepd_step_rec_t *stepd_step_rec;
	uint16_t *tasks_to_launch;
	uint32_t **tids;
} local_step_rec_t;

// Static functions
static int _setup_local_step_rec(local_step_rec_t *step_rec,
				 stepd_step_rec_t *job);
static void _free_local_step_rec(local_step_rec_t *step_rec);
static int _get_first_pe(stepd_step_rec_t *job);
static int _get_cmd_index(stepd_step_rec_t *job);
static int *_get_cmd_map(local_step_rec_t *step_rec);
static int *_get_node_cpu_map(local_step_rec_t *step_rec);
static int *_get_pe_nid_map(local_step_rec_t *step_rec);
static void _print_alpsc_pe_info(alpsc_peInfo_t *alps_info, int cmd_index);

/*
 * Fill in an alpsc_peInfo_t structure
 */
int build_alpsc_pe_info(stepd_step_rec_t *job,
			alpsc_peInfo_t *alpsc_pe_info, int *cmd_index)
{
	local_step_rec_t step_rec;

	// Sanity check everything here so we don't need to
	// do it everywhere else
	if (job == NULL) {
		CRAY_ERR("NULL job pointer");
		return SLURM_ERROR;
	} else if (job->ntasks < 1) {
		CRAY_ERR("Not enough tasks %d", job->ntasks);
		return SLURM_ERROR;
	} else if (alpsc_pe_info == NULL) {
		CRAY_ERR("NULL alpsc_pe_info");
		return SLURM_ERROR;
	} else if (cmd_index == NULL) {
		CRAY_ERR("NULL cmd_index");
		return SLURM_ERROR;
	} else if (job->flags & LAUNCH_MULTI_PROG) {
		if (job->mpmd_set == NULL) {
			CRAY_ERR("MPMD launch but no mpmd_set");
			return SLURM_ERROR;
		} else if (job->mpmd_set->first_pe == NULL) {
			CRAY_ERR("NULL first_pe");
			return SLURM_ERROR;
		} else if (job->mpmd_set->start_pe == NULL) {
			CRAY_ERR("NULL start_pe");
			return SLURM_ERROR;
		} else if (job->mpmd_set->total_pe == NULL) {
			CRAY_ERR("NULL total_pe");
			return SLURM_ERROR;
		} else if (job->mpmd_set->placement == NULL) {
			CRAY_ERR("NULL placement");
			return SLURM_ERROR;
		} else if (job->mpmd_set->num_cmds < 1) {
			CRAY_ERR("Not enough commands %d",
				 job->mpmd_set->num_cmds);
			return SLURM_ERROR;
		}
	}

	if (_setup_local_step_rec(&step_rec, job) != SLURM_SUCCESS)
		return SLURM_ERROR;

	// Fill in the structure
	alpsc_pe_info->totalPEs = step_rec.ntasks;
	alpsc_pe_info->firstPeHere = _get_first_pe(job);
	alpsc_pe_info->pesHere = job->node_tasks;
	alpsc_pe_info->peDepth = job->cpus_per_task;
	alpsc_pe_info->peNidArray = _get_pe_nid_map(&step_rec);
	alpsc_pe_info->peCmdMapArray = _get_cmd_map(&step_rec);
	alpsc_pe_info->nodeCpuArray = _get_node_cpu_map(&step_rec);

	// Get the command index
	*cmd_index = _get_cmd_index(job);

	/* Clean up */
	_free_local_step_rec(&step_rec);

	// Check results
	if (alpsc_pe_info->peNidArray == NULL ||
	    alpsc_pe_info->peCmdMapArray == NULL ||
	    alpsc_pe_info->nodeCpuArray == NULL || *cmd_index == -1) {
		free_alpsc_pe_info(alpsc_pe_info);
		return SLURM_ERROR;
	}

	// Print pe info if debug flag is set
	if (debug_flags & DEBUG_FLAG_SWITCH) {
		_print_alpsc_pe_info(alpsc_pe_info, *cmd_index);
	}

	return SLURM_SUCCESS;
}

static int _setup_local_step_rec(local_step_rec_t *step_rec,
				 stepd_step_rec_t *job)
{
	int cnt = 0, rc;
	xassert(step_rec);
	xassert(job);

	if ((job->pack_jobid != NO_VAL) && !job->pack_tids) {
		/* pack_tids == NULL if request from pre-v19.05 srun */
		CRAY_ERR("Old version of srun does not support heterogeneous jobs");
		return SLURM_ERROR;
	}

	memset(step_rec, 0, sizeof(local_step_rec_t));

	step_rec->stepd_step_rec = job;

	if (job->pack_jobid != NO_VAL) {
		step_rec->nnodes = job->pack_nnodes;
		step_rec->ntasks = job->pack_ntasks;
		step_rec->nodelist = job->pack_node_list;
		step_rec->tasks_to_launch = job->pack_task_cnts;
		step_rec->tids = job->pack_tids;
	} else {
		step_rec->nnodes = job->nnodes;
		step_rec->ntasks = job->ntasks;
		step_rec->nodelist = job->msg->complete_nodelist;
		step_rec->tasks_to_launch = job->msg->tasks_to_launch;
		step_rec->tids = job->msg->global_task_ids;
	}

	// Convert the node list to an array of nids
	rc = list_str_to_array(step_rec->nodelist, &cnt,
			       &step_rec->nodes);
	if (rc < 0) {
		return SLURM_ERROR;
	} else if (step_rec->nnodes != cnt) {
		CRAY_ERR("list_str_to_array cnt %d expected %u",
			 cnt, step_rec->nnodes);
		xfree(step_rec->nodes);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void _free_local_step_rec(local_step_rec_t *step_rec)
{
	xfree(step_rec->nodes);
}

/*
 * Get the first PE placed on this node, or -1 if not found
 */
static int _get_first_pe(stepd_step_rec_t *job)
{
	int i = 0;
	uint32_t first_pe;
	uint32_t taskid = 0;
	uint32_t offset = 0;

	if (job->pack_task_offset != NO_VAL)
		offset = job->pack_task_offset;

	first_pe = offset + job->task[0]->gtid;

	for (i = 1; i < job->node_tasks; i++) {
		taskid = offset + job->task[i]->gtid;
		if (taskid < first_pe)
			first_pe = taskid;
	}
	return first_pe;
}

/*
 * Get a peCmdMapArray, or NULL on error
 */
static int *_get_cmd_map(local_step_rec_t *step_rec)
{
	size_t size;
	int cmd_index, i, pe;
	int *cmd_map = NULL;

	size = step_rec->ntasks * sizeof(int);
	cmd_map = xmalloc(size);
	if (step_rec->stepd_step_rec->mpmd_set) {
		// Multiple programs, fill in from mpmd_set information
		for (i = 0; i < step_rec->ntasks; i++) {
			cmd_map[i] = -1;
		}

		// Loop over the MPMD commands
		for (cmd_index = 0;
		     cmd_index < step_rec->stepd_step_rec->mpmd_set->num_cmds;
		     cmd_index++) {

			// Fill in start_pe to start_pe+total_pe
			for (i = 0, pe = step_rec->stepd_step_rec->
				     mpmd_set->start_pe[cmd_index];
			     i < step_rec->stepd_step_rec->mpmd_set->
				     total_pe[cmd_index];
			     i++, pe++) {
				if (pe >= step_rec->ntasks) {
					CRAY_ERR("PE index %d too large", pe);
					xfree(cmd_map);
					return NULL;
				}
				cmd_map[pe] = cmd_index;
			}
		}

		// Verify the entire array was filled
		for (pe = 0; pe < step_rec->ntasks; pe++) {
			if (cmd_map[pe] == -1) {
				CRAY_ERR("No command on PE index %d", pe);
				xfree(cmd_map);
				return NULL;
			}
		}
	} else if (step_rec->stepd_step_rec->pack_jobid != NO_VAL) {
		if (!step_rec->stepd_step_rec->pack_tid_offsets) {
			CRAY_ERR("Missing pack_tid_offsets for HetJob");
			xfree(cmd_map);
			return NULL;
		}
		memset(cmd_map, 0, size);
		for (pe = 0; pe < step_rec->ntasks; pe++)
			cmd_map[pe] = step_rec->stepd_step_rec->pack_tid_offsets[pe];
	} else {
		// Only one program, index 0
		memset(cmd_map, 0, size);
	}

	return cmd_map;
}

/*
 * Get the pe to nid map, or NULL on error
 */
static int *_get_pe_nid_map(local_step_rec_t *step_rec)
{
	size_t size;
	int *pe_nid_map = NULL;
	int task, i, j;
	int tasks_to_launch_sum, nid;

	size = step_rec->ntasks * sizeof(int);
	pe_nid_map = xmalloc(size);

	// If we have it, just copy the mpmd set information
	/* FIXME: this is not configured for hetjob yet */
	if (step_rec->stepd_step_rec->mpmd_set &&
	    step_rec->stepd_step_rec->mpmd_set->placement) {
		// mpmd_set->placement is an int * too so this works
		memcpy(pe_nid_map,
		       step_rec->stepd_step_rec->mpmd_set->placement, size);
	} else {
		// Initialize to -1 so we can tell if we missed any
		for (i = 0; i < step_rec->ntasks; i++) {
			pe_nid_map[i] = -1;
		}

		// Search the task id map for the values we need
		tasks_to_launch_sum = 0;
		for (i = 0; i < step_rec->nnodes; i++) {
			tasks_to_launch_sum += step_rec->tasks_to_launch[i];
			for (j = 0; j < step_rec->tasks_to_launch[i]; j++) {
				task = step_rec->tids[i][j];
				pe_nid_map[task] = step_rec->nodes[i];
			}
		}

		// If this is LAM/MPI only one task per node is launched,
		// NOT job->ntasks. So fill in the rest of the tasks
		// assuming a block distribution
		if ((tasks_to_launch_sum == step_rec->nnodes) &&
		    (step_rec->nnodes < step_rec->ntasks)) {
			nid = step_rec->nodes[0]; // failsafe value
			for (i = 0; i < step_rec->ntasks; i++) {
				if (pe_nid_map[i] > -1) {
					nid = pe_nid_map[i];
				} else {
					pe_nid_map[i] = nid;
				}
			}
		}

		// Make sure we didn't miss any tasks
		for (i = 0; i < step_rec->ntasks; i++) {
			if (pe_nid_map[i] == -1) {
				CRAY_ERR("No NID for PE index %d", i);
				xfree(pe_nid_map);
				return NULL;
			}
		}
	}
	return pe_nid_map;
}

/*
 * Get number of cpus per node, or NULL on error
 */
static int *_get_node_cpu_map(local_step_rec_t *step_rec)
{
	int *node_cpu_map;
	int nodeid;

	node_cpu_map = xcalloc(step_rec->nnodes, sizeof(int));
	for (nodeid = 0; nodeid < step_rec->nnodes; nodeid++) {
		node_cpu_map[nodeid] =
			step_rec->tasks_to_launch[nodeid] *
			step_rec->stepd_step_rec->cpus_per_task;
	}

	return node_cpu_map;
}

/*
 * Get the command index. Note this is incompatible with MPMD so for now
 * we'll just return one of the command indices on this node.
 * Returns -1 if no command is found on this node.
 */
static int _get_cmd_index(stepd_step_rec_t *job)
{
	int cmd_index;

	if (job->mpmd_set && job->mpmd_set->first_pe) {
		// Use the first index found in the list
		for (cmd_index = 0; cmd_index < job->mpmd_set->num_cmds;
		     cmd_index++) {
			if (job->mpmd_set->first_pe[cmd_index] != -1) {
				return cmd_index;
			}
		}
		// If we've made it here we didn't find any on this node
		CRAY_ERR("No command found on this node");
		return -1;
	} else if (job->pack_jobid != NO_VAL) {
		return job->pack_offset;
	}

	// Not an MPMD job, the one command has index 0
	return 0;
}

/*
 * Print information about an alpsc_peInfo_t structure
 */
static void _print_alpsc_pe_info(alpsc_peInfo_t *alps_info, int cmd_index)
{
	int i, nid_index = 0;
	info("peInfo totalPEs: %d firstPeHere: %d pesHere: %d peDepth: %d"
	     " cmdIndex: %d",
	     alps_info->totalPEs, alps_info->firstPeHere, alps_info->pesHere,
	     alps_info->peDepth, cmd_index);
	for (i = 0; i < alps_info->totalPEs; i++) {
		info("Task: %d Node: %d MPMD index: %d",
		     i, alps_info->peNidArray[i], alps_info->peCmdMapArray[i]);
		if (i == alps_info->totalPEs - 1 ||
		    alps_info->peNidArray[i] != alps_info->peNidArray[i + 1]) {
			info("Node: %d CPUs: %d",
			     alps_info->peNidArray[i],
			     alps_info->nodeCpuArray[nid_index]);
			nid_index++;
		}
	}
}

/*
 * Function: free_alpsc_pe_info
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
	xfree(alpsc_pe_info->peNidArray);
	xfree(alpsc_pe_info->peCmdMapArray);
	xfree(alpsc_pe_info->nodeCpuArray);
}

#endif
