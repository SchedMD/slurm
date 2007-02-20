/*****************************************************************************\
 *  proctrack.c - Installation code for the Process tracking kernel extension 
 *  for AIX: proctrackext.c.  
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
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

#define _LDEBUG 0
#define _KERNEL


#include <sys/id.h>
#include <sys/lock_alloc.h>
#include <sys/lock_def.h>
#include <sys/malloc.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include "proctrack.h"
#include "proctrack_loader.h"

#include <unistd.h>

#if _LDEBUG
#include <stdio.h>
#define PROCTRACK_MAGIC 0xDEAD9832
#include <sys/file.h>
#endif

#define MALLOC_ALIGN 3		/* 4 byte alignment */

static char *proctrackext_version =
	"@(#)proctrack kernel extension version = " PROCTRACK_VERSION;

/* process records, locate entries via proc_hash or proc_free and 
 * link records on collision */
struct proc_rec {
	uint32_t		job_id;	/* slurm job id */
	pid_t			pid;
	/* uid is the uid of the job creator, not necessarily the uid
	   of the owner of the process */
	uid_t			uid;
	struct proc_rec *	next;	/* next hash entry if collision */
#if _LDEBUG
	uint32_t		magic;
#endif
}; 

static struct proc_rec *	proc_recs = NULL;
static struct proc_rec **	proc_hash = NULL;
static struct proc_rec *	proc_free = NULL;
static int			proc_max  = 0;

static struct prochr		prochr;
Simple_lock			proc_lock;

static int proctrack_init(int max_procs);
static int proctrack_fini(void);
static struct proc_rec *_add_proc_recs(pid_t pid, uint32_t job_id, uid_t uid);
static struct proc_rec *_find_proc_recs(pid_t pid);
static int _free_proc_recs(pid_t pid);
static int  _fini_proc_recs(void);
static int  _init_proc_recs(int max_procs);
static void _prochr_handler(struct prochr* prochr, int reason, long pid);
static void _prochr_create_handler(struct prochr* prochr, int reason, long pid);
static void _prochr_term_handler(struct prochr* prochr, int reason, long pid);
static uid_t _get_job_uid(uint32_t job_id);

#if _LDEBUG
/* Log routine prototypes */
int open_log(char *path, struct file **fpp);
int write_log(struct file *fpp, char *buf, int *bytes_written);
int close_log(struct file *fpp);
int *tmpInt;
#endif
static void _log(char *msg);

struct file *fpp = NULL;
int fstat;
char buf[100];
int bytes_written;

int
proctrackext(int cmd, struct uio *uiop)
{
        int rc;
        char *bufp;
        static int j = 0;
        struct extparms *parms;
        int i;
        
        strcpy(buf, "proctrackext was called for configuration\n");
        _log(buf);
        if (fstat != 0) return(fstat);
        
        switch (cmd)
                {
                case 1:  /* configure/startup */
                        if ((uiop == NULL) || (uiop->uio_iov == NULL)) {
                                strcpy(buf, "uiop->uio_vec is null\n");
                                _log(buf);
                                break;
                        }
                        
                        parms = (struct extparms *) uiop->uio_iov->iov_base;
                        j = *(int *) &parms->buf[0];
                        
                        if (rc = proctrack_init(j))
                                {
                                        sprintf(buf, "Init failed %d\n", rc);
                                        _log(buf);
                                } else {
                                        sprintf(buf, "Init succeeded\n");
                                        _log(buf);
                                }
                        break;
                        
                case 2:  /* cleanup/shutdown */
                        sprintf(buf, "SHUTDOWN command\n");
                        _log(buf);
                        if (rc = proctrack_fini())
                                {
                                        sprintf(buf, "Fini failed %d\n", rc);
                                        _log(buf);
                                } else {
                                        sprintf(buf, "Fini succeeded\n");
                                        _log(buf);
                                }
                        
                        /* as last thing : close the log file */
                        strcpy(buf, "proctrack_fini: shutting down.\n");
                        _log(buf);
#if _LDEBUG                        
                        close_log(fpp);
#endif
                        fpp = NULL;
                        break;
                        
                default:  /* Unknown command value */
                        sprintf(buf, "Received unknown command of %d\n", cmd);
                        _log(buf);
                        return(-1);
                        break;
                }
        
        return(0);
}

