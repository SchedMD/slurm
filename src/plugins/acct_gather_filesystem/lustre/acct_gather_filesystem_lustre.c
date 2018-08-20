/*****************************************************************************\
 *  acct_gather_filesystem_lustre.c -slurm filesystem accounting plugin for lustre
 *****************************************************************************
 *  Copyright (C) 2013
 *  Written by Bull- Yiannis Georgiou
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

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"
#include "src/common/assoc_mgr.h"
#include "src/common/slurm_acct_gather_filesystem.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"
#include "src/common/slurm_acct_gather_profile.h"

#include "src/slurmd/slurmd/slurmd.h"


/***************************************************************/



#define _DEBUG 1
#define _DEBUG_FILESYSTEM 1
#define FILESYSTEM_DEFAULT_PORT 1

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

const char plugin_name[] = "AcctGatherFilesystem LUSTRE plugin";
const char plugin_type[] = "acct_gather_filesystem/lustre";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

typedef struct {
	time_t last_update_time;
	time_t update_time;
	uint64_t lustre_nb_writes;
	uint64_t lustre_nb_reads;
	uint64_t all_lustre_nb_writes;
	uint64_t all_lustre_nb_reads;
	uint64_t lustre_write_bytes;
	uint64_t lustre_read_bytes;
	uint64_t all_lustre_write_bytes;
	uint64_t all_lustre_read_bytes;
} lustre_sens_t;

static lustre_sens_t lustre_se = {0,0,0,0,0,0,0,0};

static uint64_t debug_flags = 0;
static pthread_mutex_t lustre_lock = PTHREAD_MUTEX_INITIALIZER;
static int tres_pos = -1;

/* Default path to lustre stats */
const char proc_base_path[] = "/proc/fs/lustre";

/**
 *  is lustre fs supported
 **/
static int _check_lustre_fs(void)
{
	static bool set = false;
	static int rc = SLURM_SUCCESS;

	if (!set) {
		uint32_t profile = 0;
		char lustre_directory[BUFSIZ];
		DIR *proc_dir;

		set = true;
		acct_gather_profile_g_get(ACCT_GATHER_PROFILE_RUNNING,
					  &profile);
		if ((profile & ACCT_GATHER_PROFILE_LUSTRE)) {
			snprintf(lustre_directory, BUFSIZ,
				 "%s/llite", proc_base_path);
			proc_dir = opendir(proc_base_path);
			if (!proc_dir) {
				error("%s: not able to read %s %m",
				      __func__, lustre_directory);
				rc = SLURM_FAILURE;
			} else {
				closedir(proc_dir);
			}
		} else
			rc = SLURM_ERROR;
	}

	return rc;
}

/* _read_lustre_counters()
 * Read counters from all mounted lustre fs
 * from the file stats under the directories:
 *
 * /proc/fs/lustre/llite/lustre-xxxx
 *
 * From the file stat we use 2 entries:
 *
 * read_bytes          17996 samples [bytes] 0 4194304 30994606834
 * write_bytes         9007 samples [bytes] 2 4194304 31008331389
 *
 */
