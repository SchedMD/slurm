/*****************************************************************************\
 *  src/common/stepd_api.h - slurmstepd message API
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
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

#ifndef _STEPD_API_H
#define _STEPD_API_H

#include <inttypes.h>

#include "slurm/slurm.h"
#include "src/common/list.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/io_hdr.h"

typedef enum {
	REQUEST_CONNECT = 0,
	REQUEST_SIGNAL_PROCESS_GROUP, /* Defunct, See REQUEST_SIGNAL_CONTAINER */
	REQUEST_SIGNAL_TASK_LOCAL, /* Defunct see REQUEST_SIGNAL_CONTAINER */
	REQUEST_SIGNAL_TASK_GLOBAL, /* Defunct see REQUEST_SIGNAL_CONTAINER */
	REQUEST_SIGNAL_CONTAINER,
	REQUEST_STATE,
	REQUEST_INFO,  /* Defunct, See REQUEST_STEP_MEM_LIMITS|UID|NODEID */
	REQUEST_ATTACH,
	REQUEST_PID_IN_CONTAINER,
	REQUEST_DAEMON_PID,
	REQUEST_STEP_SUSPEND,
	REQUEST_STEP_RESUME,
	REQUEST_STEP_TERMINATE,
	REQUEST_STEP_COMPLETION,  /* Defunct, See REQUEST_STEP_COMPLETION_V2 */
	REQUEST_STEP_TASK_INFO,
	REQUEST_STEP_LIST_PIDS,
	REQUEST_STEP_RECONFIGURE,
	REQUEST_STEP_STAT,
	REQUEST_STEP_COMPLETION_V2,
	REQUEST_STEP_MEM_LIMITS,
	REQUEST_STEP_UID,
	REQUEST_STEP_NODEID,
	REQUEST_ADD_EXTERN_PID,
	REQUEST_X11_DISPLAY,
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
	uint16_t protocol_version;
	uint64_t job_mem_limit;		/* job's memory limit, MB */
	uint64_t step_mem_limit;	/* step's memory limit, MB */
} slurmstepd_info_t;

typedef struct {
	uint64_t job_mem_limit;		/* job's memory limit, MB */
	uint32_t nodeid;
	uint64_t step_mem_limit;	/* step's memory limit, MB */
} slurmstepd_mem_info_t;

typedef struct {
	int             id;	    /* local task id */
	uint32_t        gtid;	    /* global task id */
	pid_t           pid;	    /* task pid */
	bool            exited;     /* true if task has exited */
	int             estatus;    /* exit status if exited is true*/
} slurmstepd_task_info_t;

typedef struct step_location {
	uint32_t jobid;
	uint32_t stepid;
	char *nodename;
	char *directory;
	uint16_t protocol_version;
} step_loc_t;


/*
 * Cleanup stale stepd domain sockets.
 */
int stepd_cleanup_sockets(const char *directory, const char *nodename);

int stepd_terminate(int fd, uint16_t protocol_version);

/*
 * Connect to a slurmstepd proccess by way of its unix domain socket.
 *
 * Both "directory" and "nodename" may be null, in which case stepd_connect
 * will attempt to determine them on its own.  If you are using multiple
 * slurmd on one node (unusual outside of development environments), you
 * will get one of the local NodeNames more-or-less at random.
 *
 * Returns a socket descriptor for the opened socket on success,
 * and -1 on error.  Also fills in protocol_version with the version
 * of the running stepd.
 */
extern int stepd_connect(const char *directory, const char *nodename,
		  uint32_t jobid, uint32_t stepid, uint16_t *protocol_version);

/*
 * Retrieve a job step's current state.
 */
slurmstepd_state_t stepd_state(int fd, uint16_t protocol_version);

/*
 * Retrieve slurmstepd_info_t structure for a job step.
 *
 * Must be xfree'd by the caller.
 */
slurmstepd_info_t *stepd_get_info(int fd);

