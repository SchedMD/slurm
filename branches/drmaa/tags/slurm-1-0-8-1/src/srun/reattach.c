/****************************************************************************\
 * src/srun/reattach.c - reattach to a running job
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsignal.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/hostlist.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/read_config.h"

#include "src/srun/srun_job.h"
#include "src/srun/launch.h"
#include "src/srun/opt.h"
#include "src/srun/io.h"
#include "src/srun/msg.h"



/* number of active threads */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;

typedef enum {THD_NEW, THD_ACTIVE, THD_DONE, THD_FAILED} state_t;

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        pthread_attr_t	attr;			/* thread attributes */
        state_t		state;      		/* thread state */
	slurm_msg_t    *msg;
	srun_job_t          *job;
} thd_t;

static void		 _p_reattach(slurm_msg_t *req, srun_job_t *job);
static void 		*_p_reattach_task(void *args);

typedef struct _srun_step {
	uint32_t  jobid;
	uint32_t  stepid;
	uint32_t  ntasks;
	char     *nodes;
	char     *name;
	bool      complete_job;
} srun_step_t;

static void
_srun_step_destroy(srun_step_t *s)
{
	if (s->name)
		xfree(s->name);
	if (s->nodes)
		xfree(s->nodes);
	xfree(s);
}

static srun_step_t *
_srun_step_create(uint32_t jobid, uint32_t stepid, char *name)
{
	srun_step_t *s = xmalloc(sizeof(*s));
	s->jobid  = jobid;
	s->stepid = stepid;
	s->ntasks = 0;
	s->nodes  = NULL;
	s->name   = NULL;

	s->complete_job = false;

	if (name == NULL) 
		return s;
	s->name = xstrdup(name);
	return s;
}

static char *
_next_tok(char *sep, char **str)
{
	char *tok;

	/* push str past any leading separators */
	while ((**str != '\0') && (strchr(sep, **str) != '\0'))
		(*str)++;

	if (**str == '\0')
		return NULL;

	/* assign token ptr */
	tok = *str;

	/* push str past token and leave pointing to first separator */
	while ((**str != '\0') && (strchr(sep, **str) == '\0'))
		(*str)++;

	/* nullify consecutive separators and push str beyond them */
	while ((**str != '\0') && (strchr(sep, **str) != '\0'))
		*(*str)++ = '\0';

	return tok;
}


static List
_step_list_create(char *steplist)
{
	List     l    = NULL;
	char    *str  = NULL;
	char    *orig = NULL;
	char    *tok  = NULL;
	uint32_t jobid, stepid;
       
	if (steplist == NULL)
		return NULL;

	orig = str = xstrdup(steplist);

	l = list_create((ListDelF)_srun_step_destroy);

	while ((tok = _next_tok(",", &str))) {
		char *cur = tok;
		char *p   = strchr(tok, '.');
		char *q   = NULL;

		if (p) *(p++) = '\0';

		jobid  = strtoul(tok, &q, 10);

		if (q == tok) {
			error("Invalid jobid: `%s'", cur);
			goto error;
		}

		stepid = (p && *p) ? strtoul(p, &q, 10) : NO_VAL;

		if ((q == p) || (*q != '\0')) {
			error("Invalid job step id: `%s'", cur);
			goto error;
		}
		
		list_append(l, _srun_step_create(jobid, stepid, cur));
	}

	xfree(orig);
	return l;

    error:
	xfree(orig);
	list_destroy(l);
	return NULL;

}