/* log a failure mode */
static void _log(char *msg)
{
#if _LDEBUG
        if (!fpp) {
                /* Open the log file */
                strcpy(buf, "./proctrackext.log");
                fstat = open_log(buf, &fpp);
        } else {
                write_log(fpp, msg, &bytes_written);
        } 
#endif
}

/*
 * proctrack_init - Initialization. Establish data structures and 
 * register for notification of process creation and terminiation.
 * max_procs_ptr IN - pointer to maximum number of processes to accomodate
 * RET 0 on success, -1 on error and set ut_error field for thread 
 */
static int proctrack_init(int max_procs)
{
        int rc = 0;
        
#if _LDEBUG
        sprintf(buf, "proctrack_init: max procs = %d\n", max_procs);
        _log(buf);
#  ifdef __64BIT_KERNEL
	sprintf(buf, "proctrack_init: kernel is 64bit\n");
        _log(buf);
#  else
	sprintf(buf, "proctrack_init: kernel is 32bit\n");
        _log(buf);
#  endif
#endif
        
        /* validate the request */
        if (getuidx(ID_EFFECTIVE) != 0) {
                setuerror(EPERM);
#if _LDEBUG
                sprintf(buf, "effective id is not zero (%d)\n",getuidx(ID_EFFECTIVE));
                _log(buf);
#endif
                return -1;
        }
        if (max_procs < 1) {
                setuerror(EINVAL);
#if _LDEBUG
                strcpy(buf, "max_procs less than 1\n");
                _log(buf);
#endif
                return -2;
        }
        if (proc_recs != NULL) {
                /* already initialized, never terminated */
                setuerror(EALREADY);
#if _LDEBUG
                strcpy(buf, "proctrack_init: already initialized\n");
                _log(buf);
#endif
                return -3;
        }
        
        /* initialize the lock */
        lock_alloc(&proc_lock, LOCK_ALLOC_PIN, 0, -1);
        simple_lock(&proc_lock);
        
        prochr.prochr_next = NULL;
        prochr.prochr_handler = _prochr_handler;
        prochr.prochr_mask = PROCHR_INITIALIZE | PROCHR_TERMINATE | PROCHR_RESTART;
        proch_reg(&prochr);
        
        /* initialize process tracking records */
        if (_init_proc_recs(max_procs) < 0) {
                (void) proch_unreg(&prochr);
                rc = -5;
        }
        
        simple_unlock(&proc_lock);
        return rc;
}

/*
 * proctrack_fini - Termination. Clean everything up.
 * RET 0 on success, -1 on error and set ut_error field for thread
 */
static int proctrack_fini(void)
{
        int rc = 0;
        
#if _LDEBUG
        _log("proctrack_fini");
#endif
        
        /* validate the request */
        if (getuidx(ID_EFFECTIVE) != 0) {
                setuerror(EPERM);
                return -1;
        }
        
        if (proc_recs == NULL) {
                /* already cleared or never initialized */
                setuerror(EALREADY);
                return -1;
        }
        
        proch_unreg(&prochr);
        
        (void) _fini_proc_recs();
        simple_unlock(&proc_lock);
        lock_free(&proc_lock);
        return rc;
}

/*
 * proctrack_job_kill - Kill processes associated with a SLURM job 
 * RET 0 on success, -1 on failure
 */
