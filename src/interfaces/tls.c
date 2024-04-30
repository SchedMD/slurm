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

typedef struct {
	int index;
	char data[];
} tls_wrapper_t;

typedef struct {
	uint32_t (*plugin_id);
	void *(*create_conn)(int fd, tls_conn_mode_t mode);
	void (*destroy_conn)(void *conn);
	ssize_t (*send)(void *conn, const void *buf, size_t n);
	ssize_t (*recv)(void *conn, void *buf, size_t n);
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
	static bool daemon_run = false, daemon_set = false;

	slurm_rwlock_wrlock(&context_lock);

	if (g_context_num > 0)
		goto done;

	/* Only slurmctld/slurmdbd support tls currently */
	if (run_in_daemon(&daemon_run, &daemon_set, "slurmctld,slurmdbd"))
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

extern void *tls_g_create_conn(int fd, tls_conn_mode_t mode)
{
	int plugin_index = 0;
	tls_wrapper_t *wrapper = NULL;
	xassert(g_context);

	log_flag(TLS, "%s: fd:%d mode:%d", __func__, fd, mode);

	/*
	 * All other modes use the default plugin.
	 */
	if (mode == TLS_CONN_NULL)
		plugin_index = _get_plugin_index(TLS_PLUGIN_NONE);

	wrapper = (*(ops[plugin_index].create_conn))(fd, mode);

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
