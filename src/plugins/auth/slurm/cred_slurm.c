/*****************************************************************************\
 *  cred_slurm.c
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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/plugins/auth/slurm/auth_slurm.h"
#include "src/plugins/cred/common/cred_common.h"

extern slurm_cred_t *cred_p_create(slurm_cred_arg_t *cred_arg, bool sign_it,
				   uint16_t protocol_version)
{
	slurm_cred_t *cred = NULL;
	char *token = NULL, *extra = NULL;

	xassert(cred_arg && cred_arg->id);

	/* support 'srun -Z' operation */
	if (!running_in_slurmctld())
		init_internal();

	extra = get_identity_string(cred_arg->id, cred_arg->id->uid,
				    cred_arg->id->gid);

	cred = cred_create(cred_arg, protocol_version);

	if (!(token = create_internal("launch",
				      cred_arg->id->uid,
				      cred_arg->id->gid,
				      slurm_conf.slurmd_user_id,
				      get_buf_data(cred->buffer),
				      get_buf_offset(cred->buffer),
				      extra)))
		error("create_internal() failed: %m");

	set_buf_offset(cred->buffer, 0);
	packstr(token, cred->buffer);
	cred->signature = token;

	xfree(extra);
	return cred;
}

extern slurm_cred_t *cred_p_unpack(buf_t *buf, uint16_t protocol_version)
{
	char *token = NULL;
	jwt_t *jwt = NULL;
	auth_cred_t *auth_cred = NULL;
	buf_t *packed_buf = NULL;
	char *json_id = NULL;
	slurm_cred_t *cred = NULL;

	safe_unpackstr(&token, buf);

	if (!(jwt = decode_jwt(token, running_in_slurmd(), getuid()))) {
		error("%s: decode_jwt() failed", __func__);
		goto unpack_error;
	}

	auth_cred = new_cred();
	if (copy_jwt_grants_to_cred(jwt, auth_cred))
		goto unpack_error;

	if (xstrcmp(auth_cred->context, "launch"))
		goto unpack_error;

	packed_buf = create_shadow_buf(auth_cred->data, auth_cred->dlen);
	if (cred_unpack((void **) &cred, packed_buf, protocol_version))
		goto unpack_error;

	cred->arg->uid = auth_cred->uid;
	cred->arg->gid = auth_cred->gid;
	cred->ctime = auth_cred->ctime;
	cred->verified = running_in_slurmd();

	FREE_NULL_IDENTITY(cred->arg->id);
	if (!(json_id = jwt_get_grants_json(jwt, "id"))) {
		debug2("%s: no identity provided", __func__);
		cred->arg->id = fetch_identity(auth_cred->uid, auth_cred->gid,
					       false);
	} else if (!(cred->arg->id = extract_identity(json_id, auth_cred->uid,
						      auth_cred->gid))) {
		error("%s: extract_identity() failed", __func__);
		goto unpack_error;
	}
	identity_debug2(cred->arg->id, __func__);

	if (!running_in_slurmstepd()) {
		cred->buffer = init_buf(4096);
		packstr(token, cred->buffer);
		cred->buf_version = protocol_version;
	}

	/* FIXME: use a hash instead of the entire token? */
	cred->signature = token;

	FREE_NULL_CRED(auth_cred);
	FREE_NULL_BUFFER(packed_buf);
	free(json_id);
	jwt_free(jwt);
	return cred;

unpack_error:
	FREE_NULL_CRED(auth_cred);
	xfree(token);
	FREE_NULL_BUFFER(packed_buf);
	slurm_cred_destroy(cred);
	if (json_id)
		free(json_id);
	if (jwt)
		jwt_free(jwt);
	return NULL;
}

extern char *cred_p_create_net_cred(void *addrs, uint16_t protocol_version)
{
	char *token = NULL, *extra = NULL;

	extra = encode_net_aliases(addrs);

	if (!(token = create_internal("net", getuid(), getgid(),
				      slurm_conf.slurmd_user_id,
				      NULL, 0, extra)))
		error("create_internal() failed: %m");

	xfree(extra);
	return token;
}