extern int proctrack_job_kill(int *job_id_ptr, int *signal_ptr)
{
        uint32_t job_id = (uint32_t) fuword(job_id_ptr);
        uint32_t signal = (uint32_t) fuword(signal_ptr);
        struct proc_rec *proc_ptr;
        int rc = 0;
        int i;
        int found = FALSE;
	pid_t uid = getuidx(ID_EFFECTIVE);

#if _LDEBUG
        sprintf(buf, "proctrack_job_kill(%d, %d)\n", job_id, signal);
        _log(buf);
#endif

        simple_lock(&proc_lock);
        for (i = 0 ; i < proc_max ; i++) {
                proc_ptr = proc_hash[i];
                while (proc_ptr) {
                        if (proc_ptr->job_id == job_id) {
                                found = TRUE;
#if _LDEBUG
				sprintf(buf, "proctrack_job_kill found process %d, uid %d (caller uid %d)\n",
					proc_ptr->pid, proc_ptr->uid, uid);
				_log(buf);
#endif
                                if (signal) {
					/* Only allow root and the creator of
					   the job are allowed to signal
					   the job. */
/* 					if (uid != 0 && uid != proc_ptr->uid) { */
/* 						simple_unlock(&proc_lock); */
/* 						setuerror(EPERM); */
/* 						return -1; */
/* 					} */
					/* The above check isn't necessary
					   because kill won't send signals
					   to processes if the caller does
					   not have permission to do so.
					   But this doesn't seem
					   to be documented anywere... */
                                        rc = kill(proc_ptr->pid, signal);
					if (rc == -1) {
						simple_unlock(&proc_lock);
						return -1;
					}
#if _LDEBUG
					sprintf(buf, "proctrack_job_kill pid"
						" %d: rc = %d\n",
						proc_ptr->pid, rc);
					_log(buf);
#endif
				}
			} 
			proc_ptr = proc_ptr->next;
		}
	}
        simple_unlock(&proc_lock);
        if (found == FALSE)
                return -1;
        else
                return 0;
}

/*
 * proctrack_job_unreg -Unregister the creation of a SLURM job and this 
 * process, based upon getpid()
 * RET 0 on success, -1 on error and set ut_error field for thread
 */
extern int proctrack_job_unreg(int *job_id_ptr)
{
        uint32_t job_id = (uint32_t) fuword(job_id_ptr);
        struct proc_rec *proc_ptr;
        int rc = 0;
        int i;

#if _LDEBUG
        sprintf(buf, "proctrack_job_unreg(%d)\n", job_id);
        _log(buf);
#endif

        simple_lock(&proc_lock);
        for (i = 0 ; i < proc_max ; i++) {
                proc_ptr = proc_hash[i];
                
                while (proc_ptr) {
                        if (proc_ptr->job_id == job_id) {
#if _LDEBUG
                                sprintf(buf, "proctrack_job_unreg: cannot unregister job, lingering pid: %d\n", proc_ptr->pid);
                                _log(buf);
#endif
                                rc = -1;
                                goto done;
                        } else {
                                proc_ptr = proc_ptr->next;
                        }
                }                                
        }
done:	simple_unlock(&proc_lock);
        return rc;
}


/*
 * proctrack_job_reg_self - Register the creation of a SLURM job and this 
 * process, based upon getpid()
 * RET 0 on success, -1 on error and set ut_error field for thread
 */
extern int proctrack_job_reg_self(int *job_id_ptr)
{
        uint32_t job_id = (uint32_t) fuword(job_id_ptr);
        int rc = 0;
        pid_t pid = getpid();
	uid_t caller_uid = getuidx(ID_EFFECTIVE);
	uid_t job_uid;
        
#if _LDEBUG
        sprintf(buf, "proctrack_job_reg_self(%d)\n", job_id);
        _log(buf);
#endif
        if (proc_recs == NULL) {	/* not initialized */
#if _LDEBUG
                _log("process records not initialized");
#endif
                setuerror(ENOENT);
                return -1;
        }
        
        simple_lock(&proc_lock);
        
        /* Insure this pid not a duplicate */
        if (_find_proc_recs(pid) != NULL) {
                _log("proctrack_job_reg duplicate");
                setuerror(EEXIST);
                rc = -1;
                goto done;
        }
        
	job_uid = _get_job_uid(job_id);
#if _LDEBUG
        sprintf(buf, "proctrack_job_reg_self: _get_job_uid returned %d\n", job_id);
        _log(buf);
#endif
	if (job_uid == (uid_t)-1) {
		/* job id is not yet used */
		job_uid = caller_uid;
	} else if (caller_uid != job_uid) {
		setuerror(EPERM);
		rc = -1;
		goto done;
	}

        /* Create a new record with SLURM job id */
        if (_add_proc_recs(pid, job_id, job_uid) == NULL) {
                _log("proctrack_job_reg table full");
                setuerror(ENOMEM);
                rc = -1;
        }
        
 done:	simple_unlock(&proc_lock);
        return rc;
}

