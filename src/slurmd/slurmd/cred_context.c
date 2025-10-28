/*****************************************************************************\
 *  cred_context.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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
	time_t revoked;		/* Time at which credentials were revoked   */
	slurm_step_id_t step_id;
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

static job_state_t *_job_state_create(slurm_step_id_t *step_id)
{
	job_state_t *j = xmalloc(sizeof(*j));

	j->step_id = *step_id;
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
	slurm_step_id_t *step_id = key;

	/*
	 * Backwards compatibility hack: only check that the SLUID matches
	 * if both the cred_cache and the lookup have it set.
	 */
	if (j->step_id.sluid && step_id->sluid)
		return (j->step_id.sluid == step_id->sluid);

	if (j->step_id.job_id == step_id->job_id)
		return 1;
	return 0;
}

static job_state_t *_find_job_state(slurm_step_id_t *step_id)
{
	return list_find_first(cred_job_list, _list_find_job_state,
			       step_id);
}

static void _clear_expired_job_states(void)
{
	time_t now;

	if (!cred_job_list) {
		warning("No cred_job_list, unable to clear expired job states");
		return;
	}

	now = time(NULL);
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
	time_t now;

	if (!cred_state_list) {
		warning("No cred_state_list, unable to clear expired credential states");
		return;
	}

	now = time(NULL);
	list_delete_all(cred_state_list, _list_find_expired_cred_state, &now);
}

static void _job_state_pack(void *x, uint16_t protocol_version, buf_t *buffer)
{
	job_state_t *j = x;

	pack_step_id(&j->step_id, buffer, protocol_version);
	pack_time(j->revoked, buffer);
	pack_time(j->ctime, buffer);
	pack_time(j->expiration, buffer);
}

static int _job_state_unpack(void **out, uint16_t protocol_version,
			     buf_t *buffer)
{
	job_state_t *j = xmalloc(sizeof(*j));

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&j->step_id, buffer,
					   protocol_version))
			goto unpack_error;
		safe_unpack_time(&j->revoked, buffer);
		safe_unpack_time(&j->ctime, buffer);
		safe_unpack_time(&j->expiration, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&j->step_id.job_id, buffer);
		safe_unpack_time(&j->revoked, buffer);
		safe_unpack_time(&j->ctime, buffer);
		safe_unpack_time(&j->expiration, buffer);
	}

	debug3("cred_unpack: %pI ctime:%ld revoked:%ld expires:%ld",
	       &j->step_id, j->ctime, j->revoked, j->expiration);

	if ((j->revoked) && (j->expiration == (time_t) MAX_TIME)) {
		warning("revoke on %pI has no expiration", &j->step_id);
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

	pack_step_id(&s->step_id, buffer, protocol_version);
	pack_time(s->ctime, buffer);
	pack_time(s->expiration, buffer);
}

static int _cred_state_unpack(void **out, uint16_t protocol_version,
			      buf_t *buffer)
{
	cred_state_t *s = xmalloc(sizeof(*s));

	if (unpack_step_id_members(&s->step_id, buffer, protocol_version))
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
	uint16_t version = SLURM_PROTOCOL_VERSION;

	pack16(version, buffer);

	slurm_mutex_lock(&cred_cache_mutex);
	slurm_pack_list(cred_job_list, _job_state_pack, buffer, version);
	slurm_pack_list(cred_state_list, _cred_state_pack, buffer, version);
	slurm_mutex_unlock(&cred_cache_mutex);
}

