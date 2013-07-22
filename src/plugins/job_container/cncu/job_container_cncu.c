/*****************************************************************************\
 *  job_container_cncu.c - Define job container management functions for
 *                         Cray systems
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette, SchedMD
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <job.h>	/* Cray's job module component */

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmd/common/proctrack.h"

#define _DEBUG	0

#define ADD_FLAGS	0
#define CREATE_FLAGS	0
#define DELETE_FLAGS	0

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
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "job_container cncu plugin";
const char plugin_type[]        = "job_container/cncu";
const uint32_t plugin_version   = 101;

#define JOB_BUF_SIZE 128

static uint32_t *job_id_array = NULL;
static uint32_t  job_id_count = 0;
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;
static char *state_dir = NULL;

static int _save_state(char *dir_name)
{
	char *file_name;
	int ret = SLURM_SUCCESS;
	int state_fd;

	if (!dir_name) {
		error("job_container state directory is NULL");
		return SLURM_ERROR;
	}
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/job_container_state");
	(void) unlink(file_name);
	state_fd = creat(file_name, 0600);
	if (state_fd < 0) {
		error("Can't save state, error creating file %s %m",
		      file_name);
		ret = SLURM_ERROR;
	} else {
		char  *buf = (char *) job_id_array;
		size_t len = job_id_count * sizeof(uint32_t);
		while (1) {
	  		int wrote = write(state_fd, buf, len);
			if ((wrote < 0) && (errno == EINTR))
				continue;
	 		if (wrote == 0)
		 		break;
			if (wrote < 0) {
				error("Can't save job_container state: %m");
				ret = SLURM_ERROR;
				break;
			}
			buf += wrote;
			len -= wrote;
		}
		close(state_fd);
	}
	xfree(file_name);

	return ret;
}

static int _restore_state(char *dir_name)
{
	char *data = NULL, *file_name;
	int error_code = SLURM_SUCCESS;
	int state_fd, data_allocated = 0, data_read = 0, data_size = 0;

	if (!dir_name) {
		error("job_container state directory is NULL");
		return SLURM_ERROR;
	}

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/job_container_state");
	state_fd = open (file_name, O_RDONLY);
	if (state_fd >= 0) {
		data_allocated = JOB_BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 JOB_BUF_SIZE);
			if ((data_read < 0) && (errno == EINTR))
				continue;
			if (data_read < 0) {
				error ("Read error on %s, %m", file_name);
				error_code = SLURM_ERROR;
				break;
			} else if (data_read == 0)
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
		xfree(file_name);
	} else {
		error("No %s file for %s state recovery",
		      file_name, plugin_type);
		xfree(file_name);
		return SLURM_SUCCESS;
	}

	if (error_code == SLURM_SUCCESS) {
		job_id_array = (uint32_t *) data;
		job_id_count = data_size / sizeof(uint32_t);
	}

	return error_code;
}

#if _DEBUG
static void _stat_reservation(char *type, rid_t resv_id)
{
	struct job_resv_stat buf;

	if (job_stat_reservation(resv_id, &buf)) {
		error("%s: stat(%"PRIu64"): %m", plugin_type, resv_id);
	} else {
		info("%s: %s/stat(%"PRIu64"): flags=%d "
		     "num_jobs=%d num_files=%d num_ipc_objs=%d",
		     plugin_type, type, resv_id, buf.flags, buf.num_jobs,
		     buf.num_files, buf.num_ipc_objs);
	}
}
#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
#if _DEBUG
	info("%s loaded", plugin_name);
#else
	debug("%s loaded", plugin_name);
#endif
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	xfree(state_dir);
	return SLURM_SUCCESS;
}

extern int container_p_restore(char *dir_name, bool recover)
{
	int i;

	slurm_mutex_lock(&context_lock);
	_restore_state(dir_name);
	slurm_mutex_unlock(&context_lock);
	for (i = 0; i < job_id_count; i++) {
		if (job_id_array[i] == 0)
			continue;
		if (recover) {
			info("%s: recovered job(%u)",
			     plugin_type, job_id_array[i]);
		} else {
			info("%s: purging job(%u)",
			     plugin_type, job_id_array[i]);
			job_id_array[i] = 0;
		}
	}

	xfree(state_dir);
	state_dir = xstrdup(dir_name);
	return SLURM_SUCCESS;
}

