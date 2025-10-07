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
#include "src/interfaces/acct_gather_filesystem.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/interfaces/proctrack.h"
#include "src/interfaces/acct_gather_profile.h"

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
	bool first; /* distinguish first FS read */
	uint64_t p_read_bytes; /* previous bytes read. */
	uint64_t p_read_samples; /* previous number of read samples. */
	uint64_t p_write_bytes; /* previous bytes written. */
	uint64_t p_write_samples; /* previous number of write samples. */
	uint64_t read_bytes; /* cumulative bytes read. */
	uint64_t read_samples; /* cumulative read samples. */
	char *stats_file; /* filename containing the stats */
	time_t update_time; /* time of last plugin stats sampling. */
	uint64_t write_bytes; /* cumulative bytes written. */
	uint64_t write_samples; /* cumulative write samples. */
} lustre_stats_t;

list_t *lstats_list = NULL;
static pthread_mutex_t lustre_lock = PTHREAD_MUTEX_INITIALIZER;
static int tres_pos = -1;
static uint64_t total_read_samples = 0;
static uint64_t total_read_bytes = 0;
static uint64_t total_write_samples = 0;
static uint64_t total_write_bytes = 0;
time_t update_time; /* time of last plugin stats sampling. */

void _lustre_destroy_stats(void *object)
{
	lustre_stats_t *lstats = (lustre_stats_t *) object;

	if (lstats) {
		xfree(lstats->stats_file);
	}
}

static int _list_find_file(void *x, void *key)
{
	lustre_stats_t *found_stats = x;
	char *stats_file = key;
	if (stats_file)
		return (!xstrcasecmp(found_stats->stats_file, stats_file));
	return 0;
}

void _set_current_as_prev_lstats(lustre_stats_t *lstats)
{
	lstats->p_read_samples = lstats->read_samples;
	lstats->p_read_bytes = lstats->read_bytes;
	lstats->p_write_samples = lstats->write_samples;
	lstats->p_write_bytes = lstats->write_bytes;
}

/*
 * _llite_path()
 *
 * returns the path to Lustre clients stats (depends on Lustre version)
 * or NULL if none found
 *
 */
static char *_llite_path(void)
{
	static char *llite_path = NULL;
	int i = 0;
	DIR *llite_dir;
	static char *test_paths[] = {
		"/proc/fs/lustre/llite",
		"/sys/kernel/debug/lustre/llite",
		NULL
	};

	if (llite_path)
		return llite_path;

	while ((llite_path = test_paths[i++])) {
		if ((llite_dir = opendir(llite_path))) {
			closedir(llite_dir);
			return llite_path;
		}

		debug("%s: unable to open %s %m", __func__, llite_path);
	}
	return NULL;
}


/*
 * _check_lustre_fs()
 *
 * check if Lustre is supported
 *
 */
static int _check_lustre_fs(void)
{
	static bool set = false;
	static int rc = SLURM_SUCCESS;

	if (!set) {
		uint32_t profile = 0;

		set = true;
		acct_gather_profile_g_get(ACCT_GATHER_PROFILE_RUNNING,
					  &profile);
		if ((profile & ACCT_GATHER_PROFILE_LUSTRE)) {
			char *llite_path = _llite_path();
			if (!llite_path) {
				error("%s: can't find Lustre stats", __func__);
				rc = SLURM_ERROR;
			} else
				debug("%s: using Lustre stats in %s",
				      __func__, llite_path);
		} else
			rc = SLURM_ERROR;
	}

	return rc;
}

/* _read_lustre_counters()
 *
 * Read counters from all mounted lustre fs
 * from the file stats under the directories:
 *
 * /proc/fs/lustre/llite/lustre-xxxx
 *  or
 * /sys/kernel/debug/lustre/llite/lustre-xxxx
 *
 * From the file stat we use 2 entries:
 *
 * read_bytes          17996 samples [bytes] 0 4194304 30994606834
 * write_bytes         9007 samples [bytes] 2 4194304 31008331389
 *
 */
