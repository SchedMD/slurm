#include <unistd.h>
#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/task_mgr.h>

int main ( int argc , char ** argv )
{
	
	launch_tasks_request_msg_t launch_tasks_msg ;
	slurm_addr	io_pipe_addrs ;
	int		gids[1] ;
	char arg0[] = "./testme" ;
	char arg1[] = "" ;
	char * args[] = { arg0 , arg1 } ;
	
	gids[1] = 9999 ;
	
	slurm_set_addr_char ( & io_pipe_addrs , 7071 , "localhost" ) ;
	//kill_tasks_msg_t kill_tasks_msg ;
	launch_tasks_msg . job_id = 1000 ;
	launch_tasks_msg . job_step_id = 2000 ; 
	launch_tasks_msg . uid = 8207 ;
	launch_tasks_msg . credential = NULL ;
	launch_tasks_msg . tasks_to_launch = 1 ;
	launch_tasks_msg . envc = 0 ;
	launch_tasks_msg . env = NULL ;
	launch_tasks_msg . cwd = "." ;
	launch_tasks_msg . argc = 2 ;
	launch_tasks_msg . argv = args ;
	launch_tasks_msg . streams = io_pipe_addrs ; 
	launch_tasks_msg . global_task_ids = gids ;
	
	
	launch_tasks ( & launch_tasks_msg ) ;
	//kill_tasks ( & kill_tasks_msg ) ;
	return SLURM_SUCCESS ;
}
