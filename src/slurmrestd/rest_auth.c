/*****************************************************************************\
 *  rest_auth.c - Slurm REST API HTTP authentication
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
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

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/plugin.h"
#include "src/common/slurm_auth.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/rest_auth.h"
#include "src/slurmrestd/xjson.h"

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	int (*init)(void);
	int (*fini)(void);
	int (*auth)(on_http_request_args_t *args, rest_auth_context_t *ctxt);
	int (*apply)(rest_auth_context_t *context);
	void (*free)(rest_auth_context_t *context);
} slurm_rest_auth_ops_t;

/*
 * Must be synchronized with slurm_rest_auth_ops_t above.
 */
static const char *syms[] = {
	"slurm_rest_auth_p_init",
	"slurm_rest_auth_p_fini",
	"slurm_rest_auth_p_authenticate",
	"slurm_rest_auth_p_apply",
	"slurm_rest_auth_p_free", /* release contents of plugin_data */
};

static slurm_rest_auth_ops_t *ops;
static uint32_t *plugin_ids;
static int g_context_cnt = -1;
static plugin_context_t **g_context = NULL;

#define MAGIC 0xDEDEDEDE

static void _check_magic(rest_auth_context_t *ctx)
{
	xassert(ctx);
	xassert(ctx->magic == MAGIC);

	if (ctx->plugin_id) {
		xassert(ctx->user_name);
	} else {
		xassert(!ctx->plugin_data);
		xassert(!ctx->user_name);
	}
}

extern void destroy_rest_auth(void)
{
	slurm_mutex_lock(&init_lock);

	for (int i = 0; (g_context_cnt > 0) && (i < g_context_cnt); i++) {
		(*(ops[i].fini))();

		if (g_context[i] && plugin_context_destroy(g_context[i]))
				fatal_abort("%s: unable to unload plugin",
					    __func__);
	}
	xfree(ops);
	xfree(g_context);
	g_context_cnt = -1;

	slurm_mutex_unlock(&init_lock);
}

extern int init_rest_auth(const plugin_handle_t *plugin_handles,
			  const size_t plugin_count)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&init_lock);

	/* Load OpenAPI plugins */
	xassert(g_context_cnt == -1);
	g_context_cnt = 0;

	xrecalloc(ops, (plugin_count + 1), sizeof(slurm_rest_auth_ops_t));
	xrecalloc(plugin_ids, (plugin_count + 1), sizeof(plugin_ids));
	xrecalloc(g_context, (plugin_count + 1), sizeof(plugin_context_t *));

	for (size_t i = 0; (i < plugin_count); i++) {
		void *id_ptr;

		if (plugin_handles[i] == PLUGIN_INVALID_HANDLE)
			fatal("Invalid plugin to load?");

		if (plugin_get_syms(plugin_handles[i],
				    (sizeof(syms)/sizeof(syms[0])),
				    syms, (void **)&ops[g_context_cnt])
		    < (sizeof(syms)/sizeof(syms[0])))
			fatal("Incomplete plugin detected");

		id_ptr = plugin_get_sym(plugin_handles[i], "plugin_id");
		if (!id_ptr)
			fatal("%s: unable to find plugin_id symbol",
			      __func__);

		plugin_ids[g_context_cnt] = *(uint32_t *)id_ptr;
		if (!plugin_ids[g_context_cnt])
			fatal("%s: invalid plugin_id: %u",
			      __func__, plugin_ids[g_context_cnt]);
		else
			debug5("%s: found plugin_id: %u",
			       __func__, plugin_ids[g_context_cnt]);

		(*(ops[g_context_cnt].init))();
		g_context_cnt++;
	}

	slurm_mutex_unlock(&init_lock);

	return rc;
}

static void _clear_auth(rest_auth_context_t *ctxt)
{
	_check_magic(ctxt);

	if (ctxt->plugin_id) {
		bool found = false;

		for (int i = 0; (g_context_cnt > 0) && (i < g_context_cnt); i++) {
			if (plugin_ids[i] == ctxt->plugin_id) {
				(*(ops[i].free))(ctxt);
				found = true;
				break;
			}
		}

		if (!found)
			fatal_abort("%s: unable to free auth context with plugin_id: %u",
				    __func__, ctxt->plugin_id);
	}

	xassert(!ctxt->plugin_data);
	xfree(ctxt->user_name);
	ctxt->plugin_id = 0;

	rest_auth_context_clear();
}

extern int rest_authenticate_http_request(on_http_request_args_t *args)
{
	int rc = ESLURM_AUTH_CRED_INVALID;
	rest_auth_context_t *context =
		(rest_auth_context_t *) args->context->auth;

	if (!context) {
		context = rest_auth_context_new();
		args->context->auth = context;
	}

	_check_magic(context);

	/* continue if already authenticated via plugin */
	if (context->plugin_id)
		return rest_auth_context_apply(context);

	for (int i = 0; (g_context_cnt > 0) && (i < g_context_cnt); i++) {
		rc = (*(ops[i].auth))(args, context);

		if (rc == ESLURM_AUTH_SKIP)
			continue;

		if (!rc) {
			context->plugin_id = plugin_ids[i];
			_check_magic(context);
			return rest_auth_context_apply(context);
		} else /* plugin explicit rejected */
			break;
	}

	rest_auth_context_clear();
	return rc;
}

extern rest_auth_context_t *rest_auth_context_new(void)
{
	rest_auth_context_t *context = xmalloc(sizeof(*context));

	context->magic = MAGIC;
	context->plugin_id = 0; /* explicitly set to 0 */

	return context;
}

extern int rest_auth_context_apply(rest_auth_context_t *context)
{
	if (!context->plugin_id)
		return ESLURM_AUTH_CRED_INVALID;

	for (int i = 0; (g_context_cnt > 0) && (i < g_context_cnt); i++)
		if (context->plugin_id == plugin_ids[i])
			return (*(ops[i].apply))(context);

	return ESLURM_AUTH_CRED_INVALID;
}

extern void rest_auth_context_clear(void)
{
	g_slurm_auth_thread_clear();
}

extern void rest_auth_context_free(rest_auth_context_t *context)
{
	bool found = false;
	if (!context)
		return;

	_clear_auth(context);
	if (context->plugin_id) {
		for (int i = 0;
		     (g_context_cnt > 0) && (i < g_context_cnt);
		     i++) {
			if (context->plugin_id == plugin_ids[i]) {
				found = true;
				(*(ops[i].free))(context);
				xassert(!context->plugin_data);
				break;
			}
		}

		if (!found)
			fatal_abort("%s: unable to find plugin_id: %u",
				    __func__, context->plugin_id);
	}
	context->magic = ~MAGIC;
	xfree(context);
}
