#ifndef _SLURMD_IO_H_
#define _SLURMD_IO_H_
#include <src/slurmd/task_mgr.h>

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

int forward_io ( task_start_t * task_arg ) ;
void * stdin_io_pipe_thread ( void * arg ) ;
void * stdout_io_pipe_thread ( void * arg ) ;
void * stderr_io_pipe_thread ( void * arg ) ;

#endif
