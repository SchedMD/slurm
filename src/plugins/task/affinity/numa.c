/*****************************************************************************\
 *  src/plugins/task/affinity/numa.c - numa-based memory affinity functions
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California and
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifdef HAVE_NUMA

static uint16_t *numa_array = NULL;

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
		*ptr++ = slurm_hex_to_char(val);
	}
	*ptr = 0;
	return ret ? ret : ptr - 1;
}

static int _str_to_memset(nodemask_t *mask, const char* str, int local_id)
{
	int len = strlen(str);
	const char *ptr = str + len - 1;
	int base = 0;
	int numa_node_max = numa_max_node();

	/* skip 0x, it's all hex anyway */
	if (len > 1 && !memcmp(str, "0x", 2L))
		str += 2;

	nodemask_zero(mask);
	while (ptr >= str) {
		char val = slurm_char_to_hex(*ptr);
		int err_base = -1;
		if (val == (char) -1) {
			error("Failed to convert hex string 0x%s into hex for local task %d (--mem-bind=mask_mem)",
			      str, local_id);
			return -1;
		}
		if ((val & 1) && (base > numa_node_max))
			err_base = base;
		else if ((val & 2) && ((base + 1) > numa_node_max))
			err_base = base + 1;
		else if ((val & 4) && ((base + 2) > numa_node_max))
			err_base = base + 2;
		else if ((val & 8) && ((base + 3) > numa_node_max))
			err_base = base + 3;

		if (err_base != -1) {
			error("NUMA node %d does not exist; cannot bind local task %d to it (--mem-bind=mask_mem; 0x%s)",
			      err_base, local_id, str);
			return -1;
		}

		if (val & 1)
			nodemask_set(mask, base);
		if (val & 2)
			nodemask_set(mask, base+1);
		if (val & 4)
			nodemask_set(mask, base+2);
		if (val & 8)
			nodemask_set(mask, base+3);
		ptr--;
		base += 4;
	}

	return 0;
}

void slurm_chk_memset(nodemask_t *mask, stepd_step_rec_t *step)
{
	char *action, *bind_type, *mode;
	char mstr[1 + NUMA_NUM_NODES / 4];
	int task_gid = step->envtp->procid;
	int task_lid = step->envtp->localid;
	pid_t mypid = step->envtp->task_pid;

	if (!(step->mem_bind_type & MEM_BIND_VERBOSE))
		return;

	if (step->mem_bind_type & MEM_BIND_NONE) {
		mode = "=";
		action = "";
		bind_type = "NONE";
	} else {
		action = " set";
		if (step->mem_bind_type & MEM_BIND_PREFER)
			mode = " PREFER ";
		else
			mode = "=";
		if (step->mem_bind_type & MEM_BIND_RANK) {
			bind_type = "RANK";
		} else if (step->mem_bind_type & MEM_BIND_LOCAL) {
			bind_type = "LOC";
		} else if (step->mem_bind_type & MEM_BIND_MAP) {
			bind_type = "MAP";
		} else if (step->mem_bind_type & MEM_BIND_MASK) {
			bind_type = "MASK";
		} else if (step->mem_bind_type & (~MEM_BIND_VERBOSE)) {
			bind_type = "UNK";
		} else {
			action = "";
			bind_type = "NULL";
		}
	}

	fprintf(stderr, "mem-bind%s%s - "
			"%s, task %2u %2u [%u]: mask 0x%s%s\n",
			mode, bind_type,
			conf->hostname,
			task_gid,
			task_lid,
			mypid,
			_memset_to_str(mask, mstr),
			action);
}

