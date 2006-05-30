/*****************************************************************************\
 *  signal.c - Send a signal to a slurm job or job step
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/xmalloc.h"
#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"

#define MAX_THREADS 50

static int _signal_job_step(const job_step_info_t *step,
			    const resource_allocation_response_msg_t *allocation,
			    uint16_t signal);
static int _signal_batch_script_step(
	const resource_allocation_response_msg_t *allocation, uint16_t signal);
static int _terminate_job_step(const job_step_info_t *step,
		       const resource_allocation_response_msg_t *allocation);
static int _terminate_batch_script_step(
	const resource_allocation_response_msg_t *allocation);
static int _p_send_recv_rc_msg(int num_nodes, slurm_msg_t msg[],
			       int rc[], int timeout);
static void *_thr_send_recv_rc_msg(void *args);
struct send_recv_rc {
	slurm_msg_t *msg;
	int *rc;
	int timeout;
	pthread_mutex_t *lock;
	pthread_cond_t *cond;
	int *active;
};

/*
 * slurm_signal_job - send the specified signal to all steps of an existing job
 * IN job_id     - the job's id
 * IN signal     - signal number
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int 
slurm_signal_job (uint32_t job_id, uint16_t signal)
{
	int rc = SLURM_SUCCESS;
	resource_allocation_response_msg_t *alloc_info;
	slurm_msg_t *msg; /* array of message structs, one per node */
	signal_job_msg_t rpc;
	int *rc_array;
	int i;

	if (slurm_allocation_lookup(job_id, &alloc_info)) {
		rc = slurm_get_errno(); 
		goto fail1;
	}

	/* same remote procedure call for each node */
	rpc.job_id = job_id;
	rpc.signal = (uint32_t)signal;

        msg = xmalloc(sizeof(slurm_msg_t) * alloc_info->node_cnt);
	rc_array = xmalloc(sizeof(int) * alloc_info->node_cnt);
	for (i = 0; i < alloc_info->node_cnt; i++) {
		msg[i].msg_type = REQUEST_SIGNAL_JOB;
		msg[i].data = &rpc;
		msg[i].address = alloc_info->node_addr[i];
	}

	_p_send_recv_rc_msg(alloc_info->node_cnt, msg, rc_array, 10);
	
	for (i = 0; i < alloc_info->node_cnt; i++) {
		if (rc_array[i]) {
			rc = rc_array[i];
			break;
		}
	}

	xfree(msg);
	xfree(rc_array);
	slurm_free_resource_allocation_response_msg(alloc_info);
fail1:
	if (rc) {
		slurm_seterrno_ret(rc);
		return SLURM_FAILURE;
	} else {
		return SLURM_SUCCESS;
	}
}

/*
 * slurm_signal_job_step - send the specified signal to an existing job step
 * IN job_id  - the job's id
 * IN step_id - the job step's id - use SLURM_BATCH_SCRIPT as the step_id
 *              to send a signal to a job's batch script
 * IN signal  - signal number
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int 
slurm_signal_job_step (uint32_t job_id, uint32_t step_id, uint16_t signal)
{
	resource_allocation_response_msg_t *alloc_info;
	job_step_info_response_msg_t *step_info;
	int rc;
	int i;
	int save_errno = 0;

	if (slurm_allocation_lookup(job_id, &alloc_info)) {
		return -1;
	}

	/*
	 * The controller won't give us info about the batch script job step,
	 * so we need to handle that seperately.
	 */
	if (step_id == SLURM_BATCH_SCRIPT) {
		rc = _signal_batch_script_step(alloc_info, signal);
		slurm_free_resource_allocation_response_msg(alloc_info);
		errno = rc;
		return rc ? -1 : 0;
	}

	/*
	 * Otherwise, look through the list of job step info and find
	 * the one matching step_id.  Signal that step.
	 */
	rc = slurm_get_job_steps((time_t)0, job_id, step_id, 
				 &step_info, SHOW_ALL);
 	if (rc != 0) {
 		save_errno = errno;
 		goto fail;
 	}
	for (i = 0; i < step_info->job_step_count; i++) {
		if (step_info->job_steps[i].job_id == job_id
		    && step_info->job_steps[i].step_id == step_id) {
 			rc = _signal_job_step(&step_info->job_steps[i],
 					      alloc_info, signal);
 			save_errno = errno;
			break;
		}
	}
	slurm_free_job_step_info_response_msg(step_info);
