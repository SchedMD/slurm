/*****************************************************************************\
 *  srun_comm.c - srun communications
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "config.h"

#include <string.h>

#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/select.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

/* Launch the srun request. Note that retry is always zero since
 * we don't want to clog the system up with messages destined for
 * defunct srun processes
 */
static void _srun_agent_launch(slurm_addr_t *addr, char *host,
			       slurm_msg_type_t type, void *msg_args,
			       uid_t r_uid, uint16_t protocol_version)
{
	agent_arg_t *agent_args = xmalloc(sizeof(agent_arg_t));

	agent_args->node_count = 1;
	agent_args->retry      = 0;
	agent_args->addr       = addr;
	agent_args->hostlist   = hostlist_create(host);
	agent_args->msg_type   = type;
	agent_args->msg_args   = msg_args;
	set_agent_arg_r_uid(agent_args, r_uid);
	agent_args->protocol_version = protocol_version;

	agent_queue_request(agent_args);
}

static bool _pending_het_jobs(job_record_t *job_ptr)
{
	job_record_t *het_job_leader, *het_job;
	ListIterator iter;
	bool pending_job = false;

	if (job_ptr->het_job_id == 0)
		return false;

	het_job_leader = find_job_record(job_ptr->het_job_id);
	if (!het_job_leader) {
		error("Hetjob leader %pJ not found", job_ptr);
		return false;
	}
	if (!het_job_leader->het_job_list) {
		error("Hetjob leader %pJ lacks het_job_list",
		      job_ptr);
		return false;
	}

	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		if (het_job_leader->het_job_id != het_job->het_job_id) {
			error("%s: Bad het_job_list for %pJ",
			      __func__, het_job_leader);
			continue;
		}
		if (IS_JOB_PENDING(het_job)) {
			pending_job = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	return pending_job;
}

static void _free_srun_alloc(void *x)
{
	resource_allocation_response_msg_t *alloc_msg;

	alloc_msg = (resource_allocation_response_msg_t *) x;
	/* NULL working_cluster_rec because it's pointing to global memory */
	alloc_msg->working_cluster_rec = NULL;
	slurm_free_resource_allocation_response_msg(alloc_msg);
}

/*
 * srun_allocate - notify srun of a resource allocation
 * IN job_ptr - job allocated resources
 */
extern void srun_allocate(job_record_t *job_ptr)
{
	job_record_t *het_job, *het_job_leader;
	resource_allocation_response_msg_t *msg_arg = NULL;
	slurm_addr_t *addr;
	ListIterator iter;
	List job_resp_list = NULL;

	xassert(job_ptr);
	if (!job_ptr || !job_ptr->alloc_resp_port || !job_ptr->alloc_node ||
	    !job_ptr->resp_host || !job_ptr->job_resrcs ||
	    !job_ptr->job_resrcs->cpu_array_cnt)
		return;

	if (job_ptr->het_job_id == 0) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, job_ptr->alloc_resp_port,
			job_ptr->resp_host);

		msg_arg = build_alloc_msg(job_ptr, SLURM_SUCCESS, NULL);
		_srun_agent_launch(addr, job_ptr->alloc_node,
				   RESPONSE_RESOURCE_ALLOCATION, msg_arg,
				   job_ptr->user_id,
				   job_ptr->start_protocol_ver);
	} else if (_pending_het_jobs(job_ptr)) {
		return;
	} else if ((het_job_leader = find_job_record(job_ptr->het_job_id))) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, het_job_leader->alloc_resp_port,
			       het_job_leader->resp_host);
		job_resp_list = list_create(_free_srun_alloc);
		iter = list_iterator_create(het_job_leader->het_job_list);
		while ((het_job = list_next(iter))) {
			if (het_job_leader->het_job_id !=
				het_job->het_job_id) {
				error("%s: Bad het_job_list for %pJ",
				      __func__, het_job_leader);
				continue;
			}
			msg_arg = build_alloc_msg(het_job, SLURM_SUCCESS,
						  NULL);
			list_append(job_resp_list, msg_arg);
			msg_arg = NULL;
		}
		list_iterator_destroy(iter);
		_srun_agent_launch(addr, job_ptr->alloc_node,
				   RESPONSE_HET_JOB_ALLOCATION, job_resp_list,
				   job_ptr->user_id,
				   job_ptr->start_protocol_ver);
	} else {
		error("%s: Can not find hetjob leader %pJ",
		      __func__, job_ptr);
	}
}

