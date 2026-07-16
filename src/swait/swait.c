/*****************************************************************************\
 *  swait.c - Block until all regular steps of a stepmgr-enabled job
 *	      have ended.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/sluid.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/conn.h"

#include "src/swait/opt.h"

#define SWAIT_CONMGR_THREADS 3

/*
 * exit_lock arbitrates between _on_msg (steps-drained: keep exit_rc=0) and
 * _timeout_fire (timeout: exit_rc=1). exit_decided is set by whichever
 * callback wins the race; the loser becomes a no-op.
 */
static pthread_mutex_t exit_lock = PTHREAD_MUTEX_INITIALIZER;
static bool exit_decided;
static int exit_rc;

static char *my_cert;
static char *my_host;
static uint16_t my_port;
static char *stepmgr_node;

/*
 * query slurmctld for job's stepmgr node
 *
 * IN/OUT target - step_id selecting the job; job_id is overwritten
 *                 with the per-task id for array-task input.
 * RET xmalloc()'d hostname string (caller xfree()s)
 */
static char *_resolve_stepmgr_via_ctld(slurm_step_id_t *target)
{
	job_info_msg_t *resp = NULL;
	slurm_job_info_t *info = NULL;
	char *host = NULL;
	bool is_array = false;

	if (slurm_load_job(&resp, *target, SHOW_ALL) != SLURM_SUCCESS) {
		error("cannot load %pI: %s", target, slurm_strerror(errno));
		slurm_free_job_info_msg(resp);
		exit(2);
	}
	if (!resp || (resp->record_count < 1) || !resp->job_array) {
		error("cannot load %pI: empty controller response", target);
		slurm_free_job_info_msg(resp);
		exit(2);
	}
	for (uint32_t i = 0; i < resp->record_count; i++) {
		if (resp->job_array[i].array_task_id != NO_VAL)
			is_array = true;
		if ((opt.array_task_id != NO_VAL) &&
		    (resp->job_array[i].array_task_id == opt.array_task_id)) {
			info = &resp->job_array[i];
			break;
		}
	}

	if (opt.array_task_id != NO_VAL) {
		if (!info) {
			if (is_array)
				error("array task %u_%u: not found",
				      opt.array_job_id, opt.array_task_id);
			else
				error("%pI: not an array job", target);
			slurm_free_job_info_msg(resp);
			exit(2);
		}
		target->job_id = info->job_id;
	} else if (is_array && !target->sluid) {
		if (resp->record_count > 1) {
			error("%pI is an array job; pass a specific task offset (jobid_task)",
			      target);
			slurm_free_job_info_msg(resp);
			exit(2);
		}
		info = &resp->job_array[0];
		opt.array_job_id = info->array_job_id;
		opt.array_task_id = info->array_task_id;
		target->job_id = info->job_id;
	} else {
		info = &resp->job_array[0];
		target->job_id = info->job_id;
	}
	if (!(info->bitflags & STEPMGR_ENABLED)) {
		error("%pI does not have stepmgr enabled", target);
		slurm_free_job_info_msg(resp);
		exit(2);
	}
	if (!info->batch_host || !*info->batch_host) {
		if (IS_JOB_PENDING(info))
			error("%pI is still pending; swait does not wait for jobs to start",
			      target);
		else
			error("%pI: stepmgr host is unknown (controller bug?)",
			      target);
		slurm_free_job_info_msg(resp);
		exit(2);
	}
	host = xstrdup(info->batch_host);
	verbose("resolved %pI via controller: stepmgr=%s, JobId=%u",
		target, host, info->job_id);
	slurm_free_job_info_msg(resp);
	return host;
}

/*
 * Resolve an array-task's stepmgr from env vars when SLURM_ARRAY_* match.
 *
 * IN/OUT target - target->job_id is overwritten with the per-task job_id.
 * RET xmalloc()'d hostname string, or NULL on any mismatch / missing var.
 */
