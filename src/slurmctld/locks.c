/*****************************************************************************\
 *  locks.c - semaphore functions for slurmctld
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, Randy Sanchez <rsancez@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_rwlock_t slurmctld_locks[ENTITY_COUNT];

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

/* lock_slurmctld - Issue the required lock requests in a well defined order */
extern void lock_slurmctld(slurmctld_lock_t lock_levels)
{
	static bool init_run = false;
	xassert(_store_locks(lock_levels));

	if (!init_run) {
		init_run = true;
		for (int i = 0; i < ENTITY_COUNT; i++)
			slurm_rwlock_init(&slurmctld_locks[i]);
	}

	if (lock_levels.conf == READ_LOCK)
		slurm_rwlock_rdlock(&slurmctld_locks[CONF_LOCK]);
	else if (lock_levels.conf == WRITE_LOCK)
		slurm_rwlock_wrlock(&slurmctld_locks[CONF_LOCK]);

	if (lock_levels.job == READ_LOCK)
		slurm_rwlock_rdlock(&slurmctld_locks[JOB_LOCK]);
	else if (lock_levels.job == WRITE_LOCK)
		slurm_rwlock_wrlock(&slurmctld_locks[JOB_LOCK]);

	if (lock_levels.node == READ_LOCK)
		slurm_rwlock_rdlock(&slurmctld_locks[NODE_LOCK]);
	else if (lock_levels.node == WRITE_LOCK)
		slurm_rwlock_wrlock(&slurmctld_locks[NODE_LOCK]);

	if (lock_levels.part == READ_LOCK)
		slurm_rwlock_rdlock(&slurmctld_locks[PART_LOCK]);
	else if (lock_levels.part == WRITE_LOCK)
		slurm_rwlock_wrlock(&slurmctld_locks[PART_LOCK]);

	if (lock_levels.fed == READ_LOCK)
		slurm_rwlock_rdlock(&slurmctld_locks[FED_LOCK]);
	else if (lock_levels.fed == WRITE_LOCK)
		slurm_rwlock_wrlock(&slurmctld_locks[FED_LOCK]);
}

/* unlock_slurmctld - Issue the required unlock requests in a well
 *	defined order */
extern void unlock_slurmctld(slurmctld_lock_t lock_levels)
{
	xassert(_clear_locks(lock_levels));

	if (lock_levels.fed)
		slurm_rwlock_unlock(&slurmctld_locks[FED_LOCK]);

	if (lock_levels.part)
		slurm_rwlock_unlock(&slurmctld_locks[PART_LOCK]);

	if (lock_levels.node)
		slurm_rwlock_unlock(&slurmctld_locks[NODE_LOCK]);

	if (lock_levels.job)
		slurm_rwlock_unlock(&slurmctld_locks[JOB_LOCK]);

	if (lock_levels.conf)
		slurm_rwlock_unlock(&slurmctld_locks[CONF_LOCK]);
}

/*
 * _report_lock_set - report whether the read or write lock is set
 */
static void _report_lock_set(char **str, lock_datatype_t datatype)
{
	/* the try functions return zero on success */
	if (slurm_rwlock_tryrdlock(&slurmctld_locks[datatype])) {
		*str = "W";
	} else {
		slurm_rwlock_unlock(&slurmctld_locks[datatype]);
		if (slurm_rwlock_trywrlock(&slurmctld_locks[datatype]))
			*str = "R";
		else
			slurm_rwlock_unlock(&slurmctld_locks[datatype]);
	}
}

/*
 * report_locks_set - report any slurmctld locks left set
 * RET count of locks currently set
 */
int report_locks_set(void)
{
	char *conf = "", *job = "", *node = "", *part = "", *fed = "";
	int lock_count;

	_report_lock_set(&conf, CONF_LOCK);
	_report_lock_set(&job, JOB_LOCK);
	_report_lock_set(&node, NODE_LOCK);
	_report_lock_set(&part, PART_LOCK);
	_report_lock_set(&fed, FED_LOCK);

	lock_count = strlen(conf) + strlen(job) + strlen(node)
		     + strlen(part) + strlen(fed);

	if (lock_count > 0) {
		error("Locks left set "
		      "config:%s, job:%s, node:%s, partition:%s, federation:%s",
		      conf, job, node, part, fed);
	}
	return lock_count;
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