static int _read_lustre_counters(void)
{
	char lustre_dir[PATH_MAX];
	DIR *proc_dir;
	struct dirent *entry;
	FILE *fff;
	char buffer[BUFSIZ];


	snprintf(lustre_dir, PATH_MAX, "%s/llite", proc_base_path);

	proc_dir = opendir(lustre_dir);
	if (proc_dir == NULL) {
		error("%s: Cannot open %s %m", __func__, lustre_dir);
		return SLURM_FAILURE;
	}

	while ((entry = readdir(proc_dir))) {
		char *path_stats = NULL;
		bool bread;
		bool bwrote;

		if (xstrcmp(entry->d_name, ".") == 0
		    || xstrcmp(entry->d_name, "..") == 0)
			continue;

		xstrfmtcat(path_stats, "%s/%s/stats",
			   lustre_dir, entry->d_name);
		debug3("%s: Found file %s", __func__, path_stats);

		fff = fopen(path_stats, "r");
		if (fff == NULL) {
			error("%s: Cannot open %s %m", __func__, path_stats);
			xfree(path_stats);
			continue;
		}
		xfree(path_stats);

		bread = bwrote = false;
		while (fgets(buffer, BUFSIZ, fff)) {

			if (bread && bwrote)
				break;

			if (strstr(buffer, "write_bytes")) {
				sscanf(buffer,
				       "%*s %"PRIu64" %*s %*s "
				       "%*d %*d %"PRIu64"",
				       &lustre_se.lustre_nb_writes,
				       &lustre_se.lustre_write_bytes);
				debug3("%s "
				       "%"PRIu64" "
				       "write_bytes %"PRIu64" "
				       "writes",
				       __func__,
				       lustre_se.lustre_write_bytes,
				       lustre_se.lustre_nb_writes);
				bwrote = true;
			}

			if (strstr(buffer, "read_bytes")) {
				sscanf(buffer,
				       "%*s %"PRIu64" %*s %*s "
				       "%*d %*d %"PRIu64"",
				       &lustre_se.lustre_nb_reads,
				       &lustre_se.lustre_read_bytes);
				debug3("%s "
				       "%"PRIu64" "
				       "read_bytes %"PRIu64" "
				       "reads",
				       __func__,
				       lustre_se.lustre_read_bytes,
				       lustre_se.lustre_nb_reads);
				bread = true;
			}
		}
		fclose(fff);

		lustre_se.all_lustre_write_bytes +=
			lustre_se.lustre_write_bytes;
		lustre_se.all_lustre_read_bytes += lustre_se.lustre_read_bytes;
		lustre_se.all_lustre_nb_writes += lustre_se.lustre_nb_writes;
		lustre_se.all_lustre_nb_reads += lustre_se.lustre_nb_reads;
		debug3("%s: all_lustre_write_bytes %"PRIu64" "
		       "all_lustre_read_bytes %"PRIu64"",
		       __func__, lustre_se.all_lustre_write_bytes,
		       lustre_se.all_lustre_read_bytes);
		debug3("%s: all_lustre_nb_writes %"PRIu64" "
		       "all_lustre_nb_reads %"PRIu64"",
		       __func__, lustre_se.all_lustre_nb_writes,
		       lustre_se.all_lustre_nb_reads);

	} /* while ((entry = readdir(proc_dir)))  */
	closedir(proc_dir);

	lustre_se.last_update_time = lustre_se.update_time;
	lustre_se.update_time = time(NULL);

	return SLURM_SUCCESS;
}




/*
 * _thread_update_node_energy calls _read_ipmi_values and updates all values
 * for node consumption
 */
