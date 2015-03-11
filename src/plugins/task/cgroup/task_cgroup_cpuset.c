/***************************************************************************** \
 *  task_cgroup_cpuset.c - cpuset cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *  Portions copyright (C) 2012 Bull
 *  Written by Martin Perry <martin.perry@bull.com>
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

#if !(defined(__FreeBSD__) || defined(__NetBSD__))
#if HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <ctype.h>
#include <sched.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/common/cpu_frequency.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/bitstring.h"
#include "src/common/proc_args.h"
#include "src/common/xstring.h"

#include "task_cgroup.h"

#ifdef HAVE_HWLOC
#include <hwloc.h>

#if !defined(__FreeBSD__)
#include <hwloc/glibc-sched.h>
#else
// For cpuset
#include <pthread_np.h>
#define cpu_set_t cpuset_t
#endif

# if HWLOC_API_VERSION <= 0x00010000
/* After this version the cpuset structure and all it's functions
 * changed to bitmaps.  So to work with old hwloc's we just to the
 * opposite to avoid having to put a bunch of ifdef's in the code we
 * just do it here.
 */
typedef hwloc_cpuset_t hwloc_bitmap_t;
typedef hwloc_const_cpuset_t hwloc_const_bitmap_t;

static inline hwloc_bitmap_t hwloc_bitmap_alloc(void)
{
	return hwloc_cpuset_alloc();
}

static inline void hwloc_bitmap_free(hwloc_bitmap_t bitmap)
{
	hwloc_cpuset_free(bitmap);
}

static inline void hwloc_bitmap_or(
	hwloc_bitmap_t res, hwloc_bitmap_t bitmap1, hwloc_bitmap_t bitmap2)
{
	hwloc_cpuset_or(res, bitmap1, bitmap2);
}

static inline int hwloc_bitmap_asprintf(char **str, hwloc_bitmap_t bitmap)
{
	return hwloc_cpuset_asprintf(str, bitmap);
}

static inline int hwloc_bitmap_isequal(
	hwloc_const_bitmap_t bitmap1, hwloc_const_bitmap_t bitmap2)
{
	return hwloc_cpuset_isequal(bitmap1, bitmap2);
}

# endif

static uint16_t bind_mode = CPU_BIND_NONE   | CPU_BIND_MASK   |
			    CPU_BIND_RANK   | CPU_BIND_MAP    |
			    CPU_BIND_LDMASK | CPU_BIND_LDRANK |
			    CPU_BIND_LDMAP;
static uint16_t bind_mode_ldom =
			    CPU_BIND_LDMASK | CPU_BIND_LDRANK |
			    CPU_BIND_LDMAP;

#endif

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

static bool cpuset_prefix_set = false;
static char *cpuset_prefix = "";

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];

static xcgroup_ns_t cpuset_ns;

static xcgroup_t user_cpuset_cg;
static xcgroup_t job_cpuset_cg;
static xcgroup_t step_cpuset_cg;

static int _xcgroup_cpuset_init(xcgroup_t* cg);
char * cpuset_to_str(const cpu_set_t *mask, char *str);
int str_to_cpuset(cpu_set_t *mask, const char* str);

inline int val_to_char(int v)
{
	if (v >= 0 && v < 10)
		return '0' + v;
	else if (v >= 10 && v < 16)
		return ('a' - 10) + v;
	else
		return -1;
}

inline int char_to_val(int c)
{
	int cl;

	cl = tolower(c);
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (cl >= 'a' && cl <= 'f')
		return cl + (10 - 'a');
	else
		return -1;
}

char *cpuset_to_str(const cpu_set_t *mask, char *str)
{
	int base;
	char *ptr = str;
	char *ret = NULL;

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
		if (!ret && val)
			ret = ptr;
		*ptr++ = val_to_char(val);
	}
	*ptr = '\0';
	return ret ? ret : ptr - 1;
}

int str_to_cpuset(cpu_set_t *mask, const char* str)
{
	int len = strlen(str);
	const char *ptr = str + len - 1;
	int base = 0;

	/* skip 0x, it's all hex anyway */
	if (len > 1 && !memcmp(str, "0x", 2L))
		str += 2;

	CPU_ZERO(mask);
	while (ptr >= str) {
		char val = char_to_val(*ptr);
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
		len--;
		ptr--;
		base += 4;
	}

	return 0;
}

/* when cgroups are configured with cpuset, at least
 * cpuset.cpus and cpuset.mems must be set or the cgroup
 * will not be available at all.
 * we duplicate the ancestor configuration in the init step */
