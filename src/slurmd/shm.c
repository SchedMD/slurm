/*****************************************************************************\
 * src/slurmd/shm.c - slurmd shared memory routines 
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

#if HAVE_SYS_IPC_H
#  include <sys/ipc.h>
#endif

#if HAVE_SYS_SHM_H
#  include <sys/shm.h>
#endif

#if HAVE_SYS_SEM_H
#  include <sys/sem.h>
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#if HAVE_ERRNO_H
#  include <errno.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#define __USE_XOPEN_EXTENDED
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <signal.h>

#include <slurm/slurm_errno.h>

#include "src/common/macros.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/shm.h"

/* We use Chris Dunlap's POSIX semaphore implementation if necessary */
#include "src/slurmd/semaphore.h"

#define MAX_JOB_STEPS	16
#define MAX_BATCH_JOBS	128
#define MAX_TASKS	1024

#define SHM_LOCKNAME	"/.slurm.lock"

/* Increment SHM_VERSION if format changes */
#define SHM_VERSION	1005

/* These macros convert shared memory pointers to local memory
 * pointers and back again. Pointers in shared memory are relative
 * to the address at which the shared memory is attached
 *
 * These routines must be used to convert a pointer in shared memory
 * back to a "real" pointer in local memory. e.g. t = _taskp(t->next)
 */
#define _laddr(p) \
	(                                                                    \
	  (p) ? (((unsigned long)(p)) + ((unsigned long)slurmd_shm))         \
	     : (unsigned long) NULL                                          \
	)
#define _offset(p) \
	(                                                                    \
	  (p) ? (((unsigned long)(p)) - ((unsigned long)slurmd_shm))         \
	     : (unsigned long) NULL                                          \
	)

#define _taskp(__p) (task_t *)     _laddr(__p)
#define _toff(__p)  (task_t *)     _offset(__p)
#define _stepp(__p) (job_step_t *) _laddr(__p)
#define _soff(__p)  (job_step_t *) _offset(__p)

typedef struct shmem_struct {
	int          version;
	int          users;	
	job_step_t   step[MAX_JOB_STEPS];
	task_t       task[MAX_TASKS];
} slurmd_shm_t;


/* 
 * static variables: 
 * */
static sem_t        *shm_lock;
static char         *lockname; 
static char         *lockdir;
static slurmd_shm_t *slurmd_shm;
static int           shmid;
static pid_t         attach_pid = (pid_t) 0;


/* 
 * static function prototypes: 
 */
static int  _is_valid_ipc_name(const char *name);
static char *_create_ipc_name(const char *name);
static int  _shm_unlink_lock(void);
static int  _shm_lock_and_initialize(void);
static void _shm_lock(void);
static void _shm_unlock(void);
static void _shm_initialize(void);
static void _shm_prepend_task_to_step(job_step_t *, task_t *);
static void _shm_prepend_task_to_step_internal(job_step_t *, task_t *);
static void _shm_task_copy(task_t *, task_t *);
static void _shm_step_copy(job_step_t *, job_step_t *);
static void _shm_clear_task(task_t *);
static void _shm_clear_step(job_step_t *);
static int  _shm_clear_stale_entries(void);
static int  _shm_find_step(uint32_t, uint32_t);
static bool _shm_sane(void);
static int  _shm_validate(void);
static bool _valid_slurmd_sid(pid_t sid);

static task_t *     _shm_alloc_task(void);
static task_t *     _shm_find_task_in_step(job_step_t *s, int taskid);
static job_step_t * _shm_copy_step(job_step_t *j);


/* initialize shared memory: Attach to memory if shared region 
 * already exists - otherwise create and attach
 */
int
shm_init(bool startup)
{
	int rc;

	if ((rc = _shm_lock_and_initialize()) < 0)
		return rc;
	if (startup)
		rc = _shm_validate();
	return rc;
}

