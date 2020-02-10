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
#include "src/slurmrestd/http.h"

/*
 * Bitmap of auth types that will be currently accepted
 */
typedef enum {
	AUTH_TYPE_INVALID = 0,
	/*
	 * No auth required (only for inetd mode).
	 * Auth via owner of pipe or socket.
	 */
	AUTH_TYPE_LOCAL = 1 << 0,
	/* preshared key per UID */
	AUTH_TYPE_USER_PSK = 1 << 1,
} rest_auth_type_t;

typedef struct {
	int magic;

	/*
	 * auth type of this connection.
	 * only a single bit should ever be set
	 * or none if auth failed.
	 */
	rest_auth_type_t type;

	/* user supplied user name */
	char *user_name;
	/* user supplied token (may be null) */
	char *token;
} rest_auth_context_t;

/*
 * Create new auth context.
 * Must free with rest_auth_context_free().
 * RET ptr to auth context
 */
extern rest_auth_context_t *rest_auth_context_new(void);

/*
 * Release auth context
 * IN context - ptr to context
 */
extern void rest_auth_context_free(rest_auth_context_t *context);

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
 * will fatal on error
 */
extern void rest_auth_context_apply(rest_auth_context_t *context);

/*
 * Clear current auth context
 * will fatal on error
 */
extern void rest_auth_context_clear(void);

/*
 * Setup locks and register openapi.
 * 	Only call once!
 * IN type auth type to enforce
 */
extern int init_rest_auth(rest_auth_type_t type);

/*
 * Cleanup rest auth
 */
extern void destroy_rest_auth(void);

#endif /* SLURMRESTD_AUTH_H */
