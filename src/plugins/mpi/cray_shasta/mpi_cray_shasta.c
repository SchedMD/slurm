/*****************************************************************************\
 *  mpi_cray_shasta.c - Cray Shasta MPI plugin
 *****************************************************************************
 *  Copyright 2019,2022 Hewlett Packard Enterprise Development LP
 *  Written by David Gloe <dgloe@cray.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
\*****************************************************************************/

#include "config.h"

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#ifdef HAVE_GETRANDOM
#include <sys/random.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/env.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/interfaces/mpi.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "apinfo.h"

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
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for Slurm switch) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "mpi Cray Shasta plugin";
const char plugin_type[] = "mpi/cray_shasta";
const uint32_t plugin_id = MPI_PLUGIN_CRAY_SHASTA;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Environment variables available for applications */
#define PALS_APID_ENV "PALS_APID"
#define PALS_APINFO_ENV "PALS_APINFO"
#define PALS_LOCAL_RANKID_ENV "PALS_LOCAL_RANKID"
#define PALS_NODEID_ENV "PALS_NODEID"
#define PALS_RANKID_ENV "PALS_RANKID"
#define PALS_SPOOL_DIR_ENV "PALS_SPOOL_DIR"

#define PMI_JOBID_ENV "PMI_JOBID"
#define PMI_LOCAL_RANK_ENV "PMI_LOCAL_RANK"
#define PMI_LOCAL_SIZE_ENV "PMI_LOCAL_SIZE"
#define PMI_RANK_ENV "PMI_RANK"
#define PMI_SIZE_ENV "PMI_SIZE"
#define PMI_UNIVERSE_SIZE_ENV "PMI_UNIVERSE_SIZE"
#define PMI_SHARED_SECRET_ENV "PMI_SHARED_SECRET"

/* GLOBAL vars */
char *appdir = NULL; // Application-specific spool directory
char *apinfo = NULL; // Application PMI file

/*
 * Create the Cray MPI directory under the slurmd spool directory
 */
static int _create_mpi_dir(const char *spool)
{
	char *mpidir = NULL;
	int rc = SLURM_SUCCESS;

	mpidir = xstrdup_printf("%s/%s", spool, MPI_CRAY_DIR);
	if ((mkdir(mpidir, 0755) == -1) && (errno != EEXIST)) {
		error("%s: Couldn't create Cray MPI directory %s: %m",
		      plugin_type, mpidir);
		rc = SLURM_ERROR;
	}
	xfree(mpidir);

	return rc;
}

/*
 * Create the application-specific directory under the Cray MPI directory
 */
static int _create_app_dir(const stepd_step_rec_t *step, const char *spool)
{
	xfree(appdir);
	// Format the directory name
	appdir = xstrdup_printf("%s/%s/%u.%u",
				spool, MPI_CRAY_DIR,
				step->step_id.job_id, step->step_id.step_id);

	// Create the directory
	if ((mkdir(appdir, 0700) == -1) && (errno != EEXIST)) {
		error("%s: Couldn't create directory %s: %m",
		      plugin_type, appdir);
		goto error;
	}

	// Change directory owner
	if (chown(appdir, step->uid, step->gid) == -1) {
		error("%s: Couldn't change directory %s owner: %m",
		      plugin_type, appdir);
		goto error;
	}

	debug("%s: Created application directory %s", plugin_type, appdir);
	return SLURM_SUCCESS;

error:
	if (rmdir(appdir) < 0)
		error("rmdir(%s): %m",  appdir);
	xfree(appdir);
	return SLURM_ERROR;
}

/*
 * Set the PMI port to use in the application's environment
 */
static void _set_pmi_port(char ***env)
{
	char *resv_ports = NULL;
	char *endp = NULL;
	unsigned long pmi_port = 0;

	if (!(resv_ports = getenvp(*env, "SLURM_STEP_RESV_PORTS")))
		return;

	// Get the first port from the range
	errno = 0;
	pmi_port = strtoul(resv_ports, &endp, 10);
	if ((errno != 0) || (pmi_port > 65535) ||
	    ((*endp != '-') && (*endp != ',') && (*endp != '\0'))) {
		error("%s: Couldn't parse reserved ports %s",
		      plugin_type, resv_ports);
		return;
	}

	env_array_overwrite_fmt(env, "PMI_CONTROL_PORT", "%lu", pmi_port);
}

/*
 * Determine whether the given path is a directory
 */
static int _is_dir(char *path)
{
	struct stat stat_buf;

	if (stat(path, &stat_buf)) {
		error("%s: Cannot stat %s: %m", plugin_type, path);
		return 1;
	} else if (!S_ISDIR(stat_buf.st_mode)) {
		return 0;
	}
	return 1;
}

/*
 * Recursively remove a directory
 */