/* Detach from shared memory */
int
shm_fini(void)
{
	int destroy = 0;
	int i;
	xassert(slurmd_shm != NULL);
	_shm_lock();

	debug3("%ld calling shm_fini() (attached by %ld)", 
		(long) getpid(), attach_pid);

	debug("[%ld] shm_fini: shm_users = %d", 
	      (long) getpid(), slurmd_shm->users); 

	for (i = 0; i < MAX_JOB_STEPS; i++) {
		if (slurmd_shm->step[i].state > SLURMD_JOB_UNUSED) {
			job_step_t *s = &slurmd_shm->step[i];
			info ("Used shm for job %u.%u\n", s->jobid, s->stepid); 
		}
	}

	if (--slurmd_shm->users == 0)
		destroy = 1;

	/* detach segment from local memory */
	if (shmdt(slurmd_shm) < 0) {
		error("shmdt: %m");
		goto error;
	}

	slurmd_shm = NULL;

	if (destroy && (shmctl(shmid, IPC_RMID, NULL) < 0)) {
		error("shmctl: %m");
		goto error;
	}
	_shm_unlock();
	if (destroy && (_shm_unlink_lock() < 0)) {
		error("_shm_unlink_lock: %m");
		goto error;
	}

	return 0;

    error:
	return -1;
}

void
shm_cleanup(void)
{
	char *s;
	key_t key;
	int id = -1;

	if (!lockdir)
		lockdir = xstrdup(conf->spooldir);

	if ((s = _create_ipc_name(SHM_LOCKNAME))) {

		info("request to destroy shm lock [%s]", s);

		key = ftok(s, 1);
		if (sem_unlink(s) < 0)
			error("sem_unlink: %m");
		xfree(s);
	
		/* This seems to be the only way to get a shared memory ID 
		 *  given a key, if you don't already know the size of the 
		 *  region.
		 */
		id = shmget(key, 1, 0);
	} 

	if ((id > 0) && (shmctl(shmid, IPC_RMID, NULL) < 0)) {
		error ("Unable to destroy existing shm segment");
	}
}

List
shm_get_steps(void)
{
	List l;
	int  i;
	xassert(slurmd_shm != NULL);
	l = list_create((ListDelF) &shm_free_step);
	_shm_lock();
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		if (slurmd_shm->step[i].state > SLURMD_JOB_UNUSED) {
			job_step_t *s = _shm_copy_step(&slurmd_shm->step[i]);
			if (s) list_append(l, (void *) s);
		}
	}
	_shm_unlock();
	return l;
}

bool
shm_step_still_running(uint32_t jobid, uint32_t stepid)
{
	bool        retval = false;
	int         i;
	job_step_t *s;

	xassert(slurmd_shm != NULL);

	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) < 0) 
		goto done;

	s = &slurmd_shm->step[i];

	/*
	 *  Consider a job step running if the step state is less 
	 *   than STARTED or if s->sid has not yet been set then 
	 *   validate that the sid is a valid slurmd process. 
	 */
	if (  (s->state < SLURMD_JOB_STARTED)
	   || (s->sid <= (pid_t) 0) 
	   || (_valid_slurmd_sid(s->sid)) )
		retval = true;

    done:
	_shm_unlock();

	return retval;
}

static int
_is_valid_ipc_name(const char *name)
{
	if (!name)
		return(0);
	else if (strlen(name) <= 1)
		return(2);
	else if (strlen(name) >= PATH_MAX)
		return(3);
	else if (strcmp(name, "/.") == 0)
		return(4);
	else if (strcmp(name, "/..") == 0)
		return(5);
	else if (strrchr(name, '/') != name)
		return(6);
	return(1);
}

/*
 *  Create IPC name by appending `name' to slurmd spooldir
 *   setting. 
 */
