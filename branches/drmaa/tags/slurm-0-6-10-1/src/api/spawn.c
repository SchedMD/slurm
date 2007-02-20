/*****************************************************************************\
 *  spawn.c - spawn task functions for use by AIX/POE
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-2002-040.
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

#include "src/common/hostlist.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define _DEBUG 0
#define _MAX_THREAD_COUNT 50
#define STEP_CTX_MAGIC 0xc7a3

extern char **environ;

struct slurm_step_ctx_struct {
	uint16_t magic;	/* magic number */

	uint32_t job_id;	/* assigned job id */
	uint32_t user_id;	/* user the job runs as */
	uint32_t num_tasks;	/* number of tasks to execute */
	uint16_t task_dist;	/* see enum task_dist_state */

	resource_allocation_response_msg_t *alloc_resp;
	job_step_create_response_msg_t *step_resp;

	char *cwd;		/* working directory */
	uint32_t argc;		/* count of arguments */
	char **argv;		/* argument list */
	uint16_t env_set;	/* flag if user set env */
	uint32_t envc;		/* count of env vars */
	char **env;		/* environment variables */

	char **host;		/* name for each host */
	uint32_t *cpus;		/* count of processors on each host */
	uint32_t *tasks;	/* number of tasks on each host */
	uint32_t **tids;	/* host id => task id mapping */
	hostlist_t hl;		/* hostlist of assigned nodes */
	uint32_t nhosts;	/* node count */
};

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;
typedef struct thd {
        pthread_t	thread;		/* thread ID */
	pthread_attr_t	attr;		/* pthread attributes */
        state_t		state;		/* thread state */
	time_t		tstart;		/* time thread started */
	slurm_msg_t *	req;		/* the message to send */
} thd_t;

static pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t thread_cond   = PTHREAD_COND_INITIALIZER;
static uint32_t threads_active = 0;	/* currently active threads */

#if _DEBUG
static void	_dump_ctx(slurm_step_ctx ctx);
#endif
static int	_envcount(char **env);
static void	_free_char_array(char ***argv_p, int cnt);
static int	_p_launch(slurm_msg_t *req, slurm_step_ctx ctx);
static int	_sock_bind_wild(int sockfd);
static int	_task_layout(slurm_step_ctx ctx);
static int	_task_layout_block(slurm_step_ctx ctx);
static int	_task_layout_cyclic(slurm_step_ctx ctx);
static void *	_thread_per_node_rpc(void *args);
static int	_validate_ctx(slurm_step_ctx ctx);
static void	_xcopy_char_array(char ***argv_p, char **argv, int cnt);
static void	_xfree_char_array(char ***argv_p, int cnt);

/*
 * slurm_step_ctx_create - Create a job step and its context. 
 * IN step_req - description of job step request
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using slurm_step_ctx_destroy.
 */
extern slurm_step_ctx
slurm_step_ctx_create (job_step_create_request_msg_t *step_req)
{
	struct slurm_step_ctx_struct *rc;
	old_job_alloc_msg_t old_job_req;
	job_step_create_response_msg_t *step_resp = NULL;
	resource_allocation_response_msg_t *alloc_resp;

	old_job_req.job_id	= step_req->job_id;
	old_job_req.uid		= getuid();
	if (slurm_confirm_allocation(&old_job_req, &alloc_resp) < 0)
		return NULL;

	if ((slurm_job_step_create(step_req, &step_resp) < 0) ||
	    (step_resp == NULL)) {
		slurm_free_resource_allocation_response_msg(alloc_resp);
		return NULL;	/* slurm errno already set */
	}

	rc = xmalloc(sizeof(struct slurm_step_ctx_struct));
	rc->magic	= STEP_CTX_MAGIC;
	rc->job_id	= step_req->job_id;
	rc->user_id	= step_req->user_id;
	rc->num_tasks	= step_req->num_tasks;
	rc->task_dist	= step_req->task_dist;
	rc->step_resp	= step_resp;
	rc->alloc_resp	= alloc_resp;

	rc->hl		= hostlist_create(rc->step_resp->node_list);
	rc->nhosts	= hostlist_count(rc->hl);
	(void) _task_layout(rc);

	return rc;
}

