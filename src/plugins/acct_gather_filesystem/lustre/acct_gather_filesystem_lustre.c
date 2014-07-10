/*****************************************************************************\
 *  acct_gather_filesystem_lustre.c -slurm filesystem accounting plugin for lustre
 *****************************************************************************
 *  Copyright (C) 2013
 *  Written by Bull- Yiannis Georgiou
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

const char plugin_name[] = "AcctGatherFilesystem LUSTRE plugin";
const char plugin_type[] = "acct_gather_filesystem/lustre";
const uint32_t plugin_version = 100;


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
			sprintf(lustre_directory, "%s/llite", proc_base_path);
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
	char path_stats[PATH_MAX];
	DIR *proc_dir;
	struct dirent *entry;
	FILE *fff;
	char buffer[BUFSIZ];


	sprintf(lustre_dir, "%s/llite", proc_base_path);

	proc_dir = opendir(lustre_dir);
	if (proc_dir == NULL) {
		error("%s: Cannot open %s %m", __func__, lustre_dir);
		return SLURM_FAILURE;
	}

	while ((entry = readdir(proc_dir))) {
		bool bread;
		bool bwrote;

		if (strcmp(entry->d_name, ".") == 0
		    || strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(path_stats, PATH_MAX - 1, "%s/%s/stats", lustre_dir,
			 entry->d_name);
		debug3("%s: Found file %s", __func__, path_stats);

		fff = fopen(path_stats, "r");
		if (fff == NULL) {
			error("%s: Cannot open %s %m", __func__, path_stats);
			continue;
		}

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
	static acct_filesystem_data_t fls;
	static acct_filesystem_data_t current;
	static acct_filesystem_data_t previous;
	static bool first = true;
	int cc;

	slurm_mutex_lock(&lustre_lock);

	cc = _read_lustre_counters();
	if (cc != SLURM_SUCCESS) {
		error("%s: Cannot read lustre counters", __func__);
		slurm_mutex_unlock(&lustre_lock);
		return SLURM_FAILURE;
	}

	if (first) {
		/* First time initialize the counters and return.
		 */
		previous.reads = lustre_se.all_lustre_nb_reads;
		previous.writes = lustre_se.all_lustre_nb_writes;
		previous.read_size
			= (double)lustre_se.all_lustre_read_bytes/1048576.0;
		previous.write_size
			= (double)lustre_se.all_lustre_write_bytes/1048576.0;

		first = false;
		memset(&lustre_se, 0, sizeof(lustre_sens_t));
		slurm_mutex_unlock(&lustre_lock);

		return SLURM_SUCCESS;
	}

	/* Compute the current values read from all lustre-xxxx
	 * directories
	 */
	current.reads = lustre_se.all_lustre_nb_reads;
	current.writes = lustre_se.all_lustre_nb_writes;
	current.read_size = (double)lustre_se.all_lustre_read_bytes/1048576.0;
	current.write_size = (double)lustre_se.all_lustre_write_bytes/1048576.0;

	/* Now compute the difference between the two snapshots
	 * and send it to hdf5 log.
	 */
	fls.reads = fls.reads + (current.reads - previous.reads);
	fls.writes = fls.writes + (current.writes - previous.writes);
	fls.read_size = fls.read_size
		+ (current.read_size - previous.read_size);
	fls.write_size = fls.write_size
		+ (current.write_size - previous.write_size);

	acct_gather_profile_g_add_sample_data(ACCT_GATHER_PROFILE_LUSTRE, &fls);

	/* Save current as previous and clean up the working
	 * data structure.
	 */
	memcpy(&previous, &current, sizeof(acct_filesystem_data_t));
	memset(&lustre_se, 0, sizeof(lustre_sens_t));

	info("%s: num reads %"PRIu64" nums write %"PRIu64" "
	     "read %f MB wrote %f MB",
	     __func__, fls.reads, fls.writes, fls.read_size, fls.write_size);

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
	debug_flags = slurm_get_debug_flags();

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
