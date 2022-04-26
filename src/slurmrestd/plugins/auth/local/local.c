/*****************************************************************************\
 *  local.c - Slurm REST auth local plugin
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

#define _GNU_SOURCE /* needed for SO_PEERCRED */

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/slurm_auth.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/rest_auth.h"

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
const char plugin_name[] = "REST auth/local";
const char plugin_type[] = "rest_auth/local";
const uint32_t plugin_id = 101;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

extern int slurm_rest_auth_p_apply(rest_auth_context_t *context);

#define MAGIC 0xd11abee2
typedef struct {
	int magic;
	void *db_conn;
} plugin_data_t;

extern void *slurm_rest_auth_p_get_db_conn(rest_auth_context_t *context)
{
	plugin_data_t *data = context->plugin_data;
	xassert(data->magic == MAGIC);
	xassert(context->plugin_id == plugin_id);

	if (slurm_rest_auth_p_apply(context))
		return NULL;

	if (data->db_conn)
		return data->db_conn;

	errno = 0;
	data->db_conn = slurmdb_connection_get(NULL);

	if (!errno && data->db_conn)
		return data->db_conn;

	error("%s: unable to connect to slurmdbd: %m",
	      __func__);
	data->db_conn = NULL;

	return NULL;
}

static int _auth_socket(on_http_request_args_t *args,
			rest_auth_context_t *ctxt,
			const char *header_user_name)
{
	struct ucred cred = { 0 };
	socklen_t len = sizeof(cred);
	const int input_fd = args->context->con->input_fd;
	const char *name = args->context->con->name;

	xassert(!ctxt->user_name);

	if (getsockopt(input_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == -1) {
		/* socket may be remote, local auth doesn't apply */
		debug("%s: [%s] unable to get socket ownership: %m",
		      __func__, name);
		return ESLURM_AUTH_CRED_INVALID;
	}

	if ((cred.uid == -1) || (cred.gid == -1) || (cred.pid == 0)) {
		/* SO_PEERCRED failed silently */
		error("%s: [%s] rejecting socket connection with invalid SO_PEERCRED response",
		      __func__, name);
		return ESLURM_AUTH_CRED_INVALID;
	} else if (cred.uid == 0) {
		/* requesting socket is root */
		error("%s: [%s] accepted root socket connection with uid:%u gid:%u pid:%ld",
		      __func__, name, cred.uid, cred.gid,
		      (long) cred.pid);

		/*
		 * root can be any user if they want - default to
		 * running user.
		 */
		if (header_user_name)
			ctxt->user_name = xstrdup(header_user_name);
		else
			ctxt->user_name = uid_to_string_or_null(getuid());
	} else if (getuid() == cred.uid) {
		info("%s: [%s] accepted user socket connection with uid:%u gid:%u pid:%ld",
		     __func__, name, cred.uid, cred.gid,
		     (long) cred.pid);

		ctxt->user_name = uid_to_string_or_null(cred.uid);
	} else {
		/* another user -> REJECT */
		error("%s: [%s] rejecting socket connection with uid:%u gid:%u pid:%ld",
		      __func__, name, cred.uid, cred.gid,
		      (long) cred.pid);
		return ESLURM_AUTH_CRED_INVALID;
	}

	if (ctxt->user_name) {
		plugin_data_t *data = xmalloc(sizeof(*data));
		data->magic = MAGIC;
		ctxt->plugin_data = data;
		return SLURM_SUCCESS;
	} else
		return ESLURM_USER_ID_MISSING;
}

extern int slurm_rest_auth_p_authenticate(on_http_request_args_t *args,
					  rest_auth_context_t *ctxt)
{
	struct stat status = { 0 };
	const char *header_user_name = find_http_header(args->headers,
							HTTP_HEADER_USER_NAME);

	const int input_fd = args->context->con->input_fd;
	const int output_fd = args->context->con->output_fd;
	const char *name = args->context->con->name;

	xassert(!ctxt->user_name);

	if ((input_fd < 0) || (output_fd < 0)) {
		/* local auth requires there to be a valid fd */
		debug3("%s: skipping auth local with invalid input_fd:%u output_fd:%u",
		       __func__, input_fd, output_fd);
		return ESLURM_AUTH_SKIP;
	}

	if (args->context->con->is_socket && !args->context->con->unix_socket) {
		/*
		 * SO_PEERCRED only works on unix sockets
		 */
		debug("%s: [%s] socket authentication only supported on UNIX sockets",
		      __func__, name);
		return ESLURM_AUTH_SKIP;
	} else if (args->context->con->is_socket &&
		   args->context->con->unix_socket) {
		return _auth_socket(args, ctxt, header_user_name);
	} else if (fstat(input_fd, &status)) {
		error("%s: [%s] unable to stat fd %d: %m",
		      __func__, name, input_fd);
		return ESLURM_AUTH_CRED_INVALID;
	} else if (S_ISCHR(status.st_mode) || S_ISFIFO(status.st_mode) ||
		   S_ISREG(status.st_mode)) {
		if (status.st_mode & (S_ISUID | S_ISGID)) {
			/* FIFO has sticky bits -> REJECT */
			error("%s: [%s] rejecting PIPE connection sticky bits permissions: %07o",
			      __func__, name, status.st_mode);
			return ESLURM_AUTH_CRED_INVALID;
		} else if (status.st_mode & S_IRWXO) {
			/* FIFO has other read/write -> REJECT */
			error("%s: [%s] rejecting PIPE connection other read or write bits permissions: %07o",
			      __func__, name, status.st_mode);
			return ESLURM_AUTH_CRED_INVALID;
		} else if (status.st_uid == getuid()) {
			/* FIFO is owned by same user */
			ctxt->user_name = uid_to_string_or_null(status.st_uid);

			if (ctxt->user_name) {
				plugin_data_t *data = xmalloc(sizeof(*data));
				data->magic = MAGIC;
				ctxt->plugin_data = data;

				info("[%s] accepted connection from user: %s[%u]",
				     name, ctxt->user_name, status.st_uid);
				return SLURM_SUCCESS;
			} else {
				error("[%s] rejecting connection from unresolvable uid:%u",
				      name, status.st_uid);
				return ESLURM_USER_ID_MISSING;
			}
		}

		return ESLURM_AUTH_CRED_INVALID;
	} else {
		error("%s: [%s] rejecting unknown file type with mode:%07o blk:%u char:%u dir:%u fifo:%u reg:%u link:%u",
		      __func__, name, status.st_mode, S_ISBLK(status.st_mode),
		      S_ISCHR(status.st_mode), S_ISDIR(status.st_mode),
		      S_ISFIFO(status.st_mode), S_ISREG(status.st_mode),
		      S_ISLNK(status.st_mode));
		return ESLURM_AUTH_CRED_INVALID;
	}
}

extern int slurm_rest_auth_p_apply(rest_auth_context_t *context)
{
	int rc;
	char *user = uid_to_string_or_null(getuid());

	xassert(((plugin_data_t *) context->plugin_data)->magic == MAGIC);
	xassert(context->plugin_id == plugin_id);

	rc = auth_g_thread_config(NULL, context->user_name);

	xfree(user);

	return rc;
}

extern void slurm_rest_auth_p_free(rest_auth_context_t *context)
{
	plugin_data_t *data = context->plugin_data;
	xassert(data->magic == MAGIC);
	xassert(context->plugin_id == plugin_id);
	data->magic = ~MAGIC;

	if (data->db_conn)
		slurmdb_connection_close(&data->db_conn);

	xfree(context->plugin_data);
}

extern void slurm_rest_auth_p_init(void)
{
	debug5("%s: REST local auth activated", __func__);
}

extern void slurm_rest_auth_p_fini(void)
{
	debug5("%s: REST local auth deactivated", __func__);
}
