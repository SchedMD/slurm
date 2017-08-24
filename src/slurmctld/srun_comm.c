/*****************************************************************************\
 *  srun_comm.c - srun communications
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#include <string.h>

#include "src/common/node_select.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

/* Launch the srun request. Note that retry is always zero since
 * we don't want to clog the system up with messages destined for
 * defunct srun processes
 */
static void _srun_agent_launch(slurm_addr_t *addr, char *host,
			       slurm_msg_type_t type, void *msg_args,
			       uint16_t protocol_version)
{
	agent_arg_t *agent_args = xmalloc(sizeof(agent_arg_t));

	agent_args->node_count = 1;
	agent_args->retry      = 0;
	agent_args->addr       = addr;
	agent_args->hostlist   = hostlist_create(host);
	agent_args->msg_type   = type;
	agent_args->msg_args   = msg_args;
	agent_args->protocol_version = protocol_version;

	agent_queue_request(agent_args);
}

/*
 * srun_allocate - notify srun of a resource allocation
 * IN job_id - id of the job allocated resource
 */
extern void srun_allocate (uint32_t job_id)
{
	struct job_record *job_ptr = find_job_record (job_id);
	int i;

	xassert(job_ptr);
	if (job_ptr && job_ptr->alloc_resp_port && job_ptr->alloc_node &&
	    job_ptr->resp_host && job_ptr->job_resrcs &&
	    job_ptr->job_resrcs->cpu_array_cnt) {
		slurm_addr_t * addr;
		resource_allocation_response_msg_t *msg_arg;
		job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;

		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->alloc_resp_port,
			job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(resource_allocation_response_msg_t));
		msg_arg->job_id 	= job_ptr->job_id;
		msg_arg->node_list	= xstrdup(job_ptr->nodes);
		msg_arg->partition	= xstrdup(job_ptr->partition);
		msg_arg->alias_list	= xstrdup(job_ptr->alias_list);
		msg_arg->num_cpu_groups	= job_resrcs_ptr->cpu_array_cnt;
		msg_arg->cpus_per_node  = xmalloc(sizeof(uint16_t) *
					  job_resrcs_ptr->cpu_array_cnt);
		if (job_ptr->details) {
			msg_arg->pn_min_memory = job_ptr->details->
						 pn_min_memory;
			msg_arg->cpu_freq_min = job_ptr->details->cpu_freq_min;
			msg_arg->cpu_freq_max = job_ptr->details->cpu_freq_max;
			msg_arg->cpu_freq_gov = job_ptr->details->cpu_freq_gov;
			if (job_ptr->details->env_cnt) {
				msg_arg->env_size = job_ptr->details->env_cnt;
				msg_arg->environment = xmalloc(
					sizeof(char *) * msg_arg->env_size);
				for (i = 0; i < msg_arg->env_size; i++) {
					msg_arg->environment[i] =
						xstrdup(job_ptr->details->
							env_sup[i]);
				}
			}
		}
		memcpy(msg_arg->cpus_per_node,
		       job_resrcs_ptr->cpu_array_value,
		       (sizeof(uint16_t) * job_resrcs_ptr->cpu_array_cnt));
		msg_arg->cpu_count_reps  = xmalloc(sizeof(uint32_t) *
					   job_resrcs_ptr->cpu_array_cnt);
		memcpy(msg_arg->cpu_count_reps,
		       job_resrcs_ptr->cpu_array_reps,
		       (sizeof(uint32_t) * job_resrcs_ptr->cpu_array_cnt));
		msg_arg->node_cnt	= job_ptr->node_cnt;
		msg_arg->select_jobinfo = select_g_select_jobinfo_copy(
				job_ptr->select_jobinfo);
		msg_arg->error_code	= SLURM_SUCCESS;

		_srun_agent_launch(addr, job_ptr->alloc_node,
				   RESPONSE_RESOURCE_ALLOCATION, msg_arg,
				   job_ptr->start_protocol_ver);
	}
}

