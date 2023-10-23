/*****************************************************************************\
 *  internal.c
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

#include "src/plugins/auth/slurm/auth_slurm.h"

static int lifespan = DEFAULT_TTL;
static buf_t *slurm_key = NULL;
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

extern void init_internal(void)
{
	char *key_file = xstrdup(getenv("SLURM_SACK_KEY"));

	if (!key_file)
		key_file = get_extra_conf_path("slurm.key");

	_check_key_permissions(key_file, S_IRWXO);

	debug("loading key: `%s`", key_file);
	if (!(slurm_key = create_mmap_buf(key_file))) {
		fatal("%s: Could not load key file (%s)",
		      plugin_type, key_file);
	}

	xfree(key_file);

	this_hostname = xshort_hostname();

	if (!(lifespan = slurm_get_auth_ttl()))
		lifespan = DEFAULT_TTL;
}

extern void fini_internal(void)
{
	FREE_NULL_BUFFER(slurm_key);
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

	if (!slurm_key || !this_hostname)
		fatal("slurm_key or this_hostname missing");

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

	if (jwt_set_alg(jwt, opt_alg, (unsigned char *) slurm_key->head,
			slurm_key->size)) {
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

	if (!slurm_key)
		fatal("slurm_key missing");

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

	if ((rc = jwt_decode(&jwt, token,
			     verify ? (unsigned char *) slurm_key->head : NULL,
			     verify ? slurm_key->size : 0))) {
		error("%s: jwt_decode failure: %s",
		      __func__, slurm_strerror(rc));
		goto fail;
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
