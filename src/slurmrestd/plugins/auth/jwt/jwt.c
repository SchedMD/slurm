/*****************************************************************************\
 *  jwt.c - Slurm REST auth JWT plugin
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
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

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/slurm_auth.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/http.h"
#include "src/slurmrestd/rest_auth.h"
#include "src/slurmrestd/xjson.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for Slurm node selection) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "REST auth/jwt";
const char plugin_type[] = "rest_auth/jwt";
const uint32_t plugin_id = 100;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

typedef struct {
	char *token;
} plugin_data_t;

extern int slurm_rest_auth_p_authenticate(on_http_request_args_t *args,
					  rest_auth_context_t *ctxt)
{
	plugin_data_t *data;
	const char *key, *user_name;

	key = find_http_header(args->headers, HTTP_HEADER_USER_TOKEN);
	user_name = find_http_header(args->headers, HTTP_HEADER_USER_TOKEN);

	if (!key && !user_name) {
		debug3("%s: [%s] skipping token authentication",
		       __func__, args->context->con->name);
		return ESLURM_AUTH_SKIP;
	}

	if (!key) {
		error("%s: [%s] missing header user token: %s",
		      __func__, args->context->con->name,
		      HTTP_HEADER_USER_TOKEN);
		return ESLURM_AUTH_CRED_INVALID;
	}
	if (!user_name) {
		error("%s: [%s] missing header user name: %s",
		      __func__, args->context->con->name,
		      HTTP_HEADER_USER_NAME);
		return ESLURM_AUTH_CRED_INVALID;
	}

	debug3("%s: [%s] attempting user_name %s token authentication",
	       __func__, args->context->con->name, user_name);

	xassert(!ctxt->user_name);
	xassert(!ctxt->plugin_data);
	xassert(!ctxt->plugin_id);

	ctxt->plugin_data = data = xmalloc(sizeof(*data));
	ctxt->user_name = xstrdup(user_name);
	data->token = xstrdup(key);

	return SLURM_SUCCESS;
}

extern int slurm_rest_auth_p_apply(rest_auth_context_t *context)
{
	plugin_data_t *data = context->plugin_data;

	xassert(context->plugin_id == plugin_id);

	return g_slurm_auth_thread_config(data->token, context->user_name);
}

extern void slurm_rest_auth_p_free(rest_auth_context_t *context)
{
	plugin_data_t *data = context->plugin_data;
	xassert(context->plugin_id == plugin_id);
	xfree(data->token);
	xfree(context->plugin_data);
}

extern void slurm_rest_auth_p_init(void)
{
	debug5("%s: REST JWT auth activated", __func__);
}

extern void slurm_rest_auth_p_fini(void)
{
	debug5("%s: REST JWT auth deactivated", __func__);
}
