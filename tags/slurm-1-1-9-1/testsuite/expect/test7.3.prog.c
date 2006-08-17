/*****************************************************************************\
 *  test7.3.prog.c - Test of slurm_spawn API (needed on IBM SP systems).
 *  
 *  Usage: test7.3.prog [min_nodes] [max_nodes] [tasks]
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#define TASKS_PER_NODE 1	/* Can't have more with current spawn RPC */

static int *_build_socket_array(int tasks);
static void _do_task_work(int *fd_array, int tasks);

int main (int argc, char *argv[])
{
	int i, min_nodes = 1, max_nodes = 1, nodes, tasks = 0, rc = 0;
	job_desc_msg_t job_req;
	resource_allocation_response_msg_t *job_resp;
	job_step_create_request_msg_t step_req;
	old_job_alloc_msg_t old_alloc;
	slurm_step_ctx ctx = NULL;
	char *task_argv[3];
	char cwd[128];
	int *fd_array = NULL;

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
	job_req.min_nodes = min_nodes;
	job_req.max_nodes = max_nodes;
	job_req.user_id   = getuid();
	job_req.group_id  = getgid();
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
		old_alloc.job_id = job_resp->job_id;
		while ((job_resp->node_list == NULL) ||
		       (strlen(job_resp->node_list) == 0)) {
			sleep(5);
			if (slurm_confirm_allocation(&old_alloc, &job_resp) &&
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

	/* Set up step configuration */
	bzero(&step_req, sizeof(job_step_create_request_msg_t ));
	step_req.job_id = job_resp->job_id;
	step_req.user_id = getuid();
	step_req.node_count = nodes;
	step_req.num_tasks = tasks;

	ctx = slurm_step_ctx_create(&step_req);
	if (ctx == NULL) {
		slurm_perror("slurm_step_ctx_create");
		rc = 1;
		goto done;
	}

	task_argv[0] = "./test7.3.io";
	if (slurm_step_ctx_set(ctx, SLURM_STEP_CTX_ARGS,
	                   1, task_argv))
		slurm_perror("slurm_step_ctx_create");
	getcwd(cwd, sizeof(cwd));
	if (slurm_step_ctx_set(ctx, SLURM_STEP_CTX_CHDIR, cwd))
		slurm_perror("slurm_step_ctx_create");
	fd_array = _build_socket_array(tasks);

	/* Spawn the tasks */
	if (slurm_spawn(ctx, fd_array)) {
		slurm_perror("slurm_spawn");
		slurm_kill_job(job_resp->job_id, SIGKILL, 0);
		rc = 1;
		goto done;
	}

	/* Interact with spawned tasks as desired */
	_do_task_work(fd_array, tasks);

	if (slurm_spawn_kill(ctx, SIGKILL))
		slurm_perror("slurm_spawn_kill");

	/* Terminate the job killing all tasks */
done:	slurm_kill_job(job_resp->job_id, SIGKILL, 0);

	/* clean up storage */
	slurm_free_resource_allocation_response_msg(job_resp);
	if (ctx)
		slurm_step_ctx_destroy(ctx);
	if (fd_array) {
		for (i=0; i<tasks; i++)
			(void) close(fd_array[i]);
		free(fd_array);
	}
	exit(0);
}


static int *_build_socket_array(int tasks)
{
	int *fd_array;
	int i, val;

	fd_array = malloc(tasks * sizeof(int));
	if (fd_array == NULL) {
		fprintf(stderr, "malloc error\n");
		exit(0);
	}

	for (i=0; i<tasks; i++) {
		if ((fd_array[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
			fprintf(stderr, "malloc error\n");
			exit(0);
		}
		setsockopt(fd_array[i], SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	}
	
	return fd_array;
}

static void _do_task_work(int *fd_array, int tasks)
{
	int i, j, size, fd;
	char buf[1024];
	struct sockaddr sock_addr;
	socklen_t sock_len = sizeof(sock_addr);

	for (i=0; i<tasks; i++) {
		fd = accept(fd_array[i], &sock_addr, &sock_len);
		if (fd < 0) {
			perror("accept");
			continue;
		}

		sprintf(buf, "test message");
		write(fd, buf, strlen(buf));

		while (1) {
			size = read(fd, buf, sizeof(buf));
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
		close(fd);
		close(fd_array[i]);
	}

	return;
}