/*
 * proctrack_job_reg_pid - Register a new process in a SLURM job
 * RET 0 on success, -1 on error and set ut_error field for thread
 */
extern int proctrack_job_reg_pid(int *job_id_ptr, int *pid_ptr)
{
        uint32_t job_id = (uint32_t) fuword(job_id_ptr);
        pid_t pid = (pid_t) fuword(pid_ptr);
        int rc = 0;
	uid_t caller_uid = getuidx(ID_EFFECTIVE);
        
#if _LDEBUG
        sprintf(buf, "proctrack_job_reg_pid(%d)\n", job_id);
        _log(buf);
#endif
        if (proc_recs == NULL) {	/* not initialized */
#if _LDEBUG
                _log("process records not initialized");
#endif
                setuerror(ENOENT);
                return -1;
        }

	if (caller_uid != 0) {
#if _LDEBUG
                _log("only root may use proctrack_job_reg_pid");
#endif
		setuerror(EPERM);
		return -1;
	}
        
        simple_lock(&proc_lock);
        
        /* Insure this pid not a duplicate */
        if (_find_proc_recs(pid) != NULL) {
                _log("proctrack_job_reg duplicate");
                setuerror(EEXIST);
                rc = -1;
                goto done;
        }

        /* Create a new record with SLURM job id */
        if (_add_proc_recs(pid, job_id, 0) == NULL) {
                _log("proctrack_job_reg table full");
                setuerror(ENOMEM);
                rc = -1;
        }
        
 done:	simple_unlock(&proc_lock);
        return rc;
}

/*
 * proctrack_get_job_id - Map a pid to a SLURM job id
 * RET the associated SLURM job id or zero if not found
 */
extern uint32_t proctrack_get_job_id(int *pid_ptr)
{
        pid_t pid = (pid_t) fuword(pid_ptr);
        struct proc_rec *proc_ptr;
        uint32_t rc;
        
        if (proc_recs == NULL) {	/* not initialized */
                _log("process records not initialized");
                setuerror(ENOENT);
                return 0;
        }
        
        simple_lock(&proc_lock);
        
        proc_ptr = _find_proc_recs((pid_t) pid);
        if (proc_ptr == NULL) {
                setuerror(ENOENT);
                rc = 0;
        } else
                rc = proc_ptr->job_id;
        
        simple_unlock(&proc_lock);
        return rc;
}

/* Write all process tracking records */
extern void proctrack_dump_records(void)
{
#if _LDEBUG
        int i;
        
        _log("proctrack_dump_records\n");
        
        if (proc_recs == NULL) {	/* not initialized */
                _log("process records not initialized");
                return;
        }
        
        simple_lock(&proc_lock);
        
        sprintf(buf, "proctrack_dump_records: proc_max= %d\n", proc_max);
        _log(buf);
        
        for (i=0; i<proc_max; i++) {
                char buf[128];
                static struct proc_rec *proc_ptr;
                
                proc_ptr = proc_hash[i];
                
                while (proc_ptr) {
                        if ((proc_ptr->pid != 0) && (proc_ptr->job_id !=0)) {
                                sprintf(buf, "pid=%d jid=%u\n", 
                                        proc_ptr->pid, proc_ptr->job_id);
                                _log(buf);
                        }
                        proc_ptr = proc_ptr->next;
                }
        }
        
        simple_unlock(&proc_lock);
#endif
}


extern uint32_t proctrack_version(void)
{
	long version = atoi(PROCTRACK_VERSION);
	return (uint32_t) version;
}