/*
 * slurm_step_ctx_get - get parameters from a job step context.
 * IN ctx - job step context generated by slurm_step_ctx_create
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int
slurm_step_ctx_get (slurm_step_ctx ctx, int ctx_key, ...)
{
	va_list ap;
	int rc = SLURM_SUCCESS;
	uint32_t node_inx;
	uint32_t *step_id_ptr;
	uint32_t **array_pptr = (uint32_t **) NULL;
	job_step_create_response_msg_t ** step_resp_pptr;

	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	va_start(ap, ctx_key);
	switch (ctx_key) {
		case SLURM_STEP_CTX_STEPID:
			step_id_ptr = (uint32_t *) va_arg(ap, void *);
			*step_id_ptr = ctx->step_resp->job_step_id;
			break;
		case SLURM_STEP_CTX_TASKS:
			array_pptr = (uint32_t **) va_arg(ap, void *);
			*array_pptr = ctx->tasks;
			break;

		case SLURM_STEP_CTX_TID:
			node_inx = va_arg(ap, uint32_t);
			if ((node_inx < 0) || (node_inx > ctx->nhosts)) {
				slurm_seterrno(EINVAL);
				rc = SLURM_ERROR;
				break;
			}
			array_pptr = (uint32_t **) va_arg(ap, void *);
			*array_pptr = ctx->tids[node_inx];
			break;

		case SLURM_STEP_CTX_RESP:
			step_resp_pptr = (job_step_create_response_msg_t **) 
				va_arg(ap, void *);
			*step_resp_pptr = ctx->step_resp;
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
 * slurm_step_ctx_set - set parameters in job step context.
 * IN ctx - job step context generated by slurm_step_ctx_create
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int
slurm_step_ctx_set (slurm_step_ctx ctx, int ctx_key, ...)
{
	va_list ap;
	int rc = SLURM_SUCCESS;

	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	va_start(ap, ctx_key);
	switch (ctx_key) {
		case SLURM_STEP_CTX_ARGS:
			if (ctx->argv)
				_xfree_char_array(&ctx->argv, ctx->argc);
			ctx->argc = va_arg(ap, int);
			if ((ctx->argc < 1) || (ctx->argc > 1024)) {
				slurm_seterrno(EINVAL);
				break;
			}
			_xcopy_char_array(&ctx->argv, va_arg(ap, char **), 
					ctx->argc);
			break;

		case SLURM_STEP_CTX_CHDIR:
			if (ctx->cwd)
				xfree(ctx->cwd);
			ctx->cwd = xstrdup(va_arg(ap, char *));
			break;

		case SLURM_STEP_CTX_ENV:
			ctx->env_set = 1;
			if (ctx->env)
				_xfree_char_array(&ctx->env, ctx->envc);
			ctx->envc = va_arg(ap, int);
			if ((ctx->envc < 1) || (ctx->envc > 1024)) {
				slurm_seterrno(EINVAL);
				break;
			}
			_xcopy_char_array(&ctx->env, va_arg(ap, char **), 
					ctx->envc);
			break;

		default:
			slurm_seterrno(EINVAL);
			rc = SLURM_ERROR;
	}
	va_end(ap);

	return rc;
}


/*
 * slurm_step_ctx_destroy - free allocated memory for a job step context.
 * IN ctx - job step context generated by slurm_step_ctx_create
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int
slurm_step_ctx_destroy (slurm_step_ctx ctx)
{
	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	if (ctx->step_resp)
		slurm_free_job_step_create_response_msg(ctx->step_resp);

	if (ctx->alloc_resp)
		slurm_free_resource_allocation_response_msg(ctx->alloc_resp);

	if (ctx->argv)
		_xfree_char_array(&ctx->argv, ctx->argc);
	if (ctx->env_set)
		_xfree_char_array(&ctx->env, ctx->envc);

	if (ctx->host)
		_free_char_array(&ctx->host, ctx->nhosts);

	if (ctx->hl)
		hostlist_destroy(ctx->hl);

	if (ctx->cpus)
		xfree(ctx->cpus);
	if (ctx->tasks)
		xfree(ctx->tasks);

	if (ctx->tids) {
		int i;
		for (i=0; i<ctx->nhosts; i++) {
			if (ctx->tids[i])
				xfree(ctx->tids[i]);
		}
	}

	xfree(ctx);
	return SLURM_SUCCESS;
}


/*
 * slurm_spawn - spawn tasks for the given job step context
 * IN ctx - job step context generated by slurm_step_ctx_create
 * IN fd_array  - array of socket file descriptors to connect with 
 *	stdin, stdout, and stderr of spawned task
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int slurm_spawn (slurm_step_ctx ctx, int *fd_array)
{
	spawn_task_request_msg_t *msg_array_ptr;
	int *sock_array;
	slurm_msg_t *req_array_ptr;
	int i, rc = SLURM_SUCCESS;
	uint16_t slurmd_debug = 0;
	char *env_var;

	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC) ||
	    (fd_array == NULL)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	if (_validate_ctx(ctx))
		return SLURM_ERROR;

	/* get slurmd_debug level from SLURMD_DEBUG env var */
	env_var = getenv("SLURMD_DEBUG");
	if (env_var) {
		i = atoi(env_var);
		if (i >= 0)
			slurmd_debug = i;
	}

	/* validate fd_array and bind them to ports */
	sock_array = xmalloc(ctx->nhosts * sizeof(int));
	for (i=0; i<ctx->nhosts; i++) {
		if (fd_array[i] < 0) {
			slurm_seterrno(EINVAL);
			free(sock_array);
			return SLURM_ERROR;
		}
		sock_array[i] = _sock_bind_wild(fd_array[i]);
		if (sock_array[i] < 0) {
			slurm_seterrno(EINVAL);
			free(sock_array);
			return SLURM_ERROR;
		}
		listen(fd_array[i], 5);
	}

	msg_array_ptr = xmalloc(sizeof(spawn_task_request_msg_t) *
			ctx->nhosts);
	req_array_ptr = xmalloc(sizeof(slurm_msg_t) * ctx->nhosts);
	for (i=0; i<ctx->nhosts; i++) {
		spawn_task_request_msg_t *r = &msg_array_ptr[i];
		slurm_msg_t              *m = &req_array_ptr[i];

		/* Common message contents */
		r->job_id	= ctx->job_id;
		r->uid		= ctx->user_id;
		r->argc		= ctx->argc;
		r->argv		= ctx->argv;
		r->cred		= ctx->step_resp->cred;
		r->job_step_id	= ctx->step_resp->job_step_id;
		r->envc		= ctx->envc;
		r->env		= ctx->env;
		r->cwd		= ctx->cwd;
		r->nnodes	= ctx->nhosts;
		r->nprocs	= ctx->num_tasks;
		r->switch_job	= ctx->step_resp->switch_job; 
		r->slurmd_debug	= slurmd_debug;

		/* Task specific message contents */
		r->global_task_id	= ctx->tids[i][0];
		r->cpus_allocated	= ctx->cpus[i];
		r->srun_node_id	= (uint32_t) i;
		r->io_port	= ntohs(sock_array[i]);
		m->msg_type	= REQUEST_SPAWN_TASK;
		m->data		= r;
		memcpy(&m->address, &ctx->alloc_resp->node_addr[i], 
			sizeof(slurm_addr));
#if		_DEBUG
		printf("tid=%d, fd=%d, port=%u, node_id=%u\n",
			ctx->tids[i][0], fd_array[i], r->io_port, i);
#endif
	}

	rc = _p_launch(req_array_ptr, ctx);

	xfree(msg_array_ptr);
	xfree(req_array_ptr);
	xfree(sock_array);

	return rc;
}


