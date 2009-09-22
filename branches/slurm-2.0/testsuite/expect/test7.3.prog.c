/*****************************************************************************\
 *  test7.3.prog.c - Test of "user managed" IO with the slurm_step_launch()
 *                   API function (required for "poe" launch on IBM
 *                   AIX systems).
 *  
 *  Usage: test7.3.prog [min_nodes] [max_nodes] [tasks]
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#define TASKS_PER_NODE 1

static void _do_task_work(int *fd_array, int tasks);

int main (int argc, char *argv[])
{
	int i, min_nodes = 1, max_nodes = 1, nodes, tasks = 0, rc = 0;
	job_desc_msg_t job_req;
	resource_allocation_response_msg_t *job_resp;
	slurm_step_ctx_params_t step_params[1];
	slurm_step_ctx ctx = NULL;
	slurm_step_launch_params_t launch[1];
	char *task_argv[3];
	int *fd_array = NULL;
	int num_fd;

	if (argc > 1) {
		i = atoi(argv[1]);
		if (i > 0)
			min_nodes = i;
	}
	if (argc > 2) {
		i = atoi(argv[2]);
		if (i > 0)
			max_nodes = i;
	}
	if (max_nodes < min_nodes)
		max_nodes = min_nodes;

	/* Create a job allocation */
	slurm_init_job_desc_msg( &job_req );
	job_req.min_nodes  = min_nodes;
	job_req.max_nodes  = max_nodes;
	job_req.user_id    = getuid();
	job_req.group_id   = getgid();
	job_req.time_limit = 1;
	if (slurm_allocate_resources(&job_req, &job_resp)) {
		slurm_perror ("slurm_allocate_resources");
		exit(0);
	}
	printf("job_id %u\n", job_resp->job_id);
	fflush(stdout);

	/* Wait for allocation request to be satisfied */
	if ((job_resp->node_list == NULL) ||
	    (strlen(job_resp->node_list) == 0)) {
		printf("Waiting for resource allocation\n");
		fflush(stdout);
		while ((job_resp->node_list == NULL) ||
		       (strlen(job_resp->node_list) == 0)) {
			sleep(5);
			if (slurm_allocation_lookup_lite(job_resp->job_id, 
							 &job_resp) &&
			    (slurm_get_errno() != ESLURM_JOB_PENDING)) {
				slurm_perror("slurm_confirm_allocation");
				exit(0);
			}
		}
	}
	nodes = job_resp->node_cnt;
	if (argc > 3)
		tasks = atoi(argv[3]);
	if (tasks < 1)
		tasks = nodes * TASKS_PER_NODE;
	if (tasks < nodes) {
		fprintf(stderr, "Invalid task count argument\n");
		exit(1);
	}
	printf("Starting %d tasks on %d nodes\n", tasks, nodes);
	fflush(stdout);

	/*
	 * Create a job step context.
	 */
	slurm_step_ctx_params_t_init(step_params);
	step_params->job_id = job_resp->job_id;
	step_params->node_count = nodes;
	step_params->task_count = tasks;

	ctx = slurm_step_ctx_create(step_params);
	if (ctx == NULL) {
		slurm_perror("slurm_step_ctx_create");
		rc = 1;
		goto done;
	}

	/*
	 * Hack to run one task per node, regardless of what we set up
	 * when we created the job step context.
	 */
	if (slurm_step_ctx_daemon_per_node_hack(ctx) != SLURM_SUCCESS) {
		slurm_perror("slurm_step_ctx_daemon_per_node_hack");
		rc = 1;
		goto done;
	}

	/*
	 * Launch the tasks using "user managed" IO.
	 * "user managed" IO means a TCP stream for each task, directly
         * connected to the stdin, stdout, and stderr the task.
	 */
	slurm_step_launch_params_t_init(launch);
	task_argv[0] = "./test7.3.io";
	launch->argv = task_argv;
	launch->argc = 1;
	launch->user_managed_io = true; /* This is the key to using
					  "user managed" IO */
	
	if (slurm_step_launch(ctx, launch, NULL) != SLURM_SUCCESS) {
		slurm_perror("slurm_step_launch");
		rc = 1;
		goto done;
	}

	if (slurm_step_launch_wait_start(ctx) != SLURM_SUCCESS) {
		slurm_perror("slurm_step_launch_wait_start");
		rc =1;
		goto done;
	}

	slurm_step_ctx_get(ctx, SLURM_STEP_CTX_USER_MANAGED_SOCKETS,
			   &num_fd, &fd_array);

	/* Interact with launched tasks as desired */
	_do_task_work(fd_array, tasks);

	for (i = 0; i < tasks; i++) {
		close(fd_array[i]);
	}

	slurm_step_launch_wait_finish(ctx);

	/* Terminate the job killing all tasks */
done:	slurm_kill_job(job_resp->job_id, SIGKILL, 0);

	/* clean up storage */
	slurm_free_resource_allocation_response_msg(job_resp);
	if (ctx)
		slurm_step_ctx_destroy(ctx);
	exit(0);
}

static void _do_task_work(int *fd_array, int tasks)
{
	int i, j, size;
	char buf[1024];

	for (i=0; i<tasks; i++) {
		if (fd_array[i] < 0) {
			perror("Invalid file descriptor");
			continue;
		}

		sprintf(buf, "test message");
		write(fd_array[i], buf, strlen(buf));

		while (1) {
			size = read(fd_array[i], buf, sizeof(buf));
			if (size > 0) {
				printf("task %d read:size:%d:msg:", i, size);
				for (j=0; j<size; j++)
					printf("%c",buf[j]);
				printf("\n");
				fflush(stdout);
			} else if (size == 0) {
				printf("task:%d:EOF\n", i);
				fflush(stdout);
				break;
			} else {
				perror("read");
				break;
			}
		}
	}

	return;
}
