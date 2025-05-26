/*****************************************************************************\
 *  tls.c - TLS API definitions
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
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/interfaces/conn.h"
#include "src/interfaces/tls.h"

#define TLS_PLUGIN_TYPE "tls"
#define DEFAULT_TLS_PLUGIN "tls/s2n"

/* Must mirror conn_ops_t from src/interfaces/conn.c */
typedef struct {
	int (*load_own_cert)(char *cert, uint32_t cert_len, char *key,
			     uint32_t key_len);
	void *(*create_conn)(const conn_args_t *tls_conn_args);
	void (*destroy_conn)(void *conn, bool close_fds);
	ssize_t (*send)(void *conn, const void *buf, size_t n);
	ssize_t (*recv)(void *conn, void *buf, size_t n);
	timespec_t (*get_delay)(void *conn);
	int (*negotiate)(void *conn);
	int (*set_conn_fds)(void *conn, int input_fd, int output_fd);
	int (*set_conn_callbacks)(void *conn, conn_callbacks_t *callbacks);
} tls_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for tls_ops_t.
 * Must mirror syms[] from src/interfaces/conn.c
 */
static const char *syms[] = {
	"tls_p_load_own_cert",
	"tls_p_create_conn",
	"tls_p_destroy_conn",
	"tls_p_send",
	"tls_p_recv",
	"tls_p_get_delay",
	"tls_p_negotiate_conn",
	"tls_p_set_conn_fds",
	"tls_p_set_conn_callbacks",
};

static tls_ops_t ops;
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

extern int tls_g_init(void)
{
	const char *plugin_type = TLS_PLUGIN_TYPE;
	const char *tls_type = slurm_conf.tls_type;
	int rc = SLURM_SUCCESS;

	if (!tls_type || xstrstr(tls_type, "none"))
		tls_type = DEFAULT_TLS_PLUGIN;

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	if (!(g_context = plugin_context_create(plugin_type, tls_type,
						(void **) &ops, syms,
						sizeof(syms)))) {
		debug("%s: cannot create %s context for %s",
		      __func__, plugin_type, tls_type);
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;

done:
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

extern void *tls_g_create_conn(const conn_args_t *tls_conn_args)
{
	xassert(plugin_inited == PLUGIN_INITED);

	log_flag(TLS, "%s: fd:%d->%d mode:%d",
		 __func__, tls_conn_args->input_fd, tls_conn_args->output_fd,
		 tls_conn_args->mode);

	return (*(ops.create_conn))(tls_conn_args);
}

extern int tls_g_negotiate_conn(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.negotiate))(conn);
}

extern void tls_g_destroy_conn(void *conn, bool close_fds)
{
	if (!conn)
		return;

	xassert(plugin_inited == PLUGIN_INITED);

	(*(ops.destroy_conn))(conn, close_fds);
}

extern ssize_t tls_g_recv(void *conn, void *buf, size_t n)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.recv))(conn, buf, n);
}

extern ssize_t tls_g_send(void *conn, const void *buf, size_t n)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.send))(conn, buf, n);
}

extern timespec_t tls_g_get_delay(void *conn)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.get_delay))(conn);
}

extern int tls_g_set_conn_fds(void *conn, int input_fd, int output_fd)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.set_conn_fds))(conn, input_fd, output_fd);
}

extern int tls_g_set_conn_callbacks(void *conn, conn_callbacks_t *callbacks)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.set_conn_callbacks))(conn, callbacks);
}

extern int tls_g_load_own_cert(char *cert, uint32_t cert_len, char *key,
			       uint32_t key_len)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return (*(ops.load_own_cert))(cert, cert_len, key, key_len);
}

extern bool tls_available(void)
{
	return (plugin_inited == PLUGIN_INITED);
}
