#ifndef _SHMEM_STRUCT_H
#define _SHMEM_STRUCT_H

#include <src/slurmd/task_mgr.h>

#define MAX_TASKS 128
#define MAX_JOB_STEPS 128

typedef struct job_step job_step_t ;
typedef struct task task_t ;
struct task
{
	uint32_t	task_id;
	uint32_t	uid;
	uint32_t	gid;
	task_start_t 	task_start;
	char 		used;
	job_step_t *	job_step;
	task_t * 	next;
} ;

struct job_step
{
	uint32_t	job_id;
	uint32_t	job_step_id;
	char 		used;
	task_t *	head_task;
} ;

typedef struct slurmd_shmem
{
	pthread_mutex_t mutex;
	task_t tasks[MAX_TASKS];
	job_step_t job_steps[MAX_JOB_STEPS];
} slurmd_shmem_t ;

void * get_shmem ( );
void init_shmem ( slurmd_shmem_t * shmem );
int rel_shmem ( void * shmem_addr );

void * alloc_job_step ( slurmd_shmem_t * shmem , int job_id , int job_step_id ) ;
void * alloc_task ( slurmd_shmem_t * shmem , job_step_t * job_step ) ;

#endif
