#include <src/common/slurm_protocol_api.h>

int main ( int argc , char* argv[] )
{
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
        launch_tasks_msg_t launch_tasks_msg ;
	slurm_addr      io_pipe_addrs[2] ;
	slurm_addr      slurmd_addr ;
	int             gids[1] ;
	slurm_job_credential_t credential ;
	
	credential . node_list = "TESTING" ;
	gids[1] = 9999 ;
	slurm_set_addr_char ( & slurmd_addr , 7002 , "localhost" ) ;
	request_msg . msg_type = REQUEST_LAUNCH_TASKS ;
	request_msg . data = & launch_tasks_msg ;
	request_msg . address = slurmd_addr ;

	slurm_set_addr_char ( io_pipe_addrs , 7071 , "localhost" ) ;
	slurm_set_addr_char ( io_pipe_addrs + 1 , 7072 , "localhost" ) ;
	launch_tasks_msg . job_id = 1000 ;
	launch_tasks_msg . job_step_id = 2000 ;
	launch_tasks_msg . uid = 8207 ;
	launch_tasks_msg . credentials = & credential ;
	launch_tasks_msg . tasks_to_launch = 1 ;
	launch_tasks_msg . envc = 0 ;
	launch_tasks_msg . env = NULL ;
	launch_tasks_msg . cwd = "." ;
	launch_tasks_msg . cmd_line = "./testme" ;
	launch_tasks_msg . streams = io_pipe_addrs ;
	launch_tasks_msg . global_task_ids = gids ;

	slurm_send_only_node_msg ( & request_msg ) ;

	switch ( response_msg . msg_type ) 
	{
		case RESPONSE_SLURM_RC :
			break ;
		case RESPONSE_LAUNCH_TASKS :
			break ;
		default:
			break ;
	}
	return SLURM_SUCCESS ;
}
