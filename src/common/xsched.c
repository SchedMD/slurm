/*****************************************************************************\
 *  xsched.c - kernel cpu affinity handlers
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

#define _GNU_SOURCE
#include <stdbool.h>
#include <string.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/xsched.h"

extern char *task_cpuset_to_str(const cpu_set_t *mask, char *str)
{
#if defined(__APPLE__)
	fatal("%s: not supported on macOS", __func__);
#else
	int base;
	char *ptr = str;
	char *ret = NULL;
	bool leading_zeros = true;

	for (base = CPU_SETSIZE - 4; base >= 0; base -= 4) {
		char val = 0;
		if (CPU_ISSET(base, mask))
			val |= 1;
		if (CPU_ISSET(base + 1, mask))
			val |= 2;
		if (CPU_ISSET(base + 2, mask))
			val |= 4;
		if (CPU_ISSET(base + 3, mask))
			val |= 8;
		/* If it's a leading zero, ignore it */
		if (leading_zeros && !val)
			continue;
		if (!ret && val)
			ret = ptr;
		*ptr++ = slurm_hex_to_char(val);
		/* All zeros from here on out will be written */
		leading_zeros = false;
	}
	/* If the bitmask is all 0s, add a single 0 */
	if (leading_zeros)
		*ptr++ = '0';
	*ptr = '\0';
	return ret ? ret : ptr - 1;
#endif
}

extern int task_str_to_cpuset(cpu_set_t *mask, const char *str)
{
#if defined(__APPLE__)
	fatal("%s: not supported on macOS", __func__);
#else
	int len = strlen(str);
	const char *ptr = str + len - 1;
	int base = 0;

	/* skip 0x, it's all hex anyway */
	if ((len > 1) && !memcmp(str, "0x", 2L)) {
		str += 2;
		len -= 2;
	}

	/* Check that hex chars plus NULL <= CPU_SET_HEX_STR_SIZE */
	if ((len + 1) > CPU_SET_HEX_STR_SIZE) {
		error("%s: Hex string is too large to convert to cpu_set_t (length %ld > %d)",
		      __func__, (long int) len, CPU_SET_HEX_STR_SIZE - 1);
		return -1;
	}

	CPU_ZERO(mask);
	while (ptr >= str) {
		char val = slurm_char_to_hex(*ptr);
		if (val == (char) -1)
			return -1;
		if (val & 1)
			CPU_SET(base, mask);
		if (val & 2)
			CPU_SET(base + 1, mask);
		if (val & 4)
			CPU_SET(base + 2, mask);
		if (val & 8)
			CPU_SET(base + 3, mask);
		ptr--;
		base += 4;
	}

	return 0;
#endif
}

extern int slurm_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask)
{
	int rval;
	char mstr[CPU_SET_HEX_STR_SIZE];

#ifdef __FreeBSD__
	rval = cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, pid, size,
				  mask);
#else
	rval = sched_setaffinity(pid, size, mask);
#endif
	if (rval) {
		verbose("sched_setaffinity(%d,%zu,0x%s) failed: %m",
			pid, size, task_cpuset_to_str(mask, mstr));
	}
	return rval;
}

extern int slurm_getaffinity(pid_t pid, size_t size, cpu_set_t *mask)
{
	int rval;
	char mstr[CPU_SET_HEX_STR_SIZE];

	CPU_ZERO(mask);

	/*
	 * The FreeBSD cpuset API is a superset of the Linux API.
	 * In addition to PIDs, it supports threads, interrupts,
	 * jails, and potentially other objects.  The first two arguments
	 * to cpuset_*etaffinity() below indicate that the third argument
	 * is a PID.  -1 indicates the PID of the calling process.
	 * Linux sched_*etaffinity() uses 0 for this.
	 */
#ifdef __FreeBSD__
	rval = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, pid, size,
				  mask);
#else
	rval = sched_getaffinity(pid, size, mask);
#endif
	if (rval) {
		verbose("sched_getaffinity(%d,%zu,0x%s) failed with status %d",
			pid, size, task_cpuset_to_str(mask, mstr), rval);
	} else {
		debug3("sched_getaffinity(%d) = 0x%s",
		       pid, task_cpuset_to_str(mask, mstr));
	}
	return rval;
}

extern int task_cpuset_get_assigned_count(size_t size, cpu_set_t *mask)
{
	int count = 0;

	if (!size || !mask)
		return -1;

	/*
	 * Count CPUs assigned instead of assuming all CPUs should be included.
	 */
	for (size_t max = CPU_COUNT(mask), cpu = 0; cpu < max; cpu++)
		if (CPU_ISSET(cpu, mask))
			count++;

	return count;
}
