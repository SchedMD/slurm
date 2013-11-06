/*****************************************************************************\
 *  launch.h - Define job launch plugin functions.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _LAUNCH_H
#define _LAUNCH_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/xstring.h"

#include "srun_job.h"
#include "opt.h"
#include "debugger.h"

/*
 * launch_common_get_slurm_step_layout() gets the slurm job step layout.
 *
 * IN job - the job step layout to get.
 *
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern slurm_step_layout_t *launch_common_get_slurm_step_layout(
	srun_job_t *job);

/*
 * launch_common_create_job_step() creates the job step with the given info.
 *
 * IN job - job to be created into a job step
 * IN use_all_cpus - the choice to use all the cpus.
 * IN signal_function - function that handles the signals coming in.
 * IN destroy_job - pointer to a global flag signifying if the job was
 *                  canceled while allocating.
 *
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int launch_common_create_job_step(srun_job_t *job, bool use_all_cpus,
					 void (*signal_function)(int),
					 sig_atomic_t *destroy_job);

/*
 * launch_common_set_stdio_fds() sets the stdio_fds to given info.
 *
 * IN job - the job that is set.
 * IN cio_fds - filling in io descriptors.
 */
extern void launch_common_set_stdio_fds(srun_job_t *job,
					slurm_step_io_fds_t *cio_fds);

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int launch_init(void);

/*
 * fini() is called when the plugin is removed. Clear any allocated
 * storage here.
 */
extern int launch_fini(void);

/*
 * launch_g_setup_srun_opt() is called when the plugin needs the srun
 * operation needs to be set up.
 *
 * IN rest - extra parameters on the command line not processed by srun
 */
extern int launch_g_setup_srun_opt(char **rest);

/*
 * launch_g_handle_multi_prog_verify() is called to verify a
 * multi-prog file if verifying needs to be done.
 *
 * IN command_pos - to be used with global opt variable to tell which
 *                  spot the command is in opt.argv.
 *
 * RET 0 if not handled, 1 if handled
 */
extern int launch_g_handle_multi_prog_verify(int command_pos);

/*
 * launch_g_create_job_step() creates the job step.
 *
 * IN/OUT job - the job to be created into a job step.
 * IN use_all_cpus - the choice to use all the cpus.
 * IN signal_function - function that handles the signals coming in.
 * IN destroy_job - pointer to a global flag signifying if the job was
 *                  canceled while allocating.
 *
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int launch_g_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job);

/*
 * launch_g_step_launch() is called to launch the job step that
 * was created.
 *
 * IN/OUT job - the job needing to be launched
 * IN cio_fds - filled in io descriptors.
 * IN/OUT global_rc - srun global return code.
 * IN step_callbacks - callbacks for various points in the life of the step.
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int launch_g_step_launch(
	srun_job_t *job, slurm_step_io_fds_t *cio_fds,
	uint32_t *global_rc, slurm_step_launch_callbacks_t *step_callbacks);

/*
 * launch_g_step_wait() is called to wait for the job step to be finished.
 *
 * IN/OUT job - the job waiting to finish.
 * IN got_alloc - if the resource allocation was created inside srun
 *
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int launch_g_step_wait(srun_job_t *job, bool got_alloc);

/*
 * launch_g_step_terminate() is called to end the job step.
 *
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int launch_g_step_terminate(void);

/*
 * launch_g_print_status() displays the the status of the job step.
 */
extern void launch_g_print_status(void);

/*
 * launch_g_fwd_signal() send a forward signal to an underlining task.
 *
 * IN signal - the signal to forward to the underlying tasks.
 */
extern void launch_g_fwd_signal(int signal);

#endif /* _LAUNCH_H */
