/*****************************************************************************\
 *  commands.c - Slurm scrun commands handler
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

#include "config.h"

#include <unistd.h>

#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"

#include "scrun.h"

static data_for_each_cmd_t _foreach_load_annotation(const char *key,
						    data_t *data, void *arg)
{
	config_key_pair_t *pair = xmalloc(sizeof(*pair));

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING) {
		xfree(pair);
		return DATA_FOR_EACH_FAIL;
	}

	pair->name = xstrdup(key);
	pair->value = xstrdup(data_get_string(data));

	list_append(state.annotations, pair);

	return DATA_FOR_EACH_CONT;
}

static void _load_config()
{
	int rc;
	data_t *term, *ver, *rp, *annot, *config = NULL;
	buf_t *buf;

	xassert(!state.config_file);
	xstrfmtcat(state.config_file, "%s/config.json", state.bundle);

	if (!(buf = create_mmap_buf(state.config_file)))
		fatal("unable to load %s: %m", state.config_file);

	if ((rc = serialize_g_string_to_data(&config, get_buf_data(buf),
					     size_buf(buf), MIME_TYPE_JSON))) {
		fatal("unable to parse %s: %s",
		      state.config_file, slurm_strerror(rc));
	}

	FREE_NULL_BUFFER(buf);
	debug("%s: loaded container config: %s", __func__, state.config_file);
	xassert(!state.config);
	state.config = config;

	/* parse out key fields */
	xassert(!state.root_path);
	rp = data_resolve_dict_path(state.config, "/root/path/");
	if (data_get_type(rp) != DATA_TYPE_STRING)
		fatal("Invalid /root/path");

	/* need absolute path for root path */
	if (data_get_string(rp)[0] == '/')
		state.root_path = xstrdup(data_get_string(rp));
	else
		state.root_path = xstrdup_printf("%s/%s", state.bundle,
						 data_get_string(rp));
	state.orig_root_path = xstrdup(state.root_path);

	if ((annot = data_key_get(state.config, "annotations")) &&
	    (data_dict_for_each(annot, _foreach_load_annotation, NULL) < 0))
		fatal("Invalid /annotations");

	ver = data_resolve_dict_path(state.config, "/ociVersion/");
	if (data_get_type(ver) != DATA_TYPE_STRING)
		fatal("Invalid /ociVersion/ type %s",
		      data_type_to_string(data_get_type(ver)));
	xfree(state.oci_version);
	state.oci_version = xstrdup(data_get_string(ver));

	if ((term = data_resolve_dict_path(state.config,
					   "/process/terminal"))) {
		if (data_get_type(term) != DATA_TYPE_BOOL)
			fatal("Invalid /process/terminal type %s",
			      data_type_to_string(data_get_type(term)));
		state.requested_terminal = data_get_bool(term);
	} else {
		state.requested_terminal = false;
	}

}

static data_for_each_cmd_t _foreach_env(data_t *data, void *arg)
{
	int *i = arg;
	static const char *match_env[] = {
		"SCRUN_",
		"SLURM_",
	};

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		fatal("%s: expected string at /process/env[%d] in %s but found type %s",
		      __func__, *i, state.config_file,
		      data_type_to_string(data_get_type(data)));

	for (int j = 0; match_env[j]; j++) {
		if (xstrncmp(match_env[j], data_get_string(data),
			     strlen(match_env[j]))) {
			setenvfs("%s", data_get_string(data));
			break;
		}
	}

	(*i)++;

	return DATA_FOR_EACH_CONT;
}

static void _load_config_environ()
{
	int i = 0;

	data_t *denv = data_resolve_dict_path(state.config, "/process/env/");

	if (!denv)
		return;

	if (data_get_type(denv) != DATA_TYPE_LIST)
		fatal("%s: expected list at /process/env/ in %s but found type %s",
		      __func__, state.config_file,
		      data_type_to_string(data_get_type(denv)));

	(void) data_list_for_each(denv, _foreach_env, &i);
}

extern int command_create(void)
{
	xstrfmtcat(state.spool_dir, "%s/%s/", state.root_dir, state.id);

	if (mkdirpath(state.spool_dir, S_IRWXU, true)) {
		if (errno != EEXIST) {
			fatal("%s: unable to create spool directory %s: %s",
			      __func__, state.spool_dir, slurm_strerror(errno));
		} else {
			debug2("%s: spool directory %s already exists",
			       __func__, state.spool_dir);
		}
	} else {
		debug2("%s: created spool directory %s",
		      __func__, state.spool_dir);
	}

	_load_config();
	_load_config_environ();

	return spawn_anchor();
}

extern int command_version(void)
{
	printf("scrun version %s\nspec: %s\n",
	       SLURM_VERSION_STRING, OCI_VERSION);
	return SLURM_SUCCESS;
}

