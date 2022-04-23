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
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
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
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/switch.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "step_ctx.h"
#include "launch.h"

int step_signals[] = {
	SIGINT,  SIGQUIT, SIGCONT, SIGTERM, SIGHUP,
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0 };
static int destroy_step = 0;

static void _signal_while_allocating(int signo)
{
	debug("Got signal %d", signo);
	if (signo == SIGCONT)
		return;

	destroy_step = signo;
}

static void _job_fake_cred(struct slurm_step_ctx_struct *ctx)
{
	uint32_t node_cnt = ctx->step_resp->step_layout->node_cnt;
	slurm_cred_arg_t *arg = xmalloc(sizeof(*arg));

	memcpy(&arg->step_id, &ctx->step_req->step_id, sizeof(arg->step_id));
	arg->uid = ctx->user_id;

	arg->job_nhosts = node_cnt;
	arg->job_hostlist = ctx->step_resp->step_layout->node_list;
	arg->job_mem_limit = 0;
	arg->job_mem_alloc = xmalloc(sizeof(uint64_t));
	arg->job_mem_alloc[0] = 0;
	arg->job_mem_alloc_rep_count = xmalloc(sizeof(uint64_t));
	arg->job_mem_alloc_rep_count[0] = node_cnt;
	arg->job_mem_alloc_size = 1;

	arg->step_hostlist = ctx->step_req->node_list;
	arg->step_mem_limit = 0;
	arg->step_mem_alloc = xmalloc(sizeof(uint64_t));
	arg->step_mem_alloc[0] = 0;
	arg->step_mem_alloc_rep_count = xmalloc(sizeof(uint64_t));
	arg->step_mem_alloc_rep_count[0] = node_cnt;
	arg->step_mem_alloc_size = 1;

	arg->job_core_bitmap = bit_alloc(node_cnt);
	bit_nset(arg->job_core_bitmap, 0, node_cnt - 1);
	arg->step_core_bitmap  = bit_alloc(node_cnt);
	bit_nset(arg->step_core_bitmap, 0, node_cnt - 1);

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

/*
 * step_ctx_create - Create a job step and its context.
 * IN step_params - job step parameters
 * IN timeout - in milliseconds
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using slurm_step_ctx_destroy.
 */
extern slurm_step_ctx_t *step_ctx_create_timeout(
	job_step_create_request_msg_t *step_req, int timeout)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	job_step_create_response_msg_t *step_resp = NULL;
	int i, rc, time_left;
	int sock = -1;
	uint16_t port = 0;
	int errnum = 0;
	int cc;
	uint16_t *ports;
	struct pollfd fds;
	long elapsed_time;
	DEF_TIMERS;

	xassert(step_req);

	/*
	 * We will handle the messages in the step_launch.c mesage handler,
	 * but we need to open the socket right now so we can tell the
	 * controller which port to use.
	 */
	if ((ports = slurm_get_srun_port_range()))
		cc = net_stream_listen_ports(&sock, &port, ports, false);
	else
		cc = net_stream_listen(&sock, &port);
	if (cc < 0) {
		error("unable to initialize step request socket: %m");
		return NULL;
	}

	step_req->port = port;

	rc = slurm_job_step_create(step_req, &step_resp);
	if ((rc < 0) && launch_common_step_retry_errno(errno)) {
		START_TIMER;
		errnum = errno;
		fds.fd = sock;
		fds.events = POLLIN;
		xsignal_unblock(step_signals);
		for (i = 0; step_signals[i]; i++)
			xsignal(step_signals[i], _signal_while_allocating);
		while (1) {
			END_TIMER;
			elapsed_time = DELTA_TIMER / 1000;
			if (elapsed_time >= timeout)
				break;
			time_left = timeout - elapsed_time;
			i = poll(&fds, 1, time_left);
			if ((i >= 0) || destroy_step)
				break;
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			break;
		}
		xsignal_block(step_signals);
		if (destroy_step) {
			info("Cancelled pending job step with signal %d",
			     destroy_step);
			errnum = ESLURM_ALREADY_DONE;
		}
		close(sock);
		errno = errnum;
	} else if ((rc < 0) || (step_resp == NULL)) {
		close(sock);
	} else {
		ctx = xmalloc(sizeof(struct slurm_step_ctx_struct));
		ctx->launch_state = NULL;
		ctx->magic	= STEP_CTX_MAGIC;
		ctx->job_id	= step_req->step_id.job_id;
		ctx->user_id	= step_req->user_id;
		ctx->step_req   = step_req;
		/*
		 * Grab the step id here if we don't already have it, we will
		 * need to to send to the slurmd.
		 */
		if (step_req->step_id.step_id == NO_VAL)
			step_req->step_id.step_id = step_resp->job_step_id;

		ctx->step_resp	= step_resp;
		ctx->launch_state = step_launch_state_create(ctx);
		ctx->launch_state->slurmctld_socket_fd = sock;
	}

	return (slurm_step_ctx_t *) ctx;
}