extern void *cred_p_extract_net_cred(char *net_cred, uint16_t protocol_version)
{
	slurm_node_alias_addrs_t *addrs = NULL;
	jwt_t *jwt = NULL;
	const char *context = NULL;

	if (!(jwt = decode_jwt(net_cred, running_in_slurmd(), getuid()))) {
		error("%s: decode_jwt() failed", __func__);
		return NULL;
	}

	errno = 0;
	context = jwt_get_grant(jwt, "context");
	if (!context || (errno == EINVAL)) {
		error("%s: jwt_get_grant failure for context", __func__);
		goto unpack_error;
	}
	if (xstrcmp(context, "net")) {
		error("%s: wrong context in cred: %s", __func__, context);
		goto unpack_error;
	}

	if (!(addrs = extract_net_aliases(jwt))) {
		error("%s: extract_net_aliases() failed", __func__);
		goto unpack_error;
	}

	/* decode_jwt() already validated this previously */
	addrs->expiration = jwt_get_grant_int(jwt, "exp");

	jwt_free(jwt);
	return addrs;

unpack_error:
	jwt_free(jwt);
	return NULL;
}

extern sbcast_cred_t *sbcast_p_create(sbcast_cred_arg_t *cred_arg,
				      uint16_t protocol_version)
{
	sbcast_cred_t *cred = NULL;
	char *token = NULL, *extra = NULL;

	extra = encode_sbcast(cred_arg);

	if (!(token = create_internal("sbcast", cred_arg->id->uid,
				      cred_arg->id->gid,
				      slurm_conf.slurmd_user_id,
				      NULL, 0, extra))) {
		error("create_internal() failed: %m");
		xfree(extra);
		return NULL;
	}

	xfree(extra);

	cred = xmalloc(sizeof(*cred));
	cred->signature = token;

	return cred;
}

extern sbcast_cred_t *sbcast_p_unpack(buf_t *buf, bool verify,
				      uint16_t protocol_version)
{
	sbcast_cred_t *cred = NULL;
	char *token = NULL;
	jwt_t *jwt = NULL;
	auth_cred_t *auth_cred = NULL;
	char *json_id = NULL, *json_sbcast = NULL;

	safe_unpackstr(&token, buf);

	if (!running_in_slurmd())
		verify = false;

	if (!(jwt = decode_jwt(token, verify, getuid()))) {
		error("%s: decode_jwt() failed", __func__);
		goto unpack_error;
	}

	auth_cred = new_cred();
	if (copy_jwt_grants_to_cred(jwt, auth_cred))
		goto unpack_error;

	if (xstrcmp(auth_cred->context, "sbcast"))
		goto unpack_error;

	if (!(json_sbcast = jwt_get_grants_json(jwt, "sbcast"))) {
		error("%s: jwt_get_grants_json() failure for sbcast", __func__);
		goto unpack_error;
	} else if (!(cred = extract_sbcast(json_sbcast))) {
		error("%s: extract_sbcast() failed", __func__);
		goto unpack_error;
	}

	if (!(json_id = jwt_get_grants_json(jwt, "id"))) {
		debug2("%s: no identity provided", __func__);
		cred->arg.id = fetch_identity(auth_cred->uid, auth_cred->gid,
					      false);
	} else if (!(cred->arg.id = extract_identity(json_id, auth_cred->uid,
						     auth_cred->gid))) {
		error("%s: extract_identity() failed", __func__);
		goto unpack_error;
	} else {
		identity_debug2(cred->arg.id, __func__);
	}

	cred->signature = token;

	jwt_free(jwt);
	FREE_NULL_CRED(auth_cred);
	free(json_sbcast);
	free(json_id);
	return cred;

unpack_error:
	xfree(token);
	if (jwt)
		jwt_free(jwt);
	FREE_NULL_CRED(auth_cred);
	if (json_sbcast)
		free(json_sbcast);
	if (json_id)
		free(json_id);
	return NULL;
}
