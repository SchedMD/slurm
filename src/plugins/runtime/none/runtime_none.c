/*****************************************************************************\
 *  runtime_none.c - no-op job runtime plugin.
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

#include <getopt.h>

#include "slurm/slurm.h"

#include "src/common/slurm_opt.h"

#include "src/interfaces/runtime.h"

/*
 * These variables are required by the generic plugin interface.  See plugin.h
 * for more information.
 */
const char plugin_name[] = "no-op runtime plugin";
const char plugin_type[] = "runtime/none";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

extern int runtime_p_init(runtime_context_t context)
{
	return SLURM_SUCCESS;
}

extern int runtime_p_fini(void)
{
	return SLURM_SUCCESS;
}

extern int runtime_p_setup(slurmd_conf_t *conf, stepd_step_rec_t *step,
			   slurm_addr_t *cli, slurm_msg_t *msg)
{
	return SLURM_SUCCESS;
}

extern void runtime_p_cleanup(slurmd_conf_t *conf, stepd_step_rec_t *step)
{
	/* no-op */
}

extern void runtime_p_task_init(slurmd_conf_t *conf, stepd_step_rec_t *step,
				stepd_step_task_info_t *task)
{
	/* no-op */
}

extern void runtime_p_run(slurmd_conf_t *conf, stepd_step_rec_t *step,
			  stepd_step_task_info_t *task)
{
	/* no-op */
}