/*
 * step_ctx_create_no_alloc - Create a job step and its context without
 *                            getting an allocation.
 * IN step_params - job step parameters
 * IN step_id     - since we are faking it give me the id to use
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using slurm_step_ctx_destroy.
 */
extern slurm_step_ctx_t *step_ctx_create_no_alloc(
	job_step_create_request_msg_t *step_req, uint32_t step_id)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	job_step_create_response_msg_t *step_resp = NULL;
	int sock = -1;
	uint16_t port = 0;

	xassert(step_req);
	/* We will handle the messages in the step_launch.c mesage handler,
	 * but we need to open the socket right now so we can tell the
	 * controller which port to use.
	 */
	if (net_stream_listen(&sock, &port) < 0) {
		error("unable to initialize step context socket: %m");
		return NULL;
	}
	step_req->port = port;

	/* Then make up a reponse with only certain things filled in */
	step_resp = (job_step_create_response_msg_t *)
		xmalloc(sizeof(job_step_create_response_msg_t));

	step_resp->step_layout = fake_slurm_step_layout_create(
		step_req->node_list,
		NULL, NULL,
		step_req->min_nodes,
		step_req->num_tasks,
		0);

	if (switch_g_alloc_jobinfo(&step_resp->switch_job,
				   step_req->step_id.job_id,
				   step_resp->job_step_id) < 0)
		fatal("switch_g_alloc_jobinfo: %m");
	if (switch_g_build_jobinfo(step_resp->switch_job,
				   step_resp->step_layout,
				   NULL) < 0)
		fatal("switch_g_build_jobinfo: %m");

	step_resp->job_step_id = step_id;

	ctx = xmalloc(sizeof(struct slurm_step_ctx_struct));
	ctx->launch_state = NULL;
	ctx->magic	= STEP_CTX_MAGIC;
	ctx->job_id	= step_req->step_id.job_id;
	ctx->user_id	= step_req->user_id;
	ctx->step_req   = step_req;

	/*
	 * Grab the step id here if we don't already have it, we will
	 * need to to send to the slurmd.
	 */
	if (step_req->step_id.step_id == NO_VAL)
		step_req->step_id.step_id = step_resp->job_step_id;

	ctx->step_resp	= step_resp;
	ctx->launch_state = step_launch_state_create(ctx);
	ctx->launch_state->slurmctld_socket_fd = sock;

	_job_fake_cred(ctx);

	return (slurm_step_ctx_t *)ctx;
}

/*
 * step_ctx_destroy - free allocated memory for a job step context.
 * IN ctx - job step context generated by slurm_step_ctx_create
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int step_ctx_destroy(slurm_step_ctx_t *ctx)
{
	if ((ctx == NULL) || (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}
	slurm_free_job_step_create_request_msg(ctx->step_req);
	slurm_free_job_step_create_response_msg(ctx->step_resp);
	step_launch_state_destroy(ctx->launch_state);
	xfree(ctx);
	return SLURM_SUCCESS;
}
