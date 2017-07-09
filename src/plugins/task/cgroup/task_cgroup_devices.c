/***************************************************************************** \
 *  task_cgroup_devices.c - devices cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2011 BULL
 *  Written by Yiannis Georgiou <yiannis.georgiou@bull.fr>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#define _GNU_SOURCE
#include <glob.h>
#include <limits.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef MAJOR_IN_MKDEV
#  include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#  include <sys/sysmacros.h>
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/xstring.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "task_cgroup.h"

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];
static char cgroup_allowed_devices_file[PATH_MAX];

static xcgroup_ns_t devices_ns;

static xcgroup_t user_devices_cg;
static xcgroup_t job_devices_cg;
static xcgroup_t step_devices_cg;

static void _calc_device_major(char *dev_path[PATH_MAX],
			       char *dev_major[PATH_MAX],
			       int lines);

static int read_allowed_devices_file(char *allowed_devices[PATH_MAX]);

extern int task_cgroup_devices_init(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	uint16_t cpunum;

	/* initialize cpuinfo internal data */
	if ( xcpuinfo_init() != XCPUINFO_SUCCESS )
		return SLURM_ERROR;

	/* initialize user/job/jobstep cgroup relative paths */
	user_cgroup_path[0] = '\0';
	job_cgroup_path[0] = '\0';
	jobstep_cgroup_path[0] = '\0';
	/* initialize allowed_devices_filename */
	cgroup_allowed_devices_file[0] = '\0';

	if ( get_procs(&cpunum) != 0 ) {
		error("task/cgroup: unable to get a number of CPU");
		goto error;
	}

	(void) gres_plugin_node_config_load(cpunum, conf->node_name, NULL);

	strcpy(cgroup_allowed_devices_file,
	       slurm_cgroup_conf->allowed_devices_file);
	if (xcgroup_ns_create(slurm_cgroup_conf, &devices_ns, "", "devices")
	    != XCGROUP_SUCCESS ) {
		error("task/cgroup: unable to create devices namespace");
		goto error;
	}

	return SLURM_SUCCESS;

error:
	xcgroup_ns_destroy(&devices_ns);
	xcpuinfo_fini();
	return SLURM_ERROR;
}

extern int task_cgroup_devices_fini(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	xcgroup_t devices_cg;

	/* Similarly to task_cgroup_{memory,cpuset}_fini(), we must lock the
	 * root cgroup so we don't race with another job step that is
	 * being started.  */
        if (xcgroup_create(&devices_ns, &devices_cg,"",0,0)
	    == XCGROUP_SUCCESS) {
                if (xcgroup_lock(&devices_cg) == XCGROUP_SUCCESS) {
			int i = 0, npids = 0, cnt = 0;
			pid_t* pids = NULL;
			/* First move slurmstepd to the root devices cg
			 * so we can remove the step/job/user devices
			 * cg's.  */
			xcgroup_move_process(&devices_cg, getpid());

			/* There is a delay in the cgroup system when moving the
			 * pid from one cgroup to another.  This is usually
			 * short, but we need to wait to make sure the pid is
			 * out of the step cgroup or we will occur an error
			 * leaving the cgroup unable to be removed.
			 */
			do {
				xcgroup_get_pids(&step_devices_cg,
						 &pids, &npids);
				for (i = 0 ; i<npids ; i++)
					if (pids[i] == getpid()) {
						cnt++;
						break;
					}
				xfree(pids);
			} while ((i < npids) && (cnt < MAX_MOVE_WAIT));

			if (cnt < MAX_MOVE_WAIT)
				debug3("Took %d checks before stepd pid was removed from the step cgroup.",
				       cnt);
			else
				error("Pid %d is still in the step cgroup.  It might be left uncleaned after the job.",
				      getpid());

			if (xcgroup_delete(&step_devices_cg) != SLURM_SUCCESS)
                                debug2("task/cgroup: unable to remove step "
                                       "devices : %m");
                        if (xcgroup_delete(&job_devices_cg) != XCGROUP_SUCCESS)
                                debug2("task/cgroup: not removing "
                                       "job devices : %m");
                        if (xcgroup_delete(&user_devices_cg)
			    != XCGROUP_SUCCESS)
                                debug2("task/cgroup: not removing "
                                       "user devices : %m");
                        xcgroup_unlock(&devices_cg);
                } else
                        error("task/cgroup: unable to lock root devices : %m");
                xcgroup_destroy(&devices_cg);
        } else
                error("task/cgroup: unable to create root devices : %m");

	if ( user_cgroup_path[0] != '\0' )
		xcgroup_destroy(&user_devices_cg);
	if ( job_cgroup_path[0] != '\0' )
		xcgroup_destroy(&job_devices_cg);
	if ( jobstep_cgroup_path[0] != '\0' )
		xcgroup_destroy(&step_devices_cg);

	user_cgroup_path[0] = '\0';
	job_cgroup_path[0] = '\0';
	jobstep_cgroup_path[0] = '\0';

	cgroup_allowed_devices_file[0] = '\0';

	xcgroup_ns_destroy(&devices_ns);

	xcpuinfo_fini();
	return SLURM_SUCCESS;
}

