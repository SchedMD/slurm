/*****************************************************************************\
 *  job_submit_throttle.c - Limits the number of job submissions that any
 *			    single user can submit based upon configuration.
 *
 *  NOTE: Enforce by configuring
 *  SchedulerParameters=jobs_per_user_per_hour=#
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmctld/slurmctld.h"

#define MAX_ACCTG_FREQUENCY 30

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting t#include <time.h>he type of the plugin or its
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
const char plugin_name[]       	= "Job submit throttle plugin";
const char plugin_type[]       	= "job_submit/throttle";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

typedef struct thru_put {
	uint32_t    uid;
	uint32_t job_count;
} thru_put_t;

static int jobs_per_user_per_hour = 0;
static time_t last_reset = (time_t) 0;
static thru_put_t *thru_put_array = NULL;
static int thru_put_size = 0;

static void _get_config(void)
{
	char *opt;

	/*                      01234567890123456789012 */
	if ((opt = xstrcasestr(slurm_conf.sched_params,
			       "jobs_per_user_per_hour=")))
		jobs_per_user_per_hour = atoi(opt + 23);
	info("%s: jobs_per_user_per_hour=%d",
	     plugin_type, jobs_per_user_per_hour);
}

static void _reset_counters(void)
{
	time_t now = time(NULL);
	uint32_t orig_count;
	int delta_t, i;

	if (!last_reset) {
		last_reset = now;
		return;
	}
	delta_t = difftime(now, last_reset) / 60;
	if (delta_t < 6)
		return;
	delta_t /= 6;
	last_reset += (delta_t * 360);
	for (i = 0; i < thru_put_size; i++) {
		orig_count = thru_put_array[i].job_count;
		if (thru_put_array[i].job_count <= 10) {
			if (thru_put_array[i].job_count > delta_t)
				thru_put_array[i].job_count -= delta_t;
			else
				thru_put_array[i].job_count = 0;
		} else if (delta_t >= 10) {
			thru_put_array[i].job_count = 0;
		} else {
			thru_put_array[i].job_count *= (delta_t - 1);
			thru_put_array[i].job_count /=  delta_t;
		}
		debug2("count for user %u reset from %u to %u",
		       thru_put_array[i].uid, orig_count,
		       thru_put_array[i].job_count);
	}
}

extern int init(void)
{
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	xfree(thru_put_array);
	return SLURM_SUCCESS;
}

extern int job_submit(job_desc_msg_t *job_desc, uint32_t submit_uid,
		      char **err_msg)
{
	int i;

	if (!last_reset)
		_get_config();
	if (jobs_per_user_per_hour == 0)
		return SLURM_SUCCESS;
	_reset_counters();

	for (i = 0; i < thru_put_size; i++) {
		if (thru_put_array[i].uid != job_desc->user_id)
			continue;
		if (thru_put_array[i].job_count < jobs_per_user_per_hour) {
			thru_put_array[i].job_count++;
			return SLURM_SUCCESS;
		}
		if (err_msg)
			*err_msg = xstrdup("Reached jobs per hour limit");
		return ESLURM_ACCOUNTING_POLICY;
	}
	thru_put_size++;
	thru_put_array = xrealloc(thru_put_array,
				  (sizeof(thru_put_t) * thru_put_size));
	thru_put_array[thru_put_size - 1].uid = job_desc->user_id;
	thru_put_array[thru_put_size - 1].job_count = 1;
	return SLURM_SUCCESS;
}

extern int job_modify(job_desc_msg_t *job_desc, job_record_t *job_ptr,
		      uint32_t submit_uid)
{
	return SLURM_SUCCESS;
}