/*
 * srun_allocate_abort - notify srun of a resource allocation failure
 * IN job_ptr - job allocated resources
 */
extern void srun_allocate_abort(job_record_t *job_ptr)
{
	if (job_ptr && job_ptr->alloc_resp_port && job_ptr->alloc_node &&
	    job_ptr->resp_host) {
		slurm_addr_t * addr;
		srun_job_complete_msg_t *msg_arg;
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, job_ptr->alloc_resp_port,
			       job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_timeout_msg_t));
		msg_arg->job_id = job_ptr->job_id;
		msg_arg->step_id = NO_VAL;
		msg_arg->step_het_comp = NO_VAL;
		_srun_agent_launch(addr, job_ptr->alloc_node,
				   SRUN_JOB_COMPLETE,
				   msg_arg, job_ptr->user_id,
				   job_ptr->start_protocol_ver);
	}
}

typedef struct {
	int bit_position;
	char *node_name;
} srun_node_fail_args_t;

static int _srun_node_fail(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	srun_node_fail_args_t *args = (srun_node_fail_args_t *) arg;
	slurm_addr_t *addr;
	char *nodeaddr;
	srun_node_fail_msg_t *msg_arg;

	if (!step_ptr->step_node_bitmap)   /* pending step */
		return 0;
	if (step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT)
		return 0;
	if ((args->bit_position >= 0) &&
	    (!bit_test(step_ptr->step_node_bitmap, args->bit_position)))
		return 0;	/* job step not on this node */
	if (!step_ptr->port || !step_ptr->host || (step_ptr->host[0] == '\0'))
		return 0;

	addr = xmalloc(sizeof(slurm_addr_t));
	nodeaddr = slurm_conf_get_nodeaddr(step_ptr->host);
	slurm_set_addr(addr, step_ptr->port, nodeaddr);
	xfree(nodeaddr);

	msg_arg = xmalloc(sizeof(*msg_arg));
	memcpy(&msg_arg->step_id, &step_ptr->step_id, sizeof(msg_arg->step_id));
	msg_arg->nodelist = xstrdup(args->node_name);
	_srun_agent_launch(addr, step_ptr->host, SRUN_NODE_FAIL,
			   msg_arg, step_ptr->job_ptr->user_id,
			   step_ptr->start_protocol_ver);

	return 0;
}

/*
 * srun_node_fail - notify srun of a node's failure
 * IN job_ptr - job to notify
 * IN node_name - name of failed node
 */
extern void srun_node_fail(job_record_t *job_ptr, char *node_name)
{
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
#endif
	srun_node_fail_args_t args = {
		.bit_position = -1,
		.node_name = node_name,
	};
	slurm_addr_t * addr;
	srun_node_fail_msg_t *msg_arg;

	xassert(job_ptr);
	xassert(node_name);
	if (!job_ptr || !IS_JOB_RUNNING(job_ptr))
		return;

#ifdef HAVE_FRONT_END
	/* Purge all jobs steps in front end mode */
#else
	if (!node_name || (node_ptr = find_node_record(node_name)) == NULL)
		return;
	args.bit_position = node_ptr->index;
#endif

	list_for_each(job_ptr->step_list, _srun_node_fail, &args);

	if (job_ptr->other_port && job_ptr->alloc_node && job_ptr->resp_host) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_node_fail_msg_t));
		msg_arg->step_id.job_id = job_ptr->job_id;
		msg_arg->step_id.step_id  = NO_VAL;
		msg_arg->step_id.step_het_comp = NO_VAL;
		msg_arg->nodelist = xstrdup(node_name);
		_srun_agent_launch(addr, job_ptr->alloc_node, SRUN_NODE_FAIL,
				   msg_arg, job_ptr->user_id,
				   job_ptr->start_protocol_ver);
	}
}

