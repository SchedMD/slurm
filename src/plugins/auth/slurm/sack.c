/*****************************************************************************\
 *  sack.c - [S]lurm's [a]uth and [c]red [k]iosk
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <jwt.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/conmgr.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/sack_api.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/serializer.h"

#include "src/plugins/auth/slurm/auth_slurm.h"

/*
 * Loosely inspired by MUNGE.
 *
 * Listen on a UNIX socket for connections. Use SO_PEERCRED to establish the
 * identify of the connecting process, and generate a credential from their
 * requested payload.
 */

static void _prepare_run_dir(void)
{
	int dirfd, slurmdirfd;
	struct stat statbuf;

	if ((dirfd = open("/run", O_DIRECTORY | O_NOFOLLOW)) < 0)
		fatal("%s: could not open /run", __func__);

	if ((slurmdirfd = openat(dirfd, "slurm",
				 (O_DIRECTORY | O_NOFOLLOW))) < 0) {
		/* just assume ENOENT and attempt to create */
		if (mkdirat(dirfd, "slurm", 0755) < 0)
			fatal("%s: failed to create /run/slurm", __func__);
		if (fchownat(dirfd, "slurm", slurm_conf.slurm_user_id, -1,
			     AT_SYMLINK_NOFOLLOW) < 0)
			fatal("%s: failed to change ownership of /run/slurm to SlurmUser",
			      __func__);
		close(dirfd);
		return;
	}

	if (!fstat(slurmdirfd, &statbuf)) {
		if (!(statbuf.st_mode & S_IFDIR))
			fatal("%s: /run/slurm exists but is not a directory",
			      __func__);
		if (statbuf.st_uid != slurm_conf.slurm_user_id) {
			if (statbuf.st_uid)
				fatal("%s: /run/slurm exists but is owned by %u",
				      __func__, statbuf.st_uid);
			warning("%s: /run/slurm exists but is owned by root, not SlurmUser",
				__func__);
		}
	}

	if (unlinkat(slurmdirfd, "sack.socket", 0) && (errno != ENOENT))
		fatal("%s: failed to remove /run/slurm/sack.socket", __func__);

	close(slurmdirfd);
	close(dirfd);
}

static int _sack_create(con_mgr_fd_t *con, buf_t *in)
{
	uid_t r_uid = SLURM_AUTH_NOBODY;
	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;
	pid_t pid = 0;
	char *data = NULL;
	uint32_t dlen = 0;
	char *extra = NULL, *token = NULL;
	buf_t *out = init_buf(1024);

	if (con_mgr_get_fd_auth_creds(con, &uid, &gid, &pid)) {
		error("%s: con_mgr_get_fd_auth_creds() failed", __func__);
		goto unpack_error;
	}

	safe_unpack32(&r_uid, in);
	safe_unpackmem_xmalloc(&data, &dlen, in);

	/*
	 * Feed identity info to slurmctld when required.
	 * Only the "sack" type should provide this for auth tokens.
	 * Internal communication between system components should be as
	 * root/SlurmUser, who must already exist on all nodes.
	 */
	if (use_client_ids)
		extra = get_identity_string(NULL, uid, gid);

	if (!(token = create_internal("sack", uid, gid, r_uid,
				      data, dlen, extra))) {
		error("create_internal() failed: %m");
		goto unpack_error;
	}

	packstr(token, out);
	con_mgr_fd_xfer_out_buffer(con, out);

	FREE_NULL_BUFFER(out);
	xfree(extra);
	xfree(token);
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_BUFFER(out);
	xfree(extra);
	xfree(token);
	return SLURM_ERROR;
}

static int _sack_verify(con_mgr_fd_t *con, buf_t *in)
{
	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;
	pid_t pid = 0;
	uint32_t rc = SLURM_ERROR;
	auth_cred_t *cred = new_cred();

	safe_unpackstr(&cred->token, in);

	if (con_mgr_get_fd_auth_creds(con, &uid, &gid, &pid)) {
		error("%s: con_mgr_get_fd_auth_creds() failed", __func__);
		goto unpack_error;
	}

	rc = htonl(verify_internal(cred, uid));
	con_mgr_queue_write_fd(con, &rc, sizeof(uint32_t));

	FREE_NULL_CRED(cred);
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_CRED(cred);
	return SLURM_ERROR;
}