extern int task_cgroup_devices_create(stepd_step_rec_t *job)
{
	int f, k, rc, gres_conf_lines, allow_lines;
	int fstatus = SLURM_ERROR;
	char **gres_name = NULL;
	char **gres_cgroup = NULL, **dev_path = NULL;
	char *allowed_devices[PATH_MAX], *allowed_dev_major[PATH_MAX];
	int *gres_job_bit_alloc = NULL;
	int *gres_step_bit_alloc = NULL;
	int *gres_count = NULL;
	xcgroup_t devices_cg;
	uint32_t jobid = job->jobid;
	uint32_t stepid = job->stepid;
	uid_t uid = job->uid;
	uid_t gid = job->gid;

	List job_gres_list = job->job_gres_list;
	List step_gres_list = job->step_gres_list;

	char* slurm_cgpath ;

	/* create slurm root cgroup in this cgroup namespace */
	slurm_cgpath = task_cgroup_create_slurm_cg(&devices_ns);
	if (slurm_cgpath == NULL)
		return SLURM_ERROR;

	/* build user cgroup relative path if not set (should not be) */
	if (*user_cgroup_path == '\0') {
		if (snprintf(user_cgroup_path, PATH_MAX, "%s/uid_%u",
			     slurm_cgpath, uid) >= PATH_MAX) {
			error("unable to build uid %u cgroup relative path : %m",
			      uid);
			xfree(slurm_cgpath);
			return SLURM_ERROR;
		}
	}
	xfree(slurm_cgpath);

	/* build job cgroup relative path if no set (should not be) */
	if (*job_cgroup_path == '\0') {
		if (snprintf(job_cgroup_path, PATH_MAX, "%s/job_%u",
			     user_cgroup_path, jobid) >= PATH_MAX) {
			error("task/cgroup: unable to build job %u devices "
			      "cgroup relative path : %m", jobid);
			return SLURM_ERROR;
		}
	}

	/* build job step cgroup relative path (should not be) */
	if (*jobstep_cgroup_path == '\0') {
		int cc;
		if (stepid == SLURM_BATCH_SCRIPT) {
			cc = snprintf(jobstep_cgroup_path, PATH_MAX,
				      "%s/step_batch", job_cgroup_path);
		} else if (stepid == SLURM_EXTERN_CONT) {
			cc = snprintf(jobstep_cgroup_path, PATH_MAX,
				      "%s/step_extern", job_cgroup_path);
		} else {
			cc = snprintf(jobstep_cgroup_path, PATH_MAX,
				     "%s/step_%u",
				     job_cgroup_path, stepid);
		}
		if (cc >= PATH_MAX) {
			error("task/cgroup: unable to build job step %u.%u "
			      "devices cgroup relative path : %m",
			      jobid, stepid);
			return SLURM_ERROR;
		}
	}

	/*
	 * create devices root cgroup and lock it
	 *
	 * we will keep the lock until the end to avoid the effect of a release
	 * agent that would remove an existing cgroup hierarchy while we are
	 * setting it up. As soon as the step cgroup is created, we can release
	 * the lock.
	 * Indeed, consecutive slurm steps could result in cgroup being removed
	 * between the next EEXIST instanciation and the first addition of
	 * a task. The release_agent will have to lock the root devices cgroup
	 * to avoid this scenario.
	 */
	if (xcgroup_create(&devices_ns, &devices_cg, "", 0, 0) !=
	    XCGROUP_SUCCESS ) {
		error("task/cgroup: unable to create root devices cgroup");
		return SLURM_ERROR;
	}
	if (xcgroup_lock(&devices_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&devices_cg);
		error("task/cgroup: unable to lock root devices cgroup");
		return SLURM_ERROR;
	}

	info("task/cgroup: manage devices jor job '%u'", jobid);

	 /*
	  * collect info concerning the gres.conf file
	  * the GRES devices paths and the GRES names
	  */
	gres_conf_lines = gres_plugin_node_config_devices_path(&dev_path,
							       &gres_name,
							       job->node_name);

	/*
	 * create the entry for cgroup devices subsystem with major minor
	 */
	gres_cgroup = xmalloc(sizeof(char *) * gres_conf_lines);
	_calc_device_major(dev_path, gres_cgroup, gres_conf_lines);

	/*
         * create the entry with major minor for the default allowed devices
         * read from the file
         */
	allow_lines = read_allowed_devices_file(allowed_devices);
	_calc_device_major(allowed_devices, allowed_dev_major, allow_lines);

	/*
	 * calculate the number of gres.conf records for each gres name
	 */
	gres_count = xmalloc(sizeof(int) * gres_conf_lines);
	f = 0;
	gres_count[f] = 1;
	for (k = 0; k < gres_conf_lines; k++) {
		if ((k+1 < gres_conf_lines) &&
		    (xstrcmp(gres_name[k], gres_name[k+1]) == 0))
			gres_count[f]++;
		if ((k+1 < gres_conf_lines) &&
		    (xstrcmp(gres_name[k], gres_name[k+1]) != 0)) {
			f++;
			gres_count[f] = 1;
		}
	}

	/*
	 * create user cgroup in the devices ns (it could already exist)
	 */
	if (xcgroup_create(&devices_ns, &user_devices_cg, user_cgroup_path,
			   getuid(), getgid()) != XCGROUP_SUCCESS) {
		goto error;
	}
	if (xcgroup_instantiate(&user_devices_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_devices_cg);
		goto error;
	}

	/* TODO
	 * check that user's devices cgroup is consistant and allow the
	 * appropriate devices
	 */


	/*
	 * create job cgroup in the devices ns (it could already exist)
	 */
	if (xcgroup_create(&devices_ns, &job_devices_cg, job_cgroup_path,
			    getuid(), getgid()) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_devices_cg);
		goto error;
	}
	if (xcgroup_instantiate(&job_devices_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_devices_cg);
		xcgroup_destroy(&job_devices_cg);
		goto error;
	}

	/* fetch information concerning the gres devices allocation for the job */
	gres_job_bit_alloc = xmalloc(sizeof (int) * (gres_conf_lines + 10));
	gres_plugin_job_state_file(job_gres_list, gres_job_bit_alloc,
				   gres_count);

	/*
	 * with the current cgroup devices subsystem design (whitelist only
	 * supported) we need to allow all different devices that are supposed
	 * to be allowed by* default.
	 */
	for (k = 0; k < allow_lines; k++) {
		info("Default access allowed to device %s",
		     allowed_dev_major[k]);
		xcgroup_set_param(&job_devices_cg, "devices.allow",
				  allowed_dev_major[k]);
	}

	/*
         * allow or deny access to devices according to job GRES permissions
         */
	for (k = 0; k < gres_conf_lines; k++) {
		if (gres_job_bit_alloc[k] == 1) {
			info("Allowing access to device %s", gres_cgroup[k]);
			xcgroup_set_param(&job_devices_cg, "devices.allow",
                                          gres_cgroup[k]);
		} else {
			info("Not allowing access to device %s", gres_cgroup[k]);
			xcgroup_set_param(&job_devices_cg, "devices.deny",
					  gres_cgroup[k]);
		}
	}

	/*
	 * create step cgroup in the devices ns (it should not exists)
	 * use job's user uid/gid to enable tasks cgroups creation by
	 * the user inside the step cgroup owned by root
	 */
	if (xcgroup_create(&devices_ns, &step_devices_cg, jobstep_cgroup_path,
			   uid, gid) != XCGROUP_SUCCESS ) {
		/* do not delete user/job cgroup as */
		/* they can exist for other steps */
		xcgroup_destroy(&user_devices_cg);
		xcgroup_destroy(&job_devices_cg);
		goto error;
	}
	if ( xcgroup_instantiate(&step_devices_cg) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_devices_cg);
		xcgroup_destroy(&job_devices_cg);
		xcgroup_destroy(&step_devices_cg);
		goto error;
	}


	if ((job->stepid != SLURM_BATCH_SCRIPT) &&
	    (job->stepid != SLURM_EXTERN_CONT)) {

		/* fetch information about step GRES devices allocation */
		gres_step_bit_alloc = xmalloc(sizeof (int) * (gres_conf_lines + 10));
		gres_plugin_step_state_file(step_gres_list, gres_step_bit_alloc,
					    gres_count);

		/*
		 * with the current cgroup devices subsystem design (whitelist
		 * only supported) we need to allow all different devices that
		 * are supposed to be allowed by default.
		 */
		for (k = 0; k < allow_lines; k++) {
			info("Default access allowed to device %s",
			     allowed_dev_major[k]);
			xcgroup_set_param(&step_devices_cg, "devices.allow",
					  allowed_dev_major[k]);
		}

		/*
		 * allow or deny access to devices according to GRES permissions
		 * for the step
		 */
		for (k = 0; k < gres_conf_lines; k++) {
			if (gres_step_bit_alloc[k] == 1) {
				info("Allowing access to device %s for step",
				     gres_cgroup[k]);
				xcgroup_set_param(&step_devices_cg,
						 "devices.allow",
						  gres_cgroup[k]);
			} else {
				info("Not allowing access to device %s for step",
				     gres_cgroup[k]);
				xcgroup_set_param(&step_devices_cg,
						 "devices.deny",
						  gres_cgroup[k]);
			}
		}
	}
	/* attach the slurmstepd to the step devices cgroup */
	pid_t pid = getpid();
	rc = xcgroup_add_pids(&step_devices_cg, &pid, 1);
	if (rc != XCGROUP_SUCCESS) {
		error("task/cgroup: unable to add slurmstepd to devices cg '%s'",
		      step_devices_cg.path);
		fstatus = SLURM_ERROR;
	} else {
		fstatus = SLURM_SUCCESS;
	}

