/*****************************************************************************\
 *  threadpool.h - Thread Pool
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef SLURM_THREAD_POOL_H
#define SLURM_THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"

#define THREADPOOL_MAX_THREADS 2048

#ifndef MEMORY_LEAK_DEBUG
#define THREADPOOL_DEFAULT_PRESERVE 512
#define THREADPOOL_DEFAULT_PREALLOCATE 8
#else
#define THREADPOOL_DEFAULT_PRESERVE 12
#define THREADPOOL_DEFAULT_PREALLOCATE 8
#endif

typedef void *(*threadpool_func_t)(void *arg);

/*
 * From man prctl:
 *	If the length of the  string, including the terminating null byte,
 *	exceeds 16 bytes, the string is silently truncated.
 */
#define PRCTL_BUF_BYTES 17

/*
 * Create new pthread
 * See pthread_create() for use cases.
 * IN func - function for thread to call
 * IN func_name - function name (for logging)
 * IN thread_name - thread or process name
 *	thread_name must be less than strlen(PRCTL_BUF_BYTES) or it will be
 *	silently truncated by prctl().
 * IN arg - arg to pass to function
 * IN detached
 *	True: create detached thread.
 *	False: create joinable thread.
 *		Thread that must be cleaned up with threadpool_join().
 * IN id_ptr - Populate pointer with new thread's ID or 0 on failure
 * IN caller - __func__ from caller
 * RET SLURM_SUCCESS or error
 */
extern int threadpool_create(threadpool_func_t func, const char *func_name,
			     void *arg, const bool detached,
			     const char *thread_name, pthread_t *id_ptr,
			     const char *caller);

/*
 * Note that the attr argument is intentionally omitted, as it will
 * be setup within the macro to Slurm's default options.
 */
#define slurm_thread_create(id, func, arg) \
	do { \
		int thread_err = SLURM_SUCCESS; \
		if ((thread_err = \
			     threadpool_create((func), \
					       XSTRINGIFY(func), (arg), false, \
							  NULL, (id), \
							  __func__))) \
			fatal("%s: threadpool_create() failed: %s", \
			      __func__, slurm_strerror(thread_err)); \
	} while (false)

/*
 * Both the thread and attr arguments are intentionally omitted. There
 * is basically nothing safe you can do with a detached thread's id,
 * so this macro intentionally prevents you from capturing it.
 */
#define slurm_thread_create_detached(func, arg) \
	do { \
		int thread_err = SLURM_SUCCESS; \
		if ((thread_err = \
			     threadpool_create(func, XSTRINGIFY(func), arg, \
								true, NULL, \
								NULL, \
								__func__))) \
			fatal("%s: threadpool_create() failed: %s", \
			      __func__, slurm_strerror(thread_err)); \
	} while (false)
/*
 * Wait for pthread to exit.
 * See pthread_join() for use cases.
 * NOTE: can only be called once per thread.
 * NOTE: thread IDs repeat but the count of times to join is maintained
 * IN id - thread ID
 * IN caller - __func__ from caller
 * RET SLURM_SUCCESS or error
 */
extern int threadpool_join(const pthread_t id, const char *caller);

#define slurm_thread_join(id) \
	do { \
		int thread_err = SLURM_SUCCESS; \
		if ((thread_err = threadpool_join(id, __func__))) \
			errno = thread_err; \
		else \
			id = 0; \
	} while (false)

#define THREADPOOL_PARAM "THREADPOOL="
#define THREADPOOL_PARAM_PREALLOCATE "THREADPOOL_PREALLOCATE="
#define THREADPOOL_PARAM_PRESERVE "THREADPOOL_PRESERVE="

/*
 * Create thread pool
 * IN default_count - Per daemon default number of threads to pre-allocate
 * IN params - CSV string with parameters for threadpool
 *	See THREADPOOL_PARAM_* for possible parameters.
 */
extern void threadpool_init(const int default_count, const char *params);

/* Shutdown the threadpool */
extern void threadpool_fini(void);

#endif