static char *
_create_ipc_name(const char *name)
{
	char *dst = NULL, *dir = NULL, *slash = NULL;
	int rc;

	xassert (lockdir != NULL);

	if ((rc = _is_valid_ipc_name(name)) != 1)
		fatal("invalid ipc name: `%s' %d", name, rc);
	else if (!(dst = xmalloc(PATH_MAX)))
		fatal("memory allocation failure");

	dir = lockdir;

	slash = (dir[strlen(dir) - 1] == '/') ? "" : "/";

#ifdef HAVE_SNPRINTF
	snprintf(dst, PATH_MAX, "%s%s%s", dir, slash, name+1);
#else
	sprintf(dst, "%s%s%s", dir, slash, name+1);
#endif /* HAVE_SNPRINTF */

	return(dst);
}

static int
_shm_unlink_lock()
{
	debug("process %ld removing shm lock", (long) getpid());
	if (sem_unlink(lockname) == -1) 
		return 0;
	xfree(lockname);
	return 1;
}

static sem_t *
_sem_open(const char *name, int oflag, ...)
{
	sem_t *sem;
	va_list ap;
	mode_t mode;
	unsigned int value;

	if (!(lockname = _create_ipc_name(name)))
		fatal("sem_open failed for [%s]: invalid IPC name", name);

	if (oflag & O_CREAT) {
		va_start(ap, oflag);
		mode = va_arg(ap, mode_t);
		value = va_arg(ap, unsigned int);
		va_end(ap);
		sem = sem_open(lockname, oflag, mode, value);
	} else 
		sem = sem_open(lockname, oflag);

	return(sem);
}


static void
_shm_initialize()
{
	int i;
	memset(slurmd_shm, 0, sizeof(slurmd_shm_t));
	for (i = 0; i < MAX_TASKS; i++)
		slurmd_shm->task[i].used = false;
	for (i = 0; i < MAX_JOB_STEPS; i++)
		slurmd_shm->step[i].state = SLURMD_JOB_UNUSED;
	slurmd_shm->version = SHM_VERSION;
}

int 
shm_insert_step(job_step_t *step)
{
	int i = 0;
	_shm_lock();
	if (_shm_find_step(step->jobid, step->stepid) >= 0) {
		_shm_unlock();
		error("shm_insert_step duplicate StepId=%u.%u",
		      step->jobid, step->stepid);
		slurm_seterrno_ret(EEXIST);
	}

    again:
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		if (slurmd_shm->step[i].state <= SLURMD_JOB_UNUSED)
			break;
	}

	if (i == MAX_JOB_STEPS) {
		if (_shm_clear_stale_entries() > 0)
			goto again;
		_shm_unlock();
		slurm_seterrno_ret(ENOSPC);
	}

	_shm_step_copy(&slurmd_shm->step[i], step);
	slurmd_shm->step[i].state = SLURMD_JOB_ALLOCATED;

	_shm_unlock();
	return SLURM_SUCCESS;
}

int 
shm_delete_step(uint32_t jobid, uint32_t stepid)
{
	int i;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) < 0) {
		_shm_unlock();
		slurm_seterrno_ret(ESRCH);
	}
	debug3("shm: found step %u.%u at %d", jobid, stepid, i);
	_shm_clear_step(&slurmd_shm->step[i]); 
	_shm_unlock();
	return 0;
}

int 
shm_update_step(job_step_t *step)
{
	int i, retval = 0;
	_shm_lock();
	if ((i = _shm_find_step(step->jobid, step->stepid)) >= 0) 
		_shm_step_copy(&slurmd_shm->step[i], step);
	else
		retval = -1;
	_shm_unlock();
	return retval;
}

