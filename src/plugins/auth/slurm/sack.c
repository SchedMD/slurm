/*****************************************************************************\
 *  sack.c - [S]lurm's [a]uth and [c]red [k]iosk
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

#include <jwt.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/sack_api.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/serializer.h"

#include "src/plugins/auth/slurm/auth_slurm.h"

#define SLURMCTLD_SACK_SOCKET "/run/slurmctld/sack.socket"
#define SLURMDBD_SACK_SOCKET "/run/slurmdbd/sack.socket"
#define SLURM_SACK_SOCKET "/run/slurm/sack.socket"
#define SACK_RECONFIG_ENV "SACK_RECONFIG_FD"

static int sack_fd = -1;

/*
 * Loosely inspired by MUNGE.
 *
 * Listen on a UNIX socket for connections. Use SO_PEERCRED to establish the
 * identify of the connecting process, and generate a credential from their
 * requested payload.
 */

static void _prepare_run_dir(const char *subdir)
{
	int dirfd, subdirfd;
	struct stat statbuf;

	if ((dirfd = open("/run", O_DIRECTORY | O_NOFOLLOW)) < 0)
		fatal("%s: could not open /run", __func__);

	if ((subdirfd = openat(dirfd, subdir,
			       (O_DIRECTORY | O_NOFOLLOW))) < 0) {
		/* just assume ENOENT and attempt to create */
		if (mkdirat(dirfd, subdir, 0755) < 0)
			fatal("%s: failed to create /run/%s", __func__, subdir);
		if (fchownat(dirfd, subdir, slurm_conf.slurm_user_id, -1,
			     AT_SYMLINK_NOFOLLOW) < 0)
			fatal("%s: failed to change ownership of /run/%s to SlurmUser",
			      __func__, subdir);
		close(dirfd);
		return;
	}

	if (!fstat(subdirfd, &statbuf)) {
		if (!(statbuf.st_mode & S_IFDIR))
			fatal("%s: /run/%s exists but is not a directory",
			      __func__, subdir);
		if (statbuf.st_uid != slurm_conf.slurm_user_id) {
			if (statbuf.st_uid)
				fatal("%s: /run/%s exists but is owned by %u",
				      __func__, subdir, statbuf.st_uid);
			warning("%s: /run/%s exists but is owned by root, not SlurmUser",
				__func__, subdir);
		}
	}

	if (unlinkat(subdirfd, "sack.socket", 0) && (errno != ENOENT))
		fatal("%s: failed to remove /run/%s/sack.socket",
		      __func__, subdir);

	close(subdirfd);
	close(dirfd);
}

static int _sack_create(conmgr_fd_t *con, buf_t *in)
{
	uid_t r_uid = SLURM_AUTH_NOBODY;
	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;
	pid_t pid = 0;
	char *data = NULL;
	uint32_t dlen = 0;
	char *extra = NULL, *token = NULL;
	buf_t *out = init_buf(1024);

	if (conmgr_get_fd_auth_creds(con, &uid, &gid, &pid)) {
		error("%s: conmgr_get_fd_auth_creds() failed", __func__);
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
	conmgr_fd_xfer_out_buffer(con, out);

	FREE_NULL_BUFFER(out);
	xfree(data);
	xfree(extra);
	xfree(token);
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_BUFFER(out);
	xfree(extra);
	xfree(token);
	return SLURM_ERROR;
}

static int _sack_verify(conmgr_fd_t *con, buf_t *in)
{
	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;
	pid_t pid = 0;
	uint32_t rc = SLURM_ERROR;
	auth_cred_t *cred = new_cred();

	safe_unpackstr(&cred->token, in);

	if (conmgr_get_fd_auth_creds(con, &uid, &gid, &pid)) {
		error("%s: conmgr_get_fd_auth_creds() failed", __func__);
		goto unpack_error;
	}

	rc = htonl(verify_internal(cred, uid));
	conmgr_queue_write_data(con, &rc, sizeof(uint32_t));

	FREE_NULL_CRED(cred);
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_CRED(cred);
	return SLURM_ERROR;
}

static int _on_connection_data(conmgr_fd_t *con, void *arg)
{
	int rc = SLURM_ERROR;
	uint16_t version = 0;
	uint32_t length = 0, rpc = 0;
	buf_t *in = NULL;

	log_flag(SACK, "%s", conmgr_fd_get_name(con));

	if (!(in = conmgr_fd_shadow_in_buffer(con))) {
		log_flag(SACK, "conmgr_fd_shadow_in_buffer() failed");
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
	conmgr_fd_mark_consumed_in_buffer(con, length);

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
	conmgr_queue_close_fd(con);
	FREE_NULL_BUFFER(in);

	return rc;
}

extern void init_sack_conmgr(void)
{
	conmgr_callbacks_t callbacks = {NULL, NULL};
	static const conmgr_events_t events = {
		.on_data = _on_connection_data,
	};
	int rc;
	const char *path = NULL;
	const char *env_fd = NULL;

	conmgr_init(0, 0, callbacks);

	if (sack_fd >= 0) {
		/* already have the FD -> do nothing */
	} else if ((env_fd = getenv(SACK_RECONFIG_ENV))) {
		if ((sack_fd = atoi(env_fd)) < 0)
			fatal("%s: Invalid %s=%s environment variable",
			      __func__, SACK_RECONFIG_ENV, env_fd);
	} else {
		slurm_addr_t addr = {0};
		mode_t mask;

		if (running_in_slurmctld()) {
			_prepare_run_dir("slurmctld");
			path = SLURMCTLD_SACK_SOCKET;
		} else if (running_in_slurmdbd()) {
			_prepare_run_dir("slurmdbd");
			path = SLURMDBD_SACK_SOCKET;
		} else {
			_prepare_run_dir("slurm");
			path = SLURM_SACK_SOCKET;
		}

		if ((addr = sockaddr_from_unix_path(path)).ss_family != AF_UNIX)
			fatal("%s: Unexpected invalid socket address",
			      __func__);

		if ((sack_fd = socket(AF_UNIX, (SOCK_STREAM | SOCK_CLOEXEC), 0))
		     < 0)
			fatal("%s: socket() failed: %m", __func__);

		/* set value of socket path */
		mask = umask(0);

		/* bind() will EINVAL if socklen=sizeof(addr) */
		if ((rc = bind(sack_fd, (const struct sockaddr *) &addr,
			       sizeof(struct sockaddr_un))))
			fatal("%s: [%pA] Unable to bind UNIX socket: %m",
			      __func__, &addr);
		umask(mask);

		fd_set_oob(sack_fd, 0);

		if ((rc = listen(sack_fd, SLURM_DEFAULT_LISTEN_BACKLOG)))
			fatal("%s: [%pA] unable to listen(): %m",
			      __func__, &addr);
	}

	if ((rc = conmgr_process_fd_listen(sack_fd, CON_TYPE_RAW, &events,
					   CON_FLAG_NONE, NULL)))
		fatal("%s: [fd:%d] conmgr rejected socket: %s",
		      __func__, sack_fd, slurm_strerror(rc));

	/*
	 * We do not need to call conmgr_run() here since only the daemons
	 * get here, and all the daemons call conmgr_run() separately.
	 */

	/* Prepare for reconfigure */
	setenvfs("%s=%d", SACK_RECONFIG_ENV, sack_fd);
	fd_set_noclose_on_exec(sack_fd);
}

extern int auth_p_get_reconfig_fd(void)
{
	return sack_fd;
}
