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

/*
 * A pushed step the wait dropped before launch.  The stepmgr counts the
 * push as delivered, so without a completion the step would hold its
 * resources until the job ends.
 */
typedef struct {
	slurm_step_id_t step_id; /* the dropped step's id */
	uint32_t node_cnt; /* nodes in the pushed layout (completion range) */
	char *stepmgr; /* batch host running the step's stepmgr */
} dropped_step_t;

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
 * Answer an SRUN_STEP_ALLOC_CHECK with a credential the stepmgr can check this
 * srun's uid with, rather than trusting whoever holds the listener port.  A
 * response omits the credential by default, so pack one and restrict its
 * decode to the requester.  Answer SUCCESS only while this job is waiting on
 * the named step (or on a step whose id it never learned), so a check relayed
 * to another of the user's sruns is refused.
 * IN msg - the received check
 * IN job - job owning the listener
 * RET SLURM_SUCCESS or error
 */
static int _answer_alloc_check(slurm_msg_t *msg, srun_job_t *job)
{
	return_code_msg_t rc_msg = { .return_code = SLURM_SUCCESS };
	step_alloc_check_msg_t *check = msg->data;
	slurm_msg_t resp_msg;
	bool match = false;

	/* Without an authenticated requester there is nobody to restrict to. */
	if (!msg->conmgr_con || !msg->auth.ids_set)
		return SLURM_ERROR;

	slurm_mutex_lock(&srun_destroy_sig_lock);
	/* A NO_VAL wait id verifies against any step by design. */
	match = job->step_wait_active &&
		verify_step_id(&check->step_id, &job->wait_step_id);
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	if (!match) {
		info("Refusing a push check for %ps; this srun is not waiting on it",
		     &check->step_id);
		rc_msg.return_code = SLURM_ERROR;
	}

	slurm_resp_msg_init(&resp_msg, msg, RESPONSE_SLURM_RC, &rc_msg);
	slurm_msg_set_r_uid(&resp_msg, msg->auth.uid);
	resp_msg.flags &= ~SLURM_NO_AUTH_CRED;

	return conmgr_con_queue_write_msg(msg->conmgr_con, &resp_msg);
}

/*
 * Derive what an RPC received while a sync step is queued asks srun to do.
 * SRUN_STEP_SIGNAL with a kill signal for the queued job is a cancel;
 * SRUN_STEP_ALLOCATED carries a step the stepmgr created at wake;
 * RESPONSE_SLURM_RC carries a terminal create error; anything else is a wake
 * (retry the create).  A push naming another step is none: the stepmgr
 * answered a different request, and this wait is not for it.
 * IN msg - received message; an accepted pushed step is taken from msg->data
 * IN step_id - step the queued wait is for
 * OUT resp - the pushed step, if STEP_POKE_ALLOCATED is returned
 * OUT fail_rc - the create error, if STEP_POKE_FAILED is returned
 * RET the poke to record
 */
