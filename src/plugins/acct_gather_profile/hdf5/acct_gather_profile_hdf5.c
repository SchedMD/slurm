/*****************************************************************************\
 *  acct_gather_profile_hdf5.c - slurm energy accounting plugin for
 *                               hdf5 profiling.
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Portions Copyright (C) 2013 SchedMD LLC.
 *
 *  Initially written by Rod Schultz <rod.schultz@bull.com> @ Bull
 *  and Danny Auble <da@schedmd.com> @ SchedMD.
 *  Adapted by Yoann Blein <yoann.blein@bull.net> @ Bull.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
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
#include "src/common/slurm_time.h"
#include "src/slurmd/common/proctrack.h"
#include "hdf5_api.h"

#define HDF5_CHUNK_SIZE 10
/* Compression level, a value of 0 through 9. Level 0 is faster but offers the
 * least compression; level 9 is slower but offers maximum compression.
 * A setting of -1 indicates that no compression is desired. */
/* TODO: Make this configurable with a parameter */
#define HDF5_COMPRESS 0

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "AcctGatherProfile hdf5 plugin";
const char plugin_type[] = "acct_gather_profile/hdf5";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

typedef struct {
	char *dir;
	uint32_t def;
} slurm_hdf5_conf_t;

typedef struct {
	hid_t  table_id;
	size_t type_size;
} table_t;

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
static uint32_t g_profile_running = ACCT_GATHER_PROFILE_NOT_SET;
static stepd_step_rec_t *g_job = NULL;
static time_t step_start_time;

static hid_t *groups = NULL;
static size_t groups_len = 0;
static table_t *tables = NULL;
static size_t   tables_max_len = 0;
static size_t   tables_cur_len = 0;

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

