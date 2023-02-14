/*****************************************************************************\
 *  state.c - scrun state handlers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include <sys/stat.h>

#include "src/common/env.h"
#include "src/common/oci_config.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/auth.h"
#include "scrun.h"

#define STATE_MAGIC 0x0a0a0b0b

state_t state = {0};

extern void check_state(void)
{
#ifndef NDEBUG
	struct stat statbuf;

	xassert(oci_conf);

	/* general sanity checks that apply in all cases */
	xassert(state.magic == STATE_MAGIC);
	xassert(state.status != CONTAINER_ST_INVALID);
	xassert(state.status >= CONTAINER_ST_UNKNOWN);
	xassert(state.status < CONTAINER_ST_MAX);
	xassert((state.ptm == -1) || (state.ptm >= 0));
	xassert((state.pts == -1) || (state.pts >= 0));
	xassert(state.requested_signal >= 0);
	xassert(!state.job_env || (xsize(state.job_env) >= sizeof(NULL)));
	xassert(!state.id || xsize(state.id) >= 1);
	xassert((state.pid_file_fd == -1) ||
		(state.pid_file_fd > STDERR_FILENO));

	/* make sure all strings are xmalloc()ed */
	xassert(!state.bundle || xsize(state.bundle) >= 1);
	xassert(!state.console_socket || xsize(state.console_socket) >= 1);
	xassert(!state.pid_file || xsize(state.pid_file) >= 1);
	xassert(!state.anchor_socket || xsize(state.anchor_socket) >= 1);
	xassert(!state.spool_dir || xsize(state.spool_dir) >= 1);
	xassert(!state.config_file || xsize(state.config_file) >= 1);
	xassert(!state.root_dir || xsize(state.root_dir) >= 1);
	xassert(!state.root_path || xsize(state.root_path) >= 1);
	xassert(!state.config || data_get_type(state.config));
	xassert(list_count(state.annotations) >= 0);

	if (state.ptm >= 0) {
		/* this is the anchor pid */
		xassert(state.ptm >= 0);
		xassert(state.id && state.id[0]);
		xassert(state.oci_version && state.oci_version[0]);
		xassert(state.bundle && state.bundle[0]);
		xassert(!stat(state.root_dir, &statbuf));

		switch (state.status) {
		case CONTAINER_ST_MAX :
		case CONTAINER_ST_INVALID :
			fatal("%s: status should never be invalid", __func__);
		case CONTAINER_ST_UNKNOWN :
			/* not loaded yet */
			break;
		case CONTAINER_ST_CREATING :
			xassert(!state.srun_exited);
			xassert(state.root_dir);
			xassert(state.anchor_socket && state.anchor_socket[0]);
			break;
		case CONTAINER_ST_CREATED :
			xassert(state.jobid > 0);
			xassert(!stat(state.root_path, &statbuf));
			xassert(!stat(state.anchor_socket, &statbuf));
			xassert(!stat(state.config_file, &statbuf));
			xassert(state.anchor_socket && state.anchor_socket[0]);
			break;
		case CONTAINER_ST_STARTING :
			xassert(state.user_id != SLURM_AUTH_NOBODY);
			xassert(!state.spool_dir ||
				!stat(state.spool_dir, &statbuf));
			xassert(!stat(state.anchor_socket, &statbuf));
			xassert(!stat(state.config_file, &statbuf));
			xassert(state.anchor_socket && state.anchor_socket[0]);
			/* fall through */
		case CONTAINER_ST_RUNNING :
			xassert(state.user_id != SLURM_AUTH_NOBODY);
			xassert(state.jobid > 0);
			xassert(!state.srun_rc);
			xassert(!state.srun_exited);
			xassert(!state.spool_dir ||
				!stat(state.spool_dir, &statbuf));
			xassert(!stat(state.anchor_socket, &statbuf));
			xassert(state.pid > 1);
			xassert(state.anchor_socket && state.anchor_socket[0]);
			break;
		case CONTAINER_ST_STOPPING :
			xassert(!stat(state.anchor_socket, &statbuf));
			break;
		case CONTAINER_ST_STOPPED :
			xassert(state.job_completed);
			break;
		}
	}
