/*****************************************************************************\
 *  src/common/stepd_api.h - slurmstepd message API
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
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

#ifndef _STEPD_API_H
#define _STEPD_API_H

#include <inttypes.h>

#include "slurm/slurm.h"
#include "src/common/list.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/io_hdr.h"

typedef struct step_location {
	uint32_t jobid;
	uint32_t stepid;
	char *nodename;
	char *directory;
} step_loc_t;

typedef enum {
	REQUEST_CONNECT = 0,
	REQUEST_SIGNAL_PROCESS_GROUP,
	REQUEST_SIGNAL_TASK_LOCAL,
	REQUEST_SIGNAL_TASK_GLOBAL,
	REQUEST_SIGNAL_CONTAINER,
	REQUEST_STATE,
	REQUEST_INFO,
	REQUEST_ATTACH,
	REQUEST_PID_IN_CONTAINER,
	REQUEST_DAEMON_PID,
	REQUEST_STEP_SUSPEND,
	REQUEST_STEP_RESUME,
	REQUEST_STEP_TERMINATE,
	REQUEST_STEP_COMPLETION,
	REQUEST_STEP_TASK_INFO,
	REQUEST_STEP_LIST_PIDS
} step_msg_t;

typedef enum {
	SLURMSTEPD_NOT_RUNNING = 0,
	SLURMSTEPD_STEP_STARTING,
	SLURMSTEPD_STEP_RUNNING,
	SLURMSTEPD_STEP_ENDING
} slurmstepd_state_t;

typedef struct {
	uid_t uid;
	uint32_t jobid;
	uint32_t stepid;
	uint32_t nodeid;
	uint32_t job_mem_limit;		/* job's memory limit, MB */
} slurmstepd_info_t;

typedef struct {
	int             id;	    /* local task id */
	uint32_t        gtid;	    /* global task id */
	pid_t           pid;	    /* task pid */
	bool            exited;     /* true if task has exited */
	int             estatus;    /* exit status if exited is true*/
} slurmstepd_task_info_t;

/*
 * Cleanup stale stepd domain sockets.
 */
int stepd_cleanup_sockets(const char *directory, const char *nodename);

int stepd_terminate(int fd);

/*
 * Connect to a slurmstepd proccess by way of its unix domain socket.
 *
 * Both "directory" and "nodename" may be null, in which case stepd_connect
 * will attempt to determine them on its own.  If you are using multiple
 * slurmd on one node (unusual outside of development environments), you
 * will get one of the local NodeNames more-or-less at random.
 *
 * Returns a socket descriptor for the opened socket on success, 
 * and -1 on error.
 */
int stepd_connect(const char *directory, const char *nodename,
		  uint32_t jobid, uint32_t stepid);

/*
 * Retrieve a job step's current state.
 */
slurmstepd_state_t stepd_state(int fd);

/*
 * Retrieve slurmstepd_info_t structure for a job step.
 *
 * Must be xfree'd by the caller.
 */
slurmstepd_info_t *stepd_get_info(int fd);

/*
 * Send a signal to the process group of a job step.
 */
int stepd_signal(int fd, int signal);

/*
 * Send a checkpoint request to all tasks of a job step.
 */
int stepd_checkpoint(int fd, int signal, time_t timestamp);

/*
 * Send a signal to a single task in a job step.
 */
int stepd_signal_task_local(int fd, int signal, int ltaskid); 

/*
 * Send a signal to a single task in a job step.
 */
int stepd_signal_task_global(int fd, int signal, int gtaskid);

/*
 * Send a signal to the proctrack container of a job step.
 */
int stepd_signal_container(int fd, int signal);

/*
 * Attach a client to a running job step.
 *
 * On success returns SLURM_SUCCESS and fills in resp->local_pids,
 * resp->gtids, resp->ntasks, and resp->executable.
 *
 * FIXME - The pid/gtid info returned in the "resp" parameter should
 *         probably be moved into a more generic stepd_api call so that
 *         this header does not need to include slurm_protocol_defs.h.
 */
int stepd_attach(int fd, slurm_addr *ioaddr, slurm_addr *respaddr,
		 void *job_cred_sig, reattach_tasks_response_msg_t *resp);

/*
 * Scan for available running slurm step daemons by checking
 * "directory" for unix domain sockets with names beginning in "nodename".
 *
 * Both "directory" and "nodename" may be null, in which case stepd_available
 * will attempt to determine them on its own.  If you are using multiple
 * slurmd on one node (unusual outside of development environments), you
 * will get one of the local NodeNames more-or-less at random.
 *
 * Returns a List of pointers to step_loc_t structures.
 */
List stepd_available(const char *directory, const char *nodename);

/*
 * Return true if the process with process ID "pid" is found in
 * the proctrack container of the slurmstepd "step".
 */
bool stepd_pid_in_container(int fd, pid_t pid);

/*
 * Return the process ID of the slurmstepd.
 */
pid_t stepd_daemon_pid(int fd);

/*
 * Suspend execution of the job step.  Only root or SlurmUser is
 * authorized to use this call.
 *
 * Returns SLURM_SUCCESS is successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
int stepd_suspend(int *fd, int size, uint32_t jobid);

/*
 * Resume execution of the job step that has been suspended by a
 * call to stepd_suspend().  Only root or SlurmUser is
 * authorized to use this call.
 *
 * Returns SLURM_SUCCESS is successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
int stepd_resume(int fd);

/*
 *
 * Returns SLURM_SUCCESS is successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
int stepd_completion(int fd, step_complete_msg_t *sent);

/*
 *
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on error.  
 * resp receives a jobacctinfo_t which must be freed if SUCCESS.
 */
int stepd_stat_jobacct(int fd, stat_jobacct_msg_t *sent, 
		       stat_jobacct_msg_t *resp);


int stepd_task_info(int fd, slurmstepd_task_info_t **task_info,
		    uint32_t *task_info_count);

int stepd_list_pids(int fd, pid_t **pids_array, int *pids_count);


#endif /* _STEPD_API_H */
