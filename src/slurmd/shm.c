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
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <signal.h>

#include <src/common/list.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>
#include <src/common/xassert.h>
#include <src/common/slurm_errno.h>

#include <src/slurmd/shm.h>

/* We use Chris Dunlap's POSIX semaphore implementation if necessary */
#include <src/slurmd/test/semaphore.h>

#define MAX_JOB_STEPS	16
#define MAX_TASKS	1024

#define SHM_LOCKNAME	"/.slurm.lock"

/* Increment SHM_VERSION if format changes */
#define SHM_VERSION	0x1001

typedef struct shmem_struct {
	int        version;
	int        users;	
	job_step_t step[MAX_JOB_STEPS];
	task_t     task[MAX_TASKS];
} slurmd_shm_t;


/* static variables: */
static sem_t *shm_lock;
static char  *lockname;
static int shmid;
static slurmd_shm_t *slurmd_shm;

/* static function prototypes: */
static int  _is_valid_ipc_name(const char *name);
static char *_create_ipc_name(const char *name);
static int _shm_unlink_lock(void);
static int  _shm_lock_and_initialize(void);
static void _shm_lock(void);
static void _shm_unlock(void);
static void _shm_initialize(void);
static void _shm_prepend_task_to_step(job_step_t *, task_t *);
static void _shm_task_copy(task_t *, task_t *);
static void _shm_step_copy(job_step_t *, job_step_t *);
static void _shm_clear_task(task_t *);
static void _shm_clear_step(job_step_t *);
static int  _shm_find_step(uint32_t, uint32_t);
static task_t * _shm_alloc_task(void);
static task_t * _shm_find_task_in_step(job_step_t *s, int taskid);


