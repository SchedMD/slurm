/****************************************************************************\
 *  sdiag.c - Utility for getting information about slurmctld behavior
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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"

#include "sdiag.h"

/********************
 * Global Variables *
 ********************/
struct sdiag_parameters params = {0};

stats_info_response_msg_t *buf;
uint32_t *rpc_type_ave_time = NULL, *rpc_user_ave_time = NULL;

static int  _print_stats(void);
static void _sort_rpc(void);

stats_info_request_msg_t req;

extern void parse_command_line(int argc, char **argv);

int main(int argc, char **argv)
{
	int rc = 0;

	slurm_init(NULL);
	parse_command_line(argc, argv);

	if (params.mode == STAT_COMMAND_RESET) {
		req.command_id = STAT_COMMAND_RESET;
		rc = slurm_reset_statistics((stats_info_request_msg_t *)&req);
		if (rc == SLURM_SUCCESS)
			printf("Reset scheduling statistics\n");
		else
			slurm_perror("slurm_reset_statistics");
	} else {
		req.command_id = STAT_COMMAND_GET;
		rc = slurm_get_statistics(&buf, &req);
		if (rc == SLURM_SUCCESS) {
			_sort_rpc();

			if (params.mimetype) {
				rc = DATA_DUMP_CLI(STATS_MSG, *buf,
						   "statistics", argc, argv,
						   NULL, params.mimetype);
			} else {
				rc = _print_stats();
			}
			slurm_free_stats_response_msg(buf);
			xfree(rpc_type_ave_time);
			xfree(rpc_user_ave_time);
		} else
			slurm_perror("slurm_get_statistics");
	}

	exit(rc);
}

