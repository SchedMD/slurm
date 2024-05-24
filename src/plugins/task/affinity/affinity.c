/*****************************************************************************\
 *  src/plugins/task/affinity/affinity.c - task affinity plugin
 *****************************************************************************
 *  Copyright (C) 2005-2006 Hewlett-Packard Development Company, L.P.
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

#include "affinity.h"

/* Older versions of sched.h (ie. Centos5) don't include CPU_OR. */
#ifndef CPU_OR

#ifndef CPU_OP_S
# define __CPU_OP_S(setsize, destset, srcset1, srcset2, op) \
  (__extension__      \
   ({ cpu_set_t *__dest = (destset);      \
     const __cpu_mask *__arr1 = (srcset1)->__bits;      \
     const __cpu_mask *__arr2 = (srcset2)->__bits;      \
     size_t __imax = (setsize) / sizeof (__cpu_mask);      \
     size_t __i;      \
     for (__i = 0; __i < __imax; ++__i)      \
       ((__cpu_mask *) __dest->__bits)[__i] = __arr1[__i] op __arr2[__i];    \
     __dest; }))
#endif

# define CPU_OR(destset, srcset1, srcset2) \
  __CPU_OP_S (sizeof (cpu_set_t), destset, srcset1, srcset2, |)
#endif

/* If HAVE_NUMA, create mask for given ldom.
 * Otherwise create mask for given socket
 */
static int _bind_ldom(uint32_t ldom, cpu_set_t *mask)
{
#ifdef HAVE_NUMA
	int c, maxcpus, nnid = 0;
	int nmax = numa_max_node();
	if (nmax > 0)
		nnid = ldom % (nmax+1);
	debug3("binding to NUMA node %d", nnid);
	maxcpus = conf->sockets * conf->cores * conf->threads;
	for (c = 0; c < maxcpus; c++) {
		if (slurm_get_numa_node(c) == nnid)
			CPU_SET(c, mask);
	}
	return true;
#else
	uint16_t s, sid  = ldom % conf->sockets;
	uint16_t i, cpus = conf->cores * conf->threads;
	warning("%s: Attempting to bind to NUMA locality domains while Slurm was build without NUMA support",
		__func__);
	if (!conf->block_map)
		return false;
	for (s = sid * cpus; s < (sid+1) * cpus; s++) {
		i = s % conf->block_map_size;
		CPU_SET(conf->block_map[i], mask);
	}
	return true;
#endif
}

int get_cpuset(cpu_set_t *mask, stepd_step_rec_t *step, uint32_t node_tid)
{
	int nummasks, maskid, i;
	char *curstr, *selstr;
	char mstr[CPU_SET_HEX_STR_SIZE];
	uint32_t local_id = node_tid;
	char buftype[1024];

	slurm_sprint_cpu_bind_type(buftype, step->cpu_bind_type);
	debug3("get_cpuset (%s[%d]) %s", buftype, step->cpu_bind_type,
		step->cpu_bind);
	CPU_ZERO(mask);

	if (step->cpu_bind_type & CPU_BIND_NONE) {
		return false;
	}

	if (step->cpu_bind_type & CPU_BIND_LDRANK) {
		/* if HAVE_NUMA then bind this task ID to it's corresponding
		 * locality domain ID. Otherwise, bind this task ID to it's
		 * corresponding socket ID */
		return _bind_ldom(local_id, mask);
	}

	if (!step->cpu_bind)
		return false;

	nummasks = 1;
	selstr = NULL;

	/* get number of strings present in cpu_bind */
	curstr = step->cpu_bind;
	while (*curstr) {
		if (nummasks == local_id+1) {
			selstr = curstr;
			break;
		}
		if (*curstr == ',')
			nummasks++;
		curstr++;
	}

	/* if we didn't already find the mask... */
	if (!selstr) {
		/* ...select mask string by wrapping task ID into list */
		maskid = local_id % nummasks;
		i = maskid;
		curstr = step->cpu_bind;
		while (*curstr && i) {
			if (*curstr == ',')
			    	i--;
			curstr++;
		}
		if (!*curstr) {
			return false;
		}
		selstr = curstr;
	}

	/* extract the selected mask from the list */
	i = 0;
	curstr = mstr;
	while (*selstr && (*selstr != ',') && (++i < CPU_SET_HEX_STR_SIZE))
		*curstr++ = *selstr++;
	*curstr = '\0';

	if (step->cpu_bind_type & CPU_BIND_MASK) {
		/* convert mask string into cpu_set_t mask */
		if (task_str_to_cpuset(mask, mstr) < 0) {
			error("task_str_to_cpuset %s", mstr);
			return false;
		}
		return true;
	}

	if (step->cpu_bind_type & CPU_BIND_MAP) {
		unsigned int mycpu = 0;
		if (xstrncmp(mstr, "0x", 2) == 0) {
			mycpu = strtoul (&(mstr[2]), NULL, 16);
		} else {
			mycpu = strtoul (mstr, NULL, 10);
		}
		CPU_SET(mycpu, mask);
		return true;
	}

	if (step->cpu_bind_type & CPU_BIND_LDMASK) {
		/* if HAVE_NUMA bind this task to the locality domains
		 * identified in mstr. Otherwise bind this task to the
		 * sockets identified in mstr */
		int len = strlen(mstr);
		char *ptr = mstr + len - 1;
		uint32_t base = 0;

		curstr = mstr;
		/* skip 0x, it's all hex anyway */
		if (len > 1 && !memcmp(mstr, "0x", 2L))
			curstr += 2;
		while (ptr >= curstr) {
			char val = slurm_char_to_hex(*ptr);
			if (val == (char) -1)
				return false;
			if ((val & 1) && !_bind_ldom(base, mask))
				return false;
			if ((val & 2) && !_bind_ldom(base + 1, mask))
				return false;
			if ((val & 4) && !_bind_ldom(base + 2, mask))
				return false;
			if ((val & 8) && !_bind_ldom(base + 3, mask))
				return false;
			ptr--;
			base += 4;
		}
		return true;
	}

	if (step->cpu_bind_type & CPU_BIND_LDMAP) {
		/* if HAVE_NUMA bind this task to the given locality
		 * domain. Otherwise bind this task to the given
		 * socket */
		uint32_t myldom = 0;
		if (xstrncmp(mstr, "0x", 2) == 0) {
			myldom = strtoul (&(mstr[2]), NULL, 16);
		} else {
			myldom = strtoul (mstr, NULL, 10);
		}
		return _bind_ldom(myldom, mask);
	}

	return false;
}

int slurm_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask)
{
	int rval;
	char mstr[CPU_SET_HEX_STR_SIZE];

#ifdef __FreeBSD__
        rval = cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID,
				pid, size, mask);
#else
	rval = sched_setaffinity(pid, size, mask);
#endif
	if (rval) {
		verbose("sched_setaffinity(%d,%zu,0x%s) failed: %m",
			pid, size, task_cpuset_to_str(mask, mstr));
	}
	return (rval);
}

int slurm_getaffinity(pid_t pid, size_t size, cpu_set_t *mask)
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
        rval = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID,
				pid, size, mask);
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
	return (rval);
}
