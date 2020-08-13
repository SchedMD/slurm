/*****************************************************************************\
 *  workq.h - declarations for work queue manager
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#ifndef SLURMRESTD_WORKQ_H
#define SLURMRESTD_WORKQ_H

#include "src/common/list.h"

/*
 * Call back for generic work
 *
 * IN arg pointer to data for function
 */
typedef void (*work_func_t)(void *arg);

/* Opaque struct */
typedef struct {
	int magic;
	/* list of workq_worker_t */
	List workers;
	/* list of workq_work_t */
	List work;

	/* track simple stats for logging */
	int active;
	int total;

	/* manger is actively shutting down */
	bool shutdown;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
} workq_t;

/*
 * Initialize a new workq struct
 * IN count - number of workers to add
 * RET ptr to new workq struct
 */
extern workq_t *new_workq(int count);

/*
 * Stop all work (eventually) and reject new requests
 * This will block until all work is complete.
 */
extern void quiesce_workq(workq_t *workq);

/*
 * Free workq struct.
 * Will stop all workers (eventually).
 */
extern void free_workq(workq_t *workq);

/*
 * Add work to workq
 * IN workq - work queue to queue up work on
 * IN func - function pointer to run work
 * IN arg - arg to hand to function pointer
 * IN tag - tag used in logging this function
 * NOTE: never add a thread that will never return or free_workq() will never
 * return either.
 * RET SLURM_SUCCESS or error if workq already shutdown
 */
extern int workq_add_work(workq_t *workq, work_func_t func, void *arg,
			  const char *tag);

/*
 * Grab copy of the workq active count
 */
extern int workq_get_active(workq_t *workq);

#define FREE_NULL_WORKQ(_X)             \
	do {                            \
		if (_X)                 \
			free_workq(_X); \
		_X = NULL;              \
	} while (0)

#endif /* SLURMRESTD_WORKQ_H */