/*
 * srun_allocate_abort - notify srun of a resource allocation failure
 * IN job_id - id of the job allocated resource
 */
extern void srun_allocate_abort(struct job_record *job_ptr)
{
	if (job_ptr && job_ptr->alloc_resp_port && job_ptr->alloc_node
	&&  job_ptr->resp_host) {
		slurm_addr_t * addr;
		srun_job_complete_msg_t *msg_arg;
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->alloc_resp_port,
			       job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_timeout_msg_t));
		msg_arg->job_id   = job_ptr->job_id;
		msg_arg->step_id  = NO_VAL;
		_srun_agent_launch(addr, job_ptr->alloc_node,
				   SRUN_JOB_COMPLETE,
				   msg_arg,
				   job_ptr->start_protocol_ver);
	}
}

/*
 * srun_node_fail - notify srun of a node's failure
 * IN job_id    - id of job to notify
 * IN node_name - name of failed node
 */
extern void srun_node_fail (uint32_t job_id, char *node_name)
{
#ifndef HAVE_FRONT_END
	struct node_record *node_ptr;
#endif
	struct job_record *job_ptr = find_job_record (job_id);
	int bit_position = -1;
	slurm_addr_t * addr;
	srun_node_fail_msg_t *msg_arg;
	ListIterator step_iterator;
	struct step_record *step_ptr;

	xassert(job_ptr);
	xassert(node_name);
	if (!job_ptr || !IS_JOB_RUNNING(job_ptr))
		return;

#ifdef HAVE_FRONT_END
	/* Purge all jobs steps in front end mode */
#else
	if (!node_name || (node_ptr = find_node_record(node_name)) == NULL)
		return;
	bit_position = node_ptr - node_record_table_ptr;
#endif

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next(step_iterator))) {
		if (step_ptr->step_node_bitmap == NULL)   /* pending step */
			continue;
		if ((bit_position >= 0) &&
		    (!bit_test(step_ptr->step_node_bitmap, bit_position)))
			continue;	/* job step not on this node */
		if ( (step_ptr->port    == 0)    ||
		     (step_ptr->host    == NULL) ||
		     (step_ptr->batch_step)      ||
		     (step_ptr->host[0] == '\0') )
			continue;
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, step_ptr->port, step_ptr->host);
		msg_arg = xmalloc(sizeof(srun_node_fail_msg_t));
		msg_arg->job_id   = job_ptr->job_id;
		msg_arg->step_id  = step_ptr->step_id;
		msg_arg->nodelist = xstrdup(node_name);
		_srun_agent_launch(addr, step_ptr->host, SRUN_NODE_FAIL,
				   msg_arg, step_ptr->start_protocol_ver);
	}
	list_iterator_destroy(step_iterator);

	if (job_ptr->other_port && job_ptr->alloc_node && job_ptr->resp_host) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_node_fail_msg_t));
		msg_arg->job_id   = job_id;
		msg_arg->step_id  = NO_VAL;
		msg_arg->nodelist = xstrdup(node_name);
		_srun_agent_launch(addr, job_ptr->alloc_node, SRUN_NODE_FAIL,
				   msg_arg, job_ptr->start_protocol_ver);
	}
}

/* srun_ping - ping all srun commands that have not been heard from recently */
extern void srun_ping (void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	slurm_addr_t * addr;
	time_t now = time(NULL);
	time_t old = now - (slurmctld_conf.inactive_limit / 3) +
			   slurmctld_conf.msg_timeout + 1;
	srun_ping_msg_t *msg_arg;

	if (slurmctld_conf.inactive_limit == 0)
		return;		/* No limit, don't bother pinging */

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);

		if (!IS_JOB_RUNNING(job_ptr))
			continue;

		if ((job_ptr->time_last_active <= old) && job_ptr->other_port
		    &&  job_ptr->alloc_node && job_ptr->resp_host) {
			addr = xmalloc(sizeof(struct sockaddr_in));
			slurm_set_addr(addr, job_ptr->other_port,
				job_ptr->resp_host);
			msg_arg = xmalloc(sizeof(srun_ping_msg_t));
			msg_arg->job_id  = job_ptr->job_id;
			msg_arg->step_id = NO_VAL;
			_srun_agent_launch(addr, job_ptr->alloc_node,
					   SRUN_PING, msg_arg,
					   job_ptr->start_protocol_ver);
		}
	}

	list_iterator_destroy(job_iterator);
}

