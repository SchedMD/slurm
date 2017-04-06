/*****************************************************************************\
 *  locks.c - semaphore functions for slurmctld
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, Randy Sanchez <rsancez@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

static pthread_mutex_t locks_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t locks_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

static slurmctld_lock_flags_t slurmctld_locks;

static void _wr_rdlock(lock_datatype_t datatype);
static void _wr_rdunlock(lock_datatype_t datatype);
static void _wr_wrlock(lock_datatype_t datatype);
static void _wr_wrunlock(lock_datatype_t datatype);

#ifndef NDEBUG
/*
 * Used to protect against double-locking within a single thread. Calling
 * lock_slurmctld() while already holding locks will lead to deadlock;
 * this will force such instances to abort() in development builds.
 */
/*
 * FIXME: __thread is non-standard, and may cause build failures on unusual
 * systems. Only used within development builds to mitigate possible problems
 * with production builds.
 */
static __thread bool slurmctld_locked = false;

/*
 * Used to detect any location where the acquired locks differ from the
 * release locks.
 */

static __thread slurmctld_lock_t thread_locks;

static bool _store_locks(slurmctld_lock_t lock_levels)
{
	if (slurmctld_locked)
		return false;
	slurmctld_locked = true;

	memcpy((void *) &thread_locks, (void *) &lock_levels,
	       sizeof(slurmctld_lock_t));

	return true;
}

static bool _clear_locks(slurmctld_lock_t lock_levels)
{
	if (!slurmctld_locked)
		return false;
	slurmctld_locked = false;

	if (memcmp((void *) &thread_locks, (void *) &lock_levels,
		       sizeof(slurmctld_lock_t)))
		return false;

	memset((void *) &thread_locks, 0, sizeof(slurmctld_lock_t));

	return true;
}

extern bool verify_lock(lock_datatype_t datatype, lock_level_t level)
{
	return (((lock_level_t *) &thread_locks)[datatype] >= level);
}
#endif

/* init_locks - create locks used for slurmctld data structure access
 *	control */
void init_locks(void)
{
	/* just clear all semaphores */
	memset((void *) &slurmctld_locks, 0, sizeof(slurmctld_locks));
}

/* lock_slurmctld - Issue the required lock requests in a well defined order */
extern void lock_slurmctld(slurmctld_lock_t lock_levels)
{
	xassert(_store_locks(lock_levels));

	if (lock_levels.config == READ_LOCK)
		_wr_rdlock(CONFIG_LOCK);
	else if (lock_levels.config == WRITE_LOCK)
		_wr_wrlock(CONFIG_LOCK);

	if (lock_levels.job == READ_LOCK)
		_wr_rdlock(JOB_LOCK);
	else if (lock_levels.job == WRITE_LOCK)
		_wr_wrlock(JOB_LOCK);

	if (lock_levels.node == READ_LOCK)
		_wr_rdlock(NODE_LOCK);
	else if (lock_levels.node == WRITE_LOCK)
		_wr_wrlock(NODE_LOCK);

	if (lock_levels.partition == READ_LOCK)
		_wr_rdlock(PART_LOCK);
	else if (lock_levels.partition == WRITE_LOCK)
		_wr_wrlock(PART_LOCK);

	if (lock_levels.federation == READ_LOCK)
		_wr_rdlock(FED_LOCK);
	else if (lock_levels.federation == WRITE_LOCK)
		_wr_wrlock(FED_LOCK);
}

/* unlock_slurmctld - Issue the required unlock requests in a well
 *	defined order */