static int _print_stats(void)
{
	int i;

	if (!buf) {
		printf("No data available. Probably slurmctld is not working\n");
		return -1;
	}

	printf("*******************************************************\n");
	printf("sdiag output at %s (%ld)\n",
	       slurm_ctime2(&buf->req_time), buf->req_time);
	printf("Data since      %s (%ld)\n",
	       slurm_ctime2(&buf->req_time_start), buf->req_time_start);
	printf("*******************************************************\n");

	printf("Server thread count:  %d\n", buf->server_thread_count);
	printf("Agent queue size:     %d\n", buf->agent_queue_size);
	printf("Agent count:          %d\n", buf->agent_count);
	printf("Agent thread count:   %d\n", buf->agent_thread_count);
	printf("DBD Agent queue size: %d\n\n", buf->dbd_agent_queue_size);

	printf("Jobs submitted: %d\n", buf->jobs_submitted);
	printf("Jobs started:   %d\n", buf->jobs_started);
	printf("Jobs completed: %d\n", buf->jobs_completed);
	printf("Jobs canceled:  %d\n", buf->jobs_canceled);
	printf("Jobs failed:    %d\n\n", buf->jobs_failed);

	printf("Job states ts:  %s (%ld)\n",
	       slurm_ctime2(&buf->job_states_ts), buf->job_states_ts);
	printf("Jobs pending:   %d\n", buf->jobs_pending);
	printf("Jobs running:   %d\n", buf->jobs_running);

	printf("\nMain schedule statistics (microseconds):\n");
	printf("\tLast cycle:   %u\n", buf->schedule_cycle_last);
	printf("\tMax cycle:    %u\n", buf->schedule_cycle_max);
	printf("\tTotal cycles: %u\n", buf->schedule_cycle_counter);
	if (buf->schedule_cycle_counter > 0) {
		printf("\tMean cycle:   %u\n",
		       buf->schedule_cycle_sum / buf->schedule_cycle_counter);
		printf("\tMean depth cycle:  %u\n",
		       buf->schedule_cycle_depth / buf->schedule_cycle_counter);
	}
	if ((buf->req_time - buf->req_time_start) > 60) {
		printf("\tCycles per minute: %u\n",
		       (uint32_t) (buf->schedule_cycle_counter /
		       ((buf->req_time - buf->req_time_start) / 60)));
	}
	printf("\tLast queue length: %u\n", buf->schedule_queue_len);

	if (buf->bf_active) {
		printf("\nBackfilling stats (WARNING: data obtained"
		       " in the middle of backfilling execution.)\n");
	} else
		printf("\nBackfilling stats\n");

	printf("\tTotal backfilled jobs (since last slurm start): %u\n",
	       buf->bf_backfilled_jobs);
	printf("\tTotal backfilled jobs (since last stats cycle start): %u\n",
	       buf->bf_last_backfilled_jobs);
	printf("\tTotal backfilled heterogeneous job components: %u\n",
	       buf->bf_backfilled_het_jobs);
	printf("\tTotal cycles: %u\n", buf->bf_cycle_counter);
	if (buf->bf_when_last_cycle > 0) {
		printf("\tLast cycle when: %s (%ld)\n",
		       slurm_ctime2(&buf->bf_when_last_cycle),
		       buf->bf_when_last_cycle);
	} else {
		printf("\tLast cycle when: N/A\n");
	}
	printf("\tLast cycle: %u\n", buf->bf_cycle_last);
	printf("\tMax cycle:  %u\n", buf->bf_cycle_max);
	if (buf->bf_cycle_counter > 0) {
		printf("\tMean cycle: %"PRIu64"\n",
		       buf->bf_cycle_sum / buf->bf_cycle_counter);
	}
	printf("\tLast depth cycle: %u\n", buf->bf_last_depth);
	printf("\tLast depth cycle (try sched): %u\n", buf->bf_last_depth_try);
	if (buf->bf_cycle_counter > 0) {
		printf("\tDepth Mean: %u\n",
		       buf->bf_depth_sum / buf->bf_cycle_counter);
		printf("\tDepth Mean (try depth): %u\n",
		       buf->bf_depth_try_sum / buf->bf_cycle_counter);
	}
	printf("\tLast queue length: %u\n", buf->bf_queue_len);
	if (buf->bf_cycle_counter > 0) {
		printf("\tQueue length mean: %u\n",
		       buf->bf_queue_len_sum / buf->bf_cycle_counter);
	}
	printf("\tLast table size: %u\n", buf->bf_table_size);
	if (buf->bf_cycle_counter > 0) {
		printf("\tMean table size: %u\n",
		       buf->bf_table_size_sum / buf->bf_cycle_counter);
	}

	printf("\nLatency for 1000 calls to gettimeofday(): %d microseconds\n",
	       buf->gettimeofday_latency);

	printf("\nRemote Procedure Call statistics by message type\n");
	for (i = 0; i < buf->rpc_type_size; i++) {
		printf("\t%-40s(%5u) count:%-6u "
		       "ave_time:%-6u total_time:%"PRIu64"\n",
		       rpc_num2string(buf->rpc_type_id[i]),
		       buf->rpc_type_id[i], buf->rpc_type_cnt[i],
		       rpc_type_ave_time[i], buf->rpc_type_time[i]);
	}

	printf("\nRemote Procedure Call statistics by user\n");
	for (i = 0; i < buf->rpc_user_size; i++) {
		char *user = uid_to_string_or_null(buf->rpc_user_id[i]);
		if (!user)
			xstrfmtcat(user, "%u", buf->rpc_user_id[i]);

		printf("\t%-16s(%8u) count:%-6u "
		       "ave_time:%-6u total_time:%"PRIu64"\n",
		       user, buf->rpc_user_id[i], buf->rpc_user_cnt[i],
		       rpc_user_ave_time[i], buf->rpc_user_time[i]);

		xfree(user);
	}

	printf("\nPending RPC statistics\n");
	if (buf->rpc_queue_type_count == 0)
		printf("\tNo pending RPCs\n");
	for (i = 0; i < buf->rpc_queue_type_count; i++){
		printf("\t%-40s(%5u) count:%-6u\n",
		       rpc_num2string(buf->rpc_queue_type_id[i]),
		       buf->rpc_queue_type_id[i],
		       buf->rpc_queue_count[i]);
	}

	if (buf->rpc_dump_count > 0) {
		printf("\nPending RPCs\n");
	}

	for (i = 0; i < buf->rpc_dump_count; i++) {
		printf("\t%2u: %-36s %s\n",
		       i+1,
		       rpc_num2string(buf->rpc_dump_types[i]),
		       buf->rpc_dump_hostlist[i]);
	}

	return 0;
}

