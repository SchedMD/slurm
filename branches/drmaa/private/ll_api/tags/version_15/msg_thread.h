/*****************************************************************************\
 *  msg_thread.h - Header file for msg_thread.c. 
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Danny Auble <da@llnl.gov>
 * 
 *  This file is part of slurm_ll_api, a collection of LoadLeveler-compatable
 *  interfaces to Simple Linux Utility for Resource Managment (SLURM).  These 
 *  interfaces are used by POE (IBM's Parallel Operating Environment) to 
 *  initiated SLURM jobs. For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  This notice is required to be provided under our contract with the U.S.
 *  Department of Energy (DOE).  This work was produced at the University
 *  of California, Lawrence Livermore National Laboratory under Contract
 *  No. W-7405-ENG-48 with the DOE.
 * 
 *  Neither the United States Government nor the University of California
 *  nor any of their employees, makes any warranty, express or implied, or
 *  assumes any liability or responsibility for the accuracy, completeness,
 *  or usefulness of any information, apparatus, product, or process
 *  disclosed, or represents that its use would not infringe
 *  privately-owned rights.
 *
 *  Also, reference herein to any specific commercial products, process, or
 *  services by trade name, trademark, manufacturer or otherwise does not
 *  necessarily constitute or imply its endorsement, recommendation, or
 *  favoring by the United States Government or the University of
 *  California.  The views and opinions of authors expressed herein do not
 *  necessarily state or reflect those of the United States Government or
 *  the University of California, and shall not be used for advertising or
 *  product endorsement purposes.
 * 
 *  The precise terms and conditions for copying, distribution and
 *  modification are specified in the file "COPYING".
\*****************************************************************************/
#ifndef _MSG_THREAD_H
#define _MSG_THREAD_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STRING_H
#    include <string.h>
#  endif
#  if HAVE_PTHREAD_H
#    include <pthread.h>
#  endif
#else                /* !HAVE_CONFIG_H */
#  include <string.h>
#  include <pthread.h>
#endif                /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h> 
#include <sys/select.h> 
#include <sys/types.h>

#include "common.h"

#define _poll_set_rd(_pfd, _fd) do {    \
	(_pfd).fd = _fd;                \
	(_pfd).events = POLLIN;         \
	} while (0)

typedef int32_t slurm_fd;
typedef uint32_t slurm_uid;

