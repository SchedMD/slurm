#include <src/common/slurm_protocol_api.h>

int interconnect_init ( launch_tasks_request_msg_t * launch_msg ) ;
int fan_out_task_launch ( launch_tasks_request_msg_t * launch_msg ) ;
/* exported module funtion to launch tasks */
/*launch_tasks should really be named launch_job_step*/
int launch_tasks ( launch_tasks_request_msg_t * launch_msg )
{
	return interconnect_init ( launch_msg );
}

/* Contains interconnect specific setup instructions and then calls 
 * fan_out_task_launch */
int interconnect_init ( launch_tasks_request_msg_t * launch_msg )
{
	return fan_out_task_launch ( launch_msg ) ;
}


