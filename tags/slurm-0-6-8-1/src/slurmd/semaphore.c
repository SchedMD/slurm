/*****************************************************************************\
 * semaphore.c - POSIX semaphore implementation via SysV semaphores
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
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

/*
 *  semaphore.c
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  Posix Semaphores implementation using System V Semaphores
 *    (cf. Stevens' Unix Network Programming, v2, 2e, Section 10.16)
 *
 *  Id: semaphore.c,v 1.1.1.1 2000/10/20 21:56:06 dun Exp 
 */

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if (!HAVE_MALLOC)
#  include "src/common/malloc.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>

#include "src/slurmd/semaphore.h"
#include "src/common/log.h"


#define MAX_TRIES 3


sem_t * sem_open(const char *name, int oflag, ...)
{
    va_list ap;
    mode_t mode;
    unsigned int value;
    int i, fd, errno_bak;
    key_t key;
    int semflag, semid;
    union semun semarg;
    struct sembuf semval;
    struct semid_ds seminfo;
    sem_t *sem;

    if (!name) {
        return(SEM_FAILED);
    }

    semid = -1;

    /* Create a new semaphore.
     */
    if (oflag & O_CREAT) {
        va_start(ap, oflag);
        mode = va_arg(ap, mode_t);
        value = va_arg(ap, unsigned int);
        va_end(ap);

        /* Create ancillary file and map pathname into SysV IPC key.
         */
        if ((fd = open(name, oflag, mode)) == -1) {
            /*
             * If the O_EXCL flag is specified and we return before the sem
             *   is actually created in the following semget(), we create a
             *   race-condition.
             * This can present itself when two processes try to simultaneously
             *   open the same sem.  Suppose the first process succeeds in
             *   opening the file.  The second process will fail in its call to
             *   open() because of the O_EXCL flag and sem_open() will return
             *   SEM_FAILED.  Now suppose a subsequent sem_open() call is made
             *   w/o the O_EXCL flag to open the (presumably existing) sem.
             *   If the first process has not returned from semget() by now,
             *   this sem_open() will return SEM_FAILED with errno=ENOENT since
             *   the sem does not yet exist!
             */
            if ((errno == EEXIST) && (oflag & O_EXCL)) {
                if ((key = ftok(name, 1)) == -1) {
                    return(SEM_FAILED);
                }
                for (i=0; i<MAX_TRIES; i++) {
                    if (((semget(key, 0, 0)) != -1) || (errno != ENOENT)) {
                        break;
                    }
                    sleep(1);
                }
                errno = EEXIST;		/* don't let semget() change errno */
            }
            return(SEM_FAILED);
        }
        close(fd);
        if ((key = ftok(name, 1)) == -1) {
            return(SEM_FAILED);
        }

        /* Convert Posix sem flag to SysV sem flag.
         */
        semflag = IPC_CREAT | (mode & 0777);
        if (oflag & O_EXCL) {
            semflag |= IPC_EXCL;
        }

        /* Create SysV semaphore set with one member.
         * Note that semget() sets sem_otime to zero during sem creation.
         */
        if ((semid = semget(key, 1, semflag | IPC_EXCL)) >= 0) {
            /*
             * With IPC_EXCL, we're the first to create sem, so init to 0.
             */
            semarg.val = 0;
            if (semctl(semid, 0, SETVAL, semarg) == -1) {
                goto err;
            }
            /* SysV sems are normally stored as ushorts, so enforce max val.
             */
            if (value > SEMVMX) {
                errno = EINVAL;
                goto err;
            }
            /* Now increment sem by 'value' w/ semop() to set sem_otime nonzero.
             */
            semval.sem_num = 0;
            semval.sem_op = value;
            semval.sem_flg = 0;
            if (semop(semid, &semval, 1) == -1) {
                goto err;
            }
            goto end;
        }
        /* If the sem already exists and the caller does not specify O_EXCL,
         *   this is NOT an error.  Instead, fall-thru to open existing sem.
         */
        else if ((errno != EEXIST) || ((semflag & IPC_EXCL) != 0)) {
            goto err;
        }
    }


    /* Open (presumably) existing semaphore.  Either O_CREAT was not specified,
     *   or O_CREAT was specified w/o O_EXCL and the semaphore already exists.
     */
    if ((key = ftok(name, 1)) == -1) {
        goto err;
    }
    if ((semid = semget(key, 0, 0)) == -1) {
        goto err;
    }

    /* If sem_otime is 0, sem has not yet been initialized by its creator.
     *   Spin up to MAX_TRIES before giving up.
     *
     * DANGER, WILL ROBINSON!
     *   Unfortunatley, semop() on a BSD system does not appear to update the
     *   sem_otime member for some sick and twisted reason.  So we'll sleep,
     *   cross our fingers, and hope for the best.
     */
#ifdef HAVE_BROKEN_SEM_OTIME
    sleep(1);
    goto end;
#endif /* HAVE_BROKEN_SEM_OTIME */

    semarg.buf = &seminfo;
    seminfo.sem_otime = 0;
    for (i=0; i<MAX_TRIES; i++) {
        if (semctl(semid, 0, IPC_STAT, semarg) == -1) {
            goto err;
        }
        if (seminfo.sem_otime != 0) {
            goto end;
        }
        sleep(1);
    }
    errno = ETIMEDOUT;
    /* fall-thru to 'err' */
    

/* Clean up failed semaphore before returning.
 */
err:
    errno_bak = errno;			/* don't let semctl() change errno */
    if (semid != -1) {
        semctl(semid, 0, IPC_RMID);
    }
    errno = errno_bak;
    return(SEM_FAILED);

/* SysV sem creation was successful, so create Posix sem wrapper.
 */
end:
    if ((sem = malloc(sizeof(sem_t))) == NULL) {
        goto err;
    }
    sem->id = semid;
    return(sem);
}


