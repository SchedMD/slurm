/*****************************************************************************\
 * src/slurmd/semaphore.h - POSIX semaphore implementation via SysV semaphores
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <Dunlap@llnl.gov>.
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
 *  semaphore.h
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  Posix Semaphores implementation using System V Semaphores
 *    (cf. Stevens' Unix Network Programming, v2, 2e, Section 10.16)
 *
 *  Id: semaphore.h,v 1.1.1.1 2000/10/02 20:56:53 dun Exp 
 *
 */


#ifndef DUN_SEMAPHORE_H
#define DUN_SEMAPHORE_H

#include "config.h"

#ifdef HAVE_POSIX_SEMS
#include <semaphore.h>
#else


typedef struct {
    int id;				/* SysV semaphore ID */
} sem_t;

#ifdef SEM_FAILED
#undef SEM_FAILED
#endif /* SEM_FAILED */
#define SEM_FAILED ((sem_t *)(-1))	/* avoid compiler warnings */

#ifndef SEMVMX
#define SEMVMX 32767			/* historical SysV sem max value */
#endif /* !SEMVMX */

/* Default perms for new SysV semaphores.
 */
#define SYSV_SEM_DEF_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

#ifndef HAVE_SEMUN_UNION
union semun {
    int val;				/* value for SETVAL */
    struct semid_ds *buf;		/* buffer for IPC_SET and IPC_STAT */
    unsigned short int *array;		/* array for GETALL and SETALL */
};
#endif /* !HAVE_SEMUN_UNION */


sem_t * sem_open(const char *name, int oflag, ...);

int sem_close(sem_t *sem);

int sem_unlink(const char *name);

int sem_wait(sem_t *sem);

int sem_trywait(sem_t *sem);

int sem_post(sem_t *sem);

int sem_getvalue(sem_t *sem, int *valp);


#endif /* !HAVE_POSIX_SEMS */

#endif /* !DUN_SEMAPHORE_H */
