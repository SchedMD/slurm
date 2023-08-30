/*****************************************************************************\
 *  job_submit_pbs.c - Translate PBS job options specifications to the Slurm
 *			equivalents, particularly job dependencies.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC.
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

#include <dlfcn.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/xstring.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

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
const char plugin_name[]       	= "Job submit PBS plugin";
const char plugin_type[]       	= "job_submit/pbs";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static pthread_mutex_t depend_mutex = PTHREAD_MUTEX_INITIALIZER;

int init (void)
{
	return SLURM_SUCCESS;
}

int fini (void)
{
	return SLURM_SUCCESS;
}

static void _add_env(job_desc_msg_t *job_desc, char *new_env)
{
	if (!job_desc->environment || !new_env)
		return;	/* Nothing we can do for interactive jobs */

	xrealloc(job_desc->environment,
		 sizeof(char *) * (job_desc->env_size + 2));
	job_desc->environment[job_desc->env_size] = xstrdup(new_env);
	job_desc->env_size++;
}

static void _add_env2(job_desc_msg_t *job_desc, char *key, char *val)
{
	char *new_env = NULL;

	if (!job_desc->environment || !key || !val)
		return;	/* Nothing we can do for interactive jobs */

	xstrfmtcat(new_env, "%s=%s", key, val);
	_add_env(job_desc, new_env);
	xfree(new_env);
}

static void _decr_depend_cnt(job_record_t *job_ptr)
{
	char buf[16], *end_ptr = NULL, *tok = NULL;
	int cnt, width;

	if (job_ptr->comment)
		tok = strstr(job_ptr->comment, "on:");
	if (!tok) {
		info("%s: invalid job depend before option on job %u",
		     plugin_type, job_ptr->job_id);
		return;
	}

	cnt = strtol(tok + 3, &end_ptr, 10);
	if (cnt > 0)
		cnt--;
	width = MIN(sizeof(buf) - 1, (end_ptr - tok - 3));
	sprintf(buf, "%*d", width, cnt);
	memcpy(tok + 3, buf, width);
}

/* We can not invoke update_job_dependency() until the new job record has
 * been created, hence this sleeping thread modifies the dependent job
 * later. */
static void *_dep_agent(void *args)
{
	/* Locks: Write job, read node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	job_record_t *job_ptr = (job_record_t *) args;
	char *end_ptr = NULL, *tok;
	int cnt = 0;

	usleep(100000);
	lock_slurmctld(job_write_lock);
	if (job_ptr && job_ptr->details && (job_ptr->magic == JOB_MAGIC) &&
	    job_ptr->comment && strstr(job_ptr->comment, "on:")) {
		char *new_depend = job_ptr->details->dependency;
		job_ptr->details->dependency = NULL;
		update_job_dependency(job_ptr, new_depend);
		xfree(new_depend);
		tok = strstr(job_ptr->comment, "on:");
		cnt = strtol(tok + 3, &end_ptr, 10);
	}
	if (cnt == 0)
		set_job_prio(job_ptr);
	unlock_slurmctld(job_write_lock);

	return NULL;
}

static void _xlate_before(char *depend, uint32_t submit_uid, uint32_t my_job_id)
{
	uint32_t job_id;
	char *last_ptr = NULL, *new_dep = NULL, *tok, *type;
	job_record_t *job_ptr;

	tok = strtok_r(depend, ":", &last_ptr);
	if (!xstrcmp(tok, "before"))
		type = "after";
	else if (!xstrcmp(tok, "beforeany"))
		type = "afterany";
	else if (!xstrcmp(tok, "beforenotok"))
		type = "afternotok";
	else if (!xstrcmp(tok, "beforeok"))
		type = "afterok";
	else {
		info("%s: discarding invalid job dependency option %s",
		     plugin_type, tok);
		return;
	}

	/* NOTE: We are updating a job record here in order to implement
	 * the depend=before option. We are doing so without the write lock
	 * on the job record, but using a local mutex to prevent multiple
	 * updates on the same job when multiple jobs satisfying the dependency
	 * are being processed at the same time (all with read locks). The
	 * job read lock will prevent anyone else from getting a job write
	 * lock and using a job write lock causes serious performance problems
	 * for slow job_submit plugins. Not an ideal solution, but the best
	 * option that we see. */
	slurm_mutex_lock(&depend_mutex);
	tok = strtok_r(NULL, ":", &last_ptr);
	while (tok) {
		job_id = atoi(tok);
		job_ptr = find_job_record(job_id);
		if (!job_ptr) {
			info("%s: discarding invalid job dependency before %s",
			     plugin_type, tok);
		} else if ((submit_uid != job_ptr->user_id) &&
			   !validate_super_user(submit_uid)) {
			error("%s: Security violation: uid %u trying to alter "
			      "job %u belonging to uid %u",
			      plugin_type, submit_uid, job_ptr->job_id,
			      job_ptr->user_id);
		} else if ((!IS_JOB_PENDING(job_ptr)) ||
			   (job_ptr->details == NULL)) {
			info("%s: discarding job before dependency on "
			     "non-pending job %u",
			     plugin_type, job_ptr->job_id);
		} else {
			if (job_ptr->details->dependency) {
				xstrcat(new_dep, job_ptr->details->dependency);
				xstrcat(new_dep, ",");
			}
			xstrfmtcat(new_dep, "%s:%u", type, my_job_id);
			xfree(job_ptr->details->dependency);
			job_ptr->details->dependency = new_dep;
			new_dep = NULL;
			_decr_depend_cnt(job_ptr);

			slurm_thread_create_detached(NULL, _dep_agent, job_ptr);
		}
		tok = strtok_r(NULL, ":", &last_ptr);
	}
	slurm_mutex_unlock(&depend_mutex);
}

