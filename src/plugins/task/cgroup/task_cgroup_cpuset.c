/***************************************************************************** \
 *  task_cgroup_cpuset.c - cpuset cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *  Portions copyright (C) 2012 Bull
 *  Written by Martin Perry <martin.perry@bull.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <sched.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/common/cpu_frequency.h"

#include "src/common/bitstring.h"
#include "src/common/xstring.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xcgroup.h"

#include "task_cgroup.h"

#ifdef HAVE_HWLOC
#include <hwloc.h>
#include <hwloc/glibc-sched.h>

# if HWLOC_API_VERSION <= 0x00010000
/* After this version the cpuset structure and all it's functions
 * changed to bitmaps.  So to work with old hwloc's we just to the
 * opposite to avoid having to put a bunch of ifdef's in the code we
 * just do it here.
 */
typedef hwloc_cpuset_t hwloc_bitmap_t;

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

# endif

#endif

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];

static xcgroup_ns_t cpuset_ns;

static xcgroup_t user_cpuset_cg;
static xcgroup_t job_cpuset_cg;
static xcgroup_t step_cpuset_cg;

static int _xcgroup_cpuset_init(xcgroup_t* cg);

/*
 * convert abstract range into the machine one
 */
static int _abs_to_mac(char* lrange, char** prange)
{
	static int total_cores = -1, total_cpus = -1;
	bitstr_t* absmap = NULL;
	bitstr_t* macmap = NULL;
	int icore, ithread;
	int absid, macid;
	int rc = SLURM_SUCCESS;

	if (total_cores == -1) {
		total_cores = conf->sockets * conf->cores;
		total_cpus  = conf->block_map_size;
	}

	/* allocate bitmap */
	absmap = bit_alloc(total_cores);
	macmap = bit_alloc(total_cpus);

	if (!absmap || !macmap) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* string to bitmap conversion */
	if (bit_unfmt(absmap, lrange)) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* mapping abstract id to machine id using conf->block_map */
	for (icore = 0; icore < total_cores; icore++) {
		if (bit_test(absmap, icore)) {
			for (ithread = 0; ithread<conf->threads; ithread++) {
				absid  = icore*conf->threads + ithread;
				absid %= total_cpus;

				macid  = conf->block_map[absid];
				macid %= total_cpus;

				bit_set(macmap, macid);
			}
		}
 	}

	/* convert machine cpu bitmap to range string */
	*prange = (char*)xmalloc(total_cpus*6);
	bit_fmt(*prange, total_cpus*6, macmap);

	/* free unused bitmaps */
end_it:
	FREE_NULL_BITMAP(absmap);
	FREE_NULL_BITMAP(macmap);

	if (rc != SLURM_SUCCESS)
		info("_abs_to_mac failed");

	return rc;
}

/* when cgroups are configured with cpuset, at least
 * cpuset.cpus and cpuset.mems must be set or the cgroup
 * will not be available at all.
 * we duplicate the ancestor configuration in the init step */
