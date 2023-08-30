/*****************************************************************************\
 *  spank.h -- plugin stack handling
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _PLUGSTACK_H
#define _PLUGSTACK_H

#include <config.h>

#define _GNU_SOURCE

#include <getopt.h>

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#define SPANK_OPTION_ENV_PREFIX "_SLURM_SPANK_OPTION_"

struct spank_launcher_job_info {
	uid_t       uid;
	gid_t       gid;
	uint32_t    jobid;
	uint32_t    stepid;
	slurm_step_layout_t *step_layout;
	int         argc;
	char      **argv;
};

int spank_init(stepd_step_rec_t *step);

int spank_slurmd_init (void);

int spank_job_prolog(uint32_t jobid, uid_t uid, gid_t gid);

int spank_init_allocator (void);

int spank_init_post_opt (void);

int spank_user(stepd_step_rec_t *step);

int spank_local_user (struct spank_launcher_job_info *job);

int spank_task_privileged(stepd_step_rec_t *step, int taskid);

int spank_user_task(stepd_step_rec_t *step, int taskid);

int spank_task_post_fork(stepd_step_rec_t *step, int taskid);

int spank_task_exit(stepd_step_rec_t *step, int taskid);

int spank_job_epilog(uint32_t jobid, uid_t uid, gid_t gid);

int spank_slurmd_exit (void);

int spank_fini(stepd_step_rec_t *step);

/*
 * Return true if a loaded spank plugin has a prolog function defined.
 */
extern bool spank_has_prolog(void);

/*
 * Return true if a loaded spank plugin has an epilog function defined.
 */
extern bool spank_has_epilog(void);

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
 *  Process all spank options in the environment calling the options callback if
 *  found. The option should handle being called twice -- environment variable
 *  and by command line.
 *
 *  Returns <0 if any option's callback fails. Zero otherwise.
 */
extern int spank_process_env_options();

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
int spank_print_options(FILE *fp, int left_pad, int width);

/*  Set all registered remote options (i.e. those passed to
 *   spank_process_option) in the job options `options'.
 */
int spank_set_remote_options(List options);

/*  Clear any spank remote options encoded in environment.
 */
int spank_clear_remote_options_env (char **env);

/*
 * spank_get_plugin_names
 * Get names of all spank plugins
 *
 * Parameters:
 *   names IN/OUT: pointer to char ** (should be NULL when called)
 *                 output of function is allocated memory for the
 *                 array of string pointers, and allocated memory
 *                 for the strings.  Array will be NULL terminated.
 *                 Caller should manage the memory.
 * Returns:
 *   number of allocated strings (excluding NULL terminator)
 */
size_t spank_get_plugin_names(char ***names);

/*
 * spank_get_plugin_option_names
 * Get names of all spank plugins
 *
 * Parameters:
 * IN plugin_name	- Name of spank plugin being considered
 *			  (e.g., from spank_get_plugin_names)
 * IN/OUT opts		- Pointer to char ** (should be NULL when called)
 *			  output of function is allocated memory for the array
 *			  of string pointers, and allocated memory for the
 *			  strings. Array will be NULL terminated. Caller
 *			  should manage the memory.
 * Returns:
 *			- Number of allocated strings (excluding NULL
 *			  terminator)
 */
size_t spank_get_plugin_option_names(const char *plugin_name, char ***opts);

/*
 * Get option value by common option name
 */
extern char *spank_option_get(char *optname);

/*
 * Get plugin name by common option name
 */
extern char *spank_option_plugin(char *optname);

/*
 * Is option set? Discover by common option name
 */
extern bool spank_option_isset(char *optname);

/*
 * Function for iterating through all the common option data structure
 * and returning (via parameter arguments) the name and value of each
 * set slurm option.
 *
 * OUT plugin	- pointer to string to store the plugin name
 * OUT name	- pointer to string to store the option name
 * OUT value	- pointer to string to store the value
 * IN/OUT state	- internal state, should point to NULL for the first call
 * RETURNS	- true if plugin/name/value set; false if no more options
 */
extern bool spank_option_get_next_set(char **plugin, char **name,
				      char **value, void **state);
#endif /* !_PLUGSTACK_H */
