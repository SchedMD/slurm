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

/*
global variables 
*/
static List task_list ;

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

void task_mgr_init ( ) 
{
	task_list = list_create ( slurm_free_task ) ;
}

int launch_tasks ( launch_tasks_msg_t * launch_msg )
{
#ifdef ELAN
#else
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

	if ( i == launch_msg->tasks_to_launch ) /*parent*/
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
	else /*child*/
	{
		iowatch_launch ( launch_msg ) ;
	}
#endif
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
	task -> job_id = launch_msg -> job_id; 
	task -> job_step_id = launch_msg -> job_step_id;
	task -> uid = launch_msg -> uid;
	task -> gid = launch_msg -> gid;
	
	list_append ( task_list , task ) ;
	return SLURM_SUCCESS ;
}

int kill_tasks ( kill_tasks_msg_t * kill_task_msg )
{
	int error_code ;
	task_t key ;
	task_t * curr_task ;
	ListIterator iterator ;
	iterator = list_iterator_create ( task_list ) ;

	key . job_id = kill_task_msg -> job_id ;
	key . job_id = kill_task_msg -> job_step_id ;

	while ( ( curr_task = list_find ( iterator , match_job_id_job_step_id , & key ) ) )
	{
		if ( kill_task ( curr_task ) )
		{	
			error_code = ESLURMD_KILL_TASK_FAILED ;
		}
		list_delete ( iterator ) ;
	}	

	list_iterator_destroy ( iterator ) ;
	return error_code ;
}


int kill_task ( task_t * task )
{
	return kill ( task -> pid , SIGKILL ) ;
}

int match_job_id_job_step_id ( void * _x, void * _key )
{
	task_t * x = ( task_t * ) _x ;
	task_t * key = ( task_t * ) _key ;

	if ( x->job_id == key->job_id && x->job_step_id == key->job_step_id )
	{
		return true ;
	}
	else
	{
		return false ;
	}
}

void slurm_free_task ( void * _task )
{
	task_t * task = ( task_t * ) _task ;
	if ( task ) 
		free ( task ) ;
}
