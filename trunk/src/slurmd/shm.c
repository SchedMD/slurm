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
#  include <config.h>
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

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/slurm_errno.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/shm.h"

/* We use Chris Dunlap's POSIX semaphore implementation if necessary */
#include "src/slurmd/semaphore.h"

#define MAX_JOB_STEPS	16
#define MAX_BATCH_JOBS	128
#define MAX_TASKS	1024

#define SHM_LOCKNAME	"/.slurm.lock"

/* Increment SHM_VERSION if format changes */
#define SHM_VERSION	1001

/* These macros convert shared memory pointers to local memory
 * pointers and back again. Pointers in shared memory are relative
 * to the address at which the shared memory is attached
 *
 * These routines must be used to convert a pointer in shared memory
 * back to a "real" pointer in local memory. e.g. t = _taskp(t->next)
 */
#define _laddr(p)  \
	((p) ? (((size_t)(p)) + ((size_t)slurmd_shm)) : (size_t)NULL)  
#define _offset(p) \
        ((p) ? (((size_t)(p)) - ((size_t)slurmd_shm)) : (size_t)NULL) 

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
static int  _shm_find_step(uint32_t, uint32_t);

static task_t *     _shm_alloc_task(void);
static task_t *     _shm_find_task_in_step(job_step_t *s, int taskid);
static job_step_t * _shm_copy_step(job_step_t *j);


/* initialize shared memory: Attach to memory if shared region 
 * already exists - otherwise create and attach
 */
int
shm_init(void)
{
	return _shm_lock_and_initialize();
}

/* Detach from shared memory */
int
shm_fini(void)
{
	int destroy = 0;
	xassert(slurmd_shm != NULL);
	_shm_lock();

	debug3("%ld calling shm_fini() (attached by %ld)", 
		getpid(), attach_pid);
	/* xassert(attach_pid == getpid()); */

	/* if ((attach_pid == getpid()) && (--slurmd_shm->users == 0))
         *		destroy = 1;
	 */
	if (--slurmd_shm->users == 0)
		destroy = 1;

	/* detach segment from local memory */
	if (shmdt(slurmd_shm) < 0) {
		error("shmdt: %m");
		return -1;
	}

	slurmd_shm = NULL;

	if (destroy && (shmctl(shmid, IPC_RMID, NULL) < 0)) {
		error("shmctl: %m");
		return -1;
	}
	_shm_unlock();
	if (destroy && (_shm_unlink_lock() < 0)) {
		error("_shm_unlink_lock: %m");
		return -1;
	}

	return 0;
}