#endif /* !NDEBUG */
}

extern void init_state(void)
{
	xassert(state.magic == 0);
	state.magic = STATE_MAGIC;

	slurm_rwlock_init(&state.lock);
	state.oci_version = xstrdup(OCI_VERSION);
	state.annotations = list_create(destroy_config_key_pair);
	state.ptm = -1;
	state.pts = -1;

	/*
	 * Use the running uid and gid until we hear back from slurmctld with
	 * the resolved uid/gid
	 */
	state.user_id = getuid();
	state.group_id = getgid();

	state.status = CONTAINER_ST_UNKNOWN;
	state.pid_file_fd = -1;
}

extern void destroy_state(void)
{
	check_state();

	state.magic = ~STATE_MAGIC;
	slurm_rwlock_destroy(&state.lock);

	state.status = CONTAINER_ST_INVALID;

	if (state.pid_file_fd != -1)
		(void) close(state.pid_file_fd);

	xfree(state.oci_version);
	xfree(state.id);
	xfree(state.bundle);
	xfree(state.orig_bundle);
	FREE_NULL_LIST(state.annotations);
	xfree(state.console_socket);
	xfree(state.pid_file);
	xfree(state.anchor_socket);
	xfree(state.spool_dir);
	env_array_free(state.job_env);
	xfree(state.config_file);
	xfree(state.root_dir);
	xfree(state.root_path);
	xfree(state.orig_root_path);
	FREE_NULL_DATA(state.config);
	FREE_NULL_LIST(state.start_requests);
	FREE_NULL_LIST(state.delete_requests);
	xassert(!state.startup_con); /* conmgr owns this */
}

static int _get_job_step_state(slurm_job_info_t *job)
{
	/* job is running so we must see if there is a step */
	int rc;
	job_step_info_response_msg_t *resp = NULL;

	rc = slurm_get_job_steps(0, job->job_id, 0, &resp, 0);

	if (rc) {
		/* query failed...job may have just died */
		change_status(CONTAINER_ST_STOPPED);
	} else if (!resp->job_step_count) {
		/* job is running but no steps yet */
		change_status(CONTAINER_ST_CREATED);
	} else {
		/* job and step exist */
		change_status(CONTAINER_ST_RUNNING);
	}

	slurm_free_job_step_info_response_msg(resp);
	return rc;
}

/* based partially on slurm_load_jobs() */
static int _get_job_state()
{
	/*
	 * For any reason the anchor failed to respond with a state.
	 * Infer the state of the container from the job.
	 */
	int rc;
	job_info_msg_t *jobs = NULL;
	slurm_job_info_t *job;
	slurm_step_id_t *step;
	list_t *steps = list_create((ListDelF) slurm_free_step_id);

	debug2("%s: attempting to query slurmctld for state of %s",
	       __func__, state.id);

	rc = slurm_find_step_ids_by_container_id(SHOW_LOCAL, SLURM_AUTH_NOBODY,
						 state.id, steps);

	if (rc || !list_count(steps)) {
		/* container ID is not known */

		debug2("%s: query slurmctld for state of %s failed",
		       __func__, state.id);

		change_status(CONTAINER_ST_STOPPED);
		goto cleanup;
	}

	if (list_count(steps) > 1)
		info("WARNING: more than one job has same container id:%s. State information may be invalid.",
		     state.id);

	/* there should only ever 1 one job with the same container id */
	//TODO: may want to check every job that matches?
	step = list_peek(steps);

	debug2("%s: query slurmctld for %pS state of %s",
	       __func__, &step, state.id);

	rc = slurm_load_job(&jobs, step->job_id, 0);

	if (rc || !jobs || (jobs->record_count <= 0)) {
		/* no job with name found */

		debug2("%s: query slurmctld for %pS state of %s failed",
		       __func__, &step, state.id);

		change_status(CONTAINER_ST_STOPPED);
		goto cleanup;
	}

	job = jobs->job_array;

	if (!job->container) {
		/* job with same name but no container */

		debug2("%s: query slurmctld for %pS did not have correct container %s",
		       __func__, &step, state.id);

		rc = ESLURM_INVALID_CONTAINER_ID;
		goto cleanup;
	}

	/* sanity check the search is working correctly */
	xassert(!xstrcmp(job->name, state.id));

	/* note the job id in case we want to kill the job */
	state.jobid = job->job_id;

	switch (job->job_state)
	{
	case JOB_PENDING:
		change_status(CONTAINER_ST_CREATING);
		break;
	case JOB_RUNNING:
	case JOB_SUSPENDED:
		/* need to see status of step now */
		rc = _get_job_step_state(job);
		break;
	case JOB_COMPLETE:
	case JOB_CANCELLED:
	case JOB_FAILED:
	case JOB_TIMEOUT:
	case JOB_NODE_FAIL:
	case JOB_PREEMPTED:
	case JOB_BOOT_FAIL:
	case JOB_DEADLINE:
		change_status(CONTAINER_ST_STOPPED);
		break;
	default:
		xassert(false);
	}

	debug2("%s: query slurmctld for %pS for %s found JobId=%u: %s -> %s",
	       __func__, &step, state.id, job->job_id,
	       job_state_string(job->job_state),
	       slurm_container_status_to_str(state.status));

cleanup:
	if (jobs)
		slurm_free_job_info_msg(jobs);
	FREE_NULL_LIST(steps);
	return rc;
}

