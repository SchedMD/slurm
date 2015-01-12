/*****************************************************************************\
 *  priority_basic.c - NO-OP slurm priority plugin.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <math.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_priority.h"
#include "src/common/assoc_mgr.h"

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
 * the plugin (e.g., "jobcomp" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the job completion logging API
 * matures.
 */
const char plugin_name[]       	= "Priority BASIC plugin";
const char plugin_type[]       	= "priority/basic";
const uint32_t plugin_version	= 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM priority API.
 */

extern uint32_t priority_p_set(uint32_t last_prio, struct job_record *job_ptr)
{
	uint32_t new_prio = 1;

	if (job_ptr->direct_set_prio && (job_ptr->priority > 1))
		return job_ptr->priority;

	if (last_prio >= 2)
		new_prio = (last_prio - 1);

	if (job_ptr->details) {
		int offset = job_ptr->details->nice;
		offset -= NICE_OFFSET;
		if ((offset <= 0) || (new_prio > (offset+1)))
			new_prio -= offset;
	}

	/* System hold is priority 0 */
	if (new_prio < 1)
		new_prio = 1;

	return new_prio;
}

extern void priority_p_reconfig(bool assoc_clear)
{
	return;
}

extern void priority_p_set_assoc_usage(slurmdb_association_rec_t *assoc)
{
	return;
}

extern double priority_p_calc_fs_factor(long double usage_efctv,
					long double shares_norm)
{
	/* This calculation is needed for sshare when ran from a
	 * non-multifactor machine to a multifactor machine.  It
	 * doesn't do anything on regular systems, it should always
	 * return 0 since shares_norm will always be NO_VAL. */
	double priority_fs;

	xassert(!fuzzy_equal(usage_efctv, NO_VAL));

	if ((shares_norm <= 0.0) || fuzzy_equal(shares_norm, NO_VAL))
		priority_fs = 0.0;
	else
		priority_fs = pow(2.0, -(usage_efctv / shares_norm));

	return priority_fs;
}

extern List priority_p_get_priority_factors_list(
	priority_factors_request_msg_t *req_msg, uid_t uid)
{
	return(list_create(NULL));
}

extern void priority_p_job_end(struct job_record *job_ptr)
{
	uint64_t unused_cpu_run_secs = 0;
	uint64_t time_limit_secs = (uint64_t)job_ptr->time_limit * 60;
	slurmdb_association_rec_t *assoc_ptr;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	/* No unused cpu_run_secs if job ran past its time limit */
	if (job_ptr->end_time >= job_ptr->start_time + time_limit_secs)
		return;

	unused_cpu_run_secs = job_ptr->total_cpus *
		(job_ptr->start_time + time_limit_secs - job_ptr->end_time);

	assoc_mgr_lock(&locks);
	if (job_ptr->qos_ptr) {
		slurmdb_qos_rec_t *qos_ptr =
			(slurmdb_qos_rec_t *)job_ptr->qos_ptr;
		if (unused_cpu_run_secs >
		    qos_ptr->usage->grp_used_cpu_run_secs) {
			qos_ptr->usage->grp_used_cpu_run_secs = 0;
			debug2("acct_policy_job_fini: "
			       "grp_used_cpu_run_secs "
			       "underflow for qos %s", qos_ptr->name);
		} else
			qos_ptr->usage->grp_used_cpu_run_secs -=
				unused_cpu_run_secs;
	}
	assoc_ptr = (slurmdb_association_rec_t *)job_ptr->assoc_ptr;
	while (assoc_ptr) {
		/* If the job finished early remove the extra time now. */
		if (unused_cpu_run_secs >
		    assoc_ptr->usage->grp_used_cpu_run_secs) {
			assoc_ptr->usage->grp_used_cpu_run_secs = 0;
			debug2("acct_policy_job_fini: "
			       "grp_used_cpu_run_secs "
			       "underflow for account %s",
			       assoc_ptr->acct);
		} else {
			assoc_ptr->usage->grp_used_cpu_run_secs -=
				unused_cpu_run_secs;
			debug4("acct_policy_job_fini: job %u. "
			       "Removed %"PRIu64" unused seconds "
			       "from assoc %s "
			       "grp_used_cpu_run_secs = %"PRIu64"",
			       job_ptr->job_id, unused_cpu_run_secs,
			       assoc_ptr->acct,
			       assoc_ptr->usage->grp_used_cpu_run_secs);
		}
		/* now handle all the group limits of the parents */
		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
	}
	assoc_mgr_unlock(&locks);

	return;
}
