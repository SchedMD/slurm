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
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/plugins/jobacct_gather/cgroup/jobacct_gather_cgroup.h"
#include "../common/common_jag.h"

#define _DEBUG 0

/* These are defined here so when we link with something other than
 * the slurmd we will have these symbols defined.  They will get
 * overwritten when linking with the slurmd.
 */
#if defined (__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf;
#endif

typedef struct {
	uint32_t taskid;
	pid_t pid;
	uid_t uid;
	gid_t gid;
	List task_cg_list;
	char *step_cgroup_path;
	char *task_cgroup_path;
} jobacct_cgroup_create_callback_t;

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

static void _prec_extra(jag_prec_t *prec, uint32_t taskid)
{
	unsigned long utime, stime, total_rss, total_pgpgin;
	char *cpu_time = NULL, *memory_stat = NULL, *ptr;
	size_t cpu_time_size = 0, memory_stat_size = 0;
	xcgroup_t *task_cpuacct_cg = NULL;;
	xcgroup_t *task_memory_cg = NULL;
	bool exit_early = false;

	/* Find which task cgroups to use */
	task_memory_cg = list_find_first(task_memory_cg_list,
					 find_task_cg_info,
					 &taskid);
	task_cpuacct_cg = list_find_first(task_cpuacct_cg_list,
					  find_task_cg_info,
					  &taskid);

	/*
	 * We should always find the task cgroups; if we don't for some reason,
	 * just print an error and return.
	 */
	if (!task_cpuacct_cg) {
		error("Could not find task_cpuacct_cg, this should never happen");
		exit_early = true;
	}
	if (!task_memory_cg) {
		error("Could not find task_memory_cg, this should never happen");
		exit_early = true;
	}
	if (exit_early)
		return;

	//DEF_TIMERS;
	//START_TIMER;
	/* info("before"); */
	xcgroup_get_param(task_cpuacct_cg, "cpuacct.stat",
			  &cpu_time, &cpu_time_size);
	if (cpu_time == NULL) {
		debug2("failed to collect cpuacct.stat pid %d ppid %d",
		       prec->pid, prec->ppid);
	} else {
		sscanf(cpu_time, "%*s %lu %*s %lu", &utime, &stime);
		/*
		 * Store unnormalized times, we will normalize in when
		 * transfering to a struct jobacctinfo in job_common_poll_data()
		 */
		prec->usec = utime;
		prec->ssec = stime;
	}

	xcgroup_get_param(task_memory_cg, "memory.stat",
			  &memory_stat, &memory_stat_size);
	if (memory_stat == NULL) {
		debug2("failed to collect memory.stat  pid %d ppid %d",
		       prec->pid, prec->ppid);
	} else {
		/*
		 * This number represents the amount of "dirty" private memory
		 * used by the cgroup.  From our experience this is slightly
		 * different than what proc presents, but is probably more
		 * accurate on what the user is actually using.
		 */
		if ((ptr = strstr(memory_stat, "total_rss"))) {
			sscanf(ptr, "total_rss %lu", &total_rss);
			prec->tres_data[TRES_ARRAY_MEM].size_read = total_rss;
		}

		/*
		 * total_pgmajfault is what is reported in proc, so we use
		 * the same thing here.
		 */
		if ((ptr = strstr(memory_stat, "total_pgmajfault"))) {
			sscanf(ptr, "total_pgmajfault %lu", &total_pgpgin);
			prec->tres_data[TRES_ARRAY_PAGES].size_read =
				total_pgpgin;
		}
	}

	xfree(cpu_time);
	xfree(memory_stat);

	return;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init (void)
{
	/* If running on the slurmctld don't do any of this since it
	   isn't needed.
	*/
	if (running_in_slurmstepd()) {
		jag_common_init(0);

		/* initialize cpuinfo internal data */
		if (xcpuinfo_init() != XCPUINFO_SUCCESS) {
			return SLURM_ERROR;
		}

		/* enable cpuacct cgroup subsystem */
		if (jobacct_gather_cgroup_cpuacct_init() != SLURM_SUCCESS) {
			xcpuinfo_fini();
			return SLURM_ERROR;
		}

		/* enable memory cgroup subsystem */
		if (jobacct_gather_cgroup_memory_init() != SLURM_SUCCESS) {
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
		jobacct_gather_cgroup_cpuacct_fini();
		jobacct_gather_cgroup_memory_fini();
		acct_gather_energy_fini();
	}
	return SLURM_SUCCESS;
}

/*
 * jobacct_gather_p_poll_data() - Build a table of all current processes
 *
 * IN/OUT: task_list - list containing current processes.
 * IN: pgid_plugin - if we are running with the pgid plugin.
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
extern void jobacct_gather_p_poll_data(
	List task_list, bool pgid_plugin, uint64_t cont_id, bool profile)
{
	static jag_callbacks_t callbacks;
	static bool first = 1;

	if (first) {
		memset(&callbacks, 0, sizeof(jag_callbacks_t));
		first = 0;
		callbacks.prec_extra = _prec_extra;
	}

	jag_common_poll_data(task_list, pgid_plugin, cont_id, &callbacks,
			     profile);

	return;
}

extern int jobacct_gather_p_endpoll(void)
{
	jag_common_fini();

	return SLURM_SUCCESS;
}

extern int jobacct_gather_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	if (jobacct_gather_cgroup_cpuacct_attach_task(pid, jobacct_id) !=
	    SLURM_SUCCESS)
		return SLURM_ERROR;

	if (jobacct_gather_cgroup_memory_attach_task(pid, jobacct_id) !=
	    SLURM_SUCCESS)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int find_task_cg_info(void *x, void *key)
{
	task_cg_info_t *task_cg = (task_cg_info_t*)x;
	uint32_t taskid = *(uint32_t*)key;

	if (task_cg->taskid == taskid)
		return 1;

	return 0;
}

extern void free_task_cg_info(void *object)
{
	task_cg_info_t *task_cg = (task_cg_info_t *)object;

	if (task_cg) {
		xcgroup_destroy(&task_cg->task_cg);
		xfree(task_cg);
	}
}

static int _handle_task_cgroup(const char *calling_func,
			       xcgroup_ns_t *ns,
			       void *callback_arg)
{
	int rc;
	bool need_to_add = false;
	task_cg_info_t *task_cg_info;
	jobacct_cgroup_create_callback_t *cgroup_callback =
		(jobacct_cgroup_create_callback_t *)callback_arg;

	uint32_t taskid = cgroup_callback->taskid;
	pid_t pid = cgroup_callback->pid;
	uid_t uid = cgroup_callback->uid;
	gid_t gid = cgroup_callback->gid;
	List task_cg_list = cgroup_callback->task_cg_list;
	char *step_cgroup_path = cgroup_callback->step_cgroup_path;
	char *task_cgroup_path = cgroup_callback->task_cgroup_path;

	/* build task cgroup relative path */
	if (snprintf(task_cgroup_path, PATH_MAX, "%s/task_%u",
		     step_cgroup_path, taskid) >= PATH_MAX) {
		error("%s: unable to build task %u memory cg relative path: %m",
		      calling_func, taskid);
		return SLURM_ERROR;
	}

	if (!(task_cg_info = list_find_first(task_cg_list,
					     find_task_cg_info,
					     &taskid))) {
		task_cg_info = xmalloc(sizeof(*task_cg_info));
		task_cg_info->taskid = taskid;
		need_to_add = true;
	}
	/*
	 * Create task cgroup in the memory ns
	 */
	if (xcgroup_create(ns, &task_cg_info->task_cg,
			   task_cgroup_path,
			   uid, gid) != SLURM_SUCCESS) {
		/* Don't use free_task_cg_info as the task_cg isn't there */
		xfree(task_cg_info);

		error("%s: unable to create task %u cgroup",
		      calling_func, taskid);
		return SLURM_ERROR;
	}

	if (xcgroup_instantiate(&task_cg_info->task_cg) != SLURM_SUCCESS) {
		free_task_cg_info(task_cg_info);
		error("%s: unable to instantiate task %u cgroup",
		      calling_func, taskid);
		return SLURM_ERROR;
	}

	/*
	 * Attach the slurmstepd to the task memory cgroup
	 */
	rc = xcgroup_add_pids(&task_cg_info->task_cg, &pid, 1);
	if (rc != SLURM_SUCCESS) {
		error("%s: unable to add slurmstepd to memory cg '%s'",
		      calling_func, task_cg_info->task_cg.path);
		rc = SLURM_ERROR;
	} else
		rc = SLURM_SUCCESS;

	/* Add the task cgroup to the list now that it is initialized. */
	if (need_to_add)
		list_append(task_cg_list, task_cg_info);

	return rc;
}

extern int create_jobacct_cgroups(const char *calling_func,
				  const jobacct_id_t *jobacct_id,
				  pid_t pid,
				  xcgroup_ns_t *ns,
				  xcgroup_t *job_cg,
				  xcgroup_t *step_cg,
				  List task_cg_list,
				  xcgroup_t *user_cg,
				  char job_cgroup_path[],
				  char step_cgroup_path[],
				  char task_cgroup_path[],
				  char user_cgroup_path[])
{
	stepd_step_rec_t *job = jobacct_id->job;
	jobacct_cgroup_create_callback_t cgroup_callback = {
		.taskid = jobacct_id->taskid,
		.pid = pid,
		.uid = job->uid,
		.gid = job->gid,
		.task_cg_list = task_cg_list,
		.step_cgroup_path = step_cgroup_path,
		.task_cgroup_path = task_cgroup_path,
	};

	return xcgroup_create_hierarchy(__func__,
					job,
					ns,
					job_cg,
					step_cg,
					user_cg,
					job_cgroup_path,
					step_cgroup_path,
					user_cgroup_path,
					_handle_task_cgroup,
					&cgroup_callback);
}
