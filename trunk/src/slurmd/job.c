/*****************************************************************************\
 * src/slurmd/job.c - slurmd_job_t routines
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#include <signal.h>

#include <src/common/xmalloc.h>
#include <src/common/xassert.h>
#include <src/common/xstring.h>
#include <src/common/log.h>
#include <src/common/eio.h>
#include <src/common/slurm_protocol_api.h>

#include <src/slurmd/job.h>
#include <src/slurmd/shm.h>
#include <src/slurmd/io.h>

static char ** _array_copy(int n, char **src);
static void _array_free(int n, char ***array);
static void _srun_info_destructor(void *arg);
static void _job_init_task_info(slurmd_job_t *job, 
		                launch_tasks_request_msg_t *msg);


/* create a slurmd job structure from a launch tasks message */
slurmd_job_t * 
job_create(launch_tasks_request_msg_t *msg, slurm_addr *cli_addr)
{
	struct passwd *pwd;
	slurmd_job_t  *job;
	srun_info_t   *srun;
	slurm_addr     resp_addr;
	slurm_addr     io_addr;

	xassert(msg != NULL);

	if ((pwd = getpwuid((uid_t)msg->uid)) < 0) {
		error("uid %ld not found on system", msg->uid);
		return NULL;
	}

	memcpy(&resp_addr, cli_addr, sizeof(slurm_addr));
	memcpy(&io_addr,   cli_addr, sizeof(slurm_addr));
	slurm_set_addr(&resp_addr, msg->resp_port, NULL); 
	slurm_set_addr(&io_addr,   msg->io_port,   NULL); 

	job = xmalloc(sizeof(*job));

	job->jobid   = msg->job_id;
	job->stepid  = msg->job_step_id;
	job->uid     = msg->uid;
	job->pwd     = pwd;
	job->nprocs  = msg->nprocs;
	job->nnodes  = msg->nnodes;
	job->nodeid  = msg->srun_node_id;
	job->ntasks  = msg->tasks_to_launch;

	job->timelimit   = msg->credential->expiration_time;

	job->envc    = msg->envc;
	job->env     = _array_copy(job->envc, msg->env);
	job->argc    = msg->argc;
	job->argv    = _array_copy(job->argc, msg->argv);

	job->cwd     = xstrdup(msg->cwd);
	job->ofname  = xstrdup(msg->ofname);
	job->efname  = msg->efname ? xstrdup(msg->efname) : job->ofname;
	job->ifname  = xstrdup(msg->ifname);

#ifdef HAVE_LIBELAN3
	job->qsw_job = msg->qsw_job;
#endif

	job->objs    = list_create((ListDelF) io_obj_destroy);

	srun = srun_info_create((void *)msg->credential->signature, 
			        &resp_addr, &io_addr);

	job->sruns  = list_create((ListDelF) _srun_info_destructor);

	list_append(job->sruns, (void *) srun);

	_job_init_task_info(job, msg);

	return job;
}

static char *
_mkfilename(slurmd_job_t *job, const char *name)
{
	char buf[256];

	if (name == NULL) {
		snprintf(buf, 256, "%s/job%u.out", job->cwd, job->jobid);
		return xstrdup(buf);
	} else
		return xstrdup(name);
}

slurmd_job_t * 
job_batch_job_create(batch_job_launch_msg_t *msg)
{
	struct passwd *pwd;
	slurmd_job_t *job = xmalloc(sizeof(*job));
	task_info_t  *t   = task_info_create(0, 0);

	if ((pwd = getpwuid((uid_t)msg->uid)) < 0) {
		error("uid %ld not found on system", msg->uid);
		return NULL;
	}

	job->pwd     = pwd;
	job->ntasks  = 1; 
	job->jobid   = msg->job_id;
	job->stepid  = NO_VAL;
	job->uid     = (uid_t)msg->uid;
	job->cwd     = xstrdup(msg->work_dir);

	job->ofname  = _mkfilename(job, msg->out);
	job->efname  = msg->err ? xstrdup(msg->err): job->ofname;
	job->ifname  = xstrdup("/dev/null");

	job->envc    = msg->envc;
	job->env     = _array_copy(job->envc, msg->environment);
	job->objs    = list_create((ListDelF) io_obj_destroy);
	job->sruns   = list_create((ListDelF) _srun_info_destructor);

	job->task    = (task_info_t **) xmalloc(sizeof(task_info_t *));
	job->task[0] = t;
	t->ofname    = xstrdup(job->ofname);
	t->efname    = xstrdup(job->efname);
	t->ifname    = xstrdup(job->ifname);

	job->argc    = msg->argc > 0 ? msg->argc : 2;
	job->argv    = (char **) xmalloc(job->argc * sizeof(char *));
	return job;
}

static int
_wid(uint32_t n)
{
	int width = 1;
	while (n /= 10L)
		width++;
	return width;
}

static char *
_task_filename_create(const char *basename, int i, int width)
{
	int  len = basename ? strlen(basename) : 0;
	char buf[len+width+16];
	if (basename == NULL)
		return NULL;
	snprintf(buf, len+width+16, "%s%0*d", basename, width, i);
	return xstrdup(buf);
}