int sem_close(sem_t *sem)
{
    if (sem->id < 0) {
        errno = EINVAL;
        return(-1);
    }
    sem->id = -1;
    free(sem);
    return(0);
}


int sem_unlink(const char *name)
{
    key_t key;
    int semid;

    if (!name) {
        return(-1);
    }
    if ((key = ftok(name, 1)) == -1) {
        return(-1);
    }
    if ((semid = semget(key, 0, 0)) == -1) {
        goto done;
    }
    if (semctl(semid, 0, IPC_RMID) == -1) {
	goto done;
    }

   done:
    if (unlink(name) == -1) {
        return(-1);
    }
    return(0);
}


int sem_wait(sem_t *sem)
{
    struct sembuf op;

    if (sem->id < 0) {
        errno = EINVAL;
        return(-1);
    }
    op.sem_num = 0;
    op.sem_op = -1;
    op.sem_flg = 0;
    if (semop(sem->id, &op, 1) == -1) {
        return(-1);
    }
    return(0);
}


int sem_trywait(sem_t *sem)
{
    struct sembuf op;

    if (sem->id < 0) {
        errno = EINVAL;
        return(-1);
    }
    op.sem_num = 0;
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT;
    if (semop(sem->id, &op, 1) == -1) {
        return(-1);
    }
    return(0);
}


int sem_post(sem_t *sem)
{
    struct sembuf op;

    if (sem->id < 0) {
        errno = EINVAL;
        return(-1);
    }
    op.sem_num = 0;
    op.sem_op = 1;
    op.sem_flg = 0;
    if (semop(sem->id, &op, 1) == -1) {
        return(-1);
    }
    return(0);
}


int sem_getvalue(sem_t *sem, int *valp)
{
    int val;

    if (sem->id < 0) {
        errno = EINVAL;
        return(-1);
    }
    if ((val = semctl(sem->id, 0, GETVAL)) == -1) {
        return(-1);
    }
    *valp = val;
    return(0);
}
