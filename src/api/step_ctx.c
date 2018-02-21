/*****************************************************************************\
 *  step_ctx.c - step_ctx task functions for use by AIX/POE
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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
#include "src/common/timers.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/api/step_ctx.h"

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

static void
_job_fake_cred(struct slurm_step_ctx_struct *ctx)
{
	slurm_cred_arg_t arg;
	uint32_t node_cnt = ctx->step_resp->step_layout->node_cnt;

	memset(&arg, 0, sizeof(slurm_cred_arg_t));
	arg.jobid          = ctx->job_id;
	arg.stepid         = ctx->step_resp->job_step_id;
	arg.uid            = ctx->user_id;

	arg.job_nhosts     = node_cnt;
	arg.job_hostlist   = ctx->step_resp->step_layout->node_list;
	arg.job_mem_limit  = 0;

	arg.step_hostlist  = ctx->step_req->node_list;
	arg.step_mem_limit = 0;

	arg.job_gres_list     = NULL;
	arg.job_constraints   = NULL;
	arg.job_core_bitmap   = bit_alloc(node_cnt);
	bit_nset(arg.job_core_bitmap,  0, node_cnt-1);
	arg.step_core_bitmap  = bit_alloc(node_cnt);
	bit_nset(arg.step_core_bitmap, 0, node_cnt-1);

	arg.cores_per_socket = xmalloc(sizeof(uint16_t));
	arg.cores_per_socket[0] = 1;
	arg.sockets_per_node = xmalloc(sizeof(uint16_t));
	arg.sockets_per_node[0] = 1;
	arg.sock_core_rep_count = xmalloc(sizeof(uint32_t));
	arg.sock_core_rep_count[0] = node_cnt;

	ctx->step_resp->cred = slurm_cred_faker(&arg);
}

static job_step_create_request_msg_t *_create_step_request(
	const slurm_step_ctx_params_t *step_params)
{
	job_step_create_request_msg_t *step_req =
		xmalloc(sizeof(job_step_create_request_msg_t));
	step_req->job_id = step_params->job_id;
	step_req->step_id = step_params->step_id;
	step_req->user_id = (uint32_t)step_params->uid;
	step_req->min_nodes = step_params->min_nodes;
	step_req->max_nodes = step_params->max_nodes;
	step_req->cpu_count = step_params->cpu_count;
	step_req->cpu_freq_min = step_params->cpu_freq_min;
	step_req->cpu_freq_max = step_params->cpu_freq_max;
	step_req->cpu_freq_gov = step_params->cpu_freq_gov;
	step_req->num_tasks = step_params->task_count;
	step_req->relative = step_params->relative;
	step_req->resv_port_cnt = step_params->resv_port_cnt;
	step_req->exclusive  = step_params->exclusive;
	step_req->immediate  = step_params->immediate;
	step_req->ckpt_interval = step_params->ckpt_interval;
	step_req->ckpt_dir = xstrdup(step_params->ckpt_dir);
	step_req->features = xstrdup(step_params->features);
	step_req->gres = xstrdup(step_params->gres);
	step_req->task_dist = step_params->task_dist;
	step_req->plane_size = step_params->plane_size;
	step_req->node_list = xstrdup(step_params->node_list);
	step_req->network = xstrdup(step_params->network);
	step_req->name = xstrdup(step_params->name);
	step_req->no_kill = step_params->no_kill;
	step_req->overcommit = step_params->overcommit ? 1 : 0;
	step_req->pn_min_memory = step_params->pn_min_memory;
	step_req->srun_pid = (uint32_t) getpid();
	step_req->time_limit = step_params->time_limit;

	return step_req;
}

/*
 * slurm_step_ctx_create - Create a job step and its context.
 * IN step_params - job step parameters
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using slurm_step_ctx_destroy.
 */
