/*****************************************************************************\
 * locks.c - semaphore functions for slurmctld
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
/* NOTE: These functions closely resemble the semget/semop/semctl functions, 
 *	but are written using the pthread_mutex_ functions
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>

#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
/* union semun is defined by including <sys/sem.h> */
#else
/* according to X/OPEN we have to define it ourselves */
union semun {
	int val;                    /* value for SETVAL */
	struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
	unsigned short int *array;  /* array for GETALL, SETALL */
	struct seminfo *__buf;      /* buffer for IPC_INFO */
};
#endif

/* available data structure locks 
 * we actually use three semaphores for each, see macros below
 *	(lock_datatype_t * 3 + 0) = read_lock
 *	(lock_datatype_t * 3 + 1) = write_lock
 *	(lock_datatype_t * 3 + 2) = write_wait_lock
 */
typedef enum {
	CONFIG_LOCK,
	JOB_LOCK, 
	NODE_LOCK, 
	PART_LOCK,
	COUNT_OF_LOCKS
}	lock_datatype_t;

#define read_lock(lock_type)		(lock_type * 3 + 0) 
#define write_lock(lock_type)		(lock_type * 3 + 1) 
#define write_wait_lock(lock_type)	(lock_type * 3 + 2) 

int sem_id = -1;	/* semaphore ID */

void wr_rdlock (lock_datatype_t datatype);
void wr_rdunlock (lock_datatype_t datatype);
void wr_wrlock (lock_datatype_t datatype);
void wr_wrunlock (lock_datatype_t datatype);

/* init_locks - create locks used for slurmctld data structure access control */
void
init_locks ( )
{
	if (sem_id >= 0)
		return;

	sem_id = semget ( IPC_PRIVATE, (COUNT_OF_LOCKS * 3), IPC_CREAT | 0600 );

	if (sem_id < 0)
		fatal ("semget errno %d", errno);
}

/* lock_slurmctld - Issue the required lock requests in a well defined order
 * Returns 0 on success, -1 on failure */
void 
lock_slurmctld (slurmctld_lock_t lock_levels)
{
	if (sem_id < 0) {
		error ("init_locks was not called before lock use");
		init_locks ();
	}

	if (lock_levels.config == READ_LOCK)
		wr_rdlock (CONFIG_LOCK);
	else if (lock_levels.config == WRITE_LOCK)
		wr_wrlock (CONFIG_LOCK);

	if (lock_levels.job == READ_LOCK)
		wr_rdlock (JOB_LOCK);
	else if (lock_levels.job == WRITE_LOCK)
		wr_wrlock (JOB_LOCK);

	if (lock_levels.node == READ_LOCK)
		wr_rdlock (NODE_LOCK);
	else if (lock_levels.node == WRITE_LOCK)
		wr_wrlock (NODE_LOCK);

	if (lock_levels.partition == READ_LOCK)
		wr_rdlock (PART_LOCK);
	else if (lock_levels.partition == WRITE_LOCK)
		wr_wrlock (PART_LOCK);
}

/* unlock_slurmctld - Issue the required unlock requests in a well defined order */
void 
unlock_slurmctld (slurmctld_lock_t lock_levels)
{
	if (lock_levels.partition == READ_LOCK)
		wr_rdunlock (PART_LOCK);
	else if (lock_levels.partition == WRITE_LOCK)
		wr_wrunlock (PART_LOCK);

	if (lock_levels.node == READ_LOCK)
		wr_rdunlock (NODE_LOCK);
	else if (lock_levels.node == WRITE_LOCK)
		wr_wrunlock (NODE_LOCK);

	if (lock_levels.job == READ_LOCK)
		wr_rdunlock (JOB_LOCK);
	else if (lock_levels.job == WRITE_LOCK)
		wr_wrunlock (JOB_LOCK);

	if (lock_levels.config == READ_LOCK)
		wr_rdunlock (CONFIG_LOCK);
	else if (lock_levels.config == WRITE_LOCK)
		wr_wrunlock (CONFIG_LOCK);
}

