/*****************************************************************************\
 *  tres_frequency.h - Define TRES frequency control functions
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

#ifndef _TRES_FREQUENCY_H_
#define _TRES_FREQUENCY_H_

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * check if TRES frequency setting is allowed on this node
 * if so, create and initialize appropriate data structures
 */
extern void tres_freq_init(slurmd_conf_t *conf);

/*
 * free memory from TRES frequency data structures
 */
extern void tres_freq_fini(void);

/*
 * reset debug flag (slurmd)
 */
extern void tres_freq_reconfig(void);

/*
 * Send the tres_frequency info to slurmstepd
 */
extern void tres_freq_send_info(int fd);

/*
 * Receive the tres_frequency table info from slurmd
 */
extern void tres_freq_recv_info(int fd);

/*
 * Validate the TRES frequency to set
 * Called from task cpuset code
 */
extern void tres_freq_cpuset_validate(stepd_step_rec_t *step);

/*
 * Validate the cpus and select the frequency to set
 * Called from task cgroup code
 */
extern void tres_freq_cgroup_validate(stepd_step_rec_t *step,
				      char *step_alloc_cores);

#if 0
//FIXME: Not applicable
/*
 * Verify slurm.conf CpuFreqGovernors list
 *
 * Input:  - arg  - string list of governors
 *	   - govs - pointer to composite of enum for each governor in list
 * Returns - -1 on error, else 0
 */
extern int
cpu_freq_verify_govlist(const char *arg, uint32_t *govs);
#endif

/*
 * Verify slurm.conf TresFreqDef option
 *
 * arg IN - Parameter value to check
 * RET - -1 on error, else 0
 */
extern int tres_freq_verify_def(const char *arg);

/*
 * Verify --tres-freq command line option
 *
 * arg IN - Parameter value to check
 * RET - -1 on error, else 0
 */
extern int tres_freq_verify_cmdline(const char *arg);

/*
 * Set environment variables associated with TRES frequency variables.
 */
extern int tres_freq_set_env(char *var);

#if 0
/* Convert a cpu_freq number to its equivalent string */
extern void
cpu_freq_to_string(char *buf, int buf_size, uint32_t cpu_freq);
#endif

/*
 * set TRES frequency values
 */
extern void tres_freq_set(stepd_step_rec_t *step);

/*
 * reset TRES frequency values after suspend/resume
 */
extern void tres_freq_reset(stepd_step_rec_t *step);

#if 0
/*
 * Convert frequency parameters to strings
 * Typically called to produce string for a log or reporting utility.
 *
 *
 * When label!=NULL, info message is put to log. This is convienient for
 *      inserting debug calls to verify values in structures or messages.
 * noval_str==NULL allows missing parameters not to be reported.
 * freq_str is a buffer to hold the composite string for all input values.
 * freq_len is length of freq_str
 *
 * Returns 0 if all parameters are NO_VAL (or 0)
 */
extern int
cpu_freq_debug(char* label, char* noval_str, char* freq_str, int freq_len,
		  uint32_t gov, uint32_t min, uint32_t max, uint32_t freq);
#endif

#endif /* _TRES_FREQUENCY_H_ */
