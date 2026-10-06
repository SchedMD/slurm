/*****************************************************************************\
 *  step_ctx.c - step_ctx task functions for use by AIX/POE
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/switch.h"

#include "src/srun/launch.h"
#include "src/srun/signals.h"
#include "src/srun/srun_job.h"
#include "src/srun/step_ctx.h"

static void *_step_listen_connection(conmgr_callback_args_t conmgr_args,
				     void *arg);
static void _step_con_finish(conmgr_callback_args_t conmgr_args, void *arg);
static void *_step_listen_connect(conmgr_callback_args_t conmgr_args,
				  void *arg);
static void _step_listen_finish(conmgr_callback_args_t conmgr_args, void *arg);
static int _on_step_msg(conmgr_callback_args_t conmgr_args, slurm_msg_t *msg,
			int unpack_rc, void *arg);

static const conmgr_events_t step_listen_events = {
	.on_connection = _step_listen_connection,
	.on_finish = _step_con_finish,
	.on_listen_connect = _step_listen_connect,
	.on_listen_finish = _step_listen_finish,
	.on_msg = _on_step_msg,
};

static void _job_fake_cred(struct slurm_step_ctx_struct *ctx)
{
	uint32_t node_cnt = ctx->step_resp->step_layout->node_cnt;
	slurm_cred_arg_t *arg = xmalloc(sizeof(*arg));

	memcpy(&arg->step_id, &ctx->step_req->step_id, sizeof(arg->step_id));
	arg->uid = getuid();

	arg->job_max_npids = NO_VAL;
	arg->job_nhosts = node_cnt;
	arg->job_hostlist = ctx->step_resp->step_layout->node_list;
	arg->job_mem_alloc = xmalloc(sizeof(uint64_t));
	arg->job_mem_alloc[0] = 0;
	arg->job_mem_alloc_rep_count = xmalloc(sizeof(uint64_t));
	arg->job_mem_alloc_rep_count[0] = node_cnt;
	arg->job_mem_alloc_size = 1;

	arg->step_hostlist = ctx->step_req->node_list;
	arg->step_mem_alloc = xmalloc(sizeof(uint64_t));
	arg->step_mem_alloc[0] = 0;
	arg->step_mem_alloc_rep_count = xmalloc(sizeof(uint64_t));
	arg->step_mem_alloc_rep_count[0] = node_cnt;
	arg->step_mem_alloc_size = 1;

	arg->job_core_bitmap = bit_alloc(node_cnt);
	bit_set_all(arg->job_core_bitmap);
	arg->step_core_bitmap  = bit_alloc(node_cnt);
	bit_set_all(arg->step_core_bitmap);

	arg->cores_per_socket = xmalloc(sizeof(uint16_t));
	arg->cores_per_socket[0] = 1;
	arg->sockets_per_node = xmalloc(sizeof(uint16_t));
	arg->sockets_per_node[0] = 1;
	arg->sock_core_rep_count = xmalloc(sizeof(uint32_t));
	arg->sock_core_rep_count[0] = node_cnt;

	ctx->step_resp->cred = slurm_cred_faker(arg);

	/* Don't free, this memory will be free'd later */
	arg->job_hostlist = NULL;
	arg->step_hostlist = NULL;
	slurm_cred_free_args(arg);
}

static void *_step_listen_connection(conmgr_callback_args_t conmgr_args,
				     void *arg)
{
	log_flag(NET, "%s: [%s] new connection",
		 __func__, conmgr_con_get_name(conmgr_args.ref));

	return arg;
}

static void _step_con_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(NET, "%s: [%s] connection finished",
		 __func__, conmgr_con_get_name(conmgr_args.ref));
}

/*
 * conmgr hands the listener back here.  Link it into the job.
 */
static void *_step_listen_connect(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *con = conmgr_args.ref;
	srun_job_t *job = arg;

	slurm_mutex_lock(&srun_destroy_sig_lock);
	CONMGR_CON_LINK(con, job->step_listener);
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	log_flag(NET, "%s: [%s] Successfully opened step launch RPC listener",
		 __func__, conmgr_con_get_name(con));

	return arg;
}

/*
 * Close and free a queued RPC, releasing its connection reference.
 * IN arg - message to discard
 */
static void _free_step_msg(void *arg)
{
	slurm_msg_t *msg = arg;

	if (!msg)
		return;

	conmgr_con_queue_close(msg->conmgr_con);
	slurm_free_msg(msg);
}

