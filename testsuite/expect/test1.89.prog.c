#define _GNU_SOURCE
#define __USE_GNU
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gnu/libc-version.h>

static void _load_mask(cpu_set_t *mask)
{
	int rc, affinity_args = 3;
	int (*fptr_sched_getaffinity)() = sched_getaffinity;

#if defined __GLIBC__
	const char *glibc_vers = gnu_get_libc_version();
	if (glibc_vers != NULL) {
		int scnt = 0, major = 0, minor = 0, point = 0;
		scnt = sscanf (glibc_vers, "%d.%d.%d", &major,
			&minor, &point);
		if (scnt == 3) {
			if ((major <= 2) && (minor <= 3) && (point <= 2)) {
				affinity_args = 2;
			}
		}
	}
#endif
	if (affinity_args == 3) {
		rc = (*fptr_sched_getaffinity)((pid_t) 0, 
			(unsigned int) sizeof(cpu_set_t), mask);
	} else {
		rc = (*fptr_sched_getaffinity)((pid_t) 0, mask);
	}
	if (rc != 0) {
		fprintf(stderr, "ERROR: sched_getaffinity: %s\n",
			strerror(errno));
		exit(1);
	}
}

static int _mask_to_int(cpu_set_t *mask)
{
	int i, rc = 0;
	for (i=0; i<CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, mask))
			rc += (1 << i);
	}
	return rc;
}
	
	
main (int argc, char **argv)
{
	char *task_str;
	cpu_set_t mask;
	int task_id;

	_load_mask(&mask);
	if ((task_str = getenv("SLURM_PROCID")) == NULL) {
		fprintf(stderr, "ERROR: getenv(SLURM_TASKID) failed\n");
		exit(1);
	}
	task_id = atoi(task_str);
	printf("TASK_ID:%d,MASK:%u\n", task_id, _mask_to_int(&mask));
	exit(0);
}