/* wr_rdlock - Issue a read lock on the specified data type */
void 
wr_rdlock (lock_datatype_t datatype)
{
	struct sembuf rdlock_op[] = {
		{ 0, 0, 0 },		/* write-wait count must be zero */
		{ 0, 0, 0 },		/* write count must be zero */
		{ 0, +1, SEM_UNDO }	/* increment read count */		
	} ;

	rdlock_op[0] . sem_num = write_wait_lock (datatype);
	rdlock_op[1] . sem_num = write_lock (datatype);
	rdlock_op[2] . sem_num = read_lock (datatype);

	if (semop (sem_id, rdlock_op, 3) == -1)
		fatal ("semop errno %d", errno);
}

/* wr_rdunlock - Issue a read unlock on the specified data type */
void
wr_rdunlock (lock_datatype_t datatype)
{
	struct sembuf rdunlock_op[] = {
		{ 0, -1, SEM_UNDO }	/* decrement read count */		
	} ;

	rdunlock_op[0] . sem_num = read_lock (datatype);

	if (semop (sem_id, rdunlock_op, 1) == -1)
		fatal ("semop errno %d", errno);
}

/* wr_wrlock - Issue a write lock on the specified data type */
void
wr_wrlock (lock_datatype_t datatype)
{
	struct sembuf waitlock_op[] = {
		{ 0, +1, SEM_UNDO }	/* increment write-wait count */		
	} ;

	struct sembuf wrlock_op[] = {
		{ 0, 0, 0 },		/* read count must be zero */
		{ 0, 0, 0 },		/* write count must be zero */
		{ 0, -1, SEM_UNDO },	/* decrement write-wait count */		
		{ 0, +1, SEM_UNDO }	/* increment write count */		
	} ;

	waitlock_op[0] . sem_num = write_wait_lock (datatype);

	wrlock_op[0] . sem_num = read_lock (datatype);
	wrlock_op[1] . sem_num = write_lock (datatype);
	wrlock_op[2] . sem_num = write_wait_lock (datatype);
	wrlock_op[3] . sem_num = write_lock (datatype);

	if (semop (sem_id, waitlock_op, 1) == -1)
		fatal ("semop errno %d", errno);

	if (semop (sem_id, wrlock_op, 4) == -1)
		fatal ("semop errno %d", errno);
}

/* wr_wrunlock - Issue a write unlock on the specified data type */
void
wr_wrunlock (lock_datatype_t datatype)
{
	struct sembuf wrunlock_op[] = {
		{ 0, -1, SEM_UNDO }	/* decrement write count */		
	} ;

	wrunlock_op[0] . sem_num = write_lock (datatype);

	if (semop (sem_id, wrunlock_op, 1) == -1)
		fatal ("semop errno %d", errno);
}

/* get_lock_values - Get the current value of all locks */
void
get_lock_values (slurmctld_lock_flags_t *lock_flags)
{
	union semun arg;
	unsigned short int array[12];

	arg.array = array;
	if (semctl (sem_id, 0, GETALL, arg)) {
		error ("semctld GETALL errno %d", errno);
		return;
	}

	lock_flags -> config.read = arg.array[0];
	lock_flags -> config.write = arg.array[1];
	lock_flags -> config.write_wait = arg.array[2];

	lock_flags -> job.read = arg.array[3];
	lock_flags -> job.write = arg.array[4];
	lock_flags -> job.write_wait = arg.array[5];

	lock_flags -> node.read = arg.array[6];
	lock_flags -> node.write = arg.array[7];
	lock_flags -> node.write_wait = arg.array[8];

	lock_flags -> partition.read = arg.array[9];
	lock_flags -> partition.write = arg.array[10];
	lock_flags -> partition.write_wait = arg.array[11];

}

/* remove_locks - remove semaphores associated with our locks */
void
remove_locks ( void )
{
	union semun arg;
	if (semctl (sem_id, 0, IPC_RMID, arg))
		error ("semctl IPC_RMID errno %d", errno);
}