int
shm_signal_step(uint32_t jobid, uint32_t stepid, uint32_t signal)
{
	int         signo  = (int) signal;
	int         retval = SLURM_SUCCESS;
	int         i;
	job_step_t *s;
	task_t     *t;

	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) < 0) {
		retval = EINVAL;
		goto done;
	}

	s = &slurmd_shm->step[i];

	if (stepid != NO_VAL)
		debug2 ("signal %d for %u.%u (sid: %lu)", 
	  	        signal, jobid, stepid, (unsigned long) s->sid);
	else
		debug2 ("signal %d for %u (sid: %lu)", 
		        signal, jobid, (unsigned long) s->sid);

	for (t = _taskp(s->task_list); t; t = _taskp(t->next)) {
		pid_t sid = getsid(t->pid);

		if ((sid <= (pid_t) 0) || (sid != s->sid))
			continue;

		if (t->pid <= (pid_t) 0) {
			debug ("job %u.%u: Bad pid value %lu", 
			       jobid, stepid, (unsigned long) t->pid);
			continue;
		}

		if (kill(t->pid, signo) < 0) {
			error ("kill %u.%u task %d pid %ld: %m", 
			       jobid, stepid, t->id, (long)t->pid);
			retval = errno;
		}
	}
done:
	_shm_unlock();
	if (retval > 0)
		slurm_seterrno_ret(retval);
	else
		return SLURM_SUCCESS;
}

static job_step_t *
_shm_copy_step(job_step_t *j)
{
	job_step_t *s;
	task_t *t;

	s = xmalloc(sizeof(job_step_t));
	_shm_step_copy(s, j);

	for (t = _taskp(j->task_list); t; t = _taskp(t->next)) {
		task_t *u = xmalloc(sizeof(*u));
		_shm_task_copy(u, t);
		_shm_prepend_task_to_step(s, u);
	}
	return s;
}


job_step_t *
shm_get_step(uint32_t jobid, uint32_t stepid)
{
	int i;
	job_step_t *s = NULL;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0) 
		s = _shm_copy_step(&slurmd_shm->step[i]);
	_shm_unlock();
	xassert(!s || ((s->stepid == stepid) && (s->jobid == jobid)));
	return s;
}

uid_t 
shm_get_step_owner(uint32_t jobid, uint32_t stepid)
{
	int i;
	uid_t owner = (uid_t) -1;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0)
		owner = slurmd_shm->step[i].uid;
	else 
		slurm_seterrno(ESRCH);
	_shm_unlock();
	return owner;
}


/*
 * Free a job step structure in local memory
 */
void 
shm_free_step(job_step_t *step)
{
	task_t *p, *t = step->task_list;
	xfree(step);
	if (!t) 
		return;
	do {
		p = t->next;
		xfree(t);
	} while ((t = p));
}

int 
shm_update_step_mpid(uint32_t jobid, uint32_t stepid, int mpid)
{
	int i, retval = SLURM_SUCCESS;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0)
		slurmd_shm->step[i].mpid = mpid;
	else {
		slurm_seterrno(ESRCH);
		retval = SLURM_FAILURE;
	}
	_shm_unlock();
	return retval;
}

int 
shm_update_step_sid(uint32_t jobid, uint32_t stepid, int sid)
{
	int i, retval = SLURM_SUCCESS;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0)
		slurmd_shm->step[i].sid = sid;
	else {
		slurm_seterrno(ESRCH);
		retval = SLURM_FAILURE;
	}
	_shm_unlock();
	return retval;
}

int 
shm_step_sid(uint32_t jobid, uint32_t stepid)
{
	int i, sid;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0)
		sid = slurmd_shm->step[i].sid;
	else {
		slurm_seterrno(ESRCH);
		sid = SLURM_FAILURE;
	}
	_shm_unlock();
	return sid;
}


int 
shm_update_step_state(uint32_t jobid, uint32_t stepid, job_state_t state)
{
	int i, retval = SLURM_SUCCESS;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0)
		slurmd_shm->step[i].state = state;
	else {
		slurm_seterrno(ESRCH);
		retval = SLURM_FAILURE;
	}
	_shm_unlock();
	return retval;
}

