/*****************************************************************************\
 *  util.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Copyright 2013 Hewlett Packard Enterprise Development LP
 *  Written by David Gloe
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

#include <dirent.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "switch_cray_aries.h"
#include "slurm/slurm.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_step_layout.h"
#include "src/common/xstring.h"


#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
static void _recursive_rmdir(const char *dirnm);

/*
 * Create APID directory with given uid/gid as the owner.
 */
int create_apid_dir(uint64_t apid, uid_t uid, gid_t gid)
{
	int rc = 0;
	char *apid_dir = NULL;

	apid_dir = xstrdup_printf(LEGACY_SPOOL_DIR "%" PRIu64, apid);

	rc = mkdir(apid_dir, 0700);
	if (rc) {
		CRAY_ERR("mkdir %s failed: %m", apid_dir);
		xfree(apid_dir);
		return SLURM_ERROR;
	}

	rc = chown(apid_dir, uid, gid);
	if (rc) {
		CRAY_ERR("chown %s, %u, %u failed: %m",
			 apid_dir, uid, gid);
		xfree(apid_dir);
		return SLURM_ERROR;
	}

	if (apid != SLURM_ID_HASH_LEGACY(apid)) {
		char *oldapid_dir = xstrdup_printf(LEGACY_SPOOL_DIR "%" PRIu64,
						   SLURM_ID_HASH_LEGACY(apid));
		if (symlink(apid_dir, oldapid_dir)) {
			CRAY_ERR("symlink %s, %s failed: %m",
				apid_dir, oldapid_dir);
			xfree(apid_dir);
			xfree(oldapid_dir);
			return SLURM_ERROR;
		}
		xfree(oldapid_dir);
	}

	xfree(apid_dir);
	return SLURM_SUCCESS;
}

/*
 * Clean up spool directory files, directories, and links
 */
int remove_spool_files(uint64_t apid)
{
	char *path_name = NULL;
	uint64_t oldapid = SLURM_ID_HASH_LEGACY(apid);

	// Remove the backwards compatibility apid directory symlink
	if (apid != oldapid) {
		path_name = xstrdup_printf(
			LEGACY_SPOOL_DIR "%" PRIu64, oldapid);
		if (remove(path_name)) {
			CRAY_ERR("remove %s failed: %m", path_name);
			xfree(path_name);
			return SLURM_ERROR;
		}
		xfree(path_name);
	}

	// Remove the apid directory LEGACY_SPOOL_DIR/<APID>
	path_name = xstrdup_printf(LEGACY_SPOOL_DIR "%" PRIu64, apid);
	_recursive_rmdir(path_name);
	xfree(path_name);

	// Remove the backwards compatibility ALPS placement file
	if (apid != oldapid) {
		path_name = xstrdup_printf(LEGACY_SPOOL_DIR "places%" PRIu64,
					   oldapid);
		if (remove(path_name)) {
			CRAY_ERR("remove %s failed: %m", path_name);
			xfree(path_name);
			return SLURM_ERROR;
		}
	}

	// Remove the ALPS placement file LEGACY_SPOOL_DIR/places<APID>
	path_name = xstrdup_printf(LEGACY_SPOOL_DIR "places%" PRIu64, apid);
	if (remove(path_name)) {
		CRAY_ERR("remove %s failed: %m", path_name);
		xfree(path_name);
		return SLURM_ERROR;
	}

	xfree(path_name);
	return SLURM_SUCCESS;
}

/*
 * Set job environment variables used by LLI and PMI
 */
