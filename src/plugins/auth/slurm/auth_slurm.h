/*****************************************************************************\
 *  auth_slurm.h
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

#ifndef _AUTH_SLURM_H
#define _AUTH_SLURM_H

#include <jwt.h>
#include <stdbool.h>
#include <sys/types.h>

#include "src/common/data.h"
#include "src/common/identity.h"
#include "src/interfaces/cred.h"

#define DEFAULT_TTL 60

typedef struct {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */

	bool verified;

	time_t ctime;

	uid_t uid;
	gid_t gid;
	char *hostname;
	char *cluster;
	char *context;

	void *data;
	int dlen;

	identity_t *id;

	/* packed data below */
	char *token;
} auth_cred_t;

extern bool internal;
extern bool use_client_ids;

/* Borrow these from libjwt despite them not being public. */
extern int jwt_Base64encode(char *encoded, const char *string, int len);
extern int jwt_Base64decode(unsigned char *bufplain, const char *bufcoded);

extern void init_internal(void);
extern void fini_internal(void);
extern char *create_internal(char *context, uid_t uid, gid_t gid, uid_t r_uid,
			     void *data, int dlen, char *extra);
extern int verify_internal(auth_cred_t *cred, uid_t decoder_uid);
extern jwt_t *decode_jwt(char *token, bool verify, uid_t decoder_uid);

extern auth_cred_t *create_external(uid_t r_uid, void *data, int dlen);
extern int verify_external(auth_cred_t *cred);

extern void init_sack_conmgr(void);

extern auth_cred_t *new_cred(void);
extern void destroy_cred(auth_cred_t *cred);
#define FREE_NULL_CRED(_X)		\
do {					\
	if (_X)				\
		destroy_cred(_X);	\
	_X = NULL;			\
} while (0)

extern int copy_jwt_grants_to_cred(jwt_t *jwt, auth_cred_t *cred);
extern char *get_identity_string(identity_t *id, uid_t uid, gid_t gid);
extern data_t *identity_to_data(identity_t *id);
extern identity_t *extract_identity(char *json, uid_t uid, gid_t gid);

extern char *encode_launch(slurm_cred_arg_t *cred);
extern slurm_cred_t *extract_launch(char *json);

extern char *encode_sbcast(sbcast_cred_arg_t *cred);
extern sbcast_cred_t *extract_sbcast(char *json);

extern char *encode_net_aliases(slurm_node_alias_addrs_t *aliases);
extern slurm_node_alias_addrs_t *extract_net_aliases(jwt_t *jwt);

#endif