extern slurm_step_ctx_t *
slurm_step_ctx_create (const slurm_step_ctx_params_t *step_params)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	job_step_create_request_msg_t *step_req = NULL;
	job_step_create_response_msg_t *step_resp = NULL;
	int sock = -1;
	uint16_t port = 0;
	int errnum = 0;

	/* First copy the user's step_params into a step request struct */
	step_req = _create_step_request(step_params);

	/*
	 * We will handle the messages in the step_launch.c mesage handler,
	 * but we need to open the socket right now so we can tell the
	 * controller which port to use.
	 */
	if (net_stream_listen(&sock, &port) < 0) {
		errnum = errno;
		error("unable to initialize step context socket: %m");
		slurm_free_job_step_create_request_msg(step_req);
		goto fail;
	}
	step_req->port = port;
	step_req->host = xshort_hostname();

	if ((slurm_job_step_create(step_req, &step_resp) < 0) ||
	    (step_resp == NULL)) {
		errnum = errno;
		slurm_free_job_step_create_request_msg(step_req);
		close(sock);
		goto fail;
	}

	ctx = xmalloc(sizeof(struct slurm_step_ctx_struct));
	ctx->launch_state = NULL;
	ctx->magic	= STEP_CTX_MAGIC;
	ctx->job_id	= step_req->job_id;
	ctx->user_id	= step_req->user_id;
	ctx->step_req   = step_req;
	ctx->step_resp	= step_resp;
	ctx->verbose_level = step_params->verbose_level;

	ctx->launch_state = step_launch_state_create(ctx);
	ctx->launch_state->slurmctld_socket_fd = sock;
fail:
	errno = errnum;
	return (slurm_step_ctx_t *)ctx;
}

/*
 * Return TRUE if the job step create request should be retried later
 * (i.e. the errno set by slurm_step_ctx_create_timeout() is recoverable).
 */
extern bool slurm_step_retry_errno(int rc)
{
	if ((rc == EAGAIN) ||
	    (rc == ESLURM_DISABLED) ||
	    (rc == ESLURM_INTERCONNECT_BUSY) ||
	    (rc == ESLURM_NODES_BUSY) ||
	    (rc == ESLURM_PORTS_BUSY) ||
	    (rc == ESLURM_POWER_NOT_AVAIL) ||
	    (rc == ESLURM_POWER_RESERVED) ||
	    (rc == SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT))
		return true;
	return false;
}

/*
 * slurm_step_ctx_create - Create a job step and its context.
 * IN step_params - job step parameters
 * IN timeout - in milliseconds
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using slurm_step_ctx_destroy.
 */
extern slurm_step_ctx_t *
slurm_step_ctx_create_timeout (const slurm_step_ctx_params_t *step_params,
			       int timeout)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	job_step_create_request_msg_t *step_req = NULL;
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
		error("unable to initialize step context socket: %m");
		return (slurm_step_ctx_t *) NULL;
	}

	step_req = _create_step_request(step_params);
	step_req->port = port;
	step_req->host = xshort_hostname();

	rc = slurm_job_step_create(step_req, &step_resp);
	if ((rc < 0) && slurm_step_retry_errno(errno)) {
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
		slurm_free_job_step_create_request_msg(step_req);
		close(sock);
		errno = errnum;
	} else if ((rc < 0) || (step_resp == NULL)) {
		slurm_free_job_step_create_request_msg(step_req);
		close(sock);
	} else {
		ctx = xmalloc(sizeof(struct slurm_step_ctx_struct));
		ctx->launch_state = NULL;
		ctx->magic	= STEP_CTX_MAGIC;
		ctx->job_id	= step_req->job_id;
		ctx->user_id	= step_req->user_id;
		ctx->step_req   = step_req;
		ctx->step_resp	= step_resp;
		ctx->verbose_level = step_params->verbose_level;
		ctx->launch_state = step_launch_state_create(ctx);
		ctx->launch_state->slurmctld_socket_fd = sock;
	}

	return (slurm_step_ctx_t *) ctx;
}

