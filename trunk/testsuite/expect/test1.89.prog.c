#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

main (int argc, char **argv)
{
	char *task_str;
	unsigned long mask;
	int task_id;

	if (sched_getaffinity((pid_t) 0, (unsigned int) sizeof(mask), &mask)
			!= 0) {
		fprintf(stderr, "ERROR: sched_getaffinity: %s\n", 
			strerror(errno));
		exit(1);
	}
	if ((task_str = getenv("SLURM_PROCID")) == NULL) {
		fprintf(stderr, "ERROR: getenv(SLURM_TASKID) failed\n");
		exit(1);
	}
	task_id = atoi(task_str);
	printf("TASK_ID:%d,MASK:%lu\n", task_id, mask);
	exit(0);
}
