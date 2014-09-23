/*****************************************************************************\
 *  src/plugins/task/affinity/numa.c - numa-based memory affinity functions
 *  $Id: affinity.c,v 1.2 2005/11/04 02:46:51 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California and
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include "affinity.h"

#ifdef HAVE_NUMA

static char * _memset_to_str(nodemask_t *mask, char *str)
{
	int base, begin = 0;
	char *ptr = str;
	char *ret = 0;

	for (base = NUMA_NUM_NODES - 4; base >= 0; base -= 4) {
		char val = 0;
		if (nodemask_isset(mask, base))
			val |= 1;
		if (nodemask_isset(mask, base + 1))
			val |= 2;
		if (nodemask_isset(mask, base + 2))
			val |= 4;
		if (nodemask_isset(mask, base + 3))
			val |= 8;
		if ((begin == 0) && (val == 0) && (base > 124)) {
			/* try to keep output to 32 bit mask */
			continue;
		}
		begin = 1;
		if (!ret && val)
			ret = ptr;
		*ptr++ = val_to_char(val);
	}
	*ptr = 0;
	return ret ? ret : ptr - 1;
}

static int _str_to_memset(nodemask_t *mask, const char* str)
{
	int len = strlen(str);
	const char *ptr = str + len - 1;
	int base = 0;

	/* skip 0x, it's all hex anyway */
	if (len > 1 && !memcmp(str, "0x", 2L))
		str += 2;

	nodemask_zero(mask);
	while (ptr >= str) {
		char val = char_to_val(*ptr);
		if (val == (char) -1)
			return -1;
		if (val & 1)
			nodemask_set(mask, base);
		if (val & 2)
			 nodemask_set(mask, base+1);
		if (val & 4)
			 nodemask_set(mask, base+2);
		if (val & 8)
			 nodemask_set(mask, base+3);
		len--;
		ptr--;
		base += 4;
	}

	return 0;
}

void slurm_chk_memset(nodemask_t *mask, stepd_step_rec_t *job)
{
	char bind_type[42];
	char action[42];
	char status[42];
	char mstr[1 + NUMA_NUM_NODES / 4];
	int task_gid = job->envtp->procid;
	int task_lid = job->envtp->localid;
	pid_t mypid = job->envtp->task_pid;

	if (!(job->mem_bind_type & MEM_BIND_VERBOSE))
		return;

	action[0] = '\0';
	status[0] = '\0';

	if (job->mem_bind_type & MEM_BIND_NONE) {
		strcpy(action, "");
		strcpy(bind_type, "=NONE");
	} else {
		strcpy(action, " set");
		if (job->mem_bind_type & MEM_BIND_RANK) {
			strcpy(bind_type, "=RANK");
		} else if (job->mem_bind_type & MEM_BIND_LOCAL) {
			strcpy(bind_type, "=LOC ");
		} else if (job->mem_bind_type & MEM_BIND_MAP) {
			strcpy(bind_type, "=MAP ");
		} else if (job->mem_bind_type & MEM_BIND_MASK) {
			strcpy(bind_type, "=MASK");
		} else if (job->mem_bind_type & (~MEM_BIND_VERBOSE)) {
			strcpy(bind_type, "=UNK ");
		} else {
			strcpy(action, "");
			strcpy(bind_type, "=NULL");
		}
	}

	fprintf(stderr, "mem_bind%s - "
			"%s, task %2u %2u [%u]: mask 0x%s%s%s\n",
			bind_type,
			conf->hostname,
			task_gid,
			task_lid,
			mypid,
			_memset_to_str(mask, mstr),
			action,
			status);
}

