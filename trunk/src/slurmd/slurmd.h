#ifndef _SLURMD_H
#define _SLURMD_H
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

typedef struct tasks_req
{
	List		thread_list;
	pthread_t	threadid;
	uint32_t	job_id;
	uint32_t	job_step_id;
	uint32_t	task_id;
	uint32_t	uid;
	uint32_t	gid;
} tasks_req_t ;
