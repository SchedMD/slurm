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
	char bind_type[42];
	char action[42];
	char status[42];
	char mstr[1 + CPU_SETSIZE / 4];
	int task_gid = job->envtp->procid;
	int task_lid = job->envtp->localid;
	pid_t mypid = job->envtp->task_pid;

	if (!(job->cpu_bind_type & CPU_BIND_VERBOSE)) return;

	action[0] = '\0';
	status[0] = '\0';
	if (statval) { strcpy(status, " FAILED"); }

	if (job->cpu_bind_type & CPU_BIND_NONE) {
		strcpy(action, "");
		strcpy(bind_type, "=NONE");
	} else {
		strcpy(action, " set");
		if (job->cpu_bind_type & CPU_BIND_RANK) {
			strcpy(bind_type, "=RANK");
		} else if (job->cpu_bind_type & CPU_BIND_MAP) {
			strcpy(bind_type, "=MAP ");
		} else if (job->cpu_bind_type & CPU_BIND_MASK) {
			strcpy(bind_type, "=MASK");
		} else if (job->cpu_bind_type & (~CPU_BIND_VERBOSE)) {
			strcpy(bind_type, "=UNK ");
		} else {
			strcpy(action, "");
			strcpy(bind_type, "=NULL");
		}
	}

	fprintf(stderr, "cpu_bind%s - "
			"%s, task %2u %2u [%u]: mask 0x%s%s%s\n",
			bind_type,
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

#ifdef HAVE_CPUSETS_EXP
int get_cpuset_mask(cs_cpumask_t *mask, slurmd_job_t *job)
{
	int nbr_task_cpus;
	int nummasks, i, j;
	char *curstr, *selstr;
	char mstr[1 + CPU_SETSIZE / 4];
	int local_id = job->envtp->localid;

	debug2("get_cpuset_mask bind_type = %d, bind_list = %s\n", 
		job->cpu_bind_type, job->cpu_bind);

	if ((!job->cpu_bind_type) || (job->cpu_bind_type & CPU_BIND_NONE)) {
		return false;
	}

	nbr_task_cpus = job->cpus / job->ntasks;

	if (job->cpu_bind_type & CPU_BIND_RANK) {
		for (i=0;i<nbr_task_cpus;i++){
			cs_cpumask_add(mask, local_id * nbr_task_cpus + i);
		}
		return true;
	}

	nummasks = 0;
	selstr = NULL;

	if (job->cpu_bind_type & CPU_BIND_MAPCPU) {
		unsigned int mycpu = 0;
		/* find first entry for this task */
		curstr = job->cpu_bind;
		while (*curstr) {
			if (nummasks == local_id*nbr_task_cpus) {
				selstr = curstr;
				break;
			}
			if (*curstr == ',')
				nummasks++;
			curstr++;
		}
		/* check if we found a cpu entry */
		if (!selstr){
			error("not enough entries in map_cpu:<list>");
			return false;
		}
		/* add cpus to mask from list for "nbr_task_cpus" */
		for (i=0;i<nbr_task_cpus;i++){
			if (!*selstr){
				error("not enough entries in map_cpu:<list>");
				return false;
			}
			/* extract the selected mask from the list */
			j = 0;
			curstr = mstr;
			while ((*selstr && *selstr != ',') && (j++ < (CPU_SETSIZE/4)))
				*curstr++ = *selstr++;
			*curstr = '\0';
			if (*selstr == ',')
				selstr++;
			if (strncmp(mstr, "0x", 2) == 0) {
				mycpu = strtoul (&(mstr[2]), NULL, 16);
			} else {
				mycpu = strtoul (mstr, NULL, 10);
			}
			cs_cpumask_add(mask, mycpu);
		}
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_MASKCPU) {
		/* find mask entry for this task */
		curstr = job->cpu_bind;
		while (*curstr) {
			if (nummasks == local_id) {
				selstr = curstr;
				break;
			}
			if (*curstr == ',')
			        nummasks++;
			curstr++;
		}
		/* check if we found a cpu entry */
		if (!selstr){
			error("not enough entries in mask_cpu:<list>");
			return false;
		}
		/* extract the selected mask from the list */
		j = 0;
		curstr = mstr;
		while ((*selstr && *selstr != ',') && (j++ < (CPU_SETSIZE/4)))
			*curstr++ = *selstr++;
		*curstr = '\0';
		/* convert mask string into cpu_set_t mask */
		if (str_to_cpuset( (cpu_set_t *) mask, mstr) < 0) {
			error("str_to_cpuset %s", mstr);
			return false;
		}
		return true;
	}

	return false;
}

int get_memset_mask(cs_memmask_t *mem_mask, cs_cpumask_t *cpu_mask, slurmd_job_t *job)
{
	cs_cpumask_t cur_mask, tst_mask;
	int nbr_nodes, nummasks, i, j;
	char *curstr, *selstr;
	char mstr[1 + CPU_SETSIZE / 4];
	int local_id = job->envtp->localid;

	debug2("get_memset_mask bind_type = %d, bind_list = %s\n", 
		job->mem_bind_type, job->mem_bind);

	/* If "not specified" or "None" or "Rank", do not set a new memory mask in the CPUset  */
	if ((!job->mem_bind_type) || (job->mem_bind_type & (MEM_BIND_NONE | MEM_BIND_RANK))) 
		return false;

	/* For now, make LOCAL and MAP_CPU the same */
	if (job->mem_bind_type & (MEM_BIND_LOCAL | MEM_BIND_MAPCPU)) {
		nbr_nodes = cs_nr_nodes();
		for (i=0; i<nbr_nodes; i++) {
			cs_get_node_cpus(i, &cur_mask);
			cs_cpumask_and(&tst_mask, &cur_mask, cpu_mask);
			if (!cs_cpumask_empty(&tst_mask)) {
				cs_memmask_add(mem_mask, i);
				debug2("added node = %d to mem mask %08x \n",i,*mem_mask);
			}
		}
		
		return true;
	}
	    
	/* allow user to set specific memory masks */
	if (job->mem_bind_type & MEM_BIND_MASKCPU) {
		/* find mask entry for this task */
		nummasks = 0;
		selstr = NULL;

		curstr = job->mem_bind;
		while (*curstr) {
			if (nummasks == local_id) {
				selstr = curstr;
				break;
			}
			if (*curstr == ',')
			        nummasks++;
			curstr++;
		}
		/* check if we found a mem entry */
		if (!selstr){
			error("not enough entries in mask_mem:<list>");
			return false;
		}
		/* extract the selected mask from the list */
		j = 0;
		curstr = mstr;
		while ((*selstr && *selstr != ',') && (j++ < (CPU_SETSIZE/4)))
			*curstr++ = *selstr++;
		*curstr = '\0';
		/* convert mask string into cpu_set_t mask */
		if (str_to_cpuset( (cpu_set_t *) mem_mask, mstr) < 0) {
			error("str_to_cpuset %s", mstr);
			return false;
		}
		return true;
	}
	return false;
}

int make_task_cpuset(slurmd_job_t *job, cs_cpumask_t *cpu_mask, cs_memmask_t *mem_mask){

	char path[PATH_MAX];
	char *current_cs = NULL;
	int retval = 0;
	int success = 0;

	info("cpuset - cs_init called");
	cs_init();

	current_cs = cs_get_current();
	if (!current_cs)
		return -ENOMEM;

	int l = snprintf(path, PATH_MAX, "%sslurm%u_%d", current_cs, job->jobid, 
			 job->envtp->localid);
	if (l > PATH_MAX) {
		retval = -ENAMETOOLONG;
		goto out;
	}

	debug("cpuset path = %s",path);

	retval = cs_create(path);
	if (retval < 0)
		goto out;
	retval = cs_set_autoclean(path, CS_AUTOCLEAN);
	if (retval < 0)
		goto out;

	cs_lock_libcpuset();
	retval = cs_set_cpus(path, *cpu_mask);
	if (retval < 0)
		debug3("cpuset - error on cs_set_cpus = %d %s",retval,cs_strerror(retval));
	success = 1;

	if (cs_supports_mem()) {

		/* Check for mem_bind options */
		if (get_memset_mask(mem_mask, cpu_mask, job)) {
			debug("cpuset - mem_mask = %d (decimal) and %08x (hex)",
				*mem_mask, *mem_mask);
			retval = cs_set_mems(path, *mem_mask);
			if (retval < 0) {
				debug3("cpuset - error on cs_set_mems = %d %s",
					retval,cs_strerror(retval));
				success = 0;
				goto out_created;
			}
		} else {
			/* Copy parent of new cpuset (i.e current) mems mask */
			retval = cs_get_mems(current_cs, mem_mask);
			if (retval < 0) {
				debug3("cpuset - error on cs_get_mems = %d %s",
					retval,cs_strerror(retval));
				success = 0;
				goto out_created;
			}
			retval = cs_set_mems(path, *mem_mask);
			if (retval < 0) {
				debug3("cpuset - error on cs_set_mems = %d %s",
					retval,cs_strerror(retval));
				success = 0;
				goto out_created;
			}
		}
	}

	retval = cs_add_task(path, job->envtp->task_pid);
	if (retval < 0) {
		debug3("cpuset - error on cs_add_task = %d %s",retval,cs_strerror(retval));
	}

 out_created:
	if (!success)
		cs_destroy(path);
	cs_unlock_libcpuset();

 out:
	free(current_cs);

	current_cs = cs_get_current();
	debug("cpuset - exit make_task_cpuset retval = %d cpuset = %s",retval,current_cs);
	free(current_cs);

	return retval;
}
#endif
