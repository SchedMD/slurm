/*****************************************************************************\
 *  src/slurmd/slurmstepd/slurmstepd_job.h  slurmd_job_t definition
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#ifndef _SLURMSTEPD_JOB_H
#define _SLURMSTEPD_JOB_H

#if WITH_PTHREADS
#include <pthread.h>
#endif

#include <pwd.h>

#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/list.h"
#include "src/common/eio.h"
#include "src/common/switch.h"
#include "src/common/env.h"
#include "src/common/io_hdr.h"
#include "src/common/job_options.h"
#include "src/common/stepd_api.h"

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif

typedef struct srun_key {
	unsigned char data[SLURM_IO_KEY_SIZE];
} srun_key_t;

typedef struct srun_info {
	srun_key_t *key;	   /* srun key for IO verification         */
	slurm_addr resp_addr;	   /* response addr for task exit msg      */
	slurm_addr ioaddr;         /* Address to connect on for normal I/O.
				      Spawn IO uses messages to the normal
				      resp_addr. */
} srun_info_t;

typedef enum task_state {
	SLURMD_TASK_INIT,
	SLURMD_TASK_STARTING,
	SLURMD_TASK_RUNNING,
	SLURMD_TASK_COMPLETE
} slurmd_task_state_t;

typedef struct task_info {
	pthread_mutex_t mutex;	    /* mutex to protect task state          */
	slurmd_task_state_t state;  /* task state                           */
 
	int             id;	    /* local task id                        */
	uint32_t        gtid;	    /* global task id                       */
	pid_t           pid;	    /* task pid                             */

	char           *ifname;     /* standard input file name             */
	char           *ofname;     /* standard output file name            */
	char           *efname;     /* standard error file name             */
	int             stdin_fd;   /* standard input file descriptor       */
	int             stdout_fd;  /* standard output file descriptor      */
	int             stderr_fd;  /* standard error file descriptor       */
	int             to_stdin;   /* write file descriptor for task stdin */
	int             from_stdout;/* read file descriptor from task stdout*/
	int             from_stderr;/* read file descriptor from task stderr*/
	eio_obj_t      *in;         /* standard input event IO object       */
	eio_obj_t      *out;        /* standard output event IO object      */
	eio_obj_t      *err;        /* standard error event IO object       */

	bool            esent;      /* true if exit status has been sent    */
	bool            exited;     /* true if task has exited              */
	int             estatus;    /* this task's exit status              */

	int		argc;
	char	      **argv;
} slurmd_task_info_t;

typedef struct slurmd_job {
	slurmstepd_state_t state;
	uint32_t       jobid;  /* Current SLURM job id                      */
	uint32_t       stepid; /* Current step id (or NO_VAL)               */
	uint32_t       nnodes; /* number of nodes in current job            */
	uint32_t       nprocs; /* total number of processes in current job  */
	uint32_t       nodeid; /* relative position of this node in job     */
	uint32_t       ntasks; /* number of tasks on *this* node            */
	uint32_t       cpus_per_task;	/* number of cpus desired per task  */
	uint32_t       debug;  /* debug level for job slurmd                */
	uint32_t       job_mem;  /* MB of memory reserved for the job       */
	uint16_t       cpus;   /* number of cpus to use for this job        */
	uint16_t       argc;   /* number of commandline arguments           */
	char         **env;    /* job environment                           */
	char         **argv;   /* job argument vector                       */
	char          *cwd;    /* path to current working directory         */
	task_dist_states_t task_dist;/* -m distribution                     */
	char          *node_name; /* node name of node running job
				   * needed for front-end systems           */
	cpu_bind_type_t cpu_bind_type; /* --cpu_bind=                       */
	char          *cpu_bind;       /* binding map for map/mask_cpu      */
	mem_bind_type_t mem_bind_type; /* --mem_bind=                       */
	char          *mem_bind;       /* binding map for tasks to memory   */
	switch_jobinfo_t switch_job; /* switch-specific job information     */
	uid_t         uid;     /* user id for job                           */
	gid_t         gid;     /* group ID for job                          */
	int           ngids;   /* length of the following gids array        */
	gid_t        *gids;    /* array of gids for user specified in uid   */
	bool           aborted;    /* true if already aborted               */
	bool           batch;      /* true if this is a batch job           */
	bool           run_prolog; /* true if need to run prolog            */
	bool           user_managed_io;
	time_t         timelimit;  /* time at which job must stop           */
	char          *task_prolog; /* per-task prolog                      */
	char          *task_epilog; /* per-task epilog                      */
	struct passwd *pwd;   /* saved passwd struct for user job           */
	slurmd_task_info_t  **task;  /* array of task information pointers  */
	eio_handle_t  *eio;
	List 	       sruns; /* List of srun_info_t pointers               */
	List           clients; /* List of struct client_io_info pointers   */
	List stdout_eio_objs; /* List of objs that gather stdout from tasks */
	List stderr_eio_objs; /* List of objs that gather stderr from tasks */
	List free_incoming;   /* List of free struct io_buf * for incoming
			       * traffic. "incoming" means traffic from srun
			       * to the tasks.
			       */
	List free_outgoing;   /* List of free struct io_buf * for outgoing
			       * traffic "outgoing" means traffic from the
			       * tasks to srun.
			       */
	int incoming_count;   /* Count of total incoming message buffers
			       * including free_incoming buffers and
			       * buffers in use.
			       */
	int outgoing_count;   /* Count of total outgoing message buffers
			       * including free_outgoing buffers and
			       * buffers in use.
			       */

	List outgoing_cache;  /* cache of outgoing stdio messages
			       * used when a new client attaches
			       */

	uint8_t	buffered_stdio; /* stdio buffering flag, 1 for line-buffering,
				 * 0 for no buffering
				 */
	uint8_t labelio;	/* 1 for labelling output with the task id */

	pthread_t      ioid;  /* pthread id of IO thread                    */
	pthread_t      msgid; /* pthread id of message thread               */
	eio_handle_t  *msg_handle; /* eio handle for the message thread     */

	pid_t          jmgr_pid;     /* job manager pid                     */
	pid_t          pgid;         /* process group id for tasks          */

	uint16_t       task_flags; 
	uint16_t       multi_prog;
	uint16_t       overcommit;
	env_t          *envtp;
	uint32_t       cont_id;

	char          *batchdir;
	jobacctinfo_t *jobacct;
	uint8_t        open_mode;	/* stdout/err append or truncate */
	uint8_t        pty;		/* set if creating pseudo tty	*/
	job_options_t  options;
	char          *ckpt_dir;
	time_t         ckpt_timestamp;
	char          *restart_dir;	/* restart from context */
	char          *resv_id;		/* Cray/BASIL reservation ID	*/
	uint16_t       restart_cnt;	/* batch job restart count	*/
	char	      *alloc_cores;	/* needed by the SPANK cpuset plugin */
} slurmd_job_t;


slurmd_job_t * job_create(launch_tasks_request_msg_t *msg);
slurmd_job_t * job_batch_job_create(batch_job_launch_msg_t *msg);

void job_kill(slurmd_job_t *job, int signal);

void job_destroy(slurmd_job_t *job);

struct srun_info * srun_info_create(slurm_cred_t cred, slurm_addr *respaddr, 
		                    slurm_addr *ioaddr);

void  srun_info_destroy(struct srun_info *srun);

slurmd_task_info_t * task_info_create(int taskid, int gtaskid,
				      char *ifname, char *ofname, char *efname);

#endif /* !_SLURMSTEPD_JOB_H */
