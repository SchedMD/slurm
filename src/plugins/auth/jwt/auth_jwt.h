/*****************************************************************************\
 *  auth_jwt.h
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

#ifndef _AUTH_JWT_H_
#define _AUTH_JWT_H_

#include "stdbool.h"

#include "src/common/data.h"
#include "src/common/pack.h"

typedef struct auth_token_s auth_token_t;

typedef struct {
	char *claim_field;
	bool use_client_ids;
	bool use_client_ids_only;
	data_t *jwks;
	buf_t *key;
} auth_context_t;

extern char *pem_from_mod_exp(const char *mod, const char *exp);

extern int cred_verify(auth_context_t *ctxt, auth_token_t *cred);

extern auth_token_t *auth_p_create(char *auth_info, uid_t r_uid, void *data,
				   int dlen);

extern void cred_get_ids(auth_context_t *ctxt, auth_token_t *cred, uid_t *uid,
			 gid_t *gid);

extern int auth_p_thread_config(const char *token, const char *username);

extern void auth_p_destroy(auth_token_t *cred);

extern void init_jwks(auth_context_t *ctxt, const char *auth_info);

extern void init_hs256(auth_context_t *ctxt, const char *auth_info);

extern void parse_auth_params(auth_context_t *ctxt, const char *auth_info);

extern void cred_set_token(auth_token_t *cred, const char *token,
			   const char *username);

#endif
