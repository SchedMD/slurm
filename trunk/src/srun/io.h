#ifndef _HAVE_IO_H
#define _HAVE_IO_H

#include "src/srun/job.h"

#define WAITING_FOR_IO -1
#define IO_DONE -9

void *io_thr(void *arg);
int   io_thr_create(job_t *job);
void  report_job_status(job_t *job);
void  report_task_status(job_t *job);

#endif /* !_HAVE_IO_H */
