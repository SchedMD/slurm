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
	void *(*create_conn)(const tls_conn_args_t *tls_conn_args);
	void (*destroy_conn)(void *conn);
	ssize_t (*send)(void *conn, const void *buf, size_t n);
	ssize_t (*recv)(void *conn, void *buf, size_t n);
	timespec_t (*get_delay)(void *conn);
	int (*negotiate)(void *conn);
	int (*set_conn_fds)(void *conn, int input_fd, int output_fd);
	int (*set_conn_callbacks)(void *conn, tls_conn_callbacks_t *callbacks);
} tls_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for tls_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"tls_p_create_conn",
	"tls_p_destroy_conn",
	"tls_p_send",
	"tls_p_recv",
	"tls_p_get_delay",
	"tls_p_negotiate_conn",
	"tls_p_set_conn_fds",
	"tls_p_set_conn_callbacks",
};

static tls_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static int g_context_num = 0;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

static int _get_plugin_index(int plugin_id)
{
	xassert(ops);

	for (int i = 0; i < g_context_num; i++)
		if (plugin_id == *ops[i].plugin_id)
			return i;

	return 0;
}

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
	xassert(ops);

	return (*ops[0].plugin_id != TLS_PLUGIN_NONE);
}

extern int tls_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "tls";
	char *tls_plugin_list = NULL, *plugin_list = NULL, *type = NULL;
	char *save_ptr = NULL;

	slurm_rwlock_wrlock(&context_lock);

	if (g_context_num > 0)
		goto done;

	if (running_in_daemon())
		tls_plugin_list = xstrdup(slurm_conf.tls_type);
	else
		tls_plugin_list = xstrdup("none");

	/* ensure none plugin is always loaded */
	if (!xstrstr(tls_plugin_list, "none"))
		xstrcat(tls_plugin_list, ",none");
	plugin_list = tls_plugin_list;

	while ((type = strtok_r(tls_plugin_list, ",", &save_ptr))) {
		char *full_type = NULL;

		xrecalloc(ops, g_context_num + 1, sizeof(tls_ops_t));
		xrecalloc(g_context, g_context_num + 1,
			  sizeof(plugin_context_t));

		if (!xstrncmp(type, "tls/", 4))
			type += 4;
		full_type = xstrdup_printf("tls/%s", type);

		g_context[g_context_num] = plugin_context_create(
			plugin_type, full_type, (void **) &ops[g_context_num],
			syms, sizeof(syms));

		if (!g_context[g_context_num]) {
			error("cannot create %s context for %s",
			      plugin_type, full_type);
			rc = SLURM_ERROR;
			xfree(full_type);
			goto done;
		}
		xfree(full_type);

		g_context_num++;
		tls_plugin_list = NULL; /* for next iteration */
	}

done:
	slurm_rwlock_unlock(&context_lock);
	xfree(plugin_list);
	return rc;
}

extern int tls_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_rwlock_wrlock(&context_lock);
	for (int i = 0; i < g_context_num; i++) {
		int rc2 = plugin_context_destroy(g_context[i]);
		if (rc2) {
			debug("%s: %s: %s",
			      __func__, g_context[i]->type,
			      slurm_strerror(rc2));
			rc = SLURM_ERROR;
		}
	}

	xfree(ops);
	xfree(g_context);
	g_context_num = -1;

	slurm_rwlock_unlock(&context_lock);

	return rc;
}

extern void *tls_g_create_conn(const tls_conn_args_t *tls_conn_args)
{
	int plugin_index = 0;
	tls_wrapper_t *wrapper = NULL;
	xassert(g_context);

	log_flag(TLS, "%s: fd:%d->%d mode:%d",
		 __func__, tls_conn_args->input_fd, tls_conn_args->output_fd,
		 tls_conn_args->mode);

	/*
	 * All other modes use the default plugin.
	 */
	if (tls_conn_args->mode == TLS_CONN_NULL)
		plugin_index = _get_plugin_index(TLS_PLUGIN_NONE);

	wrapper = (*(ops[plugin_index].create_conn))(tls_conn_args);

	if (wrapper)
		wrapper->index = plugin_index;

	return wrapper;
}

extern void tls_g_destroy_conn(void *conn)
{
	tls_wrapper_t *wrapper = conn;

	if (!wrapper)
		return;

	xassert(g_context);

	(*(ops[wrapper->index].destroy_conn))(conn);
}

extern ssize_t tls_g_send(void *conn, const void *buf, size_t n)
{
	tls_wrapper_t *wrapper = conn;

	xassert(g_context);

	if (!wrapper)
		return SLURM_ERROR;

	return (*(ops[wrapper->index].send))(conn, buf, n);
}

extern ssize_t tls_g_recv(void *conn, void *buf, size_t n)
{
	tls_wrapper_t *wrapper = conn;

	xassert(g_context);

	if (!wrapper)
		return SLURM_ERROR;

	return (*(ops[wrapper->index].recv))(conn, buf, n);
}

extern timespec_t tls_g_get_delay(void *conn)
{
	tls_wrapper_t *wrapper = conn;

	xassert(g_context);

	if (!wrapper)
		return ((timespec_t) { 0, 0 });

	return (*(ops[wrapper->index].get_delay))(conn);
}

extern int tls_g_negotiate_conn(void *conn)
{
	tls_wrapper_t *wrapper = conn;

	xassert(g_context);

	if (!wrapper)
		return ESLURM_NOT_SUPPORTED;

	return (*(ops[wrapper->index].negotiate))(conn);
}

extern int tls_g_set_conn_fds(void *conn, int input_fd, int output_fd)
{
	tls_wrapper_t *wrapper = conn;

	xassert(g_context);

	if (!wrapper)
		return ESLURM_NOT_SUPPORTED;

	return (*(ops[wrapper->index].set_conn_fds))(conn, input_fd, output_fd);
}

extern int tls_g_set_conn_callbacks(void *conn,
				    tls_conn_callbacks_t *callbacks)
{
	tls_wrapper_t *wrapper = conn;

	xassert(g_context);

	if (!wrapper)
		return ESLURM_NOT_SUPPORTED;

	return (*(ops[wrapper->index].set_conn_callbacks))(conn, callbacks);
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
	protocol_version = ntohs(protocol_version);

	if ((protocol_version < PROTOCOL_VERSION_MIN) ||
	    (protocol_version > PROTOCOL_VERSION_MAX))
		return ENOENT;

	length |= p[3] << 8;
	length |= p[4];
	length = ntohl(length);

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
	length = ntohl(length);

	if ((length < HEADER_LENGTH_MIN) || (length > HEADER_LENGTH_MAX))
		return ENOENT;

	protocol_version |= p[4] << 8;
	protocol_version |= p[5];
	protocol_version = ntohs(protocol_version);

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