/*
 * srun_step_timeout - notify srun of a job step's imminent timeout
 * IN step_ptr - pointer to the slurmctld step record
 * IN timeout_val - when it is going to time out
 */
extern void srun_step_timeout(struct step_record *step_ptr, time_t timeout_val)
{
	slurm_addr_t *addr;
	srun_timeout_msg_t *msg_arg;

	xassert(step_ptr);

	if (step_ptr->batch_step || !step_ptr->port
	    || !step_ptr->host || (step_ptr->host[0] == '\0'))
		return;

	addr = xmalloc(sizeof(struct sockaddr_in));
	slurm_set_addr(addr, step_ptr->port, step_ptr->host);
	msg_arg = xmalloc(sizeof(srun_timeout_msg_t));
	msg_arg->job_id   = step_ptr->job_ptr->job_id;
	msg_arg->step_id  = step_ptr->step_id;
	msg_arg->timeout  = timeout_val;
	_srun_agent_launch(addr, step_ptr->host, SRUN_TIMEOUT, msg_arg,
			   step_ptr->start_protocol_ver);
}

/*
 * srun_timeout - notify srun of a job's imminent timeout
 * IN job_ptr - pointer to the slurmctld job record
 */
extern void srun_timeout (struct job_record *job_ptr)
{
	slurm_addr_t * addr;
	srun_timeout_msg_t *msg_arg;
	ListIterator step_iterator;
	struct step_record *step_ptr;

	xassert(job_ptr);
	if (!IS_JOB_RUNNING(job_ptr))
		return;

	if (job_ptr->other_port && job_ptr->alloc_node && job_ptr->resp_host) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_timeout_msg_t));
		msg_arg->job_id   = job_ptr->job_id;
		msg_arg->step_id  = NO_VAL;
		msg_arg->timeout  = job_ptr->end_time;
		_srun_agent_launch(addr, job_ptr->alloc_node, SRUN_TIMEOUT,
				   msg_arg, job_ptr->start_protocol_ver);
	}


	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next(step_iterator)))
		srun_step_timeout(step_ptr, job_ptr->end_time);
	list_iterator_destroy(step_iterator);
}

/*
 * srun_user_message - Send arbitrary message to an srun job (no job steps)
 */
