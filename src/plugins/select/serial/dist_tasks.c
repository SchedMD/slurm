/*****************************************************************************\
 *  dist_tasks.c - Assign specific CPUs to the job
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC.
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

#include "select_serial.h"
#include "dist_tasks.h"

/* Compute the number of CPUs to use on each node */
static int _compute_c_b_task_dist(struct job_record *job_ptr)
{
	job_resources_t *job_res = job_ptr->job_resrcs;

	if (!job_res || !job_res->cpus) {
		error("select/serial: _compute_c_b_task_dist job_res==NULL");
		return SLURM_ERROR;
	}
	if (job_res->nhosts != 1) {
		error("select/serial: _compute_c_b_task_dist given nhosts==%u",
		      job_res->nhosts);
		return SLURM_ERROR;
	}

	xfree(job_res->cpus);
	job_res->cpus = xmalloc(sizeof(uint16_t));
	job_res->cpus[0] = 1;

	return SLURM_SUCCESS;
}

/* Select the specific cores in the job's allocation */
static void _block_sync_core_bitmap(struct job_record *job_ptr,
				    const uint16_t cr_type)
{
	job_resources_t *job_res = job_ptr->job_resrcs;
	int c_first, c_size;

	if (!job_res || !job_res->core_bitmap)
		return;

	c_size  = bit_size(job_res->core_bitmap);
	c_first = bit_ffs(job_res->core_bitmap);
	bit_nclear(job_res->core_bitmap, 0, c_size - 1);
	bit_set(job_res->core_bitmap, c_first);
}

extern int cr_dist(struct job_record *job_ptr, const uint16_t cr_type)
{
	int error_code;

	error_code = _compute_c_b_task_dist(job_ptr);
	if (error_code != SLURM_SUCCESS)
		return error_code;

	_block_sync_core_bitmap(job_ptr, cr_type);
	return SLURM_SUCCESS;
}
