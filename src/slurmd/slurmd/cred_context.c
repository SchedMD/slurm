/*****************************************************************************\
 *  cred_context.c
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/pack.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"

#include "src/slurmd/slurmd/slurmd.h"

/* FIXME: Y2038 problem */
#define MAX_TIME 0x7fffffff

typedef struct {
	time_t ctime;		/* Time that the cred was created	*/
	time_t expiration;	/* Time at which cred is no longer good	*/
	slurm_step_id_t step_id;/* Slurm step id for this credential	*/
} cred_state_t;

typedef struct {
	time_t ctime;		/* Time that this entry was created         */
	time_t expiration;	/* Time at which credentials can be purged  */
	uint32_t jobid;		/* Slurm job id for this credential         */
	time_t revoked;		/* Time at which credentials were revoked   */
} job_state_t;

static pthread_mutex_t cred_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static list_t *cred_job_list = NULL;
static list_t *cred_state_list = NULL;

static void _drain_node(char *reason)
{
	update_node_msg_t update_node_msg;

	slurm_init_update_node_msg(&update_node_msg);
	update_node_msg.node_names = conf->node_name;
	update_node_msg.node_state = NODE_STATE_DRAIN;
	update_node_msg.reason = reason;

	(void) slurm_update_node(&update_node_msg);
}

static cred_state_t *_cred_state_create(slurm_cred_t *cred)
{
	cred_state_t *s = xmalloc(sizeof(*s));

	memcpy(&s->step_id, &cred->arg->step_id, sizeof(s->step_id));
	s->ctime = cred->ctime;
	s->expiration = cred->ctime + cred_expiration();

	return s;
}

static job_state_t *_job_state_create(uint32_t jobid)
{
	job_state_t *j = xmalloc(sizeof(*j));

	j->jobid = jobid;
	j->revoked = (time_t) 0;
	j->ctime = time(NULL);
	j->expiration = (time_t) MAX_TIME;

	return j;
}

static int _list_find_expired_job_state(void *x, void *key)
{
	job_state_t *j = x;
	time_t curr_time = *(time_t *) key;

	if (j->revoked && (curr_time > j->expiration))
		return 1;
	return 0;
}

static int _list_find_job_state(void *x, void *key)
{
	job_state_t *j = x;
	uint32_t jobid = *(uint32_t *) key;

	if (j->jobid == jobid)
		return 1;
	return 0;
}

static job_state_t *_find_job_state(uint32_t jobid)
{
	return list_find_first(cred_job_list, _list_find_job_state, &jobid);
}

static void _clear_expired_job_states(void)
{
	time_t now = time(NULL);
	list_delete_all(cred_job_list, _list_find_expired_job_state, &now);
}

static int _list_find_expired_cred_state(void *x, void *key)
{
	cred_state_t *s = x;
	time_t curr_time = *(time_t *) key;

	if (curr_time > s->expiration)
		return 1;
	return 0;
}

static void _clear_expired_credential_states(void)
{
	time_t now = time(NULL);
	list_delete_all(cred_state_list, _list_find_expired_cred_state, &now);
}

static void _job_state_pack(void *x, uint16_t protocol_version, buf_t *buffer)
{
	job_state_t *j = x;

	pack32(j->jobid, buffer);
	pack_time(j->revoked, buffer);
	pack_time(j->ctime, buffer);
	pack_time(j->expiration, buffer);
}

static int _job_state_unpack(void **out, uint16_t protocol_version,
			     buf_t *buffer)
{
	job_state_t *j = xmalloc(sizeof(*j));

	safe_unpack32(&j->jobid, buffer);
	safe_unpack_time(&j->revoked, buffer);
	safe_unpack_time(&j->ctime, buffer);
	safe_unpack_time(&j->expiration, buffer);

	debug3("cred_unpack: job %u ctime:%ld revoked:%ld expires:%ld",
	       j->jobid, j->ctime, j->revoked, j->expiration);

	if ((j->revoked) && (j->expiration == (time_t) MAX_TIME)) {
		warning("revoke on job %u has no expiration", j->jobid);
		j->expiration = j->revoked + 600;
	}

	*out = j;
	return SLURM_SUCCESS;

unpack_error:
	xfree(j);
	*out = NULL;
	return SLURM_ERROR;
}

static void _cred_state_pack(void *x, uint16_t protocol_version, buf_t *buffer)
{
	cred_state_t *s = x;

	/* WARNING: this is not safe if pack_step_id() ever changes format */
	pack_step_id(&s->step_id, buffer, protocol_version);
	pack_time(s->ctime, buffer);
	pack_time(s->expiration, buffer);
}

