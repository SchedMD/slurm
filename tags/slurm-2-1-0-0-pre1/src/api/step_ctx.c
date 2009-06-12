/*****************************************************************************\
 *  step_ctx.c - step_ctx task functions for use by AIX/POE
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <slurm/slurm.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/net.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_cred.h"
#include "src/api/step_ctx.h"

static void
_job_fake_cred(struct slurm_step_ctx_struct *ctx)
{
	slurm_cred_arg_t arg;
	uint32_t node_cnt = ctx->step_resp->step_layout->node_cnt;

	arg.hostlist      = ctx->step_req->node_list;
	arg.job_mem       = 0;
	arg.jobid         = ctx->job_id;
	arg.stepid        = ctx->step_resp->job_step_id;
	arg.uid           = ctx->user_id;
	arg.core_bitmap   = bit_alloc(node_cnt);
	bit_nset(arg.core_bitmap, 0, node_cnt-1);
	arg.cores_per_socket = xmalloc(sizeof(uint16_t));
	arg.cores_per_socket[0] = 1;
	arg.sockets_per_node = xmalloc(sizeof(uint16_t));
	arg.sockets_per_node[0] = 1;
	arg.sock_core_rep_count = xmalloc(sizeof(uint32_t));
	arg.sock_core_rep_count[0] = node_cnt;
	arg.job_nhosts    = node_cnt;
	arg.job_hostlist  = ctx->step_resp->step_layout->node_list;
	ctx->step_resp->cred = slurm_cred_faker(&arg);
}

static job_step_create_request_msg_t *_create_step_request(
	const slurm_step_ctx_params_t *step_params)
{
	job_step_create_request_msg_t *step_req = 
		xmalloc(sizeof(job_step_create_request_msg_t));
	step_req->job_id = step_params->job_id;
	step_req->user_id = (uint32_t)step_params->uid;
	step_req->node_count = step_params->node_count;
	step_req->cpu_count = step_params->cpu_count;
	step_req->num_tasks = step_params->task_count;
	step_req->relative = step_params->relative;
	step_req->resv_port_cnt = step_params->resv_port_cnt;
	step_req->exclusive  = step_params->exclusive;
	step_req->immediate  = step_params->immediate;
	step_req->ckpt_interval = step_params->ckpt_interval;
	step_req->ckpt_dir = xstrdup(step_params->ckpt_dir);
	step_req->task_dist = step_params->task_dist;
	step_req->plane_size = step_params->plane_size;
	step_req->node_list = xstrdup(step_params->node_list);
	step_req->network = xstrdup(step_params->network);
	step_req->name = xstrdup(step_params->name);
	step_req->no_kill = step_params->no_kill;
	step_req->overcommit = step_params->overcommit ? 1 : 0;
	step_req->mem_per_task = step_params->mem_per_task;
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
	short port = 0;
	int errnum = 0;
	
	/* First copy the user's step_params into a step request
	 * struct */
	step_req = _create_step_request(step_params);

	/* We will handle the messages in the step_launch.c mesage handler,
	 * but we need to open the socket right now so we can tell the
	 * controller which port to use.
	 */
	if (net_stream_listen(&sock, &port) < 0) {
		errnum = errno;
		error("unable to intialize step context socket: %m");
		slurm_free_job_step_create_request_msg(step_req);
		goto fail;
	}
	step_req->port = port;
	step_req->host = xshort_hostname();

	if ((slurm_job_step_create(step_req, &step_resp) < 0) ||
	    (step_resp == NULL)) {
		errnum = errno;
		slurm_free_job_step_create_request_msg(step_req);
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
	short port = 0;
	int errnum = 0;
	int cyclic = (step_params->task_dist == SLURM_DIST_CYCLIC);
	
	/* First copy the user's step_params into a step request struct */
	step_req = _create_step_request(step_params);

	/* We will handle the messages in the step_launch.c mesage handler,
	 * but we need to open the socket right now so we can tell the
	 * controller which port to use.
	 */
	if (net_stream_listen(&sock, &port) < 0) {
		errnum = errno;
		error("unable to intialize step context socket: %m");
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
		step_req->node_count,
		step_req->num_tasks);
	
	if (switch_alloc_jobinfo(&step_resp->switch_job) < 0)
		fatal("switch_alloc_jobinfo: %m");
	if (switch_build_jobinfo(step_resp->switch_job, 
				 step_resp->step_layout->node_list, 
				 step_resp->step_layout->tasks, 
				 cyclic, step_req->network) < 0)
		fatal("switch_build_jobinfo: %m");



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
	char **char_array_pptr = (char **) NULL;
	job_step_create_response_msg_t ** step_resp_pptr;
	slurm_cred_t  *cred;     /* Slurm job credential    */
	switch_jobinfo_t *switch_job;
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
		if ((node_inx < 0)
		    || (node_inx > ctx->step_resp->step_layout->node_cnt)) {
			slurm_seterrno(EINVAL);
			rc = SLURM_ERROR;
			break;
		}
		uint32_array_pptr = (uint32_t **) va_arg(ap, void *);
		*uint32_array_pptr =
			ctx->step_resp->step_layout->tids[node_inx];
		break;
		
	case SLURM_STEP_CTX_RESP:
		step_resp_pptr = (job_step_create_response_msg_t **) 
			va_arg(ap, void *);
		*step_resp_pptr = ctx->step_resp;
		break;
	case SLURM_STEP_CTX_CRED:
		cred = (slurm_cred_t *) va_arg(ap, void *);
		*cred = ctx->step_resp->cred;
		break;
	case SLURM_STEP_CTX_SWITCH_JOB:
		switch_job = (switch_jobinfo_t *) va_arg(ap, void *);
		*switch_job = ctx->step_resp->switch_job;
		break;
	case SLURM_STEP_CTX_NUM_HOSTS:
		uint32_ptr = (uint32_t *) va_arg(ap, void *);
		*uint32_ptr = ctx->step_resp->step_layout->node_cnt;
		break;
	case SLURM_STEP_CTX_HOST:
		node_inx = va_arg(ap, uint32_t);
		if ((node_inx < 0)
		    || (node_inx > ctx->step_resp->step_layout->node_cnt)) {
			slurm_seterrno(EINVAL);
			rc = SLURM_ERROR;
			break;
		}
		char_array_pptr = (char **) va_arg(ap, void *);
		*char_array_pptr = nodelist_nth_host(
			ctx->step_resp->step_layout->node_list, node_inx);
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
slurm_jobinfo_ctx_get(switch_jobinfo_t jobinfo, int data_type, void *data)
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
 *	This is primarily used on AIX by the slurm_ll_api in support of
 * 	poe.  The slurm_ll_api will want to launch a single pmd daemon
 *	on each node regardless of the number of tasks running on each
 *	node.
 * IN ctx - job step context generated by slurm_step_ctx_create
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int
slurm_step_ctx_daemon_per_node_hack(slurm_step_ctx_t *ctx)
{
	slurm_step_layout_t *new_layout, *old_layout;
	int i;

	if ((ctx == NULL) || (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	/* hack the context node count */
	ctx->step_req->num_tasks = ctx->step_req->node_count;

	/* hack the context step layout */
	old_layout = ctx->step_resp->step_layout;
	new_layout = (slurm_step_layout_t *)
		     xmalloc(sizeof(slurm_step_layout_t));
	new_layout->node_cnt = old_layout->node_cnt;
	new_layout->task_cnt = old_layout->node_cnt;
	new_layout->node_list = xstrdup(old_layout->node_list);
	slurm_step_layout_destroy(old_layout);
	new_layout->tasks = (uint16_t *) xmalloc(sizeof(uint16_t) * 
						 new_layout->node_cnt);
	new_layout->tids = (uint32_t **) xmalloc(sizeof(uint32_t *) * 
						 new_layout->node_cnt);
	for (i = 0; i < new_layout->node_cnt; i++) {
		new_layout->tasks[i] = 1;
		new_layout->tids[i] = (uint32_t *)xmalloc(sizeof(uint32_t));
		new_layout->tids[i][0] = i;
	}
	ctx->step_resp->step_layout = new_layout;

	/* recreate the launch state structure now that the settings
	   have changed */
	step_launch_state_destroy(ctx->launch_state);
	ctx->launch_state = step_launch_state_create(ctx);

	return SLURM_SUCCESS;
}

/* 
 * slurm_step_ctx_params_t_init - This initializes parameters
 *	in the structure that you will pass to slurm_step_ctx_create().
 *	This function will NOT allocate any new memory.
 * IN ptr - pointer to a structure allocated by the user.  The structure will
 *      be intialized.
 */
extern void slurm_step_ctx_params_t_init (slurm_step_ctx_params_t *ptr)
{
	char *jobid_str;

	/* zero the entire structure */
	memset(ptr, 0, sizeof(slurm_step_ctx_params_t));

	/* now set anything that shouldn't be 0 or NULL by default */
	ptr->relative = (uint16_t)NO_VAL;
	ptr->task_dist = SLURM_DIST_CYCLIC;
	ptr->plane_size = (uint16_t)NO_VAL;
	ptr->resv_port_cnt = (uint16_t)NO_VAL;

	ptr->uid = getuid();

	if ((jobid_str = getenv("SLURM_JOB_ID")) != NULL) {
		ptr->job_id = (uint32_t)atol(jobid_str);
	} else if ((jobid_str = getenv("SLURM_JOBID")) != NULL) {
		/* handle old style env variable for backwards compatibility */
		ptr->job_id = (uint32_t)atol(jobid_str);
	} else {
		ptr->job_id = (uint32_t)NO_VAL;
	}
}

