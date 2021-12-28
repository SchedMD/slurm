/*****************************************************************************\
 *  slurmscriptd.h - Definitions of functions and structures for slurmscriptd.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
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

#ifndef _HAVE_SLURMSCRIPTD_H
#define _HAVE_SLURMSCRIPTD_H

extern int slurmscriptd_init(int argc, char **argv);

extern int slurmscriptd_fini(void);

/*
 * slurmscriptd_flush - kill all running scripts.
 *
 * This function blocks until slurmscriptd responds that it is finished.
 */
extern void slurmscriptd_flush(void);

/*
 * slurmscriptd_flush_job - kill all running script for a specific job
 */
extern void slurmscriptd_flush_job(uint32_t job_id);

/*
 * slurmscriptd_reconfig - re-initialize slurmscriptd configuration
 *
 * This function acquires locks in slurmctld_lock_t, so none of those should be
 * locked upon calling this function.
 */
extern void slurmscriptd_reconfig(void);

/*
 * slurmscriptd_run_bb_lua
 * Tell slurmscriptd to run a specific function in the script in the
 * burst_buffer/lua plugin
 * IN job_id - the job for which we're running the script
 * IN function - the function in the lua script we should run
 * IN argc - number of arguments
 * IN argv - arguments for the script
 * IN timeout - timeout in seconds
 * OUT resp - response message from the script
 * OUT track_script_signalled - true if the script was killed by track_script,
 *                              false otherwise.
 * RET return code of the script or SLURM_ERROR if there was some other failure
 */
extern int slurmscriptd_run_bb_lua(uint32_t job_id, char *function,
				   uint32_t argc, char **argv, uint32_t timeout,
				   char **resp, bool *track_script_signalled);

extern int slurmscriptd_run_mail(char *script_path, uint32_t argc, char **argv,
				 char **env, uint32_t timeout, char **resp);

/*
 * slurmscriptd_run_prepilog
 * Tell slurmscriptd to run PrologSlurmctld or EpilogSlurmctld for the job
 * IN job_id - Job that wants to run the script
 * IN is_epilog - True if the EpilogSlurmctld should run; false if the
 *                PrologSlurmctld should run
 * IN script - Full path to the script that needs to run
 * IN env - Environment to pass to the script
 */
extern void slurmscriptd_run_prepilog(uint32_t job_id, bool is_epilog,
				      char *script, char **env);

/*
 * slurmscriptd_update_debug_flags
 * Update the debug flags for slurmscriptd.
 */
extern void slurmscriptd_update_debug_flags(uint64_t debug_flags);

/*
 * slurmscriptd_update_log_level
 * Update the logging level for slurmscriptd.
 *
 * IN debug_level
 * IN log_rotate - true if log_rotate (re-open log files)
 */
extern void slurmscriptd_update_log_level(int debug_level, bool log_rotate);

#endif /* !_HAVE_SLURMSCRIPTD_H */
