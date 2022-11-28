/*****************************************************************************\
 *  test7.12.prog.c - Test of slurm_job_step_stat() API call.
 *****************************************************************************
 *  Portions Copyright (C) 2014 SchedMD LLC
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@schedmd.com>
 *  CODE-OCEC-09-009. All rights reserved.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

int main(int argc, char **argv)
{
	int i, rc = 0;
	job_step_stat_response_msg_t *resp = NULL;
	job_step_stat_t *step_stat = NULL;
	ListIterator itr;
	job_info_msg_t *job_info_msg;
	slurm_job_info_t *job_ptr;
	slurm_step_id_t step_id;

	if (argc < 3) {
		printf("Usage: job_id step_id\n");
		exit(1);
	}
	step_id.job_id = atoi(argv[1]);
	step_id.step_id = atoi(argv[2]);
	step_id.step_het_comp = NO_VAL;
	printf("job_id:%u step_id:%u\n", step_id.job_id, step_id.step_id);

	slurm_init(NULL);

	rc = slurm_job_step_stat(&step_id, NULL, NO_VAL16, &resp);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("slurm_job_step_stat");
		exit(1);
	}

	itr = slurm_list_iterator_create(resp->stats_list);
	while ((step_stat = slurm_list_next(itr))) {
		for (i = 0; i < step_stat->step_pids->pid_cnt; i++)
			printf("pid:%u\n", step_stat->step_pids->pid[i]);
	}
	slurm_list_iterator_destroy(itr);
	slurm_job_step_pids_response_msg_free(resp);

	rc = slurm_load_job(&job_info_msg, step_id.job_id, SHOW_ALL);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("slurm_load_job");
		exit(1);
	}
	for (i = 0; i < job_info_msg->record_count; i++) {
		job_ptr = job_info_msg->job_array + i;
		printf("job_id:%u name:%s user_id:%u\n",
		       job_ptr->job_id, job_ptr->name, job_ptr->user_id);
	}
	slurm_free_job_info_msg(job_info_msg);

	exit(0);
}