static void
_job_init_task_info(slurmd_job_t *job, launch_tasks_request_msg_t *msg)
{
	int          i;
	int          n    = job->ntasks;
	int          wid  = _wid(job->nprocs);
	uint32_t    *gid  = msg->global_task_ids;
	srun_info_t *srun = (srun_info_t *) list_peek(job->sruns);

	job->task = (task_info_t **) xmalloc(n * sizeof(task_info_t *));

	for (i = 0; i < n; i++){
		task_info_t *t = job->task[i] = task_info_create(i, gid[i]);
		if (srun != NULL) 
			list_append(t->srun_list, (void *)srun);
		t->ofname = _task_filename_create(job->ofname, t->gid, wid);
		t->efname = _task_filename_create(job->efname, t->gid, wid);
		t->ifname = _task_filename_create(job->ifname, t->gid, wid);
	}
}

void
job_signal_tasks(slurmd_job_t *job, int signal)
{
	int n = job->ntasks;
	while (--n >= 0) {
		if (kill(job->task[n]->pid, signal) < 0) {
			if (errno != EEXIST) {
				error("job %d.%d: kill task %d: %m", 
				      job->jobid, job->stepid, n);
			}
		}
	}
}


/* remove job from shared memory, kill initiated tasks, etc */
void 
job_kill(slurmd_job_t *job, int rc)
{
	job_state_t *state;

	xassert(job != NULL);

	if (!(state = shm_lock_step_state(job->jobid, job->stepid))) 
		return;

	if (*state > SLURMD_JOB_STARTING) {
		/* signal all tasks on step->task_list 
		 * This will result in task exit msgs being sent to srun
		 * XXX IMPLEMENT
		 */
		job_signal_tasks(job, SIGKILL);
	}
	*state = SLURMD_JOB_ENDING;
	shm_unlock_step_state(job->jobid, job->stepid);
	
	return;
}


void 
job_destroy(slurmd_job_t *job)
{
	int i;

	_array_free(job->envc, &job->env);
	_array_free(job->argc, &job->argv);

	for (i = 0; i < job->ntasks; i++)
		task_info_destroy(job->task[i]);
	list_destroy(job->sruns);
	list_destroy(job->objs);

	xfree(job);
}

static char **
_array_copy(int n, char **src)
{
	char **dst = xmalloc((n+1) * sizeof(char *));
	dst[n] = NULL;
	while (--n >= 0)
		dst[n] = xstrdup(src[n]);
	return dst;
}

static void
_array_free(int n, char ***array)
{
	while (--n >= 0)
		xfree(*array[n]);
	xfree(*array);
	*array = NULL;
}


struct srun_info *
srun_info_create(void *keydata, slurm_addr *resp_addr, slurm_addr *ioaddr)
{
	struct srun_info *srun = xmalloc(sizeof(*srun));
	srun_key_t       *key  = xmalloc(sizeof(*key ));

	if (keydata != NULL)
		memcpy((void *) key->data, keydata, SLURM_KEY_SIZE);
	srun->key = key;
	if (ioaddr != NULL)
		srun->ioaddr = *ioaddr;
	else
		srun->noconnect = true;
	srun->resp_addr = *resp_addr;
	return srun;
}

/* destructor for list routines */
static void
_srun_info_destructor(void *arg)
{
	struct srun_info *srun = (struct srun_info *)arg;
	srun_info_destroy(srun);
}

void
srun_info_destroy(struct srun_info *srun)
{
	xfree(srun->key);
	xfree(srun);
}

task_info_t *
task_info_create(int taskid, int gtaskid)
{
	task_info_t *t = (task_info_t *) xmalloc(sizeof(*t));

	xassert(taskid >= 0);
	xassert(gtaskid >= 0);

	slurm_mutex_init(&t->mutex);
	slurm_mutex_lock(&t->mutex);
	t->state     = SLURMD_TASK_INIT;
	t->id        = taskid;
	t->gid	     = gtaskid;
	t->pid       = (pid_t) -1;
	t->pin[0]    = -1;
	t->pin[1]    = -1;
	t->pout[0]   = -1;
	t->pout[1]   = -1;
	t->perr[0]   = -1;
	t->perr[1]   = -1;
	t->estatus   = -1;
	t->in        = NULL;
	t->out       = NULL;
	t->err       = NULL;
	t->ifname    = NULL;
	t->ofname    = NULL;
	t->efname    = NULL;
	t->srun_list = list_create(NULL); 
	slurm_mutex_unlock(&t->mutex);
	return t;
}


void 
task_info_destroy(task_info_t *t)
{
	slurm_mutex_lock(&t->mutex);
	list_destroy(t->srun_list);
	slurm_mutex_unlock(&t->mutex);
	slurm_mutex_destroy(&t->mutex);
	xfree(t);
}

void
job_update_shm(slurmd_job_t *job)
{
	job_step_t s;

	s.uid	    = job->uid;
	s.jobid     = job->jobid;
	s.stepid    = job->stepid;
	s.ntasks    = job->ntasks;
	s.timelimit = job->timelimit;

	s.sw_id     = 0;

	if (shm_insert_step(&s) < 0)
		error("Updating shmem with new step info: %m");

	verbose("shm_insert job %d.%d", job->jobid, job->stepid);
}

void 
job_delete_shm(slurmd_job_t *job)
{
	if (shm_delete_step(job->jobid, job->stepid) == SLURM_FAILURE)
		error("deleting step:  %ld.%ld not found in shmem", 
				job->jobid, job->stepid); 
}
