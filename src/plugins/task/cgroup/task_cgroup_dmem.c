/*****************************************************************************\
 *  task_cgroup_dmem.c - dmem (device memory) cgroup support for task/cgroup
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "config.h"

#define _GNU_SOURCE

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cgroup.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/namespace.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "task_cgroup_dmem.h"

typedef struct {
	uint64_t *counts; /* shard counts indexed by gres bitmap index */
	int bit_cnt; /* number of entries in counts */
	cgroup_level_t level;
	int rc;
} handle_dmem_args_t;

static bool is_first_task = true;

/*
 * Drain this node. A dmem region that existed at slurmd startup rejected a
 * limit write, which means the device or its driver went away or changed
 * under a running slurmd (e.g. a driver reload renaming the region). Jobs
 * must stop landing on this node until an administrator fixes the device
 * state and restarts the slurmd to re-discover the regions.
 * IN reason - drain reason shown by sinfo
 */
static void _drain_node(char *reason)
{
	update_node_msg_t update_node_msg;

	slurm_init_update_node_msg(&update_node_msg);
	update_node_msg.node_names = conf->node_name;
	update_node_msg.node_state = NODE_STATE_DRAIN;
	update_node_msg.reason = reason;

	if (slurm_update_node(&update_node_msg) != SLURM_SUCCESS)
		error("Unable to drain node %s: %m", conf->node_name);
}

static int _handle_dmem_limit(void *x, void *arg)
{
	gres_device_t *dev = x;
	gres_dmem_dev_t *dmem = dev->dmem;
	handle_dmem_args_t *handle_args = arg;
	cgroup_limits_t limits;

	if (!dmem || (dmem->state != GRES_DMEM_USABLE))
		return 0;
	if ((dev->index < 0) || (dev->index >= handle_args->bit_cnt))
		return 0;
	if (!handle_args->counts[dev->index] || !dmem->slice)
		return 0;

	cgroup_init_limits(&limits);

	limits.dmem_region = dmem->region;
	limits.limit_in_bytes = (handle_args->counts[dev->index] * dmem->slice);

	log_flag(CGROUP, "%s: setting dmem limit of %"PRIu64" bytes (%"PRIu64" shards) on region %s (%s)",
		 (handle_args->level == CG_LEVEL_JOB) ? "job" : "step",
		 limits.limit_in_bytes, handle_args->counts[dev->index],
		 dmem->region, dev->path);

	if (cgroup_g_constrain_set(CG_DMEM, handle_args->level, &limits) !=
	    SLURM_SUCCESS) {
		char *reason = NULL;

		error("Unable to set device memory limit of %"PRIu64" bytes on dmem region %s (%s)",
		      limits.limit_in_bytes, dmem->region, dev->path);

		reason = xstrdup_printf(
			"Cannot set device memory limit on dmem region %s (%s)",
			dmem->region, dev->path);
		_drain_node(reason);
		xfree(reason);

		handle_args->rc = SLURM_ERROR;
		return -1;
	}

	return 0;
}

/*
 * Write the device memory limits of one cgroup level.
 * IN dmem_devs - sharing devices with their dmem state (from gres/shard)
 * IN gres_list - job or step gres list providing the allocated shard counts
 * IN is_job - true if gres_list is the job gres list
 * IN level - cgroup level to constrain
 * RET SLURM_SUCCESS or SLURM_ERROR if any limit could not be written
 */
static int _constrain_level(list_t *dmem_devs, list_t *gres_list, bool is_job,
			    cgroup_level_t level)
{
	handle_dmem_args_t handle_args = {
		.level = level,
		.rc = SLURM_SUCCESS,
	};

	handle_args.counts = gres_get_per_bit_alloc(gres_list, is_job,
						    gres_build_id("shard"),
						    &handle_args.bit_cnt);
	if (!handle_args.counts)
		return SLURM_SUCCESS;

	(void) list_for_each(dmem_devs, _handle_dmem_limit, &handle_args);
	xfree(handle_args.counts);

	return handle_args.rc;
}

extern int task_cgroup_dmem_init(void)
{
	if (cgroup_g_initialize(CG_DMEM) != SLURM_SUCCESS) {
		error("unable to initialize the dmem controller");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int task_cgroup_dmem_fini(void)
{
	return cgroup_g_step_destroy(CG_DMEM);
}

extern int task_cgroup_dmem_create(stepd_step_rec_t *step)
{
	list_t *dmem_devs = gres_g_get_dmem_devices();
	int rc = SLURM_SUCCESS;

	if (is_first_task) {
		if (cgroup_g_step_create(CG_DMEM, step) != SLURM_SUCCESS)
			return SLURM_ERROR;
		is_first_task = false;
	}

	if (!dmem_devs)
		return SLURM_SUCCESS;

	/*
	 * Without eBPF the limits would be evadable by simply opening a device
	 * that is not allocated to the job.
	 */
	if (!namespace_g_can_bpf(step) && cgroup_g_bpf_get_token() <= 0) {
		log_flag(CGROUP, "Skipping job and step device memory constrain as we are in a user namespace without a BPF token available");
		return SLURM_SUCCESS;
	}

	if ((rc = _constrain_level(dmem_devs, step->job_gres_list, true,
				   CG_LEVEL_JOB)) != SLURM_SUCCESS)
		return rc;

	if ((step->step_id.step_id != SLURM_BATCH_SCRIPT) &&
	    (step->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (step->step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    (!(step->flags & LAUNCH_EXT_LAUNCHER))) {
		rc = _constrain_level(dmem_devs, step->step_gres_list, false,
				      CG_LEVEL_STEP);
	}

	return rc;
}