/*
 * proctrack_get_pids returns an array of process ids for the given
 * job_id.  The array of pids is returned in the array pointed to
 * by the pid_array_ptr parameter.  The caller is responsible for 
 * allocating and freeing the memory for the array pointed to by
 * pid_array_ptr.  pid_array_len is an integer representing
 * the number of pids that can be held by the pid_array_ptr array.
 * 
 * Upon successful completion, returns the the number of pids found in the
 * specified job.  Note that this number MAY be larger than the 
 * number pointed to by pid_array_len, in which case caller knows that
 * the pid_array_ptr array is truncated.  The caller will want to allocate
 * a longer array and try again.
 *
 * On error returns -1 and sets errno.
 */
extern int proctrack_get_pids(uint32_t job_id, int pid_array_len,
			      int32_t *pid_array_ptr)
{
        struct proc_rec *proc_ptr;
	int found_pids = 0;
        int rc = 0;
        int i;
	int errnum;
	char *uaddr;
	int32_t pid;

#if _LDEBUG
        sprintf(buf, "proctrack_get_pids(%d), process is in %sbit mode\n",
		job_id, IS64U ? "64" : "32");
        _log(buf);
        sprintf(buf, "proctrack_get_pids sizeof(int32_t *) = %d\n",
		sizeof(int32_t *));
        _log(buf);
#endif

        simple_lock(&proc_lock);
        for (i = 0 ; i < proc_max ; i++) {
                proc_ptr = proc_hash[i];
                
                while (proc_ptr) {
                        if (proc_ptr->job_id == job_id) {
#if _LDEBUG
                                sprintf(buf, "proctrack_get_pids:"
					" found pid: %d\n", proc_ptr->pid);
                                _log(buf);
#endif
				if (found_pids < pid_array_len) {
					/* copy pid into user space array */
					uaddr = (char *)(pid_array_ptr+found_pids);
					pid = (int32_t)proc_ptr->pid;
#if _LDEBUG
					sprintf(buf, "pid_array_ptr = %lx,"
						" uaddr = %lx, pid addr = %lx\n",
						pid_array_ptr, uaddr, &pid);
					_log(buf);
#endif
					if (IS64U) {
						errnum = copyout64((char *)&pid,
								   uaddr,
								   sizeof(int32_t));
					} else {
						errnum = copyout((char *)&pid,
								 uaddr,
								 sizeof(int32_t));
					}
					if (errnum != 0) {
						setuerror(errnum);
						rc = -1;
						goto done;
					}
				} else {
					/* Whoops, user pid_array is not
					   long enough!  Don't copy any
					   more pids into user space, but
					   we'll keep counting the number
					   of pids in this job. */
				}
				found_pids++;
                        }
			proc_ptr = proc_ptr->next;
                }                                
        }
	rc = found_pids;

done:	simple_unlock(&proc_lock);
        return rc;
}


/*
 * proctrack_get_all_pids returns two arrays.  The first array lists
 * every process that proctrack is currently tracking, and the second
 * array contains the job ID for each process.  The array of pids is
 * returned in the array pointed to by the pid_array_ptr parameter, and the
 * array of job IDs is returned in the array pointed to by the
 * jid_array_ptr.  The caller is responsible for  allocating and freeing
 * the memory for both arrays.  array_len is an integer representing
 * the number of pids that can be held by the pid_array_ptr array.
 * 
 * Upon successful completion, returns the the number of pids and job IDs
 * written to the arrays.  Note that this number MAY be larger than the 
 * number pointed to by pid_array_len, in which case caller knows that
 * the arrays were not large enough to hold all of the pids and job IDs.
 * The caller will want to allocate a longer array and try again.
 *
 * On error returns -1 and sets errno.
 */
