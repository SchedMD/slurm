/*****************************************************************************\
 *  plugstack.h -- plugin stack handling
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifndef _PLUGSTACK_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef   _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include "src/common/job_options.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

struct spank_launcher_job_info {
	uid_t       uid;
	gid_t       gid;
	uint32_t    jobid;
	uint32_t    stepid;
	slurm_step_layout_t *step_layout;
	int         argc;
	char      **argv;
};

int spank_init (slurmd_job_t *job);

int spank_user (slurmd_job_t *job);

int spank_local_user (struct spank_launcher_job_info *job);

int spank_user_task (slurmd_job_t *job, int taskid);

int spank_task_post_fork (slurmd_job_t *job, int taskid);

int spank_task_exit (slurmd_job_t *job, int taskid);

int spank_fini (slurmd_job_t *job);

/*
 *  Option processing
 */

/*
 *  Create a struct option table (suitable for passing to getopt_long())
 *   from SPANK plugin provided options, optionally prepending an existing
 *   table of options `orig_options'  Result must be freed by 
 *   spank_option_table_destroy().
 *
 *  If any options in orig_options conflict with internal spank options,
 *   a warning will be printed and the spank option will be disabled.
 *
 */
struct option *spank_option_table_create (const struct option *orig_options);

/*
 *  Free memory associated with an option table created by 
 *   spank_p[tion_table_create.
 */
void spank_option_table_destroy (struct option *opt_table);

/*
 *  Process a single spank option which was tagged by `optval' in the
 *   spank option table. If the option takes and argument (i.e. has_arg = 1)
 *   then optarg must be non-NULL.
 *
 *  Returns < 0 if no option is found which matches `optval', or if
 *   the option belongs to a *required* plugin, and the plugin's callback
 *   for that option fails.
 */
int spank_process_option (int optval, const char *optarg); 

/*
 *  Generate --help style output on stream `fp' for all internal spank
 *   options which were not previously disabled (e.g. due to conflicts
 *   with existing options or other plugins). `width' defines the column
 *   after which the usage text may be displayed, and `left_pad' is the
 *   amount of space to pad on the left before printing the --option.
 */
int spank_print_options (FILE *fp, int width, int left_pad);

/*  Set all registered remote options (i.e. those passed to 
 *   spank_process_option) in the job options `options'.
 */
int spank_set_remote_options (job_options_t options);

/*  Register any remote spank options that exist in `options'
 *    to their respective spank plugins. This function ends up invoking
 *    all plugin option callbacks, and will fail (return < 0) if
 *    a *required* plugin callback returns < 0.
 *
 *  A warning is printed if no plugin matches a remote option
 *   in the job_options structure, but the funtion does not return failure.
 */
int spank_get_remote_options (job_options_t options);

#endif /* !_PLUGSTACK_H */
