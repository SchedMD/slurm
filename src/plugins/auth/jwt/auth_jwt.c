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
#include <sys/stat.h>
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
#include "src/interfaces/serializer.h"

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
	bool ids_set;

	uid_t uid;
	gid_t gid;

	/* packed data below */
	char *token;
	char *username;
} auth_token_t;

static data_t *jwks = NULL;
static buf_t *key = NULL;
static char *token = NULL;
static char *claim_field = NULL;
static __thread char *thread_token = NULL;
static __thread char *thread_username = NULL;

/*
 * This plugin behaves differently than the others in that it needs to operate
 * asynchronously. If we're running in one of the daemons, it's presumed that
 * we're receiving tokens but do not need to generate them as part of our
 * responses. In the client commands, responses are not validated, although
 * for safety the auth_p_get_ids call is set to fatal.
 *
 * This plugin does implement a few calls that are unique to its operation:
 *	auth_p_thread_config() - used to set a different token specific to
 *		the current thread.
 *	auth_p_thread_clear() - free any thread_config memory
 *	auth_p_token_generate() - creates a JWT to be passed back to the
 *		requestor for a given username and duration.
 */

static void _check_key_permissions(const char *path, int bad_perms)
{
	struct stat buf;

	xassert(path);

	if (stat(path, &buf))
		fatal("%s: cannot stat '%s': %m", plugin_type, path);

	if ((buf.st_uid != 0) && (buf.st_uid != slurm_conf.slurm_user_id))
		warning("%s: '%s' owned by uid=%u, instead of SlurmUser(%u) or root",
			plugin_type, path, buf.st_uid,
			slurm_conf.slurm_user_id);

	if (buf.st_mode & bad_perms)
		fatal("%s: key file is insecure: '%s' mode=0%o",
		      plugin_type, path, buf.st_mode & 0777);
}

static data_for_each_cmd_t _build_jwks_keys(data_t *d, void *arg)
{
	char *alg, *kid, *n, *e, *key;

	if (!(kid = data_get_string(data_key_get(d, "kid"))))
		fatal("%s: failed to load kid field", __func__);

	/* Ignore non-RS256 keys in the JWKS if algorthim is provided */
	if ((alg = data_get_string(data_key_get(d, "alg"))) &&
	    xstrcasecmp(alg, "RS256"))
		return DATA_FOR_EACH_CONT;

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
	char *key_file;
	buf_t *buf;

	if (!(key_file = conf_get_opt_str(slurm_conf.authalt_params, "jwks=")))
		return;

	_check_key_permissions(key_file, S_IWOTH);

	if (serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))
		fatal("%s: serializer_g_init() failed", __func__);

	debug("loading jwks file `%s`", key_file);
	if (!(buf = create_mmap_buf(key_file))) {
		fatal("%s: Could not load key file (%s)",
		      plugin_type, key_file);
	}

	if (serialize_g_string_to_data(&jwks, buf->head, buf->size,
				       MIME_TYPE_JSON))
		fatal("%s: failed to deserialize jwks file `%s`",
		      __func__, key_file);
	FREE_NULL_BUFFER(buf);

	/* force everything to be a string */
	(void) data_convert_tree(jwks, DATA_TYPE_STRING);

	(void) data_list_for_each(data_key_get(jwks, "keys"), _build_jwks_keys,
				  NULL);
}

static void _init_hs256(void)
{
	char *key_file;

	key_file = conf_get_opt_str(slurm_conf.authalt_params, "jwt_key=");

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

	_check_key_permissions(key_file, S_IRWXO);

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
		char *claim;

		_init_jwks();
		_init_hs256();

		/*
		 * Support an optional custom username claim field in addition
		 * to 'sun' and 'username'.
		 */
		if ((claim = xstrstr(slurm_conf.authalt_params, "userclaimfield="))) {
			char *end;

			claim_field = xstrdup(claim + 15);
			if ((end = xstrstr(claim_field, ",")))
				*end = '\0';

			info("Custom user claim field: %s", claim_field);
		}
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
	xfree(claim_field);
	FREE_NULL_DATA(jwks);
	FREE_NULL_BUFFER(key);

	return SLURM_SUCCESS;
}