static void _sort_rpc(void)
{
	int i, j;
	uint16_t type_id;
	uint32_t type_ave, type_cnt, user_ave, user_cnt, user_id;
	uint64_t type_time, user_time;

	rpc_type_ave_time = xmalloc(sizeof(uint32_t) * buf->rpc_type_size);
	rpc_user_ave_time = xmalloc(sizeof(uint32_t) * buf->rpc_user_size);

	if (params.sort == SORT_ID) {
		for (i = 0; i < buf->rpc_type_size; i++) {
			for (j = i+1; j < buf->rpc_type_size; j++) {
				if (buf->rpc_type_id[i] <= buf->rpc_type_id[j])
					continue;
				type_id   = buf->rpc_type_id[i];
				type_cnt  = buf->rpc_type_cnt[i];
				type_time = buf->rpc_type_time[i];
				buf->rpc_type_id[i]   = buf->rpc_type_id[j];
				buf->rpc_type_cnt[i]  = buf->rpc_type_cnt[j];
				buf->rpc_type_time[i] = buf->rpc_type_time[j];
				buf->rpc_type_id[j]   = type_id;
				buf->rpc_type_cnt[j]  = type_cnt;
				buf->rpc_type_time[j] = type_time;
			}
			if (buf->rpc_type_cnt[i]) {
				rpc_type_ave_time[i] = buf->rpc_type_time[i] /
						       buf->rpc_type_cnt[i];
			}
		}
		for (i = 0; i < buf->rpc_user_size; i++) {
			for (j = i+1; j < buf->rpc_user_size; j++) {
				if (buf->rpc_user_id[i] <= buf->rpc_user_id[j])
					continue;
				user_id   = buf->rpc_user_id[i];
				user_cnt  = buf->rpc_user_cnt[i];
				user_time = buf->rpc_user_time[i];
				buf->rpc_user_id[i]   = buf->rpc_user_id[j];
				buf->rpc_user_cnt[i]  = buf->rpc_user_cnt[j];
				buf->rpc_user_time[i] = buf->rpc_user_time[j];
				buf->rpc_user_id[j]   = user_id;
				buf->rpc_user_cnt[j]  = user_cnt;
				buf->rpc_user_time[j] = user_time;
			}
			if (buf->rpc_user_cnt[i]) {
				rpc_user_ave_time[i] = buf->rpc_user_time[i] /
						       buf->rpc_user_cnt[i];
			}
		}
	} else if (params.sort == SORT_TIME) {
		for (i = 0; i < buf->rpc_type_size; i++) {
			for (j = i+1; j < buf->rpc_type_size; j++) {
				if (buf->rpc_type_time[i] >= buf->rpc_type_time[j])
					continue;
				type_id   = buf->rpc_type_id[i];
				type_cnt  = buf->rpc_type_cnt[i];
				type_time = buf->rpc_type_time[i];
				buf->rpc_type_id[i]   = buf->rpc_type_id[j];
				buf->rpc_type_cnt[i]  = buf->rpc_type_cnt[j];
				buf->rpc_type_time[i] = buf->rpc_type_time[j];
				buf->rpc_type_id[j]   = type_id;
				buf->rpc_type_cnt[j]  = type_cnt;
				buf->rpc_type_time[j] = type_time;
			}
			if (buf->rpc_type_cnt[i]) {
				rpc_type_ave_time[i] = buf->rpc_type_time[i] /
						       buf->rpc_type_cnt[i];
			}
		}
		for (i = 0; i < buf->rpc_user_size; i++) {
			for (j = i+1; j < buf->rpc_user_size; j++) {
				if (buf->rpc_user_time[i] >= buf->rpc_user_time[j])
					continue;
				user_id   = buf->rpc_user_id[i];
				user_cnt  = buf->rpc_user_cnt[i];
				user_time = buf->rpc_user_time[i];
				buf->rpc_user_id[i]   = buf->rpc_user_id[j];
				buf->rpc_user_cnt[i]  = buf->rpc_user_cnt[j];
				buf->rpc_user_time[i] = buf->rpc_user_time[j];
				buf->rpc_user_id[j]   = user_id;
				buf->rpc_user_cnt[j]  = user_cnt;
				buf->rpc_user_time[j] = user_time;
			}
			if (buf->rpc_user_cnt[i]) {
				rpc_user_ave_time[i] = buf->rpc_user_time[i] /
						       buf->rpc_user_cnt[i];
			}
		}
	} else if (params.sort == SORT_TIME2) {
		for (i = 0; i < buf->rpc_type_size; i++) {
			if (buf->rpc_type_cnt[i]) {
				rpc_type_ave_time[i] = buf->rpc_type_time[i] /
						       buf->rpc_type_cnt[i];
			}
		}
		for (i = 0; i < buf->rpc_type_size; i++) {
			for (j = i+1; j < buf->rpc_type_size; j++) {
				if (rpc_type_ave_time[i] >= rpc_type_ave_time[j])
					continue;
				type_ave  = rpc_type_ave_time[i];
				type_id   = buf->rpc_type_id[i];
				type_cnt  = buf->rpc_type_cnt[i];
				type_time = buf->rpc_type_time[i];
				rpc_type_ave_time[i]  = rpc_type_ave_time[j];
				buf->rpc_type_id[i]   = buf->rpc_type_id[j];
				buf->rpc_type_cnt[i]  = buf->rpc_type_cnt[j];
				buf->rpc_type_time[i] = buf->rpc_type_time[j];
				rpc_type_ave_time[j]  = type_ave;
				buf->rpc_type_id[j]   = type_id;
				buf->rpc_type_cnt[j]  = type_cnt;
				buf->rpc_type_time[j] = type_time;
			}
		}
		for (i = 0; i < buf->rpc_user_size; i++) {
			if (buf->rpc_user_cnt[i]) {
				rpc_user_ave_time[i] = buf->rpc_user_time[i] /
						       buf->rpc_user_cnt[i];
			}
		}
		for (i = 0; i < buf->rpc_user_size; i++) {
			for (j = i+1; j < buf->rpc_user_size; j++) {
				if (rpc_user_ave_time[i] >= rpc_user_ave_time[j])
					continue;
				user_ave  = rpc_user_ave_time[i];
				user_id   = buf->rpc_user_id[i];
				user_cnt  = buf->rpc_user_cnt[i];
				user_time = buf->rpc_user_time[i];
				rpc_user_ave_time[i]  = rpc_user_ave_time[j];
				buf->rpc_user_id[i]   = buf->rpc_user_id[j];
				buf->rpc_user_cnt[i]  = buf->rpc_user_cnt[j];
				buf->rpc_user_time[i] = buf->rpc_user_time[j];
				rpc_user_ave_time[j]  = user_ave;
				buf->rpc_user_id[j]   = user_id;
				buf->rpc_user_cnt[j]  = user_cnt;
				buf->rpc_user_time[j] = user_time;
			}
		}
	} else { /* sort by count */
		for (i = 0; i < buf->rpc_type_size; i++) {
			for (j = i+1; j < buf->rpc_type_size; j++) {
				if (buf->rpc_type_cnt[i] >= buf->rpc_type_cnt[j])
					continue;
				type_id   = buf->rpc_type_id[i];
				type_cnt  = buf->rpc_type_cnt[i];
				type_time = buf->rpc_type_time[i];
				buf->rpc_type_id[i]   = buf->rpc_type_id[j];
				buf->rpc_type_cnt[i]  = buf->rpc_type_cnt[j];
				buf->rpc_type_time[i] = buf->rpc_type_time[j];
				buf->rpc_type_id[j]   = type_id;
				buf->rpc_type_cnt[j]  = type_cnt;
				buf->rpc_type_time[j] = type_time;
			}
			if (buf->rpc_type_cnt[i]) {
				rpc_type_ave_time[i] = buf->rpc_type_time[i] /
						       buf->rpc_type_cnt[i];
			}
		}
		for (i = 0; i < buf->rpc_user_size; i++) {
			for (j = i+1; j < buf->rpc_user_size; j++) {
				if (buf->rpc_user_cnt[i] >= buf->rpc_user_cnt[j])
					continue;
				user_id   = buf->rpc_user_id[i];
				user_cnt  = buf->rpc_user_cnt[i];
				user_time = buf->rpc_user_time[i];
				buf->rpc_user_id[i]   = buf->rpc_user_id[j];
				buf->rpc_user_cnt[i]  = buf->rpc_user_cnt[j];
				buf->rpc_user_time[i] = buf->rpc_user_time[j];
				buf->rpc_user_id[j]   = user_id;
				buf->rpc_user_cnt[j]  = user_cnt;
				buf->rpc_user_time[j] = user_time;
			}
			if (buf->rpc_user_cnt[i]) {
				rpc_user_ave_time[i] = buf->rpc_user_time[i] /
						       buf->rpc_user_cnt[i];
			}
		}
	}
}