error:
	xcgroup_unlock(&devices_cg);
	xcgroup_destroy(&devices_cg);
	xfree(gres_step_bit_alloc);
	xfree(gres_job_bit_alloc);
	xfree(gres_count);
	xfree(gres_name);
	xfree(dev_path);
	xfree(gres_cgroup);

	return fstatus;
}

extern int task_cgroup_devices_attach_task(stepd_step_rec_t *job)
{
	int fstatus = SLURM_ERROR;

	/* tasks are automatically attached as slurmstepd is in the step cg */
	fstatus = SLURM_SUCCESS;

	return fstatus;
}

static void _calc_device_major(char *dev_path[PATH_MAX],
				char *dev_major[PATH_MAX],
				int lines)
{

	int k, major, minor;
	char str1[256], str2[256];
	struct stat fs;

	if (lines > PATH_MAX) {
		error("task/cgroup: more devices configured than table size "
		      "(%d > %d)", lines, PATH_MAX);
		lines = PATH_MAX;
	}
	for (k = 0; k < lines; k++) {
		stat(dev_path[k], &fs);
		major = (int)major(fs.st_rdev);
		minor = (int)minor(fs.st_rdev);
		debug3("device : %s major %d, minor %d\n",
			dev_path[k], major, minor);
		memset(str1, 0, sizeof(str1));
		if (S_ISBLK(fs.st_mode)) {
			sprintf(str1, "b %d:", major);
			//info("device is block ");
		}
		if (S_ISCHR(fs.st_mode)) {
			sprintf(str1, "c %d:", major);
			//info("device is character ");
		}
		sprintf(str2, "%d rwm", minor);
		strcat(str1, str2);
		dev_major[k] = xstrdup((char *)str1);
	}
}


static int read_allowed_devices_file(char **allowed_devices)
{

	FILE *file = fopen (cgroup_allowed_devices_file, "r" );
	int i, l, num_lines = 0;
	char line[256];
	glob_t globbuf;

	for( i=0; i<256; i++ )
		line[i] = '\0';

	if ( file != NULL ){
		while ( fgets ( line, sizeof line, file ) != NULL ){
			line[strlen(line)-1] = '\0';

			/* global pattern matching and return the list of matches*/
			if (glob(line, GLOB_NOSORT, NULL, &globbuf) != 0){
				debug3("Device %s does not exist", line);
			}else{
				for(l=0; l < globbuf.gl_pathc; l++){
					allowed_devices[num_lines] =
						xstrdup(globbuf.gl_pathv[l]);
					num_lines++;
				}
			}
		}
		fclose ( file );
	}
	else
		perror (cgroup_allowed_devices_file);

	return num_lines;
}


extern int task_cgroup_devices_add_pid(pid_t pid)
{
	return xcgroup_add_pids(&step_devices_cg, &pid, 1);
}