fail:
	slurm_free_resource_allocation_response_msg(alloc_info);
 	errno = save_errno;
 	return rc ? -1 : 0;
}

/*
 * Retrieve the host address from the "allocation" structure for each
 * node in the specified "step".
 */
static void
_get_step_addresses(const job_step_info_t *step,
		    const resource_allocation_response_msg_t *allocation,
		    slurm_addr **address, int *num_addresses)
{
	hostset_t alloc_nodes;
	hostset_t step_nodes;
	hostlist_iterator_t step_nodes_it;
	slurm_addr *addrs;
	int num_nodes;
	char *hostname;
	int i;
	
	alloc_nodes = hostset_create(allocation->node_list);
	step_nodes = hostset_create(step->nodes);
	step_nodes_it = hostset_iterator_create(step_nodes);

	num_nodes = hostset_count(step_nodes);
	addrs = xmalloc(sizeof(slurm_addr) * num_nodes);
	while ((hostname = hostlist_next(step_nodes_it))) {
		i = hostset_index(alloc_nodes, hostname, 0);
		addrs[i] = allocation->node_addr[i];
		free(hostname);
	}

	hostlist_iterator_destroy(step_nodes_it);
	hostset_destroy(step_nodes);
	hostset_destroy(alloc_nodes);

	*address = addrs;
	*num_addresses = num_nodes;
}

static int
_signal_job_step(const job_step_info_t *step,
		 const resource_allocation_response_msg_t *allocation,
		 uint16_t signal)
{
	slurm_msg_t *msg; /* array of message structs, one per node */
	kill_tasks_msg_t rpc;
	slurm_addr *address;
	int num_nodes;
	int *rc_array;
	int rc = SLURM_SUCCESS;
	int i;

	_get_step_addresses(step, allocation,
			    &address, &num_nodes);

	/* same remote procedure call for each node */
	rpc.job_id = step->job_id;
	rpc.job_step_id = step->step_id;
	rpc.signal = (uint32_t)signal;

        msg = xmalloc(sizeof(slurm_msg_t) * num_nodes);
	rc_array = xmalloc(sizeof(int) * num_nodes);
	for (i = 0; i < num_nodes; i++) {
		msg[i].msg_type = REQUEST_SIGNAL_TASKS;
		msg[i].data = &rpc;
		msg[i].address = address[i];
	}

	_p_send_recv_rc_msg(num_nodes, msg, rc_array, 10);
	
	xfree(address);
	xfree(msg);

	for (i = 0; i < num_nodes; i++) {
		if (rc_array[i]) {
			rc = rc_array[i];
			break;
		}
	}
	xfree(rc_array);

	return rc;
}

static int _signal_batch_script_step(
	const resource_allocation_response_msg_t *allocation, uint16_t signal)
{
	slurm_msg_t msg;
	kill_tasks_msg_t rpc;
	int rc = SLURM_SUCCESS;

	rpc.job_id = allocation->job_id;
	rpc.job_step_id = SLURM_BATCH_SCRIPT;
	rpc.signal = (uint32_t)signal;

	msg.msg_type = REQUEST_SIGNAL_TASKS;
	msg.data = &rpc;
	msg.address = allocation->node_addr[0];

	if (slurm_send_recv_rc_msg_only_one(&msg, &rc, 0) < 0) {
		error("_signal_batch_script_step: %m");
		rc = -1;
	}
	return rc;
}


/*
 * Issue "messages" number of slurm_send_recv_rc_msg calls using
 * the messages in the array "msg", and the return code of
 * each call is placed in the array "rc".  Each slurm_send_recv_rc_msg
 * call is executed in a seperate pthread.  Up to MAX_THREADS threads
 * can be running at the same time.
 */
