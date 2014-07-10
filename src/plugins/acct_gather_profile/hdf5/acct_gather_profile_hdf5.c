/*****************************************************************************\
 *  acct_gather_profile_hdf5.c - slurm energy accounting plugin for
 *                               hdf5 profiling.
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  Portions Copyright (C) 2013 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#include "src/common/slurm_xlator.h"
#include "src/common/fd.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"
#include "hdf5_api.h"

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
const char plugin_name[] = "AcctGatherProfile hdf5 plugin";
const char plugin_type[] = "acct_gather_profile/hdf5";
const uint32_t plugin_version = 100;

hid_t typTOD;

typedef struct {
	char *dir;
	uint32_t def;
} slurm_hdf5_conf_t;

// Global HDF5 Variables
//	The HDF5 file and base objects will remain open for the duration of the
//	step. This avoids reconstruction on every acct_gather_sample and
//	flushing the buffers on every put.
// Static variables ok as add function are inside a lock.
static hid_t 	 file_id = -1; // File
static hid_t     gid_node = -1;
static hid_t     gid_tasks = -1;
static hid_t     gid_samples = -1;
static hid_t     gid_totals = -1;
static char      group_node[MAX_GROUP_NAME+1];
static slurm_hdf5_conf_t hdf5_conf;
static uint64_t debug_flags = 0;
static uint32_t g_profile_running = ACCT_GATHER_PROFILE_NOT_SET;
static stepd_step_rec_t *g_job = NULL;

static void _reset_slurm_profile_conf(void)
{
	xfree(hdf5_conf.dir);
	hdf5_conf.def = ACCT_GATHER_PROFILE_NONE;
}

static uint32_t _determine_profile(void)
{
	uint32_t profile;

	xassert(g_job);

	if (g_profile_running != ACCT_GATHER_PROFILE_NOT_SET)
		profile = g_profile_running;
	else if (g_job->profile >= ACCT_GATHER_PROFILE_NONE)
		profile = g_job->profile;
	else
		profile = hdf5_conf.def;

	return profile;
}

static int _get_taskid_from_pid(pid_t pid, uint32_t *gtid)
{
	int tx;

	xassert(g_job);

	for (tx=0; tx<g_job->node_tasks; tx++) {
		if (g_job->task[tx]->pid == pid) {
			*gtid = g_job->task[tx]->gtid;
			return SLURM_SUCCESS;
		}
	}

	return SLURM_ERROR;
}

static int _create_directories(void)
{
	int rc;
	struct stat st;
	char   *user_dir = NULL;

	xassert(g_job);
	xassert(hdf5_conf.dir);
	/*
	 * If profile director does not exist, try to create it.
	 *  Otherwise, ensure path is a directory as expected, and that
	 *  we have permission to write to it.
	 *  also make sure the subdirectory tmp exists.
	 */

	if (((rc = stat(hdf5_conf.dir, &st)) < 0) && (errno == ENOENT)) {
		if (mkdir(hdf5_conf.dir, 0755) < 0)
			fatal("mkdir(%s): %m", hdf5_conf.dir);
	} else if (rc < 0)
		fatal("Unable to stat acct_gather_profile_dir: %s: %m",
		      hdf5_conf.dir);
	else if (!S_ISDIR(st.st_mode))
		fatal("acct_gather_profile_dir: %s: Not a directory!",
		      hdf5_conf.dir);
	else if (access(hdf5_conf.dir, R_OK|W_OK|X_OK) < 0)
		fatal("Incorrect permissions on acct_gather_profile_dir: %s",
		      hdf5_conf.dir);
	chmod(hdf5_conf.dir, 0755);

	user_dir = xstrdup_printf("%s/%s", hdf5_conf.dir, g_job->user_name);
	if (((rc = stat(user_dir, &st)) < 0) && (errno == ENOENT)) {
		if (mkdir(user_dir, 0700) < 0)
			fatal("mkdir(%s): %m", user_dir);
	}
	chmod(user_dir, 0700);
	if (chown(user_dir, (uid_t)g_job->uid,
		  (gid_t)g_job->gid) < 0)
		error("chown(%s): %m", user_dir);

	xfree(user_dir);

	return SLURM_SUCCESS;
}

static bool _do_profile(uint32_t profile, uint32_t req_profiles)
{
	if (req_profiles <= ACCT_GATHER_PROFILE_NONE)
		return false;
	if ((profile == ACCT_GATHER_PROFILE_NOT_SET)
	    || (req_profiles & profile))
		return true;

	return false;
}

