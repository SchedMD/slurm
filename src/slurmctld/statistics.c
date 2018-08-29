/****************************************************************************\
 *  statistics.c - functions for sdiag command
 *****************************************************************************
 *  Produced at Barcelona Supercomputing Center, December 2011
 *  Written by Alejandro Lucero <alucero@bsc.es>
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/xstring.h"
#include "src/common/slurmdbd_defs.h"

extern int retry_list_size(void);

/* Pack all scheduling statistics */
extern void pack_all_stat(int resp, char **buffer_ptr, int *buffer_size,
			  uint16_t protocol_version)
{
	Buf buffer;
	int parts_packed;
	int agent_queue_size;
	int agent_count;
	int slurmdbd_queue_size;
	time_t now = time(NULL);
	uint32_t uint32_tmp;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	if (resp) {
		if (acct_storage_g_get_data(acct_db_conn,
					    ACCT_STORAGE_INFO_AGENT_COUNT,
					    &slurmdbd_queue_size)
		    != SLURM_SUCCESS)
			slurmdbd_queue_size = 0;
	}

	buffer = init_buf(BUF_SIZE);
	if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
		parts_packed = resp;
		pack32(parts_packed, buffer);

		if (resp) {
			pack_time(now, buffer);
			debug3("pack_all_stat: time = %u",
			       (uint32_t) last_proc_req_start);
			pack_time(last_proc_req_start, buffer);

			debug3("pack_all_stat: server_thread_count = %u",
			       slurmctld_config.server_thread_count);
			pack32(slurmctld_config.server_thread_count, buffer);

			agent_queue_size = retry_list_size();
			pack32(agent_queue_size, buffer);
			agent_count = get_agent_count();
			pack32(agent_count, buffer);
			pack32(slurmdbd_queue_size, buffer);
			pack32(slurmctld_diag_stats.latency, buffer);

			pack32(slurmctld_diag_stats.jobs_submitted, buffer);
			pack32(slurmctld_diag_stats.jobs_started, buffer);
			pack32(slurmctld_diag_stats.jobs_completed, buffer);
			pack32(slurmctld_diag_stats.jobs_canceled, buffer);
			pack32(slurmctld_diag_stats.jobs_failed, buffer);

			pack32(slurmctld_diag_stats.jobs_pending, buffer);
			pack32(slurmctld_diag_stats.jobs_running, buffer);
			pack_time(slurmctld_diag_stats.job_states_ts, buffer);

			pack32(slurmctld_diag_stats.schedule_cycle_max,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_last,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_sum,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_counter,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_depth,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_queue_len, buffer);

			pack32(slurmctld_diag_stats.backfilled_jobs, buffer);
			pack32(slurmctld_diag_stats.last_backfilled_jobs,
			       buffer);
			pack32(slurmctld_diag_stats.bf_cycle_counter, buffer);
			pack64(slurmctld_diag_stats.bf_cycle_sum, buffer);
			pack32(slurmctld_diag_stats.bf_cycle_last, buffer);
			pack32(slurmctld_diag_stats.bf_last_depth, buffer);
			pack32(slurmctld_diag_stats.bf_last_depth_try, buffer);

			pack32(slurmctld_diag_stats.bf_queue_len, buffer);
			pack32(slurmctld_diag_stats.bf_cycle_max, buffer);
			pack_time(slurmctld_diag_stats.bf_when_last_cycle,
				  buffer);
			pack32(slurmctld_diag_stats.bf_depth_sum, buffer);
			pack32(slurmctld_diag_stats.bf_depth_try_sum, buffer);
			pack32(slurmctld_diag_stats.bf_queue_len_sum, buffer);

			pack32(slurmctld_diag_stats.bf_active, buffer);
			pack32(slurmctld_diag_stats.backfilled_pack_jobs,
			       buffer);
		}
	} else if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		parts_packed = resp;
		pack32(parts_packed, buffer);

		if (resp) {
			pack_time(now, buffer);
			debug3("pack_all_stat: time = %u",
			       (uint32_t) last_proc_req_start);
			pack_time(last_proc_req_start, buffer);

			debug3("pack_all_stat: server_thread_count = %u",
			       slurmctld_config.server_thread_count);
			pack32(slurmctld_config.server_thread_count, buffer);

			agent_queue_size = retry_list_size();
			pack32(agent_queue_size, buffer);
			pack32(slurmdbd_queue_size, buffer);

			pack32(slurmctld_diag_stats.jobs_submitted, buffer);
			pack32(slurmctld_diag_stats.jobs_started, buffer);
			pack32(slurmctld_diag_stats.jobs_completed, buffer);
			pack32(slurmctld_diag_stats.jobs_canceled, buffer);
			pack32(slurmctld_diag_stats.jobs_failed, buffer);

			pack32(slurmctld_diag_stats.jobs_running, buffer);
			pack_time(slurmctld_diag_stats.job_states_ts, buffer);

			pack32(slurmctld_diag_stats.schedule_cycle_max,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_last,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_sum,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_counter,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_depth,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_queue_len, buffer);

			pack32(slurmctld_diag_stats.backfilled_jobs, buffer);
			pack32(slurmctld_diag_stats.last_backfilled_jobs,
			       buffer);
			pack32(slurmctld_diag_stats.bf_cycle_counter, buffer);
			pack64(slurmctld_diag_stats.bf_cycle_sum, buffer);
			pack32(slurmctld_diag_stats.bf_cycle_last, buffer);
			pack32(slurmctld_diag_stats.bf_last_depth, buffer);
			pack32(slurmctld_diag_stats.bf_last_depth_try, buffer);

			pack32(slurmctld_diag_stats.bf_queue_len, buffer);
			pack32(slurmctld_diag_stats.bf_cycle_max, buffer);
			pack_time(slurmctld_diag_stats.bf_when_last_cycle,
				  buffer);
			pack32(slurmctld_diag_stats.bf_depth_sum, buffer);
			pack32(slurmctld_diag_stats.bf_depth_try_sum, buffer);
			pack32(slurmctld_diag_stats.bf_queue_len_sum, buffer);

			/* FIXME: Pack as 2 fields in v18.08 */
			uint32_tmp = slurmctld_diag_stats.backfilled_pack_jobs;
			if (slurmctld_diag_stats.bf_active)
				uint32_tmp |= 0x80000000;
			pack32(uint32_tmp,	 buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		parts_packed = resp;
		pack32(parts_packed, buffer);

		if (resp) {
			pack_time(now, buffer);
			debug3("pack_all_stat: time = %u",
			       (uint32_t) last_proc_req_start);
			pack_time(last_proc_req_start, buffer);

			debug3("pack_all_stat: server_thread_count = %u",
			       slurmctld_config.server_thread_count);
			pack32(slurmctld_config.server_thread_count, buffer);

			agent_queue_size = retry_list_size();
			pack32(agent_queue_size, buffer);

			pack32(slurmctld_diag_stats.jobs_submitted, buffer);
			pack32(slurmctld_diag_stats.jobs_started, buffer);
			pack32(slurmctld_diag_stats.jobs_completed, buffer);
			pack32(slurmctld_diag_stats.jobs_canceled, buffer);
			pack32(slurmctld_diag_stats.jobs_failed, buffer);

			pack32(slurmctld_diag_stats.schedule_cycle_max,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_last,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_sum,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_counter,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_cycle_depth,
			       buffer);
			pack32(slurmctld_diag_stats.schedule_queue_len, buffer);

			pack32(slurmctld_diag_stats.backfilled_jobs, buffer);
			pack32(slurmctld_diag_stats.last_backfilled_jobs,
			       buffer);
			pack32(slurmctld_diag_stats.bf_cycle_counter, buffer);
			pack64(slurmctld_diag_stats.bf_cycle_sum, buffer);
			pack32(slurmctld_diag_stats.bf_cycle_last, buffer);
			pack32(slurmctld_diag_stats.bf_last_depth, buffer);
			pack32(slurmctld_diag_stats.bf_last_depth_try, buffer);

			pack32(slurmctld_diag_stats.bf_queue_len, buffer);
			pack32(slurmctld_diag_stats.bf_cycle_max, buffer);
			pack_time(slurmctld_diag_stats.bf_when_last_cycle,
				  buffer);
			pack32(slurmctld_diag_stats.bf_depth_sum, buffer);
			pack32(slurmctld_diag_stats.bf_depth_try_sum, buffer);
			pack32(slurmctld_diag_stats.bf_queue_len_sum, buffer);
			pack32(slurmctld_diag_stats.bf_active,	 buffer);
		}
	}

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}