extern int srun_user_message(struct job_record *job_ptr, char *msg)
{
	slurm_addr_t * addr;
	srun_user_msg_t *msg_arg;

	xassert(job_ptr);
	if (!IS_JOB_PENDING(job_ptr) && !IS_JOB_RUNNING(job_ptr))
		return ESLURM_ALREADY_DONE;

	if (job_ptr->other_port &&
	    job_ptr->resp_host && job_ptr->resp_host[0]) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_user_msg_t));
		msg_arg->job_id = job_ptr->job_id;
		msg_arg->msg    = xstrdup(msg);
		_srun_agent_launch(addr, job_ptr->resp_host, SRUN_USER_MSG,
				   msg_arg, job_ptr->start_protocol_ver);
		return SLURM_SUCCESS;
	} else if (job_ptr->batch_flag && IS_JOB_RUNNING(job_ptr)) {
#ifndef HAVE_FRONT_END
		struct node_record *node_ptr;
#endif
		job_notify_msg_t *notify_msg_ptr;
		agent_arg_t *agent_arg_ptr;
#ifdef HAVE_FRONT_END
		if (job_ptr->batch_host == NULL)
			return ESLURM_DISABLED;	/* no allocated nodes */
		agent_arg_ptr = (agent_arg_t *) xmalloc(sizeof(agent_arg_t));
		agent_arg_ptr->hostlist = hostlist_create(job_ptr->batch_host);
		if (!agent_arg_ptr->hostlist)
			fatal("Invalid srun host: %s", job_ptr->batch_host);

		if (job_ptr->front_end_ptr)
			agent_arg_ptr->protocol_version =
				job_ptr->front_end_ptr->protocol_version;

#else
		node_ptr = find_first_node_record(job_ptr->node_bitmap);
		if (node_ptr == NULL)
			return ESLURM_DISABLED;	/* no allocated nodes */
		agent_arg_ptr = (agent_arg_t *) xmalloc(sizeof(agent_arg_t));
		agent_arg_ptr->hostlist = hostlist_create(node_ptr->name);
		agent_arg_ptr->protocol_version = node_ptr->protocol_version;
		if (!agent_arg_ptr->hostlist)
			fatal("Invalid srun host: %s", node_ptr->name);
#endif
		notify_msg_ptr = (job_notify_msg_t *)
				 xmalloc(sizeof(job_notify_msg_t));
		notify_msg_ptr->job_id = job_ptr->job_id;
		notify_msg_ptr->message = xstrdup(msg);
		agent_arg_ptr->node_count = 1;
		agent_arg_ptr->retry = 0;
		agent_arg_ptr->msg_type = REQUEST_JOB_NOTIFY;
		agent_arg_ptr->msg_args = (void *) notify_msg_ptr;
		/* Launch the RPC via agent */
		agent_queue_request(agent_arg_ptr);
		return SLURM_SUCCESS;
	}
	return ESLURM_DISABLED;
}

/*
 * srun_job_complete - notify srun of a job's termination
 * IN job_ptr - pointer to the slurmctld job record
 */
extern void srun_job_complete (struct job_record *job_ptr)
{
	slurm_addr_t * addr;
	srun_job_complete_msg_t *msg_arg;
	ListIterator step_iterator;
	struct step_record *step_ptr;

	xassert(job_ptr);

	if (job_ptr->other_port && job_ptr->alloc_node && job_ptr->resp_host) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_job_complete_msg_t));
		msg_arg->job_id   = job_ptr->job_id;
		msg_arg->step_id  = NO_VAL;
		_srun_agent_launch(addr, job_ptr->alloc_node,
				   SRUN_JOB_COMPLETE, msg_arg,
				   job_ptr->start_protocol_ver);
	}

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next(step_iterator))) {
		if (step_ptr->batch_step)	/* batch script itself */
			continue;
		srun_step_complete(step_ptr);
	}
	list_iterator_destroy(step_iterator);
}

/*
 * srun_job_suspend - notify salloc of suspend/resume operation
 * IN job_ptr - pointer to the slurmctld job record
 * IN op - SUSPEND_JOB or RESUME_JOB (enum suspend_opts from slurm.h)
 * RET - true if message send, otherwise false
 */
extern bool srun_job_suspend (struct job_record *job_ptr, uint16_t op)
{
	slurm_addr_t * addr;
	suspend_msg_t *msg_arg;
	bool msg_sent = false;

	xassert(job_ptr);

	if (job_ptr->other_port && job_ptr->alloc_node && job_ptr->resp_host) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(suspend_msg_t));
		msg_arg->job_id  = job_ptr->job_id;
		msg_arg->op     = op;
		_srun_agent_launch(addr, job_ptr->alloc_node,
				   SRUN_REQUEST_SUSPEND, msg_arg,
				   job_ptr->start_protocol_ver);
		msg_sent = true;
	}
	return msg_sent;
}

/*
 * srun_step_complete - notify srun of a job step's termination
 * IN step_ptr - pointer to the slurmctld job step record
 */
extern void srun_step_complete (struct step_record *step_ptr)
{
	slurm_addr_t * addr;
	srun_job_complete_msg_t *msg_arg;

	xassert(step_ptr);
	if (step_ptr->port && step_ptr->host && step_ptr->host[0]) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, step_ptr->port, step_ptr->host);
		msg_arg = xmalloc(sizeof(srun_job_complete_msg_t));
		msg_arg->job_id   = step_ptr->job_ptr->job_id;
		msg_arg->step_id  = step_ptr->step_id;
		_srun_agent_launch(addr, step_ptr->host, SRUN_JOB_COMPLETE,
				   msg_arg, step_ptr->start_protocol_ver);
	}
}

