#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <src/common/log.h>
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

#define STDIN_FILENO 0
#define STDOUT_FILENO 1 
#define STDERR_FILENO 2
#define MAX_TASKS_PER_LAUNCH 64
#define CHILD_IN 0
#define CHILD_IN_RD 0
#define CHILD_IN_WR 1
#define CHILD_OUT 2
#define CHILD_OUT_RD 2
#define CHILD_OUT_WR 3
#define CHILD_ERR 4
#define CHILD_ERR_RD 4
#define CHILD_ERR_WR 5

/* prototypes */
void slurm_free_task ( void * _task ) ;
void * iowatch_launch_thread ( void * arg ) ;
int kill_task ( task_t * task ) ;
int interconnect_init ( launch_tasks_msg_t * launch_msg );
int fan_out_task_launch ( launch_tasks_msg_t * launch_msg );
void * task_exec_thread ( void * arg ) ;
int init_parent_pipes ( int * pipes ) ;
void setup_parent_pipes ( int * pipes ) ; 
int setup_child_pipes ( int * pipes ) ;
int forward_io ( task_start_t * task_arg ) ;
void * stdout_io_pipe_thread ( void * arg ) ;
/******************************************************************
 *task launch method call hierarchy
 *
 *launch_tasks()
 *	interconnect_init()
 *		fan_out_task_launch() (pthread_create)
 *			iowatch_launch_thread()
 *				task_exec_thread() (pthread_create)
 ******************************************************************/			

/* exported module funtion to launch tasks */
int launch_tasks ( launch_tasks_msg_t * launch_msg )
{
	return interconnect_init ( launch_msg );
}

/* Contains interconnect specific setup instructions and then calls 
 * fan_out_task_launch */
int interconnect_init ( launch_tasks_msg_t * launch_msg )
{
	return fan_out_task_launch ( launch_msg ) ;
}

int fan_out_task_launch ( launch_tasks_msg_t * launch_msg )
{
	int i ;
	int rc ;
	task_start_t task_start[MAX_TASKS_PER_LAUNCH] ;
	/*place on the stack so we don't have to worry about mem clean up should
	 *this task_launch get brutally killed by a kill_job step message */
	//task_start = xmalloc ( sizeof ( task_start[MAX_TASKS_PER_LAUNCH] ) );

	/* launch requested number of threads */
	for ( i = 0 ; i < launch_msg->tasks_to_launch ; i ++ )
	{
		task_start[i].launch_msg = launch_msg ;
		task_start[i].slurmd_fanout_id = i ;
		rc = pthread_create ( & task_start[i].pthread_id , NULL , iowatch_launch_thread , ( void * ) & task_start[i] ) ;
	}
	
	/* wait for all the launched threads to finish */
	for ( i = 0 ; i < launch_msg->tasks_to_launch ; i ++ )
	{
		rc = pthread_join( task_start[i].pthread_id , & task_start[i].thread_return ) ;
	}

	//xfree ( tast_start )
	return SLURM_SUCCESS ;
}


void * iowatch_launch_thread ( void * arg ) 
{
	task_start_t * task_start = ( task_start_t * ) arg ;
	int pipes[6] ;
	/*
	int * childin = &newstdin[1] ;
	int * childout = &newstdout[0] ;
	int * childerr = &newstderr[0] ;
	*/
	/* init arg to be passed to task launch thread */
	task_start->pipes = pipes ;

	/* create pipes to read child stdin, stdout, sterr */
	init_parent_pipes ( pipes ) ;

	/* create task thread */
	pthread_create ( & task_start->exec_pthread_id , NULL , task_exec_thread , ( void * ) arg ) ;

	/* pipe output from child task to srun over sockets */
	setup_parent_pipes ( pipes ) ;
	//forward_io ( arg ) ;

	/* wait for thread to die */
	pthread_join ( task_start->exec_pthread_id , & task_start->exec_thread_return ) ;

	return ( void * ) SLURM_SUCCESS ;
}

int forward_io ( task_start_t * task_arg ) 
{

	pthread_create ( & task_arg->io_pthread_id[STDOUT_FILENO] , NULL , stdout_io_pipe_thread ,  task_arg ) ;

	return SLURM_SUCCESS ;
}