static void _create_directories(void)
{
	char *user_dir = NULL;

	xassert(g_job);
	xassert(hdf5_conf.dir);

	xstrfmtcat(user_dir, "%s/%s", hdf5_conf.dir, g_job->user_name);

	/*
	 * To avoid race conditions (TOCTOU) with stat() calls, always
	 * attempt to create the ProfileHDF5Dir and the user directory within.
	 */
	if (((mkdir(hdf5_conf.dir, 0755)) < 0) && (errno != EEXIST))
		fatal("mkdir(%s): %m", hdf5_conf.dir);
	if (chmod(hdf5_conf.dir, 0755) < 0)
		fatal("chmod(%s): %m", hdf5_conf.dir);

	if (((mkdir(user_dir, 0700)) < 0) && (errno != EEXIST))
		fatal("mkdir(%s): %m", user_dir);
	if (chmod(user_dir, 0700) < 0)
		fatal("chmod(%s): %m", user_dir);
	if (chown(user_dir, g_job->uid, g_job->gid) < 0)
		fatal("chown(%s): %m", user_dir);

	xfree(user_dir);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	if (!running_in_slurmstepd())
		return SLURM_SUCCESS;

	/* Move HDF5 trace printing to log file instead of stderr */
	H5Eset_auto(H5E_DEFAULT, (herr_t (*)(hid_t, void *))H5Eprint,
	            log_fp());

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	xfree(tables);
	xfree(groups);
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

	char *profile_file_name;

	xassert(running_in_slurmstepd());

	g_job = job;

	xassert(hdf5_conf.dir);

	log_flag(PROFILE, "PROFILE: option --profile=%s",
		 acct_gather_profile_to_string(g_job->profile));

	if (g_profile_running == ACCT_GATHER_PROFILE_NOT_SET)
		g_profile_running = _determine_profile();

	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return rc;

	_create_directories();

	/*
	 * Use a more user friendly string "batch" rather
	 * then 4294967294.
	 */
	if (g_job->step_id.step_id == SLURM_BATCH_SCRIPT) {
		profile_file_name = xstrdup_printf("%s/%s/%u_%s_%s.h5",
						   hdf5_conf.dir,
						   g_job->user_name,
						   g_job->step_id.job_id,
						   "batch",
						   g_job->node_name);
	} else {
		profile_file_name = xstrdup_printf(
			"%s/%s/%u_%u_%s.h5",
			hdf5_conf.dir, g_job->user_name,
			g_job->step_id.job_id, g_job->step_id.step_id,
			g_job->node_name);
	}

	log_flag(PROFILE, "PROFILE: node_step_start, opt=%s file=%s",
		 acct_gather_profile_to_string(g_profile_running),
		 profile_file_name);

	/*
	 * Create a new file using the default properties
	 */
	file_id = H5Fcreate(profile_file_name, H5F_ACC_TRUNC, H5P_DEFAULT,
			    H5P_DEFAULT);
	if (chown(profile_file_name, (uid_t)g_job->uid,
		  (gid_t)g_job->gid) < 0)
		error("chown(%s): %m", profile_file_name);
	if (chmod(profile_file_name, 0600) < 0)
		error("chmod(%s): %m", profile_file_name);
	xfree(profile_file_name);

	if (file_id < 1) {
		info("PROFILE: Failed to create Node group");
		return SLURM_ERROR;
	}
	/*
	 * fd_set_close_on_exec(file_id); Not supported for HDF5
	 */
	sprintf(group_node, "/%s", g_job->node_name);
	gid_node = make_group(file_id, group_node);
	if (gid_node < 0) {
		H5Fclose(file_id);
		file_id = -1;
		info("PROFILE: Failed to create Node group");
		return SLURM_ERROR;
	}
	put_int_attribute(gid_node, ATTR_NODEINX, g_job->nodeid);
	put_string_attribute(gid_node, ATTR_NODENAME, g_job->node_name);
	put_int_attribute(gid_node, ATTR_NTASKS, g_job->node_tasks);
	put_int_attribute(gid_node, ATTR_CPUPERTASK, g_job->cpus_per_task);

	step_start_time = time(NULL);
	put_string_attribute(gid_node, ATTR_STARTTIME,
			     slurm_ctime2(&step_start_time));

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
	size_t i;

	xassert(running_in_slurmstepd());

	xassert(g_profile_running != ACCT_GATHER_PROFILE_NOT_SET);

	// No check for --profile as we always want to close the HDF5 file
	// if it has been opened.


	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return rc;

	log_flag(PROFILE, "PROFILE: node_step_end (shutdown)");

	/* close tables */
	for (i = 0; i < tables_cur_len; ++i) {
		H5PTclose(tables[i].table_id);
	}
	/* close groups */
	for (i = 0; i < groups_len; ++i) {
		H5Gclose(groups[i]);
	}

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

	xassert(running_in_slurmstepd());
	xassert(g_job);

	xassert(g_profile_running != ACCT_GATHER_PROFILE_NOT_SET);

	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return rc;

	log_flag(PROFILE, "PROFILE: task_start");

	return rc;
}

extern int acct_gather_profile_p_task_end(pid_t taskpid)
{
	log_flag(PROFILE, "PROFILE: task_end");
	return SLURM_SUCCESS;
}

extern int64_t acct_gather_profile_p_create_group(const char* name)
{
	hid_t gid_group = make_group(gid_node, name);
	if (gid_group < 0) {
		return SLURM_ERROR;
	}

	/* store the group to keep track of it */
	groups = xrealloc(groups, (groups_len + 1) * sizeof(hid_t));
	groups[groups_len] = gid_group;
	++groups_len;

	return gid_group;
}

