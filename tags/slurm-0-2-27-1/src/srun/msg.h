#include "src/srun/job.h"

#ifndef _HAVE_MSG_H
#define _HAVE_MSG_H

void *msg_thr(void *arg);
int   msg_thr_create(job_t *job);

#endif /* !_HAVE_MSG_H */