void * stdout_io_pipe_thread ( void * arg )
{
	slurm_fd connection_fd ;
	slurm_addr dest_addr ;
	task_start_t * io_arg = ( task_start_t * ) arg ;
	char buffer[SLURMD_IO_MAX_BUFFER_SIZE] ;
	int bytes_read ;
	int sock_bytes_written ;
	
	/* dest_addr = somethiong from arg */

	if ( ( connection_fd = slurm_open_stream ( & dest_addr ) ) == SLURM_PROTOCOL_ERROR )
	{
		info ( "error opening socket to srun to pipe stdout" ) ;
		pthread_exit ( 0 ) ;
	}
	
	while ( true )
	{
		if ( ( bytes_read = read ( io_arg->pipes[CHILD_OUT_RD] , buffer , SLURMD_IO_MAX_BUFFER_SIZE ) ) <= 0 )
		{
			info ( "error reading stdout stream for task %i", 1 ) ;
			pthread_exit ( 0 ) ;
		}
		if ( ( sock_bytes_written = slurm_write_stream ( connection_fd , buffer , bytes_read ) ) == SLURM_PROTOCOL_ERROR )

		{
			info ( "error sending stdout stream for task %i", 1 ) ;
			pthread_exit ( 0 ) ; 
		}
	}
}

/*
int forward_io ( task_thread_arg_t * task_arg ) 
{
	slurm_addr in_out_addr = task_arg->launch_msg->
		slurm_addr sig_err_addr = task_arg->launch_msg->
	slurm_fd in_out ; 
	slurm_fd sig_err ; 
	int * pipes = task_arg-> pipes ;

	fd_set n_rd_set ;
	fd_set n_wr_set ;
	fd_set n_err_set ;
	slurm_fd_set ns_rd_set ;
	slurm_fd_set ns_wr_set ;
	slurm_fd_set ns_err_set ;

	fd_set rd_set ;
	fd_set wr_set ;
	fd_set err_set ;
	slurm_fd_set s_rd_set ;
	slurm_fd_set s_wr_set ;
	slurm_fd_set s_err_set ;
	int s_rc ;
	int rc ;
	struct timeval timeout ;

	timeout . tv_sec = 0 ;
	timeout . tv_sec = SLURM_SELECT_TIMEOUT ;

	in_out = slurm_stream_connect ( & in_out_addr ) ;
	sig_err = slurm_stream_connect ( & sig_err_addr ) ;
	

	FD_ZERO ( n_rd_set ) ;
	FD_ZERO ( n_wd_set ) ;
	FD_ZERO ( n_err_set ) ;

	slurm_FD_ZERO ( ns_rd_set ) ;
	slurm_FD_ZERO ( ns_wr_set ) ;
	slurm_FD_ZERO ( ns_err_set ) ;

	slurm_FD_SET ( in_out, & ns_rd_set ) ;
	slurm_FD_SET ( in_out, & ns_err_set ) ;

	slurm_FD_SET ( ig_err & ns_rd_set ) ;
	slurm_FD_SET ( ig_err & ns_err_set ) ;

	FD_SET ( pipes[CHILD_OUT_RD], & n_rd_set ) ;
	FD_SET ( pipes[CHILD_ERR_RD], & n_rd_set ) ;

	FD_SET ( pipes[CHILD_IN_WR], & n_err_set ) ;
	FD_SET ( pipes[CHILD_OUT_RD], & n_err_set ) ;
	FD_SET ( pipes[CHILD_ERR_RD], & n_err_set ) ;


	while ( )
	{
		memcpy ( &rd_set , &n_rd_set , sizeof ( rd_set ) ) ;
		memcpy ( &wr_set , &n_wr_set , sizeof ( wr_set ) ) ;
		memcpy ( &err_set , &ns_err_set , sizeof ( err_set ) ) ;

		memcpy ( &s_rd_set , &ns_rd_set , sizeof ( s_rd_set ) ) ;
		memcpy ( &s_wr_set , &ns_wr_set , sizeof ( s_wr_set ) ) ;
		memcpy ( &s_err_set , &ns_err_set , sizeof ( s_err_set ) ) ;

		rc = select ( pipes[CHILD_ERR_RD] + 1  , rd_set , wr_set , err_set , & timeout ) ; 
		if ( rc > 0 )

		timeout . tv_sec = 0 ;
		timeout . tv_sec = SLURM_SELECT_TIMEOUT ;
		s_rc = slurm_select ( sig_err_addr + 1 , s_rd_set , s_wr_set , s_err_set , & timeout ) ;

		FD_ZERO ( n_rd_set ) ;
		FD_ZERO ( n_wd_set ) ;
		FD_ZERO ( n_err_set ) ;

		slurm_FD_ZERO ( ns_rd_set ) ;
		slurm_FD_ZERO ( ns_wr_set ) ;
		slurm_FD_ZERO ( ns_err_set ) ;

		slurm_FD_SET ( in_out, & ns_rd_set ) ;
		slurm_FD_SET ( ig_err & ns_rd_set ) ;

		slurm_FD_SET ( in_out, & ns_err_set ) ;
		slurm_FD_SET ( ig_err & ns_err_set ) ;

		FD_SET ( pipes[CHILD_OUT_RD], & n_rd_set ) ;
		FD_SET ( pipes[CHILD_ERR_RD], & n_rd_set ) ;

		FD_SET ( pipes[CHILD_IN_WR], & n_err_set ) ;
		FD_SET ( pipes[CHILD_OUT_RD], & n_err_set ) ;
		FD_SET ( pipes[CHILD_ERR_RD], & n_err_set ) ;
	}

}
*/
void * task_exec_thread ( void * arg )
{
	task_start_t * task_arg = ( task_start_t * ) arg ;
	launch_tasks_msg_t * launch_msg = task_arg -> launch_msg ;
	int * pipes = task_arg->pipes ;
	int rc ;

	setup_child_pipes ( pipes ) ;
	/* setuid and gid*/
	if ( ( rc = setuid ( launch_msg->uid ) ) == SLURM_ERROR )  ;

	if ( ( rc = setgid ( launch_msg->gid ) ) == SLURM_ERROR ) ;

	/* run bash and cmdline */
	chdir ( launch_msg->cwd ) ;
	execl ( "/bin/bash" , "bash" , "-c" , launch_msg->cmd_line );
	return ( void * ) SLURM_SUCCESS ;
}