int set_job_env(stepd_step_rec_t *step, slurm_cray_jobinfo_t *sw_job)
{
	int rc, i;
	char *buff = NULL, *resv_ports = NULL, *tmp = NULL;

	/*
	 * Write the CRAY_NUM_COOKIES and CRAY_COOKIES variables out
	 */
	rc = env_array_overwrite_fmt(&step->env, CRAY_NUM_COOKIES_ENV,
				     "%"PRIu32, sw_job->num_cookies);
	if (rc == 0) {
		CRAY_ERR("Failed to set env var " CRAY_NUM_COOKIES_ENV);
		return SLURM_ERROR;
	}

	/*
	 * Create the CRAY_COOKIES environment variable in the application's
	 * environment.
	 * Create one string containing a comma separated list of cookies.
	 */
	for (i = 0; i < sw_job->num_cookies; i++) {
		if (i > 0) {
			xstrfmtcat(buff, ",%s", sw_job->cookies[i]);
		} else
			xstrcat(buff, sw_job->cookies[i]);
	}

	rc = env_array_overwrite(&step->env, CRAY_COOKIES_ENV, buff);
	if (rc == 0) {
		CRAY_ERR("Failed to set env var " CRAY_COOKIES_ENV);
		xfree(buff);
		return SLURM_ERROR;
	}
	xfree(buff);

	/*
	 * Write the PMI_CONTROL_PORT
	 * Cray's PMI uses this is the port to communicate its control tree
	 * information.
	 */
	resv_ports = getenvp(step->env, "SLURM_STEP_RESV_PORTS");
	if (resv_ports != NULL) {
		buff = xstrdup(resv_ports);
		tmp = strchr(buff, '-');
		if (tmp != NULL) {
			*tmp = '\0';
		}
		rc = env_array_overwrite(&step->env, PMI_CONTROL_PORT_ENV,
					 buff);
		xfree(buff);
		if (rc == 0) {
			CRAY_ERR("Failed to set env var "PMI_CONTROL_PORT_ENV);
			return SLURM_ERROR;
		}

	}

	/* Set if task IDs are not monotonically increasing across all nodes */
	rc = env_array_overwrite_fmt(&step->env, PMI_CRAY_NO_SMP_ENV,
				     "%d", step->non_smp);
	if (rc == 0) {
		CRAY_ERR("Failed to set env var "PMI_CRAY_NO_SMP_ENV);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Print the results of an alpscomm call
 * err_msg is freed and NULLed
 */
void alpsc_debug(const char *file, int line, const char *func,
		  int rc, int expected_rc, const char *alpsc_func,
		  char **err_msg)
{
	if (rc != expected_rc) {
		error("(%s: %d: %s) %s failed: %s", file, line, func,
		      alpsc_func,
		      *err_msg ? *err_msg : "No error message present");
	} else if (*err_msg) {
		info("(%s: %d: %s) %s: %s", file, line, func,
		     alpsc_func, *err_msg);
	}
	free(*err_msg);
	*err_msg = NULL;
}


/*
 * Function: list_str_to_array
 * Description:
 * 	Convert the list string into an array of integers.
 *
 * IN list     -- The list string
 * OUT cnt     -- The number of numbers in the list string
 * OUT numbers -- Array of integers;  Caller is responsible to xfree()
 *                this.
 *
 * N.B. Caller is responsible to xfree() numbers.
 *
 * RETURNS
 * Returns 0 on success and -1 on failure.
 */

int list_str_to_array(char *list, int *cnt, int32_t **numbers)
{

	int32_t *item_ptr = NULL;
	hostlist_t hl;
	int i, ret = 0;
	char *str, *cptr = NULL;

	/*
	 * Create a hostlist
	 */
	if (!(hl = hostlist_create(list))) {
		CRAY_ERR("hostlist_create error on %s", list);
		error("hostlist_create error on %s", list);
		return -1;
	}

	*cnt = hostlist_count(hl);

	if (!*cnt) {
		*numbers = NULL;
		return 0;
	}

	/*
	 * Create an integer array of item_ptr in the same order as in the list.
	 */
	i = 0;
	item_ptr = *numbers = xmalloc((*cnt) * sizeof(int32_t));
	while ((str = hostlist_shift(hl))) {
		if (!(cptr = strpbrk(str, "0123456789"))) {
			CRAY_ERR("Error: Node was not recognizable: %s", str);
			free(str);
			xfree(item_ptr);
			*numbers = NULL;
			hostlist_destroy(hl);
			return -1;
		}
		item_ptr[i] = atoll(cptr);
		i++;
		free(str);
	}

	// Clean up
	hostlist_destroy(hl);

	return ret;
}

/*
 * Recursive directory delete
 *
 * Call with a directory name and this function will delete
 * all files and directories rooted in this name. Finally
 * the named directory will be deleted.
 * If called with a file name, only that file will be deleted.
 */
static void _recursive_rmdir(const char *dirnm)
{
	int st;
	size_t dirnm_len, fnm_len, name_len;
	char *fnm = 0;
	DIR *dirp;
	struct dirent *dir;
	struct stat st_buf;

	/* Don't do anything if there is no directory name */
	if (!dirnm) {
		return;
	}
	dirp = opendir(dirnm);
	if (!dirp) {
		if (errno == ENOTDIR)
			goto fileDel;
		CRAY_ERR("Error opening directory %s", dirnm);
		return;
	}

	dirnm_len = strlen(dirnm);
	if (dirnm_len == 0)
		return;
	while ((dir = readdir(dirp))) {
		name_len = strlen(dir->d_name);
		if (name_len == 1 && dir->d_name[0] == '.')
			continue;
		if (name_len == 2 && xstrcmp(dir->d_name, "..") == 0)
			continue;
		fnm_len = dirnm_len + name_len + 2;
		free(fnm);
		fnm = malloc(fnm_len);
		snprintf(fnm, fnm_len, "%s/%s", dirnm, dir->d_name);
		st = stat(fnm, &st_buf);
		if (st < 0) {
			CRAY_ERR("stat of %s", fnm);
			continue;
		}
		if (st_buf.st_mode & S_IFDIR) {
			_recursive_rmdir(fnm);
		} else {

			st = unlink(fnm);
			if (st < 0 && errno == EISDIR)
				st = rmdir(fnm);
			if (st < 0 && errno != ENOENT) {
				CRAY_ERR("Error removing %s", fnm);
			}
		}
	}
	free(fnm);
	closedir(dirp);
fileDel: st = unlink(dirnm);
	if (st < 0 && errno == EISDIR)
		st = rmdir(dirnm);
	if (st < 0 && errno != ENOENT) {
		CRAY_ERR("Error removing %s", dirnm);
	}
}

void print_jobinfo(slurm_cray_jobinfo_t *job)
{
	int i;
	char *cookie_str = NULL, *cookie_id_str = NULL;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_ERR("job pointer was NULL");
		return;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

	// Create cookie strings
	for (i = 0; i < job->num_cookies; i++) {
		xstrfmtcat(cookie_str, "%s%s", i ? "," : "", job->cookies[i]);
		xstrfmtcat(cookie_id_str, "%s%"PRIu32,
			   i ? "," : "", job->cookie_ids[i]);
	}

	// Log jobinfo
	info("jobinfo magic=%"PRIx32" apid=%"PRIu64
	     " num_cookies=%"PRIu32" cookies=%s cookie_ids=%s",
	     job->magic, job->apid,
	     job->num_cookies, cookie_str, cookie_id_str);

	// Cleanup
	xfree(cookie_str);
	xfree(cookie_id_str);
}
#endif /* HAVE_NATIVE_CRAY || HAVE_CRAY_NETWORK */