static void _cred_context_unpack(buf_t *buffer)
{
	uint16_t version = 0;

	slurm_mutex_lock(&cred_cache_mutex);

	safe_unpack16(&version, buffer);

	FREE_NULL_LIST(cred_job_list);
	if (slurm_unpack_list(&cred_job_list, _job_state_unpack,
			      xfree_ptr, buffer, version))
		goto unpack_error;
	_clear_expired_job_states();

	FREE_NULL_LIST(cred_state_list);
	if (slurm_unpack_list(&cred_state_list, _cred_state_unpack,
			      xfree_ptr, buffer, version))
		goto unpack_error;
	_clear_expired_credential_states();
	slurm_mutex_unlock(&cred_cache_mutex);
	return;

unpack_error:
	warning("%s: failed to restore job state from file", __func__);
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

extern bool cred_job_cached(slurm_step_id_t *step_id)
{
	bool retval = false;

	slurm_mutex_lock(&cred_cache_mutex);
	_clear_expired_job_states();
	retval = (_find_job_state(step_id) != NULL);
	slurm_mutex_unlock(&cred_cache_mutex);

	return retval;
}

extern int cred_insert_job(slurm_step_id_t *step_id)
{
	slurm_mutex_lock(&cred_cache_mutex);
	_clear_expired_job_states();
	if (_find_job_state(step_id)) {
		debug2("%s: we already have a job state for %pI",
		       __func__, step_id);
	} else {
		job_state_t *j = _job_state_create(step_id);
		list_append(cred_job_list, j);
	}
	slurm_mutex_unlock(&cred_cache_mutex);

	return SLURM_SUCCESS;
}

extern int cred_revoke(slurm_step_id_t *step_id, time_t time, time_t start_time)
{
	job_state_t  *j = NULL;

	slurm_mutex_lock(&cred_cache_mutex);

	_clear_expired_job_states();

	if (!(j = _find_job_state(step_id))) {
		/*
		 * This node has not yet seen a job step for this job.
		 * Insert a job state object so that we can revoke any future
		 * credentials.
		 */
		j = _job_state_create(step_id);
		list_append(cred_job_list, j);
	}
	if (j->revoked) {
		if (start_time && (j->revoked < start_time)) {
			debug("%pI requeued, but started no tasks", step_id);
			j->expiration = (time_t) MAX_TIME;
		} else {
			errno = EEXIST;
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

	j = _find_job_state(&cred->arg->step_id);

	if (j && (j->revoked != (time_t) 0) && (cred->ctime <= j->revoked))
		rc = true;

	slurm_mutex_unlock(&cred_cache_mutex);

	return rc;
}

extern int cred_begin_expiration(slurm_step_id_t *step_id)
{
	job_state_t *j = NULL;

	slurm_mutex_lock(&cred_cache_mutex);

	_clear_expired_job_states();

	if (!(j = _find_job_state(step_id))) {
		errno = ESRCH;
		goto error;
	}

	if (j->expiration < (time_t) MAX_TIME) {
		errno = EEXIST;
		goto error;
	}

	j->expiration = time(NULL) + cred_expiration();
	debug2("set revoke expiration for %pI to %ld UTS",
	       &j->step_id, j->expiration);
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

	j = _find_job_state(&cred->arg->step_id);

	if (j && j->revoked && (cred->ctime > j->revoked)) {
		/* The credential has been reissued.  Purge the
		 * old record so that "cred" will look like a new
		 * credential to any ensuing commands. */
		info("reissued job credential for %pI", &j->step_id);
		list_delete_ptr(cred_job_list, j);
	}

	if (!locked)
		slurm_mutex_unlock(&cred_cache_mutex);
}

static bool _credential_revoked(slurm_cred_t *cred)
{
	job_state_t *j = NULL;

	if (!(j = _find_job_state(&cred->arg->step_id))) {
		j = _job_state_create(&cred->arg->step_id);
		list_append(cred_job_list, j);
		return false;
	}

	if (cred->ctime <= j->revoked) {
		debug3("cred for %pI revoked. expires at %ld UTS",
		       &j->step_id, j->expiration);
		return true;
	}

	return false;
}

static int _list_find_cred_state(void *x, void *key)
{
	cred_state_t *s = x;
	slurm_cred_t *cred = key;

	if (s->ctime != cred->ctime)
		return 0;

	/* If the SLUID is set on both, then reject if they're not equal. */
	if (s->step_id.sluid && cred->arg->step_id.sluid &&
	    (s->step_id.sluid != cred->arg->step_id.sluid))
		return 0;

	return verify_step_id(&s->step_id, &cred->arg->step_id);
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
		errno = ESLURMD_CREDENTIAL_REVOKED;
		goto error;
	}

	if (_credential_replayed(cred)) {
		errno = ESLURMD_CREDENTIAL_REPLAYED;
		goto error;
	}

	slurm_mutex_unlock(&cred_cache_mutex);
	return true;

error:
	slurm_mutex_unlock(&cred_cache_mutex);
	return false;
}
