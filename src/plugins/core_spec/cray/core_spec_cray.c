/*****************************************************************************\
 *  core_spec_cray.c - Cray core specialization plugin.
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Written by Morris Jette <jette@schemd.com>
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
#  include "config.h"
#  if STDC_HEADERS
#    include <string.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif /* HAVE_SYS_TYPES_H */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

/* Set _DEBUG to 1 for detailed module debugging, 0 otherwise */
#define _DEBUG 0

#ifdef HAVE_NATIVE_CRAY
#include <stdlib.h>
#include <job.h>
#endif

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
 * the plugin (e.g., "auth" for SLURM authentication) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version   - specifies the version number of the plugin.
 * min_plug_version - specifies the minumum version number of incoming
 *                    messages that this plugin can accept
 */
const char plugin_name[]       	= "Cray core specialization plugin";
const char plugin_type[]       	= "core_spec/cray";
const uint32_t plugin_version   = 100;

// If job_set_corespec fails, retry this many times to wait
// for suspends to complete.
#define CORE_SPEC_RETRIES 5

extern int init(void)
{
	info("%s: init", plugin_type);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	info("%s: fini", plugin_type);
	return SLURM_SUCCESS;
}

/*
 * Set the count of specialized cores at job start
 *
 * Return SLURM_SUCCESS on success
 */
extern int core_spec_p_set(uint64_t cont_id, uint16_t core_count)
{
#if _DEBUG
	info("core_spec_p_set(%"PRIu64") to %u", cont_id, core_count);
#endif

#ifdef HAVE_NATIVE_CRAY
	int rc;
	struct job_set_affinity_info affinity_info;
	pid_t pid;
	int i;

	// Skip core spec setup for no specialized cores
	if ((core_count == (uint16_t) NO_VAL) || (core_count < 1)) {
		return SLURM_SUCCESS;
	}

	// Set the core spec information
	// Retry because there's a small timing window during preemption
	// when two core spec jobs can be running at once.
	for (i = 0; i < CORE_SPEC_RETRIES; i++) {
		if (i) {
			sleep(1);
		}

		errno = 0;
		rc = job_set_corespec(cont_id, core_count, NULL);
		if (rc == 0 || errno != EINVAL) {
			break;
		}
	}
	if (rc != 0) {
		error("job_set_corespec(%"PRIu64", %"PRIu16") failed: %m",
		      cont_id, core_count);
		return SLURM_ERROR;
	}

	pid = getpid();

	// Slurm detaches the slurmstepd from the job, so we temporarily
	// reattach so the job_set_affinity doesn't mess up one of the
	// task's affinity settings
	if (job_attachpid(pid, cont_id) == (jid_t)-1) {
		error("job_attachpid(%zu, %"PRIu64") failed: %m",
		      (size_t)pid, cont_id);
		return SLURM_ERROR;
	}

	// Apply the core specialization with job_set_affinity
	// Use NONE for the cpu list because Slurm handles its
	// own task->cpu binding
	memset(&affinity_info, 0, sizeof(struct job_set_affinity_info));
	affinity_info.cpu_list = JOB_AFFINITY_NONE;
	rc = job_set_affinity(cont_id, pid, &affinity_info);
	if (rc != 0) {
		if (affinity_info.message != NULL) {
			error("job_set_affinity(%"PRIu64", %zu) failed %s: %m",
			      cont_id, (size_t)pid, affinity_info.message);
			free(affinity_info.message);
		} else {
			error("job_set_affinity(%"PRIu64", %zu) failed: %m",
			      cont_id, (size_t)pid);
		}
		job_detachpid(pid);
		return SLURM_ERROR;
	} else if (affinity_info.message != NULL) {
		info("job_set_affinity(%"PRIu64", %zu): %s",
		     cont_id, (size_t)pid, affinity_info.message);
		free(affinity_info.message);
	}
	job_detachpid(pid);
#endif
	// The code that was here is now performed by
	// switch_p_job_step_{pre,post}_suspend()
	return SLURM_SUCCESS;
}

/*
 * Clear specialized cores at job termination
 *
 * Return SLURM_SUCCESS on success
 */
extern int core_spec_p_clear(uint64_t cont_id)
{
#if _DEBUG
	info("core_spec_p_clear(%"PRIu64")", cont_id);
#endif
	// Core specialization is automatically cleared when
	// the job exits.
	return SLURM_SUCCESS;
}

/*
 * Reset specialized cores at job suspend
 *
 * Return SLURM_SUCCESS on success
 */
extern int core_spec_p_suspend(uint64_t cont_id, uint16_t core_count)
{
#if _DEBUG
	info("core_spec_p_suspend(%"PRIu64") count %u", cont_id, core_count);
#endif
	// The code that was here is now performed by
	// switch_p_job_step_{pre,post}_suspend()
	return SLURM_SUCCESS;
}

/*
 * Reset specialized cores at job resume
 *
 * Return SLURM_SUCCESS on success
 */
extern int core_spec_p_resume(uint64_t cont_id, uint16_t core_count)
{
#if _DEBUG
	info("core_spec_p_resume(%"PRIu64") count %u", cont_id, core_count);
#endif
	// The code that was here is now performed by
	// switch_p_job_step_{pre,post}_resume()
	return SLURM_SUCCESS;
}