extern auth_token_t *auth_p_create(char *auth_info, uid_t r_uid, void *data,
				   int dlen)
{
	return xmalloc(sizeof(auth_token_t));
}

extern void auth_p_destroy(auth_token_t *cred)
{
	if (!cred)
		return;

	xfree(cred->token);
	xfree(cred->username);
	xfree(cred);
}

typedef struct {
	const char *kid;
	const char *token;
	jwt_t **jwt;
} foreach_rs256_args_t;

static data_for_each_cmd_t _verify_rs256_jwt(data_t *d, void *arg)
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
extern int auth_p_verify(auth_token_t *cred, char *auth_info)
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
	    !(username = xstrdup(jwt_get_grant(jwt, "username"))) &&
	    (!claim_field ||
	     !(username = xstrdup(jwt_get_grant(jwt, claim_field)))))
	{
		error("%s: jwt_get_grant failure", __func__);
		goto fail;
	}

	jwt_free(jwt);
	jwt = NULL;

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

extern void auth_p_get_ids(auth_token_t *cred, uid_t *uid, gid_t *gid)
{
	*uid = SLURM_AUTH_NOBODY;
	*gid = SLURM_AUTH_NOBODY;

	if (!cred || !cred->verified)
		return;

	if (cred->cannot_verify)
		fatal("%s: asked for uid for an unverifiable token, this should never happen",
		      __func__);

	if (cred->ids_set) {
		*uid = cred->uid;
		*gid = cred->gid;
		return;
	}

	if (uid_from_string(cred->username, &cred->uid))
		return;

	if (((cred->gid = gid_from_uid(cred->uid)) == (gid_t) -1))
		return;

	cred->ids_set = true;

	*uid = cred->uid;
	*gid = cred->gid;
}

extern char *auth_p_get_host(auth_token_t *cred)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	/* No way to encode this in a useful manner */
	return NULL;
}

extern int auth_p_get_data(auth_token_t *cred, char **data, uint32_t *len)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	*data = NULL;
	*len = 0;
	return SLURM_SUCCESS;
}

extern void *auth_p_get_identity(auth_token_t *cred)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	return NULL;
}

extern int auth_p_pack(auth_token_t *cred, buf_t *buf,
		       uint16_t protocol_version)
{
	char *pack_this = (thread_token) ? thread_token : token;

	if (!buf) {
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

extern auth_token_t *auth_p_unpack(buf_t *buf, uint16_t protocol_version)
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

extern int auth_p_thread_config(const char *token, const char *username)
{
	xfree(thread_token);
	xfree(thread_username);

	thread_token = xstrdup(token);
	thread_username = xstrdup(username);

	return SLURM_SUCCESS;
}

extern void auth_p_thread_clear(void)
{
	xfree(thread_token);
	xfree(thread_username);
}

extern char *auth_p_token_generate(const char *username, int lifespan)
{
	jwt_alg_t opt_alg = JWT_ALG_HS256;
	time_t now = time(NULL);
	jwt_t *jwt;
	char *token, *xtoken;
	long grant_time = now + lifespan;

	if (!key) {
		error("%s: cannot issue tokens, no key loaded", __func__);
		return NULL;
	}

	if ((lifespan >= NO_VAL) || (lifespan <= 0) || (grant_time <= 0)) {
		error("%s: cannot issue token: requested lifespan %ds not supported",
		      __func__, lifespan);
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
	if (jwt_add_grant_int(jwt, "exp", grant_time)) {
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
	/*
	 * Ideally this would be jwt_free_str() instead of free(),
	 * but that function doesn't exist in older versions of libjwt.
	 */
	free(token);

	jwt_free(jwt);

	info("created token for %s for %d seconds", username, lifespan);

	return xtoken;

fail:
	jwt_free(jwt);
	return NULL;
}