static void _step_listen_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *con = conmgr_args.ref;
	srun_job_t *job = arg;

	log_flag(NET, "%s: [%s] Step launch RPC listener closed",
		 __func__, conmgr_con_get_name(con));

	/* The job outlives this; only the conmgr fd is gone. */
	slurm_mutex_lock(&srun_destroy_sig_lock);
	if (job->step_listener)
		CONMGR_CON_UNLINK(job->step_listener);
	/* Release message references before conmgr_fini() waits for them. */
	FREE_NULL_LIST(job->step_pending_msgs);
	slurm_mutex_unlock(&srun_destroy_sig_lock);
}

/*
 * Derive what an RPC received while a sync step is queued asks srun to do:
 * SRUN_STEP_SIGNAL with a kill signal for the queued job is a cancel,
 * anything else a wake (retry the create).
 * IN msg - received message
 * IN job_id - job the queued step belongs to
 * RET the poke to record
 */
static step_poke_t _msg_to_poke(slurm_msg_t *msg, uint32_t job_id)
{
	if (msg->msg_type == SRUN_STEP_SIGNAL) {
		job_step_kill_msg_t *kill_msg = msg->data;

		if (kill_msg && kill_msg->signal &&
		    (kill_msg->step_id.job_id == job_id))
			return STEP_POKE_CANCEL;
	}

	return STEP_POKE_WAKE;
}

/*
 * Queue RPCs until a pending-step wait consumes them or launch takes over.
 * IN conmgr_args - connection receiving the RPC
 * IN msg - received message, ownership transferred here
 * IN unpack_rc - unpacking result
 * IN arg - job owning the listener
 * RET SLURM_SUCCESS or a validation error
 */
static int _on_step_msg(conmgr_callback_args_t conmgr_args, slurm_msg_t *msg,
			int unpack_rc, void *arg)
{
	srun_job_t *job = arg;
	step_launch_state_t *sls = NULL;
	int rc = EINVAL;

	slurm_mutex_lock(&srun_destroy_sig_lock);
	sls = job->step_launch_ready;
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	/* Outside the lock: the launch handler may block on a slow peer. */
	if (sls)
		return step_launch_on_msg(conmgr_args, msg, unpack_rc, sls);

	if ((rc = step_launch_check_msg(conmgr_args, msg, unpack_rc)))
		return rc;

	slurm_mutex_lock(&srun_destroy_sig_lock);
	/* Publication may have completed while the message was validated. */
	sls = job->step_launch_ready;
	if (!sls && job->step_pending_msgs) {
		list_append(job->step_pending_msgs, msg);
		msg = NULL;
		EVENT_BROADCAST(&srun_wait_event);
	}
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	if (sls)
		return step_launch_on_msg(conmgr_args, msg, unpack_rc, sls);

	/* A closed listener no longer has a queue. */
	_free_step_msg(msg);
	return SLURM_SUCCESS;
}

extern int step_ctx_listener_create(srun_job_t *job, uint16_t *port_ptr)
{
	int rc = EINVAL;
	int sock = -1;
	uint16_t port = 0;

	/* Only SLURM_ERROR comes back; the cause is left in errno. */
	if (slurm_init_msg_engine_srun_ports(&sock, &port))
		return errno ? errno : SLURM_ERROR;

	job->step_pending_msgs = list_create(_free_step_msg);
	if ((rc = conmgr_process_fd_listen(sock, CON_TYPE_RPC,
					   step_launch_listen_timeouts(),
					   &step_listen_events, CON_FLAG_NONE,
					   job))) {
		(void) close(sock);
		FREE_NULL_LIST(job->step_pending_msgs);
		return rc;
	}

	*port_ptr = port;

	return SLURM_SUCCESS;
}

extern void step_ctx_publish_launch(srun_job_t *job)
{
	slurm_msg_t *msg = NULL;
	step_launch_state_t *sls = job->step_ctx->launch_state;

	slurm_mutex_lock(&srun_destroy_sig_lock);
	while (job->step_pending_msgs &&
	       (msg = list_pop(job->step_pending_msgs))) {
		slurm_mutex_unlock(&srun_destroy_sig_lock);
		step_launch_on_msg(
			(conmgr_callback_args_t) {
				.ref = msg->conmgr_con,
			},
			msg, SLURM_SUCCESS, sls);
		slurm_mutex_lock(&srun_destroy_sig_lock);
	}
	job->step_launch_ready = sls;
	slurm_mutex_unlock(&srun_destroy_sig_lock);
}

