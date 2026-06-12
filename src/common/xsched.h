/*****************************************************************************\
 *  sched.h - kernel cpu affinity handlers
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

#ifndef _XSCHED_H
#define _XSCHED_H

#ifdef __FreeBSD__
#include <sys/param.h> /* param.h must precede cpuset.h */
#include <sys/cpuset.h>
typedef cpuset_t cpu_set_t;
#endif

#ifdef __NetBSD__
#define CPU_ZERO(c) cpuset_zero(*(c))
#define CPU_ISSET(i, c) cpuset_isset((i), *(c))
#define sched_getaffinity sched_getaffinity_np
#endif

#include <sched.h>

/* The size to represent a cpu_set_t as a hex string (including null) */
#define CPU_SET_HEX_STR_SIZE (1 + (CPU_SETSIZE / 4))

/*
 * Convert a CPU bitmask to a hex string.
 *
 * IN mask - A CPU bitmask pointer.
 * IN/OUT str - A char pointer used to return a string of size
 *		CPU_SET_HEX_STR_SIZE.
 * RET - Returns a pointer to a string slice in str that starts at the first
 *	 non-zero hex char or last zero hex char if all bits are not set.
 */
extern char *task_cpuset_to_str(const cpu_set_t *mask, char *str);

/*
 * Convert a hex string to a CPU bitmask.
 *
 * IN/OUT mask - An empty CPU bitmask pointer that will be set according to CPUs
 *		 specified by the hex values in str.
 * IN str - A null-terminated hex string that specifies CPUs to set.
 * RET - Returns -1 if str could not be interpreted into valid hex or if str is
 *	 too large, else returns 0 on success.
 */
extern int task_str_to_cpuset(cpu_set_t *mask, const char *str);

/* Wrapper for sched_setaffinity() */
extern int slurm_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask);

/* Wrapper for sched_getaffinity() */
extern int slurm_getaffinity(pid_t pid, size_t size, cpu_set_t *mask);

/*
 * Get number of CPUs assigned in mask
 * RET CPUs set or -1 on error
 */
extern int task_cpuset_get_assigned_count(size_t size, cpu_set_t *mask);

#endif
