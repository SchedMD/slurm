/*****************************************************************************\
 *  job_test.h - Determine if job can be allocated resources.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#ifndef _CONS_TRES_JOB_TEST_H
#define _CONS_TRES_JOB_TEST_H

#include "select_cons_tres.h"

/*
 * Add job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT sys_resrcs_ptr - bitmap array (one per node) of available cores,
 *			   allocated as needed
 * RET 1 on success, 0 otherwise
 * NOTE: Patterned after add_job_to_cores() in src/common/job_resources.c
 */
extern void add_job_res(job_resources_t *job_resrcs_ptr,
			bitstr_t ***sys_resrcs_ptr);

/*
 * Add job resource use to the partition data structure
 */
extern void add_job_to_row(struct job_resources *job,
			   struct part_row_data *r_ptr);

/*
 * Build an empty array of bitmaps, one per node
 * Use free_core_array() to release returned memory
 */
extern bitstr_t **build_core_array(void);

/* test for conflicting core bitmap elements */
extern int can_job_fit_in_row(struct job_resources *job,
			      struct part_row_data *r_ptr);

/* Clear all elements of an array of bitmaps, one per node */
extern void clear_core_array(bitstr_t **core_array);

/*
 * Copy an array of bitmaps, one per node
 * Use free_core_array() to release returned memory
 */
extern bitstr_t **copy_core_array(bitstr_t **core_array);

/*
 * Return the number of usable logical processors by a given job on
 * some specified node. Returns 0xffff if no limit.
 */
extern int vpus_per_core(struct job_details *details, int node_inx);

/*
 * Return count of set bits in array of bitmaps, one per node
 */
extern int count_core_array_set(bitstr_t **core_array);

/*
 * Set row_bitmap1 to core_array1 & core_array2
 */
extern void core_array_and(bitstr_t **core_array1, bitstr_t **core_array2);

/*
 * Set row_bitmap1 to core_array1 & !core_array2
 * In other words, any bit set in row_bitmap2 is cleared from row_bitmap1
 */
extern void core_array_and_not(bitstr_t **core_array1, bitstr_t **core_array2);

/*
 * Set row_bitmap1 to core_array1 | core_array2
 */
extern void core_array_or(bitstr_t **core_array1, bitstr_t **core_array2);

/* Free an array of bitmaps, one per node */
extern void free_core_array(bitstr_t ***core_array);

/*
 * Test if job can fit into the given set of core_bitmaps
 * IN job_resrcs_ptr - resources allocated to a job
 * IN sys_resrcs_ptr - bitmap array (one per node) of available cores
 * RET 1 on success, 0 otherwise
 * NOTE: Patterned after job_fits_into_cores() in src/common/job_resources.c
 */
extern int job_fit_test(job_resources_t *job_resrcs_ptr,
			bitstr_t **sys_resrcs_ptr);

extern void log_tres_state(struct node_use_record *node_usage,
			   struct part_res_record *part_record_ptr);

/*
 * deallocate resources previously allocated to the given job
 * - subtract 'struct job_resources' resources from 'struct part_res_record'
 * - subtract job's memory requirements from 'struct node_res_record'
 *
 * if action = 0 then subtract cores, memory + GRES (running job was terminated)
 * if action = 1 then subtract memory + GRES (suspended job was terminated)
 * if action = 2 then only subtract cores (job is suspended)
 */
extern int rm_job_res(struct part_res_record *part_record_ptr,
		      struct node_use_record *node_usage,
		      struct job_record *job_ptr, int action);

/* Allocate resources for a job now, if possible */
extern int run_now(struct job_record *job_ptr, bitstr_t *node_bitmap,
		   uint32_t min_nodes, uint32_t max_nodes,
		   uint32_t req_nodes, uint16_t job_node_req,
		   List preemptee_candidates, List *preemptee_job_list,
		   bitstr_t **exc_cores);

/* Determine if a job can ever run */
extern int test_only(struct job_record *job_ptr, bitstr_t *node_bitmap,
		     uint32_t min_nodes, uint32_t max_nodes,
		     uint32_t req_nodes, uint16_t job_node_req);

/*
 * Determine where and when the job at job_ptr can begin execution by updating
 * a scratch cr_record structure to reflect each job terminating at the
 * end of its time limit and use this to show where and when the job at job_ptr
 * will begin execution. Used by Slurm's sched/backfill plugin.
 */
extern int will_run_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
			 uint32_t min_nodes, uint32_t max_nodes,
			 uint32_t req_nodes, uint16_t job_node_req,
			 List preemptee_candidates, List *preemptee_job_list,
			 bitstr_t **exc_cores);

#endif /* !_CONS_TRES_JOB_TEST_H */