job_state_t *
shm_lock_step_state(uint32_t jobid, uint32_t stepid)
{
	int i;
	job_state_t *state = NULL;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0)
		state = &slurmd_shm->step[i].state;
	else {
		slurm_seterrno(ESRCH);
		_shm_unlock();
	}
	/* caller is responsible for unlocking */ 
	return state;
}

void
shm_unlock_step_state(uint32_t jobid, uint32_t stepid)
{
	/* May support individual job locks in the future, so we
	 * keep the arguments above
	 */
	_shm_unlock();
}


int 
shm_update_step_addrs(uint32_t jobid, uint32_t stepid, 
		      slurm_addr *ioaddr, slurm_addr *respaddr,
		      char *keydata)
{
	int i, retval = SLURM_SUCCESS;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0) {
		job_step_t *s = &slurmd_shm->step[i];

		/* Only allow one addr update at a time */
		if (!s->io_update) {
			s->ioaddr    = *ioaddr;
			s->respaddr  = *respaddr;
			memcpy(s->key.data, keydata, SLURM_IO_KEY_SIZE);
			s->io_update = true;

			debug3("Going to send shm update signal to %ld", 
				(long) s->mpid);
			if ((s->mpid > 0) && (kill(s->mpid, SIGHUP) < 0)) {
				slurm_seterrno(EPERM);
				retval = SLURM_FAILURE;
			}

		} else {
			slurm_seterrno(EAGAIN);
			retval = SLURM_FAILURE;
		}

	} else {
		slurm_seterrno(ESRCH);
		retval = SLURM_FAILURE;
	}
	_shm_unlock();
	return retval;
}

int
shm_step_addrs(uint32_t jobid, uint32_t stepid, 
	       slurm_addr *ioaddr, slurm_addr *respaddr, srun_key_t *key)
{
	int i, retval = SLURM_SUCCESS;

	xassert(jobid  >= 0);
	xassert(stepid >= 0);

	xassert(ioaddr   != NULL);
	xassert(respaddr != NULL);
	xassert(key      != NULL);

	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0) {
		job_step_t *s = &slurmd_shm->step[i];
		if (!s->io_update) {
			slurm_seterrno(0);
			retval = SLURM_FAILURE;
		} else {
			*ioaddr   = s->ioaddr;
			*respaddr = s->respaddr;
			memcpy(key->data, s->key.data, SLURM_IO_KEY_SIZE);
			s->io_update = false;
		}
	} else {
		slurm_seterrno(ESRCH);
		retval = SLURM_FAILURE;
	}
	_shm_unlock();
	return retval;
}

int 
shm_update_job_timelimit(uint32_t jobid, time_t newlim)
{
	int i, found = 0, retval = SLURM_SUCCESS;

	_shm_lock();
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		job_step_t *s = &slurmd_shm->step[i];
		if (s->jobid == jobid) {
			slurmd_shm->step[i].timelimit = newlim;
			found = 1;
		}
	}
	_shm_unlock();

	if (found == 0) { 
		slurm_seterrno(ESRCH);
		retval = SLURM_FAILURE;
	}
	return retval;
}

int 
shm_update_step_timelimit(uint32_t jobid, uint32_t stepid, time_t newlim)
{
	int i, retval = SLURM_SUCCESS;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0)
		slurmd_shm->step[i].timelimit = newlim;
	else { 
		slurm_seterrno(ESRCH);
		retval = SLURM_FAILURE;
	}
	_shm_unlock();
	return retval;
}

time_t
shm_step_timelimit(uint32_t jobid, uint32_t stepid)
{
	int i;
	time_t timelimit;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0)
		timelimit = slurmd_shm->step[i].timelimit;
	else {
		slurm_seterrno(ESRCH);
		timelimit = (time_t) SLURM_FAILURE;
	}
	_shm_unlock();
	return timelimit;
}