static int _update_node_filesystem(void)
{
	static acct_gather_data_t previous;
	static int dataset_id = -1;
	static bool first = true;
	acct_gather_data_t current;

	enum {
		FIELD_READ,
		FIELD_READMB,
		FIELD_WRITE,
		FIELD_WRITEMB,
		FIELD_CNT
	};

	acct_gather_profile_dataset_t dataset[] = {
		{ "Reads", PROFILE_FIELD_UINT64 },
		{ "ReadMB", PROFILE_FIELD_DOUBLE },
		{ "Writes", PROFILE_FIELD_UINT64 },
		{ "WriteMB", PROFILE_FIELD_DOUBLE },
		{ NULL, PROFILE_FIELD_NOT_SET }
	};

	union {
		double d;
		uint64_t u64;
	} data[FIELD_CNT];

	slurm_mutex_lock(&lustre_lock);

	if (_read_lustre_counters() != SLURM_SUCCESS) {
		error("%s: Cannot read lustre counters", __func__);
		slurm_mutex_unlock(&lustre_lock);
		return SLURM_FAILURE;
	}

	if (first) {
		dataset_id = acct_gather_profile_g_create_dataset(
			"Filesystem", NO_PARENT, dataset);
		if (dataset_id == SLURM_ERROR) {
			error("FileSystem: Failed to create the dataset "
			      "for Lustre");
			slurm_mutex_unlock(&lustre_lock);
			return SLURM_ERROR;
		}

		previous.num_reads = lustre_se.all_lustre_nb_reads;
		previous.num_writes = lustre_se.all_lustre_nb_writes;
		previous.size_read = lustre_se.all_lustre_read_bytes;
		previous.size_write = lustre_se.all_lustre_write_bytes;

		first = false;
	}

	if (dataset_id < 0) {
		slurm_mutex_unlock(&lustre_lock);
		return SLURM_ERROR;
	}

	/* Compute the current values read from all lustre-xxxx directories */
	current.num_reads = lustre_se.all_lustre_nb_reads;
	current.num_writes = lustre_se.all_lustre_nb_writes;
	current.size_read = lustre_se.all_lustre_read_bytes;
	current.size_write = lustre_se.all_lustre_write_bytes;

	/* record sample */
	data[FIELD_READ].u64 = current.num_reads - previous.num_reads;
	data[FIELD_READMB].d =
		(double)(current.size_read - previous.size_read) / (1 << 20);
	data[FIELD_WRITE].u64 = current.num_writes - previous.num_writes;
	data[FIELD_WRITEMB].d =
		(double)(current.size_write - previous.size_write) / (1 << 20);

	if (debug_flags & DEBUG_FLAG_PROFILE) {
		char str[256];
		info("PROFILE-Lustre: %s", acct_gather_profile_dataset_str(
			     dataset, data, str, sizeof(str)));
	}
	acct_gather_profile_g_add_sample_data(dataset_id, (void *)data,
					      lustre_se.update_time);

	/* Save current as previous and clean up the working
	 * data structure.
	 */
	memcpy(&previous, &current, sizeof(acct_gather_data_t));
	memset(&lustre_se, 0, sizeof(lustre_sens_t));

	slurm_mutex_unlock(&lustre_lock);

	return SLURM_SUCCESS;
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
	slurmdb_tres_rec_t tres_rec;

	if (!_run_in_daemon())
		return SLURM_SUCCESS;

	debug_flags = slurm_get_debug_flags();

	memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
	tres_rec.type = "fs";
	tres_rec.name = "lustre";
	tres_pos = assoc_mgr_find_tres_pos(&tres_rec, false);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	if (!_run_in_daemon())
		return SLURM_SUCCESS;

	if (debug_flags & DEBUG_FLAG_FILESYSTEM)
		info("lustre: ended");

	return SLURM_SUCCESS;
}

extern int acct_gather_filesystem_p_node_update(void)
{
	if (_run_in_daemon() && (_check_lustre_fs() == SLURM_SUCCESS))
		_update_node_filesystem();

	return SLURM_SUCCESS;
}


extern void acct_gather_filesystem_p_conf_set(s_p_hashtbl_t *tbl)
{
	if (!_run_in_daemon())
		return;

	debug("%s loaded", plugin_name);
}

extern void acct_gather_filesystem_p_conf_options(s_p_options_t **full_options,
						  int *full_options_cnt)
{

	return;
}

extern void acct_gather_filesystem_p_conf_values(List *data)
{
	return;
}

extern int acct_gather_filesystem_p_get_data(acct_gather_data_t *data)
{
	int retval = SLURM_SUCCESS;

	if ((tres_pos == -1) || !data) {
		debug2("%s: We are not tracking TRES fs/lustre", __func__);
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&lustre_lock);

	if (_read_lustre_counters() != SLURM_SUCCESS) {
		error("%s: Cannot read lustre counters", __func__);
		slurm_mutex_unlock(&lustre_lock);
		return SLURM_FAILURE;
	}

	/* Obtain the current values read from all lustre-xxxx directories */

	data[tres_pos].num_reads = lustre_se.all_lustre_nb_reads;
	data[tres_pos].num_writes = lustre_se.all_lustre_nb_writes;
	data[tres_pos].size_read = lustre_se.all_lustre_read_bytes;
	data[tres_pos].size_write = lustre_se.all_lustre_write_bytes;

	slurm_mutex_unlock(&lustre_lock);
	return retval;
}
