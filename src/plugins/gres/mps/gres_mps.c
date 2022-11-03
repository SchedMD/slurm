/*****************************************************************************\
 *  gres_mps.c - Support MPS as a generic resources.
 *  MPS or CUDA Multi-Process Services is a mechanism to share GPUs.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Written by Morris Jette
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

#include <ctype.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/bitstring.h"
#include "src/common/env.h"
#include "src/interfaces/gres.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "../common/gres_common.h"
#include "../common/gres_c_s.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Gres MPS plugin";
const char	plugin_type[]		= "gres/mps";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;

static List	gres_devices		= NULL;

extern int init(void)
{
	debug("loaded");

	return SLURM_SUCCESS;
}
extern int fini(void)
{
	debug("unloading");
	FREE_NULL_LIST(gres_devices);
	gres_c_s_fini();

	return SLURM_SUCCESS;
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int gres_p_node_config_load(List gres_conf_list,
				   node_config_load_t *config)
{
	return gres_c_s_init_share_devices(
		gres_conf_list, &gres_devices, config, "gpu");
}

/* Given a global device ID, return its gres/mps count */
static uint64_t _get_dev_count(int global_id)
{
	ListIterator itr;
	shared_dev_info_t *mps_ptr;
	uint64_t count = NO_VAL64;

	if (!shared_info) {
		error("shared_info is NULL");
		return 100;
	}
	itr = list_iterator_create(shared_info);
	while ((mps_ptr = list_next(itr))) {
		if (mps_ptr->id == global_id) {
			count = mps_ptr->count;
			break;
		}
	}
	list_iterator_destroy(itr);
	if (count == NO_VAL64) {
		error("Could not find gres/mps count for device ID %d",
		      global_id);
		return 100;
	}

	return count;
}

static void _set_env(common_gres_env_t *gres_env)
{
	char perc_str[64];
	uint64_t count_on_dev, percentage;

	gres_env->global_id = -1;
	gres_env->gres_conf_flags = GRES_CONF_ENV_NVML;
	gres_env->gres_devices = gres_devices;
	gres_env->prefix = "";

	gres_common_gpu_set_env(gres_env);

	/*
	 * Set environment variables if GRES is found. Otherwise, unset
	 * environment variables, since this means GRES is not allocated.
	 * This is useful for jobs and steps that request --gres=none within an
	 * existing job allocation with GRES.
	 */
	if (gres_env->gres_cnt && shared_info) {
		count_on_dev = _get_dev_count(gres_env->global_id);
		if (count_on_dev > 0) {
			percentage = (gres_env->gres_cnt * 100) / count_on_dev;
			percentage = MAX(percentage, 1);
		} else
			percentage = 0;
		snprintf(perc_str, sizeof(perc_str), "%"PRIu64, percentage);
		env_array_overwrite(gres_env->env_ptr,
				    "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE",
				    perc_str);
	} else if (gres_env->gres_cnt) {
		error("shared_info list is NULL");
		snprintf(perc_str, sizeof(perc_str), "%"PRIu64,
			 gres_env->gres_cnt);
		env_array_overwrite(gres_env->env_ptr,
				    "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE",
				    perc_str);
	} else {
		unsetenvp(*gres_env->env_ptr,
			  "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE");
	}
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job's GRES state.
 */
extern void gres_p_job_set_env(char ***job_env_ptr,
			       bitstr_t *gres_bit_alloc,
			       uint64_t gres_per_node,
			       gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = job_env_ptr,
		.flags = flags,
		.gres_cnt = gres_per_node,
		.is_job = true,
	};

	_set_env(&gres_env);
}

/*
 * Set environment variables as appropriate for a step (i.e. all tasks) based
 * upon the job step's GRES state.
 */
extern void gres_p_step_set_env(char ***step_env_ptr,
				bitstr_t *gres_bit_alloc,
				uint64_t gres_per_node,
				gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = step_env_ptr,
		.flags = flags,
		.gres_cnt = gres_per_node,
	};

	_set_env(&gres_env);
}

/*
 * Reset environment variables as appropriate for a job (i.e. this one task)
 * based upon the job step's GRES state and assigned CPUs.
 */
