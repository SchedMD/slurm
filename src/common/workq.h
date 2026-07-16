/*****************************************************************************\
 *  workq.h - work queue
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _COMMON_WORKQ_H
#define _COMMON_WORKQ_H

#include <stdbool.h>
#include <stddef.h>

typedef struct workq_s workq_t;
typedef struct workq_allocator_s workq_allocator_t;

typedef enum {
	WORKQ_PRIORITY_INVALID = 0,
	WORKQ_PRIORITY_IDLE,
	WORKQ_PRIORITY_NORMAL,
	WORKQ_PRIORITY_INVALID_MAX
} workq_priority_t;

/*
 * Prototype for all work callbacks
 * IN shutdown - True if workq is shutting down
 * IN arg - Arbitrary pointer handed to workq_enqueue()
 */
typedef void (*work_func_t)(const bool shutdown, void *arg);

/*
 * Create new work to run in workq
 * IN workq - Pointer to work queue to enqueue the work
 * IN alloc - Pointer to allocator for the work request
 * IN priority - which priority queue for work
 * IN func - function to call
 * IN func_name - function name for logging
 * IN caller - __func__ from caller
 * IN arg - Arbitrary pointer to hand to func()
 */
extern void workq_enqueue(workq_t *workq, workq_allocator_t *alloc,
			  const workq_priority_t priority, work_func_t func,
			  const char *func_name, const char *caller, void *arg);

/*
 * Run pending work in workq
 * IN workq - Run all work from this workq
 * IN blocking
 *	True: Wait for new work until workq_fini()
 *	False: Run all pending work and return
 */
extern void workq_run(workq_t *workq, const bool blocking);

/* Get number of bytes required to store a workq instance */
extern const size_t workq_bytes(void);

/*
 * Initialize a new workq
 * IN workq - Pointer with workq_bytes() bytes of allocated memory
 * IN name - NULL or Name of workq probe
 * RET pointer to workq
 */
extern workq_t *workq_init(workq_t *workq, const char *name);

/*
 * Shutdown and release workq
 * NOTE: Use FREE_NULL_WORKQ() instead of calling directly
 * IN workq - Pointer to release workq state
 */
extern void workq_fini(workq_t *workq);

#define FREE_NULL_WORKQ(workq) \
	do { \
		if (workq) { \
			workq_fini(workq); \
			(workq) = NULL; \
		} \
	} while (0)

/*
 * Initialize allocator for work requests
 * NOTE: Allocator will be owned by workq
 * IN count - Number of work requests to preallocate
 * IN name - Name to use for logging (must be valid life of allocator)
 * RET pointer to allocator
 */
extern workq_allocator_t *workq_allocator(workq_t *workq, const int count,
					  const char *name);

#endif