static char *_array_task_env_stepmgr(slurm_step_id_t *target)
{
	const char *array_jid_env = getenv("SLURM_ARRAY_JOB_ID");
	const char *array_tid_env = getenv("SLURM_ARRAY_TASK_ID");
	const char *jid_env = getenv("SLURM_JOB_ID");
	const char *stepmgr = getenv("SLURM_STEPMGR");
	unsigned long ajid = 0, atid = 0, jid = 0;
	char *end = NULL;

	if (!array_jid_env || !*array_jid_env || !array_tid_env ||
	    !*array_tid_env || !jid_env || !*jid_env || !stepmgr || !*stepmgr)
		return NULL;

	/* Check if SLURM_ARRAY_JOB_ID matches */
	errno = 0;
	ajid = strtoul(array_jid_env, &end, 10);
	if (errno || *end || (ajid != opt.array_job_id))
		return NULL;

	/* Check if SLURM_ARRAY_TASK_ID matches */
	errno = 0;
	atid = strtoul(array_tid_env, &end, 10);
	if (errno || *end || (atid != opt.array_task_id))
		return NULL;

	/* Check if SLURM_JOB_ID matches */
	errno = 0;
	jid = strtoul(jid_env, &end, 10);
	if (errno || *end || !jid || (jid >= MAX_VAL))
		return NULL;

	target->job_id = (uint32_t) jid;
	verbose("resolved array task %u_%u via env: stepmgr=%s, JobId=%lu",
		opt.array_job_id, opt.array_task_id, stepmgr, jid);
	return xstrdup(stepmgr);
}

/*
 * Resolve the job stepmgr
 *
 * IN/OUT target - step_id selecting the job; job_id is overwritten
 *                 with the per-task id for array-task input.
 * RET xmalloc()'d hostname string; caller must xfree() it.
 */
static char *_resolve_stepmgr(slurm_step_id_t *target)
{
	const char *stepmgr;

	/*
	 * The stepmgr node that we want to talk to might not be the one in
	 * SLURM_STEPMGR. Double check that the target job matches what the
	 * environment says before trusting SLURM_STEPMGR env var. If there's a
	 * mismatch, resolve stepmgr via ctld.
	 */

	/* Check if SLURM_ARRAY_TASK_ID/SLURM_ARRAY_TASK_JOB_ID match target */
	if (opt.array_task_id != NO_VAL) {
		char *host = _array_task_env_stepmgr(target);
		if (host)
			return host;
		goto resolve_via_ctld;
	}

	/* Check if SLURM_JOB_SLUID matches target */
	if (target->sluid) {
		const char *sluid_env = getenv("SLURM_JOB_SLUID");
		if (!sluid_env || !*sluid_env ||
		    (str2sluid(sluid_env) != target->sluid))
			goto resolve_via_ctld;
		stepmgr = getenv("SLURM_STEPMGR");
		if (!stepmgr || !*stepmgr)
			goto resolve_via_ctld;
		verbose("resolved stepmgr via SLURM_STEPMGR env (SLUID match): %s",
			stepmgr);
		return xstrdup(stepmgr);
	}

	/* Check if SLURM_JOB_ID matches target */
	if (target->job_id != NO_VAL) {
		const char *jobid_env = getenv("SLURM_JOB_ID");
		unsigned long parsed;
		char *end = NULL;

		if (!jobid_env || !*jobid_env)
			goto resolve_via_ctld;
		errno = 0;
		parsed = strtoul(jobid_env, &end, 10);
		if (errno || *end || !parsed || (parsed >= MAX_VAL) ||
		    (parsed != target->job_id))
			goto resolve_via_ctld;
		stepmgr = getenv("SLURM_STEPMGR");
		if (!stepmgr || !*stepmgr)
			goto resolve_via_ctld;
		verbose("resolved stepmgr via SLURM_STEPMGR env (JobId match): %s",
			stepmgr);
		return xstrdup(stepmgr);
	}

resolve_via_ctld:
	debug("falling back to controller for stepmgr resolution");
	return _resolve_stepmgr_via_ctld(target);
}

/*
 * IN  node - hostname of the stepmgr stepd
 * OUT addr - filled with the resolved address on success
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
static int _resolve_stepmgr_addr(const char *node, slurm_addr_t *addr)
{
	slurm_node_alias_addrs_t *alias_addrs = NULL;

	if (slurm_conf_get_addr(node, addr, 0) == SLURM_SUCCESS)
		return SLURM_SUCCESS;

	if (slurm_get_node_alias_addrs((char *) node, &alias_addrs) ||
	    !alias_addrs)
		return SLURM_ERROR;

	add_remote_nodes_to_conf_tbls(alias_addrs->node_list,
				      alias_addrs->node_addrs);
	slurm_free_node_alias_addrs(alias_addrs);

	return slurm_conf_get_addr(node, addr, 0);
}

/*
 * conmgr on-message callback: authenticate, dispatch on msg_type, free msg.
 * On SRUN_STEPS_DRAINED, sets exit_decided and requests conmgr shutdown.
 * IN args      - conmgr callback args
 * IN msg       - unpacked message; freed before return
 * IN unpack_rc - non-zero if message unpack failed
 * IN arg       - unused
 * RET SLURM_SUCCESS, SLURM_PROTOCOL_AUTHENTICATION_ERROR, or unpack_rc
 */
