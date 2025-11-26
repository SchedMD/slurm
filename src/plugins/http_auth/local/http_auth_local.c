/*****************************************************************************\
 *  http_auth_local.c - Slurm HTTP auth local plugin
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

#include "config.h"

#include <stdint.h>

#include "src/common/http_con.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"

#include "slurm/slurm_errno.h"

#include "src/interfaces/http_auth.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "HTTP local socket authentication";
const char plugin_type[] = "http_auth/local";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Required for http_auth plugins: */
const uint32_t plugin_id = HTTP_AUTH_PLUGIN_LOCAL;

extern int http_auth_p_init(void)
{
	return SLURM_SUCCESS;
}

extern void http_auth_p_fini(void)
{
	/* do nothing */
}

static int _auth_fd(uid_t *uid_ptr, http_con_t *hcon, const char *name,
		    const http_con_request_t *request)
{
	struct stat status = { 0 };
	int rc = EINVAL;

	if ((rc = http_con_fstat_input(hcon, &status))) {
		error("%s: [%s] fstat() failed: %s",
		      __func__, name, slurm_strerror(rc));
		return rc;
	}

	if (!S_ISCHR(status.st_mode) && !S_ISFIFO(status.st_mode) &&
	    !S_ISREG(status.st_mode)) {
		error("%s: [%s] skipping %s for unknown file type with mode:%07o blk:%u char:%u dir:%u fifo:%u reg:%u link:%u",
		      __func__, name, plugin_type, status.st_mode,
		      S_ISBLK(status.st_mode), S_ISCHR(status.st_mode),
		      S_ISDIR(status.st_mode), S_ISFIFO(status.st_mode),
		      S_ISREG(status.st_mode), S_ISLNK(status.st_mode));
		return ESLURM_AUTH_SKIP;
	}

	if (status.st_mode & (S_ISUID | S_ISGID)) {
		/* FIFO has sticky bits */
		error("%s: [%s] skipping PIPE connection sticky bits permissions: %07o",
		      __func__, name, status.st_mode);
		return ESLURM_AUTH_SKIP;
	}

	if (status.st_mode & S_IRWXO) {
		/* FIFO has other read/write */
		error("%s: [%s] skipping %s PIPE connection other read or write bits permissions: %07o",
		      __func__, name, plugin_type, status.st_mode);
		return ESLURM_AUTH_SKIP;
	}

	if (status.st_uid == SLURM_AUTH_NOBODY) {
		error("%s: [%s] rejecting file owned by nobody", __func__, name);
		return ESLURM_AUTH_CRED_INVALID;
	}

	if (uid_ptr)
		*uid_ptr = status.st_uid;

	info("[%s] authenticated %s connection via kernel for user=%s(%u)",
	     name, plugin_type, uid_to_string_cached(status.st_uid),
	     status.st_uid);

	return SLURM_SUCCESS;
}

static int _auth_socket(uid_t *uid_ptr, http_con_t *hcon, const char *name,
			const http_con_request_t *request)
{
	int rc = EINVAL;
	uid_t cred_uid = SLURM_AUTH_NOBODY;
	gid_t cred_gid = SLURM_AUTH_NOBODY;
	pid_t cred_pid = 0;

	if ((rc = http_con_get_auth_creds(hcon, &cred_uid, &cred_gid,
					  &cred_pid))) {
		debug("%s: [%s] unable to get socket ownership: %s",
		      __func__, name, slurm_strerror(rc));
		return rc;
	}

	if (cred_uid == SLURM_AUTH_NOBODY) {
		rc = ESLURM_AUTH_NOBODY;
		info("%s: [%s] rejecting authenticated socket connection via kernel with uid:%u gid:%u pid:%ld",
		     __func__, name, cred_uid, cred_gid, (long) cred_pid);
	} else {
		rc = SLURM_SUCCESS;
		info("%s: [%s] authenticated socket connection via kernel with uid:%u gid:%u pid:%ld",
		     __func__, name, cred_uid, cred_gid, (long) cred_pid);
	}

	if (uid_ptr)
		*uid_ptr = cred_uid;

	return rc;
}

extern int http_auth_p_authenticate(uid_t *uid_ptr, http_con_t *hcon,
				    const char *name,
				    const http_con_request_t *request)
{
	int rc = EINVAL;
	conmgr_fd_status_t status = { 0 };

	if (uid_ptr)
		*uid_ptr = SLURM_AUTH_NOBODY;

	if ((rc = http_con_get_status(hcon, &status))) {
		debug3("%s: [%s] invalid connection status: %s",
		       __func__, name, slurm_strerror(rc));
		return rc;
	}

	if (status.is_socket) {
		if (!status.unix_socket) {
			/* SO_PEERCRED only works on unix sockets */
			debug3("%s: [%s] skipping %s due to AF_UNIX socket required",
			       __func__, name, plugin_type);
			return ESLURM_AUTH_SKIP;
		}

		return _auth_socket(uid_ptr, hcon, name, request);
	}

	return _auth_fd(uid_ptr, hcon, name, request);
}

extern int http_auth_p_proxy_token(http_con_t *hcon, const char *name,
				   const http_con_request_t *request)
{
	int rc = EINVAL;
	uid_t uid = SLURM_AUTH_NOBODY;

	/* Local authentication must never authenticate as nobody */
	if (geteuid() == SLURM_AUTH_NOBODY) {
		debug3("%s: [%s] skipping %s while running as nobody",
		       __func__, name, plugin_type);
		return ESLURM_AUTH_SKIP;
	}

	if ((rc = http_auth_p_authenticate(&uid, hcon, name, request)))
		return rc;

	if (geteuid() != uid) {
		debug3("%s: [%s] skipping %s due to non-matching connection user uid=%u while process euid=%u",
		       __func__, name, plugin_type, uid, geteuid());
		return ESLURM_AUTH_SKIP;
	}

	return SLURM_SUCCESS;
}
