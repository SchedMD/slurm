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

#define _GNU_SOURCE /* needed for SO_PEERCRED */

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/slurm_auth.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/rest_auth.h"
#include "src/slurmrestd/xjson.h"

/* only set by init_rest_auth() */
static rest_auth_type_t auth_type = AUTH_TYPE_INVALID;

#define MAGIC 0xDEDEDEDE
#define HTTP_HEADER_USER_TOKEN "X-SLURM-USER-TOKEN"
#define HTTP_HEADER_USER_NAME "X-SLURM-USER-NAME"

static void _check_magic(rest_auth_context_t *ctx)
{
	xassert(ctx);
	xassert(ctx->magic == MAGIC);
}

extern void destroy_rest_auth(void)
{
	auth_type = AUTH_TYPE_INVALID;
}

extern int init_rest_auth(rest_auth_type_t type)
{
	static volatile bool run_once = false;
	int rc = SLURM_SUCCESS;

	xassert(!run_once);
	if (run_once)
		return SLURM_ERROR;
	run_once = true;

	if (type == AUTH_TYPE_INVALID)
		fatal("%s: invalid authentication type requested", __func__);
	auth_type = type;

	if (type & AUTH_TYPE_LOCAL)
		debug3("%s: AUTH_TYPE_LOCAL activated", __func__);
	if (type & AUTH_TYPE_USER_PSK)
		debug3("%s: AUTH_TYPE_USER_PSK activated", __func__);

	return rc;
}

static void _clear_auth(rest_auth_context_t *ctxt)
{
	_check_magic(ctxt);

	ctxt->type = AUTH_TYPE_INVALID;
	xfree(ctxt->user_name);
	xfree(ctxt->token);

	rest_auth_context_clear();
}

static void _auth_local(on_http_request_args_t *args, rest_auth_context_t *ctxt)
{
	struct stat status = { 0 };
	uid_t uid = getuid();
	uid_t auth_uid = SLURM_AUTH_NOBODY;
	const char *header_user_name = find_http_header(args->headers,
							HTTP_HEADER_USER_NAME);

	const int input_fd = args->context->con->input_fd;
	const int output_fd = args->context->con->output_fd;
	const char *name = args->context->con->name;

	/* ensure user name is always NULL */
	xfree(ctxt->user_name);

	if (input_fd < 0 || output_fd < 0) {
		/* local auth requires there to be a valid fd */
		debug3("%s: rejecting auth local with invalid input_fd:%u output_fd:%u",
		       __func__, input_fd, output_fd);
		return _clear_auth(ctxt);
	}

	if (args->context->con->is_socket && !args->context->con->unix_socket) {
		/*
		 * SO_PEERCRED only works on unix sockets
		 */
		debug("%s: [%s] socket authentication only supported on UNIX sockets",
		      __func__, name);
		return _clear_auth(ctxt);
	} else if (args->context->con->is_socket &&
		   args->context->con->unix_socket) {
		struct ucred cred = { 0 };
		socklen_t len = sizeof(cred);

		if (getsockopt(input_fd, SOL_SOCKET, SO_PEERCRED, &cred,
			       &len) == -1) {
			/* socket may be remote, local auth doesn't apply */
			debug("%s: [%s] unable to get socket ownership: %m",
			      __func__, name);
			return _clear_auth(ctxt);
		}

		if (cred.uid == -1 || cred.gid == -1 || cred.pid == 0) {
			/* SO_PEERCRED failed silently */
			error("%s: [%s] rejecting socket connection with invalid SO_PEERCRED response",
			      __func__, name);
			return _clear_auth(ctxt);
		} else if (!cred.uid) {
			/* requesting socket is root */
			error("%s: [%s] accepted root socket connection with uid:%u gid:%u pid:%ld",
			      __func__, name, cred.uid, cred.gid,
			      (long) cred.pid);

			xfree(ctxt->user_name);
			ctxt->type |= AUTH_TYPE_LOCAL;
			/*
			 * root can be any user if they want - default to
			 * running user. This will be rejected with auth/jwt.
			 */
			if (header_user_name) {
				ctxt->user_name = xstrdup(header_user_name);
				auth_uid = cred.uid;
			} else
				auth_uid = uid;
		} else if (uid == cred.uid) {
			info("%s: [%s] accepted user socket connection with uid:%u gid:%u pid:%ld",
			     __func__, name, cred.uid, cred.gid,
			     (long) cred.pid);

			ctxt->type |= AUTH_TYPE_LOCAL;
			auth_uid = cred.uid;
		} else {
			/* another user -> REJECT */
			error("%s: [%s] rejecting socket connection with uid:%u gid:%u pid:%ld",
			      __func__, name, cred.uid, cred.gid,
			      (long) cred.pid);
			return _clear_auth(ctxt);
		}
	} else if (fstat(input_fd, &status)) {
		error("%s: [%s] unable to stat fd %d: %m",
		      __func__, name, input_fd);
		return _clear_auth(ctxt);
	} else if (S_ISCHR(status.st_mode) || S_ISFIFO(status.st_mode) ||
		   S_ISREG(status.st_mode)) {
		if (status.st_mode & (S_ISUID | S_ISGID)) {
			/* FIFO has sticky bits -> REJECT */
			error("%s: [%s] rejecting PIPE connection sticky bits permissions: %07o",
			      __func__, name, status.st_mode);
			return _clear_auth(ctxt);
		} else if (status.st_mode & S_IRWXO) {
			/* FIFO has other read/write -> REJECT */
			error("%s: [%s] rejecting PIPE connection other read or write bits permissions: %07o",
			      __func__, name, status.st_mode);
			return _clear_auth(ctxt);
		} else if (status.st_uid == uid) {
			/* FIFO is owned by same user */
			info("%s: [%s] accepted connection from uid:%u",
			     __func__, name, status.st_uid);

			ctxt->type |= AUTH_TYPE_LOCAL;
			auth_uid = status.st_uid;
		}
	} else {
		error("%s: [%s] rejecting unknown file type with mode:%07o blk:%u char:%u dir:%u fifo:%u reg:%u link:%u",
		      __func__, name, status.st_mode, S_ISBLK(status.st_mode),
		      S_ISCHR(status.st_mode), S_ISDIR(status.st_mode),
		      S_ISFIFO(status.st_mode), S_ISREG(status.st_mode),
		      S_ISLNK(status.st_mode));
		return _clear_auth(ctxt);
	}

	xassert(ctxt->type & AUTH_TYPE_LOCAL);

	if (!ctxt->user_name)
		ctxt->user_name = uid_to_string_or_null(auth_uid);

	xfree(ctxt->token);
	if (!ctxt->user_name) {
		error("%s: [%s] unable to lookup user_name for uid:%u",
		      __func__, args->context->con->name, auth_uid);
		return _clear_auth(ctxt);
	}
}