static int _on_msg(conmgr_callback_args_t args, slurm_msg_t *msg, int unpack_rc,
		   void *arg)
{
	int rc = SLURM_SUCCESS;

	if (unpack_rc) {
		if (unpack_rc == SLURM_PROTOCOL_AUTHENTICATION_ERROR)
			debug("swait: dropping unauthenticated RPC");
		else
			debug("swait: dropping malformed RPC: %s",
			      slurm_strerror(unpack_rc));
		rc = unpack_rc;
		goto out;
	}

	if (!msg->auth_ids_set ||
	    ((msg->auth_uid != slurm_conf.slurmd_user_id) &&
	     (msg->auth_uid != slurm_conf.slurm_user_id))) {
		debug("swait: dropping %s from uid %u",
		      rpc_num2string(msg->msg_type),
		      msg->auth_ids_set ? msg->auth_uid : (uid_t) -1);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto out;
	}

	switch (msg->msg_type) {
	case SRUN_STEPS_DRAINED:
		verbose("received SRUN_STEPS_DRAINED; shutting down");
		slurm_mutex_lock(&exit_lock);
		exit_decided = true;
		slurm_mutex_unlock(&exit_lock);
		conmgr_request_shutdown();
		break;
	default:
		debug("swait: unexpected msg type %d/%s",
		      msg->msg_type, rpc_num2string(msg->msg_type));
		break;
	}

out:
	slurm_free_msg(msg);
	return rc;
}

/*
 * One-shot --timeout deadline: sets exit_rc=1 and requests conmgr shutdown.
 * IN args - conmgr callback args
 * IN arg  - unused
 */
static void _timeout_fire(conmgr_callback_args_t args, void *arg)
{
	if (args.status == CONMGR_WORK_STATUS_CANCELLED)
		return;

	slurm_mutex_lock(&exit_lock);
	if (exit_decided) {
		slurm_mutex_unlock(&exit_lock);
		return;
	}
	exit_decided = true;
	exit_rc = 1;
	slurm_mutex_unlock(&exit_lock);

	error("timed out after %u seconds", opt.timeout);
	conmgr_request_shutdown();
}

static void *_on_connection(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(NET, "%s: [%s] new connection",
		 __func__, conmgr_con_get_name(conmgr_args.ref));

	return conmgr_args.con;
}

/*
 * Send REQUEST_STEPS_DRAINED_SUBSCRIBE to the stepmgr at node.
 * IN node - hostname of the stepmgr stepd
 * IN host - listener hostname swait advertised
 * IN port - listener port swait advertised
 * IN cert - swait's TLS certificate, or NULL when TLS is off
 * RET SLURM_SUCCESS, ESLURM_STEPS_DRAINED (no blocking steps; fast-return),
 *     EAGAIN (subscriber slots full), or another error code on failure
 */
static int _send_subscribe(const char *node, const char *host, uint16_t port,
			   const char *cert)
{
	slurm_msg_t req_msg, resp_msg;
	steps_drained_sub_msg_t data;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	slurm_msg_set_r_uid(&req_msg, slurm_conf.slurmd_user_id);

	if (_resolve_stepmgr_addr(node, &req_msg.address)) {
		rc = SLURM_ERROR;
		goto cleanup;
	}

	data = (steps_drained_sub_msg_t) {
		.host = (char *) host,
		.port = port,
		.step_id = opt.target,
		.tls_cert = (char *) cert,
	};
	req_msg.msg_type = REQUEST_STEPS_DRAINED_SUBSCRIBE;
	req_msg.data = &data;

	debug("sending REQUEST_STEPS_DRAINED_SUBSCRIBE to %s; listener=%s:%u",
	      node, host, port);

	if (slurm_send_recv_node_msg(&req_msg, &resp_msg, 0)) {
		rc = SLURM_ERROR;
		goto cleanup;
	}

	if (resp_msg.msg_type == RESPONSE_SLURM_RC)
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
	else
		rc = SLURM_UNEXPECTED_MSG_ERROR;

cleanup:
	slurm_free_msg_members(&resp_msg);
	return rc;
}

