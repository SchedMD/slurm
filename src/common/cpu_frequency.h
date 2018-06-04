/*****************************************************************************\
 *  cpu_frequency.h - Define cpu frequency control functions
 *****************************************************************************
 *  Copyright (C) 2012 Bull
 *  Written by Don Albert, <don.albert@bull.com>
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

#ifndef _CPU_FREQUENCY_H_
#define _CPU_FREQUENCY_H_

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * check if cpu frequency setting is allowed on this node
 * if so, create and initialize the cpu frequency table entry for each cpu
 */
extern void
cpu_freq_init(slurmd_conf_t *conf);

/*
 * free memory from cpu frequency table
 */
extern void
cpu_freq_fini(void);

/*
 * reset debug flag (slurmd)
 */
extern void
cpu_freq_reconfig(void);

/*
 * Send the cpu_frequency table info to slurmstepd
 */
extern void
cpu_freq_send_info(int fd);

/*
 * Receive the cpu_frequency table info from slurmd
 */
extern void
cpu_freq_recv_info(int fd);

/*
 * Validate the cpus and select the frequency to set
 * Called from task cpuset code with job record containing
 *  a pointer to a hex map of the cpus to be used by this step
 */
extern void
cpu_freq_cpuset_validate(stepd_step_rec_t *job);

/*
 * Validate the cpus and select the frequency to set
 * Called from task cgroup cpuset code with string containing
 *  the list of cpus to be used by this step
 */
extern void
cpu_freq_cgroup_validate(stepd_step_rec_t *job, char *step_alloc_cores);

/*
 * Verify slurm.conf CpuFreqGovernors list
 *
 * Input:  - arg  - string list of governors
 *	   - govs - pointer to composite of enum for each governor in list
 * Returns - -1 on error, else 0
 */
extern int
cpu_freq_verify_govlist(const char *arg, uint32_t *govs);

/*
 * Verify slurm.conf CpuFreqDef option
 *
 * Input:  - arg  - frequency value to check
 * 		    valid governor, low, medium, highm1, high,
 * 		    or numeric frequency
 *	   - freq - pointer to corresponging enum or numberic value
 * Returns - -1 on error, else 0
 */
extern int
cpu_freq_verify_def(const char *arg, uint32_t *freq);

/*
 * Verify cpu_freq command line option
 *
 * --cpu-freq=arg
 *   where arg is p1{-p2}{:p3}
 *
 * - p1 can be  [#### | low | medium | high | highm1]
 * 	which will set the current and max cpu frequency, but not the governor.
 * - p1 can be [Conservative | OnDemand | Performance | PowerSave]
 *      which will set the governor to the corresponding value.
 * - If p1 is the first case and is preceded with <, then the value of p1
 *   becomes the max frequency and min is set to "low".
 *   Similarly, if p1 is followed by > then p1 becomes the minimum frequency
 *   and max is set to "high".
 * - When p2 is present, p1 will be the minimum frequency and p2 will be
 *   the maximum.
 * - p2 can be  [#### | medium | high | highm1] p2 must be greater than p1.
 * - If the current frequency is < min, it will be set to min.
 *   Likewise, if the current frequency is > max, it will be set to max.
 * - p3 can be [Conservative | OnDemand | Performance | PowerSave]
 *   which will set the governor to the corresponding value.
 *
 * returns -1 on error, 0 otherwise
 */
extern int
cpu_freq_verify_cmdline(const char *arg,
		uint32_t *cpu_freq_min,
		uint32_t *cpu_freq_max,
		uint32_t *cpu_freq_gov);

/* Convert a composite cpu governor enum to its equivalent string
 *
 * Input:  - buf   - buffer to contain string
 *         - bufsz - size of buffer
 *         - gpvs  - composite enum of governors
 */
extern void
cpu_freq_govlist_to_string(char* buf, uint16_t bufsz, uint32_t govs);

/*
 * Set environment variables associated with the frequency variables.
 */
extern int
cpu_freq_set_env(char* var, uint32_t min, uint32_t max, uint32_t gov);

/* Convert a cpu_freq number to its equivalent string */
extern void
cpu_freq_to_string(char *buf, int buf_size, uint32_t cpu_freq);


/*
 * set the userspace governor and the new frequency value
 */
extern void
cpu_freq_set(stepd_step_rec_t *job);

/*
 * reset the governor and cpu frequency to the configured values
 */
extern void
cpu_freq_reset(stepd_step_rec_t *job);

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

#endif /* _CPU_FREQUENCY_H_ */
