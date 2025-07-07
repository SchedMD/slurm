/*****************************************************************************\
 *  certmgr.c - certmgr API definitions
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

#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/state_save.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/conmgr/conmgr.h"

#include "src/interfaces/certmgr.h"
#include "src/interfaces/conn.h"

typedef struct {
	char *(*get_node_cert_key)(char *node_name);
	char *(*get_node_token)(char *node_name);
	char *(*generate_csr)(char *node_name);
	char *(*sign_csr)(char *csr, bool is_client_auth, char *token,
			  char *name);
} certmgr_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for certmgr_ops_t.
 */
static const char *syms[] = {
	"certmgr_p_get_node_cert_key",
	"certmgr_p_get_node_token",
	"certmgr_p_generate_csr",
	"certmgr_p_sign_csr",
};

static certmgr_ops_t ops;
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

static char *conf_spooldir = NULL;

extern bool certmgr_enabled(void)
{
	return (plugin_inited == PLUGIN_INITED);
}

extern int certmgr_get_renewal_period_mins(void)
{
	static int renewal_period = -1;
	char *renewal_str = NULL;

	if (renewal_period > 0)
		return renewal_period;

	if ((renewal_str = conf_get_opt_str(slurm_conf.certmgr_params,
					    "certificate_renewal_period="))) {
		int i = atoi(renewal_str);
		if (i < 0) {
			error("Invalid certificate_renewal_period: %s. Needs to be positive integer",
			      renewal_str);
			xfree(renewal_str);
			return SLURM_ERROR;
		}

		renewal_period = i;
		xfree(renewal_str);
		return renewal_period;
	} else {
		/* default setting */
		renewal_period = DAY_MINUTES;
	}

	return renewal_period;
}

extern int certmgr_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "certmgr";

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	if (!slurm_conf.certmgr_type) {
		plugin_inited = PLUGIN_NOOP;
		goto done;
	}

	g_context = plugin_context_create(plugin_type, slurm_conf.certmgr_type,
					  (void **) &ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.certmgr_type);
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	if (certmgr_get_renewal_period_mins() == SLURM_ERROR) {
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;

done:
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern int certmgr_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_rwlock_wrlock(&context_lock);

	if (g_context) {
		rc = plugin_context_destroy(g_context);
		g_context = NULL;
	}

	plugin_inited = PLUGIN_NOT_INITED;

	slurm_rwlock_unlock(&context_lock);

	return rc;
}

extern char *certmgr_g_get_node_cert_key(char *node_name)
{
	xassert(running_in_slurmd() || running_in_sackd());
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	return (*(ops.get_node_cert_key))(node_name);
}

extern char *certmgr_g_get_node_token(char *node_name)
{
	xassert(running_in_slurmd() || running_in_sackd());
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	return (*(ops.get_node_token))(node_name);
}

extern char *certmgr_g_generate_csr(char *node_name)
{
	xassert(running_in_slurmd() || running_in_sackd());
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	return (*(ops.generate_csr))(node_name);
}

extern char *certmgr_g_sign_csr(char *csr, bool is_client_auth, char *token,
				char *name)
{
	xassert(running_in_slurmctld());
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	return (*(ops.sign_csr))(csr, is_client_auth, token, name);
}

static void _get_tls_cert_work(conmgr_callback_args_t conmgr_args, void *arg)
{
	char *name = arg;
	time_t delay_seconds;
	char time_str[256];

	xassert(name);

	if (conmgr_args.status != CONMGR_WORK_STATUS_RUN)
		return;

	if (certmgr_get_cert_from_ctld(name, false)) {
		/*
		 * Don't do full delay between tries to get TLS certificate if
		 * we failed to get it.
		 */
		delay_seconds = slurm_conf.msg_timeout;
		debug("Retry getting TLS certificate in %lu seconds...",
		      delay_seconds);
	} else {
		delay_seconds =
			certmgr_get_renewal_period_mins() * MINUTE_SECONDS;
	}

	/* Periodically renew TLS certificate indefinitely */
	conmgr_add_work_delayed_fifo(_get_tls_cert_work, name, delay_seconds,
				     0);

	if (slurm_conf.debug_flags & DEBUG_FLAG_AUDIT_TLS) {
		time_t next_renewal = time(NULL) + delay_seconds;
		slurm_make_time_str(&next_renewal, time_str, sizeof(time_str));
		log_flag(AUDIT_TLS, "Next certificate renewal will happen at %s",
			 time_str);
	}
}

static void _pack_cert_and_key(char *cert, char *key, time_t last_renewal,
			       buf_t *buffer)
{
	uint16_t version = SLURM_PROTOCOL_VERSION;

	pack16(version, buffer);

	pack_time(last_renewal, buffer);
	packstr(cert, buffer);
	packstr(key, buffer);
}

