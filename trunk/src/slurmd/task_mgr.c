#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <src/common/list.h>
#include <src/common/xerrno.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_errno.h>
#include <src/slurmd/task_mgr.h>
#include <src/slurmd/shmem_struct.h>

/*
global variables 
*/

/* file descriptor defines */

#define STDIN 0
#define STDOUT 1 
#define STDERR 2
#define READFD 0
#define WRITEFD 1


/* prototypes */
void slurm_free_task ( void * _task ) ;
int iowatch_launch (  launch_tasks_msg_t * launch_msg ) ;
int match_job_id_job_step_id ( void * _x, void * _key ) ;
int append_task_to_list (  launch_tasks_msg_t * launch_msg , int pid ) ;
int kill_task ( task_t * task ) ;
int interconnect_init ( launch_tasks_msg_t * launch_msg );
int fan_out_task_launch ( launch_tasks_msg_t * launch_msg );

void task_mgr_init ( ) 
{
}

int launch_tasks ( launch_tasks_msg_t * launch_msg )
{
	return interconnect_init ( launch_msg );
}

/* Contains interconnect specific setup instructions and then calls fan_out_task_launch */
int interconnect_init ( launch_tasks_msg_t * launch_msg )
{
	return fan_out_task_launch ( launch_msg ) ;
}

int fan_out_task_launch ( launch_tasks_msg_t * launch_msg )
{
	int i ;
	int cpid[64] ;
	for ( i = 0 ; i < launch_msg->tasks_to_launch ; i ++ )
	{
		cpid[i] = fork ( ) ;
		if ( cpid[i] == 0 )
		{
			break ;
		}
	}
	
	/*parent*/
	if ( i == launch_msg->tasks_to_launch ) 
	{
		int waiting = i ;
		int j ;
		int pid ;
                while (waiting > 0) {
                        pid = waitpid(0, NULL, 0);
                        if (pid < 0) {
                                xperror("waitpid");
                                exit(1);
                        }
                        for (j = 0; j < launch_msg->tasks_to_launch ; j++) {
                                if (cpid[j] == pid)
                                        waiting--;
                        }
                }
	}
	/*child*/
	else
	{
		iowatch_launch ( launch_msg ) ;
	}
	return SLURM_SUCCESS ;
}

int iowatch_launch (  launch_tasks_msg_t * launch_msg )
{
	int rc ;
	int newstdin[2] ;		
	int newstdout[2] ;		
	int newstderr[2] ;		
	/*
	int * childin = &newstdin[1] ;
	int * childout = &newstdout[0] ;
	int * childerr = &newstderr[0] ;
	*/
	task_t * task ;
	int pid ;

	/* open pipes to be used in dup after fork */
	if( ( rc = pipe ( newstdin ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}
	if( ( rc = pipe ( newstdout ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}
	if( ( rc = pipe ( newstderr ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}

	switch ( ( pid = fork ( ) ) )
	{
		/*error*/
		case -1:
			return SLURM_ERROR ;
			break ;
		/*child*/
		case 0:
			/*dup stdin*/
			close ( STDIN );
			dup ( newstdin[READFD] ) ;
			close ( newstdin[READFD] );
			close ( newstdin[READFD] );

			/*dup stdout*/
			close ( STDOUT );
			dup ( newstdout[WRITEFD] ) ;
			close ( newstdout[READFD] );
			close ( newstdout[WRITEFD] );

			/*dup stderr*/
			close ( STDERR );
			dup ( newstderr[WRITEFD] ) ;
			close ( newstderr[READFD] );
			close ( newstderr[READFD] );
			
			/* setuid and gid*/
			if ( ( rc = setuid ( launch_msg->uid ) ) == SLURM_ERROR ) 
			
			if ( ( rc = setgid ( launch_msg->gid ) ) == SLURM_ERROR ) ;
			
			/* run bash and cmdline */
			chdir ( launch_msg->cwd ) ;
			execl ( "/bin/bash" , "bash" , "-c" , launch_msg->cmd_line );
			return SLURM_SUCCESS ;
			break ;
		/*parent*/
		default:
			
			task = xmalloc ( sizeof ( task_t ) ) ;
			append_task_to_list ( launch_msg , pid ) ;
			close ( newstdin[READFD] ) ;
			close ( newstdout[WRITEFD] ) ;
			close ( newstderr[WRITEFD] ) ;
			break ;
	}

	if ( pid != 0 ) /*parent*/
	{
		/* select and io copy from std streams to sockets */
	}
	return SLURM_SUCCESS ;
}


int append_task_to_list (  launch_tasks_msg_t * launch_msg , int pid )
{
	task_t * task ;
	task = ( task_t * ) xmalloc ( sizeof ( task_t ) ) ;
	if ( task == NULL )
		return ENOMEM ;

	task -> pid = pid;
	task -> uid = launch_msg -> uid;
	task -> gid = launch_msg -> gid;
	
	return SLURM_SUCCESS ;
}

int kill_tasks ( kill_tasks_msg_t * kill_task_msg )
{
	int error_code = SLURM_SUCCESS ;

	return error_code ;
}


int kill_task ( task_t * task )
{
	return kill ( task -> pid , SIGKILL ) ;
}
