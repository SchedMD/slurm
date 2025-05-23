/*****************************************************************************\
 *  conn.c - connection API definitions
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

#include "src/interfaces/certmgr.h"
#include "src/interfaces/conn.h"

typedef struct {
	uint32_t (*plugin_id);
	int (*load_ca_cert)(char *cert_file);
	char *(*get_own_public_cert)(void);
	int (*load_own_cert)(char *cert, uint32_t cert_len, char *key,
			     uint32_t key_len);
	int (*load_self_signed_cert)(void);
	bool (*own_cert_loaded)(void);
	void *(*create_conn)(const conn_args_t *conn_args);
	void (*destroy_conn)(void *conn, bool close_fds);
	ssize_t (*send)(void *conn, const void *buf, size_t n);
	ssize_t (*sendv)(void *conn, const struct iovec *bufs, int count);
	uint32_t (*peek)(void *conn);
	ssize_t (*recv)(void *conn, void *buf, size_t n);
	timespec_t (*get_delay)(void *conn);
	int (*negotiate)(void *conn);
	bool (*is_client_authenticated)(void *conn);
	int (*get_conn_fd)(void *conn);
	int (*set_conn_fds)(void *conn, int input_fd, int output_fd);
	int (*set_conn_callbacks)(void *conn, conn_callbacks_t *callbacks);
	void (*set_graceful_shutdown)(void *conn, bool do_graceful_shutdown);
} conn_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for conn_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"tls_p_load_ca_cert",
	"tls_p_get_own_public_cert",
	"tls_p_load_own_cert",
	"tls_p_load_self_signed_cert",
	"tls_p_own_cert_loaded",
	"tls_p_create_conn",
	"tls_p_destroy_conn",
	"tls_p_send",
	"tls_p_sendv",
	"tls_p_peek",
	"tls_p_recv",
	"tls_p_get_delay",
	"tls_p_negotiate_conn",
	"tls_p_is_client_authenticated",
	"tls_p_get_conn_fd",
	"tls_p_set_conn_fds",
	"tls_p_set_conn_callbacks",
	"tls_p_set_graceful_shutdown",
};

static conn_ops_t ops;
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

static bool tls_enabled_bool = false;

extern char *conn_mode_to_str(conn_mode_t mode)
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

extern int conn_g_init(void)
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

	if (tls_enabled_bool) {
		/* Load CA cert now, wait until later in configless */
		if (!running_in_slurmstepd() && slurm_conf.last_update &&
		    conn_g_load_ca_cert(NULL)) {
			error("Could not load trusted certificates for s2n");
			rc = SLURM_ERROR;
			goto done;
		}

		/* Load own cert from file */
		if ((running_in_slurmctld() || running_in_slurmdbd() ||
		     running_in_slurmrestd() || running_in_slurmd() ||
		     running_in_sackd()) &&
		    slurm_conf.last_update &&
		    conn_g_load_own_cert(NULL, 0, NULL, 0)) {
			error("Could not load own TLS certificate from file");
			rc = SLURM_ERROR;
			goto done;
		}

		/* Load self-signed certificate in client commands */
		if (!running_in_daemon() && conn_g_load_self_signed_cert()) {
			error("Could not load self-signed TLS certificate");
			rc = SLURM_ERROR;
			goto done;
		}
	}

done:
	xfree(tls_type);
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern int conn_g_fini(void)
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

extern int conn_g_load_ca_cert(char *cert_file)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.load_ca_cert))(cert_file);
}

extern char *conn_g_get_own_public_cert(void)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.get_own_public_cert))();
}

extern int conn_g_load_own_cert(char *cert, uint32_t cert_len, char *key,
				uint32_t key_len)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.load_own_cert))(cert, cert_len, key, key_len);
}

extern int conn_g_load_self_signed_cert(void)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.load_self_signed_cert))();
}

extern bool conn_g_own_cert_loaded(void)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.own_cert_loaded))();
}

extern void *conn_g_create(const conn_args_t *conn_args)
{
	xassert(plugin_inited == PLUGIN_INITED);

	log_flag(TLS, "%s: fd:%d->%d mode:%d",
		 __func__, conn_args->input_fd, conn_args->output_fd,
		 conn_args->mode);

	return (*(ops.create_conn))(conn_args);
}

extern void conn_g_destroy(void *conn, bool close_fds)
{
	if (!conn)
		return;

	xassert(plugin_inited == PLUGIN_INITED);

	(*(ops.destroy_conn))(conn, close_fds);
}

extern ssize_t conn_g_send(void *conn, const void *buf, size_t n)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.send))(conn, buf, n);
}

extern ssize_t conn_g_sendv(void *conn, const struct iovec *bufs, int count)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.sendv))(conn, bufs, count);
}

extern uint32_t conn_g_peek(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.peek))(conn);
}

extern ssize_t conn_g_recv(void *conn, void *buf, size_t n)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.recv))(conn, buf, n);
}

extern timespec_t conn_g_get_delay(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.get_delay))(conn);
}

extern int conn_g_negotiate_tls(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.negotiate))(conn);
}

extern bool conn_g_is_client_authenticated(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.is_client_authenticated))(conn);
}

extern int conn_g_get_fd(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.get_conn_fd))(conn);
}

extern int conn_g_set_fds(void *conn, int input_fd, int output_fd)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.set_conn_fds))(conn, input_fd, output_fd);
}

extern int conn_g_set_callbacks(void *conn, conn_callbacks_t *callbacks)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.set_conn_callbacks))(conn, callbacks);
}

extern void conn_g_set_graceful_shutdown(void *conn, bool do_graceful_shutdown)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.set_graceful_shutdown))(conn, do_graceful_shutdown);
}
