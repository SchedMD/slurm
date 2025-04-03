/*****************************************************************************\
 *  internal.c
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
#include <sys/stat.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/assoc_mgr.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/plugins/auth/slurm/auth_slurm.h"

typedef struct {
	const char *kid;
	time_t exp;
	unsigned char *key;
	unsigned int keylen;
} key_details_t;

static key_details_t *default_key = NULL;
static data_t *key_data = NULL;
static list_t *key_list = NULL;
static int lifespan = DEFAULT_TTL;
static char *this_hostname = NULL;

static void _check_key_permissions(const char *path, int bad_perms)
{
	struct stat statbuf;

	xassert(path);

	if (stat(path, &statbuf))
		fatal("%s: cannot stat '%s': %m", plugin_type, path);

	/*
	 * Configless operation means slurm_user_id is 0.
	 * Avoid an incorrect warning if the key is actually owned by the
	 * (currently unknown) SlurmUser. (Although if you're running with
	 * SlurmUser=root, this warning will be skipped inadvertently.)
	 */
	if ((statbuf.st_uid != 0) && slurm_conf.slurm_user_id &&
	    (statbuf.st_uid != slurm_conf.slurm_user_id))
		warning("%s: '%s' owned by uid=%u, instead of SlurmUser(%u) or root",
			plugin_type, path, statbuf.st_uid,
			slurm_conf.slurm_user_id);

	if (statbuf.st_mode & bad_perms)
		fatal("%s: key file is insecure: '%s' mode=0%o",
		      plugin_type, path, statbuf.st_mode & 0777);
}

static void _free_key_details(void *x)
{
	key_details_t *key = x;

	xfree(key->key);
	xfree(key);
}

static int _find_kid(void *x, void *y)
{
	key_details_t *key = x;
	char *kid = y;

	return (!xstrcmp(key->kid, kid));
}

/*
 * slurm.jwks: Must be a JSON list of "keys".
 *
 * Fields for each key are:
 * alg - Required. MUST be "HS256".
 * kty - Required. MUST be "oct".
 * kid - Required. Case-sensitive text field.
 * k - Required. Base64 / Base64url encoded binary blob.
 * use - Optional. "default" indicates the default key.
 * exp - Optional. Unix timestamp for key expiration.
 */
static data_for_each_cmd_t _build_key_list(data_t *d, void *arg)
{
	key_details_t *key_ptr;
	const char *kty = NULL, *alg = NULL, *k = NULL, *k_base64 = NULL;
	const char *use = NULL;
	data_t *exp = NULL;

	key_ptr = xmalloc(sizeof(*key_ptr));

	if (!(key_ptr->kid = data_get_string(data_key_get(d, "kid"))))
		fatal("%s: failed to load kid field", __func__);
	if (list_find_first_ro(key_list, _find_kid, (void *) key_ptr->kid))
		fatal("%s: kid fields must be unique", __func__);

	if (!(kty = data_get_string(data_key_get(d, "kty"))))
		fatal("%s: failed to load kty field", __func__);
	if (xstrcasecmp(kty, "oct"))
		fatal("%s: kty field must be oct", __func__);

	if (!(alg = data_get_string(data_key_get(d, "alg"))))
		fatal("%s: failed to load alg field", __func__);
	if (xstrcasecmp(alg, "HS256"))
		fatal("%s: alg field must be HS256", __func__);

	if (!(k = data_get_string(data_key_get(d, "k"))))
		fatal("%s: failed to load key field", __func__);

	k_base64 = xbase64_from_base64url(k);
	key_ptr->key = xmalloc(strlen(k_base64));
	key_ptr->keylen = jwt_Base64decode(key_ptr->key, k_base64);
	xfree(k_base64);

	if (key_ptr->keylen < 16)
		fatal("%s: key lacks sufficient entropy", __func__);

	if ((use = data_get_string(data_key_get(d, "use"))) &&
	    !xstrcasecmp(use, "default")) {
		if (default_key)
			fatal("%s: multiple default keys defined", __func__);

		default_key = key_ptr;
	}

	if ((exp = data_key_get(d, "exp"))) {
		int64_t expiration;
		if (data_get_int_converted(exp, &expiration))
			fatal("%s: invalid value for exp", __func__);
		key_ptr->exp = expiration;
	}

	list_append(key_list, key_ptr);

	return DATA_FOR_EACH_CONT;
}

static void _read_keys_file(char *key_file)
{
	buf_t *jwks = NULL;
	data_t *keys = NULL;

	if (serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))
		fatal("%s: serializer_g_init() failed", __func__);

	debug("loading keys file `%s`", key_file);

	if (!(jwks = create_mmap_buf(key_file)))
		fatal("%s: Could not load keys file (%s)",
		      plugin_type, key_file);

	if (serialize_g_string_to_data(&key_data, jwks->head, jwks->size,
				       MIME_TYPE_JSON))
		fatal("%s: failed to deserialize keys file `%s`",
			__func__, key_file);

	key_list = list_create(_free_key_details);

	if (!(keys = data_key_get(key_data, "keys")))
		fatal("%s: jwks file invalid", __func__);

	(void) data_list_for_each(keys, _build_key_list, NULL);

	if (!default_key)
		default_key = list_peek(key_list);

	FREE_NULL_BUFFER(jwks);
}