extern int proctrack_get_all_pids(int array_len,
				  int32_t *pid_array_ptr,
				  uint32_t *jid_array_ptr)
{
	struct proc_rec *proc_ptr;
	int count = 0;
	int rc = 0;
	int i;
	int err1, err2;
	char *uaddr;
	int32_t pid;
	uint32_t jid;

        if (proc_recs == NULL) {	/* not initialized */
#if _LDEBUG
                _log("process records not initialized");
#endif
                return 0;
        }
        
        simple_lock(&proc_lock);
        
#if _LDEBUG
        sprintf(buf, "proctrack_get_all_pids: proc_max= %d\n", proc_max);
        _log(buf);
#endif
        
        for (i = 0; i < proc_max; i++) {
                proc_ptr = proc_hash[i];
                
                while (proc_ptr) {
                        if ((proc_ptr->pid != 0) && (proc_ptr->job_id !=0)) {
				if (count >= array_len) {
					/* User arrays are not long enough. */
					count++;
					proc_ptr = proc_ptr->next;
					continue;
				}

				pid = (int32_t)proc_ptr->pid;
				jid = (uint32_t)proc_ptr->job_id;
#if _LDEBUG
				sprintf(buf, "proctrack_get_all_pids: pid=%d jid=%u\n",
					pid, jid);
				_log(buf);
#endif

				if (IS64U) {
					uaddr = (char *)(pid_array_ptr + count);
					err1 = copyout64((char *)&pid,
							 uaddr,
							 sizeof(int32_t));
					uaddr = (char *)(jid_array_ptr + count);
					err2 = copyout64((char *)&jid,
							 uaddr,
							 sizeof(uint32_t));
				} else {
					uaddr = (char *)(pid_array_ptr + count);
					err1 = copyout((char *)&pid,
						       uaddr,
						       sizeof(int32_t));
					uaddr = (char *)(jid_array_ptr + count);
					err2 = copyout((char *)&jid,
						       uaddr,
						       sizeof(uint32_t));
				}
				if (err1 != 0) {
					setuerror(err1);
					rc = -1;
					goto done;
				} else if (err2 != 0) {
					setuerror(err2);
					rc = -1;
					goto done;
				}
				count++;
                        }
                        proc_ptr = proc_ptr->next;
                }
        }
	rc = count;
done:
	simple_unlock(&proc_lock);
	return rc;
}

static void _prochr_handler(struct prochr * prochr, int reason, long id)
{
        if (id == 0) return; /* ignore all the initialize calls with 0 as pid */
        switch (reason) {
        case PROCHR_INITIALIZE:
#if _LDEBUG
                sprintf(buf, "_prochr_handler(%d, INITIALIZE, %d)\n", prochr, id);
                _log(buf);
#endif
                _prochr_create_handler(prochr, reason, id);
                
                break;
                
        case PROCHR_TERMINATE:
#if _LDEBUG
                sprintf(buf, "_prochr_handler(%d, TERMINATE, %d)\n", prochr, id);
                _log(buf);
#endif
                _prochr_term_handler(prochr, reason, id);
                break;

        case PROCHR_RESTART:
#if _LDEBUG
                sprintf(buf, "_prochr_handler(%d, RESTART, %d)\n", prochr, id);
                _log(buf);
#endif
                break;
                
        default:
#if _LDEBUG
                sprintf(buf, "_prochr_handler(%d, unrecognized!, %d)\n", prochr, id);
                _log(buf);
#endif
                break;
                
        }
        
        if (proc_recs== NULL) {	/* not initialized */
#if _LDEBUG
                _log("process records not initialized");
#endif
                return;
        }
}

/* handle process creation event */
static void _prochr_create_handler(struct prochr* prochr, int reason, long id)
{
        pid_t pid = (pid_t) id;
        struct proc_rec *parent_ptr, *child_ptr;
        pid_t ppid = getpid();
        
        simple_lock(&proc_lock);
        
        parent_ptr = _find_proc_recs(ppid);
#if _LDEBUG
        sprintf(buf, "id: %d ppid = %d parent_ptr = %d\n", id, ppid, parent_ptr);
        _log(buf);
#endif
        if (parent_ptr == NULL)
                goto done;		/* Non-slurm job, skip it */
        
        if (_find_proc_recs(pid) != NULL) {
#if _LDEBUG
                sprintf(buf, "_proch_create_handler duplicate entry: %d\n", pid);
                _log(buf);
#endif		
                goto done;
        }
        
        child_ptr = _add_proc_recs(pid, parent_ptr->job_id, parent_ptr->uid);

#if _LDEBUG        
        if (child_ptr == NULL)
                _log("_proc_create_handler add failure");
#endif
        
 done:	simple_unlock(&proc_lock);
        return;
}

