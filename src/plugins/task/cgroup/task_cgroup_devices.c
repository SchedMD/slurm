/***************************************************************************** \
 *  task_cgroup_devices.c - devices cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2011 BULL
 *  Written by Yiannis Georgiou <yiannis.georgiou@bull.fr>
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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <sched.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <slurm/slurm_errno.h>
#include <slurm/slurm.h>
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/common/xstring.h"
#include "src/common/gres.h"
#include "src/common/list.h"

#include "task_cgroup.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

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

	(void) gres_plugin_node_config_load(cpunum, conf->node_name);

	strcpy(cgroup_allowed_devices_file,
	       slurm_cgroup_conf->allowed_devices_file);
	if (xcgroup_ns_create(slurm_cgroup_conf, &devices_ns, "", "devices")
	    != XCGROUP_SUCCESS ) {
		error("task/cgroup: unable to create devices namespace");
		goto error;
	}

	return SLURM_SUCCESS;

error:
	xcpuinfo_fini();
	return SLURM_ERROR;
}

extern int task_cgroup_devices_fini(slurm_cgroup_conf_t *slurm_cgroup_conf)
{

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
	char *gres_name[PATH_MAX];
	char *gres_cgroup[PATH_MAX], *dev_path[PATH_MAX];
	char *allowed_devices[PATH_MAX], *allowed_dev_major[PATH_MAX];
	int *gres_bit_alloc = NULL;
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

	/* create slurm root cg in this cg namespace */
	slurm_cgpath = task_cgroup_create_slurm_cg(&devices_ns);
	if ( slurm_cgpath == NULL ) {
		return SLURM_ERROR;
	}

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
	if ( *job_cgroup_path == '\0' ) {
		if ( snprintf(job_cgroup_path,PATH_MAX, "%s/job_%u",
			      user_cgroup_path,jobid) >= PATH_MAX ) {
			error("task/cgroup: unable to build job %u devices "
			      "cg relative path : %m", jobid);
			return SLURM_ERROR;
		}
	}

	/* build job step cgroup relative path (should not be) */
	if ( *jobstep_cgroup_path == '\0' ) {
		int cc;

		if (stepid == SLURM_BATCH_SCRIPT) {
			cc = snprintf(jobstep_cgroup_path, PATH_MAX,
				      "%s/step_batch", job_cgroup_path);
			if (cc >= PATH_MAX) {
				error("task/cgroup: unable to build "
				      "step batch devices cg path : %m");

			}
		} else {

			if (snprintf(jobstep_cgroup_path,PATH_MAX, "%s/step_%u",
				     job_cgroup_path,stepid) >= PATH_MAX ) {
				error("task/cgroup: unable to build job step %u "
				      "devices cg relative path : %m",stepid);
				return SLURM_ERROR;
			}
		}
	}

	/*
	 * create devices root cg and lock it
	 *
	 * we will keep the lock until the end to avoid the effect of a release
	 * agent that would remove an existing cgroup hierarchy while we are
	 * setting it up. As soon as the step cgroup is created, we can release
	 * the lock.
	 * Indeed, consecutive slurm steps could result in cg being removed
	 * between the next EEXIST instanciation and the first addition of
	 * a task. The release_agent will have to lock the root devices cgroup
	 * to avoid this scenario.
	 */
	if ( xcgroup_create(&devices_ns, &devices_cg, "", 0, 0) !=
	     XCGROUP_SUCCESS ) {
		error("task/cgroup: unable to create root devices xcgroup");
		return SLURM_ERROR;
	}
	if ( xcgroup_lock(&devices_cg) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&devices_cg);
		error("task/cgroup: unable to lock root devices cg");
		return SLURM_ERROR;
	}

	info("task/cgroup: manage devices jor job '%u'",jobid);

	 /*
	  * collect info concerning the gres.conf file
	  * the gres devices paths and the gres names
	  */
	gres_conf_lines = gres_plugin_node_config_devices_path(dev_path,
							       gres_name,
							       PATH_MAX,
							       job->node_name);

	/*
	 * create the entry for cgroup devices subsystem with major minor
	 */
	_calc_device_major(dev_path,gres_cgroup,gres_conf_lines);

	allow_lines = read_allowed_devices_file(allowed_devices);

	/*
         * create the entry with major minor for the default allowed devices
         * read from the file
         */
	_calc_device_major(allowed_devices,allowed_dev_major,allow_lines);

	gres_count = xmalloc ( sizeof (int) * (gres_conf_lines) );

	/*
	 * calculate the number of gres.conf records for each gres name
	 *
	 */
	f = 0;
	gres_count[f] = 1;
	for (k = 0; k < gres_conf_lines; k++) {
		if ((k+1 < gres_conf_lines) &&
		    (strcmp(gres_name[k],gres_name[k+1]) == 0))
			gres_count[f]++;
		if ((k+1 < gres_conf_lines) &&
		    (strcmp(gres_name[k],gres_name[k+1]) != 0)) {
			f++;
			gres_count[f] = 1;
		}
	}

	/*
	 * create user cgroup in the devices ns (it could already exist)
	 */
	if ( xcgroup_create(&devices_ns,&user_devices_cg,
			    user_cgroup_path,
			    getuid(),getgid()) != XCGROUP_SUCCESS ) {
		goto error;
	}
	if ( xcgroup_instanciate(&user_devices_cg) != XCGROUP_SUCCESS ) {
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
	if ( xcgroup_create(&devices_ns,&job_devices_cg,
			    job_cgroup_path,
			    getuid(), getgid()) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_devices_cg);
		goto error;
	}
	if ( xcgroup_instanciate(&job_devices_cg) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_devices_cg);
		xcgroup_destroy(&job_devices_cg);
		goto error;
	}

	gres_bit_alloc = xmalloc ( sizeof (int) * (gres_conf_lines + 1));

	/* fetch information concerning the gres devices allocation for the job */
	gres_plugin_job_state_file(job_gres_list, gres_bit_alloc, gres_count);

	/*
	 * with the current cgroup devices subsystem design (whitelist only supported)
	 * we need to allow all different devices that are supposed to be allowed by
	 * default.
	 */
	for (k = 0; k < allow_lines; k++) {
		info("Default access allowed to device %s", allowed_dev_major[k]);
		xcgroup_set_param(&job_devices_cg,"devices.allow",
			allowed_dev_major[k]);
	}

	/*
         * allow or deny access to devices according to gres permissions for the job
         */
	for (k = 0; k < gres_conf_lines; k++) {
		if (gres_bit_alloc[k] == 1) {
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
	if ( xcgroup_create(&devices_ns,&step_devices_cg,
			    jobstep_cgroup_path,
			    uid,gid) != XCGROUP_SUCCESS ) {
		/* do not delete user/job cgroup as */
		/* they can exist for other steps */
		xcgroup_destroy(&user_devices_cg);
		xcgroup_destroy(&job_devices_cg);
		goto error;
	}
	if ( xcgroup_instanciate(&step_devices_cg) != XCGROUP_SUCCESS ) {
		xcgroup_destroy(&user_devices_cg);
		xcgroup_destroy(&job_devices_cg);
		xcgroup_destroy(&step_devices_cg);
		goto error;
	}


	if (job->stepid != SLURM_BATCH_SCRIPT ) {

		gres_step_bit_alloc = xmalloc ( sizeof (int) * (gres_conf_lines + 1));

		/* fetch information concerning the gres devices allocation for the step */
		gres_plugin_step_state_file(step_gres_list, gres_step_bit_alloc,
				    gres_count);


		/*
		 * with the current cgroup devices subsystem design (whitelist only supported)
		 * we need to allow all different devices that are supposed to be allowed by
		 * default.
		 */
		for (k = 0; k < allow_lines; k++) {
			info("Default access allowed to device %s", allowed_dev_major[k]);
			xcgroup_set_param(&step_devices_cg,"devices.allow",
					  allowed_dev_major[k]);
		}

		/*
		 * allow or deny access to devices according to gres permissions for the step
		 */
		for (k = 0; k < gres_conf_lines; k++) {
			if (gres_step_bit_alloc[k] == 1){
				info("Allowing access to device %s for step",
				     gres_cgroup[k]);
				xcgroup_set_param(&step_devices_cg, "devices.allow",
						  gres_cgroup[k]);
			} else {
				info("Not allowing access to device %s for step",
				     gres_cgroup[k]);
			xcgroup_set_param(&step_devices_cg, "devices.deny",
					  gres_cgroup[k]);
			}
		}
	}
	/* attach the slurmstepd to the step devices cgroup */
	pid_t pid = getpid();
	rc = xcgroup_add_pids(&step_devices_cg,&pid,1);
	if ( rc != XCGROUP_SUCCESS ) {
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
	xfree(gres_bit_alloc);
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