extern int command_start(void)
{
	slurm_msg_t req, *resp = NULL;
	int rc;
	slurm_step_id_t step = {0};

	get_anchor_state();
	check_state();

	debug("%s: processing %s in state:%s",
	      __func__, state.id,
	      slurm_container_status_to_str(state.status));

	slurm_msg_t_init(&req);
	slurm_msg_set_r_uid(&req, SLURM_AUTH_UID_ANY);
	req.msg_type = REQUEST_CONTAINER_START;

	if ((rc = send_rpc(&req, &resp, state.id, NULL)))
	    fatal("%s: send_rpc() failed: %s",
		  __func__, slurm_strerror(rc));

	slurm_free_msg_members(&req);

	if (resp && resp->data &&
	    (resp->msg_type == RESPONSE_CONTAINER_START)) {
		container_started_msg_t *st_msg = resp->data;

		rc = st_msg->rc;
		step = st_msg->step;
	} else {
		fatal("%s: unexpected RPC=%u response",
		      __func__, resp->msg_type);
	}

	if (!rc) {
		debug("%s: container %s start requested JobId=%u StepId=%u",
		      __func__, state.id, step.job_id, step.step_id);
	} else if (rc == ESLURM_CAN_NOT_START_IMMEDIATELY) {
		slurm_free_msg(resp);
		resp = NULL;
	} else {
		error("%s: container %s start JobId=%u StepId=%u failed: %s",
		      __func__, state.id, step.job_id, step.step_id,
		      slurm_strerror(rc));
	}

#ifdef MEMORY_LEAK_DEBUG
	slurm_free_msg(resp);
#endif /* MEMORY_LEAK_DEBUG */

	return rc;
}

static int _foreach_state_annotation(void *x, void *arg)
{
	config_key_pair_t *key_pair_ptr = x;
	data_t *annot = arg;

	data_set_string(data_key_set(annot, key_pair_ptr->name),
			key_pair_ptr->value);

	return SLURM_SUCCESS;
}

extern int command_state(void)
{
	int rc = SLURM_SUCCESS;
	char *str = NULL, *status_str;
	container_state_msg_status_t status;
	data_t *o = data_set_dict(data_new());

	debug("%s: processing for %s", __func__, state.id);

	get_anchor_state();

	debug("%s: got container:%s state:%s",
	      __func__, state.id, slurm_container_status_to_str(state.status));

	status = state.status;

	/*
	 * Translate internal status to a OCI compliant status:
	 *  https://github.com/opencontainers/runtime-spec/blame/main/runtime.md#L19
	 */
	switch (status) {
	case CONTAINER_ST_INVALID :
	case CONTAINER_ST_MAX :
		fatal("%s: status %d should never happen", __func__, status);
	case CONTAINER_ST_STARTING :
		status = CONTAINER_ST_CREATING;
		break;
	case CONTAINER_ST_CREATING :
	case CONTAINER_ST_CREATED :
	case CONTAINER_ST_RUNNING :
		/* no need to override */
		break;
	case CONTAINER_ST_STOPPING :
	case CONTAINER_ST_STOPPED :
	case CONTAINER_ST_UNKNOWN :
		status = CONTAINER_ST_STOPPED;
		break;
	}
	if (status >= CONTAINER_ST_STOPPED)
		status = CONTAINER_ST_STOPPED;

	/* callers may be case-sensitive */
	status_str = xstrdup(slurm_container_status_to_str(status));
	xstrtolower(status_str);

	data_set_string(data_key_set(o, "ociVersion"), state.oci_version);
	data_set_string(data_key_set(o, "id"), state.id);
	data_set_string_own(data_key_set(o, "status"), status_str);

	data_set_int(data_key_set(o, "pid"), state.pid);
	data_set_string(data_key_set(o, "bundle"), state.bundle);

	list_for_each_ro(state.annotations, _foreach_state_annotation,
			 data_set_dict(data_key_set(o, "annotations")));

	if ((rc = serialize_g_data_to_string(&str, NULL, o, MIME_TYPE_JSON,
					     SER_FLAGS_PRETTY)))
		fatal("unable to serialise: %s", slurm_strerror(rc));

	printf("%s\n", str);

	xfree(str);

	debug("%s: state with anchor status=%s and reported status=%s complete: %s",
	      __func__, slurm_container_status_to_str(status),
	      slurm_container_status_to_str(status), slurm_strerror(rc));
	return rc;
}

