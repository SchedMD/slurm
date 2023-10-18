/*****************************************************************************\
 *  cred_munge.c - Munge based credential signature plugin
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <inttypes.h>
#include <munge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/cred.h"
#include "src/plugins/cred/common/cred_common.h"

#define RETRY_COUNT		20
#define RETRY_USEC		100000

#if defined (__APPLE__)
extern slurm_conf_t slurm_conf __attribute__((weak_import));
#else
slurm_conf_t slurm_conf;
#endif

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
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]	= "Munge credential signature plugin";
const char plugin_type[]	= "cred/munge";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*
 *  Error codes local to this plugin:
 */
enum local_error_code {
	ESIG_BUF_DATA_MISMATCH = 5000,
	ESIG_BUF_SIZE_MISMATCH,
	ESIG_BAD_USERID,
	ESIG_CRED_REPLAYED,
};

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded,
 * free any global memory allocations here to avoid memory leaks.
 */
extern int fini(void)
{
	verbose("%s unloaded", plugin_name);
	return SLURM_SUCCESS;
}

static munge_ctx_t _munge_ctx_create(void)
{
	static int auth_ttl = 0;
	munge_ctx_t ctx;
	char *socket;
	int rc;

	if (!auth_ttl)
		auth_ttl = slurm_get_auth_ttl();

	if ((ctx = munge_ctx_create()) == NULL) {
		error("%s: munge_ctx_create failed", __func__);
		return NULL;
	}

	socket = slurm_auth_opts_to_socket(slurm_conf.authinfo);
	if (socket) {
		rc = munge_ctx_set(ctx, MUNGE_OPT_SOCKET, socket);
		xfree(socket);
		if (rc != EMUNGE_SUCCESS) {
			error("Failed to set MUNGE socket: %s",
			      munge_ctx_strerror(ctx));
			munge_ctx_destroy(ctx);
			return NULL;
		}
	}

	if (auth_ttl) {
		rc = munge_ctx_set(ctx, MUNGE_OPT_TTL, auth_ttl);
		if (rc != EMUNGE_SUCCESS) {
			error("Failed to set MUNGE ttl: %s",
			      munge_ctx_strerror(ctx));
			munge_ctx_destroy(ctx);
			return NULL;
		}
	}

	return ctx;
}

extern const char *cred_p_str_error(int errnum)
{
	if (errnum == ESIG_BUF_DATA_MISMATCH)
		return "Credential data mismatch";
	else if (errnum == ESIG_BUF_SIZE_MISMATCH)
		return "Credential data size mismatch";
	else if (errnum == ESIG_BAD_USERID)
		return "Credential created by invalid user";
	else if (errnum == ESIG_CRED_REPLAYED)
		return "Credential replayed";
	else
		return munge_strerror ((munge_err_t) errnum);
}

static int _encode(char **signature, buf_t *buffer)
{
	int retry = RETRY_COUNT;
	char *cred;
	munge_err_t err;
	munge_ctx_t ctx = _munge_ctx_create();

	if (!ctx)
		return 0;

again:
	err = munge_encode(&cred, ctx, get_buf_data(buffer),
			   get_buf_offset(buffer));
	if (err != EMUNGE_SUCCESS) {
		if ((err == EMUNGE_SOCKET) && retry--) {
			debug("Munge encode failed: %s (retrying ...)",
			      munge_ctx_strerror(ctx));
			usleep(RETRY_USEC);	/* Likely munged too busy */
			goto again;
		}
		if (err == EMUNGE_SOCKET)  /* Also see MUNGE_OPT_TTL above */
			error("If munged is up, restart with --num-threads=10");
		munge_ctx_destroy(ctx);
		return err;
	}

	*signature = xstrdup(cred);
	free(cred);
	munge_ctx_destroy(ctx);
	return 0;
}

/* NOTE: Caller must xfree the signature returned by sig_pp */
extern char *cred_p_sign(char *buffer, int buf_size)
{
	char *signature = NULL;
	buf_t *buf = NULL;

	buf = create_shadow_buf(buffer, buf_size);
	buf->processed = buf_size;
	if (_encode(&signature, buf))
		error("%s: _encode() failed", __func__);
	FREE_NULL_BUFFER(buf);

	return signature;
}

/*
 * WARNING: the buf_t returned from this is slightly non-standard.
 * The head points to malloc()'d memory, not xmalloc()'d, and needs
 * to be managed directly. This is done so the buffer can be used
 * alongside the unpack functions without an additional memcpy step.
 */