/* handle process termination event */
static void _prochr_term_handler(struct prochr* prochr, int reason, long pid)
{
        simple_lock(&proc_lock);
        
        (void) _free_proc_recs((pid_t) pid);
        
        simple_unlock(&proc_lock);
}

/*
 * initialize process record table records including hash table and all linking
 * max_procs IN - maximum number of processes to accomodate
 * RET 0 on success, -1 on error and set ut_error field for thread
 */
static int  _init_proc_recs(int max_procs)
{
        int i;
        
#if _LDEBUG
        sprintf(buf, "_init_proc_recs: %d\n", max_procs);
        _log(buf);
#endif
        proc_recs = (struct proc_rec *) xmalloc(
                          (sizeof(struct proc_rec) * max_procs),
                          MALLOC_ALIGN, pinned_heap);
        if (proc_recs == NULL) {
                setuerror(ENOMEM);
                return -1;
        }
        bzero(proc_recs, max_procs *(sizeof(struct proc_rec)));
        proc_hash = (struct proc_rec **) xmalloc(
                          (sizeof(struct proc_rec *) * max_procs),
                          MALLOC_ALIGN, pinned_heap);
        if (proc_hash == NULL) {
                setuerror(ENOMEM);
                xmfree(proc_recs, pinned_heap);
                proc_recs = NULL;
                return -1;
        }
#if _LDEBUG        
        _log("_init_proc_recs: init data\n");
#endif        
        for (i=0; i<max_procs; i++) {
                proc_hash[i] = NULL;
                proc_recs[i].next = &proc_recs[i+1];
#if _LDEBUG
                proc_recs[i].magic = PROCTRACK_MAGIC;
#endif
        }
        proc_free = &proc_recs[0];
        proc_recs[max_procs-1].next = NULL;
        proc_max = max_procs;

#if _LDEBUG        
        _log("_init_proc_recs: returning\n");
#endif        
        return 0;
}

/*
 * deallocate process record table records, clear pointers
 * RET 0 on success, -1 on error and set ut_error field for thread
 */
static int  _fini_proc_recs(void)
{
        simple_lock(&proc_lock);
        if (proc_recs) {
                xmfree(proc_recs, pinned_heap);
                proc_recs = NULL;
        }
        
        if (proc_hash) {
                xmfree(proc_hash, pinned_heap);
                proc_hash = NULL;
        }
        
        proc_free = NULL;
        simple_unlock(&proc_lock);
        return 0;
}


/*
 * Add a process record for a given pid and job_id
 * RET pointer to process record added or NULL if no more space in table
 * NOTE: Caller is responsible for insuring unique pid value before issuing
 *       this call.
 *	 Caller is also responsible for insuring that the uid matches the
 *	 uid of other processes in the same job_id.
 *          
 */
static struct proc_rec *_add_proc_recs(pid_t pid, uint32_t job_id, uid_t uid)
{
        int hash;
        struct proc_rec *add_rec, *next_rec_ptr;
        
        sprintf(buf, "_add_proc_recs(%d, %d)\n", pid, job_id);
        _log(buf);

        if (job_id == 0) {
                _log("add_proc_recs: job_id NULL, can't add record\n");
                return NULL;
        }
        
        if ((proc_free == NULL) || (proc_max < 1)) {
                _log("proc_free empty, can't add record\n");
                return NULL;
        }

        hash = pid % proc_max;

#if _LDEBUG
        if (proc_free->magic != PROCTRACK_MAGIC) {
                _log("proc_free bad\n");
                return NULL;
        }
#endif
        /* get a record to use */
        add_rec = proc_free;
        proc_free = proc_free->next;
        add_rec->next = NULL;
        
        /* record this pid */
        add_rec->job_id = job_id;
        add_rec->pid = pid;
	add_rec->uid = uid;
        
        /* find where to put it */
        if (proc_hash[hash]) {
                next_rec_ptr = proc_hash[hash];
                while (next_rec_ptr->next != NULL) {
                        next_rec_ptr = next_rec_ptr->next;
                }
                next_rec_ptr->next = add_rec;
        } else {
                proc_hash[hash] = add_rec;
        }
        