extern int command_kill(void)
{
	slurm_msg_t req, *resp = NULL;
	container_signal_msg_t sig_msg;
	int rc, signal = state.requested_signal;

	debug("%s: processing %s", __func__, state.id);

	get_anchor_state();

	if (state.status >= CONTAINER_ST_STOPPED) {
		debug("%s: container:%s already stopped (state:%s)",
		      __func__, state.id,
		      slurm_container_status_to_str(state.status));
		return SLURM_SUCCESS;
	}

	debug("%s: got container:%s state:%s",
	      __func__, state.id, slurm_container_status_to_str(state.status));

	slurm_msg_t_init(&req);
	slurm_msg_set_r_uid(&req, SLURM_AUTH_UID_ANY);
	req.msg_type = REQUEST_CONTAINER_KILL;
	sig_msg.signal = signal;
	req.data = &sig_msg;

	debug("%s: requesting signal %s be sent to %s",
	      __func__, strsignal(signal), state.id);
	if ((rc = send_rpc(&req, &resp, state.id, NULL))) {
		if (state.jobid) {
			debug("%s: unable to connect to anchor to signal %s container %s directly: %s",
			      __func__, strsignal(signal), state.id,
			      slurm_strerror(rc));

			rc = slurm_signal_job(state.jobid, signal);
			if ((rc == SLURM_ERROR) && errno)
				rc = errno;

			if (rc == ESLURM_ALREADY_DONE) {
				info("%s: JobId=%u with container %s already complete",
				     __func__, state.jobid, state.id);
				rc = SLURM_SUCCESS;
			} else if (rc) {
				error("%s: unable to signal %s container %s or signal JobId=%u: %m",
				      __func__, strsignal(signal), state.id,
				      state.jobid);
			} else {
				info("%s: JobId=%u running container %s has been sent signal %s",
				     __func__, state.jobid, state.id,
				     strsignal(signal));
				rc = SLURM_SUCCESS;
			}
		} else {
			/* assume job has already ran and been purged */
			info("%s: container %s assumed already complete",
			     __func__, state.id);
			rc = SLURM_SUCCESS;
		}
	} else if (resp && resp->data) {
		if (resp->msg_type == RESPONSE_CONTAINER_KILL) {
			return_code_msg_t *rc_msg = resp->data;

			if ((rc = rc_msg->return_code))
				error("%s: unable to signal container %s: %s",
				      __func__, state.id,
				      slurm_strerror(rc_msg->return_code));
			else
				info("%s: successfully sent signal %s to container %s",
				     __func__, strsignal(signal), state.id);
		} else {
			error("%s: unexpected response RPC#%u",
			      __func__, resp->msg_type);
		}
	}

#ifdef MEMORY_LEAK_DEBUG
	req.data = NULL; /* on stack */
	slurm_free_msg_members(&req);
	slurm_free_msg(resp);
#endif /* MEMORY_LEAK_DEBUG */

	debug("%s: kill complete: %s", __func__, slurm_strerror(rc));
	return rc;
}

extern int command_delete(void)
{
	int rc;
	slurm_msg_t req, *resp = NULL;
	container_delete_msg_t delete_msg;

	debug("%s: processing %s", __func__, state.id);

	get_anchor_state();

	if (state.status >= CONTAINER_ST_STOPPED) {
		/*
		 * containers will auto cleanup and delete themselves so we can
		 * just ignore this request
		 */
		debug("container %s already stopped", state.id);
		return SLURM_SUCCESS;
	}

	debug("sending delete RPC %s", state.id);

	slurm_msg_t_init(&req);
	slurm_msg_set_r_uid(&req, SLURM_AUTH_UID_ANY);
	req.msg_type = REQUEST_CONTAINER_DELETE;
	delete_msg.force = state.force;
	req.data = &delete_msg;

	if ((rc = send_rpc(&req, &resp, state.id, NULL))) {
		int signal = SIGTERM;

		if (state.jobid) {
			debug("%s: unable to connect to anchor to delete container %s directly: %s",
			      __func__, state.id, slurm_strerror(rc));

			if (slurm_signal_job(state.jobid, signal)) {
				rc = errno;
				error("%s: unable to signal %s container %s or signal JobId=%u: %m",
				      __func__, strsignal(signal), state.id,
				      state.jobid);
			} else {
				info("%s: JobId=%u running contianer %s has been sent signal %s",
				     __func__, state.jobid, state.id,
				     strsignal(signal));
				rc = SLURM_SUCCESS;
			}
		} else {
			if (state.force) {
				/* assume job has already ran and been purged */
				info("%s: container %s assumed already deleted",
				     __func__, state.id);
				rc = SLURM_SUCCESS;
			} else {
				/* no known job: nothing else we can do here */
				error("%s: unable to delete container %s: %s",
				      __func__, state.id, slurm_strerror(rc));
				rc = ESLURM_INVALID_JOB_ID;
			}
		}
	}

	if (!rc && resp && resp->data &&
	    (resp->msg_type == RESPONSE_CONTAINER_DELETE)) {
		return_code_msg_t *rc_msg = resp->data;

		if (rc_msg->return_code) {
			error("%s: unable to delete container %s: %s",
			      __func__, state.id,
			      slurm_strerror(rc_msg->return_code));
		} else {
			debug("%s: delete container %s successful",
			      __func__, state.id);
		}

		rc = rc_msg->return_code;
	}

#ifdef MEMORY_LEAK_DEBUG
	req.data = NULL; /* on stack */
	slurm_free_msg_members(&req);
	slurm_free_msg(resp);
#endif /* MEMORY_LEAK_DEBUG */

	debug("%s: delete complete: %s", __func__, slurm_strerror(rc));
	return rc;
}
