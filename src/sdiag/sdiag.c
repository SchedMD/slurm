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

/*
 * Use one common struct for both rpcs and users.
 * Use the larger type size from either.
 */
typedef struct {
	uint32_t id;
	uint32_t count;
	uint64_t time;
	uint64_t average_time;
	uint16_t queued;
	uint64_t dropped;
	uint16_t cycle_last;
	uint16_t cycle_max;
} rpc_stat_t;

static rpc_stat_t *types = NULL, *users = NULL;

struct sdiag_parameters params = {0};

stats_info_response_msg_t *buf;

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
				DATA_DUMP_CLI_SINGLE(OPENAPI_DIAG_RESP, buf,
						     argc, argv, NULL,
						     params.mimetype,
						     params.data_parser, rc);
			} else {
				rc = _print_stats();
			}
			slurm_free_stats_response_msg(buf);
			xfree(types);
			xfree(users);
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
	printf("RPC queue enabled:    %d\n", buf->rpc_queue_enabled);
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

	printf("\nMain scheduler exit:\n");

	for (i = 0; i < buf->schedule_exit_cnt; i++) {
		printf("\t%s:%2u\n", schedule_exit2string(i),
		       buf->schedule_exit[i]);
	}

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
	printf("\nBackfill exit\n");

	for (i = 0; i < buf->bf_exit_cnt; i++) {
		printf("\t%s:%2u\n", bf_exit2string(i),
		       buf->bf_exit[i]);
	}

	printf("\nLatency for 1000 calls to gettimeofday(): %d microseconds\n",
	       buf->gettimeofday_latency);

	printf("\nRemote Procedure Call statistics by message type\n");
	for (i = 0; i < buf->rpc_type_size; i++) {
		if (!buf->rpc_queue_enabled)
			printf("\t%-40s(%5u) count:%-6u ave_time:%-6"PRIu64" total_time:%"PRIu64"\n",
			       rpc_num2string(types[i].id), types[i].id,
			       types[i].count, types[i].average_time,
			       types[i].time);
		else
			printf("\t%-40s(%5u) count:%-6u ave_time:%-6"PRIu64" total_time:%-12"PRIu64" queued:%-6u cycle_last:%-6u cycle_max:%-6u dropped:%"PRIu64"\n",
			       rpc_num2string(types[i].id), types[i].id,
			       types[i].count, types[i].average_time,
			       types[i].time, types[i].queued,
			       types[i].cycle_last, types[i].cycle_max,
			       types[i].dropped);
	}
	if (!buf->rpc_type_size)
		printf("\tNo RPCs recorded yet.\n");

	printf("\nRemote Procedure Call statistics by user\n");
	for (i = 0; i < buf->rpc_user_size; i++) {
		char *user = uid_to_string(users[i].id);

		printf("\t%-16s(%8u) count:%-6u ave_time:%-6"PRIu64" total_time:%"PRIu64"\n",
		       user, users[i].id, users[i].count, users[i].average_time,
		       users[i].time);

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

/* lowest to highest */
static int _sort_id(const void *p1, const void *p2)
{
	const rpc_stat_t *s1 = p1, *s2 = p2;

	if (s1->id > s2->id)
		return 1;
	else if (s1->id < s2->id)
		return -1;
	return 0;
}

/* highest to lowest */
static int _sort_time(const void *p1, const void *p2)
{
	const rpc_stat_t *s1 = p1, *s2 = p2;

	if (s1->time < s2->time)
		return 1;
	else if (s1->time > s2->time)
		return -1;
	return 0;
}

/* highest to lowest */
static int _sort_average_time(const void *p1, const void *p2)
{
	const rpc_stat_t *s1 = p1, *s2 = p2;

	if (s1->average_time < s2->average_time)
		return 1;
	else if (s1->average_time > s2->average_time)
		return -1;
	return 0;
}

/* highest to lowest */
static int _sort_count(const void *p1, const void *p2)
{
	const rpc_stat_t *s1 = p1, *s2 = p2;

	if (s1->count < s2->count)
		return 1;
	else if (s1->count > s2->count)
		return -1;
	return 0;
}

static void _sort_rpc(void)
{
	int (*sort_function)(const void *, const void *) = _sort_count;

	types = xcalloc(buf->rpc_type_size, sizeof(rpc_stat_t));
	for (int i = 0; i < buf->rpc_type_size; i++) {
		types[i].id = buf->rpc_type_id[i];
		types[i].count = buf->rpc_type_cnt[i];
		types[i].time = buf->rpc_type_time[i];
		if (buf->rpc_type_cnt[i])
			types[i].average_time = buf->rpc_type_time[i] /
						buf->rpc_type_cnt[i];
		if (buf->rpc_queue_enabled) {
			types[i].queued = buf->rpc_type_queued[i];
			types[i].dropped = buf->rpc_type_dropped[i];
			types[i].cycle_last = buf->rpc_type_cycle_last[i];
			types[i].cycle_max = buf->rpc_type_cycle_max[i];
		}
	}

	users = xcalloc(buf->rpc_user_size, sizeof(rpc_stat_t));
	for (int i = 0; i < buf->rpc_user_size; i++) {
		users[i].id = buf->rpc_user_id[i];
		users[i].count = buf->rpc_user_cnt[i];
		users[i].time = buf->rpc_user_time[i];
		if (buf->rpc_user_cnt[i])
			users[i].average_time = buf->rpc_user_time[i] /
						buf->rpc_user_cnt[i];
	}

	if (params.sort == SORT_ID)
		sort_function = _sort_id;
	else if (params.sort == SORT_TIME)
		sort_function = _sort_time;
	else if (params.sort == SORT_TIME2)
		sort_function = _sort_average_time;
	else
		sort_function = _sort_count;

	qsort(types, buf->rpc_type_size, sizeof(rpc_stat_t), sort_function);
	qsort(users, buf->rpc_user_size, sizeof(rpc_stat_t), sort_function);
}
