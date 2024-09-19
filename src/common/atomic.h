/*****************************************************************************\
 *  atomic.h - Slurm atomics handlers and helpers
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

#ifndef _SLURM_ATOMICS_H
#define _SLURM_ATOMICS_H

/*
 * Need at least C11 for atomics and guarentee __STDC_NO_ATOMICS__ is always
 * defined if not even if not by a non-compliant complier.
 */
#if (!defined(__STDC_VERSION__) || (__STDC_VERSION__ <= 201112L)) && \
	!defined(__STDC_NO_ATOMICS__)
#define __STDC_NO_ATOMICS__
#endif

#ifndef __STDC_NO_ATOMICS__
#include <stdatomic.h>
#endif /* __STDC_NO_ATOMICS__ */

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef __STDC_NO_ATOMICS__

/* Always use helper functions to access and ATOMIC_INT32_INITIALIZER() */
typedef struct {
	_Atomic int32_t value;
} atomic_int32_t;

#define ATOMIC_INT32_INITIALIZER(init) \
	((atomic_int32_t) { \
		.value = init, \
	})

/* Always helper functions to access and ATOMIC_BOOL_INITIALIZER() */
typedef struct {
	_Atomic bool value;
} atomic_bool_t;

#define ATOMIC_BOOL_INITIALIZER(init) \
	((atomic_bool_t) { \
		.value = init, \
	})

/* Always init with ATOMIC_PTHREAD_INITIALIZER () */
typedef _Atomic pthread_t atomic_pthread_t;

#define ATOMIC_PTHREAD_INITIALIZER(init) ((atomic_pthread_t) init)

#else /* __STDC_NO_ATOMICS__ */

typedef struct {
	pthread_mutex_t mutex;
	int32_t value;
} atomic_int32_t;

#define ATOMIC_INT32_INITIALIZER(init) \
	((atomic_int32_t) { \
		.mutex = PTHREAD_MUTEX_INITIALIZER, \
		.value = init, \
	})

typedef struct {
	pthread_mutex_t mutex;
	bool value;
} atomic_bool_t;

#define ATOMIC_BOOL_INITIALIZER(init) \
	((atomic_bool_t) { \
		.mutex = PTHREAD_MUTEX_INITIALIZER, \
		.value = init, \
	})

#endif /* __STDC_NO_ATOMICS__ */

/* Use atomic_bool_set_true() instead */
extern bool atomic_bool_ptr_set_true(atomic_bool_t *target);

/*
 * Set target bool to true
 * RET prior value of target
 */
#define atomic_bool_set_true(target) atomic_bool_ptr_set_true(&target)

/* Use atomic_bool_set_false() instead */
extern bool atomic_bool_ptr_set_false(atomic_bool_t *target);

/*
 * Set target bool to false
 * RET prior value of target
 */
#define atomic_bool_set_false(target) atomic_bool_ptr_set_false(&target)

/* Use atomic_bool_set_true_from_false() instead */
extern bool atomic_bool_ptr_set_true_from_false(atomic_bool_t *target);

/*
 * Set target bool to true if target was false
 * RET
 *	true: if target was false and changed to true
 *	false: if target was true (and not changed)
 */
#define atomic_bool_set_true_from_false(target) \
	atomic_bool_ptr_set_true_from_false(&target)

/* Use atomic_bool_set_false_from_true() instead */
extern bool atomic_bool_ptr_set_false_from_true(atomic_bool_t *target);

/*
 * Set target bool to false if target was true
 * RET
 *	true: if target was true and changed to false
 *	false: if target was false (and not changed)
 */
#define atomic_bool_set_false_from_true(target) \
	atomic_bool_ptr_set_false_from_true(&target)

/* Use atomic_bool_get() instead */
extern bool atomic_bool_ptr_get(atomic_bool_t *target);

/* Get value of target */
#define atomic_bool_get(target) atomic_bool_ptr_get(&target)

/* Use atomic_int32_add() instead */
extern int32_t atomic_int32_ptr_add(atomic_int32_t *target, int32_t value);

/* Increment target by value and return prior value */
#define atomic_int32_add(target, value) atomic_int32_ptr_add(&target, value)

/* Use atomic_int32_increment() instead */
extern void atomic_int32_ptr_increment(atomic_int32_t *target);

/* Increment target by 1 */
#define atomic_int32_increment(target) atomic_int32_ptr_increment(&target)

/* Use atomic_int32_decrement() instead */
extern void atomic_int32_ptr_decrement(atomic_int32_t *target);

/* Decrement target by 1 */
#define atomic_int32_decrement(target) atomic_int32_ptr_decrement(&target)

/* use atomic_int32_get() instead */
extern int32_t atomic_int32_ptr_get(atomic_int32_t *target);

/* get value of target */
#define atomic_int32_get(target) atomic_int32_ptr_get(&target)

/* use atomic_int32_set() instead */
extern int32_t atomic_int32_ptr_set(atomic_int32_t *target, int32_t value);

/* Set target to value and return value */
#define atomic_int32_set(target, value) atomic_int32_ptr_set(&target, value)

/* Set value of target to zero and return value */
#define atomic_int32_set_zero(target) atomic_int32_ptr_set(&target, 0)

/* Debug log current features of Atomic support from compiler */
extern void atomic_log_features(void);

#endif /* _SLURM_ATOMICS_H */