extern void init_internal(void)
{
	struct stat statbuf;
	char *key_file = xstrdup(getenv("SLURM_SACK_KEY"));
	char *jwks_file = xstrdup(getenv("SLURM_SACK_JWKS"));

	if (!key_file)
		key_file = get_extra_conf_path("slurm.key");
	if (!jwks_file)
		jwks_file = get_extra_conf_path("slurm.jwks");

	if (!stat(jwks_file, &statbuf)) {
		_check_key_permissions(jwks_file, S_IRWXO);
		_read_keys_file(jwks_file);
	} else {
		buf_t *slurm_key = NULL;
		_check_key_permissions(key_file, S_IRWXO);

		debug("loading key: `%s`", key_file);
		if (!(slurm_key = create_mmap_buf(key_file))) {
			fatal("%s: Could not load key file (%s)",
			      plugin_type, key_file);
		}

		default_key = xmalloc(sizeof(*default_key));
		default_key->key = xmalloc(slurm_key->size);
		default_key->keylen = slurm_key->size;
		memcpy(default_key->key, slurm_key->head, slurm_key->size);
		FREE_NULL_BUFFER(slurm_key);
	}

	this_hostname = xshort_hostname();

	if (!(lifespan = slurm_get_auth_ttl()))
		lifespan = DEFAULT_TTL;
}

extern void fini_internal(void)
{
	if (key_data) {
		FREE_NULL_DATA(key_data);
		FREE_NULL_LIST(key_list);
	} else {
		xfree(default_key->key);
		xfree(default_key);
	}

	xfree(this_hostname);
	/* save token cache to state */
	/* terminate processing thread */
}

extern char *create_internal(char *context, uid_t uid, gid_t gid, uid_t r_uid,
			     void *data, int dlen, char *extra)
{
	jwt_alg_t opt_alg = JWT_ALG_HS256;
	time_t now = time(NULL);
	jwt_t *jwt;
	char *token = NULL, *xtoken = NULL;
	long grant_time = now + lifespan;

	if (!default_key || !this_hostname)
		fatal("default_key or this_hostname missing");

	if (jwt_new(&jwt)) {
		error("%s: jwt_new failure", __func__);
		goto fail;
	}

	if (jwt_add_grant_int(jwt, "iat", now)) {
		error("%s: jwt_add_grant_int failure for iat", __func__);
		goto fail;
	}
	if (jwt_add_grant_int(jwt, "exp", grant_time)) {
		error("%s: jwt_add_grant_int failure for exp", __func__);
		goto fail;
	}
	if (jwt_add_grant_int(jwt, "ver", SLURM_PROTOCOL_VERSION)) {
		error("%s: jwt_add_grant_int failure for ver", __func__);
		goto fail;
	}
	if (jwt_add_grant_int(jwt, "ruid", r_uid)) {
		error("%s: jwt_add_grant_int failure for r_uid", __func__);
		goto fail;
	}
	if (jwt_add_grant(jwt, "context", context)) {
		error("%s: jwt_add_grant_int failure for context", __func__);
		goto fail;
	}
	if (slurm_conf.cluster_name &&
	    jwt_add_grant(jwt, "cluster", slurm_conf.cluster_name)) {
		error("%s: jwt_add_grant_int failure for cluster", __func__);
		goto fail;
	}

	if (extra && jwt_add_grants_json(jwt, extra)) {
		error("%s: jwt_add_grants_json failure for extra grants",
		      __func__);
		goto fail;
	}

	if (jwt_add_grant_int(jwt, "uid", uid)) {
		error("%s: jwt_add_grant_int failure for uid", __func__);
		goto fail;
	}
	if (jwt_add_grant_int(jwt, "gid", gid)) {
		error("%s: jwt_add_grant_int failure for gid", __func__);
		goto fail;
	}
	if (jwt_add_grant(jwt, "host", this_hostname)) {
		error("%s: jwt_add_grant failure for host", __func__);
		goto fail;
	}
	if (data && dlen) {
		/* This is excessive, but also easy to calculate. */
		char *payload = xcalloc(2, dlen);
		jwt_Base64encode(payload, data, dlen);
		if (jwt_add_grant(jwt, "payload", payload)) {
			error("%s: jwt_add_grant failure for payload", __func__);
			xfree(payload);
			goto fail;
		}
		xfree(payload);
	}

	/* Set the kid if available. */
	if (default_key->kid) {
		if (jwt_add_header(jwt, "kid", default_key->kid)) {
			error("%s: jwt_add_header failure", __func__);
			goto fail;
		}
	}

	if (jwt_set_alg(jwt, opt_alg, default_key->key, default_key->keylen)) {
		error("%s: jwt_set_alg failure", __func__);
		goto fail;
	}

	if (!(token = jwt_encode_str(jwt))) {
		error("%s: jwt_encode_str failure", __func__);
		goto fail;
	}

	xtoken = xstrdup(token);
	free(token);

	jwt_free(jwt);

	return xtoken;

fail:
	jwt_free(jwt);
	return NULL;
}

