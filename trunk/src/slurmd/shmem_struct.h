#ifndef _SHMEM_STRUCT_H
#define _SHMEM_STRUCT_H 

#define MAX_TASKS 128
#define MAX_JOB_STEPS 128

typedef struct job_step job_step_t ;
typedef struct task task_t ;
struct task
{
	pthread_t	threadid;
	uint32_t	pid;
	uint32_t	task_id;
	uint32_t	uid;
	uint32_t	gid;
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
void * add_job_step ( slurmd_shmem_t * shmem , job_step_t * new_job_step ) ;
void * add_task ( slurmd_shmem_t * shmem , task_t * new_task );
void copy_task ( task_t * dest , task_t * const src );
void copy_job_step ( job_step_t * dest , job_step_t * src );
int prepend_task ( slurmd_shmem_t * shmem , job_step_t * job_step , task_t * task );
int deallocate_job_step ( job_step_t * jobstep );
void clear_task ( task_t * task );
void clear_job_step( job_step_t * job_step );
#endif