static int
_p_send_recv_rc_msg(int messages, slurm_msg_t msg[],
		    int rc[], int timeout)
{
	pthread_mutex_t active_mutex;
	pthread_cond_t  active_cond;
	pthread_attr_t  attr;
	pthread_t       thread_id;
	int             active = 0;
	int i;
	struct send_recv_rc *args;
	
	pthread_mutex_init(&active_mutex, NULL);
	pthread_cond_init(&active_cond, NULL);

	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		fprintf(stderr, "pthread_attr_setdetachstate failed");

	for (i = 0; i < messages; i++) {
		args = xmalloc(sizeof(struct send_recv_rc));
		args->msg = &msg[i];
		args->rc = &rc[i];
		args->timeout = timeout;
		args->lock = &active_mutex;
		args->cond = &active_cond;
		args->active = &active;

		slurm_mutex_lock(&active_mutex);
		while (active >= MAX_THREADS) {
			pthread_cond_wait(&active_cond, &active_mutex);
		}
		active++;
		slurm_mutex_unlock(&active_mutex);
		
		if (pthread_create(&thread_id, &attr,
				   _thr_send_recv_rc_msg, (void *)args)) {
			fprintf(stderr, "pthread_create failed");
			_thr_send_recv_rc_msg((void *)args);
		}
	}
	slurm_attr_destroy(&attr);

	/* Wait for pthreads to finish */
	slurm_mutex_lock(&active_mutex);
	while (active > 0) {
		pthread_cond_wait(&active_cond, &active_mutex);
	}
	slurm_mutex_unlock(&active_mutex);

	pthread_cond_destroy(&active_cond);
	pthread_mutex_destroy(&active_mutex);

	return (0);
}

static void *
_thr_send_recv_rc_msg(void *args)
{
	struct send_recv_rc *params = (struct send_recv_rc *)args;
	pthread_mutex_t *lock = params->lock;
	pthread_cond_t *cond = params->cond;
	int *active = params->active;

	if (slurm_send_recv_rc_msg_only_one(params->msg, params->rc, 
					    params->timeout) < 0) {
		error("_thr_send_recv_rc_msg: %m");
		*params->rc = -1;
	}

	xfree(args);
	slurm_mutex_lock(lock);
	(*active)--;
	pthread_cond_signal(cond);
	slurm_mutex_unlock(lock);

	return (NULL);
}

/*
 * slurm_terminate_job - terminates all steps of an existing job by sending
 * 	a REQUEST_TERMINATE_JOB rpc to all slurmd in the the job allocation,
 *      and then calls slurm_complete_job().
 * IN job_id     - the job's id
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int 
slurm_terminate_job (uint32_t job_id)
{
	int rc = SLURM_SUCCESS;
	resource_allocation_response_msg_t *alloc_info;
	slurm_msg_t *msg; /* array of message structs, one per node */
	signal_job_msg_t rpc;
	int *rc_array;
	int i;

	if (slurm_allocation_lookup(job_id, &alloc_info)) {
		rc = slurm_get_errno(); 
		goto fail1;
	}

	/* same remote procedure call for each node */
	rpc.job_id = job_id;
	rpc.signal = (uint32_t)-1; /* not used by slurmd */

        msg = xmalloc(sizeof(slurm_msg_t) * alloc_info->node_cnt);
	rc_array = xmalloc(sizeof(int) * alloc_info->node_cnt);
	for (i = 0; i < alloc_info->node_cnt; i++) {
		msg[i].msg_type = REQUEST_TERMINATE_JOB;
		msg[i].data = &rpc;
		msg[i].address = alloc_info->node_addr[i];
	}

	_p_send_recv_rc_msg(alloc_info->node_cnt, msg, rc_array, 10);
	
	for (i = 0; i < alloc_info->node_cnt; i++) {
		if (rc_array[i]) {
			rc = rc_array[i];
			break;
		}
	}

	xfree(msg);
	xfree(rc_array);
	slurm_free_resource_allocation_response_msg(alloc_info);

	slurm_complete_job(job_id, 0);
fail1:
	if (rc) {
		slurm_seterrno_ret(rc);
		return SLURM_FAILURE;
	} else {
		return SLURM_SUCCESS;
	}
}

