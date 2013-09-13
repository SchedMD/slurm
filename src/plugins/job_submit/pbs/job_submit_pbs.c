/*****************************************************************************\
 *  job_submit_pbs.c - Translate PBS job options specifications to the Slurm
 *			equivalents, particularly job dependencies.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>

#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
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
const char plugin_name[]       	= "Job submit PBS plugin";
const char plugin_type[]       	= "job_submit/pbs";
const uint32_t plugin_version   = 100;
const uint32_t min_plug_version = 100;

int init (void)
{
	return SLURM_SUCCESS;
}

int fini (void)
{
	return SLURM_SUCCESS;
}

static void _add_env(struct job_descriptor *job_desc, char *new_env)
{
	if (!job_desc->environment || !new_env)
		return;	/* Nothing we can do for interactive jobs */

	xrealloc(job_desc->environment,
		 sizeof(char *) * (job_desc->env_size + 2));
	job_desc->environment[job_desc->env_size] = xstrdup(new_env);
	job_desc->env_size++;
}

static void _add_env2(struct job_descriptor *job_desc, char *key, char *val)
{
	int len;
	char *new_env;

	if (!job_desc->environment || !key || !val)
		return;	/* Nothing we can do for interactive jobs */

	len = sizeof(key) + sizeof(val) + 2;
	new_env = xmalloc(sizeof(len));
	snprintf(new_env, len, "%s=%s", key, val);
	_add_env(job_desc, new_env);
	xfree(new_env);
}

static void _decr_depend_cnt(struct job_record *job_ptr)
{
	int cnt;

	if (!job_ptr->comment || strncmp(job_ptr->comment, "on:", 3)) {
		info("%s: invalid job depend before option on job %u",
		     plugin_type, job_ptr->job_id);
		return;
	}

	cnt = atoi(job_ptr->comment + 3);
	if (cnt > 0)
		cnt--;
	xfree(job_ptr->comment);
	xstrfmtcat(job_ptr->comment, "on:%d", cnt);		
}

/* We can not invoke update_job_dependency() until the new job record has
 * been created, hence this sleeping thread modifies the dependent job
 * later. */
static void *_dep_agent(void *args)
{
	struct job_record *job_ptr = (struct job_record *) args;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK};

	usleep(100000);
	lock_slurmctld(job_write_lock);
	if (job_ptr && job_ptr->details && (job_ptr->magic == JOB_MAGIC) &&
	    job_ptr->comment && !strncmp(job_ptr->comment, "on:", 3)) {
		update_job_dependency(job_ptr, job_ptr->details->dependency);
		if (!strcmp(job_ptr->comment, "on:0")) {
			xfree(job_ptr->comment);
			set_job_prio(job_ptr);
		}
	}
	unlock_slurmctld(job_write_lock);
	return NULL;
}

static void _xlate_before(char *depend, uint32_t submit_uid)
{
	uint32_t job_id;
	char *last_ptr = NULL, *new_dep = NULL, *tok, *type;
	struct job_record *job_ptr;
        pthread_attr_t attr;
	pthread_t dep_thread;


	tok = strtok_r(depend, ":", &last_ptr);
	if (!strcmp(tok, "before"))
		type = "after";
	else if (!strcmp(tok, "beforeany"))
		type = "afterany";
	else if (!strcmp(tok, "beforenotok"))
		type = "afternotok";
	else if (!strcmp(tok, "beforeok"))
		type = "afterok";
	else {
		info("%s: discarding invalid job dependency option %s",
		     plugin_type, tok);
		return;
	}

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
			xstrfmtcat(new_dep, "%s:%u", type, get_next_job_id());
			xfree(job_ptr->details->dependency);
			job_ptr->details->dependency = new_dep;
			new_dep = NULL;
			_decr_depend_cnt(job_ptr);

			slurm_attr_init(&attr);
			pthread_attr_setdetachstate(&attr,
						    PTHREAD_CREATE_DETACHED);
			pthread_create(&dep_thread, &attr, _dep_agent, job_ptr);
			slurm_attr_destroy(&attr);
		}
		tok = strtok_r(NULL, ":", &last_ptr);
	}
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
static void _xlate_dependency(struct job_descriptor *job_desc,
			      uint32_t submit_uid)
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
		if (!strncmp(tok, "after", 5)  ||
		    !strncmp(tok, "expand", 6) ||
		    !strncmp(tok, "singleton", 9)) {
			if (result)
				xstrcat(result, ",");
			xstrcat(result, tok);
		} else if (!strncmp(tok, "on:", 3)) {
			job_desc->priority = 0;	/* Job is held */
			xfree(job_desc->comment);
			xstrcat(job_desc->comment, tok);
		} else if (!strncmp(tok, "before", 6)) {
			_xlate_before(tok, submit_uid);
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

extern int job_submit(struct job_descriptor *job_desc, uint32_t submit_uid)
{
	_xlate_dependency(job_desc, submit_uid);

	if (job_desc->account)
		_add_env2(job_desc, "PBS_ACCOUNT", job_desc->account);

	if (job_desc->script) {
		_add_env(job_desc, "PBS_ENVIRONMENT=PBS_BATCH");
	} else {
		/* Interactive jobs lack an environment in the job submit
		 * RPC, so it needs to be handled by a SPANK plugin */
		/* _add_env(job_desc, "PBS_ENVIRONMENT=PBS_INTERACTIVE"); */
	}

	if (job_desc->partition)
		_add_env2(job_desc, "PBS_QUEUE", job_desc->partition);

	return SLURM_SUCCESS;
}

/* Lua script hook called for "modify job" event. */
extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid)
{
	_xlate_dependency(job_desc, submit_uid);
	return SLURM_SUCCESS;
}