/*
 * Block until an RPC ends the queued step's wait, the srun destroy signal
 * fires, or the timeout elapses. Only this wait consumes RPCs as pokes;
 * messages arriving during a create RPC remain queued until its result.
 * IN/OUT job - job whose wait state receives the queued step's RPCs
 * IN job_id - job the queued step belongs to
 * IN timeout - in milliseconds
 * IN retry_errno - errno to return so the caller retries the create
 * RET errno to surface: retry_errno if woken to retry, ESLURM_STEP_TIMED_OUT
 *	if the wait timed out, or ESLURM_STEP_CANCELLED if the job/step was
 *	cancelled
 */
static int _wait_pending_step(srun_job_t *job, uint32_t job_id, int timeout,
			      int retry_errno)
{
	int errnum = retry_errno;
	bool timed_out = false;
	step_poke_t poke = STEP_POKE_NONE;
	slurm_msg_t *msg = NULL;
	timespec_t deadline = { 0 };

	/*
	 * One absolute deadline: srun_wait_event waits on CLOCK_REALTIME, so a
	 * spurious wake never extends the timeout.  A timeout overflowing int
	 * goes negative and must yield a past deadline, not an invalid
	 * timespec: timespec_add() normalizes in both directions.
	 */
	deadline = timespec_add(timespec_now(),
				(timespec_t) {
					.tv_sec = timeout / MSEC_IN_SEC,
					.tv_nsec = (timeout % MSEC_IN_SEC) *
						   NSEC_IN_MSEC,
				});

	slurm_mutex_lock(&srun_destroy_sig_lock);
	while (true) {
		while (job->step_pending_msgs &&
		       (msg = list_pop(job->step_pending_msgs))) {
			if (poke != STEP_POKE_CANCEL)
				poke = _msg_to_poke(msg, job_id);
			_free_step_msg(msg);
		}
		if (poke != STEP_POKE_NONE)
			break;
		if (srun_destroy_sig)
			break;
		/*
		 * EVENT_WAIT_TIMED() swallows ETIMEDOUT, and the timeout
		 * must be seen: test the deadline on every pass, after the
		 * outcome checks so an outcome recorded as the deadline
		 * passes still ends this wait instead of being wiped.
		 * timespec_after_deadline() drops nsecs, so it can read
		 * 0 with up to a second left; compare at full precision.
		 */
		if (!timespec_is_after(deadline, timespec_now())) {
			timed_out = true;
			break;
		}
		EVENT_WAIT_TIMED(&srun_wait_event, deadline,
				 &srun_destroy_sig_lock);
	}
	/* The destroy signal overrides whatever the wait ended on. */
	if (srun_destroy_sig) {
		info("Cancelled pending job step with signal %d",
		     srun_destroy_sig);
		errnum = ESLURM_STEP_CANCELLED;
	}
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	if (poke == STEP_POKE_CANCEL) {
		info("Pending job step cancelled");
		errnum = ESLURM_STEP_CANCELLED;
	}

	if (timed_out && (errnum != ESLURM_STEP_CANCELLED))
		errnum = ESLURM_STEP_TIMED_OUT;

	return errnum;
}

/*
 * step_ctx_create - Create a job step and its context.
 * IN step_params - job step parameters
 * IN timeout - in milliseconds
 * IN srun_opt - srun options
 * OUT retry_cause - why the step could not be created yet, so the caller can
 *	report it alongside ESLURM_STEP_TIMED_OUT
 * RET the step context or NULL on failure with slurm errno set
 *	(ESLURM_STEP_TIMED_OUT or ESLURM_STEP_CANCELLED while waiting on a
 *	queued step)
 * NOTE: Free allocated memory using step_ctx_destroy()
 */
