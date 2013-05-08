/*****************************************************************************\
 *  io_energy.c - slurm energy accounting plugin for io and energy using hdf5.
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>

#include "src/common/fd.h"
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/plugins/acct_gather_profile/common/profile_hdf5.h"
#include "src/slurmd/common/proctrack.h"

#include "io_energy.h"

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
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the job accounting API
 * matures.
 */
const char plugin_name[] = "AcctGatherProfile io_energy plugin";
const char plugin_type[] = "acct_gather_profile/io_energy";
const uint32_t plugin_version = 100;

static uint32_t debug_flags = 0;


// Global HDF5 Variables
//	The HDF5 file and base objects will remain open for the duration of the
//	step. This avoids reconstruction on every acct_gather_sample and
//	flushing the buffers on every put.
// Static variables ok as add function are inside a lock.
static uint32_t  jobid;
static uint32_t  stepid;
static uint32_t  nodetasks;
static uint32_t  sampleNo = 0;
static char*     stepd_nodename = NULL;
static char*     profileFileName;
static hid_t 	 file_id = -1; // File
static hid_t     gidNode = -1;
static hid_t     gidTasks = -1;
static hid_t     gidSamples = -1;
static hid_t     gidTotals = -1;
static char      groupNode[MAX_GROUP_NAME+1];
static int       nOpts = 0;
static char**    profileOpts = NULL;
static slurm_acct_gather_conf_t acct_gather_conf;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	debug_flags = slurm_get_debug_flags();
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	xfree(profileFileName);
	return SLURM_SUCCESS;
}

extern void reset_slurm_profile_conf()
{
	xfree(acct_gather_conf.profile_dir);
	xfree(acct_gather_conf.profile_DefaultProfile);
}

extern void acct_gather_profile_p_conf_set(s_p_hashtbl_t *tbl)
{
	reset_slurm_profile_conf();
	if (!tbl)
		return;

	if (!s_p_get_string(&acct_gather_conf.profile_dir,
			    "ProfileDir", tbl)) {
		acct_gather_conf.profile_dir = NULL;
	}
	if (!s_p_get_string(&acct_gather_conf.profile_DefaultProfile,
			    "ProfileDefaultProfile", tbl)) {
		acct_gather_conf.profile_DefaultProfile =
				xstrdup(PROFILE_DEFAULT_PROFILE);
	}

	ValidSeriesList(acct_gather_conf.profile_DefaultProfile);
}

extern void* acct_gather_profile_p_conf_get()
{
	return &acct_gather_conf;
}

extern void acct_gather_profile_p_conf_options(s_p_options_t **full_options,
					      int *full_options_cnt)
{
	s_p_options_t options[] = {
		{"ProfileDir", S_P_STRING},
		{"ProfileDefaultProfile", S_P_STRING},
		{NULL} };

	transfer_s_p_options(full_options, options, full_options_cnt);
	return;
}

extern int acct_gather_profile_p_controller_start()
{
#ifdef HAVE_HDF5
	int rc;
	struct stat st;
	const char*  profdir;
	char   tmpdir[MAX_PROFILE_PATH+1];
	if (acct_gather_conf.profile_dir == NULL) {
		fatal("PROFILE: ProfileDir is required in acct_gather.conf"
		      "with AcctGatherPluginType=io_energy");
	}
	profdir = xstrdup(acct_gather_conf.profile_dir);
	/*
	 * If profile director does not exist, try to create it.
	 *  Otherwise, ensure path is a directory as expected, and that
	 *  we have permission to write to it.
	 *  also make sure the subdirectory tmp exists.
	 */

	if (((rc = stat(profdir, &st)) < 0) && (errno == ENOENT)) {
		if (mkdir(profdir, 0777) < 0)
			fatal("mkdir(%s): %m", profdir);
	}
	else if (rc < 0)
		fatal("Unable to stat acct_gather_profile_dir: %s: %m",
				profdir);
	else if (!S_ISDIR(st.st_mode))
		fatal("acct_gather_profile_dir: %s: Not a directory!",profdir);
	else if (access(profdir, R_OK|W_OK|X_OK) < 0)
		fatal("Incorrect permissions on acct_gather_profile_dir: %s",
				profdir);
	chmod(profdir,0777);
	if ((strlen(profdir)+4) > MAX_PROFILE_PATH)
		fatal("Length of profile director is too long");
	sprintf(tmpdir,"%s/tmp",profdir);
	if (((rc = stat(tmpdir, &st)) < 0) && (errno == ENOENT)) {
		if (mkdir(tmpdir, 0777) < 0)
			fatal("mkdir(%s): %m", tmpdir);
		chmod(tmpdir,0777);
	}
	xfree(profdir);
#endif
	return SLURM_SUCCESS;
}