static bool _run_in_daemon(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmstepd");
	}

	return run;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	if (!_run_in_daemon())
		return SLURM_SUCCESS;

	debug_flags = slurm_get_debug_flags();

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	xfree(hdf5_conf.dir);
	return SLURM_SUCCESS;
}

extern void acct_gather_profile_p_conf_options(s_p_options_t **full_options,
					       int *full_options_cnt)
{
	s_p_options_t options[] = {
		{"ProfileHDF5Dir", S_P_STRING},
		{"ProfileHDF5Default", S_P_STRING},
		{NULL} };

	transfer_s_p_options(full_options, options, full_options_cnt);
	return;
}

extern void acct_gather_profile_p_conf_set(s_p_hashtbl_t *tbl)
{
	char *tmp = NULL;
	_reset_slurm_profile_conf();
	if (tbl) {
		s_p_get_string(&hdf5_conf.dir, "ProfileHDF5Dir", tbl);

		if (s_p_get_string(&tmp, "ProfileHDF5Default", tbl)) {
			hdf5_conf.def = acct_gather_profile_from_string(tmp);
			if (hdf5_conf.def == ACCT_GATHER_PROFILE_NOT_SET) {
				fatal("ProfileHDF5Default can not be "
				      "set to %s, please specify a valid "
				      "option", tmp);
			}
			xfree(tmp);
		}
	}

	if (!hdf5_conf.dir)
		fatal("No ProfileHDF5Dir in your acct_gather.conf file.  "
		      "This is required to use the %s plugin", plugin_type);

	debug("%s loaded", plugin_name);
}

extern void acct_gather_profile_p_get(enum acct_gather_profile_info info_type,
				      void *data)
{
	uint32_t *uint32 = (uint32_t *) data;
	char **tmp_char = (char **) data;

	switch (info_type) {
	case ACCT_GATHER_PROFILE_DIR:
		*tmp_char = xstrdup(hdf5_conf.dir);
		break;
	case ACCT_GATHER_PROFILE_DEFAULT:
		*uint32 = hdf5_conf.def;
		break;
	case ACCT_GATHER_PROFILE_RUNNING:
		*uint32 = g_profile_running;
		break;
	default:
		debug2("acct_gather_profile_p_get info_type %d invalid",
		       info_type);
	}
}

extern int acct_gather_profile_p_node_step_start(stepd_step_rec_t* job)
{
	int rc = SLURM_SUCCESS;

	time_t start_time;
	char    *profile_file_name;
	char *profile_str;

	xassert(_run_in_daemon());

	g_job = job;

	if (g_job->stepid == NO_VAL) {
		g_profile_running = ACCT_GATHER_PROFILE_NONE;
		return rc;
	}

	xassert(hdf5_conf.dir);

	if (debug_flags & DEBUG_FLAG_PROFILE) {
		profile_str = acct_gather_profile_to_string(g_job->profile);
		info("PROFILE: option --profile=%s", profile_str);
	}

	if (g_profile_running == ACCT_GATHER_PROFILE_NOT_SET)
		g_profile_running = _determine_profile();

	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return rc;

	_create_directories();

	profile_file_name = xstrdup_printf(
		"%s/%s/%u_%u_%s.h5",
		hdf5_conf.dir, g_job->user_name,
		g_job->jobid, g_job->stepid, g_job->node_name);

	if (debug_flags & DEBUG_FLAG_PROFILE) {
		profile_str = acct_gather_profile_to_string(g_profile_running);
		info("PROFILE: node_step_start, opt=%s file=%s",
		     profile_str, profile_file_name);
	}

	// Create a new file using the default properties.
	profile_init();
	file_id = H5Fcreate(profile_file_name, H5F_ACC_TRUNC, H5P_DEFAULT,
			    H5P_DEFAULT);
	if (chown(profile_file_name, (uid_t)g_job->uid,
		  (gid_t)g_job->gid) < 0)
		error("chown(%s): %m", profile_file_name);
	chmod(profile_file_name,  0600);
	xfree(profile_file_name);

	if (file_id < 1) {
		info("PROFILE: Failed to create Node group");
		return SLURM_FAILURE;
	}
	/* fd_set_close_on_exec(file_id); Not supported for HDF5 */
	sprintf(group_node, "/%s_%s", GRP_NODE, g_job->node_name);
	gid_node = H5Gcreate(file_id, group_node, H5P_DEFAULT,
			     H5P_DEFAULT, H5P_DEFAULT);
	if (gid_node < 1) {
		H5Fclose(file_id);
		file_id = -1;
		info("PROFILE: Failed to create Node group");
		return SLURM_FAILURE;
	}
	put_string_attribute(gid_node, ATTR_NODENAME, g_job->node_name);
	put_int_attribute(gid_node, ATTR_NTASKS, g_job->node_tasks);
	start_time = time(NULL);
	put_string_attribute(gid_node, ATTR_STARTTIME,
			     slurm_ctime(&start_time));

	return rc;
}