int get_memset(nodemask_t *mask, stepd_step_rec_t *step)
{
	int nummasks, i, threads;
	char *curstr, *selstr;
	char mstr[1 + NUMA_NUM_NODES / 4];
	int local_id = step->envtp->localid;

	debug3("get_memset (%d) %s", step->mem_bind_type, step->mem_bind);
	if (step->mem_bind_type & MEM_BIND_LOCAL) {
		*mask = numa_get_run_node_mask();
		return true;
	}

	nodemask_zero(mask);

	if (step->mem_bind_type & MEM_BIND_RANK) {
		int node;
		threads = MAX(conf->threads, 1);
		node = local_id % (step->cpus * threads);
		if (node > numa_max_node()) {
			error("NUMA node %d does not exist; cannot bind local task %d to it (--mem-bind=rank)",
			      node, local_id);
			return false;
		}

		nodemask_set(mask, node);
		return true;
	}

	if (!step->mem_bind) {
		error("--mem-bind value is empty for local task %d", local_id);
		return false;
	}

	nummasks = 1;
	selstr = NULL;

	/* get number of strings present in mem_bind */
	curstr = step->mem_bind;
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
		curstr = step->mem_bind;
		while (*curstr && i) {
			if (*curstr == ',')
			    	i--;
			curstr++;
		}
		if (!*curstr) {
			error("--mem-bind value '%s' is malformed for local task %d",
			      step->mem_bind, local_id);
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

	if (step->mem_bind_type & MEM_BIND_MASK) {
		/* convert mask string into nodemask_t mask */
		if (_str_to_memset(mask, mstr, local_id) < 0) {
			return false;
		} else {
			/* Check that at least one NUMA node is specified */
			nodemask_t tmp;
			nodemask_zero(&tmp);
			if (nodemask_equal(mask, &tmp)) {
				error("NUMA node mask is NULL (0x0). Must bind at least one NUMA node to local task %d (--mem-bind=mask_mem)",
				      local_id);
				return false;
			}
		}
		return true;
	}

	if (step->mem_bind_type & MEM_BIND_MAP) {
		long int my_node = 0;
		char *end_ptr = NULL;
		slurm_seterrno(0);
		if (xstrncmp(mstr, "0x", 2) == 0) {
			my_node = strtol(&(mstr[2]), &end_ptr, 16);
		} else {
			my_node = strtol(mstr, &end_ptr, 10);
		}
		if (slurm_get_errno()) {
			error("--mem-bind=map_mem:%s failed to parse into valid NUMA nodes for local task %d: %m",
			      mstr, local_id);
			return false;
		} else if (end_ptr && (mstr[0] != '\0') && (end_ptr[0] != '\0')) {
			/* i.e. the string was not all parsable into digits */
			error("--mem-bind=map_mem:%s contained non-numeric values for local task %d",
			      mstr, local_id);
			return false;
		}
		if ((my_node < 0) || (my_node > (long int)numa_max_node())) {
			error("NUMA node %ld does not exist; cannot bind local task %d to it (--mem-bind=map_mem)",
			      my_node, local_id);
			return false;
		}
		nodemask_set(mask, (int)my_node);
		return true;
	}

	error("Unhandled --mem-bind option for local task %d", local_id);
	return false;
}


/* return the numa node for the given cpuid */
extern uint16_t slurm_get_numa_node(uint16_t cpuid)
{
	uint16_t maxcpus = 0;
	int nnid, j, max_node;
	struct bitmask *collective;

	if (numa_array)
		return numa_array[cpuid];

	maxcpus = conf->sockets * conf->cores * conf->threads;

	if (cpuid >= maxcpus)
		return 0;

	/* need to load the numa_array */
	max_node = numa_max_node();
	numa_array = xmalloc(sizeof(uint16_t) * maxcpus);

	collective = numa_allocate_cpumask();
	if (maxcpus > collective->size) {
		error("Size mismatch!!!! %d %lu",
		      maxcpus, collective->size);
		numa_free_cpumask(collective);
		return 0;
	}

	for (nnid = 0; nnid <= max_node; nnid++) {
		/* FIXME: This is a hack to make it work like NUMA v2, but for
		 * the time being we are stuck on v1. (numa_node_to_cpus will
		 * multiple the size by 8 and the collective is already at the
		 * correct size)
		 */
		if (numa_node_to_cpus(nnid, collective->maskp,
				      collective->size / 8)) {
			error("numa_node_to_cpus: %m");
			numa_free_cpumask(collective);
			return 0;
		}
		for (j = 0; j < maxcpus; j++)
			if (numa_bitmask_isbitset(collective, j))
				numa_array[j] = nnid;
	}

	numa_free_cpumask(collective);
	return numa_array[cpuid];
}

#endif	/* HAVE_NUMA */