void setup_parent_pipes ( int * pipes )
{
	close ( pipes[CHILD_IN_RD] ) ;
	close ( pipes[CHILD_OUT_WR] ) ;
	close ( pipes[CHILD_ERR_WR] ) ;
}

int init_parent_pipes ( int * pipes )
{
	int rc ;
	/* open pipes to be used in dup after fork */
	if( ( rc = pipe ( & pipes[CHILD_IN] ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}
	if( ( rc = pipe ( & pipes[CHILD_OUT] ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}
	if( ( rc = pipe ( & pipes[CHILD_ERR] ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}
	return SLURM_SUCCESS ;
}

int setup_child_pipes ( int * pipes )
{
	int error_code = 0 ;

	/*dup stdin*/
	close ( STDIN_FILENO );
	if ( SLURM_ERROR == ( error_code = dup ( CHILD_IN_RD ) ) ) info ("dup failed on child standard in pipe");
	close ( CHILD_IN_RD );
	close ( CHILD_IN_WR );

	/*dup stdout*/
	close ( STDOUT_FILENO );
	if ( SLURM_ERROR == ( error_code = dup ( CHILD_OUT_WR ) ) ) info ("dup failed on child standard out pipe");
	close ( CHILD_OUT_RD );
	close ( CHILD_OUT_WR );

	/*dup stderr*/
	close ( STDERR_FILENO );
	if ( SLURM_ERROR == ( error_code = dup ( CHILD_ERR_WR ) ) ) info ("dup failed on child standard err pipe");
	close ( CHILD_ERR_RD );
	close ( CHILD_ERR_WR );

	return error_code ;
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
