/*****************************************************************************\
 *  http_auth.h - HTTP authentication plugin interface
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

#ifndef _INTERFACES_HTTP_AUTH_H
#define _INTERFACES_HTTP_AUTH_H

#include "slurm/slurm.h"

#include "src/common/http_con.h"
#include "src/common/plugrack.h"

typedef enum {
	HTTP_AUTH_PLUGIN_INVALID = 0,
	HTTP_AUTH_PLUGIN_JWT = 100,
	HTTP_AUTH_PLUGIN_LOCAL = 101,
	HTTP_AUTH_PLUGIN_INVALID_MAX,
	HTTP_AUTH_PLUGIN_ANY = INFINITE16,
} http_auth_plugin_id_t;

extern int http_auth_g_init(const char *plugin_type, plugrack_foreach_t listf);
extern void http_auth_g_fini(void);

/*
 * Authenticate HTTP connection
 * IN plugin_id - Authenticate using given plugin or HTTP_AUTH_PLUGIN_ANY for
 *	any matching plugin
 * IN uid_ptr - Pointer to populate when authenticated to given UID or set to
 *	SLURM_AUTH_NOBODY on failure
 * IN name - Connection name for logging
 * IN request - HTTP connection request state
 * RET SLURM_SUCCESS on successful authentication or error
 */
extern int http_auth_g_authenticate(http_auth_plugin_id_t plugin_id,
				    uid_t *uid_ptr, http_con_t *hcon,
				    const char *name,
				    const http_con_request_t *request);

/*
 * Get authentication proxy token from HTTP connection for this thread
 * WARNING: authentication may not be verified!
 * IN plugin_id -
 *	HTTP_AUTH_PLUGIN_*: Authenticate using given plugin
 *	HTTP_AUTH_PLUGIN_ANY: any matching plugin
 *	HTTP_AUTH_PLUGIN_INVALID: Remove proxy authentication from this thread
 * IN name - Connection name for logging
 * IN request - HTTP connection request state
 * RET SLURM_SUCCESS on successful authentication or error
 */
extern int http_auth_g_proxy_token(http_auth_plugin_id_t plugin_id,
				   http_con_t *hcon, const char *name,
				   const http_con_request_t *request);

#endif