/*
 * slurm_step_ctx_create_no_alloc - Create a job step and its context without
 *                                  getting an allocation.
 * IN step_params - job step parameters
 * IN step_id     - since we are faking it give me the id to use
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using slurm_step_ctx_destroy.
 */
extern slurm_step_ctx_t *
slurm_step_ctx_create_no_alloc (const slurm_step_ctx_params_t *step_params,
				uint32_t step_id)
{
	struct slurm_step_ctx_struct *ctx = NULL;
	job_step_create_request_msg_t *step_req = NULL;
	job_step_create_response_msg_t *step_resp = NULL;
	int sock = -1;
	uint16_t port = 0;
	int errnum = 0;

	/* First copy the user's step_params into a step request struct */
	step_req = _create_step_request(step_params);

	/* We will handle the messages in the step_launch.c mesage handler,
	 * but we need to open the socket right now so we can tell the
	 * controller which port to use.
	 */
	if (net_stream_listen(&sock, &port) < 0) {
		errnum = errno;
		error("unable to initialize step context socket: %m");
		slurm_free_job_step_create_request_msg(step_req);
		goto fail;
	}
	step_req->port = port;
	step_req->host = xshort_hostname();

	/* Then make up a reponse with only certain things filled in */
	step_resp = (job_step_create_response_msg_t *)
		xmalloc(sizeof(job_step_create_response_msg_t));

	step_resp->step_layout = fake_slurm_step_layout_create(
		step_req->node_list,
		NULL, NULL,
		step_req->min_nodes,
		step_req->num_tasks);

	if (switch_g_alloc_jobinfo(&step_resp->switch_job,
				   step_req->job_id,
				   step_resp->job_step_id) < 0)
		fatal("switch_g_alloc_jobinfo: %m");
	if (switch_g_build_jobinfo(step_resp->switch_job,
				 step_resp->step_layout,
				 step_req->network) < 0)
		fatal("switch_g_build_jobinfo: %m");



	step_resp->job_step_id = step_id;

	ctx = xmalloc(sizeof(struct slurm_step_ctx_struct));
	ctx->launch_state = NULL;
	ctx->magic	= STEP_CTX_MAGIC;
	ctx->job_id	= step_req->job_id;
	ctx->user_id	= step_req->user_id;
	ctx->step_req   = step_req;
	ctx->step_resp	= step_resp;
	ctx->verbose_level = step_params->verbose_level;

	ctx->launch_state = step_launch_state_create(ctx);
	ctx->launch_state->slurmctld_socket_fd = sock;

	_job_fake_cred(ctx);

fail:
	errno = errnum;
	return (slurm_step_ctx_t *)ctx;
}

