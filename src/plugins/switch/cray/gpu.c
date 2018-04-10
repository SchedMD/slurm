/*****************************************************************************\
 *  gpu.c - Library for managing a switch on a Cray system.
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

#include "switch_cray.h"

#ifdef HAVE_NATIVE_CRAY

#define CRAY_CUDA_MPS_ENV   "CRAY_CUDA_MPS"
#define CRAY_CUDA_PROXY_ENV "CRAY_CUDA_PROXY"

/*
 * Search the job's environment to determine if the
 * user requested the MPS to be on or off.
 * Returns 0 for off, 1 for on, 2 for not requested,
 * 3 for error.
 */
static int _get_mps_request(stepd_step_rec_t *job)
{

        char *envval;

	// Determine what user wants the mps to be set at by the
	// CRAY_CUDA_MPS and CRAY_CUDA_PROXY variables. If not set,
	// do nothing.
	if (!(envval = getenvp(job->env, CRAY_CUDA_MPS_ENV)) &&
	    !(envval = getenvp(job->env, CRAY_CUDA_PROXY_ENV))) {
		debug2("No GPU action requested");
		return 2;
	}

	if (!xstrcasecmp(envval, "on") || !xstrcmp(envval, "1")) {
		debug2("GPU mps requested on");
		return 1;
	} else if (!xstrcasecmp(envval, "off") || !xstrcmp(envval, "0")) {
		debug2("GPU mps requested off");
		return 0;
	}

	CRAY_ERR("Couldn't parse %s value %s, expected on,off,0,1",
		 CRAY_CUDA_MPS_ENV, envval);
	return 3;
}

/*
 * Set up the GPU proxy service if requested to do so through the
 * CRAY_CUDA_MPS or CRAY_CUDA_PROXY environment variables.
 * Returns SLURM_SUCCESS or SLURM_ERROR.
 */
int setup_gpu(stepd_step_rec_t *job)
{
	int rc, gpu_enable;
	char *err_msg;

	gpu_enable = _get_mps_request(job);
	if (gpu_enable > 1) {
		// No action required, just exit with success
		return SLURM_SUCCESS;
	}

	// Establish GPU's default state
	// NOTE: We have to redo this for every job because the job_init call
	// is made from the stepd, so the default state in the slurmd is wiped
	debug2("Getting default GPU mps state");
	rc = alpsc_establish_GPU_mps_def_state(&err_msg);
	ALPSC_CN_DEBUG("alpsc_establish_GPU_mps_def_state");
	if (rc != 1) {
		return SLURM_ERROR;
	}

	// If the request is different than the default, perform the
	// required action.
	debug2("Setting GPU mps state to %d prior to launch", gpu_enable);
	rc = alpsc_pre_launch_GPU_mps(&err_msg, gpu_enable);
	ALPSC_CN_DEBUG("alpsc_pre_launch_GPU_mps");
	if (rc != 1) {
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/*
 * Reset the gpu to its default state after the job completes.
 *
 */
int reset_gpu(stepd_step_rec_t *job)
{
	int rc, gpu_enable;
	char *err_msg;

	gpu_enable = _get_mps_request(job);
	if (gpu_enable > 1) {
		// No action required, return with success.
		return SLURM_SUCCESS;
	}

	debug2("Resetting GPU mps state from %d after launch", gpu_enable);
	rc = alpsc_post_launch_GPU_mps(&err_msg, gpu_enable);
	ALPSC_CN_DEBUG("alpsc_post_launch_GPU_mps");
	if (rc != 1) {
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

#endif
