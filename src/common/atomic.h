/*****************************************************************************\
 *  atomic.h - Atomic definitions
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

#ifndef _SLURM_ATOMIC_H
#define _SLURM_ATOMIC_H

#include <stdint.h>

/*
 * Need at least C11 for atomics and guarantee __STDC_NO_ATOMICS__ is always
 * defined if not even if not by a non-compliant compiler or CPU Architecture.
 *
 * https://fedoraproject.org/wiki/Architectures/ARM/GCCBuiltInAtomicOperations:
 *	older ARM processors (prior to ARMv6) did not provide hardware
 *	instructions to support this.
 */
#if (!defined(__STDC_VERSION__) || (__STDC_VERSION__ <= 201112L)) && \
	!defined(__STDC_NO_ATOMICS__)
#define __STDC_NO_ATOMICS__
#endif

#ifndef __STDC_NO_ATOMICS__

#include <stdatomic.h>

/* Debug log current features of Atomic support from compiler */
extern void atomic_log_features(void);

/******************************************************************************
 * atomic uint64
 ******************************************************************************/

/* Always use helper functions to access and ATOMIC_UINT64_INITIALIZER() */
typedef struct {
	_Atomic uint64_t value;
} atomic_uint64_t;

#define ATOMIC_UINT64_INITIALIZER(init) \
	((atomic_uint64_t) { \
		.value = (init), \
	})

/* Use atomic_uint64_add() instead */
extern uint64_t atomic_uint64_ptr_add(atomic_uint64_t *target, uint64_t value);

/* Increment target by value and return prior value */
#define atomic_uint64_add(target, value) atomic_uint64_ptr_add(&target, value)

/* Use atomic_uint64_increment() instead */
extern uint64_t atomic_uint64_ptr_increment(atomic_uint64_t *target);

/* Increment target by 1 */
#define atomic_uint64_increment(target) atomic_uint64_ptr_increment(&target)

/* Use atomic_uint64_decrement() instead */
extern uint64_t atomic_uint64_ptr_decrement(atomic_uint64_t *target);

/* Decrement target by 1 */
#define atomic_uint64_decrement(target) atomic_uint64_ptr_decrement(&target)

/* use atomic_uint64_get() instead */
extern uint64_t atomic_uint64_ptr_get(atomic_uint64_t *target);

/* get value of target */
#define atomic_uint64_get(target) atomic_uint64_ptr_get(&target)

/* use atomic_uint64_set() instead */
extern uint64_t atomic_uint64_ptr_set(atomic_uint64_t *target, uint64_t value);

/* Set target to value and return value */
#define atomic_uint64_set(target, value) atomic_uint64_ptr_set(&target, value)

/* Set value of target to zero and return value */
#define atomic_uint64_set_zero(target) atomic_uint64_ptr_set(&target, 0)

#else

#include "src/common/log.h"

#define atomic_log_features() \
	debug("%s: Atomic operations are not supported", __func__)

#endif

#endif
