#ifndef _SHMEM_STRUCT_H
#define _SHMEM_STRUCT_H 

typedef struct slurmd_shmem
{
	pthread_mutex mutex;
	task_desc tasks[128];
} slurmd_shmem_t ;
#endif
