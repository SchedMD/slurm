/*****************************************************************************\
 *  src/plugins/task/affinity/affinity.c - task affinity plugin
 *  $Id: affinity.c,v 1.2 2005/11/04 02:46:51 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2005-2006 Hewlett-Packard Development Company, L.P.
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

#ifdef HAVE_PLPA
#  include <plpa.h>
#endif

void slurm_chkaffinity(cpu_set_t *mask, slurmd_job_t *job, int statval)
{
	char *bind_type, *action, *status, *units;
	char mstr[1 + CPU_SETSIZE / 4];
	int task_gid = job->envtp->procid;
	int task_lid = job->envtp->localid;
	pid_t mypid = job->envtp->task_pid;

	if (!(job->cpu_bind_type & CPU_BIND_VERBOSE))
		return;

	if (statval)
		status = " FAILED";
	else
		status = "";

	if (job->cpu_bind_type & CPU_BIND_NONE) {
		action = "";
		units  = "";
		bind_type = "NONE";
	} else {
		action = " set";
		if (job->cpu_bind_type & CPU_BIND_TO_THREADS)
			units = "_threads";
		else if (job->cpu_bind_type & CPU_BIND_TO_CORES)
			units = "_cores";
		else if (job->cpu_bind_type & CPU_BIND_TO_SOCKETS)
			units = "_sockets";
		else
			units = "";
		if (job->cpu_bind_type & CPU_BIND_RANK) {
			bind_type = "RANK";
		} else if (job->cpu_bind_type & CPU_BIND_MAP) {
			bind_type = "MAP ";
		} else if (job->cpu_bind_type & CPU_BIND_MASK) {
			bind_type = "MASK";
		} else if (job->cpu_bind_type & (~CPU_BIND_VERBOSE)) {
			bind_type = "UNK ";
		} else {
			action = "";
			bind_type = "NULL";
		}
	}

	fprintf(stderr, "cpu_bind%s=%s - "
			"%s, task %2u %2u [%u]: mask 0x%s%s%s\n",
			units, bind_type,
			conf->hostname,
			task_gid,
			task_lid,
			mypid,
			cpuset_to_str(mask, mstr),
			action,
			status);
}

int get_cpuset(cpu_set_t *mask, slurmd_job_t *job)
{
	int nummasks, maskid, i;
	char *curstr, *selstr;
	char mstr[1 + CPU_SETSIZE / 4];
	int local_id = job->envtp->localid;
	char buftype[1024];

	slurm_sprint_cpu_bind_type(buftype, job->cpu_bind_type);
	debug3("get_cpuset (%s[%d]) %s\n", buftype, job->cpu_bind_type, 
		job->cpu_bind);
	CPU_ZERO(mask);

	if (job->cpu_bind_type & CPU_BIND_NONE) {
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_RANK) {
		CPU_SET(job->envtp->localid % job->cpus, mask);
		return true;
	}

	if (!job->cpu_bind)
		return false;

	nummasks = 1;
	maskid = 0;
	selstr = NULL;

	/* get number of strings present in cpu_bind */
	curstr = job->cpu_bind;
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
		if (str_to_cpuset(mask, mstr) < 0) {
			error("str_to_cpuset %s", mstr);
			return false;
		}
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_MAP) {
		unsigned int mycpu = 0;
		if (strncmp(mstr, "0x", 2) == 0) {
			mycpu = strtoul (&(mstr[2]), NULL, 16);
		} else {
			mycpu = strtoul (mstr, NULL, 10);
		}
		CPU_SET(mycpu, mask);
		return true;
	}

	return false;
}

int slurm_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask)
{
	int rval;
	char mstr[1 + CPU_SETSIZE / 4];

#ifdef HAVE_PLPA
	rval = plpa_sched_setaffinity(pid, size, (plpa_cpu_set_t *) mask);
#else
#  ifdef SCHED_GETAFFINITY_THREE_ARGS
	rval = sched_setaffinity(pid, size, mask);
#  else
	rval = sched_setaffinity(pid, mask);
#  endif
#endif
	if (rval)
		verbose("sched_setaffinity(%d,%d,0x%s) failed with status %d",
				pid, size, cpuset_to_str(mask, mstr), rval);
	return (rval);
}

int slurm_getaffinity(pid_t pid, size_t size, cpu_set_t *mask)
{
	int rval;
	char mstr[1 + CPU_SETSIZE / 4];

	CPU_ZERO(mask);
#ifdef HAVE_PLPA
	rval = plpa_sched_getaffinity(pid, size, (plpa_cpu_set_t *) mask);
#else
#  ifdef SCHED_GETAFFINITY_THREE_ARGS
	rval = sched_getaffinity(pid, size, mask);
#  else
	rval = sched_getaffinity(pid, mask);
#  endif
#endif
	if (rval)
		verbose("sched_getaffinity(%d,%d,0x%s) failed with status %d",
				pid, size, cpuset_to_str(mask, mstr), rval);

	debug3("sched_getaffinity(%d) = 0x%s", pid, cpuset_to_str(mask, mstr));
	return (rval);
}