/*
 * slurm_spawn_kill - send the specified signal to an existing job step
 * IN ctx - job step context generated by slurm_step_ctx_create
 * IN signal  - signal number
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int 
slurm_spawn_kill (slurm_step_ctx ctx, uint16_t signal)
{
	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	return slurm_kill_job_step (ctx->job_id, 
			ctx->step_resp->job_step_id, signal);
}


static int _sock_bind_wild(int sockfd)
{
	socklen_t len;
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(0);	/* bind ephemeral port */

	if (bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0)
		return (-1);
	len = sizeof(sin);
	if (getsockname(sockfd, (struct sockaddr *) &sin, &len) < 0)
		return (-1);
	return (sin.sin_port);
}


/* validate the context of ctx, set default values as needed */
static int _validate_ctx(slurm_step_ctx ctx)
{
	int rc = SLURM_SUCCESS;

	if (ctx->cwd == NULL) {
		ctx->cwd = xmalloc(MAXPATHLEN);
		if (ctx->cwd == NULL) {
			slurm_seterrno(ENOMEM);
			return SLURM_ERROR;
		}
		getcwd(ctx->cwd, MAXPATHLEN);
	}

	if ((ctx->env_set == 0) && (ctx->envc == 0)) {
		ctx->envc	= _envcount(environ);
		ctx->env	= environ;
	}

#if _DEBUG
	_dump_ctx(ctx);
#endif
	return rc;
}


