/*****************************************************************************\
 *  launch.h - Define job launch plugin functions.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#include "src/srun/srun_job.h"
#include "src/srun/opt.h"
#include "src/srun/debugger.h"

extern slurm_step_layout_t *launch_common_get_slurm_step_layout(
	srun_job_t *job);

extern int launch_common_create_job_step(srun_job_t *job, bool use_all_cpus,
					 void (*signal_function)(int),
					 sig_atomic_t *destroy_job);

extern int launch_init(void);
extern int launch_fini(void);
extern int launch_g_setup_srun_opt(char **rest);

extern int launch_g_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job);
extern int launch_g_step_launch(
	srun_job_t *job, slurm_step_io_fds_t *cio_fds,
	uint32_t *global_rc, bool got_alloc);

extern int launch_g_step_terminate(void);

extern void launch_g_print_status(void);

extern void launch_g_fwd_signal(int signal);

#endif /* _LAUNCH_H */
