/*****************************************************************************\
 *  proctrack_cgroup.c - process tracking via linux cgroup containers
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/log.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include "read_config.h"
#include "xcgroup.h"
#include "xcpuinfo.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job completion logging API 
 * matures.
 */
const char plugin_name[]      = "Process tracking via linux cgroup";
const char plugin_type[]      = "proctrack/cgroup";
const uint32_t plugin_version = 10;

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#define CGROUP_SLURMDIR CGROUP_BASEDIR "/slurm"

char user_cgroup_path[PATH_MAX];
char job_cgroup_path[PATH_MAX];
char jobstep_cgroup_path[PATH_MAX];

int _slurm_cgroup_init()
{
	int fstatus;
	xcgroup_opts_t opts;

	/* initialize job/jobstep cgroup path */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	/* we first check that cgroup is mounted */
	if ( ! xcgroup_is_available() ) {
		if ( slurm_cgroup_conf->cgroup_automount ) {
			if ( xcgroup_mount(slurm_cgroup_conf->
					   cgroup_mount_opts) ) {
				error("unable to mount cgroup");
				return SLURM_ERROR;
			}
			info("cgroup system is now mounted");
			/* we then set the release_agent if necessary */
			if ( slurm_cgroup_conf->cgroup_release_agent ) {
				xcgroup_set_release_agent(slurm_cgroup_conf->
							  cgroup_release_agent);
			}
		}
		else {
			error("cgroup is not mounted. aborting");
			return SLURM_ERROR;
		}
	}

	/* create a non releasable root cgroup for slurm usage */
	opts.uid=getuid();
	opts.gid=getgid();
	opts.create_only=0;
	opts.notify=0;
	fstatus = xcgroup_create(CGROUP_SLURMDIR,&opts);
	if ( fstatus != SLURM_SUCCESS ) {
		error("unable to create SLURM cgroup directory '%s'. aborting",
		      CGROUP_SLURMDIR);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

int _slurm_cgroup_create(slurmd_job_t *job,uint32_t id,uid_t uid,gid_t gid)
{
	int fstatus;

	xcgroup_opts_t opts;
	uint32_t cur_memlimit,cur_memswlimit;

	/* build user cgroup path if no set (should not be) */
	if ( *user_cgroup_path == '\0' ) {
		if ( snprintf(user_cgroup_path,PATH_MAX,CGROUP_SLURMDIR 
			      "/uid_%u",uid) >= PATH_MAX ) {
			error("unable to build uid %u cgroup filepath : %m",
			      uid);
			return SLURM_ERROR;
		}
	}

	/* build job cgroup path if no set (should not be) */
	if ( *job_cgroup_path == '\0' ) {
		if ( snprintf(job_cgroup_path,PATH_MAX,"%s/job_%u",
			      user_cgroup_path,job->jobid) >= PATH_MAX ) {
			error("unable to build job %u cgroup filepath : %m",
			      job->jobid);
			return SLURM_ERROR;
		}
	}

	/* build job step cgroup path (should not be) */
	if ( *jobstep_cgroup_path == '\0' ) {
		if ( snprintf(jobstep_cgroup_path,PATH_MAX,"%s/step_%u",
			      job_cgroup_path,job->stepid) >= PATH_MAX ) {
			error("unable to build job step %u cgroup filepath "
			      ": %m",job->stepid);
			return SLURM_ERROR;
		}
	}

	/* create user cgroup (it could already exists) */
	opts.uid=getuid();
	opts.gid=getgid();
	opts.create_only=0;
	opts.notify=1;
	if ( xcgroup_create(user_cgroup_path,&opts)
	     != SLURM_SUCCESS )
		return SLURM_ERROR;
	if ( slurm_cgroup_conf->user_cgroup_params )
		xcgroup_set_params(user_cgroup_path,
				   slurm_cgroup_conf->user_cgroup_params);
	
	/*
	 * if memory constraints have to be added to uid cgroup 
	 * use_hierachy=1 must be set here, but this would result
	 * in impossibility to configure some job memory parameters
	 * differently, so skip this stage for now
	 */

	/* create job cgroup (it could already exists) */
	opts.uid=getuid();
	opts.gid=getgid();
	opts.create_only=0;
	opts.notify=1;
	if ( xcgroup_create(job_cgroup_path,&opts)
	     != SLURM_SUCCESS )
		return SLURM_ERROR;
	
	/* job cgroup parameters must be set before any sub cgroups 
	   are created */
	xcgroup_set_mem_use_hierarchy(job_cgroup_path,1);
	if ( slurm_cgroup_conf->job_cgroup_params )
		xcgroup_set_params(job_cgroup_path,
				   slurm_cgroup_conf->job_cgroup_params);

	/*
	 *  Warning: OOM Killer must be disabled for slurmstepd
	 *  or it would be destroyed if the application use
	 *  more memory than permitted
	 *
	 *  If an env value is already set for slurmstepd
	 *  OOM killer behavior, keep it, otherwise set the 
	 *  -17 value, wich means do not let OOM killer kill it
	 *  
	 *  FYI, setting "export SLURMSTEPD_OOM_ADJ=-17" 
	 *  in /etc/sysconfig/slurm would be the same
	 */
	setenv("SLURMSTEPD_OOM_ADJ","-17",0);

	/* 
	 * FIXME!
	 * Warning, with slurm-2.1.0 job_mem more corresponds to the
	 * missing field jobstep_mem and thus must not be
	 * trusted to set the job mem limit constraint
	 * Due to the lack of jobstep_mem field in slurm-2.1.0
	 * we only allow to extend the amount of allowed memory
	 * as a step requiring less than the max allowed amount
	 * for the job could otherwise reduce the allowed amount of other
	 * already running steps
	 * Thus, as a long as a step comes with a value that is higher
	 * than the current value, we use it as it means that the
	 * job is at least authorized to use this amount
	 * In the future, a jobstep_mem field should be added
	 * to avoid this workaround and be more deterministic
	 *
	 * Unfortunately with this workaround comes a collateral problem ! 
	 * As we propose to alter already fixed limits for both mem and 
	 * mem+swap, we have to respect a certain order while doing the
	 * modification to respect the kernel cgroup implementation
	 * requirements : when sets, memory limit must be lower or equal
	 * to memory+swap limit
	 *
	 * Notes : a limit value of -1 means that the limit was not
	 * previously set
	 * Notes : this whole part should be much more simpler when 
	 * the jobstep_mem field will be added
	 *
	 */

	/*
	 * Get current limits for both mem and mem+swap
	 */
	xcgroup_get_memlimit(job_cgroup_path,&cur_memlimit);
	xcgroup_get_memswlimit(job_cgroup_path,&cur_memswlimit);

	/* 
	 * set memory constraints according to cgroup conf
	 */
	if ( slurm_cgroup_conf->constrain_ram_space &&
	     cur_memlimit == -1 ) {
		uint32_t limit;
		limit = (uint32_t) job->job_mem ;
		limit = (uint32_t) limit *
			( slurm_cgroup_conf->allowed_ram_space / 100.0 ) ;
		xcgroup_set_memlimit(job_cgroup_path,limit);
	}
	if ( slurm_cgroup_conf->constrain_swap_space ) {
		uint32_t limit,memlimit,swaplimit;
		memlimit = (uint32_t) job->job_mem ;
		swaplimit = memlimit ;
		memlimit = (uint32_t) memlimit * 
			( slurm_cgroup_conf->allowed_ram_space / 100.0 ) ;
		swaplimit = (uint32_t) swaplimit * 
			( slurm_cgroup_conf->allowed_swap_space / 100.0 ) ;
		limit = memlimit + swaplimit ;
		/* 
		 * if memlimit was not set in the previous block, 
		 * we have to set it here or it will not be possible 
		 * to set mem+swap limit as the mem limit value could be
		 * higher.
		 * FIXME!
		 * However, due to the restriction mentioned in the previous
		 * block (job_mem...) if a step already set it, we will
		 * have to skip this as if the new amount is bigger
		 * we will not be allowed by the kernel to set it as 
		 * the mem+swap value will certainly be lower. In such 
		 * scenario, we will have to set memlimit after mem+swap limit
		 * to still be clean regarding to cgroup kernel implementation
		 * ( memlimit must be lower or equal to mem+swap limit when
		 * set ). See stage 2 below...
		 */
		if ( !slurm_cgroup_conf->constrain_ram_space && 
		     cur_memlimit == -1 )
			xcgroup_set_memlimit(job_cgroup_path,limit);
		/*
		 * FIXME!
		 * for the reason why we do this, see the previous block too
		 */

		if ( cur_memswlimit == -1 || cur_memswlimit < limit )
			xcgroup_set_memswlimit(job_cgroup_path,limit);
		else
			debug3("keeping previously set mem+swap limit of %uMB"
			       " for '%s'",cur_memswlimit,job_cgroup_path);
		/* 
		 * FIXME!
		 * stage 2
		 */
		if ( !slurm_cgroup_conf->constrain_ram_space && 
		     cur_memlimit != -1 ) {
			/*
			 * FIXME!
			 * for the reason why we do this, see the previous 
			 * block
			 */
			if ( cur_memlimit == -1 || cur_memlimit < limit ) 
				xcgroup_set_memlimit(job_cgroup_path,limit);
			else
				debug3("keeping previously set mem limit of "
				       "%uMB for '%s'",cur_memlimit,
				       job_cgroup_path);
		}
	}
	/*
	 * FIXME!
	 * yet an other stage 2 due to jobstep_mem lack... 
	 * only used when ram_space constraint is enforced
	 */
	if ( slurm_cgroup_conf->constrain_ram_space &&
	     cur_memlimit != -1 ) {
		uint32_t limit;
		limit = (uint32_t) job->job_mem ;
		limit = (uint32_t) limit *
			( slurm_cgroup_conf->allowed_ram_space / 100.0 ) ;
		if ( cur_memlimit == -1 || cur_memlimit < limit )
			xcgroup_set_memlimit(job_cgroup_path,limit);
		else
			debug3("keeping previously set mem limit of "
			       "%uMB for '%s'",cur_memlimit,job_cgroup_path);
	}

	/* set cores constraints if required by conf */
	if ( slurm_cgroup_conf->constrain_cores && 
	     job->job_alloc_cores ) {
		/*
		 * abstract mapping of cores in slurm must
		 * first be mapped into the machine one
		 */
		char* mach;
		if ( xcpuinfo_abs_to_mac(job->job_alloc_cores,&mach) !=
		     XCPUINFO_SUCCESS ) {
			error("unable to convert abstract slurm allocated "
			      "cores '%s' into a valid machine map",
			      job->job_alloc_cores);
		}
		else {
			debug3("allocated cores conversion done : "
			       "%s (abstract) -> %s (machine)",
			       job->job_alloc_cores,mach);
			xcgroup_set_cpuset_cpus(job_cgroup_path,
						mach);
			xfree(mach);
		}
	}
	else if ( ! job->job_alloc_cores ) {
		error("job_alloc_cores not defined for this job! ancestor's conf"
		      " will be used instead");
	}

	/* create the step sub cgroup  (it sould not already exists) */
	opts.uid=uid;
	opts.gid=gid;
	opts.create_only=1;
	opts.notify=1;
	fstatus = xcgroup_create(jobstep_cgroup_path,&opts);
	if ( fstatus != XCGROUP_SUCCESS ) {
		rmdir(job_cgroup_path);
		return fstatus;
	}

	/* set jobstep cgroup parameters */
	if ( slurm_cgroup_conf->jobstep_cgroup_params )
		xcgroup_set_params(jobstep_cgroup_path,
				   slurm_cgroup_conf->jobstep_cgroup_params);

	return fstatus;
}

int _slurm_cgroup_destroy(void)
{
	if ( jobstep_cgroup_path[0] != '\0' )
		xcgroup_destroy(jobstep_cgroup_path);
	
	if ( job_cgroup_path[0] != '\0' )
		xcgroup_destroy(job_cgroup_path);
	
	if ( user_cgroup_path[0] != '\0' )
		xcgroup_destroy(user_cgroup_path);
	
	return SLURM_SUCCESS;
}

int _slurm_cgroup_add_pids(uint32_t id,pid_t* pids,int npids)
{
	if ( *jobstep_cgroup_path == '\0' )
		return SLURM_ERROR;
	
	return xcgroup_add_pids(jobstep_cgroup_path,pids,npids);
}

int
_slurm_cgroup_get_pids(uint32_t id, pid_t **pids, int *npids)
{
	if ( *jobstep_cgroup_path == '\0' )
		return SLURM_ERROR;
	
	return xcgroup_get_pids(jobstep_cgroup_path,pids,npids);
}

int _slurm_cgroup_set_memlimit(uint32_t id,uint32_t memlimit)
{
	if ( *jobstep_cgroup_path == '\0' )
		return SLURM_ERROR;
	
	return xcgroup_set_memlimit(jobstep_cgroup_path,memlimit);
}

int _slurm_cgroup_set_memswlimit(uint32_t id,uint32_t memlimit)
{
	if ( *jobstep_cgroup_path == '\0' )
		return SLURM_ERROR;
	
	return xcgroup_set_memswlimit(jobstep_cgroup_path,memlimit);
}

int
_slurm_cgroup_find_by_pid(uint32_t* pcont_id, pid_t pid)
{
	int fstatus;
	int rc;
	uint32_t cont_id;
	char cpath[PATH_MAX];
	char* token;

	fstatus = xcgroup_find_by_pid(cpath,pid);
	if (  fstatus != SLURM_SUCCESS )
		return fstatus;

	token = rindex(cpath,'/');
	if ( token == NULL ) {
		debug3("pid %u cgroup '%s' does not match %s cgroup pattern",
		      pid,cpath,plugin_type);
		return SLURM_ERROR;
	}

	rc = sscanf(token,"/%u",&cont_id);
	if ( rc == 1 ) {
		if ( pcont_id != NULL )
			*pcont_id=cont_id;
		fstatus = SLURM_SUCCESS;
	}
	else {
		fstatus = SLURM_ERROR;
	}

	return fstatus;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	/* read cgroup configuration */
	if ( read_slurm_cgroup_conf() )
		return SLURM_ERROR;

	/* initialize cpuinfo internal data */
	if ( xcpuinfo_init() != XCPUINFO_SUCCESS ) {
		free_slurm_cgroup_conf();
		return SLURM_ERROR;
	}

	/* initialize cgroup internal data */
	if ( _slurm_cgroup_init() != SLURM_SUCCESS ) {
		xcpuinfo_fini();
		free_slurm_cgroup_conf();
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	_slurm_cgroup_destroy();
	xcpuinfo_fini();
	free_slurm_cgroup_conf();
	return SLURM_SUCCESS;
}

/*
 * Uses slurmd job-step manager's pid as the unique container id.
 */
extern int slurm_container_create ( slurmd_job_t *job )
{
	int fstatus;

	/* create a new cgroup for that container */ 
	fstatus = _slurm_cgroup_create(job,(uint32_t)job->jmgr_pid,
				       job->uid,job->gid);
	if ( fstatus )
		return SLURM_ERROR;

	/* set the cgroup paths to adhoc env variables */
	env_array_overwrite(&job->env,"SLURM_JOB_CGROUP",
			    job_cgroup_path);
	env_array_overwrite(&job->env,"SLURM_STEP_CGROUP",
			    jobstep_cgroup_path);

	/* add slurmstepd pid to this newly created container */
	fstatus = _slurm_cgroup_add_pids((uint32_t)job->jmgr_pid,
					 &(job->jmgr_pid),1);
	if ( fstatus ) {
		_slurm_cgroup_destroy();		
		return SLURM_ERROR;
	}

	/* we use slurmstepd pid as the identifier of the container 
	 * the corresponding cgroup could be found using
	 * _slurm_cgroup_find_by_pid */
	job->cont_id = (uint32_t)job->jmgr_pid;

	return SLURM_SUCCESS;
}

extern int slurm_container_add ( slurmd_job_t *job, pid_t pid )
{
	return _slurm_cgroup_add_pids(job->cont_id,&pid,1);
}

extern int slurm_container_signal ( uint32_t id, int signal )
{
	pid_t* pids = NULL;
	int npids;
	int i;

	if ( _slurm_cgroup_get_pids(id,&pids,&npids) !=
	     SLURM_SUCCESS ) {
		error("unable to get pids list for cont_id=%u",id);
		return SLURM_ERROR;
	}
	
	for ( i = 0 ; i<npids ; i++ ) {
		/* do not kill slurmstepd */
		if ( pids[i] != id ) {
			debug2("killing process %d with signal %d",
			       pids[i],signal);
			kill(pids[i],signal);
		}
	}
	
	xfree(pids);
	
	return SLURM_SUCCESS;
}

extern int slurm_container_destroy ( uint32_t id )
{
	_slurm_cgroup_destroy();
	return SLURM_SUCCESS;
}

extern uint32_t slurm_container_find(pid_t pid)
{
	uint32_t cont_id=-1;
	_slurm_cgroup_find_by_pid(&cont_id,pid);
	return cont_id;
}

extern bool slurm_container_has_pid(uint32_t cont_id, pid_t pid)
{
	int fstatus;
	uint32_t lid;

	fstatus = _slurm_cgroup_find_by_pid(&lid,pid);
	if ( fstatus != SLURM_SUCCESS )
		return false;

	if ( lid == cont_id )
		return true;
	else
		return false;

}

extern int slurm_container_wait(uint32_t cont_id)
{
	int delay = 1;

	if (cont_id == 0 || cont_id == 1) {
		errno = EINVAL;
		return SLURM_ERROR;
	}

	/* Spin until the container is successfully destroyed */
	while (slurm_container_destroy(cont_id) != SLURM_SUCCESS) {
		slurm_container_signal(cont_id, SIGKILL);
		sleep(delay);
		if (delay < 120) {
			delay *= 2;
		} else {
			error("Unable to destroy container %u", cont_id);
		}
	}

	return SLURM_SUCCESS;
}

extern int slurm_container_get_pids(uint32_t cont_id, pid_t **pids, int *npids)
{
	return _slurm_cgroup_get_pids(cont_id,pids,npids);
}