static int _xcgroup_cpuset_init(xcgroup_t* cg)
{
	int fstatus,i;

	char* cpuset_metafiles[] = {
		"cpus",
		"mems"
	};
	char cpuset_meta[PATH_MAX];
	char* cpuset_conf;
	size_t csize;

	xcgroup_t acg;
	char* acg_name;
	char* p;

	fstatus = XCGROUP_ERROR;

	/* load ancestor cg */
	acg_name = (char*) xstrdup(cg->name);
	p = rindex(acg_name,'/');
	if (p == NULL) {
		debug2("task/cgroup: unable to get ancestor path for "
		       "cpuset cg '%s' : %m",cg->path);
		return fstatus;
	} else
		*p = '\0';
	if (xcgroup_load(cg->ns,&acg, acg_name) != XCGROUP_SUCCESS) {
		debug2("task/cgroup: unable to load ancestor for "
		       "cpuset cg '%s' : %m",cg->path);
		return fstatus;
	}

	/* inherits ancestor params */
	for (i = 0 ; i < 2 ; i++) {
	again:
		snprintf(cpuset_meta, sizeof(cpuset_meta), "%s%s",
			 cpuset_prefix, cpuset_metafiles[i]);
		if (xcgroup_get_param(&acg,cpuset_meta,
				      &cpuset_conf,&csize)
		    != XCGROUP_SUCCESS) {
			if (!cpuset_prefix_set) {
				cpuset_prefix_set = 1;
				cpuset_prefix = "cpuset.";
				goto again;
			}

			debug("task/cgroup: assuming no cpuset cg "
			       "support for '%s'",acg.path);
			xcgroup_destroy(&acg);
			return fstatus;
		}
		if (csize > 0)
			cpuset_conf[csize-1]='\0';
		if (xcgroup_set_param(cg,cpuset_meta,cpuset_conf)
		    != XCGROUP_SUCCESS) {
			debug("task/cgroup: unable to write %s configuration "
			       "(%s) for cpuset cg '%s'",cpuset_meta,
			       cpuset_conf,cg->path);
			xcgroup_destroy(&acg);
			xfree(cpuset_conf);
			return fstatus;
		}
		xfree(cpuset_conf);
	}

	xcgroup_destroy(&acg);
	return XCGROUP_SUCCESS;
}