/*
 * Send job notification message to a batch job
 */
int stepd_notify_job(int fd, uint16_t protocol_version, char *message);

/*
 * Send a checkpoint request to all tasks of a job step.
 */
int stepd_checkpoint(int fd, uint16_t protocol_version,
		     time_t timestamp, char *image_dir);

/*
 * Send a signal to the proctrack container of a job step.
 */
int stepd_signal_container(int fd, uint16_t protocol_version, int signal,
			   int flags, uid_t uid);

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
int stepd_attach(int fd, uint16_t protocol_version,
		 slurm_addr_t *ioaddr, slurm_addr_t *respaddr,
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
extern List stepd_available(const char *directory, const char *nodename);

/*
 * Return true if the process with process ID "pid" is found in
 * the proctrack container of the slurmstepd "step".
 */
bool stepd_pid_in_container(int fd, uint16_t protocol_version, pid_t pid);

/*
 * Add a pid to the "extern" step of a job, meaning add it to the
 * jobacct_gather and proctrack plugins.
 */
extern int stepd_add_extern_pid(int fd, uint16_t protocol_version, pid_t pid);

/*
 * Fetch the display number if this extern step is providing x11 tunneling.
 * If temporary XAUTHORITY files are in use, xauthority is set to that path,
 * otherwise NULL.
 * Returns 0 to indicate no display forwarded.
 */
extern int stepd_get_x11_display(int fd, uint16_t protocol_version,
				 char **xauthority);

/*
 * Return the process ID of the slurmstepd.
 */
pid_t stepd_daemon_pid(int fd, uint16_t protocol_version);

/*
 * Suspend execution of the job step.  Only root or SlurmUser is
 * authorized to use this call.
 *
 * Returns SLURM_SUCCESS if successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
extern int stepd_suspend(int fd, uint16_t protocol_version,
			 suspend_int_msg_t *susp_req, int phase);

/*
 * Resume execution of the job step that has been suspended by a
 * call to stepd_suspend().  Only root or SlurmUser is
 * authorized to use this call.
 *
 * Returns SLURM_SUCCESS if successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
extern int stepd_resume(int fd, uint16_t protocol_version,
			suspend_int_msg_t *susp_req, int phase);

/*
 * Reconfigure the job step (Primarily to allow the stepd to refresh
 * it's log file pointer.
 *
 * Returns SLURM_SUCCESS if successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
int stepd_reconfig(int fd, uint16_t protocol_version);

/*
 *
 * Returns SLURM_SUCCESS if successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
int stepd_completion(int fd, uint16_t protocol_version,
		     step_complete_msg_t *sent);

/*
 *
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on error.
 * resp receives a jobacctinfo_t which must be freed if SUCCESS.
 */
int stepd_stat_jobacct(int fd, uint16_t protocol_version,
		       job_step_id_msg_t *sent, job_step_stat_t *resp);


int stepd_task_info(int fd, uint16_t protocol_version,
		    slurmstepd_task_info_t **task_info,
		    uint32_t *task_info_count);

int stepd_list_pids(int fd, uint16_t protocol_version,
		    uint32_t **pids_array, uint32_t *pids_count);

/*
 * Get the memory limits of the step
 * Returns uid of the running step if successful.  On error returns -1.
 */
extern int stepd_get_mem_limits(int fd, uint16_t protocol_version,
				slurmstepd_mem_info_t *stepd_mem_info);

/*
 * Get the uid of the step
 * Returns uid of the running step if successful.  On error returns -1.
 *
 * FIXME: BUG: On Linux, uid_t is uint32_t but this can return -1.
 */
extern uid_t stepd_get_uid(int fd, uint16_t protocol_version);

/*
 * Get the nodeid of the stepd
 * Returns nodeid of the running stepd if successful.  On error returns NO_VAL.
 */
extern uint32_t stepd_get_nodeid(int fd, uint16_t protocol_version);

#endif /* _STEPD_API_H */