static int
_shm_find_step(uint32_t jobid, uint32_t stepid)
{
	int i;
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		job_step_t *s = &slurmd_shm->step[i];
		if (s->jobid == jobid && s->stepid == stepid) 
			return i;
	}
	return -1;
}

int
shm_add_task(uint32_t jobid, uint32_t stepid, task_t *task)
{
	int i;
	job_step_t *s;
	task_t *t;

	xassert(task != NULL);

	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) < 0) {
		_shm_unlock();
		slurm_seterrno_ret(ESRCH);
	} 
	s = &slurmd_shm->step[i];

	debug3("adding task %d to step %u.%u", task->id, jobid, stepid);

	if (_shm_find_task_in_step(s, task->id)) {
		_shm_unlock();
		slurm_seterrno_ret(EEXIST);
	}

	if (!(t = _shm_alloc_task())) {
		_shm_unlock();
		slurm_seterrno_ret(ENOMEM);
	}

	_shm_task_copy(t, task);
	_shm_prepend_task_to_step_internal(s, t);
	_shm_unlock();

	return 0;
}

static void
_shm_prepend_task_to_step_internal(job_step_t *s, task_t *task)
{
	task->next     = (s->task_list);
	s->task_list   = _toff(task);
	task->job_step = _soff(s);
}

static void
_shm_prepend_task_to_step(job_step_t *s, task_t *task)
{
	task->next     = s->task_list;
	s->task_list   = task;
	task->job_step = s;
}


static task_t *
_shm_find_task_in_step(job_step_t *s, int taskid)
{
	task_t *t = NULL;
	for (t = _taskp(s->task_list); t && t->used; t = _taskp(t->next)) {
		if (t->id == taskid)
			return t;
	}
	return NULL;
}

static task_t *
_shm_alloc_task(void)
{
	int i;
	for (i = 0; i < MAX_TASKS; i++) {
		if (slurmd_shm->task[i].used == 0) {
			slurmd_shm->task[i].used = 1;
			return &slurmd_shm->task[i];
		}
	}
	return NULL;
}

static void
_shm_task_copy(task_t *to, task_t *from)
{
	*to = *from;
	/* next and job_step are not valid for copying */
	to->used = 1;
	to->next = NULL;
	to->job_step = NULL;
}

static void 
_shm_step_copy(job_step_t *to, job_step_t *from)
{
	memcpy(to, from, sizeof(job_step_t));

	/* addition of tasks is another step */
	to->task_list = NULL;  
}

static void
_shm_clear_task(task_t *t)
{
	memset(t, 0, sizeof(task_t));
}

static void
_shm_clear_step(job_step_t *s)
{
	task_t *p, *t = _taskp(s->task_list);

	memset(s, 0, sizeof(job_step_t));
	if (!t) 
		return;
	do {
	       	p = _taskp(t->next);
		debug3("going to clear task %d", t->id);
		_shm_clear_task(t);
	} while ((t = p));
}

static int
_shm_clear_stale_entries(void)
{
	int i;
	int count = 0;
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		job_step_t *s = &slurmd_shm->step[i];
		if (s->state == SLURMD_JOB_UNUSED) 
			continue;
		
		if ((s->sid > (pid_t) 0) && (kill(-s->sid, 0) != 0)) {
			debug ("Clearing stale job %u.%u from shm",
					s->jobid, s->stepid);
			_shm_clear_step(s);
			count++;
		}
	}

	return count;
}

static int
_shm_create()
{
	int oflags = IPC_CREAT | IPC_EXCL | 0600;
	key_t key = ftok(lockname, 1);

	if ((shmid = shmget(key, sizeof(slurmd_shm_t), oflags)) < 0) 
		return SLURM_ERROR;

	slurmd_shm = shmat(shmid, NULL, 0);
	if (slurmd_shm == (void *)-1 || slurmd_shm == NULL) {
		error("shmat: %m");
		return SLURM_ERROR;
	}

	_shm_initialize();

	return SLURM_SUCCESS;
}

