/*****************************************************************************\
 *  auth_jwt.c - JWT token-based slurm authentication plugin
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
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
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/pack.h"
#include "src/common/uid.h"

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
const char plugin_name[] = "JWT authentication plugin";
const char plugin_type[] = "auth/jwt";
const uint32_t plugin_id = AUTH_PLUGIN_JWT;
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;
bool hash_enable = false;

typedef struct {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */

	bool verified;
	bool cannot_verify;
	bool uid_set;
	bool gid_set;

	uid_t uid;
	gid_t gid;

	/* packed data below */
	char *token;
	char *username;
} auth_token_t;

buf_t *key = NULL;
char *token = NULL;
__thread char *thread_token = NULL;
__thread char *thread_username = NULL;

/*
 * This plugin behaves differently than the others in that it needs to operate
 * asynchronously. If we're running in one of the daemons, it's presumed that
 * we're receiving tokens but do not need to generate them as part of our
 * responses. In the client commands, responses are not validated, although
 * for safety the slurm_auth_get_uid()/slurm_auth_get_gid() calls are set to
 * fatal.
 *
 * This plugin does implement a few calls that are unique to its operation:
 *	slurm_auth_thread_config() - used to set a different token specific to
 *		the current thread.
 *	slurm_auth_thread_clear() - free any thread_config memory
 *	slurm_auth_token_generate() - creates a JWT to be passed back to the
 *		requestor for a given username and duration.
 */

static int _init_key(void)
{
	char *key_file = NULL;

	if (slurm_conf.authalt_params && slurm_conf.authalt_params[0]) {
		const char *jwt_key_field = "jwt_key=";
		char *begin = xstrcasestr(slurm_conf.authalt_params,
					  jwt_key_field);

		/* find the begin and ending offsets of the jwt_key */
		if (begin) {
			char *start = begin + sizeof(jwt_key_field);
			char *end = NULL;

			if ((end = xstrstr(start, ",")))
				key_file = xstrndup(start, (end - start));
			else
				key_file = xstrdup(start);
		}
	}

	if (!key_file && slurm_conf.state_save_location) {
		const char *default_key = "jwt_hs256.key";
		/* default to state_save_location for slurmctld */
		xstrfmtcat(key_file, "%s/%s",
			   slurm_conf.state_save_location, default_key);
	} else if (!key_file) {
		/* Must be in slurmdbd */
		error("No jwt_key set. Please set the jwt_key=/path/to/key/file option in AuthAltParameters in slurmdbd.conf.");
		return ESLURM_AUTH_SKIP;
	}

	debug("%s: Loading key: %s", __func__, key_file);

	if (!(key = create_mmap_buf(key_file))) {
		error("%s: Could not load key file (%s)",
		      plugin_type, key_file);
		xfree(key_file);
		return ESLURM_AUTH_FOPEN_ERROR;
	}

	xfree(key_file);
	return SLURM_SUCCESS;
}

extern int init(void)
{
	if (running_in_slurmctld() || running_in_slurmdbd()) {
		int rc;

		if ((rc = _init_key()))
			return rc;
	} else {
		/* we must be in a client command */
		token = getenv("SLURM_JWT");

		/* slurmrestd can wait for the tokens from the clients */
		if (!running_in_slurmrestd() && !token) {
			error("Could not load SLURM_JWT environment variable.");
			return SLURM_ERROR;
		}
	}

	debug("%s loaded", plugin_name);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	free_buf(key);

	return SLURM_SUCCESS;
}

auth_token_t *slurm_auth_create(char *auth_info, uid_t r_uid,
				void *data, int dlen)
{
	return xmalloc(sizeof(auth_token_t));
}

int slurm_auth_destroy(auth_token_t *cred)
{
	if (cred == NULL) {
		slurm_seterrno(ESLURM_AUTH_MEMORY);
		return SLURM_ERROR;
	}

	xfree(cred->token);
	xfree(cred->username);
	xfree(cred);
	return SLURM_SUCCESS;
}

/*
 * Verify a credential to approve or deny authentication.
 *
 * Return SLURM_SUCCESS if the credential is in order and valid.
 */
int slurm_auth_verify(auth_token_t *cred, char *auth_info)
{
	jwt_t *jwt = NULL;
	char *username = NULL;

	if (!cred)
		return SLURM_ERROR;

	if (cred->verified || cred->cannot_verify)
		return SLURM_SUCCESS;

	/* in a client command, we cannot verify responses */
	if (!key) {
		cred->cannot_verify = true;
		return SLURM_SUCCESS;
	}

	if (!cred->token) {
		error("%s: reject NULL token for jwt_decode", __func__);
		goto fail;
	}

	if (jwt_decode(&jwt, cred->token,
		       (unsigned char *) key->head, key->size) ||
	    !jwt) {
		error("%s: jwt_decode failure", __func__);
		goto fail;
	}

	if (jwt_get_grant_int(jwt, "exp") < time(NULL)) {
		error("%s: token expired", __func__);
		goto fail;
	}
	if (!(username = xstrdup(jwt_get_grant(jwt, "sun")))) {
		error("%s: jwt_get_grant failure", __func__);
		goto fail;
	}

	if (!cred->username)
		cred->username = username;
	else if (!xstrcmp(cred->username, username)) {
		/* if they match, ignore it, they were being redundant */
		xfree(username);
	} else {
		uid_t uid;
		if (uid_from_string(username, &uid)) {
			error("%s: uid_from_string failure", __func__);
			goto fail;
		}
		if ((uid != 0) && (slurm_conf.slurm_user_id != uid)) {
			error("%s: attempt to authenticate as alternate user %s from non-SlurmUser %s",
			      __func__, username, cred->username);
			goto fail;
		}
		/* use the packed username instead of the token value */
		xfree(username);
	}

	cred->verified = true;
	return SLURM_SUCCESS;

fail:
	xfree(username);
	if (jwt)
		jwt_free(jwt);
	return SLURM_ERROR;
}