static int _cred_state_unpack(void **out, uint16_t protocol_version,
			      buf_t *buffer)
{
	cred_state_t *s = xmalloc(sizeof(*s));

	if (unpack_step_id_members(&s->step_id, buffer,
				   SLURM_PROTOCOL_VERSION) != SLURM_SUCCESS)
		goto unpack_error;
	safe_unpack_time(&s->ctime, buffer);
	safe_unpack_time(&s->expiration, buffer);

	*out = s;
	return SLURM_SUCCESS;

unpack_error:
	xfree(s);
	*out = NULL;
	return SLURM_ERROR;
}

static void _cred_context_pack(buf_t *buffer)
{
	/* FIXME: find a way to version this file at some point */
	uint16_t version = SLURM_PROTOCOL_VERSION;

	slurm_mutex_lock(&cred_cache_mutex);
	slurm_pack_list(cred_job_list, _job_state_pack, buffer, version);
	slurm_pack_list(cred_state_list, _cred_state_pack, buffer, version);
	slurm_mutex_unlock(&cred_cache_mutex);
}

static void _cred_context_unpack(buf_t *buffer)
{
	/* FIXME: find a way to version this file at some point */
	uint16_t version = SLURM_PROTOCOL_VERSION;

	slurm_mutex_lock(&cred_cache_mutex);

	FREE_NULL_LIST(cred_job_list);
	if (slurm_unpack_list(&cred_job_list, _job_state_unpack,
			      xfree_ptr, buffer, version)) {
		warning("%s: failed to restore job state from file", __func__);
	}
	_clear_expired_job_states();

	FREE_NULL_LIST(cred_state_list);
	if (slurm_unpack_list(&cred_state_list, _cred_state_unpack,
			      xfree_ptr, buffer, version)) {
		warning("%s: failed to restore job state from file", __func__);
	}
	_clear_expired_credential_states();

	slurm_mutex_unlock(&cred_cache_mutex);
}

extern void save_cred_state(void)
{
	char *new_file, *reg_file;
	int cred_fd = -1, rc;
	buf_t *buffer = NULL;
	static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

	reg_file = xstrdup(conf->spooldir);
	xstrcat(reg_file, "/cred_state");
	new_file = xstrdup(conf->spooldir);
	xstrcat(new_file, "/cred_state.new");

	slurm_mutex_lock(&state_mutex);
	if ((cred_fd = creat(new_file, 0600)) < 0) {
		error("creat(%s): %m", new_file);
		if (errno == ENOSPC)
			_drain_node("SlurmdSpoolDir is full");
		goto cleanup;
	}
	buffer = init_buf(1024);
	_cred_context_pack(buffer);
	rc = write(cred_fd, get_buf_data(buffer), get_buf_offset(buffer));
	if (rc != get_buf_offset(buffer)) {
		error("write %s error %m", new_file);
		(void) unlink(new_file);
		if ((rc < 0) && (errno == ENOSPC))
			_drain_node("SlurmdSpoolDir is full");
		goto cleanup;
	}
	(void) unlink(reg_file);
	if (link(new_file, reg_file))
		debug4("unable to create link for %s -> %s: %m",
		       new_file, reg_file);
	(void) unlink(new_file);

cleanup:
	slurm_mutex_unlock(&state_mutex);
	xfree(reg_file);
	xfree(new_file);
	FREE_NULL_BUFFER(buffer);
	if (cred_fd >= 0)
		close(cred_fd);
}

static void _restore_cred_state(void)
{
	char *file_name = NULL;
	buf_t *buffer = NULL;

	file_name = xstrdup(conf->spooldir);
	xstrcat(file_name, "/cred_state");

	if (!(buffer = create_mmap_buf(file_name)))
		goto cleanup;

	_cred_context_unpack(buffer);

cleanup:
	xfree(file_name);
	FREE_NULL_BUFFER(buffer);
}

extern void cred_state_init(void)
{
	if (!conf->cleanstart)
		_restore_cred_state();

	if (!cred_job_list)
		cred_job_list = list_create(xfree_ptr);
	if (!cred_state_list)
		cred_state_list = list_create(xfree_ptr);
}

extern void cred_state_fini(void)
{
	save_cred_state();
	FREE_NULL_LIST(cred_job_list);
	FREE_NULL_LIST(cred_state_list);
}

extern bool cred_jobid_cached(uint32_t jobid)
{
	bool retval = false;

	slurm_mutex_lock(&cred_cache_mutex);
	_clear_expired_job_states();
	retval = (_find_job_state(jobid) != NULL);
	slurm_mutex_unlock(&cred_cache_mutex);

	return retval;
}

extern int cred_insert_jobid(uint32_t jobid)
{
	slurm_mutex_lock(&cred_cache_mutex);
	_clear_expired_job_states();
	if (_find_job_state(jobid)) {
		debug2("%s: we already have a job state for job %u.",
		       __func__, jobid);
	} else {
		job_state_t *j = _job_state_create(jobid);
		list_append(cred_job_list, j);
	}
	slurm_mutex_unlock(&cred_cache_mutex);

	return SLURM_SUCCESS;
}