/* Reset all scheduling statistics
 * level IN - clear backfilled_jobs count if set */
extern void reset_stats(int level)
{
	slurmctld_diag_stats.proc_req_raw = 0;
	slurmctld_diag_stats.proc_req_threads = 0;
	slurmctld_diag_stats.schedule_cycle_max = 0;
	slurmctld_diag_stats.schedule_cycle_sum = 0;
	slurmctld_diag_stats.schedule_cycle_counter = 0;
	slurmctld_diag_stats.schedule_cycle_depth = 0;
	slurmctld_diag_stats.jobs_submitted = 0;
	slurmctld_diag_stats.jobs_started = 0;
	slurmctld_diag_stats.jobs_completed = 0;
	slurmctld_diag_stats.jobs_canceled = 0;
	slurmctld_diag_stats.jobs_failed = 0;

	/* Just resetting this value when reset requested explicitly */
	if (level)
		slurmctld_diag_stats.backfilled_jobs = 0;

	slurmctld_diag_stats.last_backfilled_jobs = 0;
	slurmctld_diag_stats.backfilled_pack_jobs = 0;
	slurmctld_diag_stats.bf_cycle_counter = 0;
	slurmctld_diag_stats.bf_cycle_sum = 0;
	slurmctld_diag_stats.bf_cycle_last = 0;
	slurmctld_diag_stats.bf_depth_sum = 0;
	slurmctld_diag_stats.bf_depth_try_sum = 0;
	slurmctld_diag_stats.bf_queue_len = 0;
	slurmctld_diag_stats.bf_queue_len_sum = 0;
	slurmctld_diag_stats.bf_cycle_max = 0;
	slurmctld_diag_stats.bf_last_depth = 0;
	slurmctld_diag_stats.bf_last_depth_try = 0;
	slurmctld_diag_stats.bf_active = 0;

	last_proc_req_start = time(NULL);
}
