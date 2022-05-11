/*****************************************************************************\
 *  core_spec_cray_aries.c - Cray/Aries core specialization plugin.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC
 *  Written by Morris Jette <jette@schemd.com>
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Cray/Aries core specialization plugin";
const char plugin_type[]       	= "core_spec/cray_aries";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

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
	DEF_TIMERS;
	START_TIMER;
#if _DEBUG
	char *spec_type;
	int spec_count;
	if (core_count == NO_VAL16) {
		spec_type  = "Cores";
		spec_count = 0;
	} else if (core_count & CORE_SPEC_THREAD) {
		spec_type  = "Threads";
		spec_count = core_count & (~CORE_SPEC_THREAD);
	} else {
		spec_type  = "Cores";
		spec_count = core_count;
	}
	info("core_spec_p_set(%"PRIu64") to %d %s",
	     cont_id, spec_count, spec_type);
#endif

#ifdef HAVE_NATIVE_CRAY
	int rc;
	struct job_set_affinity_info affinity_info;
	pid_t pid;
	int i;

	// Skip core spec setup for no specialized cores
	if ((core_count == NO_VAL16) ||
	    (core_count == CORE_SPEC_THREAD)) {
		return SLURM_SUCCESS;
	}
	core_count &= (~CORE_SPEC_THREAD);

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
		debug("job_set_corespec(%"PRIu64", %"PRIu16") failed: %m",
		      cont_id, core_count);
		return SLURM_ERROR;
	}

	// Get a pid in the job to use with job_set_affinity
	pid = job_getprimepid(cont_id);
	if (pid < 0) {
		error("job_getprimepid(%"PRIu64") returned %d: %m",
		      cont_id, (int)pid);
		return SLURM_ERROR;
	}

	// Apply the core specialization with job_set_affinity
	// JOB_AFFINITY_NONE tells the kernel to not alter the process'
	// affinity unless required (the process is only allowed to run
	// on cores that will be specialized).
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
		return SLURM_ERROR;
	} else if (affinity_info.message != NULL) {
		info("job_set_affinity(%"PRIu64", %zu): %s",
		     cont_id, (size_t)pid, affinity_info.message);
		free(affinity_info.message);
	}
#endif
	END_TIMER;
	if (slurm_conf.debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

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
	char *spec_type;
	int spec_count;
	if (core_count == NO_VAL16) {
		spec_type  = "Cores";
		spec_count = 0;
	} else if (core_count & CORE_SPEC_THREAD) {
		spec_type  = "Threads";
		spec_count = core_count & (~CORE_SPEC_THREAD);
	} else {
		spec_type  = "Cores";
		spec_count = core_count;
	}
	info("core_spec_p_suspend(%"PRIu64") count %d %s",
	     cont_id, spec_count, spec_type);
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
	char *spec_type;
	int spec_count;
	if (core_count == NO_VAL16) {
		spec_type  = "Cores";
		spec_count = 0;
	} else if (core_count & CORE_SPEC_THREAD) {
		spec_type  = "Threads";
		spec_count = core_count & (~CORE_SPEC_THREAD);
	} else {
		spec_type  = "Cores";
		spec_count = core_count;
	}
	info("core_spec_p_resume(%"PRIu64") count %d %s",
	     cont_id, spec_count, spec_type);
#endif
	// The code that was here is now performed by
	// switch_p_job_step_{pre,post}_resume()
	return SLURM_SUCCESS;
}