extern void gres_p_task_set_env(char ***task_env_ptr,
				bitstr_t *gres_bit_alloc,
				bitstr_t *usable_gres,
				uint64_t gres_per_node,
				gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = task_env_ptr,
		.flags = flags,
		.gres_cnt = gres_per_node,
		.is_task = true,
		.usable_gres = usable_gres,
	};

	_set_env(&gres_env);
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_p_send_stepd(buf_t *buffer)
{
	gres_send_stepd(buffer, gres_devices);

	gres_c_s_send_stepd(buffer);

	return;
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_p_recv_stepd(buf_t *buffer)
{
	gres_recv_stepd(buffer, &gres_devices);

	gres_c_s_recv_stepd(buffer);

	return;
}

/*
 * get data from a job's GRES data structure
 * IN job_gres_data  - job's GRES data structure
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired
 * IN data_type - type of data to get from the job's data
 * OUT data - pointer to the data from job's GRES data structure
 *            DO NOT FREE: This is a pointer into the job's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int gres_p_get_job_info(gres_job_state_t *gres_js,
			       uint32_t node_inx,
			       enum gres_job_data_type data_type, void *data)
{
	return EINVAL;
}

/*
 * get data from a step's GRES data structure
 * IN gres_ss  - step's GRES data structure
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired. Note this can differ from the step's
 *	node allocation index.
 * IN data_type - type of data to get from the step's data
 * OUT data - pointer to the data from step's GRES data structure
 *            DO NOT FREE: This is a pointer into the step's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int gres_p_get_step_info(gres_step_state_t *gres_ss,
				uint32_t node_inx,
				enum gres_step_data_type data_type, void *data)
{
	return EINVAL;
}

/*
 * Return a list of devices of this type. The list elements are of type
 * "gres_device_t" and the list should be freed using FREE_NULL_LIST().
 */
extern List gres_p_get_devices(void)
{
	return gres_devices;
}

extern void gres_p_step_hardware_init(bitstr_t *usable_gres, char *settings)
{
	return;
}

extern void gres_p_step_hardware_fini(void)
{
	return;
}

/*
 * Build record used to set environment variables as appropriate for a job's
 * prolog or epilog based GRES allocated to the job.
 */
extern gres_prep_t *gres_p_prep_build_env(
	gres_job_state_t *gres_js)
{
	int i;
	gres_prep_t *gres_prep;

	gres_prep = xmalloc(sizeof(gres_prep_t));
	gres_prep->node_cnt = gres_js->node_cnt;
	gres_prep->gres_bit_alloc = xcalloc(gres_prep->node_cnt,
					  sizeof(bitstr_t *));
	gres_prep->gres_cnt_node_alloc = xcalloc(gres_prep->node_cnt,
					       sizeof(uint64_t));
	for (i = 0; i < gres_prep->node_cnt; i++) {
		if (gres_js->gres_bit_alloc &&
		    gres_js->gres_bit_alloc[i]) {
			gres_prep->gres_bit_alloc[i] =
				bit_copy(gres_js->gres_bit_alloc[i]);
		}
		if (gres_js->gres_bit_alloc &&
		    gres_js->gres_bit_alloc[i]) {
			gres_prep->gres_cnt_node_alloc[i] =
				gres_js->gres_cnt_node_alloc[i];
		}
	}

	return gres_prep;
}

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 */
extern void gres_p_prep_set_env(char ***prep_env_ptr,
				gres_prep_t *gres_prep, int node_inx)
{
	int dev_inx = -1, global_id = -1, i;
	uint64_t count_on_dev, gres_per_node = 0, percentage;
	gres_device_t *gres_device;
	ListIterator iter;

	if (gres_common_prep_set_env(prep_env_ptr,
				     gres_prep, node_inx,
				     GRES_CONF_ENV_NVML, gres_devices))
		return;

	if (gres_prep->gres_bit_alloc &&
	    gres_prep->gres_bit_alloc[node_inx])
		dev_inx = bit_ffs(gres_prep->gres_bit_alloc[node_inx]);
	if (dev_inx >= 0) {
		/* Translate bit to device number, may differ */
		i = -1;
		iter = list_iterator_create(gres_devices);
		while ((gres_device = list_next(iter))) {
			i++;
			if (i == dev_inx) {
				global_id = gres_device->dev_num;
				break;
			}
		}
		list_iterator_destroy(iter);
	}
	if ((global_id >= 0) &&
	    gres_prep->gres_cnt_node_alloc &&
	    gres_prep->gres_cnt_node_alloc[node_inx]) {
		gres_per_node = gres_prep->gres_cnt_node_alloc[node_inx];
		count_on_dev = _get_dev_count(global_id);
		if (count_on_dev > 0) {
			percentage = (gres_per_node * 100) / count_on_dev;
			percentage = MAX(percentage, 1);
		} else
			percentage = 0;

		xassert(*prep_env_ptr);
		env_array_overwrite_fmt(prep_env_ptr,
					"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE",
					"%"PRIu64, percentage);
	}

	return;
}
