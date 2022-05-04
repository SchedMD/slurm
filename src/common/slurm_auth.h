/*****************************************************************************\
 *  slurm_auth.h - implementation-independent authentication API definitions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#ifndef __SLURM_AUTHENTICATION_H__
#define __SLURM_AUTHENTICATION_H__

#include <inttypes.h>
#include <stdio.h>

#include "src/common/plugrack.h"
#include "src/common/pack.h"

/*
 * This API operates on a global authentication
 * context, one per application.  The API thunks with the "g_" prefix
 * operate on that global instance.  It is initialized implicitly if
 * necessary when any API thunk is called, or explicitly with
 *
 *	slurm_auth_init();
 *
 * The authentication type and other parameters are taken from the
 * system's global configuration.
 */

/*
 * This is what the UID and GID accessors return on error.
 * The value is currently RedHat Linux's ID for the user "nobody".
 */
#define SLURM_AUTH_NOBODY 99

/*
 * This should be equal to MUNGE_UID_ANY
 * do not restrict decode via uid
 */
#define SLURM_AUTH_UID_ANY -1

/*
 * Default auth_index value, corresponds to the primary AuthType used.
 */
#define AUTH_DEFAULT_INDEX 0

/*
 * Prepare the global context.
 * auth_type IN: authentication mechanism (e.g. "auth/munge") or
 *	NULL to use slurm_conf.auth_type
 */
extern int slurm_auth_init(char *auth_type);

/*
 * Destroy global context, free memory.
 */
extern int slurm_auth_fini(void);

/*
 * Retrieve the auth_index corresponding to the authentication
 * plugin used to create a given credential.
 */
extern int slurm_auth_index(void *cred);

/*
 * Check if plugin type corresponding to the authentication
 * plugin index supports hash.
 */
extern bool slurm_get_plugin_hash_enable(int index);

/*
 * Static bindings for the global authentication context.
 */
extern void *g_slurm_auth_create(int index, char *auth_info, uid_t r_uid,
				 void *data, int dlen);
extern int g_slurm_auth_destroy(void *cred);
extern int g_slurm_auth_verify(void *cred, char *auth_info);
extern uid_t g_slurm_auth_get_uid(void *cred);
extern gid_t g_slurm_auth_get_gid(void *cred);
extern char *g_slurm_auth_get_host(void *cred);
extern int auth_g_get_data(void *cred, char **data, uint32_t *len);
extern int g_slurm_auth_pack(void *cred, Buf buf, uint16_t protocol_version);
extern void *g_slurm_auth_unpack(Buf buf, uint16_t protocol_version);

extern char *g_slurm_auth_token_generate(int plugin_id, const char *username,
					 int lifespan);

/*
 * Set local thread security context
 * IN token - security token - may be general token, or per user token, or NULL
 * IN username - username to run as (only available for SlurmUser/root),
 *		 or NULL
 */
extern int g_slurm_auth_thread_config(const char *token, const char *username);
/*
 * clear local thread security context
 */
extern void g_slurm_auth_thread_clear(void);

#endif /*__SLURM_AUTHENTICATION_H__*/