/*
 * Set up listener to receive SRUN_STEPS_DRAINED from stepmgr
 *
 * RET SLURM_SUCCESS on success, ESLURM_STEPS_DRAINED if the job has no
 *	running or pending steps (nothing to wait for), or other non-zero
 *	error.
 */
static int _setup_steps_drained_listener(void)
{
	static const conmgr_events_t swait_events = {
		.on_connection = _on_connection,
		.on_msg = _on_msg,
	};
	conmgr_con_flags_t flags = CON_FLAG_NONE;
	char *host = NULL;
	char *cert = NULL;
	uint16_t port = 0;
	int fd = -1;
	int rc = SLURM_ERROR;
	char hostname[HOST_NAME_MAX + 1];

	/* Create listening socket that stepmgr can talk to */
	if (gethostname(hostname, sizeof(hostname))) {
		error("swait: gethostname failed: %m");
		return errno ? errno : SLURM_ERROR;
	}
	host = xstrdup(hostname);
	if ((rc = slurm_init_msg_engine_srun_ports(&fd, &port))) {
		error("swait: unable to open listen socket: %m");
		goto fail;
	}
	verbose("listening on %s:%u", host, port);

	if (conn_tls_enabled()) {
		/* stepmgr will need TLS certificate to trust our "server" */
		if (!(cert = conn_g_get_own_public_cert())) {
			error("swait: conn_g_get_own_public_cert failed");
			rc = SLURM_ERROR;
			goto fail;
		}
		flags |= CON_FLAG_TLS_FINGERPRINT;
	}

	/* Send stepmgr listening socket info so it can talk back to us */
	if ((rc = _send_subscribe(stepmgr_node, host, port, cert))) {
		if (rc != ESLURM_STEPS_DRAINED) {
			error("swait: subscribe RPC failed: %s",
			      slurm_strerror(rc));
		}
		goto fail;
	}
	verbose("subscribed to stepmgr %s", stepmgr_node);

	my_host = host;
	my_cert = cert;
	my_port = port;

	/* Start waiting for connections from stepmgr on the listening socket */
	if ((rc = conmgr_process_fd_listen(fd, CON_TYPE_RPC, NULL,
					   &swait_events, flags, NULL))) {
		error("swait: conmgr_process_fd_listen failed: %s",
		      slurm_strerror(rc));
		my_host = NULL;
		my_cert = NULL;
		my_port = 0;
		goto fail;
	}
	/* conmgr owns fd now */
	fd = -1;
	return SLURM_SUCCESS;

fail:
	if (fd >= 0)
		(void) close(fd);
	xfree(host);
	xfree(cert);
	return rc;
}

/*
 * swait entry point. RET 0 on success, 1 on error.
 */
int main(int argc, char **argv)
{
	log_options_t log_opts = LOG_OPTS_STDERR_ONLY;
	int setup_rc;

	log_init("swait", log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	slurm_init(NULL);

	parse_command_line(argc, argv);

	if (opt.verbose || opt.quiet) {
		log_opts.stderr_level += opt.verbose;
		log_opts.stderr_level -= opt.quiet;
		log_alter(log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	}

	stepmgr_node = _resolve_stepmgr(&opt.target);

	conmgr_init(0, SWAIT_CONMGR_THREADS, 0);

	setup_rc = _setup_steps_drained_listener();
	if (setup_rc == ESLURM_STEPS_DRAINED) {
		verbose("steps already drained; exiting without waiting");
	} else if (setup_rc == EAGAIN) {
		error("stepmgr %s subscriber slots full; try again later",
		      stepmgr_node);
		exit_rc = 2;
	} else if (setup_rc) {
		error("subscribe to stepmgr %s failed: %s",
		      stepmgr_node, slurm_strerror(setup_rc));
		exit_rc = 2;
	} else {
		if (opt.timeout > 0) {
			verbose("waiting for steps to drain (timeout %us)",
				opt.timeout);
			conmgr_add_work_delayed_fifo(_timeout_fire, NULL,
						     opt.timeout, 0);
		} else {
			verbose("waiting for steps to drain (no timeout)");
		}
		conmgr_run(true);
	}

	verbose("exiting rc=%d", exit_rc);
	conmgr_fini();

#ifdef MEMORY_LEAK_DEBUG
	xfree(stepmgr_node);
	xfree(my_host);
	xfree(my_cert);
	slurm_fini();
#endif /* MEMORY_LEAK_DEBUG */

	return exit_rc;
}