void slurm_chkaffinity(cpu_set_t *mask, stepd_step_rec_t *job, int statval)
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
		else if (job->cpu_bind_type & CPU_BIND_TO_LDOMS)
			units = "_ldoms";
		else if (job->cpu_bind_type & CPU_BIND_TO_BOARDS)
			units = "_boards";
		else
			units = "";
		if (job->cpu_bind_type & CPU_BIND_RANK) {
			bind_type = "RANK";
		} else if (job->cpu_bind_type & CPU_BIND_MAP) {
			bind_type = "MAP ";
		} else if (job->cpu_bind_type & CPU_BIND_MASK) {
			bind_type = "MASK";
		} else if (job->cpu_bind_type & CPU_BIND_LDRANK) {
			bind_type = "LDRANK";
		} else if (job->cpu_bind_type & CPU_BIND_LDMAP) {
			bind_type = "LDMAP ";
		} else if (job->cpu_bind_type & CPU_BIND_LDMASK) {
			bind_type = "LDMASK";
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

#ifdef HAVE_HWLOC
/*
 * Get sched cpuset for ldom
 *
 * in:  topology   = hwloc topology
 *      hwtype     = actual hardware type
 *      req_hwtype = requested hardware type
 *      ldom       = ldom#
 * out: mask       = pointer to sched cpuset
 */
static int _get_ldom_sched_cpuset(
		hwloc_topology_t topology,
		hwloc_obj_type_t hwtype, hwloc_obj_type_t req_hwtype,
		uint32_t ldom, cpu_set_t *mask);

/*
 * Get sched cpuset based on cpu_bind_type
 *
 * in:  topology   = hwloc topology
 *      hwtype     = actual hardware type
 *      req_hwtype = requested hardware type
 *      job        = pointer to job/step record
 * out: mask       = pointer to sched cpuset
 */
static int _get_sched_cpuset(
		hwloc_topology_t topology,
		hwloc_obj_type_t hwtype, hwloc_obj_type_t req_hwtype,
		cpu_set_t *mask, stepd_step_rec_t *job);

/*
 * Add hwloc cpuset for a hwloc object to the total cpuset for a task, using
 * the appropriate ancestor object cpuset if necessary
 *
 * in:  obj    = object to add
 * out: cpuset = hwloc cpuset for task
 */
static void _add_hwloc_cpuset(
		hwloc_obj_type_t hwtype, hwloc_obj_type_t req_hwtype,
		hwloc_obj_t obj, uint32_t taskid,  int bind_verbose,
		hwloc_bitmap_t cpuset);

/*
 * Distribute cpus to task using cyclic distribution across sockets
 *
 * in:  topology     = hwloc topology
 *      hwtype       = actual hardware type
 *      req_hwtype   = requested hardware type
 *      job          = pointer to job/step record
 *      bind_verbose = verbose option
 * out: cpuset       = hwloc cpuset for task
 */
static int _task_cgroup_cpuset_dist_cyclic(
	hwloc_topology_t topology, hwloc_obj_type_t hwtype,
	hwloc_obj_type_t req_hwtype, stepd_step_rec_t *job, int bind_verbose,
	hwloc_bitmap_t cpuset);

/*
 * Distribute cpus to task using block distribution
 *
 * in:  topology     = hwloc topology
 *      hwtype       = actual hardware type
 *      req_hwtype   = requested hardware type
 *      job          = pointer to job/step record
 *      bind_verbose = verbose option
 * out: cpuset       = hwloc cpuset for task
 */
static int _task_cgroup_cpuset_dist_block(
	hwloc_topology_t topology, hwloc_obj_type_t hwtype,
	hwloc_obj_type_t req_hwtype, uint32_t nobj,
	stepd_step_rec_t *job, int bind_verbose, hwloc_bitmap_t cpuset);

/* The job has specialized cores, synchronize user mask with available cores */
static void _validate_mask(uint32_t task_id, hwloc_obj_t obj, cpu_set_t *ts);


static int _get_ldom_sched_cpuset(hwloc_topology_t topology,
		hwloc_obj_type_t hwtype, hwloc_obj_type_t req_hwtype,
		uint32_t ldom, cpu_set_t *mask)
{
	hwloc_obj_t obj;
	hwloc_bitmap_t cpuset;
	int hwdepth;

	cpuset = hwloc_bitmap_alloc();
	hwdepth = hwloc_get_type_depth(topology, hwtype);
	obj = hwloc_get_obj_by_depth(topology, hwdepth, ldom);
	_add_hwloc_cpuset(hwtype, req_hwtype, obj, 0, 0, cpuset);
	hwloc_cpuset_to_glibc_sched_affinity(topology, cpuset,
						 mask, sizeof(cpu_set_t));
	hwloc_bitmap_free(cpuset);
	return true;
}

int _get_sched_cpuset(hwloc_topology_t topology,
		hwloc_obj_type_t hwtype, hwloc_obj_type_t req_hwtype,
		cpu_set_t *mask, stepd_step_rec_t *job)
{
	int nummasks, maskid, i, threads;
	char *curstr, *selstr;
	char mstr[1 + CPU_SETSIZE / 4];
	uint32_t local_id = job->envtp->localid;
	char buftype[1024];

	/* For CPU_BIND_RANK, CPU_BIND_MASK and CPU_BIND_MAP, generate sched
	 * cpuset directly from cpu numbers.
	 * For CPU_BIND_LDRANK, CPU_BIND_LDMASK and CPU_BIND_LDMAP, generate
	 * sched cpuset from hwloc topology.
	 */
	slurm_sprint_cpu_bind_type(buftype, job->cpu_bind_type);
	debug3("task/cgroup: (%s[%d]) %s", buftype,
			job->cpu_bind_type, job->cpu_bind);
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
		return _get_ldom_sched_cpuset(topology, hwtype, req_hwtype,
				local_id, mask);
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
		if (str_to_cpuset(mask, mstr) < 0) {
			error("task/cgroup: str_to_cpuset %s", mstr);
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

	if (job->cpu_bind_type & CPU_BIND_LDMASK) {
		int len = strlen(mstr);
		char *ptr = mstr + len - 1;
		uint32_t base = 0;

		curstr = mstr;
		/* skip 0x, it's all hex anyway */
		if (len > 1 && !memcmp(mstr, "0x", 2L))
			curstr += 2;
		while (ptr >= curstr) {
			char val = char_to_val(*ptr);
			if (val == (char) -1)
				return false;
			if (val & 1)
				_get_ldom_sched_cpuset(topology, hwtype,
						req_hwtype, base, mask);
			if (val & 2)
				_get_ldom_sched_cpuset(topology, hwtype,
						req_hwtype, base + 1, mask);
			if (val & 4)
				_get_ldom_sched_cpuset(topology, hwtype,
						req_hwtype, base + 2, mask);
			if (val & 8)
				_get_ldom_sched_cpuset(topology, hwtype,
						req_hwtype, base + 3, mask);
			len--;
			ptr--;
			base += 4;
		}
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_LDMAP) {
		uint32_t myldom = 0;
		if (strncmp(mstr, "0x", 2) == 0) {
			myldom = strtoul (&(mstr[2]), NULL, 16);
		} else {
			myldom = strtoul (mstr, NULL, 10);
		}
		return _get_ldom_sched_cpuset(topology, hwtype, req_hwtype,
				myldom, mask);
	}

	return false;
}

static void _add_hwloc_cpuset(
	hwloc_obj_type_t hwtype, hwloc_obj_type_t req_hwtype,
	hwloc_obj_t obj, uint32_t taskid,  int bind_verbose,
	hwloc_bitmap_t cpuset)
{
	struct hwloc_obj *pobj;

	/* if requested binding overlaps the granularity */
	/* use the ancestor cpuset instead of the object one */
	if (hwloc_compare_types(hwtype,req_hwtype) > 0) {

		/* Get the parent object of req_hwtype or the */
		/* one just above if not found (meaning of >0)*/
		/* (useful for ldoms binding with !NUMA nodes)*/
		pobj = obj->parent;
		while (pobj != NULL &&
		       hwloc_compare_types(pobj->type, req_hwtype) > 0)
			pobj = pobj->parent;

		if (pobj != NULL) {
			if (bind_verbose)
				info("task/cgroup: task[%u] higher level %s "
				     "found", taskid,
				     hwloc_obj_type_string(pobj->type));
			hwloc_bitmap_or(cpuset, cpuset, pobj->allowed_cpuset);
		} else {
			/* should not be executed */
			if (bind_verbose)
				info("task/cgroup: task[%u] no higher level "
				     "found", taskid);
			hwloc_bitmap_or(cpuset, cpuset, obj->allowed_cpuset);
		}

	} else
		hwloc_bitmap_or(cpuset, cpuset, obj->allowed_cpuset);
}

static int _task_cgroup_cpuset_dist_cyclic(
	hwloc_topology_t topology, hwloc_obj_type_t hwtype,
	hwloc_obj_type_t req_hwtype, stepd_step_rec_t *job, int bind_verbose,
	hwloc_bitmap_t cpuset)
{
	hwloc_obj_t obj;
	uint32_t *obj_idx;
	uint32_t i, j, sock_idx, sock_loop, ntskip, npdist, nsockets;
	uint32_t taskid = job->envtp->localid;

	if (bind_verbose)
		info("task/cgroup: task[%u] using %s distribution "
		     "(task_dist=%u)", taskid,
		     format_task_dist_states(job->task_dist), job->task_dist);
	nsockets = (uint32_t) hwloc_get_nbobjs_by_type(topology,
						       HWLOC_OBJ_SOCKET);
	obj_idx = xmalloc(nsockets * sizeof(uint32_t));

	if (hwloc_compare_types(hwtype, HWLOC_OBJ_CORE) >= 0) {
		/* cores or threads granularity */
		ntskip = taskid;
		npdist = job->cpus_per_task;
	} else {
		/* sockets or ldoms granularity */
		ntskip = taskid;
		npdist = 1;
	}

	/* skip objs for lower taskids, then add them to the
	   current task cpuset. To prevent infinite loop, check
	   that we do not loop more than npdist times around the available
	   sockets, which is the worst scenario we should afford here. */
	i = 0; j = 0;
	sock_idx = 0;
	sock_loop = 0;
	while (i < ntskip + 1 && sock_loop < npdist + 1) {
		/* fill one or multiple sockets using block mode, unless
		   otherwise stated in the job->task_dist field */
		while ((sock_idx < nsockets) && (j < npdist)) {
			obj = hwloc_get_obj_below_by_type(
				topology, HWLOC_OBJ_SOCKET, sock_idx,
				hwtype, obj_idx[sock_idx]);
			if (obj != NULL) {
				obj_idx[sock_idx]++;
				j++;
				if (i == ntskip)
					_add_hwloc_cpuset(hwtype, req_hwtype,
							  obj, taskid,
							  bind_verbose, cpuset);
				if ((j < npdist) &&
				    ((job->task_dist ==
				      SLURM_DIST_CYCLIC_CFULL) ||
				     (job->task_dist ==
				      SLURM_DIST_BLOCK_CFULL)))
					sock_idx++;
			} else {
				sock_idx++;
			}
		}
		/* if it succeed, switch to the next task, starting
		   with the next available socket, otherwise, loop back
		   from the first socket trying to find available slots. */
		if (j == npdist) {
			i++; j = 0;
			sock_idx++; // no validity check, handled by the while
			sock_loop = 0;
		} else {
			sock_loop++;
			sock_idx = 0;
		}
	}

	xfree(obj_idx);

	/* should never happened in normal scenario */
	if (sock_loop > npdist) {
		error("task/cgroup: task[%u] infinite loop broken while trying"
		      "to provision compute elements using %s", taskid,
		      format_task_dist_states(job->task_dist));
		return XCGROUP_ERROR;
	} else
		return XCGROUP_SUCCESS;
}

static int _task_cgroup_cpuset_dist_block(
	hwloc_topology_t topology, hwloc_obj_type_t hwtype,
	hwloc_obj_type_t req_hwtype, uint32_t nobj,
	stepd_step_rec_t *job, int bind_verbose, hwloc_bitmap_t cpuset)
{
	hwloc_obj_t obj;
	uint32_t i, pfirst,plast;
	uint32_t taskid = job->envtp->localid;
	int hwdepth;

	if (bind_verbose)
		info("task/cgroup: task[%u] using block distribution, "
		     "task_dist %u", taskid, job->task_dist);
	if (hwloc_compare_types(hwtype,HWLOC_OBJ_CORE) >= 0) {
		/* cores or threads granularity */
		pfirst = taskid *  job->cpus_per_task ;
		plast = pfirst + job->cpus_per_task - 1;
	} else {
		/* sockets or ldoms granularity */
		pfirst = taskid;
		plast = pfirst;
	}
	hwdepth = hwloc_get_type_depth(topology,hwtype);
	for (i = pfirst; i <= plast && i < nobj ; i++) {
		obj = hwloc_get_obj_by_depth(topology, hwdepth, (int)i);
		_add_hwloc_cpuset(hwtype, req_hwtype, obj, taskid,
			    bind_verbose, cpuset);
	}
	return XCGROUP_SUCCESS;
}


/* The job has specialized cores, synchronize user mask with available cores */
static void _validate_mask(uint32_t task_id, hwloc_obj_t obj, cpu_set_t *ts)
{
	int i, j, overlaps = 0;
	bool superset = true;

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, ts))
			continue;
		j = hwloc_bitmap_isset(obj->allowed_cpuset, i);
		if (j > 0) {
			overlaps++;
		} else if (j == 0) {
			CPU_CLR(i, ts);
			superset = false;
		}
	}

	if (overlaps == 0) {
		/* The task's cpu map is completely invalid.
		 * Give it all allowed CPUs */
		for (i = 0; i < CPU_SETSIZE; i++) {
			if (hwloc_bitmap_isset(obj->allowed_cpuset, i) > 0)
				CPU_SET(i, ts);
		}
	}

	if (!superset) {
		info("task/cgroup: Ignoring user CPU binding outside of job "
		     "step allocation for task[%u]", task_id);
		fprintf(stderr, "Requested cpu_bind option outside of job "
			"step allocation for task[%u]\n", task_id);
	}
}