extern slurm_step_ctx_t *step_ctx_create_timeout(job_step_create_request_msg_t
							 *step_req,
						 int timeout,
						 srun_opt_t *srun_opt,
						 srun_job_t *job,
						 int *retry_cause)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	job_step_create_response_msg_t *step_resp = NULL;
	int rc = EINVAL;
	int errnum = SLURM_SUCCESS;

	xassert(step_req);
	xassert(retry_cause);
	xassert(job);
	/* Async steps are fire and forget, so they are given no listener. */
	xassert(!srun_opt->async || !job->step_listener);

	rc = slurm_job_step_create(step_req, &step_resp);
	if ((rc < 0) && launch_step_retry_errno(errno)) {
		*retry_cause = errno;
		/* The step may be queued despite the error. */
		errnum = _wait_pending_step(job, step_req->step_id.job_id,
					    timeout, errno);
		errno = errnum;
	} else if ((rc < 0) || (step_resp == NULL)) {
		/* errno already holds the cause */
	} else {
		if (step_resp->state == JOB_PENDING) {
			bool newly_pending =
				(step_req->step_id.step_id == NO_VAL);
			/*
			 * Controller queued the step with a real step_id.
			 * Record it.
			 */
			if (newly_pending)
				step_req->step_id.step_id =
					step_resp->step_id.step_id;
			if (step_req->array_task_id != NO_VAL) {
				/*
				 * job_id is now the task's own, so a re-send
				 * must resolve on it directly rather than
				 * looking it up as an array task again.
				 */
				step_req->step_id.job_id =
					step_resp->step_id.job_id;
				step_req->array_task_id = NO_VAL;
			}
			slurm_free_job_step_create_response_msg(step_resp);

			if (srun_opt->async) {
				/* Fire-and-forget; srun returns SUCCESS. */
				errno = ESLURM_STEP_QUEUED;
				return NULL;
			}

			if (newly_pending)
				info("%ps queued", &step_req->step_id);

			*retry_cause = ESLURM_STEP_QUEUED;
			/*
			 * The step is known queued now, so wait until a step
			 * completes (poke, re-send), the create fails, or we
			 * time out.
			 */
			errnum =
				_wait_pending_step(job,
						   step_req->step_id.job_id,
						   timeout, ESLURM_STEP_QUEUED);
			errno = errnum;
			return NULL;
		}

		ctx = xmalloc(sizeof(struct slurm_step_ctx_struct));
		ctx->launch_state = NULL;
		ctx->magic	= STEP_CTX_MAGIC;
		ctx->job_id	= step_req->step_id.job_id;
		ctx->step_req   = step_req;
		/*
		 * Grab the step id here if we don't already have it, we will
		 * need to to send to the slurmd.
		 */
		if (step_req->step_id.step_id == NO_VAL)
			step_req->step_id.step_id = step_resp->step_id.step_id;

		if (step_req->array_task_id != NO_VAL) {
			step_req->step_id.job_id = step_resp->step_id.job_id;
			ctx->job_id = step_resp->step_id.job_id;
		}

		ctx->step_resp	= step_resp;
		ctx->launch_state = step_launch_state_create(ctx);
	}

	return (slurm_step_ctx_t *) ctx;
}

/*
 * step_ctx_create_no_alloc - Create a job step and its context without
 *                            getting an allocation.
 * IN step_params - job step parameters
 * IN step_id     - since we are faking it give me the id to use
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using step_ctx_destroy()
 */
extern slurm_step_ctx_t *step_ctx_create_no_alloc(
	job_step_create_request_msg_t *step_req, uint32_t step_id)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	job_step_create_response_msg_t *step_resp = NULL;

	xassert(step_req);

	/* Then make up a response with only certain things filled in */
	step_resp = (job_step_create_response_msg_t *)
		xmalloc(sizeof(job_step_create_response_msg_t));

	step_resp->step_layout = fake_slurm_step_layout_create(
		step_req->node_list,
		NULL, NULL,
		step_req->min_nodes,
		step_req->num_tasks,
		0);

	step_resp->step_id.step_id = step_id;

	ctx = xmalloc(sizeof(struct slurm_step_ctx_struct));
	ctx->launch_state = NULL;
	ctx->magic	= STEP_CTX_MAGIC;
	ctx->job_id	= step_req->step_id.job_id;
	ctx->step_req   = step_req;

	/*
	 * Grab the step id here if we don't already have it, we will
	 * need to to send to the slurmd.
	 */
	if (step_req->step_id.step_id == NO_VAL)
		step_req->step_id.step_id = step_resp->step_id.step_id;

	ctx->step_resp	= step_resp;
	ctx->launch_state = step_launch_state_create(ctx);

	_job_fake_cred(ctx);

	return (slurm_step_ctx_t *)ctx;
}
