/*****************************************************************************\
 *  src/srun/srun_job.h - specification of an srun "job"
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _HAVE_JOB_H
#define _HAVE_JOB_H

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <netinet/in.h>

#include <slurm/slurm.h>

#include "src/common/eio.h"
#include "src/common/cbuf.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/api/step_io.h"


typedef enum {
	SRUN_JOB_INIT = 0,         /* Job's initial state                   */
	SRUN_JOB_LAUNCHING,        /* Launch thread is running              */
	SRUN_JOB_STARTING,         /* Launch thread is complete             */
	SRUN_JOB_RUNNING,          /* Launch thread complete                */
	SRUN_JOB_TERMINATING,      /* Once first task terminates            */
	SRUN_JOB_TERMINATED,       /* All tasks terminated (may have IO)    */
	SRUN_JOB_WAITING_ON_IO,    /* All tasks terminated; waiting for IO  */
	SRUN_JOB_DONE,             /* tasks and IO complete                 */
	SRUN_JOB_DETACHED,         /* Detached IO from job (Not used now)   */
	SRUN_JOB_FAILED,           /* Job failed for some reason            */
	SRUN_JOB_CANCELLED,        /* CTRL-C cancelled                      */
	SRUN_JOB_FORCETERM         /* Forced termination of IO thread       */
} srun_job_state_t;

enum io_t {
	IO_ALL          = 0, /* multiplex output from all/bcast stdin to all */
	IO_ONE          = 1, /* output from only one task/stdin to one task  */
	IO_PER_TASK     = 2, /* separate output/input file per task          */
	IO_NONE         = 3, /* close output/close stdin                     */
};

#define format_io_t(t) (t == IO_ONE) ? "one" : (t == IO_ALL) ? \
                                                     "all" : "per task"

typedef struct fname {
	char      *name;
	enum io_t  type;
	int        taskid;  /* taskid for IO if IO_ONE */
} fname_t;

typedef struct srun_job {
	uint32_t jobid;		/* assigned job id 	                  */
	uint32_t stepid;	/* assigned step id 	                  */

	uint32_t cpu_count;	/* allocated CPUs */
	uint32_t nhosts;	/* node count */
	uint32_t ntasks;	/* task count */
	srun_job_state_t state;	/* job state	   	                  */
	pthread_mutex_t state_mutex; 
	pthread_cond_t  state_cond;

	int  rc;                /* srun return code                       */

	char *nodelist;		/* nodelist in string form */

	fname_t *ifname;
	fname_t *ofname;
	fname_t *efname;

	/* Output streams and stdin fileno */
	select_jobinfo_t select_jobinfo;

	/* Pseudo terminial support */
	pthread_t pty_id;	/* pthread to communicate window size changes */
	int pty_fd;		/* file to communicate window size changes */ 
	uint16_t pty_port;	/* used to communicate window size changes */
	uint8_t ws_col;		/* window size, columns */
	uint8_t ws_row;		/* window size, row count */
	slurm_step_ctx_t *step_ctx;
	slurm_step_ctx_params_t ctx_params;
} srun_job_t;

void    update_job_state(srun_job_t *job, srun_job_state_t newstate);
void    job_force_termination(srun_job_t *job);

srun_job_state_t job_state(srun_job_t *job);

extern srun_job_t * job_create_noalloc(void);
extern srun_job_t *job_step_create_allocation(
	resource_allocation_response_msg_t *resp);
extern srun_job_t * job_create_allocation(
	resource_allocation_response_msg_t *resp);
extern srun_job_t * job_create_structure(
	resource_allocation_response_msg_t *resp);

/*
 *  Update job filenames and modes for stderr, stdout, and stdin.
 */
void    job_update_io_fnames(srun_job_t *j);

/* Set up port to handle messages from slurmctld */
slurm_fd slurmctld_msg_init(void);

#endif /* !_HAVE_JOB_H */