extern int cred_revoke(uint32_t jobid, time_t time, time_t start_time)
{
	job_state_t  *j = NULL;

	slurm_mutex_lock(&cred_cache_mutex);

	_clear_expired_job_states();

	if (!(j = _find_job_state(jobid))) {
		/*
		 * This node has not yet seen a job step for this job.
		 * Insert a job state object so that we can revoke any future
		 * credentials.
		 */
		j = _job_state_create(jobid);
		list_append(cred_job_list, j);
	}
	if (j->revoked) {
		if (start_time && (j->revoked < start_time)) {
			debug("job %u requeued, but started no tasks", jobid);
			j->expiration = (time_t) MAX_TIME;
		} else {
			slurm_seterrno(EEXIST);
			goto error;
		}
	}

	j->revoked = time;

	slurm_mutex_unlock(&cred_cache_mutex);
	return SLURM_SUCCESS;

error:
	slurm_mutex_unlock(&cred_cache_mutex);
	return SLURM_ERROR;
}

extern bool cred_revoked(slurm_cred_t *cred)
{
	job_state_t *j;
	bool rc = false;

	slurm_mutex_lock(&cred_cache_mutex);

	j = _find_job_state(cred->arg->step_id.job_id);

	if (j && (j->revoked != (time_t) 0) && (cred->ctime <= j->revoked))
		rc = true;

	slurm_mutex_unlock(&cred_cache_mutex);

	return rc;
}

extern int cred_begin_expiration(uint32_t jobid)
{
	job_state_t *j = NULL;

	slurm_mutex_lock(&cred_cache_mutex);

	_clear_expired_job_states();

	if (!(j = _find_job_state(jobid))) {
		slurm_seterrno(ESRCH);
		goto error;
	}

	if (j->expiration < (time_t) MAX_TIME) {
		slurm_seterrno(EEXIST);
		goto error;
	}

	j->expiration = time(NULL) + cred_expiration();
	debug2("set revoke expiration for jobid %u to %ld UTS",
	       j->jobid, j->expiration);
	slurm_mutex_unlock(&cred_cache_mutex);
	return SLURM_SUCCESS;

error:
	slurm_mutex_unlock(&cred_cache_mutex);
	return SLURM_ERROR;
}

extern void cred_handle_reissue(slurm_cred_t *cred, bool locked)
{
	job_state_t *j;

	if (!locked)
		slurm_mutex_lock(&cred_cache_mutex);

	j = _find_job_state(cred->arg->step_id.job_id);

	if (j && j->revoked && (cred->ctime > j->revoked)) {
		/* The credential has been reissued.  Purge the
		 * old record so that "cred" will look like a new
		 * credential to any ensuing commands. */
		info("reissued job credential for job %u", j->jobid);
		list_delete_ptr(cred_job_list, j);
	}

	if (!locked)
		slurm_mutex_unlock(&cred_cache_mutex);
}

static bool _credential_revoked(slurm_cred_t *cred)
{
	job_state_t *j = NULL;

	if (!(j = _find_job_state(cred->arg->step_id.job_id))) {
		j = _job_state_create(cred->arg->step_id.job_id);
		list_append(cred_job_list, j);
		return false;
	}

	if (cred->ctime <= j->revoked) {
		debug3("cred for %u revoked. expires at %ld UTS",
		       j->jobid, j->expiration);
		return true;
	}

	return false;
}

static int _list_find_cred_state(void *x, void *key)
{
	cred_state_t *s = x;
	slurm_cred_t *cred = key;

	if (!memcmp(&s->step_id, &cred->arg->step_id, sizeof(s->step_id)) &&
	    (s->ctime == cred->ctime))
		return 1;

	return 0;
}

static bool _credential_replayed(slurm_cred_t *cred)
{
	cred_state_t *s = NULL;

	s = list_find_first(cred_state_list, _list_find_cred_state, cred);

	/*
	 * If we found a match, this credential is being replayed.
	 */
	if (s)
		return true;

	/*
	 * Otherwise, save the credential state
	 */
	s = _cred_state_create(cred);
	list_append(cred_state_list, s);

	return false;
}

extern bool cred_cache_valid(slurm_cred_t *cred)
{
	slurm_mutex_lock(&cred_cache_mutex);

	_clear_expired_job_states();
	_clear_expired_credential_states();

	cred_handle_reissue(cred, true);

	if (_credential_revoked(cred)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_REVOKED);
		goto error;
	}

	if (_credential_replayed(cred)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_REPLAYED);
		goto error;
	}

	slurm_mutex_unlock(&cred_cache_mutex);
	return true;

error:
	slurm_mutex_unlock(&cred_cache_mutex);
	return false;
}