int get_memset(nodemask_t *mask, stepd_step_rec_t *job)
{
	int nummasks, i, threads;
	char *curstr, *selstr;
	char mstr[1 + NUMA_NUM_NODES / 4];
	int local_id = job->envtp->localid;

	debug3("get_memset (%d) %s", job->mem_bind_type, job->mem_bind);
	if (job->mem_bind_type & MEM_BIND_LOCAL) {
		*mask = numa_get_run_node_mask();
		return true;
	}

	nodemask_zero(mask);
	if (job->mem_bind_type & MEM_BIND_NONE) {
		return true;
	}

	if (job->mem_bind_type & MEM_BIND_RANK) {
		threads = MAX(conf->threads, 1);
		nodemask_set(mask, job->envtp->localid % (job->cpus*threads));
		return true;
	}

	if (!job->mem_bind)
		return false;

	nummasks = 1;
	selstr = NULL;

	/* get number of strings present in mem_bind */
	curstr = job->mem_bind;
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
		i = local_id % nummasks;
		curstr = job->mem_bind;
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
	while (*selstr && *selstr != ',' && i++ < (NUMA_NUM_NODES/4))
		*curstr++ = *selstr++;
	*curstr = '\0';

	if (job->mem_bind_type & MEM_BIND_MASK) {
		/* convert mask string into nodemask_t mask */
		if (_str_to_memset(mask, mstr) < 0) {
			error("_str_to_memset %s", mstr);
			return false;
		}
		return true;
	}

	if (job->mem_bind_type & MEM_BIND_MAP) {
		unsigned int my_node = 0;
		if (strncmp(mstr, "0x", 2) == 0) {
			my_node = strtoul (&(mstr[2]), NULL, 16);
		} else {
			my_node = strtoul (mstr, NULL, 10);
		}
		nodemask_set(mask, my_node);
		return true;
	}

	return false;
}


static uint16_t *numa_array = NULL;

/* helper function */
static void _add_numa_mask_to_array(unsigned long *cpu_mask, int size,
					uint16_t maxcpus, uint16_t nnode_id)
{
	unsigned long count = 1;
	int i, j, x = sizeof(unsigned long) * 8;
	for (i = 0; i < size; i++) {
		/* iterate over each bit of this unsigned long */
		for (j = 0, count = 1; j < x; j++, count *= 2) {
			if (count & cpu_mask[i]) {
				/* this bit in the cpu_mask is set */
				int cpu = i * sizeof(unsigned long) + j;
				if (cpu < maxcpus) {
					numa_array[cpu] = nnode_id;
				}
			}
		}
	}
}

/* return the numa node for the given cpuid */
extern uint16_t slurm_get_numa_node(uint16_t cpuid)
{
	uint16_t maxcpus = 0, nnid = 0;
	int size, retry, max_node;
	unsigned long *cpu_mask;

	maxcpus = conf->sockets * conf->cores * conf->threads;
	if (cpuid >= maxcpus)
		return 0;

	if (numa_array) {
		return numa_array[cpuid];
	}

	/* need to load the numa_array */
	max_node = numa_max_node();

	/* The required size of the mask buffer for numa_node_to_cpus()
	 * is goofed up. The third argument is supposed to be the size
	 * of the mask, which is an array of unsigned longs. The *unit*
	 * of the third argument is unclear - should it be in bytes or
	 * in unsigned longs??? Since I don't know, I'm using this retry
	 * loop to try and determine an acceptable size. If anyone can
	 * fix this interaction, please do!!
	 */
	size = 8;
	cpu_mask = xmalloc(sizeof(unsigned long) * size);
	retry = 0;
	while (retry++ < 8 && numa_node_to_cpus(nnid, cpu_mask, size) < 0) {
		size *= 2;
		xrealloc(cpu_mask, sizeof(unsigned long) * size);
	}
	if (retry >= 8) {
		xfree(cpu_mask);
		error("NUMA problem with numa_node_to_cpus arguments");
		return 0;
	}
	numa_array = xmalloc(sizeof(uint16_t) * maxcpus);
	_add_numa_mask_to_array(cpu_mask, size, maxcpus, nnid);
	while (nnid++ < max_node) {
		if (numa_node_to_cpus(nnid, cpu_mask, size) < 0) {
			error("NUMA problem - numa_node_to_cpus 2nd call fail");
			xfree(cpu_mask);
			xfree(numa_array);
			numa_array = NULL;
			return 0;
		}
		_add_numa_mask_to_array(cpu_mask, size, maxcpus, nnid);
	}
	xfree(cpu_mask);
	return numa_array[cpuid];
}

#endif	/* HAVE_NUMA */
