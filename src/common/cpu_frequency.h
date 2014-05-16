/*****************************************************************************\
 *  cpu_frequency.h - Define cpu frequency control functions
 *****************************************************************************
 *  Copyright (C) 2012 Bull
 *  Written by Don Albert, <don.albert@bull.com>
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

#ifndef _CPU_FREQUENCY_H_
#define _CPU_FREQUENCY_H_

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * check if cpu frequency setting is allowed on this node
 * if so, create and initialize the cpu frequency table entry for each cpu
 */
void
cpu_freq_init(slurmd_conf_t *conf);

/*
 * free memory from cpu frequency table
 */
extern void
cpu_freq_fini(void);

/*
 * Send the cpu_frequency table info to slurmstepd
 */
void
cpu_freq_send_info(int fd);

/*
 * Receive the cpu_frequency table info from slurmd
 */
void
cpu_freq_recv_info(int fd);

/*
 * Validate the cpus and select the frequency to set
 * Called from task cpuset code with job record containing
 *  a pointer to a hex map of the cpus to be used by this step
 */
void
cpu_freq_cpuset_validate(stepd_step_rec_t *job);

/*
 * Validate the cpus and select the frequency to set
 * Called from task cgroup cpuset code with string containing
 *  the list of cpus to be used by this step
 */
void
cpu_freq_cgroup_validate(stepd_step_rec_t *job, char *step_alloc_cores);

/*
 * Verify cpu_freq parameter
 *
 * In addition to a numeric frequency value, we allow the user to specify
 * "low", "medium", "highm1", or "high" frequency plus "performance",
 * "powersave", "userspace" and "ondemand" governor
 *
 * returns -1 on error, 0 otherwise
 */
int
cpu_freq_verify_param(const char *arg, uint32_t *cpu_freq);

/* Convert a cpu_freq number to its equivalent string */
void
cpu_freq_to_string(char *buf, int buf_size, uint32_t cpu_freq);

/*
 * set the userspace governor and the new frequency value
 */
void
cpu_freq_set(stepd_step_rec_t *job);

/*
 * reset the governor and cpu frequency to the configured values
 */
void
cpu_freq_reset(stepd_step_rec_t *job);

#endif /* _CPU_FREQUENCY_H_ */
