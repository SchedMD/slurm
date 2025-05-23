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
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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

extern int certmgr_get_cert_from_ctld(char *name)
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

	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec) <
	    0) {
		error("Unable to get TLS certificate from slurmctld: %m");
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

	xfree(key);
	slurm_free_msg_data(RESPONSE_TLS_CERT, cert_resp);

	return SLURM_SUCCESS;
}
