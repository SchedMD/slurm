/*****************************************************************************\
 *  http_auth.c - HTTP authentication plugin interface
 ******************************************************************************
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

#include "slurm/slurm_errno.h"

#include "src/common/macros.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_defs.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/http_auth.h"

#define HTTP_AUTH_PLUGIN_TYPE "http_auth"

typedef struct {
	uint32_t *plugin_id;
	int (*init)(const char *auth_info);
	void (*fini)(void);
	int (*authenticate)(uid_t *uid_ptr, http_con_t *hcon, const char *name,
			    const http_con_request_t *request);
	int (*proxy_token)(http_con_t *hcon, const char *name,
			   const http_con_request_t *request);
} ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"http_auth_p_init",
	"http_auth_p_fini",
	"http_auth_p_authenticate",
	"http_auth_p_proxy_token",
};

static plugins_t *http_plugins = NULL;
static plugins_t *auth_plugins = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void _init_plugins(plugins_t *plugins, const char *auth_info)
{
	for (int i = 0; i < plugins->count; i++) {
		int rc = EINVAL;
		const ops_t *ops = plugins->functions[i];

		if ((rc = ops->init(auth_info)))
			fatal("%s: %s loaded failed: %s", __func__,
			      plugins->types[i], slurm_strerror(rc));
	}
}

static int _load(const char *auth_info, const char *plugin_type,
		 plugrack_foreach_t listf)
{
	int rc;

	if (http_plugins || auth_plugins)
		return SLURM_SUCCESS;

	_Static_assert(sizeof(ops_t) == (sizeof(void *) * ARRAY_SIZE(syms)),
		       "Check symbol table is correct size");

	if ((rc = load_plugins(&http_plugins, HTTP_AUTH_PLUGIN_TYPE, NULL,
			       listf, syms, ARRAY_SIZE(syms))))
		return rc;

	if ((rc = load_plugins(&auth_plugins, AUTH_PLUGIN_TYPE,
			       auth_get_plugin_name(AUTH_PLUGIN_JWT), listf,
			       syms, ARRAY_SIZE(syms))))
		return rc;

	if ((http_plugins->count <= 0) && (auth_plugins->count <= 0))
		return ESLURM_PLUGIN_NOT_LOADED;

	_init_plugins(http_plugins, auth_info);
	_init_plugins(auth_plugins, auth_info);

	return SLURM_SUCCESS;
}

extern int http_auth_g_init(const char *auth_info, const char *plugin_type,
			    plugrack_foreach_t listf)
{
	int rc;

	if (!auth_info || !auth_info[0])
		return SLURM_SUCCESS;

	slurm_mutex_lock(&mutex);

	rc = _load(auth_info, plugin_type, listf);

	slurm_mutex_unlock(&mutex);

	return rc;
}

static void _fini_plugins(plugins_t *plugins)
{
	if (!plugins || (plugins->count <= 0))
		return;

	for (int i = 0; i < plugins->count; i++) {
		const ops_t *ops = plugins->functions[i];

		ops->fini();
	}
}

extern void http_auth_g_fini(void)
{
	slurm_mutex_lock(&mutex);

	_fini_plugins(http_plugins);
	FREE_NULL_PLUGINS(http_plugins);
	_fini_plugins(auth_plugins);
	FREE_NULL_PLUGINS(auth_plugins);

	slurm_mutex_unlock(&mutex);
}

static int _authenticate(plugins_t *plugins, http_auth_plugin_id_t plugin_id,
			 uid_t *uid_ptr, http_con_t *hcon, const char *name,
			 const http_con_request_t *request)
{
	if (!plugins || (plugins->count <= 0))
		return ESLURM_AUTH_SKIP;

	/*
	 * Intentionally not holding mutex to avoid performance penalty.
	 * fini() from other threads could cause NULL dereference like in any
	 * other Slurm plugin. The mutex in init/fini is only intended to guard
	 * against stacked load/unload (e.g. sackd).
	 */
	for (int i = 0; i < plugins->count; i++) {
		const ops_t *ops = plugins->functions[i];
		int rc = EINVAL;

		if ((plugin_id != HTTP_AUTH_PLUGIN_ANY) &&
		    (plugin_id != *ops->plugin_id))
			continue;

		if ((rc = ops->authenticate(uid_ptr, hcon, name, request)) ==
		    ESLURM_AUTH_SKIP)
			continue;

		return rc;
	}

	return ESLURM_AUTH_SKIP;
}

extern int http_auth_g_authenticate(http_auth_plugin_id_t plugin_id,
				    uid_t *uid_ptr, http_con_t *hcon,
				    const char *name,
				    const http_con_request_t *request)
{
	int rc = EINVAL;

	xassert(plugin_id > HTTP_AUTH_PLUGIN_INVALID);
	xassert((plugin_id < HTTP_AUTH_PLUGIN_INVALID_MAX) ||
		(plugin_id == HTTP_AUTH_PLUGIN_ANY));

	/* Always populate the UID with SLURM_AUTH_NOBODY */
	if (uid_ptr)
		*uid_ptr = SLURM_AUTH_NOBODY;

	if ((rc = _authenticate(http_plugins, plugin_id, uid_ptr, hcon, name,
				request)) != ESLURM_AUTH_SKIP)
		return rc;

	if ((rc = _authenticate(auth_plugins, plugin_id, uid_ptr, hcon, name,
				request)) != ESLURM_AUTH_SKIP)
		return rc;

	return ESLURM_AUTH_CRED_INVALID;
}

static int _proxy_token(plugins_t *plugins, http_auth_plugin_id_t plugin_id,
			http_con_t *hcon, const char *name,
			const http_con_request_t *request)
{
	if (!plugins || (plugins->count <= 0))
		return ESLURM_NOT_SUPPORTED;

	for (int i = 0; i < plugins->count; i++) {
		const ops_t *ops = plugins->functions[i];
		int rc;

		if ((plugin_id != HTTP_AUTH_PLUGIN_ANY) &&
		    (plugin_id != *ops->plugin_id))
			continue;

		if ((rc = ops->proxy_token(hcon, name, request)) ==
		    ESLURM_AUTH_SKIP)
			continue;

		return rc;
	}

	return ESLURM_AUTH_SKIP;
}

extern int http_auth_g_proxy_token(http_auth_plugin_id_t plugin_id,
				   http_con_t *hcon, const char *name,
				   const http_con_request_t *request)
{
	int rc = EINVAL;

	xassert(plugin_id >= HTTP_AUTH_PLUGIN_INVALID);
	xassert((plugin_id < HTTP_AUTH_PLUGIN_INVALID_MAX) ||
		(plugin_id == HTTP_AUTH_PLUGIN_ANY));

	if (plugin_id == HTTP_AUTH_PLUGIN_INVALID) {
		auth_g_thread_clear();
		return SLURM_SUCCESS;
	}

	if ((rc = _proxy_token(http_plugins, plugin_id, hcon, name, request)) !=
	    ESLURM_AUTH_SKIP)
		return rc;

	if ((rc = _proxy_token(auth_plugins, plugin_id, hcon, name, request)) !=
	    ESLURM_AUTH_SKIP)
		return rc;

	return ESLURM_AUTH_CRED_INVALID;
}
