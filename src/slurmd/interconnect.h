#ifndef _SLURMD_INTERCONNECT_H_
#define _SLURMD_INTERCONNECT_H_

int interconnect_init ( launch_tasks_request_msg_t * launch_msg );
int fan_out_task_launch ( launch_tasks_request_msg_t * launch_msg );
int interconnect_set_capabilities ( task_start_t * task_start ) ;

#endif
