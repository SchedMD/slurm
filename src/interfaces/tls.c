/*****************************************************************************\
 *  tls.c - tls API definitions
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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/util-net.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/tls.h"
#include "src/interfaces/certmgr.h"

#define HEADER_MSG_TYPE_HANDSHAKE 0x16 /* SSLv3: handshake(22) */
#define HEADER_MSG_TYPE_CLIENT_HELLO 0x01 /* TLSv1.X: client_hello(1) */

#define HEADER_LENGTH_MIN (sizeof(uint16_t))
#define HEADER_LENGTH_MAX 0x0FFF

#define PROTOCOL_VERSION_MIN 0x0300
#define PROTOCOL_VERSION_MAX 0x03ff

typedef struct {
	int index;
	char data[];
} tls_wrapper_t;

typedef struct {
	uint32_t (*plugin_id);
	int (*load_ca_cert)(char *cert_file);
	char *(*get_own_public_cert)(void);
	int (*load_own_cert)(char *cert, uint32_t cert_len, char *key,
			     uint32_t key_len);
	bool (*own_cert_loaded)(void);
	void *(*create_conn)(const tls_conn_args_t *tls_conn_args);
	void (*destroy_conn)(void *conn, bool close_fds);
	ssize_t (*send)(void *conn, const void *buf, size_t n);
	ssize_t (*sendv)(void *conn, const struct iovec *bufs, int count);
	uint32_t (*peek)(void *conn);
	ssize_t (*recv)(void *conn, void *buf, size_t n);
	timespec_t (*get_delay)(void *conn);
	int (*negotiate)(void *conn);
	int (*get_conn_fd)(void *conn);
	int (*set_conn_fds)(void *conn, int input_fd, int output_fd);
	int (*set_conn_callbacks)(void *conn, tls_conn_callbacks_t *callbacks);
	void (*set_graceful_shutdown)(void *conn, bool do_graceful_shutdown);
} tls_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for tls_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"tls_p_load_ca_cert",
	"tls_p_get_own_public_cert",
	"tls_p_load_own_cert",
	"tls_p_own_cert_loaded",
	"tls_p_create_conn",
	"tls_p_destroy_conn",
	"tls_p_send",
	"tls_p_sendv",
	"tls_p_peek",
	"tls_p_recv",
	"tls_p_get_delay",
	"tls_p_negotiate_conn",
	"tls_p_get_conn_fd",
	"tls_p_set_conn_fds",
	"tls_p_set_conn_callbacks",
	"tls_p_set_graceful_shutdown",
};

static tls_ops_t ops;
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

static bool tls_enabled_bool = false;

extern char *tls_conn_mode_to_str(tls_conn_mode_t mode)
{
	switch (mode) {
	case TLS_CONN_NULL:
		return "null";
	case TLS_CONN_SERVER:
		return "server";
	case TLS_CONN_CLIENT:
		return "client";
	}

	return "INVALID";
}

extern bool tls_enabled(void)
{
	return tls_enabled_bool;
}

extern int tls_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "tls";
	char *tls_type = NULL;

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	tls_type = xstrdup(slurm_conf.tls_type);

	if (!tls_type) {
		plugin_inited = PLUGIN_NOOP;
		goto done;
	}

	g_context = plugin_context_create(plugin_type, tls_type, (void **) &ops,
					  syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, tls_type);
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	if (xstrstr(slurm_conf.tls_type, "s2n"))
		tls_enabled_bool = true;

	plugin_inited = PLUGIN_INITED;
done:
	xfree(tls_type);
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern int tls_g_fini(void)
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

extern int tls_g_load_ca_cert(char *cert_file)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.load_ca_cert))(cert_file);
}

extern char *tls_g_get_own_public_cert(void)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.get_own_public_cert))();
}

extern int tls_g_load_own_cert(char *cert, uint32_t cert_len, char *key,
			       uint32_t key_len)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.load_own_cert))(cert, cert_len, key, key_len);
}

extern bool tls_g_own_cert_loaded(void)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.own_cert_loaded))();
}

extern void *tls_g_create_conn(const tls_conn_args_t *tls_conn_args)
{
	xassert(plugin_inited == PLUGIN_INITED);

	log_flag(TLS, "%s: fd:%d->%d mode:%d",
		 __func__, tls_conn_args->input_fd, tls_conn_args->output_fd,
		 tls_conn_args->mode);

	return (*(ops.create_conn))(tls_conn_args);
}

extern void tls_g_destroy_conn(void *conn, bool close_fds)
{
	if (!conn)
		return;

	xassert(plugin_inited == PLUGIN_INITED);

	(*(ops.destroy_conn))(conn, close_fds);
}

extern ssize_t tls_g_send(void *conn, const void *buf, size_t n)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.send))(conn, buf, n);
}

extern ssize_t tls_g_sendv(void *conn, const struct iovec *bufs, int count)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.sendv))(conn, bufs, count);
}

extern uint32_t tls_g_peek(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.peek))(conn);
}

extern ssize_t tls_g_recv(void *conn, void *buf, size_t n)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.recv))(conn, buf, n);
}

extern timespec_t tls_g_get_delay(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.get_delay))(conn);
}

extern int tls_g_negotiate_conn(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.negotiate))(conn);
}

