#ifndef _TASK_MGR_H
#define _TASK_MGR_H

#if HAVE_CONFIG_H
#  include <config.h>
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#define STDIN_IO_THREAD 0
#define STDOUT_IO_THREAD 1
#define STDERR_IO_THREAD 2
#define STDSIG_IO_THREAD 3
#define SLURMD_NUMBER_OF_IO_THREADS 4
#define SLURMD_IO_MAX_BUFFER_SIZE 4096

/* function prototypes */
int launch_tasks ( launch_tasks_msg_t * launch_msg ) ;
int kill_tasks ( kill_tasks_msg_t * kill_task_msg ) ;

typedef struct task_start
{
	/*task control thread id*/
	pthread_t		pthread_id;
	int 			thread_return;
	/*actual exec thread id*/
	pthread_t		exec_pthread_id;
	int 			exec_thread_return;
	/*io threads ids*/
	pthread_t		io_pthread_id[SLURMD_NUMBER_OF_IO_THREADS];
	int			io_thread_return[SLURMD_NUMBER_OF_IO_THREADS];
	launch_tasks_msg_t * 	launch_msg;
	int			pipes[6];
	int			sockets[2];
} task_start_t ;
#endif
