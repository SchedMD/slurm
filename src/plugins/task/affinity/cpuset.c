/*****************************************************************************\
 *  cpuset.c - Library for interacting with /dev/cpuset file system
 *****************************************************************************
 *  Copyright (C) 2007 Bull
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Written by Don Albert <Don.Albert@Bull.com> and 
 *             Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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

int	slurm_set_cpuset(char *path, pid_t pid, size_t size, 
		const cpu_set_t *mask)
{
	int fd, rc;
	char file_path[PATH_MAX];
	char mstr[1 + CPU_SETSIZE / 4];

	if (mkdir(path, 0700) && (errno != EEXIST)) {
		error("mkdir(%s): %m", path);
		return -1;
	}

	snprintf(file_path, sizeof(file_path), "%s/notify_on_release", path);
	fd = open(file_path, O_CREAT | O_WRONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = write(fd, "1", 2);		/* not sure if this is right */
	close(fd);

	snprintf(file_path, sizeof(file_path), "%s/cpus", path);
	cpuset_to_str(mask, mstr);
	fd = open(file_path, O_CREAT | O_WRONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = write(fd, mstr, strlen(mstr));
	close(fd);
	if (rc < 1) {
		error("write(%s): %m", file_path);
		return -1;
	}

	snprintf(file_path, sizeof(file_path), "%s/tasks", path);
	snprintf(mstr, sizeof(mstr), "%d", pid);
	fd = open(file_path, O_CREAT);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = write(fd, mstr, strlen(mstr));
	close(fd);
	if (rc < 1) {
		error("write(%s): %m", file_path);
		return -1;
	}

	return 0;
}

int	slurm_get_cpuset(char *path, pid_t pid, size_t size, cpu_set_t *mask)
{
	int fd, rc;
	char file_path[PATH_MAX];
	char mstr[1 + CPU_SETSIZE / 4];

	snprintf(file_path, sizeof(file_path), "%s/cpus", path);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("read(%s): %m", file_path);
		return -1;
	}
	str_to_cpuset(mask, mstr);

	snprintf(file_path, sizeof(file_path), "%s/tasks", path);
	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		error("open(%s): %m", file_path);
		return -1;
	}
	rc = read(fd, mstr, sizeof(mstr));
	close(fd);
	if (rc < 1) {
		error("read(%s): %m", file_path);
		return -1;
	}
	/* verify that pid is in mstr */

	return 0;
}

#if 0
int get_memset_mask(slurm_memmask_t *mem_mask, slurm_cpumask_t *cpu_mask, 
		slurmd_job_t *job)
{
	slurm_cpumask_t cur_mask, tst_mask;
	int nbr_nodes, nummasks, i, j;
	char *curstr, *selstr;
	char mstr[1 + CPU_SETSIZE / 4];
	int local_id = job->envtp->localid;

	debug2("get_memset_mask bind_type = %d, bind_list = %s\n", 
		job->mem_bind_type, job->mem_bind);

	/* If "not specified" or "None" or "Rank", 
	 * do not set a new memory mask in the CPUset  */
	if ((!job->mem_bind_type)
	|| (job->mem_bind_type & (MEM_BIND_NONE | MEM_BIND_RANK))) 
		return false;

	/* For now, make LOCAL and MAP_CPU the same */
	if (job->mem_bind_type & (MEM_BIND_LOCAL | MEM_BIND_MAP)) {
		nbr_nodes = cs_nr_nodes();
		for (i=0; i<nbr_nodes; i++) {
			cs_get_node_cpus(i, &cur_mask);
			cs_cpumask_and(&tst_mask, &cur_mask, cpu_mask);
			if (!cs_cpumask_empty(&tst_mask)) {
				cs_memmask_add(mem_mask, i);
				debug2("added node = %d to mem mask %08x \n",
					i,*mem_mask);
			}
		}
		
		return true;
	}
	    
	/* allow user to set specific memory masks */
	if (job->mem_bind_type & MEM_BIND_MASK) {
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
		while ((*selstr && *selstr != ',') && 
				(j++ < (CPU_SETSIZE/4))) {
			*curstr++ = *selstr++;
		}
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

int make_task_cpuset(slurmd_job_t *job, slurm_cpumask_t *cpu_mask, 
		slurm_memmask_t *mem_mask)
{

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
	if (retval < 0) {
		debug3("cpuset - error on cs_set_cpus = %d %s",
			retval, cs_strerror(retval));
	}
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
					retval, cs_strerror(retval));
				success = 0;
				goto out_created;
			}
		}
	}

	retval = cs_add_task(path, job->envtp->task_pid);
	if (retval < 0) {
		debug3("cpuset - error on cs_add_task = %d %s",
			retval, cs_strerror(retval));
	}

 out_created:
	if (!success)
		cs_destroy(path);
	cs_unlock_libcpuset();

 out:
	free(current_cs);

	current_cs = cs_get_current();
	debug("cpuset - exit make_task_cpuset retval = %d cpuset = %s",
		retval, current_cs);
	free(current_cs);

	return retval;
}

#endif