extern int acct_gather_profile_p_child_forked(void)
{
	if (gid_totals > 0)
		H5Gclose(gid_totals);
	if (gid_samples > 0)
		H5Gclose(gid_samples);
	if (gid_tasks > 0)
		H5Gclose(gid_tasks);
	if (gid_node > 0)
		H5Gclose(gid_node);
	if (file_id > 0)
		H5Fclose(file_id);

	return SLURM_SUCCESS;
}

extern int acct_gather_profile_p_node_step_end(void)
{
	int rc = SLURM_SUCCESS;

	xassert(_run_in_daemon());

	if (g_job->stepid == NO_VAL)
		return rc;

	xassert(g_profile_running != ACCT_GATHER_PROFILE_NOT_SET);

	// No check for --profile as we always want to close the HDF5 file
	// if it has been opened.


	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return rc;

	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: node_step_end (shutdown)");

	if (gid_totals > 0)
		H5Gclose(gid_totals);
	if (gid_samples > 0)
		H5Gclose(gid_samples);
	if (gid_tasks > 0)
		H5Gclose(gid_tasks);
	if (gid_node > 0)
		H5Gclose(gid_node);
	if (file_id > 0)
		H5Fclose(file_id);
	profile_fini();
	file_id = -1;

	return rc;
}

extern int acct_gather_profile_p_task_start(uint32_t taskid)
{
	int rc = SLURM_SUCCESS;

	xassert(_run_in_daemon());
	xassert(g_job);

	if (g_job->stepid == NO_VAL)
		return rc;

	xassert(g_profile_running != ACCT_GATHER_PROFILE_NOT_SET);

	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return rc;

	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: task_start");

	return rc;
}

extern int acct_gather_profile_p_task_end(pid_t taskpid)
{
	hid_t   gid_task;
	char 	group_task[MAX_GROUP_NAME+1];
	uint32_t task_id;
	int rc = SLURM_SUCCESS;

	xassert(_run_in_daemon());
	xassert(g_job);

	if (g_job->stepid == NO_VAL)
		return rc;

	xassert(g_profile_running != ACCT_GATHER_PROFILE_NOT_SET);

	if (!_do_profile(ACCT_GATHER_PROFILE_NOT_SET, g_profile_running))
		return rc;

	if (_get_taskid_from_pid(taskpid, &task_id) != SLURM_SUCCESS)
		return SLURM_FAILURE;
	if (file_id == -1) {
		info("PROFILE: add_task_data, HDF5 file is not open");
		return SLURM_FAILURE;
	}
	if (gid_tasks < 0) {
		gid_tasks = make_group(gid_node, GRP_TASKS);
		if (gid_tasks < 1) {
			info("PROFILE: Failed to create Tasks group");
			return SLURM_FAILURE;
		}
	}
	sprintf(group_task, "%s_%d", GRP_TASK, task_id);
	gid_task = get_group(gid_tasks, group_task);
	if (gid_task == -1) {
		gid_task = make_group(gid_tasks, group_task);
		if (gid_task < 0) {
			info("Failed to open tasks %s", group_task);
			return SLURM_FAILURE;
		}
		put_int_attribute(gid_task, ATTR_TASKID, task_id);
	}
	put_int_attribute(gid_task, ATTR_CPUPERTASK, g_job->cpus_per_task);

	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: task_end");
	return rc;
}

