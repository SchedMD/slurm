/*****************************************************************************\
 *  jobacct_gather_cgroup.c - slurm job accounting gather plugin for cgroup.
 *****************************************************************************
 *  Copyright (C) 2011 Bull.
 *  Written by Martin Perry, <martin.perry@bull.com>, who borrowed heavily
 *  from other parts of Slurm
 *  CODE-OCEC-09-009. All rights reserved.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <fcntl.h>
#include <signal.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/xstring.h"
#include "src/common/cgroup.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/plugins/jobacct_gather/cgroup/jobacct_gather_cgroup.h"
#include "../common/common_jag.h"

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
 * the plugin (e.g., "jobacct" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Job accounting gather cgroup plugin";
const char plugin_type[] = "jobacct_gather/cgroup";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static bool is_first_task = true;

static void _prec_extra(jag_prec_t *prec, uint32_t taskid)
{
	cgroup_acct_t *cgroup_acct_data;

	cgroup_acct_data = cgroup_g_task_get_acct_data(taskid);

	if (!cgroup_acct_data) {
		error("Cannot get cgroup accounting data for %d", taskid);
		return;
	}

	/* We discard the data if some value was incorrect */
	if (cgroup_acct_data->usec == NO_VAL64 &&
	    cgroup_acct_data->ssec == NO_VAL64) {
		debug2("failed to collect cgroup cpu stats pid %d ppid %d",
		       prec->pid, prec->ppid);
	} else {
		prec->usec = cgroup_acct_data->usec;
		prec->ssec = cgroup_acct_data->ssec;
	}

	if (cgroup_acct_data->total_rss == NO_VAL64 &&
	    cgroup_acct_data->total_pgmajfault == NO_VAL64 &&
	    cgroup_acct_data->total_vmem == NO_VAL64) {
		debug2("failed to collect cgroup memory stats pid %d ppid %d",
		       prec->pid, prec->ppid);
	} else {
		/*
		 * This number represents the amount of "dirty" private memory
		 * used by the cgroup.  From our experience this is slightly
		 * different than what proc presents, but is probably more
		 * accurate on what the user is actually using.
		 */
		prec->tres_data[TRES_ARRAY_MEM].size_read =
			cgroup_acct_data->total_rss;

		/*
		 * total_pgmajfault is what is reported in proc, so we use
		 * the same thing here.
		 */
		prec->tres_data[TRES_ARRAY_PAGES].size_read =
			cgroup_acct_data->total_pgmajfault;

		/*
		 * The most important thing about getting the values from cgroup
		 * is that it returns the amount of mem occupied by the whole
		 * process tree, not only the stepd child like by default.
		 * Adding vmem to cgroup as well, so the user doesn't see a
		 * RSS>VMem in some cases.
		 */
		prec->tres_data[TRES_ARRAY_VMEM].size_read =
			cgroup_acct_data->total_vmem;

	}

	xfree(cgroup_acct_data);
	return;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init (void)
{
	if (running_in_slurmd() &&
	    ((cgroup_g_initialize(CG_MEMORY) != SLURM_SUCCESS) ||
	     (cgroup_g_initialize(CG_CPUACCT) != SLURM_SUCCESS))) {
		error("There's an issue initializing memory or cpu controller");
		return SLURM_ERROR;
	}

	if (running_in_slurmstepd()) {
		jag_common_init(cgroup_g_get_acct_units());

		if (xcpuinfo_init() != SLURM_SUCCESS) {
			return SLURM_ERROR;
		}

		/* Initialize the controllers which we want accounting for. */
		if (cgroup_g_initialize(CG_MEMORY) != SLURM_SUCCESS) {
			xcpuinfo_fini();
			return SLURM_ERROR;
		}

		if (cgroup_g_initialize(CG_CPUACCT) != SLURM_SUCCESS) {
			xcpuinfo_fini();
			return SLURM_ERROR;
		}
	}

	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini (void)
{
	if (running_in_slurmstepd()) {
		/* Only destroy step if it has been previously created */
		if (!is_first_task) {
			/* Remove job/uid/step directories */
			cgroup_g_step_destroy(CG_MEMORY);
			cgroup_g_step_destroy(CG_CPUACCT);
		}

		acct_gather_energy_fini();
	}

	debug("%s unloaded", plugin_name);

	return SLURM_SUCCESS;
}

/*
 * jobacct_gather_p_poll_data() - Build a table of all current processes
 *
 * IN/OUT: task_list - list containing current processes.
 * IN: cont_id - container id of processes if not running with pgid.
 *
 * OUT:	none
 *
 * THREADSAFE! Only one thread ever gets here.  It is locked in
 * slurm_jobacct_gather.
 *
 * Assumption:
 *    Any file with a name of the form "/proc/[0-9]+/stat"
 *    is a Linux-style stat entry. We disregard the data if they look
 *    wrong.
 */
extern void jobacct_gather_p_poll_data(List task_list, uint64_t cont_id,
				       bool profile)
{
	static jag_callbacks_t callbacks;
	static bool first = 1;

	if (first) {
		memset(&callbacks, 0, sizeof(jag_callbacks_t));
		first = 0;
		callbacks.prec_extra = _prec_extra;
	}

	jag_common_poll_data(task_list, cont_id, &callbacks, profile);

	return;
}

extern int jobacct_gather_p_endpoll(void)
{
	jag_common_fini();

	return SLURM_SUCCESS;
}

extern int jobacct_gather_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	int rc = SLURM_SUCCESS;

	if (is_first_task) {
		/* Only do once in this plugin */
		if (cgroup_g_step_create(CG_CPUACCT, jobacct_id->job)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;

		if (cgroup_g_step_create(CG_MEMORY, jobacct_id->job)
		    != SLURM_SUCCESS) {
			cgroup_g_step_destroy(CG_CPUACCT);
			return SLURM_ERROR;
		}
		is_first_task = false;
	}

	if (cgroup_g_task_addto(CG_CPUACCT, jobacct_id->job, pid,
				jobacct_id->taskid) != SLURM_SUCCESS)
		rc = SLURM_ERROR;

	if (cgroup_g_task_addto(CG_MEMORY, jobacct_id->job, pid,
				jobacct_id->taskid) != SLURM_SUCCESS)
		rc = SLURM_ERROR;

	return rc;
}
