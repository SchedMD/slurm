#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <string.h>

#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/shmem_struct.h>

#define SHMEM_KEY "slurm_shmem_key"
void * get_shmem ( )
{
	int shmem_id ;
	void * shmem_addr ;
	shmem_id = shmget ( 0 , sizeof ( slurmd_shmem_t ) , IPC_CREAT ); 
	assert ( shmem_id != SLURM_ERROR ) ;
	shmem_addr = shmat ( shmem_id , NULL , 0 ) ;
	assert ( shmem_addr != (void * ) SLURM_ERROR ) ;
	return shmem_addr ;
}

void init_shmem ( slurmd_shmem_t * shmem )
{
	int i ;
	memset ( shmem , 0 , sizeof ( slurmd_shmem_t ) );
	for ( i=0 ; i < MAX_TASKS ; i ++ )
	{
		clear_task ( & shmem->tasks[i] ) ;
/*
		shmem->tasks[i] . used = false ;
		shmem->tasks[i] . job_step = NULL ;
		shmem->tasks[i] . next = NULL ;
*/
	}
	for ( i=0 ; i < MAX_JOB_STEPS ; i ++ )
	{
		clear_job_step ( & shmem->job_steps[i] ) ;
/*
		shmem->job_steps[i] . used = false ;
		shmem->job_steps[i] . haed_task = NULL ;
*/
	}
}

void * add_job_step ( slurmd_shmem_t * shmem , job_step_t * new_job_step ) 
{
	int i ;
	for ( i=0 ; i < MAX_JOB_STEPS ; i ++ )
        {
		if (shmem -> job_steps[i].used == false )
		{
			shmem -> job_steps[i].used = true ;
			copy_job_step ( & shmem -> job_steps[i] , new_job_step );
			return & shmem -> job_steps[i] ;
		} 
        }
		fatal ( "No available job_step slots in shmem segment");
	return (void * ) SLURM_ERROR ;
}

void * add_task ( slurmd_shmem_t * shmem , task_t * new_task ) 
{
	int i ;
	for ( i=0 ; i < MAX_TASKS ; i ++ )
        {
		if (shmem -> tasks[i].used == false )
		{
			shmem -> tasks[i].used = true ;
			copy_task ( & shmem -> tasks[i] , new_task ) ;
			return & shmem -> tasks[i] ;
		} 
        }
		fatal ( "No available task slots in shmem segment");
	return (void * ) SLURM_ERROR ;
}

void copy_task ( task_t * dest , task_t * const src ) 
{
	dest -> threadid 	= src -> threadid;
	dest -> pid		= src -> pid;
	dest -> task_id		= src -> task_id;
	dest -> uid		= src -> uid;
	dest -> gid		= src -> gid;
}

void copy_job_step ( job_step_t * dest , job_step_t * src )
{
	dest -> job_id		= src -> job_id ;
	dest -> job_step_id	= src -> job_step_id ;
}

int prepend_task ( slurmd_shmem_t * shmem , job_step_t * job_step , task_t * task )
{
	task_t * new_task ;
	if ( ( new_task = add_task ( shmem , task ) ) == ( void * ) SLURM_ERROR )
	{
		fatal ( "No available task slots in shmem segment during prepend_task call ");
		return SLURM_ERROR ;
	}
	/* prepend operation*/
	/* newtask next pointer gets head of the jobstep task list */
	new_task -> next = job_step -> head_task ;
	/* newtask pointer becomes the new head of the jobstep task list */
	job_step -> head_task = new_task ;
	/* set back pointer from task to job_step */
	new_task -> job_step = job_step ;
	return SLURM_SUCCESS ;
}

int deallocate_job_step ( job_step_t * jobstep )
{
	task_t * task_ptr = jobstep -> head_task ;
	task_t * task_temp_ptr ; 
	while ( task_ptr != NULL )
	{
		task_temp_ptr = task_ptr -> next ;
		clear_task ( task_ptr ) ;
		task_ptr = task_temp_ptr ;
	}
	clear_job_step ( jobstep ) ;
	return SLURM_SUCCESS ;
}

void clear_task ( task_t * task )
{
	task -> used = false ;
	task -> job_step = NULL ;
	task -> next = NULL ;
}

void clear_job_step( job_step_t * job_step )
{
	job_step -> used = false ;
	job_step -> head_task = NULL ;
}