static int
_get_job_info(srun_step_t *s)
{
	int             i, rc = -1;
	job_info_msg_t *resp = NULL;
	job_info_t     *job  = NULL;
	hostlist_t      hl;

	s->nodes = NULL;

	if (slurm_load_jobs((time_t) 0, &resp, 1) < 0) {
		error("Unable to load jobs: %m");
		goto done;
	}

	for (i = 0; i < resp->record_count; i++) {
		job = &resp->job_array[i];
		if (job->job_id == s->jobid)
			break;
		job = NULL;
	}

	if (job == NULL) {
		error ("Unable to find job %u", s->jobid);
		goto done;
	}

	if ((job->job_state != JOB_RUNNING)
	&&  (job->job_state != JOB_SUSPENDED)) {
		error ("Cannot attach to job %d in state %s",
		       job->job_id, job_state_string(job->job_state));
		goto done;
	}

	if (!job->batch_flag) {
		rc = 0;
		goto done;
	}

	if (!(hl = hostlist_create(job->nodes))) {
		error ("Unable to create hostlist from `%s'", job->nodes);
		goto done;
	}
	s->nodes  = hostlist_shift(hl);
	hostlist_destroy(hl);

	s->ntasks = 1;
	rc = 0;

  done:
	if (resp)
		slurm_free_job_info_msg(resp);
	return rc;
}

static void
_get_step_info(srun_step_t *s)
{
	job_step_info_response_msg_t *resp = NULL;

	xassert(s->stepid != NO_VAL);

	if (slurm_get_job_steps((time_t) 0, s->jobid, s->stepid, &resp, 1) < 0) {
		error("Unable to get step information for %u.%u: %m", 
		      s->jobid, s->stepid);
		goto done;
	}
	if (resp->job_step_count == 0) {
		error("No nodes in %u.%u", s->jobid, s->stepid);
		s->ntasks = 0;
		goto done;
	}

	s->nodes  = xstrdup(resp->job_steps->nodes);
	s->ntasks = resp->job_steps->num_tasks;

    done:
	if (resp)
		slurm_free_job_step_info_response_msg(resp);
	return;
}

static void
_get_attach_info(srun_step_t *s)
{
	if (s->stepid == NO_VAL) {
		if (_get_job_info(s) < 0)
			return;

		/* If job was not a batch job, try step 0
		 */
		if (s->nodes == NULL) {
			s->stepid = 0;
			_get_step_info(s);
		}

	} else {
		_get_step_info(s);
	}
}

static int
_attach_to_job(srun_job_t *job)
{
	int i;
	reattach_tasks_request_msg_t *req;
	slurm_msg_t *msg;

	req = xmalloc(job->nhosts * sizeof(*req));
	msg = xmalloc(job->nhosts * sizeof(*msg));

	debug("Going to attach to job %u.%u", job->jobid, job->stepid);

	for (i = 0; i < job->nhosts; i++) {
		reattach_tasks_request_msg_t *r = &req[i];
		slurm_msg_t                  *m = &msg[i];

		r->job_id          = job->jobid;
		r->job_step_id     = job->stepid;
		r->srun_node_id    = (uint32_t) i;
		r->io_port         = 
			ntohs(job->
			      listenport[i%job->num_listen]);
		r->resp_port       = 
			ntohs(job->
			      jaddr[i%job->njfds].sin_port);
		r->cred            = job->cred;


		/* XXX: redirecting output to files not yet
		 * supported
		 */
		r->ofname          = NULL;
		r->efname          = NULL;
		r->ifname          = NULL;

		m->data            = r;
		m->msg_type        = REQUEST_REATTACH_TASKS;
		memcpy(&m->address, &job->slurmd_addr[i], sizeof(slurm_addr));
	}

	_p_reattach(msg, job);

	return SLURM_SUCCESS;
}

static void
_p_reattach(slurm_msg_t *msg, srun_job_t *job)
{
	int i;
	thd_t *thd = xmalloc(job->nhosts * sizeof(thd_t));

	for (i = 0; i < job->nhosts; i++) {

		slurm_mutex_lock(&active_mutex);
		while (active >= opt.max_threads) {
			pthread_cond_wait(&active_cond, &active_mutex);
		}
		active++;
		slurm_mutex_unlock(&active_mutex);

		thd[i].msg = &msg[i];
		thd[i].job = job;

		slurm_attr_init(&thd[i].attr);
		if (pthread_attr_setdetachstate(&thd[i].attr,
				PTHREAD_CREATE_DETACHED ) < 0)
			fatal("pthread_attr_setdetachstate: %m");

		if (pthread_create( &thd[i].thread, &thd[i].attr,
				    _p_reattach_task, (void *) &thd[i])) {
			error("pthread_create: %m");
			_p_reattach_task((void *) &thd[i]);
		}

	}

	slurm_mutex_lock(&active_mutex);
	while (active > 0)
		pthread_cond_wait(&active_cond, &active_mutex);
	slurm_mutex_unlock(&active_mutex);

	xfree(thd);
}