static void _auth_user_psk(on_http_request_args_t *args,
			   rest_auth_context_t *ctxt)
{
	const char *key = find_http_header(args->headers,
					   HTTP_HEADER_USER_TOKEN);
	const char *user_name = find_http_header(args->headers,
						 HTTP_HEADER_USER_NAME);

	_check_magic(ctxt);

	if (!key) {
		error("%s: [%s] missing header user token: %s",
		      __func__, args->context->con->name,
		      HTTP_HEADER_USER_TOKEN);
		return _clear_auth(ctxt);
	}
	if (!user_name) {
		error("%s: [%s] missing header user name: %s",
		      __func__, args->context->con->name,
		      HTTP_HEADER_USER_NAME);
		return _clear_auth(ctxt);
	}

	debug3("%s: [%s] attempting user_name %s token authentication",
	       __func__, args->context->con->name, user_name);

	xfree(ctxt->user_name);
	xfree(ctxt->token);

	ctxt->type |= AUTH_TYPE_USER_PSK;
	ctxt->user_name = xstrdup(user_name);
	ctxt->token = xstrdup(key);
}

extern int rest_authenticate_http_request(on_http_request_args_t *args)
{
	rest_auth_context_t *context =
		(rest_auth_context_t *) args->context->auth;
	_check_magic(context);

	if (!context) {
		context = rest_auth_context_new();
		args->context->auth = context;
	}

	/* continue if already authenticated */
	if (context->type)
		return SLURM_SUCCESS;

	/* favor PSK if it is provided */
	if (context->type == AUTH_TYPE_INVALID &&
	    (auth_type & AUTH_TYPE_USER_PSK))
		_auth_user_psk(args, context);

	if (context->type == AUTH_TYPE_INVALID &&
	    auth_type & AUTH_TYPE_LOCAL)
		_auth_local(args, context);

	if (context->type == AUTH_TYPE_INVALID) {
		g_slurm_auth_thread_clear();
		return ESLURM_AUTH_CRED_INVALID;
	}

	_check_magic(context);

	rest_auth_context_apply(context);

	return SLURM_SUCCESS;
}

extern rest_auth_context_t *rest_auth_context_new(void)
{
	rest_auth_context_t *context = xmalloc(sizeof(*context));

	context->magic = MAGIC;

	return context;
}

extern void rest_auth_context_apply(rest_auth_context_t *context)
{
	bool found = false;

	if (context->type == AUTH_TYPE_INVALID) {
		return rest_auth_context_clear();
	} else if (context->type == AUTH_TYPE_LOCAL) {
		found = true;
		g_slurm_auth_thread_config(NULL, context->user_name);
	} else if (context->type == AUTH_TYPE_USER_PSK) {
		found = true;
		g_slurm_auth_thread_config(context->token, context->user_name);
	}

	if (!found)
		fatal_abort("%s: invalid auth type to apply", __func__);
}

extern void rest_auth_context_clear(void)
{
	g_slurm_auth_thread_clear();
}

extern void rest_auth_context_free(rest_auth_context_t *context)
{
	if (!context)
		return;

	_clear_auth(context);

	context->magic = ~MAGIC;
	xfree(context);
}
