/*****************************************************************************\
 * src/slurmd/job.h  slurmd_job_t definition
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _JOB_H
#define _JOB_H

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include <src/common/macros.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/list.h>
#include <src/common/eio.h>


#define SLURM_KEY_SIZE	SLURM_SSL_SIGNATURE_LENGTH
typedef struct srun_key {
	unsigned char data[SLURM_KEY_SIZE];
} srun_key_t;

typedef enum task_state {
	SLURMD_TASK_INIT,
	SLURMD_TASK_STARTING,
	SLURMD_TASK_RUNNING,
	SLURMD_TASK_COMPLETE
} task_state_t;

typedef struct task_info {
	pthread_mutex_t mutex;	   /* mutex to protect task state         */
	task_state_t    state;	   /* task state                          */

	int             id;	   /* local task id                       */
	uint32_t        gid;	   /* global task id                      */
	pid_t           pid;	   /* task pid                            */
	int             pin[2];    /* stdin pipe                          */
	int             pout[2];   /* stdout pipe                         */
	int             perr[2];   /* stderr pipe                         */
	io_obj_t       *in, 
	               *out,       /* I/O objects used in IO event loop   */
		       *err;       
	int             estatus;   /* this task's exit status             */
	char *		ofile;	   /* output file (if any)                */
	char *		errfile;   /* error file (if any)		  */
	List            srun_list; /* List of srun objs for this task     */
} task_info_t;


typedef struct srun_info {
	srun_key_t *key;	/* srun key for IO verification       */
	slurm_addr resp_addr;	/* response addr for task exit msg    */
	slurm_addr ioaddr;      /* Address to connect on for I/O      */
} srun_info_t;

typedef struct slurmd_job {
	uint32_t      jobid;
	uint32_t      stepid;
	uint32_t      nnodes;
	uint32_t      nprocs;
	uint32_t      nodeid;
	uint32_t      ntasks;
	uint16_t      envc;
	uint16_t      argc;
	char        **env;
	char        **argv;
	char         *cwd;
#ifdef HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;
#endif
	uid_t         uid;
	time_t        timelimit;
	task_info_t **task;
	List          objs; 
	List 	      sruns;
	int           unixsock;
	pthread_t     ioid;
} slurmd_job_t;


slurmd_job_t * job_create(launch_tasks_request_msg_t *msg, slurm_addr *client);

void job_kill(slurmd_job_t *job, int signal);

void job_destroy(slurmd_job_t *job);

struct srun_info * srun_info_create(void *keydata, slurm_addr resp_addr, 
		                    slurm_addr ioaddr);

void  srun_info_destroy(struct srun_info *srun);

struct task_info * task_info_create(int taskid, int gtaskid);

void task_info_destroy(struct task_info *t);

void job_update_shm(slurmd_job_t *job);

void job_delete_shm(slurmd_job_t *job);

#define job_error(j, fmt, args...)					\
        do { 								\
            error("%d.%d: " fmt, (j)->jobid, (j)->stepid, ## args);	\
	} while (0)

#define job_verbose(j, fmt, args...)					\
        do { 								\
            verbose("%d.%d: " fmt, (j)->jobid, (j)->stepid, ## args);	\
	} while (0)

#define job_debug(j, fmt, args...)					\
        do { 								\
            debug("%d.%d: " fmt, (j)->jobid, (j)->stepid, ## args);	\
	} while (0)

#define job_debug2(j, fmt, args...)					\
        do { 								\
            debug2("%d.%d: " fmt, (j)->jobid, (j)->stepid, ## args);	\
	} while (0)

#define job_debug3(j, fmt, args...)					\
        do { 								\
            debug3("%d.%d: " fmt, (j)->jobid, (j)->stepid, ## args);	\
	} while (0)

#define job_info(j, fmt, args...)					\
        do { 								\
            info("%d.%d: " fmt, (j)->jobid, (j)->stepid, ## args);	\
	} while (0)

#endif /* !_JOB_H */
