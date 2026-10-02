/*****************************************************************************\
 *  common_as.c - common functions for accounting storage
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"

#include "src/common/slurmdbd_defs.h"
#include "src/common/xstring.h"

extern int as_build_step_start_msg(dbd_step_start_msg_t *req,
				   step_record_t *step_ptr)
{
	uint32_t tasks = 0, nodes = 0, task_dist = 0;
	char *node_list = NULL;

	xassert(req);
	xassert(step_ptr);

	if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		tasks = step_ptr->job_ptr->total_cpus;
		nodes = step_ptr->job_ptr->total_nodes;
		node_list = step_ptr->job_ptr->nodes;
	} else {
		tasks = step_ptr->step_layout->task_cnt;
		nodes = step_ptr->step_layout->node_cnt;
		task_dist = step_ptr->step_layout->task_dist;
		node_list = step_ptr->step_layout->node_list;
	}

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("jobacct_storage_p_step_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}
	memset(req, 0, sizeof(dbd_step_start_msg_t));

	req->assoc_id    = step_ptr->job_ptr->assoc_id;
	req->container   = step_ptr->container;
	/*
	 * In Slurm <= 25.05 step_id.sluid=0, so use db_index.
	 * Once 25.05 is no longer supported only use
	 * step_ptr->step_id.sluid.
	 */
	req->db_index = step_ptr->step_id.sluid ?
		step_ptr->step_id.sluid : step_ptr->job_ptr->db_index;
	req->name        = step_ptr->name;
	req->nodes       = node_list;
	/* create req->node_inx outside of locks when packing */
	req->node_cnt    = nodes;
	if (step_ptr->start_time > step_ptr->job_ptr->resize_time)
		req->start_time = step_ptr->start_time;
	else
		req->start_time = step_ptr->job_ptr->resize_time;

	if (step_ptr->job_ptr->resize_time)
		req->job_submit_time   = step_ptr->job_ptr->resize_time;
	else if (step_ptr->job_ptr->details)
		req->job_submit_time   =
			step_ptr->job_ptr->details->submit_time;

	req->time_limit = step_ptr->time_limit;

	memcpy(&req->step_id, &step_ptr->step_id, sizeof(req->step_id));

	if (step_ptr->step_layout)
		req->task_dist   = step_ptr->step_layout->task_dist;
	req->task_dist   = task_dist;

	req->total_tasks = tasks;

	if (!(slurm_conf.conf_flags & CONF_FLAG_NO_STDIO)) {
		req->cwd = step_ptr->cwd;
		req->std_err = step_ptr->std_err;
		req->std_in = step_ptr->std_in;
		req->std_out = step_ptr->std_out;
	}

	req->state = step_ptr->state;
	req->submit_line = step_ptr->submit_line;
	req->tres_alloc_str = step_ptr->tres_alloc_str;

	req->req_cpufreq_min = step_ptr->cpu_freq_min;
	req->req_cpufreq_max = step_ptr->cpu_freq_max;
	req->req_cpufreq_gov = step_ptr->cpu_freq_gov;

	return SLURM_SUCCESS;
}

extern int as_build_step_comp_msg(dbd_step_comp_msg_t *req,
				  step_record_t *step_ptr)
{
	uint32_t tasks = 0;

	xassert(req);
	xassert(step_ptr);

	if (step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT)
		tasks = 1;
	else {
		if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
			tasks = step_ptr->job_ptr->total_cpus;
		else
			tasks = step_ptr->step_layout->task_cnt;
	}

	if (!step_ptr->job_ptr->db_index
	    && ((!step_ptr->job_ptr->details
		 || !step_ptr->job_ptr->details->submit_time)
		&& !step_ptr->job_ptr->resize_time)) {
		error("jobacct_storage_p_step_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	memset(req, 0, sizeof(dbd_step_comp_msg_t));

	req->assoc_id    = step_ptr->job_ptr->assoc_id;
	/*
	 * In Slurm <= 25.05 step_id.sluid=0, so use db_index.
	 * Once 25.05 is no longer supported only use
	 * step_ptr->step_id.sluid.
	 */
	req->db_index = step_ptr->step_id.sluid ?
		step_ptr->step_id.sluid : step_ptr->job_ptr->db_index;
	req->end_time    = time(NULL);	/* called at step completion */
	req->exit_code   = step_ptr->exit_code;
	req->jobacct     = step_ptr->jobacct;
	req->req_uid     = step_ptr->requid;
	if (step_ptr->start_time > step_ptr->job_ptr->resize_time)
		req->start_time = step_ptr->start_time;
	else
		req->start_time = step_ptr->job_ptr->resize_time;

	if (step_ptr->job_ptr->resize_time)
		req->job_submit_time   = step_ptr->job_ptr->resize_time;
	else if (step_ptr->job_ptr->details)
		req->job_submit_time   =
			step_ptr->job_ptr->details->submit_time;

	if (step_ptr->job_ptr->bit_flags & TRES_STR_CALC)
		req->job_tres_alloc_str = step_ptr->job_ptr->tres_alloc_str;

	req->state       = step_ptr->state;

	memcpy(&req->step_id, &step_ptr->step_id, sizeof(req->step_id));

	req->total_tasks = tasks;

	return SLURM_SUCCESS;
}