static int _srun_ping(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	time_t *old = (time_t *) arg;
	slurm_addr_t *addr;
	srun_ping_msg_t *msg_arg;

	xassert(job_ptr->magic == JOB_MAGIC);

	if (!IS_JOB_RUNNING(job_ptr) || (job_ptr->time_last_active > *old))
		return 0;

	if (!job_ptr->other_port || !job_ptr->alloc_node || !job_ptr->resp_host)
		return 0;

	addr = xmalloc(sizeof(*addr));
	msg_arg = xmalloc(sizeof(*msg_arg));

	slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
	msg_arg->job_id = job_ptr->job_id;

	_srun_agent_launch(addr, job_ptr->alloc_node, SRUN_PING, msg_arg,
			   job_ptr->user_id, job_ptr->start_protocol_ver);

	return 0;
}

/*
 * srun_ping - Ping all allocations srun/salloc that have not been heard from
 * recently. This does not ping sruns inside a allocation from sbatch or salloc.
 */
extern void srun_ping (void)
{
	time_t old = time(NULL) - (slurm_conf.inactive_limit / 3) +
		     slurm_conf.msg_timeout + 1;

	if (slurm_conf.inactive_limit == 0)
		return;		/* No limit, don't bother pinging */

	list_for_each_ro(job_list, _srun_ping, &old);
}

static int _srun_step_timeout(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	slurm_addr_t *addr;
	char *nodeaddr;
	srun_timeout_msg_t *msg_arg;

	xassert(step_ptr);

	if (step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT)
		return 0;

	if (!step_ptr->port || !step_ptr->host || (step_ptr->host[0] == '\0'))
		return 0;

	addr = xmalloc(sizeof(*addr));
	msg_arg = xmalloc(sizeof(*msg_arg));

	nodeaddr = slurm_conf_get_nodeaddr(step_ptr->host);
	slurm_set_addr(addr, step_ptr->port, nodeaddr);
	xfree(nodeaddr);

	memcpy(&msg_arg->step_id, &step_ptr->step_id, sizeof(msg_arg->step_id));
	msg_arg->timeout = step_ptr->job_ptr->end_time;

	_srun_agent_launch(addr, step_ptr->host, SRUN_TIMEOUT, msg_arg,
			   step_ptr->job_ptr->user_id,
			   step_ptr->start_protocol_ver);

	return 0;
}

/*
 * srun_timeout - notify srun of a job's imminent timeout
 * IN job_ptr - pointer to the slurmctld job record
 */
extern void srun_timeout(job_record_t *job_ptr)
{
	slurm_addr_t * addr;
	srun_timeout_msg_t *msg_arg;

	xassert(job_ptr);
	if (!IS_JOB_RUNNING(job_ptr))
		return;

	if (job_ptr->other_port && job_ptr->alloc_node && job_ptr->resp_host) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_timeout_msg_t));
		msg_arg->step_id.job_id   = job_ptr->job_id;
		msg_arg->step_id.step_id  = NO_VAL;
		msg_arg->step_id.step_het_comp = NO_VAL;
		msg_arg->timeout  = job_ptr->end_time;
		_srun_agent_launch(addr, job_ptr->alloc_node, SRUN_TIMEOUT,
				   msg_arg, job_ptr->user_id,
				   job_ptr->start_protocol_ver);
	}

	list_for_each(job_ptr->step_list, _srun_step_timeout, NULL);
}

/*
 * srun_user_message - Send arbitrary message to an srun job (no job steps)
 */