static int _decode(char *signature, bool replay_okay, buf_t **buffer,
		   time_t *expiration)
{
	int retry = RETRY_COUNT;
	uid_t uid;
	gid_t gid;
	void *buf_out = NULL;
	int buf_out_size;
	int rc = SLURM_SUCCESS;
	munge_err_t err;
	munge_ctx_t ctx = _munge_ctx_create();

	if (!ctx)
		return SLURM_ERROR;

again:
	err = munge_decode(signature, ctx, &buf_out, &buf_out_size,
			   &uid, &gid);

	if (err != EMUNGE_SUCCESS) {
		if ((err == EMUNGE_SOCKET) && retry--) {
			debug("Munge decode failed: %s (retrying ...)",
			      munge_ctx_strerror(ctx));
			usleep(RETRY_USEC);	/* Likely munged too busy */
			goto again;
		}
		if (err == EMUNGE_SOCKET)
			error("If munged is up, restart with --num-threads=10");

		if (err != EMUNGE_CRED_REPLAYED) {
			rc = err;
			goto end_it;
		}

		if (!replay_okay) {
			rc = ESIG_CRED_REPLAYED;
			goto end_it;
		}

		debug2("We had a replayed credential, but this is expected.");
	}

	if ((uid != slurm_conf.slurm_user_id) && (uid != 0)) {
		error("%s: Unexpected uid (%u) != Slurm uid (%u)",
		      plugin_type, uid, slurm_conf.slurm_user_id);
		rc = ESIG_BAD_USERID;
		goto end_it;
	}

	if (expiration) {
		int ttl;
		time_t t;
		munge_ctx_get(ctx, MUNGE_OPT_TTL, &ttl);
		munge_ctx_get(ctx, MUNGE_OPT_ENCODE_TIME, &t);
		*expiration = t + ttl;
	}

	munge_ctx_destroy(ctx);
	*buffer = create_buf(buf_out, buf_out_size);
	return SLURM_SUCCESS;

end_it:
	if (buf_out)
		free(buf_out);
	munge_ctx_destroy(ctx);
	return rc;
}

extern int cred_p_verify_sign(char *buffer, uint32_t buf_size, char *signature)
{
	int rc = SLURM_SUCCESS;
	bool replay_okay = false;
	buf_t *payload = NULL;

#ifdef MULTIPLE_SLURMD
	replay_okay = true;
#endif

	rc = _decode(signature, replay_okay, &payload, NULL);

	if (buf_size != payload->size)
		rc = ESIG_BUF_SIZE_MISMATCH;
	else if (memcmp(buffer, payload->head, payload->size))
		rc = ESIG_BUF_DATA_MISMATCH;

	/* warning: do not use free_buf() on this! */
	if (payload) {
		free(get_buf_data(payload));
		xfree(payload);
	}

	return rc;
}

extern slurm_cred_t *cred_p_create(slurm_cred_arg_t *cred_arg, bool sign_it,
				   uint16_t protocol_version)
{
	slurm_cred_t *cred = cred_create(cred_arg, protocol_version);

	if (sign_it && _encode(&cred->signature, cred->buffer)) {
		error("%s: failed to sign, returning NULL", __func__);
		slurm_cred_destroy(cred);
		return NULL;
	}

	return cred;
}

extern int cred_p_unpack(void **cred, buf_t *buf, uint16_t protocol_version)
{
	slurm_cred_t *credential = NULL;
	int rc = SLURM_ERROR;

	if (!(credential = cred_unpack_with_signature(buf, protocol_version)))
		goto unpack_error;

	/*
	 * Using the saved position, verify the credential.
	 * This avoids needing to re-pack the entire thing just to
	 * cross-check that the signature matches up later.
	 * (Only done in slurmd.)
	 */
	if (credential->signature && running_in_slurmd()) {
		if ((rc = cred_p_verify_sign(get_buf_data(credential->buffer),
					     get_buf_offset(credential->buffer),
					     credential->signature)))
			goto unpack_error;

		credential->verified = true;
	}

	*cred = credential;
	return SLURM_SUCCESS;

unpack_error:
	slurm_cred_destroy(credential);
	return rc;
}

extern char *cred_p_create_net_cred(void *addrs, uint16_t protocol_version)
{
	int rc;
	char *signature;
	buf_t *buffer = init_buf(BUF_SIZE);

	slurm_pack_node_alias_addrs(addrs, buffer, protocol_version);

	if ((rc = _encode(&signature, buffer))) {
		error("%s: _encode failure: %s",
		      __func__, slurm_strerror(rc));
		free_buf(buffer);
		return NULL;
	}

	free_buf(buffer);
	return signature;
}

extern void *cred_p_extract_net_cred(char *net_cred, uint16_t protocol_version)
{
	int rc;
	time_t expiration;
	slurm_node_alias_addrs_t *addrs = NULL;
	buf_t *buffer = NULL;

	/* warning: do not use free_buf() on the returned buffer */
	if ((rc = _decode(net_cred, true, &buffer, &expiration))) {
		error("%s: failed decode", __func__);
		return NULL;
	}

	if (slurm_unpack_node_alias_addrs(&addrs, buffer, protocol_version)) {
		error("%s: failed unpack", __func__);
		if (buffer) {
			free(get_buf_data(buffer));
			xfree(buffer);
		}
		return NULL;
	}
	addrs->expiration = expiration;
	if (buffer) {
		free(get_buf_data(buffer));
		xfree(buffer);
	}
	return addrs;
}