/* build maps for task layout on nodes */
static int _task_layout(slurm_step_ctx ctx)
{
	int cpu_cnt = 0, cpu_inx = 0, i;

	if (ctx->cpus)	/* layout already completed */
		return SLURM_SUCCESS;

	ctx->cpus  = xmalloc(sizeof(uint32_t) * ctx->nhosts);
	ctx->tasks = xmalloc(sizeof(uint32_t) * ctx->nhosts);
	ctx->host  = xmalloc(sizeof(char *)   * ctx->nhosts);
	if ((ctx->cpus == NULL) || (ctx->tasks == NULL) ||
	    (ctx->host == NULL)) {
		slurm_seterrno(ENOMEM);
		return SLURM_ERROR;
	}

	for (i=0; i<ctx->nhosts; i++) {
		ctx->host[i] = hostlist_shift(ctx->hl);
		ctx->cpus[i] = ctx->alloc_resp->cpus_per_node[cpu_inx];
		if ((++cpu_cnt) >= ctx->alloc_resp->cpu_count_reps[cpu_inx]) {
			/* move to next record */
			cpu_inx++;
			cpu_cnt = 0;
		}
	}

	ctx->tasks = xmalloc(sizeof(uint32_t)   * ctx->nhosts);
	ctx->tids  = xmalloc(sizeof(uint32_t *) * ctx->nhosts);
	if ((ctx->tasks == NULL) || (ctx->tids == NULL)) {
		slurm_seterrno(ENOMEM);
		return SLURM_ERROR;
	}

	if (ctx->task_dist == SLURM_DIST_CYCLIC)
		return _task_layout_cyclic(ctx);
	else
		return _task_layout_block(ctx);
}


/* to effectively deal with heterogeneous nodes, we fake a cyclic
 * distribution to figure out how many tasks go on each node and
 * then make those assignments in a block fashion */