uid_t slurm_auth_get_uid(auth_token_t *cred)
{
	if (cred == NULL || !cred->verified) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_AUTH_NOBODY;
	}

	if (cred->cannot_verify)
		fatal("%s: asked for uid for an unverifiable token, this should never happen",
		      __func__);

	if (cred->uid_set)
		return cred->uid;

	if (uid_from_string(cred->username, &cred->uid)) {
		slurm_seterrno(ESLURM_USER_ID_MISSING);
		return SLURM_AUTH_NOBODY;
	}

	cred->uid_set = true;

	return cred->uid;
}

gid_t slurm_auth_get_gid(auth_token_t *cred)
{
	uid_t uid;

	if (cred == NULL || !cred->verified) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_AUTH_NOBODY;
	}

	if (cred->cannot_verify)
		fatal("%s: asked for gid for an unverifiable token, this should never happen",
		      __func__);

	if (cred->gid_set)
		return cred->gid;

	if ((uid = slurm_auth_get_uid(cred)) == SLURM_AUTH_NOBODY) {
		slurm_seterrno(ESLURM_USER_ID_MISSING);
		return SLURM_AUTH_NOBODY;
	}

	if (((cred->gid = gid_from_uid(uid)) == (gid_t) -1)) {
		slurm_seterrno(ESLURM_USER_ID_MISSING);
		return SLURM_AUTH_NOBODY;
	}

	cred->gid_set = true;

	return cred->gid;
}

char *slurm_auth_get_host(auth_token_t *cred)
{
	if (cred == NULL) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	/* No way to encode this in a useful manner */
	return NULL;
}

int auth_p_get_data(auth_token_t *cred, char **data, uint32_t *len)
{
	if (cred == NULL) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	*data = NULL;
	*len = 0;
	return SLURM_SUCCESS;
}

int slurm_auth_pack(auth_token_t *cred, Buf buf, uint16_t protocol_version)
{
	char *pack_this = (thread_token) ? thread_token : token;

	if (buf == NULL) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	if (protocol_version >= SLURM_20_02_PROTOCOL_VERSION) {
		packstr(pack_this, buf);
		packstr(thread_username, buf);
	} else {
		error("%s: Unknown protocol version %d",
		      __func__, protocol_version);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

auth_token_t *slurm_auth_unpack(Buf buf, uint16_t protocol_version)
{
	auth_token_t *cred = NULL;
	uint32_t uint32_tmp;

	if (!buf) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	cred = xmalloc(sizeof(*cred));
	cred->verified = false;		/* just to be explicit */

	if (protocol_version >= SLURM_20_02_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&cred->token, &uint32_tmp, buf);
		safe_unpackstr_xmalloc(&cred->username, &uint32_tmp, buf);
	} else {
		error("%s: unknown protocol version %u",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return cred;

unpack_error:
	slurm_seterrno(ESLURM_AUTH_UNPACK);
	slurm_auth_destroy(cred);
	return NULL;
}

int slurm_auth_thread_config(const char *token, const char *username)
{
	xfree(thread_token);
	xfree(thread_username);

	thread_token = xstrdup(token);
	thread_username = xstrdup(username);

	return SLURM_SUCCESS;
}

void slurm_auth_thread_clear(void)
{
	xfree(thread_token);
	xfree(thread_username);
}

char *slurm_auth_token_generate(const char *username, int lifespan)
{
	jwt_alg_t opt_alg = JWT_ALG_HS256;
	time_t now = time(NULL);
	jwt_t *jwt;
	char *token, *xtoken;

	if (!key) {
		error("%s: cannot issue tokens, no key loaded", __func__);
		return NULL;
	}

	if (jwt_new(&jwt)) {
		error("%s: jwt_new failure", __func__);
		goto fail;
	}

	if (jwt_add_grant_int(jwt, "iat", now)) {
		error("%s: jwt_add_grant_int failure", __func__);
		goto fail;
	}
	if (jwt_add_grant_int(jwt, "exp", now + lifespan)) {
		error("%s: jwt_add_grant_int failure", __func__);
		goto fail;
	}
	/* "sun" is "[s]lurm [u]ser[n]ame" */
	if (jwt_add_grant(jwt, "sun", username)) {
		error("%s: jwt_add_grant failure", __func__);
		goto fail;
	}

	if (jwt_set_alg(jwt, opt_alg, (unsigned char *) key->head, key->size)) {
		error("%s: jwt_add_grant failure", __func__);
		goto fail;
	}

	if (!(token = jwt_encode_str(jwt))) {
		error("%s: jwt_encode_str failure", __func__);
		goto fail;
	}
	xtoken = xstrdup(token);

	jwt_free(jwt);

	info("created token for %s for %d seconds", username, lifespan);

	return xtoken;

fail:
	jwt_free(jwt);
	return NULL;
}
