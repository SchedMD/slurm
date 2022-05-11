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

#include "src/common/data.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "auth_jwt.h"

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
const bool hash_enable = false;

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

data_t *jwks = NULL;
buf_t *key = NULL;
char *token = NULL;
__thread char *thread_token = NULL;
__thread char *thread_username = NULL;

/*
 * This plugin behaves differently than the others in that it needs to operate
 * asynchronously. If we're running in one of the daemons, it's presumed that
 * we're receiving tokens but do not need to generate them as part of our
 * responses. In the client commands, responses are not validated, although
 * for safety the auth_p_get_uid()/auth_p_get_gid() calls are set to
 * fatal.
 *
 * This plugin does implement a few calls that are unique to its operation:
 *	auth_p_thread_config() - used to set a different token specific to
 *		the current thread.
 *	auth_p_thread_clear() - free any thread_config memory
 *	auth_p_token_generate() - creates a JWT to be passed back to the
 *		requestor for a given username and duration.
 */

static const char *jwt_key_field = "jwt_key=";
static const char *jwks_key_field = "jwks=";

static data_for_each_cmd_t _build_jwks_keys(data_t *d, void *arg)
{
	char *alg, *kid, *n, *e, *key;

	/* Ignore non-RS256 keys in the JWKS */
	alg = data_get_string(data_key_get(d, "alg"));
	if (xstrcasecmp(alg, "RS256"))
		return DATA_FOR_EACH_CONT;

	if (!(kid = data_get_string(data_key_get(d, "kid"))))
		fatal("%s: failed to load kid field", __func__);
	if (!(e = data_get_string(data_key_get(d, "e"))))
		fatal("%s: failed to load e field", __func__);
	if (!(n = data_get_string(data_key_get(d, "n"))))
		fatal("%s: failed to load n field", __func__);

	key = pem_from_mod_exp(n, e);
	debug3("key for kid %s mod %s exp %s is\n%s", kid, n, e, key);

	data_set_int(data_key_set(d, "slurm-pem-len"), strlen(key));
	data_set_string_own(data_key_set(d, "slurm-pem"), key);

	return DATA_FOR_EACH_CONT;
}

static void _init_jwks(void)
{
	char *begin, *start, *end, *key_file;
	buf_t *buf;

	if (!(begin = xstrstr(slurm_conf.authalt_params, jwks_key_field)))
		return;

	if (data_init(MIME_TYPE_JSON_PLUGIN, NULL))
		fatal("%s: data_init() failed", __func__);

	start = begin + strlen(jwks_key_field);
	if ((end = xstrstr(start, ",")))
		key_file = xstrndup(start, (end - start));
	else
		key_file = xstrdup(start);

	debug("loading jwks file `%s`", key_file);
	if (!(buf = create_mmap_buf(key_file))) {
		fatal("%s: Could not load key file (%s)",
		      plugin_type, key_file);
	}

	if (data_g_deserialize(&jwks, buf->head, buf->size, MIME_TYPE_JSON))
		fatal("%s: failed to deserialize jwks file `%s`",
		      __func__, key_file);
	free_buf(buf);

	/* force everything to be a string */
	(void) data_convert_tree(jwks, DATA_TYPE_STRING);

	(void) data_list_for_each(data_key_get(jwks, "keys"), _build_jwks_keys,
				  NULL);
}

static void _init_hs256(void)
{
	char *begin, *key_file = NULL;

	if ((begin = xstrstr(slurm_conf.authalt_params, jwt_key_field))) {
		char *start = begin + strlen(jwt_key_field);
		char *end = NULL;

		if ((end = xstrstr(start, ",")))
			key_file = xstrndup(start, (end - start));
		else
			key_file = xstrdup(start);
	}

	/*
	 * If jwks was loaded, and jwt is not explicitly configured, skip setup.
	 */
	if (!key_file && jwks)
		return;

	if (!key_file && slurm_conf.state_save_location) {
		const char *default_key = "jwt_hs256.key";
		/* default to state_save_location for slurmctld */
		xstrfmtcat(key_file, "%s/%s",
			   slurm_conf.state_save_location, default_key);
	} else if (!key_file) {
		/* Must be in slurmdbd */
		fatal("No jwt_key set. Please set the jwt_key=/path/to/key/file option in AuthAltParameters in slurmdbd.conf.");
	}

	debug("%s: Loading key: %s", __func__, key_file);

	if (!(key = create_mmap_buf(key_file))) {
		fatal("%s: Could not load key file (%s)",
		      plugin_type, key_file);
	}

	xfree(key_file);
}

