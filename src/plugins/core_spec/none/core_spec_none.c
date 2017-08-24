/*****************************************************************************\
 *  core_spec_none.c - NO-OP slurm core specialization plugin.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC
 *  Written by Morris Jette <jette@schemd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

/* Set _DEBUG to 1 for detailed module debugging, 0 otherwise */
#define _DEBUG 0

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
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Null core specialization plugin";
const char plugin_type[]       	= "core_spec/none";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

extern int init(void)
{
	return SLURM_SUCCESS;
}

extern int fini(void)
{
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
	char *spec_type;
	int spec_count;
	if (core_count == (uint16_t) NO_VAL) {
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
	if (core_count == (uint16_t) NO_VAL) {
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
	if (core_count == (uint16_t) NO_VAL) {
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
	return SLURM_SUCCESS;
}
