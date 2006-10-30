/*****************************************************************************\
 * src/common/global_srun.c - functions needed by more than just srun
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>, and
 *             Morris Jette  <jette1@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_PTHREAD
#include <pthread.h>
#endif

#include <signal.h>
#include <string.h>

#include <slurm/slurm_errno.h>
#include <stdlib.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsignal.h"
#include "src/common/forward.h"
#include "src/common/global_srun.h"

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        pthread_attr_t	attr;			/* thread attributes */
        state_t		state;      		/* thread state */
} thd_t;

int message_thread = 0;

void 
fwd_signal(srun_job_t *job, int signo, int max_threads)
{
	int i;
	slurm_msg_t req;
	kill_tasks_msg_t msg;
	static pthread_mutex_t sig_mutex = PTHREAD_MUTEX_INITIALIZER;
	pipe_enum_t pipe_enum = PIPE_SIGNALED;
	hostlist_t hl;
	char *name = NULL;
	char buf[8192];
	List ret_list = NULL;
	ListIterator itr;
	ret_data_info_t *ret_data_info = NULL;
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&sig_mutex);

	if (signo == SIGKILL || signo == SIGINT || signo == SIGTERM) {
		slurm_mutex_lock(&job->state_mutex);
		job->signaled = true;
		slurm_mutex_unlock(&job->state_mutex);
		if(message_thread) {
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &job->signaled,sizeof(int));
		}
	}

	debug2("forward signal %d to job", signo);

	/* common to all tasks */
	msg.job_id      = job->jobid;
	msg.job_step_id = job->stepid;
	msg.signal      = (uint32_t) signo;
	
	hl = hostlist_create("");
	for (i = 0; i < job->nhosts; i++) {
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			name = nodelist_nth_host(
				job->step_layout->node_list, i);
			debug2("%s has not yet replied\n", name);
			free(name);
			continue;
		}
		if (job_active_tasks_on_host(job, i) == 0)
			continue;
		name = nodelist_nth_host(job->step_layout->node_list, i);
		hostlist_push(hl, name);
		free(name);
	}
	if(!hostlist_count(hl)) {
		hostlist_destroy(hl);
		goto nothing_left;
	}
	hostlist_ranged_string(hl, sizeof(buf), buf);
	hostlist_destroy(hl);
	name = xstrdup(buf);

	slurm_msg_t_init(&req);	
	req.msg_type = REQUEST_SIGNAL_TASKS;
	req.data     = &msg;
	
	debug3("sending signal to host %s", name);
	
	if (!(ret_list = slurm_send_recv_msgs(name, &req, 0))) { 
		error("fwd_signal: slurm_send_recv_msgs really failed bad");
		xfree(name);
		slurm_mutex_unlock(&sig_mutex);
		return;
	}
	xfree(name);
	itr = list_iterator_create(ret_list);		
	while((ret_data_info = list_next(itr))) {
		rc = slurm_get_return_code(ret_data_info->type, 
					   ret_data_info->data);
		/*
		 *  Report error unless it is "Invalid job id" which 
		 *    probably just means the tasks exited in the meanwhile.
		 */
		if ((rc != 0) && (rc != ESLURM_INVALID_JOB_ID)
		    &&  (rc != ESLURMD_JOB_NOTRUNNING) && (rc != ESRCH)) {
			error("%s: signal: %s", 
			      ret_data_info->node_name, 
			      slurm_strerror(rc));
			destroy_data_info(ret_data_info);
		}
	}
	list_iterator_destroy(itr);
	list_destroy(ret_list);
nothing_left:
	debug2("All tasks have been signalled");
	
	slurm_mutex_unlock(&sig_mutex);
}

int
job_active_tasks_on_host(srun_job_t *job, int hostid)
{
	int i;
	int retval = 0;

	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->step_layout->tasks[hostid]; i++) {
		uint32_t *tids = job->step_layout->tids[hostid];
		xassert(tids != NULL);
		debug("Task %d state: %d", tids[i], job->task_state[tids[i]]);
		if (job->task_state[tids[i]] == SRUN_TASK_RUNNING) 
			retval++;
	}
	slurm_mutex_unlock(&job->task_mutex);
	return retval;
}


