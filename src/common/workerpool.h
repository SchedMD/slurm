/*****************************************************************************\
 *  workerpool.h - worker pool
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

#ifndef _COMMON_WORKERPOOL_H
#define _COMMON_WORKERPOOL_H

#include <stdbool.h>
#include <stddef.h>

#include "src/common/workq.h"

#ifndef MEMORY_LEAK_DEBUG
#define WORKERPOOL_THREAD_COUNT_MAX 256
#else
#define WORKERPOOL_THREAD_COUNT_MAX 64
#endif

#define WORKERPOOL_THREAD_COUNT_MIN 2

/* aliased by CONMGR_PARAM_THREADS */
#define WORKERPOOL_PARAM_THREADS "WORKERPOOL_THREADS="

/*
 * Create workerpool
 * IN thread_count - User requested thread count
 * IN default_thread_count - Default number of threads for current daemon
 * IN params - CSV string with parameters for workerpool
 *	See WORKERPOOL_PARAM_* for possible parameters.
 */
extern void workerpool_init(const int thread_count,
			    const int default_thread_count, const char *params);

/* Shutdown and release workerpool */
extern void workerpool_fini(void);

/*
 * Create new work to run in workerpool's workq
 * WARNING: always use workerpool_enqueue*() macros if alloc is NULL
 * IN alloc - workq allocator
 * IN priority - Priority of work to run
 * IN func - function to call
 * IN func_name - function name for logging
 * IN arg - Arbitrary pointer to hand to func()
 * IN caller - __func__ from caller
 */
extern void workerpool_enqueue(workq_allocator_t *alloc,
			       workq_priority_t priority, work_func_t func,
			       const char *func_name, void *arg,
			       const char *caller);

#define workerpool_enqueue_normal(func, arg) \
	workerpool_enqueue(NULL, WORKQ_PRIORITY_NORMAL, func, #func, arg, \
			   __func__)

#define workerpool_enqueue_idle(func, arg) \
	workerpool_enqueue(NULL, WORKQ_PRIORITY_IDLE, func, #func, arg, \
			   __func__)

#endif