static step_poke_t _msg_to_poke(slurm_msg_t *msg, slurm_step_id_t *step_id,
				step_create_resp_msg_t **resp, int *fail_rc)
{
	switch (msg->msg_type) {
	case SRUN_STEP_SIGNAL:
	{
		job_step_kill_msg_t *kill_msg = msg->data;

		if (kill_msg && kill_msg->signal &&
		    (kill_msg->step_id.job_id == step_id->job_id))
			return STEP_POKE_CANCEL;
		break;
	}
	case SRUN_STEP_ALLOCATED:
	{
		step_create_resp_msg_t *pushed = msg->data;

		/*
		 * Only take a push that answers this srun's own request; a
		 * layout is what makes it launchable.  Until the server has
		 * assigned an id the request cannot be matched on one, and
		 * arriving on this srun's own listener is what identifies it.
		 */
		if (!pushed || !pushed->step_layout || !pushed->cred) {
			error("Discarding a malformed step allocation for %ps",
			      step_id);
			break;
		}
		if (!verify_step_id(&pushed->step_id, step_id)) {
			if (step_id->step_id != NO_VAL) {
				error("Discarding a step allocation that does not match %ps",
				      step_id);
				return STEP_POKE_NONE;
			}
			/*
			 * The request never learned its id: the create failed
			 * after the stepmgr committed the placeholder but before
			 * the queued reply arrived.  There is no id to match on,
			 * and an array-task push carries the task's own job id
			 * rather than the array job id this wait holds, so the
			 * listener identity is all there is.
			 */
			debug("Accepting a step allocation for %ps on listener identity alone",
			      step_id);
		}
		*resp = pushed;
		msg->data = NULL; /* ownership transferred to the wait */
		return STEP_POKE_ALLOCATED;
	}
	case RESPONSE_SLURM_RC:
	{
		return_code_msg_t *rc_msg = msg->data;

		*fail_rc = rc_msg ? (int) rc_msg->return_code : SLURM_ERROR;
		return STEP_POKE_FAILED;
	}
	default:
		break;
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

	/*
	 * Keep the connection open after answering a push check: the stepmgr
	 * pushes the step on it once it has checked the answer.
	 */
	if (msg->msg_type == SRUN_STEP_ALLOC_CHECK) {
		if (_answer_alloc_check(msg, job))
			conmgr_con_queue_close(conmgr_args.ref);
		FREE_NULL_MSG(msg);
		return SLURM_SUCCESS;
	}

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

static void _free_dropped_step(void *x)
{
	dropped_step_t *dropped = x;

	xfree(dropped->stepmgr);
	xfree(dropped);
}

/*
 * Record a delivered pushed step that the wait is dropping.
 * IN/OUT dropped_steps - list of dropped_step_t, created on first use
 * IN resp - the pushed step being dropped (step id, layout, stepmgr host
 *	copied)
 */
static void _note_dropped_step(list_t **dropped_steps,
			       step_create_resp_msg_t *resp)
{
	dropped_step_t *dropped = xmalloc(sizeof(*dropped));

	dropped->step_id = resp->step_id;
	dropped->node_cnt = resp->step_layout ? resp->step_layout->node_cnt : 0;
	dropped->stepmgr = xstrdup(resp->stepmgr);
	if (!*dropped_steps)
		*dropped_steps = list_create(_free_dropped_step);
	list_append(*dropped_steps, dropped);
}

/*
 * Complete a dropped pushed step: the step only exists in the stepmgr, so
 * send the completion straight to the batch node's slurmd marked
 * send_to_stepmgr.  SIGKILL records it CANCELLED, matching a scancel'ed
 * step.  Best effort: a send failure leaves the step for the job to clean
 * up, as before.
 * IN dropped - the dropped step
 */
static void _complete_dropped_step(dropped_step_t *dropped)
{
	step_complete_msg_t req;
	slurm_msg_t msg;
	int rc = SLURM_ERROR;

	slurm_msg_t_init(&msg);
	slurm_msg_set_r_uid(&msg, slurm_conf.slurmd_user_id);
	memset(&req, 0, sizeof(req));
	req.step_id = dropped->step_id;
	req.range_first = 0;
	req.range_last = dropped->node_cnt ? dropped->node_cnt - 1 : 0;
	req.step_rc = SIGKILL;
	req.send_to_stepmgr = true;
	msg.msg_type = REQUEST_STEP_COMPLETE;
	msg.data = &req;

	if (slurm_conf_get_addr(dropped->stepmgr, &msg.address, msg.flags)) {
		slurm_node_alias_addrs_t *alias_addrs = NULL;

		if (!slurm_get_node_alias_addrs(dropped->stepmgr, &alias_addrs))
			add_remote_nodes_to_conf_tbls(alias_addrs->node_list,
						      alias_addrs->node_addrs);
		slurm_free_node_alias_addrs(alias_addrs);
		if (slurm_conf_get_addr(dropped->stepmgr, &msg.address,
					msg.flags)) {
			error("Could not resolve the step manager for dropped %ps",
			      &dropped->step_id);
			return;
		}
	}

	if (slurm_send_recv_rc_msg_only_one(&msg, &rc, 0))
		error("Could not complete dropped pushed %ps: %m",
		      &dropped->step_id);
	else if (rc && (rc != ESLURM_INVALID_JOB_ID) &&
		 (rc != ESLURMD_STEP_NOTRUNNING))
		error("Could not complete dropped pushed %ps: %s",
		      &dropped->step_id, slurm_strerror(rc));
}

/*
 * Block until an RPC ends the queued step's wait, the srun destroy signal
 * fires, or the timeout elapses. Only this wait consumes RPCs as pokes;
 * messages arriving during a create RPC remain queued until its result.
 * IN/OUT job - job whose wait state receives the queued step's RPCs
 * IN step_id - step the queued wait is for
 * IN timeout - in milliseconds
 * IN retry_errno - errno to return so the caller retries the create
 * OUT resp_out - the pushed create response if the stepmgr created the step
 *	at wake; ownership transfers to the caller
 * RET errno to surface: SLURM_SUCCESS if resp_out was set, retry_errno if
 *	woken to retry, the pushed error if the create failed,
 *	ESLURM_STEP_TIMED_OUT if the wait timed out, or ESLURM_STEP_CANCELLED
 *	if the job/step was cancelled
 */
static int _wait_pending_step(srun_job_t *job, slurm_step_id_t *step_id,
			      int timeout, int retry_errno,
			      step_create_resp_msg_t **resp_out)
{
	int destroy_sig = 0;
	int errnum = retry_errno;
	int fail_rc = SLURM_ERROR;
	bool timed_out = false;
	step_poke_t poke = STEP_POKE_NONE;
	slurm_msg_t *msg = NULL;
	list_t *dropped_steps = NULL;
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
	job->wait_step_id = *step_id;
	job->step_wait_active = true;
	while (true) {
		while (job->step_pending_msgs &&
		       (msg = list_pop(job->step_pending_msgs))) {
			step_poke_t p = STEP_POKE_NONE;
			bool consume = (poke == STEP_POKE_NONE) ||
				       (poke == STEP_POKE_WAKE);

			/*
			 * The first decisive outcome stands; a wake is only
			 * replaced.  A cancel is the exception: it overrides
			 * whatever stands, or the cancelled step launches and
			 * runs anyway.  Later RPCs still queued here are
			 * otherwise launch traffic or stale pokes: drop them.
			 */
			if (consume || (msg->msg_type == SRUN_STEP_SIGNAL))
				p = _msg_to_poke(msg, step_id, resp_out,
						 &fail_rc);

			if (p == STEP_POKE_CANCEL) {
				/* A cancel drops an accepted push too. */
				if (*resp_out)
					_note_dropped_step(&dropped_steps,
							   *resp_out);
				slurm_free_step_create_resp_msg(*resp_out);
				*resp_out = NULL;
				poke = p;
			} else if (consume && (p != STEP_POKE_NONE)) {
				poke = p;
			} else if ((msg->msg_type == SRUN_STEP_ALLOCATED) &&
				   msg->data) {
				step_create_resp_msg_t *pushed = msg->data;

				/*
				 * Never drop the step this wait accepted: a
				 * duplicate push of it still launches.  A push
				 * for any other step is a drop.
				 */
				if (!(*resp_out &&
				      verify_step_id(&pushed->step_id,
						     &(*resp_out)->step_id)))
					_note_dropped_step(&dropped_steps,
							   pushed);
			}
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
	destroy_sig = srun_destroy_sig;
	job->step_wait_active = false;
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	if (destroy_sig && *resp_out) {
		/* A destroy signal discards an accepted push as well. */
		_note_dropped_step(&dropped_steps, *resp_out);
		slurm_free_step_create_resp_msg(*resp_out);
		*resp_out = NULL;
	}

	/* Blocking RPCs: only send them outside the destroy-signal lock. */
	if (dropped_steps) {
		dropped_step_t *dropped = NULL;

		while ((dropped = list_pop(dropped_steps))) {
			_complete_dropped_step(dropped);
			_free_dropped_step(dropped);
		}
		FREE_NULL_LIST(dropped_steps);
	}

	if (destroy_sig) {
		/* The destroy signal overrides whatever the wait ended on. */
		info("Cancelled pending job step with signal %d", destroy_sig);
		errnum = ESLURM_STEP_CANCELLED;
	} else if (poke == STEP_POKE_ALLOCATED) {
		errnum = SLURM_SUCCESS;
	} else if (poke == STEP_POKE_FAILED) {
		errnum = fail_rc;
	} else if (poke == STEP_POKE_CANCEL) {
		info("Pending job step cancelled");
		errnum = ESLURM_STEP_CANCELLED;
	} else if (timed_out) {
		errnum = ESLURM_STEP_TIMED_OUT;
	}

	return errnum;
}

/*
 * Build a launchable step context from a create response, recording the
 * assigned step id (and array task's job id) on the request.
 * IN/OUT step_req - the create request, updated with the assigned id
 * IN step_resp - the create response; ownership transfers to the ctx
 * RET the step context
 */
static struct slurm_step_ctx_struct *_step_ctx_from_resp(
	step_create_req_msg_t *step_req,
	step_create_resp_msg_t *step_resp)
{
	struct slurm_step_ctx_struct *ctx = xmalloc(sizeof(*ctx));

	ctx->magic = STEP_CTX_MAGIC;
	ctx->job_id = step_req->step_id.job_id;
	ctx->step_req = step_req;
	/* Grab the step id if we don't already have it; the slurmd needs it. */
	if (step_req->step_id.step_id == NO_VAL)
		step_req->step_id.step_id = step_resp->step_id.step_id;
	if (step_req->array_task_id != NO_VAL) {
		step_req->step_id.job_id = step_resp->step_id.job_id;
		ctx->job_id = step_resp->step_id.job_id;
	}
	ctx->step_resp = step_resp;
	ctx->launch_state = step_launch_state_create(ctx);

	return ctx;
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
extern slurm_step_ctx_t *step_ctx_create_timeout(step_create_req_msg_t
							 *step_req,
						 int timeout,
						 srun_opt_t *srun_opt,
						 srun_job_t *job,
						 int *retry_cause)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	step_create_resp_msg_t *step_resp = NULL;
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
		step_resp = NULL;
		/* The step may be queued despite the error. */
		errnum = _wait_pending_step(job, &step_req->step_id, timeout,
					    errno, &step_resp);
		if (!errnum && step_resp)
			return (slurm_step_ctx_t *)
				_step_ctx_from_resp(step_req, step_resp);
		errno = errnum;
	} else if ((rc < 0) || (step_resp == NULL)) {
		/* errno already holds the cause */
	} else {
		if (IS_STEP_PENDING(step_resp)) {
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
			slurm_free_step_create_resp_msg(step_resp);

			if (srun_opt->async) {
				/* Fire-and-forget; srun returns SUCCESS. */
				errno = ESLURM_STEP_QUEUED;
				return NULL;
			}

			if (newly_pending)
				info("%ps queued", &step_req->step_id);

			*retry_cause = ESLURM_STEP_QUEUED;
			/*
			 * The step is known queued now, so wait until the
			 * stepmgr pushes the created step (create-at-wake), a
			 * step completes (poke, re-send), the create fails, or
			 * we time out.
			 */
			step_resp = NULL;
			errnum = _wait_pending_step(job, &step_req->step_id,
						    timeout, ESLURM_STEP_QUEUED,
						    &step_resp);
			if (!errnum && step_resp)
				return (slurm_step_ctx_t *)
					_step_ctx_from_resp(step_req,
							    step_resp);
			errno = errnum;
			return NULL;
		}

		ctx = _step_ctx_from_resp(step_req, step_resp);
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
	step_create_req_msg_t *step_req, uint32_t step_id)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	step_create_resp_msg_t *step_resp = NULL;

	xassert(step_req);

	/* Then make up a response with only certain things filled in */
	step_resp = (step_create_resp_msg_t *)
		xmalloc(sizeof(step_create_resp_msg_t));

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