static int _xcgroup_cpuset_init(xcgroup_t* cg)
{
	int fstatus,i;

	char* cpuset_metafiles[] = {
		"cpuset.cpus",
		"cpuset.mems"
	};
	char* cpuset_meta;
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
		cpuset_meta = cpuset_metafiles[i];
		if (xcgroup_get_param(&acg,cpuset_meta,
				      &cpuset_conf,&csize)
		    != XCGROUP_SUCCESS) {
			debug2("task/cgroup: assuming no cpuset cg "
			       "support for '%s'",acg.path);
			xcgroup_destroy(&acg);
			return fstatus;
		}
		if (csize > 0)
			cpuset_conf[csize-1]='\0';
		if (xcgroup_set_param(cg,cpuset_meta,cpuset_conf)
		    != XCGROUP_SUCCESS) {
			debug2("task/cgroup: unable to write %s configuration "
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

#ifdef HAVE_HWLOC

/*
 * Add cpuset for an object to the total cpuset for a task, using the
 * appropriate ancestor object cpuset if necessary
 *
 * obj = object to add
 * cpuset = cpuset for task
 */
static void _add_cpuset(
	hwloc_obj_type_t hwtype, hwloc_obj_type_t req_hwtype,
	hwloc_obj_t obj, uint32_t taskid,  int bind_verbose,
	hwloc_bitmap_t cpuset)
{
	struct hwloc_obj *pobj;

	/* if requested binding overlap the granularity */
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

/*
 * Distribute cpus to the task using cyclic distribution across sockets
 */
static int _task_cgroup_cpuset_dist_cyclic(
	hwloc_topology_t topology, hwloc_obj_type_t hwtype,
	hwloc_obj_type_t req_hwtype, slurmd_job_t *job, int bind_verbose,
	hwloc_bitmap_t cpuset)
{
	hwloc_obj_t obj;
	uint32_t *obj_idx;
	uint32_t i, sock_idx, npskip, npdist, nsockets;
	uint32_t taskid = job->envtp->localid;

	if (bind_verbose)
		info("task/cgroup: task[%u] using cyclic distribution, "
		     "task_dist %u", taskid, job->task_dist);
	nsockets = (uint32_t) hwloc_get_nbobjs_by_type(topology,
						       HWLOC_OBJ_SOCKET);
	obj_idx = xmalloc(nsockets * sizeof(uint32_t));

	if (hwloc_compare_types(hwtype,HWLOC_OBJ_CORE) >= 0) {
		/* cores or threads granularity */
		npskip = taskid * job->cpus_per_task;
		npdist = job->cpus_per_task;
	} else {
		/* sockets or ldoms granularity */
		npskip = taskid;
		npdist = 1;
	}

	/* skip objs for lower taskids */
	i = 0;
	sock_idx = 0;
	while (i < npskip) {
		while ((sock_idx < nsockets) && (i < npskip)) {
			obj = hwloc_get_obj_below_by_type(
				topology, HWLOC_OBJ_SOCKET, sock_idx,
				hwtype, obj_idx[sock_idx]);
			if (obj != NULL) {
				obj_idx[sock_idx]++;
				i++;
			}
			sock_idx++;
		}
		if (i < npskip)
			sock_idx = 0;
	}

	/* distribute objs cyclically across sockets */
	i = npdist;
	while (i > 0) {
		while ((sock_idx < nsockets) && (i > 0)) {
			obj = hwloc_get_obj_below_by_type(
				topology, HWLOC_OBJ_SOCKET, sock_idx,
				hwtype, obj_idx[sock_idx]);
			if (obj != NULL) {
				obj_idx[sock_idx]++;
				_add_cpuset(hwtype, req_hwtype, obj, taskid,
					    bind_verbose, cpuset);
				i--;
			}
			sock_idx++;
		}
		sock_idx = 0;
	}
	xfree(obj_idx);
	return XCGROUP_SUCCESS;
}

/*
 * Distribute cpus to the task using block distribution
 */
static int _task_cgroup_cpuset_dist_block(
	hwloc_topology_t topology, hwloc_obj_type_t hwtype,
	hwloc_obj_type_t req_hwtype, uint32_t nobj,
	slurmd_job_t *job, int bind_verbose, hwloc_bitmap_t cpuset)
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
		_add_cpuset(hwtype, req_hwtype, obj, taskid, bind_verbose,
			    cpuset);
	}
	return XCGROUP_SUCCESS;
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

extern int task_cgroup_cpuset_create(slurmd_job_t *job)
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

	char* cpus = NULL;
	size_t cpus_size;

	char* slurm_cgpath ;
	xcgroup_t slurm_cg;

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
	rc = xcgroup_get_param(&slurm_cg,"cpuset.cpus",&cpus,&cpus_size);
	if (rc != XCGROUP_SUCCESS || cpus_size == 1) {
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
			error("unable to build uid %u cgroup relative "
			      "path : %m", uid);
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
		if (stepid == NO_VAL) {
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
	if (_abs_to_mac(job->job_alloc_cores,
			&job_alloc_cores) != SLURM_SUCCESS) {
		error("task/cgroup: unable to build job physical cores");
		goto error;
	}
	if (_abs_to_mac(job->step_alloc_cores,
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
	rc = xcgroup_get_param(&user_cpuset_cg,"cpuset.cpus",&cpus,&cpus_size);
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
	xcgroup_set_param(&user_cpuset_cg,"cpuset.cpus",user_alloc_cores);
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
	xcgroup_set_param(&job_cpuset_cg,"cpuset.cpus",job_alloc_cores);

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
	xcgroup_set_param(&step_cpuset_cg,"cpuset.cpus",step_alloc_cores);

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
	if (job->cpu_freq != NO_VAL) {
		cpu_freq_cgroup_validate(job, step_alloc_cores);
	}

error:
	xcgroup_unlock(&cpuset_cg);
	xcgroup_destroy(&cpuset_cg);

	xfree(user_alloc_cores);
	xfree(job_alloc_cores);
	xfree(step_alloc_cores);

	return fstatus;
}

extern int task_cgroup_cpuset_attach_task(slurmd_job_t *job)
{
	int fstatus = SLURM_ERROR;

	/* tasks are automatically attached as slurmstepd is in the step cg */
	fstatus = SLURM_SUCCESS;

	return fstatus;
}

/* affinity should be set using sched_setaffinity to not force */
/* user to have to play with the cgroup hierarchy to modify it */
extern int task_cgroup_cpuset_set_task_affinity(slurmd_job_t *job)
{
	int fstatus = SLURM_ERROR;

#ifndef HAVE_HWLOC

	error("task/cgroup: plugin not compiled with hwloc support, "
	      "skipping affinity.");
	return fstatus;

#else
	hwloc_obj_type_t socket_or_node;
	uint32_t nldoms;
	uint32_t nsockets;
	uint32_t ncores;
	uint32_t npus;
	uint32_t nobj;
	uint32_t taskid = job->envtp->localid;
	uint32_t jntasks = job->node_tasks;
	uint32_t jnpus = jntasks * job->cpus_per_task;
	pid_t    pid = job->envtp->task_pid;

	cpu_bind_type_t bind_type;
	int bind_verbose = 0;

	hwloc_topology_t topology;
	hwloc_bitmap_t cpuset;
	hwloc_obj_type_t hwtype;
	hwloc_obj_type_t req_hwtype;

	size_t tssize;
	cpu_set_t ts;

	bind_type = job->cpu_bind_type ;
	if (conf->task_plugin_param & CPU_BIND_VERBOSE ||
	    bind_type & CPU_BIND_VERBOSE)
		bind_verbose = 1 ;

	/* Allocate and initialize hwloc objects */
	hwloc_topology_init(&topology);

	cpuset = hwloc_bitmap_alloc();

	hwloc_topology_load(topology);
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
	    bind_type & CPU_BIND_TO_LDOMS) {
		hwtype = HWLOC_OBJ_NODE;
		nobj = nldoms;
	}

	/*
	 * Bind the detected object to the taskid, respecting the
	 * granularity, using the designated or default distribution
	 * method (block or cyclic).
	 * If not enough objects to do the job, revert to no affinity mode
	 */
	if (hwloc_compare_types(hwtype,HWLOC_OBJ_MACHINE) == 0) {

		info("task/cgroup: task[%u] disabling affinity because of %s "
		     "granularity",taskid,hwloc_obj_type_string(hwtype));

	} else if (hwloc_compare_types(hwtype,HWLOC_OBJ_CORE) >= 0 &&
		   jnpus > nobj) {

		info("task/cgroup: task[%u] not enough %s objects, disabling "
		     "affinity",taskid,hwloc_obj_type_string(hwtype));

	} else {
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
		 * src/plugins/task/affinity/dist_tasks.c, around line 384.
		 */
		switch (job->task_dist) {
		case SLURM_DIST_CYCLIC:
		case SLURM_DIST_BLOCK:
		case SLURM_DIST_CYCLIC_CYCLIC:
		case SLURM_DIST_BLOCK_CYCLIC:
			_task_cgroup_cpuset_dist_cyclic(
				topology, hwtype, req_hwtype,
				job, bind_verbose, cpuset);
			break;
		default:
			_task_cgroup_cpuset_dist_block(
				topology, hwtype, req_hwtype,
				nobj, job, bind_verbose, cpuset);
		}

		hwloc_bitmap_asprintf(&str, cpuset);

		tssize = sizeof(cpu_set_t);
		if (hwloc_cpuset_to_glibc_sched_affinity(topology,cpuset,
							 &ts,tssize) == 0) {
			fstatus = SLURM_SUCCESS;
			if (sched_setaffinity(pid,tssize,&ts)) {
				error("task/cgroup: task[%u] unable to set "
				      "taskset '%s'",taskid,str);
				fstatus = SLURM_ERROR;
			} else if (bind_verbose) {
				info("task/cgroup: task[%u] taskset '%s' is set"
				     ,taskid,str);
			}
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
