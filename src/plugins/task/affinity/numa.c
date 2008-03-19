/*****************************************************************************\
 *  src/plugins/task/affinity/numa.c - numa-based memory affinity functions
 *  $Id: affinity.c,v 1.2 2005/11/04 02:46:51 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California and
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

void slurm_chk_memset(nodemask_t *mask, slurmd_job_t *job)
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

int get_memset(nodemask_t *mask, slurmd_job_t *job)
{
	int nummasks, maskid, i;
	char *curstr, *selstr;
	char mstr[1 + NUMA_NUM_NODES / 4];
	int local_id = job->envtp->localid;

	debug3("get_memset (%d) %s\n", job->mem_bind_type, job->mem_bind);
	if (job->mem_bind_type & MEM_BIND_LOCAL) {
		*mask = numa_get_run_node_mask();
		return true;
	}

	nodemask_zero(mask);
	if (job->mem_bind_type & MEM_BIND_NONE) {
		return true;
	}

	if (job->mem_bind_type & MEM_BIND_RANK) {
		nodemask_set(mask, job->envtp->localid % job->cpus);
		return true;
	}

	if (!job->mem_bind)
		return false;

	nummasks = 1;
	maskid = 0;
	selstr = NULL;

	/* get number of strings present in mem_bind */
	curstr = job->mem_bind;
	while (*curstr) {
		if (nummasks == local_id+1) {
			selstr = curstr;
			maskid = local_id;
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

#endif	/* HAVE_NUMA */