extern int tls_g_get_conn_fd(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.get_conn_fd))(conn);
}

extern int tls_g_set_conn_fds(void *conn, int input_fd, int output_fd)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.set_conn_fds))(conn, input_fd, output_fd);
}

extern int tls_g_set_conn_callbacks(void *conn,
				    tls_conn_callbacks_t *callbacks)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.set_conn_callbacks))(conn, callbacks);
}

extern void tls_g_set_graceful_shutdown(void *conn, bool do_graceful_shutdown)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.set_graceful_shutdown))(conn, do_graceful_shutdown);
}

static int _is_sslv3_handshake(const void *buf, const size_t n)
{
	const uint8_t *p = buf;
	uint8_t msg_type = 0;
	uint16_t protocol_version = 0;
	uint16_t length = 0;

	/* Extract header if possible */
	if (n < 5)
		return EWOULDBLOCK;

	/*
	 * Match per SSLv3 RFC#6101:
	 *
	 * Record Handshake Header:
	 * |------------------------------------------------------|
	 * | 8 - msg_type | 16 - SSL version | 16 - packet length |
	 * |------------------------------------------------------|
	 *
	 * Example Record Headers:
	 *	0x16 03 01 02 00
	 *	0x16 03 01 00 f4
	 */

	if ((msg_type = p[0]) != HEADER_MSG_TYPE_HANDSHAKE)
		return ENOENT;

	protocol_version |= p[1] << 8;
	protocol_version |= p[2];

	if ((protocol_version < PROTOCOL_VERSION_MIN) ||
	    (protocol_version > PROTOCOL_VERSION_MAX))
		return ENOENT;

	length |= p[3] << 8;
	length |= p[4];

	if ((length < HEADER_LENGTH_MIN) || (length > HEADER_LENGTH_MAX))
		return ENOENT;

	return SLURM_SUCCESS;
}

static int _is_tls_handshake(const void *buf, const size_t n)
{
	const uint8_t *p = buf;
	uint8_t msg_type = 0;
	uint32_t length = 0;
	uint16_t protocol_version = 0;

	/* Extract header if possible */
	if (n < 6)
		return EWOULDBLOCK;

	/*
	 * Match per TLSv1.x RFC#8446:
	 *
	 * Client Hello Header:
	 * |----------------------------------------------------|
	 * | 8 - msg_type | 24 - length | 16 - protocol version |
	 * |----------------------------------------------------|
	 *
	 * Example Hello: 0x01 00 01 fc 03 03
	 */

	if ((msg_type = p[0]) != HEADER_MSG_TYPE_CLIENT_HELLO)
		return ENOENT;

	length |= p[1] << 16;
	length |= p[2] << 8;
	length |= p[3];

	if ((length < HEADER_LENGTH_MIN) || (length > HEADER_LENGTH_MAX))
		return ENOENT;

	protocol_version |= p[4] << 8;
	protocol_version |= p[5];

	if ((protocol_version < PROTOCOL_VERSION_MIN) ||
	    (protocol_version > PROTOCOL_VERSION_MAX))
		return ENOENT;

	return SLURM_SUCCESS;
}

extern int tls_is_handshake(const void *buf, const size_t n, const char *name)
{
	int match_tls = EINVAL, match_ssl = EINVAL;

	if (!(match_ssl = _is_sslv3_handshake(buf, n))) {
		log_flag(NET, "%s: [%s] SSLv3 handshake fingerprint matched",
			 __func__, name);
		log_flag_hex(NET_RAW, buf, n, "[%s] matched SSLv3 handshake",
			     name);
		return SLURM_SUCCESS;
	}

	if (!(match_tls = _is_tls_handshake(buf, n))) {
		log_flag(NET, "%s: [%s] TLS handshake fingerprint matched",
			 __func__, name);
		log_flag_hex(NET_RAW, buf, n, "[%s] matched TLS handshake",
			     name);
		return SLURM_SUCCESS;
	}

	if ((match_tls == EWOULDBLOCK) || (match_ssl == EWOULDBLOCK)) {
		log_flag(NET, "%s: [%s] waiting for more bytes to fingerprint match TLS handshake",
				 __func__, name);
		return EWOULDBLOCK;
	}

	if ((match_tls == ENOENT) && (match_ssl == ENOENT)) {
		log_flag(NET, "%s: [%s] TLS not detected",
			 __func__, name);
		log_flag_hex(NET_RAW, buf, n,
			     "[%s] unable to match TLS handshake", name);
		return ENOENT;
	}

	return MAX(match_tls, match_ssl);
}

extern int tls_get_cert_from_ctld(char *name)
{
	slurm_msg_t req, resp;
	tls_cert_request_msg_t *cert_req;
	tls_cert_response_msg_t *cert_resp;
	size_t cert_len, key_len;
	char *key;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	cert_req = xmalloc(sizeof(*cert_req));

	if (!(cert_req->token = certmgr_g_get_node_token(name))) {
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

	if (tls_g_load_own_cert(cert_resp->signed_cert, cert_len, key,
				key_len)) {
		error("%s: Could not load signed certificate and private key into tls plugin",
		      __func__);
		return SLURM_ERROR;
	}

	xfree(key);
	slurm_free_msg_data(RESPONSE_TLS_CERT, cert_resp);

	return SLURM_SUCCESS;
}
