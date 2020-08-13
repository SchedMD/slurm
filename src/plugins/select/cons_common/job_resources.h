/*****************************************************************************\
 *  job_resources.h - Functions for structures dealing with resources unique to
 *                    the select plugin.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Derived in large part from select/cons_[res|tres] plugins
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

#ifndef _CONS_COMMON_JOB_RES_H
#define _CONS_COMMON_JOB_RES_H

#include "cons_common.h"

#include "src/common/bitstring.h"
#include "src/common/job_resources.h"

extern bool select_state_initializing;

/*
 * Add job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT sys_resrcs_ptr - bitmap array (one per node) of available cores,
 *			   allocated as needed
 * NOTE: Patterned after add_job_to_cores() in src/common/job_resources.c
 */
extern void job_res_add_cores(job_resources_t *job_resrcs_ptr,
			      bitstr_t ***sys_resrcs_ptr);

/*
 * Remove job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT full_bitmap - bitmap of available CPUs, allocate as needed
 * NOTE: Patterned after remove_job_from_cores() in src/common/job_resources.c
 */
extern void job_res_rm_cores(job_resources_t *job_resrcs_ptr,
			     bitstr_t ***sys_resrcs_ptr);

/*
 * Test if job can fit into the given set of core_bitmaps
 * IN job_resrcs_ptr - resources allocated to a job
 * IN r_ptr - row we are trying to fit
 * RET 1 on success, 0 otherwise
 * NOTE: Patterned after job_fits_into_cores() in src/common/job_resources.c
 */
extern int job_res_fit_in_row(job_resources_t *job_resrcs_ptr,
			      part_row_data_t *r_ptr);


/*
 * allocate resources to the given job
 * - add 'struct job_resources' resources to 'part_res_record_t'
 * - add job's memory requirements to 'node_res_record_t'
 *
 * if action = 0 then add cores, memory + GRES (starting new job)
 * if action = 1 then add memory + GRES (adding suspended job at restart)
 * if action = 2 then only add cores (suspended job is resumed)
 *
 * See also: job_res_rm_job()
 */
extern int job_res_add_job(job_record_t *job_ptr, int action);

/*
 * Deallocate resources previously allocated to the given job
 * - subtract 'struct job_resources' resources from 'part_res_record_t'
 * - subtract job's memory requirements from 'node_res_record_t'
 *
 * if action = 0 then subtract cores, memory + GRES (running job was terminated)
 * if action = 1 then subtract memory + GRES (suspended job was terminated)
 * if action = 2 then only subtract cores (job is suspended)
 * IN: job_fini - job fully terminating on this node (not just a test)
 *
 * RET SLURM_SUCCESS or error code
 *
 * See also: job_res_add_job()
 */
extern int job_res_rm_job(part_res_record_t *part_record_ptr,
			  node_use_record_t *node_usage,
			  job_record_t *job_ptr, int action, bool job_fini,
			  bitstr_t *node_map);

#endif /* _CONS_COMMON_JOB_RES_H */
