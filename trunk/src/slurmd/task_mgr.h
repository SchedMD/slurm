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
#endif

/* function prototypes */
void task_mgr_init ( ) ;
int launch_tasks ( launch_tasks_msg_t * launch_msg ) ;
int kill_tasks ( kill_tasks_msg_t * kill_task_msg ) ;

typedef struct task
{
	pthread_t	threadid;
	uint32_t	pid;
	uint32_t	job_id;
	uint32_t	job_step_id;
	uint32_t	task_id;
	uint32_t	uid;
	uint32_t	gid;
} task_t ;


