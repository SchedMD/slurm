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
#  include "config.h"
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#include <signal.h>

#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/eio.h"
#include "src/common/slurm_protocol_api.h"

#include "src/slurmd/job.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/io.h"
#include "src/slurmd/fname.h"
#include "src/slurmd/slurmd.h"

static char ** _array_copy(int n, char **src);
static void _array_free(char ***array);
static void _srun_info_destructor(void *arg);
static void _job_init_task_info(slurmd_job_t *job, uint32_t *gids);

static struct passwd *
_pwd_create(uid_t uid)
{
	struct passwd *pwd = xmalloc(sizeof(*pwd));
	struct passwd *ppwd = getpwuid(uid);

	if (!ppwd) {
		xfree(pwd);
		return NULL;
	}

	pwd->pw_name   = xstrdup(ppwd->pw_name);
	pwd->pw_passwd = xstrdup(ppwd->pw_passwd);
	pwd->pw_gecos  = xstrdup(ppwd->pw_gecos);
	pwd->pw_shell  = xstrdup(ppwd->pw_shell);
	pwd->pw_dir    = xstrdup(ppwd->pw_dir);
	pwd->pw_uid    = ppwd->pw_uid;
	pwd->pw_gid    = ppwd->pw_gid;

	return pwd;
}

static void
_pwd_destroy(struct passwd *pwd)
{
	if (!pwd)
		return;
	xfree(pwd->pw_name);
	xfree(pwd->pw_passwd);
	xfree(pwd->pw_gecos);
	xfree(pwd->pw_shell);
	xfree(pwd->pw_dir);
	xfree(pwd);
}


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

	debug3("entering job_create");

	if ((pwd = _pwd_create((uid_t)msg->uid)) == NULL) {
		error("uid %ld not found on system", (long) msg->uid);
		slurm_seterrno (ESLURMD_UID_NOT_FOUND);
		return NULL;
	}
	job = xmalloc(sizeof(*job));

	job->jobid   = msg->job_id;
	job->stepid  = msg->job_step_id;
	job->uid     = (uid_t) msg->uid;
	job->pwd     = pwd;
	job->gid     = job->pwd->pw_gid;
	job->nprocs  = msg->nprocs;
	job->nnodes  = msg->nnodes;
	job->nodeid  = msg->srun_node_id;
	job->ntasks  = msg->tasks_to_launch;
	job->debug   = msg->slurmd_debug;
	job->cpus    = msg->cpus_allocated;

	job->timelimit   = (time_t) -1;
	job->task_flags  = msg->task_flags;

	job->env     = _array_copy(msg->envc, msg->env);
	job->argc    = msg->argc;
	job->argv    = _array_copy(job->argc, msg->argv);

	job->cwd     = xstrdup(msg->cwd);

	memcpy(&resp_addr, cli_addr, sizeof(slurm_addr));
	slurm_set_addr(&resp_addr, msg->resp_port, NULL); 
	memcpy(&io_addr,   cli_addr, sizeof(slurm_addr));
	slurm_set_addr(&io_addr,   msg->io_port,   NULL); 

	job->switch_job = msg->switch_job;

	job->objs    = list_create((ListDelF) io_obj_destroy);
	job->eio     = eio_handle_create();

	srun = srun_info_create(msg->cred, &resp_addr, &io_addr);

	srun->ofname = xstrdup(msg->ofname);
	srun->efname = xstrdup(msg->efname);
	srun->ifname = xstrdup(msg->ifname);

	job->sruns   = list_create((ListDelF) _srun_info_destructor);

	list_append(job->sruns, (void *) srun);

	_job_init_task_info(job, msg->global_task_ids);

	if (pipe(job->fdpair) < 0) {
		error("pipe: %m");
		return NULL;
	}

	fd_set_close_on_exec(job->fdpair[0]);
	fd_set_close_on_exec(job->fdpair[1]);

	job->smgr_status = -1;

	return job;
}

