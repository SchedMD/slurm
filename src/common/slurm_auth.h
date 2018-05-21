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
 * system's global configuration.  A typical order of calls is:
 *
 *	void *bar = g_slurm_auth_alloc();
 *	g_slurm_auth_verify( bar );
 *	g_slurm_auth_free( bar );
 *
 */

/*
 * General error codes that plugins (or the plugin system) can
 * generate.  Plugins may produce additional codes starting with
 * SLURM_AUTH_FIRST_LOCAL_ERROR.  They are responsible for providing
 * text messages to accompany the codes.  This API resolves string
 * messages for these codes.
 */
enum {
    SLURM_AUTH_NOPLUGIN,            /* No plugin for this type.          */
    SLURM_AUTH_BADARG,              /* Bad argument to an API func.      */
    SLURM_AUTH_MEMORY,              /* Problem allocating memory.        */
    SLURM_AUTH_NOUSER,              /* User not defined on host.         */
    SLURM_AUTH_INVALID,             /* Invalid credential.               */
    SLURM_AUTH_MISMATCH,            /* Credential from another plugin.   */
    SLURM_AUTH_VERSION,             /* Credential from old plugin.       */

    SLURM_AUTH_FIRST_LOCAL_ERROR    /* Always keep me last. */
};

/* Arguments passed to plugin functions */
enum {
	ARG_HOST_LIST = 0,
	ARG_TIMEOUT,
	ARG_COUNT,
};

/*
 * Slurm authentication context opaque type.
 */
typedef struct slurm_auth_context * slurm_auth_context_t;

/*
 * Create an authentication context.
 *
 * Returns NULL on failure.
 */
slurm_auth_context_t slurm_auth_context_create( const char *auth_type );

/*
 * Destroy an authentication context.  Any jumptables returned by
 * calls to slurm_auth_get_ops() for this context will silently become
 * invalid, and calls to their functions may result in core dumps and
 * other nasty behavior.
 *
 * Returns a Slurm errno.
 */
int slurm_auth_context_destroy( slurm_auth_context_t ctxt );

/*
 * This is what the UID and GID accessors return on error.  The value
 * is currently RedHat Linux's ID for the user "nobody".
 */
#define SLURM_AUTH_NOBODY		99

/*
 * Prepare the global context.
 * auth_type IN: authentication mechanism (e.g. "auth/munge") or
 *	NULL to select based upon slurm_get_auth_type() results
 */
extern int slurm_auth_init( char *auth_type );

/*
 * Destroy global context, free memory.
 */
extern int slurm_auth_fini( void );

/*
 * Static bindings for the global authentication context.
 */
extern void *	g_slurm_auth_create(char *auth_info);
extern int	g_slurm_auth_destroy( void *cred );
extern int	g_slurm_auth_verify(void *cred, char *auth_info);
extern uid_t	g_slurm_auth_get_uid( void *cred, char *auth_info );
extern gid_t	g_slurm_auth_get_gid( void *cred, char *auth_info );
extern int	g_slurm_auth_pack( void *cred, Buf buf );

/*
 * WARNING!  The returned auth pointer WILL have pointers
 *           into "buf" so do NOT free "buf" until you are done
 *           with the auth pointer.
 */
extern void	*g_slurm_auth_unpack( Buf buf );

int	g_slurm_auth_print( void *cred, FILE *fp );
int	g_slurm_auth_errno( void *cred );
const char *g_slurm_auth_errstr( int slurm_errno );

#endif /*__SLURM_AUTHENTICATION_H__*/