extern int acct_gather_profile_p_node_step_start(slurmd_job_t* job)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_HDF5
	time_t startTime;
	char*  slurmDataRoot;
	char*  optString;
	jobid = job->jobid;
	stepid = job->stepid;
	if (stepid == NO_VAL)
		return rc;

	if (job->profile)
		optString = job->profile;
	else
		optString = acct_gather_conf.profile_DefaultProfile;

	profileOpts = GetStringList(optString, &nOpts);

	if (strcasecmp(profileOpts[0],"none")  == 0)
		return rc;

	if (acct_gather_conf.profile_dir == NULL) {
		fatal("PROFILE: ProfileDir is required in acct_gather.conf"
				"with AcctGatherPluginType=io_energy");
	}
	slurmDataRoot = xstrdup(acct_gather_conf.profile_dir);

	stepd_nodename = xstrdup(job->node_name);
	nodetasks = job->node_tasks;

	profileFileName = make_node_step_profile_path(slurmDataRoot,
						job->node_name, jobid, stepid);
	xfree(slurmDataRoot);
	if (profileFileName == NULL) {
		info("PROFILE: failed create profileFileName job=%d step=%d",
				jobid,stepid);
	}
	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: node_step_start, opt=%s file=%s",
				optString, profileFileName);

	// Create a new file using the default properties.
	ProfileInit();
	file_id = H5Fcreate(profileFileName, H5F_ACC_TRUNC, H5P_DEFAULT,
			    H5P_DEFAULT);
	if (file_id < 1) {
		info("PROFILE: Failed to create Node group");
		return SLURM_FAILURE;
	}

	sprintf(groupNode,"/%s~%s",GRP_NODE,stepd_nodename);
	gidNode = H5Gcreate(file_id, groupNode, H5P_DEFAULT,
				H5P_DEFAULT, H5P_DEFAULT);
	if (gidNode < 1) {
		H5Fclose(file_id);
		file_id = -1;
		info("PROFILE: Failed to create Node group");
		return SLURM_FAILURE;
	}
	put_string_attribute(gidNode, ATTR_NODENAME, stepd_nodename);
	put_int_attribute(gidNode, ATTR_NTASKS, nodetasks);
	startTime = time(NULL);
	put_string_attribute(gidNode,ATTR_STARTTIME,ctime(&startTime));

#endif
	return rc;
}

extern int acct_gather_profile_p_node_step_end(slurmd_job_t* job)
{
	int rc = SLURM_SUCCESS;
	// No check for --profile as we always want to close the HDF5 file
	// if it has been opened.
#ifdef HAVE_HDF5
	if (job->stepid == NO_VAL) {
		return rc;
	}

	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: node_step_end (shutdown)");

	xfree(stepd_nodename);
	if (gidTotals > 0)
		H5Gclose(gidTotals);
	if (gidSamples > 0)
		H5Gclose(gidSamples);
	if (gidTasks > 0)
		H5Gclose(gidTasks);
	if (gidNode > 0)
		H5Gclose(gidNode);
	if (file_id > 0)
		H5Fclose(file_id);
	ProfileFinish();
	file_id = -1;
#endif
	return rc;
}

extern int acct_gather_profile_p_task_start(slurmd_job_t* job, uint32_t taskid)
{
	int rc = SLURM_SUCCESS;
	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: task_start");

	return rc;
}

extern int acct_gather_profile_p_task_end(slurmd_job_t* job, pid_t taskpid)
{
	hid_t   gidTask;
	char 	groupTask[MAX_GROUP_NAME+1];
	uint64_t taskId;
	int rc = SLURM_SUCCESS;
	if (!DoSeries(NULL, profileOpts, nOpts))
		return rc;

	if (get_taskid_from_pid(taskpid, &taskId) != SLURM_SUCCESS)
		return SLURM_FAILURE;
	if (file_id == -1) {
		info("PROFILE: add_task_data, HDF5 file is not open");
		return SLURM_FAILURE;
	}
	if (gidTasks < 0) {
		gidTasks = make_group(gidNode, GRP_TASKS);
		if (gidTasks < 1) {
			info("PROFILE: Failed to create Tasks group");
			return SLURM_FAILURE;
		}
	}
	sprintf(groupTask,"%s~%d", GRP_TASK,taskId);
	gidTask = get_group(gidTasks, groupTask);
	if (gidTask == -1) {
		gidTask = make_group(gidTasks, groupTask);
		if (gidTask < 0) {
			info("Failed to open tasks %s",groupTask);
			return SLURM_FAILURE;
		}
		put_int_attribute(gidTask,ATTR_TASKID,taskId);
	}
	put_int_attribute(gidTask,ATTR_CPUPERTASK,job->cpus_per_task);

	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: task_end");
	return rc;
}

