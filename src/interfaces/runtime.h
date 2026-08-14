/*****************************************************************************\
 *  runtime.h - job runtime plugin interface.
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

#ifndef _INTERFACES_RUNTIME_H
#define _INTERFACES_RUNTIME_H

#include <stdbool.h>
#include <stdio.h>

#include "src/common/slurm_opt.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

struct option;

/* Forward declaration to avoid pulling slurmd.h into this header. */
typedef struct slurmd_config slurmd_conf_t;

/*
 * Index into the arrays of loaded runtime plugins. Index 0 is a permanently
 * empty placeholder, so a zero-initialized index means no runtime resolved.
 */
#define RUNTIME_IDX_INVALID 0
#define RUNTIME_IDX_DEFAULT 1

typedef enum {
	RUNTIME_CTXT_INVALID = 0,
	RUNTIME_CTXT_SUBMIT, /* srun/salloc/sbatch/slurmrestd */
	RUNTIME_CTXT_SLURMD,
	RUNTIME_CTXT_SLURMSTEPD,
	RUNTIME_CTXT_INVALID_MAX
} runtime_context_t;

/*
 * Initialize the runtime plugin.
 * IN plugin_name - Plugin name or NULL for default
 * IN context - Calling context for plugin
 * OUT idx_ptr - index of the loaded plugin, RUNTIME_IDX_INVALID on failure
 * RET SLURM_SUCCESS or error
 */
extern int runtime_g_init(const char *plugin_name, runtime_context_t context,
			  int *idx_ptr);
extern void runtime_g_fini(void);

/*
 * The accessors below read the plugin arrays without holding the interface's
 * lock, while loading a plugin grows them with xrecalloc() under it, which may
 * move them and free the block a reader is walking. Every plugin a caller
 * intends to use must therefore be loaded before the first call to any of
 * them. slurmstepd meets this by loading its one plugin in _init_from_slurmd()
 * before any thread exists; a caller that wants to load plugins concurrently
 * with these calls needs the arrays to stop moving first.
 */

/*
 * Set up the runtime for the step. Runs in slurmstepd.
 * IN idx - index of the loaded plugin to set up
 */
extern int runtime_g_setup(const int idx, slurmd_conf_t *conf,
			   stepd_step_rec_t *step, slurm_addr_t *cli,
			   slurm_msg_t *msg);

/*
 * Clean up the runtime for the step. Runs in slurmstepd.
 * IN idx - index of the loaded plugin to clean up
 */
extern void runtime_g_cleanup(const int idx, slurmd_conf_t *conf,
			      stepd_step_rec_t *step);

/*
 * Prepare the runtime for the specified task. Runs in slurmstepd.
 * IN idx - index of the loaded plugin to prepare
 */
extern void runtime_g_task_init(const int idx, slurmd_conf_t *conf,
				stepd_step_rec_t *step,
				stepd_step_task_info_t *task);

/*
 * Run the specified task within the runtime. Runs in slurmstepd.
 * The plugin execs the task and only returns on failure, or returns
 * ESLURM_NOT_SUPPORTED without exec'ing to have the caller exec the task.
 * IN idx - index of the loaded plugin to run the task within
 * RET ESLURM_NOT_SUPPORTED if the caller must exec the task, otherwise the
 *	errno from the failed exec
 */
extern int runtime_g_run(const int idx, slurmd_conf_t *conf,
			 stepd_step_rec_t *step, stepd_step_task_info_t *task);

#endif
