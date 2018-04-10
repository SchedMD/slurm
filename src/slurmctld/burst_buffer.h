/*****************************************************************************\
 *  burst_buffer.h - driver for burst buffer infrastructure and plugin
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#ifndef _SLURM_BURST_BUFFER_H
#define _SLURM_BURST_BUFFER_H

#include "slurm/slurm.h"
#include "src/common/pack.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Initialize the burst buffer infrastructure.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_init(void);

/*
 * Terminate the burst buffer infrastructure. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Load the current burst buffer state (e.g. how much space is available now).
 * Run at the beginning of each scheduling cycle in order to recognize external
 * changes to the burst buffer state (e.g. capacity is added, removed, fails,
 * etc.)
 *
 * init_config IN - true if called as part of slurmctld initialization
 * Returns a Slurm errno.
 */
extern int bb_g_load_state(bool init_config);

/*
 * Return string containing current burst buffer status
 * argc IN - count of status command arguments
 * argv IN - status command arguments
 * RET status string, release memory using xfree()
 */
extern char *bb_g_get_status(uint32_t argc, char **argv);

/*
 * Pack current burst buffer state information for network transmission to
 * user (e.g. "scontrol show burst")
 *
 * Returns a Slurm errno.
 */
extern int bb_g_state_pack(uid_t uid, Buf buffer, uint16_t protocol_version);

/*
 * Note configuration may have changed. Handle changes in BurstBufferParameters.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_reconfig(void);

/*
 * Give the total burst buffer size in MB of a given plugin name (e.g. "cray");.
 * If "name" is NULL, return the total space of all burst buffer plugins.
 */
extern uint64_t bb_g_get_system_size(char *name);

/*
 * Preliminary validation of a job submit request with respect to burst buffer
 * options. Performed after setting default account + qos, but prior to
 * establishing job ID or creating script file.
 *
 * job_desc IN - Job submission request
 * submit_uid IN - ID of the user submitting the job.
 * Returns a Slurm errno.
 */
extern int bb_g_job_validate(struct job_descriptor *job_desc,
			     uid_t submit_uid);

/*
 * Secondary validation of a job submit request with respect to burst buffer
 * options. Performed after establishing job ID and creating script file.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_validate2(struct job_record *job_ptr, char **err_msg);

/*
 * Convert a pack job batch script into a script containing only the portions
 * relevant to a specific pack job component.
 *
 * script IN - Whole job batch script
 * pack_job_offset IN - Zero origin pack job component ID
 * RET script for that job component, call xfree() to release memory
 */
extern char *bb_g_build_pack_script(char *script, uint32_t pack_job_offset);

/*
 * Fill in the tres_cnt (in MB) based off the job record
 * NOTE: Based upon job-specific burst buffers, excludes persistent buffers
 * IN job_ptr - job record
 * IN/OUT tres_cnt - fill in this already allocated array with tres_cnts
 * IN locked - if the assoc_mgr tres read locked is locked or not
 */
extern void bb_g_job_set_tres_cnt(struct job_record *job_ptr,
				  uint64_t *tres_cnt, bool locked);

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_g_job_get_est_start(struct job_record *job_ptr);

/*
 * Allocate burst buffers to jobs expected to start soonest
 * Job records must be read locked
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_try_stage_in(void);

/*
 * Determine if a job's burst buffer stage-in is complete
 * job_ptr IN - Job to test
 * test_only IN - If false, then attempt to load burst buffer if possible
 *
 * RET: 0 - stage-in is underway
 *      1 - stage-in complete
 *     -1 - stage-in not started or burst buffer in some unexpected state
 */
extern int bb_g_job_test_stage_in(struct job_record *job_ptr, bool test_only);

/* Attempt to claim burst buffer resources.
 * At this time, bb_g_job_test_stage_in() should have been run sucessfully AND
 * the compute nodes selected for the job.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_begin(struct job_record *job_ptr);

/* Revoke allocation, but do not release resources.
 * Executed after bb_g_job_begin() if there was an allocation failure.
 * Does not release previously allocated resources.
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_revoke_alloc(struct job_record *job_ptr);

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_start_stage_out(struct job_record *job_ptr);

/*
 * Determine if a job's burst buffer post_run operation is complete
 *
 * RET: 0 - post_run is underway
 *      1 - post_run complete
 *     -1 - fatal error
 */
extern int bb_g_job_test_post_run(struct job_record *job_ptr);

/*
 * Determine if a job's burst buffer stage-out is complete
 *
 * RET: 0 - stage-out is underway
 *      1 - stage-out complete
 *     -1 - fatal error
 */
extern int bb_g_job_test_stage_out(struct job_record *job_ptr);

/*
 * Terminate any file staging and completely release burst buffer resources
 *
 * Returns a Slurm errno.
 */
extern int bb_g_job_cancel(struct job_record *job_ptr);

/*
 * Translate a burst buffer string to it's equivalent TRES string
 * (e.g. "cray:2G,generic:4M" -> "1004=2048,1005=4")
 * Caller must xfree the return value
 */
extern char *bb_g_xlate_bb_2_tres_str(char *burst_buffer);

#endif /* !_SLURM_BURST_BUFFER_H */