extern int init(void)
{
	if (running_in_slurmctld() || running_in_slurmdbd()) {
		_init_jwks();
		_init_hs256();
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
	FREE_NULL_DATA(jwks);
	FREE_NULL_BUFFER(key);

	return SLURM_SUCCESS;
}

auth_token_t *auth_p_create(char *auth_info, uid_t r_uid, void *data, int dlen)
{
	return xmalloc(sizeof(auth_token_t));
}

int auth_p_destroy(auth_token_t *cred)
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

typedef struct {
	const char *kid;
	const char *token;
	jwt_t **jwt;
} foreach_rs256_args_t;

data_for_each_cmd_t _verify_rs256_jwt(data_t *d, void *arg)
{
	char *alg, *kid, *key;
	int len;
	jwt_t *jwt;
	int rc;
	foreach_rs256_args_t *args = (foreach_rs256_args_t *) arg;

	/* Ignore non-RS256 keys in the JWKS */
	alg = data_get_string(data_key_get(d, "alg"));
	if (xstrcasecmp(alg, "RS256"))
		return DATA_FOR_EACH_CONT;

	/* Return early if this key doesn't match */
	kid = data_get_string(data_key_get(d, "kid"));
	if (xstrcmp(args->kid, kid))
		return DATA_FOR_EACH_CONT;

	debug("matched on kid '%s'", kid);

	key = data_get_string(data_key_get(d, "slurm-pem"));
	len = data_get_int(data_key_get(d, "slurm-pem-len"));

	if ((rc = jwt_decode(&jwt, args->token,
			     (const unsigned char *) key, len))) {
		error("failed to verify jwt, rc=%d", rc);
		return DATA_FOR_EACH_FAIL;
	}

	*args->jwt = jwt;

	return DATA_FOR_EACH_STOP;
}

/*
 * Verify a credential to approve or deny authentication.
 *
 * Return SLURM_SUCCESS if the credential is in order and valid.
 */
int auth_p_verify(auth_token_t *cred, char *auth_info)
{
	int rc;
	const char *alg;
	jwt_t *unverified_jwt = NULL, *jwt = NULL;
	char *username = NULL;

	if (!cred)
		return SLURM_ERROR;

	if (cred->verified || cred->cannot_verify)
		return SLURM_SUCCESS;

	/* in a client command, we cannot verify responses */
	if (!jwks && !key) {
		cred->cannot_verify = true;
		return SLURM_SUCCESS;
	}

	if (!cred->token) {
		error("%s: reject NULL token for jwt_decode", __func__);
		goto fail;
	}

	if ((rc = jwt_decode(&unverified_jwt, cred->token, NULL, 0))) {
		error("%s: initial jwt_decode failure: %s",
		      __func__, slurm_strerror(rc));
		goto fail;
	}

	alg = jwt_get_header(unverified_jwt, "alg");

	if (!xstrcasecmp(alg, "RS256")) {
		foreach_rs256_args_t args;

		if (!jwks) {
			error("%s: no jwks file loaded, cannot decode RS256 keys",
			      __func__);
			goto fail;
		}

		args.kid = jwt_get_header(unverified_jwt, "kid");
		args.token = cred->token;
		args.jwt = &jwt;

		if (!args.kid) {
			error("%s: no kid in credential", __func__);
			goto fail;
		}

		/*
		 * Deal with errors within the matching kid.
		 */
		(void) data_list_for_each(data_key_get(jwks, "keys"), _verify_rs256_jwt, &args);

		if (!jwt) {
			error("could not find matching kid or decode failed");
			goto fail;
		}
	} else if (!xstrcasecmp(alg, "HS256")) {
		if (!key) {
			error("%s: no key file loaded, cannot decode HS256 keys",
			      __func__);
			goto fail;
		}

		if ((rc = jwt_decode(&jwt, cred->token,
				     (unsigned char *) key->head,
				     key->size))) {
			error("%s: jwt_decode failure: %s",
			      __func__, slurm_strerror(rc));
			goto fail;
		}
	} else {
		error("%s: no support for alg=%s", __func__, alg);
		goto fail;
	}

	jwt_free(unverified_jwt);
	unverified_jwt = NULL;

	/*
	 * at this point we have a verified jwt to work with
	 * check the expiration, and sort out the appropriate username
	 */

	if (jwt_get_grant_int(jwt, "exp") < time(NULL)) {
		error("%s: token expired", __func__);
		goto fail;
	}

	/*
	 * 'sun' is preferred if available
	 * 'username' is used otherwise
	 */
	if (!(username = xstrdup(jwt_get_grant(jwt, "sun"))) &&
	    !(username = xstrdup(jwt_get_grant(jwt, "username")))) {
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
	if (unverified_jwt)
		jwt_free(unverified_jwt);
	if (jwt)
		jwt_free(jwt);
	xfree(username);
	return SLURM_ERROR;
}

uid_t auth_p_get_uid(auth_token_t *cred)
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

gid_t auth_p_get_gid(auth_token_t *cred)
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

	if ((uid = auth_p_get_uid(cred)) == SLURM_AUTH_NOBODY) {
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

char *auth_p_get_host(auth_token_t *cred)
{
	if (cred == NULL) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	/* No way to encode this in a useful manner */
	return NULL;
}

extern int auth_p_get_data(auth_token_t *cred, char **data, uint32_t *len)
{
	if (cred == NULL) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	*data = NULL;
	*len = 0;
	return SLURM_SUCCESS;
}

int auth_p_pack(auth_token_t *cred, buf_t *buf, uint16_t protocol_version)
{
	char *pack_this = (thread_token) ? thread_token : token;

	if (buf == NULL) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(pack_this, buf);
		packstr(thread_username, buf);
	} else {
		error("%s: Unknown protocol version %d",
		      __func__, protocol_version);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

auth_token_t *auth_p_unpack(buf_t *buf, uint16_t protocol_version)
{
	auth_token_t *cred = NULL;
	uint32_t uint32_tmp;

	if (!buf) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	cred = xmalloc(sizeof(*cred));
	cred->verified = false;		/* just to be explicit */

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
	auth_p_destroy(cred);
	return NULL;
}

int auth_p_thread_config(const char *token, const char *username)
{
	xfree(thread_token);
	xfree(thread_username);

	thread_token = xstrdup(token);
	thread_username = xstrdup(username);

	return SLURM_SUCCESS;
}

void auth_p_thread_clear(void)
{
	xfree(thread_token);
	xfree(thread_username);
}

char *auth_p_token_generate(const char *username, int lifespan)
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