/*
 * slurm_terminate_job_step - terminates a job step by sending a
 * 	REQUEST_TERMINATE_TASKS rpc to all slurmd of a job step.
 * IN job_id  - the job's id
 * IN step_id - the job step's id - use SLURM_BATCH_SCRIPT as the step_id
 *              to terminate a job's batch script
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int 
slurm_terminate_job_step (uint32_t job_id, uint32_t step_id)
{
	resource_allocation_response_msg_t *alloc_info;
	job_step_info_response_msg_t *step_info;
	int rc = 0;
	int i;
	int save_errno = 0;

	if (slurm_allocation_lookup(job_id, &alloc_info)) {
		return -1;
	}

	/*
	 * The controller won't give us info about the batch script job step,
	 * so we need to handle that seperately.
	 */
	if (step_id == SLURM_BATCH_SCRIPT) {
		rc = _terminate_batch_script_step(alloc_info);
		slurm_free_resource_allocation_response_msg(alloc_info);
		errno = rc;
		return rc ? -1 : 0;
	}

	/*
	 * Otherwise, look through the list of job step info and find
	 * the one matching step_id.  Terminate that step.
	 */
	rc = slurm_get_job_steps((time_t)0, job_id, step_id, 
				 &step_info, SHOW_ALL);
	if (rc != 0) {
		save_errno = errno;
		goto fail;
	}
	for (i = 0; i < step_info->job_step_count; i++) {
		if (step_info->job_steps[i].job_id == job_id
		    && step_info->job_steps[i].step_id == step_id) {
			rc = _terminate_job_step(&step_info->job_steps[i],
						 alloc_info);
			save_errno = errno;
			break;
		}
	}
	slurm_free_job_step_info_response_msg(step_info);
fail:
	slurm_free_resource_allocation_response_msg(alloc_info);
	errno = save_errno;
	return rc ? -1 : 0;
}


/*
 * Send a REQUEST_TERMINATE_TASKS rpc to all nodes in a job step.
 *
 * RET Upon successful termination of the job step, 0 shall be returned.
 * Otherwise, -1 shall be returned and errno set to indicate the error.
 */
static int
_terminate_job_step(const job_step_info_t *step,
		    const resource_allocation_response_msg_t *allocation)
{
	slurm_msg_t *msg; /* array of message structs, one per node */
	kill_tasks_msg_t rpc;
	slurm_addr *address;
	int num_nodes;
	int *rc_array;
	int rc = SLURM_SUCCESS;
	int i;

	_get_step_addresses(step, allocation,
			    &address, &num_nodes);

	/*
	 *  Send REQUEST_TERMINATE_TASKS to all nodes of the step
	 */
	rpc.job_id = step->job_id;
	rpc.job_step_id = step->step_id;
	rpc.signal = (uint32_t)-1; /* not used by slurmd */

        msg = xmalloc(sizeof(slurm_msg_t) * num_nodes);
	rc_array = xmalloc(sizeof(int) * num_nodes);
	for (i = 0; i < num_nodes; i++) {
		msg[i].msg_type = REQUEST_TERMINATE_TASKS;
		msg[i].data = &rpc;
		msg[i].address = address[i];
	}

	_p_send_recv_rc_msg(num_nodes, msg, rc_array, 10);

	xfree(msg);
	xfree(rc_array);
	xfree(address);

	if (rc == -1 && errno == ESLURM_ALREADY_DONE) {
		rc = 0;
		errno = 0;
	}

	return rc;
}

static int _terminate_batch_script_step(
	const resource_allocation_response_msg_t *allocation)
{
	slurm_msg_t msg;
	kill_tasks_msg_t rpc;
	int rc = SLURM_SUCCESS;
	int i;

	rpc.job_id = allocation->job_id;
	rpc.job_step_id = SLURM_BATCH_SCRIPT;
	rpc.signal = (uint32_t)-1; /* not used by slurmd */

	msg.msg_type = REQUEST_TERMINATE_TASKS;
	msg.data = &rpc;
	msg.address = allocation->node_addr[0];

	i = slurm_send_recv_rc_msg_only_one(&msg, &rc, 10);
	if (i != 0)
		rc = i;

	return rc;
}

