/*****************************************************************************\
 *  rest_auth.h - definitions for handling http authentication
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

#ifndef SLURMRESTD_AUTH_H
#define SLURMRESTD_AUTH_H

#include <sys/types.h>

#include "src/common/data.h"
#include "src/common/plugin.h"
#include "src/slurmrestd/http.h"

#define HTTP_HEADER_USER_TOKEN "X-SLURM-USER-TOKEN"
#define HTTP_HEADER_AUTH "Authorization"
#define HTTP_HEADER_AUTH_BEARER "Bearer "
#define HTTP_HEADER_USER_NAME "X-SLURM-USER-NAME"

typedef struct {
	int magic;
	uint32_t plugin_id;
	/* optional user supplied user name */
	char *user_name;
	void *plugin_data;
} rest_auth_context_t;

/*
 * Create new auth context.
 * Must free with rest_auth_g_free().
 * RET ptr to auth context
 */
extern rest_auth_context_t *rest_auth_g_new(void);

/*
 * Release auth context
 * IN context - ptr to context
 */
extern void rest_auth_g_free(rest_auth_context_t *context);

/*
 * Attempt to authenticate HTTP request
 * IN/OUT args - HTTP request
 * 	sets instructions in args
 * RET SLURM_SUCCESS or error
 */
extern int rest_authenticate_http_request(on_http_request_args_t *args);

/*
 * Apply current auth context to thread
 * IN context - security context to apply
 * RET SLURM_SUCCESS or error
 */
extern int rest_auth_g_apply(rest_auth_context_t *context);

/*
 * Retrieve db_conn for slurmdbd calls.
 * WARNING: pointer will be invalidated by next call to rest_auth_g_free()
 * RET NULL on error or db_conn pointer
 */
extern void *rest_auth_g_get_db_conn(rest_auth_context_t *context);

#define FREE_NULL_REST_AUTH(_X)			\
	do {					\
		if (_X)				\
			rest_auth_g_free(_X);	\
		_X = NULL;			\
	} while (0)

/*
 * Setup locks and register REST authentication plugins.
 * 	Only call once!
 * IN become_user - notify auth plugin user requests become user mode
 * IN plugin_handles - array of rest_plugins to init
 * IN plugin_count - number of plugins in plugin_handles array
 */
extern int init_rest_auth(bool become_user,
			  const plugin_handle_t *plugin_handles,
			  const size_t plugin_count);

/*
 * Cleanup rest auth
 */
extern void destroy_rest_auth(void);

#endif /* SLURMRESTD_AUTH_H */
