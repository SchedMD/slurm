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

#include "slurm/slurm.h"

#define _DEBUG 0

/*
 * Check the bgblock's status every POLL_SLEEP seconds.
 * Retry until the job is removed
 */
#define POLL_SLEEP 3			/* retry interval in seconds  */

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
	int is_ready = 1, rc;

#if _DEBUG
	printf("Waiting for job %u to be not ready.", job_id);
#endif

	/* It has been decided waiting forever is a better solution
	   than ending early and saying we are done when in reality
	   the job is still running.  So now we trust the slurmctld to
	   tell us when we are done and never end until that happens.
	*/
	while (1) {
		rc = slurm_job_node_ready(job_id);
		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if (rc == READY_JOB_ERROR)		/* error */
			continue;			/* retry */
		if ((rc & READY_NODE_STATE) == 0) {
			is_ready = 0;
			break;
		}
		sleep(POLL_SLEEP);
#if _DEBUG
		printf(".");
#endif
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
