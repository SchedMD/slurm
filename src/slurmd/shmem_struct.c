#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <string.h>

#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/shmem_struct.h>


#define OCTAL_RW_PERMISSIONS 0666

/* function prototypes */
static void clear_task ( task_t * task );
static void clear_job_step( job_step_t * job_step );
static int prepend_task ( slurmd_shmem_t * shmem , job_step_t * job_step , task_t * task ) ;

/* gets a pointer to the slurmd shared memory segment
 * if it doesn't exist, one is created 
 * returns - a void * pointer to the shared memory segment
 */
void * get_shmem ( )
{
	int shmem_id ;
	void * shmem_addr ;
	int key ;
	key = ftok ( ".", 'a' );
	assert ( key != SLURM_ERROR );
	shmem_id = shmget ( key , sizeof ( slurmd_shmem_t ) , IPC_CREAT | OCTAL_RW_PERMISSIONS ); 
	assert ( shmem_id != SLURM_ERROR ) ;
	shmem_addr = shmat ( shmem_id , NULL , 0 ) ;
	assert ( shmem_addr != (void * ) SLURM_ERROR ) ;
	return shmem_addr ;
}

int rel_shmem ( void * shmem_addr ) 
{
	return shmdt( shmem_addr ) ;
}

/* initializes the shared memory segment, this should only be called once by the master slurmd 
 * after the initial get_shmem call
 * shmem - pointer to the shared memory segment returned by get_shmem ( )
 */
void init_shmem ( slurmd_shmem_t * shmem )
{
	int i ;
	/* set everthing to zero */
	memset ( shmem , 0 , sizeof ( slurmd_shmem_t ) );
	/* sanity check */
	/* set all task objects to unused */

	for ( i=0 ; i < MAX_TASKS ; i ++ )
	{
		clear_task ( & shmem->tasks[i] ) ;
	}
	
	/* set all job_step objects to unused */
	for ( i=0 ; i < MAX_JOB_STEPS ; i ++ )
	{
		clear_job_step ( & shmem->job_steps[i] ) ;
	}
	pthread_mutex_init ( & shmem -> mutex , NULL ) ;
}

/* runs through the job_step array looking for a unused job_step.
 * upon finding one the passed src job_step is copied into the shared mem job_step array
 * shmem - pointer to the shared memory segment returned by get_shmem ( )
 * job_step_t - src job_step to be added to the shared memory list
 * returns - the address of the assigned job_step in the shared mem job_step array or
 * the function dies on a fatal log call if the array is full
 */

void * alloc_job_step ( slurmd_shmem_t * shmem , int job_id , int job_step_id ) 
{
	int i ;
	pthread_mutex_lock ( & shmem -> mutex ) ;
	for ( i=0 ; i < MAX_JOB_STEPS ; i ++ )
        {
		if (shmem -> job_steps[i].used == false )
		{
			clear_job_step ( & shmem -> job_steps[i] ) ;
			shmem -> job_steps[i].used = true ;
			shmem -> job_steps[i].job_id=job_id;
			shmem -> job_steps[i].job_step_id=job_step_id;
			pthread_mutex_unlock ( & shmem -> mutex ) ;
			return & shmem -> job_steps[i] ;
		} 
        }
	pthread_mutex_unlock ( & shmem -> mutex ) ;
	fatal ( "No available job_step slots in shmem segment");
	return (void * ) SLURM_ERROR ;
}

/* runs through the task array looking for a unused task.
 * upon finding one the passed src task is copied into the shared mem task array
 * shmem - pointer to the shared memory segment returned by get_shmem ( )
 * new_task - src task to be added to the shared memory list
 * returns - the address of the assigned task in the shared mem task array
 * the function dies on a fatal log call if the array is full
 */

void * alloc_task ( slurmd_shmem_t * shmem , job_step_t * job_step ) 
{
	int i ;
	pthread_mutex_lock ( & shmem -> mutex ) ;
	for ( i=0 ; i < MAX_TASKS ; i ++ )
        {
		if (shmem -> tasks[i].used == false )
		{
			clear_task ( & shmem -> tasks[i] ) ;
			shmem -> tasks[i].used = true ;
			prepend_task ( shmem , job_step , & shmem -> tasks[i] ) ;
			pthread_mutex_unlock ( & shmem -> mutex ) ;
			return & shmem -> tasks[i] ;
		} 
	}
	pthread_mutex_unlock ( & shmem -> mutex ) ;
	fatal ( "No available task slots in shmem segment");
	return (void * ) SLURM_ERROR ;
}


/* prepends a new task onto the front of a list of tasks assocuated with a job_step.
 * it calls add_task which copies the passed task into a task array in shared memoery
 * sets pointers from the task to the corresponding job_step array 
 * note if the task array is full,  the add_task function will assert and exiti
 * shmem - pointer to the shared memory segment returned by get_shmem ( )
 * job_step - job_step to receive the new task
 * task - task to be prepended
 */

static int prepend_task ( slurmd_shmem_t * shmem , job_step_t * job_step , task_t * task )
{
	/* prepend operation*/
	/* newtask next pointer gets head of the jobstep task list */
	task -> next = job_step -> head_task ;
	/* newtask pointer becomes the new head of the jobstep task list */
	job_step -> head_task = task ;
	/* set back pointer from task to job_step */
	task -> job_step = job_step ;
	return SLURM_SUCCESS ;
}

/* clears a job_step and associated task list for future use */
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

/* clears a job_step array memeber for future use */
static void clear_task ( task_t * task )
{
	task -> used = false ;
	task -> job_step = NULL ;
	task -> next = NULL ;
}

/* clears a job_step array memeber for future use */
static void clear_job_step( job_step_t * job_step )
{
	job_step -> used = false ;
	job_step -> head_task = NULL ;
}

/* api call for DPCS to return a job_id given a session_id */
int find_job_id_for_session ( slurmd_shmem_t * shmem , int session_id )
{
	int i ;
	pthread_mutex_lock ( & shmem -> mutex ) ;
	for ( i=0 ; i < MAX_JOB_STEPS ; i ++ )
        {
		if (shmem -> job_steps[i].used == true )
		{
			if (shmem -> job_steps[i].session_id == session_id )

			pthread_mutex_unlock ( & shmem -> mutex ) ;
			return shmem -> job_steps[i].job_id ;
		} 
        }
	pthread_mutex_unlock ( & shmem -> mutex ) ;
	info ( "No job_id found for session_id %i", session_id );
	return SLURM_FAILURE ; 
}