        return add_rec;
}

/*
 * Search for a process record with the specified "job_id"
 * and return the uid of the process record.
 *
 * (Caller should be holding the lock)
 */
static uid_t _get_job_uid(uint32_t job_id)
{
        int i;
        
        for (i=0; i<proc_max; i++) {
                static struct proc_rec *proc_ptr;
                
                proc_ptr = proc_hash[i];
                
                while (proc_ptr) {
                        if ((proc_ptr->pid != 0)
			    && (proc_ptr->job_id == job_id)) {
				return proc_ptr->uid;
                        }
                        proc_ptr = proc_ptr->next;
                }
        }

	return (uid_t)-1;
}

/*
 * Find a process record for a given pid
 * RET pointer to process record or NULL if not found
 */
static struct proc_rec *_find_proc_recs(pid_t pid)
{
        int hash;
        struct proc_rec *next_rec_ptr;
        
        if ((proc_max < 1) || (pid <= 0))
                return NULL;
        
        hash = pid % proc_max;
        
        next_rec_ptr = proc_hash[hash];
        while (next_rec_ptr != NULL) {
#if _LDEBUG
                if (next_rec_ptr->magic != PROCTRACK_MAGIC) {
                        _log("proc_rec bad");
                        return NULL;
                }
#endif
                if (next_rec_ptr->pid == pid)
                        return next_rec_ptr;
                next_rec_ptr = next_rec_ptr->next;
        }
        
        return NULL;
}

/*
 * Move the process record on the free list
 * RET 0 on success, -1 if not found or other error
FIXME - this routine needs to use the semaphore to make changes.
*/
static int _free_proc_recs(pid_t pid)
{
        int hash;
        struct proc_rec *next_rec_ptr;
        struct proc_rec **next_rec_loc;
        
        sprintf(buf, "_free_proc_recs(%d)\n", pid);
        _log(buf);
        if (proc_max < 1)
                return -1;	/* no records */
        hash = pid % proc_max;

        next_rec_loc = &proc_hash[hash];
        next_rec_ptr = proc_hash[hash];
        while (next_rec_ptr != NULL) {
#if _LDEBUG
                if (next_rec_ptr->magic != PROCTRACK_MAGIC) {
                        _log("proc_rec bad");
                        return NULL;
                }
#endif
                if (next_rec_ptr->pid == pid) {
                        _log("_free_proc_recs: found the record - deleting\n");
                        /* re-link the entry */
                        *next_rec_loc = next_rec_ptr->next;
                        next_rec_ptr->next = proc_free;
                        proc_free = next_rec_ptr;
                        return(-1);
                }
                next_rec_loc = &next_rec_ptr->next;
                next_rec_ptr = next_rec_ptr->next;
        }

        return -1;	/* not found */
}


/*
 * OBSOLETE!  Use proctrack_job_reg_self() instead.
 *
 * proctrack_job_reg - Register the creation of a SLURM job and this 
 * process, based upon getpid()
 * RET 0 on success, -1 on error and set ut_error field for thread
 */
extern int proctrack_job_reg(int *job_id_ptr)
{
	return proctrack_job_reg_self(job_id_ptr);
}


#if _LDEBUG
/***************************************************
 * Routines for logging debug information:         *
 * open_log - Opens a log file                     *
 * write_log - Output a string to a log file       *
 * close_log - Close a log file                    *
 ***************************************************/
int open_log (char *path, struct file **fpp)
{
        int rc;
        rc = fp_open(path, O_CREAT | O_APPEND | O_WRONLY,
                     S_IRUSR | S_IWUSR, 0, SYS_ADSPACE, fpp);
        return(rc);
}

int write_log(struct file *fpp, char *buf, int *bytes_written)
{
        int rc;
        rc = fp_write(fpp, buf, strlen(buf), 0, SYS_ADSPACE, bytes_written);
        return(rc);
}

int close_log(struct file *fpp)
{
        int rc;
        rc = fp_close(fpp);
        return(rc);
}
#endif
