/*****************************************************************************\
 * slurm_ epilog.c - Wait until the specified partition is no longer ready and
 *      owned by this user. This is executed via SLURM to synchronize the
 *      user's job execution with slurmctld configuration of partitions.
 *
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <slurm/slurm.h>

#include "src/common/hostlist.h"

#define _DEBUG 0

/*
 * Check the bgblock's status every POLL_SLEEP seconds.
 * Retry for a period of MIN_DELAY + (INCR_DELAY * base partition count).
 * For example if MIN_DELAY=300 and INCR_DELAY=20, wait up to 428 seconds
 * for a 16 base partition bgblock to ready (300 + 20 * 16).
 */
#define POLL_SLEEP 3			/* retry interval in seconds  */
#define MIN_DELAY  300			/* time in seconds */
#define INCR_DELAY 20			/* time in seconds per BP */

int max_delay = MIN_DELAY;
int cur_delay = 0;

static int  _get_job_size(uint32_t job_id);
static void _wait_part_not_ready(uint32_t job_id);

int main(int argc, char *argv[])
{
	char *job_id_char = NULL;
	uint32_t job_id;

	job_id_char = getenv("SLURM_JOB_ID");		/* get SLURM job ID */
	if (!job_id_char) {
		fprintf(stderr, "SLURM_JOB_ID not set\n");
		exit(0);
	}

	job_id = (uint32_t) atol(job_id_char);
	if (job_id == 0) {
		fprintf(stderr, "SLURM_JOB_ID invalid: %s\n", job_id_char);
		exit(0);
	}

	_wait_part_not_ready(job_id);
	exit(0);
}

static void _wait_part_not_ready(uint32_t job_id)
{
	int is_ready = 1, i, rc;

	max_delay = MIN_DELAY + (INCR_DELAY * _get_job_size(job_id));

#if _DEBUG
	printf("Waiting for job %u to be not ready.", job_id);
#endif

	for (i=0; (cur_delay < max_delay); i++) {
		if (i) {
			sleep(POLL_SLEEP);
			cur_delay += POLL_SLEEP;
#if _DEBUG
			printf(".");
#endif
		}

		rc = slurm_job_node_ready(job_id);
		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if (rc == READY_JOB_ERROR)		/* error */
			continue;			/* retry */
		if ((rc & READY_NODE_STATE) == 0) {
			is_ready = 0;
			break;
		}
	}

#if _DEBUG
	if (is_ready == 1)
		printf("\n");
	else
     		printf("\nJob %u is not ready.\n", job_id);
#endif
	if (is_ready == 1)
		fprintf(stderr, "Job %u is still ready.\n", job_id);

}

static int _get_job_size(uint32_t job_id)
{
	job_info_msg_t *job_buffer_ptr;
	job_info_t * job_ptr;
	int i, size = 1;
	hostlist_t hl;

	if (slurm_load_jobs((time_t) 0, &job_buffer_ptr, SHOW_ALL)) {
		slurm_perror("slurm_load_jobs");
		return 1;
	}

	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		job_ptr = &job_buffer_ptr->job_array[i];
		if (job_ptr->job_id != job_id)
			continue;
		hl = hostlist_create(job_ptr->nodes);
		if (hl) {
			size = hostlist_count(hl);
			hostlist_destroy(hl);
		}
		break;
	}
	slurm_free_job_info_msg (job_buffer_ptr);

#if _DEBUG
	printf("Size is %d\n", size);
#endif
	return size;
}