static int
_shm_attach()
{
	int oflags = 0;
	struct shmid_ds shmi;
	key_t key = ftok(lockname, 1);

	if ((shmid = shmget(key, 1/* sizeof(slurmd_shm_t) */, oflags)) < 0) {
		error("shmget: %m");
		return SLURM_ERROR;
	}

	if (shmctl(shmid, IPC_STAT, &shmi) < 0) {
		error ("shmctl: unable to get info for shm id %d", shmid);
	}

	if (shmi.shm_segsz != sizeof(slurmd_shm_t)) {
		error("size for shm segment id %d is %dK, expected %dK",
		      shmid, (int)(shmi.shm_segsz/1024), 
		      (sizeof(slurmd_shm_t)/1024));
		error("You probably need to run with `-c' "
			"or just delete old segment.");
		slurm_seterrno_ret(EINVAL);
	}

	slurmd_shm = shmat(shmid, NULL, 0);
	if (slurmd_shm == (void *)-1 || slurmd_shm == NULL) { 
		error("shmat: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/* 
 * Attempt to create a new shared segment. If exclusive create fails,
 * attach to existing segment and reinitialize it.
 */
static int
_shm_new()
{
	xassert(shm_lock != NULL);
	xassert(shm_lock != SEM_FAILED);

	if (_shm_create() < 0) {
		if (_shm_attach() < 0) {
			error("shm_attach: %m");
			return SLURM_FAILURE;
		}
		debug("Existing shm segment found, going to reinitialize it.");
		_shm_initialize();
	}
	attach_pid = getpid();
	slurmd_shm->users = 1;
	_shm_unlock();
	return SLURM_SUCCESS;
}

/*
 * Reattach to existing shared memory segment. If shared segment does
 * not exist, create and initialize it.
 * Here we assume that create of new semaphore failed, so we attach to
 * the existing semaphore.
 */
static int
_shm_reopen()
{
	int retval = SLURM_SUCCESS;
	int oflags = O_EXCL;        /* Try to reopen semaphore first */

	debug2("going to reopen slurmd shared memory");

	shm_lock = _sem_open(SHM_LOCKNAME, oflags);
	/*
	 * If open of shm lock failed, we could be in one of two
	 * situations:  
	 *
	 * 1. The lockfile associated with the semaphore exists,
	 *    but the semaphore does not exist (errno == ENOENT) 
	 *    or
	 * 2. system failure trying to attach to semaphore.
	 *
	 *  For 1, we can cleanup the shm lock, then initialize
	 *  a new shared memory region, but for 2, we need to
	 *  exit with a failure
	 */

	if ((shm_lock == SEM_FAILED) || (!_shm_sane())) {
		debug2("Shared memory not in sane state - reinitializing.");

		/*
		 * Unlink old lockfile, reopen semaphore with create flag,
		 * and create new shared memory area
		 */
		sem_unlink(lockname);
		shm_lock = _sem_open(SHM_LOCKNAME, oflags|O_CREAT, 0600, 1);
		if (shm_lock == SEM_FAILED) {
			error ("reopen of [%s] failed: %m", lockname);
			return SLURM_ERROR;
		}
		return _shm_new();
	}

	
	/* 
	 * Attach to shared memory region 
	 * If attach fails, try to create a new shm segment
	 */
	if ((_shm_attach() < 0) && (_shm_create() < 0)) {
		error("shm_create(): %m");
		return SLURM_FAILURE;
	}

	debug3("successfully attached to slurmd shm");

	/* 
	 * Lock and unlock semaphore to ensure data is initialized 
	 */
	_shm_lock();
	if (slurmd_shm->version != SHM_VERSION) {
		error("_shm_reopen: Wrong version in shared memory");
		retval = SLURM_FAILURE;
	} else {
		slurmd_shm->users++;
		attach_pid = getpid();
	}

	_shm_unlock();
	debug3("leaving _shm_reopen()");

	return retval;
}


/* get and initialize, if necessary, the shm semaphore
 * if lock did not exist, assume we need to initialize shared region
 */
static int
_shm_lock_and_initialize()
{
	if (slurmd_shm 
	   && (slurmd_shm->version == SHM_VERSION)
	   && (shm_lock != SEM_FAILED)) {           
		/* we've already opened shared memory */
		_shm_lock();
		attach_pid = getpid();
		slurmd_shm->users++;
		_shm_unlock();
		return SLURM_SUCCESS;
	}

	/*
	 * Create locked semaphore (initial value == 0)
	 */

	/* 
	 * Init lockdir to slurmd spooldir. 
	 * Make sure it does not change for this instance of slurmd,
	 *  even if spooldir does.
	 */
	if (!lockdir)
		lockdir = xstrdup(conf->spooldir);

	shm_lock = _sem_open(SHM_LOCKNAME, O_CREAT|O_EXCL, 0600, 0);
	debug3("slurmd lockfile is \"%s\"", lockname);

	if (shm_lock != SEM_FAILED) /* lock didn't exist. Create shmem      */
		return _shm_new();
	else                        /* lock exists. Attach to shared memory */
		return _shm_reopen();

}

/* Validate the shm contents (particilarly the pids) are valid */
static int
_shm_validate(void)
{
	int i;

	_shm_lock();
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		job_step_t *s = &slurmd_shm->step[i];
		if (s->state == SLURMD_JOB_UNUSED)
			continue;

		/*
		 * Consider a job step running if the step state is less
		 * than STARTED or if s->sid has not yet been set.
		 * If the state is >= STARTED, check for running processes
		 * by attempting to signal the running session.
		 */
		if ((s->state >= SLURMD_JOB_STARTED) &&
		    (s->sid   >  (pid_t) 0) &&
		    (!_valid_slurmd_sid(s->sid))) {
			info ("Clearing defunct job %u.%u sid %u from shm",
					s->jobid, s->stepid, 
					(unsigned int) s->sid);
			_shm_clear_step(s);
		} else
			debug3 ("Preserving shm for job %u.%u",
					s->jobid, s->stepid);
	}

	_shm_unlock();
	return SLURM_SUCCESS;
}

/*
 * Confirm that the supplied sid belongs to a valid slurmd job manager. 
 * For now just confirm the sid is still valid.
 * Ideallly we want to test argv[0] of the process, but that is far
 * more work and slurmd shared memory corruption has never been observed.
 */
static bool
_valid_slurmd_sid(pid_t sid)
{
	pid_t session;
	xassert(sid > (pid_t) 1);

	/* Check for active session */
	if (kill(-sid, 0) != 0)
		return false;

	/* Ensure that session leader's pid == sid */
	session = getsid(sid);
       	if ((session > (pid_t) 0) && (session != sid))
		return false;

	return true;
}

/*
 * Check that shared memory is in a clean state
 *    
 */
static bool
_shm_sane(void)
{
	struct stat st;
	int         val;

	if (stat(lockname, &st) < 0)
		error("Unable to stat lock file: %m");

	sem_getvalue(shm_lock, &val);

	debug3("shm lock val = %d, last accessed at %s", 
	      val, ctime(&st.st_atime));

	if ((val == 0) && ((time(NULL) - st.st_atime) > 30))
	     return false;	

	return true;
}

static void 
_shm_lock()
{
    restart:
	if (sem_wait(shm_lock) == -1) {
		if (errno == EINTR)
			goto restart;
		fatal("_shm_lock: %m");
	}
	return;
}

static void
_shm_unlock()
{
	int saved_errno = errno;
    restart:
	if (sem_post(shm_lock) == -1) {
		if (errno == EINTR)
			goto restart;
		fatal("_shm_unlock: %m");
	}
	errno = saved_errno;
	return;
}
