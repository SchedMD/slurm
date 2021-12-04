/*****************************************************************************\
 *  src/srun/srun_job.h - specification of an srun "job"
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _HAVE_JOB_H
#define _HAVE_JOB_H

#include <netinet/in.h>
#include <pthread.h>

#include "slurm/slurm.h"

#include "src/common/eio.h"
#include "src/common/cbuf.h"
#include "src/common/macros.h"
#include "src/common/select.h"
#include "src/common/slurm_protocol_defs.h"

#include "src/api/step_io.h"
#include "src/srun/libsrun/opt.h"
#include "src/srun/libsrun/step_ctx.h"

typedef enum {
	SRUN_JOB_INIT = 0,         /* Job's initial state                   */
	SRUN_JOB_LAUNCHING,        /* Launch thread is running              */
	SRUN_JOB_STARTING,         /* Launch thread is complete             */
	SRUN_JOB_RUNNING,          /* Launch thread complete                */
	SRUN_JOB_CANCELLED,        /* CTRL-C cancelled                      */
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
	slurm_step_id_t step_id; /* assigned step id */
	uint32_t het_job_node_offset;	/* Hetjob node offset or NO_VAL */
	uint32_t het_job_id;	/* Hetjob leader or NO_VAL */
	char    *het_job_node_list; /* node list for combined hetjob */
	uint32_t het_job_nnodes; /* total node count for entire hetjob */
	uint32_t het_job_ntasks; /* total task count for entire hetjob */
	uint32_t het_job_offset; /* Hetjob offset or NO_VAL */
	uint32_t het_job_task_offset; /* Hetjob task offset or NO_VAL */
	uint16_t *het_job_task_cnts; /* tasks invoked on each node of hetjob */
	uint32_t **het_job_tids;	/* Task IDs on each node of hetjob */
	uint32_t *het_job_tid_offsets;/* map of tasks (by id) to originating
				       * hetjob */

	char *container; /* OCI container bundle path */
	uint32_t cpu_count;	/* allocated CPUs */
	uint32_t nhosts;	/* node count */
	uint32_t ntasks;	/* task count */
	uint16_t ntasks_per_board;/* number of tasks to invoke on each board */
	uint16_t ntasks_per_core; /* number of tasks to invoke on each core */
	uint16_t ntasks_per_tres; /* number of tasks that can access each gpu */
	uint16_t ntasks_per_socket;/* number of tasks to invoke on
				    * each socket */

	srun_job_state_t state;	/* job state	   	                  */
	pthread_mutex_t state_mutex;
	pthread_cond_t  state_cond;

	int  rc;                /* srun return code                       */

	char *alias_list;	/* node name/address/hostname aliases */
	char **env;		/* hetjob specific environment */
	char *nodelist;		/* nodelist in string form */
	char *partition;	/* name of partition running job */

	fname_t *ifname;
	fname_t *ofname;
	fname_t *efname;

	/* Output streams and stdin fileno */
	dynamic_plugin_data_t *select_jobinfo;

	/* Pseudo terminial support */
	int pty_fd;		/* file to communicate window size changes */
	uint16_t pty_port;	/* used to communicate window size changes */
	uint16_t ws_col;	/* window size, columns */
	uint16_t ws_row;	/* window size, row count */
	slurm_step_ctx_t *step_ctx;
	char *account;    /* account of this job */
	char *qos;        /* job's qos */
	char *resv_name;  /* reservation the job is using */
} srun_job_t;

void    update_job_state(srun_job_t *job, srun_job_state_t newstate);
void    job_force_termination(srun_job_t *job);

srun_job_state_t job_state(srun_job_t *job);

extern srun_job_t * job_create_noalloc(void);

/*
 * Create an srun job structure for a step w/out an allocation response msg.
 * (i.e. inside an allocation)
 */
extern srun_job_t *job_step_create_allocation(
			resource_allocation_response_msg_t *resp,
			slurm_opt_t *opt_local);

/*
 * Create an srun job structure from a resource allocation response msg
 */
extern srun_job_t *job_create_allocation(
			resource_allocation_response_msg_t *resp,
			slurm_opt_t *opt_local);

extern void init_srun(int argc, char **argv, log_options_t *logopt,
		      bool handle_signals);

extern void create_srun_job(void **p_job, bool *got_alloc,
			    bool slurm_started, bool handle_signals);

extern void pre_launch_srun_job(srun_job_t *job, bool slurm_started,
				bool handle_signals, slurm_opt_t *opt_local);

extern void fini_srun(srun_job_t *job, bool got_alloc, uint32_t *global_rc,
		      bool slurm_started);

/*
 *  Update job filenames and modes for stderr, stdout, and stdin.
 */
extern void job_update_io_fnames(srun_job_t *job, slurm_opt_t *opt_local);

/* Set up port to handle messages from slurmctld */
int slurmctld_msg_init(void);

#endif /* !_HAVE_JOB_H */
