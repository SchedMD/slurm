/*****************************************************************************\
 * locks.c - semaphore functions for slurmd
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, Randy Sanchez <rsancez@llnl.gov>
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
#  include <config.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>

#include <src/slurmd/locks.h>
#include <src/common/log.h>

pthread_mutex_t locks_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t locks_cond = PTHREAD_COND_INITIALIZER;
slurmd_lock_flags_t slurmd_locks;

void wr_rdlock (lock_datatype_t datatype);
void wr_rdunlock (lock_datatype_t datatype);
void wr_wrlock (lock_datatype_t datatype);
void wr_wrunlock (lock_datatype_t datatype);

/* init_locks - create locks used for slurmd data structure access control */
void
init_locks ( void )
{
	/* just clear all semaphores */
	memset ((void *)&slurmd_locks, 0, sizeof (slurmd_locks) );
}

/* lock_slurmd - Issue the required lock requests in a well defined order
 * Returns 0 on success, -1 on failure */
void 
lock_slurmd (slurmd_lock_t lock_levels)
{
	if (lock_levels.jobs == READ_LOCK)
		wr_rdlock (JOB_LIST_LOCK);
	else if (lock_levels.jobs == WRITE_LOCK)
		wr_wrlock (JOB_LIST_LOCK);

	if (lock_levels.tasks == READ_LOCK)
		wr_rdlock (TASK_LIST_LOCK);
	else if (lock_levels.tasks == WRITE_LOCK)
		wr_wrlock (TASK_LIST_LOCK);

	if (lock_levels.credentials == READ_LOCK)
		wr_rdlock (CREDENTIAL_STATE_LOCK);
	else if (lock_levels.credentials == WRITE_LOCK)
		wr_wrlock (CREDENTIAL_STATE_LOCK);
}

/* unlock_slurmd - Issue the required unlock requests in a well defined order */
void 
unlock_slurmd (slurmd_lock_t lock_levels)
{
	if (lock_levels.credentials == READ_LOCK)
		wr_rdunlock (CREDENTIAL_STATE_LOCK);
	else if (lock_levels.credentials == WRITE_LOCK)
		wr_wrunlock (CREDENTIAL_STATE_LOCK);

	if (lock_levels.tasks == READ_LOCK)
		wr_rdunlock (TASK_LIST_LOCK);
	else if (lock_levels.tasks == WRITE_LOCK)
		wr_wrunlock (TASK_LIST_LOCK);

	if (lock_levels.jobs == READ_LOCK)
		wr_rdunlock (JOB_LIST_LOCK);
	else if (lock_levels.jobs == WRITE_LOCK)
		wr_wrunlock (JOB_LIST_LOCK);
}

/* wr_rdlock - Issue a read lock on the specified data type */
void 
wr_rdlock (lock_datatype_t datatype)
{
	pthread_mutex_lock (&locks_mutex);
	while (1) {
		if ((slurmd_locks.entity [write_wait_lock (datatype)] == 0) &&
		    (slurmd_locks.entity [write_lock (datatype)] == 0)) {
			slurmd_locks.entity [read_lock (datatype)]++;
			break;
		} 
		else {	/* wait for state change and retry */
			pthread_cond_wait (&locks_cond, &locks_mutex);
		}
	}
	pthread_mutex_unlock (&locks_mutex);
}

/* wr_rdunlock - Issue a read unlock on the specified data type */
void
wr_rdunlock (lock_datatype_t datatype)
{
	pthread_mutex_lock (&locks_mutex);
	slurmd_locks.entity [read_lock (datatype)]--;
	pthread_mutex_unlock (&locks_mutex);
	pthread_cond_broadcast (&locks_cond);
}

/* wr_wrlock - Issue a write lock on the specified data type */
void
wr_wrlock (lock_datatype_t datatype)
{
	pthread_mutex_lock (&locks_mutex);
	slurmd_locks.entity [write_wait_lock (datatype)]++;

	while (1) {
		if ((slurmd_locks.entity [read_lock (datatype)] == 0) &&
		    (slurmd_locks.entity [write_lock (datatype)] == 0)) {
			slurmd_locks.entity [write_lock (datatype)]++;
			slurmd_locks.entity [write_wait_lock (datatype)]--;
			break;
		} 
		else {	/* wait for state change and retry */
			pthread_cond_wait (&locks_cond, &locks_mutex);
		}
	}
	pthread_mutex_unlock (&locks_mutex);
}

/* wr_wrunlock - Issue a write unlock on the specified data type */
void
wr_wrunlock (lock_datatype_t datatype)
{
	pthread_mutex_lock (&locks_mutex);
	slurmd_locks.entity [write_lock (datatype)]--;
	pthread_mutex_unlock (&locks_mutex);
	pthread_cond_broadcast (&locks_cond);
}

/* get_lock_values - Get the current value of all locks */
void
get_lock_values (slurmd_lock_flags_t *lock_flags)
{
	if (lock_flags == NULL)
		fatal ("get_lock_values passed null pointer");

	memcpy ((void *)lock_flags, (void *) &slurmd_locks, sizeof (slurmd_locks) );
}
