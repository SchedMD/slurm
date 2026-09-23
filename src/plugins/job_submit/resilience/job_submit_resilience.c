/*****************************************************************************\
 *  job_submit_resilience.c - opt jobs into adaptive resilience
 *****************************************************************************
 *  Copyright (C) 2026 Gustavo Lima <gustcol@gmail.com>
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

#include <stdlib.h>
#include <string.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * A job is opted into adaptive resilience when its comment contains the
 * word "resilience" (sbatch --comment=resilience), or when it is submitted
 * to a partition listed in SchedulerParameters=resilience_partitions=a,b.
 */

const char plugin_name[] = "Job submit adaptive resilience plugin";
const char plugin_type[] = "job_submit/resilience";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static bool _partition_enabled(const char *part_name)
{
	char *list, *tok, *save_ptr = NULL, *end;
	bool found = false;

	if (!part_name || !slurm_conf.sched_params)
		return false;

	tok = xstrcasestr(slurm_conf.sched_params, "resilience_partitions=");
	if (!tok)
		return false;

	list = xstrdup(tok + strlen("resilience_partitions="));
	if ((end = strchr(list, ' ')))
		*end = '\0';
	for (tok = strtok_r(list, ",", &save_ptr); tok;
	     tok = strtok_r(NULL, ",", &save_ptr)) {
		if (!xstrcasecmp(tok, part_name)) {
			found = true;
			break;
		}
	}
	xfree(list);

	return found;
}

static bool _comment_enabled(const char *comment)
{
	char *list, *tok, *save_ptr = NULL;
	bool found = false;

	if (!comment)
		return false;

	list = xstrdup(comment);
	for (tok = strtok_r(list, " ,;", &save_ptr); tok;
	     tok = strtok_r(NULL, " ,;", &save_ptr)) {
		if (!xstrcasecmp(tok, "resilience")) {
			found = true;
			break;
		}
	}
	xfree(list);

	return found;
}

extern int job_submit(job_desc_msg_t *job_desc, uint32_t submit_uid,
		      char **err_msg)
{
	if (_comment_enabled(job_desc->comment) ||
	    _partition_enabled(job_desc->partition)) {
		job_desc->bitflags |= ADAPTIVE_RESILIENCE;
		debug2("%s: adaptive resilience enabled for job from uid %u",
		       plugin_type, submit_uid);
	}

	return SLURM_SUCCESS;
}

extern int job_modify(job_desc_msg_t *job_desc, job_record_t *job_ptr,
		      uint32_t submit_uid, char **err_msg)
{
	if (job_desc->comment && _comment_enabled(job_desc->comment)) {
		job_desc->bitflags |= ADAPTIVE_RESILIENCE;
		debug2("%s: adaptive resilience enabled for %pJ by uid %u",
		       plugin_type, job_ptr, submit_uid);
	}

	return SLURM_SUCCESS;
}