extern int get_anchor_state(void)
{
	slurm_msg_t req, *resp = NULL;
	int rc;

	check_state();

	slurm_msg_t_init(&req);
	slurm_msg_set_r_uid(&req, SLURM_AUTH_UID_ANY);
	req.msg_type = REQUEST_CONTAINER_STATE;

	debug2("%s: attempting to query state via %s",
	       __func__, state.anchor_socket);
	rc = send_rpc(&req, &resp, state.id, NULL);
	slurm_free_msg_members(&req);

	if (rc) {
		slurm_free_msg(resp);
		debug("%s: send_rpc() failed: %s",
		      __func__, slurm_strerror(rc));

		/* fail back to asking slurmctld about the job */
		debug2("%s: failed to query state via %s",
		       __func__, state.anchor_socket);
		return _get_job_state();
	}

	/*
	 * TODO: future fail state could be to try to talk to slurmstepd incase
	 * this request is coming from different node than the anchor
	 */

	if (resp && resp->data &&
	    (resp->msg_type == RESPONSE_CONTAINER_STATE)) {
		/* take ownership of the state contents */
		container_state_msg_t *s = resp->data;
		xassert(!xstrcmp(s->id, state.id));
		xassert(s->status != CONTAINER_ST_INVALID);

		debug2("%s: state via %s: %s",
		       __func__, state.anchor_socket,
		       slurm_container_status_to_str(s->status));

		SWAP(s->oci_version, state.oci_version);
		change_status(s->status);
		state.pid = s->pid;
		SWAP(s->bundle, state.bundle);
		SWAP(s->annotations, state.annotations);
	} else {
		debug2("%s: failed to query state via %s",
		       __func__, state.anchor_socket);
		rc = _get_job_state();
	}

	check_state();
	slurm_free_msg(resp);
	return rc;
}

extern void change_status_funcname(container_state_msg_status_t status,
				   bool force, const char *src, bool locked)
{
#ifndef NDEBUG
	static container_state_msg_status_t last_status = CONTAINER_ST_UNKNOWN;
#endif

	if (locked) {
		xassert(state.locked);
		xassert(state.needs_lock);
	} else {
		xassert(!state.locked);
		xassert(!state.needs_lock);
	}

#ifndef NDEBUG
	/* detect if anything else changed value */
	xassert(force || (last_status == state.status));
	last_status = status;
#endif

	debug("%s: changing status from %s to %s",
	      src, slurm_container_status_to_str(state.status),
	      slurm_container_status_to_str(status));

	xassert(status != CONTAINER_ST_INVALID);
	xassert(status >= CONTAINER_ST_CREATING);
	xassert(status < CONTAINER_ST_MAX);

	/* status can never go backwards */
	xassert(force || (status >= state.status));

	state.status = status;
}
