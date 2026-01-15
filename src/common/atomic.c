/*****************************************************************************\
 *  atomic.c - Atomic definitions
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

#include "stdint.h"

#include "src/common/atomic.h"
#include "src/common/log.h"

#ifndef __STDC_NO_ATOMICS__

static const char *_lock_type_str(const int type)
{
	switch (type) {
	case 0:
		return "locking";
	case 1:
		return "partial-locking";
	case 2:
		return "lock-free";
	}

	fatal_abort("should never happen");
}

extern void atomic_log_features(void)
{
	debug("%s: _Atomic enabled: bool=%s char=%s char16=%s char32=%s wchar=%s short=%s int=%s long=%s llong=%s pointer=%s char8=%s",
	      __func__, _lock_type_str(ATOMIC_BOOL_LOCK_FREE),
	      _lock_type_str(ATOMIC_CHAR_LOCK_FREE),
	      _lock_type_str(ATOMIC_CHAR16_T_LOCK_FREE),
	      _lock_type_str(ATOMIC_CHAR32_T_LOCK_FREE),
	      _lock_type_str(ATOMIC_WCHAR_T_LOCK_FREE),
	      _lock_type_str(ATOMIC_SHORT_LOCK_FREE),
	      _lock_type_str(ATOMIC_INT_LOCK_FREE),
	      _lock_type_str(ATOMIC_LONG_LOCK_FREE),
	      _lock_type_str(ATOMIC_LLONG_LOCK_FREE),
	      _lock_type_str(ATOMIC_POINTER_LOCK_FREE),
#ifdef ATOMIC_CHAR8_T_LOCK_FREE
	      /* Added in C23 */
	      _lock_type_str(ATOMIC_CHAR8_T_LOCK_FREE)
#else /* !ATOMIC_CHAR8_T_LOCK_FREE */
	      "N/A"
#endif /* !ATOMIC_CHAR8_T_LOCK_FREE */
	     );
}

extern uint64_t atomic_uint64_ptr_add(atomic_uint64_t *target, uint64_t value)
{
	return atomic_fetch_add(&target->value, value);
}

extern uint64_t atomic_uint64_ptr_increment(atomic_uint64_t *target)
{
	return atomic_fetch_add(&target->value, 1);
}

extern uint64_t atomic_uint64_ptr_decrement(atomic_uint64_t *target)
{
	return atomic_fetch_sub(&target->value, 1);
}

extern uint64_t atomic_uint64_ptr_get(atomic_uint64_t *target)
{
	return atomic_load(&target->value);
}

extern uint64_t atomic_uint64_ptr_set(atomic_uint64_t *target, uint64_t value)
{
	return atomic_exchange(&target->value, value);
}

#endif /* __STDC_NO_ATOMICS__ */