extern int srun_user_message(job_record_t *job_ptr, char *msg)
{
	slurm_addr_t * addr;
	srun_user_msg_t *msg_arg;

	xassert(job_ptr);
	if (!IS_JOB_PENDING(job_ptr) && !IS_JOB_RUNNING(job_ptr))
		return ESLURM_ALREADY_DONE;

	if (job_ptr->other_port &&
	    job_ptr->resp_host && job_ptr->resp_host[0]) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_user_msg_t));
		msg_arg->job_id = job_ptr->job_id;
		msg_arg->msg    = xstrdup(msg);
		_srun_agent_launch(addr, job_ptr->resp_host, SRUN_USER_MSG,
				   msg_arg, job_ptr->user_id,
				   job_ptr->start_protocol_ver);
		return SLURM_SUCCESS;
	} else if (job_ptr->batch_flag && IS_JOB_RUNNING(job_ptr)) {
#ifndef HAVE_FRONT_END
		node_record_t *node_ptr;
#endif
		job_notify_msg_t *notify_msg_ptr;
		agent_arg_t *agent_arg_ptr;
#ifdef HAVE_FRONT_END
		if (job_ptr->batch_host == NULL)
			return ESLURM_DISABLED;	/* no allocated nodes */
		agent_arg_ptr = xmalloc(sizeof(agent_arg_t));
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
		agent_arg_ptr = xmalloc(sizeof(agent_arg_t));
		agent_arg_ptr->hostlist = hostlist_create(node_ptr->name);
		agent_arg_ptr->protocol_version = node_ptr->protocol_version;
		if (!agent_arg_ptr->hostlist)
			fatal("Invalid srun host: %s", node_ptr->name);
#endif
		notify_msg_ptr = (job_notify_msg_t *)
				 xmalloc(sizeof(job_notify_msg_t));
		notify_msg_ptr->step_id.job_id = job_ptr->job_id;
		notify_msg_ptr->step_id.step_id = NO_VAL;
		notify_msg_ptr->step_id.step_het_comp = NO_VAL;
		notify_msg_ptr->message = xstrdup(msg);
		agent_arg_ptr->node_count = 1;
		agent_arg_ptr->retry = 0;
		agent_arg_ptr->msg_type = REQUEST_JOB_NOTIFY;
		agent_arg_ptr->msg_args = (void *) notify_msg_ptr;
		/* Launch the RPC via agent */
		set_agent_arg_r_uid(agent_arg_ptr, SLURM_AUTH_UID_ANY);
		agent_queue_request(agent_arg_ptr);
		return SLURM_SUCCESS;
	}
	return ESLURM_DISABLED;
}

static int _srun_job_complete(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;

	if (step_ptr->step_id.step_id != SLURM_BATCH_SCRIPT)
		srun_step_complete(step_ptr);

	return 0;
}

/*
 * srun_job_complete - notify srun of a job's termination
 * IN job_ptr - pointer to the slurmctld job record
 */
extern void srun_job_complete(job_record_t *job_ptr)
{
	slurm_addr_t * addr;
	srun_job_complete_msg_t *msg_arg;

	xassert(job_ptr);

	if (job_ptr->other_port && job_ptr->alloc_node && job_ptr->resp_host) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(srun_job_complete_msg_t));
		msg_arg->job_id = job_ptr->job_id;
		msg_arg->step_id = NO_VAL;
		msg_arg->step_het_comp = NO_VAL;
		_srun_agent_launch(addr, job_ptr->alloc_node,
				   SRUN_JOB_COMPLETE, msg_arg,
				   job_ptr->user_id,
				   job_ptr->start_protocol_ver);
	}

	list_for_each(job_ptr->step_list, _srun_job_complete, NULL);
}

/*
 * srun_job_suspend - notify salloc of suspend/resume operation
 * IN job_ptr - pointer to the slurmctld job record
 * IN op - SUSPEND_JOB or RESUME_JOB (enum suspend_opts from slurm.h)
 * RET - true if message send, otherwise false
 */
extern bool srun_job_suspend(job_record_t *job_ptr, uint16_t op)
{
	slurm_addr_t * addr;
	suspend_msg_t *msg_arg;
	bool msg_sent = false;

	xassert(job_ptr);

	if (job_ptr->other_port && job_ptr->alloc_node && job_ptr->resp_host) {
		addr = xmalloc(sizeof(slurm_addr_t));
		slurm_set_addr(addr, job_ptr->other_port, job_ptr->resp_host);
		msg_arg = xmalloc(sizeof(suspend_msg_t));
		msg_arg->job_id  = job_ptr->job_id;
		msg_arg->op     = op;
		_srun_agent_launch(addr, job_ptr->alloc_node,
				   SRUN_REQUEST_SUSPEND, msg_arg,
				   job_ptr->user_id,
				   job_ptr->start_protocol_ver);
		msg_sent = true;
	}
	return msg_sent;
}

