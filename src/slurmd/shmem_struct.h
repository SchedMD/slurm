#ifndef _SHMEM_STRUCT_H
#define _SHMEM_STRUCT_H

#include <src/slurmd/task_mgr.h>

#define MAX_TASKS 128
#define MAX_JOB_STEPS 128

typedef struct job_step job_step_t ;
typedef struct task task_t ;
/* represents a task running on a node */
struct task
{
	uint32_t	task_id;	/* srun assigned globally unique taskid */
	task_start_t 	task_start;	/* task_start_message see task_mgr.h */
	char 		used;		/* boolean type that is marked when this record is used */
	job_step_t *	job_step;	/* reverse pointer to the controlling job_step */
	task_t * 	next;		/* next task pointer in the job_step */
} ;
/* represents a job_step consisting of a list of tasks */
struct job_step
{
	uint32_t	job_id;		/* slurmctrld assigned jobid */
	uint32_t	job_step_id;	/* slurmctrld assigned job_step id */
	uint32_t	session_id; 
	char 		used;		/* boolean type that is marked when this record is used */
	task_t *	head_task;	/* fist task in the job_step */
} ;

/* shared memory structure.  This structure is overlayed on top of the allocated shared ram */
typedef struct slurmd_shmem
{
	pthread_mutex_t mutex;			/* mutex to protect shared ram */
	task_t tasks[MAX_TASKS];		/* array of task objects */
	job_step_t job_steps[MAX_JOB_STEPS];	/* array of job_step objects */
} slurmd_shmem_t ;

/* gets shared memory segment, allocating it if needed */
void * get_shmem ( );

/* should only be called once after allocation of shared ram
 * Marks all task and job_step objects as unused */
void init_shmem ( slurmd_shmem_t * shmem );

/* detaches from shared ram and deallocates shared ram if no other
 * attachments exist */
int rel_shmem ( void * shmem_addr );

/* allocates job step from shared memory array */
void * alloc_job_step ( slurmd_shmem_t * shmem , int job_id , int job_step_id ) ;
/* allocates task from shared memory array */
void * alloc_task ( slurmd_shmem_t * shmem , job_step_t * job_step ) ;
/* api call for DPCS to return a job_id given a session_id */
int find_job_id_for_session ( slurmd_shmem_t * shmem , int session_id ) ;
/* clears a job_step and associated task list for future use */
int deallocate_job_step ( job_step_t * jobstep ) ;
/* find a particular job_step */
void * find_job_step ( slurmd_shmem_t * shmem , int job_id , int job_step_id ) ;
#endif