/* Translate PBS job dependencies to Slurm equivalents to the exptned possible
 *
 * PBS option		Slurm nearest equivalent
 * ===========		========================
 * after		after
 * afterok		afterok
 * afternotok		afternotok
 * afterany		after
 * before		(set after      in referenced job and release as needed)
 * beforeok		(set afterok    in referenced job and release as needed)
 * beforenotok		(set afternotok in referenced job and release as needed)
 * beforeany		(set afterany   in referenced job and release as needed)
 * N/A			expand
 * on			(store value in job comment and hold it)
 * N/A			singleton
 */
static void _xlate_dependency(job_desc_msg_t *job_desc, uint32_t submit_uid,
			      uint32_t my_job_id)
{
	char *result = NULL;
	char *last_ptr = NULL, *tok;

	if (!job_desc->dependency)
		return;

#if _DEBUG
	info("dependency  in:%s", job_desc->dependency);
#endif

	tok = strtok_r(job_desc->dependency, ",", &last_ptr);
	while (tok) {
		if (!xstrncmp(tok, "after", 5)  ||
		    !xstrncmp(tok, "expand", 6) ||
		    !xstrncmp(tok, "singleton", 9)) {
			if (result)
				xstrcat(result, ",");
			xstrcat(result, tok);
		} else if (!xstrncmp(tok, "on:", 3)) {
			job_desc->priority = 0;	/* Job is held */
			if (job_desc->comment)
				xstrcat(job_desc->comment, ",");
			xstrcat(job_desc->comment, tok);
		} else if (!xstrncmp(tok, "before", 6)) {
			_xlate_before(tok, submit_uid, my_job_id);
		} else {
			info("%s: discarding unknown job dependency option %s",
			     plugin_type, tok);
		}
		tok = strtok_r(NULL, ",", &last_ptr);
	}
#if _DEBUG
	info("dependency out:%s", result);
#endif
	xfree(job_desc->dependency);
	job_desc->dependency = result;
}

extern int job_submit(job_desc_msg_t *job_desc, uint32_t submit_uid)
{
	char *std_out, *tok;
	uint32_t my_job_id;

	my_job_id = get_next_job_id(true);
	_xlate_dependency(job_desc, submit_uid, my_job_id);

	if (job_desc->account)
		_add_env2(job_desc, "PBS_ACCOUNT", job_desc->account);

	if (job_desc->script) {
		/* Setting PBS_ENVIRONMENT causes Intel MPI to believe that
		 * it is running on a PBS system, which isn't the case here. */
		/* _add_env(job_desc, "PBS_ENVIRONMENT=PBS_BATCH"); */
	} else {
		/* Interactive jobs lack an environment in the job submit
		 * RPC, so it needs to be handled by a SPANK plugin */
		/* _add_env(job_desc, "PBS_ENVIRONMENT=PBS_INTERACTIVE"); */
	}

	if (job_desc->partition)
		_add_env2(job_desc, "PBS_QUEUE", job_desc->partition);

	if (job_desc->std_out)
		std_out = job_desc->std_out;
	else
		std_out = "slurm-%j.out";
	if (job_desc->comment)
		xstrcat(job_desc->comment, ",");
	xstrcat(job_desc->comment, "stdout=");
	if (std_out && (std_out[0] != '/') && job_desc->work_dir) {
		xstrcat(job_desc->comment, job_desc->work_dir);
		xstrcat(job_desc->comment, "/");
	}
	tok = strstr(std_out, "%j");
	if (tok) {
		char *tok2;
		char *tmp = xstrdup(std_out);
		tok2 = strstr(tmp, "%j");
		tok2[0] = '\0';
		xstrfmtcat(tmp, "%u", my_job_id);
		xstrcat(tmp, tok + 2);
		xstrcat(job_desc->comment, tmp);
		xfree(tmp);
	} else {
		xstrcat(job_desc->comment, std_out);
	}

	return SLURM_SUCCESS;
}

/* Lua script hook called for "modify job" event. */
extern int job_modify(job_desc_msg_t *job_desc, job_record_t *job_ptr,
		      uint32_t submit_uid, char **err_msg)
{
	/* Locks: Read config, write job, read node, read partition
	 * HAVE BEEN SET ON ENTRY TO THIS FUNCTION */
	char *tok;

	xassert(job_ptr);

	_xlate_dependency(job_desc, submit_uid, job_ptr->job_id);

	if (job_desc->std_out) {
		if (job_ptr->comment)
			xstrcat(job_ptr->comment, ",");
		xstrcat(job_ptr->comment, "stdout=");
		if ((job_desc->std_out[0] != '/') && job_ptr->details &&
		    job_ptr->details->work_dir) {
			xstrcat(job_ptr->comment, job_ptr->details->work_dir);
			xstrcat(job_ptr->comment, "/");
		}
		tok = strstr(job_desc->std_out, "%j");
		if (tok) {
			char *tok2;
			char *tmp = xstrdup(job_desc->std_out);
			tok2 = strstr(tmp, "%j");
			tok2[0] = '\0';
			xstrfmtcat(tmp, "%u", job_ptr->job_id);
			xstrcat(tmp, tok + 2);
			xstrcat(job_ptr->comment, tmp);
			xfree(tmp);
		} else {
			xstrcat(job_ptr->comment, job_desc->std_out);
		}
		xfree(job_desc->std_out);
	}

	return SLURM_SUCCESS;
}
