/* */

#ifndef _HAVE_LAUNCH_H
#define _HAVE_LAUNCH_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#if HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#include <src/common/slurm_protocol_api.h>
#include <src/common/macros.h>

#include <src/srun/opt.h>

typedef struct launch_thr {
	pthread_t	thread;
	pthread_attr_t  attr;
	char            *host;		/* name of host on which to run       */
	int             ntasks;		/* number of tasks to initiate on host*/
	int 		*taskid;	/* list of global task ids 	      */
	int 		i;		/* temporary index into array	      */
} launch_thr_t;


void * launch(void *arg);

#endif /* !_HAVE_LAUNCH_H */