/*
 * slurm_step_ctx_get - get parameters from a job step context.
 * IN ctx - job step context generated by slurm_step_ctx_create
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int
slurm_step_ctx_get (slurm_step_ctx_t *ctx, int ctx_key, ...)
{
	va_list ap;
	int rc = SLURM_SUCCESS;
	uint32_t node_inx;
	uint16_t **uint16_array_pptr = (uint16_t **) NULL;
	uint32_t *uint32_ptr;
	uint32_t **uint32_array_pptr = (uint32_t **) NULL;
	uint32_t ***uint32_array_ppptr = (uint32_t ***) NULL;
	char **char_array_pptr = (char **) NULL;
	job_step_create_response_msg_t ** step_resp_pptr;
	slurm_cred_t  **cred;     /* Slurm job credential    */
	dynamic_plugin_data_t **switch_job;
	int *int_ptr;
	int **int_array_pptr = (int **) NULL;

	if ((ctx == NULL) || (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	va_start(ap, ctx_key);
	switch (ctx_key) {
	case SLURM_STEP_CTX_JOBID:
		uint32_ptr = (uint32_t *) va_arg(ap, void *);
		*uint32_ptr = ctx->job_id;
		break;
	case SLURM_STEP_CTX_STEPID:
		uint32_ptr = (uint32_t *) va_arg(ap, void *);
		*uint32_ptr = ctx->step_resp->job_step_id;
		break;
	case SLURM_STEP_CTX_TASKS:
		uint16_array_pptr = (uint16_t **) va_arg(ap, void *);
		*uint16_array_pptr = ctx->step_resp->step_layout->tasks;
		break;

	case SLURM_STEP_CTX_TID:
		node_inx = va_arg(ap, uint32_t);
		if (node_inx > ctx->step_resp->step_layout->node_cnt) {
			slurm_seterrno(EINVAL);
			rc = SLURM_ERROR;
			break;
		}
		uint32_array_pptr = (uint32_t **) va_arg(ap, void *);
		*uint32_array_pptr =
			ctx->step_resp->step_layout->tids[node_inx];
		break;
	case SLURM_STEP_CTX_TIDS:
		uint32_array_ppptr = (uint32_t ***) va_arg(ap, void *);
		*uint32_array_ppptr = ctx->step_resp->step_layout->tids;
		break;

	case SLURM_STEP_CTX_RESP:
		step_resp_pptr = (job_step_create_response_msg_t **)
			va_arg(ap, void *);
		*step_resp_pptr = ctx->step_resp;
		break;
	case SLURM_STEP_CTX_CRED:
		cred = (slurm_cred_t **) va_arg(ap, void *);
		*cred = ctx->step_resp->cred;
		break;
	case SLURM_STEP_CTX_SWITCH_JOB:
		switch_job = (dynamic_plugin_data_t **) va_arg(ap, void *);
		*switch_job = ctx->step_resp->switch_job;
		break;
	case SLURM_STEP_CTX_NUM_HOSTS:
		uint32_ptr = (uint32_t *) va_arg(ap, void *);
		*uint32_ptr = ctx->step_resp->step_layout->node_cnt;
		break;
	case SLURM_STEP_CTX_HOST:
		node_inx = va_arg(ap, uint32_t);
		if (node_inx > ctx->step_resp->step_layout->node_cnt) {
			slurm_seterrno(EINVAL);
			rc = SLURM_ERROR;
			break;
		}
		char_array_pptr = (char **) va_arg(ap, void *);
		*char_array_pptr = nodelist_nth_host(
			ctx->step_resp->step_layout->node_list, node_inx);
		break;
	case SLURM_STEP_CTX_NODE_LIST:
		char_array_pptr = (char **) va_arg(ap, void *);
		*char_array_pptr =
			xstrdup(ctx->step_resp->step_layout->node_list);
		break;
	case SLURM_STEP_CTX_USER_MANAGED_SOCKETS:
		int_ptr = va_arg(ap, int *);
		int_array_pptr = va_arg(ap, int **);

		if (ctx->launch_state == NULL
		    || ctx->launch_state->user_managed_io == false
		    || ctx->launch_state->io.user == NULL) {
			*int_ptr = 0;
			*int_array_pptr = (int *)NULL;
			rc = SLURM_ERROR;
			break;
		}
		*int_ptr = ctx->launch_state->tasks_requested;
		*int_array_pptr = ctx->launch_state->io.user->sockets;
		break;
	default:
		slurm_seterrno(EINVAL);
		rc = SLURM_ERROR;
	}
	va_end(ap);

	return rc;
}

/*
 * slurm_jobinfo_ctx_get - get parameters from jobinfo context.
 * IN jobinfo - job information from context, returned by slurm_step_ctx_get()
 * IN data_type - type of data required, specific to the switch type
 * OUT data - the requested data type
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int
slurm_jobinfo_ctx_get(dynamic_plugin_data_t *jobinfo, int data_type, void *data)
{
	if (jobinfo == NULL) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	return switch_g_get_jobinfo(jobinfo, data_type, data);
}


/*
 * slurm_step_ctx_destroy - free allocated memory for a job step context.
 * IN ctx - job step context generated by slurm_step_ctx_create
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int
slurm_step_ctx_destroy (slurm_step_ctx_t *ctx)
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

/*
 * slurm_step_ctx_daemon_per_node_hack - Hack the step context
 *	to run a single process per node, regardless of the settings
 *	selected at slurm_step_ctx_create time.
 *
 *	This is primarily used when launching 1 task per node as done
 * 	with IBM's PE where we want to launch a single pmd daemon
 *	on each node regardless of the number of tasks running on each
 *	node.
 * IN ctx - job step context generated by slurm_step_ctx_create
 * IN node_list - node list of nodes to run on
 * IN node_cnt - number of nodes to run on
 * IN/OUT curr_task_num - task_id of newest task, initialze to zero
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int
slurm_step_ctx_daemon_per_node_hack(
	slurm_step_ctx_t *ctx, char *node_list, uint32_t node_cnt,
	uint32_t *curr_task_num)
{
	slurm_step_layout_t *layout;
	int i, orig_task_num = *curr_task_num;
	int sock = -1;

	if ((ctx == NULL) || (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	if (!orig_task_num) {
		/* hack the context step layout */
		sock = ctx->launch_state->slurmctld_socket_fd;
		slurm_step_layout_destroy(ctx->step_resp->step_layout);
		ctx->step_resp->step_layout =
			xmalloc(sizeof(slurm_step_layout_t));
		layout = ctx->step_resp->step_layout;

		layout->tasks = xmalloc(sizeof(uint16_t) * node_cnt);
		layout->tids = xmalloc(sizeof(uint32_t *) * node_cnt);
	} else {
		layout = ctx->step_resp->step_layout;
		xrealloc(layout->tasks, sizeof(uint16_t) * node_cnt);
		xrealloc(layout->tids, sizeof(uint32_t *) * node_cnt);
	}

	ctx->step_req->num_tasks = layout->task_cnt = layout->node_cnt
		= node_cnt;

	xfree(layout->node_list);
	layout->node_list = xstrdup(node_list);
	for (i = orig_task_num; i < layout->node_cnt; i++) {
		layout->tasks[i] = 1;
		layout->tids[i] = (uint32_t *)xmalloc(sizeof(uint32_t));
		layout->tids[i][0] = (*curr_task_num)++;
	}

	/* Alter the launch state structure now that the settings
	 * have changed */
	if (!ctx->launch_state) {
		ctx->launch_state = step_launch_state_create(ctx);
		ctx->launch_state->slurmctld_socket_fd = sock;
	} else
		step_launch_state_alter(ctx);

	return SLURM_SUCCESS;
}