void
shm_cleanup(void)
{
	char *s;

	if ((s = _create_ipc_name(SHM_LOCKNAME))) {
		verbose("request to destroy shm lock `%s'", s);
		if (sem_unlink(s) < 0)
			error("sem_unlink: %m");
		xfree(s);
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
	task_t     *t;

	xassert(slurmd_shm != NULL);

	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0) {
		s = &slurmd_shm->step[i];
		for (t = _taskp(s->task_list); t; t = _taskp(t->next)) {
			/* If at least one task still remains, consider
			 * the job running
			 */
			if (getsid(t->pid) == s->sid)
				retval = true;
		}	
	} 
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

static char *
_create_ipc_name(const char *name)
{
	char *dst, *dir, *slash;
	int rc;

	if ((rc = _is_valid_ipc_name(name)) != 1) {
		error("invalid ipc name: `%s' %d", name, rc);
		return NULL;
	}
	else if (!(dst = xmalloc(PATH_MAX)))
		return NULL;

#if defined(POSIX_IPC_PREFIX) && defined(HAVE_POSIX_SEMS)
	dir = POSIX_IPC_PREFIX;
#else
	if (!(dir = conf->spooldir) || !(dir = getenv("TMPDIR")) || !strlen(dir)) 
		dir = "/tmp";
#endif /* POSIX_IPC_PREFIX */

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
	debug("process %ld removing shm lock", getpid());
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

	if (!(lockname = _create_ipc_name(name))) {
		fatal("sem_open failed for [%s]: invalid IPC name", name);
	}

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
		slurm_seterrno_ret(EEXIST);
	}

	for (i = 0; i < MAX_JOB_STEPS; i++) {
		if (slurmd_shm->step[i].state <= SLURMD_JOB_UNUSED)
			break;
	}
	if (i == MAX_JOB_STEPS) {
		_shm_unlock();
		slurm_seterrno_ret(ENOSPC);
	} else {
		_shm_step_copy(&slurmd_shm->step[i], step);
	}

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
	debug3("shm: found step %d.%d at %d", jobid, stepid, i);
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
	if ((i = _shm_find_step(jobid, stepid)) >= 0) {
		s = &slurmd_shm->step[i];
		for (t = _taskp(s->task_list); t; t = _taskp(t->next)) {
			if (getsid(t->pid) != s->sid)
				continue;
			if (t->pid > 0 && kill(t->pid, signo) < 0) {
				error("kill %d.%d task %d pid %ld: %m", 
				      jobid, stepid, t->id, (long)t->pid);
				retval = errno;
			}
		}	
	} else
		retval = ESRCH;

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

	s = xmalloc(sizeof(*s));
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
	job_step_t *s;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0) 
		s = _shm_copy_step(&slurmd_shm->step[i]);
	_shm_unlock();
	return s;
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
		      slurm_addr *ioaddr, slurm_addr *respaddr)
{
	int i, retval = SLURM_SUCCESS;
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0) {
		job_step_t *s = &slurmd_shm->step[i];

		/* Only allow one addr update at a time */
		if (!s->io_update) {
			s->ioaddr = *ioaddr;
			s->respaddr = *respaddr;
			s->io_update = true;
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
	       slurm_addr *ioaddr, slurm_addr *respaddr)
{
	int i, retval = SLURM_SUCCESS;
	xassert(ioaddr != NULL);
	xassert(respaddr != NULL);
	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0) {
		job_step_t *s = &slurmd_shm->step[i];
		*ioaddr   = s->ioaddr;
		*respaddr = s->respaddr;
		s->io_update = false;
	} else {
		slurm_seterrno(ESRCH);
		retval = SLURM_FAILURE;
	}
	_shm_unlock();
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

	debug3("adding task %d to step %d.%d", task->id, jobid, stepid);

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
	/* next and step are not valid for copying */
	to->used = 1;
	to->next = NULL;
	to->job_step = NULL;
}

static void 
_shm_step_copy(job_step_t *to, job_step_t *from)
{
	*to = *from;
	to->state = SLURMD_JOB_ALLOCATED;

	/* addition of tasks is another step */
	to->task_list = NULL;  
}

static void
_shm_clear_task(task_t *t)
{
	memset(t, 0, sizeof(*t));
}

static void
_shm_clear_step(job_step_t *s)
{
	task_t *p, *t = _taskp(s->task_list);
	memset(s, 0, sizeof(*s));
	if (!t) 
		return;
	do {
	       	p = _taskp(t->next);
		debug3("going to clear task %d", t->id);
		_shm_clear_task(t);
	} while ((t = p));
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
	key_t key = ftok(lockname, 1);

	if ((shmid = shmget(key, sizeof(slurmd_shm_t), oflags)) < 0) 
		return SLURM_ERROR;

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

	if ((shm_lock = _sem_open(SHM_LOCKNAME, 0)) == SEM_FAILED) {
		if (errno == ENOENT) {
			debug("Lockfile found but semaphore deleted:"
			     " creating new shm segment");
			shm_cleanup();
			return _shm_lock_and_initialize();
		}
		error("Unable to initialize semaphore: %m");
		return SLURM_FAILURE;
	}

	/* Attach to shared memory region */
	if ((_shm_attach() < 0) && (_shm_create() < 0)) {
		error("shm_create(): %m");
		return SLURM_FAILURE;
	}


	/* 
	 * Lock and unlock semaphore to ensure data is initialized 
	 */
	_shm_lock();
	if (slurmd_shm->version != SHM_VERSION) {
		error("shm_init: Wrong version in shared memory");
		retval = SLURM_FAILURE;
	} else {
		slurmd_shm->users++;
		attach_pid = getpid();
	}

	_shm_unlock();

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
		if (attach_pid != getpid()) {
			attach_pid = getpid();
			slurmd_shm->users++;
		}
		_shm_unlock();
		return SLURM_SUCCESS;
	}

	shm_lock = _sem_open(SHM_LOCKNAME, O_CREAT|O_EXCL, 0600, 0);
	if (shm_lock != SEM_FAILED) /* lock didn't exist. Create shmem      */
		return _shm_new();
	else                        /* lock exists. Attach to shared memory */
		return _shm_reopen();
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
    restart:
	if (sem_post(shm_lock) == -1) {
		if (errno == EINTR)
			goto restart;
		fatal("_shm_unlock: %m");
	}
	return;
}