static int _unpack_cert_and_key(char **cert_ptr, char **key_ptr,
				time_t *last_renewal, buf_t *buffer)
{
	uint16_t version = 0;

	xassert(running_in_slurmd());

	safe_unpack16(&version, buffer);

	if (version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_unpack_time(last_renewal, buffer);
		safe_unpackstr(cert_ptr, buffer);
		safe_unpackstr(key_ptr, buffer);
	} else {
		error("certmgr_state has invalid protocol version %d", version);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;

unpack_error:
	error("Unable to unpack certmgr_state");
	return SLURM_ERROR;
}

static int _get_cert_and_key_from_state(char **cert_ptr, char **key_ptr,
					time_t *last_renewal)
{
	int rc = SLURM_SUCCESS;
	char *state_file = NULL;
	char *tmp_state_file;
	buf_t *buffer = NULL;

	xstrfmtcat(state_file, "%s/certmgr_state", conf_spooldir);

	if (!(buffer = state_save_open(state_file, &tmp_state_file))) {
		rc = SLURM_ERROR;
		goto end;
	}

	if (_unpack_cert_and_key(cert_ptr, key_ptr, last_renewal, buffer)) {
		rc = SLURM_ERROR;
	}

end:
	xfree(tmp_state_file);
	xfree(state_file);
	FREE_NULL_BUFFER(buffer);
	return rc;
}

static int _load_cert_and_key_from_state(time_t now,
					 time_t renewal_period_seconds,
					 time_t *secs_since_last_renewal_ptr)
{
	char time_str[256];
	int rc = SLURM_SUCCESS;
	char *cert = NULL, *key = NULL;
	size_t cert_len, key_len;
	time_t last_renewal = 0, secs_since_last_renewal = 0;

	if (!conf_spooldir)
		return SLURM_ERROR;

	if (_get_cert_and_key_from_state(&cert, &key, &last_renewal)) {
		log_flag(AUDIT_TLS, "Could not find cert/key pair in state, getting new signed certificate from slurmctld now");
		return SLURM_ERROR;
	}

	cert_len = strlen(cert);
	key_len = strlen(key);

	/*
	 * Found cert/key in state, need to determine when to renew the
	 * certificate based on the last renewal time read from state. If the
	 * cert/key from state is too old, get a signed certificate from
	 * slurmctld now.
	 */
	secs_since_last_renewal = now - last_renewal;
	if (secs_since_last_renewal >= renewal_period_seconds) {
		slurm_make_time_str(&last_renewal, time_str, sizeof(time_str));
		log_flag(AUDIT_TLS,
			"More time than the renewal period of %d minute(s) has passed since the cert in state was renewed (%s). Renewing certificate now.",
			certmgr_get_renewal_period_mins(), time_str);
		rc = SLURM_ERROR;
		goto end;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_AUDIT_TLS) {
		secs2time_str(secs_since_last_renewal, time_str,
			      sizeof(time_str));
		log_flag(AUDIT_TLS, "Certificate renewal delay is reduced by %s based on last renewal time read from state.",
			 time_str);
	}

	if (conn_g_load_own_cert(cert, cert_len, key, key_len)) {
		error("%s: Could not load signed certificate and private key from state",
		      __func__);
		rc = SLURM_ERROR;
		goto end;
	}

	log_flag(AUDIT_TLS, "Successfully loaded signed certificate and private key from state");
	*secs_since_last_renewal_ptr = secs_since_last_renewal;
end:
	xfree(cert);
	xfree(key);
	return rc;
}

static int _save_cert_and_key_to_state(char *cert, char *key)
{
	int rc = SLURM_SUCCESS;
	char *state_file = NULL;
	buf_t *buffer = init_buf(1024);

	_pack_cert_and_key(cert, key, time(NULL), buffer);

	xstrfmtcat(state_file, "%s/certmgr_state", conf_spooldir);
	if (save_buf_to_state(state_file, buffer, NULL) < 0) {
		error("Failed to write cert/key pair to %s", state_file);
		rc = SLURM_ERROR;
		goto end;
	}

	log_flag(AUDIT_TLS, "Successfully saved signed certificate and private key to state");
end:

	xfree(state_file);
	FREE_NULL_BUFFER(buffer);
	return rc;
}

extern void certmgr_client_daemon_init(char *name, char *spooldir)
{
	char time_str[256];
	char hostname[HOST_NAME_MAX];
	time_t delay_until_next_renewal = 0, secs_since_last_renewal = 0;
	time_t renewal_period_seconds = 0;
	time_t now = time(NULL);

	if (!name) {
		if (gethostname(hostname, HOST_NAME_MAX))
			fatal("Could not get hostname, cannot get TLS certificate from slurmctld.");
		name = hostname;
	}

	renewal_period_seconds =
		(certmgr_get_renewal_period_mins() * MINUTE_SECONDS);

	/* Get initial cert/key either from state or from slurmctld */
	conf_spooldir = spooldir;
	if (!_load_cert_and_key_from_state(now, renewal_period_seconds,
					   &secs_since_last_renewal)) {
		/*
		 * Got valid cert/key from state, don't get signed cert from
		 * slurmctld now, wait until next renewal.
		 */
	} else if (certmgr_get_cert_from_ctld(name, true)) {
		fatal("Unable to retrieve signed certificate from slurmctld due to misconfiguration.");
	}

	/*
	 * Setup indefinite certificate renewal after retrieving the
	 * an initial signed certificate.
	 */
	delay_until_next_renewal =
		renewal_period_seconds - secs_since_last_renewal;
	conmgr_add_work_delayed_fifo(_get_tls_cert_work, name,
				     delay_until_next_renewal, 0);

	if (slurm_conf.debug_flags & DEBUG_FLAG_AUDIT_TLS) {
		time_t next_renewal = now + delay_until_next_renewal;
		slurm_make_time_str(&next_renewal, time_str, sizeof(time_str));
		log_flag(AUDIT_TLS, "Next certificate renewal will happen at %s",
			 time_str);
	}
}

extern int certmgr_get_cert_from_ctld(char *name, bool retry_forever)
{
	slurm_msg_t req, resp;
	tls_cert_request_msg_t *cert_req;
	tls_cert_response_msg_t *cert_resp;
	size_t cert_len, key_len;
	char *key;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	cert_req = xmalloc(sizeof(*cert_req));

	if (conn_g_own_cert_loaded()) {
		log_flag(AUDIT_TLS, "Using previously signed certificate to authenticate with slurmctld via mTLS");
	} else if (!(cert_req->token = certmgr_g_get_node_token(name))) {
		error("%s: Failed to get unique node token", __func__);
		slurm_free_tls_cert_request_msg(cert_req);
		return SLURM_ERROR;
	}

	if (!(cert_req->csr = certmgr_g_generate_csr(name))) {
		error("%s: Failed to generate certificate signing request",
		      __func__);
		slurm_free_tls_cert_request_msg(cert_req);
		return SLURM_ERROR;
	}

	cert_req->node_name = xstrdup(name);

	req.msg_type = REQUEST_TLS_CERT;
	req.data = cert_req;

	log_flag(AUDIT_TLS, "Sending certificate signing request to slurmctld:\n%s",
		 cert_req->csr);

retry:
	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec) <
	    0) {
		error("Unable to get TLS certificate from slurmctld: %m");
		if (retry_forever) {
			debug("Retry getting TLS certificate in %d seconds...",
			      slurm_conf.msg_timeout);
			sleep(slurm_conf.msg_timeout);
			goto retry;
		}
		slurm_free_tls_cert_request_msg(cert_req);
		return SLURM_ERROR;
	}
	slurm_free_tls_cert_request_msg(cert_req);

	switch (resp.msg_type) {
	case RESPONSE_TLS_CERT:
		break;
	case RESPONSE_SLURM_RC:
	{
		uint32_t resp_rc =
			((return_code_msg_t *) resp.data)->return_code;
		error("%s: slurmctld response to TLS certificate request: %s",
		      __func__, slurm_strerror(resp_rc));
		return SLURM_ERROR;
	}
	default:
		error("%s: slurmctld responded with unexpected msg type: %s",
		      __func__, rpc_num2string(resp.msg_type));
		return SLURM_ERROR;
	}

	cert_resp = resp.data;

	log_flag(AUDIT_TLS, "Successfully got signed certificate from slurmctld:\n%s",
		 cert_resp->signed_cert);

	if (!(key = certmgr_g_get_node_cert_key(name))) {
		error("%s: Could not get node's private key", __func__);
		return SLURM_ERROR;
	}

	cert_len = strlen(cert_resp->signed_cert);
	key_len = strlen(key);

	if (conn_g_load_own_cert(cert_resp->signed_cert, cert_len, key,
				 key_len)) {
		error("%s: Could not load signed certificate and private key into tls plugin",
		      __func__);
		return SLURM_ERROR;
	}

	if (running_in_slurmd() &&
	    _save_cert_and_key_to_state(cert_resp->signed_cert, key)) {
		error("%s: Failed to save signed certificate and key to state. A new signed certificate will need to be retrieved after restart",
		      __func__);
	}

	xfree(key);
	slurm_free_msg_data(RESPONSE_TLS_CERT, cert_resp);

	return SLURM_SUCCESS;
}
