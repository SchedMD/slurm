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

typedef void *(*threadpool_func_t)(void *arg);

#ifdef PTHREAD_SCOPE_SYSTEM
#define slurm_attr_init(attr) \
	do { \
		int err = pthread_attr_init(attr); \
		if (err) { \
			errno = err; \
			fatal("pthread_attr_init: %m"); \
		} \
		/* we want 1:1 threads if there is a choice */ \
		err = pthread_attr_setscope(attr, PTHREAD_SCOPE_SYSTEM); \
		if (err) { \
			errno = err; \
			error("pthread_attr_setscope: %m"); \
		} \
		err = pthread_attr_setstacksize(attr, STACK_SIZE); \
		if (err) { \
			errno = err; \
			error("pthread_attr_setstacksize: %m"); \
		} \
	} while (0)
#else
#define slurm_attr_init(attr) \
	do { \
		int err = pthread_attr_init(attr); \
		if (err) { \
			errno = err; \
			fatal("pthread_attr_init: %m"); \
		} \
		err = pthread_attr_setstacksize(attr, STACK_SIZE); \
		if (err) { \
			errno = err; \
			error("pthread_attr_setstacksize: %m"); \
		} \
	} while (0)
#endif

#define slurm_attr_destroy(attr) \
	do { \
		int err = pthread_attr_destroy(attr); \
		if (err) { \
			errno = err; \
			error("pthread_attr_destroy failed, " \
			      "possible memory leak!: %m"); \
		} \
	} while (0)

/*
 * Create new pthread
 * See pthread_create() for use cases.
 * IN func - function for thread to call
 * IN func_name - function name (for logging)
 * IN thread_name - thread name (assigned to thread via prctl())
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

#endif