static int _read_lustre_counters(bool logged)
{
	char *lustre_dir;
	DIR *proc_dir;
	struct dirent *entry;
	lustre_stats_t *lstats;
	FILE *fff;
	char buffer[BUFSIZ];

	lustre_dir = _llite_path();
	if (!lustre_dir) {
		if (!logged)
			error("%s: can't find Lustre stats", __func__);
		return SLURM_ERROR;
	}

	proc_dir = opendir(lustre_dir);
	if (!proc_dir) {
		if (!logged)
			error("%s: Cannot open %s %m", __func__, lustre_dir);
		return SLURM_ERROR;
	}

	while ((entry = readdir(proc_dir))) {
		char *path_stats = NULL;
		bool bread;
		bool bwrote;

		if (!xstrcmp(entry->d_name, ".") ||
		    !xstrcmp(entry->d_name, ".."))
			continue;

		xstrfmtcat(path_stats, "%s/%s/stats",
			   lustre_dir, entry->d_name);
		debug3("%s: Found file %s", __func__, path_stats);

		fff = fopen(path_stats, "r");
		if (!fff) {
			error("%s: Cannot open %s %m", __func__, path_stats);
			xfree(path_stats);
			continue;
		}
		if (!(lstats = list_find_first_ro(lstats_list, _list_find_file,
						  path_stats))) {
			lstats = xmalloc(sizeof(lustre_stats_t));
			lstats->stats_file = xstrdup(path_stats);
			lstats->first = true;
			list_append(lstats_list, lstats);
			debug3("Creating lstats for file %s", path_stats);
		}

		xfree(path_stats);

		bread = bwrote = false;
		while (fgets(buffer, BUFSIZ, fff)) {

			if (bread && bwrote)
				break;

			if (strstr(buffer, "write_bytes")) {
				sscanf(buffer,
				       "%*s %"PRIu64" %*s %*s %*d %*d %"PRIu64,
				       &lstats->write_samples,
				       &lstats->write_bytes);
				debug3("%s %"PRIu64" write_bytes %"PRIu64" writes",
				       __func__, lstats->write_bytes,
				       lstats->write_samples);
				bwrote = true;
			}

			if (strstr(buffer, "read_bytes")) {
				sscanf(buffer,
				       "%*s %"PRIu64" %*s %*s %*d %*d %"PRIu64,
				       &lstats->read_samples,
				       &lstats->read_bytes);
				debug3("%s %"PRIu64" read_bytes %"PRIu64" reads",
				       __func__, lstats->read_bytes,
				       lstats->read_samples);
				bread = true;
			}
		}
		fclose(fff);

		/* On first iteration set prev = current */
		if (lstats->first) {
			_set_current_as_prev_lstats(lstats);
			lstats->first = false;
		}

		/*
		 * If stats are reset or overflow mid-execution we set prev
		 * to 0 in order to minimize data loss
		 */
		if (lstats->p_read_samples > lstats->read_samples)
			lstats->p_read_samples = 0;
		if (lstats->p_read_bytes > lstats->read_bytes)
			lstats->p_read_bytes = 0;
		if (lstats->p_write_samples > lstats->write_samples)
			lstats->p_write_samples = 0;
		if (lstats->p_write_bytes > lstats->write_bytes)
			lstats->p_write_bytes = 0;

		/* Add up the read data to the totals */
		total_write_bytes +=
			lstats->write_bytes - lstats->p_write_bytes;
		total_read_bytes += lstats->read_bytes - lstats->p_read_bytes;
		total_write_samples +=
			lstats->write_samples - lstats->p_write_samples;
		total_read_samples +=
			lstats->read_samples - lstats->p_read_samples;

		/* Set current as previous values */
		_set_current_as_prev_lstats(lstats);

		debug3("%s: write_bytes %"PRIu64" read_bytes %"PRIu64,
		       __func__, total_write_bytes, total_read_bytes);
		debug3("%s: write_samples %"PRIu64" read_samples %"PRIu64,
		       __func__, total_write_samples, total_read_samples);
	} /* while ((entry = readdir(proc_dir))) */

	update_time = time(NULL);

	closedir(proc_dir);

	return SLURM_SUCCESS;
}

/*
 *_update_node_filesystem()
 *
 * acct_gather_filesystem_p_node_update calls _update_node_filesystem and
 * updates all values for node Lustre usage
 *
 */