/* create a slurmd job structure from a spawn task message */
slurmd_job_t * 
job_spawn_create(spawn_task_request_msg_t *msg, slurm_addr *cli_addr)
{
	struct passwd *pwd;
	slurmd_job_t  *job;
	srun_info_t   *srun;
	slurm_addr     io_addr;

	xassert(msg != NULL);

	debug3("entering job_spawn_create");

	if ((pwd = _pwd_create((uid_t)msg->uid)) == NULL) {
		error("uid %ld not found on system", (long) msg->uid);
		slurm_seterrno (ESLURMD_UID_NOT_FOUND);
		return NULL;
	}
	job = xmalloc(sizeof(*job));

	job->jobid   = msg->job_id;
	job->stepid  = msg->job_step_id;
	job->uid     = (uid_t) msg->uid;
	job->pwd     = pwd;
	job->gid     = job->pwd->pw_gid;
	job->nprocs  = msg->nprocs;
	job->nnodes  = msg->nnodes;
	job->nodeid  = msg->srun_node_id;
	job->ntasks  = 1;	/* tasks to launch always one */
	job->debug   = msg->slurmd_debug;
	job->cpus    = msg->cpus_allocated;

	job->timelimit   = (time_t) -1;
	job->task_flags  = msg->task_flags;
	job->spawn_task = true;

	job->env     = _array_copy(msg->envc, msg->env);
	job->argc    = msg->argc;
	job->argv    = _array_copy(job->argc, msg->argv);

	job->cwd     = xstrdup(msg->cwd);

	memcpy(&io_addr,   cli_addr, sizeof(slurm_addr));
	slurm_set_addr(&io_addr,   msg->io_port,   NULL); 

	job->switch_job = msg->switch_job;

	job->objs    = list_create((ListDelF) io_obj_destroy);
	job->eio     = eio_handle_create();

	srun = srun_info_create(msg->cred, NULL, &io_addr);

	job->sruns   = list_create((ListDelF) _srun_info_destructor);

	list_append(job->sruns, (void *) srun);

	_job_init_task_info(job, &(msg->global_task_id));

	if (pipe(job->fdpair) < 0) {
		error("pipe: %m");
		return NULL;
	}

	fd_set_close_on_exec(job->fdpair[0]);
	fd_set_close_on_exec(job->fdpair[1]);

	job->smgr_status = -1;

	return job;
}

/*
 * return the default output filename for a batch job
 */
static char *
_mkfilename(slurmd_job_t *job, const char *name)
{
	if (name == NULL) 
		return fname_create(job, "slurm-%j.out", 0);
	else
		return fname_create(job, name, 0);
}

slurmd_job_t * 
job_batch_job_create(batch_job_launch_msg_t *msg)
{
	struct passwd *pwd;
	slurmd_job_t *job = xmalloc(sizeof(*job));
	srun_info_t  *srun = NULL;
	uint32_t      global_taskid = 0;

	if ((pwd = _pwd_create((uid_t)msg->uid)) == NULL) {
		error("uid %ld not found on system", (long) msg->uid);
		slurm_seterrno (ESLURMD_UID_NOT_FOUND);
		return NULL;
	}

	job->pwd     = pwd;
	job->ntasks  = 1; 
	job->jobid   = msg->job_id;
	job->stepid  = NO_VAL;
	job->batch   = true;

	job->uid     = (uid_t) msg->uid;
	job->gid     = job->pwd->pw_gid;
	job->cwd     = xstrdup(msg->work_dir);

	job->env     = _array_copy(msg->envc, msg->environment);
	job->eio     = eio_handle_create();
	job->objs    = list_create((ListDelF) io_obj_destroy);
	job->sruns   = list_create((ListDelF) _srun_info_destructor);

	srun = srun_info_create(NULL, NULL, NULL);

	srun->ofname = _mkfilename(job, msg->out);
	srun->efname = msg->err ? xstrdup(msg->err) : srun->ofname;
	srun->ifname = xstrdup("/dev/null");
	list_append(job->sruns, (void *) srun);

	if (msg->argc) {
		job->argc    = msg->argc;
		job->argv    = _array_copy(job->argc, msg->argv);
	} else {
		job->argc    = 2;
		/* job script has not yet been written out to disk --
		 * argv will be filled in later
		 */
		job->argv    = (char **) xmalloc(job->argc * sizeof(char *));
	}

	if (pipe(job->fdpair) < 0) {
		error("pipe: %m");
		return NULL;
	}
	fd_set_close_on_exec(job->fdpair[0]);
	fd_set_close_on_exec(job->fdpair[1]);

	job->smgr_status = -1;

	_job_init_task_info(job, &global_taskid);

	return job;
}