static void *
_p_reattach_task(void *arg)
{
	thd_t *t   = (thd_t *) arg;
	int rc     = 0;
	reattach_tasks_request_msg_t *req = t->msg->data;
	int nodeid = req->srun_node_id; 
	char *host = t->job->step_layout->host[nodeid];
	
	t->state = THD_ACTIVE;
	debug3("sending reattach request to %s", host);

	rc = slurm_send_only_node_msg(t->msg);
	if (rc < 0) {
		error("reattach: %s: %m", host);
		t->state = THD_FAILED;
		t->job->host_state[nodeid] = SRUN_HOST_REPLIED;
	} else {
		t->state = THD_DONE;
		t->job->host_state[nodeid] = SRUN_HOST_UNREACHABLE;
	}

	slurm_mutex_lock(&active_mutex);
	active--;
	pthread_cond_signal(&active_cond);
	slurm_mutex_unlock(&active_mutex);

	return NULL;
}


int reattach()
{
	List          steplist = _step_list_create(opt.attach);
	srun_step_t  *s        = NULL;
	srun_job_t        *job      = NULL;

	if ((steplist == NULL) || (list_count(steplist) == 0)) {
		info("No job/steps in attach");
		exit(1);
	}

	if (list_count(steplist) > 1)
		info("Warning: attach to multiple jobs/steps not supported");
	s = list_peek(steplist);

	_get_attach_info(s);

	if (!opt.join)
		opt.ifname = "none";

	if ((opt.nodelist = s->nodes) == NULL) 
		exit(1);

	if ((opt.nprocs = s->ntasks) == 0) 
		exit(1);

	/*
	 * Indicate that nprocs has been manually set
	 */
	opt.nprocs_set = true;

	if (!(job = job_create_noalloc()))
		exit(1);

	job->jobid  = s->jobid;
	job->stepid = s->stepid;
	job->step_layout->tids   = xmalloc(job->nhosts * sizeof(uint32_t *));
	job->hostid = xmalloc(s->ntasks   * sizeof(uint32_t *));

	if (job->stepid == NO_VAL) {
		char *new_argv0 = NULL;
		xstrfmtcat(new_argv0, "attach[%d]", job->jobid);
		log_set_argv0(new_argv0);
	}

	/*
	 * mask and handle certain signals iff we are "joining" with
	 * the job in question. If opt.join is off, attached srun is in
	 * "read-only" mode and cannot forward stdin/signals.
	 */
	if (opt.join)
		sig_setup_sigmask();

	if (msg_thr_create(job) < 0) {
		error("Unable to create msg thread: %m");
		exit(1);
	}

	if (io_thr_create(job) < 0) {
		error("Unable to create IO thread: %m");
		exit(1);
	}

	if (opt.join && sig_thr_create(job) < 0) {
		error("Unable to create signals thread: %m");
	}

	_attach_to_job(job);

	slurm_mutex_lock(&job->state_mutex);
	while (job->state < SRUN_JOB_TERMINATED) {
		pthread_cond_wait(&job->state_cond, &job->state_mutex);
	}
	slurm_mutex_unlock(&job->state_mutex);

	if (job->state == SRUN_JOB_FAILED)
		info("Job terminated abnormally.");

	/*
	 *  Signal the IO thread to shutdown, which will stop
	 *  the listening socket and file read (stdin) event
	 *  IO objects, but allow file write (stdout) objects to
	 *  complete any writing that remains.
	 */
	debug("Waiting for IO thread");
	eio_signal_shutdown(job->eio);
	if (pthread_join(job->ioid, NULL) < 0)
		error ("Waiting on IO: %m");

	/* kill msg server thread */
	pthread_kill(job->jtid, SIGHUP);

	/* _complete_job(job); */
	
	exit(0);
}
