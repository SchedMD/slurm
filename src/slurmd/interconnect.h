#ifndef _SLURMD_INTERCONNECT_H_
#define _SLURMD_INTERCONNECT_H_

/* interconnect_init
 * called by launch_tasks to initialize the interconnect
 * IN launch_msg	- launch_tasks_msg
 * RET int		- return_code
 */
int interconnect_init ( launch_tasks_request_msg_t * launch_msg );

/* fan_out_task_launch
 * called by launch_tasks to do the task fan out
 * IN launch_msg	- launch_tasks_msg
 * RET int		- return_code
 */
int fan_out_task_launch ( launch_tasks_request_msg_t * launch_msg );

/* interconnect_set_capabilities
 * called by fan_out_task_launch to set interconnect capabilities
 * IN task_start	- task_start structure
 * RET int		- return_code
 */
int interconnect_set_capabilities ( task_start_t * task_start ) ;

#endif
