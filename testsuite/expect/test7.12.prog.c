/*****************************************************************************\
 *  test7.12.prog.c - Test of slurm_job_step_stat() API call.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

main(int argc, char **argv)
{
	int i, rc = 0;
	uint32_t job_id=0, step_id=0;
	job_step_stat_response_msg_t *resp = NULL;
	job_step_stat_t *step_stat = NULL;
	ListIterator itr;

	if (argc < 2) {
		printf("Usage: job_id [step_id]\n");
		exit(1);
	}
	job_id = atoi(argv[1]);
	if (argc > 2)
		step_id = atoi(argv[2]);
	printf("job_id:%u step_id:%u\n", job_id, step_id);

	rc = slurm_job_step_stat(job_id, step_id, NULL, &resp);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("slurm_job_step_stat");
		exit(1);
	}

	itr = slurm_list_iterator_create(resp->stats_list);
	while ((step_stat = slurm_list_next(itr))) {
		for (i=0; i<step_stat->step_pids->pid_cnt; i++)
			printf("pid:%u\n", step_stat->step_pids->pid[i]);
	}
	slurm_list_iterator_destroy(itr);
	slurm_job_step_pids_response_msg_free(resp);
	exit(0);
}
