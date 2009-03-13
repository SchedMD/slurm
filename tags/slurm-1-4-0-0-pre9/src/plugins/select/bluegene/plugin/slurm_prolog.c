/*****************************************************************************\
 *  slurm_ prolog.c - Wait until the specified partition is ready and owned by 
 *	this user. This is executed via SLURM to synchronize the user's job 
 *	execution with slurmctld configuration of partitions.
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

#include "src/api/job_info.h"
#include "src/common/hostlist.h"
#include "src/common/node_select.h"
#include "src/api/node_select_info.h"
#include "src/plugins/select/bluegene/plugin/bg_boot_time.h"

#define _DEBUG 0
#define POLL_SLEEP 3			/* retry interval in seconds  */

int max_delay = BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT;
int cur_delay = 0; 
  
enum rm_partition_state {RM_PARTITION_FREE, 
			 RM_PARTITION_CONFIGURING,
			 RM_PARTITION_READY,
			 RM_PARTITION_BUSY,
			 RM_PARTITION_DEALLOCATING,
			 RM_PARTITION_ERROR,
			 RM_PARTITION_NAV};

static int  _get_job_size(uint32_t job_id);
static int  _wait_part_ready(uint32_t job_id);
static int  _partitions_dealloc();

int main(int argc, char *argv[])
{
	char *job_id_char = NULL;
	uint32_t job_id;

	job_id_char = getenv("SLURM_JOBID");		/* get SLURM job ID */
	if (!job_id_char) {
		fprintf(stderr, "SLURM_JOBID not set\n");
		exit(1);				/* abort job */
	}

	job_id = (uint32_t) atol(job_id_char);
	if (job_id == 0) {
		fprintf(stderr, "SLURM_JOBID invalid: %s\n", job_id_char);
		exit(1);				/* abort job */
	}

	if (_wait_part_ready(job_id) == 1)
		exit(0);				/* Success */

	exit(1);					/* abort job */
}

/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _wait_part_ready(uint32_t job_id)
{
	int is_ready = 0, i, rc;
	
	max_delay = BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT +
		   (BG_INCR_BLOCK_BOOT * _get_job_size(job_id));

#if _DEBUG
	printf("Waiting for job %u to become ready.", job_id);
#endif

	for (i=0; (cur_delay < max_delay); i++) {
		if (i) {
			sleep(POLL_SLEEP);
			rc = _partitions_dealloc();
			if ((rc == 0) || (rc == -1)) 
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
		if ((rc & READY_JOB_STATE) == 0) {	/* job killed */
			/* return 1 so we don't get a prolog error */
			is_ready = 1;
			break;
		}
		if (rc & READY_NODE_STATE) {		/* job and node ready */
			is_ready = 1;
			break;
		}
	}

#if _DEBUG
	if (is_ready == 0)
		printf("\n");
	else
     		printf("\nJob %u is ready.\n", job_id);
#endif
	if (is_ready == 0)
		fprintf(stderr, "Job %u is not ready.\n", job_id);
	return is_ready;
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

/*
 * Test if any BG blocks are in deallocating state since they are
 * probably related to this job we will want to sleep longer
 * RET	1:  deallocate in progress
 *	0:  no deallocate in progress
 *     -1: error occurred
 */
static int _partitions_dealloc()
{
	static node_select_info_msg_t *bg_info_ptr = NULL, *new_bg_ptr = NULL;
	int rc = 0, error_code = 0, i;
	
	if (bg_info_ptr) {
		error_code = slurm_load_node_select(bg_info_ptr->last_update, 
						   &new_bg_ptr);
		if (error_code == SLURM_SUCCESS)
			select_g_free_node_info(&bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, &new_bg_ptr);
	}

	if (error_code) {
		fprintf(stderr, "slurm_load_partitions: %s\n",
		       slurm_strerror(slurm_get_errno()));
		return -1;
	}
	for (i=0; i<new_bg_ptr->record_count; i++) {
		if(new_bg_ptr->bg_info_array[i].state 
		   == RM_PARTITION_DEALLOCATING) {
			rc = 1;
			break;
		}
	}
	bg_info_ptr = new_bg_ptr;
	return rc;
}