static int _task_layout_block(slurm_step_ctx ctx)
{
	int i, j, taskid = 0;
	bool over_subscribe = false;

	/* figure out how many tasks go to each node */
	for (j=0; (taskid<ctx->num_tasks); j++) {   /* cycle counter */
		bool space_remaining = false;
		for (i=0; ((i<ctx->nhosts) && (taskid<ctx->num_tasks)); i++) {
			if ((j<ctx->cpus[i]) || over_subscribe) {
				taskid++;
				ctx->tasks[i]++;
				if ((j+1) < ctx->cpus[i])
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}

	/* now distribute the tasks */
	taskid = 0;
	for (i=0; i < ctx->nhosts; i++) {
		ctx->tids[i] = xmalloc(sizeof(uint32_t) * ctx->tasks[i]);
		if (ctx->tids[i] == NULL) {
			slurm_seterrno(ENOMEM);
			return SLURM_ERROR;
		}
		for (j=0; j<ctx->tasks[i]; j++)
			ctx->tids[i][j] = taskid++;
	}
	return SLURM_SUCCESS;
}


/* distribute tasks across available nodes: allocate tasks to nodes
 * in a cyclic fashion using available processors. once all available
 * processors are allocated, continue to allocate task over-subscribing
 * nodes as needed. for example
 * cpus per node        4  2  4  2
 *                     -- -- -- --
 * task distribution:   0  1  2  3
 *                      4  5  6  7
 *                      8     9
 *                     10    11     all processors allocated now
 *                     12 13 14 15  etc.
 */
static int _task_layout_cyclic(slurm_step_ctx ctx)
{
	int i, j, taskid = 0;
	bool over_subscribe = false;

	for (i=0; i<ctx->nhosts; i++) {
		ctx->tids[i] = xmalloc(sizeof(uint32_t) * ctx->num_tasks);
		if (ctx->tids[i] == NULL) {
			slurm_seterrno(ENOMEM);
			return SLURM_ERROR;
		}
	}
	for (j=0; taskid<ctx->num_tasks; j++) {   /* cycle counter */
		bool space_remaining = false;
		for (i=0; ((i<ctx->nhosts) && (taskid<ctx->num_tasks)); i++) {
			if ((j<ctx->cpus[i]) || over_subscribe) {
				ctx->tids[i][ctx->tasks[i]] = taskid++;
				ctx->tasks[i]++;
				if ((j+1) < ctx->cpus[i])
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}
	return SLURM_SUCCESS;
}


/* return number of elements in environment 'env' */
static int _envcount(char **env)
{
	int envc = 0;
	while (env[envc] != NULL)
		envc++;
	return (envc);
}


#if _DEBUG
/* dump the contents of a job step context */
static void	_dump_ctx(slurm_step_ctx ctx)
{
	int i, j;

	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC)) {
		printf("Invalid _dump_ctx argument\n");
		return;
	}

	printf("job_id    = %u\n", ctx->job_id);
	printf("user_id   = %u\n", ctx->user_id);
	printf("nhosts    = %u\n", ctx->nhosts);
	printf("num_tasks = %u\n", ctx->num_tasks);
	printf("task_dist = %u\n", ctx->task_dist);

	printf("step_id   = %u\n", ctx->step_resp->job_step_id);
	printf("nodelist  = %s\n", ctx->step_resp->node_list);

	printf("cws       = %s\n", ctx->cwd);

	for (i=0; i<ctx->argc; i++) {
		printf("argv[%d]   = %s\n", i, ctx->argv[i]);
		if (i > 5) {
			printf("...\n");
			break;
		}
	}

	for (i=0; i<ctx->envc; i++) {
		if (strlen(ctx->env[i]) > 50)
			printf("env[%d]    = %.50s...\n", i, ctx->env[i]);
		else
			printf("env[%d]    = %s\n", i, ctx->env[i]);
		if (i > 5) {
			printf("...\n");
			break;
		}
	}

	for (i=0; i<ctx->nhosts; i++) {
		printf("host=%s cpus=%u tasks=%u",
			ctx->host[i], ctx->cpus[i], ctx->tasks[i]);
		for (j=0; j<ctx->tasks[i]; j++)
			printf(" tid[%d]=%u", j, ctx->tids[i][j]);
		printf("\n");
	}

	printf("\n");
}
#endif


/* xfree an array of character strings as created by _xcopy_char_array */
static void _xfree_char_array(char ***argv_p, int cnt)
{
	char **argv = *argv_p;
	int i;

	for (i=0; i<cnt; i++)
		xfree(argv[i]);
	xfree(*argv_p);
}


/* free an array of character strings as created by hostlist_shift */
static void _free_char_array(char ***argv_p, int cnt)
{
	char **argv = *argv_p;
	int i;

	for (i=0; i<cnt; i++)
		free(argv[i]); 
	xfree(*argv_p);
}


/* copy a character array, free with _xfree_char_array */
static void _xcopy_char_array(char ***argv_p, char **argv, int cnt)
{
	int i;
	char **tmp = xmalloc(sizeof(char *) * cnt);

	for (i=0; i<cnt; i++)
		tmp[i] = xstrdup(argv[i]);

	*argv_p = tmp;
}


/* parallel (multi-threaded) task launch, 
 * transmits all RPCs in parallel with timeout */
static int _p_launch(slurm_msg_t *req, slurm_step_ctx ctx)
{
	int rc = SLURM_SUCCESS, i;
	thd_t *thd;

	thd = xmalloc(sizeof(thd_t) * ctx->nhosts);
	if (thd == NULL) {
		slurm_seterrno(ENOMEM);
		return SLURM_ERROR;
	}

	for (i=0; i<ctx->nhosts; i++) {
		thd[i].state = DSH_NEW;
		thd[i].req = &req[i];
	}

	/* start all the other threads (up to _MAX_THREAD_COUNT active) */
	for (i=0; i<ctx->nhosts; i++) {
		/* wait until "room" for another thread */
		slurm_mutex_lock(&thread_mutex);
		while (threads_active >= _MAX_THREAD_COUNT) {
			pthread_cond_wait(&thread_cond, &thread_mutex);
		}

		slurm_attr_init(&thd[i].attr);
		(void) pthread_attr_setdetachstate(&thd[i].attr,
						PTHREAD_CREATE_DETACHED);
		while ((rc = pthread_create(&thd[i].thread, &thd[i].attr,
					    _thread_per_node_rpc,
	 				    (void *) &thd[i]))) {
			if (threads_active)
				pthread_cond_wait(&thread_cond, &thread_mutex);
			else {
				slurm_mutex_unlock(&thread_mutex);
				sleep(1);
				slurm_mutex_lock(&thread_mutex);
			}
		}

		threads_active++;
		slurm_mutex_unlock(&thread_mutex);
	}

	/* wait for all tasks to terminate */
	slurm_mutex_lock(&thread_mutex);
	for (i=0; i<ctx->nhosts; i++) {
		while (thd[i].state < DSH_DONE) {
			/* wait until another thread completes*/
			pthread_cond_wait(&thread_cond, &thread_mutex);
		}
	}
	slurm_mutex_unlock(&thread_mutex);

	xfree(thd);
	return rc;
}


/*
 * _thread_per_node_rpc - thread to issue an RPC to a single node
 * IN/OUT args - pointer to thd_t entry
 */
static void *_thread_per_node_rpc(void *args)
{
	int rc;
	thd_t *thread_ptr = (thd_t *) args;
	state_t new_state;

	thread_ptr->tstart = time(NULL);
	thread_ptr->state = DSH_ACTIVE;

	if (slurm_send_recv_rc_msg(thread_ptr->req, &rc, 0) < 0) {
		new_state = DSH_FAILED;
		goto cleanup;
	}

	switch (rc) {
		case SLURM_SUCCESS:
			new_state = DSH_DONE;
			break;
		default:
			slurm_seterrno(rc);
			new_state = DSH_FAILED;
	}

      cleanup:
	slurm_mutex_lock(&thread_mutex);
	thread_ptr->state = new_state;
	threads_active--;
	/* Signal completion so another thread can replace us */
	slurm_mutex_unlock(&thread_mutex);
	pthread_cond_signal(&thread_cond);

	return (void *) NULL;
}