static int _update_node_filesystem(void)
{
	static int dataset_id = -1;
	static bool first = true;
	static int errors = 0;
	char str[256];

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

	if (_read_lustre_counters(errors) != SLURM_SUCCESS) {
		if (!errors)
			error("%s: Cannot read lustre counters", __func__);
		errors++;
		slurm_mutex_unlock(&lustre_lock);
		return SLURM_ERROR;
	}

	if (errors) {
		info("%s: lustre counters successfully read after %d errors",
		     __func__, errors);
		errors = 0;
	}

	if (first) {
		dataset_id = acct_gather_profile_g_create_dataset(
			"Filesystem", NO_PARENT, dataset);
		if (dataset_id == SLURM_ERROR) {
			error("FileSystem: Failed to create the dataset for Lustre");
			slurm_mutex_unlock(&lustre_lock);
			return SLURM_ERROR;
		}

		first = false;
	}

	if (dataset_id < 0) {
		slurm_mutex_unlock(&lustre_lock);
		return SLURM_ERROR;
	}

	/* Compute the current values read from all lustre-xxxx directories */
	data[FIELD_READ].u64 = total_read_samples;
	data[FIELD_READMB].d = (double) (total_read_bytes) / (1 << 20);
	data[FIELD_WRITE].u64 = total_write_samples;
	data[FIELD_WRITEMB].d = (double) (total_write_bytes) / (1 << 20);

	/* record sample */
	log_flag(PROFILE, "PROFILE-Lustre: %s",
		 acct_gather_profile_dataset_str(dataset, data, str,
						 sizeof(str)));
	acct_gather_profile_g_add_sample_data(dataset_id, (void *) data,
					      update_time);

	slurm_mutex_unlock(&lustre_lock);

	return SLURM_SUCCESS;
}

extern int init(void)
{
	slurmdb_tres_rec_t tres_rec;

	if (!running_in_slurmstepd())
		return SLURM_SUCCESS;

	memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
	tres_rec.type = "fs";
	tres_rec.name = "lustre";
	tres_pos = assoc_mgr_find_tres_pos(&tres_rec, false);
	lstats_list = list_create(_lustre_destroy_stats);

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	if (!running_in_slurmstepd())
		return;

	FREE_NULL_LIST(lstats_list);

	debug("lustre: ended");
}

extern int acct_gather_filesystem_p_node_update(void)
{
	if (running_in_slurmstepd() && (_check_lustre_fs() == SLURM_SUCCESS))
		_update_node_filesystem();

	return SLURM_SUCCESS;
}


extern void acct_gather_filesystem_p_conf_set(s_p_hashtbl_t *tbl)
{
	if (!running_in_slurmstepd())
		return;

	debug("%s loaded", plugin_name);
}

extern void acct_gather_filesystem_p_conf_options(s_p_options_t **full_options,
						  int *full_options_cnt)
{

	return;
}

extern void acct_gather_filesystem_p_conf_values(list_t **data)
{
	return;
}

extern int acct_gather_filesystem_p_get_data(acct_gather_data_t *data)
{
	int retval = SLURM_SUCCESS;
	static int errors = 0;

	if ((tres_pos == -1) || !data) {
		debug2("%s: We are not tracking TRES fs/lustre", __func__);
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&lustre_lock);

	if (_read_lustre_counters(errors) != SLURM_SUCCESS) {
		if (!errors)
			error("%s: cannot read lustre counters", __func__);
		errors++;
		slurm_mutex_unlock(&lustre_lock);
		return SLURM_ERROR;
	}

	if (errors) {
		info("%s: lustre counters successfully read after %d errors",
		     __func__, errors);
		errors = 0;
	}

	/* Obtain the current values read from all lustre-xxxx directories */
	data[tres_pos].num_reads = total_read_samples;
	data[tres_pos].num_writes = total_write_samples;
	data[tres_pos].size_read = (double) (total_read_bytes) / (1 << 20);
	data[tres_pos].size_write = (double) (total_write_bytes) / (1 << 20);

	slurm_mutex_unlock(&lustre_lock);

	return retval;
}