/* SLURM Message types */
typedef enum {
	REQUEST_NODE_REGISTRATION_STATUS = 1001,
	MESSAGE_NODE_REGISTRATION_STATUS,
	REQUEST_RECONFIGURE,
	RESPONSE_RECONFIGURE,
	REQUEST_SHUTDOWN,
	REQUEST_SHUTDOWN_IMMEDIATE,
	RESPONSE_SHUTDOWN,
	REQUEST_PING,
	REQUEST_CONTROL,

	REQUEST_BUILD_INFO = 2001,
	RESPONSE_BUILD_INFO,
	REQUEST_JOB_INFO,
	RESPONSE_JOB_INFO,
	REQUEST_JOB_STEP_INFO,
	RESPONSE_JOB_STEP_INFO,
	REQUEST_NODE_INFO,
	RESPONSE_NODE_INFO,
	REQUEST_PARTITION_INFO,
	RESPONSE_PARTITION_INFO,
	REQUEST_ACCTING_INFO,
	RESPONSE_ACCOUNTING_INFO,
	REQUEST_JOB_ID,
	RESPONSE_JOB_ID,
	REQUEST_NODE_SELECT_INFO,
	RESPONSE_NODE_SELECT_INFO,

	REQUEST_UPDATE_JOB = 3001,
	REQUEST_UPDATE_NODE,
	REQUEST_UPDATE_PARTITION,
	REQUEST_DELETE_PARTITION,

	REQUEST_RESOURCE_ALLOCATION = 4001,
	RESPONSE_RESOURCE_ALLOCATION,
	REQUEST_SUBMIT_BATCH_JOB,
	RESPONSE_SUBMIT_BATCH_JOB,
	REQUEST_BATCH_JOB_LAUNCH,
	REQUEST_SIGNAL_JOB,
	RESPONSE_SIGNAL_JOB,
	REQUEST_CANCEL_JOB,
	RESPONSE_CANCEL_JOB,
	REQUEST_JOB_RESOURCE,
	RESPONSE_JOB_RESOURCE,
	REQUEST_JOB_ATTACH,
	RESPONSE_JOB_ATTACH,
	REQUEST_JOB_WILL_RUN,
	RESPONSE_JOB_WILL_RUN,
	REQUEST_ALLOCATION_AND_RUN_JOB_STEP,
	RESPONSE_ALLOCATION_AND_RUN_JOB_STEP,
	REQUEST_OLD_JOB_RESOURCE_ALLOCATION,
	REQUEST_UPDATE_JOB_TIME,
	REQUEST_JOB_READY,
	RESPONSE_JOB_READY,

	REQUEST_JOB_STEP_CREATE = 5001,
	RESPONSE_JOB_STEP_CREATE,
	REQUEST_RUN_JOB_STEP,
	RESPONSE_RUN_JOB_STEP,
	REQUEST_SIGNAL_JOB_STEP,
	RESPONSE_SIGNAL_JOB_STEP,
	REQUEST_CANCEL_JOB_STEP,
	RESPONSE_CANCEL_JOB_STEP,
	REQUEST_COMPLETE_JOB_STEP,
	RESPONSE_COMPLETE_JOB_STEP,
	REQUEST_CHECKPOINT,
	RESPONSE_CHECKPOINT,
	REQUEST_CHECKPOINT_COMP,
	RESPONSE_CHECKPOINT_COMP,

	REQUEST_LAUNCH_TASKS = 6001,
	RESPONSE_LAUNCH_TASKS,
	MESSAGE_TASK_EXIT,
	REQUEST_KILL_TASKS,
	REQUEST_REATTACH_TASKS,
	RESPONSE_REATTACH_TASKS,
	REQUEST_KILL_TIMELIMIT,
	REQUEST_KILL_JOB,
	MESSAGE_EPILOG_COMPLETE,
	REQUEST_SPAWN_TASK,

	SRUN_PING = 7001,
	SRUN_TIMEOUT,
	SRUN_NODE_FAIL,

	RESPONSE_SLURM_RC = 8001,
	MESSAGE_UPLOAD_ACCOUNTING_INFO,
	MESSAGE_JOBACCT_DATA,
} slurm_msg_type_t;

typedef struct slurm_msg {
	slurm_msg_type_t msg_type;
	slurm_addr address;
	slurm_fd conn_fd;
	void *cred;
	void *data;
	uint32_t data_size;
} slurm_msg_t;

typedef struct slurmctld_communication_addr {
	char *hostname;
	uint16_t port;
} slurmctld_comm_addr_t;

extern slurmctld_comm_addr_t slurmctld_comm_addr;

extern int msg_thr_create(forked_msg_t *forked_msg); 

/* out of common/slurm_protocol_api.h */
extern slurm_fd inline slurm_init_msg_engine_port(uint16_t port);
extern int inline slurm_get_stream_addr(
	slurm_fd open_fd, slurm_addr * address);
extern slurm_fd inline slurm_accept_msg_conn(slurm_fd open_fd,
				      slurm_addr * slurm_address);
extern int slurm_receive_msg(slurm_fd fd, slurm_msg_t *msg, int timeout);
extern int inline slurm_close_accepted_conn(slurm_fd open_fd);
extern int slurm_send_rc_msg(slurm_msg_t * request_msg, int rc);
extern void slurm_free_msg(slurm_msg_t * msg);

/* out of common/fd.h */
extern void fd_set_nonblocking(int fd);

/* out of common/read_config.h */
extern int getnodename (char *name, size_t len);

/* out of common/slurm_protocol_defs.h */
extern void inline slurm_free_launch_tasks_response_msg(
	void *msg);
extern void inline slurm_free_task_exit_msg(void * msg);
extern void inline slurm_free_reattach_tasks_response_msg(
	void * msg);
extern void inline slurm_free_srun_ping_msg(void * msg);
extern void inline slurm_free_srun_timeout_msg(void * msg);
extern void inline slurm_free_srun_node_fail_msg(void * msg);
extern int g_slurm_auth_destroy( void *cred );

#endif  /* _MSG_THREAD_H */