static int _rmdir_recursive(char *path)
{
	char nested_path[PATH_MAX];
	DIR *dp;
	struct dirent *ent;

	if (!(dp = opendir(path))) {
		error("%s: Can't open directory %s: %m", plugin_type, path);
		return SLURM_ERROR;
	}

	while ((ent = readdir(dp))) {
		if (!xstrcmp(ent->d_name, ".") ||
		    !xstrcmp(ent->d_name, "..")) {
			/* skip special dir's */
			continue;
		}
		snprintf(nested_path, sizeof(nested_path), "%s/%s", path,
			 ent->d_name);
		if (_is_dir(nested_path)) {
			_rmdir_recursive(nested_path);
		} else {
			debug("%s: Removed file %s", plugin_type, nested_path);
			unlink(nested_path);
		}
	}
	closedir(dp);

	if (rmdir(path) == -1) {
		error("%s: Can't remove directory %s: %m",
		      plugin_type, path);
		return SLURM_ERROR;
	}

	debug("%s: Removed directory %s", plugin_type, path);
	return SLURM_SUCCESS;
}

extern int mpi_p_slurmstepd_prefork(const stepd_step_rec_t *step, char ***env)
{
	/* do the node_name substitution once */
	char *spool = slurm_conf_expand_slurmd_path(slurm_conf.slurmd_spooldir,
						    step->node_name,
						    step->node_name);

	// Set up spool directory and apinfo
	if (_create_mpi_dir(spool) == SLURM_ERROR ||
	    _create_app_dir(step, spool) == SLURM_ERROR ||
	    create_apinfo(step, spool) == SLURM_ERROR) {
		xfree(spool);
		return SLURM_ERROR;
	}

	xfree(spool);

	return SLURM_SUCCESS;
}

extern int mpi_p_slurmstepd_task(const mpi_task_info_t *mpi_task, char ***env)
{
	// Set environment variables
	env_array_overwrite_fmt(env, PALS_APID_ENV, "%u.%u",
				mpi_task->step_id.job_id,
				mpi_task->step_id.step_id);
	env_array_overwrite_fmt(env, PALS_APINFO_ENV, "%s", apinfo);
	env_array_overwrite_fmt(env, PALS_LOCAL_RANKID_ENV, "%u",
				mpi_task->ltaskid);
	env_array_overwrite_fmt(env, PALS_NODEID_ENV, "%u", mpi_task->nodeid);
	env_array_overwrite_fmt(env, PALS_RANKID_ENV, "%u", mpi_task->gtaskid);
	env_array_overwrite_fmt(env, PALS_SPOOL_DIR_ENV, "%s", appdir);

	env_array_overwrite_fmt(env, PMI_JOBID_ENV, "%u",
				mpi_task->step_id.job_id);
	env_array_overwrite_fmt(env, PMI_LOCAL_RANK_ENV, "%u",
				mpi_task->ltaskid);
	env_array_overwrite_fmt(env, PMI_LOCAL_SIZE_ENV, "%u",
				mpi_task->ltasks);
	env_array_overwrite_fmt(env, PMI_RANK_ENV, "%u", mpi_task->gtaskid);
	env_array_overwrite_fmt(env, PMI_SIZE_ENV, "%u", mpi_task->ntasks);
	env_array_overwrite_fmt(env, PMI_UNIVERSE_SIZE_ENV, "%u",
				mpi_task->ntasks);

	_set_pmi_port(env);

	return SLURM_SUCCESS;
}

extern mpi_plugin_client_state_t *
mpi_p_client_prelaunch(const mpi_step_info_t *mpi_step, char ***env)
{
#ifdef HAVE_GETRANDOM
	uint64_t shared_secret = 0;

	/*
	 * Get a non-zero pseudo-random value. getrandom() is guaranteed to
	 * return up to 256 bytes uninturrupted. The only error we might expect
	 * here is ENOSYS, indicating that the kernel does not implement the
	 * getrandom() system call. getrandom() should be present on all
	 * supported cray systems.
	 */
	if (getrandom(&shared_secret, sizeof(shared_secret), 0) < 0) {
		error("%s: getrandom() failed: %m", __func__);
		return NULL;
	}

	/* Set PMI_SHARED_SECRET for PMI authentication */
	env_array_overwrite_fmt(env, PMI_SHARED_SECRET_ENV, "%"PRIu64,
				shared_secret);
#endif
	/* only return NULL on error */
	return (void *)0xdeadbeef;
}

extern int mpi_p_client_fini(mpi_plugin_client_state_t *state)
{
	return SLURM_SUCCESS;
}

extern int init(void)
{
	return SLURM_SUCCESS;
}

/*
 * Clean up the application
 */
extern int fini(void)
{
	// Remove application spool directory
	if (appdir)
		_rmdir_recursive(appdir);

	// Free allocated storage
	xfree(appdir);
	xfree(apinfo);

	return SLURM_SUCCESS;
}

extern void mpi_p_conf_options(s_p_options_t **full_options, int *full_opt_cnt)
{
}

extern void mpi_p_conf_set(s_p_hashtbl_t *tbl)
{
}

extern s_p_hashtbl_t *mpi_p_conf_get(void)
{
	return NULL;
}

extern List mpi_p_conf_get_printable(void)
{
	return NULL;
}