/* initialize shared memory: 
 * Attach if shared region already exists, otherwise create and attach
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
	info("process %ld detaching from shm", getpid());
	xassert(slurmd_shm != NULL);
	_shm_lock();
	if (--slurmd_shm->users == 0)
		destroy = 1;

	/* detach segment from local memory */
	if (shmdt(slurmd_shm) < 0) {
		error("shmdt: %m");
		return -1;
	}

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
		info("going to destroy shm lock `%s'", s);
		if (sem_unlink(s) < 0)
			error("sem_unlink: %m");
		xfree(s);
	}



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
	if (!(dir = getenv("TMPDIR")) || !strlen(dir)) 
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
	debug3("process %ld removing shm lock", getpid());
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
		if (slurmd_shm->step[i].state == SLURMD_JOB_UNUSED)
			break;
	}
	if (i == MAX_JOB_STEPS) {
		_shm_unlock();
		slurm_seterrno_ret(ENOSPC);
	} else
		_shm_step_copy(&slurmd_shm->step[i], step);

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
		for (t = s->task_list; t; t = t->next) {
			if (t->pid > 0 && kill(t->pid, signo) < 0) {
				error("kill %d.%d pid %ld: %m", 
				      jobid, stepid, (long)t->pid);
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


job_step_t *
shm_get_step(uint32_t jobid, uint32_t stepid)
{
	int i;
	job_step_t *s = NULL;
	task_t *t;

	_shm_lock();
	if ((i = _shm_find_step(jobid, stepid)) >= 0) {
		s = xmalloc(sizeof(job_step_t));
		_shm_step_copy(s, &slurmd_shm->step[i]);
		for (t = slurmd_shm->step[i].task_list; t; t = t->next) {
			task_t *u = xmalloc(sizeof(task_t));
			_shm_task_copy(u, t);
			_shm_prepend_task_to_step(s, u);
		}

	}
	_shm_unlock();
	return s;
}

void 
shm_free_step(job_step_t *step)
{
	task_t *p, *t;
	if ((t = step->task_list)) {
		do {
			p = t->next;
			xfree(t);
		} while ((t = p));
	}
	xfree(step);
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
	if (_shm_find_task_in_step(s, task->id)) {
		_shm_unlock();
		slurm_seterrno_ret(EEXIST);
	}
	if (!(t = _shm_alloc_task())) {
		_shm_unlock();
		slurm_seterrno_ret(ENOMEM);
	}
	_shm_task_copy(t, task);
	_shm_prepend_task_to_step(s, t);
	_shm_unlock();
	return 0;
}

static void
_shm_prepend_task_to_step(job_step_t *s, task_t *task)
{
	task->next = s->task_list;
	s->task_list = task;
	task->job_step = s;
}

static task_t *
_shm_find_task_in_step(job_step_t *s, int taskid)
{
	task_t *t = NULL;
	for (t = s->task_list; t && t->used; t = t->next) {
		if (t->id == taskid)
			break;
	}
	return t;
}

static task_t *
_shm_alloc_task(void)
{
	int i;
	for (i = 0; i < MAX_TASKS; i++) {
		if (!slurmd_shm->task[i].used) 
			return &slurmd_shm->task[i];
	}
	return NULL;
}

static void
_shm_task_copy(task_t *to, task_t *from)
{
	*to = *from;
	/* next and step are not valid for copying */
	to->next = NULL;
	to->job_step = NULL;
}

static void 
_shm_step_copy(job_step_t *to, job_step_t *from)
{
	task_t *t = NULL;
	if (to->task_list)
		t = to->task_list;
	*to = *from;
	to->state = SLURMD_JOB_ALLOCATED;
	to->task_list = t; /* addition of tasks is another step */
}

static void
_shm_clear_task(task_t *t)
{
	memset(t, 0, sizeof(*t));
}

static void
_shm_clear_step(job_step_t *s)
{
	task_t *p, *t = s->task_list;
	do {
		p = t->next;
		_shm_clear_task(t);
	} while ((t = p));

	memset(s, 0, sizeof(*s));
}


static int
_shm_create()
{
	int oflags = IPC_CREAT | IPC_EXCL | 0600;
	key_t key = ftok(".", 'a');

	if ((shmid = shmget(key, sizeof(slurmd_shm_t), oflags)) < 0) {
		if ((shmid = shmget(key, sizeof(slurmd_shm_t), 0600)) < 0)
		error("shmget: %m");
		return SLURM_ERROR;
	}

	slurmd_shm = shmat(shmid, NULL, 0);
	if (slurmd_shm == (void *)-1 || slurmd_shm == NULL) {
		error("shmat: %m");
		return SLURM_ERROR;
	}

	_shm_initialize();

	return 1;
}

static int
_shm_attach()
{
	int oflags = 0;
	key_t key = ftok(".", 'a');

	if ((shmid = shmget(key, sizeof(slurmd_shm_t), oflags)) < 0) 
		fatal("shm_attach: %m");

	slurmd_shm = shmat(shmid, NULL, 0);
	if (slurmd_shm == (void *)-1 || !slurmd_shm) 
		fatal("shmat: %m");

	return 1;
}

/* 
 * Create shared memory region if it doesn't exist, if it does exist,
 * reinitialize it.
 *
 */
static int
_shm_new()
{
	if ((_shm_create() < 0) && (_shm_attach() < 0)) {
		error("shm_attach: %m");
		return SLURM_FAILURE;
	}
	_shm_initialize();
	slurmd_shm->users = 1;
	_shm_unlock();
	return SLURM_SUCCESS;
}

static int
_shm_reopen()
{
	int retval = SLURM_SUCCESS;

	if ((shm_lock = _sem_open(SHM_LOCKNAME, 0)) == SEM_FAILED) {
		error("Unable to initialize semaphore: %m");
		return SLURM_FAILURE;
	}

	/* Attach to shared memory region */
	_shm_attach();

	/* Lock and unlock semaphore to ensure data is initialized */
	_shm_lock();
	if (slurmd_shm->version != SHM_VERSION) {
		error("shm_init: Wrong version in shared memory");
		retval = SLURM_FAILURE;
	} else
		slurmd_shm->users++;
	_shm_unlock();

	return retval;
}


/* get and initialize, if necessary, the shm semaphore
 * if lock did not exist, assume we need to initialize shared region
 */
static int
_shm_lock_and_initialize()
{
	if (slurmd_shm && slurmd_shm->version == SHM_VERSION) {           
		/* we've already opened shared memory */
		_shm_lock();
		slurmd_shm->users++;
		_shm_unlock();
		return SLURM_SUCCESS;
	}

	shm_lock = _sem_open(SHM_LOCKNAME, O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, 0);

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