/*
 * slurm_step_ctx_params_t_init - This initializes parameters
 *	in the structure that you will pass to slurm_step_ctx_create().
 *	This function will NOT allocate any new memory.
 * IN ptr - pointer to a structure allocated by the user.  The structure will
 *      be initialized.
 */
extern void slurm_step_ctx_params_t_init (slurm_step_ctx_params_t *ptr)
{
	char *jobid_str;

	/* zero the entire structure */
	memset(ptr, 0, sizeof(slurm_step_ctx_params_t));

	/* now set anything that shouldn't be 0 or NULL by default */
	ptr->relative = NO_VAL16;
	ptr->task_dist = SLURM_DIST_CYCLIC;
	ptr->plane_size = NO_VAL16;
	ptr->resv_port_cnt = NO_VAL16;
	ptr->step_id = NO_VAL;

	ptr->uid = getuid();

	if ((jobid_str = getenv("SLURM_JOB_ID")) != NULL) {
		ptr->job_id = (uint32_t)atol(jobid_str);
	} else if ((jobid_str = getenv("SLURM_JOBID")) != NULL) {
		/* handle old style env variable for backwards compatibility */
		ptr->job_id = (uint32_t)atol(jobid_str);
	} else {
		ptr->job_id = NO_VAL;
	}
}