/*
 * srun_step_missing - notify srun that a job step is missing from
 *		       a node we expect to find it on
 * IN step_ptr  - pointer to the slurmctld job step record
 * IN node_list - name of nodes we did not find the step on
 */
extern void srun_step_missing (struct step_record *step_ptr,
			       char *node_list)
{
	slurm_addr_t * addr;
	srun_step_missing_msg_t *msg_arg;

	xassert(step_ptr);
	if (step_ptr->port && step_ptr->host && step_ptr->host[0]) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, step_ptr->port, step_ptr->host);
		msg_arg = xmalloc(sizeof(srun_step_missing_msg_t));
		msg_arg->job_id   = step_ptr->job_ptr->job_id;
		msg_arg->step_id  = step_ptr->step_id;
		msg_arg->nodelist = xstrdup(node_list);
		_srun_agent_launch(addr, step_ptr->host, SRUN_STEP_MISSING,
				   msg_arg, step_ptr->start_protocol_ver);
	}
}

/*
 * srun_step_signal - notify srun that a job step should be signalled
 * NOTE: Needed on BlueGene/Q to signal runjob process
 * IN step_ptr  - pointer to the slurmctld job step record
 * IN signal - signal number
 */
extern void srun_step_signal (struct step_record *step_ptr, uint16_t signal)
{
	slurm_addr_t * addr;
	job_step_kill_msg_t *msg_arg;

	xassert(step_ptr);
	if (step_ptr->port && step_ptr->host && step_ptr->host[0]) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, step_ptr->port, step_ptr->host);
		msg_arg = xmalloc(sizeof(job_step_kill_msg_t));
		msg_arg->job_id      = step_ptr->job_ptr->job_id;
		msg_arg->job_step_id = step_ptr->step_id;
		msg_arg->signal      = signal;
		_srun_agent_launch(addr, step_ptr->host, SRUN_STEP_SIGNAL,
				   msg_arg, step_ptr->start_protocol_ver);
	}
}

/*
 * srun_exec - request that srun execute a specific command
 *	and route it's output to stdout
 * IN step_ptr - pointer to the slurmctld job step record
 * IN argv - command and arguments to execute
 */
extern void srun_exec(struct step_record *step_ptr, char **argv)
{
	slurm_addr_t * addr;
	srun_exec_msg_t *msg_arg;
	int cnt = 1, i;

	xassert(step_ptr);

	if (step_ptr->port && step_ptr->host && step_ptr->host[0]) {
		for (i=0; argv[i]; i++)
			cnt++;	/* start at 1 to include trailing NULL */
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, step_ptr->port, step_ptr->host);
		msg_arg = xmalloc(sizeof(srun_exec_msg_t));
		msg_arg->job_id  = step_ptr->job_ptr->job_id;
		msg_arg->step_id = step_ptr->step_id;
		msg_arg->argc    = cnt;
		msg_arg->argv    = xmalloc(sizeof(char *) * cnt);
		for (i=0; i<cnt ; i++)
			msg_arg->argv[i] = xstrdup(argv[i]);
		_srun_agent_launch(addr, step_ptr->host, SRUN_EXEC,
				   msg_arg, step_ptr->start_protocol_ver);
	} else {
		error("srun_exec %u.%u lacks communication channel",
			step_ptr->job_ptr->job_id, step_ptr->step_id);
	}
}

/*
 * srun_response - note that srun has responded
 * IN job_id  - id of job responding
 * IN step_id - id of step responding or NO_VAL if not a step
 */
extern void srun_response(uint32_t job_id, uint32_t step_id)
{
	struct job_record  *job_ptr = find_job_record (job_id);
	time_t now = time(NULL);

	if (job_ptr == NULL)
		return;
	job_ptr->time_last_active = now;
}
