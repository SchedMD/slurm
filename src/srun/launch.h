/* */

#ifndef _HAVE_LAUNCH_H
#define _HAVE_LAUNCH_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"

#include "src/srun/opt.h"

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