#endif

extern int task_cgroup_cpuset_init(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	/* initialize user/job/jobstep cgroup relative paths */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	/* initialize cpuset cgroup namespace */
	if (xcgroup_ns_create(slurm_cgroup_conf, &cpuset_ns, "", "cpuset")
	    != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to create cpuset namespace");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int task_cgroup_cpuset_fini(slurm_cgroup_conf_t *slurm_cgroup_conf)
{

	if (user_cgroup_path[0] != '\0')
		xcgroup_destroy(&user_cpuset_cg);
	if (job_cgroup_path[0] != '\0')
		xcgroup_destroy(&job_cpuset_cg);
	if (jobstep_cgroup_path[0] != '\0')
		xcgroup_destroy(&step_cpuset_cg);

	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	xcgroup_ns_destroy(&cpuset_ns);

	return SLURM_SUCCESS;
}

extern int task_cgroup_cpuset_create(stepd_step_rec_t *job)
{
	int rc;
	int fstatus = SLURM_ERROR;

	xcgroup_t cpuset_cg;

	uint32_t jobid = job->jobid;
	uint32_t stepid = job->stepid;
	uid_t uid = job->uid;
	uid_t gid = job->gid;
	char* user_alloc_cores = NULL;
	char* job_alloc_cores = NULL;
	char* step_alloc_cores = NULL;
	char cpuset_meta[PATH_MAX];

	char* cpus = NULL;
	size_t cpus_size;

	char* slurm_cgpath;
	xcgroup_t slurm_cg;

#ifdef HAVE_NATIVE_CRAY
	char expected_usage[32];
#endif

	/* create slurm root cg in this cg namespace */
	slurm_cgpath = task_cgroup_create_slurm_cg(&cpuset_ns);
	if ( slurm_cgpath == NULL ) {
		return SLURM_ERROR;
	}

	/* check that this cgroup has cpus allowed or initialize them */
	if (xcgroup_load(&cpuset_ns,&slurm_cg,slurm_cgpath)
	    != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to load slurm cpuset xcgroup");
		xfree(slurm_cgpath);
		return SLURM_ERROR;
	}
again:
	snprintf(cpuset_meta, sizeof(cpuset_meta), "%scpus", cpuset_prefix);
	rc = xcgroup_get_param(&slurm_cg, cpuset_meta, &cpus,&cpus_size);
	if (rc != XCGROUP_SUCCESS || cpus_size == 1) {
		if (!cpuset_prefix_set && (rc != XCGROUP_SUCCESS)) {
			cpuset_prefix_set = 1;
			cpuset_prefix = "cpuset.";
			goto again;
		}

		/* initialize the cpusets as it was inexistant */
		if (_xcgroup_cpuset_init(&slurm_cg) !=
		    XCGROUP_SUCCESS) {
			xfree(slurm_cgpath);
			xcgroup_destroy(&slurm_cg);
			return SLURM_ERROR;
		}
	}
	xfree(cpus);

	/* build user cgroup relative path if not set (should not be) */
	if (*user_cgroup_path == '\0') {
		if (snprintf(user_cgroup_path, PATH_MAX,
			     "%s/uid_%u", slurm_cgpath, uid) >= PATH_MAX) {
			error("task/cgroup: unable to build uid %u cgroup "
			      "relative path : %m", uid);
			xfree(slurm_cgpath);
			return SLURM_ERROR;
		}
	}
	xfree(slurm_cgpath);

	/* build job cgroup relative path if no set (should not be) */
	if (*job_cgroup_path == '\0') {
		if (snprintf(job_cgroup_path,PATH_MAX,"%s/job_%u",
			     user_cgroup_path,jobid) >= PATH_MAX) {
			error("task/cgroup: unable to build job %u cpuset "
			      "cg relative path : %m",jobid);
			return SLURM_ERROR;
		}
	}

	/* build job step cgroup relative path (should not be) */
	if (*jobstep_cgroup_path == '\0') {
		if (stepid == SLURM_BATCH_SCRIPT) {
			if (snprintf(jobstep_cgroup_path, PATH_MAX,
				     "%s/step_batch", job_cgroup_path)
			    >= PATH_MAX) {
				error("task/cgroup: unable to build job step"
				      " %u.batch cpuset cg relative path: %m",
				      jobid);
				return SLURM_ERROR;
			}
		} else {
			if (snprintf(jobstep_cgroup_path,
				     PATH_MAX, "%s/step_%u",
				     job_cgroup_path, stepid) >= PATH_MAX) {
				error("task/cgroup: unable to build job step"
				      " %u.%u cpuset cg relative path: %m",
				      jobid, stepid);
				return SLURM_ERROR;
			}
		}
	}

	/*
	 * create cpuset root cg and lock it
	 *
	 * we will keep the lock until the end to avoid the effect of a release
	 * agent that would remove an existing cgroup hierarchy while we are
	 * setting it up. As soon as the step cgroup is created, we can release
	 * the lock.
	 * Indeed, consecutive slurm steps could result in cg being removed
	 * between the next EEXIST instanciation and the first addition of
	 * a task. The release_agent will have to lock the root cpuset cgroup
	 * to avoid this scenario.
	 */
	if (xcgroup_create(&cpuset_ns,&cpuset_cg,"",0,0) != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to create root cpuset xcgroup");
		return SLURM_ERROR;
	}
	if (xcgroup_lock(&cpuset_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&cpuset_cg);
		error("task/cgroup: unable to lock root cpuset cg");
		return SLURM_ERROR;
	}

	/*
	 * build job and job steps allocated cores lists
	 */
	debug("task/cgroup: job abstract cores are '%s'",
	      job->job_alloc_cores);
	debug("task/cgroup: step abstract cores are '%s'",
	      job->step_alloc_cores);
	if (xcpuinfo_abs_to_mac(job->job_alloc_cores,
				&job_alloc_cores) != SLURM_SUCCESS) {
		error("task/cgroup: unable to build job physical cores");
		goto error;
	}
	if (xcpuinfo_abs_to_mac(job->step_alloc_cores,
				&step_alloc_cores) != SLURM_SUCCESS) {
		error("task/cgroup: unable to build step physical cores");
		goto error;
	}
	debug("task/cgroup: job physical cores are '%s'",
	      job_alloc_cores);
	debug("task/cgroup: step physical cores are '%s'",
	      step_alloc_cores);

	/*
	 * create user cgroup in the cpuset ns (it could already exist)
	 */
	if (xcgroup_create(&cpuset_ns,&user_cpuset_cg,
			   user_cgroup_path,
			   getuid(),getgid()) != XCGROUP_SUCCESS) {
		goto error;
	}
	if (xcgroup_instanciate(&user_cpuset_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_cpuset_cg);
		goto error;
	}

	/*
	 * check that user's cpuset cgroup is consistant and add the job cores
	 */
	rc = xcgroup_get_param(&user_cpuset_cg, cpuset_meta, &cpus,&cpus_size);
	if (rc != XCGROUP_SUCCESS || cpus_size == 1) {
		/* initialize the cpusets as it was inexistant */
		if (_xcgroup_cpuset_init(&user_cpuset_cg) !=
		    XCGROUP_SUCCESS) {
			xcgroup_delete(&user_cpuset_cg);
			xcgroup_destroy(&user_cpuset_cg);
			goto error;
		}
	}
	user_alloc_cores = xstrdup(job_alloc_cores);
	if (cpus != NULL && cpus_size > 1) {
		cpus[cpus_size-1]='\0';
		xstrcat(user_alloc_cores,",");
		xstrcat(user_alloc_cores,cpus);
	}
	xcgroup_set_param(&user_cpuset_cg, cpuset_meta, user_alloc_cores);
	xfree(cpus);

	/*
	 * create job cgroup in the cpuset ns (it could already exist)
	 */
	if (xcgroup_create(&cpuset_ns,&job_cpuset_cg,
			   job_cgroup_path,
			   getuid(),getgid()) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_cpuset_cg);
		goto error;
	}
	if (xcgroup_instanciate(&job_cpuset_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		goto error;
	}
	if (_xcgroup_cpuset_init(&job_cpuset_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		goto error;
	}
	xcgroup_set_param(&job_cpuset_cg, cpuset_meta, job_alloc_cores);

	/*
	 * create step cgroup in the cpuset ns (it should not exists)
	 * use job's user uid/gid to enable tasks cgroups creation by
	 * the user inside the step cgroup owned by root
	 */
	if (xcgroup_create(&cpuset_ns,&step_cpuset_cg,
			   jobstep_cgroup_path,
			   uid,gid) != XCGROUP_SUCCESS) {
		/* do not delete user/job cgroup as */
		/* they can exist for other steps */
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		goto error;
	}
	if (xcgroup_instanciate(&step_cpuset_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		xcgroup_destroy(&step_cpuset_cg);
		goto error;
	}
	if (_xcgroup_cpuset_init(&step_cpuset_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_cpuset_cg);
		xcgroup_destroy(&job_cpuset_cg);
		xcgroup_delete(&step_cpuset_cg);
		xcgroup_destroy(&step_cpuset_cg);
		goto error;
	}
	xcgroup_set_param(&step_cpuset_cg, cpuset_meta, step_alloc_cores);

	/*
	 * on Cray systems, set the expected usage in bytes.
	 * This is used by the Cray OOM killer
	 */
#ifdef HAVE_NATIVE_CRAY
	snprintf(expected_usage, sizeof(expected_usage), "%"PRIu64,
		 (uint64_t)job->step_mem * 1024 * 1024);
	xcgroup_set_param(&step_cpuset_cg, "expected_usage_in_bytes",
			  expected_usage);
#endif

	/* attach the slurmstepd to the step cpuset cgroup */
	pid_t pid = getpid();
	rc = xcgroup_add_pids(&step_cpuset_cg,&pid,1);
	if (rc != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to add slurmstepd to cpuset cg '%s'",
		      step_cpuset_cg.path);
		fstatus = SLURM_ERROR;
	} else
		fstatus = SLURM_SUCCESS;

	/* validate the requested cpu frequency and set it */
	cpu_freq_cgroup_validate(job, step_alloc_cores);

error:
	xcgroup_unlock(&cpuset_cg);
	xcgroup_destroy(&cpuset_cg);

	xfree(user_alloc_cores);
	xfree(job_alloc_cores);
	xfree(step_alloc_cores);

	return fstatus;
}

extern int task_cgroup_cpuset_attach_task(stepd_step_rec_t *job)
{
	int fstatus = SLURM_ERROR;

	/* tasks are automatically attached as slurmstepd is in the step cg */
	fstatus = SLURM_SUCCESS;

	return fstatus;
}

/* affinity should be set using sched_setaffinity to not force */
/* user to have to play with the cgroup hierarchy to modify it */
extern int task_cgroup_cpuset_set_task_affinity(stepd_step_rec_t *job)
{
	int fstatus = SLURM_ERROR;

#ifndef HAVE_HWLOC

	error("task/cgroup: plugin not compiled with hwloc support, "
	      "skipping affinity.");
	return fstatus;

#else
	char mstr[1 + CPU_SETSIZE / 4];
	cpu_bind_type_t bind_type;
	cpu_set_t ts;
	hwloc_obj_t obj;
	hwloc_obj_type_t socket_or_node;
	hwloc_topology_t topology;
	hwloc_bitmap_t cpuset;
	hwloc_obj_type_t hwtype;
	hwloc_obj_type_t req_hwtype;
	int bind_verbose = 0;
	int rc = SLURM_SUCCESS, match;
	pid_t    pid = job->envtp->task_pid;
	size_t tssize;
	uint32_t nldoms;
	uint32_t nsockets;
	uint32_t ncores;
	uint32_t npus;
	uint32_t nobj;
	uint32_t taskid = job->envtp->localid;
	uint32_t jntasks = job->node_tasks;
	uint32_t jnpus;

	if (job->batch) {
		jnpus = job->cpus;
		job->cpus_per_task = job->cpus;
	} else
		jnpus = jntasks * job->cpus_per_task;

	bind_type = job->cpu_bind_type;
	if ((conf->task_plugin_param & CPU_BIND_VERBOSE) ||
	    (bind_type & CPU_BIND_VERBOSE))
		bind_verbose = 1 ;

	/* Allocate and initialize hwloc objects */
	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);
	cpuset = hwloc_bitmap_alloc();

	if ( hwloc_get_type_depth(topology, HWLOC_OBJ_NODE) >
	     hwloc_get_type_depth(topology, HWLOC_OBJ_SOCKET) ) {
		/* One socket contains multiple NUMA-nodes
		 * like AMD Opteron 6000 series etc.
		 * In such case, use NUMA-node instead of socket. */
		socket_or_node = HWLOC_OBJ_NODE;
	} else {
		socket_or_node = HWLOC_OBJ_SOCKET;
	}

	if (bind_type & CPU_BIND_NONE) {
		if (bind_verbose)
			info("task/cgroup: task[%u] is requesting no affinity",
			     taskid);
		return 0;
	} else if (bind_type & CPU_BIND_TO_THREADS) {
		if (bind_verbose)
			info("task/cgroup: task[%u] is requesting "
			     "thread level binding",taskid);
		req_hwtype = HWLOC_OBJ_PU;
	} else if (bind_type & CPU_BIND_TO_CORES) {
		if (bind_verbose)
			info("task/cgroup: task[%u] is requesting "
			     "core level binding",taskid);
		req_hwtype = HWLOC_OBJ_CORE;
	} else if (bind_type & CPU_BIND_TO_SOCKETS) {
		if (bind_verbose)
			info("task/cgroup: task[%u] is requesting "
			     "socket level binding",taskid);
		req_hwtype = socket_or_node;
	} else if (bind_type & CPU_BIND_TO_LDOMS) {
		if (bind_verbose)
			info("task/cgroup: task[%u] is requesting "
			     "ldom level binding",taskid);
		req_hwtype = HWLOC_OBJ_NODE;
	} else if (bind_type & CPU_BIND_TO_BOARDS) {
		if (bind_verbose)
			info("task/cgroup: task[%u] is requesting "
			     "board level binding",taskid);
		req_hwtype = HWLOC_OBJ_GROUP;
	} else if (bind_type & bind_mode_ldom) {
		req_hwtype = HWLOC_OBJ_NODE;
	} else {
		if (bind_verbose)
			info("task/cgroup: task[%u] using core level binding"
			     " by default",taskid);
		req_hwtype = HWLOC_OBJ_CORE;
	}

	/*
	 * Perform the topology detection. It will only get allowed PUs.
	 * Detect in the same time the granularity to use for binding.
	 * The granularity can be relaxed from threads to cores if enough
	 * cores are available as with hyperthread support, ntasks-per-core
	 * param can let us have access to more threads per core for each
	 * task
	 * Revert back to machine granularity if no finer-grained granularity
	 * matching the request is found. This will result in no affinity
	 * applied.
	 * The detected granularity will be used to find where to best place
	 * the task, then the cpu_bind option will be used to relax the
	 * affinity constraint and use more PUs. (i.e. use a core granularity
	 * to dispatch the tasks across the sockets and then provide access
	 * to each task to the cores of its socket.)
	 */
	npus = (uint32_t) hwloc_get_nbobjs_by_type(topology,
						   HWLOC_OBJ_PU);
	ncores = (uint32_t) hwloc_get_nbobjs_by_type(topology,
						     HWLOC_OBJ_CORE);
	nsockets = (uint32_t) hwloc_get_nbobjs_by_type(topology,
						       socket_or_node);
	nldoms = (uint32_t) hwloc_get_nbobjs_by_type(topology,
						     HWLOC_OBJ_NODE);

	hwtype = HWLOC_OBJ_MACHINE;
	nobj = 1;
	if (npus >= jnpus || bind_type & CPU_BIND_TO_THREADS) {
		hwtype = HWLOC_OBJ_PU;
		nobj = npus;
	}
	if (ncores >= jnpus || bind_type & CPU_BIND_TO_CORES) {
		hwtype = HWLOC_OBJ_CORE;
		nobj = ncores;
	}
	if (nsockets >= jntasks &&
	    bind_type & CPU_BIND_TO_SOCKETS) {
		hwtype = socket_or_node;
		nobj = nsockets;
	}
	/*
	 * HWLOC returns all the NUMA nodes available regardless of the
	 * number of underlying sockets available (regardless of the allowed
	 * resources). So there is no guarantee that each ldom will be populated
	 * with usable sockets. So add a simple check that at least ensure that
	 * we have as many sockets as ldoms before moving to ldoms granularity
	 */
	if (nldoms >= jntasks &&
	    nsockets >= nldoms &&
	    bind_type & (CPU_BIND_TO_LDOMS | bind_mode_ldom)) {
		hwtype = HWLOC_OBJ_NODE;
		nobj = nldoms;
	}

	/*
	 * If not enough objects to do the job, revert to no affinity mode
	 */
	if (hwloc_compare_types(hwtype, HWLOC_OBJ_MACHINE) == 0) {
		info("task/cgroup: task[%u] disabling affinity because of %s "
		     "granularity",taskid, hwloc_obj_type_string(hwtype));

	} else if ((hwloc_compare_types(hwtype, HWLOC_OBJ_CORE) >= 0) &&
		   (nobj < jnpus)) {
		info("task/cgroup: task[%u] not enough %s objects (%d < %d), "
		     "disabling affinity",
		     taskid, hwloc_obj_type_string(hwtype), nobj, jnpus);

	} else if (bind_type & bind_mode) {
		/* Explicit binding mode specified by the user
		 * Bind the taskid in accordance with the specified mode
		 */
		obj = hwloc_get_obj_by_type(topology, HWLOC_OBJ_MACHINE, 0);
		match = hwloc_bitmap_isequal(obj->complete_cpuset,
					     obj->allowed_cpuset);
		if ((job->job_core_spec == (uint16_t) NO_VAL) && !match) {
			info("task/cgroup: entire node must be allocated, "
			     "disabling affinity, task[%u]", taskid);
			fprintf(stderr, "Requested cpu_bind option requires "
				"entire node to be allocated; disabling "
				"affinity\n");
		} else {
			if (bind_verbose) {
				info("task/cgroup: task[%u] is requesting "
				     "explicit binding mode", taskid);
			}
			_get_sched_cpuset(topology, hwtype, req_hwtype, &ts,
					  job);
			tssize = sizeof(cpu_set_t);
			fstatus = SLURM_SUCCESS;
			if (job->job_core_spec != (uint16_t) NO_VAL)
				_validate_mask(taskid, obj, &ts);
			if ((rc = sched_setaffinity(pid, tssize, &ts))) {
				error("task/cgroup: task[%u] unable to set "
				      "mask 0x%s", taskid,
				      cpuset_to_str(&ts, mstr));
				fstatus = SLURM_ERROR;
			} else if (bind_verbose) {
				info("task/cgroup: task[%u] mask 0x%s",
				     taskid, cpuset_to_str(&ts, mstr));
			}
			slurm_chkaffinity(&ts, job, rc);
		}
	} else {
		/* Bind the detected object to the taskid, respecting the
		 * granularity, using the designated or default distribution
		 * method (block or cyclic). */
		char *str;

		if (bind_verbose) {
			info("task/cgroup: task[%u] using %s granularity",
			     taskid,hwloc_obj_type_string(hwtype));
		}

		/* There are two "distributions,"  controlled by the
		 * -m option of srun and friends. The first is the
		 * distribution of tasks to nodes.  The second is the
		 * distribution of allocated cpus to tasks for
		 * binding.  This code is handling the second
		 * distribution.  Here's how the values get set, based
		 * on the value of -m
		 *
		 * SLURM_DIST_CYCLIC = srun -m cyclic
		 * SLURM_DIST_BLOCK = srun -m block
		 * SLURM_DIST_CYCLIC_CYCLIC = srun -m cyclic:cyclic
		 * SLURM_DIST_BLOCK_CYCLIC = srun -m block:cyclic
		 *
		 * In the first two cases, the user only specified the
		 * first distribution.  The second distribution
		 * defaults to cyclic.  In the second two cases, the
		 * user explicitly requested a second distribution of
		 * cyclic.  So all these four cases correspond to a
		 * second distribution of cyclic.   So we want to call
		 * _task_cgroup_cpuset_dist_cyclic.
		 *
		 * If the user explicitly specifies a second
		 * distribution of block, or if
		 * CR_CORE_DEFAULT_DIST_BLOCK is configured and the
		 * user does not explicitly specify a second
		 * distribution of cyclic, the second distribution is
		 * block, and we need to call
		 * _task_cgroup_cpuset_dist_block. In these cases,
		 * task_dist would be set to SLURM_DIST_CYCLIC_BLOCK
		 * or SLURM_DIST_BLOCK_BLOCK.
		 *
		 * You can see the equivalent code for the
		 * task/affinity plugin in
		 * src/plugins/task/affinity/dist_tasks.c, around line 368
		 */
		switch (job->task_dist) {
		case SLURM_DIST_BLOCK_BLOCK:
		case SLURM_DIST_CYCLIC_BLOCK:
		case SLURM_DIST_PLANE:
			/* tasks are distributed in blocks within a plane */
			_task_cgroup_cpuset_dist_block(
				topology, hwtype, req_hwtype,
				nobj, job, bind_verbose, cpuset);
			break;
		case SLURM_DIST_ARBITRARY:
		case SLURM_DIST_BLOCK:
		case SLURM_DIST_CYCLIC:
		case SLURM_DIST_UNKNOWN:
			if (slurm_get_select_type_param()
			    & CR_CORE_DEFAULT_DIST_BLOCK) {
				_task_cgroup_cpuset_dist_block(
					topology, hwtype, req_hwtype,
					nobj, job, bind_verbose, cpuset);
				break;
			}
			/* We want to fall through here if we aren't doing a
			   default dist block.
			*/
		default:
			_task_cgroup_cpuset_dist_cyclic(
				topology, hwtype, req_hwtype,
				job, bind_verbose, cpuset);
			break;
		}

		hwloc_bitmap_asprintf(&str, cpuset);

		tssize = sizeof(cpu_set_t);
		if (hwloc_cpuset_to_glibc_sched_affinity(topology, cpuset,
							 &ts, tssize) == 0) {
			fstatus = SLURM_SUCCESS;
			if ((rc = sched_setaffinity(pid, tssize, &ts))) {
				error("task/cgroup: task[%u] unable to set "
				      "taskset '%s'", taskid, str);
				fstatus = SLURM_ERROR;
			} else if (bind_verbose) {
				info("task/cgroup: task[%u] set taskset '%s'",
				     taskid, str);
			}
			slurm_chkaffinity(&ts, job, rc);
		} else {
			error("task/cgroup: task[%u] unable to build "
			      "taskset '%s'",taskid,str);
			fstatus = SLURM_ERROR;
		}
		free(str);
	}

	/* Destroy hwloc objects */
	hwloc_bitmap_free(cpuset);
	hwloc_topology_destroy(topology);

	return fstatus;
#endif

}
#endif
