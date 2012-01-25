/****************************************************************************\
 *  sdiag.c - Utility for getting information about slurmctld behaviour
 *****************************************************************************
 *  Produced at Barcelona Supercomputing Center, December 2011
 *  Written by Alejandro Lucero <alucero@bsc.es>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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


#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include <slurm.h>
#include "src/common/macros.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_defs.h"

/********************
 * Global Variables *
 ********************/
int sdiag_param = STAT_COMMAND_GET;

stats_info_response_msg_t *buf;

static int _get_info(void);
static int _print_info(void);

stats_info_request_msg_t req;

extern void parse_command_line(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	int rc = 0;

	parse_command_line(argc, argv);

	if (sdiag_param == STAT_COMMAND_RESET) {
		req.command_id = STAT_COMMAND_RESET;
		rc = slurm_reset_statistics((stats_info_request_msg_t *)&req);
		if (rc == SLURM_SUCCESS)
			printf("Reset scheduling statistics\n");
		else
			slurm_perror("slurm_reset_statistics");
		exit(rc);
	} else {
		rc = _get_info();
		if (rc == SLURM_SUCCESS)
			rc = _print_info();
	}

	exit(rc);
}

static int _get_info(void)
{
	int rc;

	req.command_id = STAT_COMMAND_GET;
	rc = slurm_get_statistics(&buf, (stats_info_request_msg_t *)&req);
	if (rc != SLURM_SUCCESS)
		slurm_perror("slurm_get_statistics");

	return rc;
}

static int _print_info(void)
{
	if (!buf) {
		printf("No data available. Probably slurmctld is not working\n");
		return -1;
	}

	printf("*******************************************************\n");
	printf("sdiag output at %s", ctime(&buf->req_time));
	printf("Data since      %s", ctime(&buf->req_time_start));
	printf("*******************************************************\n");

	printf("Server thread count: %d\n", buf->server_thread_count);
	printf("Agent queue size:    %d\n\n", buf->agent_queue_size);
	printf("Jobs submitted: %d\n", buf->jobs_submitted);
	printf("Jobs started:   %d\n",
	       buf->jobs_started + buf->bf_last_backfilled_jobs);
	printf("Jobs completed: %d\n", buf->jobs_completed);
	printf("Jobs canceled:  %d\n", buf->jobs_canceled);
	printf("Jobs failed:    %d\n", buf->jobs_failed);
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
		       " in the middle of backfilling execution\n");
	} else
		printf("\nBackfilling stats\n");

	printf("\tTotal backfilled jobs (since last slurm start): %u\n",
	       buf->bf_backfilled_jobs);
	printf("\tTotal backfilled jobs (since last stats cycle start): %u\n",
	       buf->bf_last_backfilled_jobs);
	printf("\tTotal cycles: %u\n", buf->bf_cycle_counter);
	printf("\tLast cycle when: %s", ctime(&buf->bf_when_last_cycle));
	printf("\tLast cycle: %u\n", buf->bf_cycle_last);
	printf("\tMax cycle:  %u\n", buf->bf_cycle_max);
	if (buf->bf_cycle_counter > 0) {
		printf("\tMean cycle: %u\n",
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
	return 0;
}