extern int container_p_create(uint32_t job_id)
{
	rid_t resv_id = job_id;
	int rc;
	int i, empty = -1, found = -1;
	bool job_id_change = false;
	info("%s: creating(%u)", plugin_type, job_id);

	slurm_mutex_lock(&context_lock);
	for (i = 0; i < job_id_count; i++) {
		if (job_id_array[i] == 0) {
			empty = i;
		} else if (job_id_array[i] == job_id) {
			found = i;
			break;
		}
	}
	if (found == -1) {
		if (empty == -1) {
			empty = job_id_count;
			job_id_count += 4;
			job_id_array = xrealloc(job_id_array,
						sizeof(uint32_t)*job_id_count);
		}
		job_id_array[empty] = job_id;
		job_id_change = true;
	} else {
		info("%s: duplicate create job(%u)", plugin_type, job_id);
	}
	if (job_id_change)
		_save_state(state_dir);
	slurm_mutex_unlock(&context_lock);

	rc = job_create_reservation(resv_id, CREATE_FLAGS);
	if ((rc == 0) || (errno == EEXIST)) {
		if ((rc != 0) && (errno == EEXIST)) {
			error("%s: create(%u): Reservation already exists",
			      plugin_type, job_id);
		}
#if _DEBUG
		_stat_reservation("create", resv_id);
#endif
		return SLURM_SUCCESS;
	}
	error("%s: create(%u): %m", plugin_type, job_id);
	return SLURM_ERROR;
}

/* Add proctrack container (PAGG) to a job container */
extern int container_p_add_cont(uint32_t job_id, uint64_t cont_id)
{
	jid_t cjob_id = cont_id;
	rid_t resv_id = job_id;
	int rc;

#if _DEBUG
	info("%s: adding cont(%u.%"PRIu64")", plugin_type, job_id, cont_id);
#endif
	rc = job_attach_reservation(cjob_id, resv_id, ADD_FLAGS);
	if ((rc != 0) && (errno == ENOENT)) {	/* Log and retry */
		error("%s: add(%u.%"PRIu64"): No reservation found",
		      plugin_type, job_id, cont_id);
		rc = job_create_reservation(resv_id, CREATE_FLAGS);
		rc = job_attach_reservation(cjob_id, resv_id, ADD_FLAGS);
	}
	if (rc == 0) {
#if _DEBUG
		_stat_reservation("add", resv_id);
#endif
		return SLURM_SUCCESS;
	}
	error("%s: add(%u.%"PRIu64"): %m", plugin_type, job_id, cont_id);
	return SLURM_ERROR;
}

/* Add a process to a job container, create the proctrack container to add */
extern int container_p_add_pid(uint32_t job_id, pid_t pid, uid_t uid)
{
	stepd_step_rec_t job;

#if _DEBUG
	info("%s: adding pid(%u.%u)", plugin_type, job_id, (uint32_t) pid);
#endif
	memset(&job, 0, sizeof(stepd_step_rec_t));
	job.jmgr_pid = pid;
	job.uid = uid;
	if (slurm_container_create(&job) != SLURM_SUCCESS) {
		error("%s: slurm_container_create job(%u)", plugin_type,job_id);
		return SLURM_ERROR;
	}
	return container_p_add_cont(job_id, job.cont_id);
}

extern int container_p_delete(uint32_t job_id)
{
	rid_t resv_id = job_id;
	int rc;
	int i, found = -1;
	bool job_id_change = false;

	info("%s: deleting(%u)", plugin_type, job_id);
	slurm_mutex_lock(&context_lock);
	for (i = 0; i < job_id_count; i++) {
		if (job_id_array[i] == job_id) {
			job_id_array[i] = 0;
			job_id_change = true;
			found = i;
		}
	}
	if (found == -1)
		info("%s: no job for delete(%u)", plugin_type, job_id);
	if (job_id_change)
		_save_state(state_dir);
	slurm_mutex_unlock(&context_lock);

	rc = job_end_reservation(resv_id, DELETE_FLAGS);
	if (rc == 0)
		return SLURM_SUCCESS;

	if ((errno == ENOENT) || (errno == EINPROGRESS) || (errno == EALREADY))
		return SLURM_SUCCESS;	/* Not fatal error */
	error("%s: delete(%u): %m", plugin_type, job_id);
	return SLURM_ERROR;
}