extern void unlock_slurmctld(slurmctld_lock_t lock_levels)
{
	xassert(_clear_locks(lock_levels));

	if (lock_levels.federation == READ_LOCK)
		_wr_rdunlock(FED_LOCK);
	else if (lock_levels.federation == WRITE_LOCK)
		_wr_wrunlock(FED_LOCK);

	if (lock_levels.partition == READ_LOCK)
		_wr_rdunlock(PART_LOCK);
	else if (lock_levels.partition == WRITE_LOCK)
		_wr_wrunlock(PART_LOCK);

	if (lock_levels.node == READ_LOCK)
		_wr_rdunlock(NODE_LOCK);
	else if (lock_levels.node == WRITE_LOCK)
		_wr_wrunlock(NODE_LOCK);

	if (lock_levels.job == READ_LOCK)
		_wr_rdunlock(JOB_LOCK);
	else if (lock_levels.job == WRITE_LOCK)
		_wr_wrunlock(JOB_LOCK);

	if (lock_levels.config == READ_LOCK)
		_wr_rdunlock(CONFIG_LOCK);
	else if (lock_levels.config == WRITE_LOCK)
		_wr_wrunlock(CONFIG_LOCK);
}

/* _wr_rdlock - Issue a read lock on the specified data type
 *	Wait until there are no write locks AND
 *	no pending write locks (write_wait_lock == 0)
 *
 *	NOTE: Always favoring write locks could result in starvation for
 *	read locks. */
static void _wr_rdlock(lock_datatype_t datatype)
{
	slurm_mutex_lock(&locks_mutex);
	while (1) {
		if ((slurmctld_locks.entity[write_lock(datatype)] == 0) &&
		    (slurmctld_locks.entity[write_wait_lock(datatype)] == 0)) {
			slurmctld_locks.entity[read_lock(datatype)]++;
			slurmctld_locks.entity[write_cnt_lock(datatype)] = 0;
			break;
		} else {	/* wait for state change and retry */
			slurm_cond_wait(&locks_cond, &locks_mutex);
		}
	}
	slurm_mutex_unlock(&locks_mutex);
}

/* _wr_rdunlock - Issue a read unlock on the specified data type */
static void _wr_rdunlock(lock_datatype_t datatype)
{
	slurm_mutex_lock(&locks_mutex);
	slurmctld_locks.entity[read_lock(datatype)]--;
	xassert(slurmctld_locks.entity[read_lock(datatype)] >= 0);
	slurm_cond_broadcast(&locks_cond);
	slurm_mutex_unlock(&locks_mutex);
}

/* _wr_wrlock - Issue a write lock on the specified data type */
static void _wr_wrlock(lock_datatype_t datatype)
{
	slurm_mutex_lock(&locks_mutex);
	slurmctld_locks.entity[write_wait_lock(datatype)]++;

	while (1) {
		if ((slurmctld_locks.entity[read_lock(datatype)] == 0) &&
		    (slurmctld_locks.entity[write_lock(datatype)] == 0)) {
			slurmctld_locks.entity[write_lock(datatype)]++;
			slurmctld_locks.entity[write_wait_lock(datatype)]--;
			slurmctld_locks.entity[write_cnt_lock(datatype)]++;
			break;
		} else {	/* wait for state change and retry */
			slurm_cond_wait(&locks_cond, &locks_mutex);
		}
	}
	slurm_mutex_unlock(&locks_mutex);
}

/* _wr_wrunlock - Issue a write unlock on the specified data type */
static void _wr_wrunlock(lock_datatype_t datatype)
{
	slurm_mutex_lock(&locks_mutex);
	slurmctld_locks.entity[write_lock(datatype)]--;
	xassert(slurmctld_locks.entity[write_lock(datatype)] >= 0);
	slurm_cond_broadcast(&locks_cond);
	slurm_mutex_unlock(&locks_mutex);
}

/* get_lock_values - Get the current value of all locks
 * OUT lock_flags - a copy of the current lock values */
void get_lock_values(slurmctld_lock_flags_t * lock_flags)
{
	xassert(lock_flags);
	memcpy((void *) lock_flags, (void *) &slurmctld_locks,
	       sizeof(slurmctld_locks));
}

/* un/lock semaphore used for saving state of slurmctld */
extern void lock_state_files(void)
{
	slurm_mutex_lock(&state_mutex);
}
extern void unlock_state_files(void)
{
	slurm_mutex_unlock(&state_mutex);
}