/*
 * srun_step_complete - notify srun of a job step's termination
 * IN step_ptr - pointer to the slurmctld job step record
 */
extern void srun_step_complete(step_record_t *step_ptr)
{
	slurm_addr_t * addr;
	srun_job_complete_msg_t *msg_arg;

	xassert(step_ptr);
	if (step_ptr->port && step_ptr->host && step_ptr->host[0]) {
		char *nodeaddr;
		addr = xmalloc(sizeof(slurm_addr_t));
		nodeaddr = slurm_conf_get_nodeaddr(step_ptr->host);
		slurm_set_addr(addr, step_ptr->port, nodeaddr);
		xfree(nodeaddr);

		msg_arg = xmalloc(sizeof(srun_job_complete_msg_t));
		memcpy(&msg_arg->step_id, &step_ptr->step_id,
		       sizeof(msg_arg->step_id));
		_srun_agent_launch(addr, step_ptr->host, SRUN_JOB_COMPLETE,
				   msg_arg, step_ptr->job_ptr->user_id,
				   step_ptr->start_protocol_ver);
	}
}

/*
 * srun_step_missing - notify srun that a job step is missing from
 *		       a node we expect to find it on
 * IN step_ptr  - pointer to the slurmctld job step record
 * IN node_list - name of nodes we did not find the step on
 */
extern void srun_step_missing(step_record_t *step_ptr, char *node_list)
{
	slurm_addr_t * addr;
	srun_step_missing_msg_t *msg_arg;

	xassert(step_ptr);
	if (step_ptr->port && step_ptr->host && step_ptr->host[0]) {
		char *nodeaddr;
		addr = xmalloc(sizeof(slurm_addr_t));
		nodeaddr = slurm_conf_get_nodeaddr(step_ptr->host);
		slurm_set_addr(addr, step_ptr->port, nodeaddr);
		xfree(nodeaddr);

		msg_arg = xmalloc(sizeof(srun_step_missing_msg_t));
		memcpy(&msg_arg->step_id, &step_ptr->step_id,
		       sizeof(msg_arg->step_id));
		msg_arg->nodelist = xstrdup(node_list);
		_srun_agent_launch(addr, step_ptr->host, SRUN_STEP_MISSING,
				   msg_arg, step_ptr->job_ptr->user_id,
				   step_ptr->start_protocol_ver);
	}
}

/*
 * srun_step_signal - notify srun that a job step should be signaled
 * NOTE: Needed on BlueGene/Q to signal runjob process
 * IN step_ptr  - pointer to the slurmctld job step record
 * IN signal - signal number
 */
extern void srun_step_signal(step_record_t *step_ptr, uint16_t signal)
{
	slurm_addr_t * addr;
	job_step_kill_msg_t *msg_arg;

	xassert(step_ptr);
	if (step_ptr->port && step_ptr->host && step_ptr->host[0]) {
		char *nodeaddr;
		addr = xmalloc(sizeof(slurm_addr_t));
		nodeaddr = slurm_conf_get_nodeaddr(step_ptr->host);
		slurm_set_addr(addr, step_ptr->port, nodeaddr);
		xfree(nodeaddr);

		msg_arg = xmalloc(sizeof(job_step_kill_msg_t));
		memcpy(&msg_arg->step_id, &step_ptr->step_id,
		       sizeof(msg_arg->step_id));
		msg_arg->signal      = signal;
		_srun_agent_launch(addr, step_ptr->host, SRUN_STEP_SIGNAL,
				   msg_arg, step_ptr->job_ptr->user_id,
				   step_ptr->start_protocol_ver);
	}
}

/*
 * srun_response - note that srun has responded
 * IN step_id - id of step responding or NO_VAL if not a step
 */
extern void srun_response(slurm_step_id_t *step_id)
{
	job_record_t *job_ptr = find_job_record(step_id->job_id);
	step_record_t *step_ptr;
	time_t now = time(NULL);

	if (job_ptr == NULL)
		return;
	job_ptr->time_last_active = now;

	if (step_id->step_id == NO_VAL)
		return;

	if ((step_ptr = find_step_record(job_ptr, step_id)))
		step_ptr->time_last_active = now;

}