static int _on_connection_data(con_mgr_fd_t *con, void *arg)
{
	int rc = SLURM_ERROR;
	uint16_t version = 0;
	uint32_t length = 0, rpc = 0;
	buf_t *in = NULL;

	log_flag(SACK, "%s", con_mgr_fd_get_name(con));

	if (!(in = con_mgr_fd_shadow_in_buffer(con))) {
		log_flag(SACK, "con_mgr_fd_shadow_in_buffer() failed");
		goto unpack_error;
	}

	if (size_buf(in) < SACK_HEADER_LENGTH) {
		log_flag(SACK, "incomplete header, only %u bytes available, try again",
			 size_buf(in));
		FREE_NULL_BUFFER(in);
		return SLURM_SUCCESS;
	}

	safe_unpack16(&version, in);
	safe_unpack32(&length, in);
	safe_unpack32(&rpc, in);

	/*
	 * The version is not included in length, so correct that here.
	 * This is in anticipation of splitting the version handling away
	 * from the RPC handling at some point in the future, and allowing
	 * one connection to process multiple RPCs.
	 */
	if (size_buf(in) < (length + sizeof(uint16_t))) {
		log_flag(SACK, "incomplete message, only %u bytes available of %u bytes",
			 size_buf(in), length);
		FREE_NULL_BUFFER(in);
		return SLURM_SUCCESS;
	}
	con_mgr_fd_mark_consumed_in_buffer(con, length);

	log_flag(SACK, "received version=%hu rpc=%u", version, rpc);

	switch (rpc) {
	case SACK_CREATE:
		rc = _sack_create(con, in);
		break;
	case SACK_VERIFY:
		rc = _sack_verify(con, in);
		break;
	default:
		error("%s: unexpected rpc=%u", __func__, rpc);
		break;
	}

unpack_error:
	con_mgr_queue_close_fd(con);
	FREE_NULL_BUFFER(in);

	return rc;
}

extern void init_sack_conmgr(void)
{
	con_mgr_callbacks_t callbacks = {NULL, NULL};
	con_mgr_events_t events = {
		.on_data = _on_connection_data,
	};
	int fd;
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = "/run/slurm/sack.socket",
	};
	int rc;

	_prepare_run_dir();

	init_con_mgr(0, 0, callbacks);

	if ((fd = socket(AF_UNIX, (SOCK_STREAM | SOCK_CLOEXEC), 0)) < 0)
		fatal("%s: socket() failed: %m", __func__);

	/* set value of socket path */
	if ((rc = bind(fd, (const struct sockaddr *) &addr,
		       sizeof(addr))))
		fatal("%s: [%s] Unable to bind UNIX socket: %m",
		      __func__, addr.sun_path);

	if (lchmod(addr.sun_path, 0777) < 0)
		fatal("%s: failed to chmod() `%s`: %m",
		      __func__, addr.sun_path);

	fd_set_oob(fd, 0);

	if ((rc = listen(fd, SLURM_DEFAULT_LISTEN_BACKLOG)))
		fatal("%s: [%s] unable to listen(): %m",
		      __func__, addr.sun_path);

	if ((rc = con_mgr_process_fd_unix_listen(CON_TYPE_RAW, fd, events,
						 (const slurm_addr_t *) &addr,
						 sizeof(addr), addr.sun_path,
						 NULL)))
		fatal("%s: conmgr refused fd %d: %s",
		      __func__, fd, slurm_strerror(rc));

	if ((rc = con_mgr_run(false)))
		fatal("%s: conmgr run failed: %s",
		      __func__, slurm_strerror(rc));
}

extern void fini_sack_conmgr(void)
{
	/*
	 * Do not attempt to remove /run/slurm/sack.socket.
	 * If multiple daemons are co-located on this node, we may no
	 * longer be the one that owns that socket, and removing it
	 * would prevent the current owner from responding.
	 */

	free_con_mgr();
}
