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

static int is_power = -1;

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
	debug3("task/affinity: binding to NUMA node %d", nnid);
	maxcpus = conf->sockets * conf->cores * conf->threads;
	for (c = 0; c < maxcpus; c++) {
		if (slurm_get_numa_node(c) == nnid)
			CPU_SET(c, mask);
	}
	return true;
#else
	uint16_t s, sid  = ldom % conf->sockets;
	uint16_t i, cpus = conf->cores * conf->threads;
	if (!conf->block_map)
		return false;
	for (s = sid * cpus; s < (sid+1) * cpus; s++) {
		i = s % conf->block_map_size;
		CPU_SET(conf->block_map[i], mask);
	}
	return true;
#endif
}

int get_cpuset(cpu_set_t *mask, stepd_step_rec_t *job)
{
	int nummasks, maskid, i, threads;
	char *curstr, *selstr;
	char mstr[1 + CPU_SETSIZE / 4];
	uint32_t local_id = job->envtp->localid;
	char buftype[1024];

	slurm_sprint_cpu_bind_type(buftype, job->cpu_bind_type);
	debug3("get_cpuset (%s[%d]) %s", buftype, job->cpu_bind_type,
		job->cpu_bind);
	CPU_ZERO(mask);

	if (job->cpu_bind_type & CPU_BIND_NONE) {
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_RANK) {
		threads = MAX(conf->threads, 1);
		CPU_SET(job->envtp->localid % (job->cpus*threads), mask);
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_LDRANK) {
		/* if HAVE_NUMA then bind this task ID to it's corresponding
		 * locality domain ID. Otherwise, bind this task ID to it's
		 * corresponding socket ID */
		return _bind_ldom(local_id, mask);
	}

	if (!job->cpu_bind)
		return false;

	nummasks = 1;
	selstr = NULL;

	/* get number of strings present in cpu_bind */
	curstr = job->cpu_bind;
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
		curstr = job->cpu_bind;
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
	while (*selstr && *selstr != ',' && i++ < (CPU_SETSIZE/4))
		*curstr++ = *selstr++;
	*curstr = '\0';

	if (job->cpu_bind_type & CPU_BIND_MASK) {
		/* convert mask string into cpu_set_t mask */
		if (task_str_to_cpuset(mask, mstr) < 0) {
			error("task_str_to_cpuset %s", mstr);
			return false;
		}
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_MAP) {
		unsigned int mycpu = 0;
		if (xstrncmp(mstr, "0x", 2) == 0) {
			mycpu = strtoul (&(mstr[2]), NULL, 16);
		} else {
			mycpu = strtoul (mstr, NULL, 10);
		}
		CPU_SET(mycpu, mask);
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_LDMASK) {
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
			if (val & 1)
				_bind_ldom(base, mask);
			if (val & 2)
				_bind_ldom(base + 1, mask);
			if (val & 4)
				_bind_ldom(base + 2, mask);
			if (val & 8)
				_bind_ldom(base + 3, mask);
			len--;
			ptr--;
			base += 4;
		}
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_LDMAP) {
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

/* For sysctl() functions */
#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#define	BUFFLEN	127

/* Return true if Power7 processor */
static bool _is_power_cpu(void)
{
	if (is_power == -1) {
#if defined(__FreeBSD__) || defined(__NetBSD__)

		char    buffer[BUFFLEN+1];
		size_t  len = BUFFLEN;

		if ( sysctlbyname("hw.model", buffer, &len, NULL, 0) == 0 )
		    is_power = ( strstr(buffer, "POWER7") != NULL );
		else {
		    error("_get_is_power: sysctl could not retrieve hw.model");
		    return false;
		}

#elif defined(__linux__)

		FILE *cpu_info_file;
		char buffer[BUFFLEN+1];
		char* _cpuinfo_path = "/proc/cpuinfo";
		cpu_info_file = fopen(_cpuinfo_path, "r");
		if (cpu_info_file == NULL) {
			error("_get_is_power: error %d opening %s", errno,
			      _cpuinfo_path);
			return false;	/* assume not power processor */
		}

		is_power = 0;
		while (fgets(buffer, sizeof(buffer), cpu_info_file) != NULL) {
			if (strstr(buffer, "POWER7")) {
				is_power = 1;
				break;
			}
		}
		fclose(cpu_info_file);

#else

/* Assuming other platforms don't support sysctlbyname() or /proc/cpuinfo */
#warning	"Power7 check not implemented for this platform."
	is_power = 0;

#endif
	}

	if (is_power == 1)
		return true;
	return false;
}

/* Translate global CPU index to local CPU index. This is needed for
 * Power7 processors with multi-threading disabled. On those processors,
 * the CPU mask has gaps for the unused threads (different from Intel
 * processors) which need to be skipped over in the mask used in the
 * set system call. */
void reset_cpuset(cpu_set_t *new_mask, cpu_set_t *cur_mask)
{
	cpu_set_t full_mask, newer_mask;
	int cur_offset, new_offset = 0, last_set = -1;

	if (!_is_power_cpu())
		return;

	if (slurm_getaffinity(1, sizeof(full_mask), &full_mask)) {
		/* Try to get full CPU mask from process init */
		CPU_ZERO(&full_mask);
#ifdef __FreeBSD__
		CPU_OR(&full_mask, cur_mask);
#else
		CPU_OR(&full_mask, &full_mask, cur_mask);
#endif
	}
	CPU_ZERO(&newer_mask);
	for (cur_offset = 0; cur_offset < CPU_SETSIZE; cur_offset++) {
		if (!CPU_ISSET(cur_offset, &full_mask))
			continue;
		if (CPU_ISSET(new_offset, new_mask)) {
			CPU_SET(cur_offset, &newer_mask);
			last_set = cur_offset;
		}
		new_offset++;
	}

	CPU_ZERO(new_mask);
	for (cur_offset = 0; cur_offset <= last_set; cur_offset++) {
		if (CPU_ISSET(cur_offset, &newer_mask))
			CPU_SET(cur_offset, new_mask);
	}
}

int slurm_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask)
{
	int rval;
	char mstr[1 + CPU_SETSIZE / 4];

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
	char mstr[1 + CPU_SETSIZE / 4];

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