extern int acct_gather_profile_p_create_dataset(
	const char* name, int64_t parent,
	acct_gather_profile_dataset_t *dataset)
{
	size_t type_size;
	size_t offset, field_size;
	hid_t dtype_id;
	hid_t field_id;
	hid_t table_id;
	acct_gather_profile_dataset_t *dataset_loc = dataset;

	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return SLURM_ERROR;

	debug("acct_gather_profile_p_create_dataset %s", name);

	/* compute the size of the type needed to create the table */
	type_size = sizeof(uint64_t) * 2; /* size for time field */
	while (dataset_loc && (dataset_loc->type != PROFILE_FIELD_NOT_SET)) {
		switch (dataset_loc->type) {
		case PROFILE_FIELD_UINT64:
			type_size += sizeof(uint64_t);
			break;
		case PROFILE_FIELD_DOUBLE:
			type_size += sizeof(double);
			break;
		case PROFILE_FIELD_NOT_SET:
			break;
		}
		dataset_loc++;
	}

	/* create the datatype for the dataset */
	if ((dtype_id = H5Tcreate(H5T_COMPOUND, type_size)) < 0) {
		debug3("PROFILE: failed to create datatype for table %s",
		       name);
		return SLURM_ERROR;
	}

	/* insert fields */
	if (H5Tinsert(dtype_id, "ElapsedTime", 0,
		      H5T_NATIVE_UINT64) < 0)
		return SLURM_ERROR;
	if (H5Tinsert(dtype_id, "EpochTime", sizeof(uint64_t),
		      H5T_NATIVE_UINT64) < 0)
		return SLURM_ERROR;

	dataset_loc = dataset;

	offset = sizeof(uint64_t) * 2;
	while (dataset_loc && (dataset_loc->type != PROFILE_FIELD_NOT_SET)) {
		switch (dataset_loc->type) {
		case PROFILE_FIELD_UINT64:
			field_id = H5T_NATIVE_UINT64;
			field_size = sizeof(uint64_t);
			break;
		case PROFILE_FIELD_DOUBLE:
			field_id = H5T_NATIVE_DOUBLE;
			field_size = sizeof(double);
			break;
		default:
			error("%s: unknown field type:%d",
			      __func__, dataset_loc->type);
			continue;
		}
		if (H5Tinsert(dtype_id, dataset_loc->name,
			      offset, field_id) < 0)
			return SLURM_ERROR;
		offset += field_size;
		dataset_loc++;
	}

	/* create the table */
	if (parent < 0)
		parent = gid_node; /* default parent is the node group */
	table_id = H5PTcreate_fl(parent, name, dtype_id, HDF5_CHUNK_SIZE,
	                         HDF5_COMPRESS);
	if (table_id < 0) {
		error("PROFILE: Impossible to create the table %s", name);
		H5Tclose(dtype_id);
		return SLURM_ERROR;
	}
	H5Tclose(dtype_id); /* close the datatype since H5PT keeps a copy */

	/* resize the tables array if full */
	if (tables_cur_len == tables_max_len) {
		if (tables_max_len == 0)
			++tables_max_len;
		tables_max_len *= 2;
		tables = xrealloc(tables, tables_max_len * sizeof(table_t));
	}

	/* reserve a new table */
	tables[tables_cur_len].table_id  = table_id;
	tables[tables_cur_len].type_size = type_size;
	++tables_cur_len;

	return tables_cur_len - 1;
}

extern int acct_gather_profile_p_add_sample_data(int table_id, void *data,
						 time_t sample_time)
{
	table_t *ds = &tables[table_id];
	uint8_t send_data[ds->type_size];
	int header_size = 0;
	debug("acct_gather_profile_p_add_sample_data %d", table_id);

	if (file_id < 0) {
		debug("PROFILE: Trying to add data but profiling is over");
		return SLURM_SUCCESS;
	}

	if (table_id < 0 || table_id >= tables_cur_len) {
		error("PROFILE: trying to add samples to an invalid table %d",
		      table_id);
		return SLURM_ERROR;
	}

	/* ensure that we have to record something */
	xassert(running_in_slurmstepd());
	xassert(g_job);
	xassert(g_profile_running != ACCT_GATHER_PROFILE_NOT_SET);

	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return SLURM_ERROR;

	/* prepend timestampe and relative time */
	((uint64_t *)send_data)[0] = difftime(sample_time, step_start_time);
	header_size += sizeof(uint64_t);
	((uint64_t *)send_data)[1] = sample_time;
	header_size += sizeof(uint64_t);

	memcpy(send_data + header_size, data, ds->type_size - header_size);

	/* append the record to the table */
	if (H5PTappend(ds->table_id, 1, send_data) < 0) {
		error("PROFILE: Impossible to add data to the table %d; "
		      "maybe the table has not been created?", table_id);
		return SLURM_ERROR;
	}

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

extern bool acct_gather_profile_p_is_active(uint32_t type)
{
	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return false;
	return (type == ACCT_GATHER_PROFILE_NOT_SET)
		|| (g_profile_running & type);
}