extern int verify_internal(auth_cred_t *cred, uid_t decoder_uid)
{
	jwt_t *jwt = NULL;

	if (!default_key)
		fatal("default_key missing");

	if (!cred) {
		error("%s: rejecting NULL cred", __func__);
		goto fail;
	}

	if (cred->verified)
		return SLURM_SUCCESS;

	if (!cred->token) {
		error("%s: rejecting NULL token", __func__);
		goto fail;
	}

	if (!(jwt = decode_jwt(cred->token, true, decoder_uid))) {
		error("%s: decode_jwt() failed", __func__);
		goto fail;
	}

	cred->verified = true;

	/* Provides own error messages */
	if (copy_jwt_grants_to_cred(jwt, cred))
		goto fail;

	if (xstrcmp(cred->context, "auth") && xstrcmp(cred->context, "sack"))
		goto fail;

	if (use_client_ids) {
		char *json_id;
		if ((json_id = jwt_get_grants_json(jwt, "id"))) {
			cred->id = extract_identity(json_id, cred->uid,
						    cred->gid);
			free(json_id);
			if (!cred->id)
				goto fail;

			if (running_in_slurmctld() || running_in_slurmdbd())
				assoc_mgr_set_uid(cred->uid, cred->id->pw_name);
		}
	}

	jwt_free(jwt);

	return SLURM_SUCCESS;

fail:
	if (jwt)
		jwt_free(jwt);
	return SLURM_ERROR;
}

extern jwt_t *decode_jwt(char *token, bool verify, uid_t decoder_uid)
{
	int rc;
	jwt_t *jwt = NULL;
	const char *alg;
	long r_uid, expiration;

	if (verify && key_list) {
		jwt_t *unverified_jwt = NULL;
		key_details_t *key = NULL;
		const char *kid = NULL;

		if ((rc = jwt_decode(&unverified_jwt, token, NULL, 0))) {
			error("%s: jwt_decode failure: %s",
			      __func__, slurm_strerror(rc));
			goto fail;
		}

		if ((kid = jwt_get_header(unverified_jwt, "kid"))) {
			/* Find the kid in our keys list */
			if (!(key = list_find_first_ro(key_list, _find_kid,
						       (char *) kid))) {
				error("%s: could not find kid=%s",
				      __func__, kid);
				jwt_free(unverified_jwt);
				goto fail;
			}
		} else {
			debug2("%s: jwt_get_header failed for kid, using default key",
			       __func__);
			key = default_key;
		}

		kid = NULL;	/* pointer into unverified_jwt */
		jwt_free(unverified_jwt);
		unverified_jwt = NULL;

		if (key->exp && (key->exp < time(NULL))) {
			error("%s: token received for expired key kid=%s",
			      __func__, key->kid);
			goto fail;
		}

		if ((rc = jwt_decode(&jwt, token, key->key, key->keylen))) {
			error("%s: jwt_decode (with key kid=%s) failure: %s",
			      __func__, key->kid, slurm_strerror(rc));
			goto fail;
		}
	} else if (verify) {
		if ((rc = jwt_decode(&jwt, token, default_key->key,
				     default_key->keylen))) {
			error("%s: jwt_decode (with key) failure: %s",
			      __func__, slurm_strerror(rc));
			goto fail;
		}
	} else {
		if ((rc = jwt_decode(&jwt, token, NULL, 0))) {
			error("%s: jwt_decode failure: %s",
			      __func__, slurm_strerror(rc));
			goto fail;
		}
	}

	/*
	 * WARNING: please do not remove this seemingly-redundant check.
	 * This provide an additional layer of defense against alg "none".
	 */
	alg = jwt_get_header(jwt, "alg");

	if (xstrcasecmp(alg, "HS256")) {
		error("%s: no support for alg=%s", __func__, alg);
		goto fail;
	}

	/* jwt_get_grant_int() returns 0 on error, which is caught anyways */
	expiration = jwt_get_grant_int(jwt, "exp");
	if (expiration < time(NULL)) {
		error("%s: token expired at %ld", __func__, expiration);
		goto fail;
	}

	errno = 0;
	r_uid = jwt_get_grant_int(jwt, "ruid");
	if (errno == EINVAL) {
		error("%s: jwt_get_grant_int failure for uid", __func__);
		goto fail;
	}

	/*
	 * Validate the 'restrict uid' field now.
	 * Note - the cast to (uid_t) must remain, as SLURM_AUTH_UID_ANY is
	 * negative one, and will have been converted to 4294967295 on the wire.
	 */
	if (verify &&
	    (r_uid != (uid_t) SLURM_AUTH_UID_ANY) && (r_uid != decoder_uid)) {
		error("%s: asked to verify token with r_uid=%ld for uid=%u, rejecting",
		      __func__, r_uid, decoder_uid);
		goto fail;
	}

	return jwt;

fail:
	if (jwt)
		jwt_free(jwt);
	return NULL;
}