extern int acct_gather_profile_p_job_sample(void)
{
	int rc = SLURM_SUCCESS;
	if (!DoSeries(NULL, profileOpts, nOpts))
		return rc;
#ifdef HAVE_HDF5
#endif
	return rc;
}

extern int acct_gather_profile_p_add_node_data(slurmd_job_t* job, char* group,
		char* type, void* data)
{
	if (!DoSeries(group, profileOpts, nOpts))
		return SLURM_SUCCESS;
	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: add_node_data Group-%s Type=%s", group, type);

#ifdef HAVE_HDF5
	if (file_id == -1) {
		info("PROFILE: add_node_data, HDF5 file is not open");
		return SLURM_FAILURE;
	}
	if (gidTotals < 0) {
		gidTotals = make_group(gidNode, GRP_TOTALS);
		if (gidTotals < 1) {
			info("PROFILE: failed to create Totals group");
			return SLURM_FAILURE;
		}
	}
	put_hdf5_data(gidTotals, type, SUBDATA_NODE, group, data, 1);
#endif

	return SLURM_SUCCESS;
}

extern int acct_gather_profile_p_add_sample_data(char* group, char* type,
		void* data)
{
	hid_t   gSampleGrp;
	char 	groupSample[MAX_GROUP_NAME+1];

	if (!DoSeries(group, profileOpts, nOpts))
		return SLURM_SUCCESS;
	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: add_sample_data Group-%s Type=%s", group, type);
	sampleNo++;
#ifdef HAVE_HDF5
	if (file_id == -1) {
		if (debug_flags & DEBUG_FLAG_PROFILE) {
			// This can happen from samples from the gather threads
			// before the step actually starts.
			info("PROFILE: add_sample_data, HDF5 file not open");
		}
		return SLURM_FAILURE;
	}
	if (gidSamples < 0) {
		gidSamples = make_group(gidNode, GRP_SAMPLES);
		if (gidSamples < 1) {
			info("PROFILE: failed to create TimeSeries group");
			return SLURM_FAILURE;
		}
	}
	gSampleGrp = get_group(gidSamples, group);
	if (gSampleGrp < 0) {
		gSampleGrp = make_group(gidSamples, group);
		if (gSampleGrp < 0) {
			info("PROFILE: failed to open TimeSeries %s", group);
			return SLURM_FAILURE;
		}
		put_string_attribute(gSampleGrp, ATTR_DATATYPE, type);
	}
	sprintf(groupSample,"%s~%10.10d",group,sampleNo);
	put_hdf5_data(gSampleGrp, type, SUBDATA_SAMPLE, groupSample, data, 1);
	H5Gclose(gSampleGrp);
#endif
	return SLURM_SUCCESS;
}

extern int acct_gather_profile_p_add_task_data(slurmd_job_t* job,
		uint32_t taskid, char* group, char* type, void* data)
{
	hid_t   gidTask, gidTotals;
	char 	groupTask[MAX_GROUP_NAME+1];

	if (!DoSeries(group, profileOpts, nOpts))
		return SLURM_SUCCESS;
	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: add_task_data Group-%s Type=%s", group, type);
#ifdef HAVE_HDF5
	if (file_id == -1) {
		info("PROFILE: add_task_data, HDF5 file is not open");
		return SLURM_FAILURE;
	}
	if (gidTasks < 0) {
		gidTasks = make_group(gidNode, GRP_TASKS);
		if (gidTasks < 1) {
			info("PROFILE: Failed to create Tasks group");
			return SLURM_FAILURE;
		}
	}

	sprintf(groupTask,"%s~%d", GRP_TASK,taskid);
	gidTask = get_group(gidTasks, groupTask);
	if (gidTask == -1) {
		gidTask = make_group(gidTasks, groupTask);
		if (gidTask < 0) {
			info("Failed to open tasks %s",groupTask);
			return SLURM_FAILURE;
		}
		put_int_attribute(gidTask,ATTR_TASKID,taskid);
		put_int_attribute(gidTask,ATTR_CPUPERTASK,taskid);
		gidTotals = make_group(gidTask, GRP_TOTALS);
		if (gidTotals < 0) {
			info("Failed to open %s/%s",groupTask,GRP_TOTALS);
			return SLURM_FAILURE;
		}
	}

	put_hdf5_data(gidTotals, SUBDATA_TOTAL, type, group, data, 1);

	H5Gclose(gidTask);
#endif
	return SLURM_SUCCESS;
}