static void
_job_init_task_info(slurmd_job_t *job, uint32_t *gid)
{
	int          i;
	int          n = job->ntasks;

	job->task = (task_info_t **) xmalloc(n * sizeof(task_info_t *));

	for (i = 0; i < n; i++){
		job->task[i] = task_info_create(i, gid[i]);
		/* "srun" info is attached to task in 
		 * io_add_connecting
		 */
	}
}

void
job_signal_tasks(slurmd_job_t *job, int signal)
{
	int n = job->ntasks;
	while (--n >= 0) {
		if ((job->task[n]->pid > (pid_t) 0)
		&&  (kill(job->task[n]->pid, signal) < 0)) {
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

	_array_free(&job->env);
	_array_free(&job->argv);

	_pwd_destroy(job->pwd);

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
	while ((--n >= 0) && (src[n] != NULL))
		dst[n] = xstrdup(src[n]);
	return dst;
}

static void
_array_free(char ***array)
{
	int i = 0;
	while ((*array)[i] != NULL) 
		xfree((*array)[i++]);
	xfree(*array);
	*array = NULL;
}


struct srun_info *
srun_info_create(slurm_cred_t cred, slurm_addr *resp_addr, slurm_addr *ioaddr)
{
	char             *data = NULL;
	int               len  = 0;
	struct srun_info *srun = xmalloc(sizeof(*srun));
	srun_key_t       *key  = xmalloc(sizeof(*key ));

	srun->key    = key;

	/*
	 * If no credential was provided, return the empty
	 * srun info object. (This is used, for example, when
	 * creating a batch job structure)
	 */
	if (!cred) return srun;

	slurm_cred_get_signature(cred, &data, &len);

	len = len > SLURM_IO_KEY_SIZE ? SLURM_IO_KEY_SIZE : len;

	if (data != NULL) {
		memcpy((void *) key->data, data, len);

		if (len < SLURM_IO_KEY_SIZE)
			memset( (void *) (key->data + len), 0, 
			        SLURM_IO_KEY_SIZE - len        );
	}

	if (ioaddr != NULL)
		srun->ioaddr    = *ioaddr;
	if (resp_addr != NULL)
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

int
job_update_shm(slurmd_job_t *job)
{
	job_step_t s;

	s.uid	    = job->uid;
	s.jobid     = job->jobid;
	s.stepid    = job->stepid;
	s.ntasks    = job->ntasks;
	s.timelimit = job->timelimit;
	strncpy(s.exec_name, job->argv[0], MAXPATHLEN);
	s.sw_id     = 0;
	s.mpid      = (pid_t) 0;
	s.sid       = (pid_t) 0;
	s.io_update = false;
	/*
	 * State not set in shm_insert_step()
	 * s.state     = SLURMD_JOB_STARTING;
	 */

	if (shm_insert_step(&s) < 0) 
		return SLURM_ERROR;

	if (job->stepid == NO_VAL)
		debug("updated shm with job %u", job->jobid);
	else
		debug("updated shm with step %u.%u", job->jobid, job->stepid);

	job_update_state(job, SLURMD_JOB_STARTING);

	return SLURM_SUCCESS;
}

int
job_update_state(slurmd_job_t *job, job_state_t s)
{
	return shm_update_step_state(job->jobid, job->stepid, s);
}

void 
job_delete_shm(slurmd_job_t *job)
{
	if (shm_delete_step(job->jobid, job->stepid) == SLURM_FAILURE)
		error("deleting step:  %u.%u not found in shmem", 
				job->jobid, job->stepid); 
}
