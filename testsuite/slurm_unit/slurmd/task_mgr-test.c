#include <unistd.h>
#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/task_mgr.h>

int main ( int argc , char ** argv )
{
	launch_tasks_msg_t launch_tasks_msg ;
	kill_tasks_msg_t kill_tasks_msg ;
	launch_tasks_msg . tasks_to_launch = 1 ;
	launch_tasks_msg . job_id = 1000 ;
	launch_tasks_msg . job_step_id = 2000 ; 
	launch_tasks_msg . uid = 801 ;
	launch_tasks_msg . gid = 802 ;
	launch_tasks_msg . env = "";
	launch_tasks_msg . cmd_line = "./testme" ;
	launch_tasks_msg . cwd = "." ;
	
	launch_tasks ( & launch_tasks_msg ) ;
	sleep ( 1 ) ;
	kill_tasks ( & kill_tasks_msg ) ;
	return SLURM_SUCCESS ;
}
