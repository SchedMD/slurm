#ifndef _SLURMD_IO_H_
#define _SLURMD_IO_H_
#include <src/slurmd/task_mgr.h>


int launch_task ( task_start_t * task_start ) ;
int wait_for_tasks ( launch_tasks_request_msg_t * launch_msg , task_start_t ** task_start ) ;
int kill_launched_tasks ( launch_tasks_request_msg_t * launch_msg , task_start_t ** task_start , int i ) ;
#endif
