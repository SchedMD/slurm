#include <unistd.h>
#include <errno.h>

#include <src/common/slurm_errno.h>
#include <src/common/log.h>
#include <src/slurmd/pipes.h>
#include <src/slurmd/io.h>
void setup_parent_pipes ( int * pipes )
{
	close ( pipes[CHILD_IN_RD] ) ;
	close ( pipes[CHILD_OUT_WR] ) ;
	close ( pipes[CHILD_ERR_WR] ) ;
}

void cleanup_parent_pipes ( int * pipes )
{
	close ( pipes[CHILD_IN_WR] ) ;
	close ( pipes[CHILD_OUT_RD] ) ;
	close ( pipes[CHILD_ERR_RD] ) ;
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
	int error_code = SLURM_SUCCESS ;
	int local_errno;

	/*dup stdin*/
	//close ( STDIN_FILENO );
	if ( SLURM_ERROR == ( error_code |= dup2 ( pipes[CHILD_IN_RD] , STDIN_FILENO ) ) ) 
	{
		local_errno = errno ;
		error ("dup failed on child standard in pipe, %m errno %i" , local_errno );
		//return error_code ;
	}
	close ( pipes[CHILD_IN_RD] );
	close ( pipes[CHILD_IN_WR] );

	/*dup stdout*/
	//close ( STDOUT_FILENO );
	if ( SLURM_ERROR == ( error_code |= dup2 ( pipes[CHILD_OUT_WR] , STDOUT_FILENO ) ) ) 
	{
		local_errno = errno ;
		error ("dup failed on child standard out pipe, %m errno %i" , local_errno );
		//return error_code ;
	}
	close ( pipes[CHILD_OUT_RD] );
	close ( pipes[CHILD_OUT_WR] );

	/*dup stderr*/
	//close ( STDERR_FILENO );
	if ( SLURM_ERROR == ( error_code |= dup2 ( pipes[CHILD_ERR_WR] , STDERR_FILENO ) ) ) 
	{
		local_errno = errno ;
		error ("dup failed on child standard err pipe, %m errno %i" , local_errno );
		//return error_code ;
	}
	close ( pipes[CHILD_ERR_RD] );
	close ( pipes[CHILD_ERR_WR] );
	return error_code ;
}
