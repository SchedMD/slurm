/*****************************************************************************\
 *  locks.h - definitions for semaphore functions for slurmctld (locks.c)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
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

/*****************************************************************************\
 * Read/write locks are implemented by the routines in this directory by using
 * a set of three (3) UNIX semaphores to lock a resource.
 *
 * The set of three (3) semaphores represent a reader semaphore,
 * a writer semaphore and a writers waiting semaphore.
 *
 * The reader semaphore indicates the number of readers that currently have a
 * read lock on the resource.
 * The writers semaphore indicates that a writer has the resource locked.
 * The writers waiting semaphore indicates the number of writers waiting to
 * lock the resource.
 *
 * Readers cannot lock the resource until there are no writers waiting for the
 * resource and the resource is not locked by a writer.
 *
 * Writers cannot lock the resource if the resource is locked by other writers
 * or if any readers have the resource locked.
 *
 * Writers will have priority in locking the resource over readers because
 * of the writers waiting semaphore.  The writers waiting semaphore is incremented
 * by a writer that is waiting to lock the resource.  A reader cannot lock
 * the resource until there are no writers waiting to lock the resource and
 * the resource is not locked by a writer.
 *
 * So, if the resource is locked by an unspecified number of readers,
 * and a writer trys to lock the resource, then the writer will be blocked
 * until all of the previous readers have unlocked the resource.  But,
 * just before the writer checked to see if there were any readers locking
 * the resource, the writer incremented the writers waiting semaphore,
 * indicating that there is now a writer waiting to lock the resource.
 * In the mean time, if an unspecified number of readers try to lock the
 * resource after a writer (or writers) has tried to lock the resource,
 * those readers will be blocked until all writers have obtained the lock on
 * the resource, used the resource and unlocked the resource.  The subsequent
 * unspecified number of readers are blocked because they are waiting for the
 * number of writers waiting semaphore to become 0, meaning that there are no
 * writers waiting to lock the resource.
 *
 * use lock_slurmctld() and unlock_slurmctld() to get the ordering so as to
 * prevent deadlock. The arguments indicate the lock type required for
 * each entity (job, node, etc.) in a well defined order.
 * For example: no lock on the config data structure, read lock on the job
 * and node data structures, and write lock on the partition data structure
 * would look like this: "{ NO_LOCK, READ_LOCK, READ_LOCK, WRITE_LOCK }"
 * or "{ .job = READ_LOCK, .node = READ_LOCK, .part = WRITE_LOCK }"
 *
 * NOTE: When using lock_slurmctld() and assoc_mgr_lock(), always call
 * lock_slurmctld() before calling assoc_mgr_lock() and then call
 * assoc_mgr_unlock() before calling unlock_slurmctld().
\*****************************************************************************/

#ifndef _SLURMCTLD_LOCKS_H
#define _SLURMCTLD_LOCKS_H

#include <stdbool.h>

/* levels of locking required for each data structure */
typedef enum {
	NO_LOCK,
	READ_LOCK,
	WRITE_LOCK
}	lock_level_t;

/* slurmctld specific data structures to lock via APIs */
typedef struct {
	lock_level_t conf;
	lock_level_t job;
	lock_level_t node;
	lock_level_t part;
	lock_level_t fed;
}	slurmctld_lock_t;

typedef enum {
	CONF_LOCK,
	JOB_LOCK,
	NODE_LOCK,
	PART_LOCK,
	FED_LOCK,
}	lock_datatype_t;

#ifndef NDEBUG
extern bool verify_lock(lock_datatype_t datatype, lock_level_t level);
#endif

/* lock_slurmctld - Issue the required lock requests in a well defined order */
extern void lock_slurmctld (slurmctld_lock_t lock_levels);

/* unlock_slurmctld - Issue the required unlock requests in a well
 *	defined order */
extern void unlock_slurmctld (slurmctld_lock_t lock_levels);

extern int report_locks_set(void);

/* un/lock semaphore used for saving state of slurmctld */
extern void lock_state_files ( void );
extern void unlock_state_files ( void );

#endif