extern int acct_gather_profile_p_add_sample_data(uint32_t type, void *data)
{
	hid_t   g_sample_grp;
	char    group[MAX_GROUP_NAME+1];
	char 	group_sample[MAX_GROUP_NAME+1];
	static uint32_t sample_no = 0;
	uint32_t task_id = 0;
	void *send_profile = NULL;
	char *type_name = NULL;

	profile_task_t  profile_task;
	profile_network_t  profile_network;
	profile_energy_t  profile_energy;
	profile_io_t  profile_io;

	struct jobacctinfo *jobacct = (struct jobacctinfo *)data;
	acct_network_data_t *net = (acct_network_data_t *)data;
	acct_energy_data_t *ener = (acct_energy_data_t *)data;
	struct lustre_data *lus = (struct lustre_data *)data;

	xassert(_run_in_daemon());
	xassert(g_job);

	if (g_job->stepid == NO_VAL)
		return SLURM_SUCCESS;

	xassert(g_profile_running != ACCT_GATHER_PROFILE_NOT_SET);

	if (!_do_profile(type, g_profile_running))
		return SLURM_SUCCESS;

	switch (type) {
	case ACCT_GATHER_PROFILE_ENERGY:
		snprintf(group, sizeof(group), "%s", GRP_ENERGY);

		memset(&profile_energy, 0, sizeof(profile_energy_t));
		profile_energy.time = ener->time;
		profile_energy.cpu_freq = ener->cpu_freq;
		profile_energy.power = ener->power;

		send_profile = &profile_energy;
		break;
	case ACCT_GATHER_PROFILE_TASK:
		if (_get_taskid_from_pid(jobacct->pid, &task_id)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;

		snprintf(group, sizeof(group), "%s_%u", GRP_TASK, task_id);

		memset(&profile_task, 0, sizeof(profile_task_t));
		profile_task.time = time(NULL);
		profile_task.cpu_freq = jobacct->act_cpufreq;
		profile_task.cpu_time = jobacct->tot_cpu;
		profile_task.cpu_utilization = jobacct->tot_cpu;
		profile_task.pages = jobacct->tot_pages;
		profile_task.read_size = jobacct->tot_disk_read;
		profile_task.rss = jobacct->tot_rss;
		profile_task.vm_size = jobacct->tot_vsize;
		profile_task.write_size = jobacct->tot_disk_write;

		send_profile = &profile_task;
		break;
	case ACCT_GATHER_PROFILE_LUSTRE:
		snprintf(group, sizeof(group), "%s", GRP_LUSTRE);

		memset(&profile_io, 0, sizeof(profile_io_t));
		profile_io.time = time(NULL);
		profile_io.reads = lus->reads;
		profile_io.read_size = lus->read_size;
		profile_io.writes = lus->writes;
		profile_io.write_size = lus->write_size;

		send_profile = &profile_io;

		break;
	case ACCT_GATHER_PROFILE_NETWORK:

		snprintf(group, sizeof(group), "%s", GRP_NETWORK);

		memset(&profile_network, 0, sizeof(profile_network_t));
		profile_network.time = time(NULL);
		profile_network.packets_in = net->packets_in;
		profile_network.size_in = net->size_in;
		profile_network.packets_out = net->packets_out;
		profile_network.size_out = net->size_out;

		send_profile = &profile_network;

		break;
	default:
		error("acct_gather_profile_p_add_sample_data: "
		      "Unknown type %d sent", type);
		return SLURM_ERROR;
	}

	type_name = acct_gather_profile_type_to_string(type);

	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: add_sample_data Group-%s Type=%s",
		     group, type_name);

	if (file_id == -1) {
		if (debug_flags & DEBUG_FLAG_PROFILE) {
			// This can happen from samples from the gather threads
			// before the step actually starts.
			info("PROFILE: add_sample_data, HDF5 file not open");
		}
		return SLURM_FAILURE;
	}
	if (gid_samples < 0) {
		gid_samples = make_group(gid_node, GRP_SAMPLES);
		if (gid_samples < 1) {
			info("PROFILE: failed to create TimeSeries group");
			return SLURM_FAILURE;
		}
	}
	g_sample_grp = get_group(gid_samples, group);
	if (g_sample_grp < 0) {
		g_sample_grp = make_group(gid_samples, group);
		if (g_sample_grp < 0) {
			info("PROFILE: failed to open TimeSeries %s", group);
			return SLURM_FAILURE;
		}
		put_string_attribute(g_sample_grp, ATTR_DATATYPE, type_name);
	}
	sprintf(group_sample, "%s_%10.10d", group, ++sample_no);
	put_hdf5_data(g_sample_grp, type, SUBDATA_SAMPLE,
		      group_sample, send_profile, 1);
	H5Gclose(g_sample_grp);

	return SLURM_SUCCESS;
}

extern void acct_gather_profile_p_conf_values(List *data)
{
	config_key_pair_t *key_pair;

	xassert(*data);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ProfileHDF5Dir");
	key_pair->value = xstrdup(hdf5_conf.dir);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ProfileHDF5Default");
	key_pair->value = xstrdup(acct_gather_profile_to_string(hdf5_conf.def));
	list_append(*data, key_pair);

	return;

}
